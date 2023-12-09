#include <pybind11/pybind11.h>
#ifndef PYBIND11_CPP17
  #error "A compiler supporting C++17 is required."
#endif

#include <cairo.h>
#if CAIRO_VERSION < CAIRO_VERSION_ENCODE(1, 13, 1)
  #error "cairo>=1.13.1 is required."
#endif
