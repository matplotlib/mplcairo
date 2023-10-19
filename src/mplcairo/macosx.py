from matplotlib.backends.backend_macosx import _BackendMac, FigureCanvasMac

from . import _mplcairo
from .base import FigureCanvasCairo, _LOCK


class FigureCanvasMacCairo(FigureCanvasCairo, FigureCanvasMac):

    if hasattr(FigureCanvasMac, "_draw"):
        # Matplotlib<3.6.

        def _draw(self):
            renderer = self.get_renderer()
            renderer.clear()
            self.figure.draw(renderer)
            # A bit hackish, but that's what _macosx.FigureCanvas wants...
            self._renderer = _mplcairo.cairo_to_straight_rgba8888(
                renderer._get_buffer())
            return self

    else:
        # Matplotlib>=3.6: just a copy-paste of FigureCanvasMac.draw, but the
        # copy ensures that super() correctly refers to FigureCanvasCairo, not
        # FigureCanvasAgg.

        def draw(self):
            if self._is_drawing:
                return
            self._is_drawing = True
            try:
                super().draw()
            finally:
                self._is_drawing = False
            self.update()

        # Inheriting from FigureCanvasCairo.restore_region would call
        # super().draw() which would incorrectly refer to FigureCanvasAgg;
        # directly call self.update() instead.

        def restore_region(self, region):
            with _LOCK:
                self.get_renderer().restore_region(region)
            self.update()


@_BackendMac.export
class _BackendMacCairo(_BackendMac):
    FigureCanvas = FigureCanvasMacCairo
