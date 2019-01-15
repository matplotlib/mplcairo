from functools import partial

from matplotlib.backends._backend_tk import _BackendTk, FigureCanvasTk

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
        buf = _util.cairo_to_premultiplied_rgba8888(
            self.get_renderer()._get_buffer())
        _tk_blit(self._tkphoto, buf)
        self._master.update_idletasks()

    def blit(self, bbox=None):
        buf = _util.cairo_to_premultiplied_rgba8888(
            self.get_renderer()._get_buffer())
        _tk_blit(self._tkphoto, buf, bbox=bbox)
        self._master.update_idletasks()


@_BackendTk.export
class _BackendTkCairo(_BackendTk):
    FigureCanvas = FigureCanvasTkCairo
