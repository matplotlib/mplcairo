from contextlib import ExitStack
import sys

import numpy as np

from matplotlib import _png, cbook, colors, rcParams
from matplotlib.backend_bases import (
    FigureCanvasBase, FigureManagerBase, GraphicsContextBase, RendererBase)

from . import _mpl_cairo, format_t


def _to_rgba(data):
    # Native ARGB32 to (R, G, B, A).
    data = data[
        ..., [2, 1, 0, 3] if sys.byteorder == "little" else [1, 2, 3, 0]]
    # Un-premultiply alpha.  The formula is the same as in cairo-png.c.
    rgb = data[..., :-1]
    alpha = data[..., -1]
    mask = alpha != 0
    for channel in np.rollaxis(rgb, -1):
        channel[mask] = (
            (channel[mask].astype(int) * 255 + alpha[mask] // 2)
            // alpha[mask])
    return data


def _get_drawn_subarray_and_bounds(data):
    drawn = data[..., 3] != 0
    l, r = drawn.any(axis=0).nonzero()[0][[0, -1]]
    b, t = drawn.any(axis=1).nonzero()[0][[0, -1]]
    return data[b:t+1, l:r+1], (l, b, r - l + 1, t - b + 1)


_mpl_cairo._Region.to_string_argb = (
    lambda self:
    _to_rgba(self._get_buffer())[
        ..., [2, 1, 0, 3] if sys.byteorder == "little" else [3, 0, 1, 2]]
    .tobytes())


class GraphicsContextRendererCairo(
        _mpl_cairo.GraphicsContextRendererCairo,
        # Fill in the missing methods.
        GraphicsContextBase,
        RendererBase):

    def __init__(self, width, height, dpi):
        # Hide the overloaded constructor, provided by from_pycairo_ctx.
        super().__init__(width, height, dpi)

    @classmethod
    def from_pycairo_ctx(cls, ctx, dpi):
        obj = _mpl_cairo.GraphicsContextRendererCairo.__new__(cls, ctx, dpi)
        _mpl_cairo.GraphicsContextRendererCairo.__init__(obj, ctx, dpi)
        return obj

    def option_image_nocomposite(self):
        return True  # Similarly to Agg.

    def stop_filter(self, filter_func):
        buf = _to_rgba(self._stop_filter())
        img, (l, b, w, h) = _get_drawn_subarray_and_bounds(buf)
        img, dx, dy = filter_func(img[::-1] / 255, self.dpi)
        if img.dtype.kind == "f":
            img = np.asarray(img * 255, np.uint8)
        width, height = self.get_canvas_width_height()
        self.draw_image(self, l + dx, height - b - h + dy, img)

    def tostring_rgba_minimized(self):  # NOTE: Needed by MixedModeRenderer.
        data, bounds = _get_drawn_subarray_and_bounds(
            _to_rgba(self._get_buffer()))
        return data.tobytes(), bounds


class FigureCanvasCairo(FigureCanvasBase):
    def __init__(self, figure):
        super().__init__(figure=figure)
        self._last_renderer_call = None, None

    def _get_cached_or_new_renderer(self, func, *args, **kwargs):
        last_call, last_renderer = self._last_renderer_call
        if (func, args, kwargs) == last_call:
            return last_renderer
        else:
            renderer = func(*args, **kwargs)
            self._last_renderer_call = (func, args, kwargs), renderer
            return renderer

    # NOTE: Not documented, but needed for tight_layout.
    def get_renderer(self):
        return self._get_cached_or_new_renderer(
            GraphicsContextRendererCairo,
            *self.get_width_height(), self.figure.dpi)

    renderer = property(get_renderer)  # Needed when patching FigureCanvasAgg.

    def draw(self):
        self.figure.draw(self.get_renderer())
        super().draw()

    def copy_from_bbox(self, bbox):
        if self._renderer is None:
            self.draw()
        return self._renderer.copy_from_bbox(bbox)

    def restore_region(self, region):
        if self._renderer is None:
            self.draw()
        self._renderer.restore_region(region)
        self.update()

    def print_png(self, filename_or_obj, **kwargs):
        self.draw()
        data = _to_rgba(self._renderer._get_buffer())
        file, needs_close = cbook.to_filehandle(
            filename_or_obj, "wb", return_opened=True)
        with ExitStack() as stack:
            if needs_close:
                stack.enter_context(file)
            _png.write_png(data, file)


try:  # NOTE: try... except until #8773 gets in.
    from matplotlib.backend_bases import _Backend
except ImportError:
    pass
else:
    @_Backend.export
    class _BackendCairo(_Backend):
        FigureCanvas = FigureCanvasCairo
        FigureManager = FigureManagerBase
