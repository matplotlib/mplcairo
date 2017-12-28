import cairo
from matplotlib.backends.backend_gtk3 import _BackendGTK3, FigureCanvasGTK3

from .base import FigureCanvasCairo, GraphicsContextRendererCairo


class FigureCanvasGTKCairo(FigureCanvasCairo, FigureCanvasGTK3):
    def _renderer_init(self):
        pass

    def on_draw_event(self, widget, ctx):
        # We always repaint the full canvas (doing otherwise would require an
        # additional copy of the buffer into a contiguous block, so it's not
        # clear it would be faster).
        buf = self.get_renderer(_draw_if_new=True)._get_buffer()
        height, width, _ = buf.shape
        image = cairo.ImageSurface.create_for_data(
            buf, cairo.FORMAT_ARGB32, width, height)
        ctx.set_source_surface(image, 0, 0)
        ctx.paint()


@_BackendGTK3.export
class _BackendGTKCairo(_BackendGTK3):
    FigureCanvas = FigureCanvasGTKCairo
