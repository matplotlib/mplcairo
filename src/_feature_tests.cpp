#if __cplusplus < 201703
  #error "A compiler supporting C++17 is required."
#endif

#include <ciso646>
#if defined __clang__ && !defined _LIBCPP_VERSION
  #error "Compilation with Clang requires using libc++, not libstdc++."
#endif

#include <cairo.h>
#if CAIRO_VERSION < CAIRO_VERSION_ENCODE(1, 11, 4)
  #error "Your versoin of cairo version is too old."
#endif
