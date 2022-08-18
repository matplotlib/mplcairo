====================================
A (new) cairo backend for Matplotlib
====================================

| |GitHub| |PyPI| |Fedora Rawhide|

.. |GitHub|
   image:: https://img.shields.io/badge/github-anntzer%2Fmplcairo-brightgreen
   :target: https://github.com/anntzer/mplcairo
.. |PyPI|
   image:: https://img.shields.io/pypi/v/mplcairo.svg?color=brightgreen
   :target: https://pypi.python.org/pypi/mplcairo
.. |Fedora Rawhide|
   image:: https://repology.org/badge/version-for-repo/fedora_rawhide/python:mplcairo.svg?header=Fedora%20Rawhide
   :target: fedora-package_

.. _fedora-package: https://src.fedoraproject.org/rpms/python-mplcairo

.. contents:: :local:

This is a new, essentially complete implementation of a cairo_ backend for
Matplotlib_.  It can be used in combination with a Qt, GTK, Tk, wx, or macOS
UI, or non-interactively (i.e., to save figure to various file formats).

Noteworthy points include:

.. ... sadly, currently not true.

   - Speed (the backend can be up to ~10× faster than Agg, e.g., when stamping
     circular markers of variable colors).

- Improved accuracy (e.g., with marker positioning, quad meshes, and text
  kerning; floating point surfaces are supported with cairo≥1.17.2).
- Optional multithreaded drawing of markers and path collections.
- Optional support for complex text layout (right-to-left languages, etc.) and
  OpenType font features (see `examples/opentype_features.py`_), and partial
  support for color fonts (e.g., emojis), using Raqm_.  **Note** that Raqm
  depends by default on Fribidi, which is licensed under the LGPLv2.1+.
- Support for embedding URLs in PDF (but not SVG) output (requires
  cairo≥1.15.4).
- Support for multi-page output both for PDF and PS (Matplotlib only supports
  multi-page PDF).
- Support for custom blend modes (see `examples/operators.py`_).
- Improved font embedding in vector formats: fonts are typically subsetted and
  embedded in their native format (Matplotlib≥3.5 also provides improved font
  embedding).

.. _cairo: https://www.cairographics.org/
.. _Matplotlib: http://matplotlib.org/
.. _Raqm: https://github.com/HOST-Oman/libraqm
.. _examples/opentype_features.py: examples/opentype_features.py
.. _examples/operators.py: examples/operators.py

Installation
============

mplcairo requires

