# Copyright: 2026 Jaakko Keränen <jaakko.keranen@iki.fi>
cmake_minimum_required (VERSION 3.10)

project (lagrange-core)

add_library (core STATIC
    src/core.c
    src/gmcerts.c
    src/gmrequest.c
    src/mimehooks.c
    src/gmutil.c
    src/gopher.c
    src/guppy.c
    src/lang.c
    src/resources.c
    src/prefs.c
    src/sitespec.c
    src/snippets.c
    src/visited.c
)

set_property (TARGET core PROPERTY C_STANDARD 11)

target_include_directories (core PUBLIC
    include
)

target_compile_definitions (core PUBLIC
    LAGRANGE_APP_VERSION="${CMAKE_PROJECT_VERSION}"
)

target_link_libraries (core PUBLIC
    the_Foundation::the_Foundation
)
