#!/bin/bash

# Build script for the manylinux wheel.  Depends on git and docker.
# Set the PY_VERS environment variable to a space-separated list of dotted
# Python versions (e.g. '3.5 3.6 3.7') to build the wheels only for these
# versions.

set -eo pipefail
set -x

if [[ "$MPLCAIRO_BUILD_TYPE" != manylinux ]]; then
    toplevel="$(git -C "$(dirname "$0")" rev-parse --show-toplevel)"

    tmpdir="$(mktemp -d)"
    trap 'rm -rf "$tmpdir"' EXIT INT TERM
    git clone "$toplevel" "$tmpdir/mplcairo"
    # Apparently realpath --relative-to is too recent for travis...
    docker run \
        -e MPLCAIRO_BUILD_TYPE=manylinux -e PY_VERS="${PY_VERS:-3.5 3.6 3.7}" \
        --mount type=bind,source="$tmpdir/mplcairo",target=/io/mplcairo \
        quay.io/pypa/manylinux1_x86_64 \
        "/io/mplcairo/$(python -c 'import os, sys; print(os.path.relpath(*map(os.path.realpath, sys.argv[1:])))' "$0" "$toplevel")"

    user="${SUDO_USER:-$USER}"
    chown "$user:$(id -gn "$user")" -R "$tmpdir/mplcairo/build"
    mkdir -p "$toplevel/dist"
    mv "$tmpdir/mplcairo/dist/"*-manylinux*.whl "$toplevel/dist"

else

    export MFLAG=-m64
    export PATH="/toolchain/bin:$PATH"
    export CFLAGS="-I/toolchain/include $MFLAG"
    export CXXFLAGS="-I/toolchain/include $MFLAG"
    export LD_LIBRARY_PATH="/toolchain/lib64:/toolchain/lib:$LD_LIBRARY_PATH"

    cd /
    mkdir workdir

    echo 'Setting up gcc.'
    (
        cd workdir
        curl -L https://github.com/Noctem/pogeo-toolchain/releases/download/v1.5/gcc-7.3-centos5-x86-64.tar.bz2 -o toolchain.tar.bz2
        tar -C / -xf toolchain.tar.bz2
    )

    echo 'Setting up xz.'
    yum install -y xz

    echo 'Setting up headers for dependencies.'
    (
        cd workdir
        for dep in cairo fontconfig freetype2 python-cairo; do
            mkdir "$dep"
            # In tar, ignore "ignoring unknown extended header keyword" warning.
            curl -L "https://www.archlinux.org/packages/extra/x86_64/$dep/download" |
                xz -cd - |
                (tar -C "$dep" -xf - 2>/dev/null || true)
            mv "$dep/usr/include/"* /usr/include
        done
        # Provide a shim to access pycairo's header.
        mv "$(find python-cairo -name py3cairo.h)" /usr/include
    )

    for PY_VER in $PY_VERS; do
        PY_VER_ABI_TAG="cp${PY_VER/./}-cp${PY_VER/./}m"
        PY_PREFIX="/opt/python/$PY_VER_ABI_TAG"
        echo "Building the wheel for Python $PY_VER."
        # Provide a shim to access pycairo's header.
        echo 'def get_include(): return "/dev/null"' \
            >"$PY_PREFIX/lib/python$PY_VER/site-packages/cairo.py"
        (
            cd /io/mplcairo
            "$PY_PREFIX/bin/pip" install pybind11
            # Force a rebuild of the extension.
            "$PY_PREFIX/bin/python" setup.py bdist_wheel
            auditwheel -v repair -wdist \
                "dist/mplcairo-$("$PY_PREFIX/bin/python" setup.py --version)-$PY_VER_ABI_TAG-"*".whl"
        )
    done
fi
