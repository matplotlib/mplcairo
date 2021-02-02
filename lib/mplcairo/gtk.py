from matplotlib.backends.backend_gtk3 import (
    Gtk, _BackendGTK3, FigureCanvasGTK3)

from .base import FigureCanvasCairo


class FigureCanvasGTKCairo(FigureCanvasCairo, FigureCanvasGTK3):
    def _renderer_init(self):  # matplotlib#17461 (<3.3).
        pass

    def on_draw_event(self, widget, ctx):
        # We always repaint the full canvas (doing otherwise would require an
        # additional copy of the buffer into a contiguous block, so it's not
        # clear it would be faster).
        allocation = self.get_allocation()
        Gtk.render_background(
            self.get_style_context(), ctx,
            allocation.x, allocation.y, allocation.width, allocation.height)
        surface = \
            self.get_renderer(_ensure_drawn=True)._get_context().get_target()
        surface.flush()
        ctx.set_source_surface(surface, 0, 0)
        ctx.paint()

    def blit(self, bbox=None):  # FIXME: flickering.
        super().blit(bbox=bbox)
        self.queue_draw()


@_BackendGTK3.export
class _BackendGTKCairo(_BackendGTK3):
    FigureCanvas = FigureCanvasGTKCairo
