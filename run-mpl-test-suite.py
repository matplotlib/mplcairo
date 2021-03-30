#!/usr/bin/env python
"""
Run the Matplotlib test suite, using the mplcairo backend to patch out
Matplotlib's agg backend.

.. PYTEST_DONT_REWRITE
"""

from argparse import ArgumentParser
from distutils.version import LooseVersion
import os
from pathlib import Path
import sys
import warnings

os.environ["MPLBACKEND"] = "agg"  # Avoid irrelevant framework issues on macOS.

import mplcairo.base  # Need to come before matplotlib import on macOS.

import matplotlib as mpl
import matplotlib.backends.backend_agg
import matplotlib.testing.decorators

import pytest


_IGNORED_FAILURES = {}


if sys.platform == "win32":
    # Inkscape>=1.0 testing is broken on Windows as of Matplotlib 3.3 (earlier
    # versions of Matplotlib don't support Inkscape>=1.0 at all): Inkscape
    # installs both inkscape.exe and inkscape.com, but only the latter supports
    # shell usage whereas e.g. chocolatey only adds the former to %PATH% (via a
    # shim).
    mpl.testing.compare.converter.pop("inkscape", None)


def main(argv=None):
    parser = ArgumentParser(
        description="""\
Run the Matplotlib test suite, using the mplcairo backend to patch out
Matplotlib's agg backend.

To specify a single test module, use ``--pyargs matplotlib.tests.test_foo``.
""",
        epilog="Other arguments are forwarded to pytest.")
    parser.add_argument("--tolerance", type=float,
                        help="Set image comparison tolerance.")
    args, rest = parser.parse_known_args(argv)
    if "--pyargs" not in rest:
        rest.extend(["--pyargs", "matplotlib"])

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

    with warnings.catch_warnings(record=True):  # mpl 3.0
        mpl.use("agg", force=True)
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

    mplcairo._mplcairo.install_abrt_handler()

    return pytest.main(
        ["--rootdir", str(Path(mpl.__file__).parents[1]), "-p", "__main__",
         *rest])


