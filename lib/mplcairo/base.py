import codecs
import contextlib
import functools
from functools import partial, partialmethod
import gc
from gzip import GzipFile
import logging
import os
from pathlib import Path
import shutil
from tempfile import TemporaryDirectory
from threading import RLock

import numpy as np
from PIL import Image
from PIL.PngImagePlugin import PngInfo

import matplotlib as mpl
from matplotlib import cbook, dviread
from matplotlib.backend_bases import (
    _Backend, FigureCanvasBase, FigureManagerBase, GraphicsContextBase,
    RendererBase)
from matplotlib.backends import backend_ps
from matplotlib.mathtext import MathTextParser

from . import _mplcairo, _util
from ._backports import get_glyph_name
from ._mplcairo import _StreamSurfaceType


_log = logging.getLogger()
_LOCK = RLock()  # FreeType2 is thread-unsafe.
MathTextParser._backend_mapping["mplcairo"] = \
    _mplcairo.MathtextBackendCairo


class _BytesWritingWrapper:
    def __init__(self, stream, encoding):
        self._stream = stream
        self._encoding = encoding

    def write(self, data):
        # codecs.decode can directly work with memoryviews.
        return self._stream.write(codecs.decode(data, self._encoding))


@functools.lru_cache(1)
def _get_tex_font_map():
    return dviread.PsfontsMap(dviread.find_tex_file("pdftex.map"))


def _get_drawn_subarray_and_bounds(img):
    """Return the drawn region of a buffer and its ``(l, b, w, h)`` bounds."""
    drawn = img[..., 3] != 0
    x_nz, = drawn.any(axis=0).nonzero()
    y_nz, = drawn.any(axis=1).nonzero()
    if len(x_nz) and len(y_nz):
        l, r = x_nz[[0, -1]]
        b, t = y_nz[[0, -1]]
        return img[b:t+1, l:r+1], (l, b, r - l + 1, t - b + 1)
    else:
        return np.zeros((0, 0, 4), dtype=np.uint8), (0, 0, 0, 0)


class GraphicsContextRendererCairo(
        _mplcairo.GraphicsContextRendererCairo,
        # Fill in the missing methods.
        GraphicsContextBase,
        RendererBase):

    _draw_previously_disabled = False

    def __init__(self, width, height, dpi):
        # Hide the overloaded constructors used by from_pycairo_ctx and
        # _for_fmt_output.
        _mplcairo.GraphicsContextRendererCairo.__init__(
            self, width, height, dpi)
        RendererBase.__init__(self)

    @classmethod
    def from_pycairo_ctx(cls, ctx, dpi):
        obj = _mplcairo.GraphicsContextRendererCairo.__new__(cls, ctx, dpi)
        _mplcairo.GraphicsContextRendererCairo.__init__(obj, ctx, dpi)
        RendererBase.__init__(obj)
        return obj

    @classmethod
    def _for_fmt_output(cls, fmt, stream, width, height, dpi):
        if cbook.file_requires_unicode(stream):
            if fmt in [_StreamSurfaceType.PS, _StreamSurfaceType.EPS]:
                # PS is (typically) ASCII -- Language Reference, section 3.2.
                stream = _BytesWritingWrapper(stream, "ascii")
            elif fmt is _StreamSurfaceType.SVG:
                # cairo outputs SVG with encoding="UTF-8".
                stream = _BytesWritingWrapper(stream, "utf-8")
            # (No default encoding for pdf, which is a binary format.)
        args = fmt, stream, width, height, dpi
        cairo_debug_pdf = os.environ.get("CAIRO_DEBUG_PDF")
        if mpl.rcParams["pdf.compression"]:
            os.environ.setdefault("CAIRO_DEBUG_PDF", "1")
        try:
            obj = _mplcairo.GraphicsContextRendererCairo.__new__(cls, *args)
            _mplcairo.GraphicsContextRendererCairo.__init__(obj, *args)
        finally:
            if cairo_debug_pdf is None:
                os.environ.pop("CAIRO_DEBUG_PDF", None)
            else:
                os.environ["CAIRO_DEBUG_PDF"] = cairo_debug_pdf
        try:
            name = os.fsdecode(stream.name)
        except (AttributeError, TypeError):
            pass  # In particular, stream.name is an int for TemporaryFile.
        else:
            obj._set_path(name)
        RendererBase.__init__(obj)
        return obj

    _for_pdf_output = partialmethod(_for_fmt_output, _StreamSurfaceType.PDF)
    _for_ps_output = partialmethod(_for_fmt_output, _StreamSurfaceType.PS)
    _for_eps_output = partialmethod(_for_fmt_output, _StreamSurfaceType.EPS)
    _for_svg_output = partialmethod(_for_fmt_output, _StreamSurfaceType.SVG)
    _for_script_output = partialmethod(
        _for_fmt_output, _StreamSurfaceType.Script)

    @classmethod
    def _for_svgz_output(cls, stream, width, height, dpi):
        gzip_file = GzipFile(fileobj=stream, mode="w")
        obj = cls._for_svg_output(gzip_file, width, height, dpi)
        try:
            name = os.fsdecode(stream.name)
        except (AttributeError, TypeError):
            pass  # In particular, stream.name is an int for TemporaryFile.
        else:
            obj._set_path(name)

        def _finish():
            cls._finish(obj)
            gzip_file.close()

        obj._finish = _finish
        return obj

    @contextlib.contextmanager
    def _draw_disabled(self):
        # After a disabled draw, mark the renderer as such so that later
        # fetches in _get_cached_or_new_renderer force a redraw (otherwise,
        # we can get a blank canvas after interactively saving a CL figure).
        # But that redraw is not needed if the fetch is happening in a (later)
        # _draw_disabled context, so we clear the flag at entry.
        try:
            self._draw_previously_disabled = False
            with super()._draw_disabled():
                yield
        finally:
            self._draw_previously_disabled = True

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
                raise ValueError(f"No font file found for {texfont.psname} "
                                 f"({texfont.texname!a})")
            mb._render_usetex_glyph(
                text.x, -text.y,
                texfont.filename, text.font.size,
                get_glyph_name(text) or text.glyph,
                texfont.effects.get("slant", 0),
                texfont.effects.get("extend", 1))
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
    stop_rasterizing = partialmethod(stop_filter, lambda img, dpi: (img, 0, 0))

    # "Undocumented" APIs needed to patch Agg.

    lock = _LOCK  # For webagg_core; matplotlib#10708 (<3.0).

    def buffer_rgba(self):  # For tkagg, webagg_core.
        return _util.cairo_to_straight_rgba8888(self._get_buffer())

    _renderer = property(buffer_rgba)  # For tkagg; matplotlib#18993 (<3.4).

    # For MixedModeRenderer; matplotlib#17788 (<3.4).
    def tostring_rgba_minimized(self):
        img, bounds = _get_drawn_subarray_and_bounds(
            _util.cairo_to_straight_rgba8888(self._get_buffer()))
        return img.tobytes(), bounds


