#!/usr/bin/env bash

export MANYLINUX=1
export MFLAG=-m64
export PATH="/toolchain/bin:$PATH"
export CFLAGS="-I/toolchain/include $MFLAG"
export CXXFLAGS="-I/toolchain/include $MFLAG"
export LD_LIBRARY_PATH="/toolchain/lib64:/toolchain/lib:$LD_LIBRARY_PATH"
export PY_PREFIX=/opt/python/cp36-cp36m/bin

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
for dep in cairo fontconfig freetype2; do
(
    cd workdir
    mkdir "$dep"
    curl -L "https://www.archlinux.org/packages/extra/x86_64/$dep/download" |
    xz -cd - |
    tar -C "$dep" -xf -
    mv "$dep/usr/include/"* /usr/include
)
done

echo 'Building the wheel.'
(
    cd /io/mpl_cairo
    "$PY_PREFIX/pip" install pybind11
    "$PY_PREFIX/python" setup.py bdist_wheel
    auditwheel repair -wdist dist/mpl_cairo-"$("$PY_PREFIX/python" setup.py --version)"-*
)
