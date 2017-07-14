from matplotlib import rcsetup
from matplotlib.backends.backend_qt5 import QtGui, _BackendQT5, FigureCanvasQT
import numpy as np

from .base import FigureCanvasCairo


rcsetup.interactive_bk += ["module://mpl_cairo.qt"]  # NOTE: Should be fixed in Mpl.


class FigureCanvasQTCairo(FigureCanvasCairo, FigureCanvasQT):
    def paintEvent(self, event):
        renderer = self.get_renderer()
        buf = renderer._get_buffer()
        address, _ = buf.__array_interface__["data"]
        width, height = renderer.get_canvas_width_height()
        # The image buffer is not necessarily contiguous, but the padding in
        # the ARGB32 case (each scanline is 32-bit aligned) happens to match
        # what QImage requires.
        qimage = QtGui.QImage(address, width, height,
                              QtGui.QImage.Format_ARGB32_Premultiplied)
        painter = QtGui.QPainter(self)
        painter.drawImage(0, 0, qimage)
        self._draw_rect_callback(painter)
        painter.end()


@_BackendQT5.export
class _BackendQT5Cairo(_BackendQT5):
    FigureCanvas = FigureCanvasQTCairo
