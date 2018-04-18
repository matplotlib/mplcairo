#!/usr/bin/env python
"""
Run the Matplotlib test suite, using the mplcairo backend to patch out
Matplotlib's agg backend.

.. PYTEST_DONT_REWRITE
"""

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
    import matplotlib.pyplot as plt
    plt.switch_backend("agg")

    return pytest.main([
        "-p", "__main__",
        # Don't get confused by our *own* conftest...
        "-p", "no:{}".format(Path(__file__).parent.resolve()
                             / "tests/conftest.py"),
        "--pyargs", "matplotlib",
        *rest])


def pytest_collection_modifyitems(session, config, items):
    if len(items) == 0:
        pytest.exit("No tests found; Matplotlib was likely installed without "
                    "test data.")
    excluded_modules = {
        "matplotlib.tests.test_compare_images",
    }
    excluded_nodeids = {
        "matplotlib/tests/" + name for name in [
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
    selected = []
    deselected = []
    for item in items:
        if (item.module.__name__ in excluded_modules
                or item.nodeid in excluded_nodeids):
            deselected.append(item)
        else:
            selected.append(item)
    items[:] = selected
    config.hook.pytest_deselected(items=deselected)
    invalid_exclusions = (
        (excluded_modules - {item.module.__name__ for item in deselected})
        | (excluded_nodeids - {item.nodeid for item in deselected}))
    if invalid_exclusions:
        warnings.warn("Unused exclusions:\n    {}"
                      .format("\n    ".join(sorted(invalid_exclusions))))


if __name__ == "__main__":
    sys.exit(main())
