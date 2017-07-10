#!/usr/bin/env python
from pathlib import Path
import sys

import matplotlib.backends.backend_agg
# We are going to completely swap out matplotlib.backends.backend_agg.  Because
# get_text_width_descent relies on the actual RendererAgg's implementation, we
# need to make sure that class is still available somewhere.  So we import
# backend_mixed, which has a reference to the correct RendererAgg.
import matplotlib.backends.backend_mixed

# Use mpl_cairo.qt instead of mpl_cairo.base so that we can insert calls to
# show() from within the test suite for debugging purposes.
import mpl_cairo.qt

if Path().resolve() == Path(__file__).parent.resolve():
    print("This script must be run from the Matplotlib source folder.",
          file=sys.stderr)
    sys.exit(1)

mpl_cairo.qt.get_hinting_flag = \
    matplotlib.backends.backend_agg.get_hinting_flag
mpl_cairo.qt.FigureCanvasAgg = mpl_cairo.base.FigureCanvasCairo
mpl_cairo.qt.RendererAgg = mpl_cairo.base.GraphicsContextRendererCairo
matplotlib.backends.backend_agg = \
    sys.modules["matplotlib.backends.backend_agg"] = mpl_cairo.qt
matplotlib.use("agg", warn=False, force=True)

argv = sys.argv[1:]
idxs = [i for i, arg in enumerate(argv) if arg.startswith("-k")]
if idxs:
    idx = idxs[-1]
    if argv[idx] == "-k":
        argv[idx + 1] = "[png] and ({})".format(argv[idx + 1])
    else:
        argv[idx] = "-k[png] and ({})".format(argv[idx][2:])
else:
    argv.append("-k[png]")

sys.exit(matplotlib.test(argv=argv))
