#!/bin/sh
KIND=iphone$1
if [ -z "$1" ]; then
    echo "Usage: ios-deps.sh os|simulator"
    exit 0
fi
if [ "$KIND" = "iphoneos" ]; then
    PREFIX=$HOME/SDK/ios
else
    PREFIX=$HOME/SDK/ios-sim
fi
DIR_FRIBIDI=$HOME/src/libs/fribidi
DIR_HARFBUZZ=$HOME/src/libs/harfbuzz
DIR_ICONV=$HOME/src/libs/libiconv-1.16
DIR_UNISTRING=$HOME/src/libs/libunistring-0.9.10
DIR_PCRE=$HOME/src/libs/pcre-8.44

echo "Prefix: ${PREFIX}"
read -p "--- Press Enter to begin ---"

ICONF=$HOME/src/libs/ios-autotools/iconfigure
# Note: There's a small modification to the `iconfigure` script.
export OSMINVER=9.0
export SDK=$KIND

function build_arch() {
    arch=$1
    
    cd $DIR_FRIBIDI
    rm -rf ios-build
    mkdir ios-build
    cd ios-build
    meson .. \
        --cross-file $HOME/cross-mac-arm64-ios-arm64.ini \
        -Dbuildtype=release \
        -Ddefault_library=static \
        -Dtests=false \
        -Ddocs=false \
        -Dbin=false \
        -Ddocs=false \
        --prefix ${PREFIX}/arm64
    ninja install

    cd $DIR_HARFBUZZ
    make clean
    PREFIX=$PREFIX/$arch $ICONF $arch --with-cairo=no --with-glib=no --with-freetype=no --with-gobject=no --with-chafa=no --with-graphite2=no --with-coretext=no
    make install
    
    cd $DIR_ICONV
    make clean
    PREFIX=$PREFIX/$arch $ICONF $arch
    make install

    cd $DIR_UNISTRING
    make clean
    PREFIX=$PREFIX/$arch $ICONF $arch --disable-namespacing --with-libiconv-prefix=$PREFIX/$arch
    make install

    cd $DIR_PCRE
    make clean
    PREFIX=$PREFIX/$arch $ICONF $arch --enable-unicode-properties
    make install
}

function fat_archive() {
    lipo \
        -arch x86_64 $PREFIX/x86_64/lib/$1 \
        -arch arm64 $PREFIX/arm64/lib/$1 \
        -output $PREFIX/fat/lib/$1 -create
}

#build_arch x86_64
build_arch arm64

#fat_archive libiconv.a
#fat_archive libunistring.a
#fat_archive libpcre.a

#mkdir -p $PREFIX/fat/include
#cp -r $PREFIX/arm64/include/* $PREFIX/fat/include

