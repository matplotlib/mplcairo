import os
import sys


if os.environ.get("MPLCAIRO_PATCH_AGG"):
    from importlib.machinery import PathFinder

    class MplCairoMetaPathFinder(PathFinder):
        def find_spec(self, fullname, path=None, target=None):
            spec = super().find_spec(fullname, path, target)
            if fullname == "matplotlib.backends.backend_agg":
                def exec_module(module):
                    type(spec.loader).exec_module(spec.loader, module)
                    # The pth file does not get properly uninstalled from
                    # a develop install.  See pypa/pip#4176.
                    try:
                        import mplcairo.base
                    except ImportError:
                        return
                    module.FigureCanvasAgg = \
                        mplcairo.base.FigureCanvasCairo
                    module.RendererAgg = \
                        mplcairo.base.GraphicsContextRendererCairo
                spec.loader.exec_module = exec_module
                sys.meta_path.remove(self)
            return spec
    sys.meta_path.insert(0, MplCairoMetaPathFinder())
