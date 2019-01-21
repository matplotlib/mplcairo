# Fetch dependencies and build a Windows wheel
# ============================================
#
# This script depends on cairocffi being installed to provide cairo.dll.  Note
# that in practice, the only cairocffi build whose cairo.dll includes FreeType
# support that I am aware of is Christoph Gohlke's build__, for which I have
# set up a mirror__.  (The only other freestanding cairo.dll that includes
# FreeType support that I am aware of__ appears to misrender pdfs.)
#
# __ https://www.lfd.uci.edu/~gohlke/pythonlibs/#cairocffi
# __ https://github.com/anntzer/cairocffi-windows-wheel
# __ https://github.com/preshing/cairo-windows
# __ https://preshing.com/20170529/heres-a-standalone-cairo-dll-for-windows/#IDComment1047546463
#
# The cairo headers (and their dependencies) are fetched from the Arch Linux
# repositories (the official cairo release tarball contains unbuilt headers
# (e.g. missing cairo-features.h) and is huge due to the presence of test
# baseline images).  The FreeType headers and binary are fetched from the
# "official" build__ listed on FreeType's website.
#
# __ https://github.com/ubawurinna/freetype-windows-binaries

from distutils import ccompiler
import os
from pathlib import Path
import shutil
import subprocess
import sys
import urllib.request

import cairocffi


# Prepare the directories.
os.chdir(Path(__file__).parents[1])
Path("build").mkdir(exist_ok=True)

# Download the cairo headers from Arch Linux (<1Mb, vs >40Mb for the official
# tarball, which contains baseline images), and the "official" FreeType build.
os.chdir("build")
urls = {
    Path("cairo.txz"):
        "https://www.archlinux.org/packages/extra/x86_64/cairo/download",
    Path("fontconfig.txz"):
        "https://www.archlinux.org/packages/extra/x86_64/fontconfig/download",
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
    shutil.unpack_archive(str(archive_path), dest)  # Py3.5 compat.

# Get cairo.dll from cairocffi, and build the import library.
# cffi appends ".lib" to the filename.
Path("cairo/win64").mkdir(parents=True)
shutil.copyfile(
    Path(cairocffi.cairo.__name__).with_suffix(""),
    "cairo/win64/cairo.dll")
# Build the import library.
cc = ccompiler.new_compiler()
cc.initialize()
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
    [sys.executable, "-mpip", "install", "--upgrade",
     "pip", "wheel", "pybind11"],
    check=True)
os.environ.update(
    CL=(f"/I{Path()}/build/cairo/usr/include/cairo "
        f"/I{Path()}/build/fontconfig/usr/include "
        f"/I{Path()}/build/freetype/include "),
    LINK=(f"/LIBPATH:{Path()}/build/cairo/win64 "
          f"/LIBPATH:{Path()}/build/freetype/win64 "),
    MPLCAIRO_BUILD_TYPE="package",
)
subprocess.run(
    [sys.executable, "setup.py", "bdist_wheel"],
    check=True)
