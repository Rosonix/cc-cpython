#!/bin/bash

set -e

./configure
make Parser/pgen Programs/_freeze_importlib
mv Parser/pgen Parser/hostpgen
mv Programs/_freeze_importlib Programs/_host_freeze_importlib
make clean

ROOT_DIR=$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )
BUILD_DIR="${ROOT_DIR}/build"
TOOLCHAIN="${ROOT_DIR}/../mipsel-toolchain"
ROOTFS="${ROOT_DIR}/../rootfs"
OUT_DIR="${ROOTFS}/usr"

export CROSS_COMPILE=mipsel-linux

export CC="${TOOLCHAIN}/bin/${CROSS_COMPILE}-gcc"
export CXX="${TOOLCHAIN}/bin/${CROSS_COMPILE}-g++"
export AR="${TOOLCHAIN}/bin/${CROSS_COMPILE}-ar"
export AS="${TOOLCHAIN}/bin/${CROSS_COMPILE}-as"
export LD="${TOOLCHAIN}/bin/${CROSS_COMPILE}-ld"
export NM="${TOOLCHAIN}/bin/${CROSS_COMPILE}-nm"
export RANLIB="${TOOLCHAIN}/bin/${CROSS_COMPILE}-ranlib"
export READELF="${TOOLCHAIN}/bin/${CROSS_COMPILE}-readelf"

rm -rf "${BUILD_DIR}"
mkdir "${BUILD_DIR}"
cp -r "${ROOTFS}/"* "${BUILD_DIR}"

CONFIG_SITE=config.site ./configure --host=mipsel-linux --build=x86_64-linux-gnu --disable-ipv6

export PYTHON_XCOMPILE_DEPENDENCIES_PREFIX="${BUILD_DIR}"

touch Include/graminit.h

make install BLDSHARED="${TOOLCHAIN}/bin/mipsel-linux-gcc -shared" CROSS_COMPILE="${TOOLCHAIN}/bin/mipsel-linux-" prefix="${OUT_DIR}" ENSUREPIP=no
