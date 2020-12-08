#include "_os.h"

#if defined __linux__ || defined __APPLE__
  #include <dlfcn.h>
  #include <execinfo.h>
  #include <signal.h>
#elif defined _WIN32
  #include <memory>
  #define NOMINMAX
  #include <psapi.h>
  #include <Windows.h>
#endif

#include <pybind11/pybind11.h>

namespace mplcairo::os {

namespace py = pybind11;

#if defined __linux__ || defined __APPLE__
using library_t = void*;
using symbol_t = void*;

library_t dlopen(char const* filename)
{
  return ::dlopen(filename, RTLD_LAZY);
}

bool dlclose(library_t handle)
{
  return ::dlclose(handle);
}

symbol_t dlsym(library_t handle, char const* symbol)
{
  return ::dlsym(handle, symbol);
}

void throw_dlerror()
{
  PyErr_SetString(PyExc_OSError, ::dlerror());
  throw py::error_already_set{};
}

void install_abrt_handler()
{
  signal(SIGABRT, [](int signal) {
    auto buf = std::array<void*, 64>{};
    auto size = backtrace(buf.data(), 64);
    fprintf(stderr, "Error: signal %d:\n", signal);
    backtrace_symbols_fd(buf.data(), size, STDERR_FILENO);
    exit(1);
  });
}

#elif defined _WIN32
using library_t = HMODULE;
using symbol_t = FARPROC;

library_t dlopen(char const* filename)
{
  // Respect os.add_dll_directory.
  return LoadLibraryExA(filename, nullptr, LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
}

bool dlclose(library_t handle)
{
  return !FreeLibrary(handle);
}

symbol_t dlsym(library_t handle, char const* symbol)
{
  return GetProcAddress(handle, symbol);
}

symbol_t dlsym(char const* symbol)
{
  auto hProcess = GetCurrentProcess();
  auto cbNeeded = DWORD{};
  EnumProcessModules(hProcess, nullptr, 0, &cbNeeded);
  auto n_modules = cbNeeded / sizeof(HMODULE);
  auto lphModule = std::unique_ptr<HMODULE[]>{new HMODULE[n_modules]};
  if (EnumProcessModules(hProcess, lphModule.get(), cbNeeded, &cbNeeded)) {
    for (auto i = 0; i < n_modules; ++i) {
      if (auto proc = GetProcAddress(lphModule[i], symbol)) {
        return proc;
      }
    }
  }
  return nullptr;
}

void throw_dlerror()
{
  PyErr_SetFromWindowsErr(0);
  throw py::error_already_set{};
}

void install_abrt_handler()
{
}

#endif

}
