#pragma once

#include <cmath>
#include <tuple>
#include <vector>

#include <cairo/cairo.h>

#include <pybind11/pybind11.h>
#include <pybind11/eval.h>
#include <pybind11/numpy.h>
#include <pybind11/stl.h>

#include "_util.h"

namespace mpl_cairo {

namespace py = pybind11;

struct Region {
  cairo_rectangle_int_t bbox;
  std::shared_ptr<uint8_t[]> buf;

  Region(cairo_rectangle_int_t bbox, std::shared_ptr<uint8_t[]> buf);
};

class GraphicsContextRenderer {
  // Store the additional state that needs to be pushed/popped as user data on
  // the cairo_t* to tie their lifetimes.
  struct AdditionalState {
    std::optional<double> alpha;
    std::optional<rectangle_t> clip_rectangle;
    std::shared_ptr<cairo_path_t> clip_path;
    std::optional<std::string> hatch;
    rgba_t hatch_color;
    double hatch_linewidth;
    py::object sketch;
  };

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

  cairo_t* const cr_;

  double pixels_to_points(double pixels);
  rgba_t get_rgba();
  AdditionalContext additional_context();

  public:
  // Extents cannot be easily recovered from PDF/SVG surfaces, so record them.
  int const width_, height_;
  double dpi_;
  py::object mathtext_parser_;
  py::object texmanager_;
  py::object text2path_;

  GraphicsContextRenderer(cairo_t* cr, int width, int height, double dpi);
  ~GraphicsContextRenderer();

  static cairo_t* cr_from_image_args(double width, double height);
  GraphicsContextRenderer(double width, double height, double dpi);
  static cairo_t* cr_from_pycairo_ctx(py::object ctx);
  GraphicsContextRenderer(py::object ctx, double dpi);
  static cairo_t* cr_from_fileformat_args(
      cairo_surface_type_t type, py::object file,
      double width, double height, double dpi);
  GraphicsContextRenderer(
      cairo_surface_type_t type, py::object file,
      double width, double height, double dpi);

  void _set_eps(bool eps);
  py::array_t<uint8_t> _get_buffer();
  void _finish();

  void set_alpha(std::optional<double> alpha);
  void set_antialiased(cairo_antialias_t aa);
  void set_antialiased(py::object aa); // bool, but also np.bool_.
  void set_capstyle(std::string capstyle);
  void set_clip_rectangle(std::optional<py::object> rectangle);
  void set_clip_path(std::optional<py::object> transformed_path);
  void set_dashes(
      std::optional<double> dash_offset,
      std::optional<py::array_t<double>> dash_list);
  void set_foreground(py::object fg, bool /* is_rgba */=false);
  void set_hatch(std::optional<std::string> hatch);
  void set_hatch_color(py::object hatch);
  void set_joinstyle(std::string js);
  void set_linewidth(double lw);

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
      size_t mesh_width, size_t mesh_height,
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
  py::array_t<uint8_t> _stop_filter();

  Region copy_from_bbox(py::object bbox);
  void restore_region(Region& region);
};

}
