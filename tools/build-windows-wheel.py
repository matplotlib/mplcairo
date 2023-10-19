"""
Fetch dependencies and build a Windows wheel
============================================

This script depends on setuptools being installed (even though this script
already handles installs build into a temporary venv and runs it from there, it
does not do the same for setuptools for practicality -- doing so would lead to
running doubly-nested python invocations to build the import libraries).

The cairo headers (and their dependencies) are fetched from the Arch Linux
repositories (the official cairo release tarball contains unbuilt headers (e.g.
missing cairo-features.h) and is huge due to the presence of test baseline
images).  The cairo binaries are fetched from the pycairo 1.22 wheel.

The FreeType headers and binary are fetched from the "official" build__ listed
on FreeType's website.

__ https://github.com/ubawurinna/freetype-windows-binaries
"""

import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import sysconfig
from tempfile import TemporaryDirectory
import urllib.request

import setuptools


td = TemporaryDirectory()
tmpdir = Path(td.name)

subprocess.run([sys.executable, "-mvenv", tmpdir], check=True)
subprocess.run(
    [tmpdir / "Scripts/python.exe", "-mpip", "install", "--upgrade", "build"],
    check=True)

urls = {
    # Download a pycairo 1.22 wheel and manually unzip it, because
    # pycairo>=1.23 switched to static linking, and so that the build does not
    # depend on pycairo 1.22 having a wheel for the current version of Python.
    Path("pycairo.zip"):
        max([e
             for e in json.load(urllib.request.urlopen(
                 "https://pypi.org/pypi/pycairo/1.22.0/json"))["urls"]
             if e["filename"].endswith(
                 sysconfig.get_platform().replace("-", "_") + ".whl")],
            key=lambda e: e["upload_time"])["url"],
    # Download the cairo headers from Arch Linux (<1Mb, vs >40Mb for the
    # official tarball, which contains baseline images) from before Arch
    # switched to zstd.
    Path("cairo.txz"):
        "https://archive.org/download/archlinux_pkg_cairo/"
        "cairo-1.17.2%2B17%2Bg52a7c79fd-2-x86_64.pkg.tar.xz",
    Path("fontconfig.txz"):
        "https://archive.org/download/archlinux_pkg_fontconfig/"
        "fontconfig-2%3A2.13.91%2B24%2Bg75eadca-1-x86_64.pkg.tar.xz",
    # Download the "official" FreeType build.
    Path("freetype.zip"):
        "https://github.com/ubawurinna/freetype-windows-binaries/"
        "releases/download/v2.9.1/freetype-2.9.1.zip",
}
for archive_path, url in urls.items():
    with urllib.request.urlopen(url) as request:
        (tmpdir / archive_path).write_bytes(request.read())
    shutil.unpack_archive(tmpdir / archive_path, tmpdir / archive_path.stem)

(tmpdir / "cairo/win64").mkdir(parents=True)
shutil.copy(tmpdir / "pycairo/cairo/cairo.dll", tmpdir / "cairo/win64")
# Get hold of a CCompiler object, by creating a dummy Distribution with a list
# of extension modules that claims to be truthy (but is actually empty) and
# running its build_ext command.  Prior to the deprecation of distutils, this
# was just ``cc = distutils.ccompiler.new_compiler(); cc.initialize()``.
class L(list): __bool__ = lambda self: True
be = setuptools.Distribution({"ext_modules": L()}).get_command_obj("build_ext")
be.finalize_options()
be.run()
cc = be.compiler
cc.initialize()
# On setuptools versions that use "local" distutils, ``cc.spawn(["dumpbin",
# ...])`` and ``cc.spawn(["lib", ...])`` no longer manage to locate the right
# executables, even though they are correctly on the PATH, because only the env
# kwarg to Popen() is updated, and not os.environ["PATH"].  Use shutil.which to
# walk the PATH and get absolute executable paths.
cc.spawn([
    sys.executable, "-c",
    "import pathlib, shutil, sys\n"
    "for e in ['dumpbin', 'lib']:\n"
    "    pathlib.Path(sys.argv[1], e + '-path').write_text(shutil.which(e))\n",
    str(tmpdir),
])
dumpbin_path = Path(tmpdir, "dumpbin-path").read_text()
lib_path = Path(tmpdir, "lib-path").read_text()
# Build the import library.
cc.spawn(
    [dumpbin_path, "/EXPORTS", f"/OUT:{tmpdir}/cairo/win64/cairo.exports",
     f"{tmpdir}/cairo/win64/cairo.dll"])
with open(tmpdir / "cairo/win64/cairo.exports") as raw_exports, \
     open(tmpdir / "cairo/win64/cairo.def", "x") as def_file:
    def_file.write("EXPORTS\n")
    for line in raw_exports:
        try:
            ordinal, hint, rva, name = line.split()
            int(ordinal)
            int(hint, 16)
            int(rva, 16)
        except ValueError:
            continue
        def_file.write(name + "\n")
cc.spawn(
    [lib_path, f"/DEF:{def_file.name}", "/MACHINE:x64",
     f"/OUT:{tmpdir}/cairo/win64/cairo.lib"])

# Build the wheel.
subprocess.run(
    [tmpdir / "Scripts/python.exe", "-mbuild"],
    check=True,
    cwd=Path(__file__).resolve().parents[1],
    env={
        **os.environ,
        "MPLCAIRO_NO_PYCAIRO": "1",
        "PYTHONPATH": f"{tmpdir}",
        "CL": (f"{os.environ.get('CL', '')} "
               f"/I{tmpdir}/pycairo/cairo/include "
               f"/I{tmpdir}/cairo/usr/include/cairo "
               f"/I{tmpdir}/fontconfig/usr/include "
               f"/I{tmpdir}/freetype/include "),
        "LINK": (f"{os.environ.get('LINK', '')} "
                 f"/LIBPATH:{tmpdir}/cairo/win64 "
                 f"/LIBPATH:{tmpdir}/freetype/win64 "),
    })
