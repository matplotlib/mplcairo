import multiprocessing
import sys

import pytest

import matplotlib as mpl
from matplotlib.figure import Figure
import numpy as np

from matplotlib.backends.backend_agg import FigureCanvasAgg
import mplcairo
from mplcairo import _util, antialias_t
from mplcairo.base import FigureCanvasCairo

# Import an autouse fixture.
from matplotlib.testing.conftest import mpl_test_settings


_canvas_classes = [FigureCanvasAgg, FigureCanvasCairo]


@pytest.mark.parametrize(
    "buf_name", ["random_alpha", "alpha_rows", "opaque"])
def test_cairo_to_straight_rgba8888(benchmark, buf_name):
    assert sys.byteorder == "little"  # BGRA8888
    buf = np.random.RandomState(0).randint(
        0x100, size=(256, 256, 4), dtype=np.uint8)
    if buf_name == "random_alpha":
        buf[..., :3] = buf[..., :3] * (buf[..., 3:] / 0xff)
        s = 29756813
    elif buf_name == "alpha_rows":  # Repeatedly use the same alpha values.
        buf[..., 3] = np.arange(256)[:, None]
        buf[..., :3] = buf[..., :3] * (buf[..., 3:] / 0xff)
        s = 29752202
    elif buf_name == "opaque":
        buf[..., 3] = 0xff
        s = 41774594
    else:
        assert False
    benchmark(_util.cairo_to_straight_rgba8888, buf)
    assert _util.cairo_to_straight_rgba8888(buf).sum() == s


@pytest.fixture
def axes():
    mpl.rcdefaults()
    mplcairo.set_options(cairo_circles=True, raqm=False)
    return Figure().subplots()


def despine(ax):
    ax.set(xticks=[], yticks=[])
    for spine in ax.spines.values():
        spine.set_visible(False)


@pytest.fixture
def sample_vectors():
    return np.random.RandomState(0).random_sample((2, 10000))


@pytest.fixture
def sample_image():
    return np.random.RandomState(0).random_sample((100, 100))


@pytest.mark.parametrize("canvas_cls", _canvas_classes)
def test_axes(benchmark, axes, canvas_cls):
    axes.figure.canvas = canvas_cls(axes.figure)
    benchmark(axes.figure.canvas.draw)


@pytest.mark.parametrize(
    "canvas_cls,antialiased",
    [(FigureCanvasAgg, False),
     (FigureCanvasAgg, True),
     (FigureCanvasCairo, antialias_t.NONE),
     (FigureCanvasCairo, antialias_t.GRAY),
     (FigureCanvasCairo, antialias_t.SUBPIXEL),
     (FigureCanvasCairo, antialias_t.FAST),
     (FigureCanvasCairo, antialias_t.GOOD),
     (FigureCanvasCairo, antialias_t.BEST)])
@pytest.mark.parametrize("joinstyle", ["miter", "round", "bevel"])
def test_line(
        benchmark, axes, sample_vectors, canvas_cls, antialiased, joinstyle):
    with mpl.rc_context({"agg.path.chunksize": 0}):
        axes.plot(*sample_vectors,
                  antialiased=antialiased, solid_joinstyle=joinstyle)
        despine(axes)
        axes.figure.canvas = canvas_cls(axes.figure)
        benchmark(axes.figure.canvas.draw)


# For the marker tests, try both square and round markers, as we have a special
# code path for circles which may not be representative of general performance.


_marker_test_parametrization = pytest.mark.parametrize(
    "canvas_cls, threshold, marker, marker_threads, cairo_circles", [
        (FigureCanvasAgg, 0, "o", 0, False),
        (FigureCanvasAgg, 0, "s", 0, False),
        (FigureCanvasCairo, 0, "o", 0, False),
        (FigureCanvasCairo, 0, "o", 0, True),
        (FigureCanvasCairo, 0, "s", 0, False),
        (FigureCanvasCairo, 1/8, "o", 0, False),
        (FigureCanvasCairo, 1/8, "o", 0, True),
        (FigureCanvasCairo, 1/8, "s", 0, False),
        (FigureCanvasCairo, 1/8, "o", 1, False),
        (FigureCanvasCairo, 1/8, "o", 1, True),
        (FigureCanvasCairo, 1/8, "s", 1, False),
        (FigureCanvasCairo, 1/8, "o", multiprocessing.cpu_count() - 1, False),
        (FigureCanvasCairo, 1/8, "o", multiprocessing.cpu_count() - 1, True),
        (FigureCanvasCairo, 1/8, "s", multiprocessing.cpu_count() - 1, False),
    ]
)


@_marker_test_parametrization
def test_markers(
        benchmark, axes, sample_vectors,
        canvas_cls, threshold, marker, marker_threads, cairo_circles):
    mplcairo.set_options(marker_threads=marker_threads,
                         cairo_circles=cairo_circles)
    with mpl.rc_context({"path.simplify_threshold": threshold}):
        axes.plot(*sample_vectors, linestyle="none", marker=marker)
        despine(axes)
        axes.figure.canvas = canvas_cls(axes.figure)
        benchmark(axes.figure.canvas.draw)
    mplcairo.set_options(marker_threads=0,
                         cairo_circles=False)


@_marker_test_parametrization
def test_scatter_multicolor(
        benchmark, axes, sample_vectors,
        canvas_cls, threshold, marker, marker_threads, cairo_circles):
    mplcairo.set_options(marker_threads=marker_threads,
                         cairo_circles=cairo_circles)
    with mpl.rc_context({"path.simplify_threshold": threshold}):
        a, b = sample_vectors
        axes.scatter(a, a, c=b, marker=marker)
        despine(axes)
        axes.figure.canvas = canvas_cls(axes.figure)
        benchmark(axes.figure.canvas.draw)
    mplcairo.set_options(marker_threads=0,
                         cairo_circles=False)


@_marker_test_parametrization
def test_scatter_multisize(
        benchmark, axes, sample_vectors,
        canvas_cls, threshold, marker, marker_threads, cairo_circles):
    mplcairo.set_options(marker_threads=marker_threads,
                         cairo_circles=cairo_circles)
    with mpl.rc_context({"path.simplify_threshold": threshold}):
        a, b = sample_vectors
        axes.scatter(a, a, s=100 * b ** 2, marker=marker)
        despine(axes)
        axes.figure.canvas = canvas_cls(axes.figure)
        benchmark(axes.figure.canvas.draw)
    mplcairo.set_options(marker_threads=0,
                         cairo_circles=False)


@pytest.mark.parametrize("canvas_cls", _canvas_classes)
def test_image(benchmark, canvas_cls, axes, sample_image):
    axes.imshow(sample_image)
    despine(axes)
    axes.figure.canvas = canvas_cls(axes.figure)
    benchmark(axes.figure.canvas.draw)
