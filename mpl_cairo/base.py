from collections import OrderedDict
from contextlib import ExitStack
from functools import partialmethod
import sys

import numpy as np

import matplotlib
from matplotlib import _png, cbook, colors, rcParams
from matplotlib.backend_bases import (
    FigureCanvasBase, FigureManagerBase, GraphicsContextBase, RendererBase)

from . import _mpl_cairo
from ._mpl_cairo import surface_type_t


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

    @classmethod
    def _for_eps_output(cls, file, width, height, dpi):
        args = surface_type_t.PS, file, width, height, dpi
        obj = _mpl_cairo.GraphicsContextRendererCairo.__new__(cls, *args)
        _mpl_cairo.GraphicsContextRendererCairo.__init__(obj, *args)
        obj._set_eps(True)
        return obj

    @classmethod
    def _for_pdf_output(cls, file, width, height, dpi):
        args = surface_type_t.PDF, file, width, height, dpi
        obj = _mpl_cairo.GraphicsContextRendererCairo.__new__(cls, *args)
        _mpl_cairo.GraphicsContextRendererCairo.__init__(obj, *args)
        return obj

    @classmethod
    def _for_ps_output(cls, file, width, height, dpi):
        args = surface_type_t.PS, file, width, height, dpi
        obj = _mpl_cairo.GraphicsContextRendererCairo.__new__(cls, *args)
        _mpl_cairo.GraphicsContextRendererCairo.__init__(obj, *args)
        return obj

    @classmethod
    def _for_svg_output(cls, file, width, height, dpi):
        args = surface_type_t.SVG, file, width, height, dpi
        obj = _mpl_cairo.GraphicsContextRendererCairo.__new__(cls, *args)
        _mpl_cairo.GraphicsContextRendererCairo.__init__(obj, *args)
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

    def _get_cached_or_new_renderer(
            self, func, *args, _draw_if_new=False, **kwargs):
        last_call, last_renderer = self._last_renderer_call
        if (func, args, kwargs) == last_call:
            return last_renderer
        else:
            renderer = func(*args, **kwargs)
            self._last_renderer_call = (func, args, kwargs), renderer
            if _draw_if_new:
                self.figure.draw(renderer)
            return renderer

    # NOTE: Not documented, but needed for tight_layout.
    def get_renderer(self, *, _draw_if_new=False):
        return self._get_cached_or_new_renderer(
            GraphicsContextRendererCairo,
            *self.get_width_height(), self.figure.dpi,
            _draw_if_new=_draw_if_new)

    renderer = property(get_renderer)  # Needed when patching FigureCanvasAgg.

    def draw(self):
        self.figure.draw(self.get_renderer())
        super().draw()

    def copy_from_bbox(self, bbox):
        if self.get_renderer() is None:
            self.draw()
        return self.get_renderer().copy_from_bbox(bbox)

    def restore_region(self, region):
        if self.get_renderer() is None:
            self.draw()
        self.get_renderer().restore_region(region)
        self.update()

    def print_png(
            self, filename_or_obj, *,
            dpi=72, metadata=None,
            # These arguments are already taken care of by print_figure.
            facecolor=None, edgecolor=None, orientation="portrait",
            dryrun=False, bbox_inches_restore=None):
        self.draw()
        data = _to_rgba(self.get_renderer()._get_buffer())
        full_metadata = OrderedDict(
            [("Software",
              "matplotlib version {}, https://matplotlib.org"
              .format(matplotlib.__version__))])
        full_metadata.update(metadata or {})
        file, needs_close = cbook.to_filehandle(
            filename_or_obj, "wb", return_opened=True)
        with ExitStack() as stack:
            if needs_close:
                stack.enter_context(file)
            _png.write_png(data, file, metadata=full_metadata)

    # FIXME: Native mathtext support (otherwise math looks awful).

    def _print_method(
            self, renderer_factory,
            filename_or_obj, *, dpi=72,
            # These arguments are already taken care of by print_figure.
            facecolor=None, edgecolor=None, orientation="portrait",
            dryrun=False, bbox_inches_restore=None):
        # NOTE: we do not write the metadata (this is only possible for some
        # cairo backends).
        self.figure.set_dpi(72)
        file, needs_close = cbook.to_filehandle(
            filename_or_obj, "wb", return_opened=True)
        with ExitStack() as stack:
            if needs_close:
                stack.enter_context(file)
            renderer = renderer_factory(file, *self.get_width_height(), dpi)
            self.figure.draw(renderer)
            renderer._finish()

    print_eps = partialmethod(
        _print_method, GraphicsContextRendererCairo._for_eps_output)
    print_pdf = partialmethod(
        _print_method, GraphicsContextRendererCairo._for_pdf_output)
    print_ps = partialmethod(
        _print_method, GraphicsContextRendererCairo._for_ps_output)
    print_svg = partialmethod(
        _print_method, GraphicsContextRendererCairo._for_svg_output)


try:  # NOTE: try... except until #8773 gets in.
    from matplotlib.backend_bases import _Backend
except ImportError:
    pass
else:
    @_Backend.export
    class _BackendCairo(_Backend):
        FigureCanvas = FigureCanvasCairo
        FigureManager = FigureManagerBase
