#pragma once

#include <cmath>
#include <tuple>
#include <vector>

#include <cairo/cairo.h>
#include <cairo/cairo-ft.h>
#include <freetype2/ft2build.h>
#include FT_FREETYPE_H

#include <pybind11/pybind11.h>
#include <pybind11/eval.h>
#include <pybind11/numpy.h>
#include <pybind11/stl.h>

namespace mpl_cairo {

namespace py = pybind11;
using rgb_t = std::tuple<double, double, double>;
using rgba_t = std::tuple<double, double, double, double>;
using rectangle_t = std::tuple<double, double, double, double>;

enum class PathCode {
  STOP = 0, MOVETO = 1, LINETO = 2, CURVE3 = 3, CURVE4 = 4, CLOSEPOLY = 79
};

cairo_matrix_t matrix_from_transform(py::object transform, double y0=0);
cairo_matrix_t matrix_from_transform(
    py::object transform, cairo_matrix_t* master_matrix);
void copy_for_marker_stamping(cairo_t* orig, cairo_t* dest);
void load_path(cairo_t* cr, py::object path, cairo_matrix_t* matrix);
cairo_font_face_t* ft_font_from_prop(py::object prop);

struct Region {
  cairo_rectangle_int_t bbox;
  std::shared_ptr<char[]> buf;

  Region(cairo_rectangle_int_t bbox, std::shared_ptr<char[]> buf);
};

struct GraphicsContextRenderer {
  cairo_t* cr_;
  double dpi_;
  std::optional<double> alpha_;
  py::object mathtext_parser_;
  py::object text2path_;

  private:
  double points_to_pixels(double points);
  double pixels_to_points(double pixels);
  rgba_t get_rgba(void);
  bool try_draw_circles(
      GraphicsContextRenderer& gc,
      py::object marker_path,
      cairo_matrix_t* marker_matrix,
      py::object path,
      cairo_matrix_t* matrix,
      std::optional<py::object> rgb_fc);

  public:
  GraphicsContextRenderer(double dpi);
  ~GraphicsContextRenderer();

  void set_ctx_from_surface(py::object surface);
  void set_ctx_from_image_args(int format, int width, int height);
  uintptr_t get_data_address();

  void set_alpha(std::optional<double> alpha);
  void set_antialiased(cairo_antialias_t aa);
  void set_antialiased(py::object aa); // bool, but also np.bool_.
  void set_capstyle(std::string capstyle);
  void set_clip_rectangle(std::optional<py::object> rectangle);
  void set_clip_path(std::optional<py::object> transformed_path);
  void set_dashes(
      std::optional<double> dash_offset, std::optional<py::object> dash_list);
  void set_foreground(py::object fg, bool /* is_rgba */=false);
  void set_joinstyle(std::string js);
  void set_linewidth(double lw);

  double get_linewidth();
  rgb_t get_rgb(void);

  GraphicsContextRenderer& new_gc(void);
  void copy_properties(GraphicsContextRenderer& other);
  void restore(void);

  std::tuple<int, int> get_canvas_width_height(void);
  int get_width(void);
  int get_height(void);

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
      std::optional<py::object> rgb_fc);
  void draw_path(
      GraphicsContextRenderer& gc,
      py::object path,
      py::object transform,
      std::optional<py::object> rgb_fc);
  void draw_path_collection(
      GraphicsContextRenderer& gc,
      py::object master_transform,
      std::vector<py::object> paths,
      std::vector<py::object> all_transforms,
      std::vector<py::object> offsets,
      py::object offset_transform,
      std::vector<py::object> fcs,
      std::vector<py::object> ecs,
      std::vector<py::object> lws,
      std::vector<py::object> dashes,
      std::vector<py::object> aas,
      std::vector<py::object> urls,
      std::string offset_position);
  void draw_text(
      GraphicsContextRenderer& gc,
      double x, double y, std::string s, py::object prop, double angle,
      bool ismath, py::object mtext);
  std::tuple<double, double, double> get_text_width_height_descent(
      std::string s, py::object prop, bool ismath);

  Region copy_from_bbox(py::object bbox);
  void restore_region(Region& region);
};

}
