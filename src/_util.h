#pragma once

#include <Python.h>

#include <cairo.h>
#include <cairo-ft.h>

#include <pybind11/pybind11.h>
#include <pybind11/numpy.h>
#include <pybind11/stl.h>

namespace mplcairo {

namespace py = pybind11;

namespace detail {

// Optional parts of cairo, backported from 1.15.
// Copy-pasted from cairo.h.
#define CAIRO_TAG_DEST "cairo.dest"
#define CAIRO_TAG_LINK "Link"
using tag_begin_t = void (*)(cairo_t*, char const*, char const*);
using tag_end_t = void (*)(cairo_t*, char const*);
extern tag_begin_t cairo_tag_begin;
extern tag_end_t   cairo_tag_end;

// Optional parts of cairo.
// Copy-pasted from cairo-pdf.h.
typedef enum _cairo_pdf_metadata {
    CAIRO_PDF_METADATA_TITLE,
    CAIRO_PDF_METADATA_AUTHOR,
    CAIRO_PDF_METADATA_SUBJECT,
    CAIRO_PDF_METADATA_KEYWORDS,
    CAIRO_PDF_METADATA_CREATOR,
    CAIRO_PDF_METADATA_CREATE_DATE,
    CAIRO_PDF_METADATA_MOD_DATE,
} cairo_pdf_metadata_t;
// Dynamically loaded functions.
using surface_create_for_stream_t =
  cairo_surface_t* (*)(cairo_write_func_t, void*, double, double);
using surface_set_size_t =
  void (*)(cairo_surface_t*, double, double);
using pdf_surface_set_metadata_t =
  void (*)(cairo_surface_t*, cairo_pdf_metadata_t, char const*);
using ps_surface_set_eps_t =
  void (*)(cairo_surface_t*, cairo_bool_t);
using ps_surface_dsc_comment_t =
  void (*)(cairo_surface_t*, char const*);
extern surface_create_for_stream_t cairo_pdf_surface_create_for_stream,
                                   cairo_ps_surface_create_for_stream,
                                   cairo_svg_surface_create_for_stream;
extern surface_set_size_t          cairo_pdf_surface_set_size,
                                   cairo_ps_surface_set_size;
extern pdf_surface_set_metadata_t  cairo_pdf_surface_set_metadata;
extern ps_surface_set_eps_t        cairo_ps_surface_set_eps;
extern ps_surface_dsc_comment_t    cairo_ps_surface_dsc_comment;

extern std::unordered_map<FT_Error, std::string> ft_errors;
extern FT_Library ft_library;

// Other useful values.
extern cairo_user_data_key_t const
  REFS_KEY,  // cairo_t -> kept alive Python objects.
  STATE_KEY, // cairo_t -> additional state.
  FT_KEY;    // cairo_font_face_t -> FT_Face.
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
  std::optional<std::string> url;
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
cairo_font_face_t* font_face_from_path(std::string path);
cairo_font_face_t* font_face_from_prop(py::object prop);
long get_hinting_flag();
std::unique_ptr<cairo_font_options_t, decltype(&cairo_font_options_destroy)>
  get_font_options();
std::tuple<std::unique_ptr<cairo_glyph_t, decltype(&cairo_glyph_free)>, size_t>
  text_to_glyphs(cairo_t* cr, std::string s);

}
