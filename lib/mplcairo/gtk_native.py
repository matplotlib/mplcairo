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
        # Gtk3 uses an already scaled ImageSurface, which needs to be unscaled;
        # Gtk4 uses an unscaled RecordingSurface, which needs to be scaled.
        scale = {3: 1, 4: 1 / self.device_pixel_ratio}[
            _mpl_gtk.Gtk.get_major_version()]
        surface = ctx.get_target()
        prev_scale = surface.get_device_scale()
        surface.set_device_scale(scale, scale)
        figure = self.figure
        # The context surface size may not match the figure size (it can
        # include e.g. toolbars) or may not even be known (on Gtk4, which uses
        # a RecordingSurface).
        renderer = GraphicsContextRendererCairo.from_pycairo_ctx(
            ctx, figure.bbox.width, figure.bbox.height, figure.dpi, prev_scale)
        figure.draw(renderer)
        surface.set_device_scale(*prev_scale)


@_backend_obj.export
class _BackendGTKCairo(_backend_obj):
    FigureCanvas = FigureCanvasGTKCairo
