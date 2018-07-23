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
