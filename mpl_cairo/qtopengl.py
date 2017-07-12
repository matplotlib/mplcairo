from matplotlib import rcsetup
from matplotlib.backends.backend_qt5 import (
    QtGui, QtWidgets, _BackendQT5, FigureCanvasQT)

from .base import FigureCanvasCairo


rcsetup.interactive_bk += ["module://mpl_cairo.qtopengl"]  # NOTE: Should be fixed in Mpl.


from .base import GraphicsContextRendererCairo
from matplotlib.backend_bases import *
from matplotlib.backends import backend_qt5
from PyQt5.QtCore import *
from PyQt5.QtGui import *
from PyQt5.QtWidgets import *


class GLWindow(QWindow):
    def __init__(self, canvas):
        super().__init__()
        self.figure = canvas.figure
        self._renderer = GraphicsContextRendererCairo(self.figure.dpi)
        self._gl_ctx = None
        self.setSurfaceType(QWindow.OpenGLSurface)

    def draw(self):
        if not self.isExposed():
            return
        self._draw_pending = False
        if self._gl_ctx is None:
            self._gl_ctx = QOpenGLContext(self)
            self._gl_ctx.setFormat(self.requestedFormat())
            self._gl_ctx.create()
        self._gl_ctx.makeCurrent(self)
        print(self.width(), self.height())
        self._renderer.set_ctx_from_current_gl()
        self.figure.draw(self._renderer)
        self._renderer.flush()
        self._gl_ctx.swapBuffers(self)

    def draw_idle(self):
        if not self._draw_pending:
            self._draw_pending = True
            QGuiApplication.postEvent(self, QEvent(QEvent.UpdateRequest))

    def event(self, event):
        if event.type() in [
                QEvent.Expose, QEvent.Resize, QEvent.UpdateRequest]:
            self.draw()
            return True
        return super().event(event)


class FigureManagerQTCairo(FigureManagerBase):
    def __init__(self, canvas, num):
        backend_qt5._create_qApp()
        super().__init__(canvas, num)
        self._window = GLWindow(canvas)
        fmt = QSurfaceFormat()
        fmt.setSamples(4)
        self._window.setFormat(fmt)

    def show(self):
        self._window.show()


@_BackendQT5.export
class _BackendQT5Cairo(_BackendQT5):
    FigureCanvas = FigureCanvasBase
    FigureManager = FigureManagerQTCairo
