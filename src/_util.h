#pragma once

#include <cairo/cairo.h>
#include <cairo/cairo-ft.h>

#include <pybind11/pybind11.h>
#include <pybind11/eval.h>
#include <pybind11/numpy.h>
#include <pybind11/stl.h>

namespace mpl_cairo {

namespace py = pybind11;

using rectangle_t = std::tuple<double, double, double, double>;
using rgb_t = std::tuple<double, double, double>;
using rgba_t = std::tuple<double, double, double, double>;

extern FT_Library FT_LIB;
extern py::object UNIT_CIRCLE;

enum class PathCode {
  STOP = 0, MOVETO = 1, LINETO = 2, CURVE3 = 3, CURVE4 = 4, CLOSEPOLY = 79
};

py::object rc_param(std::string key);
rgba_t to_rgba(py::object color, std::optional<double> alpha = {});
cairo_matrix_t matrix_from_transform(py::object transform, double y0 = 0);
cairo_matrix_t matrix_from_transform(
    py::object transform, cairo_matrix_t* master_matrix);
void set_ctx_defaults(cairo_t* cr);
cairo_path_t* copy_path(cairo_path_t* path);
void copy_for_marker_stamping(cairo_t* orig, cairo_t* dest);
void load_path_exact(
    cairo_t* cr, py::object path, cairo_matrix_t* matrix);
void fill_and_stroke_exact(
    cairo_t* cr, py::object path, cairo_matrix_t* matrix,
    std::optional<rgba_t> fill, std::optional<rgba_t> stroke);
cairo_font_face_t* ft_font_from_prop(py::object prop);

}
