#!/bin/bash

# Build script for the manylinux wheel.  Depends on git and docker/podman
# (set via $DOCKER).  Set $PY_VERS to a space-separated list of dotted Python
# versions ('3.x 3.y') to build the wheels only for these versions.

set -eo pipefail
set -x
shopt -s nullglob

if [[ "$MPLCAIRO_MANYLINUX" != 1 ]]; then
    toplevel="$(git -C "$(dirname "$0")" rev-parse --show-toplevel)"

    tmpdir="$(mktemp -d)"
    trap 'rm -rf "$tmpdir"' EXIT INT TERM
    git clone "$toplevel" "$tmpdir/mplcairo"
    relpath="$(
        python -c 'import os, sys; print(os.path.relpath(*map(os.path.realpath, sys.argv[1:])))' \
            "$0" "$toplevel")"
    ${DOCKER:-docker} run \
        -e MPLCAIRO_MANYLINUX=1 -e PY_VERS="${PY_VERS:-3.7 3.8 3.9}" \
        --volume "$tmpdir/mplcairo":/io/mplcairo:Z \
        quay.io/pypa/manylinux2010_x86_64 \
        "/io/mplcairo/$relpath"

    user="${SUDO_USER:-$USER}"
    chown "$user:$(id -gn "$user")" -R "$tmpdir/mplcairo/build"
    mkdir -p "$toplevel/dist"
    mv "$tmpdir/mplcairo/dist/"*-manylinux*.whl "$toplevel/dist"

else

    echo 'Setting up headers for dependencies.'
    (
        yum install -y xz
        mkdir /workdir
        cd /workdir
        # Use the last versions before Arch switched to zstd.
        filenames=(
            cairo-1.17.2%2B17%2Bg52a7c79fd-2-x86_64.pkg.tar.xz
            fontconfig-2%3A2.13.91%2B24%2Bg75eadca-1-x86_64.pkg.tar.xz
            freetype2-2.10.1-1-x86_64.pkg.tar.xz
            python-cairo-1.18.2-3-x86_64.pkg.tar.xz
        )
        for filename in "${filenames[@]}"; do
            name="$(rev <<<"$filename" | cut -d- -f4- | rev)"
            mkdir "$name"
            # In tar, ignore "ignoring unknown extended header keyword" warning.
            curl -L "https://archive.org/download/archlinux_pkg_$name/$filename" |
                xz -cd - |
                (tar -C "$name" -xf - 2>/dev/null || true)
            mv "$name/usr/include/"* /usr/include
        done
        # Provide a shim to access pycairo's header.
        mv "$(find python-cairo -name py3cairo.h)" /usr/include
    )

    for py_ver in $PY_VERS; do
        py_prefix=("/opt/python/cp${py_ver/./}-"*)
        tags="$(basename "$py_prefix")"
        echo "Building the wheel for Python $py_ver."
        # Provide a shim to access pycairo's header.
        echo 'def get_include(): return "/dev/null"' \
            >"$py_prefix/lib/python$py_ver/site-packages/cairo.py"
        (
            cd /io/mplcairo
            # Force a rebuild of the extension.
            "$py_prefix/bin/python" setup.py bdist_wheel
            mplcairo_version="$("$py_prefix/bin/python" setup.py --version)"
            for wheel in "dist/mplcairo-$mplcairo_version-$tags-"*".whl"; do
                AUDITWHEEL_PLAT= auditwheel -v repair -wdist "$wheel"
                rm "$wheel"
            done
        )
    done
fi
