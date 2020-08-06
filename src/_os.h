#include <iostream>

#ifdef _WIN32
#define NOMINMAX
#include <Windows.h>
#endif

namespace mplcairo::os {

#if defined __linux__ || defined __APPLE__
using library_t = void*;
using symbol_t = void*;
#elif defined _WIN32
using library_t = HMODULE;
using symbol_t = FARPROC;

// Like dlsym, but looks through everything listed by EnumProcessModules.
symbol_t dlsym(char const* symbol);
#endif
library_t dlopen(char const* filename);
bool dlclose(library_t handle);
symbol_t dlsym(library_t handle, char const* symbol);
void throw_dlerror();

void install_abrt_handler();

}
