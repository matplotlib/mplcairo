#include "_mplcairo.h"

#include "_os.h"
#include "_pattern_cache.h"
#include "_raqm.h"
#include "_util.h"

#include <py3cairo.h>
#include <cairo-script.h>

#include <stack>
#include <thread>

#include "_macros.h"

P11X_DECLARE_ENUM(
  "antialias_t", "enum.Enum",
  {"DEFAULT", CAIRO_ANTIALIAS_DEFAULT},
  {"NONE", CAIRO_ANTIALIAS_NONE},
  {"GRAY", CAIRO_ANTIALIAS_GRAY},
  {"SUBPIXEL", CAIRO_ANTIALIAS_SUBPIXEL},
  {"FAST", CAIRO_ANTIALIAS_FAST},
  {"GOOD", CAIRO_ANTIALIAS_GOOD},
  {"BEST", CAIRO_ANTIALIAS_BEST}
)
P11X_DECLARE_ENUM(
  "operator_t", "enum.Enum",
  {"CLEAR", CAIRO_OPERATOR_CLEAR},
  {"SOURCE", CAIRO_OPERATOR_SOURCE},
  {"OVER", CAIRO_OPERATOR_OVER},
  {"IN", CAIRO_OPERATOR_IN},
  {"OUT", CAIRO_OPERATOR_OUT},
  {"ATOP", CAIRO_OPERATOR_ATOP},
  {"DEST", CAIRO_OPERATOR_DEST},
  {"DEST_OVER", CAIRO_OPERATOR_DEST_OVER},
  {"DEST_IN", CAIRO_OPERATOR_DEST_IN},
  {"DEST_OUT", CAIRO_OPERATOR_DEST_OUT},
  {"DEST_ATOP", CAIRO_OPERATOR_DEST_ATOP},
  {"XOR", CAIRO_OPERATOR_XOR},
  {"ADD", CAIRO_OPERATOR_ADD},
  {"SATURATE", CAIRO_OPERATOR_SATURATE},
  {"MULTIPLY", CAIRO_OPERATOR_MULTIPLY},
  {"SCREEN", CAIRO_OPERATOR_SCREEN},
  {"OVERLAY", CAIRO_OPERATOR_OVERLAY},
  {"DARKEN", CAIRO_OPERATOR_DARKEN},
  {"LIGHTEN", CAIRO_OPERATOR_LIGHTEN},
  {"COLOR_DODGE", CAIRO_OPERATOR_COLOR_DODGE},
  {"COLOR_BURN", CAIRO_OPERATOR_COLOR_BURN},
  {"HARD_LIGHT", CAIRO_OPERATOR_HARD_LIGHT},
  {"SOFT_LIGHT", CAIRO_OPERATOR_SOFT_LIGHT},
  {"DIFFERENCE", CAIRO_OPERATOR_DIFFERENCE},
  {"EXCLUSION", CAIRO_OPERATOR_EXCLUSION},
  {"HSL_HUE", CAIRO_OPERATOR_HSL_HUE},
  {"HSL_SATURATION", CAIRO_OPERATOR_HSL_SATURATION},
  {"HSL_COLOR", CAIRO_OPERATOR_HSL_COLOR},
  {"HSL_LUMINOSITY", CAIRO_OPERATOR_HSL_LUMINOSITY}
)
P11X_DECLARE_ENUM(  // Only for error messages.
  "_format_t", "enum.Enum",
  {"INVALID", CAIRO_FORMAT_INVALID},
  {"ARGB32", CAIRO_FORMAT_ARGB32},
  {"RGB24", CAIRO_FORMAT_RGB24},
  {"A8", CAIRO_FORMAT_A8},
  {"A1", CAIRO_FORMAT_A1},
  {"RGB16_565", CAIRO_FORMAT_RGB16_565},
  {"RGB30", CAIRO_FORMAT_RGB30}
)
P11X_DECLARE_ENUM(  // Only for error messages.
  "_surface_type_t", "enum.Enum",
  {"IMAGE", CAIRO_SURFACE_TYPE_IMAGE},
  {"PDF", CAIRO_SURFACE_TYPE_PDF},
  {"PS", CAIRO_SURFACE_TYPE_PS},
  {"XLIB", CAIRO_SURFACE_TYPE_XLIB},
  {"XCB", CAIRO_SURFACE_TYPE_XCB},
  {"GLITZ", CAIRO_SURFACE_TYPE_GLITZ},
  {"QUARTZ", CAIRO_SURFACE_TYPE_QUARTZ},
  {"WIN32", CAIRO_SURFACE_TYPE_WIN32},
  {"BEOS", CAIRO_SURFACE_TYPE_BEOS},
  {"DIRECTFB", CAIRO_SURFACE_TYPE_DIRECTFB},
  {"SVG", CAIRO_SURFACE_TYPE_SVG},
  {"OS2", CAIRO_SURFACE_TYPE_OS2},
  {"WIN32_PRINTING", CAIRO_SURFACE_TYPE_WIN32_PRINTING},
  {"QUARTZ_IMAGE", CAIRO_SURFACE_TYPE_QUARTZ_IMAGE},
  {"SCRIPT", CAIRO_SURFACE_TYPE_SCRIPT},
  {"QT", CAIRO_SURFACE_TYPE_QT},
  {"RECORDING", CAIRO_SURFACE_TYPE_RECORDING},
  {"VG", CAIRO_SURFACE_TYPE_VG},
  {"GL", CAIRO_SURFACE_TYPE_GL},
  {"DRM", CAIRO_SURFACE_TYPE_DRM},
  {"TEE", CAIRO_SURFACE_TYPE_TEE},
  {"XML", CAIRO_SURFACE_TYPE_XML},
  {"SKIA", CAIRO_SURFACE_TYPE_SKIA},
  {"SUBSURFACE", CAIRO_SURFACE_TYPE_SUBSURFACE},
  {"COGL", CAIRO_SURFACE_TYPE_COGL}
)
P11X_DECLARE_ENUM(
  "_StreamSurfaceType", "enum.Enum",
  {"PDF", mplcairo::StreamSurfaceType::PDF},
  {"PS", mplcairo::StreamSurfaceType::PS},
  {"EPS", mplcairo::StreamSurfaceType::EPS},
  {"SVG", mplcairo::StreamSurfaceType::SVG},
  {"Script", mplcairo::StreamSurfaceType::Script}
)

namespace mplcairo {

using namespace pybind11::literals;

Region::Region(
    cairo_rectangle_int_t bbox, std::unique_ptr<uint8_t const[]> buffer) :
  bbox{bbox}, buffer{std::move(buffer)}
{}

py::buffer_info Region::get_straight_rgba8888_buffer_info()
{
  auto const& [x0, y0, width, height] = bbox;
  (void)x0; (void)y0;
  auto array = cairo_to_straight_rgba8888(
    py::array_t<uint8_t, py::array::c_style>{
      {height, width, 4}, buffer.get()});
  return array.request();
}

py::bytes Region::get_straight_argb32_bytes()
{
  auto buf = get_straight_rgba8888_buffer_info();
  auto const& size = buf.size;
  if (*reinterpret_cast<uint16_t const*>("\0\xff") > 0x100) {  // little-endian
    uint8_t* u8_ptr = static_cast<uint8_t*>(buf.ptr);
    for (auto i = 0; i < size; i += 4) {
      std::swap(u8_ptr[i], u8_ptr[i + 2]);  // RGBA->BGRA
    }
  } else {  // big-endian
    auto u32_ptr = static_cast<uint32_t*>(buf.ptr);
    for (auto i = 0; i < size / 4; i++) {
      u32_ptr[i] = (u32_ptr[i] >> 8) + (u32_ptr[i] << 24);  // RGBA->ARGB
    }
  }
  return py::bytes{static_cast<char const*>(buf.ptr), size};
}

py::object renderer_base(std::string meth_name)
{
  return
    py::module::import("matplotlib.backend_bases")
    .attr("RendererBase").attr(meth_name.c_str());
}

GraphicsContextRenderer::AdditionalContext::AdditionalContext(
  GraphicsContextRenderer* gcr) :
  gcr_{gcr}
{
  auto const& cr = gcr_->cr_;
  cairo_save(cr);
  // Force alpha, if needed.  Cannot be done earlier as we need to be able to
  // unforce it (by setting alpha to None).
  auto const& [r, g, b, a] = gcr_->get_rgba();
  cairo_set_source_rgba(cr, r, g, b, a);
  // Apply delayed additional state.
  auto const& state = gcr_->get_additional_state();
  // Set antialiasing: if "true", then pick either CAIRO_ANTIALIAS_FAST or
  // CAIRO_ANTIALIAS_BEST, depending on the linewidth.  The threshold of 1/3
  // was determined empirically.
  std::visit(overloaded {
    [&](cairo_antialias_t aa) {
      cairo_set_antialias(cr, aa);
    },
    [&](bool aa) {
      if (aa) {
        auto const& lw = cairo_get_line_width(cr);
        cairo_set_antialias(
          cr,
          (0 < lw) && (lw < 1. / 3)
          ? CAIRO_ANTIALIAS_BEST : CAIRO_ANTIALIAS_FAST);
      } else {
        cairo_set_antialias(cr, CAIRO_ANTIALIAS_NONE);
      }
    }
  }, state.antialias);
  // Clip, if needed.  Cannot be done earlier as we need to be able to unclip.
  if (auto const& rectangle = state.clip_rectangle) {
    auto const& [x, y, w, h] = *rectangle;
    cairo_save(cr);
    restore_init_matrix(cr);
    cairo_new_path(cr);
    cairo_rectangle(cr, x, state.height - h - y, w, h);
    cairo_restore(cr);
    cairo_clip(cr);
  }
  if (auto const& [py_clip_path, clip_path] = state.clip_path; clip_path) {
    (void)py_clip_path;
    cairo_new_path(cr);
    cairo_append_path(cr, clip_path.get());
    cairo_clip(cr);
  }
  if (auto const& url = state.url; url && detail::cairo_tag_begin) {
    if (detail::cairo_tag_begin) {
      detail::cairo_tag_begin(
        cr, CAIRO_TAG_LINK, ("uri='" + *url + "'").c_str());
    } else {
        py::module::import("warnings").attr("warn")(
          "cairo_tag_begin requires cairo>=1.15.4");
    }
  }
  restore_init_matrix(cr);
}

GraphicsContextRenderer::AdditionalContext::~AdditionalContext()
{
  if (gcr_->get_additional_state().url && detail::cairo_tag_end) {
    detail::cairo_tag_end(gcr_->cr_, CAIRO_TAG_LINK);
  }
  cairo_restore(gcr_->cr_);
}

double GraphicsContextRenderer::pixels_to_points(double pixels)
{
  return pixels / (get_additional_state().dpi / 72);
}

rgba_t GraphicsContextRenderer::get_rgba()
{
  double r, g, b, a;
  CAIRO_CHECK(cairo_pattern_get_rgba, cairo_get_source(cr_), &r, &g, &b, &a);
  if (auto const& alpha = get_additional_state().alpha) {
    a = *alpha;
  }
  return {r, g, b, a};
}

GraphicsContextRenderer::GraphicsContextRenderer(
  cairo_t* cr, double width, double height, double dpi) :
  // This does *not* incref the cairo_t, but the destructor *will* decref it.
  cr_{cr}
{
  if (auto const& status = cairo_status(cr);
      status == CAIRO_STATUS_INVALID_SIZE) {
    // Matplotlib wants a ValueError here, not a RuntimeError.
    throw std::length_error{cairo_status_to_string(status)};
  }
  CAIRO_CHECK(cairo_status, cr);
  // Collections and text PathEffects implicitly rely on defaulting to
  // JOIN_ROUND (cairo defaults to JOIN_MITER) and CAP_BUTT (cairo too).  See
  // GraphicsContextBase.__init__.
  cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);
  // May already have been set by cr_from_fileformat_args.
  if (!cairo_get_user_data(cr, &detail::REFS_KEY)) {
    CAIRO_CHECK_SET_USER_DATA(
      cairo_set_user_data, cr, &detail::REFS_KEY,
      new std::vector<py::object>{},
      [](void* data) -> void {
        delete static_cast<std::vector<py::object>*>(data);
      });
  }
  CAIRO_CHECK_SET_USER_DATA(
    cairo_set_user_data, cr, &detail::STATE_KEY,
    (new std::stack<AdditionalState>{{{
      /* width */           width,
      /* height */          height,
      /* dpi */             dpi,
      /* alpha */           {},
      /* antialias */       {true},
      /* clip_rectangle */  {},
      /* clip_path */       {{}, {nullptr, cairo_path_destroy}},
      /* hatch */           {},
      /* hatch_color */     {},  // Lazily loaded by get_hatch_color.
      /* hatch_linewidth */ {},  // Lazily loaded by get_hatch_linewidth.
      /* sketch */          {},
      /* snap */            true,  // Defaults to None, i.e. True for us.
      /* url */             {}
    }}}),
    [](void* data) -> void {
      // Just calling operator delete would not invoke the destructor.
      delete static_cast<std::stack<AdditionalState>*>(data);
    });
}

