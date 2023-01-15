find_package (PkgConfig REQUIRED)

if (IOS)
    include (Depends-iOS)
    return ()
endif ()

if (ANDROID)
    include (Depends-Android)
    return ()
endif ()

find_program (MESON_EXECUTABLE meson DOC "Meson build system")
find_program (NINJA_EXECUTABLE ninja DOC "Ninja build tool")
include (ExternalProject)
set (_dependsToBuild)

if (DEFINED the_Foundation_DIR OR
    NOT EXISTS ${CMAKE_SOURCE_DIR}/lib/the_Foundation/CMakeLists.txt)
    set (INSTALL_THE_FOUNDATION YES)
    find_package (the_Foundation 1.6.0 REQUIRED)
else ()
    if (EXISTS ${CMAKE_SOURCE_DIR}/lib/the_Foundation/.git)
        # the_Foundation is checked out as a submodule, make sure it's up to date.
        find_package (Git)
        if (GIT_FOUND)
            execute_process (
                COMMAND ${GIT_EXECUTABLE} submodule update
                WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
                OUTPUT_VARIABLE subout
                OUTPUT_STRIP_TRAILING_WHITESPACE
            )
            if (subout)
                message (FATAL_ERROR "'lib/the_Foundation' Git submodule has been updated, please re-run CMake.\n")
            endif ()
        endif ()
    endif ()
    set (INSTALL_THE_FOUNDATION OFF)
    set (TFDN_STATIC_LIBRARY    ON  CACHE BOOL "")
    set (TFDN_ENABLE_INSTALL    OFF CACHE BOOL "")
    set (TFDN_ENABLE_TESTS      OFF CACHE BOOL "")
    set (TFDN_ENABLE_WEBREQUEST OFF CACHE BOOL "")
    add_subdirectory (lib/the_Foundation)
    add_library (the_Foundation::the_Foundation ALIAS the_Foundation)
    if (NOT OPENSSL_FOUND)
        message (FATAL_ERROR "Lagrange requires OpenSSL for TLS. Please check if pkg-config can find 'openssl'.")
    endif ()
    if (NOT ZLIB_FOUND)
        message (FATAL_ERROR "Lagrange requires zlib for reading compressed archives. Please check if pkg-config can find 'zlib'.")
    endif ()
endif ()

if (APPLE AND CMAKE_OSX_DEPLOYMENT_TARGET)
    set (_dependMacOpts
        -Dc_args=-mmacosx-version-min=${CMAKE_OSX_DEPLOYMENT_TARGET}
        -Dc_link_args=-mmacosx-version-min=${CMAKE_OSX_DEPLOYMENT_TARGET}
        -Dcpp_args=-mmacosx-version-min=${CMAKE_OSX_DEPLOYMENT_TARGET}
        -Dcpp_link_args=-mmacosx-version-min=${CMAKE_OSX_DEPLOYMENT_TARGET}
    )
endif ()

