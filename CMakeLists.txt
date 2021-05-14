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
    VERSION 1.5.0
    DESCRIPTION "A Beautiful Gemini Client"
    LANGUAGES C
)
set (COPYRIGHT_YEAR 2021)
if (IOS)
    set (PROJECT_VERSION 1.4)  # pinned for TestFlight
    set (IOS_BUNDLE_VERSION 7) # just increment this
endif ()

# Build configuration.
option (ENABLE_IPC              "Use IPC to communicate between running instances" ON)
option (ENABLE_MPG123           "Use mpg123 for decoding MPEG audio" ON)
option (ENABLE_X11_SWRENDER     "Use software rendering under X11" OFF)
option (ENABLE_KERNING          "Enable kerning in font renderer (slower)" ON)
option (ENABLE_RESOURCE_EMBED   "Embed resources inside the executable" OFF)
option (ENABLE_RELATIVE_EMBED   "Resources should always be found via relative path" OFF)
option (ENABLE_WINDOWPOS_FIX    "Set position after showing window (workaround for SDL bug)" OFF)
option (ENABLE_IDLE_SLEEP       "While idle, sleep in the main thread instead of waiting for events" ON)
option (ENABLE_DOWNLOAD_EDIT    "Allow changing the Downloads directory" ON)
option (ENABLE_CUSTOM_FRAME     "Draw a custom window frame (Windows)" OFF)

include (BuildType.cmake)
include (res/Embed.cmake)
include (Depends.cmake)

# Embedded resources are written to a generated source file.
message (STATUS "Preparing embedded resources...")
# Fonts are too large to comfortably embed as a C source.
set (EMBED_RESOURCES
    res/about/about.gmi
    res/about/help.gmi
    res/about/lagrange.gmi
    res/about/license.gmi
    res/about/version.gmi
    res/arg-help.txt
    res/fonts/Tinos-Regular.ttf
    res/fonts/Tinos-Italic.ttf
    res/fonts/Tinos-Bold.ttf
    res/fonts/IosevkaTerm-Extended.ttf
    res/fonts/FiraSans-Bold.ttf
    res/fonts/FiraSans-Italic.ttf
    res/fonts/FiraSans-Light.ttf
    res/fonts/FiraSans-Regular.ttf
    res/fonts/FiraSans-SemiBold.ttf
    res/fonts/Literata-Regular-opsz=14.ttf
    res/fonts/Literata-Bold-opsz=36.ttf
    res/fonts/Literata-ExtraLight-opsz=18.ttf
    res/fonts/Literata-LightItalic-opsz=10.ttf
    res/fonts/NanumGothic-Regular.ttf
    res/fonts/NotoEmoji-Regular.ttf
    res/fonts/NotoSansArabicUI-Regular.ttf
    res/fonts/NotoSansJP-Regular.ttf
    res/fonts/NotoSansSC-Regular.ttf
    res/fonts/Nunito-Bold.ttf
    res/fonts/Nunito-ExtraBold.ttf
    res/fonts/Nunito-ExtraLight.ttf
    res/fonts/Nunito-LightItalic.ttf
    res/fonts/Nunito-Regular.ttf
    res/fonts/SourceSans3-Bold.ttf
    res/fonts/SourceSans3-ExtraLight.ttf
    res/fonts/SourceSans3-It.ttf
    res/fonts/SourceSans3-Regular.ttf
    res/fonts/SourceSans3-Semibold.ttf
    res/fonts/Symbola.ttf
    res/lang/de.bin
    res/lang/en.bin
    res/lang/es.bin
    res/lang/fi.bin
    res/lang/fr.bin
    res/lang/ia.bin
    res/lang/ie.bin
    res/lang/pl.bin
    res/lang/ru.bin
    res/lang/sr.bin
    res/lang/tok.bin
    res/lang/zh_Hans.bin
    res/lang/zh_Hant.bin
    res/shadow.png
)
if ((UNIX AND NOT APPLE) OR MSYS)
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
    src/gempub.c
    src/gempub.h
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
    src/lang.c
    src/lang.h
    src/lookup.c
    src/lookup.h
    src/media.c
    src/media.h
    src/mimehooks.c
    src/mimehooks.h
    src/periodic.c
    src/periodic.h
    src/prefs.c
    src/prefs.h
    src/stb_image.h
    src/stb_image_resize.h
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
    src/ui/root.c
    src/ui/root.h
    src/ui/mediaui.c
    src/ui/mediaui.h
    src/ui/scrollwidget.c
    src/ui/scrollwidget.h
    src/ui/sidebarwidget.c
    src/ui/sidebarwidget.h
    src/ui/text.c
    src/ui/text.h
    src/ui/touch.c
    src/ui/touch.h
    src/ui/translation.c
    src/ui/translation.h
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
    res/about/about.gmi
    res/about/help.gmi
    res/about/lagrange.gmi
    res/about/license.gmi
    res/about/version.gmi
    ${CMAKE_CURRENT_BINARY_DIR}/resources.lgr
    ${CMAKE_CURRENT_BINARY_DIR}/embedded.c
    ${CMAKE_CURRENT_BINARY_DIR}/embedded.h
)
if (ENABLE_IPC)
    list (APPEND SOURCES
        src/ipc.c
        src/ipc.h
    )
