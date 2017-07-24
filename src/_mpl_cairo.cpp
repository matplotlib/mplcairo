#include "_mpl_cairo.h"

#include "_util.h"
#include "_pattern_cache.h"

#if CAIRO_HAS_PDF_SURFACE
#include <cairo/cairo-pdf.h>
#define MPLCAIRO_HAS_PDF
#else
#undef MPLCAIRO_HAS_PDF
#endif
#if CAIRO_HAS_PS_SURFACE
#include <cairo/cairo-ps.h>
#define MPLCAIRO_HAS_PS
#else
#undef MPLCAIRO_HAS_PS
#endif
#if CAIRO_HAS_SVG_SURFACE
#include <cairo/cairo-svg.h>
#define MPLCAIRO_HAS_SVG
#else
#undef MPLCAIRO_HAS_SVG
#endif
#if CAIRO_HAS_XLIB_SURFACE && __has_include(<X11/Xlib.h>)
#include <cairo/cairo-xlib.h>
#define MPLCAIRO_HAS_X11
#else
#undef MPLCAIRO_HAS_X11
#endif

#include <stack>

namespace mpl_cairo {

using namespace pybind11::literals;

namespace {
cairo_user_data_key_t const
  STATE_KEY{0}, FILE_KEY{0}, MATHTEXT_TO_BASELINE_KEY{0};
// NOTE: The current dpi setting is needed by MathtextBackend to set the
// correct font size (specifically, to convert points to pixels; apparently,
// cairo does not retrieve the face size from the FT_Face object as freetype
// does not provide a way to read it).  So, we update this global variable
// every time before mathtext parsing.
double CURRENT_DPI{72};
}

Region::Region(cairo_rectangle_int_t bbox, std::shared_ptr<uint8_t[]> buf) :
  bbox{bbox}, buf{buf} {}

GraphicsContextRenderer::AdditionalContext::AdditionalContext(
    GraphicsContextRenderer* gcr) :
  gcr_{gcr} {
  auto cr = gcr_->cr_;
  cairo_save(cr);
  // Force alpha, if needed.  Cannot be done earlier as we need to be able to
  // unforce it (by setting alpha to None).
  auto [r, g, b, a] = gcr_->get_rgba();
  cairo_set_source_rgba(cr, r, g, b, a);
  // Clip, if needed.  Cannot be done earlier as we need to be able to unclip.
  auto& state = gcr_->get_additional_state();
  if (auto rectangle = state.clip_rectangle; rectangle) {
    auto [x, y, w, h] = *rectangle;
    cairo_save(cr);
    cairo_identity_matrix(cr);
    cairo_new_path(cr);
    cairo_rectangle(cr, x, gcr->height_ - h - y, w, h);
    cairo_restore(cr);
    cairo_clip(cr);
  }
  if (auto clip_path = state.clip_path; clip_path) {
    cairo_new_path(cr);
    cairo_append_path(cr, clip_path.get());
    cairo_clip(cr);
  }
}

GraphicsContextRenderer::AdditionalContext::~AdditionalContext() {
  cairo_restore(gcr_->cr_);
}

double GraphicsContextRenderer::pixels_to_points(double pixels) {
  return pixels / (dpi_ / 72);
}

rgba_t GraphicsContextRenderer::get_rgba() {
  double r, g, b, a;
  auto status = cairo_pattern_get_rgba(cairo_get_source(cr_), &r, &g, &b, &a);
  if (status != CAIRO_STATUS_SUCCESS) {
    throw std::runtime_error(
        "Could not retrieve color from pattern: "
        + std::string{cairo_status_to_string(status)});
  }
  if (auto alpha = get_additional_state().alpha; alpha) {
    a = *alpha;
  }
  return {r, g, b, a};
}

GraphicsContextRenderer::AdditionalContext
GraphicsContextRenderer::additional_context() {
  return {this};
}

GraphicsContextRenderer::GraphicsContextRenderer(
    cairo_t* cr, int width, int height, double dpi) :
  // This does *not* incref the cairo_t, but the destructor *will* decref it.
  cr_{cr},
  width_{width},
  height_{height},
  dpi_{dpi},
  mathtext_parser_{
      py::module::import("matplotlib.mathtext").attr("MathTextParser")("cairo")},
  texmanager_{py::none()},
  text2path_{py::module::import("matplotlib.textpath").attr("TextToPath")()} {
  set_ctx_defaults(cr);
  auto stack = new std::stack<AdditionalState>{{AdditionalState{}}};
  stack->top().clip_path = {nullptr, &cairo_path_destroy};
  stack->top().hatch = {};
  stack->top().hatch_color = to_rgba(rc_param("hatch.color"));
  stack->top().hatch_linewidth = rc_param("hatch.linewidth").cast<double>();
  stack->top().sketch = py::none();
  cairo_set_user_data(cr, &STATE_KEY, stack, operator delete);
}

GraphicsContextRenderer::~GraphicsContextRenderer() {
  cairo_destroy(cr_);
}

cairo_t* GraphicsContextRenderer::cr_from_image_args(
    double width, double height) {
  auto surface =
    cairo_image_surface_create(CAIRO_FORMAT_ARGB32, width, height);
  auto cr = cairo_create(surface);
  cairo_surface_destroy(surface);
  return cr;
}

GraphicsContextRenderer::GraphicsContextRenderer(
    double width, double height, double dpi) :
  GraphicsContextRenderer{
    cr_from_image_args(int(width), int(height)), int(width), int(height), dpi}
  {}

cairo_t* GraphicsContextRenderer::cr_from_pycairo_ctx(py::object ctx) {
  if ((ctx.get_type().attr("__module__").cast<std::string>() != "cairo")
      || (ctx.get_type().attr("__name__").cast<std::string>() != "Context")) {
    throw std::invalid_argument("Argument is not a cairo.Context");
  }
  typedef struct {  // Copy-pasted from pycairo.h.
    PyObject_HEAD
    cairo_t *ctx;
    PyObject *base; /* base object used to create context, or NULL */
  } PycairoContext;
  auto ptr = reinterpret_cast<PycairoContext*>(ctx.ptr());
  if (auto status = cairo_status(ptr->ctx); status != CAIRO_STATUS_SUCCESS) {
    throw std::invalid_argument(
        "Context is in an error state:"
        + std::string{cairo_status_to_string(status)});
  }
  auto cr = ptr->ctx;
  cairo_reference(cr);
  return cr;
}

GraphicsContextRenderer::GraphicsContextRenderer(py::object ctx, double dpi) :
  GraphicsContextRenderer{
    cr_from_pycairo_ctx(ctx),
    ctx.attr("get_target")().attr("get_width")().cast<int>(),
    ctx.attr("get_target")().attr("get_height")().cast<int>(),
    dpi} {}

cairo_t* GraphicsContextRenderer::cr_from_fileformat_args(
    cairo_surface_type_t type, py::object file,
    double width, double height, double dpi) {
  cairo_surface_t* surface;
  auto cb = [](void* closure, const unsigned char* data, unsigned int length) {
    auto write =
      py::reinterpret_borrow<py::object>(reinterpret_cast<PyObject*>(closure));
    // NOTE: Work around lack of const buffers in pybind11.
    auto buf_info = py::buffer_info{
      const_cast<unsigned char*>(data),
        sizeof(char), py::format_descriptor<char>::format(),
        1, {length}, {sizeof(char)}};
    return
      write(py::memoryview{buf_info}).cast<unsigned int>()
      == length
      ? CAIRO_STATUS_SUCCESS : CAIRO_STATUS_WRITE_ERROR;
  };
  auto write = file.attr("write").cast<py::handle>().inc_ref().ptr();
  auto dec_ref = [](void* write) {
    py::handle{reinterpret_cast<PyObject*>(write)}.dec_ref();
  };
  switch (type) {
#ifdef MPLCAIRO_HAS_PDF
    case CAIRO_SURFACE_TYPE_PDF:
      surface = cairo_pdf_surface_create_for_stream(cb, write, width, height);
      break;
#endif
#ifdef MPLCAIRO_HAS_PS
    case CAIRO_SURFACE_TYPE_PS:
      surface = cairo_ps_surface_create_for_stream(cb, write, width, height);
      break;
#endif
#ifdef MPLCAIRO_HAS_SVG
    case CAIRO_SURFACE_TYPE_SVG:
      surface = cairo_svg_surface_create_for_stream(cb, write, width, height);
      break;
#endif
    default:
      dec_ref(write);
      throw std::runtime_error("Unsupported surface type");
  }
  cairo_surface_set_fallback_resolution(surface, dpi, dpi);
  auto cr = cairo_create(surface);
  cairo_surface_destroy(surface);
  cairo_set_user_data(cr, &FILE_KEY, write, dec_ref);
  return cr;
}

GraphicsContextRenderer::GraphicsContextRenderer(
    cairo_surface_type_t type, py::object file,
    double width, double height, double dpi) :
  GraphicsContextRenderer{
      cr_from_fileformat_args(type, file, width, height, dpi),
      int(width), int(height), 72} {}

void GraphicsContextRenderer::_finish() {
  cairo_surface_finish(cairo_get_target(cr_));
}

void GraphicsContextRenderer::_set_eps(bool eps) {
  auto surface = cairo_get_target(cr_);
#ifdef MPLCAIRO_HAS_PS
  if (cairo_surface_get_type(surface) != CAIRO_SURFACE_TYPE_PS) {
    throw std::runtime_error("Only PS surfaces are supported");
  }
  cairo_ps_surface_set_eps(surface, eps);
#else
  throw std::runtime_error("cairo was built without PS support");
#endif
}

py::array_t<uint8_t> GraphicsContextRenderer::_get_buffer() {
  auto surface = cairo_get_target(cr_);
  if (cairo_surface_get_type(surface) != CAIRO_SURFACE_TYPE_IMAGE) {
    throw std::runtime_error("Only image surfaces are supported");
  }
  auto buf = cairo_image_surface_get_data(surface);
  auto stride = cairo_image_surface_get_stride(surface);
  cairo_surface_reference(surface);
  return
    py::array_t<uint8_t>{
      {height_, width_, 4}, {stride, 4, 1}, buf,
      py::capsule(surface, [](void* surface) {
        cairo_surface_destroy(reinterpret_cast<cairo_surface_t*>(surface));
      })};
}

void GraphicsContextRenderer::set_alpha(std::optional<double> alpha) {
  get_additional_state().alpha = alpha;
}

void GraphicsContextRenderer::set_antialiased(cairo_antialias_t aa) {
  cairo_set_antialias(cr_, aa);
}
void GraphicsContextRenderer::set_antialiased(py::object aa) {
  cairo_set_antialias(
      cr_, py::bool_(aa) ? CAIRO_ANTIALIAS_FAST : CAIRO_ANTIALIAS_NONE);
}

void GraphicsContextRenderer::set_capstyle(std::string capstyle) {
  if (capstyle == "butt") {
    cairo_set_line_cap(cr_, CAIRO_LINE_CAP_BUTT);
  } else if (capstyle == "round") {
    cairo_set_line_cap(cr_, CAIRO_LINE_CAP_ROUND);
  } else if (capstyle == "projecting") {
    cairo_set_line_cap(cr_, CAIRO_LINE_CAP_SQUARE);
  } else {
    throw std::invalid_argument("Invalid capstyle: " + capstyle);
  }
}

void GraphicsContextRenderer::set_clip_rectangle(
    std::optional<py::object> rectangle) {
  auto& clip_rectangle = get_additional_state().clip_rectangle;
  clip_rectangle =
    rectangle
    // A TransformedBbox or a tuple.
    ? py::getattr(*rectangle, "bounds", *rectangle).cast<rectangle_t>()
    : std::optional<rectangle_t>{};
}

void GraphicsContextRenderer::set_clip_path(
    std::optional<py::object> transformed_path) {
  if (transformed_path) {
    auto [path, transform] =
      transformed_path->attr("get_transformed_path_and_affine")()
      .cast<std::tuple<py::object, py::object>>();
    auto matrix = matrix_from_transform(transform, height_);
    load_path_exact(cr_, path, &matrix);
    get_additional_state().clip_path.reset(
        cairo_copy_path(cr_), &cairo_path_destroy);
  } else {
    get_additional_state().clip_path.reset();
  }
}

void GraphicsContextRenderer::set_dashes(
    std::optional<double> dash_offset,
    std::optional<py::array_t<double>> dash_list) {
  if (dash_list) {
    if (!dash_offset) {
      throw std::invalid_argument("Missing dash offset");
    }
    auto dashes_raw = dash_list->unchecked<1>();
    auto n = dashes_raw.size();
    auto buf = std::unique_ptr<double[]>(new double[n]);
    for (size_t i = 0; i < n; ++i) {
      buf[i] = points_to_pixels(dashes_raw[i]);
    }
    cairo_set_dash(cr_, buf.get(), n, points_to_pixels(*dash_offset));
  } else {
    cairo_set_dash(cr_, nullptr, 0, 0);
  }
}

void GraphicsContextRenderer::set_foreground(
    py::object fg, bool /* is_rgba */) {
  auto [r, g, b, a] = to_rgba(fg);
  if (auto alpha = get_additional_state().alpha; alpha) {
    a = *alpha;
  }
  cairo_set_source_rgba(cr_, r, g, b, a);
}

void GraphicsContextRenderer::set_hatch(std::optional<std::string> hatch) {
  get_additional_state().hatch = hatch;
}

void GraphicsContextRenderer::set_hatch_color(py::object hatch_color) {
  get_additional_state().hatch_color = to_rgba(hatch_color);
}

void GraphicsContextRenderer::set_joinstyle(std::string joinstyle) {
  if (joinstyle == "miter") {
    cairo_set_line_join(cr_, CAIRO_LINE_JOIN_MITER);
  } else if (joinstyle == "round") {
    cairo_set_line_join(cr_, CAIRO_LINE_JOIN_ROUND);
  } else if (joinstyle == "bevel") {
    cairo_set_line_join(cr_, CAIRO_LINE_JOIN_BEVEL);
  } else {
    throw std::invalid_argument("Invalid joinstyle: " + joinstyle);
  }
}

void GraphicsContextRenderer::set_linewidth(double lw) {
  cairo_set_line_width(cr_, points_to_pixels(lw));
  // NOTE: Somewhat weird setting, but that's what the Agg backend does
  // (_backend_agg.h).
  cairo_set_miter_limit(cr_, cairo_get_line_width(cr_));
}

GraphicsContextRenderer::AdditionalState&
GraphicsContextRenderer::get_additional_state() {
  return
    static_cast<std::stack<GraphicsContextRenderer::AdditionalState>*>(
        cairo_get_user_data(cr_, &STATE_KEY))->top();
}

double GraphicsContextRenderer::get_linewidth() {
  return pixels_to_points(cairo_get_line_width(cr_));
}

rgb_t GraphicsContextRenderer::get_rgb() {
  auto [r, g, b, a] = get_rgba();
  return {r, g, b};
}

GraphicsContextRenderer& GraphicsContextRenderer::new_gc() {
  cairo_save(cr_);
  auto& states =
    *static_cast<std::stack<GraphicsContextRenderer::AdditionalState>*>(
        cairo_get_user_data(cr_, &STATE_KEY));
  states.push(states.top());
  return *this;
}

void GraphicsContextRenderer::copy_properties(GraphicsContextRenderer* other) {
  // In practice the following holds.  Anything else requires figuring out what
  // to do with the properties stack.
  if (this != other) {
    throw std::invalid_argument("Independent contexts cannot be copied");
  }
}

void GraphicsContextRenderer::restore() {
  auto& states =
    *static_cast<std::stack<GraphicsContextRenderer::AdditionalState>*>(
        cairo_get_user_data(cr_, &STATE_KEY));
  states.pop();
  cairo_restore(cr_);
}

double GraphicsContextRenderer::points_to_pixels(double points) {
  return points * dpi_ / 72;
}

void GraphicsContextRenderer::draw_gouraud_triangles(
    GraphicsContextRenderer& gc,
    py::array_t<double> triangles,
    py::array_t<double> colors,
    py::object transform) {
  if (&gc != this) {
    throw std::invalid_argument("Non-matching GraphicsContext");
  }
  auto ac = additional_context();
  auto matrix = matrix_from_transform(transform, height_);
  auto tri_raw = triangles.unchecked<3>();
  auto col_raw = colors.unchecked<3>();
  auto n = tri_raw.shape(0);
  if ((n != col_raw.shape(0))
      || (tri_raw.shape(1) != 3)
      || (tri_raw.shape(2) != 2)
      || (col_raw.shape(1) != 3)
      || (col_raw.shape(2) != 4)) {
    throw std::invalid_argument("Non-matching shapes");
  }
  auto pattern = cairo_pattern_create_mesh();
  for (size_t i = 0; i < n; ++i) {
    cairo_mesh_pattern_begin_patch(pattern);
    for (size_t j = 0; j < 3; ++j) {
      cairo_mesh_pattern_line_to(pattern, tri_raw(i, j, 0), tri_raw(i, j, 1));
      cairo_mesh_pattern_set_corner_color_rgba(
          pattern, j,
          col_raw(i, j, 0), col_raw(i, j, 1),
          col_raw(i, j, 2), col_raw(i, j, 3));
    }
    cairo_mesh_pattern_end_patch(pattern);
  }
  cairo_matrix_invert(&matrix);
  cairo_pattern_set_matrix(pattern, &matrix);
  cairo_set_source(cr_, pattern);
  cairo_paint(cr_);
  cairo_pattern_destroy(pattern);
}

void GraphicsContextRenderer::draw_image(
    GraphicsContextRenderer& gc, double x, double y, py::array_t<uint8_t> im) {
  if (&gc != this) {
    throw std::invalid_argument("Non-matching GraphicsContext");
  }
  auto ac = additional_context();
  auto im_raw = im.unchecked<3>();
  auto height = im_raw.shape(0), width = im_raw.shape(1);
  if (im_raw.shape(2) != 4) {
    throw std::invalid_argument("RGBA array must have shape (m, n, 4)");
  }
  auto stride = cairo_format_stride_for_width(CAIRO_FORMAT_ARGB32, width);
  auto buf = std::unique_ptr<uint8_t[]>(new uint8_t[height * stride]);
  // The gcr's alpha has already been applied by ImageBase._make_image, we just
  // need to convert to premultiplied ARGB format.
  for (size_t i = 0; i < height; ++i) {
    auto ptr = reinterpret_cast<uint32_t*>(buf.get() + i * stride);
    for (size_t j = 0; j < width; ++j) {
      auto r = im_raw(i, j, 0), g = im_raw(i, j, 1),
           b = im_raw(i, j, 2), a = im_raw(i, j, 3);
      *(ptr++) =
        (uint8_t(a) << 24) | (uint8_t(a / 255. * r) << 16)
        | (uint8_t(a / 255. * g) << 8) | (uint8_t(a / 255. * b));
    }
  }
  auto surface = cairo_image_surface_create_for_data(
      buf.get(), CAIRO_FORMAT_ARGB32, width, height, stride);
  auto pattern = cairo_pattern_create_for_surface(surface);
  cairo_surface_destroy(surface);
  auto matrix = cairo_matrix_t{1, 0, 0, -1, -x, -y + height_};
  cairo_pattern_set_matrix(pattern, &matrix);
  cairo_set_source(cr_, pattern);
  cairo_paint(cr_);
  cairo_pattern_destroy(pattern);
}

void GraphicsContextRenderer::draw_markers(
    GraphicsContextRenderer& gc,
    py::object marker_path,
    py::object marker_transform,
    py::object path,
    py::object transform,
    std::optional<py::object> fc) {
  if (&gc != this) {
    throw std::invalid_argument("Non-matching GraphicsContext");
  }
  auto ac = additional_context();

  // As paths store their vertices in an array, the .cast<>() will not make a
  // copy and we don't need to explicitly keep the intermediate result alive.
  auto vertices =
    path.attr("vertices").cast<py::array_t<double>>().unchecked<2>();
  // NOTE: For efficiency, we ignore codes, which is the documented behavior
  // even though not the actual one of other backends.
  auto n_vertices = vertices.shape(0);

  auto marker_matrix = matrix_from_transform(marker_transform);
  auto matrix = matrix_from_transform(transform, height_);

  auto fc_raw =
    fc ? to_rgba(*fc, get_additional_state().alpha) : std::optional<rgba_t>{};
  auto ec_raw = get_rgba();

  auto draw_one_marker = [&](cairo_t* cr, double x, double y) {
    auto m = cairo_matrix_t{
        marker_matrix.xx, marker_matrix.yx, marker_matrix.xy, marker_matrix.yy,
        marker_matrix.x0 + x, marker_matrix.y0 + y};
    fill_and_stroke_exact(cr, marker_path, &m, fc_raw, ec_raw);
  };

  double simplify_threshold =
    rc_param("path.simplify_threshold").cast<double>();
  std::unique_ptr<cairo_pattern_t*[]> patterns;
  size_t n_subpix = 0;
  if (simplify_threshold >= 1. / 16) {  // NOTE: Arbitrary limit.
    n_subpix = std::ceil(1 / simplify_threshold);
    if (n_subpix * n_subpix < n_vertices) {
      patterns.reset(new cairo_pattern_t*[n_subpix * n_subpix]);
    }
  }

  if (patterns) {
    // Get the extent of the marker.  Importantly, cairo_*_extents() ignores
    // surface dimensions and clipping.
    // NOTE: Currently Matplotlib chooses *not* to call draw_markers() if the
    // marker is bigger than the canvas, but this is really a limitation on
    // Agg's side.
    load_path_exact(cr_, marker_path, &marker_matrix);
    double x0, y0, x1, y1;
    cairo_stroke_extents(cr_, &x0, &y0, &x1, &y1);
    if (fc) {
      double x1f, y1f, x2f, y2f;
      cairo_fill_extents(cr_, &x1f, &y1f, &x2f, &y2f);
      x0 = std::min(x0, x1f);
      y0 = std::max(y0, y1f);
      x1 = std::min(x1, x2f);
      y1 = std::max(y1, y2f);
    }

    // Fill the pattern cache.
    auto raster_surface = cairo_surface_create_similar_image(
        cairo_get_target(cr_), CAIRO_FORMAT_ARGB32,
        std::ceil(x1 - x0 + 1), std::ceil(y1 - y0 + 1));
    auto raster_cr = cairo_create(raster_surface);
    cairo_surface_destroy(raster_surface);
    copy_for_marker_stamping(cr_, raster_cr);
    for (size_t i = 0; i < n_subpix; ++i) {
      for (size_t j = 0; j < n_subpix; ++j) {
        cairo_push_group(raster_cr);
        draw_one_marker(
            raster_cr, -x0 + double(i) / n_subpix, -y0 + double(j) / n_subpix);
        auto pattern = patterns[i * n_subpix + j] = cairo_pop_group(raster_cr);
        cairo_pattern_set_filter(pattern, CAIRO_FILTER_NEAREST);
      }
    }
    cairo_destroy(raster_cr);

    for (size_t i = 0; i < n_vertices; ++i) {
      auto x = vertices(i, 0), y = vertices(i, 1);
      cairo_matrix_transform_point(&matrix, &x, &y);
      auto target_x = x + x0,
           target_y = y + y0;
      auto i_target_x = std::floor(target_x),
           i_target_y = std::floor(target_y);
      auto f_target_x = target_x - i_target_x,
           f_target_y = target_y - i_target_y;
      auto idx =
        int(n_subpix * f_target_x) * n_subpix + int(n_subpix * f_target_y);
      auto pattern = patterns[idx];
      // Offsetting by height_ is already taken care of by matrix.
      auto pattern_matrix =
        cairo_matrix_t{1, 0, 0, 1, -i_target_x, -i_target_y};
      cairo_pattern_set_matrix(pattern, &pattern_matrix);
      cairo_set_source(cr_, pattern);
      cairo_paint(cr_);
    }

    // Cleanup.
    for (size_t i = 0; i < n_subpix * n_subpix; ++i) {
      cairo_pattern_destroy(patterns[i]);
    }

  } else {
    for (size_t i = 0; i < n_vertices; ++i) {
      cairo_save(cr_);
      auto x = vertices(i, 0), y = vertices(i, 1);
      cairo_matrix_transform_point(&matrix, &x, &y);
      if (!(std::isfinite(x) && std::isfinite(y))) {
        continue;
      }
      draw_one_marker(cr_, x, y);
      cairo_restore(cr_);
    }
  }
}

void GraphicsContextRenderer::draw_path(
    GraphicsContextRenderer& gc,
    py::object path,
    py::object transform,
    std::optional<py::object> fc) {
  if (&gc != this) {
    throw std::invalid_argument("Non-matching GraphicsContext");
  }
  auto ac = additional_context();
  if (auto sketch = get_additional_state().sketch; !sketch.is_none()) {
    path = path.attr("cleaned")(
        "transform"_a=transform,
        "curves"_a=true,
        "sketch"_a=get_additional_state().sketch);
    auto matrix = cairo_matrix_t{1, 0, 0, -1, 0, double(height_)};
    load_path_exact(cr_, path, &matrix);
  } else {
    auto matrix = matrix_from_transform(transform, height_);
    load_path_exact(cr_, path, &matrix);
  }
  if (fc) {
    cairo_save(cr_);
    auto [r, g, b, a] = to_rgba(*fc, get_additional_state().alpha);
    cairo_set_source_rgba(cr_, r, g, b, a);
    cairo_fill_preserve(cr_);
    cairo_restore(cr_);
  }
  py::object hatch_path = py::cast(this).attr("get_hatch_path")();
  if (!hatch_path.is_none()) {
    cairo_save(cr_);
    auto dpi = int(dpi_);  // Truncating is good enough.
    auto hatch_surface = cairo_image_surface_create(
        CAIRO_FORMAT_ARGB32, dpi, dpi);
    auto hatch_cr = cairo_create(hatch_surface);
    cairo_surface_destroy(hatch_surface);
    set_ctx_defaults(hatch_cr);
    auto hatch_color = get_additional_state().hatch_color;
    cairo_set_line_width(
        hatch_cr, points_to_pixels(get_additional_state().hatch_linewidth));
    // cf. set_linewidth.
    cairo_set_miter_limit(hatch_cr, cairo_get_line_width(hatch_cr));
    auto matrix = cairo_matrix_t{
        double(dpi), 0, 0, -double(dpi), 0, double(dpi)};
    fill_and_stroke_exact(
        hatch_cr, hatch_path, &matrix, hatch_color, hatch_color);
    auto hatch_pattern = cairo_pattern_create_for_surface(hatch_surface);
    cairo_pattern_set_extend(hatch_pattern, CAIRO_EXTEND_REPEAT);
    cairo_set_source(cr_, hatch_pattern);
    cairo_clip_preserve(cr_);
    cairo_paint(cr_);
    cairo_pattern_destroy(hatch_pattern);
    cairo_destroy(hatch_cr);
    cairo_restore(cr_);
  }
  cairo_stroke(cr_);
}

void GraphicsContextRenderer::draw_path_collection(
    GraphicsContextRenderer& gc,
    py::object master_transform,
    std::vector<py::object> paths,
    std::vector<py::object> transforms,
    py::array_t<double> offsets,
    py::object offset_transform,
    py::object fcs,
    py::object ecs,
    py::array_t<double> lws,
    std::vector<std::tuple<std::optional<double>,
                           std::optional<py::array_t<double>>>> dashes,
    py::object aas,
    py::object urls,
    std::string offset_position) {
  // TODO: Persistent cache; cache eviction policy.
  if (&gc != this) {
    throw std::invalid_argument("Non-matching GraphicsContext");
  }
  auto ac = additional_context();
  // Fall back onto the slow implementation in the following, non-supported
  // cases:
  //   - Hatching is used: the stamp cache cannot be used anymore, as the hatch
  //     positions would be different on every stamp.  (NOTE: Actually it
  //     may be possible to use the hatch as the source and mask it with the
  //     pattern.)
  //   - NOTE: offset_position is set to "data".  This feature is only used by
  //     hexbin(), so it should really just be deprecated; hexbin() should
  //     provide its own Container class which correctly adjusts the transforms
  //     at draw time (or just be drawn as a quadmesh, see draw_quad_mesh).
  if ((py::bool_(py::cast(this).attr("get_hatch")()))
      || (offset_position == "data")) {
    py::module::import("matplotlib.backend_bases")
      .attr("RendererBase").attr("draw_path_collection")(
          this, gc, master_transform,
          paths, transforms, offsets, offset_transform,
          fcs, ecs, lws, dashes, aas, urls, offset_position);
    return;
  }
  auto n_paths = paths.size(),
       n_transforms = transforms.size(),
       n_offsets = offsets.shape(0),
       n = std::max({n_paths, n_transforms, n_offsets});
  if (!n_paths || !n_offsets) {
    return;
  }
  auto master_matrix = matrix_from_transform(master_transform, height_);
  auto matrices = std::unique_ptr<cairo_matrix_t[]>(
      new cairo_matrix_t[n_transforms ? n_transforms : 1]);
  if (n_transforms) {
    for (size_t i = 0; i < n_transforms; ++i) {
      matrices[i] = matrix_from_transform(transforms[i], &master_matrix);
    }
  } else {
    n_transforms = 1;
    matrices[0] = master_matrix;
  }
  auto offsets_raw = offsets.unchecked<2>();
  if (offsets_raw.shape(1) != 2) {
    throw std::invalid_argument("Invalid offsets shape");
  }
  auto offset_matrix = matrix_from_transform(offset_transform);
  auto convert_colors = [&](py::object colors) {
    auto alpha = get_additional_state().alpha;
    return
      py::module::import("matplotlib.colors").attr("to_rgba_array")(
          colors, alpha ? py::cast(*alpha) : py::none())
      .cast<py::array_t<double>>();
  };
  // Don't drop the arrays until the function exits.  NOTE: Perhaps pybind11
  // should ensure that?
  auto fcs_raw_keepref = convert_colors(fcs),
       ecs_raw_keepref = convert_colors(ecs);
  auto fcs_raw = fcs_raw_keepref.unchecked<2>(),
       ecs_raw = ecs_raw_keepref.unchecked<2>();
  auto lws_raw = lws.unchecked<1>();
  auto n_dashes = dashes.size();
  auto dashes_raw = std::unique_ptr<dash_t[]>(
      new dash_t[n_dashes ? n_dashes : 1]);
  if (n_dashes) {
    for (size_t i = 0; i < n_dashes; ++i) {
      auto [dash_offset, dash_list] = dashes[i];
      set_dashes(dash_offset, dash_list);  // Invoke the dash converter.
      dashes_raw[i] = convert_dash(cr_);
    }
  } else {
    n_dashes = 1;
    dashes_raw[0] = {};
  }
  auto cache = PatternCache{
      rc_param("path.simplify_threshold").cast<double>()};
  for (size_t i = 0; i < n; ++i) {
    auto path = paths[i % n_paths];
    auto matrix = matrices[i % n_transforms];
    auto x = offsets_raw(i % n_offsets, 0),
         y = offsets_raw(i % n_offsets, 1);
    cairo_matrix_transform_point(&offset_matrix, &x, &y);
    if (!(std::isfinite(x) && std::isfinite(y))) {
      continue;
    }
    if (fcs_raw.shape(0)) {
      auto i_mod = i % fcs_raw.shape(0);
      auto r = fcs_raw(i_mod, 0), g = fcs_raw(i_mod, 1),
           b = fcs_raw(i_mod, 2), a = fcs_raw(i_mod, 3);
      cairo_set_source_rgba(cr_, r, g, b, a);
      cache.mask(cr_, path, matrix, draw_func_t::Fill, 0, {}, x, y);
    }
    if (ecs_raw.size()) {
      auto i_mod = i % ecs_raw.shape(0);
      auto r = ecs_raw(i_mod, 0), g = ecs_raw(i_mod, 1),
           b = ecs_raw(i_mod, 2), a = ecs_raw(i_mod, 3);
      cairo_set_source_rgba(cr_, r, g, b, a);
      auto lw = lws_raw.size()
        ? points_to_pixels(lws_raw[i % lws_raw.size()])
        : cairo_get_line_width(cr_);
      auto dash = dashes_raw[i % n_dashes];
      cache.mask(cr_, path, matrix, draw_func_t::Stroke, lw, dash, x, y);
    }
    // NOTE: We drop antialiaseds because that just seems silly.
    // We drop urls as they should be handled in a post-processing step anyways
    // (cairo doesn't seem to support them?).
  }
}

// While draw_quad_mesh is technically optional, the fallback is to use
// draw_path_collections, which creates artefacts at the junctions due to
// stamping.
// NOTE: The spec for this method is overly general; it is only used by the
// QuadMesh class, which does not provide a way to set its offsets (or per-quad
// antialiasing), so we just drop them.  The mesh_{width,height} arguments are
// also redundant with the coordinates shape.
void GraphicsContextRenderer::draw_quad_mesh(
    GraphicsContextRenderer& gc,
    py::object master_transform,
    size_t mesh_width, size_t mesh_height,
    py::array_t<double> coordinates,
    py::array_t<double> offsets,
    py::object offset_transform,
    py::array_t<double> fcs,
    py::object aas,
    py::array_t<double> ecs) {
  if (&gc != this) {
    throw std::invalid_argument("Non-matching GraphicsContext");
  }
  auto ac = additional_context();
  auto matrix = matrix_from_transform(master_transform, height_);
  auto fcs_raw = fcs.unchecked<2>(), ecs_raw = ecs.unchecked<2>();
  if ((coordinates.shape(0) != mesh_height + 1)
      || (coordinates.shape(1) != mesh_width + 1)
      || (coordinates.shape(2) != 2)
      || (fcs_raw.shape(0) != mesh_height * mesh_width)
      || (fcs_raw.shape(1) != 4)
      || (ecs_raw.shape(1) != 4)) {
    throw std::invalid_argument("Non-matching shapes");
  }
  if ((offsets.ndim() != 2)
      || (offsets.shape(0) != 1) || (offsets.shape(1) != 2)
      || (*offsets.data(0, 0) != 0) || (*offsets.data(0, 1) != 0)) {
    throw std::invalid_argument("Non-trivial offsets not supported");
  }
  auto coords_raw_keepref =  // We may as well let numpy manage the buffer.
    coordinates.attr("copy")().cast<py::array_t<double>>();
  auto coords_raw = coords_raw_keepref.mutable_unchecked<3>();
  for (size_t i = 0; i < mesh_height + 1; ++i) {
    for (size_t j = 0; j < mesh_width + 1; ++j) {
      cairo_matrix_transform_point(
          &matrix,
          coords_raw.mutable_data(i, j, 0),
          coords_raw.mutable_data(i, j, 1));
    }
  }
  // If edge colors are set, we need to draw the quads one at a time in order
  // to be able to draw the edges as well.  If they are not set, using cairo's
  // mesh pattern support instead avoids conflation artifacts.  (NOTE: In fact,
  // it may make sense to rewrite hexbin in terms of quadmeshes in order to fix
  // their long-standing issues with such artifacts.)
  if (ecs_raw.shape(0)) {
    for (size_t i = 0; i < mesh_height; ++i) {
      for (size_t j = 0; j < mesh_width; ++j) {
        cairo_move_to(
            cr_, coords_raw(i, j, 0), coords_raw(i, j, 1));
        cairo_line_to(
            cr_, coords_raw(i, j + 1, 0), coords_raw(i, j + 1, 1));
        cairo_line_to(
            cr_, coords_raw(i + 1, j + 1, 0), coords_raw(i + 1, j + 1, 1));
        cairo_line_to(
            cr_, coords_raw(i + 1, j, 0), coords_raw(i + 1, j, 1));
        cairo_close_path(cr_);
        auto n = i * mesh_width + j;
        auto r = fcs_raw(n, 0), g = fcs_raw(n, 1),
             b = fcs_raw(n, 2), a = fcs_raw(n, 3);
        cairo_set_source_rgba(cr_, r, g, b, a);
        cairo_fill_preserve(cr_);
        n %= ecs_raw.shape(0);
        r = ecs_raw(n, 0); g = ecs_raw(n, 1);
        b = ecs_raw(n, 2); a = ecs_raw(n, 3);
        cairo_set_source_rgba(cr_, r, g, b, a);
        cairo_stroke(cr_);
      }
    }
  } else {
    auto pattern = cairo_pattern_create_mesh();
    for (size_t i = 0; i < mesh_height; ++i) {
      for (size_t j = 0; j < mesh_width; ++j) {
        cairo_mesh_pattern_begin_patch(pattern);
        cairo_mesh_pattern_move_to(
            pattern, coords_raw(i, j, 0), coords_raw(i, j, 1));
        cairo_mesh_pattern_line_to(
            pattern, coords_raw(i, j + 1, 0), coords_raw(i, j + 1, 1));
        cairo_mesh_pattern_line_to(
            pattern, coords_raw(i + 1, j + 1, 0), coords_raw(i + 1, j + 1, 1));
        cairo_mesh_pattern_line_to(
            pattern, coords_raw(i + 1, j, 0), coords_raw(i + 1, j, 1));
        auto n = i * mesh_width + j;
        auto r = fcs_raw(n, 0), g = fcs_raw(n, 1),
             b = fcs_raw(n, 2), a = fcs_raw(n, 3);
        for (size_t k = 0; k < 4; ++k) {
          cairo_mesh_pattern_set_corner_color_rgba(pattern, k, r, g, b, a);
        }
        cairo_mesh_pattern_end_patch(pattern);
      }
    }
    cairo_set_source(cr_, pattern);
    cairo_paint(cr_);
    cairo_pattern_destroy(pattern);
  }
}

void GraphicsContextRenderer::draw_text(
    GraphicsContextRenderer& gc,
    double x, double y, std::string s, py::object prop, double angle,
    bool ismath, py::object /* mtext */) {
  if (&gc != this) {
    throw std::invalid_argument("Non-matching GraphicsContext");
  }
  auto ac = additional_context();
  if (ismath) {
    cairo_translate(cr_, x, y);
    cairo_rotate(cr_, -angle * M_PI / 180);
    CURRENT_DPI = dpi_;
    auto capsule =  // Keep a reference to it.
      mathtext_parser_.attr("parse")(s, dpi_, prop).cast<py::capsule>();
    auto record = static_cast<cairo_surface_t*>(capsule);
    capsule.release();  // Don't decref it.
    double depth = *static_cast<double*>(
        cairo_surface_get_user_data(record, &MATHTEXT_TO_BASELINE_KEY));
    cairo_set_source_surface(cr_, record, 0, -depth);
    cairo_paint(cr_);
  } else {
    // Need to set the current point (otherwise later texts will just follow,
    // regardless of cairo_translate).
    cairo_translate(cr_, x, y);
    cairo_rotate(cr_, -angle * M_PI / 180);
    cairo_move_to(cr_, 0, 0);
    auto [ft_face, font_face] = ft_face_and_font_face_from_prop(prop);
    cairo_set_font_face(cr_, font_face);
    cairo_set_font_size(
        cr_,
        points_to_pixels(prop.attr("get_size_in_points")().cast<double>()));
    cairo_show_text(cr_, s.c_str());
    cairo_font_face_destroy(font_face);
  }
}

std::tuple<double, double, double>
GraphicsContextRenderer::get_text_width_height_descent(
    std::string s, py::object prop, py::object ismath) {
  // NOTE: "height" includes "descent", and "descent" is (normally) positive
  // (see MathtextBackendAgg.get_results()).
  // NOTE: ismath can be True, False, "TeX" (i.e., usetex).
  // NOTE: RendererAgg relies on the text.usetex rcParam, whereas RendererBase
  // relies (correctly?) on the value of ismath.
  if (py::module::import("operator").attr("eq")(ismath, "TeX").cast<bool>()) {
    return
      py::module::import("matplotlib.backend_bases").attr("RendererBase")
      .attr("get_text_width_height_descent")(this, s, prop, ismath)
      .cast<std::tuple<double, double, double>>();
  }
  if (ismath.cast<bool>()) {
    // NOTE: Agg reports nonzero descents for seemingly zero-descent cases.
    CURRENT_DPI = dpi_;
    auto capsule =  // Keep a reference to the capsule.
      mathtext_parser_.attr("parse")(s, dpi_, prop).cast<py::capsule>();
    auto record = static_cast<cairo_surface_t*>(capsule);
    capsule.release();  // ... and don't decref it.
    double to_baseline = *static_cast<double*>(
        cairo_surface_get_user_data(record, &MATHTEXT_TO_BASELINE_KEY));
    double x0, y0, width, height;
    cairo_recording_surface_ink_extents(record, &x0, &y0, &width, &height);
    return {width, height, y0 + height - to_baseline};
  } else {
    cairo_save(cr_);
    auto [ft_face, font_face] = ft_face_and_font_face_from_prop(prop);
    cairo_set_font_face(cr_, font_face);
    cairo_set_font_size(
        cr_,
        points_to_pixels(prop.attr("get_size_in_points")().cast<double>()));
    cairo_text_extents_t extents;
    cairo_text_extents(cr_, s.c_str(), &extents);
    cairo_font_face_destroy(font_face);
    cairo_restore(cr_);
    return {extents.width, extents.height, extents.height + extents.y_bearing};
  }
}

void GraphicsContextRenderer::start_filter() {
  cairo_push_group(cr_);
  new_gc();
}

py::array_t<uint8_t> GraphicsContextRenderer::_stop_filter() {
  restore();
  auto pattern = cairo_pop_group(cr_);
  auto raster_surface =
    cairo_image_surface_create(CAIRO_FORMAT_ARGB32, width_, height_);
  auto raster_cr = cairo_create(raster_surface);
  cairo_set_source(raster_cr, pattern);
  cairo_pattern_destroy(pattern);
  cairo_paint(raster_cr);
  cairo_destroy(raster_cr);
  cairo_surface_flush(raster_surface);
  auto buf = cairo_image_surface_get_data(raster_surface);
  auto stride = cairo_image_surface_get_stride(raster_surface);
  return
    py::array_t<uint8_t>{
      {height_, width_, 4}, {stride, 4, 1}, buf,
      py::capsule(raster_surface, [](void* raster_surface) {
        cairo_surface_destroy(
            reinterpret_cast<cairo_surface_t*>(raster_surface));
      })};
}

Region GraphicsContextRenderer::copy_from_bbox(py::object bbox) {
  // Use ints to avoid a bunch of warnings below.
  int x0 = std::floor(bbox.attr("x0").cast<double>()),
      x1 = std::ceil(bbox.attr("x1").cast<double>()),
      y0 = std::floor(bbox.attr("y0").cast<double>()),
      y1 = std::ceil(bbox.attr("y1").cast<double>());
  if (!((0 <= x0) && (x0 <= x1) && (x1 <= width_)
        && (0 <= y0) && (y0 <= y1) && (y1 <= height_))) {
    throw std::invalid_argument("Invalid bbox");
  }
  auto width = x1 - x0, height = y1 - y0;
  // 4 bytes per pixel throughout!
  auto buf = std::shared_ptr<uint8_t[]>(new uint8_t[4 * width * height]);
  auto surface = cairo_get_target(cr_);
  if (cairo_surface_get_type(surface) != CAIRO_SURFACE_TYPE_IMAGE) {
    throw std::runtime_error("Only image surfaces are supported");
  }
  auto raw = cairo_image_surface_get_data(surface);
  auto stride = cairo_image_surface_get_stride(surface);
  for (int y = y0; y < y1; ++y) {
    std::memcpy(
        buf.get() + (y - y0) * 4 * width, raw + y * stride + 4 * x0,
        4 * width);
  }
  return {{x0, y0, width, height}, buf};
}

void GraphicsContextRenderer::restore_region(Region& region) {
  auto [bbox, buf] = region;
  auto [x0, y0, width, height] = bbox;
  int /* x1 = x0 + width, */ y1 = y0 + height;
  auto surface = cairo_get_target(cr_);
  if (cairo_surface_get_type(surface) != CAIRO_SURFACE_TYPE_IMAGE) {
    // NOTE: We can probably use cairo_surface_map_to_image, but I'm not sure
    // there's an use for it anyways.
    throw std::runtime_error("Only image surfaces are supported");
  }
  auto raw = cairo_image_surface_get_data(surface);
  auto stride = cairo_image_surface_get_stride(surface);
  cairo_surface_flush(surface);
  // 4 bytes per pixel!
  for (int y = y0; y < y1; ++y) {
    std::memcpy(
        raw + y * stride + 4 * x0, buf.get() + (y - y0) * 4 * width,
        4 * width);
  }
  cairo_surface_mark_dirty_rectangle(surface, x0, y0, width, height);
}

MathtextBackend::MathtextBackend() : cr_{} {}

void MathtextBackend::set_canvas_size(
    double width, double height, double depth) {
  // NOTE: "height" does *not* include "descent", and "descent" is (normally)
  // positive (see MathtextBackendAgg.set_canvas_size()).  This is a different
  // convention from get_text_width_height_descent()!
  cairo_destroy(cr_);
  auto rect = cairo_rectangle_t{0, 0, width, height + depth};
  // NOTE: It would make sense to use {0, depth, width, -(height+depth)} as
  // extents ("upper" character regions correspond to negative y's), but
  // negative extents are buggy as of cairo 1.14.  Moreover, render_glyph() and
  // render_rect_filled() use coordinates relative to the upper left corner, so
  // that doesn't help anyways.
  auto surface = cairo_recording_surface_create(CAIRO_CONTENT_ALPHA, &rect);
  cr_ = cairo_create(surface);
  cairo_surface_destroy(surface);
  cairo_surface_set_user_data(
      surface, &MATHTEXT_TO_BASELINE_KEY, new double{height}, operator delete);
}

void MathtextBackend::render_glyph(double ox, double oy, py::object info) {
  auto [ft_face, font_face] =
    ft_face_and_font_face_from_path(
        info.attr("font").attr("fname").cast<std::string>());
  cairo_set_font_face(cr_, font_face);
  cairo_set_font_size(
      cr_, info.attr("fontsize").cast<double>() * CURRENT_DPI / 72);
  auto index = FT_Get_Char_Index(
      ft_face, info.attr("num").cast<unsigned long>());
  auto glyph = cairo_glyph_t{index, ox, oy};
  cairo_show_glyphs(cr_, &glyph, 1);
  cairo_font_face_destroy(font_face);
}

void MathtextBackend::render_rect_filled(
    double x1, double y1, double x2, double y2) {
  cairo_rectangle(cr_, x1, y1, x2 - x1, y2 - y1);
  cairo_fill(cr_);
}

py::capsule MathtextBackend::get_results(
    py::object box, py::object /* used_characters */) {
  py::module::import("matplotlib.mathtext").attr("ship")(0, 0, box);
  auto surface = cairo_get_target(cr_);
  cairo_surface_reference(surface);
  cairo_destroy(cr_);
  cr_ = nullptr;
  // NOTE: pybind11 2.2 will allow setting the capsule name, for additional
  // safety.
  return py::capsule(surface, [](void* surface){
    cairo_surface_destroy(reinterpret_cast<cairo_surface_t*>(surface));
  });
}

PYBIND11_PLUGIN(_mpl_cairo) {
  py::module m("_mpl_cairo", "A cairo backend for matplotlib.");

  if (py::module::import("matplotlib.ft2font").attr("__freetype_build_type__")
      .cast<std::string>() == "local") {
    throw std::runtime_error("Local freetype builds are not supported");
  }

  UNIT_CIRCLE =
    py::module::import("matplotlib.path").attr("Path").attr("unit_circle")();

  py::enum_<cairo_antialias_t>(m, "antialias_t")
    .value("DEFAULT", CAIRO_ANTIALIAS_DEFAULT)
    .value("NONE", CAIRO_ANTIALIAS_NONE)
    .value("GRAY", CAIRO_ANTIALIAS_GRAY)
    .value("SUBPIXEL", CAIRO_ANTIALIAS_SUBPIXEL)
    .value("FAST", CAIRO_ANTIALIAS_FAST)
    .value("GOOD", CAIRO_ANTIALIAS_GOOD)
    .value("BEST", CAIRO_ANTIALIAS_BEST);
  py::enum_<cairo_format_t>(m, "format_t")
    .value("INVALID", CAIRO_FORMAT_INVALID)
    .value("ARGB32", CAIRO_FORMAT_ARGB32)
    .value("RGB24", CAIRO_FORMAT_RGB24)
    .value("A8", CAIRO_FORMAT_A8)
    .value("A1", CAIRO_FORMAT_A1)
    .value("RGB16_565", CAIRO_FORMAT_RGB16_565)
    .value("RGB30", CAIRO_FORMAT_RGB30);
  py::enum_<cairo_surface_type_t>(m, "surface_type_t")
    .value("IMAGE", CAIRO_SURFACE_TYPE_IMAGE)
    .value("PDF", CAIRO_SURFACE_TYPE_PDF)
    .value("PS", CAIRO_SURFACE_TYPE_PS)
    .value("XLIB", CAIRO_SURFACE_TYPE_XLIB)
    .value("XCB", CAIRO_SURFACE_TYPE_XCB)
    .value("GLITZ", CAIRO_SURFACE_TYPE_GLITZ)
    .value("QUARTZ", CAIRO_SURFACE_TYPE_QUARTZ)
    .value("WIN32", CAIRO_SURFACE_TYPE_WIN32)
    .value("BEOS", CAIRO_SURFACE_TYPE_BEOS)
    .value("DIRECTFB", CAIRO_SURFACE_TYPE_DIRECTFB)
    .value("SVG", CAIRO_SURFACE_TYPE_SVG)
    .value("OS2", CAIRO_SURFACE_TYPE_OS2)
    .value("WIN32_PRINTING", CAIRO_SURFACE_TYPE_WIN32_PRINTING)
    .value("QUARTZ_IMAGE", CAIRO_SURFACE_TYPE_QUARTZ_IMAGE)
    .value("SCRIPT", CAIRO_SURFACE_TYPE_SCRIPT)
    .value("QT", CAIRO_SURFACE_TYPE_QT)
    .value("RECORDING", CAIRO_SURFACE_TYPE_RECORDING)
    .value("VG", CAIRO_SURFACE_TYPE_VG)
    .value("GL", CAIRO_SURFACE_TYPE_GL)
    .value("DRM", CAIRO_SURFACE_TYPE_DRM)
    .value("TEE", CAIRO_SURFACE_TYPE_TEE)
    .value("XML", CAIRO_SURFACE_TYPE_XML)
    .value("SKIA", CAIRO_SURFACE_TYPE_SKIA)
    .value("SUBSURFACE", CAIRO_SURFACE_TYPE_SUBSURFACE)
    .value("COGL", CAIRO_SURFACE_TYPE_COGL);

  py::class_<Region>(m, "_Region")
    // NOTE: Only for patching Agg.
    .def("_get_buffer", [](Region& r) {
        return py::array_t<uint8_t>{
          {r.bbox.height, r.bbox.width, 4},
          {r.bbox.width * 4, 4, 1},
          r.buf.get()};
      });

  py::class_<GraphicsContextRenderer>(m, "GraphicsContextRendererCairo")
    // The RendererAgg signature, which is also expected by MixedModeRenderer
    // (with doubles!).
    .def(py::init<double, double, double>())
    .def(py::init<py::object, double>())
    .def(py::init<cairo_surface_type_t, py::object, double, double, double>())

    .def("_set_eps", &GraphicsContextRenderer::_set_eps)
    .def("_get_buffer", &GraphicsContextRenderer::_get_buffer)
    .def("_finish", &GraphicsContextRenderer::_finish)

    // GraphicsContext API.
    .def("set_alpha", &GraphicsContextRenderer::set_alpha)
    .def("set_antialiased",
        py::overload_cast<cairo_antialias_t>(
          &GraphicsContextRenderer::set_antialiased))
    .def("set_antialiased",
        py::overload_cast<py::object>(
          &GraphicsContextRenderer::set_antialiased))
    .def("set_capstyle", &GraphicsContextRenderer::set_capstyle)
    .def("set_clip_rectangle", &GraphicsContextRenderer::set_clip_rectangle)
    .def("set_clip_path", &GraphicsContextRenderer::set_clip_path)
    .def("set_dashes", &GraphicsContextRenderer::set_dashes)
    .def("set_foreground", &GraphicsContextRenderer::set_foreground,
        "fg"_a, "isRGBA"_a=false)
    .def("set_hatch", &GraphicsContextRenderer::set_hatch)
    .def("set_hatch_color", &GraphicsContextRenderer::set_hatch_color)
    .def("set_joinstyle", &GraphicsContextRenderer::set_joinstyle)
    .def("set_linewidth", &GraphicsContextRenderer::set_linewidth)

    .def("get_clip_rectangle", [](GraphicsContextRenderer& gcr) {
        return gcr.get_additional_state().clip_rectangle;
      })
    .def("get_clip_path", [](GraphicsContextRenderer& gcr) {
        return gcr.get_additional_state().clip_path;
      })
    .def("get_hatch", [](GraphicsContextRenderer& gcr) {
        return gcr.get_additional_state().hatch;
      })
    .def("get_hatch_color", [](GraphicsContextRenderer& gcr) {
        return gcr.get_additional_state().hatch_color;
      })
    .def("get_hatch_linewidth", [](GraphicsContextRenderer& gcr) {
        return gcr.get_additional_state().hatch_linewidth;
      })
    // Not strictly needed now.
    .def("get_linewidth", &GraphicsContextRenderer::get_linewidth)
    // Needed for patheffects.
    .def("get_rgb", &GraphicsContextRenderer::get_rgb)

    // NOTE: Slightly hackish, but works.  Avoids having to reproduce the logic
    // in set_sketch_params().
    .def_property(
        "_sketch",
        [](GraphicsContextRenderer& gcr) {
          return gcr.get_additional_state().sketch;
        },
        [](GraphicsContextRenderer& gcr, py::object sketch) {
          gcr.get_additional_state().sketch = sketch;
        })

    .def("new_gc", &GraphicsContextRenderer::new_gc)
    .def("copy_properties", &GraphicsContextRenderer::copy_properties)
    .def("restore", &GraphicsContextRenderer::restore)

    // Renderer API.
    // NOTE: Needed for RendererAgg.get_text_width_height_descent.
    .def_readonly("dpi", &GraphicsContextRenderer::dpi_)
    // NOTE: Needed for RendererAgg.get_text_width_height_descent.
    .def_readonly(
        "mathtext_parser", &GraphicsContextRenderer::mathtext_parser_)
    // NOTE: Needed for RendererAgg.get_text_width_height_descent.
    .def_readwrite(
        "_texmanager", &GraphicsContextRenderer::texmanager_)
    // NOTE: Needed for usetex and patheffects.
    .def_readonly("_text2path", &GraphicsContextRenderer::text2path_)

    .def("get_canvas_width_height", [](GraphicsContextRenderer& gcr) {
        return std::tuple{gcr.width_, gcr.height_};
      })
    // NOTE: Needed for patheffects, which should use get_canvas_width_height().
    .def_readonly("width", &GraphicsContextRenderer::width_)
    .def_readonly("height", &GraphicsContextRenderer::height_)

    .def("points_to_pixels", &GraphicsContextRenderer::points_to_pixels)

    .def("draw_gouraud_triangles",
        &GraphicsContextRenderer::draw_gouraud_triangles)
    .def("draw_image", &GraphicsContextRenderer::draw_image)
    .def("draw_markers", &GraphicsContextRenderer::draw_markers,
        "gc"_a, "marker_path"_a, "marker_trans"_a, "path"_a, "trans"_a,
        "rgbFace"_a=nullptr)
    .def("draw_path", &GraphicsContextRenderer::draw_path,
        "gc"_a, "path"_a, "transform"_a, "rgbFace"_a=nullptr)
    .def("draw_path_collection",
        &GraphicsContextRenderer::draw_path_collection)
    .def("draw_quad_mesh", &GraphicsContextRenderer::draw_quad_mesh)
    .def("draw_text", &GraphicsContextRenderer::draw_text,
        "gc"_a, "x"_a, "y"_a, "s"_a, "prop"_a, "angle"_a,
        "ismath"_a=false, "mtext"_a=nullptr)
    .def("get_text_width_height_descent",
        &GraphicsContextRenderer::get_text_width_height_descent,
        "s"_a, "prop"_a, "ismath"_a)

    .def("start_filter", &GraphicsContextRenderer::start_filter)
    .def("_stop_filter", &GraphicsContextRenderer::_stop_filter)

    // Canvas API.
    .def("copy_from_bbox", &GraphicsContextRenderer::copy_from_bbox)
    .def("restore_region", &GraphicsContextRenderer::restore_region);

  py::class_<MathtextBackend>(m, "MathtextBackendCairo")
    .def(py::init<>())
    .def("set_canvas_size", &MathtextBackend::set_canvas_size)
    .def("render_glyph", &MathtextBackend::render_glyph)
    .def("render_rect_filled", &MathtextBackend::render_rect_filled)
    .def("get_results", &MathtextBackend::get_results)
    .def("get_hinting_type", [](MathtextBackend& mb) {
        return get_hinting_flag();
      });

  return m.ptr();
}

}
