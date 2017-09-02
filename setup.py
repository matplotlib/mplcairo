from pathlib import Path
import shlex
import subprocess
import sys
from tempfile import NamedTemporaryFile

from setuptools import Extension, find_packages, setup
from setuptools.command.install_lib import install_lib


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


def _get_pkg_config(info, lib):
    return shlex.split(subprocess.check_output(["pkg-config", info, lib],
                                               universal_newlines=True))


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
            {"linux": ["-std=c++17", "-fvisibility=hidden"],
             "win32": ["/EHsc"],
             "darwin": ["-std=c++17", "-fvisibility=hidden",
                        "-stdlib=libc++", "-mmacosx-version-min=10.7"]}[
                sys.platform]
            + _get_pkg_config("--cflags", "cairo"),
        extra_link_args=
            _get_pkg_config("--libs", "cairo")
    ),
]


pth_src = """\
if os.environ.get("MPLCAIRO"):
    from importlib.machinery import PathFinder
    class PyplotMetaPathFinder(PathFinder):
        def find_spec(self, fullname, path=None, target=None):
            spec = super().find_spec(fullname, path, target)
            if fullname == "matplotlib.backends.backend_agg":
                def exec_module(module):
                    type(spec.loader).exec_module(spec.loader, module)
                    import mpl_cairo.base
                    module.FigureCanvasAgg = mpl_cairo.base.FigureCanvasCairo
                    module.RendererAgg = mpl_cairo.base.GraphicsContextRendererCairo
                spec.loader.exec_module = exec_module
                sys.meta_path.remove(self)
            return spec
    sys.meta_path.insert(0, PyplotMetaPathFinder())
"""


class install_lib_with_pth(install_lib):
    def run(self):
        super().run()
        with NamedTemporaryFile("w") as file:
            file.write("import os; exec({!r})".format(pth_src))
            file.flush()
            self.copy_file(
                file.name, str(Path(self.install_dir, "mpl_cairo.pth")))


setup(
    name="mpl_cairo",
    description="A (new) cairo backend for Matplotlib.",
    long_description=open("README.rst").read(),
    version=__version__,
    cmdclass={"install_lib": install_lib_with_pth},
    author="Antony Lee",
    url="https://github.com/anntzer/mpl_cairo",
    license="BSD",
    classifiers=[
        "Development Status :: 4 - Beta",
        "License :: OSI Approved :: BSD License",
        "Programming Language :: Python :: 3.4",
        "Programming Language :: Python :: 3.5",
        "Programming Language :: Python :: 3.6"
    ],
    packages=find_packages(include=["mpl_cairo", "mpl_cairo.*"]),
    ext_modules=ext_modules,
    python_requires=">=3.4",
    install_requires=["pybind11>=2.2"],
)
