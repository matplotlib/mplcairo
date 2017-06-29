import pytest

from matplotlib.testing.conftest import mpl_test_settings
from matplotlib.figure import Figure
from matplotlib.backends.backend_qt5 import QtGui
import numpy as np

from matplotlib.backends.backend_qt5agg import FigureCanvasQTAgg
from mpl_cairo.qt import FigureCanvasQTCairo


pytest.fixture(autouse=True)(mpl_test_settings)


@pytest.fixture
def axes():
    fig = Figure()
    return fig.add_subplot(111)


def despine(ax):
    ax.set(xticks=[], yticks=[])
    for spine in ax.spines.values():
        spine.set_visible(False)


@pytest.fixture
def sample_vector():
    return np.random.RandomState(0).random_sample(10000)


@pytest.fixture
def sample_image():
    return np.random.RandomState(0).random_sample((100, 100))


@pytest.mark.parametrize(
    "canvas_cls", [FigureCanvasQTAgg, FigureCanvasQTCairo])
def test_axes(benchmark, canvas_cls, axes):
    axes.figure.canvas = canvas_cls(axes.figure)
    benchmark(axes.figure.canvas.draw)


@pytest.mark.parametrize(
    "canvas_cls", [FigureCanvasQTAgg, FigureCanvasQTCairo])
def test_line(benchmark, canvas_cls, axes, sample_vector):
    axes.plot(sample_vector)
    despine(axes)
    axes.figure.canvas = canvas_cls(axes.figure)
    benchmark(axes.figure.canvas.draw)


@pytest.mark.parametrize(
    "canvas_cls", [FigureCanvasQTAgg, FigureCanvasQTCairo])
def test_circles(benchmark, canvas_cls, axes, sample_vector):
    axes.plot(sample_vector, "o")
    despine(axes)
    axes.figure.canvas = canvas_cls(axes.figure)
    benchmark(axes.figure.canvas.draw)


@pytest.mark.parametrize(
    "canvas_cls", [FigureCanvasQTAgg, FigureCanvasQTCairo])
def test_squares(benchmark, canvas_cls, axes, sample_vector):
    axes.plot(sample_vector, "s")
    despine(axes)
    axes.figure.canvas = canvas_cls(axes.figure)
    benchmark(axes.figure.canvas.draw)


@pytest.mark.parametrize(
    "canvas_cls", [FigureCanvasQTAgg, FigureCanvasQTCairo])
def test_image(benchmark, canvas_cls, axes, sample_image):
    axes.imshow(sample_image)
    despine(axes)
    axes.figure.canvas = canvas_cls(axes.figure)
    benchmark(axes.figure.canvas.draw)