endif ()
if (ANDROID)
    set (MOBILE 1)
    add_definitions (-DiPlatformAndroidMobile=1)
endif ()
if (IOS)
    set (MOBILE 1)
    add_definitions (-DiPlatformAppleMobile=1)
    list (APPEND SOURCES
        src/ios.m
        src/ios.h
        app/Images.xcassets
        res/LaunchScreen.storyboard
    )
    set_source_files_properties(
        app/Images.xcassets
        res/LaunchScreen.storyboard
    PROPERTIES
        MACOSX_PACKAGE_LOCATION Resources
    )
elseif (APPLE)
    add_definitions (-DiPlatformAppleDesktop=1)
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
if (MSYS OR (APPLE AND NOT MOBILE) OR (UNIX AND NOT MOBILE))
    add_definitions (-DiPlatformPcDesktop=1)
endif ()
if (MOBILE)
    add_definitions (-DiPlatformMobile=1)
else ()
    add_definitions (-DiPlatformDesktop=1)
endif ()

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
if (ENABLE_IPC)
    target_compile_definitions (app PUBLIC LAGRANGE_ENABLE_IPC=1)
endif ()
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
    target_compile_definitions (app PUBLIC LAGRANGE_ENABLE_IDLE_SLEEP=1)
endif ()
if (ENABLE_DOWNLOAD_EDIT)
    target_compile_definitions (app PUBLIC LAGRANGE_ENABLE_DOWNLOAD_EDIT=1)
endif ()
if (ENABLE_CUSTOM_FRAME AND MSYS)
    target_compile_definitions (app PUBLIC LAGRANGE_ENABLE_CUSTOM_FRAME=1)
