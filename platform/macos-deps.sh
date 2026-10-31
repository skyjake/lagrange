#!/bin/bash
# Builds Lagrange's macOS dependencies for the host architecture (arm64 or x86_64).
#
# Usage: macos-deps.sh [step...]
#
# Source tarballs of pinned versions are downloaded to MACOS_SRC (default:
# ~/src/libs/macos); the_Foundation is built from TFDN_SRC (default:
# ~/src/the_Foundation). The libraries are installed in MACOS_PREFIX (default:
# ~/.local) and Sparkle.framework in SPARKLE_DIR (default: ~/Library/Frameworks).

set -e

SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
source $SCRIPT_DIR/status-line.sh
source $SCRIPT_DIR/jxl-dec.sh

OPENSSL_VER=3.5.9
ICONV_VER=1.19
UNISTRING_VER=1.5
PCRE2_VER=10.49
NCURSES_VER=6.6
MPG123_VER=1.33.7
OGG_VER=1.3.6
OPUS_VER=1.6.1
OPUSFILE_VER=0.12
WEBP_VER=1.6.0
JXL_VER=0.12.0
SDL3_VER=3.4.18
SPARKLE_VER=1.27.0
ALL_STEPS="openssl iconv unistring pcre2 ncurses foundation mpg123 ogg opus opusfile webp jxl sdl3 sparkle"

ARCH=$(uname -m)
case $ARCH in
    arm64)  OSMINVER=11.0 ;;
    x86_64) OSMINVER=10.13 ;;
    *)      echo "Unsupported architecture: $ARCH"; exit 1 ;;
esac

case "$1" in
    -h|--help)
        echo "Usage: macos-deps.sh [step...]"
        echo "Steps: $ALL_STEPS"
        exit 0
        ;;
esac
STEPS=${*:-$ALL_STEPS}

PREFIX=${MACOS_PREFIX:-$HOME/.local}
SPARKLE_DIR=${SPARKLE_DIR:-$HOME/Library/Frameworks}
SRC_LIBS=${MACOS_SRC:-$HOME/src/libs/macos}
DIR_FOUNDATION=${TFDN_SRC:-$HOME/src/the_Foundation}
TARBALLS=$SRC_LIBS/tarballs
BUILD_ROOT=$SRC_LIBS/build-$ARCH
JOBS=${JOBS:-$(sysctl -n hw.ncpu)}

echo "Arch:   $ARCH (macOS $OSMINVER)"
echo "Prefix: $PREFIX"
echo "Steps:  $STEPS"
if [ -t 0 ]; then
    read -p "--- Press Enter to begin ---"
fi

unset MAKEFLAGS CFLAGS CXXFLAGS CPPFLAGS LDFLAGS
export MACOSX_DEPLOYMENT_TARGET=$OSMINVER
export PKG_CONFIG_PATH=$PREFIX/lib/pkgconfig
# No -arch flag: it leaks into ncurses .pc files and the host arch is the default.
MINFLAGS="-mmacosx-version-min=$OSMINVER"
mkdir -p $PREFIX $TARBALLS $BUILD_ROOT

function fresh_build_dir() {
    rm -rf $BUILD_ROOT/$1
    mkdir -p $BUILD_ROOT/$1
    cd $BUILD_ROOT/$1
}

function download() {
    local tarball=$TARBALLS/${2:-$(basename $1)}
    if [ ! -f $tarball ]; then
        curl -fL -o $tarball.part $1
        mv $tarball.part $tarball
    fi
    echo $tarball
}

# Extracts a downloaded tarball into a fresh build directory.
function unpack() {
    local tarball
    status_phase unpack
    tarball=$(download $2 $3)
    fresh_build_dir $1
    tar xf $tarball --strip-components=1
}

function autotools_build() {
    unpack $1 $2
    shift 2
    status_phase configure
    CFLAGS="$MINFLAGS -O2" \
    CXXFLAGS="$MINFLAGS -O2" \
    CPPFLAGS="-I$PREFIX/include" \
    LDFLAGS="$MINFLAGS -L$PREFIX/lib" \
    ./configure --prefix=$PREFIX "$@"
    status_phase build
    make -j$JOBS
    status_phase install
    make install
}

function cmake_macos() {
    local src=$1 build=$2 tag
    shift 2
    [ $build = build ] || tag=" ($build)"
    status_phase "configure$tag"
    cmake -S $src -B $build \
        -DCMAKE_OSX_ARCHITECTURES=$ARCH \
        -DCMAKE_OSX_DEPLOYMENT_TARGET=$OSMINVER \
        -DCMAKE_INSTALL_PREFIX=$PREFIX \
        -DCMAKE_INSTALL_LIBDIR=lib \
        -DCMAKE_PREFIX_PATH=$PREFIX \
        -DCMAKE_POLICY_VERSION_MINIMUM=3.5 \
        "$@"
    status_phase "build$tag"
    cmake --build $build --parallel $JOBS
    status_phase "install$tag"
    cmake --install $build
}

function build_openssl() {
    unpack openssl https://github.com/openssl/openssl/releases/download/openssl-$OPENSSL_VER/openssl-$OPENSSL_VER.tar.gz
    status_phase configure
    ./Configure darwin64-$ARCH-cc \
        no-tests no-apps no-docs \
        -mmacosx-version-min=$OSMINVER \
        --prefix=$PREFIX \
        --openssldir=$PREFIX/ssl \
        --libdir=lib
    status_phase build
    make -j$JOBS build_libs
    status_phase install
    rm -rf $PREFIX/include/openssl
    make install_dev
}

