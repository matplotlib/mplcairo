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
        stream, was_path = cbook.to_filehandle(
            path_or_stream, "wb", return_opened=True)
        default_format = rcParams["savefig.format"]
        self._stream = stream
        self._was_path = was_path
        fmt = (
            format if format is not None
            else Path(path_or_stream).suffix[1:] or default_format if was_path
            else default_format).lower()
        self._renderer = {
            "pdf": GraphicsContextRendererCairo._for_pdf_output,
            "ps": GraphicsContextRendererCairo._for_ps_output,
        }[fmt](stream, 1, 1, 1)
        self._stack = ExitStack()
        if was_path:
            self._stack.push(stream)
            self._stack.callback(self._renderer._finish)

    def __enter__(self):
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
        self._stack.close()