GraphicsContextRenderer::~GraphicsContextRenderer()
{
  if (detail::FONT_CACHE.size() > 64) {  // font_manager._get_font cache size.
    for (auto& [pathspec, font_face]: detail::FONT_CACHE) {
      (void)pathspec;
      cairo_font_face_destroy(font_face);
    }
    detail::FONT_CACHE.clear();  // Naive cache mechanism.
  }
  try {
#ifdef _WIN32
    std::cerr << std::flush;  // See below.
#endif
    cairo_destroy(cr_);
  } catch (py::error_already_set const& e) {
    // Exceptions would cause a fatal abort from the destructor if _finish is
    // not called on e.g. a SVG surface before the GCR gets GC'd. e.g. comment
    // out this catch, and _finish() in base.py, and run
    //
    //    import gc; from matplotlib import pyplot as plt
    //    with open("/dev/null", "wb") as file:
    //        plt.gcf().savefig(file, format="svg")
    //    plt.close("all")
    //    gc.collect()
    //
    // On Windows, the stream *must* be flushed first, *in the try block* (not
    // elsewhere, including via initialization of a static variable or via
    // Python's sys.stderr); otherwise a fatal abort may be triggered (possibly
    // due to a bad interaction with pytest captures?).
    std::cerr << "Exception ignored in destructor: " << e.what() << "\n";
  }
}

cairo_t* GraphicsContextRenderer::cr_from_image_args(int width, int height)
{
  auto const& surface =
    cairo_image_surface_create(get_cairo_format(), width, height);
  auto const& cr = cairo_create(surface);
  cairo_surface_destroy(surface);
  return cr;
}

GraphicsContextRenderer::GraphicsContextRenderer(
  double width, double height, double dpi) :
  GraphicsContextRenderer{
    cr_from_image_args(int(width), int(height)),
    std::floor(width), std::floor(height), dpi}
{}

cairo_t* GraphicsContextRenderer::cr_from_pycairo_ctx(
  py::object ctx, std::tuple<double, double> device_scales)
{
  if (!detail::has_pycairo) {
    throw std::runtime_error{"pycairo is not available"};
  }
  if (!py::isinstance(
        ctx, py::handle(reinterpret_cast<PyObject*>(&PycairoContext_Type)))) {
    throw std::invalid_argument{
      "{} is not a cairo.Context"_format(ctx).cast<std::string>()};
  }
  auto const& cr = PycairoContext_GET(ctx.ptr());
  CAIRO_CHECK(cairo_status, cr);
  cairo_reference(cr);
  // With native Gtk3, the context may have a nonzero initial translation (if
  // the drawn area is not at the top left of the window); this needs to be
  // taken into account by the path loader (which works in absolute coords).
  auto mtx = new cairo_matrix_t{};
  cairo_get_matrix(cr, mtx);
  auto const& [sx, sy] = device_scales;
  mtx->x0 *= sx; mtx->y0 *= sy;
  CAIRO_CHECK_SET_USER_DATA(
    cairo_set_user_data, cr, &detail::INIT_MATRIX_KEY, mtx,
    [](void* data) -> void { delete static_cast<cairo_matrix_t*>(data); });
  return cr;
}

GraphicsContextRenderer::GraphicsContextRenderer(
  py::object ctx, double width, double height, double dpi,
  std::tuple<double, double> device_scales) :
  GraphicsContextRenderer{
    cr_from_pycairo_ctx(ctx, device_scales), width, height, dpi}
{}

cairo_t* GraphicsContextRenderer::cr_from_fileformat_args(
  StreamSurfaceType type, std::optional<py::object> file,
  double width, double height, double dpi)
{
  auto surface_create_for_stream =
    [&]() -> cairo_surface_t* (*)(cairo_write_func_t, void*, double, double) {
      switch (type) {
        case StreamSurfaceType::PDF:
          return detail::cairo_pdf_surface_create_for_stream;
        case StreamSurfaceType::PS:
        case StreamSurfaceType::EPS:
          return detail::cairo_ps_surface_create_for_stream;
        case StreamSurfaceType::SVG:
          return detail::cairo_svg_surface_create_for_stream;
        case StreamSurfaceType::Script:
          return
            [](cairo_write_func_t write, void* closure,
               double width, double height) -> cairo_surface_t* {
              auto const& script =
                cairo_script_create_for_stream(write, closure);
              auto const& surface =
                cairo_script_surface_create(
                  script, CAIRO_CONTENT_COLOR_ALPHA, width, height);
              cairo_device_destroy(script);
              return surface;
            };
        default:
          return nullptr;
      }
    }();
  if (!surface_create_for_stream) {
    throw std::runtime_error{
      "cairo was built without {.name} support"_format(type)
      .cast<std::string>()};
  }
  auto const& cb = file
    ? cairo_write_func_t{
      [](void* closure, unsigned char const* data, unsigned int length)
        -> cairo_status_t {
          auto const& write =
            py::reinterpret_borrow<py::object>(static_cast<PyObject*>(closure));
          auto const& written =
            write(py::memoryview::from_memory(data, length)).cast<unsigned int>();
          return  // NOTE: This does not appear to affect the context status.
            written == length ? CAIRO_STATUS_SUCCESS : CAIRO_STATUS_WRITE_ERROR;
        }}
    : nullptr;
  py::object write =
    // TODO: Why does py::none() not work here?
    file ? file->attr("write") : py::reinterpret_borrow<py::object>(Py_None);

  auto const& surface =
    surface_create_for_stream(cb, write.ptr(), width, height);
  cairo_surface_set_fallback_resolution(surface, dpi, dpi);
  auto const& cr = cairo_create(surface);
  cairo_surface_destroy(surface);
  CAIRO_CHECK_SET_USER_DATA(
    cairo_set_user_data, cr, &detail::REFS_KEY,
    new std::vector<py::object>{{write}},
    [](void* data) -> void {
      delete static_cast<std::vector<py::object>*>(data);
    });
  if (type == StreamSurfaceType::EPS) {
    // If cairo was built without PS support, we'd already have errored above.
    detail::cairo_ps_surface_set_eps(surface, true);
  }
  return cr;
}

GraphicsContextRenderer::GraphicsContextRenderer(
  StreamSurfaceType type, std::optional<py::object> file,
  double width, double height, double dpi) :
  GraphicsContextRenderer{
    cr_from_fileformat_args(type, file, width, height, dpi), width, height,
    type == StreamSurfaceType::Script
      && detail::MPLCAIRO_SCRIPT_SURFACE == detail::MplcairoScriptSurface::Raster
      ? dpi : 72}
{}

GraphicsContextRenderer GraphicsContextRenderer::make_pattern_gcr(
  cairo_surface_t* surface)
{
  // linewidths are already in pixels and won't get converted, but we may as
  // well set dpi to 72 to have a 1 to 1 conversion.
  auto gcr = GraphicsContextRenderer{
    cairo_create(surface),
    double(cairo_image_surface_get_width(surface)),
    double(cairo_image_surface_get_height(surface)),
    72};
  cairo_surface_destroy(surface);
  gcr.get_additional_state().snap = false;
  return gcr;
}

GraphicsContextRenderer::AdditionalContext
GraphicsContextRenderer::_additional_context()
{
  return {this};
}

void GraphicsContextRenderer::_set_path(std::optional<std::string> path) {
  path_ = path;
}

void GraphicsContextRenderer::_set_metadata(std::optional<py::dict> metadata)
{
  if (!metadata) {
    metadata = py::dict{};  // So that SOURCE_DATE_EPOCH is handled.
  }
  *metadata = metadata->attr("copy")();  // We'll add and remove keys.
  auto const& surface = cairo_get_target(cr_);
  switch (cairo_surface_get_type(surface)) {
    case CAIRO_SURFACE_TYPE_PDF:
      if (auto const& source_date_epoch = std::getenv("SOURCE_DATE_EPOCH")) {
        metadata->attr("setdefault")(
          "CreationDate",
          py::module::import("datetime").attr("datetime")
          .attr("utcfromtimestamp")(std::stol(source_date_epoch)));
      }
      if (auto maxver =
            metadata->attr("pop")("MaxVersion", py::none())
            .cast<std::optional<std::string>>()) {
        if (*maxver == "1.4") {
          detail::cairo_pdf_surface_restrict_to_version(
            surface, detail::CAIRO_PDF_VERSION_1_4);
        } else if (*maxver == "1.5") {
          detail::cairo_pdf_surface_restrict_to_version(
            surface, detail::CAIRO_PDF_VERSION_1_5);
        } else {
          throw std::invalid_argument("Invalid MaxVersion: " + *maxver);
        }
      }
      for (auto const& it: *metadata) {
        if (it.second.is_none()) {
          continue;
        }
        if (!detail::cairo_pdf_surface_set_metadata) {
          py::module::import("warnings").attr("warn")(
            "cairo_pdf_surface_set_metadata requires cairo>=1.15.4");
          break;
        }
        auto const& key = it.first.cast<std::string>();
        if (key == "Title") {
          detail::cairo_pdf_surface_set_metadata(
            surface, detail::CAIRO_PDF_METADATA_TITLE,
            it.second.cast<std::string>().c_str());
        } else if (key == "Author") {
          detail::cairo_pdf_surface_set_metadata(
            surface, detail::CAIRO_PDF_METADATA_AUTHOR,
            it.second.cast<std::string>().c_str());
        } else if (key == "Subject") {
          detail::cairo_pdf_surface_set_metadata(
            surface, detail::CAIRO_PDF_METADATA_SUBJECT,
            it.second.cast<std::string>().c_str());
        } else if (key == "Keywords") {
          detail::cairo_pdf_surface_set_metadata(
            surface, detail::CAIRO_PDF_METADATA_KEYWORDS,
            it.second.cast<std::string>().c_str());
        } else if (key == "Creator") {
          detail::cairo_pdf_surface_set_metadata(
            surface, detail::CAIRO_PDF_METADATA_CREATOR,
            it.second.cast<std::string>().c_str());
        } else if (key == "CreationDate") {
          detail::cairo_pdf_surface_set_metadata(
            surface, detail::CAIRO_PDF_METADATA_CREATE_DATE,
            it.second.attr("isoformat")().cast<std::string>().c_str());
        } else if (key == "ModDate") {
          detail::cairo_pdf_surface_set_metadata(
            surface, detail::CAIRO_PDF_METADATA_MOD_DATE,
            it.second.attr("isoformat")().cast<std::string>().c_str());
        } else {
          py::module::import("warnings").attr("warn")(
            "Unsupported PDF metadata entry: " + key);
        }
      }
      break;
    case CAIRO_SURFACE_TYPE_PS:
      if (auto maxver =
            metadata->attr("pop")("MaxVersion", py::none())
            .cast<std::optional<std::string>>()) {
        if (*maxver == "2") {
          detail::cairo_ps_surface_restrict_to_level(
            surface, detail::CAIRO_PS_LEVEL_2);
        } else if (*maxver == "3") {
          detail::cairo_ps_surface_restrict_to_level(
            surface, detail::CAIRO_PS_LEVEL_3);
        } else {
          throw std::invalid_argument("Invalid MaxVersion: " + *maxver);
        }
      }
      for (auto const& it: *metadata) {
        auto const& key = it.first.cast<std::string>();
        if (key == "_dsc_comments") {
          for (auto const& comment:
               it.second.cast<std::vector<std::string>>()) {
            detail::cairo_ps_surface_dsc_comment(surface, comment.c_str());
          }
        } else {
          py::module::import("warnings").attr("warn")(
            "Unsupported PS metadata entry: " + key);
        }
      }
      break;
    case CAIRO_SURFACE_TYPE_SVG:
      if (auto maxver =
            metadata->attr("pop")("MaxVersion", py::none())
            .cast<std::optional<std::string>>()) {
        if (*maxver == "1.1") {
          detail::cairo_svg_surface_restrict_to_version(
            surface, detail::CAIRO_SVG_VERSION_1_1);
        } else if (*maxver == "1.2") {
          detail::cairo_svg_surface_restrict_to_version(
            surface, detail::CAIRO_SVG_VERSION_1_2);
        } else {
          throw std::invalid_argument("Invalid MaxVersion: " + *maxver);
        }
      }
      break;
    default:
      if (metadata->size()) {
        py::module::import("warnings").attr("warn")(
          "Metadata support is not implemented for the current surface type");
      }
  }
}

