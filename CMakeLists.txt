cmake_minimum_required (VERSION 3.10)

project (Lagrange
    VERSION 1.20.1
    DESCRIPTION "A Beautiful Gemini Client"
    LANGUAGES C
)
set (COPYRIGHT_YEAR 2026)
if (IOS)
    set (PROJECT_VERSION 1.20)
    set (IOS_BUNDLE_VERSION 4)
    set (IOS_BUILD_DATE "2026-03-15")
endif ()
if (ANDROID)
    set (PROJECT_VERSION 1.20)
    set (ANDROID_BUILD_VERSION b48)  # remember to update Gradle, AndroidManifest.xml
    set (ANDROID_BUILD_DATE "2026-03-18")
endif ()

# Load modules from the source tree
set (CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} ${CMAKE_CURRENT_SOURCE_DIR}/cmake)

include (BuildType)
include (Resources)
include (Depends)
include (CheckCCompilerFlag)

add_subdirectory (lib/core)
add_subdirectory (portable)
