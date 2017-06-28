import sip

from matplotlib import rcsetup
from matplotlib.backend_bases import GraphicsContextBase, RendererBase
from matplotlib.backends.backend_qt5 import QtGui, _BackendQT5, FigureCanvasQT
from matplotlib.backends.qt_compat import QT_API

from . import _mpl_cairo


rcsetup.interactive_bk += ["module://mpl_cairo.qt"]  # NOTE: Should be fixed in Mpl.
FORMAT_ARGB32 = 0  # FIXME: We should export the values from the header.


class GraphicsContextRendererCairo(
        _mpl_cairo.GraphicsContextRendererCairo,
        # Fill in the missing methods.
        GraphicsContextBase,
        RendererBase):
    pass


class FigureCanvasQTCairo(FigureCanvasQT):
    def __init__(self, figure):
        super(FigureCanvasQTCairo, self).__init__(figure=figure)
        self._renderer = GraphicsContextRendererCairo(self.figure.dpi)

    def paintEvent(self, event):
        width = self.width()
        height = self.height()
        self._renderer.set_ctx_from_image_args(FORMAT_ARGB32, width, height)
        self.figure.draw(self._renderer)
        buf = sip.voidptr(self._renderer.get_data_address())
        qimage = QtGui.QImage(buf, width, height,
                              QtGui.QImage.Format_ARGB32_Premultiplied)
        # Adjust the buf reference count to work around a memory leak bug
        # in QImage under PySide on Python 3.
        if QT_API == 'PySide' and six.PY3:
            ctypes.c_long.from_address(id(buf)).value = 1
        painter = QtGui.QPainter(self)
        painter.drawImage(0, 0, qimage)
        self._draw_rect_callback(painter)
        painter.end()


@_BackendQT5.export
class _BackendQT5Cairo(_BackendQT5):
    FigureCanvas = FigureCanvasQTCairo
