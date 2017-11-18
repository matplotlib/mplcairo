====================================
A (new) cairo backend for Matplotlib
====================================

.. contents:: :local:

This is a new, essentially complete implementation of a cairo_ backend for
Matplotlib_.  It can be used in combination with a Qt5, GTK3, Tk, or wx UI, or
non-interactively (i.e., to save figure to various file formats).

This implementation "passes" Matplotlib's entire image comparison test suite
-- after accounting for inevitable differences in rasterization, and with a few
exceptions noted below.

Noteworthy points include:

- Speed (the backend can be up to ~10× faster than Agg, e.g., when stamping
  circular markers of variable colors).
- Vector backends (PDF, PS, SVG) support a wider variety of font formats, such
  as otf and pfb.
- Optional support for complex text layout (right-to-left languages, etc.)
  using Raqm_.
- Support for multi-page output both for PDF and PS (Matplotlib only supports
  multi-page PDF).

Currently, only Linux and OSX are supported.  Windows support is missing due to
lack of full C++17 support by MSVC.

.. _cairo: https://www.cairographics.org/
.. _Matplotlib: http://matplotlib.org/
.. _Raqm: https://github.com/HOST-Oman/libraqm

Installation
============

Dependencies:

- Python 3,
- Matplotlib:

  * ≥2.1.0rc1 for GTK3 or non-interactive backends,
  * with Matplotlib PR#9202 for Qt5 or wx,
  * (with a yet un-PR'd Matplotlib patch for Tk).

- pycairo≥1.12 [#]_,
- pybind11≥2.2, automatically installed [#]_.

All code examples below assume that the appropriate conda environment is active
(pycairo is not available as a wheel, so conda is the simplest option).

.. code-block:: sh

   export PIP_CONFIG_FILE=/dev/null  # Just to be sure.

   # Installing numpy from conda saves us the need to build it ourselves.
   # Strictly speaking, PyQt is only needed if you want to use an interactive
   # backend.
   conda install -y -c conda-forge pycairo numpy pyqt

   git clone https://github.com/matplotlib/matplotlib.git
   (cd matplotlib
    git fetch origin pull/9202/head:pr/9202
    git checkout pr/9202
    pip install -e .)

   # Download the wheel from Github releases -- pick either Linux or OSX.
   pip install /path/to/mplcairo-*.whl

.. [#] We do not actually rely on pycairo's Python bindings.  Rather,
   specifying a dependency on pycairo is a convenient way to specify a
   dependency on cairo itself, and allows us to load cairo at runtime
   instead of linking to it (which is problematic for a manylinux wheel).

   cairo 1.12 brings in mesh gradient support, which is used by
   ``draw_quad_mesh``.

.. [#] pybind11 is technically only a build-time requirement, but I'd rather
   not use ``setup_requires``.

**NOTE**: Matplotlib builds with the "local FreeType" option set (i.e.,
with the ``MPLLOCALFREETYPE`` environment variable set, or with the
``local_freetype`` entry set in ``setup.cfg``) are **not** supported.  This
option will statically link to a fixed version of FreeType, which may be
different from the version of FreeType that cairo is built against, causing
binary incompatibilites.  In particular, PyPI wheels are built with this
option, and are thus (unfortunately) **not** supported.

Building
========

In order to build mplcairo yourself, the following additional dependencies are
required:

- a C++ compiler with C++17 support, e.g. GCC≥7.1 or clang≥5.0.
- cairo and FreeType headers, and pkg-config information to locate them.

If the ``MPLCAIRO_USE_LIBRAQM`` environment variable is set, the build also
uses Raqm to perform complex text layout (right-to-left scripts, etc.).  An
installation of Raqm is required; run ``setup.py`` for instructions.

A suitably patched Matplotlib should first be installed as documented above.

Linux
-----

Dependencies are available on conda-forge.

.. code-block:: sh

   # cairo and pkg-config from the anaconda channel will *not* work.
   conda install -y -c conda-forge cairo pkg-config
   conda install -y -c anaconda gxx_linux-64\>=7.1

   # The environment needs to be reactivated for the compiler paths to be set.
   source activate "$CONDA_DEFAULT_ENV"

   git clone https://github.com/anntzer/mplcairo.git
   (cd mplcairo
    pip install -e .)

On a related note, the manylinux wheel is built using
``tools/build-manylinux.sh``.

OSX
---

Clang≥5.0 can be installed with Homebrew (``brew install llvm``).  Note that
the llvm formula is keg-only, i.e. it requires manual modifications to the PATH
and LDFLAGS (as documented by ``brew info llvm``).  Other dependencies are
available on conda-forge.

.. code-block:: sh

   conda install -y -c conda-forge cairo pkg-config

   git clone https://github.com/anntzer/mplcairo.git
   (cd mplcairo
    pip install -e .)

The OSX wheel is then built using delocate-wheel_ (to package a recent version
of libc++).

.. _delocate-wheel: https://github.com/matthew-brett/delocate

Use
===

The backend can be selected by setting the ``MPLBACKEND`` environment variable
to one of

- ``module://mplcairo.qt`` (Qt5 widget, copying data from a cairo image
  surface),
- ``module://mplcairo.tk`` (Tk widget, copying data from a cairo image
  surface),
- ``module://mplcairo.wx`` (wx widget, copying data from a cairo image
  surface),
- ``module://mplcairo.gtk_native`` (GTK3 widget, directly drawn onto as a
  native surface),
- ``module://mplcairo.base`` (No GUI, but can output to EPS, PDF, PS, SVG, and
  SVGZ using cairo's implementation, rather than Matplotlib's).  This backend
  can be used with Matplotlib 2.1.

