from collections import OrderedDict
from contextlib import ExitStack
from functools import partialmethod
from gzip import GzipFile
import sys
from threading import RLock

import numpy as np
try:
    from PIL import Image
except ImportError:
    Image = None

import matplotlib
from matplotlib import _png, cbook, colors, rcParams
from matplotlib.backend_bases import (
    _Backend, FigureCanvasBase, FigureManagerBase, GraphicsContextBase,
    RendererBase)
from matplotlib.mathtext import MathTextParser

from . import _mpl_cairo
from ._mpl_cairo import surface_type_t


MathTextParser._backend_mapping["cairo"] = _mpl_cairo.MathtextBackendCairo


# FreeType2 is thread-unsafe (as we rely on Matplotlib's unique FT_Library).
# Moreover, because mathtext methods globally set the dpi (see _mpl_cairo.cpp
# for rationale), the _mpl_cairo is also thread-unsafe.  Additionally, features
# such as start/stop_filter() are essentially also single-threaded.  Thus, do
# not attempt to use fine-grained locks.
_LOCK = RLock()


def _to_rgba(buf):
    """Convert a buffer from premultiplied ARGB32 to unmultiplied RGBA8888.
    """
    # Using .take() instead of indexing ensures C-contiguity of the result.
    rgba = buf.take(
        [2, 1, 0, 3] if sys.byteorder == "little" else [1, 2, 3, 0], axis=2)
    # Un-premultiply alpha.  The formula is the same as in cairo-png.c.
    rgb = rgba[..., :-1]
    alpha = rgba[..., -1]
    mask = alpha != 0
    for channel in np.rollaxis(rgb, -1):
        channel[mask] = (
            (channel[mask].astype(int) * 255 + alpha[mask] // 2)
            // alpha[mask])
    return rgba


def _get_drawn_subarray_and_bounds(img):
    """Return the drawn region of a buffer and its ``(l, b, w, h)`` bounds.
    """
    drawn = img[..., 3] != 0
    l, r = drawn.any(axis=0).nonzero()[0][[0, -1]]
    b, t = drawn.any(axis=1).nonzero()[0][[0, -1]]
    return img[b:t+1, l:r+1], (l, b, r - l + 1, t - b + 1)


_mpl_cairo._Region.to_string_argb = (
    # For spoofing BackendAgg.BufferRegion.
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
    def _for_fmt_output(cls, fmt, file, width, height, dpi):
        args = fmt, file, width, height, dpi
        obj = _mpl_cairo.GraphicsContextRendererCairo.__new__(cls, *args)
        _mpl_cairo.GraphicsContextRendererCairo.__init__(obj, *args)
        return obj

    _for_ps_output = partialmethod(_for_fmt_output, surface_type_t.PS)
    _for_pdf_output = partialmethod(_for_fmt_output, surface_type_t.PDF)
    _for_svg_output = partialmethod(_for_fmt_output, surface_type_t.SVG)

    @classmethod
    def _for_eps_output(cls, file, width, height, dpi):
        obj = cls._for_ps_output(file, width, height, dpi)
        obj._set_eps(True)
        return obj

    @classmethod
    def _for_svgz_output(cls, file, width, height, dpi):
        gzip_file = GzipFile(fileobj=file)
        obj = cls._for_svg_output(gzip_file, width, height, dpi)

        def _finish():
            cls._finish(obj)
            gzip_file.close()

        obj._finish = _finish
        return obj

    def option_image_nocomposite(self):
        return True  # Similarly to Agg.

    def stop_filter(self, filter_func):
        img = _to_rgba(self._stop_filter())
        img, (l, b, w, h) = _get_drawn_subarray_and_bounds(img)
        img, dx, dy = filter_func(img[::-1] / 255, self.dpi)
        if img.dtype.kind == "f":
            img = np.asarray(img * 255, np.uint8)
        width, height = self.get_canvas_width_height()
        self.draw_image(self, l + dx, height - b - h + dy, img)

    def tostring_rgba_minimized(self):  # NOTE: Needed by MixedModeRenderer.
        img, bounds = _get_drawn_subarray_and_bounds(
            _to_rgba(self._get_buffer()))
        return img.tobytes(), bounds


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
                with _LOCK:
                    self.figure.draw(renderer)
            return renderer

    # NOTE: Not documented, but needed for tight_layout().
    def get_renderer(self, *, _draw_if_new=False):
        return self._get_cached_or_new_renderer(
            GraphicsContextRendererCairo,
            *self.get_width_height(), self.figure.dpi,
            _draw_if_new=_draw_if_new)

    renderer = property(get_renderer)  # Needed when patching FigureCanvasAgg.

    def draw(self):
        with _LOCK:
            self.figure.draw(self.get_renderer())
        super().draw()

    def copy_from_bbox(self, bbox):
        return self.get_renderer(_draw_if_new=True).copy_from_bbox(bbox)

    def restore_region(self, region):
        with _LOCK:
            self.get_renderer().restore_region(region)
        super().draw()

    def _print_method(
            self, renderer_factory,
            filename_or_obj, *, dpi=72,
            # These arguments are already taken care of by print_figure().
            facecolor=None, edgecolor=None, orientation="portrait",
            dryrun=False, bbox_inches_restore=None):
        # NOTE: we do not write the metadata (this is only possible for some
        # cairo backends).
        self.figure.set_dpi(72)
        file, needs_close = cbook.to_filehandle(
            filename_or_obj, "wb", return_opened=True)
        with ExitStack() as stack:
            if needs_close:
                stack.push(file)
            renderer = renderer_factory(file, *self.get_width_height(), dpi)
            self.figure.draw(renderer)
            # NOTE: _finish() corresponds finalize() in Matplotlib's PDF and
            # SVG backends; it is inlined for Matplotlib's PS backend.
            renderer._finish()

    print_eps = partialmethod(
        _print_method, GraphicsContextRendererCairo._for_eps_output)
    print_pdf = partialmethod(
        _print_method, GraphicsContextRendererCairo._for_pdf_output)
    print_ps = partialmethod(
        _print_method, GraphicsContextRendererCairo._for_ps_output)
    print_svg = partialmethod(
        _print_method, GraphicsContextRendererCairo._for_svg_output)
    print_svgz = partialmethod(
        _print_method, GraphicsContextRendererCairo._for_svgz_output)

    # Split out as a separate method for metadata support.
    def print_png(
            self, filename_or_obj, *, metadata=None,
            # These arguments are already taken care of by print_figure().
            dpi=72, facecolor=None, edgecolor=None, orientation="portrait",
            dryrun=False, bbox_inches_restore=None):
        with _LOCK:
            self.draw()
            if dryrun:
                return
            img = _to_rgba(self.get_renderer()._get_buffer())
        full_metadata = OrderedDict(
            [("Software",
              "matplotlib version {}, https://matplotlib.org"
              .format(matplotlib.__version__))])
        full_metadata.update(metadata or {})
        file, needs_close = cbook.to_filehandle(
            filename_or_obj, "wb", return_opened=True)
        with ExitStack() as stack:
            if needs_close:
                stack.push(file)
            _png.write_png(img, file, metadata=full_metadata)

    if Image:

        def print_jpeg(
                self, filename_or_obj, *,
                # These arguments are already taken care of by print_figure().
                dpi=72, facecolor=None, edgecolor=None, orientation="portrait",
                dryrun=False, bbox_inches_restore=None,
                # Remaining kwargs are passed to PIL.
                **kwargs):
            with _LOCK:
                self.draw()
                if dryrun:
                    return
                img = _to_rgba(self.get_renderer()._get_buffer())
            size = self.get_renderer().get_canvas_width_height()
            img = Image.frombuffer("RGBA", size, img, "raw", "RGBA", 0, 1)
            # Composite against the background (actually we could just skip the
            # conversion to unpremultiplied RGBA earlier).
            # NOTE: Agg composites against rcParams["savefig.facecolor"].
            background = tuple(
                (np.array(colors.to_rgb(facecolor)) * 255).astype(int))
            composited = Image.new("RGB", size, background)
            composited.paste(img, img)
            kwargs.setdefault("quality", rcParams["savefig.jpeg_quality"])
            composited.save(filename_or_obj, format="jpeg", **kwargs)

        print_jpg = print_jpeg

        def print_tiff(
                self, filename_or_obj, *,
                # These arguments are already taken care of by print_figure().
                dpi=72, facecolor=None, edgecolor=None, orientation="portrait",
                dryrun=False, bbox_inches_restore=None):
            with _LOCK:
                self.draw()
                if dryrun:
                    return
                img = _to_rgba(self.get_renderer()._get_buffer())
            size = self.get_renderer().get_canvas_width_height()
            (Image.frombuffer("RGBA", size, img, "raw", "RGBA", 0, 1)
            .save(filename_or_obj, format="tiff",
                dpi=(self.figure.dpi, self.figure.dpi)))

        print_tif = print_tiff


@_Backend.export
class _BackendCairo(_Backend):
    FigureCanvas = FigureCanvasCairo
    FigureManager = FigureManagerBase
