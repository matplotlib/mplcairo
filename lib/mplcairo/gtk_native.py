from .import _util
from .base import FigureCanvasCairo, GraphicsContextRendererCairo


_mpl_gtk, _backend_obj = _util.get_matplotlib_gtk_backend()


class FigureCanvasGTKCairo(FigureCanvasCairo, _mpl_gtk.FigureCanvas):
    supports_blit = False

    def _renderer_init(self):  # matplotlib#17461 (<3.3).
        pass

    def on_draw_event(self, widget, ctx):
        allocation = self.get_allocation()
        _mpl_gtk.Gtk.render_background(
            self.get_style_context(), ctx,
            allocation.x, allocation.y, allocation.width, allocation.height)
        figure = self.figure
        # The context surface size may not match the figure size (it can
        # include e.g. toolbars) or may not even be known (on GTK4, which uses
        # a recording surface).
        renderer = self._get_cached_or_new_renderer(
            GraphicsContextRendererCairo.from_pycairo_ctx,
            ctx, figure.bbox.width, figure.bbox.height, figure.dpi)
        figure.draw(renderer)


@_backend_obj.export
class _BackendGTKCairo(_backend_obj):
    FigureCanvas = FigureCanvasGTKCairo
