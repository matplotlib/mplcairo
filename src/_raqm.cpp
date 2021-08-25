#include "_raqm.h"

#include "_os.h"

#include <stdexcept>

#include "_macros.h"

namespace mplcairo {

namespace raqm {
namespace {
os::library_t _handle;
}
}

#define DEFINE_API(name) decltype(raqm_##name)* raqm::name{};
ITER_RAQM_API(DEFINE_API)
#undef DEFINE_API
bool bad_color_glyph_spacing{};
decltype(hb::version_string) hb::version_string{};

void load_raqm() {
  if (!raqm::_handle) {
    char const* filename =
      #if defined __linux__
        "libraqm.so.0";
      #elif defined __APPLE__
        "libraqm.dylib";
      #elif defined _WIN32
        "libraqm";
      #endif
    raqm::_handle = os::dlopen(filename);
    if (!raqm::_handle) {
      os::throw_dlerror();
    }
    #define LOAD_API(name) \
      if (!(raqm::name = \
              reinterpret_cast<decltype(raqm::name)>( \
                os::dlsym(raqm::_handle, "raqm_" #name)))) { \
        os::dlclose(raqm::_handle); \
        raqm::_handle = nullptr; \
        os::throw_dlerror(); \
      }
    ITER_RAQM_API(LOAD_API)
    #undef LOAD_API
    // See text_to_glyphs_and_clusters for details.  Note that this should
    // really check the version of FreeType *that raqm was ./configure'd
    // against*, but that information is not available, so make do with what we
    // have...
    auto ft_major = 0, ft_minor = 0, ft_patch = 0;
    FT_Library_Version(detail::ft_library, &ft_major, &ft_minor, &ft_patch);
    bad_color_glyph_spacing = !(
      raqm::version_atleast(0, 7, 2)
      && (ft_major > 2 || (ft_major == 2 && ft_minor >= 11)));
    // Trying to retrieve hb_version_string from the raqm shared object
    // normally only works on POSIX, so we just allow this to be nullptr and
    // check that at the call site.
    hb::version_string = reinterpret_cast<decltype(hb::version_string)>(
      os::dlsym(raqm::_handle, "hb_version_string"));
  }
}

void unload_raqm() {
  if (raqm::_handle) {
    auto const& error = os::dlclose(raqm::_handle);
    raqm::_handle = nullptr;
    if (error) {
      os::throw_dlerror();
    }
  }
}

bool has_raqm() {
  return raqm::_handle;
}

}
