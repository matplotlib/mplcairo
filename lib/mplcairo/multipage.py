from contextlib import ExitStack
from pathlib import Path

from matplotlib import cbook, rcParams

from .base import GraphicsContextRendererCairo, _LOCK


class MultiPage:
    """Multi-page output, for backends that support them.

    Use as follows::

        with MultiPage(path) as mp:
            mp.savefig(fig1)
            mp.savefig(fig2)
    """

    def __init__(self, path_or_stream=None, format=None):
        self._path_or_stream = path_or_stream
        self._format = format

    def __enter__(self):
        self._stack = ExitStack()
        stream = self._stack.enter_context(
            cbook.open_file_cm(self._path_or_stream, "wb"))
        fmt = (self._format
               or Path(getattr(stream, "name", "")).suffix[1:]
               or rcParams["savefig.format"]).lower()
        self._renderer = {
            "pdf": GraphicsContextRendererCairo._for_pdf_output,
            "ps": GraphicsContextRendererCairo._for_ps_output,
        }[fmt](stream, 1, 1, 1)
        self._stack.callback(self._renderer._finish)
        return self

    def savefig(self, figure, **kwargs):
        # FIXME[Upstream]: Not all kwargs are supported here -- but I plan to
        # deprecate them upstream.
        figure.set_dpi(72)
        self._renderer._set_size(*figure.canvas.get_width_height(),
                                 kwargs.get("dpi", 72))
        with _LOCK:
            figure.draw(self._renderer)
        self._renderer._show_page()

    def __exit__(self, *args):
        return self._stack.__exit__(*args)
