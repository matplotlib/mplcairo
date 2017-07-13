import shlex
import subprocess
import sys

from setuptools import setup, Extension


__version__ = "0.0"


class get_pybind_include(object):
    """Helper class to determine the pybind11 include path.

    The purpose of this class is to postpone importing pybind11 until it is
    actually installed, so that the ``get_include()`` method can be invoked.
    """

    def __init__(self, user=False):
        self.user = user

    def __str__(self):
        import pybind11
        return pybind11.get_include(self.user)


ext_modules = [
    Extension(
        "mpl_cairo._mpl_cairo",
        ["src/_mpl_cairo.cpp", "src/_util.cpp", "src/_pattern_cache.cpp"],
        depends=["src/_mpl_cairo.h", "src/_util.h", "src/_pattern_cache.h"],
        language="c++",
        include_dirs=[
            get_pybind_include(), get_pybind_include(user=True)
        ],
        extra_compile_args=
            shlex.split(
                " ".join(subprocess.check_output(["pkg-config", "--cflags", lib],
                                                 universal_newlines=True)
                         for lib in ["cairo", "freetype2"]))
            + {"linux": ["-std=c++17", "-fvisibility=hidden"],
               "win32": ["/EHsc"],
               "darwin": ["-std=c++17", "-fvisibility=hidden",
                          "-stdlib=libc++", "-mmacosx-version-min=10.7"]}[
                   sys.platform],
        extra_link_args=
            shlex.split(
                " ".join(subprocess.check_output(["pkg-config", "--libs", lib],
                                                 universal_newlines=True)
                         for lib in ["cairo", "freetype2"])),
    ),
]


setup(
    name="mpl_cairo",
    version=__version__,
    author="Antony Lee",
    description="A (new) cairo backend for Matplotlib.",
    long_description="",
    ext_modules=ext_modules,
    install_requires=["pybind11"],
    zip_safe=False,
)
