from matplotlib import rcsetup
from matplotlib.backends.backend_qt5 import QtGui, _BackendQT5, FigureCanvasQT
from matplotlib.backends.qt_compat import QT_API

from . import format_t, GraphicsContextRendererCairo


rcsetup.interactive_bk += ["module://mpl_cairo.qt"]  # NOTE: Should be fixed in Mpl.


class FigureCanvasQTCairo(FigureCanvasQT):
    def __init__(self, figure):
        super(FigureCanvasQTCairo, self).__init__(figure=figure)
        self._renderer = None

    def draw(self):
        self._renderer = GraphicsContextRendererCairo(self.figure.dpi)
        self._renderer.set_ctx_from_image_args(
            format_t.ARGB32, self.width(), self.height())
        self.figure.draw(self._renderer)
        super().draw()

    def copy_from_bbox(self, bbox):
        if self._renderer is None:
            self.draw()
        return self._renderer.copy_from_bbox(bbox)

    def restore_region(self, region):
        if self._renderer is None:
            self.draw()
        self._renderer.restore_region(region)
        self.update()

    def paintEvent(self, event):
        if self._renderer is None:
            self.draw()
        buf_address = self._renderer.get_data_address()
        width, height = self._renderer.get_canvas_width_height()
        # The image buffer is not necessarily contiguous, but the padding in
        # the ARGB32 case (each scanline is 32-bit aligned) happens to match
        # what QImage requires.
        qimage = QtGui.QImage(buf_address, width, height,
                              QtGui.QImage.Format_ARGB32_Premultiplied)
        # Adjust the buf reference count to work around a memory leak bug
        # in QImage under PySide on Python 3.
        if QT_API == "PySide" and six.PY3:
            ctypes.c_long.from_address(id(buf)).value = 1
        painter = QtGui.QPainter(self)
        painter.drawImage(0, 0, qimage)
        self._draw_rect_callback(painter)
        painter.end()


@_BackendQT5.export
class _BackendQT5Cairo(_BackendQT5):
    FigureCanvas = FigureCanvasQTCairo
