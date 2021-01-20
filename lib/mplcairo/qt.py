import ctypes

from matplotlib.backends.backend_qt5 import _BackendQT5, FigureCanvasQT
from matplotlib.backends.qt_compat import QtGui

from . import _util
from .base import FigureCanvasCairo


class FigureCanvasQTCairo(FigureCanvasCairo, FigureCanvasQT):
    def paintEvent(self, event):
        if hasattr(self, "_update_dpi") and self._update_dpi():
            return  # matplotlib#19123 (<3.4).
        # We always repaint the full canvas (doing otherwise would require an
        # additional copy of the buffer into a contiguous block, so it's not
        # clear it would be faster).
        buf = _util.cairo_to_premultiplied_argb32(
            self.get_renderer(_ensure_drawn=True)._get_buffer())
        height, width, _ = buf.shape
        # The image buffer is not necessarily contiguous, but the padding
        # in the ARGB32 case (each scanline is 32-bit aligned) happens to
        # match what QImage requires; in the RGBA128F case the documented Qt
        # requirement does not seem necessary?
        if QtGui.__name__.startswith("PyQt6"):
            from PyQt6 import sip
            ptr = sip.voidptr(buf)
        else:
            ptr = buf
        qimage = QtGui.QImage(
            ptr, width, height, QtGui.QImage.Format(6))  # ARGB32_Premultiplied
        try:
            qimage_setDevicePixelRatio = qimage.setDevicePixelRatio
        except AttributeError:
            def qimage_setDevicePixelRatio(scaleFactor): pass
        try:  # matplotlib#19126 (<3.4).
            pixel_ratio = self.pixel_ratio
        except AttributeError:
            pixel_ratio = self._dpi_ratio
        qimage_setDevicePixelRatio(self._dpi_ratio)
        # FIXME[PySide{,2}]: https://bugreports.qt.io/browse/PYSIDE-140
        if QtGui.__name__.startswith("PySide"):
            ctypes.c_long.from_address(id(buf)).value -= 1
        painter = QtGui.QPainter(self)
        painter.eraseRect(self.rect())
        painter.drawImage(0, 0, qimage)
        self._draw_rect_callback(painter)
        painter.end()

    def blit(self, bbox=None):  # matplotlib#17478 (<3.3).
        # See above: we always repaint the full canvas.
        self.repaint(self.rect())


@_BackendQT5.export
class _BackendQT5Cairo(_BackendQT5):
    FigureCanvas = FigureCanvasQTCairo
