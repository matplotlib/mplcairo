from contextlib import ExitStack
import ctypes
import sys

import numpy as np

from matplotlib import _png, cbook, colors, rcParams
from matplotlib.backend_bases import (
    _Backend, FigureCanvasBase, FigureManagerBase, GraphicsContextBase,
    RendererBase)

from . import _mpl_cairo, format_t


class GraphicsContextRendererCairo(
        _mpl_cairo.GraphicsContextRendererCairo,
        # Fill in the missing methods.
        GraphicsContextBase,
        RendererBase):

    def option_image_nocomposite(self):
        return True  # Similarly to Agg.


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
        buf_address = self._renderer.get_data_address()
        width, height = self._renderer.get_canvas_width_height()
        ptr = ctypes.cast(ctypes.c_void_p(buf_address),
                          ctypes.POINTER(ctypes.c_ubyte))
        # Convert from native ARGB to (R, G, B, A).
        data = np.ctypeslib.as_array(ptr, (height, width, 4))[
            ..., [2, 1, 0, 3] if sys.byteorder == "little" else [1, 2, 3, 0]]
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