def _check_print_extra_kwargs(*,
        # These arguments are already taken care of by print_figure().
        dpi=72, facecolor=None, edgecolor=None, orientation="portrait",
        dryrun=False, bbox_inches_restore=None):
    pass


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
            if last_renderer._draw_previously_disabled:
                _ensure_cleared = _ensure_drawn = True
            if _ensure_cleared:
                # This API is present (rather than just throwing away the
                # renderer and creating a new one) so to avoid invalidating the
                # Text._get_layout cache.
                last_renderer.clear()
                if _ensure_drawn:
                    with _LOCK:
                        self.figure.draw(last_renderer)
            last_renderer._draw_previously_disabled = False
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

    def buffer_rgba(self):  # NOTE: Needed for tests.
        return self.get_renderer(_ensure_drawn=True).buffer_rgba()

    def copy_from_bbox(self, bbox):
        return self.get_renderer(_ensure_drawn=True).copy_from_bbox(bbox)

    def restore_region(self, region):
        with _LOCK:
            self.get_renderer().restore_region(region)
        super().draw()

    def _print_vector(self, renderer_factory,
                      path_or_stream, *, metadata=None, **kwargs):
        _check_print_extra_kwargs(**kwargs)
        dpi = self.figure.get_dpi()
        self.figure.set_dpi(72)
        with cbook.open_file_cm(path_or_stream, "wb") as stream:
            renderer = renderer_factory(
                stream, self.figure.bbox.width, self.figure.bbox.height, dpi)
            try:
                # Setting invalid metadata can also throw, in which case the
                # rendered needs to be _finish()ed (to avoid later writing to a
                # closed file).
                renderer._set_metadata(metadata)
                with _LOCK:
                    self.figure.draw(renderer)
            finally:
                # When using bbox_inches = "tight", an additional renderer is
                # created to measure the figure bbox, but drawing on that
                # renderer must be disabled now.  Otherwise, when it gets gc'd
                # at shutdown, cairo's attempt to finalize() the surface will
                # segfault due to already torn down structures.  (This is only
                # needed on Matplotlib 3.2.0; earlier versions used a different
                # approach ("dryrun") and later versions already perform the
                # disabling.)  See also matplotlib#16731, or repro with
                #    plot(); savefig("/tmp/test.pdf", bbox_inches="tight")
                # Alternatively, one could call gc.collect() after print_figure
                # to gc the renderer earlier, but that is very slow.
                for name in dir(RendererBase):
                    if name.startswith("draw_"):
                        setattr(renderer, name, lambda *args, **kwargs: None)
                # _finish() corresponds finalize() in Matplotlib's PDF and SVG
                # backends; it is inlined in Matplotlib's PS backend.
                renderer._finish()

    print_pdf = partialmethod(
        _print_vector, GraphicsContextRendererCairo._for_pdf_output)
    print_svg = partialmethod(
        _print_vector, GraphicsContextRendererCairo._for_svg_output)
    print_svgz = partialmethod(
        _print_vector, GraphicsContextRendererCairo._for_svgz_output)
    if os.environ.get("MPLCAIRO_SCRIPT_SURFACE") in ["raster", "vector"]:
        print_cairoscript = partialmethod(
            _print_vector, GraphicsContextRendererCairo._for_script_output)

    def _print_ps_impl(self, is_eps, path_or_stream, *,
                       metadata=None, orientation="portrait", papertype=None,
                       **kwargs):
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
            raise ValueError(f"Invalid orientation ({orientation!r})")
        metadata = {**metadata} if metadata is not None else {}
        dsc_comments = metadata["_dsc_comments"] = [
            f"%%Orientation: {orientation}"]
        if "Title" in metadata:
            dsc_comments.append("%%Title: {}".format(metadata.pop("Title")))
        if not is_eps:
            dsc_comments.append(f"%%DocumentPaperSizes: {papertype}")
        print_method = partial(self._print_vector,
                               GraphicsContextRendererCairo._for_eps_output
                               if is_eps else
                               GraphicsContextRendererCairo._for_ps_output)
        if mpl.rcParams["ps.usedistiller"]:
            with TemporaryDirectory() as tmp_dirname:
                tmp_name = Path(tmp_dirname, "tmp")
                print_method(tmp_name, metadata=metadata, **kwargs)
                # Assume we can get away without passing the bbox.
                {"ghostscript": backend_ps.gs_distill,
                 "xpdf": backend_ps.xpdf_distill}[
                     mpl.rcParams["ps.usedistiller"]](
                         str(tmp_name), False, ptype=papertype)
                # If path_or_stream is *already* a text-mode stream then
                # tmp_name needs to be opened in text-mode too.
                with cbook.open_file_cm(path_or_stream, "wb") as stream, \
                     tmp_name.open("r" if cbook.file_requires_unicode(stream)
                                   else "rb") as tmp_file:
                    shutil.copyfileobj(tmp_file, stream)
        else:
            print_method(path_or_stream, metadata=metadata, **kwargs)

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

    def print_rgba(self, path_or_stream, *,
                   dryrun=False, metadata=None, **kwargs):
        _check_print_extra_kwargs(**kwargs)
        img = self._get_fresh_straight_rgba8888()
        if dryrun:
            return
        with cbook.open_file_cm(path_or_stream, "wb") as stream:
            stream.write(img.tobytes())

    print_raw = print_rgba

    def print_png(self, path_or_stream, *,
                  dryrun=False, metadata=None, pil_kwargs=None, **kwargs):
        _check_print_extra_kwargs(**kwargs)
        img = self._get_fresh_straight_rgba8888()
        if dryrun:
            return
        metadata = {
            "Software":
            f"matplotlib version {mpl.__version__}, https://matplotlib.org",
            **(metadata if metadata is not None else {}),
        }
        if pil_kwargs is None:
            pil_kwargs = {}
        # Only use the metadata kwarg if pnginfo is not set, because the
        # semantics of duplicate keys in pnginfo is unclear.
        if "pnginfo" not in pil_kwargs:
            pnginfo = PngInfo()
            for k, v in metadata.items():
                pnginfo.add_text(k, v)
            pil_kwargs["pnginfo"] = pnginfo
        pil_kwargs.setdefault("dpi", (self.figure.dpi, self.figure.dpi))
        Image.fromarray(img).save(path_or_stream, format="png", **pil_kwargs)

    def print_jpeg(self, path_or_stream, *,
                   dryrun=False, pil_kwargs=None, **kwargs):
        # Remove transparency by alpha-blending on an assumed white background.
        r, g, b, a = mpl.colors.to_rgba(self.figure.get_facecolor())
        try:
            self.figure.set_facecolor(a * np.array([r, g, b]) + 1 - a)
            img = self._get_fresh_straight_rgba8888()[..., :3]  # Drop alpha.
        finally:
            self.figure.set_facecolor((r, g, b, a))
        if dryrun:
            return
        if pil_kwargs is None:
            pil_kwargs = {}
        pil_kwargs.setdefault("dpi", (self.figure.dpi, self.figure.dpi))
        _check_print_extra_kwargs(**kwargs)
        Image.fromarray(img).save(path_or_stream, format="jpeg", **pil_kwargs)

    print_jpg = print_jpeg

    def print_tiff(self, path_or_stream, *,
                   dryrun=False, pil_kwargs=None, **kwargs):
        if pil_kwargs is None:
            pil_kwargs = {}
        pil_kwargs.setdefault("dpi", (self.figure.dpi, self.figure.dpi))
        _check_print_extra_kwargs(**kwargs)
        img = self._get_fresh_straight_rgba8888()
        if dryrun:
            return
        Image.fromarray(img).save(path_or_stream, format="tiff", **pil_kwargs)

    print_tif = print_tiff


@_Backend.export
class _BackendCairo(_Backend):
    FigureCanvas = FigureCanvasCairo
    FigureManager = FigureManagerBase
