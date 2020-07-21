cmake_minimum_required (VERSION 3.9)

project (Lagrange
    VERSION 1.0.0
    DESCRIPTION "Beautiful Gemini Client"
    LANGUAGES C
)
set (COPYRIGHT_YEAR 2020)

include (Embed.cmake)
find_package (the_Foundation REQUIRED)
find_package (PkgConfig REQUIRED)
pkg_check_modules (SDL2 REQUIRED sdl2)

# Embedded resources are written to a generated source file.
message (STATUS "Preparing embedded resources...")
set (EMBED_RESOURCES 
    res/FiraSans-Regular.ttf
    res/FiraSans-LightItalic.ttf
    res/FiraMono-Regular.ttf
)
# if (UNIX AND NOT APPLE)
#     list (APPEND EMBED_RESOURCES res/appicon-64.png)
# endif ()
embed_make (${EMBED_RESOURCES})

# Source files.
set (SOURCES
    ${CMAKE_CURRENT_BINARY_DIR}/embedded.c
    ${CMAKE_CURRENT_BINARY_DIR}/embedded.h
    src/main.c
    src/app.c
    src/app.h
    src/stb_image.h
    src/stb_truetype.h
    # User interface:
    src/ui/color.c
    src/ui/color.h
    src/ui/command.c
    src/ui/command.h
    src/ui/documentwidget.h
    src/ui/documentwidget.c
    src/ui/metrics.c
    src/ui/metrics.h
    src/ui/paint.c
    src/ui/paint.h
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
)
if (IOS)
elseif (APPLE)
    list (APPEND SOURCES src/ui/macos.m src/ui/macos.h)
    # list (APPEND RESOURCES
    #     "res/Lagrange.icns"
    # )
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
    ${SDL2_CFLAGS}
)
target_compile_definitions (app PUBLIC LAGRANGE_APP_VERSION="${PROJECT_VERSION}")
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
;Categories=
Exec=${CMAKE_INSTALL_PREFIX}/bin/lagrange
Terminal=false
Type=Application
Icon=fi.skyjake.lagrange")
    install (TARGETS app DESTINATION bin)
    install (FILES ${desktop} DESTINATION share/applications)
    # install (FILES res/appicon-128.png
    #     DESTINATION share/icons/hicolor/256x256/apps
    #     RENAME fi.skyjake.lagrange.png
    # )
endif ()

