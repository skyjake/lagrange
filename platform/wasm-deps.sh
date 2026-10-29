#!/bin/bash
# Builds Lagrange's dependencies for WebAssembly using Emscripten.
#
# Usage: wasm-deps.sh [PREFIX]
#
# Requires an active Emscripten environment (source emsdk_env.sh). Everything
# is compiled with -pthread, since objects linked into a shared-memory module
# must use atomics and bulk memory.

set -e

SRC_ROOT=$(cd "$(dirname "$0")/.." && pwd)
PREFIX=${1:-$HOME/SDK/wasm}
WORK=${WASM_WORK:-$HOME/src/libs/wasm-build}
JOBS=${JOBS:-$(sysctl -n hw.ncpu 2>/dev/null || nproc)}
# Older config.sub scripts do not know "arm64".
BUILD_TRIPLET=$(cc -dumpmachine | sed 's/^arm64-/aarch64-/')

ZLIB_VER=1.3.1
OPENSSL_VER=3.5.4
PCRE2_VER=10.45
UNISTRING_VER=1.3
SDL3_VER=3.4.10
WEBP_VER=1.6.0
MPG123_VER=1.33.2
OGG_VER=1.3.6
OPUS_VER=1.5.2
OPUSFILE_VER=0.12

if ! command -v emcc >/dev/null; then
    echo "emcc not found; source emsdk_env.sh first"
    exit 1
fi

export CFLAGS="-O2 -pthread"
export CXXFLAGS="-O2 -pthread"
export LDFLAGS="-pthread"

echo "Prefix: $PREFIX"
echo "Work:   $WORK"
mkdir -p "$PREFIX/include" "$PREFIX/lib" "$WORK/src"

# Download and unpack a release tarball into $WORK/src.
fetch() {
    local url=$1
    local dir=$2
    local file=$WORK/src/$(basename "$url")
    if [ ! -f "$file" ]; then
        curl -fL -o "$file" "$url"
    fi
    if [ ! -d "$WORK/src/$dir" ]; then
        tar -xf "$file" -C "$WORK/src"
    fi
}

build_zlib() {
    fetch https://github.com/madler/zlib/releases/download/v$ZLIB_VER/zlib-$ZLIB_VER.tar.gz zlib-$ZLIB_VER
    rm -rf "$WORK/zlib"
    emcmake cmake -S "$WORK/src/zlib-$ZLIB_VER" -B "$WORK/zlib" -G Ninja \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_INSTALL_PREFIX="$PREFIX" \
        -DZLIB_BUILD_EXAMPLES=OFF
    cmake --build "$WORK/zlib" --target zlibstatic
    install -m 644 "$WORK/zlib/libz.a" "$PREFIX/lib/"
    install -m 644 "$WORK/src/zlib-$ZLIB_VER/zlib.h" "$WORK/zlib/zconf.h" "$PREFIX/include/"
}

build_openssl() {
    fetch https://github.com/openssl/openssl/releases/download/openssl-$OPENSSL_VER/openssl-$OPENSSL_VER.tar.gz openssl-$OPENSSL_VER
    cd "$WORK/src/openssl-$OPENSSL_VER"
    make distclean >/dev/null 2>&1 || true
    # Not using emconfigure: it would make OpenSSL prefix the compiler name twice.
    ./Configure linux-generic32 \
        no-asm no-shared no-dso no-engine no-tests no-apps no-docs \
        no-afalgeng no-secure-memory no-ui-console \
        --prefix="$PREFIX" --libdir=lib \
        CC=emcc AR=emar RANLIB=emranlib
    emmake make -j$JOBS build_libs
    emmake make install_dev
}

