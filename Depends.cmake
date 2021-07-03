if (IOS)
    include (Depends-iOS.cmake)
    return ()
endif ()

if (ENABLE_HARFBUZZ AND EXISTS ${CMAKE_SOURCE_DIR}/lib/harfbuzz/CMakeLists.txt)
    # Build HarfBuzz with minimal dependencies.
    set (HB_BUILD_SUBSET  OFF CACHE BOOL "" FORCE)
    set (HB_HAVE_CORETEXT OFF CACHE BOOL "" FORCE)
    set (HB_HAVE_FREETYPE OFF CACHE BOOL "" FORCE)
    set (HB_HAVE_GLIB     OFF CACHE BOOL "" FORCE)
    set (HB_HAVE_GOBJECT  OFF CACHE BOOL "" FORCE)
    set (HB_HAVE_ICU      OFF CACHE BOOL "" FORCE)
    cmake_policy (SET CMP0075 NEW) 
    add_subdirectory (${CMAKE_SOURCE_DIR}/lib/harfbuzz)
    set (HARFBUZZ_FOUND YES)
endif ()

if (ENABLE_FRIBIDI AND EXISTS ${CMAKE_SOURCE_DIR}/lib/fribidi)
    # Build FriBidi with Meson.
    include (ExternalProject)
    set (_dst ${CMAKE_BINARY_DIR}/lib/fribidi)
    ExternalProject_Add (fribidi
        PREFIX              ${CMAKE_BINARY_DIR}/fribidi-ext
        SOURCE_DIR          ${CMAKE_SOURCE_DIR}/lib/fribidi
        CONFIGURE_COMMAND   meson ${CMAKE_SOURCE_DIR}/lib/fribidi 
                                -Dtests=false -Ddocs=false -Dbin=false
                                --prefix ${_dst}
        BUILD_COMMAND       ninja
        INSTALL_COMMAND     ninja install
    )
    add_library (fribidi-lib INTERFACE)
    target_include_directories (fribidi-lib INTERFACE ${_dst}/include)
    target_link_libraries (fribidi-lib INTERFACE -L${_dst}/lib fribidi)
    set (FRIBIDI_FOUND YES)
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
                message (FATAL_ERROR "The 'lib/the_Foundation' submodule has been updated, please re-run CMake.\n")
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
