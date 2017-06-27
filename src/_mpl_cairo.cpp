// Missing methods:
//  - draw_path_collection
//  - draw_quad_mesh
//  - draw_gouraud_triangle{,s}

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

namespace py = pybind11;
using namespace pybind11::literals;

using rgb_t = std::tuple<double, double, double>;
using rgba_t = std::tuple<double, double, double, double>;
using rectangle_t = std::tuple<double, double, double, double>;

namespace matplotlib {
    int const STOP = 0, MOVETO = 1, LINETO = 2, CURVE3 = 3, CURVE4 = 4, CLOSEPOLY = 79;
}

static FT_Library FT_LIB;
static cairo_user_data_key_t const FT_KEY = {0};

// mcr: Matplotlib cairo private functions.
void mcr_load_path(cairo_t* cr, py::object path, py::object transform);
cairo_font_face_t* mcr_ft_font_from_prop(py::object prop);

struct GraphicsContextRenderer {

    cairo_t* cr_;
    double dpi_;
    std::optional<double> alpha_;

    // Not exposed.
    double points_to_pixels(double points);
    rgba_t get_rgba(void);

    GraphicsContextRenderer(double dpi);
    ~GraphicsContextRenderer();

    int get_width(void);
    int get_height(void);
    std::tuple<int, int> get_canvas_width_height(void);

    void set_alpha(std::optional<double> alpha);
    void set_antialiased(py::object aa); // bool, but also np.bool_.
    void set_capstyle(std::string capstyle);
    void set_clip_rectangle(std::optional<py::object> rectangle);
    void set_clip_path(py::object path);
    void set_dashes(
            std::optional<double> dash_offset, std::optional<py::object> dash_list);
    void set_foreground(py::object fg, bool is_rgba);
    void set_joinstyle(std::string js);
    void set_linewidth(double lw);

    rgb_t get_rgb(void);

    void set_ctx_from_surface(py::object surface);
    cairo_surface_t* set_ctx_from_image_args(cairo_format_t format, int width, int height);

    GraphicsContextRenderer& new_gc(void);
    void copy_properties(GraphicsContextRenderer& other);
    void restore(void);

    void draw_image(
            GraphicsContextRenderer& gc,
            double x, double y, py::array_t<uint8_t>);
    void draw_path(
            GraphicsContextRenderer& gc,
            py::object path,
            py::object transform,
            std::optional<py::object> rgb_fc);
    void draw_markers(
            GraphicsContextRenderer& gc,
            py::object marker_path,
            py::object marker_transform,
            py::object path,
            py::object transform,
            std::optional<py::object> rgb_fc);
    void draw_text(
            GraphicsContextRenderer& gc,
            double x, double y, std::string s, py::object prop, double angle,
            bool ismath, py::object mtext);
    std::tuple<double, double, double> get_text_width_height_descent(
            std::string s, py::object prop, bool ismath);
};

