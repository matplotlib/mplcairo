from contextlib import ExitStack
import ctypes
import sys

import numpy as np

from matplotlib import _png, cbook, colors, rcParams
from matplotlib.backend_bases import (
    _Backend, FigureCanvasBase, FigureManagerBase, GraphicsContextBase,
    RendererBase)

from . import _mpl_cairo, format_t


def _get_rgba_data(gcr):
    buf_address = gcr.get_data_address()
    width, height = gcr.get_canvas_width_height()
    ptr = ctypes.cast(ctypes.c_void_p(buf_address),
                      ctypes.POINTER(ctypes.c_ubyte))
    # Convert from native ARGB to (R, G, B, A).
    return np.ctypeslib.as_array(ptr, (height, width, 4))[
        ..., [2, 1, 0, 3] if sys.byteorder == "little" else [1, 2, 3, 0]]


class GraphicsContextRendererCairo(
        _mpl_cairo.GraphicsContextRendererCairo,
        # Fill in the missing methods.
        GraphicsContextBase,
        RendererBase):

    def __init__(self, dpi):
        super().__init__(dpi)
        # So that we can implement start/stop_filter.
        self._dpi = dpi
        self._filter_stack = []

    def option_image_nocomposite(self):
        return True  # Similarly to Agg.

    def start_filter(self):
        # NOTE: It may be better to implement this using
        # cairo_{push,pop}_group, avoiding the loss of whatever state had been
        # set.
        self._filter_stack.append(_get_rgba_data(self)[::-1])
        self.set_ctx_from_image_args(
            format_t.ARGB32, *self.get_canvas_width_height())

    def stop_filter(self, filter_func):
        # NOTE: we don't try to restrict the filter to the drawn region.
        img, dx, dy = filter_func(_get_rgba_data(self)[::-1] / 255, self._dpi)
        if img.dtype.kind == "f":
            img = np.asarray(img * 255, np.uint8)
        self.set_ctx_from_image_args(
            format_t.ARGB32, *self.get_canvas_width_height())
        self.draw_image(self, 0, 0, self._filter_stack.pop())
        self.draw_image(self, dx, dy, img)


class FigureCanvasCairo(FigureCanvasBase):
    def __init__(self, figure):
        super().__init__(figure=figure)
        self._renderer = None
        self._last_renderer_args = None

    # NOTE: Not documented, but needed for tight_layout.
    def get_renderer(self):
        renderer_args = self.figure.dpi, self.get_width_height()
        if renderer_args != self._last_renderer_args:
            self._renderer = GraphicsContextRendererCairo(self.figure.dpi)
            self._renderer.set_ctx_from_image_args(
                format_t.ARGB32, *self.get_width_height())
            self._last_renderer_args = renderer_args
        return self._renderer

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
        data = _get_rgba_data(self._renderer)
        # Un-premultiply alpha.  The formula is the same as in cairo-png.c.
        rgb = data[..., :-1]
        alpha = data[..., -1]
        mask = alpha != 0
        for channel in np.rollaxis(rgb, -1):
            channel[mask] = (
                (channel[mask].astype(int) * 255 + alpha[mask] // 2)
                // alpha[mask])
        file, needs_close = cbook.to_filehandle(
            filename_or_obj, "wb", return_opened=True)
        with ExitStack() as stack:
            if needs_close:
                stack.enter_context(file)
            _png.write_png(data, file)


@_Backend.export
class _BackendCairo(_Backend):
    FigureCanvas = FigureCanvasCairo
    FigureManager = FigureManagerBase
