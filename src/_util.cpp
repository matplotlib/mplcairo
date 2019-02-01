#include "_util.h"

#include "_raqm.h"

#include <regex>
#include <stack>

#include "_macros.h"

using namespace std::string_literals;

namespace mplcairo {

namespace detail {

// FreeType error codes.  They are loaded as documented in fterror.h (modified
// to use std::unordered_map).
// NOTE: If we require FreeType>=2.6.3 then the macro can be replaced by
// FTERRORS_H_.
#undef __FTERRORS_H__
#define FT_ERRORDEF( e, v, s )  { e, s },
#define FT_ERROR_START_LIST     {
#define FT_ERROR_END_LIST       }

std::unordered_map<FT_Error, std::string> ft_errors =
#include FT_ERRORS_H
;
FT_Library ft_library{};

#define DEFINE_API(name) decltype(name) name;
ITER_CAIRO_OPTIONAL_API(DEFINE_API)
#undef DEFINE_API

// Other useful values.
cairo_user_data_key_t const REFS_KEY{}, STATE_KEY{}, FT_KEY{};
py::object UNIT_CIRCLE{py::none{}}, PIXEL_MARKER{py::none{}};
bool FLOAT_SURFACE{};
int MARKER_THREADS{};
MplcairoScriptSurface MPLCAIRO_SCRIPT_SURFACE{
  []() -> MplcairoScriptSurface {
    if (auto script_surface = std::getenv("MPLCAIRO_SCRIPT_SURFACE");
        script_surface) {
      if (script_surface == "raster"s) {
        return MplcairoScriptSurface::Raster;
      } else if (script_surface == "vector"s) {
        return MplcairoScriptSurface::Vector;
      }
    }
    return MplcairoScriptSurface::None;
  }()
};

}

rgba_t AdditionalState::get_hatch_color() {
  if (!hatch_color) {
    hatch_color = to_rgba(rc_param("hatch.color"));
  }
  return *hatch_color;
}

double AdditionalState::get_hatch_linewidth() {
  if (!hatch_linewidth) {
    hatch_linewidth = rc_param("hatch.linewidth").cast<double>();
  }
  return *hatch_linewidth;
}

GlyphsAndClusters::~GlyphsAndClusters() {
  cairo_glyph_free(glyphs);
  cairo_text_cluster_free(clusters);
}

py::object operator""_format(char const* fmt, std::size_t size) {
  return py::str(fmt, size).attr("format");
}

bool py_eq(py::object obj1, py::object obj2) {
  return py::module::import("operator").attr("eq")(obj1, obj2).cast<bool>();
}

py::object rc_param(std::string key)
{
  return py::module::import("matplotlib").attr("rcParams")[key.c_str()];
}

cairo_format_t get_cairo_format() {
  return
    detail::FLOAT_SURFACE
    ? static_cast<cairo_format_t>(7) : CAIRO_FORMAT_ARGB32;
}

rgba_t to_rgba(py::object color, std::optional<double> alpha)
{
  return
    py::module::import("matplotlib.colors")
    .attr("to_rgba")(color, alpha).cast<rgba_t>();
}

cairo_matrix_t matrix_from_transform(py::object transform, double y0)
{
  if (!py::bool_(py::getattr(transform, "is_affine", py::bool_(true)))) {
    throw std::invalid_argument("only affine transforms are handled");
  }
  auto const& py_matrix = transform.cast<py::array_t<double>>().unchecked<2>();
  if (py_matrix.shape(0) != 3 || py_matrix.shape(1) != 3) {
    throw std::invalid_argument(
      "transformation matrix must have shape (3, 3), "
      "not {.shape}"_format(transform).cast<std::string>());
  }
  return cairo_matrix_t{
    py_matrix(0, 0), -py_matrix(1, 0),
    py_matrix(0, 1), -py_matrix(1, 1),
    py_matrix(0, 2), y0 - py_matrix(1, 2)};
}

cairo_matrix_t matrix_from_transform(
  py::object transform, cairo_matrix_t const* master_matrix)
{
  if (!py::bool_(py::getattr(transform, "is_affine", py::bool_(true)))) {
    throw std::invalid_argument("only affine transforms are handled");
  }
  auto const& py_matrix = transform.cast<py::array_t<double>>().unchecked<2>();
  if (py_matrix.shape(0) != 3 || py_matrix.shape(1) != 3) {
    throw std::invalid_argument(
      "transformation matrix must have shape (3, 3), "
      "not {.shape}"_format(transform).cast<std::string>());
  }
  // The y flip is already handled by the master matrix.
  auto matrix = cairo_matrix_t{
    py_matrix(0, 0), py_matrix(1, 0),
    py_matrix(0, 1), py_matrix(1, 1),
    py_matrix(0, 2), py_matrix(1, 2)};
  cairo_matrix_multiply(&matrix, &matrix, master_matrix);
  return matrix;
}

bool has_vector_surface(cairo_t* cr)
{
  switch (auto const& type = cairo_surface_get_type(cairo_get_target(cr))) {
    case CAIRO_SURFACE_TYPE_IMAGE:
    case CAIRO_SURFACE_TYPE_XLIB:
      return false;
    case CAIRO_SURFACE_TYPE_PDF:
    case CAIRO_SURFACE_TYPE_PS:
    case CAIRO_SURFACE_TYPE_SVG:
    case CAIRO_SURFACE_TYPE_RECORDING:
      return true;
    case CAIRO_SURFACE_TYPE_SCRIPT:
      switch (detail::MPLCAIRO_SCRIPT_SURFACE) {
        case detail::MplcairoScriptSurface::Raster:
          return false;
        case detail::MplcairoScriptSurface::Vector:
          return true;
        default: ;
      }
    default:
      throw std::invalid_argument(
        "unexpected surface type: " + std::to_string(type));
  }
}

// Same as GraphicsContextRenderer::get_additional_state() but with checking
// for cairo_t*'s that we may not have initialized.
AdditionalState& get_additional_state(cairo_t* cr)
{
  auto const& data = cairo_get_user_data(cr, &detail::STATE_KEY);
  if (!data) {
    throw std::runtime_error("cairo_t* missing additional state");
  }
  auto& stack = *static_cast<std::stack<AdditionalState>*>(data);
  if (stack.empty()) {
    throw std::runtime_error("cairo_t* missing additional state");
  }
  return stack.top();
}

// Set the current path of `cr` to `path`, after transformation by `matrix`,
// ignoring the CTM ("exact").
//
// TODO: Path clipping and snapping in the general case (with codes present).
// NOTE: Matplotlib also *rounds* the linewidth in some cases (see
// RendererAgg::_draw_path), which helps with snappiness.  We do not provide
// this behavior; instead, one should set the default linewidths appropriately
// if desired.
// FIXME[cairo]: cairo requires coordinates to fit within a 24-bit signed
// integer (https://gitlab.freedesktop.org/cairo/cairo/issues/252 and
// :mpltest:`test_simplification.test_overflow`).  We simply clamp the
// values in the general case (with codes) -- proper handling would involve
// clipping of polygons and of Beziers -- and use a simple clippling algorithm
// (Cohen-Sutherland) in the simple (codeless) case as we expect most segments
// to be within the clip rectangle -- cairo will run its own clipping later
// anyways.

// A helper to store the CTM without the need to cairo_save() the full state.
// (We can't simply call cairo_transform(cr, matrix) because matrix may be
// degenerate (e.g., for zero-sized markers).  Fortunately, the cost of doing
// the transformation ourselves seems negligible, if any.)
struct LoadPathContext {
  cairo_t* const cr;
  cairo_matrix_t ctm;
  bool const snap;
#ifndef _WIN32
  double (*snapper)(double);
#else
  std::function<double(double)> snapper;
#endif

