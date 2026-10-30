#!/bin/bash
# Builds Lagrange's iOS dependencies (arm64) for the device or the simulator.
#
# Usage: ios-deps.sh os|simulator [step...]
#
# Sources are expected in IOS_SRC (default: ~/src/libs); OpenSSL is downloaded
# there. The libraries are installed in ~/SDK/ios/arm64 (os) or
# ~/SDK/ios-sim/arm64 (simulator).

set -e

SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
OSMINVER=15.0
OPENSSL_VER=3.5.9
ALL_STEPS="openssl iconv unistring pcre foundation webp jxl sdl3"

case "$1" in
    os)
        KIND=iphoneos
        PREFIX=$HOME/SDK/ios/arm64
        SUFFIX=ios
        MINFLAG=-mios-version-min
        TRIPLE=arm64-apple-ios$OSMINVER
        OPENSSL_TARGET=ios64-xcrun
        ;;
    simulator)
        KIND=iphonesimulator
        PREFIX=$HOME/SDK/ios-sim/arm64
        SUFFIX=ios-sim
        MINFLAG=-mios-simulator-version-min
        TRIPLE=arm64-apple-ios$OSMINVER-simulator
        OPENSSL_TARGET=iossimulator-arm64-xcrun
        ;;
    *)
        echo "Usage: ios-deps.sh os|simulator [step...]"
        echo "Steps: $ALL_STEPS"
        exit 1
        ;;
esac
shift
STEPS=${*:-$ALL_STEPS}

SRC_LIBS=${IOS_SRC:-$HOME/src/libs}
DIR_ICONV=$SRC_LIBS/libiconv
DIR_UNISTRING=$SRC_LIBS/libunistring
DIR_PCRE=$SRC_LIBS/pcre-8.44
DIR_FOUNDATION=${TFDN_SRC:-$HOME/src/the_Foundation}
DIR_WEBP=$SRC_LIBS/libwebp
DIR_JXL=$SRC_LIBS/libjxl
DIR_SDL3=$SRC_LIBS/SDL3
BUILD_ROOT=$SRC_LIBS/build-$SUFFIX

SDKROOT=$(xcrun --sdk $KIND --show-sdk-path)
ARCHFLAGS="-arch arm64 -isysroot $SDKROOT $MINFLAG=$OSMINVER"
JOBS=${JOBS:-$(sysctl -n hw.ncpu)}

echo "SDK:    $KIND ($SDKROOT)"
echo "Prefix: $PREFIX"
echo "Steps:  $STEPS"
if [ -t 0 ]; then
    read -p "--- Press Enter to begin ---"
fi

unset MAKEFLAGS CFLAGS CXXFLAGS CPPFLAGS LDFLAGS
mkdir -p $PREFIX $BUILD_ROOT

function fresh_build_dir() {
    rm -rf $BUILD_ROOT/$1
    mkdir -p $BUILD_ROOT/$1
    cd $BUILD_ROOT/$1
}

function autotools_build() {
    cd $1
    shift
    if [ -f Makefile ]; then
        make clean
    fi
    # Differing build/host triples force cross-compilation mode.
    CC="xcrun --sdk $KIND clang" \
    CXX="xcrun --sdk $KIND clang++" \
    CFLAGS="$ARCHFLAGS" \
    CXXFLAGS="$ARCHFLAGS" \
    CPPFLAGS="-I$PREFIX/include" \
    LDFLAGS="$ARCHFLAGS -L$PREFIX/lib" \
    ./configure \
        --prefix=$PREFIX \
        --build=aarch64-apple-darwin$(uname -r) \
        --host=aarch64-apple-darwin \
        --enable-static \
        --disable-shared \
        "$@"
    make -j$JOBS
    make install
}

function cmake_ios() {
    cmake \
        -DCMAKE_SYSTEM_NAME=iOS \
        -DCMAKE_OSX_SYSROOT=$KIND \
        -DCMAKE_OSX_ARCHITECTURES=arm64 \
        -DCMAKE_OSX_DEPLOYMENT_TARGET=$OSMINVER \
        -DCMAKE_INSTALL_PREFIX=$PREFIX \
        -DCMAKE_POLICY_VERSION_MINIMUM=3.5 \
        "$@"
}

function build_openssl() {
    local tarball=$SRC_LIBS/openssl-$OPENSSL_VER.tar.gz
    if [ ! -f $tarball ]; then
        curl -fL -o $tarball https://github.com/openssl/openssl/releases/download/openssl-$OPENSSL_VER/openssl-$OPENSSL_VER.tar.gz
    fi
    fresh_build_dir openssl
    tar xzf $tarball --strip-components=1
    # Configure only accepts single-token compiler flags.
    ./Configure $OPENSSL_TARGET \
        no-shared no-tests no-apps no-docs \
        --target=$TRIPLE \
        --prefix=$PREFIX \
        --libdir=lib
    make -j$JOBS build_libs
    rm -rf $PREFIX/include/openssl
    make install_dev
}