void GraphicsContextRenderer::_set_size(
  double width, double height, double dpi)
{
  auto& state = get_additional_state();
  state.width = width;
  state.height = height;
  state.dpi = dpi;
  auto const& surface = cairo_get_target(cr_);
  switch (auto const& type = cairo_surface_get_type(surface)) {
    case CAIRO_SURFACE_TYPE_PDF:
      detail::cairo_pdf_surface_set_size(surface, width, height);
      break;
    case CAIRO_SURFACE_TYPE_PS:
      detail::cairo_ps_surface_set_size(surface, width, height);
      break;
    default:
      throw std::invalid_argument{
        "_set_size only supports PDF and PS surfaces, not {.name}"_format(type)
        .cast<std::string>()};
  }
}

void GraphicsContextRenderer::_show_page()
{
  cairo_show_page(cr_);
}

py::object GraphicsContextRenderer::_get_context()
{
  if (detail::has_pycairo) {
    cairo_reference(cr_);
    return py::reinterpret_steal<py::object>(
      PycairoContext_FromContext(cr_, &PycairoContext_Type, nullptr));
  } else {
    throw std::runtime_error{"pycairo is not available"};
  }
}

py::array GraphicsContextRenderer::_get_buffer()
{
  return image_surface_to_buffer(cairo_get_target(cr_));
}

void GraphicsContextRenderer::_finish()
{
  cairo_surface_finish(cairo_get_target(cr_));
}

void GraphicsContextRenderer::set_alpha(std::optional<double> alpha)
{
  get_additional_state().alpha = alpha;
}

void GraphicsContextRenderer::set_antialiased(
  std::variant<cairo_antialias_t, bool> aa)
{
  get_additional_state().antialias = aa;
}

void GraphicsContextRenderer::set_capstyle(std::string capstyle)
{
  if (capstyle == "butt") {
    cairo_set_line_cap(cr_, CAIRO_LINE_CAP_BUTT);
  } else if (capstyle == "round") {
    cairo_set_line_cap(cr_, CAIRO_LINE_CAP_ROUND);
  } else if (capstyle == "projecting") {
    cairo_set_line_cap(cr_, CAIRO_LINE_CAP_SQUARE);
  } else {
    throw std::invalid_argument{"invalid capstyle: " + capstyle};
  }
}

void GraphicsContextRenderer::set_clip_rectangle(
  std::optional<py::object> rectangle)
{
  get_additional_state().clip_rectangle =
    rectangle
    // A TransformedBbox or a tuple.
    ? py::getattr(*rectangle, "bounds", *rectangle).cast<rectangle_t>()
    : std::optional<rectangle_t>{};
}

void GraphicsContextRenderer::set_clip_path(
  std::optional<py::object> transformed_path)
{
  if (transformed_path) {
    auto const& [path, transform] =
      transformed_path->attr("get_transformed_path_and_affine")()
      .cast<std::tuple<py::object, py::object>>();
    auto const& mtx =
      matrix_from_transform(transform, get_additional_state().height);
    load_path_exact(cr_, path, &mtx);
    get_additional_state().clip_path =
      {transformed_path, {cairo_copy_path(cr_), cairo_path_destroy}};
  } else {
    get_additional_state().clip_path = {{}, {}};
  }
}

void GraphicsContextRenderer::set_dashes(
  std::optional<double> dash_offset,  // Just double, with mpl 3.3+ (#15828).
  std::optional<py::array_t<double>> dash_list)
{
  if (dash_list) {
    if (!dash_offset) {
      throw std::invalid_argument{"missing dash offset"};
    }
    auto const& dashes_raw = dash_list->unchecked<1>();
    auto const& n = dashes_raw.size();
    auto const& buf = std::unique_ptr<double[]>{new double[n]};
    for (auto i = 0; i < n; ++i) {
      buf[i] = points_to_pixels(dashes_raw[i]);
    }
    if (std::all_of(buf.get(), buf.get() + n, std::logical_not{})) {
      // Treat fully zero dash arrays as solid stroke.  This is consistent with
      // the SVG spec (PDF and PS specs explicitly reject such arrays), and
      // also generated by Matplotlib for zero-width lines due to dash scaling.
      cairo_set_dash(cr_, nullptr, 0, 0);
    } else {
      cairo_set_dash(cr_, buf.get(), n, points_to_pixels(*dash_offset));
    }
  } else {
    cairo_set_dash(cr_, nullptr, 0, 0);
  }
}

void GraphicsContextRenderer::set_foreground(
  py::object fg, bool /* is_rgba */)
{
  auto [r, g, b, a] = to_rgba(fg);
  if (auto const& alpha = get_additional_state().alpha) {
    a = *alpha;
  }
  cairo_set_source_rgba(cr_, r, g, b, a);
}

void GraphicsContextRenderer::set_hatch(std::optional<std::string> hatch)
{
  get_additional_state().hatch = hatch;
}

void GraphicsContextRenderer::set_hatch_color(py::object hatch_color)
{
  get_additional_state().hatch_color = to_rgba(hatch_color);
}

void GraphicsContextRenderer::set_joinstyle(std::string joinstyle)
{
  if (joinstyle == "miter") {
    cairo_set_line_join(cr_, CAIRO_LINE_JOIN_MITER);
  } else if (joinstyle == "round") {
    cairo_set_line_join(cr_, CAIRO_LINE_JOIN_ROUND);
  } else if (joinstyle == "bevel") {
    cairo_set_line_join(cr_, CAIRO_LINE_JOIN_BEVEL);
  } else {
    throw std::invalid_argument{"invalid joinstyle: " + joinstyle};
  }
}

void GraphicsContextRenderer::set_linewidth(double lw)
{
  cairo_set_line_width(cr_, points_to_pixels(lw));
  cairo_set_miter_limit(
    cr_,
    detail::MITER_LIMIT >= 0
    // Agg's default (in _backend_agg.h) is likely buggy.
    ? detail::MITER_LIMIT : cairo_get_line_width(cr_));
}

// NOTE: Don't take std::optional<bool> as argument as this appears to lead to
// additional_state.snap being uninitialized further downstream (per valgrind),
// and possibly causes a crash on Fedora's buildbots.
void GraphicsContextRenderer::set_snap(py::object snap)
{
  // NOTE: We treat None (snap if only vertical or horizontal lines) as True.
  // NOTE: It appears that even when rcParams["path.snap"] is False, this is
  // sometimes set to True.
  get_additional_state().snap = snap.is_none() ? true : snap.cast<bool>();
}

void GraphicsContextRenderer::set_url(std::optional<std::string> url)
{
  get_additional_state().url = url;
}

AdditionalState const& GraphicsContextRenderer::get_additional_state() const
{
  return
    static_cast<std::stack<AdditionalState>*>(
      cairo_get_user_data(cr_, &detail::STATE_KEY))->top();
}

AdditionalState& GraphicsContextRenderer::get_additional_state()
{
  return
    static_cast<std::stack<AdditionalState>*>(
      cairo_get_user_data(cr_, &detail::STATE_KEY))->top();
}

double GraphicsContextRenderer::get_linewidth()
{
  return pixels_to_points(cairo_get_line_width(cr_));
}

rgb_t GraphicsContextRenderer::get_rgb()
{
  auto const& [r, g, b, a] = get_rgba();
  (void)a;
  return {r, g, b};
}

GraphicsContextRenderer& GraphicsContextRenderer::new_gc()
{
  cairo_save(cr_);
  auto& states =
    *static_cast<std::stack<AdditionalState>*>(
      cairo_get_user_data(cr_, &detail::STATE_KEY));
  states.push(states.top());
  return *this;
}

void GraphicsContextRenderer::copy_properties(GraphicsContextRenderer* other)
{
  // In practice the following holds.  Anything else requires figuring out what
  // to do with the properties stack.
  if (this != other) {
    throw std::invalid_argument{"independent contexts cannot be copied"};
  }
}

void GraphicsContextRenderer::restore()
{
  auto& states =
    *static_cast<std::stack<AdditionalState>*>(
      cairo_get_user_data(cr_, &detail::STATE_KEY));
  states.pop();
  cairo_restore(cr_);
}

double GraphicsContextRenderer::points_to_pixels(double points)
{
  return points * get_additional_state().dpi / 72;
}

void GraphicsContextRenderer::draw_gouraud_triangles(
  GraphicsContextRenderer& gc,
  py::array_t<double> triangles,
  py::array_t<double> colors,
  py::object transform)
{
  if (&gc != this) {
    throw std::invalid_argument{"non-matching GraphicsContext"};
  }
  auto const& ac = _additional_context();
  auto mtx = matrix_from_transform(transform, get_additional_state().height);
  auto const& tri_raw = triangles.unchecked<3>();
  auto const& col_raw = colors.unchecked<3>();
  auto const& n = tri_raw.shape(0);
  if (col_raw.shape(0) != n
      || tri_raw.shape(1) != 3 || tri_raw.shape(2) != 2
      || col_raw.shape(1) != 3 || col_raw.shape(2) != 4) {
    throw std::invalid_argument{
      "shapes of triangles {.shape} and colors {.shape} are mismatched"_format(
        triangles, colors)
      .cast<std::string>()};
  }
  auto const& pattern = cairo_pattern_create_mesh();
  for (auto i = 0; i < n; ++i) {
    cairo_mesh_pattern_begin_patch(pattern);
    for (auto j = 0; j < 3; ++j) {
      cairo_mesh_pattern_line_to(pattern, tri_raw(i, j, 0), tri_raw(i, j, 1));
      cairo_mesh_pattern_set_corner_color_rgba(
        pattern, j,
        col_raw(i, j, 0), col_raw(i, j, 1),
        col_raw(i, j, 2), col_raw(i, j, 3));
    }
    cairo_mesh_pattern_end_patch(pattern);
  }
  cairo_matrix_invert(&mtx);
  cairo_pattern_set_matrix(pattern, &mtx);
  cairo_set_source(cr_, pattern);
  cairo_pattern_destroy(pattern);
  cairo_paint(cr_);
}

void GraphicsContextRenderer::draw_image(
  GraphicsContextRenderer& gc, double x, double y, py::array_t<uint8_t> im)
{
  if (&gc != this) {
    throw std::invalid_argument{"non-matching GraphicsContext"};
  }
  auto const& ac = _additional_context();
  auto const& im_raw = im.unchecked<3>();
  auto const& height = im_raw.shape(0), width = im_raw.shape(1);
  if (im_raw.shape(2) != 4) {
    throw std::invalid_argument{
      "RGBA array must have shape (m, n, 4), not {.shape}"_format(im)
      .cast<std::string>()};
  }
  // Let cairo manage the surface memory; as some backends only write the image
  // at flush time.
  auto const& surface =
    cairo_image_surface_create(CAIRO_FORMAT_ARGB32, width, height);
  auto const& data = cairo_image_surface_get_data(surface);
  auto const& stride = cairo_image_surface_get_stride(surface);
  cairo_surface_flush(surface);
  // The gcr's alpha has already been applied by ImageBase._make_image, we just
  // need to convert to premultiplied ARGB format.
  for (auto i = 0; i < height; ++i) {
    auto ptr = reinterpret_cast<uint32_t*>(data + i * stride);
    for (auto j = 0; j < width; ++j) {
      auto const& r = im_raw(i, j, 0),
                & g = im_raw(i, j, 1),
                & b = im_raw(i, j, 2),
                & a = im_raw(i, j, 3);
      *ptr++ =
        (uint8_t(a) << 24)
        | (uint8_t(a / 255. * r) << 16)
        | (uint8_t(a / 255. * g) << 8)
        | (uint8_t(a / 255. * b) << 0);
    }
  }
  cairo_surface_mark_dirty(surface);
  if (cairo_surface_get_type(cairo_get_target(cr_)) == CAIRO_SURFACE_TYPE_SVG
      && !rc_param("svg.image_inline").cast<bool>()) {
    if (!path_) {
      throw std::runtime_error{
        "cannot save images to filesystem when writing to a non-file stream"};
    }
    auto image_path_ptr = new std::string{};
    for (auto i = 0;; ++i) {
      *image_path_ptr = *path_ + ".image" + std::to_string(i) + ".png";
      // Matplotlib uses a hard counter.  Checking for file existence avoids
      // both the need for the counter *and* the risk of overwriting
      // preexisting files.
      if (!py::module::import("os.path").attr("exists")(*image_path_ptr)
           .cast<bool>()) {
        break;
      }
    }
    CAIRO_CHECK(cairo_surface_write_to_png, surface, image_path_ptr->c_str());
    CAIRO_CHECK(
      cairo_surface_set_mime_data,
      surface,
      CAIRO_MIME_TYPE_URI,
      reinterpret_cast<uint8_t const*>(image_path_ptr->c_str()),
      image_path_ptr->size(),
      [](void* data) -> void {
        delete static_cast<std::string*>(data);
      },
      image_path_ptr);
  }
  auto const& pattern = cairo_pattern_create_for_surface(surface);
  cairo_surface_destroy(surface);
  auto const& mtx =
    cairo_matrix_t{1, 0, 0, -1, -x, -y + get_additional_state().height};
  cairo_pattern_set_matrix(pattern, &mtx);
  cairo_set_source(cr_, pattern);
  cairo_pattern_destroy(pattern);
  cairo_paint(cr_);
}

