#pragma once

#include "_util.h"

namespace mplcairo {

namespace py = pybind11;

enum class StreamSurfaceType {
  PDF, PS, EPS, SVG, Script
};

struct Region {
  cairo_rectangle_int_t bbox;
  std::unique_ptr<uint8_t[]> buf;

  Region(cairo_rectangle_int_t bbox, std::unique_ptr<uint8_t[]> buf);
};

class GraphicsContextRenderer {
  class AdditionalContext {
    GraphicsContextRenderer* gcr_;

    public:
    AdditionalContext(GraphicsContextRenderer* gcr);
    ~AdditionalContext();
    AdditionalContext(const AdditionalContext& other) = delete;
    AdditionalContext(AdditionalContext&& other) = delete;
    AdditionalContext operator=(const AdditionalContext& other) = delete;
    AdditionalContext operator=(AdditionalContext&& other) = delete;
  };

  double pixels_to_points(double pixels);
  rgba_t get_rgba();
  AdditionalContext additional_context();

  public:
  cairo_t* const cr_;
  py::object mathtext_parser_;
  py::object texmanager_;
  py::object text2path_;

  GraphicsContextRenderer(
    cairo_t* cr, double width, double height, double dpi);
  ~GraphicsContextRenderer();

  static cairo_t* cr_from_image_args(int width, int height);
  GraphicsContextRenderer(double width, double height, double dpi);
  static cairo_t* cr_from_pycairo_ctx(py::object ctx);
  GraphicsContextRenderer(py::object ctx, double dpi);
  static cairo_t* cr_from_fileformat_args(
    StreamSurfaceType type, py::object file,
    double width, double height, double dpi);
  GraphicsContextRenderer(
    StreamSurfaceType type, py::object file,
    double width, double height, double dpi);

  static GraphicsContextRenderer make_pattern_gcr(cairo_surface_t* cr);

  void _set_metadata(std::optional<py::dict> metadata);
  void _set_size(double width, double height, double dpi);
  void _show_page();
  py::array_t<uint8_t> _get_buffer();
  void _finish();

  void set_alpha(std::optional<double> alpha);
  void set_antialiased(std::variant<cairo_antialias_t, bool> aa);
  void set_capstyle(std::string capstyle);
  void set_clip_rectangle(std::optional<py::object> rectangle);
  void set_clip_path(std::optional<py::object> transformed_path);
  void set_dashes(
    std::optional<double> dash_offset,
    std::optional<py::array_t<double>> dash_list);
  void set_foreground(py::object fg, bool is_rgba=false);
  void set_hatch(std::optional<std::string> hatch);
  void set_hatch_color(py::object hatch);
  void set_joinstyle(std::string js);
  void set_linewidth(double lw);
  void set_snap(std::optional<bool> snap);
  void set_url(std::optional<std::string> url);

  AdditionalState const& get_additional_state() const;
  AdditionalState& get_additional_state();
  double get_linewidth();
  rgb_t get_rgb();

  GraphicsContextRenderer& new_gc();
  void copy_properties(GraphicsContextRenderer* other);
  void restore();

  double points_to_pixels(double points);

  void draw_gouraud_triangles(
    GraphicsContextRenderer& gc,
    py::array_t<double> triangles,
    py::array_t<double> colors,
    py::object transform);
  void draw_image(
    GraphicsContextRenderer& gc,
    double x, double y, py::array_t<uint8_t> im);
  void draw_markers(
    GraphicsContextRenderer& gc,
    py::object marker_path,
    py::object marker_transform,
    py::object path,
    py::object transform,
    std::optional<py::object> fc);
  void draw_path(
    GraphicsContextRenderer& gc,
    py::object path,
    py::object transform,
    std::optional<py::object> fc);
  void draw_path_collection(
    GraphicsContextRenderer& gc,
    py::object master_transform,
    std::vector<py::object> paths,
    std::vector<py::object> all_transforms,
    py::array_t<double> offsets,
    py::object offset_transform,
    py::object fcs,
    py::object ecs,
    py::array_t<double> lws,
    std::vector<std::tuple<std::optional<double>,
                           std::optional<py::array_t<double>>>> dashes,
    py::object aas,
    py::object urls,
    std::string offset_position);
  void draw_quad_mesh(
    GraphicsContextRenderer& gc,
    py::object master_transform,
    ssize_t mesh_width, ssize_t mesh_height,
    py::array_t<double> coordinates,
    py::array_t<double> offsets,
    py::object offset_transform,
    py::array_t<double> fcs,
    py::object aas,
    py::array_t<double> ecs);
  void draw_text(
    GraphicsContextRenderer& gc,
    double x, double y, std::string s, py::object prop, double angle,
    bool ismath, py::object mtext);
  std::tuple<double, double, double> get_text_width_height_descent(
    std::string s, py::object prop, py::object ismath);

  void start_filter();
  py::array_t<uint8_t> _stop_filter_get_buffer();

  Region copy_from_bbox(py::object bbox);
  void restore_region(Region& region);
};

class MathtextBackend {
  struct Glyph {
    // NOTE: It may be more efficient to hold onto an array of FT_Glyphs but
    // that will wait for the ft2 rewrite in Matplotlib itself.
    std::string path;
    double size;
    unsigned long index;
    double x, y;

    Glyph(
      std::string path, double size, unsigned long index,
      double x, double y);
  };

  std::vector<Glyph> glyphs_;
  std::vector<rectangle_t> rectangles_;
  double bearing_y_;
  double xmin_, ymin_, xmax_, ymax_;

  public:
  MathtextBackend();
  void set_canvas_size(double width, double height, double depth);
  void render_glyph(double ox, double oy, py::object info);
  void _render_usetex_glyph(
    double ox, double oy, std::string filename, double size,
    unsigned long index);
  void render_rect_filled(double x1, double y1, double x2, double y2);
  // FIXME[matplotlib]: The base class fails to document the second argument.
  MathtextBackend& get_results(py::object box, py::object used_characters);
  void _draw(
    GraphicsContextRenderer& gcr, double x, double y, double angle) const;
  std::tuple<double, double, double> get_text_width_height_descent() const;
};

}
