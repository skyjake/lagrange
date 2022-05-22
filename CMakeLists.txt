# Lagrange - A Beautiful Gemini Client
# Copyright: 2020-2022 Jaakko Keränen <jaakko.keranen@iki.fi>
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
    VERSION 1.13.6
    DESCRIPTION "A Beautiful Gemini Client"
    LANGUAGES C
)
set (COPYRIGHT_YEAR 2022)
if (IOS)
    set (PROJECT_VERSION 1.13)
    set (IOS_BUNDLE_VERSION 2)
    set (IOS_BUILD_DATE "2022-05-09")
endif ()
if (ANDROID)
    set (PROJECT_VERSION 1.13)
    set (ANDROID_BUILD_VERSION b6)  # remember to update Gradle, AndroidManifest.xml
    set (ANDROID_BUILD_DATE "2022-05-09")
endif ()

# Defaults that depend on environment.
set (DEFAULT_RESIZE_DRAW ON)
if (HAIKU OR ANDROID)
    set (DEFAULT_RESIZE_DRAW OFF)
endif ()
set (DEFAULT_IDLE_SLEEP OFF)
if (IOS)
    set (DEFAULT_IDLE_SLEEP ON)
endif ()

# Build configuration.
option (ENABLE_CUSTOM_FRAME     "Draw a custom window frame (Windows)" OFF)
option (ENABLE_DOWNLOAD_EDIT    "Allow changing the Downloads directory" ON)
option (ENABLE_FRIBIDI          "Use the GNU FriBidi library for bidirectional text" ON)
option (ENABLE_FRIBIDI_BUILD    "Build the GNU FriBidi library (if OFF, try pkg-config)" OFF)
option (ENABLE_HARFBUZZ         "Use the HarfBuzz library to shape text" ON)
option (ENABLE_HARFBUZZ_MINIMAL "Build the HarfBuzz library with minimal dependencies (if OFF, try pkg-config)" OFF)
option (ENABLE_IDLE_SLEEP       "While idle, sleep in the main thread instead of waiting for events" ${DEFAULT_IDLE_SLEEP})
option (ENABLE_IPC              "Use IPC to communicate between running instances" ON)
option (ENABLE_KERNING          "Enable kerning in font renderer (slower)" ON)
option (ENABLE_MAC_MENUS        "Use native context menus (macOS)" ON)
option (ENABLE_MOBILE_PHONE     "Use the phone mobile UI design instead of desktop UI" OFF)
option (ENABLE_MOBILE_TABLET    "Use the tablet mobile UI design instead of desktop UI" OFF)
option (ENABLE_MPG123           "Use mpg123 for decoding MPEG audio" ON)
option (ENABLE_POPUP_MENUS      "Use popup windows for context menus (if OFF, menus are confined inside main window)" ON)
option (ENABLE_RELATIVE_EMBED   "Resources should always be found via relative path" OFF)
option (ENABLE_RESIZE_DRAW      "Force window to redraw during resizing" ${DEFAULT_RESIZE_DRAW})
option (ENABLE_SPARKLE          "Use Sparkle for automatic updates (macOS)" OFF)
option (ENABLE_TUI              "Enable the Curses TUI instead of GUI" OFF)
option (ENABLE_WEBP             "Use libwebp to decode .webp images (via pkg-config)" ON)
option (ENABLE_WINDOWPOS_FIX    "Set position after showing window (workaround for SDL bug)" OFF)
option (ENABLE_WINSPARKLE       "Use WinSparkle for automatic updates (Windows)" OFF)
option (ENABLE_X11_SWRENDER     "Use software rendering (X11)" OFF)
set    (CACERT_PEM_PATH         "" CACHE FILEPATH "Root CA cacert.pem file to include as a built-in resource")

include (BuildType.cmake)
include (Resources.cmake)
include (Depends.cmake)
include (CheckCCompilerFlag)

check_c_compiler_flag (-Werror=incompatible-pointer-types CFLAG_INCOMPATIBLE_POINTER_TYPES)