void GraphicsContextRenderer::draw_path(
  GraphicsContextRenderer& gc,
  py::object path,
  py::object transform,
  std::optional<py::object> fc)
{
  if (&gc != this) {
    throw std::invalid_argument{"non-matching GraphicsContext"};
  }
  auto const& ac = _additional_context();
  auto path_loaded = false;
  auto mtx = matrix_from_transform(transform, get_additional_state().height);
  auto const& load_path = [&] {
    if (!path_loaded) {
      load_path_exact(cr_, path, &mtx);
      path_loaded = true;
    }
  };
  auto const& hatch_path =
    py::cast(this).attr("get_hatch_path")().cast<std::optional<py::object>>();
  auto const& simplify =
    path.attr("should_simplify").cast<bool>() && !fc && !hatch_path;
  auto const& sketch = get_additional_state().sketch;
  if (simplify || sketch) {
    // TODO: cairo internally uses vertex reduction and Douglas-Peucker, but it
    // is unclear whether it also applies to vector output?  See mplcairo#37.
    path = path.attr("cleaned")(
      "transform"_a=transform, "simplify"_a=simplify, "curves"_a=true,
      "sketch"_a=sketch);
    mtx = cairo_matrix_t{1, 0, 0, -1, 0, get_additional_state().height};
  }
  if (fc) {
    load_path();
    cairo_save(cr_);
    auto const& [r, g, b, a] = to_rgba(*fc, get_additional_state().alpha);
    cairo_set_source_rgba(cr_, r, g, b, a);
    cairo_fill_preserve(cr_);
    cairo_restore(cr_);
  }
  if (hatch_path) {
    cairo_save(cr_);
    auto const& dpi = int(get_additional_state().dpi);  // Truncating is good enough.
    auto const& hatch_surface =
      cairo_surface_create_similar(
        cairo_get_target(cr_), CAIRO_CONTENT_COLOR_ALPHA, dpi, dpi);
    auto const& hatch_cr = cairo_create(hatch_surface);
    cairo_surface_destroy(hatch_surface);
    auto hatch_gcr = GraphicsContextRenderer{
      hatch_cr, double(dpi), double(dpi), double(dpi)};
    hatch_gcr.get_additional_state().snap = false;
    hatch_gcr.set_linewidth(get_additional_state().get_hatch_linewidth());
    auto const& mtx =
      cairo_matrix_t{double(dpi), 0, 0, -double(dpi), 0, double(dpi)};
    auto const& hatch_color = get_additional_state().get_hatch_color();
    fill_and_stroke_exact(
      hatch_cr, *hatch_path, &mtx, hatch_color, hatch_color);
    auto const& hatch_pattern =
      cairo_pattern_create_for_surface(cairo_get_target(hatch_cr));
    cairo_pattern_set_extend(hatch_pattern, CAIRO_EXTEND_REPEAT);
    cairo_set_source(cr_, hatch_pattern);
    cairo_pattern_destroy(hatch_pattern);
    load_path();
    cairo_clip_preserve(cr_);
    cairo_paint(cr_);
    cairo_restore(cr_);
  }
  auto const& chunksize = rc_param("agg.path.chunksize").cast<int>();
  if (path_loaded || !chunksize || !path.attr("codes").is_none()) {
    load_path();
    cairo_stroke(cr_);
  } else {
    auto const& vertices = path.attr("vertices").cast<py::array_t<double>>();
    auto const& n = vertices.shape(0);
    for (auto i = decltype(n)(0); i < n; i += chunksize) {
      load_path_exact(cr_, vertices, i, std::min(i + chunksize + 1, n), &mtx);
      cairo_stroke(cr_);
    }
  }
}

template<typename T>
void maybe_multithread(cairo_t* cr, int n, T /* lambda */ worker) {
  if (detail::COLLECTION_THREADS) {
    auto const& chunk_size =
      int(std::ceil(double(n) / detail::COLLECTION_THREADS));
    auto ctxs = std::vector<cairo_t*>{};
    auto threads = std::vector<std::thread>{};
    for (auto i = 0; i < detail::COLLECTION_THREADS; ++i) {
      auto const& surface =
        cairo_surface_create_similar_image(
          cairo_get_target(cr), get_cairo_format(),
          get_additional_state(cr).width, get_additional_state(cr).height);
      auto const& ctx = cairo_create(surface);
      cairo_surface_destroy(surface);
      ctxs.push_back(ctx);
      threads.emplace_back(
        worker, ctx, chunk_size * i, std::min<int>(chunk_size * (i + 1), n));
    }
    {
      auto const& nogil = py::gil_scoped_release{};
      for (auto& thread: threads) {
        thread.join();
      }
    }
    for (auto const& ctx: ctxs) {
      auto const& pattern =
        cairo_pattern_create_for_surface(cairo_get_target(ctx));
      cairo_destroy(ctx);
      cairo_set_source(cr, pattern);
      cairo_pattern_destroy(pattern);
      cairo_paint(cr);
    }
  }
  else {
    worker(cr, 0, n);
  }
}

void GraphicsContextRenderer::draw_markers(
  GraphicsContextRenderer& gc,
  py::object marker_path,
  py::object marker_transform,
  py::object path,
  py::object transform,
  std::optional<py::object> fc)
{
  if (&gc != this) {
    throw std::invalid_argument{"non-matching GraphicsContext"};
  }
  auto const& ac = _additional_context();

  // As paths store their vertices in an array, the .cast<>() will not make a
  // copy and we don't need to explicitly keep the intermediate result alive.
  auto const& vertices =
    path.attr("vertices").cast<py::array_t<double>>().unchecked<2>();
  // FIXME[matplotlib]: For efficiency, we ignore codes, which is the
  // documented behavior even though not the actual one of other backends.
  auto const& n_vertices = vertices.shape(0);

  if (n_vertices <= 2) {
    // With less than two vertices, the line join shouldn't matter, but
    // antialiasing is actually better (assuming ANTIALIAS_FAST is used, which
    // is normally the case) with LINE_JOIN_MITER (cairo#536).
    cairo_set_line_join(cr_, CAIRO_LINE_JOIN_MITER);
    cairo_set_miter_limit(cr_, 10);  // Default, any value >sqrt(2) works.
  }

  auto const& marker_matrix = matrix_from_transform(marker_transform);
  auto const& mtx =
    matrix_from_transform(transform, get_additional_state().height);

  auto const& fc_raw_opt =
    fc ? to_rgba(*fc, get_additional_state().alpha) : std::optional<rgba_t>{};
  auto const& ec_raw = get_rgba();

  auto const& draw_one_marker = [&](cairo_t* cr, double x, double y) -> void {
    auto const& m = cairo_matrix_t{
      marker_matrix.xx, marker_matrix.yx, marker_matrix.xy, marker_matrix.yy,
      marker_matrix.x0 + x, marker_matrix.y0 + y};
    fill_and_stroke_exact(cr, marker_path, &m, fc_raw_opt, ec_raw);
  };

  // Pixel markers *must* be drawn snapped.
  auto const& is_pixel_marker =
    py_eq(marker_path, detail::PIXEL_MARKER.attr("get_path")())
    && py_eq(marker_transform, detail::PIXEL_MARKER.attr("get_transform")());
  auto const& simplify_threshold =
    is_pixel_marker || has_vector_surface(cr_)
    ? 0 : rc_param("path.simplify_threshold").cast<double>();
  auto patterns = std::unique_ptr<cairo_pattern_t*[]>{};
  auto const& n_subpix =  // NOTE: Arbitrary limit of 1/16.
    simplify_threshold >= 1. / 16 ? int(std::ceil(1 / simplify_threshold)) : 0;
  if (n_subpix && n_subpix * n_subpix < n_vertices) {
    patterns.reset(new cairo_pattern_t*[n_subpix * n_subpix]);
  }

  if (patterns) {
    // When stamping subpixel-positioned markers, there is no benefit in
    // snapping (we're going to shift the path by subpixels anyways).  We don't
    // want to force snapping off in the non-stamped case, as that would e.g.
    // ruin alignment of ticks and spines, so the change is only applied in
    // this branch.
    auto const& old_snap = get_additional_state().snap;
    get_additional_state().snap = false;
    load_path_exact(cr_, marker_path, &marker_matrix);
    get_additional_state().snap = old_snap;
    // Get the extent of the marker.  Importantly, cairo_*_extents() ignores
    // surface dimensions and clipping.
    // Matplotlib chooses *not* to call draw_markers() if the marker is bigger
    // than the canvas (which may make sense if the marker is indeed huge...).
    double x0, y0, x1, y1;
    cairo_stroke_extents(cr_, &x0, &y0, &x1, &y1);
    if (fc) {
      double x0f, y0f, x1f, y1f;
      cairo_fill_extents(cr_, &x0f, &y0f, &x1f, &y1f);
      x0 = std::min(x0, x0f);
      y0 = std::min(y0, y0f);
      x1 = std::max(x1, x1f);
      y1 = std::max(y1, y1f);
    }
    x0 = std::floor(x0 / n_subpix) * n_subpix;
    y0 = std::floor(y0 / n_subpix) * n_subpix;

    // Fill the pattern cache.
    auto const& raster_gcr =
      make_pattern_gcr(
        cairo_surface_create_similar_image(
          cairo_get_target(cr_), get_cairo_format(),
          std::ceil(x1 - x0 + 1), std::ceil(y1 - y0 + 1)));
    auto const& raster_cr = raster_gcr.cr_;
    cairo_set_antialias(raster_cr, cairo_get_antialias(cr_));
    cairo_set_line_cap(raster_cr, cairo_get_line_cap(cr_));
    cairo_set_line_join(raster_cr, cairo_get_line_join(cr_));
    cairo_set_line_width(raster_cr, cairo_get_line_width(cr_));
    auto const& dash_count = cairo_get_dash_count(cr_);
    auto const& dashes = std::unique_ptr<double[]>{new double[dash_count]};
    double offset;
    cairo_get_dash(cr_, dashes.get(), &offset);
    cairo_set_dash(raster_cr, dashes.get(), dash_count, offset);
    double r, g, b, a;
    CAIRO_CHECK(cairo_pattern_get_rgba, cairo_get_source(cr_), &r, &g, &b, &a);
    cairo_set_source_rgba(raster_cr, r, g, b, a);

    for (auto i = 0; i < n_subpix; ++i) {
      for (auto j = 0; j < n_subpix; ++j) {
        cairo_push_group(raster_cr);
        draw_one_marker(
          raster_cr, -x0 + double(i) / n_subpix, -y0 + double(j) / n_subpix);
        auto const& pattern =
          patterns[i * n_subpix + j] = cairo_pop_group(raster_cr);
        cairo_pattern_set_filter(pattern, CAIRO_FILTER_NEAREST);
      }
    }

    maybe_multithread(cr_, n_vertices, [&](cairo_t* ctx, int start, int stop) {
      for (auto i = start; i < stop; ++i) {
        auto x = vertices(i, 0), y = vertices(i, 1);
        cairo_matrix_transform_point(&mtx, &x, &y);
        auto const& target_x = x + x0,
                  & target_y = y + y0;
        if (!(std::isfinite(target_x) && std::isfinite(target_y))) {
          continue;
        }
        auto const& i_target_x = std::floor(target_x),
                  & i_target_y = std::floor(target_y);
        auto const& f_target_x = target_x - i_target_x,
                  & f_target_y = target_y - i_target_y;
        auto const& idx =
          int(n_subpix * f_target_x) * n_subpix + int(n_subpix * f_target_y);
        auto const& pattern = patterns[idx];
        // Offsetting by height is already taken care of by mtx.
        auto const& pattern_matrix =
          cairo_matrix_t{1, 0, 0, 1, -i_target_x, -i_target_y};
        cairo_pattern_set_matrix(pattern, &pattern_matrix);
        cairo_set_source(ctx, pattern);
        cairo_paint(ctx);
      }
    });

    // Cleanup.
    for (auto i = 0; i < n_subpix * n_subpix; ++i) {
      cairo_pattern_destroy(patterns[i]);
    }

  } else if (is_pixel_marker && !has_vector_surface(cr_)) {
    auto const& surface = cairo_get_target(cr_);
    auto const& raw = cairo_image_surface_get_data(surface);
    auto const& stride = cairo_image_surface_get_stride(surface);
    auto const& [r, g, b, a] = fc_raw_opt ? *fc_raw_opt : ec_raw;
    auto const& fc_argb32 = uint32_t(
        (uint8_t(255 * a) << 24) | (uint8_t(255 * a * r) << 16)
        | (uint8_t(255 * a * g) << 8) | (uint8_t(255 * a * b)));
    cairo_surface_flush(surface);
    for (auto i = 0; i < n_vertices; ++i) {
      auto x = vertices(i, 0), y = vertices(i, 1);
      cairo_matrix_transform_point(&mtx, &x, &y);
      if (!(std::isfinite(x) && std::isfinite(y))) {
        continue;
      }
      // FIXME: Correctly apply alpha.
      *reinterpret_cast<uint32_t*>(
        raw + std::lround(y) * stride + 4 * std::lround(x)) = fc_argb32;
    }
    cairo_surface_mark_dirty(surface);

  } else {
    for (auto i = 0; i < n_vertices; ++i) {
      cairo_save(cr_);
      auto x = vertices(i, 0), y = vertices(i, 1);
      cairo_matrix_transform_point(&mtx, &x, &y);
      if (!(std::isfinite(x) && std::isfinite(y))) {
        cairo_restore(cr_);
        continue;
      }
      draw_one_marker(cr_, x, y);
      cairo_restore(cr_);
    }
  }
}

