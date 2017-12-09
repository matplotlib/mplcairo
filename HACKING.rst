C++ style guide
===============

In general, indent by two spaces.

Always break after ``=`` and ``return`` if the rest doesn't fit in one line,
indent the remainder by two spaces.  As an exception, break after the brace in
``auto foo = type{``.

The following vim settings may be useful::

   setlocal cinoptions+=(0,W2,l1,h0
   setlocal shiftwidth=2

Linux notes
===========

Compilation with Clang
----------------------

In order to compile mplcairo with Clang, set the ``CC`` and ``CXX`` environment
variables both to ``clang`` (setuptools uses ``CC`` to compile C++ extensions
but ``CXX`` to link them (don't ask)).  Because `Clang currently doesn't
support libstdc++'s implementation of std::variant <llvm33222>`_, this
automatically forces the use of libc++ as standard library for mplcairo.

.. _llvm33222: https://bugs.llvm.org/show_bug.cgi?id=33222

Meanwhile, mplcairo currently needs to import Matplotlib's ``ft2font``
extension in order to load the FreeType symbols before loading its own
extension module; and ``ft2font`` itself also loads a C++ standard library --
by default, libstdc++ on Linux.  But libstdc++ is currently unable to handle
exceptions thrown from libc++; thus, exception thrown from mplcairo will
``terminate()`` the process.

The workaround is to force the use of libc++ throughout by setting the
``LD_PRELOAD`` environment variable to ``/path/to/libc++.so``.  A better fix
may be possible but I hope instead that Clang will improve its compatibilty
with libstdc++.

Windows notes
=============

C++17 support
-------------

The following C++17 features are missing from MSVC to compile mplcairo (see
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