if (ENABLE_HARFBUZZ)
    # Find HarfBuzz with pkg-config.
    if (NOT ENABLE_HARFBUZZ_MINIMAL AND PKG_CONFIG_FOUND)
        pkg_check_modules (HARFBUZZ IMPORTED_TARGET harfbuzz)
    endif ()
    if (EXISTS ${CMAKE_SOURCE_DIR}/lib/harfbuzz/CMakeLists.txt AND
            (ENABLE_HARFBUZZ_MINIMAL OR NOT HARFBUZZ_FOUND))
        # Build HarfBuzz with minimal dependencies.
        if (MESON_EXECUTABLE AND NINJA_EXECUTABLE)
            set (_dst ${CMAKE_BINARY_DIR}/lib/harfbuzz)
            ExternalProject_Add (harfbuzz-ext
                PREFIX              ${CMAKE_BINARY_DIR}/harfbuzz-ext
                SOURCE_DIR          ${CMAKE_SOURCE_DIR}/lib/harfbuzz
                CONFIGURE_COMMAND   NINJA=${NINJA_EXECUTABLE} ${MESON_EXECUTABLE}
                                        ${CMAKE_SOURCE_DIR}/lib/harfbuzz
                                        -Dbuildtype=release
                                        -Ddefault_library=both
                                        -Dlibdir=lib
                                        -Dtests=disabled -Dglib=disabled -Dgobject=disabled
                                        -Dcairo=disabled -Dicu=disabled -Dfreetype=disabled
                                        -Ddocs=disabled
                                        ${_dependMacOpts}
                                        --prefix ${_dst}
                BUILD_COMMAND       ${NINJA_EXECUTABLE} install
                INSTALL_COMMAND     ""
            )
            list (APPEND _dependsToBuild harfbuzz-ext)
            add_library (harfbuzz-lib INTERFACE)
            target_include_directories (harfbuzz-lib INTERFACE ${_dst}/include/harfbuzz)
            if (MSYS)
                # Link dynamically.
                target_link_libraries (harfbuzz-lib INTERFACE -L${_dst}/lib harfbuzz)
                install (PROGRAMS ${_dst}/bin/msys-harfbuzz-0.dll DESTINATION .)
            else ()
                if (APPLE)
                    target_link_libraries (harfbuzz-lib INTERFACE ${_dst}/lib/libharfbuzz.0.dylib)
                    target_link_libraries (harfbuzz-lib INTERFACE c++)
                else ()
                    target_link_libraries (harfbuzz-lib INTERFACE ${_dst}/lib/libharfbuzz.a)
                    target_link_libraries (harfbuzz-lib INTERFACE stdc++)
                endif ()
            endif ()
            set (HARFBUZZ_FOUND YES)
        else ()
            # Try the CMake instead.
            set (HB_BUILD_SUBSET  OFF CACHE BOOL "" FORCE)
            set (HB_HAVE_CORETEXT OFF CACHE BOOL "" FORCE)
            set (HB_HAVE_FREETYPE OFF CACHE BOOL "" FORCE)
            set (HB_HAVE_GLIB     OFF CACHE BOOL "" FORCE)
            set (HB_HAVE_GOBJECT  OFF CACHE BOOL "" FORCE)
            set (HB_HAVE_ICU      OFF CACHE BOOL "" FORCE)
            set (SKIP_INSTALL_ALL YES CACHE BOOL "" FORCE)
            set (CMAKE_CXX_FLAGS)
            add_subdirectory (${CMAKE_SOURCE_DIR}/lib/harfbuzz)
            set (HARFBUZZ_LIBRARIES harfbuzz)
            # HarfBuzz is C++ so must link with the standard library.
            if (APPLE)
                list (APPEND HARFBUZZ_LIBRARIES c++)
            else ()
                list (APPEND HARFBUZZ_LIBRARIES stdc++)
            endif ()
            set (HARFBUZZ_FOUND YES)
            set (SKIP_INSTALL_ALL NO CACHE BOOL "" FORCE)
        endif ()
    endif ()
endif ()

