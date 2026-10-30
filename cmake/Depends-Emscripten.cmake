# Dependencies for the WebAssembly build. They are built with platform/wasm-deps.sh
# into WASM_DIR.

if (NOT WASM_DIR)
    set (WASM_DIR $ENV{HOME}/SDK/wasm)
endif ()
if (NOT EXISTS ${WASM_DIR}/lib/libSDL3.a)
    message (FATAL_ERROR "WebAssembly dependencies not found in ${WASM_DIR} (run platform/wasm-deps.sh)")
endif ()
message (STATUS "WebAssembly dependency directory: ${WASM_DIR}")

# There is no system certificate store, so root CA certificates are bundled.
if (NOT CACERT_PEM_PATH AND EXISTS ${WASM_DIR}/share/cacert.pem)
    set (CACERT_PEM_PATH ${WASM_DIR}/share/cacert.pem CACHE FILEPATH
         "Root CA cacert.pem file to include as a built-in resource" FORCE)
endif ()

if (NOT EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/lib/the_Foundation/CMakeLists.txt)
    message (FATAL_ERROR "'lib/the_Foundation' Git submodule not found")
endif ()
set (INSTALL_THE_FOUNDATION OFF)
set (TFDN_STATIC_LIBRARY    ON  CACHE BOOL "" FORCE)
set (TFDN_ENABLE_INSTALL    OFF CACHE BOOL "" FORCE)
set (TFDN_ENABLE_TESTS      OFF CACHE BOOL "" FORCE)
set (TFDN_ENABLE_WEBREQUEST OFF CACHE BOOL "" FORCE)
set (TFDN_ENABLE_SSE41      OFF CACHE BOOL "" FORCE)
add_subdirectory (lib/the_Foundation)
add_library (the_Foundation::the_Foundation ALIAS the_Foundation)

# The package config provides the link options needed by SDL's Emscripten backend.
find_package (SDL3 CONFIG REQUIRED
    PATHS ${WASM_DIR}/lib/cmake/SDL3 NO_DEFAULT_PATH NO_CMAKE_FIND_ROOT_PATH
)
set (SDL3_LDFLAGS SDL3::SDL3-static)

set (FRIBIDI_FOUND YES)
set (FRIBIDI_LDFLAGS ${WASM_DIR}/lib/libfribidi.a)
set (FRIBIDI_INCLUDE_DIRS ${WASM_DIR}/include/fribidi)

set (HARFBUZZ_FOUND YES)
set (HARFBUZZ_LDFLAGS ${WASM_DIR}/lib/libharfbuzz.a)
set (HARFBUZZ_INCLUDE_DIRS ${WASM_DIR}/include/harfbuzz)

# Optional media decoders, if built by wasm-deps.sh.
if (EXISTS ${WASM_DIR}/lib/libwebpdecoder.a)
    set (WEBP_FOUND YES)
    set (WEBP_LIBRARIES ${WASM_DIR}/lib/libwebpdecoder.a)
    set (WEBP_INCLUDE_DIRS ${WASM_DIR}/include)
endif ()
if (EXISTS ${WASM_DIR}/lib/libmpg123.a)
    set (MPG123_FOUND YES)
    add_library (PkgConfig::MPG123 INTERFACE IMPORTED)
    set_target_properties (PkgConfig::MPG123 PROPERTIES
        INTERFACE_LINK_LIBRARIES ${WASM_DIR}/lib/libmpg123.a
        INTERFACE_INCLUDE_DIRECTORIES ${WASM_DIR}/include
    )
endif ()
if (EXISTS ${WASM_DIR}/lib/libopusfile.a)
    set (OPUSFILE_FOUND YES)
    add_library (PkgConfig::OPUSFILE INTERFACE IMPORTED)
    set_target_properties (PkgConfig::OPUSFILE PROPERTIES
        INTERFACE_LINK_LIBRARIES
            "${WASM_DIR}/lib/libopusfile.a;${WASM_DIR}/lib/libopus.a;${WASM_DIR}/lib/libogg.a"
        INTERFACE_INCLUDE_DIRECTORIES "${WASM_DIR}/include/opus;${WASM_DIR}/include"
    )
endif ()