def pytest_collection_modifyitems(session, config, items):
    if len(items) == 0:
        pytest.exit("No tests found; Matplotlib was likely installed without "
                    "test data.")
    knownfail = pytest.mark.xfail(reason="Test known to fail with mplcairo.")
    irrelevant = pytest.mark.xfail(reason="Test irrelevant for mplcairo.")
    textfail = pytest.mark.xfail(reason=
        "Test failure with large diff due to different text rendering by "
        "mplcairo.")
    debugskip = pytest.mark.skip(reason="Temporarily skipped for debugging.")
    module_markers = {
        "matplotlib.tests.test_compare_images": irrelevant,
        "matplotlib.tests.test_backend_pgf": irrelevant,
        "matplotlib.tests.test_mathtext": textfail,
        "matplotlib.tests.test_constrainedlayout": textfail,
        "matplotlib.tests.test_tightlayout": textfail,
    }
    nodeid_markers = {
        "matplotlib/tests/" + nodeid: marker
        for marker, nodeids in [
            (knownfail, [
                "test_image.py::test_jpeg_alpha",
                "test_image.py::test_figimage[pdf-False]",
                "test_image.py::test_figimage[pdf-True]",
                "test_image.py::test_figimage0[pdf]",
                "test_image.py::test_figimage1[pdf]",
            ]),
            (irrelevant, [
                # Precision is lost when roundtripping between straight and
                # premultiplied alpha.
                "test_agg.py::test_repeated_save_with_alpha",
                # cairo doesn't cull out-of-bound markers.
                "test_artist.py::test_cull_markers",
                "test_axes.py::test_get_tightbbox_polar",
                "test_axes.py::test_normal_axes",
                "test_backend_bases.py::test_non_gui_warning",
                # Test is coupled with internal representation.
                "test_backend_pdf.py::test_composite_image",
                # Different error messages on invalid metadata.
                "test_backend_pdf.py::test_invalid_metadata",
                # PdfPages is tightly coupled with backend_pdf.
                "test_backend_pdf.py::test_multipage_pagecount",
                "test_backend_pdf.py::test_multipage_properfinalize",
                # cairo doesn't support the Trapped metadata.
                "test_backend_pdf.py::test_savefig_metadata",
                # cairo doesn't emit HiResBoundingBox.
                "test_backend_ps.py::test_bbox",
                # We're fine with partial usetex.
                "test_backend_ps.py::test_partial_usetex",
                # We do not support writing PS to text-mode streams.
                "test_backend_ps.py::test_savefig_to_stringio[ps-landscape]",
                "test_backend_ps.py::test_savefig_to_stringio[ps-portrait]",
                "test_backend_ps.py::test_savefig_to_stringio[eps-landscape]",
                "test_backend_ps.py::test_savefig_to_stringio[eps-portrait]",
                "test_backend_ps.py::test_savefig_to_stringio[eps afm-landscape]",
                "test_backend_ps.py::test_savefig_to_stringio[eps afm-portrait]",
                # cairo doesn't support SOURCE_DATE_EPOCH.
                "test_backend_ps.py::test_source_date_epoch",
                # Useful, but the tag structure is too different (e.g. cairo
                # skips emitting clips that don't intersect paths).
                "test_backend_svg.py::test_count_bitmaps",
                # cairo doesn't support custom gids.
                "test_backend_svg.py::test_gid",
                "test_backend_svg.py::test_svg_clear_all_metadata",
                "test_backend_svg.py::test_svg_clear_default_metadata",
                "test_backend_svg.py::test_svg_default_metadata",
                "test_backend_svg.py::test_svg_metadata",
                # cairo always emits text as glyph paths.
                "test_backend_svg.py::test_svgnone_with_data_coordinates",
                # cairo can't emit urls in SVG.
                "test_backend_svg.py::test_text_urls",
                "test_backend_svg.py::test_url",
                "test_backend_svg.py::test_url_tick",
                # Different tight bbox.
                "test_bbox_tight.py::test_bbox_inches_tight_suptile_legend[",
                "test_bbox_tight.py::test_bbox_inches_tight_suptitle_non_default[",
                # We already raise on invalid savefig kwargs.
                "test_figure.py::test_savefig_warns",
                # cairo uses a different representation for ps images (but
                # compositing is correct, see e.g. SVG output).
                "test_image.py::test_composite[",
                # Different tight bbox.
                "test_polar.py::test_get_tightbbox_polar",
                # cairo does not have an explicit rendering complexity limit.
                "test_simplification.py::test_throw_rendering_complexity_exceeded",
            ]),
            (textfail, [
                "test_axes.py::test_gettightbbox_ignoreNaN",
                "test_figure.py::test_align_labels[",
                "test_figure.py::test_tightbbox",
                "test_backend_pdf.py::test_text_urls",
                "test_backend_pdf.py::test_text_urls_tex",
            ]),
            (debugskip, [
            ])
        ]
        for nodeid in nodeids
    }
    if LooseVersion(mpl.__version__) < "3.0":
        module_markers.update({
            "matplotlib.sphinxext.test_tinypages": irrelevant,  # matplotlib#11360.
        })
        nodeid_markers.update({
            "matplotlib/tests" + nodeid: marker
            for marker, nodeids in [
                (irrelevant, [
                    "test_backend_pdf.py::test_empty_rasterised",
                ])
            ]
            for nodeid in nodeids
        })
    markers = []
    for item in items:
        marker = (module_markers.get(item.module.__name__)
                  or nodeid_markers.get(item.nodeid)
                  or (nodeid_markers.get(item.nodeid.split("[")[0] + "[")
                      if "[" in item.nodeid else None))
        if marker:
            markers.append(item)
            item.add_marker(marker)
    if config.getoption("file_or_dir") == ["matplotlib"]:
        invalid_markers = (
            ({*module_markers} - {item.module.__name__ for item in markers})
            | ({*nodeid_markers}
               - {item.nodeid for item in markers}
               - {item.nodeid.split("[")[0] + "[" for item in markers
                  if "[" in item.nodeid}))
        if invalid_markers:
            warnings.warn("Unused xfails:\n    {}"
                          .format("\n    ".join(sorted(invalid_markers))))


def pytest_terminal_summary(terminalreporter, exitstatus):
    write = terminalreporter.write
    if _IGNORED_FAILURES:
        write("\n"
              "Ignored the following image comparison failures:\n"
              "RMS\texpected\n")
        for rms, expected in sorted(
                ((v, k) for k, v in _IGNORED_FAILURES.items()), reverse=True):
            write(f"{rms:#.2f}\t{expected}\n")


if __name__ == "__main__":
    sys.exit(main())
