#!/bin/sh
cmake ../the_Foundation \
    -DCMAKE_TOOLCHAIN_FILE=$HOME/src/libs/ios-cmake/ios.toolchain.cmake \
    -DCMAKE_BUILD_TYPE=RelWithDebInfo \
    -DPLATFORM=OS64 \
    -DENABLE_BITCODE=0 \
    -DTFDN_STATIC_LIBRARY=YES \
    -DTFDN_ENABLE_TESTS=NO \
    -DTFDN_ENABLE_WEBREQUEST=NO \
    -DIOS_DIR=$HOME/SDK/ios/arm64 \
    -DCMAKE_INSTALL_PREFIX=$HOME/SDK/ios/arm64 
