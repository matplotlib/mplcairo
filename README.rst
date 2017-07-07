A (new) cairo backend for Matplotlib
====================================

This is a new, fairly complete implementation of a Cairo backend for
Matplotlib.  Currently, it is designed to be used with the qt-cairo backend
proposed in Matplotlib's PR #8771.

Depending on the specific task, the backend can be anywhere from ~4.5x faster
(e.g., stamping markers of variable colors) to ~10% faster (e.g., drawing
lines) than Agg.

Installation
------------

Dependencies:
- Python 3,
- cairo >=1.12,
- a C++ compiler with C++17 support, e.g. GCC≥7.1.

Run::

   $ python -mvenv /path/to/venv
   $ source /path/to/venv/bin/activate
   $ git clone https://github.com/matplotlib/matplotlib.git
   $ cd matplotlib
   $ git pull origin pull/8771/head:pr/8771
   $ git checkout pr/8771
   $ pip install -ve .
   $ cd ..
   $ git clone https://github.com/anntzer/mpl_cairo.git
   $ cd mpl_cairo
   $ pip install -ve .

Then, the backend can be selected by setting the ``MPLBACKEND`` environment
variable to ``module://mpl_cairo.qt``.

The ``examples`` folder contains a few cases where the output of this backend
is arguably more accurate (thanks to the use of subpixel marker stamping, with
an accuracy controlled by the ``path.simplify_threshold`` rcparam).

Benchmarks
----------

Install (in the virtualenv) ``pytest>=3.1.0`` and ``pytest-benchmark``, then
call (e.g.)::

   $ pytest --benchmark-group-by=fullfunc --benchmark-timer=time.process_time

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
- ``path.simplify_threshold`` is also used to control the accuracy of marker
  stamping, down to a arbitrarily chosen threshold of 1/16px.  Values lower
  than that will use the exact (slower) marker drawing path.  Marker stamping
  is also implemented for scatter plots (which can have multiple colors).
  Likewise, markers of different sizes get mapped into markers of discretized
  sizes, with an error bounded by the threshold.
- ``draw_markers`` draws a marker at each control point of the given path,
  which is the documented behavior, even though all builtin renderers only draw
  markers at straight or Bézier segment ends.

Missing features
----------------

- Hatching of ``PathCollections``\s.
- ``set_agg_filter``.
- Snapping.
- Sketching (i.e. xkcd-style plots).

Known issues
------------

- Blitting-based animation leaves small artefacts at the edges of the blitted
  region.

Possible optimizations
----------------------

- Further abuse the line cap drawer to quickly draw circles.
- Cache eviction policy and persistent cache for ``draw_path_collection``.
- ``draw_quad_mesh`` (not clear it's needed -- even the Agg backend just
  redirects to ``draw_path_collection``).
- Path simplification (although cairo appears to use vertex reduction and
  Douglas-Peucker internally?).
- Use QtOpenGLWidget and the cairo-gl backend.
- ``hexbin`` currently falls back on the slow implementation due to its use of
  the ``offset_position`` parameter.  This should be fixed on Matplotlib's
  side.

Other ideas
-----------

- Native mathtext backend (to optimize antialiasing).
- Complex text layout (e.g. using libraqm).

What about the already existing cairo (gtk3cairo) backend?
----------------------------------------------------------

It is slow (try running ``examples/mplot3d/wire3d_animation.py``), buggy (try
calling ``imshow``, especially with an alpha channel), and renders math poorly
(try ``title(r"\sqrt{2}")``).
