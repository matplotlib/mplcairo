C++ style guide
===============

In general, indent by two spaces.

Always break after ``=`` and ``return`` if the rest doesn't fit in one line,
indent the remainder by two spaces.  As an exception, break after the brace in
``auto foo = type{``.

The following vim settings may be useful::

   setlocal cinoptions+=(0,W2,l1,h0
   setlocal shiftwidth=2

Compiling on Windows
====================

C++17 support
-------------

The following C++17 features are missing from MSVC to compile ``mplcairo`` (see
http://en.cppreference.com/w/cpp/compiler_support#C.2B.2B17_features):

- guaranteed copy elision.
- template argument deduction for class templates.

cairo
-----

conda-forge's cairo is compiled without freetype support
(https://github.com/conda-forge/cairo-feedstock/issues/26).  One solution is to
use https://github.com/preshing/cairo-windows (manually replacing everything).
For the end user we may want to consider reusing wxPython's windows wheel as
well, as it ships with a cairo and a freetype dll -- so probably has freetype
support...
