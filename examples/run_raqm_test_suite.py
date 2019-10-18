"""Run a Raqm test suite entry."""

from argparse import ArgumentParser
import ast
from pathlib import Path
import sys

from matplotlib import pyplot as plt
from matplotlib.font_manager import FontProperties


def main():
    parser = ArgumentParser()
    parser.add_argument(
        "test", type=Path, help="path to a test file ('*.test') in the Raqm "
        "source tree (under libraqm/tests)")
    args = parser.parse_args()

    with args.test.open() as file:
        lines = file.read().splitlines()

    if "," in lines[0]:
        sys.exit("Multi-font strings are not supported.")

    font = args.test.parent / lines[0]
    text = ast.literal_eval(f"'{lines[1]}'")

    fig, ax = plt.subplots()
    ax.set_axis_off()
    ax.axvline(.5, alpha=.5, lw=.5)
    ax.axhline(.5, alpha=.5, lw=.5)
    ax.text(.5, .5, text, fontproperties=FontProperties(fname=font))
    plt.show()


if __name__ == "__main__":
    main()
