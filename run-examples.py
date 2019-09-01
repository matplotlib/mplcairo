#!/usr/bin/env python
from pathlib import Path
import runpy

import mplcairo.base  # macOS compat.
import matplotlib as mpl


SKIP = [
    "run_raqm_test_suite.py",
    "time_drawing_per_element.py",
]


def main():
    mpl.use("module://mplcairo.base")
    for path in sorted(
            (Path(__file__).resolve().parent / "examples").glob("*.py")):
        if path.name not in SKIP:
            print("Running", path)
            runpy.run_path(str(path), run_name=path.stem)  # Py3.5 compat.


if __name__ == "__main__":
    main()
