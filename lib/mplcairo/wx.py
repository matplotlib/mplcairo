from matplotlib.backends.backend_wx import (
    _BackendWx, _FigureCanvasWxBase, FigureFrameWx, NavigationToolbar2Wx)
from matplotlib.backends.backend_wxagg import FigureCanvasWxAgg
import wx

from . import base
from .base import FigureCanvasCairo


class FigureFrameWxCairo(FigureFrameWx):
    def get_canvas(self, fig):
        return FigureCanvasWxCairo(self, -1, fig)


class FigureCanvasWxCairo(FigureCanvasCairo, _FigureCanvasWxBase):
    def __init__(self, parent, id, figure):
        # Inline the call to FigureCanvasCairo.__init__ as _FigureCanvasWxBase
        # has a different signature and thus we cannot use cooperative
        # inheritance.
        base._fix_ipython_backend2gui()
        _FigureCanvasWxBase.__init__(self, parent, id, figure)

    if hasattr(_FigureCanvasWxBase, "gui_repaint"):  # pre-mpl#11944.
        def draw(self, drawDC=None):
            super().draw()
            buf = self.get_renderer()._get_buffer()
            height, width, _ = buf.shape
            self.bitmap = wx.Bitmap(width, height, 32)
            self.bitmap.CopyFromBuffer(buf, wx.BitmapBufferFormat_ARGB32)
            self._isDrawn = True
            self.gui_repaint(drawDC=drawDC, origin="WXCairo")
    else:
        def draw(self):
            # Copied from FigureCanvasWx.draw, bypassing
            # FigureCanvasCairo.draw.
            self._draw()
            self.Refresh()

        def _draw(self):
            # This can't use super().draw() (i.e. FigureCanvasCairo.draw)
            # because it calls its own super().draw(), leading to an infinite
            # loop.
            with base._LOCK:
                self.figure.draw(self.get_renderer())
            buf = self.get_renderer()._get_buffer()
            height, width, _ = buf.shape
            self.bitmap = wx.Bitmap(width, height, 32)
            self.bitmap.CopyFromBuffer(buf, wx.BitmapBufferFormat_ARGB32)
            self._isDrawn = True

    blit = FigureCanvasWxAgg.blit


@_BackendWx.export
class _BackendWxCairo(_BackendWx):
    FigureCanvas = FigureCanvasWxCairo
    _frame_class = FigureFrameWxCairo
