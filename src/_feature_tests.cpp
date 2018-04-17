#if defined _MSVC_LANG && _MSVC_LANG < 201703 \
  || !defined _MSVC_LANG && __cplusplus < 201703
  #error "A compiler supporting C++17 is required."
#endif

#include <ciso646>
#if defined __clang__ && !defined _LIBCPP_VERSION
  #error "Compilation with Clang requires using libc++, not libstdc++."
#endif
