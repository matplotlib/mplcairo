#include "_util.h"

#include <vector>

namespace mpl_cairo {

namespace detail {
cairo_user_data_key_t const SNAP_KEY{0};
}

namespace {
cairo_user_data_key_t const FT_KEY{0};
}

py::object UNIT_CIRCLE{};

py::object rc_param(std::string key) {
  return py::module::import("matplotlib").attr("rcParams")[key.c_str()];
}

rgba_t to_rgba(py::object color, std::optional<double> alpha) {
  return
    py::module::import("matplotlib.colors")
    .attr("to_rgba")(color, alpha).cast<rgba_t>();
}

cairo_matrix_t matrix_from_transform(py::object transform, double y0) {
  if (!py::bool_(py::getattr(transform, "is_affine", py::bool_(true)))) {
    throw std::invalid_argument("Only affine transforms are handled");
  }
  auto py_matrix = transform.cast<py::array_t<double>>().unchecked<2>();
  if ((py_matrix.shape(0) != 3) || (py_matrix.shape(1) != 3)) {
    throw std::invalid_argument(
        "Transformation matrix must have shape (3, 3)");
  }
  return cairo_matrix_t{
    py_matrix(0, 0), -py_matrix(1, 0),
    py_matrix(0, 1), -py_matrix(1, 1),
    py_matrix(0, 2), y0 - py_matrix(1, 2)};
}

cairo_matrix_t matrix_from_transform(
    py::object transform, cairo_matrix_t* master_matrix) {
  if (!py::bool_(py::getattr(transform, "is_affine", py::bool_(true)))) {
    throw std::invalid_argument("Only affine transforms are handled");
  }
  auto py_matrix = transform.cast<py::array_t<double>>().unchecked<2>();
  if ((py_matrix.shape(0) != 3) || (py_matrix.shape(1) != 3)) {
    throw std::invalid_argument(
        "Transformation matrix must have shape (3, 3)");
  }
  // The y flip is already handled by the master matrix.
  auto matrix = cairo_matrix_t{
    py_matrix(0, 0), py_matrix(1, 0),
    py_matrix(0, 1), py_matrix(1, 1),
    py_matrix(0, 2), py_matrix(1, 2)};
  cairo_matrix_multiply(&matrix, &matrix, master_matrix);
  return matrix;
}

void set_ctx_defaults(cairo_t* cr) {
  // NOTE: Collections and text PathEffects have no joinstyle and implicitly
  // rely on a "round" default.
  cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);
}

// Copy the whole path, as there is no cairo_path_reference().
cairo_path_t* copy_path(cairo_path_t* path) {
  auto surface = cairo_image_surface_create(CAIRO_FORMAT_A1, 0, 0);
  auto cr = cairo_create(surface);
  cairo_surface_destroy(surface);
  cairo_append_path(cr, path);
  auto new_path = cairo_copy_path(cr);
  cairo_destroy(cr);
  return new_path;
}

void copy_for_marker_stamping(cairo_t* orig, cairo_t* dest) {
  cairo_set_antialias(dest, cairo_get_antialias(orig));
  cairo_set_line_cap(dest, cairo_get_line_cap(orig));
  cairo_set_line_join(dest, cairo_get_line_join(orig));
  cairo_set_line_width(dest, cairo_get_line_width(orig));

  auto dash_count = cairo_get_dash_count(orig);
  auto dashes = std::unique_ptr<double[]>(new double[dash_count]);
  double offset;
  cairo_get_dash(orig, dashes.get(), &offset);
  cairo_set_dash(dest, dashes.get(), dash_count, offset);

  double r, g, b, a;
  cairo_pattern_get_rgba(cairo_get_source(orig), &r, &g, &b, &a);
  cairo_set_source_rgba(dest, r, g, b, a);
}

