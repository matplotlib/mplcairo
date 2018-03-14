#!/usr/bin/env python
from argparse import ArgumentDefaultsHelpFormatter, ArgumentParser
from pathlib import Path
import urllib.request


def main():
    parser = ArgumentParser(formatter_class=ArgumentDefaultsHelpFormatter)
    parser.add_argument("--tag", default="v0.5.0", help="raqm tag to download")
    args = parser.parse_args()
    request = urllib.request.urlopen(
        "https://raw.githubusercontent.com/HOST-Oman/libraqm/{}/src/raqm.h"
        .format(args.tag))
    with (Path(__file__).parents[1] / "include/raqm.h").open("xb") as file:
        file.write(request.read())


if __name__ == "__main__":
    main()
