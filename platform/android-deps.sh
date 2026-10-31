#!/bin/bash
# Builds Lagrange's Android dependencies with the NDK for each ABI.
#
# Usage: android-deps.sh [step...]
#
# ANDROID_NDK_HOME and ANDROID_NDK_TOOLCHAIN_BIN must be set. ARCHS selects the
# ABIs (default: arm arm64 x86 x86_64). Meson cross files are read from
# MESON_CROSS_DIR (default: ~/Scripts) as cross-android-<arch>.ini. The libraries
# are installed in DEST/<name>-android/<abi>.

set -e

SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
source $SCRIPT_DIR/status-line.sh
source $SCRIPT_DIR/jxl-dec.sh
ALL_STEPS="webp fribidi harfbuzz libpng jxl freetype openssl iconv unistring pcre"
STEPS=${*:-$ALL_STEPS}
ARCHS=${ARCHS:-arm arm64 x86 x86_64}

if [ `uname -s` == Darwin ]; then
    export CFLAGS=""
    export CXXFLAGS=""
    export LDFLAGS=""
    DEST=$HOME/SDK
    OPENSSL_DIR=$HOME/src/libs/openssl
    ICONV_DIR=$HOME/src/libs/libiconv
    UNISTRING_DIR=$HOME/src/libs/libunistring
    PCRE_DIR=$HOME/src/libs/pcre-8.44
    FREETYPE_DIR=$HOME/src/libs/freetype-2.15.0
    PNG_DIR=$HOME/src/libs/libpng-1.6.59
else
    DEST=$HOME/sdk
    OPENSSL_DIR=$HOME/src/openssl-1.1.1j
    ICONV_DIR=$HOME/src/libiconv-1.16
    UNISTRING_DIR=$HOME/src/libunistring-0.9.10
    PCRE_DIR=$HOME/src/pcre-8.45
    FREETYPE_DIR=$HOME/src/freetype-2.15.0
    PNG_DIR=$HOME/src/libpng-1.6.59
fi
WEBP_DIR=$HOME/src/libs/libwebp-android
HARFBUZZ_DIR=$HOME/src/libs/harfbuzz-15.0.0
JXL_DIR=$HOME/src/libs/libjxl
FRIBIDI_DIR=$SCRIPT_DIR/../lib/fribidi
MESON_CROSS_DIR=${MESON_CROSS_DIR:-$HOME/Scripts}
BUILD_ROOT=/tmp/build-android

echo "NDK:    $ANDROID_NDK_HOME"
echo "Prefix: $DEST/<name>-android"
echo "ABIs:   $ARCHS"
echo "Steps:  $STEPS"
if [ -t 0 ]; then
    read -p "--- Press Enter to begin ---"
fi

export PATH=$ANDROID_NDK_TOOLCHAIN_BIN:$PATH
mkdir -p $BUILD_ROOT

function set_toolchain() {
    TOOLCHAIN=$1
    LD_ABI=android
    API_LEVEL=19

    if [ $TOOLCHAIN == arm ]; then
        BINARY=armv7a
        LD_BINARY=arm
        LD_ABI=androideabi
        ABI=androideabi${API_LEVEL}
        LIB_DIR=armeabi-v7a

    elif [ $TOOLCHAIN == arm64 ]; then
        BINARY=aarch64
        LD_BINARY=$BINARY
        ABI=android21
        LIB_DIR=arm64-v8a
        API_LEVEL=21

    elif [ $TOOLCHAIN == x86 ]; then
        BINARY=i686
        LD_BINARY=$BINARY
        ABI=android${API_LEVEL}
        LIB_DIR=x86

    else
        BINARY=x86_64
        LD_BINARY=$BINARY
        ABI=android21
        LIB_DIR=x86_64
        API_LEVEL=21
    fi
    CFG_HOST=${LD_BINARY}-linux-${LD_ABI}
    MESON_CROSS=$MESON_CROSS_DIR/cross-android-${TOOLCHAIN}.ini

    export CC=$ANDROID_NDK_TOOLCHAIN_BIN/${BINARY}-linux-${ABI}-clang
    export CXX=$ANDROID_NDK_TOOLCHAIN_BIN/${BINARY}-linux-${ABI}-clang++
    export LD=$ANDROID_NDK_TOOLCHAIN_BIN/${LD_BINARY}-linux-${LD_ABI}-ld
    export CFLAGS=-D__ANDROID_API__=${API_LEVEL}
}

function fresh_build_dir() {
    BUILD_DIR=$BUILD_ROOT/$1
    rm -rf $BUILD_DIR
    mkdir -p $BUILD_DIR
}

function make_keep_going() {
    make -k "$@" || echo "(ignoring errors from make $*)"
}

# In-source build with autotools: autotools_build <dir> <name> [configure args...]
# With KEEP_GOING set, build errors are ignored (some targets fail on Android).
function autotools_build() {
    local name=$2 make=make
    cd $1
    shift 2
    if [ -n "$KEEP_GOING" ]; then
        make="make_keep_going"
    fi
    if [ -f Makefile ]; then
        status_phase clean
        make_keep_going maintainer-clean
    fi
    status_phase configure
    ./configure --prefix=$DEST/${name}-android/${LIB_DIR} --host=${CFG_HOST} "$@"
    status_phase build
    $make
    status_phase install
    $make install
    status_phase clean
    make_keep_going maintainer-clean
}

