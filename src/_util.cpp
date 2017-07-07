#include "_util.h"

namespace mpl_cairo {

static cairo_user_data_key_t const FT_KEY = {0};
FT_Library FT_LIB = nullptr;
py::object UNIT_CIRCLE = {};

cairo_matrix_t matrix_from_transform(py::object transform, double y0) {
  if (!py::getattr(transform, "is_affine", py::bool_(true)).cast<bool>()) {
    throw std::invalid_argument("Only affine transforms are handled");
  }
  auto py_matrix = transform.cast<py::array_t<double>>();
  return cairo_matrix_t{
    *py_matrix.data(0, 0), -*py_matrix.data(1, 0),
    *py_matrix.data(0, 1), -*py_matrix.data(1, 1),
    *py_matrix.data(0, 2), y0 - *py_matrix.data(1, 2)};
}

cairo_matrix_t matrix_from_transform(
    py::object transform, cairo_matrix_t* master_matrix) {
  if (!py::getattr(transform, "is_affine", py::bool_(true)).cast<bool>()) {
    throw std::invalid_argument("Only affine transforms are handled");
  }
  auto py_matrix = transform.cast<py::array_t<double>>();
  // The y flip is already handled by the master matrix.
  auto matrix = cairo_matrix_t{
    *py_matrix.data(0, 0), *py_matrix.data(1, 0),
    *py_matrix.data(0, 1), *py_matrix.data(1, 1),
    *py_matrix.data(0, 2), *py_matrix.data(1, 2)};
  cairo_matrix_multiply(&matrix, &matrix, master_matrix);
  return matrix;
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

/**
 * Temporarily add `matrix` to `cr`'s CTM, and make `path` `cr`'s current path
 * (transformed accordingly).  Note that a pre-existing CTM may already be
 * present!
 */
void load_path(cairo_t* cr, py::object path, cairo_matrix_t* matrix) {
  cairo_save(cr);
  cairo_transform(cr, matrix);
  auto vertices = path.attr("vertices").cast<py::array_t<double>>();
  auto maybe_codes = path.attr("codes");
  auto n = vertices.shape(0);
  cairo_new_path(cr);
  if (!maybe_codes.is_none()) {
    auto codes = maybe_codes.cast<py::array_t<int>>();
    for (size_t i = 0; i < n; ++i) {
      auto x0 = *vertices.data(i, 0), y0 = *vertices.data(i, 1);
      auto isfinite = std::isfinite(x0) && std::isfinite(y0);
      switch (static_cast<PathCode>(*codes.data(i))) {
        case PathCode::STOP:
          break;
        case PathCode::MOVETO:
          if (isfinite) {
            cairo_move_to(cr, x0, y0);
          } else {
            cairo_new_sub_path(cr);
          }
          break;
        case PathCode::LINETO:
          if (isfinite) {
            cairo_line_to(cr, x0, y0);
          } else {
            cairo_new_sub_path(cr);
          }
          break;
          // NOTE: The semantics of nonfinite control points seem undocumented.
          // Here, we ignore the entire curve element.
        case PathCode::CURVE3: {
          auto x1 = *vertices.data(i + 1, 0), y1 = *vertices.data(i + 1, 1);
          i += 1;
          isfinite &= std::isfinite(x1) && std::isfinite(y1);
          if (isfinite && cairo_has_current_point(cr)) {
            double x_prev, y_prev;
            cairo_get_current_point(cr, &x_prev, &y_prev);
            cairo_curve_to(cr,
                (x_prev + 2 * x0) / 3, (y_prev + 2 * y0) / 3,
                (2 * x0 + x1) / 3, (2 * y0 + y1) / 3,
                x1, y1);
          } else {
            cairo_new_sub_path(cr);
          }
          break;
        }
        case PathCode::CURVE4: {
          auto x1 = *vertices.data(i + 1, 0), y1 = *vertices.data(i + 1, 1),
               x2 = *vertices.data(i + 2, 0), y2 = *vertices.data(i + 2, 1);
          i += 2;
          isfinite &= std::isfinite(x1) && std::isfinite(y1)
            && std::isfinite(x2) && std::isfinite(y2);
          if (isfinite && cairo_has_current_point(cr)) {
            cairo_curve_to(cr, x0, y0, x1, y1, x2, y2);
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
    for (size_t i = 0; i < n; ++i) {
      auto x = *vertices.data(i, 0), y = *vertices.data(i, 1);
      auto isfinite = std::isfinite(x) && std::isfinite(y);
      if (isfinite) {
        cairo_line_to(cr, x, y);
      } else {
        cairo_new_sub_path(cr);
      }
    }
  }
  cairo_restore(cr);
}

cairo_font_face_t* ft_font_from_prop(py::object prop) {
  // It is probably not worth implementing an additional layer of caching here
  // as findfont already has its cache and object equality needs would also
  // need to go through Python anyways.
  auto font_path =
    py::module::import("matplotlib.font_manager").attr("findfont")(prop)
    .cast<std::string>();
  FT_Face ft_face;
  if (FT_New_Face(FT_LIB, font_path.c_str(), 0, &ft_face)) {
    throw std::runtime_error("FT_New_Face failed");
  }
  auto font_face = cairo_ft_font_face_create_for_ft_face(ft_face, 0);
  if (cairo_font_face_set_user_data(
        font_face, &FT_KEY, ft_face, (cairo_destroy_func_t)FT_Done_Face)) {
    cairo_font_face_destroy(font_face);
    FT_Done_Face(ft_face);
    throw std::runtime_error("cairo_font_face_set_user_data failed");
  }
  return font_face;
}

}