- Python≥3.7,
- Matplotlib≥2.2 (declared as ``install_requires``),
- on Linux and macOS, pycairo≥1.16.0 [#]_ (declared as ``install_requires``),
- on Windows, cairo≥1.11.4 [#]_ (shipped with the wheel).

It is recommended to use cairo≥1.17.4.

Additionally, building mplcairo from source requires

- pybind11≥2.6.0 [#]_ (declared as ``setup_requires``),
- pycairo≥1.16.0 (declared as ``setup_requires``).

As usual, install using pip:

.. code-block:: sh

   $ pip install mplcairo  # from PyPI
   $ pip install git+https://github.com/matplotlib/mplcairo  # from Github

Note that wheels are not available for macOS<10.13, because the libc++ included
with these versions is too old and vendoring of libc++ appears to be fragile.
Help for packaging would be welcome.

mplcairo can use Raqm_ (≥0.7.0; ≥0.7.2 is recommended as it provides better
emoji support, especially in the presence of ligatures) for complex text layout
and handling of OpenType font features.  Refer to the instructions on that
project's website for installation on Linux and macOS.  On Windows, consider
using Christoph Gohlke's `build <gohlke-libraqm_>`_ (the directory containing
``libraqm.dll`` and ``libfribidi-0.dll`` need to be added to the `DLL search
path <add_dll_directory_>`_).

.. _gohlke-libraqm: https://www.lfd.uci.edu/~gohlke/pythonlibs/#pillow
.. _add_dll_directory: https://docs.python.org/3/library/os.html#os.add_dll_directory

.. [#] pycairo 1.16.0 added ``get_include()``.

   We do not actually rely on pycairo's Python bindings.  Rather, specifying a
   dependency on pycairo is a convenient way to specify a dependency on cairo
   (≥1.13.1, for pycairo≥1.14.0) itself, and allows us to load cairo at
   runtime instead of linking to it (simplifying the build of self-contained
   wheels).

   On Windows, this strategy is (AFAIK) not possible, so we explicitly link
   against the cairo DLL.

.. [#] cairo 1.11.4 added mesh gradient support (used by ``draw_quad_mesh()``).

   cairo 1.15.4 added support for PDF metadata and links; the presence of this
   feature is detected at runtime.

   cairo 1.17.2 added support for floating point surfaces, usable with
   ``mplcairo.set_options(float_surface=True)``; the presence of this feature
   is detected at runtime.  However, cairo 1.17.2 (and only that version) also
   has a bug that causes (in particular) polar gridlines to be incorrectly
   cropped.  This bug was fixed in 2d1a137.

   cairo 1.17.4 fixed a long-standing rasterization bug (in dfe3aa6).

.. [#] pybind11 2.6.0 is needed to support Python 3.9.

On Fedora, the package is available as `python-mplcairo <fedora-package_>`_.

Building/packaging
==================

This section is only relevant if you wish to build mplcairo yourself, or
package it for redistribution.  Otherwise, proceed to the Use_ section.

In all cases, once the dependencies described below are installed, mplcairo
can be built and installed using any of the standard commands (``pip wheel
--no-deps .``, ``pip install .``, ``pip install -e .`` and ``python setup.py
build_ext -i`` being the most relevant ones).

Unix
----

The following additional dependencies are required:

- a C++ compiler with C++17 support, e.g. GCC≥7.2 or Clang≥5.0.

- cairo and FreeType headers, and pkg-config information to locate them.

  If using conda, they can be installed using ::

     conda install -y -c conda-forge pycairo pkg-config

  as pycairo (also a dependency) depends on cairo, which depends on freetype.
  Note that cairo and pkg-config from the ``anaconda`` channel will *not* work.

  On Linux, they can also be installed with your distribution's package manager
  (Arch: ``cairo``, Debian/Ubuntu: ``libcairo2-dev``, Fedora: ``cairo-devel``).

Raqm (≥0.2) headers are also needed, but will be automatically downloaded if
not found.

Linux
`````

conda's compilers (``gxx_linux-64`` on the ``anaconda`` channel) `currently
interact poorly with installing cairo and pkg-config from conda-forge
<conda-build-2523_>`_, so you are on your own to install a recent compiler
(e.g., using your distribution's package manager).  You may want to set the
``CC`` and ``CXX`` environment variables to point to your C++ compiler if it is
nonstandard [#]_.  In that case, be careful to set them to e.g. ``g++-7`` and
**not** ``gcc-7``, otherwise the compilation will succeed but the shared object
will be mis-linked and fail to load.

The manylinux wheel is built using `tools/build-manylinux-wheel.sh`_.

.. _conda-build-2523: https://github.com/conda/conda-build/issues/2523
.. [#] ``distutils`` uses ``CC`` for *compiling* C++ sources but ``CXX`` for
   linking them (don't ask).  You may run into additional issues if ``CC`` or
   ``CXX`` has multiple words; e.g., if ``CC`` is set to ``ccache g++``, you
   also need to set ``CXX`` to ``ccache gcc``.
.. _tools/build-manylinux-wheel.sh: tools/build-manylinux-wheel.sh

macOS
`````

Clang≥5.0 can be installed from ``conda``'s ``anaconda`` channel (``conda
install -c anaconda clangxx_osx-64``), or can also be installed with Homebrew
(``brew install llvm``).  Note that Homebrew's llvm formula is keg-only, i.e.
it requires manual modifications to the PATH and LDFLAGS (as documented by
``brew info llvm``).

On macOS<10.14, it is additionally necessary to use clang<8.0 (e.g. with ``brew
install llvm@7``) as clang 8.0 appears to believe that code relying on C++17
can only be run on macOS≥10.14+.

The macOS wheel is built using ``tools/build-macos-wheel.sh``, which relies on
delocate-wheel_ (to vendor a recent version of libc++).  Currently, it can only
be built from a Homebrew-clang wheel, not a conda-clang wheel (due to some path
intricacies...).

As I can personally only test the macOS build on CI, any help with the build
and the packaging on that platform would be welcome.

.. _delocate-wheel: https://github.com/matthew-brett/delocate

Windows
-------

The following additional dependencies are required:

- VS2019 (The exact minimum version is unknown, but it is known that mplcairo
  fails to build on the Github Actions ``windows-2016`` agent and requires the
  ``windows-2019`` agent.)

- cairo headers and import and dynamic libraries (``cairo.lib`` and
  ``cairo.dll``) *with FreeType support*.  Note that this excludes, in
  particular, most Anaconda and conda-forge builds: they do not include
  FreeType support.

  The currently preferred solution is to get the headers e.g. from a Linux
  distribution package, the DLL from a pycairo wheel (e.g. from PyPI), and
  generate the import library oneself using ``dumpbin`` and ``lib``.

  Alternatively, very recent conda-forge builds (≥1.16.0 build 1005) do
  include FreeType support.  In order to use them, the include path needs to be
  modified as described below.  (This is currently intentionally disabled by
  default to avoid confusing errors if the cairo build is too old.)

- FreeType headers and import and dynamic libraries (``freetype.lib`` and
  ``freetype.dll``), which can be retrieved from
  https://github.com/ubawurinna/freetype-windows-binaries, or alternatively
  using conda::

     conda install -y freetype

The (standard) |CL|_ and |LINK|_ environment variables (which always get
prepended respectively to the invocations of the compiler and the linker)
should be set as follows::

   set CL=/IC:\path\to\dir\containing\cairo.h /IC:\same\for\ft2build.h
   set LINK=/LIBPATH:C:\path\to\dir\containing\cairo.lib /LIBPATH:C:\same\for\freetype.lib

In particular, in order to use a conda-forge cairo (as described above),
``{sys.prefix}\Library\include\cairo`` needs to be added to the include path.

Moreover, we also need to find ``cairo.dll`` and ``freetype.dll`` and copy
them next to ``mplcairo``'s extension module.  As the dynamic libraries are
typically found next to import libraries, we search the ``/LIBPATH:`` entries
in the ``LINK`` environment variable and copy the first ``cairo.dll`` and
``freetype.dll`` found there.

The script ``tools/build-windows-wheel.py`` automates the retrieval of the
cairo (assuming that pycairo is already installed) and FreeType DLLs, and the
wheel build.

.. |CL| replace:: ``CL``
.. _CL: https://docs.microsoft.com/en-us/cpp/build/reference/cl-environment-variables
.. |LINK| replace:: ``LINK``
.. _LINK: https://docs.microsoft.com/en-us/cpp/build/reference/link-environment-variables

Use
===

On Linux and Windows, mplcairo can be used as any normal Matplotlib backend:
call e.g. ``matplotlib.use("module://mplcairo.qt")`` before importing pyplot,
add a ``backend: module://mplcairo.qt`` line in your ``matplotlibrc``, or set
the ``MPLBACKEND`` environment variable to ``module://mplcairo.qt``.  More
specifically, the following backends are provided:

- ``module://mplcairo.base`` (No GUI, but can output to EPS, PDF, PS, SVG, and
  SVGZ using cairo's implementation, rather than Matplotlib's),
- ``module://mplcairo.gtk`` (GTK widget, copying data from a cairo image
  surface — GTK3 or GTK4 can be selected by calling
  ``gi.require_version("Gtk", "3.0")`` or ``gi.require_version("Gtk", "4.0")``
  before importing the backend),
- ``module://mplcairo.gtk_native`` (GTK widget, directly drawn onto as a
  native surface; does not and cannot support blitting — see above for version
  selection),
- ``module://mplcairo.qt`` (Qt widget, copying data from a cairo image
  surface — select the binding to use by importing it before mplcairo, or by
  setting the ``QT_API`` environment variable),
