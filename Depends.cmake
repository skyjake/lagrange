if (IOS)
    include (Depends-iOS.cmake)
    return ()
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
endif ()
find_package (PkgConfig REQUIRED)
pkg_check_modules (SDL2 REQUIRED sdl2)
pkg_check_modules (MPG123 IMPORTED_TARGET libmpg123)
