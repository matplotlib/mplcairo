__all__ = ["GraphicsContextRendererCairo", "install", "uninstall"]

from matplotlib.backend_bases import GraphicsContextBase, RendererBase
from matplotlib.backends import backend_cairo
from matplotlib.font_manager import ttfFontProperty
from matplotlib.mathtext import MathtextBackendCairo, MathTextParser
from . import _mpl_cairo


class MathtextBackendCairo2(MathtextBackendCairo):
    def render_glyph(self, ox, oy, info):
        self.glyphs.append(
            # Convert to ttfFontProperty here.
            (ttfFontProperty(info.font),
             info.fontsize,
             chr(info.num),
             ox,
             oy - info.offset - self.height))


MathTextParser._backend_mapping["cairo2"] = MathtextBackendCairo2


class GraphicsContextRendererCairo(
        _mpl_cairo.GraphicsContextRendererCairo,
        GraphicsContextBase,
        RendererBase):
    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)
        self.mathtext_parser = MathTextParser("agg")


# Keep the two classes separate, so that we can figure out who inherits what.


class GraphicsContextCairo(GraphicsContextRendererCairo):
    pass


class RendererCairo(GraphicsContextRendererCairo):
    pass


_class_pairs = [
    (backend_cairo.GraphicsContextCairo, GraphicsContextRendererCairo),
    (backend_cairo.RendererCairo, RendererCairo)]


def _swap(class_pairs):
    for old, new in class_pairs:
        for cls in old.__subclasses__():
            idx = cls.__bases__.index(old)
            cls.__bases__ = cls.__bases__[:idx] + (new,) + cls.__bases__[idx + 1:]
        setattr(backend_cairo, new.__name__, new)


def install():
    """Switch to new cairo backend implementation.
    """
    _swap(_class_pairs)


def uninstall():
    """Switch back to upstream cairo backend implementation.
    """
    _swap([(new, old) for old, new in _class_pairs])
