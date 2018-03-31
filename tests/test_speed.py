import pytest

import matplotlib as mpl
from matplotlib.testing.conftest import mpl_test_settings
from matplotlib.figure import Figure
import numpy as np

from matplotlib.backends.backend_agg import FigureCanvasAgg
from mplcairo import antialias_t
from mplcairo.base import FigureCanvasCairo


_canvas_classes = [FigureCanvasAgg, FigureCanvasCairo]
pytest.fixture(autouse=True)(mpl_test_settings)


@pytest.fixture
def axes():
    mpl.rcdefaults()
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
def test_axes(benchmark, canvas_cls, axes):
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
        benchmark, canvas_cls, antialiased, joinstyle, axes, sample_vectors):
    with mpl.rc_context({"agg.path.chunksize": 0}):
        axes.plot(*sample_vectors,
                  antialiased=antialiased, solid_joinstyle=joinstyle)
        despine(axes)
        axes.figure.canvas = canvas_cls(axes.figure)
        benchmark(axes.figure.canvas.draw)


# For the marker tests, try both square and round markers, as we have a special
# code path for circles which may not be representative of general performance.


@pytest.mark.parametrize("canvas_cls", _canvas_classes)
@pytest.mark.parametrize("threshold", [1 / 8, 0])
@pytest.mark.parametrize("marker", ["o", "s"])
def test_markers(
        benchmark, canvas_cls, threshold, marker, axes, sample_vectors):
    with mpl.rc_context({"path.simplify_threshold": threshold}):
        axes.plot(*sample_vectors, marker=marker)
        despine(axes)
        axes.figure.canvas = canvas_cls(axes.figure)
        benchmark(axes.figure.canvas.draw)


@pytest.mark.parametrize("canvas_cls", _canvas_classes)
@pytest.mark.parametrize("threshold", [1 / 8, 0])
@pytest.mark.parametrize("marker", ["o", "s"])
def test_scatter_multicolor(
        benchmark, canvas_cls, threshold, marker, axes, sample_vectors):
    with mpl.rc_context({"path.simplify_threshold": threshold}):
        a, b = sample_vectors
        axes.scatter(a, a, c=b, marker=marker)
        despine(axes)
        axes.figure.canvas = canvas_cls(axes.figure)
        benchmark(axes.figure.canvas.draw)


@pytest.mark.parametrize("canvas_cls", _canvas_classes)
@pytest.mark.parametrize("threshold", [1 / 8, 0])
@pytest.mark.parametrize("marker", ["o", "s"])
def test_scatter_multisize(
        benchmark, canvas_cls, threshold, marker, axes, sample_vectors):
    with mpl.rc_context({"path.simplify_threshold": threshold}):
        a, b = sample_vectors
        axes.scatter(a, a, s=100 * b ** 2, marker=marker)
        despine(axes)
        axes.figure.canvas = canvas_cls(axes.figure)
        benchmark(axes.figure.canvas.draw)


@pytest.mark.parametrize("canvas_cls", _canvas_classes)
def test_image(benchmark, canvas_cls, axes, sample_image):
    axes.imshow(sample_image)
    despine(axes)
    axes.figure.canvas = canvas_cls(axes.figure)
    benchmark(axes.figure.canvas.draw)
