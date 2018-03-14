#include "_os.h"

#if defined __linux__ || defined __APPLE__
#include <dlfcn.h>
#endif

namespace mplcairo::os {

#if defined __linux__ || defined __APPLE__
void* dlopen(char const* filename) {
  return ::dlopen(filename, RTLD_LAZY);
}

bool dlclose(void* handle) {
  return ::dlclose(handle);
}

void* dlsym(void* handle, char const* symbol) {
  return ::dlsym(handle, symbol);
}

char const* dlerror() {
  return ::dlerror();
}

#elif _WIN32
void* dlopen(char const* filename) {
  return LoadLibrary(filename);
}

bool dlclose(void* handle) {
  return !FreeLibrary(handle);
}

void* dlsym(void* handle, char const* symbol) {
  return GetProcAddress(handle, symbol);
}

char const* dlerror() {
  return "";  // FIXME
}

#endif

}
