#!/bin/sh
IOS_DIR="$HOME/SDK/ios/arm64"
cmake ../lagrange \
    -G Xcode \
    -DCMAKE_TOOLCHAIN_FILE=$HOME/src/libs/ios-cmake/ios.toolchain.cmake \
    -DPLATFORM=OS64 \
    -DENABLE_BITCODE=0 \
    -DDEPLOYMENT_TARGET=9.0 \
    -DIOS_DIR=$IOS_DIR \
    -Dthe_Foundation_DIR=$IOS_DIR/lib/cmake/the_Foundation \
    -DXCODE_DEVELOPMENT_TEAM=XXXXXXXXXX \
    -DENABLE_BINCAT_SH=YES \
    -DENABLE_DOWNLOAD_EDIT=NO