void mcr_load_path(cairo_t* cr, py::object path, py::object transform) {
    auto py_matrix = transform.attr("__array__")().cast<py::array_t<double>>();
    auto matrix = cairo_matrix_t{
            *py_matrix.data(0, 0), -*py_matrix.data(1, 0),
            *py_matrix.data(0, 1), -*py_matrix.data(1, 1),
            *py_matrix.data(0, 2), -*py_matrix.data(1, 2)};
    // We cannot use cairo_set_matrix, as it would also affect the linewidth (etc.).
    auto vertices = path.attr("vertices").cast<py::array_t<double>>();
    auto maybe_codes = path.attr("codes");
    auto n = vertices.shape(0);
    if (!maybe_codes.is_none()) {
        auto codes = maybe_codes.cast<py::array_t<int>>();
        for (size_t i = 0; i < n; ++i) {
            auto x0 = *vertices.data(i, 0), y0 = *vertices.data(i, 1);
            cairo_matrix_transform_point(&matrix, &x0, &y0);
            switch (*codes.data(i)) {
                case matplotlib::MOVETO:
                    cairo_move_to(cr, x0, y0);
                    break;
                case matplotlib::LINETO:
                    cairo_line_to(cr, x0, y0);
                    break;
                case matplotlib::CURVE3: {
                    auto x1 = *vertices.data(i, 0), y1 = *vertices.data(i, 1),
                         x2 = *vertices.data(i + 1, 0), y2 = *vertices.data(i + 1, 1);
                    cairo_matrix_transform_point(&matrix, &x1, &y1);
                    cairo_matrix_transform_point(&matrix, &x2, &y2);
                    cairo_get_current_point(cr, &x0, &y0);
                    cairo_curve_to(cr,
                            (x0 + 2 * x1) / 3, (y0 + 2 * y1) / 3,
                            (2 * x1 + x2) / 3, (2 * y1 + y2) / 3,
                            x2, y2);
                    i += 1;
                    break;
                }
                case matplotlib::CURVE4: {
                    auto x1 = *vertices.data(i, 0), y1 = *vertices.data(i, 1),
                         x2 = *vertices.data(i + 1, 0), y2 = *vertices.data(i + 1, 1),
                         x3 = *vertices.data(i + 2, 0), y3 = *vertices.data(i + 2, 1);
                    cairo_matrix_transform_point(&matrix, &x1, &y1);
                    cairo_matrix_transform_point(&matrix, &x2, &y2);
                    cairo_matrix_transform_point(&matrix, &x3, &y3);
                    cairo_curve_to(cr, x1, y1, x2, y2, x3, y3);
                    i += 2;
                    break;
                }
                case matplotlib::CLOSEPOLY:
                    cairo_close_path(cr);
                    break;
            }
        }
    } else {
        auto x = *vertices.data(0, 0), y = *vertices.data(0, 1);
        cairo_matrix_transform_point(&matrix, &x, &y);
        cairo_move_to(cr, x, y);
        for (size_t i = 0; i < n; ++i) {
            auto x = *vertices.data(i, 0), y = *vertices.data(i, 1);
            cairo_matrix_transform_point(&matrix, &x, &y);
            cairo_line_to(cr, x, y);
        }
    }
}

cairo_font_face_t* mcr_ft_font_from_prop(py::object prop) {
    // It is probably not worth implementing an additional layer of caching
    // here as findfont already has its cache and object equality needs would
    // also need to go through Python anyways.
    auto font_path =
        py::module::import("matplotlib.font_manager").attr("findfont")(prop)
        .cast<std::string>();
    FT_Face ft_face;
    if (FT_New_Face(FT_LIB, font_path.c_str(), 0, &ft_face)) {
        throw std::runtime_error("FT_New_Face failed");
    }
    auto font_face = cairo_ft_font_face_create_for_ft_face(ft_face, 0);
    if (cairo_font_face_set_user_data(
                font_face, &FT_KEY, ft_face, (cairo_destroy_func_t)FT_Done_Face)) {
        cairo_font_face_destroy(font_face);
        FT_Done_Face(ft_face);
        throw std::runtime_error("cairo_font_face_set_user_data failed");
    }
    return font_face;
}

GraphicsContextRenderer::GraphicsContextRenderer(double dpi) :
    cr_{nullptr}, dpi_{dpi}, alpha_{{}} {}

GraphicsContextRenderer::~GraphicsContextRenderer() {
    if (cr_) {
        cairo_destroy(cr_);
    }
}

double GraphicsContextRenderer::points_to_pixels(double points) {
    return points * dpi_ / 72;
}

rgba_t GraphicsContextRenderer::get_rgba(void) {
    double r, g, b, a;
    auto status = cairo_pattern_get_rgba(cairo_get_source(cr_), &r, &g, &b, &a);
    if (status != CAIRO_STATUS_SUCCESS) {
        throw std::runtime_error("Could not retrieve color from pattern: "
                + std::string{cairo_status_to_string(status)});
    }
    if (alpha_) {
        a = *alpha_;
    }
    return {r, g, b, a};
}

