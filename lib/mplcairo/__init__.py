import sys

try:
    import setuptools_scm
    __version__ = setuptools_scm.get_version(  # xref setup.py
        root="../..", relative_to=__file__,
        version_scheme="post-release", local_scheme="node-and-date")
except (ImportError, LookupError):
    try:
        from ._version import version as __version__
    except ImportError:
        pass

if sys.platform != "win32":
    def _load_symbols():
        # dlopen() pycairo's extension module with RTLD_GLOBAL to dynamically
        # load cairo.
        from ctypes import CDLL, RTLD_GLOBAL
        from cairo import _cairo
        CDLL(_cairo.__file__, RTLD_GLOBAL)

    _load_symbols()

import matplotlib

from . import _mplcairo
from ._mplcairo import antialias_t, operator_t, get_options, set_options

__all__ = [
    "antialias_t", "operator_t",
    "get_options", "set_options",
    "get_raw_buffer",
]

set_options(cairo_circles=True)
try:
    set_options(raqm=True)
except OSError:
    pass


def get_versions():
    """
    Return a mapping indicating the versions of mplcairo and its dependencies.

    This function is solely intended to help gather information for bug
    reports; its output may change without notice.
    """
    return {
        "python": sys.version,
        "mplcairo": __version__,
        "matplotlib": matplotlib.__version__,
        "cairo": _mplcairo.__cairo_version__,
        "freetype": _mplcairo.__freetype_version__,
        "pybind11": _mplcairo.__pybind11_version__,
    }


def get_raw_buffer(canvas):
    """
    Get the canvas' raw internal buffer.

    This is normally a uint8 buffer of shape ``(m, n, 4)`` in
    ARGB32 order, unless the canvas was created after calling
    ``set_options(float_surfaces=True)`` in which case this is
    a float32 buffer of shape ``(m, n, 4)`` in RGBA128F order.
    """
    return canvas._get_buffer()


def _operator_patch_artist(op, artist):
    """Patch an artist to make it use this compositing operator for drawing."""

    def draw(renderer):
        gc = renderer.new_gc()
        gc.set_mplcairo_operator(op)
        _base_draw(renderer)
        gc.restore()

    _base_draw = artist.draw
    artist.draw = draw


operator_t.patch_artist = _operator_patch_artist
