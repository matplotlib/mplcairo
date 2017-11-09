from collections import ChainMap
import os
from pathlib import Path
import shlex
import subprocess
import sys

if sys.platform == "darwin":
    os.environ.setdefault("CC", "clang")
    # Funnily enough, distutils uses CC to compile c++ extensions but CXX to
    # *link* such extensions...
    os.environ.setdefault("CXX", "clang")

from setupext import Extension, find_packages, get_pybind_include, setup
from setuptools.command.build_ext import build_ext


def get_pkg_config(info, lib):
    if os.environ.get("MANYLINUX") and info == "--cflags":
        return ["-static-libgcc", "-static-libstdc++",
                "-I/usr/include/cairo",
                "-I/usr/include/freetype2",
                "-I/usr/include/pycairo"]
    return shlex.split(subprocess.check_output(["pkg-config", info, lib],
                                               universal_newlines=True))


EXTENSION = Extension(
    "mplcairo._mplcairo",
    ["src/_mplcairo.cpp", "src/_util.cpp", "src/_pattern_cache.cpp"],
    depends=
        ["setup.py", "src/_macros.h",
         "src/_mplcairo.h", "src/_util.h", "src/_pattern_cache.h"],
    language=
        "c++",
    include_dirs=
        [get_pybind_include(), get_pybind_include(user=True)],
)


if sys.platform == "linux":
    EXTENSION.extra_compile_args += (
        ["-std=c++17", "-fvisibility=hidden", "-Wextra", "-Wpedantic"]
        + get_pkg_config("--cflags", "py3cairo"))
    if os.environ.get("MPLCAIRO_USE_LIBRAQM"):
        EXTENSION.define_macros = [("MPLCAIRO_USE_LIBRAQM", "1")]
        try:
            EXTENSION.extra_compile_args += (
                get_pkg_config("--cflags", "raqm"))
            EXTENSION.extra_link_args += (
                get_pkg_config("--libs", "raqm"))
        except subprocess.CalledProcessError:
            if Path("build/raqm-prefix").is_dir():
                EXTENSION.include_dirs += (
                    ["build/raqm-prefix/include"])
                EXTENSION.extra_objects += (
                    ["build/raqm-prefix/lib/libraqm.a"])
            else:
                sys.exit("""
Raqm is not installed system-wide.  If your system package manager does not
provide it,

1. Install the FriBiDi and HarfBuzz headers (e.g., 'libfribidi-dev' and
  'libharfbuzz-dev') using your system package manager.
2. Run 'tools/build-raqm.sh' *outside of any conda environment*.
3. Build and install mplcairo normally.
""")
    if os.environ.get("MANYLINUX"):
        EXTENSION.extra_link_args += (
            ["-static-libgcc", "-static-libstdc++"])

elif sys.platform == "darwin":
    EXTENSION.extra_compile_args += (
        # version-min=10.9 avoids deprecation warning wrt. libstdc++.
        ["-std=c++17", "-fvisibility=hidden", "-mmacosx-version-min=10.9"]
        + get_pkg_config("--cflags", "py3cairo"))
    EXTENSION.extra_link_args += (
        # version-min needs to be repeated to avoid a warning.
        ["-mmacosx-version-min=10.9"])
    if os.environ.get("MPLCAIRO_USE_LIBRAQM"):
        EXTENSION.define_macros = [("MPLCAIRO_USE_LIBRAQM", "1")]
        EXTENSION.extra_compile_args += (
            get_pkg_config("--cflags", "raqm"))
        EXTENSION.extra_link_args += (
            get_pkg_config("--libs", "raqm"))

elif sys.platform == "win32":
    EXTENSION.extra_compile_args += (
        ["/std:c++17", "/EHsc", "/D_USE_MATH_DEFINES",
         # Windows conda paths.
         "-I{}".format(Path(sys.prefix, "Library/include")),
         "-I{}".format(Path(sys.prefix, "Library/include/cairo")),
         "-I{}".format(Path(sys.prefix, "include/pycairo"))])


class build_ext(build_ext):
    def build_extensions(self):
        try:
            self.compiler.compiler_so.remove("-Wstrict-prototypes")
        except ValueError:
            pass
        # Workaround https://bugs.llvm.org/show_bug.cgi?id=33222 (clang +
        # libstdc++ + std::variant = compilation error).
        if (subprocess.check_output([self.compiler.compiler[0], "--version"],
                                    universal_newlines=True)
                .startswith("clang")):
            EXTENSION.extra_compile_args += ["-stdlib=libc++"]
            # Explicitly linking to libc++ is required to avoid picking up the
            # system C++ library (libstdc++ or an outdated libc++).
            EXTENSION.extra_link_args += ["-lc++"]
        super().build_extensions()


@setup.register_pth_hook("mplcairo.pth")
def _pth_hook():
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


setup(
    name="mplcairo",
    description="A (new) cairo backend for Matplotlib.",
    long_description=open("README.rst", encoding="utf-8").read(),
    author="Antony Lee",
    url="https://github.com/anntzer/mplcairo",
    license="MIT",
    classifiers=[
        "Development Status :: 4 - Beta",
        "License :: OSI Approved :: MIT License",
        "Programming Language :: Python :: 3.4",
        "Programming Language :: Python :: 3.5",
        "Programming Language :: Python :: 3.6"
    ],
    cmdclass={"build_ext": build_ext},
    packages=find_packages("lib"),
    package_dir={"": "lib"},
    ext_modules=[EXTENSION],
    python_requires=">=3.4",
    install_requires=["pybind11>=2.2", "pycairo>=1.12.0"],
)
