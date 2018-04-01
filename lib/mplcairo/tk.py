from functools import partial

from matplotlib.backends._backend_tk import _BackendTk, FigureCanvasTk
import numpy as np

from . import _util
from .base import FigureCanvasCairo

try:
    from matplotlib.backends._backend_tk import blit as _mpl3_blit
    _tk_blit = partial(_mpl3_blit, offsets=(0, 1, 2, 3))
except ImportError:
    from matplotlib.backends.tkagg import blit as _mpl2_blit
    _tk_blit = partial(_mpl2_blit, colormode=2)


class FigureCanvasTkCairo(FigureCanvasCairo, FigureCanvasTk):
    def draw(self):
        super().draw()
        buf = self.get_renderer()._get_buffer()
        height, width, _ = buf.shape
        buf = _util.to_premultiplied_rgba8888(buf)
        _tk_blit(self._tkphoto, buf)
        self._master.update_idletasks()

    blit = draw


@_BackendTk.export
class _BackendTkCairo(_BackendTk):
    FigureCanvas = FigureCanvasTkCairo
