from matplotlib.backends.backend_wx import (
    _BackendWx, _FigureCanvasWxBase, FigureFrameWx, NavigationToolbar2Wx)
import wx

from . import _util, base
from .base import FigureCanvasCairo


class FigureFrameWxCairo(FigureFrameWx):
    def get_canvas(self, fig):
        return FigureCanvasWxCairo(self, -1, fig)


# See Bitmap.FromBufferRGBA: "On Windows and Mac the RGB values will be
# 'premultiplied' by the alpha values. (The other platforms do the
# multiplication themselves.)
_to_native_bitmap = (
    _util.to_premultiplied_rgba8888
    if wx.GetOsVersion()[0] & (wx.OS_WINDOWS | wx.OS_MAC) else
    _util.to_unmultiplied_rgba8888)


class FigureCanvasWxCairo(FigureCanvasCairo, _FigureCanvasWxBase):
    def __init__(self, parent, id, figure):
        # Inline the call to FigureCanvasCairo.__init__ as _FigureCanvasWxBase
        # has a different signature and thus we cannot use cooperative
        # inheritance.
        base._fix_ipython_backend2gui()
        _FigureCanvasWxBase.__init__(self, parent, id, figure)

    def draw(self, drawDC=None):
        super().draw()
        buf = self.get_renderer()._get_buffer()
        height, width, _ = buf.shape
        self.bitmap = wx.Bitmap.FromBufferRGBA(
            width, height, _util.to_unmultiplied_rgba8888(buf))
        self._isDrawn = True
        self.gui_repaint(drawDC=drawDC, origin='WXCairo')

    def blit(self, bbox=None):
        self.draw()


@_BackendWx.export
class _BackendWxCairo(_BackendWx):
    FigureCanvas = FigureCanvasWxCairo
    _frame_class = FigureFrameWxCairo
