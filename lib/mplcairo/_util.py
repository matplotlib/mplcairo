import functools
import sys

import matplotlib as mpl
import numpy as np


def cairo_to_premultiplied_argb32(buf):
    """
    Convert a buffer from cairo's ARGB32 (premultiplied) or RGBA128F to
    premultiplied ARGB32.
    """
    if buf.dtype == np.uint8:
        return buf
    elif buf.dtype == np.float32:
        r, g, b, a = np.moveaxis(buf, -1, 0)
        r *= a
        g *= a
        b *= a
        r = (r * 255).astype(np.uint8).astype(np.uint32)
        g = (g * 255).astype(np.uint8).astype(np.uint32)
        b = (b * 255).astype(np.uint8).astype(np.uint32)
        a = (a * 255).astype(np.uint8).astype(np.uint32)
        return (((a << 24) + (r << 16) + (g << 8) + (b << 0))
                .view(np.uint8).reshape(buf.shape))
    else:
        raise TypeError("Unexpected dtype: {}".format(buf.dtype))


def cairo_to_premultiplied_rgba8888(buf):
    """
    Convert a buffer from cairo's ARGB32 (premultiplied) or RGBA128F to
    premultiplied RGBA8888.
    """
    # Using .take() instead of indexing ensures C-contiguity of the result.
    return cairo_to_premultiplied_argb32(buf).take(
        [2, 1, 0, 3] if sys.byteorder == "little" else [1, 2, 3, 0], axis=2)


def cairo_to_straight_rgba8888(buf):
    """
    Convert a buffer from cairo's ARGB32 (premultiplied) or RGBA128F to
    straight RGBA8888.
    """
    if buf.dtype == np.uint8:
        rgba = cairo_to_premultiplied_rgba8888(buf)
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
    elif buf.dtype == np.float32:
        return (buf * 255).astype(np.uint8)
    else:
        raise TypeError("Unexpected dtype: {}".format(buf.dtype))


@functools.lru_cache(1)
def fix_ipython_backend2gui():  # aka. matplotlib#12637 (matplotlib<3.1).
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
