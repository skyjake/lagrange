cmake_minimum_required (VERSION 3.9)
set (CMAKE_OSX_DEPLOYMENT_TARGET 10.14)

project (Lagrange
    VERSION 0.1.0
    DESCRIPTION "Beautiful Gemini Client"
    LANGUAGES C
)
set (COPYRIGHT_YEAR 2020)

# Build configuration.
option (ENABLE_KERNING "Enable kerning in font renderer (slower)" ON)
option (ENABLE_RESOURCE_EMBED "Embed resources inside the executable" OFF)

include (BuildType.cmake)
include (Embed.cmake)
if (NOT EXISTS ${CMAKE_SOURCE_DIR}/lib/the_Foundation)
    find_package (the_Foundation REQUIRED)
else ()
    set (TFDN_STATIC_LIBRARY    ON  CACHE BOOL "")
    set (TFDN_ENABLE_INSTALL    OFF CACHE BOOL "")
    set (TFDN_ENABLE_TESTS      OFF CACHE BOOL "")
    set (TFDN_ENABLE_WEBREQUEST OFF CACHE BOOL "")
    add_subdirectory (lib/the_Foundation)
    add_library (the_Foundation::the_Foundation ALIAS the_Foundation)
endif ()
find_package (PkgConfig REQUIRED)
pkg_check_modules (SDL2 REQUIRED sdl2)

# Embedded resources are written to a generated source file.
message (STATUS "Preparing embedded resources...")
# Fonts are too large to comfortably embed as a C source.
set (EMBED_RESOURCES 
    res/about/help.gmi
    res/about/lagrange.gmi
    res/about/version.gmi
    res/SourceSansPro-Regular.ttf
    res/FiraSans-Regular.ttf
    res/FiraSans-Bold.ttf
    res/FiraSans-Light.ttf
    res/FiraSans-Italic.ttf
    res/Nunito-Regular.ttf
    res/Nunito-ExtraBold.ttf
    res/Nunito-ExtraLight.ttf
    res/Nunito-LightItalic.ttf
    res/FiraMono-Regular.ttf
    res/NotoEmoji-Regular.ttf
    res/Symbola.ttf
)
 if (UNIX AND NOT APPLE)
     list (APPEND EMBED_RESOURCES res/lagrange-64.png)
 endif ()
embed_make (${EMBED_RESOURCES})

set (EMB_BIN ${CMAKE_CURRENT_BINARY_DIR}/resources.binary)
set_source_files_properties (${EMB_BIN} PROPERTIES MACOSX_PACKAGE_LOCATION Resources)

# Source files.
set (SOURCES
    src/main.c
    src/app.c
    src/app.h
    src/bookmarks.c
    src/bookmarks.h
    src/gmcerts.c
    src/gmcerts.h
    src/gmdocument.c
    src/gmdocument.h
    src/gmrequest.c
    src/gmrequest.h
    src/gmutil.c
    src/gmutil.h
    src/history.c
    src/history.h
    src/lookup.c
    src/lookup.h
    src/stb_image.h
    src/stb_truetype.h
    src/visited.c
    src/visited.h
    # User interface:
    src/ui/color.c
    src/ui/color.h
    src/ui/command.c
    src/ui/command.h
    src/ui/documentwidget.c
    src/ui/documentwidget.h
    src/ui/listwidget.c
    src/ui/listwidget.h
    src/ui/lookupwidget.c
    src/ui/lookupwidget.h
    src/ui/metrics.c
    src/ui/metrics.h
    src/ui/paint.c
    src/ui/paint.h
    src/ui/scrollwidget.c
    src/ui/scrollwidget.h
    src/ui/sidebarwidget.c
    src/ui/sidebarwidget.h
    src/ui/text.c
    src/ui/text.h
    src/ui/util.c
    src/ui/util.h
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
    ${CMAKE_CURRENT_BINARY_DIR}/resources.binary
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
)
target_compile_definitions (app PUBLIC LAGRANGE_APP_VERSION="${PROJECT_VERSION}")
if (ENABLE_KERNING)
    target_compile_definitions (app PUBLIC LAGRANGE_ENABLE_KERNING=1)
endif ()
target_link_libraries (app PUBLIC the_Foundation::the_Foundation)
target_link_libraries (app PUBLIC ${SDL2_LDFLAGS})
if (APPLE)
    if (IOS)
        target_link_libraries (app PUBLIC "-framework UIKit")
    else ()
        target_link_libraries (app PUBLIC "-framework AppKit")
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
    )
endif ()
if (UNIX)
    target_link_libraries (app PUBLIC m)
endif ()

# Deployment.
if (MSYS)
    install (TARGETS app DESTINATION .)
    if (NOT ENABLE_RESOURCE_EMBED)
        install (FILES ${EMB_BIN} DESTINATION .)
    endif ()
    install (PROGRAMS 
        ${SDL2_LIBDIR}/SDL2.dll 
        $<TARGET_FILE:the_Foundation::the_Foundation>
        DESTINATION .
    )
elseif (UNIX AND NOT APPLE)
    set_target_properties (app PROPERTIES
        INSTALL_RPATH_USE_LINK_PATH YES
    )
    set (desktop ${CMAKE_CURRENT_BINARY_DIR}/fi.skyjake.lagrange.desktop)
    file (WRITE ${desktop} "[Desktop Entry]
Name=Lagrange
Comment=${PROJECT_DESCRIPTION}
Categories=Network
Exec=${CMAKE_INSTALL_PREFIX}/bin/lagrange
Terminal=false
Type=Application
Icon=fi.skyjake.lagrange
")
    install (TARGETS app DESTINATION bin)
    install (FILES ${desktop} DESTINATION share/applications)
    install (FILES res/lagrange-256.png
         DESTINATION share/icons/hicolor/256x256/apps
         RENAME fi.skyjake.lagrange.png
    )
    if (NOT ENABLE_RESOURCE_EMBED)
        install (FILES ${EMB_BIN} DESTINATION share/lagrange)
    endif ()
endif ()