# Meson cross build: meson_build <src dir> <name> [meson args...]
function meson_build() {
    local src=$1 name=$2
    shift 2
    fresh_build_dir $name
    status_phase configure
    meson setup $BUILD_DIR $src --cross-file ${MESON_CROSS} \
        -Dbuildtype=release \
        -Ddefault_library=static \
        -Dlibdir=lib \
        --prefix $DEST/${name}-android/${LIB_DIR} \
        "$@"
    status_phase build
    ninja -C $BUILD_DIR
    status_phase install
    ninja -C $BUILD_DIR install
}

# CMake build with the NDK toolchain file: cmake_build <src dir> <name> [cmake args...]
function cmake_build() {
    local src=$1 name=$2
    shift 2
    fresh_build_dir $name
    status_phase configure
    cmake -S $src -B $BUILD_DIR -G Ninja \
        -DCMAKE_TOOLCHAIN_FILE=$ANDROID_NDK_HOME/build/cmake/android.toolchain.cmake \
        -DANDROID_ABI=${LIB_DIR} \
        -DANDROID_PLATFORM=android-${API_LEVEL} \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_INSTALL_LIBDIR=lib \
        -DCMAKE_INSTALL_PREFIX=$DEST/${name}-android/${LIB_DIR} \
        "$@"
    status_phase build
    ninja -C $BUILD_DIR
    status_phase install
    ninja -C $BUILD_DIR install
}

function build_harfbuzz() {
    # libharfbuzz-raster renders COLRv1 color glyphs.
    meson_build $HARFBUZZ_DIR harfbuzz \
        --wrap-mode=nodownload \
        -Dtests=disabled \
        -Dglib=disabled \
        -Dgobject=disabled \
        -Dcairo=disabled \
        -Dchafa=disabled \
        -Dicu=disabled \
        -Dfreetype=disabled \
        -Dpng=disabled \
        -Dzlib=disabled \
        -Draster=enabled \
        -Dvector=disabled \
        -Dgpu=disabled \
        -Dsubset=disabled \
        -Dutilities=disabled \
        -Dintrospection=disabled \
        -Ddocs=disabled
}

# Needed by FreeType for CBDT color emoji.
function build_libpng() {
    cmake_build $PNG_DIR libpng \
        -DPNG_SHARED=OFF -DPNG_TESTS=OFF -DPNG_TOOLS=OFF
    # libpng.pc requires zlib; the NDK's system zlib has no .pc of its own.
    printf 'Name: zlib\nDescription: zlib from the Android NDK sysroot\nVersion: 1.2.11\nLibs: -lz\nCflags:\n' \
        > $DEST/libpng-android/${LIB_DIR}/lib/pkgconfig/zlib.pc
}

# Brotli, Highway, and skcms come from the third_party submodules.
function build_jxl() {
    fresh_build_dir libjxl
    status_phase configure
    cmake -S $JXL_DIR -B $BUILD_DIR -G Ninja \
        -DCMAKE_TOOLCHAIN_FILE=$ANDROID_NDK_HOME/build/cmake/android.toolchain.cmake \
        -DANDROID_ABI=${LIB_DIR} \
        -DANDROID_PLATFORM=android-${API_LEVEL} \
        -DCMAKE_BUILD_TYPE=Release \
        "${JXL_DEC_OPTS[@]}"
    status_phase build
    ninja -C $BUILD_DIR $JXL_DEC_TARGETS
    status_phase install
    jxl_dec_install $BUILD_DIR $DEST/libjxl-android/${LIB_DIR}
}

# pkg_config_libdir in the cross file points to libpng.
function build_freetype() {
    meson_build $FREETYPE_DIR freetype \
        --wrap-mode=nodownload \
        -Dharfbuzz=disabled \
        -Dpng=enabled \
        -Dbzip2=disabled \
        -Dbrotli=disabled \
        -Dzlib=system \
        -Dtests=disabled
}

function build_openssl() {
    cd $OPENSSL_DIR
    if [ -f Makefile ]; then
        status_phase clean
        make_keep_going distclean
    fi
    status_phase configure
    ./Configure \
        --prefix=$DEST/openssl-android/${LIB_DIR} \
        android-${TOOLCHAIN} \
        -D__ANDROID_API__=${API_LEVEL}
    status_phase build
    make
    status_phase install
    make install_sw
}

status_begin $BUILD_ROOT/.status $(for arch in $ARCHS; do echo $STEPS; done)
for arch in $ARCHS; do
    set_toolchain $arch
    for step in $STEPS; do
        status_step "$step ($LIB_DIR)"
        case $step in
            webp)       autotools_build $WEBP_DIR libwebp --disable-libwebpdemux --disable-shared --enable-libwebpdecoder ;;
            fribidi)    meson_build $FRIBIDI_DIR fribidi -Dtests=false -Ddocs=false -Dbin=false -Dc_flags=-Wno-macro-redefined ;;
            harfbuzz)   build_harfbuzz ;;
            libpng)     build_libpng ;;
            jxl)        build_jxl ;;
            freetype)   build_freetype ;;
            openssl)    build_openssl ;;
            iconv)      KEEP_GOING=1 autotools_build $ICONV_DIR libiconv --enable-extra-encodings ;;
            unistring)  KEEP_GOING=1 autotools_build $UNISTRING_DIR libunistring --with-libiconv-prefix=$DEST/libiconv-android/${LIB_DIR} ;;
            pcre)       KEEP_GOING=1 autotools_build $PCRE_DIR libpcre --enable-unicode-properties --disable-cpp ;;
            *)          echo "Unknown step: $step"; exit 1 ;;
        esac
    done
done
echo "Done: $DEST (total $(status_total))"
