import ctypes

# This does support QT_API=pyqt6 on Matplotlib versions that can handle it.
try:
    from matplotlib.backends.backend_qt import _BackendQT, FigureCanvasQT
except ImportError:
    from matplotlib.backends.backend_qt5 import (
        _BackendQT5 as _BackendQT, FigureCanvasQT)
from matplotlib.backends.qt_compat import QtCore, QtGui

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
            self.get_renderer()._get_buffer())
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
        getattr(qimage, "setDevicePixelRatio", lambda _: None)(
            self.device_pixel_ratio)
        # https://bugreports.qt.io/browse/PYSIDE-140
        if (QtCore.__name__.startswith("PySide")
                and QtCore.__version_info__ < (5, 12)):
            ctypes.c_long.from_address(id(buf)).value -= 1
        painter = QtGui.QPainter(self)
        painter.eraseRect(self.rect())
        painter.drawImage(0, 0, qimage)
        self._draw_rect_callback(painter)
        painter.end()

    def blit(self, bbox=None):  # matplotlib#17478 (<3.3).
        # See above: we always repaint the full canvas.
        self.repaint(self.rect())


@_BackendQT.export
class _BackendQTCairo(_BackendQT):
    FigureCanvas = FigureCanvasQTCairo