void GraphicsContextRenderer::draw_path_collection(
  GraphicsContextRenderer& gc,
  py::object master_transform,
  std::vector<py::handle> paths,
  std::vector<py::object> transforms,
  py::array_t<double> offsets,
  py::object offset_transform,
  py::object fcs,
  py::object ecs,
  py::array_t<double> lws,
  std::vector<std::tuple<std::optional<double>,
                         std::optional<py::array_t<double>>>> dashes,
  py::object aas,
  py::object urls,
  std::string offset_position)
{
  // TODO: Persistent cache; cache eviction policy.  However, note that
  // PatternCache currently uses handles rather than object for multithreading
  // support, which means that key lifetimes are tied to the *paths* argument.

  // Fall back onto the slow implementation in the following, non-supported
  // cases:
  // - Hatching is used: the stamp cache cannot be used anymore, as the hatch
  //   positions would be different on every stamp.  (NOTE: Actually it may be
  //   possible to use the hatch as the source and mask it with the pattern.)
  // - offset_position is set to "data".  This feature was only used by
  //   hexbin() and only in mpl<3.3 (#13696).
  if (py::bool_(py::cast(this).attr("get_hatch")())
      || offset_position == "data") {
    renderer_base("draw_path_collection")(
      this, gc, master_transform, paths, transforms, offsets, offset_transform,
      fcs, ecs, lws, dashes, aas, urls, offset_position);
    return;
  }

  if (&gc != this) {
    throw std::invalid_argument{"non-matching GraphicsContext"};
  }
  auto const& ac = _additional_context();
  auto const& old_snap = get_additional_state().snap;
  get_additional_state().snap = false;

  auto n_paths = ssize_t(paths.size()),
       n_transforms = ssize_t(transforms.size()),
       n_offsets = offsets.shape(0),
       n = std::max({n_paths, n_transforms, n_offsets});
  if (!n_paths || !n_offsets) {
    return;
  }
  auto const& master_matrix =
    matrix_from_transform(master_transform, get_additional_state().height);
  auto const& matrices = std::unique_ptr<cairo_matrix_t[]>{
    new cairo_matrix_t[n_transforms ? n_transforms : 1]};
  if (n_transforms) {
    for (auto i = 0; i < n_transforms; ++i) {
      matrices[i] = matrix_from_transform(transforms[i], &master_matrix);
    }
  } else {
    n_transforms = 1;
    matrices[0] = master_matrix;
  }
  auto const& offsets_raw = offsets.unchecked<2>();
  if (offsets_raw.shape(1) != 2) {
    throw std::invalid_argument{
      "offsets must have shape (n, 2), not {.shape}"_format(offsets)
      .cast<std::string>()};
  }
  auto const& offset_matrix = matrix_from_transform(offset_transform);
  auto const& convert_colors = [&](py::object colors) -> py::array_t<double> {
    auto const& alpha = get_additional_state().alpha;
    return
      py::module::import("matplotlib.colors").attr("to_rgba_array")(
        colors, alpha ? py::cast(*alpha) : py::none());
  };
  // Don't drop the arrays until the function exits.
  auto const& fcs_raw_keepref = convert_colors(fcs),
              ecs_raw_keepref = convert_colors(ecs);
  auto const& fcs_raw = fcs_raw_keepref.unchecked<2>(),
              ecs_raw = ecs_raw_keepref.unchecked<2>();
  auto const& lws_raw = lws.unchecked<1>();
  auto n_dashes = dashes.size();
  auto const& dashes_raw = std::unique_ptr<dash_t[]>{
    new dash_t[n_dashes ? n_dashes : 1]};
  if (n_dashes) {
    for (auto i = 0u; i < n_dashes; ++i) {
      auto const& [dash_offset, dash_list] = dashes[i];
      set_dashes(dash_offset, dash_list);  // Invoke the dash converter.
      dashes_raw[i] = convert_dash(cr_);
    }
  } else {
    n_dashes = 1;
    dashes_raw[0] = {};
  }
  auto const& simplify_threshold =
    has_vector_surface(cr_)
    ? 0 : rc_param("path.simplify_threshold").cast<double>();
  auto points_to_pixels_factor = get_additional_state().dpi / 72;

  maybe_multithread(cr_, n, [&](cairo_t* ctx, int start, int stop) {
    if (ctx != cr_) {
      CAIRO_CHECK_SET_USER_DATA(
        cairo_set_user_data, ctx, &detail::STATE_KEY,
        (new std::stack<AdditionalState>{{get_additional_state()}}),
        [](void* data) -> void {
          // Just calling operator delete would not invoke the destructor.
          delete static_cast<std::stack<AdditionalState>*>(data);
        });
    }
    auto cache = PatternCache{simplify_threshold};
    for (auto i = start; i < stop; ++i) {
      auto const& path = paths[i % n_paths];
      auto const& mtx = matrices[i % n_transforms];
      auto x = offsets_raw(i % n_offsets, 0),
           y = offsets_raw(i % n_offsets, 1);
      cairo_matrix_transform_point(&offset_matrix, &x, &y);
      if (!(std::isfinite(x) && std::isfinite(y))) {
        continue;
      }
      if (fcs_raw.shape(0)) {
        auto const& i_mod = i % fcs_raw.shape(0);
        cairo_set_source_rgba(
          ctx, fcs_raw(i_mod, 0), fcs_raw(i_mod, 1),
               fcs_raw(i_mod, 2), fcs_raw(i_mod, 3));
        cache.mask(ctx, path, mtx, draw_func_t::Fill, 0, {}, x, y);
      }
      if (ecs_raw.size()) {
        auto const& i_mod = i % ecs_raw.shape(0);
        cairo_set_source_rgba(
          ctx, ecs_raw(i_mod, 0), ecs_raw(i_mod, 1),
               ecs_raw(i_mod, 2), ecs_raw(i_mod, 3));
        auto const& lw = lws_raw.size()
          ? points_to_pixels_factor * lws_raw[i % lws_raw.size()]
          : cairo_get_line_width(ctx);
        auto const& dash = dashes_raw[i % n_dashes];
        cache.mask(ctx, path, mtx, draw_func_t::Stroke, lw, dash, x, y);
      }
      // NOTE: We drop antialiaseds because that just seems silly.
      // We drop urls as they should be handled in a post-processing step
      // anyways (cairo doesn't seem to support them?).
    }
  });

  get_additional_state().snap = old_snap;
}

// While draw_quad_mesh is technically optional, the fallback is to use
// draw_path_collection, which creates artefacts at the junctions due to
// stamping.
// The spec for this method is overly general; it is only used by the QuadMesh
// class, which does not provide a way to set its offsets (or per-quad
// antialiasing), so we just fall back onto the slow implementation if they are
// set non-trivially.  The mesh_{width,height} arguments are also redundant
// with the coordinates shape.
// FIXME: Check that aas is indeed not set.
void GraphicsContextRenderer::draw_quad_mesh(
  GraphicsContextRenderer& gc,
  py::object master_transform,
  ssize_t mesh_width, ssize_t mesh_height,
  py::array_t<double> coordinates,
  py::array_t<double> offsets,
  py::object offset_transform,
  py::array_t<double> fcs,
  py::object aas,
  py::array_t<double> ecs)
{
  if (&gc != this) {
    throw std::invalid_argument{"non-matching GraphicsContext"};
  }
  auto const& ac = _additional_context();
  auto mtx =
    matrix_from_transform(master_transform, get_additional_state().height);
  auto const& fcs_raw = fcs.unchecked<2>(),
            & ecs_raw = ecs.unchecked<2>();
  if (coordinates.shape(0) != mesh_height + 1
      || coordinates.shape(1) != mesh_width + 1
      || coordinates.shape(2) != 2
      || fcs_raw.shape(0) != mesh_height * mesh_width
      || fcs_raw.shape(1) != 4
      || ecs_raw.shape(1) != 4) {
    throw std::invalid_argument{
      "shapes of coordinates {.shape}, facecolors {.shape}, and "
      "edgecolors {.shape} do not match"_format(coordinates, fcs, ecs)
      .cast<std::string>()};
  }
  if (offsets.ndim() != 2 || offsets.shape(0) != 1 || offsets.shape(1) != 2) {
    renderer_base("draw_quad_mesh")(
      this, master_transform, mesh_height, mesh_width, coordinates,
      offsets, offset_transform, fcs, aas, ecs);
    return;
  }
  auto const& tr_offset =
    offset_transform.attr("transform")(offsets).cast<py::array_t<double>>();
  mtx.x0 += tr_offset.at(0, 0),
  mtx.y0 -= tr_offset.at(0, 1);
  auto coords_raw_keepref =  // Let numpy manage the buffer.
    coordinates.attr("copy")().cast<py::array_t<double>>();
  auto coords_raw = coords_raw_keepref.mutable_unchecked<3>();
  for (auto i = 0; i < mesh_height + 1; ++i) {
    for (auto j = 0; j < mesh_width + 1; ++j) {
      cairo_matrix_transform_point(
        &mtx,
        coords_raw.mutable_data(i, j, 0), coords_raw.mutable_data(i, j, 1));
    }
  }
  // If edge colors are set, we need to draw the quads one at a time in
  // order to be able to draw the edges as well.  If they are not set, using
  // cairo's mesh pattern support instead avoids conflation artefacts.
  // (FIXME[matplotlib]: In fact, it may make sense to rewrite hexbin in terms
  // of quadmeshes in order to fix their long-standing issues with such
  // artefacts.)
  if (ecs_raw.shape(0)) {
    for (auto i = 0; i < mesh_height; ++i) {
      for (auto j = 0; j < mesh_width; ++j) {
        cairo_move_to(
          cr_, coords_raw(i, j, 0), coords_raw(i, j, 1));
        cairo_line_to(
          cr_, coords_raw(i, j + 1, 0), coords_raw(i, j + 1, 1));
        cairo_line_to(
          cr_, coords_raw(i + 1, j + 1, 0), coords_raw(i + 1, j + 1, 1));
        cairo_line_to(
          cr_, coords_raw(i + 1, j, 0), coords_raw(i + 1, j, 1));
        cairo_close_path(cr_);
        auto n = i * mesh_width + j;
        cairo_set_source_rgba(
          cr_, fcs_raw(n, 0), fcs_raw(n, 1), fcs_raw(n, 2), fcs_raw(n, 3));
        cairo_fill_preserve(cr_);
        n %= ecs_raw.shape(0);
        cairo_set_source_rgba(
          cr_, ecs_raw(n, 0), ecs_raw(n, 1), ecs_raw(n, 2), ecs_raw(n, 3));
        cairo_stroke(cr_);
      }
    }
  } else {
    auto const& pattern = cairo_pattern_create_mesh();
    for (auto i = 0; i < mesh_height; ++i) {
      for (auto j = 0; j < mesh_width; ++j) {
        cairo_mesh_pattern_begin_patch(pattern);
        cairo_mesh_pattern_move_to(
          pattern, coords_raw(i, j, 0), coords_raw(i, j, 1));
        cairo_mesh_pattern_line_to(
          pattern, coords_raw(i, j + 1, 0), coords_raw(i, j + 1, 1));
        cairo_mesh_pattern_line_to(
          pattern, coords_raw(i + 1, j + 1, 0), coords_raw(i + 1, j + 1, 1));
        cairo_mesh_pattern_line_to(
          pattern, coords_raw(i + 1, j, 0), coords_raw(i + 1, j, 1));
        auto const& n = i * mesh_width + j;
        auto const& r = fcs_raw(n, 0),
                  & g = fcs_raw(n, 1),
                  & b = fcs_raw(n, 2),
                  & a = fcs_raw(n, 3);
        for (auto k = 0; k < 4; ++k) {
          cairo_mesh_pattern_set_corner_color_rgba(pattern, k, r, g, b, a);
        }
        cairo_mesh_pattern_end_patch(pattern);
      }
    }
    cairo_set_source(cr_, pattern);
    cairo_pattern_destroy(pattern);
    cairo_paint(cr_);
  }
}

