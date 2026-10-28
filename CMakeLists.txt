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
    set (IOS_BUILD_DATE "2026-10-09")
endif ()
if (ANDROID)
    set (PROJECT_VERSION 2.0)
    set (ANDROID_BUILD_VERSION 0)  # remember to update Gradle, AndroidManifest.xml
    set (ANDROID_BUILD_DATE "2026-10-28")
endif ()
set (LAGRANGE_APP_VERSION ${PROJECT_VERSION}) # subprojects have their own PROJECT_VERSION

#-------------------------------------------------------------------------------------------------
# Project-wide build options

if (NOT APPLE AND NOT IOS)
    set (DEFAULT_FREETYPE ON)
else ()
    set (DEFAULT_FREETYPE OFF)
endif ()

option (ENABLE_GUI              "Build the GUI application" ON)
option (ENABLE_TUI              "Build clagrange (TUI based on ncurses)" OFF)
option (ENABLE_STATIC           "Prefer linking dependencies statically" OFF)

option (ENABLE_CORETEXT         "Use Core Text for text rendering (macOS/iOS)" ${APPLE})
option (ENABLE_FREETYPE         "Use FreeType for text rendering (Windows/Linux)" ${DEFAULT_FREETYPE})
option (ENABLE_FRIBIDI          "Use the GNU FriBidi library for bidirectional text" ON)
option (ENABLE_FRIBIDI_BUILD    "Build the GNU FriBidi library (if OFF, try pkg-config)" OFF)
option (ENABLE_HARFBUZZ         "Use the HarfBuzz library to shape text" ON)
option (ENABLE_HARFBUZZ_MINIMAL "Build the HarfBuzz library with minimal dependencies (if OFF, try pkg-config)" OFF)
option (ENABLE_JXL              "Use libjxl to decode .jxl images (via pkg-config)" ON)
option (ENABLE_MPG123           "Use mpg123 for decoding MPEG audio" ON)
option (ENABLE_OPUS             "Use llibopusfile for decoding Opus audio (via pkg-config)" ON)
option (ENABLE_SPARKLE          "Use Sparkle for automatic updates (macOS)" OFF)
option (ENABLE_WEBP             "Use libwebp to decode .webp images (via pkg-config)" ON)
option (ENABLE_WINSPARKLE       "Use WinSparkle for automatic updates (Windows)" OFF)
option (ENABLE_X11_XLIB         "Use Xlib directly, if available (X11)" ON)

if (ENABLE_CORETEXT)
    if (NOT APPLE)
        message (FATAL_ERROR "Core Text is only available on Apple platforms")
    endif ()
    if (ENABLE_FREETYPE)
        message (FATAL_ERROR "ENABLE_CORETEXT and ENABLE_FREETYPE are mutually exclusive")
    endif ()
    # Core Text replaces HarfBuzz and FriBidi on Apple platforms.
    set (ENABLE_HARFBUZZ OFF)
    set (ENABLE_FRIBIDI  OFF)
endif ()
if (ENABLE_FREETYPE)
    # Shaping with FreeType requires HarfBuzz.
    set (ENABLE_HARFBUZZ ON CACHE BOOL "" FORCE)
endif ()

#-------------------------------------------------------------------------------------------------

# Load modules from the source tree
set (CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} ${CMAKE_CURRENT_SOURCE_DIR}/cmake)

include (BuildType)
include (Resources)
include (Summary)
include (Depends)
include (CheckCCompilerFlag)

add_subdirectory (lib/core)
if (ENABLE_GUI OR ENABLE_TUI)
    add_subdirectory (app)
endif ()
if (NOT IOS AND NOT ANDROID AND NOT EMSCRIPTEN)
    # Sync server (lgrsyncd) is built by default only if there is no app.
    if (TARGET app OR TARGET tuiapp)
        add_subdirectory (sync EXCLUDE_FROM_ALL)
    else ()
        add_subdirectory (sync)
    endif ()
endif ()
