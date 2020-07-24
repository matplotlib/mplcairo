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

from ctypes import (
    c_bool, c_char_p, c_ulong, c_void_p, c_wchar_p, POINTER,
    byref, create_unicode_buffer, sizeof, windll)
import os
from pathlib import Path
import shutil
import subprocess
import sys
import urllib.request

import cairo  # Needed to load the cairo dll.
import setuptools


def enum_process_modules(func_name=None):
    k32 = windll.kernel32
    psapi = windll.psapi
    k32.GetCurrentProcess.restype = c_void_p
    k32.GetModuleFileNameW.argtypes = [c_void_p, c_wchar_p, c_ulong]
    k32.GetModuleFileNameW.restype = c_ulong
    k32.GetProcAddress.argtypes = [c_void_p, c_char_p]
    k32.GetProcAddress.restypes = c_void_p
    psapi.EnumProcessModules.argtypes = [
        c_void_p, POINTER(c_void_p), c_ulong, POINTER(c_ulong)]
    psapi.EnumProcessModules.restype = c_bool

    process = k32.GetCurrentProcess()
    needed = c_ulong()
    psapi.EnumProcessModules(process, None, 0, byref(needed))
    modules = (c_void_p * (needed.value // sizeof(c_void_p)))()
    if not psapi.EnumProcessModules(
            process, modules, sizeof(modules), byref(needed)):
        raise OSError("Failed to enumerate process modules")
    path = create_unicode_buffer(1024)
    for module in modules:
        if func_name is None or k32.GetProcAddress(module, func_name):
            k32.GetModuleFileNameW(module, path, len(path))
            yield path.value


# Prepare the directories.
os.chdir(Path(__file__).resolve().parents[1])
Path("build").mkdir(exist_ok=True)

# Download the cairo headers from Arch Linux (<1Mb, vs >40Mb for the official
# tarball, which contains baseline images) from before Arch switched to zstd,
# and the "official" FreeType build.
os.chdir("build")
urls = {
    Path("cairo.txz"):
        "https://archive.org/download/archlinux_pkg_cairo/"
        "cairo-1.17.2%2B17%2Bg52a7c79fd-2-x86_64.pkg.tar.xz",
    Path("fontconfig.txz"):
        "https://archive.org/download/archlinux_pkg_fontconfig/"
        "fontconfig-2%3A2.13.91%2B24%2Bg75eadca-1-x86_64.pkg.tar.xz",
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

# Get cairo.dll (normally loaded by pycairo), checking that it include
# FreeType support.
Path("cairo/win64").mkdir(parents=True)
cairo_dll, = enum_process_modules(b"cairo_ft_font_face_create_for_ft_face")
shutil.copyfile(cairo_dll, "cairo/win64/cairo.dll")
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
# Build the import library.
cc.spawn(
    ["dumpbin", "/EXPORTS", "/OUT:cairo/win64/cairo.exports",
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
    ["lib", f"/DEF:{def_file.name}", "/MACHINE:x64",
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
    check=True)
