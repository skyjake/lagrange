/* Copyright 2020 Jaakko Keränen <jaakko.keranen@iki.fi>

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

1. Redistributions of source code must retain the above copyright notice, this
   list of conditions and the following disclaimer.
2. Redistributions in binary form must reproduce the above copyright notice,
   this list of conditions and the following disclaimer in the documentation
   and/or other materials provided with the distribution.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR
ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE. */

#include "impl.h"

#include "bookmarks.h"
#include "feeds.h"
#include "fontpack.h"
#include <lagrange/core.h>
#include <lagrange/gmcerts.h>
#include <lagrange/mimehooks.h>
#include <lagrange/resources.h>
#include <lagrange/sitespec.h>
#include <lagrange/snippets.h>
#include "gmutil.h"
#include "render/text.h"
#include "ui/command.h"
#include "ui/documentwidget.h"
#include "ui/gamepad.h"
#include "ui/keys.h"
#include "ui/root.h"
#include "ui/touch.h"
#include "ui/util.h"
#include "ui/window.h"

#include <the_Foundation/file.h>
#include <the_Foundation/fileinfo.h>
#include <the_Foundation/networkproxy.h>
#include <the_Foundation/path.h>
#include <the_Foundation/socket.h>
#include <the_Foundation/version.h>

#include <errno.h>
#include <the_Foundation/toml.h>

#if defined (iPlatformAppleDesktop)
#   include "platform/macos.h"
#endif
#if defined (iPlatformAppleMobile)
#   include "platform/ios.h"
#endif
#if defined (iPlatformAndroidMobile)
#   include "platform/android.h"
#endif
#if defined (iPlatformMsys) || defined (iPlatformWindows)
#   include "platform/win32.h"
#endif
#if defined (LAGRANGE_ENABLE_X11_XLIB)
#   include "platform/x11.h"
#endif

