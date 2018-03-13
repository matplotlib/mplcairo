====================================
A (new) cairo backend for Matplotlib
====================================

.. contents:: :local:

This is a new, essentially complete implementation of a cairo_ backend for
Matplotlib_.  It can be used in combination with a Qt5, GTK3, Tk, wx, or macOS
UI, or non-interactively (i.e., to save figure to various file formats).

Noteworthy points include:

- Speed (the backend can be up to ~10× faster than Agg, e.g., when stamping
  circular markers of variable colors).
- Support for a wider variety of font formats, such as otf and pfb, for vector
  (PDF, PS, SVG) backends (Matplotlib's Agg backend also supports such fonts).
- Optional support for complex text layout (right-to-left languages, etc.)
  using Raqm_.  **Note** that Raqm depends on Fribidi, which is licensed under
  the LGPLv2.1+.
- Support for embedding URLs in PDF (but not SVG) output (requires
  cairo≥1.15.4).
- Support for multi-page output both for PDF and PS (Matplotlib only supports
  multi-page PDF).

.. _cairo: https://www.cairographics.org/
.. _Matplotlib: http://matplotlib.org/
.. _Raqm: https://github.com/HOST-Oman/libraqm

Installation
============

mplcairo requires

- Python 3 (3.6 on Windows),
- Matplotlib≥2.2 (declared as ``install_requires``),
- pybind11≥2.2 [#]_ (declared as ``install_requires``),
- on Linux and OSX, pycairo≥1.16.0 [#]_ (declared as conditional
  ``install_requires``),
- on Windows, cairo≥1.11.4 [#]_ (shipped with the wheel).

As usual, install using pip::

   python -mpip install mplcairo

.. [#] pybind11 is actually only a build-time requirement, but doesn't play
   well with ``setup_requires``.

.. [#] pycairo 1.16.0 added ``get_include()``.

   We do not actually rely on pycairo's Python bindings.  Rather, specifying a
   dependency on pycairo is a convenient way to specify a dependency on cairo
   (≥1.13.1, for pycairo≥1.14.0) itself, and allows us to load cairo at
   runtime instead of linking to it (simplifying the build of self-contained
   wheels).

   On Windows, this strategy is (AFAIK) not possible, so we explicitly link
   against the cairo DLL.  Moreover, commonly available Windows builds of
   pycairo (Anaconda, conda-forge, Gohlke) do not include FreeType support, and
   are thus unusable anyways.

.. [#] cairo 1.11.4 added mesh gradient support (used by ``draw_quad_mesh()``).

   (cairo 1.15.4 added support for PDF metadata and links; the presence of this
   feature is detected at runtime.)

Building
========

This section is only relevant if you wish to build mplcairo yourself.
Otherwise, proceed to the Use_ section.

In all cases, once the dependencies described below are installed, mplcairo
can be built and installed using any of the standard commands (``pip wheel
--no-deps .``, ``pip install .``, ``pip install -e .`` and ``python setup.py
build_ext -i`` being the most relevant ones).

If the ``MPLCAIRO_USE_LIBRAQM`` environment variable is set, the build also
uses Raqm to perform complex text layout (right-to-left scripts, etc.).  An
installation of Raqm is required; run ``setup.py`` for instructions for Unix
OSes.

Unix
----

The following additional dependencies are required:

- a C++ compiler with C++17 support, e.g. GCC≥7.2 or Clang≥5.0.

- cairo and FreeType headers, and pkg-config information to locate them.  On
  Linux and OSX, if using conda, they can be installed using ::

     conda install -y -c conda-forge pycairo pkg-config

  as pycairo (also a dependency) depends on cairo, which depends on freetype.
  Note that cairo and pkg-config from the anaconda channel will *not* work.

Linux
`````

conda's compilers (``gxx_linux-64`` on the ``anaconda`` channel) currently
interact poorly with installing cairo and pkg-config from conda-forge, so you
are on your own to install a recent compiler (e.g., using your distribution's
package manager).

The manylinux wheel is built using ``tools/build-manylinux.sh``.  It does not
include Raqm.

**NOTE**: On Arch Linux, the python-pillow 5.0.0-1 (Arch) package includes an
invalid version ``raqm.h`` (https://bugs.archlinux.org/task/57492) and must not
be installed while building a Raqm-enabled version of mplcairo using the system
Python, even in a virtualenv (it can be installed when *using* mplcairo without
causing any problems).  One solution is to temporarily uninstall the package;
another one is to package it yourself using e.g. pypi2pkgbuild_.

.. _pypi2pkgbuild: https://github.com/anntzer/pypi2pkgbuild

OSX
```

Clang≥5.0 can be installed from ``conda``'s ``anaconda`` channel (``conda
install -c anaconda clangxx_osx-64``), or can also be installed with Homebrew
(``brew install llvm``).  Note that Homebrew's llvm formula is keg-only, i.e.
it requires manual modifications to the PATH and LDFLAGS (as documented by
``brew info llvm``).

The OSX wheel is built using delocate-wheel_ (to vendor a recent version of
libc++).  Currently, it can only be built from a Homebrew-clang wheel, not a
conda-clang wheel (due to some path intricacies...).  It does not include Raqm.

.. _delocate-wheel: https://github.com/matthew-brett/delocate

Windows
-------

The following additional dependencies are required:

- a "recent enough" version of MSVC (19.13.26128 is sufficient).  (This is the
  reason for restricting support to Python 3.6 on Windows: distutils is able to
  use MSVC 2017 only since Python 3.6.4.)

- FreeType headers, which can e.g. be installed using conda ::

     conda install -y freetype

- a cairo build (the headers, ``cairo.lib``, and ``cairo.dll``) *with FreeType
  support*.  As noted above, this excludes, in particular, the Anaconda,
  conda-forge, or Gohlke builds.  One place from where such a build is
  available is https://github.com/preshing/cairo-windows/releases: download the
  zip file and unpack it.

  Because you will always need to provide cairo yourself, we did not implement
  any special way to configure the location where it will be found.  Instead,
  you **must** set the (standard) |CL|_ and |LINK|_ environment variables
  (which always get prepended respectively to the invocations of the compiler
  and the linker) as follows::

     set CL=/IC:\path\to\directory\containing\cairo.h
     set LINK=/LIBPATH:C\path\to\directory\containing\cairo.lib

  Moreover, we also need to find ``cairo.dll`` and copy it next to
  ``mplcairo``'s extension module.  As ``cairo.dll`` is typically found next to
  ``cairo.lib``, we **explicitly** require the ``LINK`` environment variable to
  use the above format and start with ``/LIBPATH:`` (case-insensitive); we
  always copy ``cairo.dll`` from that directory.

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
- ``module://mplcairo.gtk`` (GTK3 widget, copying data from a cairo image
  surface),
- ``module://mplcairo.gtk_native`` (GTK3 widget, directly drawn onto as a
  native surface; does not and cannot support blitting),
- ``module://mplcairo.qt`` (Qt5 widget, copying data from a cairo image
  surface),
- ``module://mplcairo.tk`` (Tk widget, copying data from a cairo image
  surface),
- ``module://mplcairo.wx`` (wx widget, copying data from a cairo image
  surface),
