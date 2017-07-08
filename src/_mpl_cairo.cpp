#include "_mpl_cairo.h"

#include "_util.h"
#include "_pattern_cache.h"

#include <stack>

namespace mpl_cairo {

using namespace pybind11::literals;

static cairo_user_data_key_t const STATE_KEY = {0};

Region::Region(cairo_rectangle_int_t bbox, std::shared_ptr<char[]> buf) :
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
    cairo_rectangle(cr, x, gcr->get_height() - h - y, w, h);
    cairo_restore(cr);
    cairo_clip(cr);
  }
  if (auto clip_path = state.clip_path; clip_path) {
    cairo_new_path(cr);
    cairo_append_path(cr, *clip_path);
    cairo_clip(cr);
  }
}

GraphicsContextRenderer::AdditionalContext::~AdditionalContext() {
  cairo_restore(gcr_->cr_);
}

void GraphicsContextRenderer::destroy_state_stack(void* ptr) {
  auto* stack =
    static_cast<std::stack<GraphicsContextRenderer::AdditionalState>*>(ptr);
  while (stack->size()) {
    auto& state = stack->top();
    if (state.clip_path) {
      cairo_path_destroy(*state.clip_path);
    }
    stack->pop();
  }
  delete stack;
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

bool GraphicsContextRenderer::try_draw_circles(
    GraphicsContextRenderer& gc,
    py::object marker_path,
    cairo_matrix_t* marker_matrix,
    py::object path,
    cairo_matrix_t* matrix,
    std::optional<py::object> rgb_fc) {
  // Abuse the degenerate-segment handling by cairo to quickly draw fully
  // opaque circles.
  if ((marker_path != UNIT_CIRCLE)
      // yy = -xx due to the flipping in transform_to_matrix().
      || (marker_matrix->yy != -marker_matrix->xx)
      || marker_matrix->xy || marker_matrix->yx
      || !rgb_fc) {
    return false;
  }
  if (rgb_fc) {
    double r, g, b, a{1};
    if (py::len(*rgb_fc) == 3) {
      std::tie(r, g, b) = rgb_fc->cast<rgb_t>();
    } else {
      std::tie(r, g, b, a) = rgb_fc->cast<rgba_t>();
    }
    if (auto alpha = get_additional_state().alpha; alpha) {
      a = *alpha;
    }
    if ((rgba_t{r, g, b, a} != get_rgba())  // Can't draw edge with another color.
        || (a != 1)) {  // Can't handle alpha.  // FIXME Try drawing one at a time.
      return false;
    }
  }
  cairo_save(cr_);
  auto [r, g, b, a] = get_rgba();
  cairo_set_source_rgba(cr_, r, g, b, a);
  cairo_transform(cr_, matrix);
  auto vertices = path.attr("vertices").cast<py::array_t<double>>();
  // NOTE: For efficiency, we ignore codes, which is the documented behavior
  // even though not the actual one of other backends.
  auto n = vertices.shape(0);
  for (size_t i = 0; i < n; ++i) {
    auto x = *vertices.data(i, 0), y = *vertices.data(i, 1);
    cairo_move_to(cr_, x, y);
    cairo_close_path(cr_);
  }
  cairo_restore(cr_);
  cairo_save(cr_);
  cairo_set_line_cap(cr_, CAIRO_LINE_CAP_ROUND);
  cairo_set_line_width(cr_, 2 * std::abs(marker_matrix->xx));
  cairo_stroke(cr_);
  cairo_restore(cr_);
  return true;
}

GraphicsContextRenderer::GraphicsContextRenderer(double dpi) :
  cr_{nullptr},
  dpi_{dpi},
  mathtext_parser_{
    py::module::import("matplotlib.mathtext").attr("MathTextParser")("agg")},
  text2path_{py::module::import("matplotlib.textpath").attr("TextToPath")()} {}

GraphicsContextRenderer::~GraphicsContextRenderer() {
  if (cr_) {
    cairo_destroy(cr_);
  }
}

void GraphicsContextRenderer::set_ctx_from_surface(py::object py_surface) {
  if (py_surface.attr("__module__").cast<std::string>()
      != "cairocffi.surfaces") {
    throw std::invalid_argument(
        "Could not convert argument to cairo_surface_t*");
  }
  if (cr_) {
    cairo_destroy(cr_);
  }
  auto surface = reinterpret_cast<cairo_surface_t*>(
      py::module::import("cairocffi").attr("ffi").attr("cast")(
        "int", py_surface.attr("_pointer")).cast<uintptr_t>());
  cr_ = cairo_create(surface);
  auto stack = new std::stack<AdditionalState>({AdditionalState{}});
  stack->top().hatch = {};
  stack->top().hatch_color = to_rgba(rc_param("hatch.color"));
  stack->top().hatch_linewidth = rc_param("hatch.linewidth").cast<double>();
  stack->top().sketch = py::none();
  cairo_set_user_data(
      cr_, &STATE_KEY, stack, GraphicsContextRenderer::destroy_state_stack);
  cairo_set_line_join(cr_, CAIRO_LINE_JOIN_ROUND);  // NOTE: See below.
}

void GraphicsContextRenderer::set_ctx_from_image_args(
    cairo_format_t format, int width, int height) {
  // NOTE: This API will ultimately be favored over set_ctx_from_surface as
  // it bypasses the need to construct a surface in the Python-level using
  // yet another cairo wrapper.  In particular, cairocffi (which relies on
  // dlopen()) prevents the use of cairo-trace (which relies on LD_PRELOAD).
  if (cr_) {
    cairo_destroy(cr_);
  }
  auto surface = cairo_image_surface_create(format, width, height);
  cr_ = cairo_create(surface);
  cairo_surface_destroy(surface);
  auto stack = new std::stack<AdditionalState>({AdditionalState{}});
  stack->top().hatch = {};
  stack->top().hatch_color = to_rgba(rc_param("hatch.color"));
  stack->top().hatch_linewidth = rc_param("hatch.linewidth").cast<double>();
  stack->top().sketch = py::none();
  cairo_set_user_data(
      cr_, &STATE_KEY, stack, GraphicsContextRenderer::destroy_state_stack);
  // NOTE: Collections and text PathEffects have no joinstyle and implicitly
  // rely on a "round" default.
  cairo_set_line_join(cr_, CAIRO_LINE_JOIN_ROUND);
}

uintptr_t GraphicsContextRenderer::get_data_address() {
  // NOTE: The image buffer is not necessarily contiguous; see
  // cairo_image_surface_get_stride().
  auto surface = cairo_get_target(cr_);
  auto buf = cairo_image_surface_get_data(surface);
  return reinterpret_cast<uintptr_t>(buf);
}

void GraphicsContextRenderer::set_alpha(std::optional<double> alpha) {
  if (!cr_) {
    return;
  }
  get_additional_state().alpha = alpha;
}

void GraphicsContextRenderer::set_antialiased(cairo_antialias_t aa) {
  if (!cr_) {
    return;
  }
  cairo_set_antialias(cr_, aa);
}
void GraphicsContextRenderer::set_antialiased(py::object aa) {
  if (!cr_) {
    return;
  }
  cairo_set_antialias(
      cr_, py::bool_(aa) ? CAIRO_ANTIALIAS_FAST : CAIRO_ANTIALIAS_NONE);
}

void GraphicsContextRenderer::set_capstyle(std::string capstyle) {
  if (!cr_) {
    return;
  }
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
    ? rectangle->attr("bounds").cast<rectangle_t>()
    : std::optional<rectangle_t>{};
}

void GraphicsContextRenderer::set_clip_path(
    std::optional<py::object> transformed_path) {
  auto& clip_path = get_additional_state().clip_path;
  if (clip_path) {
    cairo_path_destroy(*clip_path);
  }
  if (transformed_path) {
    auto [path, transform] =
      transformed_path->attr("get_transformed_path_and_affine")()
      .cast<std::tuple<py::object, py::object>>();
    auto matrix = matrix_from_transform(transform, get_height());
    cairo_save(cr_);
    cairo_identity_matrix(cr_);
    load_path(cr_, path, &matrix);
    cairo_restore(cr_);
    clip_path = cairo_copy_path(cr_);
  } else {
    clip_path = {};
  }
}

void GraphicsContextRenderer::set_dashes(
    std::optional<double> dash_offset,
    std::optional<std::vector<double>> dash_list) {
  if (dash_list) {
    if (!dash_offset) {
      throw std::invalid_argument("Missing dash offset");
    }
    std::transform(
        dash_list->begin(), dash_list->end(), dash_list->begin(),
        [&](double points) { return points_to_pixels(points); });
    cairo_set_dash(cr_, dash_list->data(), dash_list->size(), *dash_offset);
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
  if (!cr_) {
    return;
  }
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
  if (!cr_) {
    return;
  }
  cairo_set_line_width(cr_, points_to_pixels(lw));
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
  if (!cr_) {
    return *this;
  }
  cairo_reference(cr_);
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
  if (!cr_) {
    return;
  }
  auto& states =
    *static_cast<std::stack<GraphicsContextRenderer::AdditionalState>*>(
        cairo_get_user_data(cr_, &STATE_KEY));
  states.pop();
  cairo_restore(cr_);
}

std::tuple<int, int> GraphicsContextRenderer::get_canvas_width_height() {
  return {get_width(), get_height()};
}

int GraphicsContextRenderer::get_width() {
  if (!cr_) {
    return 0;
  }
  return cairo_image_surface_get_width(cairo_get_target(cr_));
}

int GraphicsContextRenderer::get_height() {
  if (!cr_) {
    return 0;
  }
  return cairo_image_surface_get_height(cairo_get_target(cr_));
}

double GraphicsContextRenderer::points_to_pixels(double points) {
  return points * dpi_ / 72;
}

void GraphicsContextRenderer::draw_gouraud_triangles(
    GraphicsContextRenderer& gc,
    py::array_t<double> triangles,
    py::array_t<double> colors,
    py::object transform) {
  if (!cr_) {
    return;
  }
  if (&gc != this) {
    throw std::invalid_argument("Non-matching GraphicsContext");
  }
  auto ac = additional_context();
  auto matrix = matrix_from_transform(transform, get_height());
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
      cairo_mesh_pattern_line_to(
          pattern, *tri_raw.data(i, j, 0), *tri_raw.data(i, j, 1));
      cairo_mesh_pattern_set_corner_color_rgba(
          pattern, j,
          *col_raw.data(i, j, 0), *col_raw.data(i, j, 1),
          *col_raw.data(i, j, 2), *col_raw.data(i, j, 3));
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
  if (!cr_) {
    return;
  }
  if (&gc != this) {
    throw std::invalid_argument("Non-matching GraphicsContext");
  }
  auto ac = additional_context();
  auto im_raw = im.unchecked<3>();
  auto ni = im_raw.shape(0), nj = im_raw.shape(1);
  if (im_raw.shape(2) != 4) {
    throw std::invalid_argument("RGBA array must have size (m, n, 4)");
  }
  auto stride = cairo_format_stride_for_width(CAIRO_FORMAT_ARGB32, nj);
  auto buf = std::unique_ptr<uint8_t[]>(new uint8_t[ni * stride]);
  if (auto alpha = get_additional_state().alpha; alpha) {
    for (size_t i = 0; i < ni; ++i) {
      auto ptr = reinterpret_cast<uint32_t*>(buf.get() + i * stride);
      for (size_t j = 0; j < nj; ++j) {
        auto r = *im_raw.data(i, j, 0), g = *im_raw.data(i, j, 1),
             b = *im_raw.data(i, j, 2);
        auto a = *alpha * *im_raw.data(i, j, 3);
        *(ptr++) =
          (uint8_t(a) << 24) | (uint8_t(a / 255. * r) << 16)
          | (uint8_t(a / 255. * g) << 8) | (uint8_t(a / 255. * b));
      }
    }
  } else {
    for (size_t i = 0; i < ni; ++i) {
      auto ptr = reinterpret_cast<uint32_t*>(buf.get() + i * stride);
      for (size_t j = 0; j < nj; ++j) {
        auto r = *im_raw.data(i, j, 0), g = *im_raw.data(i, j, 1),
             b = *im_raw.data(i, j, 2), a = *im_raw.data(i, j, 3);
        *(ptr++) =
          (a << 24) | (uint8_t(a / 255. * r) << 16)
          | (uint8_t(a / 255. * g) << 8) | (uint8_t(a / 255. * b));
      }
    }
  }
  auto surface = cairo_image_surface_create_for_data(
      buf.get(), CAIRO_FORMAT_ARGB32, nj, ni, stride);
  auto pattern = cairo_pattern_create_for_surface(surface);
  auto matrix = cairo_matrix_t{1, 0, 0, -1, -x, -y + get_height()};
  cairo_pattern_set_matrix(pattern, &matrix);
  cairo_set_source(cr_, pattern);
  cairo_paint(cr_);
  cairo_pattern_destroy(pattern);
  cairo_surface_destroy(surface);
}

void GraphicsContextRenderer::draw_markers(
    GraphicsContextRenderer& gc,
    py::object marker_path,
    py::object marker_transform,
    py::object path,
    py::object transform,
    std::optional<py::object> rgb_fc) {
  if (!cr_) {
    return;
  }
  if (&gc != this) {
    throw std::invalid_argument("Non-matching GraphicsContext");
  }
  auto ac = additional_context();

  auto vertices = path.attr("vertices").cast<py::array_t<double>>();
  // NOTE: For efficiency, we ignore codes, which is the documented behavior
  // even though not the actual one of other backends.
  auto n_vertices = vertices.shape(0);

  auto marker_matrix = matrix_from_transform(marker_transform);
  // NOTE: Not clear why this is needed...
  auto matrix = matrix_from_transform(transform, get_height());

  // Initialize everyone, to avoid -Wmaybe-uninitialized in draw_one_marker().
  auto r = 0., g = 0., b = 0., a = 1.;
  if (rgb_fc) {
    if (py::len(*rgb_fc) == 3) {
      std::tie(r, g, b) = rgb_fc->cast<rgb_t>();
    } else {
      std::tie(r, g, b, a) = rgb_fc->cast<rgba_t>();
    }
    if (auto alpha = get_additional_state().alpha; alpha) {
      a = *alpha;
    }
  }

  // Recording surfaces are quite slow, so just call a lambda instead.
  auto draw_one_marker = [&](cairo_t* cr) {
    load_path(cr, marker_path, &marker_matrix);
    if (rgb_fc) {
      cairo_save(cr);
      cairo_set_source_rgba(cr, r, g, b, a);
      cairo_fill_preserve(cr);
      cairo_restore(cr);
    }
    cairo_stroke(cr);
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
    cairo_identity_matrix(cr_);
    load_path(cr_, marker_path, &marker_matrix);
    double x0, y0, x1, y1;
    cairo_stroke_extents(cr_, &x0, &y0, &x1, &y1);
    if (rgb_fc) {
      double x1f, y1f, x2f, y2f;
      cairo_fill_extents(cr_, &x1f, &y1f, &x2f, &y2f);
      x0 = std::min(x0, x1f);
      y0 = std::max(y0, y1f);
      x1 = std::min(x1, x2f);
      y1 = std::max(y1, y2f);
    }

    // Fill the pattern cache.
    for (size_t i = 0; i < n_subpix; ++i) {
      for (size_t j = 0; j < n_subpix; ++j) {
        auto raster_surface = cairo_surface_create_similar_image(
            cairo_get_target(cr_), CAIRO_FORMAT_ARGB32,
            std::ceil(x1 - x0 + 1), std::ceil(y1 - y0 + 1));
        auto raster_cr = cairo_create(raster_surface);
        copy_for_marker_stamping(cr_, raster_cr);
        cairo_translate(
            raster_cr, -x0 + double(i) / n_subpix, -y0 + double(j) / n_subpix);
        draw_one_marker(raster_cr);
        auto pattern = cairo_pattern_create_for_surface(raster_surface);
        cairo_pattern_set_filter(pattern, CAIRO_FILTER_NEAREST);
        patterns[i * n_subpix + j] = pattern;
        cairo_destroy(raster_cr);
        cairo_surface_destroy(raster_surface);
      }
    }

    for (size_t i = 0; i < n_vertices; ++i) {
      auto x = *vertices.data(i, 0), y = *vertices.data(i, 1);
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
      // Offsetting by get_height() is already taken care of by matrix.
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
    if (try_draw_circles(
          gc, marker_path, &marker_matrix, path, &matrix, rgb_fc)) {
      return;
    }
    for (size_t i = 0; i < n_vertices; ++i) {
      cairo_save(cr_);
      auto x = *vertices.data(i, 0), y = *vertices.data(i, 1);
      cairo_matrix_transform_point(&matrix, &x, &y);
      cairo_translate(cr_, x, y);
      draw_one_marker(cr_);
      cairo_restore(cr_);
    }
  }
}

void GraphicsContextRenderer::draw_path(
    GraphicsContextRenderer& gc,
    py::object path,
    py::object transform,
    std::optional<py::object> rgb_fc) {
  if (!cr_) {
    return;
  }
  if (&gc != this) {
    throw std::invalid_argument("Non-matching GraphicsContext");
  }
  auto ac = additional_context();
  if (auto sketch = get_additional_state().sketch; !sketch.is_none()) {
    path = path.attr("cleaned")(
        "transform"_a=transform,
        "curves"_a=true,
        "sketch"_a=get_additional_state().sketch);
    auto id = cairo_matrix_t{1, 0, 0, -1, 0, get_height()};
    load_path(cr_, path, &id);
  } else {
    auto matrix = matrix_from_transform(transform, get_height());
    load_path(cr_, path, &matrix);
  }
  if (rgb_fc) {
    cairo_save(cr_);
    double r, g, b, a{1};
    if (py::len(*rgb_fc) == 3) {
      std::tie(r, g, b) = rgb_fc->cast<rgb_t>();
    } else {
      std::tie(r, g, b, a) = rgb_fc->cast<rgba_t>();
    }
    if (auto alpha = get_additional_state().alpha; alpha) {
      a = *alpha;
    }
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
    auto [r, g, b, a] = get_additional_state().hatch_color;
    cairo_set_source_rgba(hatch_cr, r, g, b, a);
    cairo_set_line_width(
        hatch_cr, points_to_pixels(get_additional_state().hatch_linewidth));
    auto hatch_matrix = cairo_matrix_t{
      double(dpi), 0, 0, -double(dpi), 0, double(dpi)};
    load_path(hatch_cr, hatch_path, &hatch_matrix);
    cairo_fill_preserve(hatch_cr);
    cairo_stroke(hatch_cr);
    auto hatch_pattern = cairo_pattern_create_for_surface(hatch_surface);
    cairo_pattern_set_extend(hatch_pattern, CAIRO_EXTEND_REPEAT);
    cairo_set_source(cr_, hatch_pattern);
    cairo_clip_preserve(cr_);
    cairo_paint(cr_);
    cairo_pattern_destroy(hatch_pattern);
    cairo_destroy(hatch_cr);
    cairo_surface_destroy(hatch_surface);
    cairo_restore(cr_);
  }
  cairo_stroke(cr_);
}

void GraphicsContextRenderer::draw_path_collection(
    GraphicsContextRenderer& gc,
    py::object master_transform,
    std::vector<py::object> paths,
    std::vector<py::object> transforms,
    std::vector<py::object> offsets,
    py::object offset_transform,
    py::object fcs,
    py::object ecs,
    std::vector<double> lws,
    std::vector<std::tuple<std::optional<double>, std::optional<py::object>>>
      dashes,
    py::object aas,
    py::object urls,
    std::string offset_position) {
  // TODO: Persistent cache; cache eviction policy.
  if (!cr_) {
    return;
  }
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
  //     at draw time.
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
       n_offsets = offsets.size(),
       n = std::max({n_paths, n_transforms, n_offsets});
  if (!n_paths || !n_offsets) {
    return;
  }
  auto master_matrix = matrix_from_transform(master_transform, get_height());
  std::vector<cairo_matrix_t> matrices;
  if (n_transforms) {
    for (size_t i = 0; i < n_transforms; ++i) {
      matrices.push_back(matrix_from_transform(transforms[i], &master_matrix));
    }
  } else {
    n_transforms = 1;
    matrices.push_back(master_matrix);
  }
  auto offset_matrix = matrix_from_transform(offset_transform);
  auto convert_colors = [&](py::object colors) {
    auto alpha = get_additional_state().alpha;
    return
      py::module::import("matplotlib.colors").attr("to_rgba_array")(
          colors, alpha ? py::cast(*alpha) : py::none())
      .cast<py::array_t<double>>();
  };
  // Don't drop the arrays until the function exits.
  auto fcs_raw_keepref = convert_colors(fcs),
       ecs_raw_keepref = convert_colors(ecs);
  auto fcs_raw = fcs_raw_keepref.unchecked<2>(),
       ecs_raw = ecs_raw_keepref.unchecked<2>();
  double simplify_threshold =
    rc_param("path.simplify_threshold").cast<double>();
  auto cache = PatternCache{simplify_threshold};
  for (size_t i = 0; i < n; ++i) {
    cairo_save(cr_);
    auto path = paths[i % n_paths];
    auto matrix = matrices[i % n_transforms];
    auto [x, y] = offsets[i % n_offsets].cast<std::pair<double, double>>();
    cairo_matrix_transform_point(&offset_matrix, &x, &y);
    if (fcs_raw.shape(0)) {
      auto i_mod = i % fcs_raw.shape(0);
      auto r = *fcs_raw.data(i_mod, 0), g = *fcs_raw.data(i_mod, 1),
           b = *fcs_raw.data(i_mod, 2), a = *fcs_raw.data(i_mod, 3);
      cairo_set_source_rgba(cr_, r, g, b, a);
      cache.mask(cr_, path, matrix, draw_func_t::Fill, 0, {}, x, y);
    }
    if (ecs_raw.size()) {
      auto i_mod = i % ecs_raw.shape(0);
      auto r = *ecs_raw.data(i_mod, 0), g = *ecs_raw.data(i_mod, 1),
           b = *ecs_raw.data(i_mod, 2), a = *ecs_raw.data(i_mod, 3);
      cairo_set_source_rgba(cr_, r, g, b, a);
      auto lw = lws.size()
        ? points_to_pixels(lws[i % lws.size()]) : cairo_get_line_width(cr_);
      auto dash = dashes.size()
        ? convert_dash(dashes[i % dashes.size()]) : convert_dash(cr_);
      cache.mask(cr_, path, matrix, draw_func_t::Stroke, lw, dash, x, y);
    }
    // NOTE: We drop antialiaseds because that just seems silly.
    // We drop urls as they should be handled in a post-processing step anyways
    // (cairo doesn't seem to support them?).
    cairo_restore(cr_);
  }
}

void GraphicsContextRenderer::draw_text(
    GraphicsContextRenderer& gc,
    double x, double y, std::string s, py::object prop, double angle,
    bool ismath, py::object mtext) {
  if (!cr_) {
    return;
  }
  if (&gc != this) {
    throw std::invalid_argument("Non-matching GraphicsContext");
  }
  auto ac = additional_context();
  if (ismath) {
    // NOTE: If angle % 90 == 0, we can round x and y to avoid additional
    // aliasing on top of the one already provided by freetype.  Perhaps
    // we should let it know about the destination subpixel position?  If
    // angle % 90 != 0, all hope is lost anyways.
    if (fmod(angle, 90) == 0) {
      cairo_translate(cr_, round(x), round(y));
    } else {
      cairo_translate(cr_, x, y);
    }
    cairo_rotate(cr_, -angle * M_PI / 180);
    auto [ox, oy, width, height, descent, image, chars] =
      mathtext_parser_.attr("parse")(s, dpi_, prop)
      .cast<std::tuple<
          double, double, double, double, double, py::object, py::object>>();
    auto im_raw =
      py::array_t<uint8_t, py::array::c_style>{image}.mutable_unchecked<2>();
    auto ni = im_raw.shape(0), nj = im_raw.shape(1);
    auto stride = cairo_format_stride_for_width(CAIRO_FORMAT_A8, nj);
    // 1 byte per pixel!
    auto buf = std::unique_ptr<uint8_t[]>(new uint8_t[ni * stride]);
    for (size_t i = 0; i < ni; ++i) {
      std::memcpy(buf.get() + i * stride, im_raw.data(0, 0) + i * nj, nj);
    }
    auto surface = cairo_image_surface_create_for_data(
        buf.get(), CAIRO_FORMAT_A8, nj, ni, stride);
    cairo_mask_surface(cr_, surface, ox, oy - ni);
    cairo_surface_destroy(surface);
  } else {
    // Need to set the current point (otherwise later texts will just follow,
    // regardless of cairo_translate).
    cairo_translate(cr_, x, y);
    cairo_rotate(cr_, -angle * M_PI / 180);
    cairo_move_to(cr_, 0, 0);
    auto font_face = ft_font_from_prop(prop);
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
  // NOTE: ismath can be True, False, "TeX".
  if (rc_param("text.usetex").cast<bool>() || py::bool_(ismath)) {
    return
      py::module::import("matplotlib.backend_bases")
        .attr("RendererBase").attr("get_text_width_height_descent")(
            this, s, prop, ismath).cast<std::tuple<double, double, double>>();
  }
  cairo_save(cr_);
  auto font_face = ft_font_from_prop(prop);
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

Region GraphicsContextRenderer::copy_from_bbox(py::object bbox) {
  // Use ints to avoid a bunch of warnings below.
  int x0 = std::floor(bbox.attr("x0").cast<double>()),
      x1 = std::ceil(bbox.attr("x1").cast<double>()),
      y0 = std::floor(bbox.attr("y0").cast<double>()),
      y1 = std::ceil(bbox.attr("y1").cast<double>());
  if (!((0 <= x0) && (x0 <= x1) && (x1 < get_width())
        && (0 <= y0) && (y0 <= y1) && (y1 < get_height()))) {
    throw std::invalid_argument("Invalid bbox");
  }
  auto width = x1 - x0, height = y1 - y0;
  // 4 bytes per pixel throughout!
  auto buf = std::shared_ptr<char[]>(new char[4 * width * height]);
  auto surface = cairo_get_target(cr_);
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

PYBIND11_PLUGIN(_mpl_cairo) {
  py::module m("_mpl_cairo", "A cairo backend for matplotlib.");

  if (FT_Init_FreeType(&FT_LIB)) {
    throw std::runtime_error("FT_Init_FreeType failed");
  }
  auto clean_ft_lib = py::capsule([]() {
    if (FT_Done_FreeType(FT_LIB)) {
      throw std::runtime_error("FT_Done_FreeType failed");
    }
  });
  m.add_object("_cleanup", clean_ft_lib);
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

  py::class_<Region>(m, "_Region");

  py::class_<GraphicsContextRenderer>(m, "GraphicsContextRendererCairo")
    .def(py::init<double>())

    // Backend-specific API.
    .def("set_ctx_from_surface",
        &GraphicsContextRenderer::set_ctx_from_surface)
    .def("set_ctx_from_image_args",
        &GraphicsContextRenderer::set_ctx_from_image_args)
    .def("get_data_address", &GraphicsContextRenderer::get_data_address)

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
        return gcr.get_additional_state().clip_rectangle; })
    .def("get_clip_path", [](GraphicsContextRenderer& gcr) {
        return gcr.get_additional_state().clip_path; })
    // NOTE: Needed by get_hatch_path, which should call get_hatch().
    .def_property_readonly("_hatch", [](GraphicsContextRenderer& gcr) {
        return gcr.get_additional_state().hatch; })
    .def("get_hatch", [](GraphicsContextRenderer& gcr) {
        return gcr.get_additional_state().hatch; })
    .def("get_hatch_color", [](GraphicsContextRenderer& gcr) {
        return gcr.get_additional_state().hatch_color; })
    .def("get_hatch_linewidth", [](GraphicsContextRenderer& gcr) {
        return gcr.get_additional_state().hatch_linewidth; })
    // Needed by the default impl. of draw_quad_mesh.
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
    // NOTE: Needed for usetex and patheffects.
    .def_readonly("_text2path", &GraphicsContextRenderer::text2path_)

    .def("get_canvas_width_height",
        &GraphicsContextRenderer::get_canvas_width_height)
    // NOTE: Needed for patheffects, which should use get_canvas_width_height().
    .def_property_readonly("width", &GraphicsContextRenderer::get_width)
    .def_property_readonly("height", &GraphicsContextRenderer::get_height)

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
    .def("draw_text", &GraphicsContextRenderer::draw_text,
        "gc"_a, "x"_a, "y"_a, "s"_a, "prop"_a, "angle"_a,
        "ismath"_a=false, "mtext"_a=nullptr)
    .def("get_text_width_height_descent",
        &GraphicsContextRenderer::get_text_width_height_descent,
        "s"_a, "prop"_a, "ismath"_a)

    // Canvas API.
    .def("copy_from_bbox", &GraphicsContextRenderer::copy_from_bbox)
    .def("restore_region", &GraphicsContextRenderer::restore_region);

  return m.ptr();
}

}
