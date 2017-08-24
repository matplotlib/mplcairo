# dlopen() the ft2font extension module with RTLD_GLOBAL to make _ft2Library
# available to _mpl_cairo.

def _dlopen_ft2font():
    from ctypes import CDLL, RTLD_GLOBAL
    from matplotlib import ft2font
    CDLL(ft2font.__file__, RTLD_GLOBAL)

_dlopen_ft2font()

from ._mpl_cairo import antialias_t
