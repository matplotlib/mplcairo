from matplotlib import rcsetup
from matplotlib.backends.backend_qt5 import (
    QtGui, QtWidgets, _BackendQT5, FigureCanvasQT)

from .base import FigureCanvasCairo


rcsetup.interactive_bk += ["module://mpl_cairo.qtopengl"]  # NOTE: Should be fixed in Mpl.


class FigureCanvasQTOpenGLCairo(
        FigureCanvasCairo, QtWidgets.QOpenGLWidget, FigureCanvasQT):

    def initializeGL(self):
        print("igl")

    def paintGL(self):
        print("pgl")
        renderer = self.get_renderer()
        renderer.set_ctx_from_current_gl()
        self.figure.draw(renderer)
        self._renderer.flush()
        # self.context().swapBuffers(self.context().surface())

    # Currently, FigureCanvasQT.resizeEvent manually forwards the event to
    # FigureCanvasBase and QWidget, which mean that QOpenGLWidget.resizeGL
    # doen't get triggered.  For now, just completely reimplement it here.

    def resizeEvent(self, event):
        FigureCanvasQT.resizeEvent(self, event)
        QtWidgets.QOpenGLWidget.resizeEvent(self, event)  # Forward to resizeGL.

    def resizeGL(self, w, h):
        print("rgl", w, h)
        renderer = self.get_renderer()
        renderer.set_ctx_from_current_gl()
        self.figure.draw(renderer)
        self._renderer.flush()
        # self.context().swapBuffers(self.context().surface())


@_BackendQT5.export
class _BackendQT5Cairo(_BackendQT5):
    FigureCanvas = FigureCanvasQTOpenGLCairo
