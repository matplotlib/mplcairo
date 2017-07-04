A (new) cairo backend for Matplotlib
====================================

This is a new, fairly complete implementation of a Cairo backend for
Matplotlib.  Currently, it is designed to be used with the qt-cairo backend
proposed in Matplotlib's PR #8771.

Depending on the specific task, the backend can be as fast as Agg, or no more
than twice slower (especially for drawing markers, which is done much more
accurately -- this was one of the original motivations for this work).

Installation
------------

Only Python 3 is supported.  A very recent C++ compiler, with support for C++17
(e.g., GCC 7.1) is required.

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

Install (in the virtualenv) ``pytest-benchmark`` and call (e.g.)::

   $ pytest --benchmark-group-by=fullfunc --benchmark-timer=time.process_time

Notes
-----

- ``path.simplify_threshold`` is also used to control the accuracy of marker
  stamping, down to a arbitrarily chosen threshold of 1/16px.  Values lower
  than that will use the exact (slower) marker drawing path.
- ``draw_markers`` draws a marker at each control point of the given path,
  which is the documented behavior, even though all builtin renderers only draw
  markers at straight or Bézier segment ends.

Missing features
----------------

- Snapping.
- ``hexbin`` essentially requires its own implementation (due to the use of the
  ``offset_position`` parameter).  This should be fixed on Matplotlib's side.
- ``draw_quad_mesh`` (not clear it's needed -- even the Agg backend just
  redirects to ``draw_path_collection``).
- Sketching (i.e. xkcd-style plots).

Known issues
------------

- Blitting-based animation leaves small artefacts at the edges of the blitted
  region.

Possible optimizations
----------------------

- Path simplification (although cairo appears to use Douglas-Peucker
  internally).
- Use QtOpenGLWidget and the cairo-gl backend.

Other ideas
-----------

- Native mathtext backend (to optimize antialiasing).
- Complex text layout (e.g. using libraqm).

What about the already existing cairo (gtk3cairo) backend?
----------------------------------------------------------

It is slow (try running ``examples/mplot3d/wire3d_animation.py``), buggy (try
calling ``imshow``, especially with an alpha channel), and renders math poorly
(try ``title(r"\sqrt{2}")``).
