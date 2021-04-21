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
    "-framework Metal"
    "-framework OpenGLES"
    "-framework QuartzCore"
    "-framework UIKit"
)
