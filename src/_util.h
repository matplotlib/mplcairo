#pragma once

#include <Python.h>

#include <cairo.h>
#include <cairo-ft.h>

#include <pybind11/pybind11.h>
#include <pybind11/numpy.h>
#include <pybind11/stl.h>

// Helper for std::visit.
template<class... Ts> struct overloaded : Ts... { using Ts::operator()...; };
template<class... Ts> overloaded(Ts...) -> overloaded<Ts...>;

namespace mplcairo {

namespace py = pybind11;

namespace detail {

extern std::unordered_map<FT_Error, std::string> ft_errors;
extern FT_Library ft_library;

// Optional parts of cairo, backported from 1.15.
// Copy-pasted from cairo.h.
#define CAIRO_TAG_DEST "cairo.dest"
#define CAIRO_TAG_LINK "Link"
extern void (*cairo_tag_begin)(cairo_t*, char const*, char const*);
extern void (*cairo_tag_end)(cairo_t*, char const*);

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
extern cairo_surface_t* (*cairo_pdf_surface_create_for_stream)(
  cairo_write_func_t, void*, double, double);
extern cairo_surface_t* (*cairo_ps_surface_create_for_stream)(
  cairo_write_func_t, void*, double, double);
extern cairo_surface_t* (*cairo_svg_surface_create_for_stream)(
  cairo_write_func_t, void*, double, double);
extern void (*cairo_pdf_surface_set_size)(cairo_surface_t*, double, double);
extern void (*cairo_ps_surface_set_size)(cairo_surface_t*, double, double);
extern void (*cairo_pdf_surface_set_metadata)(
  cairo_surface_t*, cairo_pdf_metadata_t, char const*);
extern void (*cairo_ps_surface_set_eps)(cairo_surface_t*, cairo_bool_t);
extern void (*cairo_ps_surface_dsc_comment)(cairo_surface_t*, char const*);

#define ITER_CAIRO_OPTIONAL_API(_) \
  _(cairo_tag_begin) \
  _(cairo_tag_end) \
  _(cairo_pdf_surface_create_for_stream) \
  _(cairo_ps_surface_create_for_stream) \
  _(cairo_svg_surface_create_for_stream) \
  _(cairo_pdf_surface_set_size) \
  _(cairo_ps_surface_set_size) \
  _(cairo_pdf_surface_set_metadata) \
  _(cairo_ps_surface_set_eps) \
  _(cairo_ps_surface_dsc_comment)

// Other useful values.
extern cairo_user_data_key_t const
  REFS_KEY,  // cairo_t -> kept alive Python objects.
  STATE_KEY, // cairo_t -> additional state.
  FT_KEY;    // cairo_font_face_t -> FT_Face.
extern py::object UNIT_CIRCLE;
extern py::object PIXEL_MARKER;
extern bool FLOAT_SURFACE;
extern int MARKER_THREADS;
enum class MplcairoScriptSurface {
  None, Raster, Vector
};
extern MplcairoScriptSurface MPLCAIRO_SCRIPT_SURFACE;
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
  // hatch_color and linewidth are made optional only to be able to lazy-load
  // them, which is needed to keep GCR instantiation fast... which is needed
  // for the pattern cache.
  std::optional<rgba_t> hatch_color;
  std::optional<double> hatch_linewidth;
  std::optional<py::object> sketch;
  bool snap;
  std::optional<std::string> url;

  rgba_t get_hatch_color();
  double get_hatch_linewidth();
};

struct GlyphsAndClusters {
  cairo_glyph_t* glyphs{};
  int num_glyphs{};
  cairo_text_cluster_t* clusters{};
  int num_clusters{};
  cairo_text_cluster_flags_t cluster_flags{};

  ~GlyphsAndClusters();
};

py::object operator""_format(char const* fmt, std::size_t size);
bool py_eq(py::object obj1, py::object obj2);
py::object rc_param(std::string key);
cairo_format_t get_cairo_format();
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
py::array image_surface_to_buffer(cairo_surface_t* surface);
cairo_font_face_t* font_face_from_path(std::string path);
cairo_font_face_t* font_face_from_path(py::object path);
cairo_font_face_t* font_face_from_prop(py::object prop);
long get_hinting_flag();
std::unique_ptr<cairo_font_options_t, decltype(&cairo_font_options_destroy)>
  get_font_options();
void warn_on_missing_glyph(std::string s);
GlyphsAndClusters text_to_glyphs_and_clusters(cairo_t* cr, std::string s);

}
