"""
Fetch dependencies and build a Windows wheel
============================================

This script depends on pycairo being installed to provide cairo.dll; cairo.dll
must have been built with FreeType support.

The cairo headers (and their dependencies) are fetched from the Arch Linux
repositories (the official cairo release tarball contains unbuilt headers (e.g.
missing cairo-features.h) and is huge due to the presence of test baseline
images).  The FreeType headers and binary are fetched from the "official"
build__ listed on FreeType's website.

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


# Prepare the directories.
os.chdir(Path(__file__).resolve().parents[1])
Path("build").mkdir(exist_ok=True)
os.chdir("build")

pycairo_json = json.load(  # pycairo>=1.23 switched to static linking.
    urllib.request.urlopen("https://pypi.org/pypi/pycairo/1.22.0/json"))

urls = {
    # Download a pycairo wheel and manually unzip it, so that the build is not
    # gated on pycairo releasing a wheel for the current version of Python.
    Path("pycairo.zip"):
        max([e for e in pycairo_json["urls"]
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
    if not archive_path.exists():
        with urllib.request.urlopen(url) as request:
            archive_path.write_bytes(request.read())
    dest = archive_path.stem
    shutil.rmtree(dest, ignore_errors=True)
    shutil.unpack_archive(archive_path, dest)

win64_path = Path("cairo/win64")
win64_path.mkdir(parents=True)
shutil.copy("pycairo/cairo/include/py3cairo.h", "cairo/win64")
Path("cairo/__init__.py").write_text(
    f"def get_include(): return {repr(str(win64_path.resolve()))}")
shutil.copy("pycairo/cairo/cairo.dll", "cairo/win64")
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
with TemporaryDirectory() as tmpdir:
    dumpbin_path = Path(dest, "dumpbin")
    lib_path = Path(dest, "lib")
    cc.spawn([
        sys.executable, "-c",
        "import pathlib, shutil, sys\n"
        "for exec in ['dumpbin', 'lib']:\n"
        "    pathlib.Path(sys.argv[1], exec).write_text(shutil.which(exec))\n",
        tmpdir,
    ])
    dumpbin_path = Path(tmpdir, "dumpbin").read_text()
    lib_path = Path(tmpdir, "lib").read_text()
# Build the import library.
cc.spawn(
    [dumpbin_path, "/EXPORTS", "/OUT:cairo/win64/cairo.exports",
     "cairo/win64/cairo.dll"])
with open("cairo/win64/cairo.exports") as raw_exports, \
     open("cairo/win64/cairo.def", "x") as def_file:
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
     "/OUT:cairo/win64/cairo.lib"])

# Build the wheel.
os.chdir("..")
subprocess.run(
    [sys.executable, "-mpip", "install", "--upgrade", "pip", "wheel"],
    check=True)
os.environ.update(
    CL=(f"{os.environ.get('CL', '')} "
        f"/I{Path()}/build/cairo/usr/include/cairo "
        f"/I{Path()}/build/fontconfig/usr/include "
        f"/I{Path()}/build/freetype/include "),
    LINK=(f"{os.environ.get('LINK', '')} "
          f"/LIBPATH:{Path()}/build/cairo/win64 "
          f"/LIBPATH:{Path()}/build/freetype/win64 "),
)
subprocess.run(
    [sys.executable, "setup.py", "bdist_wheel"],
    check=True, env={**os.environ, "PYTHONPATH": "build"})
