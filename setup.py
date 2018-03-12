import functools
import json
import os
from pathlib import Path
import re
import shlex
import shutil
import subprocess
import sys

if sys.platform == "darwin":
    os.environ.setdefault("CC", "clang")
    # Funnily enough, distutils uses $CC to compile c++ extensions but $CXX to
    # *link* such extensions...  (Moreover, it does some funky changes to $CXX
    # if either $CC or $CXX has multiple words -- see UnixCCompiler.link for
    # details.)
    os.environ.setdefault("CXX", "clang")

from setupext import Extension, build_ext, find_packages, setup


def get_pkg_config(info, lib):
    if os.environ.get("MANYLINUX") and info == "--cflags":
        return ["-static-libgcc", "-static-libstdc++",
                "-I/usr/include/cairo",
                "-I/usr/include/freetype2"]
    return shlex.split(subprocess.check_output(["pkg-config", info, lib],
                                               universal_newlines=True))


@functools.lru_cache(1)
def path_from_link_libpath():
    match = re.fullmatch("(?i)/LIBPATH:(.*)", os.environ.get("LINK", ""))
    if not match:
        raise OSError("For a Windows build, the LINK environment variable "
                      "must be set to a value starting with '/LIBPATH:'")
    # "Easy" way to call CommandLineToArgvW...
    return Path(json.loads(subprocess.check_output(
        '"{}" -c "import json, sys; print(json.dumps(sys.argv[1]))" {}'
        .format(sys.executable, match.group(1)))))


class build_ext(build_ext):
    def _add_raqm_flags(self, ext):
        if os.environ.get("MPLCAIRO_USE_LIBRAQM"):
            ext.define_macros = (
                [("MPLCAIRO_USE_LIBRAQM", "1")])
            try:
                ext.extra_compile_args += (
                    get_pkg_config("--cflags", "raqm"))
                ext.extra_link_args += (
                    get_pkg_config("--libs", "raqm"))
            except subprocess.CalledProcessError:
                if Path("build/raqm-prefix").is_dir():
                    ext.include_dirs += (
                        ["build/raqm-prefix/include"])
                    ext.extra_objects += (
                        ["build/raqm-prefix/lib/libraqm.a"])
                else:
                    sys.exit("""
Raqm is not installed system-wide (but you requested it by setting the
MPLCAIRO_USE_LIBRAQM environment variable).  On Linux and OSX, if your system
package manager does not provide it,

1. Install the FriBiDi and HarfBuzz headers (e.g., 'libfribidi-dev' and
   'libharfbuzz-dev') using your system package manager.
2. Run 'tools/build-raqm.sh' *outside of any conda environment*.
3. Build and install mplcairo normally.
""")

    def build_extensions(self):
        import pybind11

        ext, = self.distribution.ext_modules

        ext.sources += (
            ["src/_mplcairo.cpp", "src/_util.cpp", "src/_pattern_cache.cpp"])
        ext.depends += (
            ["setup.py", "src/_macros.h",
             "src/_mplcairo.h", "src/_util.h", "src/_pattern_cache.h"])
        ext.language = "c++"
        ext.include_dirs += (
            [pybind11.get_include(), pybind11.get_include(user=True)])

        if sys.platform == "linux":
            import cairo
            ext.include_dirs += [cairo.get_include()]
            ext.extra_compile_args += (
                ["-std=c++1z", "-fvisibility=hidden", "-flto",
                 "-Wextra", "-Wpedantic"]
                + get_pkg_config("--cflags", "cairo"))
            ext.extra_link_args += (
                ["-flto"])
            self._add_raqm_flags(ext)
            if os.environ.get("MANYLINUX"):
                ext.extra_link_args += (
                    ["-static-libgcc", "-static-libstdc++"])
            else:
                ext.extra_compile_args += (
                    ["-march=native"])

        elif sys.platform == "darwin":
            import cairo
            ext.include_dirs += [cairo.get_include()]
            ext.extra_compile_args += (
                # version-min=10.9 avoids deprecation warning wrt. libstdc++.
                ["-std=c++1z", "-fvisibility=hidden", "-flto",
                 "-mmacosx-version-min=10.9"]
                + get_pkg_config("--cflags", "cairo"))
            ext.extra_link_args += (
                # version-min needs to be repeated to avoid a warning.
                ["-flto", "-mmacosx-version-min=10.9"])
            self._add_raqm_flags(ext)

        elif sys.platform == "win32":
            ext.include_dirs += (
                # Windows conda path for FreeType.
                [str(Path(sys.prefix, "Library/include"))])
            ext.extra_compile_args += (
                ["/std:c++17", "/EHsc", "/D_USE_MATH_DEFINES"])
            ext.libraries += (
                ["cairo", "freetype"])
            ext.library_dirs += (
                # Windows conda path for FreeType.
                [str(Path(sys.prefix, "Library/lib"))])

        # Workaround https://bugs.llvm.org/show_bug.cgi?id=33222 (clang +
        # libstdc++ + std::variant = compilation error).  Note that
        # `.compiler.compiler` only exists for UnixCCompiler.
        if (os.name == "posix"
            and "__clang__" in subprocess.check_output(
                self.compiler.compiler + ["-dM", "-E", "-x", "c", "/dev/null"],
                universal_newlines=True)):
            ext.extra_compile_args += ["-stdlib=libc++"]
            # Explicitly linking to libc++ is required to avoid picking up the
            # system C++ library (libstdc++ or an outdated libc++).
            ext.extra_link_args += ["-lc++"]

        super().build_extensions()

        if sys.platform == "win32":
            shutil.copy2(str(path_from_link_libpath() / "cairo.dll"),
                         str(Path(self.build_lib, "mplcairo")))


    def copy_extensions_to_source(self):
        super().copy_extensions_to_source()
        if sys.platform == "win32":
            shutil.copy2(str(path_from_link_libpath() / "cairo.dll"),
                         self.get_finalized_command("build_py")
                         .get_package_dir("mplcairo"))


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
    ext_modules = [Extension("mplcairo._mplcairo", [])],
    python_requires=">=3.4",
    setup_requires=["setuptools_scm"],
    use_scm_version={  # xref __init__.py
        "version_scheme": "post-release",
        "local_scheme": "node-and-date",
        "write_to": "lib/mplcairo/_version.py",
    },
    install_requires=[
        "matplotlib>=2.2",
        "pybind11>=2.2",
        "pycairo>=1.16.0; os_name == 'posix'",
    ],
)
