#include <pybind11/pybind11.h>
#ifndef PYBIND11_CPP17
  #error "A compiler supporting C++17 is required."
#endif

#include <cairo.h>
#if CAIRO_VERSION < CAIRO_VERSION_ENCODE(1, 11, 4)
  #error "Your versoin of cairo version is too old."
#endif
