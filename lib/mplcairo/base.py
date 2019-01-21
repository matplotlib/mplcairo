from collections import OrderedDict
import functools
from functools import partial, partialmethod
from gzip import GzipFile
import logging
import os
from pathlib import Path
import shutil
from tempfile import TemporaryDirectory
from threading import RLock

import numpy as np
try:
    from PIL import Image
except ImportError:
    Image = None

import matplotlib as mpl
from matplotlib import _png, cbook, colors, dviread
from matplotlib.backend_bases import (
    _Backend, FigureCanvasBase, FigureManagerBase, GraphicsContextBase,
    RendererBase)
from matplotlib.backends import backend_ps
from matplotlib.mathtext import MathTextParser

from . import _mplcairo, _util
from ._backports import get_glyph_name
from ._mplcairo import _StreamSurfaceType


_log = logging.getLogger()
# FreeType2 is thread-unsafe.
_LOCK = RLock()
MathTextParser._backend_mapping["mplcairo"] = \
    _mplcairo.MathtextBackendCairo


@functools.lru_cache(1)
def _get_tex_font_map():
    return dviread.PsfontsMap(dviread.find_tex_file("pdftex.map"))


def _get_drawn_subarray_and_bounds(img):
    """Return the drawn region of a buffer and its ``(l, b, w, h)`` bounds."""
    drawn = img[..., 3] != 0
    x_nz, = drawn.any(axis=0).nonzero()
    y_nz, = drawn.any(axis=1).nonzero()
    if len(x_nz) and len(y_nz):
        l, r = drawn.any(axis=0).nonzero()[0][[0, -1]]
        b, t = drawn.any(axis=1).nonzero()[0][[0, -1]]
        return img[b:t+1, l:r+1], (l, b, r - l + 1, t - b + 1)
    else:
        return np.zeros((0, 0, 4), dtype=np.uint8), (0, 0, 0, 0)


def get_raw_buffer(canvas):
    """
    Get the canvas' raw internal buffer.

    This is normally a uint8 buffer of shape ``(m, n, 4)`` in
    ARGB32 order, unless the canvas was created after calling
    ``set_options(float_surfaces=True)`` in which case this is
    a float32 buffer of shape ``(m, n, 4)`` in RGBA128F order.
    """
    return canvas._get_buffer()


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
        return (not mpl.rcParams["image.composite_image"]
                if self._has_vector_surface() else True)

    # Based on the backend_pdf implementation.
    def draw_tex(self, gc, x, y, s, prop, angle, ismath="TeX!", mtext=None):
        fontsize = prop.get_size_in_points()
        dvifile = self.get_texmanager().make_dvi(s, fontsize)
        with dviread.Dvi(dvifile, self.dpi) as dvi:
            page = next(iter(dvi))
        mb = _mplcairo.MathtextBackendCairo()
        for text in page.text:
            texfont = _get_tex_font_map()[text.font.texname]
            if texfont.filename is None:
                # Not TypeError:
                # :mpltest:`test_backend_svg.test_missing_psfont`.
                raise ValueError("No font file found for {} ({!a})"
                                 .format(texfont.psname, texfont.texname))
            mb._render_usetex_glyph(text.x, -text.y,
                                    texfont.filename, text.font.size,
                                    get_glyph_name(text) or text.glyph)
        for x1, y1, h, w in page.boxes:
            mb.render_rect_filled(x1, -y1, x1 + w, -(y1 + h))
        mb._draw(self, x, y, angle)

    def stop_filter(self, filter_func):
        img = _util.cairo_to_straight_rgba8888(self._stop_filter_get_buffer())
        img, (l, b, w, h) = _get_drawn_subarray_and_bounds(img)
        if not (w and h):
            return
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

    lock = _LOCK  # Needed for webagg_core; fixed by matplotlib#10708.

    def buffer_rgba(self):  # Needed for webagg_core.
        return _util.cairo_to_straight_rgba8888(self._get_buffer())

    _renderer = property(buffer_rgba)  # Needed for tkagg.

    def tostring_rgba_minimized(self):  # Needed for MixedModeRenderer.
        img, bounds = _get_drawn_subarray_and_bounds(
            _util.cairo_to_straight_rgba8888(self._get_buffer()))
        return img.tobytes(), bounds


