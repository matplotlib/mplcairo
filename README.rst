A (new) cairo backend for Matplotlib
====================================

This is a new, near-complete implementation of a cairo backend for Matplotlib.
Currently, it can be used with either the Qt backend proposed in Matplotlib's
PR #8771, or the Gtk3 backend proposed in #8772 (which is also included in
#8771).

This implementation "passes" Matplotlib's entire image comparison test suite
-- after accounting for inevitable differences in rasterization, and with the
exceptions noted below.

Depending on the specific task, the backend can be anywhere from ~10x faster
(e.g., stamping circular markers of variable colors) to ~10% faster (e.g.,
drawing lines) than Agg.

Installation (Linux only)
-------------------------

Dependencies:

- Python 3,
- cairo≥1.12 (needed for mesh gradient support),
- a C++ compiler with C++17 support, e.g. GCC≥7.1.

Such dependencies are available on conda and conda-forge.  Using conda, the
following commands will build and install mpl_cairo.

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

   # - pkgconfig and cairo from the anaconda channel will *not* work; thus, we
   #   may as well install everything from conda-forge.  Note that the Python
   #   version here needs to match the Python version used to build the
   #   Matplotlib wheel; thus, you may need to build the wheel in its own
   #   environment as well.
   # - numpy could be built from source too but conda saves us some time.
   # - PyQt is necessary to have an interactive backend (PyGObject, i.e. Gtk3,
   #   can also be used, but it is not conda-installable).
   conda create -n mpl_cairo -c conda-forge \
       python=3.6 pkgconfig cairo pybind11\>=2.1 numpy pyqt
   conda install -n mpl_cairo -c rdonnelly gxx_linux-64\>=7.1

   # Activation needs to happen *after* installing gcc_linux-64\>=7.1
   source activate mpl_cairo

   pip install matplotlib/dist/*.whl

   git clone https://github.com/anntzer/mpl_cairo.git
   (cd mpl_cairo
    pip install -ve .)

Then, the backend can be selected by setting the ``MPLBACKEND`` environment
variable to ``module://mpl_cairo.qt`` (or ``module://mpl_cairo.gtk3``).
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

Test suite
----------

Run ``run-mpl-test-suite.py`` *from the Matplotlib source folder* to run the
subset of matplotlib tests that rely on png image comparison, while using this
backend.  Pass command-line options as you would to pytest, although ``-k``
must not be compressed with another short argument.

Notes
-----

- Antialiasing uses ``CAIRO_ANTIALIAS_FAST`` by default.  The ``antialiased``
  artist property can also take the ``mpl_cairo.antialias_t.GOOD`` (or
  ``BEST``, etc.) value for additional control.  ``GOOD``/``BEST`` antialiasing
  of lines is ~3x slower than using Agg.

  **NOTE**: When drawing very thin lines (<0.5px, e.g.
  ``test_cycles.test_property_collision_plot``), ``CAIRO_ANTIALIAS_FAST`` may
  lead to artefacts, such that the line disappearing in certain areas.  In that
  case, switching to ``GOOD``/``BEST`` antialiasing solves the issue.  (It may
  be possible to do this automatically from within the backend, just as the
  miter limit is set whenever the line width is set.)

- ``path.simplify_threshold`` is also used to control the accuracy of marker
  stamping, down to a arbitrarily chosen threshold of 1/16px.  Values lower
  than that will use the exact (slower) marker drawing path.  Marker stamping
  is also implemented for scatter plots (which can have multiple colors).
  Likewise, markers of different sizes get mapped into markers of discretized
  sizes, with an error bounded by the threshold.

  **NOTE**: ``pcolor`` and mplot3d's ``plot_surface`` display some artifacts
  where the facets join each other.  This is because these functions internally
  use a ``PathCollection``, thus triggering the approximate stamping.
  ``pcolor`` should be deprecated in favor of ``pcolormesh`` (internally using
  a ``QuadMesh``), and ``plot_surface`` should likewise instead represent the
  surface using ``QuadMesh``, which is drawn without such artefacts.

- ``draw_markers`` draws a marker at each control point of the given path,
  which is the documented behavior, even though all builtin renderers only draw
  markers at straight or Bézier segment ends.

Missing features
----------------

- Snapping.

Other known issues
------------------

- Very large inputs (transforming to pixel values greater than ``2**23`` in
  absolute value) will be drawn incorrectly due to overflow in cairo (cairo
  #20091).  A temporary workaround partially handles the issue when only one of
  the two coordinates is too large, but not when both are.
- Blitting-based animation leaves small artefacts at the edges of the blitted
  region.

Possible optimizations
----------------------

- Cache eviction policy and persistent cache for ``draw_path_collection``.
- Path simplification (although cairo appears to use vertex reduction and
  Douglas-Peucker internally?).
- Use QtOpenGLWidget and the cairo-gl backend.
- ``hexbin`` currently falls back on the slow implementation due to its use of
  the ``offset_position`` parameter.  This should be fixed on Matplotlib's
  side.

Other ideas
-----------

- Expose the cairo PDF, PS and SVG backends.
- Native mathtext backend (to optimize antialiasing).
- Complex text layout (e.g. using libraqm).

What about the already existing cairo (gtk3cairo) backend?
----------------------------------------------------------

It is slow (try running ``examples/mplot3d/wire3d_animation.py``), buggy (try
calling ``imshow``, especially with an alpha channel), and renders math poorly
(try ``title(r"\sqrt{2}")``).
