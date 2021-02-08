# Lagrange - A Beautiful Gemini Client
# Copyright: 2020 Jaakko Keränen <jaakko.keranen@iki.fi>
#
# Notes:
# - Required dependencies: SDL 2, OpenSSL 1.1.1, libpcre,
#   GNU libunistring, zlib.
# - the_Foundation is built as a static library from 'lib/the_Foundation',
#   if it exists in that location. The Git repository has it as a submodule.
# - Windows builds require MSYS2. In theory, Clang could be set up on
#   Windows for compiling everything, but the_Foundation still lacks
#   native Win32 implementations for the Socket and Process classes.
# - Windows builds should use the SDL 2 library precompiled for native
#   Windows (MSVC variant) instead of the version from MSYS2 (get it from
#   https://libsdl.org/). To make configuration easier, consider writing
#   for your personal use a pkg-config sdl2.pc file that uses the Windows
#   version of the library.

cmake_minimum_required (VERSION 3.9)

project (Lagrange
    VERSION 1.1.2
    DESCRIPTION "A Beautiful Gemini Client"
    LANGUAGES C
)
set (COPYRIGHT_YEAR 2021)

# Build configuration.
option (ENABLE_MPG123           "Use mpg123 for decoding MPEG audio" ON)
option (ENABLE_X11_SWRENDER     "Use software rendering under X11" OFF)
option (ENABLE_KERNING          "Enable kerning in font renderer (slower)" ON)
option (ENABLE_RESOURCE_EMBED   "Embed resources inside the executable" OFF)
option (ENABLE_WINDOWPOS_FIX    "Set position after showing window (workaround for SDL bug)" OFF)
option (ENABLE_IDLE_SLEEP       "While idle, sleep in the main thread instead of waiting for events" ON)
option (ENABLE_DOWNLOAD_EDIT    "Allow changing the Downloads directory" ON)

include (BuildType.cmake)
include (res/Embed.cmake)
if (NOT EXISTS ${CMAKE_SOURCE_DIR}/lib/the_Foundation/CMakeLists.txt)
    set (INSTALL_THE_FOUNDATION YES)
    find_package (the_Foundation REQUIRED)
else ()
    set (INSTALL_THE_FOUNDATION NO)
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

# Embedded resources are written to a generated source file.
message (STATUS "Preparing embedded resources...")
# Fonts are too large to comfortably embed as a C source.
set (EMBED_RESOURCES
    res/about/help.gmi
    res/about/lagrange.gmi
    res/about/license.gmi
    res/about/version.gmi
    res/fonts/Tinos-Regular.ttf
    res/fonts/Tinos-Italic.ttf
    res/fonts/Tinos-Bold.ttf
    res/fonts/IosevkaTerm-Extended.ttf
    res/fonts/FiraSans-Bold.ttf
    res/fonts/FiraSans-Italic.ttf
    res/fonts/FiraSans-Light.ttf
    res/fonts/FiraSans-Regular.ttf
    res/fonts/Literata-Regular-opsz=14.ttf
    res/fonts/Literata-Bold-opsz=36.ttf
    res/fonts/Literata-ExtraLight-opsz=18.ttf
    res/fonts/Literata-LightItalic-opsz=10.ttf
    res/fonts/NanumGothic-Regular.ttf
    res/fonts/NotoEmoji-Regular.ttf
    res/fonts/NotoSansJP-Regular.ttf
    res/fonts/Nunito-ExtraBold.ttf
    res/fonts/Nunito-ExtraLight.ttf
    res/fonts/Nunito-LightItalic.ttf
    res/fonts/Nunito-Regular.ttf
    res/fonts/SourceSansPro-Regular.ttf
    res/fonts/SourceSansPro-Bold.ttf
    res/fonts/Symbola.ttf
)
if (UNIX AND NOT APPLE)
    list (APPEND EMBED_RESOURCES res/lagrange-64.png)
endif ()
embed_make (${EMBED_RESOURCES})

set (EMB_BIN ${CMAKE_CURRENT_BINARY_DIR}/resources.lgr)
set_source_files_properties (${EMB_BIN} PROPERTIES MACOSX_PACKAGE_LOCATION Resources)

