import functools
import sys

import matplotlib as mpl
import numpy as np

from ._mplcairo import (
    cairo_to_premultiplied_argb32,
    cairo_to_premultiplied_rgba8888,
    cairo_to_straight_rgba8888,
)


@functools.lru_cache(1)
def fix_ipython_backend2gui():  # matplotlib#12637 (<3.1).
    # Fix hard-coded module -> toolkit mapping in IPython (used for `ipython
    # --auto`).  This cannot be done at import time due to ordering issues (so
    # we do it when creating a canvas) and should only be done once (hence the
    # `lru_cache(1)`).
    if sys.modules.get("IPython") is None:  # Can be explicitly set to None.
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
