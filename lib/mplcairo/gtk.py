from .import _util
from .base import FigureCanvasCairo


_mpl_gtk, _backend_obj = _util.get_matplotlib_gtk_backend()


class FigureCanvasGTKCairo(FigureCanvasCairo, _mpl_gtk.FigureCanvas):
    def _renderer_init(self):  # matplotlib#17461 (<3.3).
        pass

    def on_draw_event(self, widget, ctx):
        # We always repaint the full canvas (doing otherwise would require an
        # additional copy of the buffer into a contiguous block, so it's not
        # clear it would be faster).
        allocation = self.get_allocation()
        _mpl_gtk.Gtk.render_background(
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


@_backend_obj.export
class _BackendGTKCairo(_backend_obj):
    FigureCanvas = FigureCanvasGTKCairo
