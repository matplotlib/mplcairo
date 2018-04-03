#include "_raqm.h"

#include "_os.h"

#include <stdexcept>

#include "_macros.h"

namespace mplcairo {

namespace raqm {

namespace {
os::library_t _handle;
}

#define DEFINE_API(name) decltype(raqm_##name)* name{};
ITER_RAQM_API(DEFINE_API)
#undef DEFINE_API

}

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
      throw std::runtime_error(os::dlerror());
    }
    #define DLLOAD_API(name) \
      if (!(raqm::name = \
              reinterpret_cast<decltype(raqm::name)>( \
                os::dlsym(raqm::_handle, "raqm_" #name)))) { \
        os::dlclose(raqm::_handle); \
        raqm::_handle = nullptr; \
        throw std::runtime_error(os::dlerror()); \
      }
    ITER_RAQM_API(DLLOAD_API)
    #undef DLLOAD_API
  }
}

void unload_raqm() {
  if (raqm::_handle) {
    auto const& error = os::dlclose(raqm::_handle);
    raqm::_handle = nullptr;
    if (error) {
      throw std::runtime_error(os::dlerror());
    }
  }
}

bool has_raqm() {
  return raqm::_handle;
}

}