# Source files.
set (SOURCES
    src/main.c
    src/app.c
    src/app.h
    src/bookmarks.c
    src/bookmarks.h
    src/defs.h
    src/feeds.c
    src/feeds.h
    src/gmcerts.c
    src/gmcerts.h
    src/gmdocument.c
    src/gmdocument.h
    src/gmrequest.c
    src/gmrequest.h
    src/gmutil.c
    src/gmutil.h
    src/gopher.c
    src/gopher.h
    src/history.c
    src/history.h
    src/lookup.c
    src/lookup.h
    src/media.c
    src/media.h
    src/mimehooks.c
    src/mimehooks.h
    src/prefs.c
    src/prefs.h
    src/stb_image.h
    src/stb_truetype.h
    src/visited.c
    src/visited.h
    # Audio playback:
    src/audio/buf.c
    src/audio/buf.h
    src/audio/player.c
    src/audio/player.h
    src/audio/stb_vorbis.c
    # User interface:
    src/ui/bindingswidget.c
    src/ui/bindingswidget.h
    src/ui/certimportwidget.c
    src/ui/certimportwidget.h
    src/ui/color.c
    src/ui/color.h
    src/ui/command.c
    src/ui/command.h
    src/ui/documentwidget.c
    src/ui/documentwidget.h
    src/ui/indicatorwidget.c
    src/ui/indicatorwidget.h
    src/ui/listwidget.c
    src/ui/listwidget.h
    src/ui/lookupwidget.c
    src/ui/lookupwidget.h
    src/ui/keys.c
    src/ui/keys.h
    src/ui/metrics.c
    src/ui/metrics.h
    src/ui/paint.c
    src/ui/paint.h
    src/ui/playerui.c
    src/ui/playerui.h
    src/ui/scrollwidget.c
    src/ui/scrollwidget.h
    src/ui/sidebarwidget.c
    src/ui/sidebarwidget.h
    src/ui/text.c
    src/ui/text.h
    src/ui/util.c
    src/ui/util.h
    src/ui/visbuf.c
    src/ui/visbuf.h
    src/ui/window.c
    src/ui/window.h
    # Widgets:
    src/ui/widget.c
    src/ui/widget.h
    src/ui/inputwidget.c
    src/ui/inputwidget.h
    src/ui/labelwidget.c
    src/ui/labelwidget.h
    # Resources:
    res/about/help.gmi
    res/about/lagrange.gmi
    res/about/version.gmi
    ${CMAKE_CURRENT_BINARY_DIR}/resources.lgr
    ${CMAKE_CURRENT_BINARY_DIR}/embedded.c
    ${CMAKE_CURRENT_BINARY_DIR}/embedded.h
)
if (IOS)
elseif (APPLE)
    list (APPEND SOURCES src/macos.m src/macos.h)
    list (APPEND RESOURCES "res/Lagrange.icns")
endif ()
if (MSYS)
    set (WINRC_FILE_VERSION ${PROJECT_VERSION_MAJOR},${PROJECT_VERSION_MINOR},${PROJECT_VERSION_PATCH},0)
    set (WINRC_PRODUCT_VERSION ${PROJECT_VERSION_MAJOR},${PROJECT_VERSION_MINOR},0,0)
    configure_file (res/lagrange.rc.in ${CMAKE_CURRENT_BINARY_DIR}/lagrange.rc NEWLINE_STYLE WIN32)
    list (APPEND SOURCES src/win32.c src/win32.h ${CMAKE_CURRENT_BINARY_DIR}/lagrange.rc)
endif ()
set_source_files_properties (${RESOURCES} PROPERTIES MACOSX_PACKAGE_LOCATION Resources)

# Target.
add_executable (app ${SOURCES} ${RESOURCES})
set_target_properties (app PROPERTIES OUTPUT_NAME lagrange)
target_include_directories (app PUBLIC
    src
    ${CMAKE_CURRENT_BINARY_DIR}
    ${SDL2_INCLUDE_DIRS}
)
target_compile_options (app PUBLIC
    -Werror=implicit-function-declaration
    -Werror=incompatible-pointer-types
    ${SDL2_CFLAGS}
    -DSTB_VORBIS_NO_STDIO=1
    -DSTB_VORBIS_NO_INTEGER_CONVERSION=1
)
target_compile_definitions (app PUBLIC LAGRANGE_APP_VERSION="${PROJECT_VERSION}")
if (ENABLE_X11_SWRENDER)
    target_compile_definitions (app PUBLIC LAGRANGE_ENABLE_X11_SWRENDER=1)
endif ()
if (ENABLE_KERNING)
    target_compile_definitions (app PUBLIC LAGRANGE_ENABLE_KERNING=1)
endif ()
if (ENABLE_WINDOWPOS_FIX)
    target_compile_definitions (app PUBLIC LAGRANGE_ENABLE_WINDOWPOS_FIX=1)
