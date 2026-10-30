#!/bin/sh
# Configures an Xcode project using the dependencies built with ios-deps.sh.
# Usage: cmake-ios-lagrange.sh os|simulator [cmake options...]
# Device builds need -DXCODE_DEVELOPMENT_TEAM=<team ID>.
case "$1" in
    os)
        SYSROOT=iphoneos
        IOS_DIR="$HOME/SDK/ios/arm64"
        ;;
    simulator)
        SYSROOT=iphonesimulator
        IOS_DIR="$HOME/SDK/ios-sim/arm64"
        ;;
    *)
        echo "Usage: cmake-ios-lagrange.sh os|simulator [cmake options...]"
        exit 1
        ;;
esac
shift
cmake "$(dirname "$0")/.." \
    -G Xcode \
    -DCMAKE_SYSTEM_NAME=iOS \
    -DCMAKE_OSX_SYSROOT=$SYSROOT \
    -DCMAKE_OSX_ARCHITECTURES=arm64 \
    -DCMAKE_OSX_DEPLOYMENT_TARGET=15.0 \
    -DCMAKE_XCODE_ATTRIBUTE_CLANG_ENABLE_OBJC_ARC=YES \
    -DIOS_DIR="$IOS_DIR" \
    -Dthe_Foundation_DIR="$IOS_DIR/lib/cmake/the_Foundation" \
    -DENABLE_DOWNLOAD_EDIT=NO \
    -DENABLE_IPC=NO \
    "$@"
