import sys

import numpy as np


def to_premultiplied_rgba8888(buf):
    """Convert a buffer from premultipled ARGB32 to premultiplied RGBA8888.
    """
    # Using .take() instead of indexing ensures C-contiguity of the result.
    return buf.take(
        [2, 1, 0, 3] if sys.byteorder == "little" else [1, 2, 3, 0], axis=2)


def to_unmultiplied_rgba8888(buf):
    """Convert a buffer from premultiplied ARGB32 to unmultiplied RGBA8888.
    """
    rgba = to_premultiplied_rgba8888(buf)
    # Un-premultiply alpha.  The formula is the same as in cairo-png.c.
    rgb = rgba[..., :-1]
    alpha = rgba[..., -1]
    mask = alpha != 0
    for channel in np.rollaxis(rgb, -1):
        channel[mask] = (
            (channel[mask].astype(int) * 255 + alpha[mask] // 2)
            // alpha[mask])
    return rgba
