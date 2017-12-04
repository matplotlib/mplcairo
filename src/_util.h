#pragma once

#include <cairo.h>
#include <cairo-ft.h>

#include <pybind11/pybind11.h>
#include <pybind11/numpy.h>
#include <pybind11/stl.h>

// Matplotlib's own FT_Library, which we load at runtime by dlopen()ing the
// ft2font extension module with RTLD_GLOBAL.
extern FT_Library _ft2Library;

namespace mplcairo {

namespace py = pybind11;

namespace detail {

using surface_create_for_stream_t =
  cairo_surface_t* (*)(cairo_write_func_t, void*, double, double);
using surface_set_size_t =
  void (*)(cairo_surface_t*, double, double);
using ps_surface_set_eps_t =
  void (*)(cairo_surface_t*, cairo_bool_t);
extern surface_create_for_stream_t cairo_pdf_surface_create_for_stream,
                                   cairo_ps_surface_create_for_stream,
                                   cairo_svg_surface_create_for_stream;
extern surface_set_size_t          cairo_pdf_surface_set_size,
                                   cairo_ps_surface_set_size;
extern ps_surface_set_eps_t        cairo_ps_surface_set_eps;

extern cairo_user_data_key_t const FILE_KEY,
                                   FT_KEY,
                                   MATHTEXT_RECTANGLE_KEY,
                                   MATHTEXT_TO_BASELINE_KEY,
                                   STATE_KEY;
extern py::object UNIT_CIRCLE;
}

using rectangle_t = std::tuple<double, double, double, double>;
using rgb_t = std::tuple<double, double, double>;
using rgba_t = std::tuple<double, double, double, double>;

enum class PathCode {
  STOP = 0, MOVETO = 1, LINETO = 2, CURVE3 = 3, CURVE4 = 4, CLOSEPOLY = 79
};

struct AdditionalState {
  // Extents cannot be easily recovered from PDF/SVG surfaces, so record them.
  double width, height, dpi;
  std::optional<double> alpha;
  std::variant<cairo_antialias_t, bool> antialias;
  std::optional<rectangle_t> clip_rectangle;
  std::tuple<std::optional<py::object>, std::shared_ptr<cairo_path_t>>
    clip_path;
  std::optional<std::string> hatch;
  rgba_t hatch_color;
  double hatch_linewidth;
  std::optional<py::object> sketch;
  bool snap;
};

py::object rc_param(std::string key);
rgba_t to_rgba(py::object color, std::optional<double> alpha = {});
cairo_matrix_t matrix_from_transform(py::object transform, double y0 = 0);
cairo_matrix_t matrix_from_transform(
  py::object transform, cairo_matrix_t const* master_matrix);
bool has_vector_surface(cairo_t* cr);
AdditionalState& get_additional_state(cairo_t* cr);
void load_path_exact(
  cairo_t* cr, py::object path, cairo_matrix_t const* matrix);
void load_path_exact(
  cairo_t* cr, py::array_t<double> vertices, ssize_t start, ssize_t stop,
  cairo_matrix_t const* matrix);
void fill_and_stroke_exact(
  cairo_t* cr, py::object path, cairo_matrix_t const* matrix,
  std::optional<rgba_t> fill, std::optional<rgba_t> stroke);
long get_hinting_flag();
cairo_font_face_t* font_face_from_path(std::string path);
cairo_font_face_t* font_face_from_prop(py::object prop);
std::tuple<std::unique_ptr<cairo_glyph_t, decltype(&cairo_glyph_free)>, size_t>
  text_to_glyphs(cairo_t* cr, std::string s);

}
