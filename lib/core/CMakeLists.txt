# Copyright: 2026 Jaakko Keränen <jaakko.keranen@iki.fi>
cmake_minimum_required (VERSION 3.10)

project (lagrange-core)

add_library (core STATIC
    src/core.c
)

set_property (TARGET core PROPERTY C_STANDARD 11)

target_include_directories (core PUBLIC
    include
)

target_link_libraries (core PUBLIC
    the_Foundation::the_Foundation
)
