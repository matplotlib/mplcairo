#!/bin/bash

# Build script for the macOS wheel.  Depends on git, python, llvm, and cairo,
# e.g. from Homebrew.

set -eo pipefail
set -x

toplevel="$(git -C "$(dirname "$0")" rev-parse --show-toplevel)"

tmpenv="$(mktemp -d)"
trap 'rm -rf "$tmpenv"' EXIT INT TERM
python -mvenv "$tmpenv"

(
    source "$tmpenv/bin/activate"
    python -mpip install --upgrade pip build delocate setuptools_scm

    cd "$toplevel"
    python -mbuild
    python -mdelocate.cmd.delocate_wheel -v dist/*.whl
)
