import sys

try:
    import setuptools_scm
    __version__ = setuptools_scm.get_version(  # xref setup.py
        root="../..", relative_to=__file__,
        version_scheme="post-release", local_scheme="node-and-date")
except (ImportError, LookupError):
    try:
        from ._version import version as __version__
    except ImportError:
        pass

if sys.platform != "win32":
    def _load_symbols():
        # dlopen() pycairo's extension module with RTLD_GLOBAL to dynamically
        # load cairo.
        from ctypes import CDLL, RTLD_GLOBAL
        from cairo import _cairo
        CDLL(_cairo.__file__, RTLD_GLOBAL)

    _load_symbols()

from ._mplcairo import antialias_t, operator_t, get_options, set_options

__all__ = ["antialias_t", "operator_t", "get_options", "set_options"]

set_options(cairo_circles=True)
try:
    set_options(raqm=True)
except RuntimeError:
    pass