# Package resources.
message (STATUS "Preparing resources...")
set (RESOURCES
    res/about/about.gmi
    res/about/lagrange.gmi
    res/about/license.gmi
    res/lang/cs.bin
    res/lang/de.bin
    res/lang/en.bin
    res/lang/eo.bin
    res/lang/es.bin
    res/lang/es_MX.bin
    res/lang/fi.bin
    res/lang/fr.bin
    res/lang/gl.bin
    res/lang/hu.bin
    res/lang/ia.bin
    res/lang/ie.bin
    res/lang/isv.bin
    res/lang/it.bin
    res/lang/nl.bin
    res/lang/pl.bin
    res/lang/ru.bin
    res/lang/sk.bin
    res/lang/sr.bin
    res/lang/tok.bin
    res/lang/tr.bin
    res/lang/uk.bin
    res/lang/zh_Hans.bin
    res/lang/zh_Hant.bin
    res/shadow.png
    res/fontpack.ini
    res/macos-system-fonts.ini
)
if (EXISTS ${CACERT_PEM_PATH})
    message (STATUS "  ${CACERT_PEM_PATH}")
    list (APPEND RESOURCES ${CACERT_PEM_PATH})
endif ()
file (GLOB FONTS RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} res/fonts/*)
list (APPEND RESOURCES ${FONTS})
if (IOS)
    list (APPEND RESOURCES
        res/about/ios-help.gmi
        res/about/ios-version.gmi
    )
elseif (ANDROID)
    list (APPEND RESOURCES
        res/about/android-help.gmi
        res/about/android-version.gmi
    )
else ()
    list (APPEND RESOURCES
        res/about/help.gmi
        res/about/version-0.13.gmi
        res/about/version-1.5.gmi
        res/about/version.gmi
        res/arg-help.txt
    )
endif ()
if ((UNIX AND NOT APPLE) OR MSYS)
    list (APPEND RESOURCES res/lagrange-64.png)
endif ()
set (EMB_BIN ${CMAKE_CURRENT_BINARY_DIR}/resources.lgr)
make_resources (${EMB_BIN} ${RESOURCES})
set_source_files_properties (${EMB_BIN} PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
if (IOS)
    set (EMB_FONTS
        res/fonts/Roboto-Regular.ttf
        res/fonts/IosevkaTerm-Extended.ttf
    )
    set_source_files_properties (${EMB_FONTS}
        PROPERTIES MACOSX_PACKAGE_LOCATION Resources
    )
endif ()

# Source files.
set (SOURCES
    src/main.c
    src/app.c
    src/app.h
    src/bookmarks.c
    src/bookmarks.h
    src/defs.h
    src/export.c
    src/export.h
    src/feeds.c
    src/feeds.h
    src/fontpack.c
    src/fontpack.h
    src/gempub.c
    src/gempub.h
    src/gmcerts.c
    src/gmcerts.h
    src/gmdocument.c
    src/gmdocument.h
    src/gmrequest.c
    src/gmrequest.h
    src/gmtypesetter.c
    src/gmtypesetter.h
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
    src/resources.c
    src/resources.h
    src/sitespec.c
    src/sitespec.h
    src/stb_image.h
    src/stb_image_resize.h
    src/stb_truetype.h
    src/updater.h
    src/visited.c
    src/visited.h
    # User interface:
    src/ui/banner.c
    src/ui/banner.h
    src/ui/bindingswidget.c
    src/ui/bindingswidget.h
    src/ui/certimportwidget.c
    src/ui/certimportwidget.h
    src/ui/certlistwidget.c
    src/ui/certlistwidget.h
    src/ui/color.c
    src/ui/color.h
    src/ui/command.c
    src/ui/command.h
    src/ui/documentwidget.c
    src/ui/documentwidget.h
    src/ui/indicatorwidget.c
    src/ui/indicatorwidget.h
    src/ui/font.c
    src/ui/font.h
    src/ui/linkinfo.c
    src/ui/linkinfo.h
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
    src/ui/mobile.c
    src/ui/mobile.h
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
    src/ui/uploadwidget.c
    src/ui/uploadwidget.h
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
    ${EMB_BIN}
)
if (ENABLE_TUI)
    set (ENABLE_STB_TRUETYPE NO)
    add_definitions (-DiPlatformTerminal=1)
    list (APPEND SOURCES
        src/ui/text_terminal.c
    )    
else ()
    set (ENABLE_STB_TRUETYPE YES)
    list (APPEND SOURCES
        src/ui/text_stb.c
    )
    # Audio playback
    add_definitions (-DLAGRANGE_ENABLE_AUDIO=1)
    list (APPEND SOURCES
        src/audio/buf.c
        src/audio/buf.h
        src/audio/player.c
        src/audio/player.h
        src/audio/stb_vorbis.c
    )
endif ()
if (IOS OR ENABLE_TUI OR NOT APPLE) # macos.m has Sparkle updater
    list (APPEND SOURCES src/updater.c)
endif ()
if (ENABLE_IPC)
    list (APPEND SOURCES
        src/ipc.c
        src/ipc.h
    )
endif ()
if (ANDROID)
    set (MOBILE 1)
    add_definitions (-DiPlatformAndroidMobile=1)
    list (APPEND SOURCES
        src/android.c
        src/android.h
    )
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
elseif (APPLE AND NOT ENABLE_TUI)
    add_definitions (-DiPlatformAppleDesktop=1)
    list (APPEND SOURCES src/macos.m src/macos.h)
    list (APPEND RESOURCES "res/Lagrange.icns")
    set_source_files_properties ("res/Lagrange.icns" PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
endif ()
if (MSYS)
    set (WINRC_FILE_VERSION ${PROJECT_VERSION_MAJOR},${PROJECT_VERSION_MINOR},${PROJECT_VERSION_PATCH},0)
    set (WINRC_PRODUCT_VERSION ${PROJECT_VERSION_MAJOR},${PROJECT_VERSION_MINOR},0,0)
    configure_file (res/lagrange.rc.in ${CMAKE_CURRENT_BINARY_DIR}/lagrange.rc NEWLINE_STYLE WIN32)
    list (APPEND SOURCES src/win32.c src/win32.h ${CMAKE_CURRENT_BINARY_DIR}/lagrange.rc)
endif ()
if (MSYS OR ENABLE_TUI OR (UNIX AND NOT MOBILE AND NOT APPLE))
    add_definitions (-DiPlatformPcDesktop=1)
endif ()
if (MOBILE)
    add_definitions (-DiPlatformMobile=1)
else ()
    add_definitions (-DiPlatformDesktop=1)
    # Forced UI design.
    if (ENABLE_MOBILE_PHONE OR ENABLE_MOBILE_TABLET)
        add_definitions (-DiPlatformMobile=1)
    endif ()
    if (ENABLE_MOBILE_PHONE)
        add_definitions (-DiPlatformMobilePhone=1)
    endif ()
    if (ENABLE_MOBILE_TABLET)
        add_definitions (-DiPlatformMobileTablet=1)
    endif ()
endif ()

# Target.
if (NOT ANDROID)
    add_executable (app ${SOURCES} ${RESOURCES} ${EMB_FONTS})
else ()
    # The whole app becomes one shared library, based on this static one.
    add_library (app SHARED ${SOURCES})
endif ()
set_property (TARGET app PROPERTY C_STANDARD 11)
if (TARGET ext-deps)
    add_dependencies (app ext-deps)
endif ()
if (ENABLE_TUI)
    set_target_properties (app PROPERTIES OUTPUT_NAME clagrange)
else ()
    set_target_properties (app PROPERTIES OUTPUT_NAME lagrange)
endif ()
target_include_directories (app PUBLIC
    src
    ${CMAKE_CURRENT_BINARY_DIR}
    ${SDL2_INCLUDE_DIRS}
)
target_compile_options (app PUBLIC
    -Werror=implicit-function-declaration
    -Wno-deprecated-declarations
    ${SDL2_CFLAGS}
    -DSTB_VORBIS_NO_STDIO=1
    -DSTB_VORBIS_NO_INTEGER_CONVERSION=1
)
if (CFLAG_INCOMPATIBLE_POINTER_TYPES)
    target_compile_options (app PUBLIC -Werror=incompatible-pointer-types)
endif ()
target_compile_definitions (app PUBLIC LAGRANGE_APP_VERSION="${PROJECT_VERSION}")
if (ENABLE_CUSTOM_FRAME AND MSYS)
    target_compile_definitions (app PUBLIC LAGRANGE_ENABLE_CUSTOM_FRAME=1)
endif ()
if (ENABLE_DOWNLOAD_EDIT)
    target_compile_definitions (app PUBLIC LAGRANGE_ENABLE_DOWNLOAD_EDIT=1)
endif ()
if (ENABLE_IDLE_SLEEP)
    target_compile_definitions (app PUBLIC LAGRANGE_ENABLE_IDLE_SLEEP=1)
endif ()
if (ENABLE_IPC)
    target_compile_definitions (app PUBLIC LAGRANGE_ENABLE_IPC=1)
endif ()
if (ENABLE_KERNING)
    target_compile_definitions (app PUBLIC LAGRANGE_ENABLE_KERNING=1)
endif ()
if (ENABLE_MAC_MENUS)
    target_compile_definitions (app PUBLIC LAGRANGE_ENABLE_MAC_MENUS=1)
endif ()
if (ENABLE_MPG123 AND MPG123_FOUND)
    target_compile_definitions (app PUBLIC LAGRANGE_ENABLE_MPG123=1)
    target_link_libraries (app PUBLIC PkgConfig::MPG123)
endif ()
if (ENABLE_POPUP_MENUS)
    target_compile_definitions (app PUBLIC LAGRANGE_ENABLE_POPUP_MENUS=1)
endif ()
if (ENABLE_RESIZE_DRAW)
    target_compile_definitions (app PUBLIC LAGRANGE_ENABLE_RESIZE_DRAW=1)
endif ()
if (ENABLE_STB_TRUETYPE)
    target_compile_definitions (app PUBLIC LAGRANGE_ENABLE_STB_TRUETYPE=1)
endif ()
if (ENABLE_WEBP AND WEBP_FOUND)
    target_compile_definitions (app PUBLIC LAGRANGE_ENABLE_WEBP=1)
    if (TARGET PkgConfig::WEBP)
        target_link_libraries (app PUBLIC PkgConfig::WEBP)
    else ()
        target_link_libraries (app PUBLIC ${WEBP_LIBRARIES})
        target_include_directories (app PUBLIC ${WEBP_INCLUDE_DIRS})
    endif ()
endif ()
if (ENABLE_WINDOWPOS_FIX)
    target_compile_definitions (app PUBLIC LAGRANGE_ENABLE_WINDOWPOS_FIX=1)
endif ()
if (ENABLE_X11_SWRENDER)
    target_compile_definitions (app PUBLIC LAGRANGE_ENABLE_X11_SWRENDER=1)
endif ()
target_link_libraries (app PUBLIC the_Foundation::the_Foundation)
target_link_libraries (app PUBLIC ${SDL2_LDFLAGS})
if (ENABLE_HARFBUZZ AND HARFBUZZ_FOUND AND NOT ENABLE_TUI)
    if (TARGET harfbuzz-lib)
        target_link_libraries (app PUBLIC harfbuzz-lib)
    else ()
        target_link_libraries (app PUBLIC ${HARFBUZZ_LIBRARIES})
        target_include_directories (app PUBLIC ${HARFBUZZ_INCLUDE_DIRS})
    endif ()
    target_compile_definitions (app PUBLIC LAGRANGE_ENABLE_HARFBUZZ=1)
endif ()
if (ENABLE_FRIBIDI AND FRIBIDI_FOUND)
    if (TARGET fribidi-lib)
        target_link_libraries (app PUBLIC fribidi-lib)
    else ()
        target_link_libraries (app PUBLIC ${FRIBIDI_LIBRARIES})
        target_include_directories (app PUBLIC ${FRIBIDI_INCLUDE_DIRS})
    endif ()
    target_compile_definitions (app PUBLIC LAGRANGE_ENABLE_FRIBIDI=1)
endif ()
if (APPLE AND NOT IOS)
    if (CMAKE_OSX_DEPLOYMENT_TARGET)
        target_compile_options (app PUBLIC -mmacosx-version-min=${CMAKE_OSX_DEPLOYMENT_TARGET})
        target_link_options (app PUBLIC -mmacosx-version-min=${CMAKE_OSX_DEPLOYMENT_TARGET})
    endif ()
endif ()
if (APPLE AND NOT ENABLE_TUI)
    if (IOS)
        target_link_libraries (app PUBLIC "-framework UIKit")
    else ()
        target_link_libraries (app PUBLIC "-framework AppKit")
    endif ()
    if (ENABLE_SPARKLE)
        target_link_libraries (app PUBLIC sparkle)
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
        target_compile_definitions (app PUBLIC
            LAGRANGE_IOS_VERSION="${IOS_BUNDLE_VERSION}"
            LAGRANGE_IOS_BUILD_DATE="${IOS_BUILD_DATE}"
        )
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
if (ANDROID)
    target_compile_definitions (app PUBLIC
        LAGRANGE_ANDROID_VERSION="${ANDROID_BUILD_VERSION}"
        LAGRANGE_ANDROID_BUILD_DATE="${ANDROID_BUILD_DATE}"
    )
endif ()
if (MSYS)
    target_link_libraries (app PUBLIC d2d1 uuid dwmapi) # querying DPI
    if (ENABLE_WINSPARKLE)
        target_link_libraries (app PUBLIC winsparkle)
    endif ()
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
    install (FILES ${EMB_BIN} DESTINATION .)
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
    target_compile_definitions (app PUBLIC
        LAGRANGE_EMB_BIN="${CMAKE_INSTALL_PREFIX}/resources.lgr")
    install (FILES ${EMB_BIN} DESTINATION .)
elseif (ANDROID)
    file (COPY ${EMB_BIN} DESTINATION ${CMAKE_SOURCE_DIR}/../src/main/assets)
elseif (ENABLE_TUI AND (UNIX OR MSYS))
    install (TARGETS app DESTINATION ${CMAKE_INSTALL_BINDIR})
    install (FILES res/lagrange.1 DESTINATION ${CMAKE_INSTALL_MANDIR}/man1)
    if (NOT ENABLE_RELATIVE_EMBED)
        target_compile_definitions (app PUBLIC
                LAGRANGE_EMB_BIN="${CMAKE_INSTALL_FULL_DATADIR}/lagrange/resources.lgr")
    endif ()
    install (FILES ${EMB_BIN} DESTINATION ${CMAKE_INSTALL_DATADIR}/lagrange)    
elseif (UNIX AND NOT APPLE)
    include (GNUInstallDirs)
    set_target_properties (app PROPERTIES
        INSTALL_RPATH_USE_LINK_PATH YES
    )
    set (desktop ${CMAKE_CURRENT_BINARY_DIR}/fi.skyjake.Lagrange.desktop)
    file (WRITE ${desktop} "[Desktop Entry]
Name=Lagrange
Comment=${PROJECT_DESCRIPTION}
Categories=Network;
Exec=${CMAKE_INSTALL_PREFIX}/bin/lagrange %U
Terminal=false
Type=Application
StartupWMClass=lagrange
Icon=fi.skyjake.Lagrange
MimeType=x-scheme-handler/gemini;x-scheme-handler/gopher;x-scheme-handler/spartan;
")
    install (TARGETS app DESTINATION ${CMAKE_INSTALL_BINDIR})
    install (FILES ${desktop} DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/applications)
    install (FILES res/lagrange-256.png
         DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/icons/hicolor/256x256/apps
         RENAME fi.skyjake.Lagrange.png
    )
    install (FILES res/fi.skyjake.Lagrange.appdata.xml
        DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/metainfo
    )
    install (FILES res/lagrange.1 DESTINATION ${CMAKE_INSTALL_MANDIR}/man1)
    if (NOT ENABLE_RELATIVE_EMBED)
        target_compile_definitions (app PUBLIC
                LAGRANGE_EMB_BIN="${CMAKE_INSTALL_FULL_DATADIR}/lagrange/resources.lgr")
    endif ()
    install (FILES ${EMB_BIN} DESTINATION ${CMAKE_INSTALL_DATADIR}/lagrange)
endif ()
