# Copyright: 2026 Jaakko Keränen <jaakko.keranen@iki.fi>
cmake_minimum_required (VERSION 3.10)

project (lagrange-core)

if (EMSCRIPTEN)
    set (DEFAULT_GUPPY OFF) # browsers have no UDP
else ()
    set (DEFAULT_GUPPY ON)
endif ()
option (ENABLE_GUPPY "Support the Guppy protocol" ${DEFAULT_GUPPY})

add_library (core STATIC
    src/builtin.c
    src/command.c
    src/core.c
    src/docprefs.c
    src/gmcerts.c
    src/gmrequest.c
    src/tools.c
    src/gmutil.c
    src/gopher.c
    src/hyphenator.c
    src/lang.c
    src/listfile.c
    src/resources.c
    src/prefs.c
    src/sitespec.c
    src/snippets.c
    src/visited.c
)
if (ENABLE_GUPPY)
    target_sources (core PRIVATE src/guppy.c)
    target_compile_definitions (core PUBLIC LAGRANGE_ENABLE_GUPPY=1)
endif ()

set_property (TARGET core PROPERTY C_STANDARD 11)
if (MSVC)
    target_compile_options (core PUBLIC
        /utf-8 /std:c11 /experimental:c11atomics
        /D_CRT_SECURE_NO_WARNINGS
    )
endif ()

target_include_directories (core PUBLIC
    include
)

target_compile_definitions (core PUBLIC
    LAGRANGE_APP_VERSION="${LAGRANGE_APP_VERSION}"
)

target_link_libraries (core PUBLIC
    the_Foundation::the_Foundation
)

add_executable (gmget test/gmget.c)
set_property (TARGET gmget PROPERTY C_STANDARD 11)
target_link_libraries (gmget PRIVATE core)