- ``module://mplcairo.macosx`` (macOS widget, copying data from a cairo image
  surface).

On OSX, **it is necessary to explicitly import mplcairo before importing
Matplotlib** due to incompatibilities associated with the use of a recent
libc++.  As such, the most practical option is to import mplcairo, then call
e.g. ``matplotlib.use("module//mplcairo.macosx")``.

To use cairo rendering in Jupyter's ``inline`` mode, patch

.. code-block:: python

   ipykernel.pylab.backnd_inline.new_figure_manager = \
       mplcairo.base.new_figure_manager

Alternatively, set the ``MPLCAIRO_PATCH_AGG`` environment variable to a
non-empty value to fully replace the Agg renderer by the cairo renderer
throughout Matplotlib.  However, this approach is inefficient (due to the need
of copies and conversions between premultiplied ARGB32 and non-premultiplied
RGBA8888 buffers); additionally, it does not work with the wx and macosx
backends due to peculiarities of the corresponding canvas classes.  On the
other hand, this is currently the only way in which the webagg-based backends
(e.g., Jupyter's inline widget) are supported.

The ``examples`` directory contains a few cases where the output of this
renderer is arguably more accurate than the one of the default renderer, Agg:

- ``circle_markers.py`` and ``square_markers.py``: more accurate and faster
  marker stamping.
- ``markevery.py``: more accurate marker stamping.
- ``quadmesh.py``: better antialiasing of quad meshes, fewer artefacts with
  masked data.
- ``text_kerning.py``: improved text kerning.

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

Run ``run-mpl-test-suite.py`` to run the Matplotlib test suite with
the Agg backend patched by the mplcairo backend.  Matplotlib *must* be
editably-installed from a git checkout.  Certain tests that are known to fail
(and listed in ``ISSUES.rst``) are automatically skipped.

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
empirically, see ``examples/thin_line_antialiasing.py``.

Note that in order to set the ``lines.antialiased`` or ``patch.antialiased``
rcparams to a ``cairo_antialias_t`` enum value, it is necessary to bypass
rcparam validation, using, e.g.

.. code-block:: python

   dict.__setitem__(plt.rcParams, "lines.antialiased", antialias_t.FAST)

The ``text.antialiased`` rcparam can likewise be set to any
``cairo_antialias_t`` enum value, or ``True`` (the default, which maps to
``GRAY`` due to `cairo bug #99021 <cairo-99021_>`_) or ``False`` (which maps to
``NONE``).

.. _cairo-99021: https://bugs.freedesktop.org/show_bug.cgi?id=99021

Fast drawing
------------

For fast drawing of path with many segments, the ``agg.path.chunksize`` rcparam
should be set to 1000 (see ``examples/time_drawing_per_element.py`` for the
determination of this value); this causes longer paths to be split into
individually rendered sections of 1000 segments each (directly rendering longer
paths appears to have slightly superlinear complexity).

Simplification threshold
------------------------

The ``path.simplify_threshold`` rcparam is used to control the accuracy of
marker stamping, down to an arbitrarily chosen threshold of 1/16px.  Values
lower than that will use the exact (slower) marker drawing path.  Marker
stamping is also implemented for scatter plots (which can have multiple
colors).  Likewise, markers of different sizes get mapped into markers of
discretized sizes, with an error bounded by the threshold.

**NOTE**: ``pcolor`` and mplot3d's ``plot_surface`` display some artifacts
where the facets join each other.  This is because these functions internally
use a ``PathCollection``, thus triggering the approximate stamping.
``pcolor`` should be deprecated in favor of ``pcolormesh`` (internally using
a ``QuadMesh``), and ``plot_surface`` should likewise instead represent the
surface using ``QuadMesh``, which is drawn without such artefacts.

Font formats
------------

In order to use a specific font that Matplotlib may be unable to use, pass a
filename directly:

.. code-block:: python

   from matplotlib.font_manager import FontProperties
   ax.text(.5, .5, "hello, world", fontproperties=FontProperties(fname="..."))

mplcairo still relies on Matplotlib's font cache, so fonts unsupported by
Matplotlib remain unavailable by other means.  Matplotlib's current FreeType
wrapper also limits the use of ttc collections to the first font in the
collection.

Note that Matplotlib's (default) Agg backend will handle such fonts equally
well (ultimately, both backends relies on FreeType for rasterization).  It
is Matplotlib's vector backends (PS, PDF, and, for pfb fonts, SVG) that do
not support these fonts, whereas mplcairo support these fonts in all output
formats.

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
   with MultiPage(path_or_stream) as mp:
       mp.savefig(fig1)
       mp.savefig(fig2)

``cairo-script`` output
-----------------------

Setting the ``MPLCAIRO_DEBUG`` environment variable to a non-empty value allows
one to save figures (with ``savefig``) in the ``.cairoscript`` format, which is
a "native script that matches the cairo drawing model".  This may be helpful
for troubleshooting purposes.

Note that this may crash the process after the file is written, due to `cairo
bug #104410 <cairo-104410_>`_.

.. _cairo-104410: https://bugs.freedesktop.org/show_bug.cgi?id=104410

Markers at Bézier control points
--------------------------------

``draw_markers`` draws a marker at each control point of the given path, which
is the documented behavior, even though all builtin renderers only draw markers
at straight or Bézier segment ends.

Known issues
============

Missing implementation
----------------------

Support for the following features is missing:

- the ``svg.image_inline`` rcparam.
- the deprecated ``svg.image_noscale`` rcparam.

Missing support from cairo
--------------------------

- SVG output does not set URLs on any element, as cairo provides no support for
  doing so.
- PS output does not respect SOURCE_DATE_EPOCH.
- The following rcparams have no effect: ``pdf.fonttype``,
  ``pdf.use14corefonts``, ``ps.fonttype``, ``ps.useafm``, ``svg.fonttype``,
  ``svg.hashsalt``.

Possible optimizations
======================

- Cache eviction policy and persistent cache for ``draw_path_collection``.
- Path simplification (although cairo appears to use vertex reduction and
  Douglas-Peucker internally?).
- mathtext should probably hold onto a vector of ``FT_Glyph``\s instead of
  reloading a ``FT_Face`` for each glyph, but that'll likely wait for the ft2
  rewrite in Matplotlib itself.
- Use QtOpenGLWidget and the cairo-gl backend.
- ``hexbin`` currently falls back on the slow implementation due to its use of
  the ``offset_position`` parameter.  This should be fixed on Matplotlib's
  side.

What about the already existing cairo (gtk3cairo) backend?
==========================================================

It is slow (try running ``examples/mplot3d/wire3d_animation.py``), buggy (try
calling ``imshow``, especially with an alpha channel), and renders math poorly
(try ``title(r"$\sqrt{2}$")``).
