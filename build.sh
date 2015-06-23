#!/bin/bash

set -e

DIR=$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )
TOOLCHAIN="${DIR}/../mipsel-toolchain"
ROOTFS="${DIR}/../rootfs"
GWLIB="${DIR}/../gwlib"
SQLITE="${DIR}/../sqlite"
_BUILD="${DIR}/_build"
_BUILD_ROOTFS="${_BUILD}/rootfs"

rm -rf ${_BUILD}
git reset
git checkout .
git clean -xdf

if [ ! -e ${_BUILD} ]; then
    mkdir -p "${_BUILD}"
fi

./configure
make python Parser/pgen
mv python hostpython
mv Parser/pgen Parser/hostpgen
make distclean

cp -r "${ROOTFS}" "${_BUILD}"
(cd "${GWLIB}" && cp *.so* *.a *.la "${_BUILD_ROOTFS}/lib")
(cd "${GWLIB}/include" && cp -r * "${_BUILD_ROOTFS}/include")
(cd "${SQLITE}" && cp *.h "${_BUILD_ROOTFS}/include")
rm -rf "${_BUILD_ROOTFS}/include/openssl"

patch -p1 < Python-2.7.3-xcompile.patch

export PYTHON_XCOMPILE_DEPENDENCIES_PREFIX="${_BUILD_ROOTFS}"
CC="${TOOLCHAIN}/bin/mipsel-linux-gcc" CXX="${TOOLCHAIN}/bin/mipsel-linux-g++" AR="${TOOLCHAIN}/bin/mipsel-linux-ar" RANLIB="${TOOLCHAIN}/bin/mipsel-linux-ranlib" PYTHON_XCOMPILE_DEPENDENCIES_PREFIX="${_BUILD_ROOTFS}" ./configure --host=mipsel-linux --build=x86_64-linux-gnu
make HOSTPYTHON=./hostpython HOSTPGEN=./Parser/hostpgen BLDSHARED="${TOOLCHAIN}/bin/mipsel-linux-gcc -shared" CROSS_COMPILE="${TOOLCHAIN}/bin/mipsel-linux-" CROSS_COMPILE_TARGET=yes HOSTARCH=mipsel-linux BUILDARCH=x86_64-linux-gnu PYTHON_XCOMPILE_DEPENDENCIES_PREFIX="${_BUILD_ROOTFS}"
make install HOSTPYTHON=./hostpython BLDSHARED="${TOOLCHAIN}/bin/mipsel-linux-gcc -shared" CROSS_COMPILE="${TOOLCHAIN}/bin/mipsel-linux-" CROSS_COMPILE_TARGET=yes prefix="${_BUILD}/python" PYTHON_XCOMPILE_DEPENDENCIES_PREFIX="${_BUILD_ROOTFS}"
