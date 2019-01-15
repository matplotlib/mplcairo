import ctypes

from matplotlib.backends import qt_compat
from matplotlib.backends.backend_qt5 import QtGui, _BackendQT5, FigureCanvasQT

from . import _util
from .base import FigureCanvasCairo


class FigureCanvasQTCairo(FigureCanvasCairo, FigureCanvasQT):
    def paintEvent(self, event):
        if self._update_dpi():
            return
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
        qimage = QtGui.QImage(buf, width, height,
                              QtGui.QImage.Format_ARGB32_Premultiplied)
        getattr(qimage, "setDevicePixelRatio", lambda _: None)(self._dpi_ratio)
        # FIXME[PySide{,2}]: https://bugreports.qt.io/browse/PYSIDE-140
        if qt_compat.QT_API.startswith("PySide"):
            ctypes.c_long.from_address(id(buf)).value -= 1
        painter = QtGui.QPainter(self)
        painter.eraseRect(self.rect())
        painter.drawImage(0, 0, qimage)
        self._draw_rect_callback(painter)
        painter.end()

    def blit(self, bbox=None):
        # See above: we always repaint the full canvas.
        self.repaint(self.rect())


@_BackendQT5.export
class _BackendQT5Cairo(_BackendQT5):
    FigureCanvas = FigureCanvasQTCairo
