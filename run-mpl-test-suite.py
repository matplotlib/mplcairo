#!/usr/bin/env python

from argparse import ArgumentParser
import os
import inspect
from pathlib import Path
import shutil
import subprocess
from subprocess import CalledProcessError
import sys
import warnings

import matplotlib as mpl
import matplotlib.backends.backend_agg
import matplotlib.pyplot as plt
import matplotlib.testing.compare
import mplcairo.base

import pytest


def main(argv=None):
    parser = ArgumentParser(
        description="""\
Run the Matplotlib test suite, using the mplcairo backend to patch out
Matplotlib's agg backend.
""",
        epilog="Other arguments are forwarded to pytest.")
    parser.add_argument("--infinite-tolerance", action="store_true",
                        help="Set image comparison tolerance to infinity.")
    args, rest = parser.parse_known_args(argv)

    if args.infinite_tolerance:
        sig = inspect.signature(mpl.testing.compare.compare_images)
        def compare_images(*args, **kwargs):
            ba = sig.bind(*args, **kwargs)
            ba.arguments["tol"] = float("inf")
            return compare_images.__wrapped__(*ba.args, **ba.kwargs)
        compare_images.__wrapped__ = mpl.testing.compare.compare_images
        mpl.testing.compare.compare_images = compare_images

    mplcairo.base.get_hinting_flag = mpl.backends.backend_agg.get_hinting_flag
    mplcairo.base.FigureCanvasAgg = \
        mplcairo.base.FigureCanvasCairo
    mplcairo.base.RendererAgg = \
        mplcairo.base.GraphicsContextRendererCairo
    mpl.backends.backend_agg = \
        sys.modules["matplotlib.backends.backend_agg"] = mplcairo.base
    mpl.use("agg", warn=False, force=True)
    plt.switch_backend("agg")

    cwd = os.getcwd()
    try:
        matplotlib_srcdir = os.fsdecode(subprocess.check_output(
            ["git", "rev-parse", "--show-toplevel"],
            cwd=Path(matplotlib.__file__).parent)[:-1])
    except CalledProcessError:
        sys.exit("This script must be run in an environment where Matplotlib "
                 "is installed as an editable install.")

    os.chdir(matplotlib_srcdir)
    rv = pytest.main(["-p", "__main__", *rest])
    os.chdir(cwd)
    result_images = Path(matplotlib_srcdir, "result_images")
    if result_images.exists():
        dest = Path(cwd, "result_images")
        shutil.rmtree(str(dest), ignore_errors=True)
        result_images.replace(dest)
    return rv


def pytest_collection_modifyitems(session, config, items):
    excluded_modules = {
        "matplotlib.tests.test_compare_images",
    }
    excluded_nodeids = {
        "lib/matplotlib/tests/" + name for name in [
            "test_agg.py::test_repeated_save_with_alpha",
            "test_artist.py::test_cull_markers",
            "test_backend_pdf.py::test_composite_image",
            "test_backend_pdf.py::test_multipage_keep_empty",
            "test_backend_pdf.py::test_multipage_pagecount",
            "test_backend_pdf.py::test_multipage_properfinalize",
            "test_backend_ps.py::test_savefig_to_stringio[eps afm]",
            "test_backend_ps.py::test_savefig_to_stringio[eps with usetex]",
            "test_backend_ps.py::test_savefig_to_stringio[eps]",
            "test_backend_ps.py::test_savefig_to_stringio[ps with distiller]",
            "test_backend_ps.py::test_savefig_to_stringio[ps with usetex]",
            "test_backend_ps.py::test_savefig_to_stringio[ps]",
            "test_backend_ps.py::test_source_date_epoch",
            "test_backend_svg.py::test_text_urls",
            "test_bbox_tight.py::test_bbox_inches_tight_suptile_legend[pdf]",
            "test_bbox_tight.py::test_bbox_inches_tight_suptile_legend[png]",
            "test_bbox_tight.py::test_bbox_inches_tight_suptile_legend[svg]",
            "test_image.py::test_composite[True-1-ps- colorimage]",
            "test_image.py::test_composite[False-2-ps- colorimage]",
            "test_simplification.py::test_throw_rendering_complexity_exceeded",
        ]
    }
    filtered = []
    for item in items:
        if item.module.__name__ in excluded_modules:
            pass
        elif item.nodeid in excluded_nodeids:
            excluded_nodeids -= {item.nodeid}
        else:
            filtered.append(item)
    if excluded_nodeids:
        warnings.warn("Unused exclusions:\n    {}"
                      .format("\n    ".join(sorted(excluded_nodeids))))
    items[:] = filtered


if __name__ == "__main__":
    sys.exit(main())
