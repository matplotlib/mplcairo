import functools
import sys

import matplotlib as mpl
import numpy as np


def to_premultiplied_rgba8888(buf):
    """Convert a buffer from premultipled ARGB32 to premultiplied RGBA8888."""
    # Using .take() instead of indexing ensures C-contiguity of the result.
    return buf.take(
        [2, 1, 0, 3] if sys.byteorder == "little" else [1, 2, 3, 0], axis=2)


def to_straight_rgba8888(buf):
    """Convert a buffer from premultiplied ARGB32 to straight RGBA8888."""
    rgba = to_premultiplied_rgba8888(buf)
    # The straightening formula is from cairo-png.c.
    rgb = rgba[..., :-1]
    alpha = rgba[..., -1]
    if alpha.max() == 255:  # Special-case fully-opaque buffers for speed.
        return rgba
    mask = alpha != 0
    for channel in np.rollaxis(rgb, -1):
        channel[mask] = (
            (channel[mask].astype(int) * 255 + alpha[mask] // 2)
            // alpha[mask])
    return rgba


@functools.lru_cache(1)
def fix_ipython_backend2gui():
    # Fix hard-coded module -> toolkit mapping in IPython (used for `ipython
    # --auto`).  This cannot be done at import time due to ordering issues (so
    # we do it when creating a canvas) and should only be done once (hence the
    # `lru_cache(1)`).
    if "IPython" not in sys.modules:
        return
    import IPython
    ip = IPython.get_ipython()
    if not ip:
        return
    from IPython.core import pylabtools as pt
    pt.backend2gui.update({
        "module://mplcairo.gtk": "gtk3",
        "module://mplcairo.qt": "qt",
        "module://mplcairo.tk": "tk",
        "module://mplcairo.wx": "wx",
        "module://mplcairo.macosx": "osx",
    })
    # Work around pylabtools.find_gui_and_backend always reading from
    # rcParamsOrig.
    orig_origbackend = mpl.rcParamsOrig["backend"]
    try:
        mpl.rcParamsOrig["backend"] = mpl.rcParams["backend"]
        ip.enable_matplotlib()
    finally:
        mpl.rcParamsOrig["backend"] = orig_origbackend