int GraphicsContextRenderer::get_width(void) {
    if (!cr_) {
        return 0;
    }
    return cairo_image_surface_get_width(cairo_get_target(cr_));
}

int GraphicsContextRenderer::get_height(void) {
    if (!cr_) {
        return 0;
    }
    return cairo_image_surface_get_height(cairo_get_target(cr_));
}

std::tuple<int, int> GraphicsContextRenderer::get_canvas_width_height(void) {
    return {get_width(), get_height()};
}

void GraphicsContextRenderer::set_alpha(std::optional<double> alpha) {
    if (!cr_) {
        return;
    }
    alpha_ = alpha;
    if (!alpha) {
        return;
    }
    auto [r, g, b] = get_rgb();
    cairo_set_source_rgba(cr_, r, g, b, *alpha);
}

void GraphicsContextRenderer::set_antialiased(py::object aa) {
    if (!cr_) {
        return;
    }
    cairo_set_antialias(cr_, aa ? CAIRO_ANTIALIAS_GOOD : CAIRO_ANTIALIAS_NONE);
}

void GraphicsContextRenderer::set_capstyle(std::string capstyle) {
    if (!cr_) {
        return;
    }
    if (capstyle == "butt") {
        cairo_set_line_cap(cr_, CAIRO_LINE_CAP_BUTT);
    } else if (capstyle == "round") {
        cairo_set_line_cap(cr_, CAIRO_LINE_CAP_ROUND);
    } else if (capstyle == "projecting") {
        cairo_set_line_cap(cr_, CAIRO_LINE_CAP_SQUARE);
    } else {
        throw std::invalid_argument("Invalid capstyle: " + capstyle);
    }
}

void GraphicsContextRenderer::set_clip_rectangle(std::optional<py::object> rectangle) {
    if (!cr_ || !rectangle) {
        return;
    }
    auto [x, y, w, h] = rectangle->attr("bounds").cast<rectangle_t>();
    cairo_new_path(cr_);
    cairo_rectangle(cr_, x, get_height() - h - y, w, h);
    cairo_clip(cr_);
}

void GraphicsContextRenderer::set_clip_path(py::object path) {
    // FIXME: Not implemented.
}

void GraphicsContextRenderer::set_dashes(
        std::optional<double> dash_offset, std::optional<py::object> dash_list) {
    if (dash_list) {
        if (!dash_offset) {
            throw std::invalid_argument("Missing offset");
        }
        std::vector<double> v;
        for (auto e: dash_list->cast<py::iterable>()) {
            v.push_back(e.cast<double>());
        }
        cairo_set_dash(cr_, v.data(), v.size(), *dash_offset);
    } else {
        cairo_set_dash(cr_, nullptr, 0, 0);
    }
}

void GraphicsContextRenderer::set_foreground(py::object fg, bool is_rgba) {
    auto [r, g, b, a] = py::module::import("matplotlib.colors").attr("to_rgba")(fg)
        .cast<rgba_t>();
    if (is_rgba) {
        *alpha_ = a;
        cairo_set_source_rgba(cr_, r, g, b, a);
    } else {
        cairo_set_source_rgb(cr_, r, g, b);
    }
}

void GraphicsContextRenderer::set_joinstyle(std::string joinstyle) {
    if (!cr_) {
        return;
    }
    if (joinstyle == "miter") {
        cairo_set_line_join(cr_, CAIRO_LINE_JOIN_MITER);
    } else if (joinstyle == "round") {
        cairo_set_line_join(cr_, CAIRO_LINE_JOIN_ROUND);
    } else if (joinstyle == "bevel") {
        cairo_set_line_join(cr_, CAIRO_LINE_JOIN_BEVEL);
    } else {
        throw std::invalid_argument("Invalid joinstyle: " + joinstyle);
    }
}

void GraphicsContextRenderer::set_linewidth(double lw) {
    if (!cr_) {
        return;
    }
    cairo_set_line_width(cr_, points_to_pixels(lw));
}

