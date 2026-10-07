cmake_minimum_required (VERSION 3.15)

project (Lagrange
    VERSION 2.0
    DESCRIPTION "A Beautiful Gemini Client"
    LANGUAGES C
)
set (COPYRIGHT_YEAR 2026)
if (IOS)
    set (PROJECT_VERSION 2.0)
    set (IOS_BUNDLE_VERSION 1)
    set (IOS_BUILD_DATE "2026-10-07")
endif ()
if (ANDROID)
    set (PROJECT_VERSION 1.21)
    set (ANDROID_BUILD_VERSION b60)  # remember to update Gradle, AndroidManifest.xml
    set (ANDROID_BUILD_DATE "2026-09-20")
endif ()
set (LAGRANGE_APP_VERSION ${PROJECT_VERSION}) # subprojects have their own PROJECT_VERSION

# Load modules from the source tree
set (CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} ${CMAKE_CURRENT_SOURCE_DIR}/cmake)

include (BuildType)
include (Resources)
include (Summary)
include (Depends)
include (CheckCCompilerFlag)

add_subdirectory (lib/core)
add_subdirectory (app)