static iString *serializePrefs_App_(const iApp *d) {
    iString *str = new_String();
    appendCStr_String(str, "version app:" LAGRANGE_APP_VERSION "\n");
#if defined (LAGRANGE_ENABLE_CUSTOM_FRAME)
    appendFormat_String(str, "customframe arg:%d\n", d->prefs.customFrame);
#endif
    if (d->prefs.retainWindowSize) {
        int w, h, x, y;
        /* Open windows' current placements. */
        iConstForEach(PtrArray, i, &d->mainWindows) {
            const iMainWindow *win = i.ptr;
            const size_t winIndex = win->place.placementIndex;
            x = win->place.normalRect.pos.x;
            y = win->place.normalRect.pos.y;
            w = win->place.normalRect.size.x;
            h = win->place.normalRect.size.y;
#if defined (LAGRANGE_ENABLE_X11_XLIB)
            int deskOut = win->place.desktop;
            if (deskOut < 0) {
                unsigned long dk;
                if (getDesktop_SDLWindow(win->base.win, &dk)) {
                    deskOut = (int) dk;
                }
            }
            if (deskOut >= 0) {  /* only save if we have a valid desktop */
                appendFormat_String(str, "window.desktop index:%zu desk:%d\n", winIndex, deskOut);
            }
#endif
            /* On macOS, maximization should be applied at creation time or the window will take
               a moment to animate to its maximized size. */
            const int winSnap = (isApple_Platform() || isMobile_Platform() ? 0 : snap_MainWindow(win));
            appendFormat_String(str,
                                "window.setrect index:%zu width:%d height:%d coord:%d %d snap:%d\n",
                                winIndex,
                                w,
                                h,
                                x,
                                y,
                                winSnap);
        }
        /* Remembered placements for slots without an open window (e.g., closed earlier
           this session but not reused). */
        iConstForEach(Array, ri, &d->initialWindowRects) {
            const iRect *rect = ri.value;
            const size_t idx = index_ArrayConstIterator(&ri);
            if (isEmpty_Rect(*rect) || isPlacementUnused_App_(d, idx)) {
                continue;
            }
            appendFormat_String(str,
                                "window.setrect index:%zu width:%d height:%d coord:%d %d\n",
                                idx,
                                width_Rect(*rect),
                                height_Rect(*rect),
                                left_Rect(*rect),
                                top_Rect(*rect));
        }
    }
    if (d->window) {
        appendFormat_String(str, "uiscale arg:%f\n", uiScale_Window(as_Window(d->window)));
    }
    appendFormat_String(str, "prefs.dialogtab arg:%d\n", d->prefs.dialogTab);
    appendFormat_String(str,
                        "font.set ui:%s heading:%s body:%s mono:%s monodoc:%s\n",
                        cstr_String(&d->prefs.strings[uiFont_PrefsString]),
                        cstr_String(&d->prefs.strings[headingFont_PrefsString]),
                        cstr_String(&d->prefs.strings[bodyFont_PrefsString]),
                        cstr_String(&d->prefs.strings[monospaceFont_PrefsString]),
                        cstr_String(&d->prefs.strings[monospaceDocumentFont_PrefsString]));
    appendFormat_String(str, "zoom.set arg:%d\n", d->prefs.zoomPercent);
    appendFormat_String(str, "inputzoom.set arg:%d\n", d->prefs.inputZoomLevel);
    appendFormat_String(str, "uploadzoom.set arg:%d\n", d->prefs.editorZoomLevel);
    appendFormat_String(str, "pinsplit.set arg:%d\n", d->prefs.pinSplit);
    appendFormat_String(str, "promptposition.set arg:%d\n", d->prefs.promptPosition);
    appendFormat_String(str, "feedinterval.set arg:%d\n", d->prefs.feedInterval);
    appendFormat_String(str, "scrollspeed arg:%d type:%d\n", d->prefs.smoothScrollSpeed[keyboard_ScrollType], keyboard_ScrollType);
    appendFormat_String(str, "scrollspeed arg:%d type:%d\n", d->prefs.smoothScrollSpeed[mouse_ScrollType], mouse_ScrollType);
    appendFormat_String(str, "cachesize.set arg:%d\n", d->prefs.maxCacheSize);
    appendFormat_String(str, "memorysize.set arg:%d\n", d->prefs.maxMemorySize);
    appendFormat_String(str, "urlsize.set arg:%d\n", d->prefs.maxUrlSize);
    appendFormat_String(str, "linewidth.set arg:%d\n", d->prefs.lineWidth);
    appendFormat_String(str, "linespacing.set arg:%f\n", d->prefs.lineSpacing);
    appendFormat_String(str, "tabwidth.set arg:%d\n", d->prefs.tabWidth);
    appendFormat_String(str, "returnkey.set arg:%d\n", d->prefs.returnKey);
    appendFormat_String(str, "collapsepre.set arg:%d\n", d->prefs.collapsePre);
    for (size_t i = 0; i < iElemCount(d->prefs.navbarActions); i++) {
        appendFormat_String(str, "navbar.action.set arg:%d button:%d\n", d->prefs.navbarActions[i], i);
    }
#if defined (LAGRANGE_USE_GAMEPAD)
    appendFormat_String(str, "gamepad.set clear:1\n");
    for (int i = 0; i < max_GamepadAction; i++) {
        if (actions_Gamepad[i] != unassigned_Gamepad) {
            appendFormat_String(str,
                                "gamepad.set trig:%d button:%d arg:%d\n",
                                (actions_Gamepad[i] & triggerMod_Gamepad) != 0,
                                actions_Gamepad[i] & ~triggerMod_Gamepad,
                                i);
        }
    }
#endif /* LAGRANGE_USE_GAMEPAD */
    if (isMobile_Platform()) {
        appendFormat_String(str, "toolbar.action.set arg:%d button:0\n", d->prefs.toolbarActions[0]);
        appendFormat_String(str, "toolbar.action.set arg:%d button:1\n", d->prefs.toolbarActions[1]);
    }
    for (size_t i = 0; i < 2; i++) {
        for (size_t j = 0; j < maxSidebarModes_Prefs; j++) {
            appendFormat_String(str,
                                "sidebar.modes.set arg:%d side:%u mode:%u\n",
                                d->prefs.sidebarModeEnabled[i][j],
                                i,
                                j);
        }
    }
    iConstForEach(StringSet, fp, d->prefs.disabledFontPacks) {
        appendFormat_String(str, "fontpack.disable id:%s\n", cstr_String(fp.value));
    }
    appendFormat_String(str, "ansiescape arg:%d\n", d->prefs.gemtextAnsiEscapes);
    serializeBools_Prefs(&d->prefs, str);
    appendFormat_String(str, "theme.set arg:%d auto:1\n", d->prefs.theme);
    appendFormat_String(str, "accent.set arg:%d\n", d->prefs.accent);
    appendFormat_String(str, "ostheme arg:%d preferdark:%d preferlight:%d\n",
                        d->prefs.useSystemTheme,
                        d->prefs.systemPreferredColorTheme[0],
                        d->prefs.systemPreferredColorTheme[1]);
    appendFormat_String(str, "doctheme.dark.set arg:%d\n", d->prefs.docThemeDark);
    appendFormat_String(str, "doctheme.light.set arg:%d\n", d->prefs.docThemeLight);
    appendFormat_String(str, "saturation.set arg:%d\n", (int) ((d->prefs.saturation * 100) + 0.5f));
    appendFormat_String(str, "imagestyle.set arg:%d\n", d->prefs.imageStyle);
    serializeStrings_Prefs(&d->prefs, str);
#if defined (LAGRANGE_ENABLE_DOWNLOAD_EDIT)
    appendFormat_String(str, "downloads path:%s\n", cstr_String(&d->prefs.strings[downloadDir_PrefsString]));
#endif
    appendFormat_String(str, "translation.languages from:%d to:%d\n", d->prefs.langFrom, d->prefs.langTo);
    iConstForEach(StringHash, sw, d->savedWidths) {
        const iString     *resizeId = key_StringHashConstIterator(&sw);
        const iSavedWidth *saved    = sw.value->object;
        appendFormat_String(str, "width.save arg:%g id:%s\n", saved->gaps, cstr_String(resizeId));
    }
    return str;
}