rgb_t GraphicsContextRenderer::get_rgb(void) {
    auto [r, g, b, a] = get_rgba();
    return {r, g, b};
}

void GraphicsContextRenderer::set_ctx_from_surface(py::object py_surface) {
    if (cr_) {
        cairo_destroy(cr_);
    }
    if (py_surface.attr("__module__").cast<std::string>() == "cairocffi.surfaces") {
        auto surface = reinterpret_cast<cairo_surface_t*>(
                py::module::import("cairocffi").attr("ffi").attr("cast")(
                    "int", py_surface.attr("_pointer")).cast<uintptr_t>());
        cr_ = cairo_create(surface);
    } else {
        throw std::invalid_argument("Could not convert argument to cairo_surface_t*");
    }
}

cairo_surface_t* GraphicsContextRenderer::set_ctx_from_image_args(
        cairo_format_t format, int width, int height) {
    // NOTE This API will ultimately be favored over set_ctx_from_surface as
    // it bypasses the need to construct a surface in the Python-level using
    // yet another cairo wrapper (in particular, cairocffi (which relies on
    // dlopen() prevents the use of cairo-trace (which relies on LD_PRELOAD).
    // Python client code should ultimately look like
    //     surface_ptr = self._renderer.set_ctx_from_image_args(
    //         backend_cairo.cairo.FORMAT_ARGB32, width, height)
    //     surface = backend_cairo.cairo.Surface(
    //         backend_cairo.cairo.ffi.cast("cairo_surface_t*", surface_ptr))
    //     surface.__class__ = backend_cairo.cairo.ImageSurface
    // where the last line ensures that the surface ultimately gets destroyed
    // (or we can provide our own GC solution for that, e.g. by returning a
    // proxy).
    if (cr_) {
        cairo_destroy(cr_);
    }
    auto surface = cairo_image_surface_create(format, width, height);
    cr_ = cairo_create(surface);
    return surface;
}

GraphicsContextRenderer& GraphicsContextRenderer::new_gc(void) {
    if (!cr_) {
        return *this;
    }
    cairo_reference(cr_);
    cairo_save(cr_);
    return *this;
}

void GraphicsContextRenderer::copy_properties(GraphicsContextRenderer& other) {
    if (cr_) {
        cairo_destroy(cr_);
    }
    cr_ = other.cr_;
    dpi_ = other.dpi_;
    alpha_ = other.alpha_;
    if (!cr_) {
        return;
    }
    cairo_reference(cr_);
    cairo_save(cr_);
}

void GraphicsContextRenderer::restore(void) {
    if (!cr_) {
        return;
    }
    cairo_restore(cr_);
}

void GraphicsContextRenderer::draw_image(
        GraphicsContextRenderer& gc, double x, double y, py::array_t<uint8_t> im) {
    if (!cr_) {
        return;
    }
    if (&gc != this) {
        throw std::invalid_argument("Non-matching GraphicsContext");
    }
    cairo_save(cr_);
    auto im_raw = im.unchecked<3>();
    auto ni = im_raw.shape(0), nj = im_raw.shape(1);
    if (im_raw.shape(2) != 4) {
        throw std::invalid_argument("RGBA array must have size (m, n, 4)");
    }
    auto stride = cairo_format_stride_for_width(CAIRO_FORMAT_ARGB32, nj);
    auto buf = std::make_unique<uint8_t[]>(ni * stride);
    if (alpha_) {
        for (size_t i = 0; i < ni; ++i) {
            auto ptr = reinterpret_cast<uint32_t*>(buf.get() + i * stride);
            for (size_t j = 0; j < nj; ++j) {
                auto r = *im_raw.data(i, j, 0), g = *im_raw.data(i, j, 1),
                     b = *im_raw.data(i, j, 2), a = *im_raw.data(i, j, 3);
                *(ptr++) =
                    (uint8_t(*alpha_ * 0xff) << 24) + (uint8_t(*alpha_ * r) << 16)
                    + (uint8_t(*alpha_ * g) << 8) + (uint8_t(*alpha_ * b));
            }
        }
    } else {
        for (size_t i = 0; i < ni; ++i) {
            auto ptr = reinterpret_cast<uint32_t*>(buf.get() + i * stride);
            for (size_t j = 0; j < nj; ++j) {
                auto r = *im_raw.data(i, j, 0), g = *im_raw.data(i, j, 1),
                     b = *im_raw.data(i, j, 2), a = *im_raw.data(i, j, 3);
                *(ptr++) =
                    (a << 24) + (uint8_t(a / 255. * r) << 16)
                    + (uint8_t(a / 255. * g) << 8) + (uint8_t(a / 255. * b));
            }
        }
    }
    auto surface = cairo_image_surface_create_for_data(
            buf.get(), CAIRO_FORMAT_ARGB32, nj, ni, stride);
    auto pattern = cairo_pattern_create_for_surface(surface);
    cairo_matrix_t matrix{1, 0, 0, -1, -x, -y + get_height()};
    cairo_pattern_set_matrix(pattern, &matrix);
    cairo_set_source(cr_, pattern);
    cairo_paint(cr_);
    cairo_pattern_destroy(pattern);
    cairo_surface_destroy(surface);
    cairo_restore(cr_);
}

