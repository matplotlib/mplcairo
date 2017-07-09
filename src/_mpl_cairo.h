#pragma once

#include <cmath>
#include <tuple>
#include <vector>

#include <cairo/cairo.h>
#include <cairo/cairo-ft.h>

#include <pybind11/pybind11.h>
#include <pybind11/eval.h>
#include <pybind11/numpy.h>
#include <pybind11/stl.h>

#include "_util.h"

namespace mpl_cairo {

namespace py = pybind11;

struct Region {
  cairo_rectangle_int_t bbox;
  std::shared_ptr<char[]> buf;

  Region(cairo_rectangle_int_t bbox, std::shared_ptr<char[]> buf);
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

  cairo_t* cr_;

  private:
  double pixels_to_points(double pixels);
  rgba_t get_rgba();
  AdditionalContext additional_context();

  public:
  double dpi_;
  py::object mathtext_parser_;
  py::object text2path_;

  GraphicsContextRenderer(double dpi);
  ~GraphicsContextRenderer();

  void set_ctx_from_surface(py::object surface);
  void set_ctx_from_image_args(cairo_format_t format, int width, int height);
  uintptr_t get_data_address();

  void set_alpha(std::optional<double> alpha);
  void set_antialiased(cairo_antialias_t aa);
  void set_antialiased(py::object aa); // bool, but also np.bool_.
  void set_capstyle(std::string capstyle);
  void set_clip_rectangle(std::optional<py::object> rectangle);
  void set_clip_path(std::optional<py::object> transformed_path);
  void set_dashes(
      std::optional<double> dash_offset,
      std::optional<std::vector<double>> dash_list);
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

  std::tuple<int, int> get_canvas_width_height();
  int get_width();
  int get_height();
  double points_to_pixels(double points);

  void draw_gouraud_triangles(
      GraphicsContextRenderer& gc,
      py::array_t<double> triangles,
      py::array_t<double> colors,
      py::object transform);
  void draw_image(
      GraphicsContextRenderer& gc,
      double x, double y, py::array_t<uint8_t>);
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
      std::vector<py::object> offsets,
      py::object offset_transform,
      py::object fcs,
      py::object ecs,
      std::vector<double> lws,
      std::vector<std::tuple<std::optional<double>,
                             std::optional<std::vector<double>>>> dashes,
      py::object aas,
      py::object urls,
      std::string offset_position);
  void draw_text(
      GraphicsContextRenderer& gc,
      double x, double y, std::string s, py::object prop, double angle,
      bool ismath, py::object mtext);
  std::tuple<double, double, double> get_text_width_height_descent(
      std::string s, py::object prop, py::object ismath);

  Region copy_from_bbox(py::object bbox);
  void restore_region(Region& region);
};

}
