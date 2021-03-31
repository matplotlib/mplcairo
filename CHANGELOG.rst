v0.4
====

- Bumped dependencies to Python≥3.7, pybind11≥2.6; Windows build also
  ``setup_requires`` pycairo.
- Fixed support for Matplotlib 3.4.
- Added ``get_context``.
- Improve snapping of codeful paths.
- Fix failure to increase cairo refcount of font cache entries.
- Support ``rcParams["pdf.compression"] = 0``.
- Dropped support for the JPEG-specific *quality*, *optimize*, and
  *progressive* parameters to `.Figure.savefig`.

v0.3
====

- Bumped dependencies to Python≥3.6, pybind11≥2.5.
- ``pybind11`` is now a ``setup_requires`` and ``-march=native`` is no longer
  passed as compilation option by default; drop support for
  ``MPLCAIRO_BUILD_TYPE``.
- Support for OpenType font features.
- Support for ``pdftex.map`` font effects (as in Matplotlib's
  ``usetex_fonteffects.py`` example).
- Fix zooming on macosx.

v0.2
====

- Add the ``MPLCAIRO_BUILD_TYPE`` environment variable for controlling the
  build.
- Support all Qt bindings.
- Improved error messages.
- ``copy_from_bbox`` now rounds boundaries inwards, to avoid overspilling the
  canvas in presence of floating point inaccuracies.
- Fixes to blitting, marker drawing in presence of nans, and shutdown sequence.
- Matplotlib tests can be selected with ``--pyargs``.
- MultiPage now provides more of Matplotlib's API, and behaves as if
  *keep_empty* is False.
- Support `pathlib.Path` arguments to `FontProperties` on Python≥3.6.
- Improved mathtext alignment and usetex support.
- Added `get_raw_buffer` to access the raw internal buffer.
- Added `operator_t.patch_artist` to simplify usage of custom compositing
  operators.
- Support ``Title`` metadata entry for PostScript output.

v0.1
====

- Integration with libraqm now occurs via dlopen() rather than being selected
  at compile-time.
- Add `set_option`, `get_option` for controlling certain rendering parameters.
- Various rendering and performance improvements.
- On Travis, we now run Matplotlib's test suite with mplcairo patching the
  default Agg renderer.

v0.1a1
======

- First public prerelease.
