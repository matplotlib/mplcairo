from matplotlib.backends.backend_macosx import _BackendMac, FigureCanvasMac

from . import _util
from .base import FigureCanvasCairo


class FigureCanvasMacCairo(FigureCanvasCairo, FigureCanvasMac):

    def _draw(self):
        renderer = self.get_renderer()
        renderer.clear()
        self.figure.draw(renderer)
        # A bit hackish, but that's what _macosx.FigureCanvas wants...
        self._renderer = _util.cairo_to_straight_rgba8888(
            renderer._get_buffer())
        return self


@_BackendMac.export
class _BackendMacCairo(_BackendMac):
    FigureCanvas = FigureCanvasMacCairo
