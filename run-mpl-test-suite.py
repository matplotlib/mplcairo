#!/usr/bin/env python
from argparse import ArgumentParser
import inspect
import os
from pathlib import Path
import subprocess
import sys

import matplotlib
matplotlib.use("agg")
import matplotlib.backends.backend_agg
import matplotlib.testing.decorators as mtd
import mplcairo.base

import pytest


def main():
    parser = ArgumentParser(epilog="Other arguments are forwarded to pytest.")
    parser.add_argument("--infinite-tolerance", action="store_true",
                        help="Set image comparison tolerance to infinity.")
    args, rest = parser.parse_known_args()

    if args.infinite_tolerance:
        sig = inspect.signature(mtd.image_comparison)
        idx = [p.name for p in sig.parameters.values()
               if p.default is not p.empty].index("tol")
        defaults = list(mtd.image_comparison.__defaults__)
        defaults[idx] = float("inf")
        mtd.image_comparison.__defaults__ = tuple(defaults)

    matplotlib_srcdir = subprocess.check_output(
        ["git", "rev-parse", "--show-toplevel"],
        cwd=Path(matplotlib.__file__).parent)[:-1]
    os.chdir(matplotlib_srcdir)

    mplcairo.base.get_hinting_flag = \
        matplotlib.backends.backend_agg.get_hinting_flag
    mplcairo.base.FigureCanvasAgg = \
        mplcairo.base.FigureCanvasCairo
    mplcairo.base.RendererAgg = \
        mplcairo.base.GraphicsContextRendererCairo
    matplotlib.backends.backend_agg = \
        sys.modules["matplotlib.backends.backend_agg"] = mplcairo.base
    matplotlib.use("agg", warn=False, force=True)

    return pytest.main(["-p", "__main__", *rest])


def pytest_collection_modifyitems(session, config, items):
    items[:] = [item for item in items if item.nodeid not in {
        "lib/matplotlib/tests/test_agg.py::test_repeated_save_with_alpha",
        "lib/matplotlib/tests/test_artist.py::test_cull_markers",
    }]


if __name__ == "__main__":
    sys.exit(main())
