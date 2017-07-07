#pragma once

#include <cairo/cairo.h>
#include <cairo/cairo-ft.h>

#include <pybind11/pybind11.h>
#include <pybind11/eval.h>
#include <pybind11/numpy.h>
#include <pybind11/stl.h>

namespace mpl_cairo {

namespace py = pybind11;

extern FT_Library FT_LIB;
extern py::object UNIT_CIRCLE;

enum class PathCode {
  STOP = 0, MOVETO = 1, LINETO = 2, CURVE3 = 3, CURVE4 = 4, CLOSEPOLY = 79
};

cairo_matrix_t matrix_from_transform(py::object transform, double y0=0);
cairo_matrix_t matrix_from_transform(
    py::object transform, cairo_matrix_t* master_matrix);
cairo_t* trivial_context();
void copy_for_marker_stamping(cairo_t* orig, cairo_t* dest);
void load_path(cairo_t* cr, py::object path, cairo_matrix_t* matrix);
cairo_font_face_t* ft_font_from_prop(py::object prop);

}