void GraphicsContextRenderer::draw_path(
        GraphicsContextRenderer& gc,
        py::object path,
        py::object transform,
        std::optional<py::object> rgb_fc) {
    if (!cr_) {
        return;
    }
    if (&gc != this) {
        throw std::invalid_argument("Non-matching GraphicsContext");
    }
    cairo_save(cr_);
    cairo_translate(cr_, 0, get_height());
    mcr_load_path(cr_, path, transform);
    if (rgb_fc) {
        cairo_save(cr_);
        double r, g, b, a{1};
        if (py::len(*rgb_fc) == 3) {
            std::tie(r, g, b) = rgb_fc->cast<rgb_t>();
        } else {
            std::tie(r, g, b, a) = rgb_fc->cast<rgba_t>();
        }
        if (alpha_) {
            a = *alpha_;
        }
        cairo_set_source_rgba(cr_, r, g, b, a);
        cairo_fill_preserve(cr_);
        cairo_restore(cr_);
    }
    cairo_stroke(cr_);
    cairo_restore(cr_);
}

void GraphicsContextRenderer::draw_markers(
        GraphicsContextRenderer& gc,
        py::object marker_path,
        py::object marker_transform,
        py::object path,
        py::object transform,
        std::optional<py::object> rgb_fc) {
    // FIXME: It would be preferable to rasterize to an intermediate surface,
    // but that would require copying the whole context.  Using groups is not
    // an option as the markers will often get clipped out and the clip path
    // cannot be reset temporarily.
    if (!cr_) {
        return;
    }
    if (&gc != this) {
        throw std::invalid_argument("Non-matching GraphicsContext");
    }
    auto py_matrix = transform.attr("__array__")().cast<py::array_t<double>>();
    auto matrix = cairo_matrix_t{
            *py_matrix.data(0, 0), -*py_matrix.data(1, 0),
            *py_matrix.data(0, 1), -*py_matrix.data(1, 1),
            *py_matrix.data(0, 2), -*py_matrix.data(1, 2) + get_height()};
    // We cannot use cairo_set_matrix, as it would also affect the linewidth (etc.).
    auto vertices = path.attr("vertices").cast<py::array_t<double>>();
    // NOTE: For efficiency, we ignore codes, which is the documented behavior
    // even though not the actual one of other backends.
    auto n = vertices.shape(0);
    for (size_t i = 0; i < n; ++i) {
        auto x = *vertices.data(i, 0), y = *vertices.data(i, 1);
        cairo_matrix_transform_point(&matrix, &x, &y);
        cairo_save(cr_);
        cairo_translate(cr_, x, y);
        mcr_load_path(cr_, marker_path, marker_transform);
        if (rgb_fc) {
            cairo_save(cr_);
            double r, g, b, a{1};
            if (py::len(*rgb_fc) == 3) {
                std::tie(r, g, b) = rgb_fc->cast<rgb_t>();
            } else {
                std::tie(r, g, b, a) = rgb_fc->cast<rgba_t>();
            }
            if (alpha_) {
                a = *alpha_;
            }
            cairo_set_source_rgba(cr_, r, g, b, a);
            cairo_fill_preserve(cr_);
            cairo_restore(cr_);
        }
        cairo_stroke(cr_);
        cairo_restore(cr_);
    }
}