static const iString *prefsFileName_(void) {
    return collectNewCStr_String(concatPath_CStr(dataDir_App_(), prefsFileName_App_));
}

void updateCACertificates_App(void) {
    iApp *d = &app_;
    if (isEmpty_String(&d->prefs.strings[caFile_PrefsString]) &&
        isEmpty_String(&d->prefs.strings[caPath_PrefsString]) &&
        !isEmpty_Block(&blobCacertPem_Resources)) {
        /* Use the bundled CA root cert store. */
        iFile *f = new_File(collect_String(concatCStr_Path(dataDir_App(), "cacert.pem")));
        iBool load = iFalse;
        if (fileExists_FileInfo(path_File(f)) &&
            fileSize_FileInfo(path_File(f)) == size_Block(&blobCacertPem_Resources)) {
            load = iTrue;
        }
        else if (open_File(f, writeOnly_FileMode)) {
            write_File(f, &blobCacertPem_Resources);
            close_File(f);
            load = iTrue;
        }
        if (load) {
            setCACertificates_TlsRequest(path_File(f), NULL);
        }
        iRelease(f);
    }
    else {
        setCACertificates_TlsRequest(&d->prefs.strings[caFile_PrefsString],
                                     &d->prefs.strings[caPath_PrefsString]);
    }
}

void loadPrefs_App_(iApp *d) {
    iUnused(d);
    iBool haveCA = iFalse;
    d->isLoadingPrefs = iTrue; /* affects which notifications get posted */
    iVersion upgradedFromAppVersion = { 0 };
    /* Create the data dir if it doesn't exist yet. */
    makeDirs_Path(collectNewCStr_String(dataDir_App_()));
    iFile *f = new_File(prefsFileName_());
    if (open_File(f, readOnly_FileMode | text_FileMode)) {
        iString *str = readString_File(f);
        const iRangecc src = range_String(str);
        iRangecc line = iNullRange;
        while (nextSplit_Rangecc(src, "\n", &line)) {
            iString cmdStr;
            initRange_String(&cmdStr, line);
            const char *cmd = cstr_String(&cmdStr);
            /* Window init commands must be handled before the window is created. */
            if (equal_Command(cmd, "uiscale")) {
                setUiScale_Window(get_Window(), argf_Command(cmd));
            }
            else if (equal_Command(cmd, "uilang")) {
                const char *id = cstr_Command(cmd, "id");
                setCStr_String(&d->prefs.strings[uiLanguage_PrefsString], id);
                setCurrent_Lang(id);
            }
            else if (equal_Command(cmd, "ca.file") || equal_Command(cmd, "ca.path")) {
                /* Background requests may be started before these commands would get
                   handled via the event loop. */
                handleCommand_App(cmd);
                haveCA = iTrue;
            }
            else if (equal_Command(cmd, "prefs.socks.changed")) {
                /* This will affect the proxy setup later during loading of prefs. */
                d->prefs.useProxy = arg_Command(cmd) != 0;
            }
            else if (equal_Command(cmd, "misfin.recent")) {
                setRange_String(&d->prefs.strings[recentMisfinId_PrefsString], range_Command(cmd, "fp"));
            }
            else if (equal_Command(cmd, "customframe")) {
                d->prefs.customFrame = arg_Command(cmd);
            }
            else if (equal_Command(cmd, "window.setrect") && !argLabel_Command(cmd, "snap")) {
                const int   index   = argLabel_Command(cmd, "index");
                const iInt2 pos     = coord_Command(cmd);
                iRect       winRect = init_Rect(
                    pos.x, pos.y, argLabel_Command(cmd, "width"), argLabel_Command(cmd, "height"));
                if (index >= 0 && index < 100) {
                    if ((size_t) index >= size_Array(&d->initialWindowRects)) {
                        resize_Array(&d->initialWindowRects, index + 1);
                    }
                    set_Array(&d->initialWindowRects, index, &winRect);
                }
            }
            else if (equal_Command(cmd, "window.desktop")) {
                 const int index = argLabel_Command(cmd, "index");
                 const int desk  = argLabel_Command(cmd, "desk");
                 if (index >= 0 && index < 100 && desk >= 0) {  // Validate desk >= 0
                     if ((size_t) index >= size_Array(&d->initialWindowDesktops)) {
                         resize_Array(&d->initialWindowDesktops, index + 1);
                     }
                     set_Array(&d->initialWindowDesktops, index, &((int){ desk }));
                 }
             }
            else if (equal_Command(cmd, "fontpack.disable")) {
                insert_StringSet(d->prefs.disabledFontPacks,
                                 collect_String(suffix_Command(cmd, "id")));
            }
            else if (equal_Command(cmd, "font.set") ||
                     equal_Command(cmd, "prefs.retaintabs.changed")) {
                /* Fonts are set immediately so the first initialization already has
                   the right ones. */
                handleCommand_App(cmd);
            }
            else if (equal_Command(cmd, "prefs.menubar.changed")) {
                handleCommand_App(cmd);
            }
            else if (equal_Command(cmd, "feedinterval.set")) {
                handleNonWindowRelatedCommand_App_(d, cmd);
            }
#if defined (iPlatformAndroidMobile)
            else if (equal_Command(cmd, "returnkey.set")) {
                /* Hardcoded to avoid accidental presses of the virtual Return key. */
                d->prefs.returnKey = default_ReturnKeyBehavior;
            }
#endif
#if !defined (LAGRANGE_ENABLE_DOWNLOAD_EDIT)
            else if (equal_Command(cmd, "downloads")) {
                continue; /* can't change downloads directory */
            }
#endif
            else if (equal_Command(cmd, "version")) {
                /* This is a special command that lets us know which version we're upgrading from.
                   It was added in v1.8.0. */
                init_Version(&upgradedFromAppVersion, range_Command(cmd, "app"));
            }
            else {
                postCommandString_Root(NULL, &cmdStr);
            }
            deinit_String(&cmdStr);
        }
        delete_String(str);
    }
    if (!haveCA) {
        /* Preferences file did not include CA settings at all. */
        updateCACertificates_App();
    }
    iRelease(f);
    /* Upgrade checks. */
#if 0 /* disabled in v1.11 (font library search) */
    if (cmp_Version(&upgradedFromAppVersion, &(iVersion){ 1, 8, 0 }) < 0) {
        /* When upgrading to v1.8.0, the old hardcoded font library is gone and that means
           UI strings may not have the right fonts available for the UI to remain
           usable. */
        postCommandf_App("uilang id:en");
        postCommand_App("~fontpack.suggest.classic");
    }
#endif
    if (cmp_Version(&upgradedFromAppVersion, &(iVersion){ 1, 15, 5 }) < 0) {
        /* LibreTranslate updated with new language models, now defaulting to Auto-Detect. */
        postCommand_App("~translation.languages from:0 to:8");
    }
    /* Some settings have fixed values depending on the platform/config. */
#if !defined (LAGRANGE_ENABLE_CUSTOM_FRAME)
    d->prefs.customFrame = iFalse;
#endif
#if defined (LAGRANGE_MAC_MENUBAR)
    d->prefs.menuBar = iFalse;
#endif
    if (deviceType_App() != desktop_AppDeviceType) {
        d->prefs.menuBar = iFalse; /* not optional on mobile */
    }
    d->isLoadingPrefs = iFalse;
}

