message (STATUS "iOS dependency directory: ${IOS_DIR}")

find_package (the_Foundation REQUIRED)

set (SDL2_INCLUDE_DIRS ${IOS_DIR}/include/SDL2)
set (SDL2_LDFLAGS 
    ${IOS_DIR}/lib/libSDL2.a
    "-framework AudioToolbox"
    "-framework AVFoundation"
    "-framework AVFAudio"
    "-framework CoreAudio"
    "-framework CoreGraphics"
    "-framework CoreHaptics"
    "-framework CoreMotion"
    "-framework Foundation"
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
set (FRIBIDI_LIBRARIES ${IOS_DIR}/lib/libfribidi.a)

set (HARFBUZZ_FOUND YES)
set (HARFBUZZ_LIBRARIES ${IOS_DIR}/lib/libharfbuzz.a)
set (HARFBUZZ_INCLUDE_DIRS ${IOS_DIR}/include/harfbuzz)
