#!/usr/bin/env python
"""
Run the Matplotlib test suite, using the mplcairo backend to patch out
Matplotlib's agg backend.

.. PYTEST_DONT_REWRITE
"""

from argparse import ArgumentParser
import os
from pathlib import Path
import shutil
import subprocess
from subprocess import CalledProcessError
import sys
import warnings

import matplotlib as mpl
import matplotlib.backends.backend_agg
import matplotlib.testing.decorators
import mplcairo.base

import pytest


_IGNORED_FAILURES = {}


def main(argv=None):
    parser = ArgumentParser(
        description="""\
Run the Matplotlib test suite, using the mplcairo backend to patch out
Matplotlib's agg backend.
""",
        epilog="Other arguments are forwarded to pytest.")
    parser.add_argument("--tolerance", type=float,
                        help="Set image comparison tolerance.")
    args, rest = parser.parse_known_args(argv)

    if args.tolerance is not None:
        def _raise_on_image_difference(expected, actual, tol):
            cmp = mpl.testing.compare.compare_images(
                expected, actual, tol, in_decorator=True)
            if cmp:
                if cmp["rms"] < args.tolerance:
                    expected = Path(expected)
                    expected = expected.relative_to(expected.parent.parent)
                    _IGNORED_FAILURES[expected] = cmp["rms"]
                else:
                    __orig_raise_on_image_tolerance(expected, actual, tol)
        __orig_raise_on_image_tolerance = \
            mpl.testing.decorators._raise_on_image_difference
        mpl.testing.decorators._raise_on_image_difference = \
            _raise_on_image_difference

    mplcairo.base.get_hinting_flag = mpl.backends.backend_agg.get_hinting_flag
    mplcairo.base.FigureCanvasAgg = \
        mplcairo.base.FigureCanvasCairo
    mplcairo.base.RendererAgg = \
        mplcairo.base.GraphicsContextRendererCairo
    mpl.backends.backend_agg = \
        sys.modules["matplotlib.backends.backend_agg"] = mplcairo.base

    mpl.use("agg", warn=False, force=True)
    from matplotlib import pyplot as plt

    __orig_switch_backend = plt.switch_backend
    def switch_backend(backend):
        __orig_switch_backend({
            "gtk3agg": "module://mplcairo.gtk",
            "qt5agg": "module://mplcairo.qt",
            "tkagg": "module://mplcairo.tk",
            "wxagg": "module://mplcairo.wx",
        }.get(backend.lower(), backend))
    plt.switch_backend = switch_backend

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
    xfail_modules = {
        "matplotlib.tests.test_compare_images",
    }
    xfail_nodeids = {
        "matplotlib/tests/" + name for name in [
            "test_agg.py::test_repeated_save_with_alpha",
            "test_artist.py::test_cull_markers",
            "test_axes.py::test_log_scales[png]",
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
            "test_scale.py::test_logscale_mask[png]",
            "test_simplification.py::test_throw_rendering_complexity_exceeded",
        ]
    }
    xfails = []
    for item in items:
        if (item.module.__name__ in xfail_modules
                or item.nodeid in xfail_nodeids):
            xfails.append(item)
            item.add_marker(
                pytest.mark.xfail(reason="Test known to fail with mplcairo"))
    invalid_xfails = (
        (xfail_modules - {item.module.__name__ for item in xfails})
        | (xfail_nodeids - {item.nodeid for item in xfails}))
    if invalid_xfails:
        warnings.warn("Unused xfails:\n    {}"
                      .format("\n    ".join(sorted(invalid_xfails))))


def pytest_terminal_summary(terminalreporter, exitstatus):
    write = terminalreporter.write
    if _IGNORED_FAILURES:
        write("\n"
              "Ignored the following image comparison failures:\n"
              "RMS\texpected\n")
        for rms, expected in sorted(
                ((v, k) for k, v in _IGNORED_FAILURES.items()), reverse=True):
            write("{:#.2f}\t{}\n".format(rms, expected))


if __name__ == "__main__":
    sys.exit(main())
