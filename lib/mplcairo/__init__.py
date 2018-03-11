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
        import ctypes.util
        from ctypes import CDLL, RTLD_GLOBAL
        from cairo import _cairo
        CDLL(_cairo.__file__, RTLD_GLOBAL)

        # Only needed if we statically linked Raqm, but doesn't hurt otherwise
        # as we allow these libraries to be missing.
        raqm_deps = [ctypes.util.find_library(lib)
                    for lib in ["fribidi", "harfbuzz"]]
        if None not in raqm_deps:
            for dep in raqm_deps:
                CDLL(dep, RTLD_GLOBAL)

    _load_symbols()

from ._mplcairo import antialias_t
