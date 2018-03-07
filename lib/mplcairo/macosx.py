import re

from matplotlib.backends import _macosx
from matplotlib.backends.backend_macosx import _BackendMac, FigureCanvasMac

from . import base
from .base import FigureCanvasCairo


class FigureCanvasMacCairo(FigureCanvasCairo, _macosx.FigureCanvas):
    # Essentially: inherit from FigureCanvasMac without inheriting from
    # FigureCanvasAgg.
    locals().update({
        k: v for k, v in vars(FigureCanvasMac).items()
        if not re.fullmatch("__.*__", k)})

    def __init__(self, figure):
        # Inline the call to FigureCanvasCairo.__init__ as _macosx.FigureCanvas
        # has a different signature and thus we cannot use cooperative
        # inheritance (basically, we want that __init__, and only __init__,
        # inserts FigureCanvasMac in the inheritance chain between
        # FigureCanvasCairo and _macosx.FigureCanvasMac).
        base._fix_ipython_backend2gui()
        FigureCanvasMac.__init__(self, figure)

    # A bit hackish, but that's what _macosx.FigureCanvas wants...
    def _draw(self):
        if self.figure.stale:
            self._last_renderer_call = None, None
        self._renderer = self.get_renderer(_draw_if_new=True)._get_buffer()
        return self


@_BackendMac.export
class _BackendMacCairo(_BackendMac):
    FigureCanvas = FigureCanvasMacCairo
