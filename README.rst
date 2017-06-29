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

For example, run

.. code:: python

   import os; os.environ["MPLBACKEND"] = "module://mpl_cairo.qt"
   from matplotlib import pyplot as plt

   data = [[0.5, 0.525, 0.55, 0.575, 0.6, 0.625],
           [0.5, 0.501, 0.502, 0.503, 0.504, 0.505]]

   fig, ax = plt.subplots(figsize=(3.5, 3.5))
   fig.subplots_adjust(
       left=0.01, right=0.99, bottom=0.01, top=0.99, hspace=0, wspace=0)
   ax.set(xlim=(0, 1), ylim=(0, 1))
   ax.scatter(data[0], data[1], s=25)
   plt.show()

and compare the marker position with the default ``qt5agg`` backend.

Benchmarks
----------

Install (in the virtualenv) ``pytest-benchmark`` and call (e.g.)::

   $ pytest --benchmark-group-by=fullfunc --benchmark-timer=time.process_time

Missing features
----------------

- Hatching.
- Snapping.
- ``hexbin`` essentially requires its own implementation (due to the use of the
  ``offset_position`` parameter).  This should be fixed on Matplotlib's side.

Missing optimizations
---------------------

- Path simplification.
- Marker stamping (but not at the cost of accuracy).

What about the already existing cairo (gtk3cairo) backend?
----------------------------------------------------------

It is slow (try running ``examples/mplot3d/wire3d_animation.py``), buggy (try
calling ``imshow``, especially with an alpha channel), and renders math poorly
(try ``title(r"\sqrt{2}")``).
