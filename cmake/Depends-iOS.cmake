message (STATUS "iOS dependency directory: ${IOS_DIR}")

find_package (the_Foundation REQUIRED)

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

pkg_check_modules (WEBP IMPORTED_TARGET libwebpdecoder)

set (FRIBIDI_FOUND YES)
set (FRIBIDI_LDFLAGS ${IOS_DIR}/lib/libfribidi.a)
set (FRIBIDI_INCLUDE_DIRS ${IOS_DIR}/include/fribidi)

set (HARFBUZZ_FOUND YES)
set (HARFBUZZ_LDFLAGS ${IOS_DIR}/lib/libharfbuzz.a)
set (HARFBUZZ_INCLUDE_DIRS ${IOS_DIR}/include/harfbuzz)
