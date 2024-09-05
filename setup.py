"""
mplcairo build
==============

Environment variables:

MPLCAIRO_NO_PYCAIRO
    If set, pycairo is not a build requirement; its include path is configured
    externally.

MPLCAIRO_NO_UNITY_BUILD
    If set, compile the various cpp files separately, instead of as a single
    compilation unit.  Unity builds tend to be faster even when using ccache,
    because linking is rather time-consuming.
"""

import functools
import json
import os
from pathlib import Path
import re
import shlex
import shutil
import subprocess
from subprocess import CalledProcessError
import sys
from tempfile import TemporaryDirectory
import tokenize
import urllib.request

import setuptools
from setuptools import Distribution
from pybind11.setup_helpers import Pybind11Extension
if os.environ.get("MPLCAIRO_NO_PYCAIRO", ""):
    cairo = None
elif os.name == "nt":
    sys.exit("""\
===============================================================================
On Windows, please use the tools/build-windows-wheel.py tool to build an
installable wheel.  Directly installing from a source tree is not supported.
You may set the MPLCAIRO_NO_PYCAIRO environment variable to clear this message,
if all necessary dependencies have been manually installed as described in the
README.
===============================================================================
""")
else:
    import cairo


MIN_CAIRO_VERSION = "1.13.1"  # Also in _feature_tests.cpp.
MIN_RAQM_VERSION = "0.7.0"
UNITY_BUILD = not bool(os.environ.get("MPLCAIRO_NO_UNITY_BUILD"))


def get_pkgconfig(*args):
    return shlex.split(
        subprocess.check_output(["pkg-config", *args], text=True))


def gen_extension(tmpdir):
    ext = Pybind11Extension(
        "mplcairo._mplcairo",
        sources=(
            ["ext/_unity_build.cpp"] if UNITY_BUILD else
            sorted({*map(str, Path("ext").glob("*.cpp"))}
                   - {"ext/_unity_build.cpp"})),
        depends=[
            "setup.py",
            *map(str, Path("ext").glob("*.h")),
            *map(str, Path("ext").glob("*.cpp")),
        ],
        cxx_std=17,
        include_dirs=[cairo.get_include()] if cairo else [],
    )

    # NOTE: Versions <= 8.2 of Arch Linux's python-pillow package included
    # *into a non-overridable distutils header directory* a ``raqm.h`` that
    # is both invalid (https://bugs.archlinux.org/task/57492) and outdated
    # (missing a declaration for `raqm_version_string`).  It is thus not
    # possible to build mplcairo with such an old distro package installed.
    try:
        get_pkgconfig(f"raqm >= {MIN_RAQM_VERSION}")
    except (FileNotFoundError, CalledProcessError):
        (tmpdir / "raqm-version.h").write_text("")  # Touch it.
        with urllib.request.urlopen(
                f"https://raw.githubusercontent.com/HOST-Oman/libraqm/"
                f"v{MIN_RAQM_VERSION}/src/raqm.h") as request:
            (tmpdir / "raqm.h").write_bytes(request.read())
        ext.include_dirs += [tmpdir]
    else:
        ext.extra_compile_args += get_pkgconfig("--cflags", "raqm")

    if os.name == "posix":
        get_pkgconfig(f"cairo >= {MIN_CAIRO_VERSION}")
        ext.extra_compile_args += [
            "-flto", "-Wall", "-Wextra", "-Wpedantic",
            *get_pkgconfig("--cflags", "cairo"),
        ]
        ext.extra_link_args += ["-flto"]

    elif os.name == "nt":
        # Windows conda path for FreeType.
        ext.include_dirs += [Path(sys.prefix, "Library/include")]
        ext.extra_compile_args += [
            "/experimental:preprocessor",
            "/wd4244", "/wd4267",  # cf. gcc -Wconversion.
        ]
        ext.libraries += ["psapi", "cairo", "freetype"]
        # Windows conda path for FreeType -- needs to be str, not Path.
        ext.library_dirs += [str(Path(sys.prefix, "Library/lib"))]

    return ext


# NOTE: Finding the dlls in libpath is not particularly correct as this really
# specifies paths to .lib import libraries; instead we should use
# ctypes.util.find_library and ask the caller to set PATH if desired.
@functools.lru_cache(1)
def paths_from_link_libpaths():
    # "Easy" way to call CommandLineToArgvW...
    argv = json.loads(subprocess.check_output(
        '"{}" -c "import json, sys; print(json.dumps(sys.argv[1:]))" {}'
        .format(sys.executable, os.environ.get("LINK", ""))))
    return [Path(match[1])
            for match in map(re.compile("(?i)/LIBPATH:(.*)").fullmatch, argv)
            if match]


class build_ext(setuptools.command.build_ext.build_ext):
    def _copy_dlls_to(self, dest):
        if os.name == "nt":
            for dll in ["cairo.dll", "freetype.dll"]:
                for path in paths_from_link_libpaths():
                    if (path / dll).exists():
                        shutil.copy2(path / dll, dest)
                        break

    def build_extensions(self):
        super().build_extensions()
        self._copy_dlls_to(Path(self.build_lib, "mplcairo"))

    def copy_extensions_to_source(self):
        super().copy_extensions_to_source()
        self._copy_dlls_to(
            self.get_finalized_command("build_py").get_package_dir("mplcairo"))


def register_pth_hook(source_path, pth_name):
    """
    ::
        setup.register_pth_hook("hook_source.py", "hook_name.pth")  # Add hook.
    """
    with tokenize.open(source_path) as file:
        source = file.read()
    _pth_hook_mixin._pth_hooks.append((pth_name, source))


class _pth_hook_mixin:
    _pth_hooks = []

    def run(self):
        super().run()
        for pth_name, source in self._pth_hooks:
            with Path(self.install_dir, pth_name).open("w") as file:
                file.write(f"import os; exec({source!r})")

    def get_outputs(self):
        return (super().get_outputs()
                + [str(Path(self.install_dir, pth_name))
                   for pth_name, _ in self._pth_hooks])


register_pth_hook("setup_mplcairo_pth.py", "mplcairo.pth")


gcc = Distribution().get_command_class
with TemporaryDirectory() as tmpdir:
    setuptools.setup(
        cmdclass={
            "build_ext": build_ext,
            "develop": type(
                "develop_wph", (_pth_hook_mixin, gcc("develop")), {}),
            "install_lib": type(
                "install_lib_wph", (_pth_hook_mixin, gcc("install_lib")), {}),
        },
        ext_modules=[gen_extension(tmpdir=Path(tmpdir))],
    )