void GraphicsContextRenderer::draw_text(
  GraphicsContextRenderer& gc,
  double x, double y, std::string s, py::object prop, double angle,
  bool ismath, py::object /* mtext */)
{
  if (&gc != this) {
    throw std::invalid_argument{"non-matching GraphicsContext"};
  }
  auto const& ac = _additional_context();
  if (ismath) {
    // NOTE: This uses unhinted metrics for parsing/positioning but normal
    // hinting for rendering, not sure whether this is a problem...
    auto const& parse =
      py::cast(this).attr("_text2path").attr("mathtext_parser").attr("parse")(
        s, get_additional_state().dpi, prop);
    auto mb = MathtextBackend{};
    for (auto const& spec: parse.attr("glyphs")) {
      // We must use the character's unicode index rather than the symbol name,
      // because the symbol may have been synthesized by
      // FT2Font::get_glyph_name for a font without FT_FACE_FLAG_GLYPH_NAMES
      // (e.g. arial.ttf), which FT_Get_Name_Index can't know about.
      auto const& [font, size, codepoint, ox, oy] =
        spec.cast<
          std::tuple<py::object, double, unsigned long, double, double>>();
      mb.add_glyph(ox, -oy,
                   font.attr("fname").cast<std::string>(), size,
                   codepoint);
    }
    for (auto const& spec: parse.attr("rects")) {
      auto const& [x1, hy2, w, h] =
        spec.cast<std::tuple<double, double, double, double>>();
      mb.add_rect(x1, -(hy2 + h), x1 + w, -hy2);
    }
    mb.draw(*this, x, y, angle);
  } else {
    auto const& font_face = font_face_from_prop(prop);
    cairo_set_font_face(cr_, font_face);
    cairo_font_face_destroy(font_face);
    auto const& font_size =
      points_to_pixels(prop.attr("get_size_in_points")().cast<double>());
    cairo_set_font_size(cr_, font_size);
    adjust_font_options(cr_);
    auto const& gac = text_to_glyphs_and_clusters(cr_, s);
    // While the warning below perhaps belongs logically to
    // text_to_glyphs_and_clusters, we don't want to also emit the warning in
    // get_text_width_height_descent, so put it here.
    auto bytes_pos = 0, glyphs_pos = 0;
    for (auto i = 0; i < gac.num_clusters; ++i) {
      auto const& cluster = gac.clusters[i];
      auto const& next_bytes_pos = bytes_pos + cluster.num_bytes,
                  next_glyphs_pos = glyphs_pos + cluster.num_glyphs;
      for (auto j = glyphs_pos; j < next_glyphs_pos; ++j) {
        if (!gac.glyphs[j].index) {
          auto missing =
            py::cast(s.substr(bytes_pos, cluster.num_bytes))
            .attr("encode")("ascii", "namereplace");
          warn_on_missing_glyph(missing.cast<std::string>());
        }
      }
      bytes_pos = next_bytes_pos;
      glyphs_pos = next_glyphs_pos;
    }
    // Set the current point (otherwise later texts will just follow,
    // regardless of cairo_translate).  The transformation needs to be
    // set after the call to text_to_glyphs_and_clusters; otherwise the
    // rotation shows up in the cairo_scaled_font_t, and, with raqm>=0.7.2 +
    // freetype>=2.11.0, ends up applied twice.
    cairo_translate(cr_, x, y);
    cairo_rotate(cr_, -angle * std::acos(-1) / 180);
    cairo_move_to(cr_, 0, 0);
    cairo_show_text_glyphs(
      cr_, s.c_str(), s.size(),
      gac.glyphs, gac.num_glyphs,
      gac.clusters, gac.num_clusters, gac.cluster_flags);
  }
}

std::tuple<double, double, double>
GraphicsContextRenderer::get_text_width_height_descent(
  std::string s, py::object prop, py::object ismath)
{
  // - "height" includes "descent", and "descent" is (normally) positive
  // (see MathtextBackendAgg.get_results()).
  // - "ismath" can be True, False, "TeX" (i.e., usetex).
  if (py_eq(ismath, py::cast("TeX")) || ismath.cast<bool>()) {
    // For the ismath (non-tex) case, the base class uses the path mathtext
    // backend here, which is also what we use for rendering.  NOTE: For
    // ismath, Agg reports nonzero descents for seemingly zero-descent cases.
    return
      renderer_base("get_text_width_height_descent")(this, s, prop, ismath)
      .cast<std::tuple<double, double, double>>();
  } else {
    cairo_save(cr_);
    auto const& font_face = font_face_from_prop(prop);
    cairo_set_font_face(cr_, font_face);
    cairo_font_face_destroy(font_face);
    auto const& font_size =
      points_to_pixels(prop.attr("get_size_in_points")().cast<double>());
    cairo_set_font_size(cr_, font_size);
    adjust_font_options(cr_);  // Needed for correct aa.
    cairo_text_extents_t extents;
    auto const& gac = text_to_glyphs_and_clusters(cr_, s);
    cairo_glyph_extents(cr_, gac.glyphs, gac.num_glyphs, &extents);
    cairo_restore(cr_);
    return {
      extents.width + extents.x_bearing,
      extents.height,
      extents.height + extents.y_bearing};
  }
}

void GraphicsContextRenderer::start_filter()
{
  cairo_push_group(cr_);
  new_gc();
}

py::array GraphicsContextRenderer::_stop_filter_get_buffer()
{
  restore();
  auto const& pattern = cairo_pop_group(cr_);
  auto const& state = get_additional_state();
  auto const& raster_surface =
    cairo_image_surface_create(
      get_cairo_format(), int(state.width), int(state.height));
  auto const& raster_cr = cairo_create(raster_surface);
  cairo_set_source(raster_cr, pattern);
  cairo_pattern_destroy(pattern);
  cairo_paint(raster_cr);
  cairo_destroy(raster_cr);
  auto const& buffer = image_surface_to_buffer(raster_surface);
  cairo_surface_destroy(raster_surface);
  return buffer;
}

Region GraphicsContextRenderer::copy_from_bbox(py::object bbox)
{
  auto const& state = get_additional_state();
  auto const& x0o = bbox.attr("x0").cast<double>(),
            & x1o = bbox.attr("x1").cast<double>(),
            // Invert y-axis.
            & y0o = state.height - bbox.attr("y1").cast<double>(),
            & y1o = state.height - bbox.attr("y0").cast<double>();
  // Use ints to avoid a bunch of warnings below.
  // auto const& x0 = int(std::ceil(x0o)),
  //           & x1 = int(std::floor(x1o)),
  //           & y0 = int(std::ceil(y0o)),
  //           & y1 = int(std::floor(y1o));
  // If using floor/ceil, we must additionally clip because of the possibility
  // of floating point inaccuracies.
  auto const& x0 = int(std::max(std::floor(x0o), 0.)),
            & x1 = int(std::min(std::ceil(x1o), state.width - 1.)),
            & y0 = int(std::max(std::floor(y0o), 0.)),
            & y1 = int(std::min(std::ceil(y1o), state.height - 1.));
  // With e.g. collapsed axes, Matplotlib can try to copy e.g. from x0 = 1.1 to
  // x1 = 1.9, in which case x1 < x0 after clipping, hence the x0o <= x1o test
  // and the max(x1 - x0, 0) below.
  if (!(0 <= x0 && x0o <= x1o && x1 <= state.width
        && 0 <= y0 && y0o <= y1o && y1 <= state.height)) {
    throw std::invalid_argument{
      "cannot copy\n{}\ni.e.\n{}\nout of canvas of width {} and height {}"_format(
        bbox, bbox.attr("frozen")(), state.width, state.height)
      .cast<std::string>()};
  }
  auto const width = std::max(x1 - x0, 0),
             height = std::max(y1 - y0, 0);
  // 4 bytes per pixel throughout.
  auto buf = std::unique_ptr<uint8_t[]>{new uint8_t[4 * width * height]};
  auto const& surface = cairo_get_target(cr_);
  if (auto const& type = cairo_surface_get_type(surface);
      type != CAIRO_SURFACE_TYPE_IMAGE) {
    throw std::runtime_error{
      "copy_from_bbox only supports IMAGE surfaces, not {.name}"_format(type)
      .cast<std::string>()};
  }
  auto const& raw = cairo_image_surface_get_data(surface);
  auto const& stride = cairo_image_surface_get_stride(surface);
  for (auto y = y0; y < y1; ++y) {
    std::memcpy(  // Inverted y, directly usable when restoring.
      buf.get() + (y - y0) * 4 * width, raw + y * stride + 4 * x0, 4 * width);
  }
  return {{x0, y0, width, height}, std::move(buf)};
}

void GraphicsContextRenderer::restore_region(Region& region)
{
  auto const& [x0, y0, width, height] = region.bbox;
  auto const& /* x1 = x0 + width, */ y1 = y0 + height;
  auto const& surface = cairo_get_target(cr_);
  if (auto const& type = cairo_surface_get_type(surface);
      type != CAIRO_SURFACE_TYPE_IMAGE) {
    throw std::runtime_error{
      "restore_region only supports IMAGE surfaces, not {.name}"_format(type)
      .cast<std::string>()};
  }
  auto const& raw = cairo_image_surface_get_data(surface);
  auto const& stride = cairo_image_surface_get_stride(surface);
  cairo_surface_flush(surface);
  for (auto y = y0; y < y1; ++y) {
    std::memcpy(  // 4 bytes per pixel.
      raw + y * stride + 4 * x0,
      region.buffer.get() + (y - y0) * 4 * width, 4 * width);
  }
  cairo_surface_mark_dirty_rectangle(surface, x0, y0, width, height);
}

MathtextBackend::Glyph::Glyph(
  std::string path, double size,
  std::variant<char32_t, std::string, FT_ULong> codepoint_or_name_or_index,
  double x, double y,
  double slant, double extend) :
  path{path}, size{size},
  codepoint_or_name_or_index{codepoint_or_name_or_index},
  x{x}, y{y},
  slant{slant}, extend{extend}
{}

MathtextBackend::MathtextBackend() : glyphs_{}, rectangles_{} {}