endif ()
target_link_libraries (app PUBLIC the_Foundation::the_Foundation)
target_link_libraries (app PUBLIC ${SDL2_LDFLAGS})
if (APPLE)
    if (IOS)
        target_link_libraries (app PUBLIC "-framework UIKit")
    else ()
        target_link_libraries (app PUBLIC "-framework AppKit")
    endif ()
    if (CMAKE_OSX_DEPLOYMENT_TARGET AND NOT IOS)
        target_compile_options (app PUBLIC -mmacosx-version-min=${CMAKE_OSX_DEPLOYMENT_TARGET})
        target_link_options (app PUBLIC -mmacosx-version-min=${CMAKE_OSX_DEPLOYMENT_TARGET})
    endif ()
    if (SDL2_LIBRARY_DIRS)
        set_property (TARGET app PROPERTY BUILD_RPATH ${SDL2_LIBRARY_DIRS})
    endif ()
    set_target_properties (app PROPERTIES
        OUTPUT_NAME "Lagrange"
        MACOSX_BUNDLE YES
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_LIST_DIR}/res/MacOSXBundleInfo.plist.in"
        MACOSX_BUNDLE_BUNDLE_NAME "Lagrange"
        MACOSX_BUNDLE_INFO_STRING "${PROJECT_VERSION}"
        MACOSX_BUNDLE_ICON_FILE "Lagrange.icns"
        MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
        MACOSX_BUNDLE_GUI_IDENTIFIER "fi.skyjake.lagrange"
        MACOSX_BUNDLE_COPYRIGHT "© ${COPYRIGHT_YEAR} Jaakko Keränen"
        XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER "fi.skyjake.lagrange"
    )
    if (IOS)
        target_compile_definitions (app PUBLIC LAGRANGE_IOS_VERSION="${IOS_BUNDLE_VERSION}")
        set_target_properties (app PROPERTIES
            MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_LIST_DIR}/res/iOSBundleInfo.plist.in"
            MACOSX_BUNDLE_BUNDLE_VERSION "${IOS_BUNDLE_VERSION}"
            XCODE_ATTRIBUTE_SKIP_INSTALL "NO"
            XCODE_ATTRIBUTE_INSTALL_PATH "$(LOCAL_APPS_DIR)"
            XCODE_ATTRIBUTE_TARGETED_DEVICE_FAMILY "1,2"
            XCODE_ATTRIBUTE_ASSETCATALOG_COMPILER_APPICON_NAME "AppIcon"
            MACOSX_BUNDLE_ICON_FILE "AppIcon"
        )
    endif ()
    if (XCODE_DEVELOPMENT_TEAM)
        set_property (TARGET app PROPERTY
            XCODE_ATTRIBUTE_DEVELOPMENT_TEAM ${XCODE_DEVELOPMENT_TEAM}
        )
    endif ()
endif ()
if (MSYS)
    target_link_libraries (app PUBLIC d2d1 uuid dwmapi) # querying DPI
endif ()
if (UNIX)
    target_link_libraries (app PUBLIC m)
endif ()
if (HAIKU)
    target_link_libraries (app PUBLIC m network bsd)
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
elseif (HAIKU)
    install (TARGETS app DESTINATION .)
    if (NOT ENABLE_RESOURCE_EMBED)
        target_compile_definitions (app PUBLIC
            LAGRANGE_EMB_BIN="${CMAKE_INSTALL_PREFIX}/resources.lgr")
        install (FILES ${EMB_BIN} DESTINATION .)
    endif ()
elseif (UNIX AND NOT APPLE)
    set_target_properties (app PROPERTIES
        INSTALL_RPATH_USE_LINK_PATH YES
    )
    set (desktop ${CMAKE_CURRENT_BINARY_DIR}/fi.skyjake.Lagrange.desktop)
    file (WRITE ${desktop} "[Desktop Entry]
Name=Lagrange
Comment=${PROJECT_DESCRIPTION}
Categories=Network
Exec=${CMAKE_INSTALL_PREFIX}/bin/lagrange %U
Terminal=false
Type=Application
Icon=fi.skyjake.Lagrange
MimeType=x-scheme-handler/gemini;x-scheme-handler/gopher
")
    install (TARGETS app DESTINATION bin)
    install (FILES ${desktop} DESTINATION share/applications)
    install (FILES res/lagrange-256.png
         DESTINATION share/icons/hicolor/256x256/apps
         RENAME fi.skyjake.Lagrange.png
    )
    install (FILES res/fi.skyjake.Lagrange.appdata.xml
        DESTINATION share/metainfo
    )
    if (NOT ENABLE_RESOURCE_EMBED)
        if (NOT ENABLE_RELATIVE_EMBED)
            target_compile_definitions (app PUBLIC
                LAGRANGE_EMB_BIN="${CMAKE_INSTALL_PREFIX}/share/lagrange/resources.lgr")
        endif ()
        install (FILES ${EMB_BIN} DESTINATION share/lagrange)
    endif ()
endif ()
