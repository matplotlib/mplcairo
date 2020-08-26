"""
mplcairo build
==============

Environment variables:

MPLCAIRO_MANYLINUX
    - If set, build a manylinux wheel: pkg-config is shimmed and libstdc++ is
      statically linked.

MPLCAIRO_NO_UNITY_BUILD
    - If set, compile the various cpp files separately, instead of as a single
      compilation unit.  Unity builds tend to be faster even when using ccache,
      because linking is rather time-consuming.
"""

from distutils.version import LooseVersion
import functools
import json
import os
from pathlib import Path
import platform
import re
import shlex
import shutil
import subprocess
from subprocess import CalledProcessError
import sys
import urllib.request

if sys.platform == "darwin":
    os.environ.setdefault("CC", "clang")
    # Funnily enough, distutils uses $CC to compile c++ extensions but
    # $CXX to *link* such extensions...  (Moreover, it does some funky
    # changes to $CXX if either $CC or $CXX has multiple words -- see e.g.
    # https://bugs.python.org/issue6863.)
    os.environ.setdefault("CXX", "clang")

from setupext import Extension, build_ext, find_packages, setup


MIN_CAIRO_VERSION = "1.11.4"  # Also in _feature_tests.cpp.
MIN_RAQM_VERSION = "0.7.0"
MANYLINUX = bool(os.environ.get("MPLCAIRO_MANYLINUX", ""))
UNITY_BUILD = not bool(os.environ.get("MPLCAIRO_NO_UNITY_BUILD"))


def get_pkgconfig(info, lib):
    if MANYLINUX:
        if info.startswith("--atleast-version"):
            if lib == "raqm":
                raise FileNotFoundError  # Trigger the header download.
            else:
                return ""
        if info == "--cflags":
            return ["-static-libgcc", "-static-libstdc++",
                    "-I/usr/include/cairo",
                    "-I/usr/include/freetype2"]
    return shlex.split(subprocess.check_output(["pkg-config", info, lib],
                                               universal_newlines=True))


@functools.lru_cache(1)
def paths_from_link_libpaths():
    # "Easy" way to call CommandLineToArgvW...
    argv = json.loads(subprocess.check_output(
        '"{}" -c "import json, sys; print(json.dumps(sys.argv[1:]))" {}'
        .format(sys.executable, os.environ.get("LINK", ""))))
    paths = []
    for arg in argv:
        match = re.fullmatch("(?i)/LIBPATH:(.*)", arg)
        if match:
            paths.append(Path(match.group(1)))
    return paths


