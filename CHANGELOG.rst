next
====

- Add the ``MPLCAIRO_BUILD_TYPE`` environment variable for controlling the
  build.
- Support all Qt bindings.
- Improved error messages.
- ``copy_from_bbox`` now rounds boundaries inwards, to avoid overspilling the
  canvas in presence of floating point inaccuracies.
- Fixes to shutdown sequence and to blitting.
- Matplotlib tests can be selected with ``--pyargs``.
- MultiPage now provides more of Matplotlib's API, and behaves as if
  *keep_empty* is False.
- Support `pathlib.Path` arguments to `FontProperties` on Python≥3.6.

v0.1
====

- Integration with libraqm now occurs via dlopen() rather than being selected
  at compile-time.
- Add ``set_option``, ``get_option`` for controlling certain rendering
  parameters.
- Various rendering and performance improvements.
- On Travis, we now run Matplotlib's test suite with mplcairo patching the
  default Agg renderer.

v0.1a1
======

- First public prerelease.
