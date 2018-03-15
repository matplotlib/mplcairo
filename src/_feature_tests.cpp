#if defined _MSVC_LANG && _MSVC_LANG < 201703 \
  || !defined _MSVC_LANG && __cplusplus < 201703
  #error "A compiler supporting C++17 is required."
#endif

#include <ciso646>
#if defined __clang__ && !defined _LIBCPP_VERSION
  #error "Compilation with Clang requires using libc++, not libstdc++."
#endif

#if !__has_include(<raqm.h>)
  #error "Please download raqm.h and place it in the include/ directory.  " \
         "You may want to use tools/download_raqm_header.py."
#endif