void savePrefs_App_(const iApp *d) {
#if defined (LAGRANGE_ENABLE_X11_XLIB)
    /* Update current workspace for all windows before saving. */ {
        iConstForEach(PtrArray, it, &app_.mainWindows) {
            const iMainWindow *win = it.ptr;
            if (win && win->base.win) {
                unsigned long dk;
                if (getDesktop_SDLWindow(win->base.win, &dk)) {
                    ((iMainWindow *) win)->place.desktop = (int) dk;
                }
            }
        }
    }
#endif
    iString *cfg = serializePrefs_App_(d);
    lock_Mutex(saveMutex_App_);
    iFile *f = newCStr_File(concatPath_CStr(dataDir_App_(), tempPrefsFileName_App_));
    if (open_File(f, writeOnly_FileMode | text_FileMode)) {
        write_File(f, &cfg->chars);
        iRelease(f);
        /* Copy it over to the real file. This avoids truncation if the app for any reason
           crashes before the prefs file is fully written. */
        commitFile_Core(concatPath_CStr(dataDir_App_(), prefsFileName_App_),
                        concatPath_CStr(dataDir_App_(), tempPrefsFileName_App_));
    }
    else {
        iRelease(f);
        fprintf(stderr, "[App] failed to save prefs: %s\n", strerror(errno));
    }
    unlock_Mutex(saveMutex_App_);
    delete_String(cfg);
}

static void updateNetworkProxy_App_(iApp *d) {
    iNetworkProxy *proxy = NULL;
    if (d->prefs.useProxy && !isEmpty_String(&d->prefs.strings[socksServer_PrefsString])) {
        iString *uri =
            collect_String(copy_String(&d->prefs.strings[socksServer_PrefsString]));
        if (indexOfCStr_String(uri, "://") == iInvalidPos) {
            prependCStr_String(uri, "socks5://");
        }
        iUrl parts;
        init_Url(&parts, uri);
        if (isEmpty_Range(&parts.host)) {
            use_NetworkProxy(NULL);
            return;
        }
        const uint16_t port = port_Url(&parts);
        proxy = new_NetworkProxy(collectNewRange_String(parts.host), port ? port : 1080);
        setCredentials_NetworkProxy(proxy,
                                    &d->prefs.strings[socksUser_PrefsString],
                                    &d->prefs.strings[socksPassword_PrefsString]);
        use_NetworkProxy(proxy);
    }
    else {
        use_NetworkProxy(NULL);
    }
}