- ``module://mplcairo.tk`` (Tk widget, copying data from a cairo image
  surface),
- ``module://mplcairo.wx`` (wx widget, copying data from a cairo image
  surface),
- ``module://mplcairo.macosx`` (macOS widget, copying data from a cairo image
  surface).

On macOS, **it is necessary to explicitly import mplcairo before importing
Matplotlib** due to incompatibilities associated with the use of a recent
libc++.  As such, the most practical option is to import mplcairo, then call
e.g. ``matplotlib.use("module://mplcairo.macosx")``.

Jupyter is entirely unsupported (patches would be appreciated).  One
possibility is to set the ``MPLCAIRO_PATCH_AGG`` environment variable to a
non-empty value *before importing Matplotlib*; this fully replaces the Agg
renderer by the cairo renderer throughout Matplotlib.  However, this approach
is inefficient (due to the need of copies and conversions between premultiplied
ARGB32 and straight RGBA8888 buffers); additionally, it does not work with
the wx and macosx backends due to peculiarities of the corresponding canvas
classes.  On the other hand, this is currently the only way in which the
webagg-based backends (e.g., Jupyter's interactive widgets) can use mplcairo.

At import-time, mplcairo will attempt to load Raqm_.  The use of that library
can be controlled and checked using the ``set_options`` and ``get_options``
functions.

The examples_ directory contains a few cases where the output of this renderer
is arguably more accurate than the one of the default renderer, Agg:

- circle_markers.py_ and square_markers.py_: more accurate and faster marker
  stamping.
- marker_stamping.py_: more accurate marker stamping.
- quadmesh.py_: better antialiasing of quad meshes, fewer artefacts with
  masked data.
- text_kerning.py_: improved text kerning.

.. _examples: examples/
.. _circle_markers.py: examples/circle_markers.py
.. _square_markers.py: examples/square_markers.py
.. _marker_stamping.py: examples/marker_stamping.py
.. _quadmesh.py: examples/quadmesh.py
.. _text_kerning.py: examples/text_kerning.py

Benchmarks
==========

Install (in the virtualenv) ``pytest>=3.1.0`` and ``pytest-benchmark``, then
call (e.g.):

.. code-block:: sh

   pytest --benchmark-group-by=fullfunc --benchmark-timer=time.process_time

Keep in mind that conda-forge's cairo is (on my setup) ~2× slower than a
"native" build of cairo.

Test suite
==========

Run ``run-mpl-test-suite.py`` (which depends on ``pytest>=3.2.2``) to run the
Matplotlib test suite with the Agg backend patched by the mplcairo backend.
Note that Matplotlib must be installed with its test data, which is not the
case when it is installed from conda or from most Linux distributions; instead,
it should be installed from PyPI or from source.

Nearly all image comparison tests "fail" as the renderers are fundamentally
different; currently, the intent is to manually check the diff images.  Passing
``--tolerance=inf`` marks these tests as "passed" (while still textually
reporting the image differences) so that one can spot issues not related to
rendering differences.  In practice, ``--tolerance=50`` appears to be enough.

Some other (non-image-comparison) tests are also known to fail (they are listed
in ``ISSUES.rst``, with the relevant explanations), and automatically skipped.

Run ``run-examples.py`` to run some examples that exercise some more aspects of
mplcairo.

Notes
=====

Antialiasing
------------

The artist antialiasing property can be set to any of the ``cairo_antialias_t``
enum values, or ``True`` (the default) or ``False`` (which is synonym to
``NONE``).

Setting antialiasing to ``True`` uses ``FAST`` antialiasing for lines thicker
than 1/3px and ``BEST`` for lines thinner than that: for lines thinner
than 1/3px, the former leads to artefacts such as lines disappearing in
certain sections (see e.g. ``test_cycles.test_property_collision_plot`` after
forcing the antialiasing to ``FAST``).  The threshold of 1/3px was determined
empirically, see `examples/thin_line_antialiasing.py`_.

.. _examples/thin_line_antialiasing.py: examples/thin_line_antialiasing.py

Note that in order to set the ``lines.antialiased`` or ``patch.antialiased``
rcparams to a ``cairo_antialias_t`` enum value, it is necessary to bypass
rcparam validation, using, e.g.

.. code-block:: python

   dict.__setitem__(plt.rcParams, "lines.antialiased", antialias_t.FAST)

The ``text.antialiased`` rcparam can likewise be set to any
``cairo_antialias_t`` enum value, or ``True`` (the default, which maps to
``SUBPIXEL`` — ``GRAY`` is not sufficient to benefit from Raqm_'s subpixel
positioning; see also `cairo issue #152 <cairo-152_>`_) or ``False`` (which
maps to ``NONE``).

.. _cairo-152: https://gitlab.freedesktop.org/cairo/cairo/issues/152

Note that in rare cases, on cairo<1.17.4, ``FAST`` antialiasing can trigger a
"double free or corruption" bug in cairo (`#44 <cairo-44_>`_).  If you hit this
problem, consider using ``BEST`` or ``NONE`` antialiasing (depending on your
quality and speed requirements).

.. _cairo-44: https://gitlab.freedesktop.org/cairo/cairo/issues/44

Fast drawing
------------

For fast drawing of path with many segments, the ``agg.path.chunksize`` rcparam
should be set to e.g. 1000 (see `examples/time_drawing_per_element.py`_ for the
determination of this value); this causes longer paths to be split into
individually rendered sections of 1000 segments each (directly rendering longer
paths appears to have slightly superlinear complexity).

.. _examples/time_drawing_per_element.py: examples/time_drawing_per_element.py

Simplification threshold
------------------------

The ``path.simplify_threshold`` rcparam is used to control the accuracy of
marker stamping, down to an arbitrarily chosen threshold of 1/16px.  If the
threshold is set to a lower value, the exact (slower) marker drawing path will
be used.  Marker stamping is also implemented for scatter plots (which can have
multiple colors).  Likewise, markers of different sizes get mapped into markers
of discretized sizes, with an error bounded by the threshold.

**NOTE**: ``pcolor`` and mplot3d's ``plot_surface`` display some artefacts
where the facets join each other.  This is because these functions internally
use a ``PathCollection``; this triggers the approximate stamping, and
even without it (by setting ``path.simplify_threshold`` to zero), cairo's
rasterization of the edge between the facets is poor.  ``pcolormesh`` (which
internally uses a ``QuadMesh``) should generally be preferred over ``pcolor``
anyways.  ``plot_surface`` could likewise instead represent the surface using
``QuadMesh``, which is drawn without such artefacts.

Font formats and features
-------------------------

In order to use a specific font that Matplotlib may be unable to use, pass a
filename directly:

.. code-block:: python

   from matplotlib.font_manager import FontProperties
   fig.text(.5, .5, "hello, world",
            fontproperties=FontProperties(fname="/path/to/font.ttf"))

or more simply, with Matplotlib≥3.3:

.. code-block:: python

   from pathlib import Path
   fig.text(.5, .5, "hello, world", font=Path("/path/to/font.ttf"))

mplcairo still relies on Matplotlib's font cache, so fonts unsupported by
Matplotlib remain unavailable by other means.

For TTC fonts (and, more generally, font formats that include multiple font
faces in a single file), the *n*\th font (*n*\≥0) can be selected by appending
``#n`` to the filename (e.g., ``"/path/to/font.ttc#1"``).

OpenType font features can be selected by appending ``|feature,...``
to the filename, followed by a `HarfBuzz feature string`_ (e.g.,
``"/path/to/font.otf|frac,onum"``); see `examples/opentype_features.py`_.  A
language_ tag can likewise be set with ``|language=...``; currently, this
always applies to the whole buffer, but a PR adding support for slicing syntax
(similar to font features) would be considered.

.. _HarfBuzz feature string: https://harfbuzz.github.io/harfbuzz-hb-common.html#hb-feature-from-string
.. _language: https://host-oman.github.io/libraqm/raqm-Raqm.html#raqm-set-language

The syntaxes for selecting TTC subfonts and OpenType font features and language
tags are **experimental** and may change, especially if such features are
implemented in Matplotlib itself.

Color fonts (e.g. emojis) are handled.

Multi-page output
-----------------

Matplotlib's ``PdfPages`` class is deeply tied with the builtin ``backend_pdf``
(in fact, it cannot even be used with Matplotlib's own cairo backend).
Instead, use ``mplcairo.multipage.MultiPage`` for multi-page PDF and PS output.
The API is similar:

.. code-block:: python

   from mplcairo.multipage import MultiPage

   fig1 = ...
   fig2 = ...
   with MultiPage(path_or_stream, metadata=...) as mp:
       mp.savefig(fig1)
       mp.savefig(fig2)

See the class' docstring for additional information.

Version control for vector formats
----------------------------------

cairo is able to write PDF 1.4 and 1.5 (defaulting to 1.5), PostScript levels 2
and 3 (defaulting to 3), and SVG versions 1.1 and 1.2 (defaulting to 1.1).
This can be controlled by passing a *metadata* dict to ``savefig`` with a
``MaxVersion`` entry, which must be one of the strings ``"1.4"``/``"1.5"`` (for
pdf), ``"2"``/``"3"`` (for ps), or ``"1.1"``/``"1.2"`` (for svg).

``cairo-script`` output
-----------------------

Setting the ``MPLCAIRO_SCRIPT_SURFACE`` environment variable *before mplcairo
is imported* to ``vector`` or ``raster`` allows one to save figures (with
``savefig``) in the ``.cairoscript`` format, which is a "native script that
matches the cairo drawing model".  The value of the variable determines the
rendering path used (e.g., whether marker stamping is used at all).  This may
be helpful for troubleshooting purposes.

Note that this may crash the process after the file is written, due to `cairo
issue #277 <cairo-277_>`_.

.. _cairo-277: https://gitlab.freedesktop.org/cairo/cairo/issues/277

Markers at Bézier control points
--------------------------------

``draw_markers`` draws a marker at each control point of the given path, which
is the documented behavior, even though all builtin renderers only draw markers
at straight or Bézier segment ends.

Known differences
=================

Due to missing support from cairo:

- SVG output does not support global metadata or set URLs or ids on any
  element, as cairo provides no support to do so.
- PS output does not respect SOURCE_DATE_EPOCH.
- PS output does not support the ``Creator`` metadata key; however it supports
  the ``Title`` key.
- The following rcparams have no effect:

  - ``pdf.fonttype`` (font type is selected by cairo internally),
  - ``pdf.inheritcolor`` (effectively always ``False``),
  - ``pdf.use14corefonts`` (effectively always ``False``),
  - ``ps.fonttype`` (font type is selected by cairo internally),
  - ``ps.useafm`` (effectively always ``False``),
  - ``svg.fonttype`` (effectively always ``"path"``, see `cairo issue #253
    <cairo-253_>`_),
  - ``svg.hashsalt``.

Additionally, the ``quality``, ``optimize``, and ``progressive`` parameters to
``savefig``, which have been removed in Matplotlib 3.5, are not supported.

.. _cairo-253: https://gitlab.freedesktop.org/cairo/cairo/issues/253

Possible optimizations
======================

- Cache eviction policy and persistent cache for ``draw_path_collection``.
- Use QtOpenGLWidget and the cairo-gl backend.

What about the already existing cairo (gtk/qt/wx/tk/...cairo) backends?
=============================================================================

They are very slow (try running `examples/mplot3d/wire3d_animation.py`_) and
render math poorly (try ``title(r"$\sqrt{2}$")``).

.. _examples/mplot3d/wire3d_animation.py: examples/mplot3d/wire3d_animation.py
