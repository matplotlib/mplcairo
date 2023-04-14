#!/usr/bin/env python

# Ensure that the current Matplotlib install includes test data.

import importlib
from pathlib import Path
import shutil
from tempfile import TemporaryDirectory
import urllib.request

import matplotlib as mpl


try:
    import matplotlib.tests
except Exception:  # ImportError if patched out, IOError by mpl itself.
    print(f"Fetching test data for Matplotlib {mpl.__version__}.")
    with TemporaryDirectory() as tmpdir:
        with urllib.request.urlopen(
                f"https://github.com/matplotlib/matplotlib/"
                f"archive/v{mpl.__version__}.tar.gz") as request, \
             Path(tmpdir, "matplotlib.tar.gz").open("wb") as file:
            file.write(request.read())
        shutil.unpack_archive(file.name, tmpdir)
        libdir = Path(tmpdir, f"matplotlib-{mpl.__version__}", "lib")
        for testdir in libdir.glob("**/tests"):
            pkgdir = testdir.relative_to(libdir).parent  # Drop "tests".
            pkgpath = list(
                importlib.import_module(pkgdir.as_posix().replace("/", "."))
                .__path__)[0]
            shutil.rmtree(Path(pkgpath, "tests"), ignore_errors=True)
            shutil.move(str(testdir), pkgpath)  # bpo#32689 (Py<3.9).
else:
    print("Matplotlib test data already present.")