build_pcre2() {
    fetch https://github.com/PCRE2Project/pcre2/releases/download/pcre2-$PCRE2_VER/pcre2-$PCRE2_VER.tar.gz pcre2-$PCRE2_VER
    rm -rf "$WORK/pcre2"
    emcmake cmake -S "$WORK/src/pcre2-$PCRE2_VER" -B "$WORK/pcre2" -G Ninja \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_INSTALL_PREFIX="$PREFIX" \
        -DBUILD_SHARED_LIBS=OFF \
        -DPCRE2_BUILD_PCRE2GREP=OFF \
        -DPCRE2_BUILD_TESTS=OFF \
        -DPCRE2_SUPPORT_JIT=OFF
    cmake --build "$WORK/pcre2" --target install
}

build_unistring() {
    fetch https://ftp.gnu.org/gnu/libunistring/libunistring-$UNISTRING_VER.tar.gz libunistring-$UNISTRING_VER
    cd "$WORK/src/libunistring-$UNISTRING_VER"
    # Specifying --build forces cross-compilation mode so that configure does not
    # try to run test programs (some hang when run under node).
    emconfigure ./configure --prefix="$PREFIX" \
        --build=$BUILD_TRIPLET --host=wasm32-unknown-emscripten \
        --disable-shared --enable-static
    emmake make -j$JOBS
    emmake make install
}

build_sdl3() {
    fetch https://github.com/libsdl-org/SDL/releases/download/release-$SDL3_VER/SDL3-$SDL3_VER.tar.gz SDL3-$SDL3_VER
    rm -rf "$WORK/sdl3"
    emcmake cmake -S "$WORK/src/SDL3-$SDL3_VER" -B "$WORK/sdl3" -G Ninja \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_INSTALL_PREFIX="$PREFIX" \
        -DSDL_SHARED=OFF \
        -DSDL_STATIC=ON \
        -DSDL_PTHREADS=ON \
        -DSDL_TESTS=OFF \
        -DSDL_EXAMPLES=OFF
    cmake --build "$WORK/sdl3" --target install
}

build_webp() {
    fetch https://storage.googleapis.com/downloads.webmproject.org/releases/webp/libwebp-$WEBP_VER.tar.gz libwebp-$WEBP_VER
    rm -rf "$WORK/webp"
    emcmake cmake -S "$WORK/src/libwebp-$WEBP_VER" -B "$WORK/webp" -G Ninja \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_INSTALL_PREFIX="$PREFIX" \
        -DBUILD_SHARED_LIBS=OFF \
        -DWEBP_BUILD_ANIM_UTILS=OFF \
        -DWEBP_BUILD_CWEBP=OFF \
        -DWEBP_BUILD_DWEBP=OFF \
        -DWEBP_BUILD_GIF2WEBP=OFF \
        -DWEBP_BUILD_IMG2WEBP=OFF \
        -DWEBP_BUILD_VWEBP=OFF \
        -DWEBP_BUILD_WEBPINFO=OFF \
        -DWEBP_BUILD_WEBPMUX=OFF \
        -DWEBP_BUILD_EXTRAS=OFF \
        -DWEBP_BUILD_LIBWEBPMUX=OFF \
        -DWEBP_ENABLE_SIMD=OFF
    cmake --build "$WORK/webp" --target webpdecoder
    install -m 644 "$WORK/webp/libwebpdecoder.a" "$PREFIX/lib/"
    mkdir -p "$PREFIX/include/webp"
    install -m 644 "$WORK/src/libwebp-$WEBP_VER/src/webp/decode.h" \
        "$WORK/src/libwebp-$WEBP_VER/src/webp/types.h" "$PREFIX/include/webp/"
}

# Only the decoder library, with the portable floating-point decoder.
build_mpg123() {
    fetch https://www.mpg123.de/download/mpg123-$MPG123_VER.tar.bz2 mpg123-$MPG123_VER
    cd "$WORK/src/mpg123-$MPG123_VER"
    make distclean >/dev/null 2>&1 || true
    emconfigure ./configure --prefix="$PREFIX" \
        --build=$BUILD_TRIPLET --host=wasm32-unknown-emscripten \
        --disable-shared --enable-static \
        --with-cpu=generic_fpu \
        --disable-components --enable-libmpg123 \
        --disable-largefile
    emmake make -j$JOBS
    emmake make install
}