static void prefsStringValueChanged_App_(iApp *d, enum iPrefsString id, iBool isDeferred,
                                         iBool wasChanged) {
    switch (id) {
        case uiLanguage_PrefsString:
            if (wasChanged) {
                setCurrent_Lang(cstr_String(&d->prefs.strings[id]));
                postCommand_App("lang.changed");
            }
            break;
        case searchUrl_PrefsString: {
            iString *url = &d->prefs.strings[id];
            if (startsWith_String(url, "//")) {
                prependCStr_String(url, "gemini:");
            }
            if (!isEmpty_String(url) && equal_Rangecc(urlScheme_String(url), "")) {
                prependCStr_String(url, "gemini://");
            }
            break;
        }
        case caFile_PrefsString:
        case caPath_PrefsString:
            if (!isDeferred) {
                updateCACertificates_App();
            }
            break;
        case socksServer_PrefsString:
        case socksUser_PrefsString:
        case socksPassword_PrefsString:
            if (!isDeferred) {
                updateNetworkProxy_App_(d);
            }
            break;
        default:
            break;
    }
}

void prefsBoolValueChanged_App_(iApp *d, enum iPrefsBool id, iBool isFrozen) {
    switch (id) {
        case hideToolbarOnScroll_PrefsBool:
            if (!d->prefs.hideToolbarOnScroll) {
                showToolbar_Root(get_Root(), iTrue);
            }
            break;
        case simpleChars_PrefsBool:
#if defined (iPlatformTerminal)
            SDL_SetHint(SDL_HINT_VIDEO_CURSES_SIMPLE_CHARACTERS, d->prefs.simpleChars ? "1" : "0");
            invalidate_Window(d->window);
#endif
            break;
        case evenSplit_PrefsBool:
            if (!isFrozen) {
                iForEach(PtrArray, i, &d->mainWindows) {
                    resizeSplits_MainWindow(i.ptr, iTrue);
                }
            }
            break;
        case useGamepad_PrefsBool:
            if (d->prefs.useGamepad && !d->gamepad) {
                d->gamepad = new_Gamepad();
            }
            else if (!d->prefs.useGamepad && d->gamepad) {
                delete_Gamepad(d->gamepad);
                d->gamepad = NULL;
            }
            break;
        case thickScrollBar_PrefsBool:
            if (!isFrozen) {
                postCommand_App("scrollbar.metrics");
                postCommand_App("window.resized"); /* redo layout */
            }
            else if (!isFinishedLaunching_App()) {
                postCommand_App("~scrollbar.metrics");
            }
            break;
        case preferIPv6_PrefsBool:
            setPreferIPv6_Socket(d->prefs.preferIPv6);
            break;
        case useProxy_PrefsBool:
            if (isFinishedLaunching_App()) {
                updateNetworkProxy_App_(d);
            }
            break;
        case colorEmoji_PrefsBool:
            resetFonts_App();
            postCommand_App("font.changed");
            break;
        case italicQuote_PrefsBool:
        case monospaceGemini_PrefsBool:
        case monospaceGopher_PrefsBool:
        case fontSmoothing_PrefsBool:
            /* The glyphs must be redrawn, so hide the intermediate state. */
            if (!isFrozen && get_MainWindow()) {
                setFreezeDraw_MainWindow(get_MainWindow(), iTrue);
                if (id == fontSmoothing_PrefsBool) {
                    resetFontCache_Text(text_Window(get_MainWindow()));
                }
                postCommand_App("font.changed");
                postCommand_App("window.unfreeze");
            }
            break;
        default:
            break;
    }
    if (isFrozen) {
        return;
    }
    const iPrefsSpec *spec = boolSpec_Prefs(id);
    if (spec->notify) {
        postCommand_App(spec->notify);
    }
    if (spec->flags & refresh_PrefsSpecFlag) {
        postRefreshAllWindows_App();
    }
    if (spec->flags & invalidate_PrefsSpecFlag) {
        invalidate_Window(d->window);
    }
}

