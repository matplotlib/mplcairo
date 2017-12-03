#!/usr/bin/env python

from argparse import ArgumentParser
import os
import inspect
from pathlib import Path
import subprocess
import sys
import warnings

import matplotlib as mpl
import matplotlib.backends.backend_agg
import matplotlib.pyplot as plt
import matplotlib.testing.compare
import mplcairo.base

import pytest


def main(argv=None):
    parser = ArgumentParser(epilog="Other arguments are forwarded to pytest.")
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

    matplotlib_srcdir = subprocess.check_output(
        ["git", "rev-parse", "--show-toplevel"],
        cwd=Path(matplotlib.__file__).parent)[:-1]
    os.chdir(matplotlib_srcdir)

    mplcairo.base.get_hinting_flag = mpl.backends.backend_agg.get_hinting_flag
    mplcairo.base.FigureCanvasAgg = \
        mplcairo.base.FigureCanvasCairo
    mplcairo.base.RendererAgg = \
        mplcairo.base.GraphicsContextRendererCairo
    mpl.backends.backend_agg = \
        sys.modules["matplotlib.backends.backend_agg"] = mplcairo.base
    mpl.use("agg", warn=False, force=True)
    plt.switch_backend("agg")

    return pytest.main(["-p", "__main__", *rest])


def pytest_collection_modifyitems(session, config, items):
    exclude = {
        "lib/matplotlib/tests/" + name for name in [
            "test_agg.py::test_repeated_save_with_alpha",
            "test_artist.py::test_cull_markers",
            "test_backend_pdf.py::test_composite_image",
            "test_backend_pdf.py::test_multipage_keep_empty",
            "test_backend_pdf.py::test_multipage_pagecount",
            "test_backend_pdf.py::test_multipage_properfinalize",
            "test_backend_pdf.py::test_pdf_savefig_when_color_is_none",
            "test_backend_pdf.py::test_source_date_epoch",
            "test_backend_ps.py::test_composite_image",
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
        ]
    }
    filtered = []
    for item in items:
        if item.nodeid in exclude:
            exclude -= {item.nodeid}
        else:
            filtered.append(item)
    if exclude:
        warnings.warn("Invalid exclusions:\n    {}"
                      .format("\n    ".join(sorted(exclude))))
    items[:] = filtered


if __name__ == "__main__":
    sys.exit(main())