function build_foundation() {
    fresh_build_dir the_Foundation
    for config in Debug RelWithDebInfo; do
        cmake_ios $DIR_FOUNDATION \
            -DCMAKE_BUILD_TYPE=$config \
            -DTFDN_STATIC_LIBRARY=YES \
            -DTFDN_ENABLE_TESTS=NO \
            -DTFDN_ENABLE_WEBREQUEST=NO \
            -DTFDN_ENABLE_DEBUG_OUTPUT=NO \
            -DIOS_DIR=$PREFIX
        make -j$JOBS install
    done
}

function build_webp() {
    fresh_build_dir libwebp
    cmake_ios $DIR_WEBP \
        -DCMAKE_BUILD_TYPE=Release \
        -DBUILD_SHARED_LIBS=OFF \
        -DWEBP_BUILD_ANIM_UTILS=OFF \
        -DWEBP_BUILD_CWEBP=OFF \
        -DWEBP_BUILD_DWEBP=OFF \
        -DWEBP_BUILD_GIF2WEBP=OFF \
        -DWEBP_BUILD_IMG2WEBP=OFF \
        -DWEBP_BUILD_VWEBP=OFF \
        -DWEBP_BUILD_WEBPINFO=OFF \
        -DWEBP_BUILD_LIBWEBPMUX=OFF \
        -DWEBP_BUILD_WEBPMUX=OFF \
        -DWEBP_BUILD_EXTRAS=OFF
    make -j$JOBS install
}

function build_jxl() {
    fresh_build_dir libjxl
    cmake_ios $DIR_JXL \
        -DCMAKE_BUILD_TYPE=Release \
        -DBUILD_SHARED_LIBS=OFF \
        -DBUILD_TESTING=OFF \
        -DCMAKE_MACOSX_BUNDLE=OFF \
        -DJPEGXL_ENABLE_TOOLS=OFF \
        -DJPEGXL_ENABLE_DEVTOOLS=OFF \
        -DJPEGXL_ENABLE_JPEGLI=OFF \
        -DJPEGXL_ENABLE_DOXYGEN=OFF \
        -DJPEGXL_ENABLE_MANPAGES=OFF \
        -DJPEGXL_ENABLE_BENCHMARK=OFF \
        -DJPEGXL_ENABLE_EXAMPLES=OFF \
        -DJPEGXL_ENABLE_JNI=OFF \
        -DJPEGXL_ENABLE_SJPEG=OFF \
        -DJPEGXL_ENABLE_OPENEXR=OFF \
        -DJPEGXL_BUNDLE_LIBPNG=OFF
    make -j$JOBS install
}

function build_sdl3() {
    fresh_build_dir SDL3
    cmake_ios $DIR_SDL3 -G Xcode \
        -DSDL_SHARED=ON \
        -DSDL_STATIC=OFF \
        -DSDL_CAMERA=OFF \
        -DSDL_HIDAPI=OFF \
        -DSDL_RPATH=OFF \
        -DSDL_TESTS=OFF \
        -DSDL_TEST_LIBRARY=OFF \
        -DSDL_EXAMPLES=OFF \
        -DSDL_UNINSTALL=OFF
    cmake --build . --config RelWithDebInfo -- -quiet
    cmake --install . --config RelWithDebInfo
    rm -rf $PREFIX/lib/libSDL3.0.dylib.dSYM
    cp -R RelWithDebInfo-$KIND/libSDL3.0.dylib.dSYM $PREFIX/lib/
    $SCRIPT_DIR/package-sdl3-ios-framework.sh $PREFIX $DIR_SDL3
}

for step in $STEPS; do
    echo "=== $step ==="
    case $step in
        openssl)    build_openssl ;;
        iconv)      autotools_build $DIR_ICONV --enable-extra-encodings ;;
        unistring)  autotools_build $DIR_UNISTRING --disable-namespacing --with-libiconv-prefix=$PREFIX ;;
        pcre)       autotools_build $DIR_PCRE --enable-unicode-properties ;;
        foundation) build_foundation ;;
        webp)       build_webp ;;
        jxl)        build_jxl ;;
        sdl3)       build_sdl3 ;;
        *)          echo "Unknown step: $step"; exit 1 ;;
    esac
done
echo "Done: $PREFIX"
