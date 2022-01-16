import functools
import importlib
import os
import sys

import matplotlib as mpl

from . import _backports
from ._mplcairo import (
    cairo_to_premultiplied_argb32,
    cairo_to_premultiplied_rgba8888,
    cairo_to_straight_rgba8888,
)


@functools.lru_cache(1)
def get_tex_font_map():
    return mpl.dviread.PsfontsMap(mpl.dviread.find_tex_file("pdftex.map"))


def get_glyph_name(dvitext):
    ps_font = get_tex_font_map()[dvitext.font.texname]
    return (_backports._parse_enc(ps_font.encoding)[dvitext.glyph]
            if ps_font.encoding is not None else None)


def get_matplotlib_gtk_backend():
    import gi
    required = gi.get_required_version("Gtk")
    if required == "4.0":
        versions = [4]
    elif required == "3.0":
        versions = [3]
    elif os.environ.get("_GTK_API"):  # Private undocumented API.
        versions = [int(os.environ["_GTK_API"])]
    else:
        versions = [4, 3]
    for version in versions:
        # Matplotlib converts require_version ValueErrors into ImportErrors.
        try:
            mod = importlib.import_module(
                f"matplotlib.backends.backend_gtk{version}")
            return mod, getattr(mod, f"_BackendGTK{version}")
        except ImportError:
            pass
    raise ImportError("Failed to import any Matplotlib GTK backend")


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