endif ()
if (ENABLE_MPG123 AND MPG123_FOUND)
    target_compile_definitions (app PUBLIC LAGRANGE_ENABLE_MPG123=1)
    target_link_libraries (app PUBLIC PkgConfig::MPG123)
endif ()
if (ENABLE_IDLE_SLEEP)
    target_compile_definitions (app PUBLIC LAGRANGE_IDLE_SLEEP=1)
endif ()
if (ENABLE_DOWNLOAD_EDIT)
    target_compile_definitions (app PUBLIC LAGRANGE_DOWNLOAD_EDIT=1)
endif ()
target_link_libraries (app PUBLIC the_Foundation::the_Foundation)
target_link_libraries (app PUBLIC ${SDL2_LDFLAGS})
if (APPLE)
    if (IOS)
        target_link_libraries (app PUBLIC "-framework UIKit")
    else ()
        target_link_libraries (app PUBLIC "-framework AppKit")
    endif ()
    if (CMAKE_OSX_DEPLOYMENT_TARGET)
        target_compile_options (app PUBLIC -mmacosx-version-min=${CMAKE_OSX_DEPLOYMENT_TARGET})
        target_link_options (app PUBLIC -mmacosx-version-min=${CMAKE_OSX_DEPLOYMENT_TARGET})
    endif ()
    set_target_properties (app PROPERTIES
        OUTPUT_NAME "Lagrange"
        BUILD_RPATH ${SDL2_LIBRARY_DIRS}
        MACOSX_BUNDLE YES
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_LIST_DIR}/res/MacOSXBundleInfo.plist.in"
        MACOSX_BUNDLE_BUNDLE_NAME "Lagrange"
        MACOSX_BUNDLE_INFO_STRING "${PROJECT_VERSION}"
        MACOSX_BUNDLE_ICON_FILE "Lagrange.icns"
        MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
        MACOSX_BUNDLE_LONG_VERSION_STRING "${PROJECT_VERSION}"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}"
        MACOSX_BUNDLE_GUI_IDENTIFIER "fi.skyjake.Lagrange"
        MACOSX_BUNDLE_COPYRIGHT "© ${COPYRIGHT_YEAR} Jaakko Keränen"
        XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER "fi.skyjake.Lagrange"
    )
endif ()
if (MSYS)
    target_link_libraries (app PUBLIC d2d1 uuid) # querying DPI
endif ()
if (UNIX)
    target_link_libraries (app PUBLIC m)
endif ()

# Deployment.
if (MSYS)
    install (TARGETS app DESTINATION .)
    if (TARGET PkgConfig::MPG123)
        install (PROGRAMS ${MPG123_LIBDIR}/../bin/msys-mpg123-0.dll DESTINATION .)
    endif ()
    if (NOT ENABLE_RESOURCE_EMBED)
        install (FILES ${EMB_BIN} DESTINATION .)
    endif ()
    install (PROGRAMS
        ${SDL2_LIBDIR}/SDL2.dll
        res/urlopen.bat
        DESTINATION .
    )
    if (INSTALL_THE_FOUNDATION)
        install (PROGRAMS $<TARGET_FILE:the_Foundation::the_Foundation> DESTINATION .)
    endif ()
elseif (UNIX AND NOT APPLE)
    set_target_properties (app PROPERTIES
        INSTALL_RPATH_USE_LINK_PATH YES
    )
    set (desktop ${CMAKE_CURRENT_BINARY_DIR}/fi.skyjake.lagrange.desktop)
    file (WRITE ${desktop} "[Desktop Entry]
Name=Lagrange
Comment=${PROJECT_DESCRIPTION}
Categories=Network
Exec=${CMAKE_INSTALL_PREFIX}/bin/lagrange %U
Terminal=false
Type=Application
Icon=fi.skyjake.lagrange
MimeType=x-scheme-handler/gemini;x-scheme-handler/gopher
")
    install (TARGETS app DESTINATION bin)
    install (FILES ${desktop} DESTINATION share/applications)
    install (FILES res/lagrange-256.png
         DESTINATION share/icons/hicolor/256x256/apps
         RENAME fi.skyjake.lagrange.png
    )
    install (FILES res/fi.skyjake.Lagrange.appdata.xml
        DESTINATION share/metainfo
    )
    if (NOT ENABLE_RESOURCE_EMBED)
        target_compile_definitions (app PUBLIC
            LAGRANGE_EMB_BIN="${CMAKE_INSTALL_PREFIX}/share/lagrange/resources.lgr")
        install (FILES ${EMB_BIN} DESTINATION share/lagrange)
    endif ()
endif ()
