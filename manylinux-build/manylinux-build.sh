#!/usr/bin/env bash

export MANYLINUX=1
MFLAG="-m64"
export PATH="/toolchain/bin:$PATH"
export CFLAGS="-I/toolchain/include $MFLAG"
export CXXFLAGS="-I/toolchain/include $MFLAG"
export LD_LIBRARY_PATH="/toolchain/lib64:/toolchain/lib:$LD_LIBRARY_PATH"

cd /
mkdir workdir

(
    cd workdir
    TOOLCHAIN_URL='https://github.com/Noctem/pogeo-toolchain/releases/download/v1.4/gcc-7.2-binutils-2.29-centos5-x86-64.tar.bz2'
    curl -L "$TOOLCHAIN_URL" -o toolchain.tar.bz2
    tar -C / -xf toolchain.tar.bz2
)

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

(
    cd /io/mpl_cairo
    /opt/python/cp36-cp36m/bin/python setup.py bdist_wheel
    auditwheel repair -wdist dist/*
)
