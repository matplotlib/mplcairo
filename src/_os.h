#ifdef _WIN32
#include <Windows.h>
#endif

namespace mplcairo::os {

#if defined __linux__ || defined __APPLE__
using library_t = void*;
using symbol_t = void*;
#elif defined _WIN32
using library_t = HMODULE;
using symbol_t = FARPROC;
#endif
library_t dlopen(char const* filename);
bool dlclose(library_t handle);
symbol_t dlsym(library_t handle, char const* symbol);
char const* dlerror();

}
