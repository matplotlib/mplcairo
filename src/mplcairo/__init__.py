"""A cairo backend for Matplotlib."""

import ast
import functools
import importlib.metadata
import os
import sys
import warnings


try:
    __version__ = importlib.metadata.version("mplcairo")
except ImportError:
    __version__ = "(unknown version)"

if sys.platform != "win32":
    def _load_symbols():
        # dlopen() pycairo's extension module with RTLD_GLOBAL to dynamically
        # load cairo.
        from ctypes import CDLL, RTLD_GLOBAL
        from cairo import _cairo
        CDLL(_cairo.__file__, RTLD_GLOBAL)

    _load_symbols()

import matplotlib as mpl

from . import _mplcairo
from ._mplcairo import (
    antialias_t, dither_t, format_t, operator_t, get_options, set_options)

__all__ = [
    "antialias_t", "dither_t", "operator_t", "format_t",
    "get_options", "set_options",
    "get_context", "get_raw_buffer",
]


def _init_options():
    # Hard-coded defaults.
    set_options(cairo_circles=True)
    try:
        set_options(raqm=True)
    except OSError:
        pass
    # Load from environment variables.
    for key in get_options():  # Easy way to list them.
        env_key = f"MPLCAIRO_{key.upper()}"
        env_val = os.environ.get(env_key)
        if env_val:
            try:
                val = ast.literal_eval(env_val)
            except (SyntaxError, ValueError):
                warnings.warn(f"Ignoring unparsable environment variable "
                              f"{env_key}={env_val!r}")
            else:
                set_options(**{key: val})


_init_options()


@functools.lru_cache(1)
def _get_mpl_version():
    # Don't trigger a git subprocess for Matplotlib's __version__ resolution if
    # possible, and cache the result as early versions of importlib.metadata
    # are slow.  We can't cache get_versions() directly as the result depends
    # on whether raqm is loaded.
    try:
        import importlib.metadata
        return importlib.metadata.version("matplotlib")
    except ImportError:
        # No importlib.metadata on Py<3.8 *or* not-installed Matplotlib.
        return mpl.__version__


def get_versions():
    """
    Return a mapping indicating the versions of mplcairo and its dependencies.

    This function is intended to help gather information for bug reports, and
    not part of the stable API.
    """
    return {
        "python": sys.version,
        "mplcairo": __version__,
        "matplotlib": _get_mpl_version(),
        **_mplcairo.get_versions(),
    }


def get_context(canvas):
    """Get ``cairo.Context`` used to draw onto the canvas."""
    return canvas.renderer._get_context()


def get_raw_buffer(canvas):
    """
    Get the canvas' raw internal buffer.

    The buffer's shape and dtype depend on the ``image_format`` passed to
    `set_options`:
    - ``ARGB32``: uint8 (h, w, 4), in ARGB32 order (the default);
    - ``RGB24``: uint8 (h, w, 3), in RGB24 order;
    - ``A8``: uint8 (h, w);
    - ``A1``: [("V{w}", void)] (h), where w is the actual buffer width in
      pixels (e.g. "V640"), and the void field is wide enough to contain the
      data but is padded to a byte boundary;
    - ``RGB16_565``: uint16 (h, w);
    - ``RGB30``: uint32 (h, w);
    - ``RGB96F``: float (h, w, 3);
    - ``RGBA128F``: float (h, w, 4).
    """
    return canvas.renderer._get_buffer()


def _patch_artist(op, artist):
    """
    Patch an artist so that it is drawn with this compositing operator or
    dithering algorithm.
    """

    def draw(renderer):
        gc = renderer.new_gc()
        if isinstance(op, operator_t):
            gc.set_mplcairo_operator(op)
        elif isinstance(op, dither_t):
            gc.set_mplcairo_dither(op)
        _base_draw(renderer)
        gc.restore()

    _base_draw = artist.draw
    artist.draw = draw


dither_t.patch_artist = _patch_artist
operator_t.patch_artist = _patch_artist
