#include "_os.h"

#if defined __linux__ || defined __APPLE__
#include <dlfcn.h>
#else
#include <Windows.h>
#endif

namespace mplcairo::os {

#if defined __linux__ || defined __APPLE__
using library_t = void*;
using symbol_t = void*;

library_t dlopen(char const* filename) {
  return ::dlopen(filename, RTLD_LAZY);
}

bool dlclose(library_t handle) {
  return ::dlclose(handle);
}

symbol_t dlsym(library_t handle, char const* symbol) {
  return ::dlsym(handle, symbol);
}

char const* dlerror() {
  return ::dlerror();
}

#elif _WIN32
using library_t = HMODULE;
using symbol_t = FARPROC;

library_t dlopen(char const* filename) {
  return LoadLibrary(filename);
}

bool dlclose(library_t handle) {
  return !FreeLibrary(handle);
}

symbol_t dlsym(library_t handle, char const* symbol) {
  return GetProcAddress(handle, symbol);
}

char const* dlerror() {
  return "";  // FIXME
}

#endif

}
