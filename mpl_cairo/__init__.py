__all__ = ["GraphicsContextRendererCairo", "install", "uninstall"]

from matplotlib.backend_bases import GraphicsContextBase, RendererBase
from matplotlib.backends import backend_cairo
from matplotlib.mathtext import MathTextParser
from . import _mpl_cairo


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
            cls.__bases__ = (
                cls.__bases__[:idx] + (new,) + cls.__bases__[idx + 1:])
        setattr(backend_cairo, new.__name__, new)


def install():
    """Switch to new cairo backend implementation.
    """
    _swap(_class_pairs)


def uninstall():
    """Switch back to upstream cairo backend implementation.
    """
    _swap([(new, old) for old, new in _class_pairs])
