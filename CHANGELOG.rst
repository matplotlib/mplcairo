v0.5 (2022-08-18)
=================

- Fixed support for Matplotlib 3.5.
- Bumped dependencies to pybind11≥2.8.
- Bumped supported raqm versions to ≥0.7; fixed integration with raqm 0.7.2.
- Support Qt6 and GTK4.
- Support HiDPI on GTK (it was already supported on Qt).
- ``marker_threads`` option renamed to ``collection_threads``; and now also
  affects ``draw_path_collection``.
- Control vector output version with the ``MaxVersion`` special metadata
  entry.
- Add HARD_LIGHT operator, which was previously missing.
- Support setting OpenType language tag.

v0.4 (2021-04-02)
=================

- Bumped dependencies to Python≥3.7, pybind11≥2.6; Windows build also
  ``setup_requires`` pycairo.
- Fixed support for Matplotlib 3.4.
- Added ``get_context``.
- Improve snapping of codeful paths.
- Fix failure to increase cairo refcount of font cache entries.
- Support ``rcParams["pdf.compression"] = 0``.
- Dropped support for the JPEG-specific *quality*, *optimize*, and
  *progressive* parameters to `.Figure.savefig`.

v0.3 (2020-05-03)
=================

- Bumped dependencies to Python≥3.6, pybind11≥2.5.
- ``pybind11`` is now a ``setup_requires`` and ``-march=native`` is no longer
  passed as compilation option by default; drop support for
  ``MPLCAIRO_BUILD_TYPE``.
- Support for OpenType font features.
- Support for ``pdftex.map`` font effects (as in Matplotlib's
  ``usetex_fonteffects.py`` example).
- Fix zooming on macosx.

v0.2 (2019-09-21)
=================

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

v0.1 (2018-07-22)
=================

- Integration with libraqm now occurs via dlopen() rather than being selected
  at compile-time.
- Add `set_option`, `get_option` for controlling certain rendering parameters.
- Various rendering and performance improvements.
- On Travis, we now run Matplotlib's test suite with mplcairo patching the
  default Agg renderer.

v0.1a1 (2018-03-13)
===================

- First public prerelease.
