import ctypes
import sys

# This does support QT_API=pyqt6 on Matplotlib versions that can handle it.
try:
    from matplotlib.backends.backend_qt import _BackendQT, FigureCanvasQT
except ImportError:
    from matplotlib.backends.backend_qt5 import (
        _BackendQT5 as _BackendQT, FigureCanvasQT)
from matplotlib.backends.qt_compat import QtCore, QtGui

from . import _mplcairo, _util
from .base import FigureCanvasCairo


_QT_VERSION = tuple(QtCore.QLibraryInfo.version().segments())


class FigureCanvasQTCairo(FigureCanvasCairo, FigureCanvasQT):
    def paintEvent(self, event):
        if hasattr(self, "_update_dpi") and self._update_dpi():
            return  # matplotlib#19123 (<3.4).
        # We always repaint the full canvas (doing otherwise would require an
        # additional copy of the buffer into a contiguous block, so it's not
        # clear it would be faster).
        buf = self.get_renderer()._get_buffer()
        fmt = _util.detect_buffer_format(buf)
        if fmt == "argb32":
            qfmt = 6  # ARGB32_Premultiplied
        elif fmt == "rgb24":
            assert buf.strides[1] == 4  # alpha channel as padding.
            qfmt = 4  # RGB32
        elif fmt == "a8":
            qfmt = 24  # Grayscale8
        elif fmt == "a1":
            qfmt = {"big": 1, "little": 2}[sys.byteorder]  # Mono, Mono_LSB
        elif fmt == "rgb16_565":
            qfmt = 7  # RGB16
        elif fmt == "rgb30":
            qfmt = 21  # RGB30
        elif fmt == "rgb96f":
            raise ValueError(f"{fmt} is not supported by Qt")
        elif fmt == "rgba128f":
            if _QT_VERSION >= (6, 2):
                qfmt = 35  # RGBA32FPx4_Premultiplied
            else:
                qfmt = 6
                buf = _mplcairo.cairo_to_premultiplied_argb32(buf)
        if fmt == "a1":
            height, = buf.shape
            fieldname, = buf.dtype.names
            assert fieldname[0] == "V"
            width = int(fieldname[1:])
        else:
            height, width = buf.shape[:2]
        ptr = (
            # Also supports non-contiguous (RGB24) data.
            buf.ctypes.data if QtCore.__name__.startswith("PyQt") else
            # Required by PySide, but fails for non-contiguous data.
            buf)
        qimage = QtGui.QImage(
            ptr, width, height, buf.strides[0], QtGui.QImage.Format(qfmt))
        if fmt == "a1":  # FIXME directly drawing Format_Mono segfaults?
            qimage = qimage.convertedTo(QtGui.QImage.Format(6))
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

    def print_figure(self, *args, **kwargs):
        # Similar to matplotlib#26309: Qt may trigger a redraw after closing
        # the file save dialog, in which case we don't want to redraw based on
        # the savefig-generated renderer but the earlier, gui one.
        lrc = self._last_renderer_call
        self._last_renderer_call = None, None
        try:
            super().print_figure(*args, **kwargs)
        finally:
            self._last_renderer_call = lrc


@_BackendQT.export
class _BackendQTCairo(_BackendQT):
    FigureCanvas = FigureCanvasQTCairo
