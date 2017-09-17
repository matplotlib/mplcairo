from ._version import get_versions
__version__ = get_versions()['version']
del get_versions

def _load_symbols():
    # dlopen() the ft2font extension module with RTLD_GLOBAL to make
    # _ft2Library available to _mpl_cairo.
    # dlopen() pycairo's extension module with RTLD_GLOBAL to dynamically load
    # cairo.
    from ctypes import CDLL, RTLD_GLOBAL
    from matplotlib import ft2font
    from cairo import _cairo
    CDLL(ft2font.__file__, RTLD_GLOBAL)
    CDLL(_cairo.__file__, RTLD_GLOBAL)

_load_symbols()

from ._mpl_cairo import antialias_t
