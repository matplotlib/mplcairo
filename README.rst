A (new) cairo backend for Matplotlib
====================================

This is a new, near-complete implementation of a cairo backend for Matplotlib.
Currently, it can be used with either the Qt backend proposed in Matplotlib's
PR #8771, or the Gtk3 backend merged with PR #8772.

This implementation “passes” Matplotlib's entire image comparison test suite
-- after accounting for inevitable differences in rasterization, and with the
exceptions noted below.

Depending on the specific task, the backend can be up to ~10× faster (e.g.,
when stamping circular markers of variable colors) than Agg.

Installation (Linux only)
-------------------------

Dependencies:

- Python 3,
- cairo≥1.12 (needed for mesh gradient support),
- a C++ compiler with C++17 support, e.g. GCC≥7.1.

Such dependencies are available on conda and conda-forge, although the
conda-forge build of cairo is (on my setup) ~2× slower than a “native” build.
Using conda, the following commands will build and install mpl_cairo.

.. code-block:: sh

   export PIP_CONFIG_FILE=/dev/null  # Just to be sure.

   # Unfortunately, the g++ install from rdonnelly/gxx_linux-64 sets some
   # include paths incorrectly, making it impossible to build Matplotlib with
   # it.  So, we build a Matplotlib wheel using the system compiler, and later
   # install it into the conda environment.
   git clone https://github.com/matplotlib/matplotlib.git
   (cd matplotlib
    git pull origin pull/8771/head:pr/8771
    git checkout pr/8771
    python setup.py bdist_wheel)

   # - pkg-config and cairo from the anaconda channel will *not* work; thus, we
   #   may as well install everything from conda-forge.  Note that the Python
   #   version here needs to match the Python version used to build the
   #   Matplotlib wheel; thus, you may need to build the wheel in its own
   #   environment as well.
   # - numpy could be built from source too but conda saves us some time.
   # - PyQt is necessary to have an interactive backend (PyGObject, i.e. Gtk3,
   #   can also be used, but it is not conda-installable).
   conda create -y -n mpl_cairo -c conda-forge \
       python=3.6 pkg-config cairo pybind11\>=2.1 numpy pyqt
   conda install -y -n mpl_cairo -c rdonnelly gxx_linux-64\>=7.1

   # Activation needs to happen *after* installing gcc_linux-64\>=7.1
   source activate mpl_cairo

   pip install matplotlib/dist/*.whl

   git clone https://github.com/anntzer/mpl_cairo.git
   (cd mpl_cairo
    pip install -ve .)

.. warning::

   Do *not* build matplotlib with the “local FreeType” option set (i.e., do not
   set the ``MPLLOCALFREETYPE`` environment variable, and do not set the
   ``local_freetype`` entry in ``setup.cfg``).  This option will statically
   link to a fixed version of FreeType, which may be different from the version
   of FreeType cairo is built against, causing binary incompatibilites.

Then, the backend can be selected by setting the ``MPLBACKEND`` environment
variable one of

- ``module://mpl_cairo.qt`` (Qt5 GUI),
- ``module://mpl_cairo.gtk3`` (GTK3 GUI),
- ``module://mpl_cairo.base`` (No GUI, but can output to EPS, PDF, PS, SVG, and
  SVGZ using cairo's implementation, rather than Matplotlib's).

Alternatively, set the ``MPLCAIRO`` environment variable to a non-empty value
to fully replace the Agg renderer by the cairo renderer throughout Matplotlib
(but plotting is *much* less efficient in that case, due to the need of copies
and conversions between various formats).

The ``examples`` folder contains a few cases where the output of this renderer
is arguably more accurate than the one of the default renderer, Agg:

- ``circle_markers.py`` and ``square_markers.py``: more accurate and faster
  marker stamping.
- ``markevery.py``: more accurate marker stamping.
- ``quadmesh.py``: better antialiasing of quad meshes, fewer artefacts with
  masked data.
- ``text_kerning.py``: improved text kerning.

Benchmarks
----------

Install (in the virtualenv) ``pytest>=3.1.0`` and ``pytest-benchmark``, then
call (e.g.):

.. code-block:: sh

   pytest --benchmark-group-by=fullfunc --benchmark-timer=time.process_time

Keep in mind that conda-forge's cairo is (on my setup) ~2× slower than a
“native” build of cairo.

Test suite
----------

Run ``run-mpl-test-suite.py`` *from the Matplotlib source folder* to run the
subset of matplotlib tests that rely on png image comparison, while using this
backend.  Pass command-line options as you would to pytest, although ``-k``
must not be compressed with another short argument.

Notes
-----

- The artist antialiasing property can be set to any of the
  ``cairo_antialias_t`` enum values, or ``True`` (the default) or ``False``
  (which is synonym to ``NONE``).

  Setting antialiasing to ``True`` uses ``FAST`` antialiasing for lines thicker
  than 1/3px and ``BEST`` for lines thinner than that: for lines thinner
  than 1/3px, the former leads to artefacts such as lines disappearing in
  certain sections (see e.g. ``test_cycles.test_property_collision_plot`` after
  forcing the antialiasing to ``FAST``).  The threshold of 1/3px was determined
  empirically, see ``examples/thin_line_antialiasing.py``.

- For fast drawing of path with many segments, the ``agg.path.chunksize``
  rcparam should be set to 1000 (see ``examples/time_drawing_per_element.py``
  for the determination of this value); this causes longer paths to be split
  into individually rendered sections of 1000 segments each (directly rendering
  longer paths appears to have slightly superlinear complexity).

  Note that in order to set the ``lines.antialiased`` or ``patch.antialiased``
  rcparams to a ``cairo_antialias_t`` enum value, it is necessary to bypass
  rcparam validation, using e.g.::

     dict.__setitem__(plt.rcParams, "lines.antialiased", antialias_t.FAST)

  but note that as of Matplotlib 2.0.2, this will cause issues when other parts
  of Matplotlib try to validate the rcparam (e.g., exiting a ``rc_context``
  will use the validating setter to restore the original values); the issue is
  fixed in Matplotlib master (and #8771).

  (Support for ``text.antialiased`` is not implemented yet, mostly because we
  need to decide on whether to map ``True`` to ``GRAY`` or ``SUBPIXEL``.)

- The ``path.simplify_threshold`` rcparam is used to control the accuracy of
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

- ``draw_markers`` draws a marker at each control point of the given path,
  which is the documented behavior, even though all builtin renderers only draw
  markers at straight or Bézier segment ends.

Other known issues
------------------

- Blitting-based animations to image-base backends (e.g., ``mpl_cairo.qt``)
  leaves small artefacts at the edges of the blitted region.  This does not
  affect Xlib-based backends (e.g., ``mpl_cairo.gtk3``).

- SVG and Xlib (i.e, GTK3) currently need to rasterize mathtext before
  rendering it (this is mostly an issue for SVG, altough it affects vertical
  hinting for Xlib), as otherwise replaying a recording surface appears to have
  no effect.  This needs to be investigated.

  Meanwhile, a workaround is to generate files in PS format and convert them to
  SVG e.g. using::

     inkscape --without-gui input.ps --export-plain-svg output.svg

  Rendering of hinted mathtext is *extremely* slow on Xlib (GTK3).  This may be
  partially fixed by setting the ``text.hinting`` rcparam to ``"none"``, or by
  implementing a rastered cache (but it would be preferable to fix the general
  issue with recording surfaces first).

Possible optimizations
----------------------

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

Other ideas
-----------

- Complex text layout (e.g. using libraqm).

What about the already existing cairo (gtk3cairo) backend?
----------------------------------------------------------

It is slow (try running ``examples/mplot3d/wire3d_animation.py``), buggy (try
calling ``imshow``, especially with an alpha channel), and renders math poorly
(try ``title(r"\sqrt{2}")``).
