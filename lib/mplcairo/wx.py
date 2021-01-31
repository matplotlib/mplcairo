from matplotlib.backends.backend_wx import (
    _BackendWx, _FigureCanvasWxBase, FigureFrameWx)
import wx

from . import _util
from .base import FigureCanvasCairo


class FigureFrameWxCairo(FigureFrameWx):
    def get_canvas(self, fig):
        return FigureCanvasWxCairo(self, -1, fig)


class FigureCanvasWxCairo(FigureCanvasCairo, _FigureCanvasWxBase):
    def __init__(self, parent, id, figure):
        # Inline the call to FigureCanvasCairo.__init__ as _FigureCanvasWxBase
        # has a different signature and thus we cannot use cooperative
        # inheritance.
        _util.fix_ipython_backend2gui()
        _FigureCanvasWxBase.__init__(self, parent, id, figure)

    def draw(self, drawDC=None):
        super().draw()
        # The source of wx.lib.wxcairo.BitmapFromImageSurface seems to suggest
        # that one can directly pass premultiplied RGBA to wx.Bitmap, but this
        # is incorrect (likely a bug?).
        buf = _util.cairo_to_straight_rgba8888(
            self.get_renderer()._get_buffer())
        height, width, _ = buf.shape
        self.bitmap = wx.Bitmap.FromBufferRGBA(width, height, buf)
        self._isDrawn = True
        self.gui_repaint(drawDC=drawDC)

    def blit(self, bbox=None):
        self.draw()


@_BackendWx.export
class _BackendWxCairo(_BackendWx):
    FigureCanvas = FigureCanvasWxCairo
    _frame_class = FigureFrameWxCairo
