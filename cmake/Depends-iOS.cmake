message (STATUS "iOS dependency directory: ${IOS_DIR}")

find_package (the_Foundation REQUIRED)

set (SDL3_FOUND YES)
set (SDL3_INCLUDE_DIRS ${IOS_DIR}/include/SDL3)
set (SDL3_LDFLAGS
    ${IOS_DIR}/SDL3.framework/SDL3
    "-framework AudioToolbox"
    "-framework AVFoundation"
    "-framework AVFAudio"
    "-framework CoreAudio"
    "-framework CoreGraphics"
    "-framework CoreHaptics"
    "-framework CoreMotion"
    "-framework Foundation"
    "-framework GameController"
    "-framework MediaPlayer"
    "-framework Metal"
    "-framework OpenGLES"
    "-framework QuartzCore"
    "-framework UIKit"
)

if (EXISTS ${IOS_DIR}/lib/libwebpdecoder.a)
    set (WEBP_FOUND YES)
    set (WEBP_INCLUDE_DIRS ${IOS_DIR}/include)
    set (WEBP_LIBRARIES ${IOS_DIR}/lib/libwebpdecoder.a)
endif ()

if (EXISTS ${IOS_DIR}/lib/libjxl.a)
    set (JXL_FOUND YES)
    set (JXL_INCLUDE_DIRS ${IOS_DIR}/include)
    set (JXL_LIBRARIES)
    foreach (lib jxl jxl_threads jxl_cms hwy brotlienc brotlidec brotlicommon)
        list (APPEND JXL_LIBRARIES ${IOS_DIR}/lib/lib${lib}.a)
    endforeach ()
    list (APPEND JXL_LIBRARIES c++)
endif ()

set (FRIBIDI_FOUND YES)
set (FRIBIDI_LDFLAGS ${IOS_DIR}/lib/libfribidi.a)
set (FRIBIDI_INCLUDE_DIRS ${IOS_DIR}/include/fribidi)

set (HARFBUZZ_FOUND YES)
set (HARFBUZZ_LDFLAGS ${IOS_DIR}/lib/libharfbuzz.a)
set (HARFBUZZ_INCLUDE_DIRS ${IOS_DIR}/include/harfbuzz)
