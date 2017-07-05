from matplotlib import colors, rcParams
from matplotlib.backend_bases import GraphicsContextBase, RendererBase

from . import _mpl_cairo
from ._mpl_cairo import antialias_t, format_t



class GraphicsContextRendererCairo(
        _mpl_cairo.GraphicsContextRendererCairo,
        # Fill in the missing methods.
        GraphicsContextBase,
        RendererBase):
    def __init__(self, *args, **kwargs):
        _mpl_cairo.GraphicsContextRendererCairo.__init__(self, *args, **kwargs)
        # Define the hatch-related attributes from GraphicsContextBase.
        # Everything else lives directly at the C-level.
        self._hatch = None
        self._hatch_color = colors.to_rgba(rcParams['hatch.color'])
        self._hatch_linewidth = rcParams['hatch.linewidth']
