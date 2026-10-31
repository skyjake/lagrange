# Decoder-only static libjxl for the dependency build scripts. Source this file,
# configure libjxl with "${JXL_DEC_OPTS[@]}", build the $JXL_DEC_TARGETS, then:
#
#   jxl_dec_install <build dir> <prefix>
#
# Lagrange only decodes, so JPEG reconstruction and metadata boxes are disabled
# as well. The only remaining dependency is highway (built by libjxl).

JXL_DEC_OPTS=(
    -DBUILD_SHARED_LIBS=OFF
    -DBUILD_TESTING=OFF
    -DJPEGXL_ENABLE_TRANSCODE_JPEG=OFF
    -DJPEGXL_ENABLE_BOXES=OFF
    -DJPEGXL_ENABLE_TOOLS=OFF
    -DJPEGXL_ENABLE_DEVTOOLS=OFF
    -DJPEGXL_ENABLE_JPEGLI=OFF
    -DJPEGXL_ENABLE_DOXYGEN=OFF
    -DJPEGXL_ENABLE_MANPAGES=OFF
    -DJPEGXL_ENABLE_BENCHMARK=OFF
    -DJPEGXL_ENABLE_EXAMPLES=OFF
    -DJPEGXL_ENABLE_JNI=OFF
    -DJPEGXL_ENABLE_SJPEG=OFF
    -DJPEGXL_ENABLE_OPENEXR=OFF
    -DJPEGXL_ENABLE_SKCMS=ON
    -DJPEGXL_ENABLE_VIEWERS=OFF
    -DJPEGXL_ENABLE_TCMALLOC=OFF
    -DJPEGXL_ENABLE_PLUGINS=OFF
    -DJPEGXL_ENABLE_FUZZERS=OFF
    -DJPEGXL_BUNDLE_LIBPNG=OFF
    -DJPEGXL_WARNINGS_AS_ERRORS=OFF
)
JXL_DEC_TARGETS="jxl_dec jxl_threads"

function jxl_dec_install() {
    local build=$1 prefix=$2 cxxlib=-lstdc++ version lib
    grep -q '^LIBCXX:INTERNAL=1' $build/CMakeCache.txt && cxxlib=-lc++
    version=$(sed -n 's/^Version: //p' $build/lib/libjxl_threads.pc)
    # Remove a full libjxl from earlier installs.
    for lib in jxl jxl_cms jxl_threads jxl_dec hwy brotlicommon brotlidec brotlienc; do
        rm -f $prefix/lib/lib$lib.* $prefix/lib/pkgconfig/lib$lib.pc
    done
    rm -rf $prefix/include/jxl
    mkdir -p $prefix/include $prefix/lib/pkgconfig
    cp -R $build/lib/include/jxl $prefix/include/
    cp $build/lib/libjxl_dec.a $build/lib/libjxl_threads.a \
       $build/third_party/highway/libhwy.a $prefix/lib/
    cat > $prefix/lib/pkgconfig/libjxl_dec.pc << EOF
prefix=$prefix
libdir=\${prefix}/lib
includedir=\${prefix}/include

Name: libjxl_dec
Description: JPEG XL decoder (static)
Version: $version
Libs: -L\${libdir} -ljxl_dec -lhwy $cxxlib -lm
Cflags: -I\${includedir} -DJXL_STATIC_DEFINE
EOF
    cat > $prefix/lib/pkgconfig/libjxl_threads.pc << EOF
prefix=$prefix
libdir=\${prefix}/lib
includedir=\${prefix}/include

Name: libjxl_threads
Description: JPEG XL multi-thread runner (static)
Version: $version
Libs: -L\${libdir} -ljxl_threads $cxxlib
Cflags: -I\${includedir} -DJXL_THREADS_STATIC_DEFINE
EOF
}