void GraphicsContextRenderer::draw_text(
        GraphicsContextRenderer& gc,
        double x, double y, std::string s, py::object prop, double angle,
        bool ismath, py::object mtext) {
    if (!cr_) {
        return;
    }
    if (&gc != this) {
        throw std::invalid_argument("Non-matching GraphicsContext");
    }
    cairo_save(cr_);
    if (ismath) {
        // FIXME: If angle == 0, we need to round x and y to avoid additional
        // aliasing on top of the one already provided by freetype.  Perhaps
        // we should let it know about the destination subpixel position?  If
        // angle != 0, all hope is lost anyways.
        if (angle) {
            cairo_translate(cr_, x, y);
            cairo_rotate(cr_, -angle * M_PI / 180);
        } else {
            cairo_translate(cr_, round(x), round(y));
        }
        auto [ox, oy, width, height, descent, image, chars] =
            py::cast(this).attr("mathtext_parser").attr("parse")(s, dpi_, prop)
            .cast<std::tuple<double, double, double, double, double,
                             py::object, py::object>>();
        auto im_raw = py::array_t<uint8_t, py::array::c_style>{image}.mutable_unchecked<2>();
        auto ni = im_raw.shape(0), nj = im_raw.shape(1);
        // Recompute the colors.  Trying to use an A8 image seems just as
        // complicated (http://cairo.cairographics.narkive.com/ijgxr19T/alpha-masks).
        auto stride = cairo_format_stride_for_width(CAIRO_FORMAT_ARGB32, nj);
        auto pix = im_raw.data(0, 0);
        auto buf = std::make_unique<uint8_t[]>(ni * stride);
        auto [r, g, b, a] = get_rgba();
        for (size_t i = 0; i < ni; ++i) {
            auto ptr = reinterpret_cast<uint32_t*>(buf.get() + i * stride);
            for (size_t j = 0; j < nj; ++j) {
                auto val = *(pix++);
                *(ptr++) =
                    (uint8_t(a * val) << 24) + (uint8_t(a * val * r) << 16)
                    + (uint8_t(a * val * g) << 8) + (uint8_t(a * val * b));
            }
        }
        auto surface = cairo_image_surface_create_for_data(
                buf.get(), CAIRO_FORMAT_ARGB32, nj, ni, stride);
        auto pattern = cairo_pattern_create_for_surface(surface);
        cairo_matrix_t matrix{1, 0, 0, 1, -ox, ni - oy};
        cairo_pattern_set_matrix(pattern, &matrix);
        cairo_set_source(cr_, pattern);
        cairo_paint(cr_);
        cairo_pattern_destroy(pattern);
        cairo_surface_destroy(surface);
    } else {
        // Need to set the current point (otherwise later texts will just
        // follow, regardless of cairo_translate).
        cairo_translate(cr_, x, y);
        cairo_rotate(cr_, -angle * M_PI / 180);
        cairo_move_to(cr_, 0, 0);
        auto font_face = mcr_ft_font_from_prop(prop);
        cairo_set_font_face(cr_, font_face);
        cairo_set_font_size(
                cr_,
                points_to_pixels(prop.attr("get_size_in_points")().cast<double>()));
        cairo_show_text(cr_, s.c_str());
        cairo_font_face_destroy(font_face);
    }
    cairo_restore(cr_);
}

