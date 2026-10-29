#!/bin/bash
# Builds the web app's UI variants and assembles them into one site: the desktop
# variant at the root, the phone and tablet variants in subdirectories.
#
# Usage: wasm-site.sh [OUTPUT_DIR]
#
# Requires an active Emscripten environment (source emsdk_env.sh) and the
# dependencies built with wasm-deps.sh. Environment variables:
#   BUILD_TYPE   CMake build type (default: Release)
#   BUILD_ROOT   directory for the build trees (default: build-web)
#   WASM_DIR     dependency prefix (default: ~/SDK/wasm)
#
# The large files also get gzip and Brotli (if the brotli tool is found) compressed
# copies for the web server.

set -e

SRC_ROOT=$(cd "$(dirname "$0")/.." && pwd)
BUILD_TYPE=${BUILD_TYPE:-Release}
BUILD_ROOT=${BUILD_ROOT:-$SRC_ROOT/build-web}
OUT=${1:-$BUILD_ROOT/site}
FILES="index.html shell.css shell.js storage.js lagrange.js lagrange.wasm lagrange.data"

if ! command -v emcc >/dev/null; then
    echo "emcc not found; source emsdk_env.sh first"
    exit 1
fi

# Build a variant and copy its files to the given subdirectory of the site.
build_variant() {
    local name=$1
    local subdir=$2
    shift 2
    local dir=$BUILD_ROOT/$name
    emcmake cmake -S "$SRC_ROOT" -B "$dir" -G Ninja \
        -DCMAKE_BUILD_TYPE=$BUILD_TYPE \
        ${WASM_DIR:+-DWASM_DIR=$WASM_DIR} \
        "$@"
    cmake --build "$dir"
    mkdir -p "$OUT/$subdir"
    for file in $FILES; do
        cp "$dir/app/$file" "$OUT/$subdir"
    done
}

# Compressed copies of the large files, which the relay serves to browsers that accept them.
compress_site() {
    find "$OUT" -name 'lagrange.*' ! -name '*.gz' ! -name '*.br' | while read -r file; do
        gzip -9 -k -f "$file"
        if command -v brotli >/dev/null; then
            brotli -q 11 -k -f "$file"
        fi
    done
}

rm -rf "$OUT"
build_variant desktop .
build_variant phone phone -DENABLE_MOBILE_PHONE=ON
build_variant tablet tablet -DENABLE_MOBILE_TABLET=ON
compress_site
echo "Site assembled in $OUT"