// Set the current path of `cr` to `path`, after transformation by `matrix`,
// ignoring the CTM ("exact").
//
// FIXME: Deal with overflow when transformed values do not fit in a 24-bit
// signed integer (https://bugs.freedesktop.org/show_bug.cgi?id=20091
// and test_simplification.test_overflow) by clipping the segments.
//
// Note that we do not need to perform a full line clipping, as cairo will
// do one too; we just need the coordinates to stay valid for cairo.  Thus,
// for example, if a segment goes from (-bignum, y0) to (+bignum, y1), it is
// sufficient to clip it to [(-2**22, y0), (2**22, y1)] -- it will be drawn
// as a horizontal line at position (y0+y1)/2 anyways.  Still, the simple
// clamping is insufficient to deal with slanted lines, and is just a temporary
// workaround.
//
// TODO: Path snapping in the general case.
void load_path_exact(
    cairo_t* cr, py::object path, cairo_matrix_t* matrix) {
  auto const min = double(-(1 << 22)), max = double(1 << 22);
  // We don't need to cairo_save()/cairo_restore() the whole state, we can just
  // store the CTM.
  cairo_matrix_t ctm;
  cairo_get_matrix(cr, &ctm);
  cairo_identity_matrix(cr);
  // We can't simply call cairo_transform(cr, matrix) because matrix may be
  // degenerate (e.g., for zero-sized markers).  Fortunately, the cost of doing
  // the transformation ourselves seems negligible (if any).
  auto vertices =
    path.attr("vertices").cast<py::array_t<double>>().unchecked<2>();
  auto maybe_codes = path.attr("codes");
  auto n = vertices.shape(0);
  if (vertices.shape(1) != 2) {
    throw std::invalid_argument("vertices must have shape (n, 2)");
  }
  cairo_new_path(cr);
  if (!maybe_codes.is_none()) {
    // codes may not be an integer array, in which case the following makes a
    // copy, so we need to keep it around.
    auto codes_keepref = maybe_codes.cast<py::array_t<int>>();
    auto codes = codes_keepref.unchecked<1>();
    if (codes.shape(0) != n) {
      throw std::invalid_argument(
          "Lengths of vertices and codes do not match");
    }
    for (size_t i = 0; i < n; ++i) {
      auto x0 = vertices(i, 0), y0 = vertices(i, 1);
      cairo_matrix_transform_point(matrix, &x0, &y0);
      auto is_finite = std::isfinite(x0) && std::isfinite(y0);
      x0 = std::clamp(x0, min, max);
      y0 = std::clamp(y0, min, max);
      switch (static_cast<PathCode>(codes(i))) {
        case PathCode::STOP:
          break;
        case PathCode::MOVETO:
          if (is_finite) {
            cairo_move_to(cr, x0, y0);
          } else {
            cairo_new_sub_path(cr);
          }
          break;
        case PathCode::LINETO:
          if (is_finite) {
            cairo_line_to(cr, x0, y0);
          } else {
            cairo_new_sub_path(cr);
          }
          break;
        // NOTE: The semantics of nonfinite control points are tested in
        // test_simplification.test_simplify_curve: if the last point is
        // finite, it sets the current point for the next curve; otherwise,
        // a new sub-path is created.
        case PathCode::CURVE3: {
          auto x1 = vertices(i + 1, 0), y1 = vertices(i + 1, 1);
          cairo_matrix_transform_point(matrix, &x1, &y1);
          i += 1;
          auto last_finite = std::isfinite(x1) && std::isfinite(y1);
          if (last_finite) {
            x1 = std::clamp(x1, min, max);
            y1 = std::clamp(y1, min, max);
            if (is_finite && cairo_has_current_point(cr)) {
              double x_prev, y_prev;
              cairo_get_current_point(cr, &x_prev, &y_prev);
              cairo_curve_to(cr,
                  (x_prev + 2 * x0) / 3, (y_prev + 2 * y0) / 3,
                  (2 * x0 + x1) / 3, (2 * y0 + y1) / 3,
                  x1, y1);
            } else {
              cairo_move_to(cr, x1, y1);
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
          auto last_finite = std::isfinite(x2) && std::isfinite(y2);
          if (last_finite) {
            x1 = std::clamp(x1, min, max);
            y1 = std::clamp(y1, min, max);
            x2 = std::clamp(x2, min, max);
            y2 = std::clamp(y2, min, max);
            if (is_finite && std::isfinite(x1) && std::isfinite(y1)
                && cairo_has_current_point(cr)) {
              cairo_curve_to(cr, x0, y0, x1, y1, x2, y2);
            } else {
              cairo_move_to(cr, x2, y2);
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
  } else {
    auto path_data = std::vector<cairo_path_data_t>{};
    path_data.reserve(2 * n);
    // NOTE: We do not implement full snapping control, as e.g. snapping of
    // Bezier control points (which is forced by SNAP_TRUE) does not make sense
    // anyways.
    auto snap = bool(cairo_get_user_data(cr, &detail::SNAP_KEY));
    auto has_current = false;
    for (size_t i = 0; i < n; ++i) {
      auto x = vertices(i, 0), y = vertices(i, 1);
      cairo_matrix_transform_point(matrix, &x, &y);
      auto is_finite = std::isfinite(x) && std::isfinite(y);
      x = std::clamp(x, min, max);
      y = std::clamp(y, min, max);
      if (is_finite) {
        cairo_path_data_t header, point;
        if (has_current) {
          header.header.type = CAIRO_PATH_LINE_TO;
          header.header.length = 2;
          if (snap) {
            auto& [x_prev, y_prev] = path_data.back().point;
            auto x_eq = x == x_prev, y_eq = y == y_prev;
            if (x_eq ^ y_eq) {
              // If we have a horizontal or a vertical line, snap both
              // coordinates.  NOTE: While it may make sense to only snap in
              // the direction orthogonal to the displacement, this would cause
              // e.g. axes spines to not line up properly, as they are drawn as
              // completely independent segments.
              x_prev = std::floor(x_prev) + .5;
              y_prev = std::floor(y_prev) + .5;
              point.point.x = std::floor(x) + .5;
              point.point.y = std::floor(y) + .5;
            } else {
              point.point.x = x;
              point.point.y = y;
            }
          } else {
            point.point.x = x;
            point.point.y = y;
          }
          path_data.push_back(header);
          path_data.push_back(point);
        } else {
          header.header.type = CAIRO_PATH_MOVE_TO;
          header.header.length = 2;
          point.point.x = x;
          point.point.y = y;
          path_data.push_back(header);
          path_data.push_back(point);
        }
        has_current = true;
      } else {
        has_current = false;
      }
    }
    auto path =
      cairo_path_t{CAIRO_STATUS_SUCCESS, path_data.data(), path_data.size()};
    cairo_append_path(cr, &path);
  }
  cairo_set_matrix(cr, &ctm);
}

// Fill and/or stroke `path` onto `cr` after transformation by `matrix`,
// ignoring the CTM ("exact").
void fill_and_stroke_exact(
    cairo_t* cr, py::object path, cairo_matrix_t* matrix,
    std::optional<rgba_t> fill, std::optional<rgba_t> stroke) {
  cairo_save(cr);
  auto path_loaded = false;
  if (fill) {
    auto [r, g, b, a] = *fill;
    cairo_set_source_rgba(cr, r, g, b, a);
    if (path == UNIT_CIRCLE) {
      // Abuse the degenerate-segment handling by cairo to draw circles
      // efficiently.
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
    auto [r, g, b, a] = *stroke;
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

long get_hinting_flag() {
  // NOTE: Should be moved out of backend_agg.
  return
    py::module::import("matplotlib.backends.backend_agg")
    .attr("get_hinting_flag")().cast<long>();
}

std::tuple<FT_Face, cairo_font_face_t*> ft_face_and_font_face_from_path(
    std::string path) {
  FT_Face ft_face;
  if (FT_New_Face(_ft2Library, path.c_str(), 0, &ft_face)) {
    throw std::runtime_error("FT_New_Face failed");
  }
  auto font_face =
    cairo_ft_font_face_create_for_ft_face(ft_face, get_hinting_flag());
  if (cairo_font_face_set_user_data(
        font_face, &FT_KEY, ft_face, cairo_destroy_func_t(FT_Done_Face))) {
    cairo_font_face_destroy(font_face);
    FT_Done_Face(ft_face);
    throw std::runtime_error("cairo_font_face_set_user_data failed");
  }
  return {ft_face, font_face};
}

std::tuple<FT_Face, cairo_font_face_t*> ft_face_and_font_face_from_prop(
    py::object prop) {
  // It is probably not worth implementing an additional layer of caching here
  // as findfont already has its cache and object equality needs would also
  // need to go through Python anyways.
  auto path =
    py::module::import("matplotlib.font_manager").attr("findfont")(prop)
    .cast<std::string>();
  return ft_face_and_font_face_from_path(path);
}

}
