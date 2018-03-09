from collections import OrderedDict
from contextlib import ExitStack
import functools
from functools import partial, partialmethod
from gzip import GzipFile
import logging
import os
from pathlib import Path
import shutil
import sys
from tempfile import TemporaryDirectory
from threading import RLock

import numpy as np
try:
    from PIL import Image
except ImportError:
    Image = None

import matplotlib
from matplotlib import _png, cbook, colors, dviread, rcParams
from matplotlib.backend_bases import (
    _Backend, FigureCanvasBase, FigureManagerBase, GraphicsContextBase,
    RendererBase)
from matplotlib.backends import backend_ps
from matplotlib.font_manager import FontProperties
from matplotlib.mathtext import MathTextParser

from . import _mplcairo, _util
from ._mplcairo import _StreamSurfaceType


_log = logging.getLogger()
# FreeType2 is thread-unsafe (as we rely on Matplotlib's unique FT_Library).
_LOCK = RLock()
MathTextParser._backend_mapping["cairo"] = _mplcairo.MathtextBackendCairo


@functools.lru_cache(1)
def _get_tex_font_map():
    return dviread.PsfontsMap(dviread.find_tex_file("pdftex.map"))


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
        _mplcairo.GraphicsContextRendererCairo.__init__(
            self, width, height, dpi)

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
        return (not rcParams["image.composite_image"]
                if self._has_vector_surface() else True)

    # Based on the backend_pdf implementation.
    def draw_tex(self, gc, x, y, s, prop, angle, ismath="TeX!", mtext=None):
        texmanager = self.get_texmanager()
        fontsize = prop.get_size_in_points()
        dvifile = texmanager.make_dvi(s, fontsize)
        with dviread.Dvi(dvifile, self.dpi) as dvi:
            page = next(iter(dvi))
        mb = _mplcairo.MathtextBackendCairo()
        for x1, y1, dvifont, glyph, width in page.text:
            mb._render_usetex_glyph(
                x1, -y1,
                _get_tex_font_map()[dvifont.texname].filename, dvifont.size,
                glyph)
        for x1, y1, h, w in page.boxes:
            mb.render_rect_filled(x1, -y1, x1 + w, -(y1 + h))
        mb._draw(self, x, y, angle)

    def stop_filter(self, filter_func):
        img = _util.to_unmultiplied_rgba8888(self._stop_filter_get_buffer())
        img, (l, b, w, h) = _get_drawn_subarray_and_bounds(img)
        img, dx, dy = filter_func(img[::-1] / 255, self.dpi)
        if img.dtype.kind == "f":
            img = np.asarray(img * 255, np.uint8)
        width, height = self.get_canvas_width_height()
        self.draw_image(self, l + dx, height - b - h + dy, img)

    start_rasterizing = _mplcairo.GraphicsContextRendererCairo.start_filter

    # While we could just write
    #   stop_rasterizing = partialmethod(stop_filter,
    #                                    lambda img, dpi: (img, 0, 0))
    # this crashes inspect.signature on Py3.6
    # (https://bugs.python.org/issue33009).
    def stop_rasterizing(self):
        return self.stop_filter(lambda img, dpi: (img, 0, 0))

    # "Undocumented" APIs needed to patch Agg.

    _renderer = property(lambda self: self._get_buffer())  # Needed for tkagg.
    lock = _LOCK  # Needed for webagg_core; fixed by matplotlib#10708.

    def buffer_rgba(self):  # Needed for webagg_core.
        return _util.to_unmultiplied_rgba8888(self._get_buffer())

    def tostring_rgba_minimized(self):  # Needed for MixedModeRenderer.
        img, bounds = _get_drawn_subarray_and_bounds(
            _util.to_unmultiplied_rgba8888(self._get_buffer()))
        return img.tobytes(), bounds


@functools.lru_cache(1)
def _fix_ipython_backend2gui():
    # Fix hard-coded module -> toolkit mapping in IPython (used for `ipython
    # --auto`).  This cannot be done at import time due to ordering issues (so
    # we do it when creating a canvas) and should only be done once (hence the
    # `lru_cache(1)`).
    if "IPython" not in sys.modules:
        return
    import IPython
    ip = IPython.get_ipython()
    if not ip:
        return
    from IPython.core import pylabtools as pt
    pt.backend2gui.update({
        "module://mplcairo.gtk": "gtk3",
        "module://mplcairo.qt": "qt",
        "module://mplcairo.tk": "tk",
        "module://mplcairo.wx": "wx",
        "module://mplcairo.macosx": "osx",
    })
    ip.enable_matplotlib()


