from matplotlib.backends.backend_gtk3 import (
    Gtk, _BackendGTK3, FigureCanvasGTK3)

from .base import FigureCanvasCairo, GraphicsContextRendererCairo


class FigureCanvasGTKCairo(FigureCanvasCairo, FigureCanvasGTK3):
    supports_blit = False

    def _renderer_init(self):  # matplotlib#17461 (<3.3).
        pass

    def on_draw_event(self, widget, ctx):
        allocation = self.get_allocation()
        Gtk.render_background(
            self.get_style_context(), ctx,
            allocation.x, allocation.y, allocation.width, allocation.height)
        renderer = self._get_cached_or_new_renderer(
            GraphicsContextRendererCairo.from_pycairo_ctx,
            ctx, self.figure.dpi)
        self.figure.draw(renderer)


@_BackendGTK3.export
class _BackendGTKCairo(_BackendGTK3):
    FigureCanvas = FigureCanvasGTKCairo
