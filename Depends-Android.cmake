if (NOT EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/lib/the_Foundation/.git)
    message (FATAL_ERROR "'lib/the_Foundation' Git submodule not found")
endif ()

# the_Foundation is checked out as a submodule, make sure it's up to date.
find_package (Git)
if (GIT_FOUND)
    execute_process (
        COMMAND ${GIT_EXECUTABLE} submodule update
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        OUTPUT_VARIABLE subout
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )
    if (subout)
        message (FATAL_ERROR "'lib/the_Foundation' Git submodule has been updated, please re-run CMake.\n")
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