  public:
  LoadPathContext(cairo_t* cr) :
    cr{cr},
    snap{!has_vector_surface(cr) && get_additional_state(cr).snap}
  {
    cairo_get_matrix(cr, &ctm);
    cairo_identity_matrix(cr);
    cairo_new_path(cr);
    auto const& lw = cairo_get_line_width(cr);
    snapper =
    // MSVC doesn't realize that the lambdas have the same type so it gets to
    // use a slower path.
#ifndef _WIN32
      snap
      ? (0 < lw && (lw < 1 || std::lround(lw) % 2 == 1)
         ? [](double x) -> double { return std::floor(x) + .5; }
         : [](double x) -> double { return std::round(x); })
      // Snap between pixels if lw is exactly zero 0 (in which case the edge is
      // defined by the fill) or if lw rounds to an even value other than 0
      // (minimizing the alpha due to antialiasing).
      : [](double x) -> double { return x; };
#else
    [=](double x) -> double {
      return
        snap
        ? (0 < lw && (lw < 1 || std::lround(lw) % 2 == 1)
           ? std::floor(x) + .5
           : std::round(x))
        : x;
    };
#endif
  }
  ~LoadPathContext()
  {
    cairo_set_matrix(cr, &ctm);
  }
};

// This overload implements the general case.
void load_path_exact(
  cairo_t* cr, py::object path, cairo_matrix_t const* matrix)
{
  auto const& min = double(-(1 << 22)), max = double(1 << 22);
  auto const& lpc = LoadPathContext{cr};

  auto const& vertices_keepref =
    path.attr("vertices").cast<py::array_t<double>>();
  auto const& codes_keepref =
    path.attr("codes").cast<std::optional<py::array_t<uint8_t>>>();
  auto const& n = vertices_keepref.shape(0);
  if (vertices_keepref.shape(1) != 2) {
    throw std::invalid_argument(
      "vertices must have shape (n, 2), not {.shape}"_format(
        path.attr("vertices")).cast<std::string>());
  }
  if (!codes_keepref) {
    load_path_exact(cr, vertices_keepref, 0, n, matrix);
    return;
  }
  auto const& vertices = vertices_keepref.unchecked<2>();
  auto const& codes = codes_keepref->unchecked<1>();
  if (codes.shape(0) != n) {
    throw std::invalid_argument(
      "lengths of vertices ({}) and codes ({}) are mistached "_format(
        n, codes.shape(0)).cast<std::string>());
  }
  auto const& snapper = lpc.snapper;
  // Main loop.
  for (auto i = 0; i < n; ++i) {
    auto x0 = vertices(i, 0), y0 = vertices(i, 1);
    cairo_matrix_transform_point(matrix, &x0, &y0);
    auto const& is_finite = std::isfinite(x0) && std::isfinite(y0);
    // Better(?) than nothing.
    x0 = std::clamp(x0, min, max);
    y0 = std::clamp(y0, min, max);
    switch (static_cast<PathCode>(codes(i))) {
      case PathCode::STOP:
        break;
      case PathCode::MOVETO:
        if (is_finite) {
          cairo_move_to(cr, snapper(x0), snapper(y0));
        } else {
          cairo_new_sub_path(cr);
        }
        break;
      case PathCode::LINETO:
        if (is_finite) {
          cairo_line_to(cr, snapper(x0), snapper(y0));
        } else {
          cairo_new_sub_path(cr);
        }
        break;
      // The semantics of nonfinite control points are tested in
      // :mpltest:`test_simplification.test_simplify_curve`: if the last point
      // is finite, it sets the current point for the next curve; otherwise, a
      // new sub-path is created.
      case PathCode::CURVE3: {
        auto x1 = vertices(i + 1, 0), y1 = vertices(i + 1, 1);
        cairo_matrix_transform_point(matrix, &x1, &y1);
        i += 1;
        auto const& last_finite = std::isfinite(x1) && std::isfinite(y1);
        if (last_finite) {
          x1 = std::clamp(x1, min, max);
          y1 = std::clamp(y1, min, max);
          if (is_finite && cairo_has_current_point(cr)) {
            double x_prev, y_prev;
            cairo_get_current_point(cr, &x_prev, &y_prev);
            cairo_curve_to(cr,
              (x_prev + 2 * x0) / 3, (y_prev + 2 * y0) / 3,
              (2 * x0 + x1) / 3, (2 * y0 + y1) / 3,
              snapper(x1), snapper(y1));
          } else {
            cairo_move_to(cr, snapper(x1), snapper(y1));
          }
        } else {
          cairo_new_sub_path(cr);
        }
        break;
      }
      case PathCode::CURVE4: {
        auto x1 = vertices(i + 1, 0), y1 = vertices(i + 1, 1),
             x2 = vertices(i + 2, 0), y2 = vertices(i + 2, 1);
        cairo_matrix_transform_point(matrix, &x1, &y1);
        cairo_matrix_transform_point(matrix, &x2, &y2);
        i += 2;
        auto const& last_finite = std::isfinite(x2) && std::isfinite(y2);
        if (last_finite) {
          x1 = std::clamp(x1, min, max);
          y1 = std::clamp(y1, min, max);
          x2 = std::clamp(x2, min, max);
          y2 = std::clamp(y2, min, max);
          if (is_finite && std::isfinite(x1) && std::isfinite(y1)
              && cairo_has_current_point(cr)) {
            cairo_curve_to(cr, x0, y0, x1, y1, snapper(x2), snapper(y2));
          } else {
            cairo_move_to(cr, snapper(x2), snapper(y2));
          }
        } else {
          cairo_new_sub_path(cr);
        }
        break;
      }
      case PathCode::CLOSEPOLY:
        cairo_close_path(cr);
        break;
    }
  }
}

// This overload implements the case of a codeless path.  Exposing start and
// stop in the signature helps implementing support for agg.path.chunksize.
void load_path_exact(
  cairo_t* cr, py::array_t<double> vertices_keepref,
  ssize_t start, ssize_t stop, cairo_matrix_t const* matrix)
{
  auto const min = double(-(1 << 22)), max = double(1 << 22);
  auto const& lpc = LoadPathContext{cr};

  auto const& vertices = vertices_keepref.unchecked<2>();
  auto const& n = vertices.shape(0);
  if (!(0 <= start && start <= stop && stop <= n)) {
    throw std::invalid_argument(
      "invalid sub-path bounds ({}, {}) for path of size {}"_format(
        start, stop, n).cast<std::string>());
  }
  auto const& snapper = lpc.snapper;

  auto path_data = std::vector<cairo_path_data_t>{};
  path_data.reserve(2 * (stop - start));
  auto const LEFT = 1 << 0, RIGHT = 1 << 1, BOTTOM = 1 << 2, TOP = 1 << 3;
  auto const& outcode = [&](double x, double y) -> int {
    auto code = 0;
    if (x < min) {
      code |= LEFT;
    } else if (x > max) {
      code |= RIGHT;
    }
    if (y < min) {
      code |= BOTTOM;
    } else if (y > max) {
      code |= TOP;
    }
    return code;
  };
  // The previous point, if any, before clipping and snapping.
  auto prev = std::optional<std::tuple<double, double>>{};
  // Main loop.
  for (auto i = start; i < stop; ++i) {
    auto x = vertices(i, 0), y = vertices(i, 1);
    cairo_matrix_transform_point(matrix, &x, &y);
    if (std::isfinite(x) && std::isfinite(y)) {
      cairo_path_data_t header, point;
      if (prev) {
        header.header = {CAIRO_PATH_LINE_TO, 2};
        auto [x_prev, y_prev] = *prev;
        prev = {x, y};
        // Cohen-Sutherland clipping: we expect most segments to be within
        // the 1 << 22 by 1 << 22 box.
        auto code0 = outcode(x_prev, y_prev);
        auto code1 = outcode(x, y);
        auto accept = false, update_prev = false;
        while (true) {
          if (!(code0 | code1)) {
            accept = true;
            break;
          } else if (code0 & code1) {
            break;
          } else {
            auto xc = 0., yc = 0.;
            auto code = code0 ? code0 : code1;
            if (code & TOP) {
              xc = x_prev + (x - x_prev) * (max - y_prev) / (y - y_prev);
              yc = max;
            } else if (code & BOTTOM) {
              xc = x_prev + (x - x_prev) * (min - y_prev) / (y - y_prev);
              yc = min;
            } else if (code & RIGHT) {
              yc = y_prev + (y - y_prev) * (max - x_prev) / (x - x_prev);
              xc = max;
            } else if (code & LEFT) {
              yc = y_prev + (y - y_prev) * (min - x_prev) / (x - x_prev);
              xc = min;
            }
            if (code == code0) {
              update_prev = true;
              x_prev = xc;
              y_prev = yc;
              code0 = outcode(x_prev, y_prev);
            } else {
              x = xc;
              y = yc;
              code1 = outcode(x, y);
            }
          }
        }
        if (accept) {
          // If we accept the segment, but the previous point moved, record
          // a MOVE_TO the new previous point (which will be followed by a
          // LINE_TO the current point).
          if (update_prev) {
            cairo_path_data_t header_prev, point_prev;
            header_prev.header = {CAIRO_PATH_MOVE_TO, 2};
            point_prev.point = {x_prev, y_prev};
            path_data.push_back(header_prev);
            path_data.push_back(point_prev);
          }
        } else {
          // If we don't accept the segment, still record a MOVE_TO the raw
          // destination, as the next point may involve snapping.
          header.header = {CAIRO_PATH_MOVE_TO, 2};
        }
        // Snapping.
        if (lpc.snap && (x == x_prev || y == y_prev)) {
          // If we have a horizontal or a vertical line, snap both coordinates.
          // While it may make sense to only snap in the direction orthogonal
          // to the displacement, this would cause e.g. axes spines to not line
          // up properly, as they are drawn as independent segments.
          path_data.back().point = {snapper(x_prev), snapper(y_prev)};
          point.point = {snapper(x), snapper(y)};
        } else {
          point.point = {x, y};
        }
        // Record the point.
        path_data.push_back(header);
        path_data.push_back(point);
      } else {
        prev = {x, y};
        header.header = {CAIRO_PATH_MOVE_TO, 2};
        point.point = {x, y};
        path_data.push_back(header);
        path_data.push_back(point);
      }
    } else {
      prev = {};
    }
  }
  auto const& path =
    cairo_path_t{
      CAIRO_STATUS_SUCCESS, path_data.data(), int(path_data.size())};
  cairo_append_path(cr, &path);
}

// Fill and/or stroke `path` onto `cr` after transformation by `matrix`,
// ignoring the CTM ("exact").
void fill_and_stroke_exact(
  cairo_t* cr, py::object path, cairo_matrix_t const* matrix,
  std::optional<rgba_t> fill, std::optional<rgba_t> stroke)
{
  cairo_save(cr);
  auto path_loaded = false;
  if (fill) {
    auto const& [r, g, b, a] = *fill;
    cairo_set_source_rgba(cr, r, g, b, a);
    if (path.is(detail::UNIT_CIRCLE) && !has_vector_surface(cr)) {
      // Abuse the degenerate-segment handling by cairo to rasterize
      // circles efficiently.  Don't do this on vector backends both
      // because the user may technically want the actual path, and because
      // Inkscape does not render circle markers on zero-sized paths
      // (FIXME[inkscape] https://bugs.launchpad.net/inkscape/+bug/689562).
      cairo_save(cr);
      cairo_new_path(cr);
      cairo_move_to(cr, matrix->x0, matrix->y0);
      cairo_close_path(cr);
      cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
      cairo_set_line_width(cr, 2);
      cairo_set_matrix(cr, matrix);
      cairo_stroke(cr);
      cairo_restore(cr);
    } else {
      if (!path_loaded) {
        load_path_exact(cr, path, matrix);
        path_loaded = true;
      }
      cairo_fill_preserve(cr);
    }
  }
  if (stroke) {
    auto const& [r, g, b, a] = *stroke;
    cairo_set_source_rgba(cr, r, g, b, a);
    if (!path_loaded) {
      load_path_exact(cr, path, matrix);
      path_loaded = true;
    }
    cairo_identity_matrix(cr);  // Dashes are interpreted using the CTM.
    cairo_stroke_preserve(cr);
  }
  cairo_restore(cr);
}

py::array image_surface_to_buffer(cairo_surface_t* surface) {
  if (auto const& type = cairo_surface_get_type(surface);
      type != CAIRO_SURFACE_TYPE_IMAGE) {
    throw std::runtime_error(
      "_get_buffer only supports image surfaces, not {}"_format(type)
      .cast<std::string>());
  }
  cairo_surface_reference(surface);
  cairo_surface_flush(surface);
  switch (auto const& fmt = cairo_image_surface_get_format(surface);
          // Avoid "not in enumerated type" warning with CAIRO_FORMAT_RGBA_128F.
          static_cast<int>(fmt)) {
    case static_cast<int>(CAIRO_FORMAT_ARGB32):
      return py::array_t<uint8_t>{
        {cairo_image_surface_get_height(surface),
         cairo_image_surface_get_width(surface),
         4},
        {cairo_image_surface_get_stride(surface), 4, 1},
         cairo_image_surface_get_data(surface),
         py::capsule(
           surface,
           [](void* surface) -> void {
             cairo_surface_destroy(static_cast<cairo_surface_t*>(surface));
           })};
    case 7:  // CAIRO_FORMAT_RGBA_128F.
      return py::array_t<float>{
        {cairo_image_surface_get_height(surface),
         cairo_image_surface_get_width(surface),
         4},
        {cairo_image_surface_get_stride(surface), 16, 4},
        reinterpret_cast<float*>(cairo_image_surface_get_data(surface)),
        py::capsule(
          surface,
          [](void* surface) -> void {
            cairo_surface_destroy(static_cast<cairo_surface_t*>(surface));
          })};
    default:
      throw std::invalid_argument(
        "_get_buffer only supports images surfaces with ARGB32 and RGBA128F "
        "formats, not {}"_format(fmt).cast<std::string>());
  }
}

cairo_font_face_t* font_face_from_path(std::string path)
{
  auto face_index = 0;
  if (auto match = std::smatch{};
      std::regex_match(path, match, std::regex{"(.*)#(\\d+)"})) {
    path = match[1];
    face_index = std::stoi(match[2]);
  }
  FT_Face ft_face;
  FT_CHECK(
    FT_New_Face, detail::ft_library, path.c_str(), face_index, &ft_face);
  auto const& font_face =
    cairo_ft_font_face_create_for_ft_face(ft_face, get_hinting_flag());
  CAIRO_CLEANUP_CHECK(
    { cairo_font_face_destroy(font_face); FT_Done_Face(ft_face); },
    cairo_font_face_set_user_data,
    font_face, &detail::FT_KEY, ft_face,
    [](void* ptr) -> void {
      FT_CHECK(FT_Done_Face, reinterpret_cast<FT_Face>(ptr));
    });
  return font_face;
}

cairo_font_face_t* font_face_from_path(py::object path) {
  return
    font_face_from_path(
#if PY_VERSION_HEX >= 0x03060000
      py::reinterpret_steal<py::object>(PY_CHECK(PyOS_FSPath, path.ptr()))
#else
      path
#endif
      .cast<std::string>());
}

cairo_font_face_t* font_face_from_prop(py::object prop)
{
  // It is probably not worth implementing an additional layer of caching here
  // as findfont already has its cache and object equality needs would also
  // need to go through Python anyways.
  auto const& path =
    py::module::import("matplotlib.font_manager").attr("findfont")(prop);
  return font_face_from_path(path);
}

long get_hinting_flag()
{
  // FIXME[matplotlib]: Should be moved out of backend_agg.
  return
    py::module::import("matplotlib.backends.backend_agg")
    .attr("get_hinting_flag")().cast<long>();
}

std::unique_ptr<cairo_font_options_t, decltype(&cairo_font_options_destroy)>
  get_font_options()
{
  auto const& options = cairo_font_options_create();
  cairo_font_options_set_antialias(
    options,
    []() -> cairo_antialias_t {
      auto aa = rc_param("text.antialiased");
      try {
        return aa.cast<cairo_antialias_t>();
      } catch (py::cast_error&) {
        return
          aa.cast<bool>() ? CAIRO_ANTIALIAS_SUBPIXEL : CAIRO_ANTIALIAS_NONE;
      }
    }());
  return {options, cairo_font_options_destroy};
}

void warn_on_missing_glyph(std::string s) {
  PY_CHECK(
    PyErr_WarnEx,
    nullptr,
    "Requested glyph ({}) missing from current font."_format(s)
    .cast<std::string>().c_str(),
    1);
}

GlyphsAndClusters text_to_glyphs_and_clusters(cairo_t* cr, std::string s)
{
  auto const& scaled_font = cairo_get_scaled_font(cr);
  auto gac = GlyphsAndClusters{};
  if (has_raqm()) {
    auto const& ft_face = cairo_ft_scaled_font_lock_face(scaled_font);
    auto const& scaled_font_unlock_cleanup =
      std::unique_ptr<
        std::remove_pointer_t<std::remove_reference_t<decltype(scaled_font)>>,
        decltype(&cairo_ft_scaled_font_unlock_face)>{
          scaled_font, cairo_ft_scaled_font_unlock_face};
    auto const& rq = raqm::create();
    auto const& rq_cleanup =
      std::unique_ptr<
        std::remove_pointer_t<std::remove_reference_t<decltype(rq)>>,
        decltype(raqm::destroy)>{
          rq, raqm::destroy};
    if (!rq) {
      throw std::runtime_error("failed to compute text layout");
    }
    TRUE_CHECK(raqm::set_text_utf8, rq, s.c_str(), s.size());
    TRUE_CHECK(raqm::set_freetype_face, rq, ft_face);
    TRUE_CHECK(raqm::layout, rq);
    auto num_glyphs = size_t{};
    auto const& rq_glyphs = raqm::get_glyphs(rq, &num_glyphs);
    gac.num_glyphs = num_glyphs;
    gac.glyphs = cairo_glyph_allocate(gac.num_glyphs);
    auto x = 0., y = 0.;
    for (auto i = 0; i < gac.num_glyphs; ++i) {
      auto const& rq_glyph = rq_glyphs[i];
      gac.glyphs[i].index = rq_glyph.index;
      gac.glyphs[i].x = x + rq_glyph.x_offset / 64.;
      x += rq_glyph.x_advance / 64.;
      gac.glyphs[i].y = y + rq_glyph.y_offset / 64.;
      y += rq_glyph.y_advance / 64.;
    }
    auto prev_cluster = size_t(-1);
    for (auto i = 0; i < gac.num_glyphs; ++i) {
      auto const& rq_glyph = rq_glyphs[i];
      if (rq_glyph.cluster != prev_cluster) {
        prev_cluster = rq_glyph.cluster;
        ++gac.num_clusters;
      }
    }
    gac.clusters = cairo_text_cluster_allocate(gac.num_clusters);
    auto cluster = &gac.clusters[-1];
    prev_cluster = -1;
    for (auto i = 0; i < gac.num_glyphs; ++i) {
      auto const& rq_glyph = rq_glyphs[i];
      if (rq_glyph.cluster != prev_cluster) {
        ++cluster;
        cluster->num_bytes = cluster->num_glyphs = 0;
      }
      cluster->num_bytes +=
        (i + 1 < gac.num_glyphs ? rq_glyphs[i + 1].cluster : s.size())
        - rq_glyph.cluster;
      cluster->num_glyphs += 1;
      prev_cluster = rq_glyph.cluster;
    }
  } else {
    CAIRO_CHECK(
      cairo_scaled_font_text_to_glyphs,
      scaled_font, 0, 0, s.c_str(), s.size(),
      &gac.glyphs, &gac.num_glyphs,
      &gac.clusters, &gac.num_clusters, &gac.cluster_flags);
  }
  return gac;
}

}