class FigureCanvasCairo(FigureCanvasBase):
    # Although this attribute should semantically be set from __init__ (it is
    # purely an instance attribute), initializing it at the class level helps
    # when patching FigureCanvasAgg (for gtk3agg) as the latter would fail to
    # initialize it.
    _last_renderer_call = None, None

    def __init__(self, *args, **kwargs):
        _util.fix_ipython_backend2gui()
        super().__init__(*args, **kwargs)

    def _get_cached_or_new_renderer(
            self, func, *args,
            _ensure_cleared=False, _ensure_drawn=False,
            **kwargs):
        last_call, last_renderer = self._last_renderer_call
        if (func, args, kwargs) == last_call:
            if _ensure_cleared:
                # This API is present (rather than just throwing away the
                # renderer and creating a new one) so to avoid invalidating the
                # Text._get_layout cache.
                last_renderer.clear()
                if _ensure_drawn:
                    with _LOCK:
                        self.figure.draw(last_renderer)
            return last_renderer
        else:
            renderer = func(*args, **kwargs)
            self._last_renderer_call = (func, args, kwargs), renderer
            if _ensure_drawn:
                with _LOCK:
                    self.figure.draw(renderer)
            return renderer

    # NOTE: Needed for tight_layout() (and we use it too).
    def get_renderer(self, *, _ensure_cleared=False, _ensure_drawn=False):
        return self._get_cached_or_new_renderer(
            GraphicsContextRendererCairo,
            self.figure.bbox.width, self.figure.bbox.height, self.figure.dpi,
            _ensure_cleared=_ensure_cleared, _ensure_drawn=_ensure_drawn)

    renderer = property(get_renderer)  # NOTE: Needed for FigureCanvasAgg.

    def draw(self):
        self.get_renderer(_ensure_cleared=True, _ensure_drawn=True)
        super().draw()

    def copy_from_bbox(self, bbox):
        return self.get_renderer(_ensure_drawn=True).copy_from_bbox(bbox)

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
        with cbook.open_file_cm(path_or_stream, "wb") as stream:
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
    if os.environ.get("MPLCAIRO_SCRIPT_SURFACE") in ["raster", "vector"]:
        print_cairoscript = partialmethod(
            _print_method, GraphicsContextRendererCairo._for_script_output)

    def _print_ps_impl(self, is_eps, path_or_stream,
                       orientation="portrait", papertype=None, **kwargs):
        if papertype is None:
            papertype = mpl.rcParams["ps.papersize"]
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
        if mpl.rcParams["ps.usedistiller"]:
            with TemporaryDirectory() as tmp_dirname:
                tmp_name = str(Path(tmp_dirname, "tmp"))  # Py3.5 compat.
                print_method(tmp_name, **kwargs)
                # Assume we can get away without passing the bbox.
                {"ghostscript": backend_ps.gs_distill,
                 "xpdf": backend_ps.xpdf_distill}[
                     mpl.rcParams["ps.usedistiller"]](
                         tmp_name, False, ptype=papertype)
                with open(tmp_name, "rb") as tmp_file, \
                     cbook.open_file_cm(path_or_stream, "wb") as stream:
                    shutil.copyfileobj(tmp_file, stream)
        else:
            print_method(path_or_stream, **kwargs)

    print_ps = partialmethod(_print_ps_impl, False)
    print_eps = partialmethod(_print_ps_impl, True)

    def _get_fresh_straight_rgba8888(self):
        # Swap out the cache, as savefig may be playing with the background
        # color.
        last_renderer_call = self._last_renderer_call
        self._last_renderer_call = (None, None)
        with _LOCK:
            renderer = self.get_renderer(_ensure_drawn=True)
        self._last_renderer_call = last_renderer_call
        return _util.cairo_to_straight_rgba8888(renderer._get_buffer())

    def print_rgba(
            self, path_or_stream, *, metadata=None,
            # These arguments are already taken care of by print_figure().
            dpi=72, facecolor=None, edgecolor=None, orientation="portrait",
            dryrun=False, bbox_inches_restore=None):
        img = self._get_fresh_straight_rgba8888()
        if dryrun:
            return
        with cbook.open_file_cm(path_or_stream, "wb") as stream:
            stream.write(img.tobytes())

    print_raw = print_rgba

    def print_png(
            self, path_or_stream, *, metadata=None,
            # These arguments are already taken care of by print_figure().
            dpi=72, facecolor=None, edgecolor=None, orientation="portrait",
            dryrun=False, bbox_inches_restore=None):
        img = self._get_fresh_straight_rgba8888()
        if dryrun:
            return
        full_metadata = OrderedDict(
            [("Software",
              "matplotlib version {}, https://matplotlib.org"
              .format(mpl.__version__))])
        full_metadata.update(metadata or {})
        with cbook.open_file_cm(path_or_stream, "wb") as stream:
            _png.write_png(img, stream, metadata=full_metadata)

    if Image:

        def print_jpeg(
                self, path_or_stream, *,
                # These arguments are already taken care of by print_figure().
                dpi=72, facecolor=None, edgecolor=None, orientation="portrait",
                dryrun=False, bbox_inches_restore=None,
                # Remaining kwargs are passed to PIL.
                **kwargs):
            buf = self._get_fresh_straight_rgba8888()
            if dryrun:
                return
            img = Image.frombuffer(
                "RGBA", buf.shape[:2][::-1], buf, "raw", "RGBA", 0, 1)
            # Composite against the background (actually we could just skip the
            # conversion to straight RGBA earlier).
            # NOTE: Agg composites against rcParams["savefig.facecolor"].
            background = tuple(
                (np.array(colors.to_rgb(facecolor)) * 255).astype(int))
            composited = Image.new("RGB", buf.shape[:2][::-1], background)
            composited.paste(img, img)
            kwargs.setdefault("quality", mpl.rcParams["savefig.jpeg_quality"])
            composited.save(path_or_stream, format="jpeg",
                            dpi=(self.figure.dpi, self.figure.dpi), **kwargs)

        print_jpg = print_jpeg

        def print_tiff(
                self, path_or_stream, *,
                # These arguments are already taken care of by print_figure().
                dpi=72, facecolor=None, edgecolor=None, orientation="portrait",
                dryrun=False, bbox_inches_restore=None):
            buf = self._get_fresh_straight_rgba8888()
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
