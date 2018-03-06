#!/bin/bash
set -eo pipefail

if [[ ! "$MANYLINUX" ]]; then
    toplevel="$(git -C "$(dirname "$0")" rev-parse --show-toplevel)"

    tmpdir="$(mktemp -d)"
    echo "$tmpdir"
    trap 'rm -rf "$tmpdir"' EXIT INT TERM
    git clone "$toplevel" "$tmpdir/mplcairo"

    docker run -it \
        -e MANYLINUX=1 \
        --mount type=bind,source="$tmpdir/mplcairo",target=/io/mplcairo \
        quay.io/pypa/manylinux1_x86_64 \
        "/io/mplcairo/$(realpath --relative-to="$toplevel" "$0")"

    user="${SUDO_USER:-$USER}"
    chown "$user:$(id -gn "$user")" -R "$tmpdir/mplcairo/build"
    mkdir -p "$toplevel/dist"
    mv "$tmpdir/mplcairo/dist/"*.whl "$toplevel/dist"

else

    export MFLAG=-m64
    export PATH="/toolchain/bin:$PATH"
    export CFLAGS="-I/toolchain/include $MFLAG"
    export CXXFLAGS="-I/toolchain/include $MFLAG"
    export LD_LIBRARY_PATH="/toolchain/lib64:/toolchain/lib:$LD_LIBRARY_PATH"
    export PY_MAJOR=3
    export PY_MINOR=6
    export PY_PREFIX="/opt/python/cp${PY_MAJOR}${PY_MINOR}-cp$PY_MAJOR${PY_MINOR}m"

    cd /
    mkdir workdir

    echo 'Setting up gcc.'
    (
        cd workdir
        curl -L https://github.com/Noctem/pogeo-toolchain/releases/download/v1.4/gcc-7.2-binutils-2.29-centos5-x86-64.tar.bz2 -o toolchain.tar.bz2
        tar -C / -xf toolchain.tar.bz2
    )

    echo 'Setting up xz.'
    yum install -y xz

    echo 'Setting up headers for dependencies.'
    (
        cd workdir
        for dep in cairo fontconfig freetype2 python-cairo; do
            mkdir "$dep"
            curl -L "https://www.archlinux.org/packages/extra/x86_64/$dep/download" |
                xz -cd - |
                (tar -C "$dep" -xf - || true)  # Ignoring unknown extended header keyword.
            mv "$dep/usr/include/"* /usr/include
        done
        # Provide a shim to access pycairo's header.
        mv "$(find python-cairo -name py3cairo.h)" /usr/include
        echo 'def get_include(): return "/dev/null"' \
            >"$PY_PREFIX/lib/python$PY_MAJOR.$PY_MINOR/site-packages/cairo.py"
    )

    echo 'Building the wheel.'
    (
        cd /io/mplcairo
        "$PY_PREFIX/bin/pip" install pybind11
        # Force a rebuild of the extension.
        "$PY_PREFIX/bin/python" setup.py bdist_wheel
        auditwheel repair -wdist \
            dist/mplcairo-"$("$PY_PREFIX/bin/python" setup.py --version)"-*
    )

fi