void MathtextBackend::add_glyph(
  double ox, double oy, std::string filename, double size, char32_t codepoint)
{
  glyphs_.emplace_back(filename, size, codepoint, ox, oy);
}

void MathtextBackend::add_usetex_glyph(
  double ox, double oy, std::string filename, double size,
  std::variant<std::string, FT_ULong> name_or_index,
  double slant, double extend)
{
  auto codepoint_or_name_or_index =
    std::variant<char32_t, std::string, FT_ULong>{};
  std::visit(
    [&](auto name_or_index) { codepoint_or_name_or_index = name_or_index; },
    name_or_index);
  glyphs_.emplace_back(
    filename, size, codepoint_or_name_or_index, ox, oy, slant, extend);
}

void MathtextBackend::add_rect(
  double x1, double y1, double x2, double y2)
{
  rectangles_.emplace_back(x1, y1, x2 - x1, y2 - y1);
}

void MathtextBackend::draw(
  GraphicsContextRenderer& gcr, double x, double y, double angle) const
{
  if (!std::isfinite(x) || !std::isfinite(y)) {
    // This happens e.g. with empty strings, and would put cr in an invalid
    // state. even though nothing is written.
    return;
  }
  auto const& ac = gcr._additional_context();
  auto const& cr = gcr.cr_;
  auto const& dpi = get_additional_state(cr).dpi;
  cairo_translate(cr, x, y);
  cairo_rotate(cr, -angle * std::acos(-1) / 180);
  for (auto const& glyph: glyphs_) {
    auto const& font_face = font_face_from_path(glyph.path);
    cairo_set_font_face(cr, font_face);
    cairo_font_face_destroy(font_face);
    auto const& size = glyph.size * dpi / 72;
    auto const& mtx = cairo_matrix_t{
      size * glyph.extend, 0, -size * glyph.slant * glyph.extend, size, 0, 0};
    cairo_set_font_matrix(cr, &mtx);
    adjust_font_options(cr);
    auto ft_face =
      static_cast<FT_Face>(
        cairo_font_face_get_user_data(font_face, &detail::FT_KEY));
    auto index = FT_UInt{};
    std::visit(overloaded {
      [&](char32_t codepoint) {
        // The last unicode charmap is the FreeType-synthesized one.
        auto i = ft_face->num_charmaps - 1;
        for (; i >= 0; --i) {
          if (ft_face->charmaps[i]->encoding == FT_ENCODING_UNICODE) {
            FT_CHECK(FT_Set_Charmap, ft_face, ft_face->charmaps[i]);
            break;
          }
        }
        if (i < 0) {
          throw std::runtime_error{"no unicode charmap found"};
        }
        index = FT_Get_Char_Index(ft_face, codepoint);
        if (!index) {
          warn_on_missing_glyph("#" + std::to_string(index));
        }
      },
      [&](std::string name) {
        index = FT_Get_Name_Index(ft_face, name.data());
        if (!index) {
          warn_on_missing_glyph(name);
        }
      },
      [&](FT_ULong idx) {
        // For the usetex case, look up the "native" font charmap,
        // which typically has a TT_ENCODING_ADOBE_STANDARD or
        // TT_ENCODING_ADOBE_CUSTOM encoding, unlike the FreeType-synthesized
        // one which has a TT_ENCODING_UNICODE encoding.
        auto found = false;
        for (auto i = 0; i < ft_face->num_charmaps; ++i) {
          if (ft_face->charmaps[i]->encoding != FT_ENCODING_UNICODE) {
            if (found) {
              throw std::runtime_error{"multiple non-unicode charmaps found"};
            }
            FT_CHECK(FT_Set_Charmap, ft_face, ft_face->charmaps[i]);
            found = true;
          }
        }
        if (!found) {
          throw std::runtime_error{"no builtin charmap found"};
        }
        index = FT_Get_Char_Index(ft_face, idx);
        if (!index) {
          warn_on_missing_glyph("#" + std::to_string(index));
        }
      }
    }, glyph.codepoint_or_name_or_index);
    auto const& raw_glyph = cairo_glyph_t{index, glyph.x, glyph.y};
    cairo_show_glyphs(cr, &raw_glyph, 1);
  }
  for (auto const& [x, y, w, h]: rectangles_) {
    cairo_rectangle(cr, x, y, w, h);
    cairo_fill(cr);
  }
}

py::array_t<uint8_t, py::array::c_style> cairo_to_premultiplied_argb32(
  std::variant<py::array_t<uint8_t, py::array::c_style>,
               py::array_t<float, py::array::c_style>> buf)
{
  return std::visit(overloaded {
    [](py::array_t<uint8_t, py::array::c_style> buf) {
      return buf;
    },
    [](py::array_t<float, py::array::c_style> buf) {
      auto f32_ptr = buf.data(0);
      auto const& size = buf.size();
      auto u8 = py::array_t<uint8_t, py::array::c_style>{buf.request().shape};
      auto u32_ptr = reinterpret_cast<uint32_t*>(u8.mutable_data(0));
      for (auto i = 0; i < size; i += 4) {
        auto const& r = *f32_ptr++, g = *f32_ptr++, b = *f32_ptr++, a = *f32_ptr++;
        *u32_ptr++ =
          (uint32_t(uint8_t(a * 0xff)) << 24)
          + (uint32_t(uint8_t(r * 0xff)) << 16)
          + (uint32_t(uint8_t(g * 0xff)) << 8)
          + (uint32_t(uint8_t(b * 0xff)) << 0);
      }
      return u8;
    }
  },
  buf);
}

py::array_t<uint8_t, py::array::c_style> cairo_to_premultiplied_rgba8888(
  std::variant<py::array_t<uint8_t, py::array::c_style>,
               py::array_t<float, py::array::c_style>> buf)
{
  auto u8 = std::visit(overloaded {
    [](py::array_t<uint8_t, py::array::c_style> buf) {
      return
        buf.attr("copy")().cast<py::array_t<uint8_t, py::array::c_style>>();
    },
    [](py::array_t<float, py::array::c_style> buf) {
      return cairo_to_premultiplied_argb32(buf);
    },
  }, buf);
  auto const& size = u8.size();
  // Much faster than `np.take(..., [2, 1, 0, 3] / [1, 2, 3, 0], axis=2)`.
  if (*reinterpret_cast<uint16_t const*>("\0\xff") > 0x100) {  // little-endian
    uint8_t* u8_ptr = u8.mutable_data();
    for (auto i = 0; i < size; i += 4) {
      std::swap(u8_ptr[i], u8_ptr[i + 2]);  // BGRA->RGBA
    }
  } else {  // big-endian
    auto u32_ptr = reinterpret_cast<uint32_t*>(u8.mutable_data());
    for (auto i = 0; i < size / 4; i++) {
      u32_ptr[i] = (u32_ptr[i] << 8) + (u32_ptr[i] >> 24);  // ARGB->RGBA
    }
  }
  return u8;
}

py::array_t<uint8_t, py::array::c_style> cairo_to_straight_rgba8888(
  std::variant<py::array_t<uint8_t, py::array::c_style>,
               py::array_t<float, py::array::c_style>> buf)
{
  auto rgba = cairo_to_premultiplied_rgba8888(buf);
  auto u8_ptr = rgba.mutable_data(0);
  auto const& size = rgba.size();
  for (auto i = 0; i < size; i += 4) {
    auto r = u8_ptr++, g = u8_ptr++, b = u8_ptr++, a = u8_ptr++;
    if (*a != 0xff) {
      // Relying on a precomputed table yields a ~2x speedup, but avoiding the
      // table lookup in the opaque case is still faster.
      auto subtable = &detail::unpremultiplication_table[*a << 8];
      *r = subtable[*r];
      *g = subtable[*g];
      *b = subtable[*b];
    }
  }
  return rgba;
}

