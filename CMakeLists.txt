cmake_minimum_required (VERSION 3.10)

project (Lagrange
    VERSION 2.0
    DESCRIPTION "A Beautiful Gemini Client"
    LANGUAGES C
)
set (COPYRIGHT_YEAR 2026)
if (IOS)
    set (PROJECT_VERSION 1.20)
    set (IOS_BUNDLE_VERSION 10)
    set (IOS_BUILD_DATE "2026-06-07")
endif ()
if (ANDROID)
    set (PROJECT_VERSION 1.20)
    set (ANDROID_BUILD_VERSION b51)  # remember to update Gradle, AndroidManifest.xml
    set (ANDROID_BUILD_DATE "2026-04-16")
endif ()

# Load modules from the source tree
set (CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} ${CMAKE_CURRENT_SOURCE_DIR}/cmake)

include (BuildType)
include (Resources)
include (Depends)
include (CheckCCompilerFlag)

add_subdirectory (lib/core)
add_subdirectory (portable)
