from ._version import get_versions
__version__ = get_versions()['version']
del get_versions

def _load_symbols():
    # dlopen() the ft2font extension module with RTLD_GLOBAL to make
    # _ft2Library available to _mplcairo.
    # dlopen() pycairo's extension module with RTLD_GLOBAL to dynamically load
    # cairo.
    import ctypes.util
    from ctypes import CDLL, RTLD_GLOBAL
    from matplotlib import ft2font
    from cairo import _cairo
    CDLL(ft2font.__file__, RTLD_GLOBAL)
    CDLL(_cairo.__file__, RTLD_GLOBAL)

    # Only needed if we statically linked Raqm, but doesn't hurt otherwise as
    # we allow these libraries to be missing.
    raqm_deps = [ctypes.util.find_library(lib)
                 for lib in ["fribidi", "harfbuzz"]]
    if None not in raqm_deps:
        for dep in raqm_deps:
            CDLL(dep, RTLD_GLOBAL)

_load_symbols()

from ._mplcairo import antialias_t