class FigureCanvasCairo(FigureCanvasBase):
    # Although this attribute should semantically be set from __init__ (it is
    # purely an instance attribute), initializing it at the class level helps
    # when patching FigureCanvasAgg (for gtk3agg) as the latter would fail to
    # initialize it.
    _last_renderer_call = None, None

    def __init__(self, *args, **kwargs):
        _fix_ipython_backend2gui()
        super().__init__(*args, **kwargs)

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

    # NOTE: Needed for tight_layout() (and we use it too).
    def get_renderer(self, *, _draw_if_new=False):
        return self._get_cached_or_new_renderer(
            GraphicsContextRendererCairo,
            *(np.array([self.figure.bbox.width, self.figure.bbox.height])
              * getattr(self, "_dpi_ratio", 1)),
            # Py3.4 support: use kwarg for dpi.
            dpi=self.figure.dpi, _draw_if_new=_draw_if_new)

    renderer = property(get_renderer)  # NOTE: Needed for FigureCanvasAgg.

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
            path_or_stream, *, metadata=None, dpi=72,
            # These arguments are already taken care of by print_figure().
            facecolor=None, edgecolor=None, orientation="portrait",
            dryrun=False, bbox_inches_restore=None):
        self.figure.set_dpi(72)
        stream, was_path = cbook.to_filehandle(
            path_or_stream, "wb", return_opened=True)
        with ExitStack() as stack:
            if was_path:
                stack.push(stream)
            renderer = renderer_factory(
                stream, self.figure.bbox.width, self.figure.bbox.height, dpi)
            renderer._set_metadata(metadata)
            with _LOCK:
                self.figure.draw(renderer)
            # _finish() corresponds finalize() in Matplotlib's PDF and SVG
            # backends; it is inlined in Matplotlib's PS backend.
            renderer._finish()

    print_pdf = partialmethod(
        _print_method, GraphicsContextRendererCairo._for_pdf_output)
    print_svg = partialmethod(
        _print_method, GraphicsContextRendererCairo._for_svg_output)
    print_svgz = partialmethod(
        _print_method, GraphicsContextRendererCairo._for_svgz_output)
    if os.environ.get("MPLCAIRO_DEBUG"):
        print_cairoscript = partialmethod(
            _print_method, GraphicsContextRendererCairo._for_script_output)

    def _print_ps_impl(self, is_eps, path_or_stream,
                       orientation="portrait", papertype=None, **kwargs):
        if papertype is None:
            papertype = rcParams["ps.papersize"]
        if orientation == "portrait":
            if papertype == "auto":
                width, height = self.figure.get_size_inches()
                papertype = backend_ps._get_papertype(height, width)
        elif orientation == "landscape":
            if papertype == "auto":
                width, height = self.figure.get_size_inches()
                papertype = backend_ps._get_papertype(width, height)
        else:
            raise ValueError("Invalid orientation ({!r})".format(orientation))
        dsc_comments = kwargs.setdefault("metadata", {})["_dsc_comments"] = [
            "%%Orientation: {}".format(orientation)]
        if not is_eps:
            dsc_comments.append("%%DocumentPaperSizes: {}".format(papertype))
        print_method = partial(self._print_method,
                               GraphicsContextRendererCairo._for_eps_output
                               if is_eps else
                               GraphicsContextRendererCairo._for_ps_output)
        if rcParams["ps.usedistiller"]:
            with TemporaryDirectory() as tmp_dirname:
                tmp_name = str(Path(tmp_dirname, "tmp"))
                print_method(tmp_name, **kwargs)
                # Assume we can get away without passing the bbox.
                {"ghostscript": backend_ps.gs_distill,
                 "xpdf": backend_ps.xpdf_distill}[
                     rcParams["ps.usedistiller"]](
                         tmp_name, False, ptype=papertype)
                stream, was_path = cbook.to_filehandle(
                    path_or_stream, "wb", return_opened=True)
                with open(tmp_name, "rb") as tmp_file, ExitStack() as stack:
                    if was_path:
                        stack.push(stream)
                    shutil.copyfileobj(tmp_file, stream)
        else:
            print_method(path_or_stream, **kwargs)

    print_ps = partialmethod(_print_ps_impl, False)
    print_eps = partialmethod(_print_ps_impl, True)

    def _get_fresh_unmultiplied_rgba8888(self):
        # Swap out the cache, as savefig may be playing with the background
        # color.
        last_renderer_call = self._last_renderer_call
        self._last_renderer_call = (None, None)
        with _LOCK:
            renderer = self.get_renderer(_draw_if_new=True)
        self._last_renderer_call = last_renderer_call
        return _util.to_unmultiplied_rgba8888(renderer._get_buffer())

    def print_rgba(
            self, path_or_stream, *, metadata=None,
            # These arguments are already taken care of by print_figure().
            dpi=72, facecolor=None, edgecolor=None, orientation="portrait",
            dryrun=False, bbox_inches_restore=None):
        img = self._get_fresh_unmultiplied_rgba8888()
        if dryrun:
            return
        stream, was_path = cbook.to_filehandle(
            path_or_stream, "wb", return_opened=True)
        with ExitStack() as stack:
            if was_path:
                stack.push(stream)
            stream.write(img.tobytes())

    print_raw = print_rgba

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
            buf = self._get_fresh_unmultiplied_rgba8888()
            if dryrun:
                return
            img = Image.frombuffer(
                "RGBA", buf.shape[:2][::-1], buf, "raw", "RGBA", 0, 1)
            # Composite against the background (actually we could just skip the
            # conversion to unpremultiplied RGBA earlier).
            # NOTE: Agg composites against rcParams["savefig.facecolor"].
            background = tuple(
                (np.array(colors.to_rgb(facecolor)) * 255).astype(int))
            composited = Image.new("RGB", buf.shape[:2][::-1], background)
            composited.paste(img, img)
            kwargs.setdefault("quality", rcParams["savefig.jpeg_quality"])
            composited.save(path_or_stream, format="jpeg",
                            dpi=(self.figure.dpi, self.figure.dpi), **kwargs)

        print_jpg = print_jpeg

        def print_tiff(
                self, path_or_stream, *,
                # These arguments are already taken care of by print_figure().
                dpi=72, facecolor=None, edgecolor=None, orientation="portrait",
                dryrun=False, bbox_inches_restore=None):
            buf = self._get_fresh_unmultiplied_rgba8888()
            if dryrun:
                return
            (Image.frombuffer(
                "RGBA", buf.shape[:2][::-1], buf, "raw", "RGBA", 0, 1)
             .save(path_or_stream, format="tiff",
                   dpi=(self.figure.dpi, self.figure.dpi)))

        print_tif = print_tiff


@_Backend.export
class _BackendCairo(_Backend):
    FigureCanvas = FigureCanvasCairo
    FigureManager = FigureManagerBase
