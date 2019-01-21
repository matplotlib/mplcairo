#!/usr/bin/env python

# Ensure that the current Matplotlib install includes test data.

from pathlib import Path
import shutil
from tempfile import TemporaryDirectory
import urllib.request

import matplotlib as mpl
import mpl_toolkits


try:
    import matplotlib.tests
except Exception:  # ImportError if patched out, IOError by mpl itself.
    print("Fetching test data for Matplotlib {}.".format(mpl.__version__))
    with TemporaryDirectory() as tmpdir:
        with urllib.request.urlopen(
                "https://github.com/matplotlib/matplotlib/archive/v{}.tar.gz"
                .format(mpl.__version__)) as request, \
             Path(tmpdir, "matplotlib.tar.gz").open("wb") as file:
            file.write(request.read())
        shutil.unpack_archive(file.name, tmpdir)
        for pkg in [mpl, mpl_toolkits]:
            shutil.rmtree(  # Py3.5 compat.
                str(Path(list(pkg.__path__)[0], "tests")), ignore_errors=True)
            shutil.move(
                str(Path(tmpdir, "matplotlib-{}".format(mpl.__version__),
                         "lib", pkg.__name__, "tests")),
                list(pkg.__path__)[0])
else:
    print("Matplotlib test data already present.".format(mpl.__version__))