class build_ext(build_ext):

    def build_extensions(self):
        import pybind11

        ext, = self.distribution.ext_modules

        ext.depends += [
            "setup.py",
            *map(str, Path("src").glob("*.h")),
            *map(str, Path("src").glob("*.cpp")),
        ]
        if UNITY_BUILD:
            ext.sources += ["src/_unity_build.cpp"]
        else:
            ext.sources += [*map(str, Path("src").glob("*.cpp"))]
            ext.sources.remove("src/_unity_build.cpp")

        ext.include_dirs += [pybind11.get_include()]

        tmp_include_dir = Path(self.get_finalized_command("build").build_base,
                               "include")
        tmp_include_dir.mkdir(parents=True, exist_ok=True)
        # On Arch Linux, the python-pillow (Arch) package
        # includes a version of ``raqm.h`` that is both invalid
        # (https://bugs.archlinux.org/task/57492) and now outdated (it is
        # missing a declaration for `raqm_version_string`), but placed in an
        # non-overridable directory for distutils.  Thus, on that distro, force
        # the use of a cleanly downloaded header.
        try:
            is_arch = "Arch Linux" in Path("/etc/os-release").read_text()
        except OSError:
            is_arch = False
        has_pkgconfig_raqm = False
        if not is_arch:
            try:
                has_pkgconfig_raqm = get_pkgconfig(
                    f"--atleast-version={MIN_RAQM_VERSION}", "raqm")
            except (FileNotFoundError, CalledProcessError):
                pass
        if not has_pkgconfig_raqm:
            (tmp_include_dir / "raqm-version.h").write_text("")  # Touch it.
            with urllib.request.urlopen(
                    f"https://raw.githubusercontent.com/HOST-Oman/libraqm/"
                    f"v{MIN_RAQM_VERSION}/src/raqm.h") as request, \
                 (tmp_include_dir / "raqm.h").open("wb") as file:
                file.write(request.read())
            ext.include_dirs += [tmp_include_dir]

        if sys.platform == "linux":
            import cairo
            get_pkgconfig(f"--atleast-version={MIN_CAIRO_VERSION}", "cairo")
            ext.include_dirs += [cairo.get_include()]
            ext.extra_compile_args += [
                "-std=c++1z", "-fvisibility=hidden", "-flto",
                "-Wall", "-Wextra", "-Wpedantic",
                *get_pkgconfig("--cflags", "cairo"),
            ]
            ext.extra_link_args += ["-flto"]
            if MANYLINUX:
                ext.extra_link_args += ["-static-libgcc", "-static-libstdc++"]

        elif sys.platform == "darwin":
            import cairo
            get_pkgconfig(f"--atleast-version={MIN_CAIRO_VERSION}", "cairo")
            ext.include_dirs += [cairo.get_include()]
            # On OSX<10.14, version-min=10.9 avoids deprecation warning wrt.
            # libstdc++, but assumes that the build uses non-Xcode-provided
            # LLVM.
            # On OSX>=10.14, assume that the build uses the normal toolchain.
            macosx_min_version = (
                "10.14" if LooseVersion(platform.mac_ver()[0]) >= "10.14"
                else "10.9")
            ext.extra_compile_args += [
                "-std=c++1z", "-fvisibility=hidden", "-flto",
                f"-mmacosx-version-min={macosx_min_version}",
                *get_pkgconfig("--cflags", "cairo"),
            ]
            ext.extra_link_args += [
                # version-min needs to be repeated to avoid a warning.
                "-flto", f"-mmacosx-version-min={macosx_min_version}",
            ]

        elif sys.platform == "win32":
            # Windows conda path for FreeType.
            ext.include_dirs += [Path(sys.prefix, "Library/include")]
            ext.extra_compile_args += [
                "/std:c++17", "/Zc:__cplusplus", "/experimental:preprocessor",
                "/EHsc", "/D_USE_MATH_DEFINES",
                "/wd4244", "/wd4267",
            ]  # cf. gcc -Wconversion.
            ext.libraries += ["psapi", "cairo", "freetype"]
            # Windows conda path for FreeType -- needs to be str, not Path.
            ext.library_dirs += [str(Path(sys.prefix, "Library/lib"))]

        # Workaround https://bugs.llvm.org/show_bug.cgi?id=33222 (clang
        # + libstdc++ + std::variant = compilation error).  Note that
        # `.compiler.compiler` only exists for UnixCCompiler.
        if os.name == "posix":
            compiler_macros = subprocess.check_output(
                [*self.compiler.compiler, "-dM", "-E", "-x", "c", "/dev/null"],
                universal_newlines=True)
            if "__clang__" in compiler_macros:
                ext.extra_compile_args += ["-stdlib=libc++"]
                # Explicitly linking to libc++ is required to avoid picking up
                # the system C++ library (libstdc++ or an outdated libc++).
                ext.extra_link_args += ["-lc++"]

        super().build_extensions()

        if sys.platform == "win32":
            for dll in ["cairo.dll", "freetype.dll"]:
                for path in paths_from_link_libpaths():
                    if (path / dll).exists():
                        shutil.copy2(path / dll,
                                     Path(self.build_lib, "mplcairo"))
                        break

    def copy_extensions_to_source(self):
        super().copy_extensions_to_source()
        if sys.platform == "win32":
            for dll in ["cairo.dll", "freetype.dll"]:
                for path in paths_from_link_libpaths():
                    if (path / dll).exists():
                        shutil.copy2(path / dll,
                                     self.get_finalized_command("build_py")
                                     .get_package_dir("mplcairo"))
                        break


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
    url="https://github.com/matplotlib/mplcairo",
    license="MIT",
    classifiers=[
        "Development Status :: 4 - Beta",
        "Framework :: Matplotlib",
        "License :: OSI Approved :: MIT License",
        "Programming Language :: Python :: 3",
    ],
    cmdclass={"build_ext": build_ext},
    packages=find_packages("lib"),
    package_dir={"": "lib"},
    ext_modules=[Extension("mplcairo._mplcairo", [])],
    python_requires=">=3.6",
    setup_requires=[
        "setuptools_scm",
        "pybind11>=2.5.0",
        # Actually also a setup_requires on Linux, but in the manylinux build
        # we need to shim it.
        "pycairo>=1.16.0; sys_platform == 'darwin'",
    ],
    use_scm_version={  # xref __init__.py
        "version_scheme": "post-release",
        "local_scheme": "node-and-date",
        "write_to": "lib/mplcairo/_version.py",
    },
    install_requires=[
        "matplotlib>=2.2",
        "pillow",  # Already a dependency of mpl>=3.3.
        "pycairo>=1.16.0; os_name == 'posix'",
    ],
)
