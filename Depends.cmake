if (IOS)
    include (Depends-iOS.cmake)
    return ()
endif ()

find_package (PkgConfig)
find_program (MESON_EXECUTABLE meson DOC "Meson build system")
find_program (NINJA_EXECUTABLE ninja DOC "Ninja build tool")
include (ExternalProject)

if (ENABLE_HARFBUZZ AND EXISTS ${CMAKE_SOURCE_DIR}/lib/harfbuzz/CMakeLists.txt)
    # Find HarfBuzz with pkg-config.
    if (NOT ENABLE_HARFBUZZ_MINIMAL AND PKG_CONFIG_FOUND)
        pkg_check_modules (HARFBUZZ IMPORTED_TARGET harfbuzz)
        if (HARFBUZZ_FOUND)
            add_library (harfbuzz-lib ALIAS PkgConfig::HARFBUZZ)
        endif ()
    endif ()
    if (ENABLE_HARFBUZZ_MINIMAL OR NOT HARFBUZZ_FOUND)
        # Build HarfBuzz with minimal dependencies.
        if (MESON_EXECUTABLE AND NINJA_EXECUTABLE)
            set (_dst ${CMAKE_BINARY_DIR}/lib/harfbuzz)
            ExternalProject_Add (harfbuzz-ext
                PREFIX              ${CMAKE_BINARY_DIR}/harfbuzz-ext
                SOURCE_DIR          ${CMAKE_SOURCE_DIR}/lib/harfbuzz
                CONFIGURE_COMMAND   NINJA=${NINJA_EXECUTABLE} ${MESON_EXECUTABLE}
                                        ${CMAKE_SOURCE_DIR}/lib/harfbuzz
                                        -Dbuildtype=release
                                        -Dtests=disabled -Dglib=disabled -Dgobject=disabled
                                        -Dcairo=disabled -Dicu=disabled -Dfreetype=disabled
                                        -Ddocs=disabled
                                        --prefix ${_dst}
                BUILD_COMMAND       ${NINJA_EXECUTABLE}
                INSTALL_COMMAND     ${NINJA_EXECUTABLE} install
            )
            add_library (harfbuzz-lib INTERFACE)
            add_dependencies (harfbuzz-lib harfbuzz-ext)
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
                    target_link_libraries (harfbuzz-lib INTERFACE ${_dst}/libharfbuzz.a)
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

if (ENABLE_FRIBIDI AND EXISTS ${CMAKE_SOURCE_DIR}/lib/fribidi)
    # Find FriBidi with pkg-config.
    if (NOT ENABLE_FRIBIDI_BUILD AND PKG_CONFIG_FOUND)
        pkg_check_modules (FRIBIDI IMPORTED_TARGET fribidi)
        if (FRIBIDI_FOUND)
            add_library (fribidi-lib ALIAS PkgConfig::FRIBIDI)
        endif ()
    endif ()
    if (ENABLE_FRIBIDI_BUILD OR NOT FRIBIDI_FOUND)
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
                                        --prefix ${_dst}
                BUILD_COMMAND       ${NINJA_EXECUTABLE}
                INSTALL_COMMAND     ${NINJA_EXECUTABLE} install
                BUILD_BYPRODUCTS    ${_dst}/lib/libfribidi.a
            )
        else ()
            message (FATAL_ERROR
                "GNU FriBidi must be built with Meson. Please install Meson and Ninja and try again, or provide FriBidi via pkg-config.")
        endif ()
        add_library (fribidi-lib INTERFACE)
        add_dependencies (fribidi-lib fribidi-ext)
        target_include_directories (fribidi-lib INTERFACE ${_dst}/include)
        target_link_libraries (fribidi-lib INTERFACE ${_dst}/lib/libfribidi.a)
        set (FRIBIDI_FOUND YES)
    endif ()
endif ()

if (NOT EXISTS ${CMAKE_SOURCE_DIR}/lib/the_Foundation/CMakeLists.txt)
    set (INSTALL_THE_FOUNDATION YES)
    find_package (the_Foundation REQUIRED)
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

find_package (PkgConfig REQUIRED)
pkg_check_modules (SDL2 REQUIRED sdl2)
pkg_check_modules (MPG123 IMPORTED_TARGET libmpg123)
