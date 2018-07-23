from contextlib import ExitStack
from pathlib import Path

from matplotlib import cbook, rcParams

from .base import GraphicsContextRendererCairo, _LOCK


class MultiPage:
    """Multi-page output, for backends that support them.

    Usage is similar to `matplotlib.backends.backend_pdf.PdfPages`::

        with MultiPage(path, metadata=...) as mp:
            mp.savefig(fig1)
            mp.savefig(fig2)

    (Note that no other methods of `PdfPages` are implemented.)
    """

    def __init__(self, path_or_stream=None, format=None, *, metadata=None):
        self._stack = ExitStack()
        stream = self._stack.enter_context(
            cbook.open_file_cm(path_or_stream, "wb"))
        fmt = (format
               or Path(getattr(stream, "name", "")).suffix[1:]
               or rcParams["savefig.format"]).lower()
        self._renderer = {
            "pdf": GraphicsContextRendererCairo._for_pdf_output,
            "ps": GraphicsContextRendererCairo._for_ps_output,
        }[fmt](stream, 1, 1, 1)  # FIXME(?) What to do with empty files.
        self._stack.callback(self._renderer._finish)
        self._renderer._set_metadata(metadata)

    def savefig(self, figure, **kwargs):
        # FIXME[Upstream]: Not all kwargs are supported here -- but I plan to
        # deprecate them upstream.
        figure.set_dpi(72)
        self._renderer._set_size(*figure.canvas.get_width_height(),
                                 kwargs.get("dpi", 72))
        with _LOCK:
            figure.draw(self._renderer)
        self._renderer._show_page()

    def close(self):
        return self._stack.__exit__(None, None, None)

    def __enter__(self):
        return self

    def __exit__(self, *args):
        return self._stack.__exit__(*args)