iBool handlePrefsCommand_App_(iApp *d, const char *cmd) {
    const iBool isFrozen = !d->window ||
        (d->window->type == main_WindowType && as_MainWindow(d->window)->isDrawFrozen);
    /* Boolean preferences. */ {
        const enum iPrefsBool boolPref = findBool_Prefs(name_Command(cmd));
        if (boolPref < max_PrefsBool) {
            if (setBool_Prefs(&d->prefs, boolPref, arg_Command(cmd) != 0)) {
                prefsBoolValueChanged_App_(d, boolPref, isFrozen);
            }
            return iTrue;
        }
    }
    /* String preferences. */ {
        const iRangecc cmdName    = name_Command(cmd);
        const iBool    isDeferred = argLabel_Command(cmd, "noset") ||
                                    argLabel_Command(cmd, "noupdate");
        iBool didSet = iFalse;
        for (enum iPrefsString i = 0; i < max_PrefsString; i++) {
            const iPrefsStringSpec *spec = stringSpec_Prefs(i);
            if (spec->flags & noDispatch_PrefsSpecFlag || !equal_Rangecc(cmdName, spec->cmd) ||
                !hasLabel_Command(cmd, spec->label)) {
                continue;
            }
            iString    *value      = &d->prefs.strings[i];
            const char *arg        = suffixPtr_Command(cmd, spec->label);
            const iBool wasChanged = !equal_Rangecc(range_String(value), arg);
            setCStr_String(value, arg);
            prefsStringValueChanged_App_(d, i, isDeferred, wasChanged);
            didSet = iTrue;
        }
        if (didSet) {
            return iTrue;
        }
    }
    /* Commands related to preferences. */
    if (equal_Command(cmd, "prefs.changed")) {
        savePrefs_App_(d);
        return iTrue;
    }
    else if (equal_Command(cmd, "snippets.changed")) {
        save_Snippets(dataDir_App_());
        return iFalse;
    }
    else if (equal_Command(cmd, "width.save")) {
        insert_StringHash(d->savedWidths,
                          string_Command(cmd, "id"),
                          iClob(new_SavedWidth(argf_Command(cmd))));
        return iTrue;
    }
    else if (equal_Command(cmd, "document.openurls.changed")) {
        saveStateQuickly_App();
        return iTrue;
    }
    else if (equal_Command(cmd, "prompturl.toggle")) {
        const iString *url = string_Command(cmd, "url");
        iUrl parts;
        init_Url(&parts, url);
        const iString *path = collectNewRange_String(parts.path);
        const iString *site = collectNewRange_String(urlRoot_String(url));
        const enum iSiteSpecKey key = promptPaths_SiteSpecKey;
        if (!contains_StringSet(stringSet_SiteSpec(site, key), path)) {
            insertString_SiteSpec(site, key, path);
        }
        else {
            removeString_SiteSpec(site, key, path);
        }
        return iTrue;
    }
    else if (equal_Command(cmd, "recentinput.clear")) {
        clearSubmittedInput_App();
        return iTrue;
    }
    else if (equal_Command(cmd, "prefs.dialogtab")) {
        d->prefs.dialogTab = arg_Command(cmd);
        return iTrue;
    }
    else if (equal_Command(cmd, "navbar.action.set")) {
        d->prefs.navbarActions[iClamp(argLabel_Command(cmd, "button"), 0, maxNavbarActions_Prefs - 1)] =
            iClamp(arg_Command(cmd), 0, max_ToolbarAction - 1);
        if (!isFrozen) {
            postCommand_App("~navbar.actions.changed");
        }
        return iTrue;
    }
    else if (startsWith_Command(cmd, "prefs.sidebar.enabled.")) {
        const int mode = atoi(cmd + 22);
        postCommandf_App("sidebar.modes.set arg:%d side:0 mode:%d", arg_Command(cmd), mode);
        return iTrue;
    }
    else if (startsWith_Command(cmd, "prefs.sidebar2.enabled.")) {
        const int mode = atoi(cmd + 23);
        postCommandf_App("sidebar.modes.set arg:%d side:1 mode:%d", arg_Command(cmd), mode);
        return iTrue;
    }
    else if (equal_Command(cmd, "sidebar.modes.set")) {
        const int side = iClamp(argLabel_Command(cmd, "side"), 0, 1);
        const int mode = iClamp(argLabel_Command(cmd, "mode"), 0, maxSidebarModes_Prefs - 1);
        const iBool newValue = arg_Command(cmd) != 0;
        if (d->prefs.sidebarModeEnabled[side][mode] != newValue) {
            d->prefs.sidebarModeEnabled[side][mode] = newValue;
            if (!isFrozen) {
                postCommand_App("~sidebar.modes.changed");
            }
        }
        return iTrue;
    }
    else if (equal_Command(cmd, "toolbar.action.set")) {
        d->prefs.toolbarActions[iClamp(argLabel_Command(cmd, "button"), 0, 1)] =
            iClamp(arg_Command(cmd), 0, max_ToolbarAction - 1);
        if (!isFrozen) {
            postCommand_App("~toolbar.actions.changed");
        }
        return iTrue;
    }
    else if (equal_Command(cmd, "prefs.menubar.changed")) {
        d->prefs.menuBar = (arg_Command(cmd) != 0) || isTerminal_Platform(); /* forced in TUI */
        if (!isFrozen) {
            postCommand_App("~root.movable");
        }
        return iTrue;
    }
    else if (equal_Command(cmd, "translation.languages")) {
        d->prefs.langFrom             = argLabel_Command(cmd, "from");
        d->prefs.langTo               = argLabel_Command(cmd, "to");
        d->prefs.translationIgnorePre = argLabel_Command(cmd, "pre") == 0;
        return iTrue;
    }
    else if (equal_Command(cmd, "window.setdesktop")) {
#if defined (LAGRANGE_ENABLE_X11_XLIB)
        const int      desk  = arg_Command(cmd);
        const uint32_t winId = argLabel_Command(cmd, "window");
        if (desk >= 0) {
            /* Find the window by ID. */
            iConstForEach(PtrArray, i, &d->mainWindows) {
                iMainWindow *win = i.ptr;
                if (id_Window(as_Window(win)) == winId) {
                    win->place.desktop = desk;
                    /* Use the active desktop switching function. */
                    setDesktop_SDLWindow(win->base.win, (unsigned long) desk);
                    break;
                }
            }
        }
#endif
        return iTrue;
    }
    else if (equal_Command(cmd, "font.set")) {
        if (!isFrozen && get_MainWindow()) {
            setFreezeDraw_MainWindow(get_MainWindow(), iTrue);
        }
        struct {
            const char *label;
            enum iPrefsString ps;
            int fontId;
        } params[] = {
            { "ui",      uiFont_PrefsString,                default_FontId },
            { "mono",    monospaceFont_PrefsString,         monospace_FontId },
            { "heading", headingFont_PrefsString,           documentHeading_FontId },
            { "body",    bodyFont_PrefsString,              documentBody_FontId },
            { "monodoc", monospaceDocumentFont_PrefsString, documentMonospace_FontId },
        };
        iBool wasChanged = iFalse;
        iForIndices(i, params) {
            if (hasLabel_Command(cmd, params[i].label)) {
                iString *ps = &d->prefs.strings[params[i].ps];
                const iString *newFont = string_Command(cmd, params[i].label);
                if (!equal_String(ps, newFont)) {
                    set_String(ps, newFont);
                    wasChanged = iTrue;
                }
            }
        }
        if (wasChanged) {
            if (isFinishedLaunching_App() && get_MainWindow()) { /* there's a reset when launch is finished */
                resetFonts_Text(text_Window(get_MainWindow()));
                postCommand_App("font.changed");
            }
        }
        if (!isFrozen) {
            postCommand_App("window.unfreeze");
        }
        return iTrue;
    }
    else if (equal_Command(cmd, "prefs.gemtext.ansi.fg.changed")) {
        iChangeFlags(d->prefs.gemtextAnsiEscapes, allowFg_AnsiFlag, arg_Command(cmd));
        return iTrue;
    }
    else if (equal_Command(cmd, "prefs.gemtext.ansi.bg.changed")) {
        iChangeFlags(d->prefs.gemtextAnsiEscapes, allowBg_AnsiFlag, arg_Command(cmd));
        return iTrue;
    }
    else if (equal_Command(cmd, "prefs.gemtext.ansi.fontstyle.changed")) {
        iChangeFlags(d->prefs.gemtextAnsiEscapes, allowFontStyle_AnsiFlag, arg_Command(cmd));
        return iTrue;
    }
    else if (equal_Command(cmd, "collapsepre.set")) {
        d->prefs.collapsePre = arg_Command(cmd);
        return iTrue;
    }
    else if (equal_Command(cmd, "prefs.hoverlink.toggle")) {
        setBool_Prefs(&d->prefs, hoverLink_PrefsBool, !d->prefs.hoverLink);
        prefsBoolValueChanged_App_(d, hoverLink_PrefsBool, isFrozen);
        return iTrue;
    }
    else if (equal_Command(cmd, "scrollspeed")) {
        const int type = argLabel_Command(cmd, "type");
        if (type == keyboard_ScrollType || type == mouse_ScrollType) {
            d->prefs.smoothScrollSpeed[type] = iClamp(arg_Command(cmd), 1, 40);
        }
        return iTrue;
    }
    else if (equal_Command(cmd, "returnkey.set")) {
        d->prefs.returnKey = arg_Command(cmd);
        return iTrue;
    }
    else if (equal_Command(cmd, "gamepad.set")) {
#if defined (LAGRANGE_USE_GAMEPAD)
        if (argLabel_Command(cmd, "clear")) {
            for (int i = 0; i < max_GamepadAction; i++) {
                actions_Gamepad[i] = unassigned_Gamepad;
            }
            return iTrue;
        }
        const int trig      = argLabel_Command(cmd, "trig");
        const int button    = argLabel_Command(cmd, "button");
        const int modButton = button | (trig ? triggerMod_Gamepad : 0);
        const int action    = arg_Command(cmd);
        if (action >= 0 && action < max_GamepadAction) {
            actions_Gamepad[action] = modButton;
        }
        else {
            for (int i = 0; i < max_GamepadAction; i++) {
                if (actions_Gamepad[i] == modButton) {
                    actions_Gamepad[i] = unassigned_Gamepad;
                }
            }
        }
#endif
        return iTrue;
    }
    else if (equal_Command(cmd, "pinsplit.set")) {
        d->prefs.pinSplit = arg_Command(cmd);
        return iTrue;
    }
    else if (equal_Command(cmd, "promptposition.set")) {
        d->prefs.promptPosition = arg_Command(cmd);
        return iTrue;
    }
    else if (equal_Command(cmd, "feedinterval.set")) {
        d->prefs.feedInterval = arg_Command(cmd);
        setRefreshInterval_Feeds(d->prefs.feedInterval);
        return iTrue;
    }
    else if (equal_Command(cmd, "theme.set")) {
        const int isAuto = argLabel_Command(cmd, "auto");
        d->prefs.theme = arg_Command(cmd);
        if (!isAuto) {
            if (isDark_ColorTheme(d->prefs.theme) && d->isDarkSystemTheme) {
                d->prefs.systemPreferredColorTheme[0] = d->prefs.theme;
            }
            else if (!isDark_ColorTheme(d->prefs.theme) && !d->isDarkSystemTheme) {
                d->prefs.systemPreferredColorTheme[1] = d->prefs.theme;
            }
            else {
                postCommand_App("ostheme arg:0");
            }
        }
        setThemePalette_Color(d->prefs.theme);
        postCommandf_App("theme.changed auto:%d", isAuto);
        return iTrue;
    }
    else if (equal_Command(cmd, "accent.set")) {
        d->prefs.accent = arg_Command(cmd);
        setThemePalette_Color(d->prefs.theme);
        if (!isFrozen) {
            invalidate_Window(d->window);
        }
        return iTrue;
    }
    else if (equal_Command(cmd, "ostheme") || equal_Command(cmd, "prefs.ostheme.changed")) {
        d->prefs.useSystemTheme = arg_Command(cmd);
        if (hasLabel_Command(cmd, "preferdark")) {
            d->prefs.systemPreferredColorTheme[0] = argLabel_Command(cmd, "preferdark");
        }
        if (hasLabel_Command(cmd, "preferlight")) {
            d->prefs.systemPreferredColorTheme[1] = argLabel_Command(cmd, "preferlight");
        }
        return iTrue;
    }
    else if (equal_Command(cmd, "doctheme.dark.set")) {
        d->prefs.docThemeDark = arg_Command(cmd);
        if (!isFrozen) {
            invalidate_Window(d->window);
        }
        return iTrue;
    }
    else if (equal_Command(cmd, "doctheme.light.set")) {
        d->prefs.docThemeLight = arg_Command(cmd);
        if (!isFrozen) {
            invalidate_Window(d->window);
        }
        return iTrue;
    }
    else if (equal_Command(cmd, "imagestyle.set")) {
        d->prefs.imageStyle = arg_Command(cmd);
        return iTrue;
    }
    else if (equal_Command(cmd, "linewidth.set")) {
        const int lineWidth = iMax(20, arg_Command(cmd));
        if (lineWidth != d->prefs.lineWidth) {
            d->prefs.lineWidth = lineWidth;
            postCommand_App("document.layout.changed redo:1");
        }
        return iTrue;
    }
    else if (equal_Command(cmd, "linespacing.set")) {
        const float spacing = iMax(0.5f, argf_Command(cmd));
        if (spacing != d->prefs.lineSpacing) {
            d->prefs.lineSpacing = spacing;
            postCommand_App("document.layout.changed redo:1");
        }
        return iTrue;
    }
    else if (equal_Command(cmd, "tabwidth.set")) {
        const int tabWidth = iMax(1, arg_Command(cmd));;
        if (tabWidth != d->prefs.tabWidth) {
            d->prefs.tabWidth = tabWidth;
            postCommand_App("document.layout.changed redo:1"); /* spaces need renormalizing */
        }
        return iTrue;
    }
    else if (equal_Command(cmd, "ansiescape")) {
        d->prefs.gemtextAnsiEscapes = arg_Command(cmd);
        return iTrue;
    }
    else if (equal_Command(cmd, "saturation.set")) {
        d->prefs.saturation = (float) arg_Command(cmd) / 100.0f;
        if (!isFrozen) {
            invalidate_Window(d->window);
        }
        return iTrue;
    }
    else if (equal_Command(cmd, "cachesize.set")) {
        d->prefs.maxCacheSize = arg_Command(cmd);
        if (d->prefs.maxCacheSize <= 0) {
            d->prefs.maxCacheSize = 0;
        }
        return iTrue;
    }
    else if (equal_Command(cmd, "memorysize.set")) {
        d->prefs.maxMemorySize = arg_Command(cmd);
        if (d->prefs.maxMemorySize <= 0) {
            d->prefs.maxMemorySize = 0;
        }
        return iTrue;
    }
    else if (equal_Command(cmd, "urlsize.set")) {
        d->prefs.maxUrlSize = arg_Command(cmd);
        if (d->prefs.maxUrlSize < 1024) {
            d->prefs.maxUrlSize = 1024; /* Gemini protocol requirement */
        }
        return iTrue;
    }
#if defined (LAGRANGE_ENABLE_DOWNLOAD_EDIT)
    else if (equal_Command(cmd, "downloads")) {
        setCStr_String(&d->prefs.strings[downloadDir_PrefsString], suffixPtr_Command(cmd, "path"));
        return iTrue;
    }
#endif
    return iFalse;
}