if (ENABLE_FRIBIDI)
    # Find FriBidi with pkg-config.
    if (NOT ENABLE_FRIBIDI_BUILD AND PKG_CONFIG_FOUND)
        pkg_check_modules (FRIBIDI IMPORTED_TARGET fribidi)
    endif ()
    if (EXISTS ${CMAKE_SOURCE_DIR}/lib/fribidi AND (ENABLE_FRIBIDI_BUILD OR NOT FRIBIDI_FOUND))
        # Build FriBidi with Meson.
        set (_dst ${CMAKE_BINARY_DIR}/lib/fribidi)
        if (MESON_EXECUTABLE AND NINJA_EXECUTABLE)
            ExternalProject_Add (fribidi-ext
                PREFIX              ${CMAKE_BINARY_DIR}/fribidi-ext
                SOURCE_DIR          ${CMAKE_SOURCE_DIR}/lib/fribidi
                CONFIGURE_COMMAND   NINJA=${NINJA_EXECUTABLE} ${MESON_EXECUTABLE}
                                        ${CMAKE_SOURCE_DIR}/lib/fribidi
                                        -Dbuildtype=release
                                        -Ddefault_library=static
                                        -Dtests=false -Ddocs=false -Dbin=false
                                        -Dc_flags=-Wno-macro-redefined
                                        -Dlibdir=lib
                                        ${_dependMacOpts}
                                        --prefix ${_dst}
                BUILD_COMMAND       ${NINJA_EXECUTABLE} install
                INSTALL_COMMAND     ""
                BUILD_BYPRODUCTS    ${_dst}/lib/libfribidi.a
            )
            list (APPEND _dependsToBuild fribidi-ext)
        else ()
            message (FATAL_ERROR
                "GNU FriBidi must be built with Meson. Please install Meson and Ninja and try again, or provide FriBidi via pkg-config.")
        endif ()
        add_library (fribidi-lib INTERFACE)
        target_include_directories (fribidi-lib INTERFACE ${_dst}/include)
        target_link_libraries (fribidi-lib INTERFACE ${_dst}/lib/libfribidi.a)
        set (FRIBIDI_FOUND YES)
    endif ()
endif ()

add_custom_target (ext-deps DEPENDS ${_dependsToBuild})

if (ENABLE_SPARKLE)
    # macOS only.
    add_library (sparkle INTERFACE)
    set (SPARKLE_FRAMEWORK ${SPARKLE_DIR}/Sparkle.framework)
    target_link_libraries (sparkle INTERFACE ${SPARKLE_FRAMEWORK})
    target_compile_definitions (sparkle INTERFACE LAGRANGE_ENABLE_SPARKLE=1)
    message (STATUS "Using Sparkle: ${SPARKLE_FRAMEWORK}")
    if (NOT SPARKLE_ARCH)
        message (FATAL_ERROR "Set SPARKLE_ARCH to a CPU architecture ID (e.g., arm64)")
    endif ()
endif ()

if (ENABLE_WINSPARKLE)
    # Windows only.
    add_library (winsparkle INTERFACE)
    target_include_directories (winsparkle INTERFACE ${WINSPARKLE_DIR}/include)
    set (WINSPARKLE_DLL ${WINSPARKLE_DIR}/x64/Release/WinSparkle.dll)
    target_link_libraries (winsparkle INTERFACE ${WINSPARKLE_DLL})
    target_compile_definitions (winsparkle INTERFACE LAGRANGE_ENABLE_WINSPARKLE=1)
    install (
        PROGRAMS ${WINSPARKLE_DLL}
        DESTINATION .
    )
    message (STATUS "Using WinSparkle: ${WINSPARKLE_DLL}")
endif ()

if (ENABLE_X11_XLIB AND NOT ENABLE_TUI AND NOT APPLE AND NOT MSYS)
    find_package (X11)
    if (X11_FOUND)
        set (XLIB_FOUND YES)
        add_library (x11-lib INTERFACE)
        target_link_libraries (x11-lib INTERFACE ${X11_LIBRARIES})
        target_include_directories (x11-lib INTERFACE ${X11_INCLUDE_DIRS})
        target_compile_definitions (x11-lib INTERFACE LAGRANGE_ENABLE_X11_XLIB=1)
        message (STATUS "Using Xlib: ${X11_LIBRARIES}")
    endif ()
endif ()

if (ENABLE_TUI)
    pkg_check_modules (SDL2 REQUIRED sealcurses)
else ()
    pkg_check_modules (SDL2 REQUIRED sdl2)
endif ()
pkg_check_modules (MPG123 IMPORTED_TARGET libmpg123)
pkg_check_modules (WEBP IMPORTED_TARGET libwebp)