PYBIND11_MODULE(_mplcairo, m)
{
  m.doc() = "A cairo backend for matplotlib.";

  // Setup global values.

  if (import_cairo() >= 0) {
    detail::has_pycairo = true;
  } else {
#ifndef _WIN32
    throw py::error_already_set{};
#else
    PyErr_Clear();
#endif
  }

#ifndef _WIN32
  auto const& ctypes = py::module::import("ctypes"),
            & _cairo = py::module::import("cairo._cairo");
  auto const& dll = ctypes.attr("CDLL")(_cairo.attr("__file__"));
  auto const& load_ptr = [&](char const* name) -> uintptr_t {
    return
      ctypes.attr("cast")(
        py::getattr(dll, name, py::int_(0)), ctypes.attr("c_void_p"))
      .attr("value").cast<std::optional<uintptr_t>>().value_or(0);
  };
#else
  auto const& load_ptr = [&](char const* name) -> os::symbol_t {
    return os::dlsym(name);
  };
#endif
#define LOAD_API(name) \
  detail::name = reinterpret_cast<decltype(detail::name)>(load_ptr(#name));
  ITER_CAIRO_OPTIONAL_API(LOAD_API)
#undef LOAD_API

  FT_CHECK(FT_Init_FreeType, &detail::ft_library);

  detail::RC_PARAMS = py::module::import("matplotlib").attr("rcParams");
  detail::PIXEL_MARKER =
    py::module::import("matplotlib.markers").attr("MarkerStyle")(",");

  py::module::import("atexit").attr("register")(
    py::cpp_function{[] {
      FT_Done_FreeType(detail::ft_library);
      // Make sure that these objects don't outlive the Python interpreter.
      // (It appears that sometimes, a weakref callback to the module doesn't
      // get called at shutdown, so if we rely on that approach instead of
      // using `atexit.register`, we may get a segfault when Python tries to
      // deallocate the type objects (via a decref by the C++ destructor) too
      // late in the shutdown sequence.)
      detail::RC_PARAMS = {};
      detail::PIXEL_MARKER = {};
      detail::UNIT_CIRCLE = {};
    }});

  // Export functions.
  m.def(
    "set_options", [](py::kwargs kwargs) -> void {
      auto const& pop_option =
        [&](std::string key, auto dummy) -> std::optional<decltype(dummy)> {
          return
            kwargs.attr("pop")(key, py::none())
            .cast<std::optional<decltype(dummy)>>();
      };
      if (auto const& cairo_circles = pop_option("cairo_circles", bool{})) {
        if (*cairo_circles) {
          detail::UNIT_CIRCLE =
            py::module::import("matplotlib.path").attr("Path")
            .attr("unit_circle")();
        } else {
          Py_XDECREF(detail::UNIT_CIRCLE.release().ptr());
        }
      }
      if (auto const& float_surface = pop_option("float_surface", bool{})) {
        if (cairo_version() < CAIRO_VERSION_ENCODE(1, 17, 2)) {
          throw std::invalid_argument{"float surfaces require cairo>=1.17.2"};
        }
        detail::FLOAT_SURFACE = *float_surface;
      }
      if (auto const& threads = pop_option("collection_threads", int{})) {
        detail::COLLECTION_THREADS = *threads;
      }
      if (auto const& miter_limit = pop_option("miter_limit", double{})) {
        detail::MITER_LIMIT = *miter_limit;
      }
      if (auto const& raqm = pop_option("raqm", bool{})) {
        if (*raqm) {
          load_raqm();
        } else {
          unload_raqm();
        }
      }
      if (auto const& debug = pop_option("_debug", bool{})) {
        detail::DEBUG = *debug;
      }
      if (py::bool_(kwargs)) {
        throw std::runtime_error{
          "unknown options passed to set_options: {}"_format(kwargs)
          .cast<std::string>()};
      }
    }, R"__doc__(
Set mplcairo options.

Note that the defaults below refer to the initial values of the options;
options not passed to `set_options` are left unchanged.

At import time, mplcairo will set the initial values of the options from the
``MPLCAIRO_<OPTION_NAME>`` environment variables (loading them as Python
literals), if any such variables are set.

Parameters
----------
cairo_circles : bool, default: True
    Whether to use cairo's circle drawing algorithm, rather than Matplotlib's
    fixed spline approximation.

collection_threads : int, default: 0
    Number of threads to use to render markers and collections, if nonzero.

float_surface : bool, default: False
    Whether to use a floating point surface (more accurate, but uses more
    memory).

miter_limit : float, default: 10
    Setting for cairo_set_miter_limit__.  If negative, use Matplotlib's (bad)
    default of matching the linewidth.  The default matches cairo's default.

    __ https://www.cairographics.org/manual/cairo-cairo-t.html#cairo-set-miter-limit

raqm : bool, default: if available
    Whether to use Raqm for text rendering.

_debug: bool, default: False
    Whether to print debugging information.  This option is only intended for
    debugging and is not part of the stable API.

Notes
-----
An additional format-specific control knob is the ``MaxVersion`` entry in the
*metadata* dict passed to ``savefig``.  It can take values ``"1.4"``/``"1.5``
(to restrict to PDF 1.4 or 1.5 -- default: 1.5), ``"2"``/``"3"`` (to restrict
to PostScript levels 2 or 3 -- default: 3), or ``"1.1"``/``"1.2"`` (to restrict
to SVG 1.1 or 1.2 -- default: 1.1).
)__doc__");
  m.def(
    "get_options", [] {
      return py::dict(
        "cairo_circles"_a=bool(detail::UNIT_CIRCLE),
        "collection_threads"_a=detail::COLLECTION_THREADS,
        "float_surface"_a=detail::FLOAT_SURFACE,
        "miter_limit"_a=detail::MITER_LIMIT,
        "raqm"_a=has_raqm(),
        "_debug"_a=detail::DEBUG);
    }, R"__doc__(
Get current mplcairo options.  See `set_options` for a description of available
options.
)__doc__");
  m.def(
    "cairo_to_premultiplied_argb32", cairo_to_premultiplied_argb32, R"__doc__(
Convert a buffer from cairo's ARGB32 (premultiplied) or RGBA128F to
premultiplied ARGB32.
)__doc__");
  m.def(
    "cairo_to_premultiplied_rgba8888", cairo_to_premultiplied_rgba8888, R"__doc__(
Convert a buffer from cairo's ARGB32 (premultiplied) or RGBA128F to
premultiplied RGBA8888.
)__doc__");
  m.def(
    "cairo_to_straight_rgba8888", cairo_to_straight_rgba8888, R"__doc__(
Convert a buffer from cairo's ARGB32 (premultiplied) or RGBA128F to
straight RGBA8888.
)__doc__");
  m.def(
    "get_versions", [] {
      auto const& cairo_version = cairo_version_string();
      auto ft_major = 0, ft_minor = 0, ft_patch = 0;
      FT_Library_Version(detail::ft_library, &ft_major, &ft_minor, &ft_patch);
      auto const& freetype_version =
        std::to_string(ft_major) + "."
        + std::to_string(ft_minor) + "."
        + std::to_string(ft_patch);
      auto const& pybind11_version =
        Py_STRINGIFY(PYBIND11_VERSION_MAJOR) "."
        Py_STRINGIFY(PYBIND11_VERSION_MINOR) "."
        Py_STRINGIFY(PYBIND11_VERSION_PATCH);
      auto const& raqm_version =
        has_raqm()
        ? std::optional<std::string>{raqm::version_string()} : std::nullopt;
      auto const& hb_version =
        has_raqm() && hb::version_string
        ? std::optional<std::string>{hb::version_string()} : std::nullopt;
      return py::dict(
        "cairo"_a=cairo_version,
        "freetype"_a=freetype_version,
        "pybind11"_a=pybind11_version,
        "raqm"_a=raqm_version,
        "hb"_a=hb_version);
    }, R"__doc__(
Get library versions.

Only intended for debugging purposes.
)__doc__");
  m.def(
    "install_abrt_handler", os::install_abrt_handler, R"__doc__(
Install a handler that dumps a backtrace on SIGABRT (POSIX only).

Only intended for debugging purposes.
)__doc__");

  // Export classes.
  p11x::bind_enums(m);

  py::class_<Region>(m, "_Region", py::buffer_protocol())
    // Only for patching Agg...
    .def_buffer(&Region::get_straight_rgba8888_buffer_info)  // on mpl 3+.
    .def("to_string_argb", &Region::get_straight_argb32_bytes)  // on mpl 2.2.
    ;

  py::class_<GraphicsContextRenderer>(m, "GraphicsContextRendererCairo")
    // The RendererAgg signature, which is also expected by MixedModeRenderer
    // (with doubles!).
    .def(py::init<double, double, double>())
    .def(py::init<
         py::object, double, double, double, std::tuple<double, double>>())
    .def(py::init<
         StreamSurfaceType, std::optional<py::object>, double, double, double>())
    .def(
      py::pickle(
        [](GraphicsContextRenderer const& gcr) -> py::tuple {
          if (auto const& type = cairo_surface_get_type(cairo_get_target(gcr.cr_));
              type != CAIRO_SURFACE_TYPE_IMAGE) {
            throw std::runtime_error{
              "only renderers to image (not {}) surfaces are picklable"_format(
                type).cast<std::string>()};
          }
          auto const& state = gcr.get_additional_state();
          return py::make_tuple(state.width, state.height, state.dpi);
        },
        [](py::tuple t) -> GraphicsContextRenderer* {
          auto width = t[0].cast<double>(),
               height = t[1].cast<double>(),
               dpi = t[2].cast<double>();
          return new GraphicsContextRenderer{width, height, dpi};
        }))

    // Internal APIs.
    .def(
      "_has_vector_surface",
      [](GraphicsContextRenderer& gcr) -> bool {
        return has_vector_surface(gcr.cr_);
      })
    .def("_set_path", &GraphicsContextRenderer::_set_path)
    .def("_set_metadata", &GraphicsContextRenderer::_set_metadata)
    .def("_set_size", &GraphicsContextRenderer::_set_size)
    .def("_show_page", &GraphicsContextRenderer::_show_page)
    .def("_get_context", &GraphicsContextRenderer::_get_context)
    .def("_get_buffer", &GraphicsContextRenderer::_get_buffer)
    .def("_finish", &GraphicsContextRenderer::_finish)

    // GraphicsContext API.
    .def("set_alpha", &GraphicsContextRenderer::set_alpha)
    .def("set_antialiased", &GraphicsContextRenderer::set_antialiased)
    .def("set_capstyle", &GraphicsContextRenderer::set_capstyle)
    .def("set_clip_rectangle", &GraphicsContextRenderer::set_clip_rectangle)
    .def("set_clip_path", &GraphicsContextRenderer::set_clip_path)
    .def("set_dashes", &GraphicsContextRenderer::set_dashes)
    .def("set_foreground", &GraphicsContextRenderer::set_foreground,
         "fg"_a, "isRGBA"_a=false)
    .def("set_hatch", &GraphicsContextRenderer::set_hatch)
    .def("set_hatch_color", &GraphicsContextRenderer::set_hatch_color)
    .def("set_joinstyle", &GraphicsContextRenderer::set_joinstyle)
    .def("set_linewidth", &GraphicsContextRenderer::set_linewidth)
    .def("set_snap", &GraphicsContextRenderer::set_snap)
    .def("set_url", &GraphicsContextRenderer::set_url)

    // This one function is specific to mplcairo.
    .def(
      "set_mplcairo_operator",
      [](GraphicsContextRenderer& gcr, cairo_operator_t op) -> void {
        cairo_set_operator(gcr.cr_, op);
      })

    .def(
      "get_clip_rectangle",
      [](GraphicsContextRenderer& gcr) -> std::optional<rectangle_t> {
        return gcr.get_additional_state().clip_rectangle;
      })
    .def(
      "get_clip_path",
      [](GraphicsContextRenderer& gcr)
      -> std::tuple<std::optional<py::object>, std::optional<py::object>> {
        auto const& [py_path, path] = gcr.get_additional_state().clip_path;
        (void)path;
        if (py_path) {
          auto it_cls =
            py::module::import("matplotlib.transforms")
            .attr("IdentityTransform");
          return {py_path, it_cls()};
        } else {
          return {{}, {}};
        }
      })
    .def(
      "get_hatch",
      [](GraphicsContextRenderer& gcr) -> std::optional<std::string> {
        return gcr.get_additional_state().hatch;
      })
    .def(
      "get_hatch_color",
      [](GraphicsContextRenderer& gcr) -> rgba_t {
        return gcr.get_additional_state().get_hatch_color();
      })
    .def(
      "get_hatch_linewidth",
      [](GraphicsContextRenderer& gcr) -> double {
        return gcr.get_additional_state().get_hatch_linewidth();
      })
    // Not strictly needed now.
    .def("get_linewidth", &GraphicsContextRenderer::get_linewidth)
    // Needed for patheffects.
    .def("get_rgb", &GraphicsContextRenderer::get_rgb)

    // Slightly hackish, but works.  Avoids having to reproduce the logic in
    // set_sketch_params().
    .def_property(
      "_sketch",
      [](GraphicsContextRenderer& gcr) -> std::optional<py::object> {
        return gcr.get_additional_state().sketch;
      },
      [](GraphicsContextRenderer& gcr, std::optional<py::object> sketch)
      -> void {
        gcr.get_additional_state().sketch = sketch;
      })

    .def("new_gc", &GraphicsContextRenderer::new_gc)
    .def("copy_properties", &GraphicsContextRenderer::copy_properties)
    .def("restore", &GraphicsContextRenderer::restore)

    // Renderer API.
    // Technically unneeded, but exposed by RendererAgg, and useful for us too.
    .def_property_readonly(
      "dpi",
      [](GraphicsContextRenderer& gcr) -> double {
        return gcr.get_additional_state().dpi;
      })
    .def(
      "get_canvas_width_height",
      [](GraphicsContextRenderer& gcr) -> std::tuple<double, double> {
        auto const& state = gcr.get_additional_state();
        return {state.width, state.height};
      })
    // FIXME[matplotlib]: Needed for patheffects and webagg_core, which should
    // use get_canvas_width_height().  Moreover webagg_core wants integers.
    .def_property_readonly(
      "width",
      [](GraphicsContextRenderer& gcr) -> py::object {
        return
          has_vector_surface(gcr.cr_)
          ? py::cast(gcr.get_additional_state().width)
          : py::cast(int(gcr.get_additional_state().width));
      })
    .def_property_readonly(
      "height",
      [](GraphicsContextRenderer& gcr) -> py::object {
        return
          has_vector_surface(gcr.cr_)
          ? py::cast(gcr.get_additional_state().height)
          : py::cast(int(gcr.get_additional_state().height));
      })

    .def("points_to_pixels", &GraphicsContextRenderer::points_to_pixels)

    .def("draw_gouraud_triangles",
         &GraphicsContextRenderer::draw_gouraud_triangles)
    .def("draw_image", &GraphicsContextRenderer::draw_image)
    .def("draw_path", &GraphicsContextRenderer::draw_path,
         "gc"_a, "path"_a, "transform"_a, "rgbFace"_a=nullptr)
    .def("draw_markers", &GraphicsContextRenderer::draw_markers,
         "gc"_a, "marker_path"_a, "marker_trans"_a, "path"_a, "trans"_a,
         "rgbFace"_a=nullptr)
    .def("draw_path_collection",
         &GraphicsContextRenderer::draw_path_collection)
    .def("draw_quad_mesh", &GraphicsContextRenderer::draw_quad_mesh)
    .def("draw_text", &GraphicsContextRenderer::draw_text,
         "gc"_a, "x"_a, "y"_a, "s"_a, "prop"_a, "angle"_a,
         "ismath"_a=false, "mtext"_a=nullptr)
    .def("get_text_width_height_descent",
         &GraphicsContextRenderer::get_text_width_height_descent,
         "s"_a, "prop"_a, "ismath"_a)

    .def("start_filter", &GraphicsContextRenderer::start_filter)
    .def("_stop_filter_get_buffer",
         &GraphicsContextRenderer::_stop_filter_get_buffer)

    // FIXME[matplotlib]: Needed for webagg_core, although we also use it.
    .def(
      "clear",
      [](GraphicsContextRenderer& gcr) -> void {
        auto const& cr = gcr.cr_;
        cairo_save(cr);
        cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
        cairo_paint(cr);
        cairo_restore(cr);
      })

    // Canvas API.
    .def("copy_from_bbox", &GraphicsContextRenderer::copy_from_bbox)
    .def("restore_region", &GraphicsContextRenderer::restore_region)
    ;

  py::class_<MathtextBackend>(m, "MathtextBackendCairo", R"__doc__(
Backend rendering mathtext to a cairo recording surface.
)__doc__")
    .def(py::init<>())
    .def("add_glyph", &MathtextBackend::add_glyph)
    .def("add_usetex_glyph", &MathtextBackend::add_usetex_glyph)
    .def("add_rect", &MathtextBackend::add_rect)
    .def("draw", &MathtextBackend::draw)
    ;
}

}
