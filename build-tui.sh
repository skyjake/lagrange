#!/bin/sh
#-----------------------------------------------------------------------------
# This script builds and optionally installs the TUI version of Lagrange.
# It is assumed that 'lib/sealcurses/' exists and contains the SEALCurses
# sources. SEALCurses is compiled first as a static library that gets
# linked to clagrange instead of SDL.
#
# When not using a source tarball, you can get SEALCurses from:
# https://git.skyjake.fi/skyjake/sealcurses.git
#
# All command line arguments given to this script are passed to CMake
# for configuring the build. However, do not set CMAKE_INSTALL_PREFIX,
# because that would interfere with the SEALCurses build.
#
# You can customize the install directory prefix here and build type here:

INSTALL_PREFIX="/usr/local"
CMAKE_BUILD_TYPE="Release"

echo "\nThis script will build and optionally install clagrange with"
echo "statically linked the_Foundation and SEALCurses. First, let's configure"
echo "the build.\n"

echo "Build type? [${CMAKE_BUILD_TYPE}]"
read INPUT
if [ "${INPUT}." != "." ]; then
    CMAKE_BUILD_TYPE=${INPUT}
fi

echo "Install prefix? [${INSTALL_PREFIX}]"
read INPUT
if [ "${INPUT}." != "." ]; then
    INSTALL_PREFIX=${INPUT}
fi

if [ ! -d lib/sealcurses ]; then
    echo "'lib/sealcurses' not found. Clone with Git? [Yn]"
    read INPUT
    if [ "${INPUT}." = "n." ]; then
        echo "Build aborted."
        exit
    fi
    git clone https://git.skyjake.fi/skyjake/sealcurses.git lib/sealcurses || exit 1
fi

#-----------------------------------------------------------------------------

if [ -d build-tui ]; then
    echo "'build-tui' already exists. Delete it? [yN]"
    read CONFIRMED
    if [ "${CONFIRMED}." != "y." ] && [ "${CONFIRMED}." != "Y." ]; then
        echo "Build aborted."
        exit
    fi
    rm -rf build-tui
fi

mkdir build-tui
cd build-tui

BUILD_DIR=`pwd`

mkdir build-tfdn
cd build-tfdn

cmake ../../lib/the_Foundation -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE} \
    -DTFDN_STATIC_LIBRARY=YES \
    -DTFDN_ENABLE_WEBREQUEST=NO \
    -DTFDN_ENABLE_TESTS=NO \
    -DCMAKE_INSTALL_LIBDIR=lib \
    -DCMAKE_INSTALL_PREFIX="${BUILD_DIR}" $*
cmake --build . || exit 1
cmake --install .
cd ..

mkdir build-sealcurses
cd build-sealcurses

cmake ../../lib/sealcurses -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE} \
    -DCMAKE_C_FLAGS_RELEASE=-O1 \
    -DENABLE_SHARED=NO \
    -Dthe_Foundation_DIR="${BUILD_DIR}/lib/cmake/the_Foundation" \
    -DCMAKE_INSTALL_LIBDIR=lib \
    -DCMAKE_INSTALL_PREFIX="${BUILD_DIR}" $*
cmake --build . || exit 1
cmake --install .

cd ..
export PKG_CONFIG_PATH="${BUILD_DIR}/lib/pkgconfig":${PKG_CONFIG_PATH}
LDFLAGS="`pkg-config --static --libs the_Foundation`"
cmake .. \
    -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE} \
    -DCMAKE_EXE_LINKER_FLAGS="${LDFLAGS}" \
    -DCMAKE_INSTALL_PREFIX=${INSTALL_PREFIX} \
    -DENABLE_TUI=YES \
    -DENABLE_MPG123=NO \
    -DENABLE_WEBP=NO \
    -DENABLE_FRIBIDI=NO \
    -DENABLE_HARFBUZZ=NO \
    -DENABLE_POPUP_MENUS=NO \
    -DENABLE_IDLE_SLEEP=NO \
    -Dthe_Foundation_DIR="${BUILD_DIR}/lib/cmake/the_Foundation" \
    $*
cmake --build . || exit 1

echo "-----"
echo "clagrange and resources.lgr can be found in 'build-tui'."
echo "Do you want to install them to ${INSTALL_PREFIX}? (s=sudo) [syN]"
read CONFIRMED
if [ "${CONFIRMED}" = "s" ]; then
    sudo cmake --install .
    exit
fi
if [ "${CONFIRMED}" = "y" ]; then
     cmake --install .
    exit
fi