Alternatively, set the ``MPLCAIRO_PATCH_AGG`` environment variable to a
non-empty value to fully replace the Agg renderer by the cairo renderer
throughout Matplotlib.  However, this approach is *much* less efficient, due to
the need of copies and conversions between various formats); additionally, it
does not work with wx due to the non-standard signature of the wx canvas class.

The ``examples`` folder contains a few cases where the output of this renderer
is arguably more accurate than the one of the default renderer, Agg:

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

Run ``run-mpl-test-suite.py`` to run the Matplotlib test suite with the Agg
backend patched by the mplcairo backend.

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

(Support for ``text.antialiased`` is not implemented yet, mostly because we
need to decide on whether to map ``True`` to ``GRAY`` or ``SUBPIXEL``.)

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

Markers at Bézier control points
--------------------------------

``draw_markers`` draws a marker at each control point of the given path, which
is the documented behavior, even though all builtin renderers only draw markers
at straight or Bézier segment ends.

Known issues
============

- Blitting-based animations to image-base backends (e.g., ``mplcairo.qt``)
  leaves small artefacts at the edges of the blitted region.  This does not
  affect Xlib-based backends (e.g., ``mplcairo.gtk_native``).

- SVG and Xlib (i.e, GTK3) currently need to rasterize mathtext before
  rendering it (this is mostly an issue for SVG, altough it affects vertical
  hinting for Xlib), as otherwise replaying a recording surface appears to have
  no effect.  This needs to be investigated.

  Meanwhile, a workaround is to generate files in PS format and convert them to
  SVG e.g. using

  .. code-block:: sh

      inkscape --without-gui input.ps --export-plain-svg output.svg

  Rendering of hinted mathtext is *extremely* slow on Xlib (GTK3).  This may
  be partially fixed by setting the ``text.hinting`` rcparam to ``"none"``, or
  by implementing a rasterization cache (but it would be preferable to fix the
  general issue with recording surfaces first).

- SVG output does not set URLs on any element, as cairo provides no support for
  doing so.

Possible optimizations
======================

- Cache eviction policy and persistent cache for ``draw_path_collection``.
- Path simplification (although cairo appears to use vertex reduction and
  Douglas-Peucker internally?).
- mathtext rendering currently reloads a ``FT_Face`` for each glyph, as
  artefacts appear when reusing the instance in ``FT2Font``.  This needs to be
  investigated; as a workaround, one could also cache the newly constructed
  ``FT_Face``\s.
- Use QtOpenGLWidget and the cairo-gl backend.
- ``hexbin`` currently falls back on the slow implementation due to its use of
  the ``offset_position`` parameter.  This should be fixed on Matplotlib's
  side.

What about the already existing cairo (gtk3cairo) backend?
==========================================================

It is slow (try running ``examples/mplot3d/wire3d_animation.py``), buggy (try
calling ``imshow``, especially with an alpha channel), and renders math poorly
(try ``title(r"$\sqrt{2}$")``).
