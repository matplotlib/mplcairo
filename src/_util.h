#pragma once

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
using ssize_t = Py_ssize_t;

namespace detail {

extern std::unordered_map<FT_Error, std::string> const ft_errors;
extern FT_Library ft_library;
extern bool has_pycairo;
extern std::array<uint8_t, 0x10000> unpremultiplication_table;

// Optional parts of cairo.

// Copy-pasted from cairo.h, backported from 1.15.
#define CAIRO_TAG_DEST "cairo.dest"
#define CAIRO_TAG_LINK "Link"
extern void (*cairo_tag_begin)(cairo_t*, char const*, char const*);
extern void (*cairo_tag_end)(cairo_t*, char const*);

// Copy-pasted from cairo-pdf.h.
typedef enum _cairo_pdf_version {
    CAIRO_PDF_VERSION_1_4,
    CAIRO_PDF_VERSION_1_5
} cairo_pdf_version_t;
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
extern void (*cairo_pdf_surface_restrict_to_version)(
  cairo_surface_t*, cairo_pdf_version_t);
extern void (*cairo_pdf_surface_set_metadata)(
  cairo_surface_t*, cairo_pdf_metadata_t, char const*);
extern void (*cairo_pdf_surface_set_size)(cairo_surface_t*, double, double);

// Copy-pasted from cairo-ps.h.
typedef enum _cairo_ps_level {
    CAIRO_PS_LEVEL_2,
    CAIRO_PS_LEVEL_3
} cairo_ps_level_t;
extern cairo_surface_t* (*cairo_ps_surface_create_for_stream)(
  cairo_write_func_t, void*, double, double);
extern void (*cairo_ps_surface_dsc_comment)(cairo_surface_t*, char const*);
extern void (*cairo_ps_surface_restrict_to_level)(
  cairo_surface_t*, cairo_ps_level_t);
extern void (*cairo_ps_surface_set_eps)(cairo_surface_t*, cairo_bool_t);
extern void (*cairo_ps_surface_set_size)(cairo_surface_t*, double, double);

// Copy-pasted from cairo-svg.h.
typedef enum _cairo_svg_version {
    CAIRO_SVG_VERSION_1_1,
    CAIRO_SVG_VERSION_1_2
} cairo_svg_version_t;
extern cairo_surface_t* (*cairo_svg_surface_create_for_stream)(
  cairo_write_func_t, void*, double, double);
extern void (*cairo_svg_surface_restrict_to_version)(
  cairo_surface_t*, cairo_svg_version_t);

#define ITER_CAIRO_OPTIONAL_API(_) \
  _(cairo_tag_begin) \
  _(cairo_tag_end) \
  _(cairo_pdf_surface_create_for_stream) \
  _(cairo_pdf_surface_restrict_to_version) \
  _(cairo_pdf_surface_set_metadata) \
  _(cairo_pdf_surface_set_size) \
  _(cairo_ps_surface_create_for_stream) \
  _(cairo_ps_surface_dsc_comment) \
  _(cairo_ps_surface_restrict_to_level) \
  _(cairo_ps_surface_set_eps) \
  _(cairo_ps_surface_set_size) \
  _(cairo_svg_surface_create_for_stream) \
  _(cairo_svg_surface_restrict_to_version)

// Other useful values.
extern std::unordered_map<std::string, cairo_font_face_t*> FONT_CACHE;
extern cairo_user_data_key_t const
  REFS_KEY,           // cairo_t -> kept alive Python objects.
  STATE_KEY,          // cairo_t -> additional state.
  INIT_MATRIX_KEY,    // cairo_t -> cairo_matrix_t.
  FT_KEY,             // cairo_font_face_t -> FT_Face.
  FEATURES_KEY,       // cairo_font_face_t -> OpenType features.
  IS_COLOR_FONT_KEY;  // cairo_font_face_t -> non-null if a color font.
extern py::object RC_PARAMS;
extern py::object PIXEL_MARKER;
extern py::object UNIT_CIRCLE;
extern int COLLECTION_THREADS;
extern bool FLOAT_SURFACE;
extern double MITER_LIMIT;
extern bool DEBUG;
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
void restore_init_matrix(cairo_t* cr);
void load_path_exact(
  cairo_t* cr, py::handle path, cairo_matrix_t const* matrix);
void load_path_exact(
  cairo_t* cr, py::array_t<double> vertices, ssize_t start, ssize_t stop,
  cairo_matrix_t const* matrix);
void fill_and_stroke_exact(
  cairo_t* cr, py::handle path, cairo_matrix_t const* matrix,
  std::optional<rgba_t> fill, std::optional<rgba_t> stroke);
py::array image_surface_to_buffer(cairo_surface_t* surface);
cairo_font_face_t* font_face_from_path(std::string path);
cairo_font_face_t* font_face_from_path(py::object path);
cairo_font_face_t* font_face_from_prop(py::object prop);
long get_hinting_flag();
void adjust_font_options(cairo_t* cr);
void warn_on_missing_glyph(std::string s);
GlyphsAndClusters text_to_glyphs_and_clusters(cairo_t* cr, std::string s);

}
