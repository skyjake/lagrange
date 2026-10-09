cmake_minimum_required (VERSION 3.10)
project (LagrangeSync
    VERSION     1.0.0
    LANGUAGES   C
)
set (CMAKE_C_STANDARD 11)

if (TARGET the_Foundation::the_Foundation)
    # Part of the Lagrange project.
elseif (the_Foundation_DIR)
    find_package (the_Foundation REQUIRED)
else ()
    # Use the_Foundation from the Lagrange repository.
    set (TFDN_STATIC_LIBRARY     ON  CACHE BOOL "" FORCE)
    set (TFDN_ENABLE_INSTALL     OFF CACHE BOOL "" FORCE)
    set (TFDN_ENABLE_TESTS       OFF CACHE BOOL "" FORCE)
    set (TFDN_ENABLE_WEBREQUEST  OFF CACHE BOOL "" FORCE)
    add_subdirectory (${CMAKE_CURRENT_LIST_DIR}/../lib/the_Foundation the_Foundation)
    add_library (the_Foundation::the_Foundation ALIAS the_Foundation)
endif ()
set (SOURCES
    src/main.c
    src/server.c
    src/server.h
    src/storage.c
    src/storage.h
)

add_executable (lgrsyncd ${SOURCES})
target_compile_definitions (lgrsyncd PUBLIC
    LAGRANGE_SYNC_VERSION="${LagrangeSync_VERSION}"
)
target_link_libraries (lgrsyncd PUBLIC the_Foundation::the_Foundation)

include (GNUInstallDirs)
install (TARGETS lgrsyncd RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
