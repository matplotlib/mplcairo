#pragma once

#include <iostream>

#ifdef _WIN32
#define NOMINMAX
#define UNICODE
#define _UNICODE
#include <Windows.h>
#endif

namespace mplcairo::os {

#if defined __linux__ || defined __APPLE__
using library_t = void*;
using symbol_ptr_t = void*;
#elif defined _WIN32
using library_t = HMODULE;
using symbol_ptr_t = FARPROC;
#endif

class symbol_t {
  symbol_ptr_t ptr;

  public:
  // Allow implicit cast from int (address) but not to int (only to pointers).
  template<typename T> symbol_t(T ptr) : ptr{reinterpret_cast<symbol_ptr_t>(ptr)} {}
  template<typename T> operator T*() const { return reinterpret_cast<T*>(ptr); }
};

library_t dlopen(char const* filename);
bool dlclose(library_t handle);
symbol_t dlsym(library_t handle, char const* symbol);
void throw_dlerror();
std::string dladdr_fname(symbol_t handle);

void install_abrt_handler();

#if defined _WIN32
// Like dlsym, but looks through everything listed by EnumProcessModules.
symbol_t dlsym(char const* symbol);
#endif

}