build_ogg() {
    fetch https://downloads.xiph.org/releases/ogg/libogg-$OGG_VER.tar.gz libogg-$OGG_VER
    cd "$WORK/src/libogg-$OGG_VER"
    make distclean >/dev/null 2>&1 || true
    emconfigure ./configure --prefix="$PREFIX" \
        --build=$BUILD_TRIPLET --host=wasm32-unknown-emscripten \
        --disable-shared --enable-static
    emmake make -j$JOBS
    emmake make install
}

build_opus() {
    fetch https://downloads.xiph.org/releases/opus/opus-$OPUS_VER.tar.gz opus-$OPUS_VER
    cd "$WORK/src/opus-$OPUS_VER"
    make distclean >/dev/null 2>&1 || true
    emconfigure ./configure --prefix="$PREFIX" \
        --build=$BUILD_TRIPLET --host=wasm32-unknown-emscripten \
        --disable-shared --enable-static \
        --disable-intrinsics --disable-rtcd \
        --disable-extra-programs --disable-doc
    emmake make -j$JOBS
    emmake make install
}

build_opusfile() {
    fetch https://downloads.xiph.org/releases/opus/opusfile-$OPUSFILE_VER.tar.gz opusfile-$OPUSFILE_VER
    cd "$WORK/src/opusfile-$OPUSFILE_VER"
    make distclean >/dev/null 2>&1 || true
    # The bundled config.sub does not know Emscripten.
    cp "$WORK/src/opus-$OPUS_VER/config.sub" "$WORK/src/opus-$OPUS_VER/config.guess" .
    # emconfigure replaces the pkg-config search path.
    DEPS_CFLAGS="-I$PREFIX/include -I$PREFIX/include/opus" DEPS_LIBS="-L$PREFIX/lib -lopus -logg" \
    emconfigure ./configure --prefix="$PREFIX" \
        --build=$BUILD_TRIPLET --host=wasm32-unknown-emscripten \
        --disable-shared --enable-static \
        --disable-http --disable-examples --disable-doc
    emmake make -j$JOBS
    emmake make install
}

# Root CA certificates (Mozilla's, as extracted by curl). Browsers do not expose a system
# certificate store, so they are bundled with the app.
build_cacert() {
    mkdir -p "$PREFIX/share"
    curl -fL -o "$PREFIX/share/cacert.pem" https://curl.se/ca/cacert.pem
}

build_meson() {
    local name=$1
    shift
    rm -rf "$WORK/$name"
    meson setup "$WORK/$name" "$SRC_ROOT/lib/$name" \
        --cross-file "$SRC_ROOT/platform/emscripten-cross.ini" \
        --prefix "$PREFIX" \
        -Dbuildtype=release \
        -Ddefault_library=static \
        -Dlibdir=lib \
        "$@"
    ninja -C "$WORK/$name" install
}

build_fribidi() {
    build_meson fribidi -Dtests=false -Ddocs=false -Dbin=false "-Dc_args=['-pthread','-Wno-macro-redefined']"
}

build_harfbuzz() {
    build_meson harfbuzz \
        -Dtests=disabled -Dglib=disabled -Dgobject=disabled \
        -Dcairo=disabled -Dicu=disabled -Dfreetype=disabled -Ddocs=disabled
}

if [ -n "$ONLY" ]; then
    for dep in $ONLY; do
        build_$dep
    done
else
    build_zlib
    build_openssl
    build_pcre2
    build_unistring
    build_sdl3
    build_fribidi
    build_harfbuzz
    build_webp
    build_mpg123
    build_ogg
    build_opus
    build_opusfile
    build_cacert
fi
echo "Dependencies installed in $PREFIX"
