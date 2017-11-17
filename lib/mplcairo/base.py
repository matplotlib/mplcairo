from collections import OrderedDict
from contextlib import ExitStack
from functools import partialmethod
from gzip import GzipFile
import os
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

from . import _mplcairo, _util
from ._mplcairo import _StreamSurfaceType


MathTextParser._backend_mapping["cairo"] = _mplcairo.MathtextBackendCairo


# FreeType2 is thread-unsafe (as we rely on Matplotlib's unique FT_Library).
# Moreover, because mathtext methods globally set the dpi (see _mplcairo.cpp
# for rationale), _mplcairo is also thread-unsafe.  Additionally, features such
# as start/stop_filter() are fundamentally also single-threaded.  Thus, we do
# not attempt to use fine-grained locks.
_LOCK = RLock()


def _get_drawn_subarray_and_bounds(img):
    """Return the drawn region of a buffer and its ``(l, b, w, h)`` bounds.
    """
    drawn = img[..., 3] != 0
    l, r = drawn.any(axis=0).nonzero()[0][[0, -1]]
    b, t = drawn.any(axis=1).nonzero()[0][[0, -1]]
    return img[b:t+1, l:r+1], (l, b, r - l + 1, t - b + 1)


_mplcairo._Region.to_string_argb = (
    # For spoofing BackendAgg.BufferRegion.
    lambda self:
    _util.to_unmultiplied_rgba8888(self._get_buffer())[
        ..., [2, 1, 0, 3] if sys.byteorder == "little" else [3, 0, 1, 2]]
    .tobytes())


class GraphicsContextRendererCairo(
        _mplcairo.GraphicsContextRendererCairo,
        # Fill in the missing methods.
        GraphicsContextBase,
        RendererBase):

    def __init__(self, width, height, dpi):
        # Hide the overloaded constructor, provided by from_pycairo_ctx.
        super().__init__(width, height, dpi)

    @classmethod
    def from_pycairo_ctx(cls, ctx, dpi):
        obj = _mplcairo.GraphicsContextRendererCairo.__new__(cls, ctx, dpi)
        _mplcairo.GraphicsContextRendererCairo.__init__(obj, ctx, dpi)
        return obj

    @classmethod
    def _for_fmt_output(cls, fmt, stream, width, height, dpi):
        args = fmt, stream, width, height, dpi
        obj = _mplcairo.GraphicsContextRendererCairo.__new__(cls, *args)
        _mplcairo.GraphicsContextRendererCairo.__init__(obj, *args)
        return obj

    _for_pdf_output = partialmethod(_for_fmt_output, _StreamSurfaceType.PDF)
    _for_ps_output = partialmethod(_for_fmt_output, _StreamSurfaceType.PS)
    _for_eps_output = partialmethod(_for_fmt_output, _StreamSurfaceType.EPS)
    _for_svg_output = partialmethod(_for_fmt_output, _StreamSurfaceType.SVG)
    _for_script_output = partialmethod(
        _for_fmt_output, _StreamSurfaceType.Script)

    @classmethod
    def _for_svgz_output(cls, stream, width, height, dpi):
        gzip_file = GzipFile(fileobj=stream)
        obj = cls._for_svg_output(gzip_file, width, height, dpi)

        def _finish():
            cls._finish(obj)
            gzip_file.close()

        obj._finish = _finish
        return obj

    def option_image_nocomposite(self):
        return True  # Similarly to Agg.

    def stop_filter(self, filter_func):
        img = _util.to_unmultiplied_rgba8888(self._stop_filter())
        img, (l, b, w, h) = _get_drawn_subarray_and_bounds(img)
        img, dx, dy = filter_func(img[::-1] / 255, self.dpi)
        if img.dtype.kind == "f":
            img = np.asarray(img * 255, np.uint8)
        width, height = self.get_canvas_width_height()
        self.draw_image(self, l + dx, height - b - h + dy, img)

    def tostring_rgba_minimized(self):  # NOTE: Needed by MixedModeRenderer.
        img, bounds = _get_drawn_subarray_and_bounds(
            _util.to_unmultiplied_rgba8888(self._get_buffer()))
        return img.tobytes(), bounds

    # Needed when patching FigureCanvasAgg (for tkagg).
    _renderer = property(lambda self: self._get_buffer())


