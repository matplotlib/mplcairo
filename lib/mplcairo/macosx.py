import re

from matplotlib.backends.backend_macosx import _BackendMac, FigureCanvasMac

from . import _util
from .base import FigureCanvasCairo


class FigureCanvasMacCairo(FigureCanvasCairo, FigureCanvasMac):

    # A bit hackish, but that's what _macosx.FigureCanvas wants...
    def _draw(self):
        if self.figure.stale:
            self._last_renderer_call = None, None
        self._renderer = _util.cairo_to_straight_rgba8888(
            self.get_renderer(_ensure_drawn=True)._get_buffer())
        return self


@_BackendMac.export
class _BackendMacCairo(_BackendMac):
    FigureCanvas = FigureCanvasMacCairo