function build_foundation() {
    fresh_build_dir the_Foundation
    for config in Debug RelWithDebInfo; do
        cmake_macos $DIR_FOUNDATION $config \
            -DCMAKE_BUILD_TYPE=$config \
            -DTFDN_ENABLE_TESTS=NO \
            -DTFDN_ENABLE_WEBREQUEST=NO \
            -DTFDN_ENABLE_DEBUG_OUTPUT=NO \
            -DUNISTRING_DIR=$PREFIX
    done
}

function build_webp() {
    unpack libwebp https://storage.googleapis.com/downloads.webmproject.org/releases/webp/libwebp-$WEBP_VER.tar.gz
    cmake_macos . build \
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
}

# Fetches a libjxl third_party submodule at the commit pinned in deps.sh.
function jxl_third_party() {
    local var=$(echo $1 | tr '[:lower:]' '[:upper:]')
    local sha=$(sed -n "s/^THIRD_PARTY_$var=\"\([0-9a-f]*\)\".*/\1/p" deps.sh)
    local tarball
    status_phase "unpack $1"
    tarball=$(download $2$sha.tar.gz jxl-$1-$sha.tar.gz)
    rm -rf third_party/$1
    mkdir -p third_party/$1
    tar xf $tarball -C third_party/$1 $3
}

function build_jxl() {
    unpack libjxl https://github.com/libjxl/libjxl/archive/refs/tags/v$JXL_VER.tar.gz libjxl-$JXL_VER.tar.gz
    # Brotli is not used by the decoder, but configuring requires it.
    jxl_third_party brotli  https://github.com/google/brotli/archive/ --strip-components=1
    jxl_third_party highway https://github.com/google/highway/archive/ --strip-components=1
    jxl_third_party skcms   https://skia.googlesource.com/skcms/+archive/
    status_phase configure
    cmake -S . -B build \
        -DCMAKE_OSX_ARCHITECTURES=$ARCH \
        -DCMAKE_OSX_DEPLOYMENT_TARGET=$OSMINVER \
        -DCMAKE_POLICY_VERSION_MINIMUM=3.5 \
        -DCMAKE_BUILD_TYPE=Release \
        "${JXL_DEC_OPTS[@]}"
    status_phase build
    cmake --build build --parallel $JOBS --target $JXL_DEC_TARGETS
    status_phase install
    jxl_dec_install build $PREFIX
}

function build_sdl3() {
    unpack SDL3 https://github.com/libsdl-org/SDL/releases/download/release-$SDL3_VER/SDL3-$SDL3_VER.tar.gz
    cmake_macos . build \
        -DCMAKE_BUILD_TYPE=Release \
        -DSDL_SHARED=ON \
        -DSDL_STATIC=OFF \
        -DSDL_TESTS=OFF \
        -DSDL_TEST_LIBRARY=OFF \
        -DSDL_EXAMPLES=OFF \
        -DSDL_UNINSTALL=OFF
}

function build_sparkle() {
    local tarball
    status_phase unpack
    tarball=$(download https://github.com/sparkle-project/Sparkle/releases/download/$SPARKLE_VER/Sparkle-$SPARKLE_VER.tar.xz)
    fresh_build_dir Sparkle
    tar xf $tarball
    mkdir -p $SPARKLE_DIR
    rm -rf $SPARKLE_DIR/Sparkle.framework
    ditto Sparkle.framework $SPARKLE_DIR/Sparkle.framework
}

status_begin $BUILD_ROOT/.status $STEPS
for step in $STEPS; do
    status_step $step
    case $step in
        openssl)    build_openssl ;;
        iconv)      autotools_build libiconv https://ftp.gnu.org/gnu/libiconv/libiconv-$ICONV_VER.tar.gz \
                        --enable-extra-encodings --disable-nls ;;
        unistring)  autotools_build libunistring https://ftp.gnu.org/gnu/libunistring/libunistring-$UNISTRING_VER.tar.gz \
                        --with-libiconv-prefix=$PREFIX ;;
        pcre2)      autotools_build pcre2 https://github.com/PCRE2Project/pcre2/releases/download/pcre2-$PCRE2_VER/pcre2-$PCRE2_VER.tar.bz2 ;;
        ncurses)    autotools_build ncurses https://ftp.gnu.org/gnu/ncurses/ncurses-$NCURSES_VER.tar.gz \
                        --enable-widec --enable-ext-colors --enable-pc-files --with-pkg-config \
                        --with-pkg-config-libdir=$PREFIX/lib/pkgconfig --without-manpages --without-tests \
                        --without-debug --without-cxx-binding --without-ada ;;
        foundation) build_foundation ;;
        mpg123)     autotools_build mpg123 https://www.mpg123.de/download/mpg123-$MPG123_VER.tar.bz2 \
                        --disable-components --enable-libmpg123 ;;
        ogg)        autotools_build libogg https://downloads.xiph.org/releases/ogg/libogg-$OGG_VER.tar.xz ;;
        opus)       autotools_build opus https://downloads.xiph.org/releases/opus/opus-$OPUS_VER.tar.gz \
                        --disable-doc --disable-extra-programs ;;
        opusfile)   autotools_build opusfile https://downloads.xiph.org/releases/opus/opusfile-$OPUSFILE_VER.tar.gz \
                        --disable-http --disable-examples --disable-doc ;;
        webp)       build_webp ;;
        jxl)        build_jxl ;;
        sdl3)       build_sdl3 ;;
        sparkle)    build_sparkle ;;
        *)          echo "Unknown step: $step"; exit 1 ;;
    esac
done
echo "Done: $PREFIX (total $(status_total))"
