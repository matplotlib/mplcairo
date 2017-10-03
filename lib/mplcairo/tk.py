from matplotlib.backends import tkagg
from matplotlib.backends._backend_tk import _BackendTk, FigureCanvasTk
import numpy as np

from . import _util
from .base import FigureCanvasCairo


class FigureCanvasTkCairo(FigureCanvasCairo, FigureCanvasTk):
    def draw(self):
        super().draw()
        buf = self.get_renderer()._get_buffer()
        height, width, _ = buf.shape
        buf = _util.to_premultiplied_rgba8888(buf)  # Empirically checked.
        tkagg.blit(self._tkphoto, buf, colormode=2)
        self._master.update_idletasks()

    blit = draw


@_BackendTk.export
class _BackendTkCairo(_BackendTk):
    FigureCanvas = FigureCanvasTkCairo
