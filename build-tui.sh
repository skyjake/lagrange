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

#-----------------------------------------------------------------------------

if [ -d build-tui ]; then
    read -p "'build-tui' already exists. Delete it? [Yn] " CONFIRMED
    if [ "${CONFIRMED}" != "y" ] && [ "${CONFIRMED}" != "Y" ]; then
        echo "Build aborted."
        exit
    fi
    rm -rf build-tui
fi

mkdir build-tui
cd build-tui

mkdir build-sealcurses
cd build-sealcurses

cmake ../../lib/sealcurses -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE} -DENABLE_SHARED=NO \
    -DCMAKE_INSTALL_PREFIX="`pwd`/.." $*
cmake --build .
cmake --install .

cd ..
export PKG_CONFIG_PATH="`pwd`/lib/pkgconfig":${PKG_CONFIG_PATH}
cmake .. \
    -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE} \
    -DCMAKE_INSTALL_PREFIX=${INSTALL_PREFIX} \
    -DENABLE_TUI=YES \
    -DENABLE_MPG123=NO \
    -DENABLE_WEBP=NO \
    -DENABLE_FRIBIDI=NO \
    -DENABLE_HARFBUZZ=NO \
    -DENABLE_POPUP_MENUS=NO \
    -DENABLE_IDLE_SLEEP=NO \
    $*
cmake --build .

echo "-----"
echo "clagrange and resources.lgr can be found in 'build-tui'."
read -p "Do you want to install them to ${INSTALL_PREFIX}? [yN] " CONFIRMED
if [ "${CONFIRMED}" == "y" ]; then
    cmake --install .
    exit
fi
