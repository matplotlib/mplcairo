from matplotlib.backends.backend_qt5 import QtGui, _BackendQT5, FigureCanvasQT
import numpy as np

from .base import FigureCanvasCairo


class FigureCanvasQTCairo(FigureCanvasCairo, FigureCanvasQT):
    def paintEvent(self, event):
        if self._update_dpi():
            return
        # We always repaint the full canvas (doing otherwise would require an
        # additional copy of the buffer into a contiguous block, so it's not
        # clear it would be faster).
        buf = self.get_renderer(_draw_if_new=True)._get_buffer()
        address, _ = buf.__array_interface__["data"]
        height, width, _ = buf.shape
        # The image buffer is not necessarily contiguous, but the padding in
        # the ARGB32 case (each scanline is 32-bit aligned) happens to match
        # what QImage requires.
        qimage = QtGui.QImage(address, width, height,
                              QtGui.QImage.Format_ARGB32_Premultiplied)
        qimage.setDevicePixelRatio(self._dpi_ratio)
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