std::tuple<double, double, double> GraphicsContextRenderer::get_text_width_height_descent(
        std::string s, py::object prop, bool ismath) {
    if (ismath) {
        auto [ox, oy, width, height, descent, image, chars] =
            py::cast(this).attr("mathtext_parser").attr("parse")(s, dpi_, prop)
            .cast<std::tuple<double, double, double, double, double,
                             py::object, py::object>>();
        return {width, height, descent};
    } else {
        cairo_save(cr_);
        auto font_face = mcr_ft_font_from_prop(prop);
        cairo_set_font_face(cr_, font_face);
        cairo_set_font_size(
                cr_,
                points_to_pixels(prop.attr("get_size_in_points")().cast<double>()));
        cairo_text_extents_t extents;
        cairo_text_extents(cr_, s.c_str(), &extents);
        cairo_font_face_destroy(font_face);
        cairo_restore(cr_);
        return {extents.width, extents.height, extents.height + extents.y_bearing};
    }
}

PYBIND11_PLUGIN(_mpl_cairo) {
    py::module m("_mpl_cairo", "A cairo backend for matplotlib.");

    if (FT_Init_FreeType(&FT_LIB)) {
        throw std::runtime_error("FT_Init_FreeType failed");
    }
    auto clean_ft_lib = py::capsule(
            []() {
                if (FT_Done_FreeType(FT_LIB)) {
                    throw std::runtime_error("FT_Done_FreeType failed");
                }
            ;});
    m.add_object("_cleanup", clean_ft_lib);

    py::class_<GraphicsContextRenderer>(m, "GraphicsContextRendererCairo")

        .def(py::init<double>())

        .def_property_readonly("width", &GraphicsContextRenderer::get_width)
        .def_property_readonly("height", &GraphicsContextRenderer::get_height)
        .def("get_canvas_width_height", &GraphicsContextRenderer::get_canvas_width_height)

        .def("set_alpha", &GraphicsContextRenderer::set_alpha)
        .def("set_antialiased", &GraphicsContextRenderer::set_antialiased)
        .def("set_capstyle", &GraphicsContextRenderer::set_capstyle)
        .def("set_clip_rectangle", &GraphicsContextRenderer::set_clip_rectangle)
        .def("set_clip_path", &GraphicsContextRenderer::set_clip_path)
        .def("set_dashes", &GraphicsContextRenderer::set_dashes)
        .def("set_foreground", &GraphicsContextRenderer::set_foreground,
                "fg"_a, "isRGBA"_a=false)
        .def("set_joinstyle", &GraphicsContextRenderer::set_joinstyle)
        .def("set_linewidth", &GraphicsContextRenderer::set_linewidth)

        .def("get_rgb", &GraphicsContextRenderer::get_rgb)

        .def("new_gc", &GraphicsContextRenderer::new_gc)
        .def("copy_properties", &GraphicsContextRenderer::copy_properties)
        .def("restore", &GraphicsContextRenderer::restore)

        .def("set_ctx_from_surface", &GraphicsContextRenderer::set_ctx_from_surface)
        .def("set_ctx_from_image_args",
                [](GraphicsContextRenderer& self,
                    int format, int width, int height) {
                return reinterpret_cast<uintptr_t>(
                        self.set_ctx_from_image_args(
                            cairo_format_t(format), width, height)); })

        .def("draw_image", &GraphicsContextRenderer::draw_image)
        .def("draw_path", &GraphicsContextRenderer::draw_path,
                "gc"_a, "path"_a, "transform"_a, "rgbFace"_a=nullptr)
        .def("draw_markers", &GraphicsContextRenderer::draw_markers,
                "gc"_a, "marker_path"_a, "marker_trans"_a, "path"_a, "trans"_a,
                "rgbFace"_a=nullptr)
        .def("draw_text", &GraphicsContextRenderer::draw_text,
                "gc"_a, "x"_a, "y"_a, "s"_a, "prop"_a, "angle"_a,
                "ismath"_a=false, "mtext"_a=nullptr)
        .def("get_text_width_height_descent",
                &GraphicsContextRenderer::get_text_width_height_descent,
                "s"_a, "prop"_a, "ismath"_a);

    return m.ptr();
}
