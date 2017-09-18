#!/usr/bin/env python
from pathlib import Path
import sys

import matplotlib.backends.backend_agg
import mplcairo.base

if Path().resolve() == Path(__file__).parent.resolve():
    print("This script must be run from the Matplotlib source folder.",
          file=sys.stderr)
    sys.exit(1)

mplcairo.base.get_hinting_flag = \
    matplotlib.backends.backend_agg.get_hinting_flag
mplcairo.base.FigureCanvasAgg = mplcairo.base.FigureCanvasCairo
mplcairo.base.RendererAgg = mplcairo.base.GraphicsContextRendererCairo
matplotlib.backends.backend_agg = \
    sys.modules["matplotlib.backends.backend_agg"] = mplcairo.base
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