class FigureCanvasCairo(FigureCanvasBase):
    # Although this attribute should semantically be set from __init__ (it is
    # purely an instance attribute), initializing it at the class level helps
    # when patching FigureCanvasAgg (for gtk3agg) as the latter would fail to
    # initialize it.
    _last_renderer_call = None, None

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

    # NOTE: Not documented, but needed for tight_layout()... and we use it too.
    def get_renderer(self, *, _draw_if_new=False):
        return self._get_cached_or_new_renderer(
            GraphicsContextRendererCairo,
            *(np.asarray(self.get_width_height())
              * getattr(self, "_dpi_ratio", 1)),
            self.figure.dpi, _draw_if_new=_draw_if_new)

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
            path_or_stream, *, dpi=72,
            # These arguments are already taken care of by print_figure().
            facecolor=None, edgecolor=None, orientation="portrait",
            dryrun=False, bbox_inches_restore=None):
        # NOTE: we do not write the metadata (this is only possible for some
        # cairo backends).
        self.figure.set_dpi(72)
        stream, was_path = cbook.to_filehandle(
            path_or_stream, "wb", return_opened=True)
        with ExitStack() as stack:
            if was_path:
                stack.push(stream)
            renderer = renderer_factory(stream, *self.get_width_height(), dpi)
            with _LOCK:
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
    if os.environ.get("MPLCAIRO_DEBUG"):
        print_cairoscript = partialmethod(
            _print_method, GraphicsContextRendererCairo._for_script_output)

    def _get_fresh_unmultiplied_rgba8888(self):
        # Swap out the cache, as savefig may be playing with the background
        # color.
        last_renderer_call = self._last_renderer_call
        self._last_renderer_call = (None, None)
        with _LOCK:
            renderer = self.get_renderer(_draw_if_new=True)
        self._last_renderer_call = last_renderer_call
        return _util.to_unmultiplied_rgba8888(renderer._get_buffer())

    # Split out as a separate method for metadata support.
    def print_png(
            self, path_or_stream, *, metadata=None,
            # These arguments are already taken care of by print_figure().
            dpi=72, facecolor=None, edgecolor=None, orientation="portrait",
            dryrun=False, bbox_inches_restore=None):
        img = self._get_fresh_unmultiplied_rgba8888()
        if dryrun:
            return
        full_metadata = OrderedDict(
            [("Software",
              "matplotlib version {}, https://matplotlib.org"
              .format(matplotlib.__version__))])
        full_metadata.update(metadata or {})
        stream, was_path = cbook.to_filehandle(
            path_or_stream, "wb", return_opened=True)
        with ExitStack() as stack:
            if was_path:
                stack.push(stream)
            _png.write_png(img, stream, metadata=full_metadata)

    if Image:

        def print_jpeg(
                self, path_or_stream, *,
                # These arguments are already taken care of by print_figure().
                dpi=72, facecolor=None, edgecolor=None, orientation="portrait",
                dryrun=False, bbox_inches_restore=None,
                # Remaining kwargs are passed to PIL.
                **kwargs):
            img = self._get_fresh_unmultiplied_rgba8888()
            if dryrun:
                return
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
            composited.save(path_or_stream, format="jpeg", **kwargs)

        print_jpg = print_jpeg

        def print_tiff(
                self, path_or_stream, *,
                # These arguments are already taken care of by print_figure().
                dpi=72, facecolor=None, edgecolor=None, orientation="portrait",
                dryrun=False, bbox_inches_restore=None):
            img = self._get_fresh_unmultiplied_rgba8888()
            if dryrun:
                return
            size = self.get_renderer().get_canvas_width_height()
            (Image.frombuffer("RGBA", size, img, "raw", "RGBA", 0, 1)
            .save(path_or_stream, format="tiff",
                dpi=(self.figure.dpi, self.figure.dpi)))

        print_tif = print_tiff


@_Backend.export
class _BackendCairo(_Backend):
    FigureCanvas = FigureCanvasCairo
    FigureManager = FigureManagerBase
