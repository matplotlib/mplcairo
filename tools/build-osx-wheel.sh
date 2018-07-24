#!/bin/bash

# Build script for the OSX wheel.  Depends on git, python, llvm, and cairo,
# e.g. from Homebrew.

set -eo pipefail
set -x

toplevel="$(git -C "$(dirname "$0")" rev-parse --show-toplevel)"

tmpdir="$(mktemp -d)"
trap 'rm -rf "$tmpdir"' EXIT INT TERM
git clone "$toplevel" "$tmpdir/mplcairo"

tmpenv="$(mktemp -d)"
trap 'rm -rf "$tmpenv"' EXIT INT TERM
python -mvenv "$tmpenv"

(
    source "$tmpenv/bin/activate"
    python -mpip install --upgrade pip
    python -mpip install --upgrade wheel delocate
    cd "$toplevel"

    # Just get the dependencies.
    pip install .
    pip uninstall -y mplcairo

    python setup.py bdist_wheel
    delocate-wheel -v dist/*
)
