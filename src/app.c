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

#include "app.h"
#include "bookmarks.h"
#include "defs.h"
#include "export.h"
#include "feeds.h"
#include "gmcerts.h"
#include "gmdocument.h"
#include "gmutil.h"
#include "history.h"
#include "ipc.h"
#include "mimehooks.h"
#include "periodic.h"
#include "resources.h"
#include "sitespec.h"
#include "ui/certimportwidget.h"
#include "ui/color.h"
#include "ui/command.h"
#include "ui/documentwidget.h"
#include "ui/inputwidget.h"
#include "ui/keys.h"
#include "ui/labelwidget.h"
#include "ui/root.h"
#include "ui/sidebarwidget.h"
#include "ui/text.h"
#include "ui/touch.h"
#include "ui/uploadwidget.h"
#include "ui/util.h"
#include "ui/window.h"
#include "updater.h"
#include "visited.h"

#include <the_Foundation/buffer.h>
#include <the_Foundation/commandline.h>
#include <the_Foundation/file.h>
#include <the_Foundation/fileinfo.h>
#include <the_Foundation/garbage.h>
#include <the_Foundation/path.h>
#include <the_Foundation/process.h>
#include <the_Foundation/sortedarray.h>
#include <the_Foundation/stringset.h>
#include <the_Foundation/time.h>
#include <the_Foundation/thread.h>
#include <the_Foundation/version.h>
#include <SDL.h>

#include <stdio.h>
#include <stdarg.h>
#include <errno.h>

//#define LAGRANGE_ENABLE_MOUSE_TOUCH_EMULATION 1

#if defined (iPlatformAppleDesktop)
#   include "macos.h"
#endif
#if defined (iPlatformAppleMobile)
#   include "ios.h"
#endif
#if defined (iPlatformAndroidMobile)
#   include "android.h"
#   include <SDL_log.h>
#endif
#if defined (iPlatformMsys)
#   include "win32.h"
#endif
#if SDL_VERSION_ATLEAST(2, 0, 14)
#   include <SDL_misc.h>
#endif

iDeclareType(App)

#if defined (iPlatformAppleDesktop)
#define EMB_BIN "../../Resources/resources.lgr"
static const char *defaultDataDir_App_ = "~/Library/Application Support/fi.skyjake.Lagrange";
#endif

#if defined (iPlatformAppleMobile)
#define EMB_BIN "../../Resources/resources.lgr"
static const char *defaultDataDir_App_ = "~/Library/Application Support";
#endif

#if defined (iPlatformMsys)
#define EMB_BIN "../resources.lgr"
static const char *defaultDataDir_App_ = "~/AppData/Roaming/fi.skyjake.Lagrange";
#endif

#if defined (iPlatformAndroidMobile)
#define EMB_BIN "resources.lgr" /* loaded from assets with SDL_rwops */
static const char *defaultDataDir_App_ = NULL; /* will ask SDL */

#elif defined (iPlatformLinux) || defined (iPlatformTerminal) || defined (iPlatformOther)
#define EMB_BIN  "../../share/lagrange/resources.lgr"
#define EMB_BIN2  "../../../share/lagrange/resources.lgr"
static const char *defaultDataDir_App_ = "~/.config/lagrange";
#endif

#if defined (iPlatformHaiku)
#define EMB_BIN "./resources.lgr"
static const char *defaultDataDir_App_ = "~/config/settings/lagrange";
#endif

#define EMB_BIN_EXEC "../resources.lgr" /* fallback from build/executable dir */
#if defined (iPlatformTerminal)
#   define STATE_NAME "cstate" /* separate for console since it's a different environment */
#   define PREFS_NAME "cprefs"
#else
#   define STATE_NAME "state"
#   define PREFS_NAME "prefs"
#endif

static const char *prefsFileName_App_      = PREFS_NAME ".cfg";
static const char *oldStateFileName_App_   = STATE_NAME ".binary";
static const char *stateFileName_App_      = STATE_NAME ".lgr";
static const char *tempStateFileName_App_  = STATE_NAME ".lgr.tmp";
static const char *defaultDownloadDir_App_ = "~/Downloads";

static const int idleThreshold_App_ = 1000; /* ms */

struct Impl_App {
    iCommandLine args;
    iString *    execPath;
    iStringSet * tempFilesPendingDeletion;
    iStringList *recentlyClosedTabUrls; /* for reopening, like an undo stack */
    iMimeHooks * mimehooks;
    iGmCerts *   certs;
    iVisited *   visited;
    iBookmarks * bookmarks;
    iMainWindow *window; /* currently active MainWindow */
    iPtrArray    mainWindows;
    iPtrArray    popupWindows;
    iSortedArray tickers; /* per-frame callbacks, used for animations */
    uint32_t     lastTickerTime;
    uint32_t     elapsedSinceLastTicker;
    iBool        isRunning;
    iBool        isRunningUnderWindowSystem;
    iBool        isDarkSystemTheme;
    iBool        isSuspended;
#if defined (LAGRANGE_ENABLE_IDLE_SLEEP)
    iBool        isIdling;
    uint32_t     lastEventTime;
    int          sleepTimer;
    unsigned int idleSleepDelayMs;
#endif
    iAtomicInt   pendingRefresh;
    iBool        isLoadingPrefs;
    iStringList *launchCommands;
    iBool        isFinishedLaunching;
    iTime        lastDropTime; /* for detecting drops of multiple items */
    int          autoReloadTimer;
    iPeriodic    periodic;
    int          warmupFrames; /* forced refresh just after resuming from background; FIXME: shouldn't be needed */
#if defined (iPlatformAndroidMobile)
    float        displayDensity;
#endif
    /* Preferences: */
    iBool        commandEcho;         /* --echo */
    iBool        forceSoftwareRender; /* --sw */
    iArray       initialWindowRects;  /* one per window */
    iPrefs       prefs;
};

static iApp app_;

/*----------------------------------------------------------------------------------------------*/

iDeclareType(Ticker)

struct Impl_Ticker {
    iAny *context;
    iRoot *root;
    void (*callback)(iAny *);
};

static int cmp_Ticker_(const void *a, const void *b) {
    const iTicker *elems[2] = { a, b };
    const int cmp = iCmp(elems[0]->context, elems[1]->context);
    if (cmp) {
        return cmp;
    }
    return iCmp((void *) elems[0]->callback, (void *) elems[1]->callback);
}

/*----------------------------------------------------------------------------------------------*/

const iString *dateStr_(const iDate *date) {
    return collectNewFormat_String("%d-%02d-%02d %02d:%02d:%02d",
                                   date->year,
                                   date->month,
                                   date->day,
                                   date->hour,
                                   date->minute,
                                   date->second);
}

static iString *serializePrefs_App_(const iApp *d) {
    iString *str = new_String();
    appendCStr_String(str, "version app:" LAGRANGE_APP_VERSION "\n");
#if defined (LAGRANGE_ENABLE_CUSTOM_FRAME)
    appendFormat_String(str, "customframe arg:%d\n", d->prefs.customFrame);
#endif
    appendFormat_String(str, "window.retain arg:%d\n", d->prefs.retainWindowSize);
    if (d->prefs.retainWindowSize) {
        int w, h, x, y;
        iConstForEach(PtrArray, i, &d->mainWindows) {
            const iMainWindow *win = i.ptr;
            const size_t winIndex = index_PtrArrayConstIterator(&i);
            x = win->place.normalRect.pos.x;
            y = win->place.normalRect.pos.y;
            w = win->place.normalRect.size.x;
            h = win->place.normalRect.size.y;
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
#if 0
#i f defined (LAGRANGE_ENABLE_CUSTOM_FRAME)
            if (snap_MainWindow(win)) {
                if (snap_MainWindow(win) == maximized_WindowSnap) {
                    appendFormat_String(str, "~window.maximize index:%zu\n", winIndex);
                }
                else if (~SDL_GetWindowFlags(win->base.win) & SDL_WINDOW_MINIMIZED) {
                    /* Save the actual visible window position, too, because snapped windows may
                       still be resized/moved without affecting normalRect. */
                    SDL_GetWindowPosition(win->base.win, &x, &y);
                    SDL_GetWindowSize(win->base.win, &w, &h);
                    appendFormat_String(
                        str, "~window.setrect index:%zu snap:%d width:%d height:%d coord:%d %d\n",
                        winIndex, snap_MainWindow(d->window), w, h, x, y);
                }
            }
//#elif !defined (iPlatformApple)
//            if (snap_MainWindow(win) == maximized_WindowSnap) {
//                appendFormat_String(str, "~window.maximize index:%zu\n", winIndex);
//            }
#endif
        }
    }
    appendFormat_String(str, "uilang id:%s\n", cstr_String(&d->prefs.strings[uiLanguage_PrefsString]));
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
    appendFormat_String(str, "smoothscroll arg:%d\n", d->prefs.smoothScrolling);
    appendFormat_String(str, "scrollspeed arg:%d type:%d\n", d->prefs.smoothScrollSpeed[keyboard_ScrollType], keyboard_ScrollType);
    appendFormat_String(str, "scrollspeed arg:%d type:%d\n", d->prefs.smoothScrollSpeed[mouse_ScrollType], mouse_ScrollType);
    appendFormat_String(str, "imageloadscroll arg:%d\n", d->prefs.loadImageInsteadOfScrolling);
    appendFormat_String(str, "cachesize.set arg:%d\n", d->prefs.maxCacheSize);
    appendFormat_String(str, "memorysize.set arg:%d\n", d->prefs.maxMemorySize);
    appendFormat_String(str, "urlsize.set arg:%d\n", d->prefs.maxUrlSize);
    appendFormat_String(str, "decodeurls arg:%d\n", d->prefs.decodeUserVisibleURLs);
    appendFormat_String(str, "linewidth.set arg:%d\n", d->prefs.lineWidth);
    appendFormat_String(str, "linespacing.set arg:%f\n", d->prefs.lineSpacing);
    appendFormat_String(str, "tabwidth.set arg:%d\n", d->prefs.tabWidth);
    appendFormat_String(str, "returnkey.set arg:%d\n", d->prefs.returnKey);
    for (size_t i = 0; i < iElemCount(d->prefs.navbarActions); i++) {
        appendFormat_String(str, "navbar.action.set arg:%d button:%d\n", d->prefs.navbarActions[i], i);
    }
    if (isMobile_Platform()) {
        appendFormat_String(str, "hidetoolbarscroll arg:%d\n", d->prefs.hideToolbarOnScroll);
        appendFormat_String(str, "toolbar.action.set arg:%d button:0\n", d->prefs.toolbarActions[0]);
        appendFormat_String(str, "toolbar.action.set arg:%d button:1\n", d->prefs.toolbarActions[1]);
    }
    iConstForEach(StringSet, fp, d->prefs.disabledFontPacks) {
        appendFormat_String(str, "fontpack.disable id:%s\n", cstr_String(fp.value));
    }
    appendFormat_String(str, "ansiescape arg:%d\n", d->prefs.gemtextAnsiEscapes);
    /* TODO: This array belongs in Prefs. It can then be used for command handling as well. */
    const struct {
        const char * id;
        const iBool *value;
    } boolPrefs[] = {
        { "prefs.animate", &d->prefs.uiAnimations },
        { "prefs.archive.openindex", &d->prefs.openArchiveIndexPages },
        { "prefs.biglede", &d->prefs.bigFirstParagraph },
        { "prefs.blink", &d->prefs.blinkingCursor },
        { "prefs.bottomnavbar", &d->prefs.bottomNavBar },
        { "prefs.bottomtabbar", &d->prefs.bottomTabBar },
        { "prefs.evensplit", &d->prefs.evenSplit },
        { "prefs.menubar", &d->prefs.menuBar },
        { "prefs.boldlink.dark", &d->prefs.boldLinkDark },
        { "prefs.boldlink.light", &d->prefs.boldLinkLight },
        { "prefs.boldlink.visited", &d->prefs.boldLinkVisited },
        { "prefs.bookmarks.addbottom", &d->prefs.addBookmarksToBottom },
        { "prefs.centershort", &d->prefs.centerShortDocs },
        { "prefs.collapsepreonload", &d->prefs.collapsePreOnLoad },
        { "prefs.dataurl.openimages", &d->prefs.openDataUrlImagesOnLoad },
        { "prefs.font.smooth", &d->prefs.fontSmoothing },
        { "prefs.font.warnmissing", &d->prefs.warnAboutMissingGlyphs },
        { "prefs.hoverlink", &d->prefs.hoverLink },
        { "prefs.justify", &d->prefs.justifyParagraph },
        { "prefs.markdown.viewsource", &d->prefs.markdownAsSource },
        { "prefs.mono.gemini", &d->prefs.monospaceGemini },
        { "prefs.mono.gopher", &d->prefs.monospaceGopher },
        { "prefs.plaintext.wrap", &d->prefs.plainTextWrap },
        { "prefs.retaintabs", &d->prefs.retainTabs },
        { "prefs.sideicon", &d->prefs.sideIcon },
        { "prefs.time.24h", &d->prefs.time24h },
        { "prefs.tui.simple", &d->prefs.simpleChars },
    };
    iForIndices(i, boolPrefs) {
        appendFormat_String(str, "%s.changed arg:%d\n", boolPrefs[i].id, *boolPrefs[i].value);
    }
    appendFormat_String(str, "parentnavskipindex arg:%d\n", d->prefs.skipIndexPageOnParentNavigation);
    appendFormat_String(str, "quoteicon.set arg:%d\n", d->prefs.quoteIcon ? 1 : 0);
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
    appendFormat_String(str, "ca.file noset:1 path:%s\n", cstr_String(&d->prefs.strings[caFile_PrefsString]));
    appendFormat_String(str, "ca.path path:%s\n", cstr_String(&d->prefs.strings[caPath_PrefsString]));
    appendFormat_String(str, "proxy.gemini address:%s\n", cstr_String(&d->prefs.strings[geminiProxy_PrefsString]));
    appendFormat_String(str, "proxy.gopher address:%s\n", cstr_String(&d->prefs.strings[gopherProxy_PrefsString]));
    appendFormat_String(str, "proxy.http address:%s\n", cstr_String(&d->prefs.strings[httpProxy_PrefsString]));
#if defined (LAGRANGE_ENABLE_DOWNLOAD_EDIT)
    appendFormat_String(str, "downloads path:%s\n", cstr_String(&d->prefs.strings[downloadDir_PrefsString]));
#endif
    appendFormat_String(str, "searchurl address:%s\n", cstr_String(&d->prefs.strings[searchUrl_PrefsString]));
    appendFormat_String(str, "translation.languages from:%d to:%d\n", d->prefs.langFrom, d->prefs.langTo);
    return str;
}

static const char *dataDir_App_(void) {
#if defined (iPlatformLinux) || defined (iPlatformOther)
    const char *configHome = getenv("XDG_CONFIG_HOME");
    if (configHome) {
        return concatPath_CStr(configHome, "lagrange");
    }
#endif
#if defined (iPlatformMsys)
    /* Check for a portable userdata directory. */
    iApp *d = &app_;
    const char *userDir = concatPath_CStr(cstr_String(d->execPath), "..\\userdata");
    if (fileExistsCStr_FileInfo(userDir)) {
        return userDir;
    }
#endif
    if (defaultDataDir_App_) {
        return defaultDataDir_App_;
    }
    return SDL_GetPrefPath("Jaakko Keränen", "fi.skyjake.lagrange");
}

static const char *downloadDir_App_(void) {
#if defined (iPlatformAndroidMobile)
    const char *dir = concatPath_CStr(SDL_AndroidGetExternalStoragePath(), "Downloads");
    makeDirs_Path(collectNewCStr_String(dir));
    return dir;
#endif
#if defined (iPlatformLinux) || defined (iPlatformOther)
    /* Parse user-dirs.dirs using the `xdg-user-dir` tool. */
    iProcess *proc = iClob(new_Process());
    setArguments_Process(
        proc, iClob(newStringsCStr_StringList("/usr/bin/env", "xdg-user-dir", "DOWNLOAD", NULL)));
    if (start_Process(proc)) {
        iString *path = collect_String(newLocal_String(collect_Block(
            readOutputUntilClosed_Process(proc))));
        trim_String(path);
        if (!isEmpty_String(path)) {
            return cstr_String(path);
        }
    }
#endif
#if defined (iPlatformAppleMobile)
    /* Save to a local cache directory from where the user can export to the cloud. */
    const iString *dlDir = cleanedCStr_Path("~/Library/Caches/Downloads");
    if (!fileExists_FileInfo(dlDir)) {
        makeDirs_Path(dlDir);
    }
    return cstr_String(dlDir);
#endif
    return defaultDownloadDir_App_;
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

static void loadPrefs_App_(iApp *d) {
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
            else if (equal_Command(cmd, "customframe")) {
                d->prefs.customFrame = arg_Command(cmd);
            }
            else if (equal_Command(cmd, "window.setrect") && !argLabel_Command(cmd, "snap")) {
                const int   index   = argLabel_Command(cmd, "index");
                const iInt2 pos     = coord_Command(cmd);
                iRect       winRect = init_Rect(
                    pos.x, pos.y, argLabel_Command(cmd, "width"), argLabel_Command(cmd, "height"));
                if (index >= 0 && index < 100) {
                    resize_Array(&d->initialWindowRects, index + 1);
                    set_Array(&d->initialWindowRects, index, &winRect);
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

static void savePrefs_App_(const iApp *d) {
    iString *cfg = serializePrefs_App_(d);
    iFile *f = new_File(prefsFileName_());
    if (open_File(f, writeOnly_FileMode | text_FileMode)) {
        write_File(f, &cfg->chars);
    }
    iRelease(f);
    delete_String(cfg);
}

static const char *magicState_App_       = "lgL1";
static const char *magicWindow_App_      = "wind";
static const char *magicTabDocument_App_ = "tabd";
static const char *magicSidebar_App_     = "side";

enum iDocumentStateFlag {
    current_DocumentStateFlag    = iBit(1),
    rootIndex1_DocumentStateFlag = iBit(2),
};

enum iWindowStateFlag {
    current_WindowStateFlag = iBit(9),
};

static iRect initialWindowRect_App_(const iApp *d, size_t windowIndex) {
    if (windowIndex < size_Array(&d->initialWindowRects)) {
        return constValue_Array(&d->initialWindowRects, windowIndex, iRect);
    }
    /* The default window rectangle. */
    iRect rect = init_Rect(-1, -1, 900, 560);
#if defined (iPlatformMsys)
    /* Must scale by UI scaling factor. */
    mulfv_I2(&rect.size, desktopDPI_Win32());
#endif
#if defined (iPlatformLinux) && !defined (iPlatformAndroid) && !defined (iPlatformTerminal)
    /* Scale by the primary (?) monitor DPI. */
    if (isRunningUnderWindowSystem_App()) {
        float vdpi;
        SDL_GetDisplayDPI(0, NULL, NULL, &vdpi);
        const float factor = vdpi / 96.0f;
        mulfv_I2(&rect.size, iMax(factor, 1.0f));
    }
#endif
    return rect;    
}

static iBool loadState_App_(iApp *d) {
    iUnused(d);
    const char *oldPath = concatPath_CStr(dataDir_App_(), oldStateFileName_App_);
    const char *path    = concatPath_CStr(dataDir_App_(), stateFileName_App_);
    iFile *f = iClob(newCStr_File(fileExistsCStr_FileInfo(path) ? path : oldPath));
    if (open_File(f, readOnly_FileMode)) {
        char magic[4];
        readData_File(f, 4, magic);
        if (memcmp(magic, magicState_App_, 4)) {
            printf("%s: format not recognized\n", cstr_String(path_File(f)));
            return iFalse;
        }
        const uint32_t version = readU32_File(f);
        /* Check supported versions. */
        if (version > latest_FileVersion) {
            printf("%s: unsupported version\n", cstr_String(path_File(f)));
            return iFalse;
        }
        setVersion_Stream(stream_File(f), version);
        /* Window state. */
        iDeclareType(CurrentTabs);
        struct Impl_CurrentTabs {
            iDocumentWidget *currentTab[2]; /* for each root */
        };
        int              numWins       = 0;
        iMainWindow *    win           = NULL;
        iMainWindow *    currentWin    = d->window;
        iArray *         currentTabs; /* two per window (per root per window) */
        iBool            isFirstTab[2];
        currentTabs = collectNew_Array(sizeof(iCurrentTabs));
        while (!atEnd_File(f)) {
            readData_File(f, 4, magic);
            if (!memcmp(magic, magicWindow_App_, 4)) {
                numWins++;
                const int   splitMode = read32_File(f);
                const int   winState  = read32_File(f);
                const int   keyRoot   = (winState & 1);
                const iBool isCurrent = (winState & current_WindowStateFlag) != 0;
//                printf("[State] '%.4s' split:%d state:%x\n", magic, splitMode, winState);
#if defined (iPlatformTerminal)
                /* Terminal only supports one window. */
                win = d->window;
#else
                if (numWins == 1) {
                    win = d->window;
                }
                else {
                    win = new_MainWindow(initialWindowRect_App_(d, numWins - 1));
                    addWindow_App(win);
                }
#endif
                pushBack_Array(currentTabs, &(iCurrentTabs){ { NULL, NULL } });
                isFirstTab[0] = isFirstTab[1] = iTrue;
                if (isCurrent) {
                    currentWin = win;
                }
                setCurrent_Window(win);
                setCurrent_Root(NULL);
                win->pendingSplitMode = splitMode;
                setSplitMode_MainWindow(win, splitMode | noEvents_WindowSplit);
                win->base.keyRoot = win->base.roots[keyRoot];
            }
            else if (!memcmp(magic, magicSidebar_App_, 4)) {
                if (!win) {
                    printf("%s: missing window\n", cstr_String(path_File(f)));
                    setCurrent_Root(NULL);
                    return iFalse;                    
                }
                const uint16_t bits = readU16_File(f);
                const uint8_t modes = readU8_File(f);
                const float widths[2] = {
                    readf_Stream(stream_File(f)),
                    readf_Stream(stream_File(f))
                };
                iIntSet *closedFolders[2] = {
                    collectNew_IntSet(),
                    collectNew_IntSet()
                };
                if (version >= bookmarkFolderState_FileVersion) {
                    deserialize_IntSet(closedFolders[0], stream_File(f));
                    deserialize_IntSet(closedFolders[1], stream_File(f));
                }
                const uint8_t rootIndex = bits & 0xff;
                const uint8_t flags     = bits >> 8;
                iRoot *root = win->base.roots[rootIndex];
                if (root) {
                    iSidebarWidget *sidebar  = findChild_Widget(root->widget, "sidebar");
                    iSidebarWidget *sidebar2 = findChild_Widget(root->widget, "sidebar2");
                    setClosedFolders_SidebarWidget(sidebar, closedFolders[0]);
                    setClosedFolders_SidebarWidget(sidebar2, closedFolders[1]);
                    postCommandf_Root(root, "sidebar.mode arg:%u", modes & 0xf);
                    postCommandf_Root(root, "sidebar2.mode arg:%u", (modes >> 4) & 0xf);
                    if (flags & 4) {
                        postCommand_Widget(sidebar, "feeds.mode arg:%d", unread_FeedsMode);
                    }
                    if (flags & 8) {
                        postCommand_Widget(sidebar2, "feeds.mode arg:%d", unread_FeedsMode);
                    }
                    if (deviceType_App() == desktop_AppDeviceType) {
                        setWidth_SidebarWidget(sidebar,  widths[0]);
                        setWidth_SidebarWidget(sidebar2, widths[1]);
                        if (flags & 1) postCommand_Root(root, "sidebar.toggle noanim:1");
                        if (flags & 2) postCommand_Root(root, "sidebar2.toggle noanim:1");
                    }
                }
            }
            else if (!memcmp(magic, magicTabDocument_App_, 4)) {
                if (!win) {
                    printf("%s: missing window\n", cstr_String(path_File(f)));
                    setCurrent_Root(NULL);
                    return iFalse;                    
                }
                const int8_t flags = read8_File(f);
                int rootIndex = flags & rootIndex1_DocumentStateFlag ? 1 : 0;
                if (rootIndex > numRoots_Window(as_Window(win)) - 1) {
                    rootIndex = 0;
                }
                setCurrent_Root(win->base.roots[rootIndex]);
                iDocumentWidget *doc = NULL;
                if (d->prefs.retainTabs) {
                    if (isFirstTab[rootIndex]) {
                        isFirstTab[rootIndex] = iFalse;
                        /* There is one pre-created tab in each root. */
                        doc = document_Root(get_Root());
                    }
                    else {
                        doc = newTab_App(NULL, iFalse /* no switching */);
                    }
                    if (flags & current_DocumentStateFlag) {
                        value_Array(currentTabs, numWins - 1, iCurrentTabs).currentTab[rootIndex] = doc;
                    }
                }
                deserializeState_DocumentWidget(doc, stream_File(f));
                doc = NULL;
            }
            else {
                printf("%s: unrecognized data\n", cstr_String(path_File(f)));
                setCurrent_Root(NULL);
                return iFalse;
            }
        }
        iForEach(Array, i, currentTabs) {
            const iCurrentTabs *cur = i.value;
            win = at_PtrArray(&d->mainWindows, index_ArrayIterator(&i));
            for (size_t j = 0; j < 2; ++j) {
                postCommandf_Root(win->base.roots[j], "tabs.switch page:%p", cur->currentTab[j]);
            }
            if (win->splitMode) {
                /* Update root placement. */
                resize_MainWindow(win, -1, -1);
            }
//            postCommand_Root(win->base.roots[0], "window.unfreeze");
            win->isDrawFrozen = iFalse;
            SDL_ShowWindow(win->base.win);
        }
        if (numWindows_App() > 1) {
            SDL_RaiseWindow(currentWin->base.win);
            setActiveWindow_App(currentWin);
        }
        setCurrent_Root(NULL);
        return iTrue;
    }
    return iFalse;
}

static void saveState_App_(const iApp *d) {
    if (isAppleDesktop_Platform() && isEmpty_PtrArray(&d->mainWindows)) {
        return; /* nothing to save; keep what was saved earlier */
    }
    trimCache_App();
    /* UI state is saved in binary because it is quite complex (e.g.,
       navigation history, cached content) and depends closely on the widget
       tree. The data is largely not reorderable and should not be modified
       by the user manually. */
    iFile *f = newCStr_File(concatPath_CStr(dataDir_App_(), tempStateFileName_App_));
    if (open_File(f, writeOnly_FileMode)) {
        writeData_File(f, magicState_App_, 4);
        writeU32_File(f, latest_FileVersion); /* version */
        iConstForEach(PtrArray, winIter, &d->mainWindows) {
            const iMainWindow *win = winIter.ptr;
            setCurrent_Window(winIter.ptr);
            /* Window state. */ {
                writeData_File(f, magicWindow_App_, 4);
                writeU32_File(f, win->splitMode);
                writeU32_File(f, (win->base.keyRoot == win->base.roots[0] ? 0 : 1) |
                                 (win == d->window ? current_WindowStateFlag : 0));
            }
            /* State of UI elements. */ {
                iForIndices(i, win->base.roots) {
                    const iRoot *root = win->base.roots[i];
                    if (root) {
                        writeData_File(f, magicSidebar_App_, 4);
                        const iSidebarWidget *sidebar  = findChild_Widget(root->widget, "sidebar");
                        const iSidebarWidget *sidebar2 = findChild_Widget(root->widget, "sidebar2");
                        writeU16_File(f, i |
                                      (isVisible_Widget(sidebar)  ? 0x100 : 0) |
                                      (isVisible_Widget(sidebar2) ? 0x200 : 0) |
                                      (feedsMode_SidebarWidget(sidebar)  == unread_FeedsMode ? 0x400 : 0) |
                                      (feedsMode_SidebarWidget(sidebar2) == unread_FeedsMode ? 0x800 : 0));
                        writeU8_File(f,
                                     mode_SidebarWidget(sidebar) |
                                     (mode_SidebarWidget(sidebar2) << 4));
                        writef_Stream(stream_File(f), width_SidebarWidget(sidebar));
                        writef_Stream(stream_File(f), width_SidebarWidget(sidebar2));
                        serialize_IntSet(closedFolders_SidebarWidget(sidebar), stream_File(f));
                        serialize_IntSet(closedFolders_SidebarWidget(sidebar2), stream_File(f));
                    }
                }
            }
            iConstForEach(ObjectList, i, iClob(listDocuments_App(NULL))) {
                iAssert(isInstance_Object(i.object, &Class_DocumentWidget));
                const iWidget *widget = constAs_Widget(i.object);
                writeData_File(f, magicTabDocument_App_, 4);
                int8_t flags = (document_Root(widget->root) == i.object ? current_DocumentStateFlag : 0);
                if (widget->root == win->base.roots[1]) {
                    flags |= rootIndex1_DocumentStateFlag;
                }
                write8_File(f, flags);
                serializeState_DocumentWidget(i.object, stream_File(f));
            }
        }
        iRelease(f);
    }
    else {
        iRelease(f);
        fprintf(stderr, "[App] failed to save state: %s\n", strerror(errno));
        return;
    }
    /* Copy it over to the real file. This avoids truncation if the app for any reason crashes
       before the state file is fully written. */
    const char *tempName = concatPath_CStr(dataDir_App_(), tempStateFileName_App_);
    const char *finalName = concatPath_CStr(dataDir_App_(), stateFileName_App_);
    remove(finalName);
    rename(tempName, finalName);
}

#if defined (LAGRANGE_ENABLE_IDLE_SLEEP)
static uint32_t checkAsleep_App_(uint32_t interval, void *param) {
    iApp *d = param;
    iUnused(d);
    SDL_Event ev = { .type = SDL_USEREVENT };
    ev.user.code = asleep_UserEventCode;
    SDL_PushEvent(&ev);
    return interval;
}
#endif

static uint32_t postAutoReloadCommand_App_(uint32_t interval, void *param) {
    iUnused(param);
    postCommand_Root(NULL, "document.autoreload");
    return interval;
}

static void terminate_App_(int rc) {
    SDL_Quit();
    deinit_Foundation();
    exit(rc);
}

#if defined (LAGRANGE_ENABLE_IPC)
static void communicateWithRunningInstance_App_(iApp *d, iProcessId instance,
                                                const iStringList *openCmds) {
    iString *cmds = new_String();
    iBool requestRaise = iFalse;
    const iProcessId pid = currentId_Process();
    iConstForEach(CommandLine, i, &d->args) {
        if (i.argType == value_CommandLineArgType) {
            continue;
        }
        if (equal_CommandLineConstIterator(&i, "go-home")) {
            appendCStr_String(cmds, "navigate.home\n");
            requestRaise = iTrue;
        }
        else if (equal_CommandLineConstIterator(&i, "new-tab")) {
            iCommandLineArg *arg = argument_CommandLineConstIterator(&i);
            if (!isEmpty_StringList(&arg->values)) {
                appendFormat_String(cmds, "open newtab:1 url:%s\n",
                                    cstr_String(constAt_StringList(&arg->values, 0)));
            }
            else {
                appendCStr_String(cmds, "tabs.new\n");
            }
            iRelease(arg);
            requestRaise = iTrue;
        }
        else if (equal_CommandLineConstIterator(&i, "close-tab")) {
            appendCStr_String(cmds, "tabs.close\n");
        }
        else if (equal_CommandLineConstIterator(&i, "tab-url")) {
            appendFormat_String(cmds, "ipc.active.url pid:%d\n", pid);
        }
        else if (equal_CommandLineConstIterator(&i, listTabUrls_CommandLineOption)) {
            appendFormat_String(cmds, "ipc.list.urls pid:%d\n", pid);
        }
    }
    if (!isEmpty_StringList(openCmds)) {
        append_String(cmds, collect_String(joinCStr_StringList(openCmds, "\n")));
        requestRaise = iTrue;
    }
    if (isEmpty_String(cmds)) {
        /* By default open a new tab. */
        appendCStr_String(cmds, "tabs.new\n");
        requestRaise = iTrue;
    }
    iBool gotResult = iFalse;
    if (!isEmpty_String(cmds)) {
        iString *result = communicate_Ipc(cmds, requestRaise);
        if (result) {
            fwrite(cstr_String(result), 1, size_String(result), stdout);
            fflush(stdout);
            if (!isEmpty_String(result)) {
                gotResult = iTrue;
            }
        }
        delete_String(result);
    }
    iUnused(instance);
    if (!gotResult) {
        printf("Commands sent to Lagrange process %d\n", instance);
    }
    terminate_App_(0);
}
#endif /* defined (LAGRANGE_ENABLE_IPC) */

static iBool hasCommandLineOpenableScheme_(const iRangecc uri) {
    static const char *schemes[] = {
        "gemini:", "gopher:", "finger:", "spartan:", "file:", "data:", "about:"
    };
    iForIndices(i, schemes) {
        if (startsWithCase_Rangecc(uri, schemes[i])) {
            return iTrue;
        }
    }
    return iFalse;
}

static void init_App_(iApp *d, int argc, char **argv) {
#if defined (iPlatformLinux) && !defined (iPlatformAndroid) && !defined (iPlatformTerminal)
    d->isRunningUnderWindowSystem = !iCmpStr(SDL_GetCurrentVideoDriver(), "x11") ||
                                    !iCmpStr(SDL_GetCurrentVideoDriver(), "wayland");
#else
    d->isRunningUnderWindowSystem = iTrue;
#endif
    d->isDarkSystemTheme = iTrue; /* will be updated by system later on, if supported */
    d->isSuspended = iFalse;
    d->tempFilesPendingDeletion = new_StringSet();
    d->recentlyClosedTabUrls = new_StringList();
    init_Array(&d->initialWindowRects, sizeof(iRect));    
    init_CommandLine(&d->args, argc, argv);
    /* Where was the app started from? We ask SDL first because the command line alone
       cannot be relied on (behavior differs depending on OS). */ {
        char *exec = SDL_GetBasePath();
        if (exec) {
            d->execPath = newCStr_String(concatPath_CStr(
                exec, cstr_Rangecc(baseName_Path(executablePath_CommandLine(&d->args)))));
        }
        else {
            d->execPath = copy_String(executablePath_CommandLine(&d->args));
        }
        SDL_free(exec);
    }
    /* Load the resources from a file. Check the executable directory first, then a
       system-wide location, and as a final fallback, the current working directory. */ {
        const char *execPath = cstr_String(execPath_App());
        const char *paths[] = {
            concatPath_CStr(execPath, EMB_BIN_EXEC), /* first the executable's directory */
#if defined (LAGRANGE_EMB_BIN) /* specified in build config (absolute path) */
            LAGRANGE_EMB_BIN,
#endif
#if defined (EMB_BIN2) /* alternative location */
            concatPath_CStr(execPath, EMB_BIN2),
#endif
            concatPath_CStr(execPath, EMB_BIN),
            "resources.lgr" /* cwd */
        };
        iBool wasLoaded = iFalse;
        iForIndices(i, paths) {
            if (init_Resources(paths[i])) {
                wasLoaded = iTrue;
                break;
            }
        }
        if (!wasLoaded) {
            fprintf(stderr, "failed to load resources: %s\n", strerror(errno));
            exit(-1);
        }
    }
    init_Lang();
    iStringList *openCmds = new_StringList();
#if !defined (iPlatformAndroidMobile)
    /* Configure the valid command line options. */ {
        defineValues_CommandLine(&d->args, "close-tab", 0);
        defineValues_CommandLine(&d->args, "echo;E", 0);
        defineValues_CommandLine(&d->args, "go-home", 0);
        defineValues_CommandLine(&d->args, "help", 0);
        defineValues_CommandLine(&d->args, listTabUrls_CommandLineOption, 0);
        defineValues_CommandLine(&d->args, openUrlOrSearch_CommandLineOption, 1);
        defineValues_CommandLine(&d->args, windowWidth_CommandLineOption, 1);
        defineValues_CommandLine(&d->args, windowHeight_CommandLineOption, 1);
        defineValuesN_CommandLine(&d->args, "new-tab", 0, 1);
        defineValues_CommandLine(&d->args, "tab-url", 0);
        defineValues_CommandLine(&d->args, "sw", 0);
        defineValues_CommandLine(&d->args, "version;V", 0);
    }
    /* Handle command line options. */ {
        if (contains_CommandLine(&d->args, "help")) {
            puts(cstr_Block(&blobArghelp_Resources));
            terminate_App_(0);
        }
        if (contains_CommandLine(&d->args, "version;V")) {
            printf("Lagrange version " LAGRANGE_APP_VERSION "\n");
            terminate_App_(0);
        }
        /* Check for URLs. */
        iConstForEach(CommandLine, i, &d->args) {
            const iRangecc arg = i.entry;
            if (i.argType == value_CommandLineArgType) {
                /* URLs and file paths accepted. */
                const iBool isOpenable = hasCommandLineOpenableScheme_(arg);
                if (isOpenable || fileExistsCStr_FileInfo(cstr_Rangecc(arg))) {
                    iString *decUrl =
                        isOpenable ? urlDecodeExclude_String(collectNewRange_String(arg), "/?#:")
                                   : makeFileUrl_String(collectNewRange_String(arg));
                    pushBack_StringList(openCmds,
                                        collectNewFormat_String(
                                            "open newtab:1 url:%s", cstr_String(decUrl)));
                    delete_String(decUrl);
                }
                else {
                    fprintf(stderr, "Invalid URL/file: %s\n", cstr_Rangecc(arg));
                    terminate_App_(1);
                }
            }
            else if (equal_CommandLineConstIterator(&i, openUrlOrSearch_CommandLineOption)) {
                const iCommandLineArg *arg = iClob(argument_CommandLineConstIterator(&i));
                const iString *input = value_CommandLineArg(arg, 0);
                if (startsWith_String(input, "//")) {
                    input = collectNewFormat_String("gemini:%s", cstr_String(input));
                }
                if (hasCommandLineOpenableScheme_(range_String(input))) {
                    input = collect_String(urlDecodeExclude_String(input, "/?#:"));
                }
                pushBack_StringList(
                    openCmds,
                    collectNewFormat_String("search newtab:1 query:%s", cstr_String(input)));
            }
            else if (!isDefined_CommandLine(&d->args, collectNewRange_String(i.entry))) {
                fprintf(stderr, "Unknown option: %s\n", cstr_Rangecc(arg));
                terminate_App_(1);
            }
        }
    }
#endif
#if defined (LAGRANGE_ENABLE_IPC)
    /* Only one instance is allowed to run at a time; the runtime files (bookmarks, etc.)
       are not shareable. */ {
        init_Ipc(dataDir_App_());
        const iProcessId instance = check_Ipc();
        if (instance) {
            communicateWithRunningInstance_App_(d, instance, openCmds);
            terminate_App_(0);
        }
        /* Some options are intended only for controlling other instances. */
        if (contains_CommandLine(&d->args, listTabUrls_CommandLineOption)) {
            terminate_App_(0);
        }
        listen_Ipc(); /* We'll respond to commands from other instances. */
    }
#endif
    puts("Lagrange: A Beautiful Gemini Client");
    const iBool isFirstRun =
        !fileExistsCStr_FileInfo(cleanedPath_CStr(concatPath_CStr(dataDir_App_(), "prefs.cfg")));
    d->isFinishedLaunching = iFalse;
    d->isLoadingPrefs      = iFalse;
    d->warmupFrames        = 0;
    d->launchCommands      = new_StringList();
    iZap(d->lastDropTime);
    init_SortedArray(&d->tickers, sizeof(iTicker), cmp_Ticker_);
    d->lastTickerTime         = SDL_GetTicks();
    d->elapsedSinceLastTicker = 0;
    d->commandEcho            = iClob(checkArgument_CommandLine(&d->args, "echo;E")) != NULL;
    d->forceSoftwareRender    = iClob(checkArgument_CommandLine(&d->args, "sw")) != NULL;
    init_Prefs(&d->prefs);
    init_SiteSpec(dataDir_App_());
    setCStr_String(&d->prefs.strings[downloadDir_PrefsString], downloadDir_App_());
    set_Atomic(&d->pendingRefresh, iFalse);
    d->isRunning = iFalse;
    d->window    = NULL;
    d->mimehooks = new_MimeHooks();
    d->certs     = new_GmCerts(dataDir_App_());
    d->visited   = new_Visited();
    d->bookmarks = new_Bookmarks();
    init_Periodic(&d->periodic);
#if defined (iPlatformAppleDesktop)
    setupApplication_MacOS();
#endif
#if defined (iPlatformAppleMobile)
    setupApplication_iOS();
#endif
#if defined (iPlatformAndroidMobile)
    setupApplication_Android();
#endif
    init_Keys();
    init_Fonts(dataDir_App_());
    loadPalette_Color(dataDir_App_());
    setThemePalette_Color(d->prefs.theme); /* default UI colors */
    /* Initial window rectangle of the first window. */ {
        iAssert(isEmpty_Array(&d->initialWindowRects));
        const iRect winRect = initialWindowRect_App_(d, 0); /* calculated */
        resize_Array(&d->initialWindowRects, 1);
        set_Array(&d->initialWindowRects, 0, &winRect);
    }
    loadPrefs_App_(d); 
    updateActive_Fonts();
    load_Keys(dataDir_App_());
    iRect *winRect0 = at_Array(&d->initialWindowRects, 0);
    /* See if the user wants to override the window size. */ {
        iCommandLineArg *arg = iClob(checkArgument_CommandLine(&d->args, windowWidth_CommandLineOption));
        if (arg) {
            winRect0->size.x = toInt_String(value_CommandLineArg(arg, 0));
        }
        arg = iClob(checkArgument_CommandLine(&d->args, windowHeight_CommandLineOption));
        if (arg) {
            winRect0->size.y = toInt_String(value_CommandLineArg(arg, 0));
        }
    }
    init_PtrArray(&d->mainWindows);
    init_PtrArray(&d->popupWindows);
    d->window = new_MainWindow(*winRect0); /* first window is always created */
    addWindow_App(d->window);
    load_Visited(d->visited, dataDir_App_());
    load_Bookmarks(d->bookmarks, dataDir_App_());
    load_MimeHooks(d->mimehooks, dataDir_App_());
    if (isFirstRun) {
        /* Create the default bookmarks for a quick start. */
        add_Bookmarks(d->bookmarks,
                      collectNewCStr_String("gemini://skyjake.fi/lagrange/"),
                      collectNewCStr_String("Lagrange"),
                      NULL,
                      0x1f306);
        add_Bookmarks(d->bookmarks,
                      collectNewCStr_String("gemini://skyjake.fi/lagrange/getting_started.gmi"),
                      collectNewCStr_String("Getting Started"),
                      NULL,
                      0x1f306);
    }
    init_Feeds(dataDir_App_());
    /* Widget state init. */
    processEvents_App(postedEventsOnly_AppEventMode);
    if (!loadState_App_(d)) {
        postCommand_Root(NULL, "open url:about:help");
    }
    else if (!d->prefs.retainTabs) {
        /* All roots will just show home. */
        iForEach(PtrArray, w, &d->mainWindows) {
            const iWindow *win = w.ptr;
            iForIndices(ri, win->roots) {
                if (win->roots[ri]) {
                    postCommand_Root(win->roots[ri], "navigate.home");
                }
            }
        }
    }
    postCommand_App("~navbar.actions.changed");
    postCommand_App("~toolbar.actions.changed");
    postCommand_App("~root.movable");
    postCommand_App("~window.unfreeze");
    postCommand_App("~focus.set id:"); /* clear focus */
    postCommand_App("font.reset");
    d->autoReloadTimer = SDL_AddTimer(60 * 1000, postAutoReloadCommand_App_, NULL);
    postCommand_Root(NULL, "document.autoreload");
#if defined (LAGRANGE_ENABLE_IDLE_SLEEP)
    /* Initialize idle sleep. */ {
        d->isIdling      = iFalse;
        d->lastEventTime = 0;
        d->sleepTimer    = SDL_AddTimer(1000, checkAsleep_App_, d);
        SDL_DisplayMode dispMode;
        SDL_GetWindowDisplayMode(d->window->base.win, &dispMode);
        if (dispMode.refresh_rate) {
            d->idleSleepDelayMs = 1000 / dispMode.refresh_rate;
        }
        else {
            d->idleSleepDelayMs = 1000 / 60;
        }
        d->idleSleepDelayMs *= 0.9f;
    }
#endif
    d->isFinishedLaunching = iTrue;
    /* Run any commands that were pending completion of launch. */ {
        iForEach(StringList, i, d->launchCommands) {
            postCommandString_Root(NULL, i.value);
        }
    }
    /* URLs from the command line. */ {
        iConstForEach(StringList, i, openCmds) {
            postCommandString_Root(NULL, i.value);
        }
        iRelease(openCmds);
    }
    fetchRemote_Bookmarks(d->bookmarks);
    if (deviceType_App() != desktop_AppDeviceType) {
        /* HACK: Force a resize so widgets update their state. */
        resize_MainWindow(d->window, -1, -1);
    }
}

static void deinit_App(iApp *d) {
    iReverseForEach(PtrArray, i, &d->popupWindows) {
        delete_Window(i.ptr);
    }
    iAssert(isEmpty_PtrArray(&d->popupWindows));
    deinit_PtrArray(&d->popupWindows);
#if defined (LAGRANGE_ENABLE_IDLE_SLEEP)
    SDL_RemoveTimer(d->sleepTimer);
#endif
    SDL_RemoveTimer(d->autoReloadTimer);
    saveState_App_(d);
    savePrefs_App_(d);
    iReverseForEach(PtrArray, j, &d->mainWindows) {
        delete_MainWindow(j.ptr);
    }
    iAssert(isEmpty_PtrArray(&d->mainWindows));
    deinit_PtrArray(&d->mainWindows);
    d->window = NULL;
    deinit_Feeds();
    save_Keys(dataDir_App_());
    deinit_Keys();
    deinit_Fonts();
    deinit_SiteSpec();
    deinit_Prefs(&d->prefs);
    save_Bookmarks(d->bookmarks, dataDir_App_());
    delete_Bookmarks(d->bookmarks);
    save_Visited(d->visited, dataDir_App_());
    delete_Visited(d->visited);
    delete_GmCerts(d->certs);
    save_MimeHooks(d->mimehooks);
    delete_MimeHooks(d->mimehooks);
    deinit_CommandLine(&d->args);
    iRelease(d->launchCommands);
    delete_String(d->execPath);
#if defined (LAGRANGE_ENABLE_IPC)
    deinit_Ipc();
#endif
    deinit_SortedArray(&d->tickers);
    deinit_Periodic(&d->periodic);
    deinit_Lang();
    iRecycle();
    /* Delete all temporary files created while running. */
    iConstForEach(StringSet, tmp, d->tempFilesPendingDeletion) {
        remove(cstr_String(tmp.value));
    }
    deinit_Array(&d->initialWindowRects);
    iRelease(d->recentlyClosedTabUrls);
    iRelease(d->tempFilesPendingDeletion);
}

const iString *execPath_App(void) {
    return app_.execPath;
}

const iString *dataDir_App(void) {
    return collect_String(cleanedCStr_Path(dataDir_App_()));
}

const iString *downloadDir_App(void) {
    return collect_String(cleaned_Path(&app_.prefs.strings[downloadDir_PrefsString]));
}

const iString *fileNameForUrl_App(const iString *url, const iString *mime) {
    /* Figure out a file name from the URL. */
    iUrl parts;
    init_Url(&parts, url);
    iString *urlPath =
        collect_String(urlDecodeExclude_String(collectNewRange_String(parts.path), "\\/:;"));
    iRangecc path = range_String(urlPath);
    while (startsWith_Rangecc(path, "/")) {
        path.start++;
    }
    while (endsWith_Rangecc(path, "/")) {
        path.end--;
    }
    iString *name = collectNewCStr_String("pagecontent");
    if (isEmpty_Range(&path)) {
        if (!isEmpty_Range(&parts.host)) {
            setRange_String(name, parts.host);
            replace_Block(&name->chars, '.', '_');
        }
    }
    else {
        const size_t slashPos = lastIndexOfCStr_Rangecc(path, "/");
        iRangecc fn = { path.start + (slashPos != iInvalidPos ? slashPos + 1 : 0),
                        path.end };
        if (!isEmpty_Range(&fn)) {
            setRange_String(name, fn);
        }
    }
    if (startsWith_String(name, "~")) {
        /* This might be interpreted as a reference to a home directory. */
        remove_Block(&name->chars, 0, 1);
    }
    if (lastIndexOfCStr_String(name, ".") == iInvalidPos) {
        /* TODO: Needs the inverse of `mediaTypeFromFileExtension_String()`. */
        /* No extension specified in URL. */
        if (startsWith_String(mime, "text/gemini")) {
            appendCStr_String(name, ".gmi");
        }
        else if (startsWith_String(mime, "text/")) {
            appendCStr_String(name, ".txt");
        }
        else if (startsWith_String(mime, "image/")) {
            appendCStr_String(name, cstr_String(mime) + 6);
        }
    }
    return name;
}

const iString *downloadPathForUrl_App(const iString *url, const iString *mime) {
    iString *savePath = concat_Path(downloadDir_App(), fileNameForUrl_App(url, mime));
    if (fileExists_FileInfo(savePath)) {
        /* Make it unique. */
        iDate now;
        initCurrent_Date(&now);
        size_t insPos = lastIndexOfCStr_String(savePath, ".");
        if (insPos == iInvalidPos) {
            insPos = size_String(savePath);
        }
        const iString *date = collect_String(format_Date(&now, "_%Y-%m-%d_%H%M%S"));
        insertData_Block(&savePath->chars, insPos, cstr_String(date), size_String(date));
    }
    return collect_String(savePath);
}

const iString *temporaryPathForUrl_App(const iString *url, const iString *mime) {
    iApp *d = &app_;
#if defined (iPlatformMsys)
    iString *      tmpPath = collectNew_String();
    const iRangecc tmpDir  = range_String(collect_String(tempDirectory_Win32()));
#elif defined (P_tmpdir)
    iString *      tmpPath = collectNew_String();
    const iRangecc tmpDir  = range_CStr(P_tmpdir);
#else
    iString *      tmpPath = collectNewCStr_String(tmpnam(NULL));
    const iRangecc tmpDir  = dirName_Path(tmpPath);
#endif
    set_String(
        tmpPath,
        collect_String(concat_Path(collectNewRange_String(tmpDir), fileNameForUrl_App(url, mime))));
    insert_StringSet(d->tempFilesPendingDeletion, tmpPath); /* deleted in `deinit_App` */
    return tmpPath;
}

const iString *debugInfo_App(void) {
    extern char **environ; /* The environment variables. */
    iApp *d = &app_;
    iString *msg = collectNew_String();
    iObjectList *docs = iClob(listDocuments_App(NULL));
    format_String(msg, "# Debug information\n");
    appendFormat_String(msg, "## Memory usage\n"); {
        iMemInfo total = { 0, 0 };
        iForEach(ObjectList, i, docs) {
            iDocumentWidget *doc = i.object;
            iMemInfo usage = memoryUsage_History(history_DocumentWidget(doc));
            total.cacheSize += usage.cacheSize;
            total.memorySize += usage.memorySize;
        }
        appendFormat_String(msg, "Total cache: %.3f MB\n", total.cacheSize / 1.0e6f);
        appendFormat_String(msg, "Total memory: %.3f MB\n", total.memorySize / 1.0e6f);
    }
    appendFormat_String(msg, "## Documents\n");
    iForEach(ObjectList, k, docs) {
        iDocumentWidget *doc = k.object;
        appendFormat_String(msg, "### Tab %d.%zu: %s\n",
                            constAs_Widget(doc)->root == get_Window()->roots[0] ? 1 : 2,
                            indexOfChild_Widget(constAs_Widget(doc)->parent, k.object) + 1,
                            cstr_String(bookmarkTitle_DocumentWidget(doc)));
        append_String(msg, collect_String(debugInfo_History(history_DocumentWidget(doc))));
    }
    appendCStr_String(msg, "## Environment\n```\n");
    for (char **env = environ; *env; env++) {
        appendFormat_String(msg, "%s\n", *env);
    }
    appendCStr_String(msg, "```\n");
    appendFormat_String(msg, "## Launch arguments\n```\n");
    iConstForEach(StringList, i, args_CommandLine(&d->args)) {
        appendFormat_String(msg, "%3zu : %s\n", i.pos, cstr_String(i.value));
    }
    appendFormat_String(msg, "```\n## Launch commands\n");
    iConstForEach(StringList, j, d->launchCommands) {
        appendFormat_String(msg, "%s\n", cstr_String(j.value));
    }
    appendFormat_String(msg, "## MIME hooks\n");
    append_String(msg, debugInfo_MimeHooks(d->mimehooks));
    return msg;
}

static void clearCache_App_(void) {
    iForEach(ObjectList, i, iClob(listDocuments_App(NULL))) {
        clearCache_History(history_DocumentWidget(i.object));
    }
}

iObjectList *listAllDocuments_App(void) {
    iWindow     *oldWindow = get_Window();
    iRoot       *oldRoot   = current_Root();
    iObjectList *allDocs   = new_ObjectList();
    iConstForEach(PtrArray, window, mainWindows_App()) {
        setCurrent_Window(window.ptr);
        iObjectList *docs = listDocuments_App(NULL);
        iForEach(ObjectList, i, docs) {
            pushBack_ObjectList(allDocs, i.object);
        }
        iRelease(docs);
    }
    setCurrent_Window(oldWindow);
    setCurrent_Root(oldRoot);
    return allDocs;
}

void trimCache_App(void) {
    iApp *d = &app_;
    size_t cacheSize = 0;
    const size_t limit = d->prefs.maxCacheSize * 1000000;
    iObjectList *docs = listAllDocuments_App();
    iForEach(ObjectList, i, docs) {
        cacheSize += cacheSize_History(history_DocumentWidget(i.object));
    }
    init_ObjectListIterator(&i, docs);
    iBool wasPruned = iFalse;
    while (cacheSize > limit) {
        iDocumentWidget *doc = i.object;
        const size_t pruned = pruneLeastImportant_History(history_DocumentWidget(doc));
        if (pruned) {
            cacheSize -= pruned;
            wasPruned = iTrue;
        }
        next_ObjectListIterator(&i);
        if (!i.value) {
            if (!wasPruned) break;
            wasPruned = iFalse;
            init_ObjectListIterator(&i, docs);
        }
    }
    iRelease(docs);
}

void trimMemory_App(void) {
    iApp *d = &app_;
    size_t memorySize = 0;
    const size_t limit = d->prefs.maxMemorySize * 1000000;
    iObjectList *docs = listAllDocuments_App();
    iForEach(ObjectList, i, docs) {
        memorySize += memorySize_History(history_DocumentWidget(i.object));
    }
    init_ObjectListIterator(&i, docs);
    iBool wasPruned = iFalse;
    while (memorySize > limit) {
        iDocumentWidget *doc = i.object;
        const size_t pruned = pruneLeastImportantMemory_History(history_DocumentWidget(doc));
        if (pruned) {
            memorySize -= pruned;
            wasPruned = iTrue;
        }
        next_ObjectListIterator(&i);
        if (!i.value) {
            if (!wasPruned) break;
            wasPruned = iFalse;
            init_ObjectListIterator(&i, docs);
        }
    }
    iRelease(docs);
}

iLocalDef iBool isWaitingAllowed_App_(iApp *d) {
    if (d->warmupFrames > 0) {
        return iFalse;
    }
#if defined (LAGRANGE_ENABLE_IDLE_SLEEP)
    if (d->isIdling) {
        return iFalse;
    }
#endif
    return !isRefreshPending_App();
}

static iBool nextEvent_App_(iApp *d, enum iAppEventMode eventMode, SDL_Event *event) {
    if (eventMode == waitForNewEvents_AppEventMode && isWaitingAllowed_App_(d)) {
        /* We may be allowed to block here until an event comes in. */
        if (isWaitingAllowed_App_(d)) {
            if (isAppleDesktop_Platform() && d->window && d->window->enableBackBuf) {
                /* SDL Metal workaround: if we block here for too long, there will be a longer
                   ~100ms stutter later on after refresh resumes, when the render pipeline does
                   some kind of an update (?). */
                return SDL_WaitEventTimeout(event, 250);
            }
            /* Wait indefinitely. */
            return SDL_WaitEvent(event);
        }
    }
    /* SDL regression circa 2.0.18? SDL_PollEvent() doesn't always return 
       events posted immediately beforehand. Waiting with a very short timeout
       seems to work better. */
#if defined (iPlatformLinux) && SDL_VERSION_ATLEAST(2, 0, 18)
    return SDL_WaitEventTimeout(event, 1);
#else
    return SDL_PollEvent(event);
#endif
}

static iPtrArray *listWindows_App_(const iApp *d, iPtrArray *windows) {
    clear_PtrArray(windows);
    iReverseConstForEach(PtrArray, i, &d->popupWindows) {
        pushBack_PtrArray(windows, i.ptr);
    }
    if (d->window) {
        pushBack_PtrArray(windows, d->window);
    }
    iConstForEach(PtrArray, j, &d->mainWindows) {
        if (j.ptr != d->window) {
            pushBack_PtrArray(windows, j.ptr);
        }
    }
    return windows;
}

iPtrArray *listWindows_App(void) {
    iPtrArray *wins = new_PtrArray();
    listWindows_App_(&app_, wins);
    return wins;
}

void processEvents_App(enum iAppEventMode eventMode) {
    iApp *d = &app_;
    iRoot *oldCurrentRoot = current_Root(); /* restored afterwards */
    SDL_Event ev;
    iBool gotEvents = iFalse;
    iBool gotRefresh = iFalse;
    iPtrArray windows;
    init_PtrArray(&windows);
    while (nextEvent_App_(d, gotRefresh ? postedEventsOnly_AppEventMode : eventMode, &ev)) {
#if defined (iPlatformAppleMobile)
        if (processEvent_iOS(&ev)) {
            continue;
        }
#endif
        switch (ev.type) {
            case SDL_QUIT:
                d->isRunning = iFalse;
                if (findWidget_App("prefs")) {
                    /* Make sure changed preferences get saved. */
                    postCommand_Root(NULL, "prefs.dismiss");
                    processEvents_App(postedEventsOnly_AppEventMode);
                }
                goto backToMainLoop;
            case SDL_APP_LOWMEMORY:
                clearCache_App_();
                break;
            case SDL_APP_WILLENTERFOREGROUND:
                invalidate_Window(as_Window(d->window));
                d->isSuspended = iFalse;
                break;
            case SDL_APP_DIDENTERFOREGROUND:
                d->warmupFrames = 5;
#if defined (LAGRANGE_ENABLE_IDLE_SLEEP)
                gotEvents = iTrue;
                d->isIdling = iFalse;
                d->lastEventTime = SDL_GetTicks();
#endif
                postRefresh_App();
                break;
            case SDL_APP_WILLENTERBACKGROUND:
#if defined (iPlatformAppleMobile)
                updateNowPlayingInfo_iOS();
#endif
                setFreezeDraw_MainWindow(d->window, iTrue);
                savePrefs_App_(d);
                saveState_App_(d);
                d->isSuspended = iTrue;
                break;
            case SDL_APP_TERMINATING:
                setFreezeDraw_MainWindow(d->window, iTrue);
                savePrefs_App_(d);
                saveState_App_(d);
                break;
            case SDL_DROPFILE: {
                iBool wasUsed = processEvent_Window(as_Window(d->window), &ev);
                if (!wasUsed) {
                    iBool newTab = iFalse;
                    if (elapsedSeconds_Time(&d->lastDropTime) < 0.1) {
                        /* Each additional drop gets a new tab. */
                        newTab = iTrue;
                    }
                    d->lastDropTime = now_Time();
                    if (startsWithCase_CStr(ev.drop.file, "gemini:") ||
                        startsWithCase_CStr(ev.drop.file, "gopher:") ||
                        startsWithCase_CStr(ev.drop.file, "spartan:") ||
                        startsWithCase_CStr(ev.drop.file, "file:")) {
                        postCommandf_Root(NULL, "~open newtab:%d url:%s", newTab, ev.drop.file);
                    }
                    else {
                        postCommandf_Root(NULL,
                            "~open newtab:%d url:%s", newTab, makeFileUrl_CStr(ev.drop.file));
                    }
                }
                break;
            }
            default: {
                if (ev.type == SDL_USEREVENT && ev.user.code == periodic_UserEventCode) {
                    dispatchCommands_Periodic(&d->periodic);
                    continue;
                }
                if (ev.type == SDL_USEREVENT && ev.user.code == releaseObject_UserEventCode) {
                    iRelease(ev.user.data1);
                    continue;
                }
                if (ev.type == SDL_USEREVENT && ev.user.code == refresh_UserEventCode) {
                    gotRefresh = iTrue;
                    continue;
                }
#if defined (LAGRANGE_ENABLE_IDLE_SLEEP)
                if (ev.type == SDL_USEREVENT && ev.user.code == asleep_UserEventCode) {
                    if (SDL_GetTicks() - d->lastEventTime > idleThreshold_App_ &&
                        isEmpty_SortedArray(&d->tickers)) {
                        if (!d->isIdling) {
//                            printf("[App] idling...\n");
//                            fflush(stdout);
                        }
                        d->isIdling = iTrue;
                    }
                    continue;
                }
                d->lastEventTime = SDL_GetTicks();
                if (d->isIdling) {
//                    printf("[App] ...woke up\n");
//                    fflush(stdout);
                }
                d->isIdling = iFalse;
                gotEvents = iTrue;
#endif
                /* Keyboard modifier mapping. */
                if (ev.type == SDL_KEYDOWN || ev.type == SDL_KEYUP) {
                    /* Track Caps Lock state as a modifier. */
                    if (ev.key.keysym.sym == SDLK_CAPSLOCK) {
                        setCapsLockDown_Keys(ev.key.state == SDL_PRESSED);
                    }
                    ev.key.keysym.mod = mapMods_Keys(ev.key.keysym.mod & ~KMOD_CAPS);
                }
#if defined (iPlatformAndroidMobile)
                /* Use the system Back button to close panels, if they're open. */
                if (ev.type == SDL_KEYDOWN && ev.key.keysym.sym == SDLK_AC_BACK) {
                    SDL_UserEvent panelBackCmd = { .type = SDL_USEREVENT,
                                                   .code = command_UserEventCode,
                                                   .data1 = iDupStr("panel.close"),
                                                   .data2 = d->window->base.keyRoot };
                    if (dispatchEvent_Window(&d->window->base, (SDL_Event *) &panelBackCmd)) {
                        continue; /* Was handled by someone. */
                    }
                }
                /* Ignore all mouse events; just use touch. */
                if (ev.type == SDL_MOUSEBUTTONDOWN ||
                    ev.type == SDL_MOUSEBUTTONUP ||
                    ev.type == SDL_MOUSEMOTION ||
                    ev.type == SDL_MOUSEWHEEL) {
                    continue;
                }
#endif
                /* Scroll events may be per-pixel or mouse wheel steps. */
                if (ev.type == SDL_MOUSEWHEEL) {
#if defined (iPlatformMsys)
                    ev.wheel.x = -ev.wheel.x;
#endif
                }
#if defined (LAGRANGE_ENABLE_MOUSE_TOUCH_EMULATION)
                /* Convert mouse events to finger events to test the touch handling. */ {
                    static float xPrev = 0.0f;
                    static float yPrev = 0.0f;
                    if (ev.type == SDL_MOUSEBUTTONDOWN || ev.type == SDL_MOUSEBUTTONUP) {
                        const float xf = (d->window->pixelRatio * ev.button.x) / (float) d->window->size.x;
                        const float yf = (d->window->pixelRatio * ev.button.y) / (float) d->window->size.y;
                        ev.type = (ev.type == SDL_MOUSEBUTTONDOWN ? SDL_FINGERDOWN : SDL_FINGERUP);
                        ev.tfinger.x = xf;
                        ev.tfinger.y = yf;
                        ev.tfinger.dx = xf - xPrev;
                        ev.tfinger.dy = yf - yPrev;
                        xPrev = xf;
                        yPrev = yf;
                        ev.tfinger.fingerId = 0x1234;
                        ev.tfinger.pressure = 1.0f;
                        ev.tfinger.timestamp = SDL_GetTicks();
                        ev.tfinger.touchId = SDL_TOUCH_MOUSEID;
                    }
                    else if (ev.type == SDL_MOUSEMOTION) {
                        if (~ev.motion.state & SDL_BUTTON(SDL_BUTTON_LEFT)) {
                            continue; /* only when pressing a button */
                        }
                        const float xf = (d->window->pixelRatio * ev.motion.x) / (float) d->window->size.x;
                        const float yf = (d->window->pixelRatio * ev.motion.y) / (float) d->window->size.y;
                        ev.type = SDL_FINGERMOTION;
                        ev.tfinger.x = xf;
                        ev.tfinger.y = yf;
                        ev.tfinger.dx = xf - xPrev;
                        ev.tfinger.dy = yf - yPrev;
                        xPrev = xf;
                        yPrev = yf;
                        ev.tfinger.fingerId = 0x1234;
                        ev.tfinger.pressure = 1.0f;
                        ev.tfinger.timestamp = SDL_GetTicks();
                        ev.tfinger.touchId = SDL_TOUCH_MOUSEID;
                    }
                }
#endif
                iBool wasUsed = iFalse;
                /* Focus navigation events take prioritity. */
                if (!wasUsed) {
                    /* Keyboard focus navigation with arrow keys. */
                    iWidget *menubar = NULL;
                    if (ev.type == SDL_KEYDOWN && ev.key.keysym.mod == 0 && focus_Widget() &&
                        parentMenu_Widget(focus_Widget())) {
                        setCurrent_Window(window_Widget(focus_Widget()));
                        const int key = ev.key.keysym.sym;
                        if (key == SDLK_DOWN || key == SDLK_UP) {
                            iWidget *nextFocus = findFocusable_Widget(focus_Widget(),
                                                                      key == SDLK_UP
                                                                          ? backward_WidgetFocusDir
                                                                          : forward_WidgetFocusDir);
                            if (nextFocus && parent_Widget(nextFocus) == parent_Widget(focus_Widget())) {
                                setFocus_Widget(nextFocus);
                            }
                            wasUsed = iTrue;
                        }
                        else if ((key == SDLK_LEFT || key == SDLK_RIGHT)) {
                            if ((menubar = findParent_Widget(focus_Widget(), "menubar")) != NULL) {
                                iWidget *button = parent_Widget(parent_Widget(focus_Widget()));
                                size_t index = indexOfChild_Widget(menubar, button);
                                const size_t curIndex = index;
                                if (key == SDLK_LEFT && index > 0) {
                                    index--;
                                }
                                else if (key == SDLK_RIGHT && index < childCount_Widget(menubar) - 1) {
                                    index++;
                                }
                                if (curIndex != index) {
                                    setFocus_Widget(child_Widget(menubar, index));                                
                                    postCommand_Widget(child_Widget(menubar, index), "trigger");
                                }
                            }
                            else {
                                postCommand_Widget(focus_Widget(), "cancel");
                            }
                            wasUsed = iTrue;
                        }
                    }
                }
                /* Per-window processing. */
                if (!wasUsed && !isEmpty_PtrArray(&d->mainWindows)) {
                    listWindows_App_(d, &windows);
                    iConstForEach(PtrArray, iter, &windows) {
                        iWindow *window = iter.ptr;
                        setCurrent_Window(window);
                        window->lastHover = window->hover;
                        wasUsed = processEvent_Window(window, &ev);
                        if (ev.type == SDL_MOUSEMOTION) {
                            /* Only offered to the frontmost window. */
                            break;
                        }
                        if (wasUsed) break;
                    }
                }
                setCurrent_Window(d->window);
                if (!wasUsed) {
                    /* There may be a key binding for this. */
                    wasUsed = processEvent_Keys(&ev);
                }
                if (!wasUsed) {
                    /* Focus cycling. */
                    if (ev.type == SDL_KEYDOWN && ev.key.keysym.sym == SDLK_TAB && current_Root()) {
                        iWidget *startFrom = focus_Widget();
                        const iBool isLimitedFocus = focusRoot_Widget(startFrom) != get_Root()->widget;
                        /* Go to a sidebar if one is visible. */
                        if (!startFrom && !isLimitedFocus && 
                            isVisible_Widget(findWidget_App("sidebar"))) {
                            setFocus_Widget(as_Widget(
                                list_SidebarWidget((iSidebarWidget *) findWidget_App("sidebar"))));
                        }
                        else if (!startFrom && !isLimitedFocus && 
                            isVisible_Widget(findWidget_App("sidebar2"))) {
                            setFocus_Widget(as_Widget(
                                list_SidebarWidget((iSidebarWidget *) findWidget_App("sidebar2"))));
                        }
                        else {
                            setFocus_Widget(findFocusable_Widget(startFrom,
                                                                 ev.key.keysym.mod & KMOD_SHIFT
                                                                     ? backward_WidgetFocusDir
                                                                     : forward_WidgetFocusDir));
                        }
                        wasUsed = iTrue;
                    }
                }
                if (!wasUsed) {
                    /* ^G is an alternative for Escape. */
                    if (ev.type == SDL_KEYDOWN && ev.key.keysym.sym == 'g' &&
                        keyMods_Sym(ev.key.keysym.mod) == KMOD_CTRL) {
                        SDL_KeyboardEvent esc = ev.key;
                        esc.keysym.sym = SDLK_ESCAPE;
                        esc.keysym.mod = 0;
                        SDL_PushEvent((SDL_Event *) &esc);
                        esc.state = SDL_RELEASED;
                        esc.type  = SDL_KEYUP;
                        esc.timestamp++;
                        SDL_PushEvent((SDL_Event *) &esc);
                        wasUsed = iTrue;
                    }
                }
                if (ev.type == SDL_USEREVENT && ev.user.code == command_UserEventCode) {
#if defined (iPlatformAppleDesktop)
                    handleCommand_MacOS(command_UserEvent(&ev));
#endif
#if defined (iPlatformAndroidMobile)
                    handleCommand_Android(command_UserEvent(&ev));
#endif
#if defined (iPlatformMsys)
                    handleCommand_Win32(command_UserEvent(&ev));
#endif
                    if (isMetricsChange_UserEvent(&ev)) {
                        listWindows_App_(d, &windows);
                        iConstForEach(PtrArray, iter, &windows) {
                            iWindow *window = iter.ptr;
                            iForIndices(i, window->roots) {
                                iRoot *root = window->roots[i];
                                if (root) {
                                    arrange_Widget(root->widget);
                                }
                            }
                        }
                    }
                    if (!wasUsed) {
                        /* No widget handled the command, so we'll do it. */
                        setCurrent_Window(d->window);
                        handleCommand_App(ev.user.data1);
                    }
                    /* Allocated by postCommand_Apps(). */
                    free(ev.user.data1);
                }
                /* Refresh after hover changes. */ {
                    listWindows_App_(d, &windows);
                    iConstForEach(PtrArray, iter, &windows) {
                        iWindow *window = iter.ptr;
                        if (window->lastHover != window->hover) {
                            refresh_Widget(window->lastHover);
                            refresh_Widget(window->hover);
                        }
                    }
                }
                break;
            }
        }
    }
    deinit_PtrArray(&windows);
#if defined (LAGRANGE_ENABLE_IDLE_SLEEP)
    if (d->isIdling && !gotEvents) {
        /* This is where we spend most of our time when idle. The sleep delay depends on the
           display refresh rate. iOS SDL_WaitEvent() seems to use 10x more CPU time compared to
           just sleeping (2.0.18). */
        SDL_Delay(d->idleSleepDelayMs);
    }
#endif
backToMainLoop:;
    setCurrent_Root(oldCurrentRoot);
}

static void runTickers_App_(iApp *d) {
    const uint32_t now = SDL_GetTicks();
    d->elapsedSinceLastTicker = (d->lastTickerTime ? now - d->lastTickerTime : 0);
    d->lastTickerTime = now;
    if (isEmpty_SortedArray(&d->tickers)) {
        d->lastTickerTime = 0;
        return;
    }
    iForIndices(i, d->window->base.roots) {
        iRoot *root = d->window->base.roots[i];
        if (root) {
            root->didAnimateVisualOffsets = iFalse;
        }
    }
    /* Tickers may add themselves again, so we'll run off a copy. */
    iSortedArray *pending = copy_SortedArray(&d->tickers);
    clear_SortedArray(&d->tickers);
    iConstForEach(Array, i, &pending->values) {
        const iTicker *ticker = i.value;
        if (ticker->callback) {
            if (ticker->root) {
                setCurrent_Window(ticker->root->window);
            }
            setCurrent_Root(ticker->root); /* root might be NULL */
            ticker->callback(ticker->context);
        }
    }
    setCurrent_Root(NULL);
    delete_SortedArray(pending);
    if (isEmpty_SortedArray(&d->tickers)) {
        d->lastTickerTime = 0;
    }
}

static int resizeWatcher_(void *user, SDL_Event *event) {
    iApp *d = user;
    if (event->type == SDL_WINDOWEVENT && event->window.event == SDL_WINDOWEVENT_SIZE_CHANGED) {
        const SDL_WindowEvent *winev = &event->window;
#if defined (iPlatformMsys)
        /* TODO: Investigate if this is still necessary. */
        resetFontCache_Text(text_Window(d->window)); {
            SDL_Event u = { .type = SDL_USEREVENT };
            u.user.code = command_UserEventCode;
            u.user.data1 = strdup("theme.changed auto:1");
            dispatchEvent_Window(as_Window(d->window), &u);
        }
#endif
        /* Find the window that is being resized and redraw it immediately. */
        iConstForEach(PtrArray, i, &d->mainWindows) {
            const iMainWindow *win = i.ptr;
            if (SDL_GetWindowID(win->base.win) == winev->windowID) {
                drawWhileResizing_MainWindow(d->window, winev->data1, winev->data2);
                break;
            }
        }
    }
    return 0;
}

static int run_App_(iApp *d) {
    /* Initial arrangement. */
    iForIndices(i, d->window->base.roots) {
        if (d->window->base.roots[i]) {
            arrange_Widget(d->window->base.roots[i]->widget);
        }
    }
    d->isRunning = iTrue;
    SDL_EventState(SDL_DROPFILE, SDL_ENABLE); /* open files via drag'n'drop */
#if defined (LAGRANGE_ENABLE_RESIZE_DRAW)
    SDL_AddEventWatch(resizeWatcher_, d); /* redraw window during resizing */
#endif
    while (d->isRunning) {
        processEvents_App(waitForNewEvents_AppEventMode);
        runTickers_App_(d);
        refresh_App();
        /* Change the widget tree while we are not iterating through it. */
        checkPendingSplit_MainWindow(d->window);
        recycle_Garbage();
    }
    SDL_DelEventWatch(resizeWatcher_, d);
    return 0;
}

void refresh_App(void) {
    iApp *d = &app_;
#if defined (LAGRANGE_ENABLE_IDLE_SLEEP)
    if (d->warmupFrames == 0 && d->isIdling) {
        return;
    }
#endif
    iPtrArray windows;
    init_PtrArray(&windows);
    listWindows_App_(d, &windows);
    /* Destroy pending widgets. */ {
        iConstForEach(PtrArray, j, &windows) {
            iWindow *win = j.ptr;
            setCurrent_Window(win);
            iForIndices(i, win->roots) {
                iRoot *root = win->roots[i];
                if (root) {
                    destroyPending_Root(root);
                }
            }
        }
    }
    /* TODO: `pendingRefresh` should be window-specific. */
    if (d->warmupFrames || exchange_Atomic(&d->pendingRefresh, iFalse)) {
        /* Draw each window. */
        iConstForEach(PtrArray, j, &windows) {
            iWindow *win = j.ptr;
            if (!d->warmupFrames && !exchange_Atomic(&win->isRefreshPending, iFalse)) {
                continue; /* No need to draw this window. */
            }
            setCurrent_Window(win);
            switch (win->type) {
                case main_WindowType: {
//                    iTime draw;
//                    initCurrent_Time(&draw);
                    draw_MainWindow(as_MainWindow(win));
//                    printf("draw: %lld \u03bcs\n", (long long) (elapsedSeconds_Time(&draw) * 1000000));
//                    fflush(stdout);
                    break;
                }
                default:
                    draw_Window(win);
                    break;
            }
            win->frameCount++;
            if (isTerminal_Platform()) {
                sleep_Thread(1.0 / 60.0);
            }
        }
    }
    else {
#if defined (iPlatformApple)
        /* Nothing needs redrawing, however we may have to keep blitting the latest contents.
           The Metal renderer can get stuttery otherwise. */ 
        iConstForEach(PtrArray, j, &windows) {
            iWindow *win = j.ptr;
            switch (win->type) {
                case main_WindowType:
                    drawQuick_MainWindow(as_MainWindow(win));
                    break;
                default:
                    break;
            }
        }
#endif
    }
    if (d->warmupFrames > 0) {
        d->warmupFrames--;
    }
    deinit_PtrArray(&windows);
}

iBool isRefreshPending_App(void) {
    const iApp *d = &app_;
    return !isEmpty_SortedArray(&d->tickers) || value_Atomic(&app_.pendingRefresh);
}

iBool isFinishedLaunching_App(void) {
    return app_.isFinishedLaunching;
}

uint32_t elapsedSinceLastTicker_App(void) {
    return app_.elapsedSinceLastTicker;
}

const iPrefs *prefs_App(void) {
    return &app_.prefs;
}

iBool forceSoftwareRender_App(void) {
    if (app_.forceSoftwareRender) {
        return iTrue;
    }
#if defined (LAGRANGE_ENABLE_X11_SWRENDER)
    if (getenv("DISPLAY")) {
        return iTrue;
    }
#endif
    return iFalse;
}

void setForceSoftwareRender_App(iBool sw) {
    app_.forceSoftwareRender = sw;
}

enum iColorTheme colorTheme_App(void) {
    return app_.prefs.theme;
}

const iString *schemeProxy_App(iRangecc scheme) {
    iApp *d = &app_;
    const iString *proxy = NULL;
    if (equalCase_Rangecc(scheme, "gemini")) {
        proxy = &d->prefs.strings[geminiProxy_PrefsString];
    }
    else if (equalCase_Rangecc(scheme, "gopher")) {
        proxy = &d->prefs.strings[gopherProxy_PrefsString];
    }
    else if (equalCase_Rangecc(scheme, "http") || equalCase_Rangecc(scheme, "https")) {
        proxy = &d->prefs.strings[httpProxy_PrefsString];
    }
    return isEmpty_String(proxy) ? NULL : proxy;
}

int run_App(int argc, char **argv) {
    init_App_(&app_, argc, argv);
    const int rc = run_App_(&app_);
    deinit_App(&app_);
    return rc;
}

void postRefresh_App(void) {
    iApp *d = &app_;
#if defined (LAGRANGE_ENABLE_IDLE_SLEEP)
    d->isIdling = iFalse;
#endif
    iAtomicInt *pendingWindow = (get_Window() ? &get_Window()->isRefreshPending
                                              : NULL);
    iBool wasPending = exchange_Atomic(&d->pendingRefresh, iTrue);
    if (pendingWindow) {
        wasPending |= exchange_Atomic(pendingWindow, iTrue);
    }
    if (!wasPending) {
        SDL_Event ev = { .type = SDL_USEREVENT };
        ev.user.code = refresh_UserEventCode;
        SDL_PushEvent(&ev);
    }
}

void postCommand_Root(iRoot *d, const char *command) {
    iAssert(command);
    if (strlen(command) == 0) {
        return;
    }
    if (*command == '!') {
        /* Global command; this is global context so just ignore. */
        command++;
    }
    if (*command == '~') {
        /* Requires launch to be finished; defer it if needed. */
        command++;
        if (!app_.isFinishedLaunching) {
            pushBackCStr_StringList(app_.launchCommands, command);
            return;
        }
    }
    SDL_Event ev = { .type = SDL_USEREVENT };
    ev.user.code = command_UserEventCode;
    ev.user.data1 = strdup(command);
    ev.user.data2 = d; /* all events are root-specific */
    ev.user.windowID = d ? id_Window(d->window) : 0; /* root-specific means window-specific */
    SDL_PushEvent(&ev);
    iWindow *win = d ? d->window : NULL;
#if defined (iPlatformAndroid)
    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "%s[command] {%d} %s",
                app_.isLoadingPrefs ? "[Prefs] " : "",
                (d == NULL || win == NULL ? 0 : d == win->roots[0] ? 1 : 2),
                command);
#else
    if (app_.commandEcho) {
        const int windowIndex =
            win && type_Window(win) == main_WindowType ? windowIndex_App(as_MainWindow(win)) + 1 : 0;
        printf("%s%s[command] {%d:%d} %s\n",
               !app_.isFinishedLaunching ? "<Ln> " : "",
               app_.isLoadingPrefs ? "<Pr> " : "",
               windowIndex,
               (d == NULL || win == NULL ? 0
                : d == win->roots[0]     ? 1
                                         : 2),
               command);
        fflush(stdout);
    }
#endif
}

void postCommandf_Root(iRoot *d, const char *command, ...) {
    iBlock chars;
    init_Block(&chars, 0);
    va_list args;
    va_start(args, command);
    vprintf_Block(&chars, command, args);
    va_end(args);
    postCommand_Root(d, cstr_Block(&chars));
    deinit_Block(&chars);
}

void postCommandf_App(const char *command, ...) {
    iBlock chars;
    init_Block(&chars, 0);
    va_list args;
    va_start(args, command);
    vprintf_Block(&chars, command, args);
    va_end(args);
    postCommand_Root(NULL, cstr_Block(&chars));
    deinit_Block(&chars);
}

void rootOrder_App(iRoot *roots[2]) {
    const iWindow *win = get_Window();
    if (win) {
        roots[0] = win->keyRoot;
        roots[1] = (roots[0] == win->roots[0] ? win->roots[1] : win->roots[0]);
    }
    else {
        roots[0] = roots[1] = NULL;
    }
}

iAny *findWidget_App(const char *id) {
    if (!*id) return NULL;
    iRoot *order[2];
    rootOrder_App(order);
    iForIndices(i, order) {
        if (order[i]) {
            iAny *found = findChild_Widget(order[i]->widget, id);
            if (found) {
                return found;
            }
        }
    }
    return NULL;
}

void addTicker_App(iTickerFunc ticker, iAny *context) {
    iApp *d = &app_;
    insert_SortedArray(&d->tickers, &(iTicker){ context, get_Root(), ticker });
    postRefresh_App();
}

void addTickerRoot_App(iTickerFunc ticker, iRoot *root, iAny *context) {
    iApp *d = &app_;
    insert_SortedArray(&d->tickers, &(iTicker){ context, root, ticker });
    postRefresh_App();
}

void removeTicker_App(iTickerFunc ticker, iAny *context) {
    iApp *d = &app_;
    remove_SortedArray(&d->tickers, &(iTicker){ context, NULL, ticker });
}

void addWindow_App(iMainWindow *win) {
    iApp *d = &app_;
    pushBack_PtrArray(&d->mainWindows, win);
}

void removeWindow_App(iMainWindow *win) {
    iApp *d = &app_;
    removeOne_PtrArray(&d->mainWindows, win);
    if (isEmpty_PtrArray(&d->mainWindows)) {
        d->window = NULL;
    }
}

size_t numWindows_App(void) {
    return size_PtrArray(&app_.mainWindows);
}

size_t windowIndex_App(const iMainWindow *win) {
    return indexOf_PtrArray(&app_.mainWindows, win); 
}

iMainWindow *newMainWindow_App(void) {
    iApp *d = &app_;
    iMainWindow *win = new_MainWindow(initialWindowRect_App_(d, size_PtrArray(&d->mainWindows)));
    addWindow_App(win);
    return win;
}

const iPtrArray *mainWindows_App(void) {
    return &app_.mainWindows;
}

void setActiveWindow_App(iMainWindow *win) {
    iApp *d = &app_;
    d->window = win;
}

void addPopup_App(iWindow *popup) {
    iApp *d = &app_;
    pushBack_PtrArray(&d->popupWindows, popup);
}

void removePopup_App(iWindow *popup) {
    iApp *d = &app_;
    removeOne_PtrArray(&d->popupWindows, popup);
}

iMimeHooks *mimeHooks_App(void) {
    return app_.mimehooks;
}

iPeriodic *periodic_App(void) {
    return &app_.periodic;
}

iBool isLandscape_App(void) {
    const iInt2 size = size_Window(get_Window());
    return size.x > size.y;
}

enum iAppDeviceType deviceType_App(void) {
#if defined (iPlatformMobilePhone)
    return phone_AppDeviceType;
#elif defined (iPlatformMobileTablet)
    return tablet_AppDeviceType;
#elif defined (iPlatformAppleMobile)
    return isPhone_iOS() ? phone_AppDeviceType : tablet_AppDeviceType;
#elif defined (iPlatformAndroidMobile)
    return phone_AppDeviceType; /* TODO: Java side could tell us via cmdline if this is a tablet. */
#else
    return desktop_AppDeviceType;
#endif
}

iBool isRunningUnderWindowSystem_App(void) {
    return app_.isRunningUnderWindowSystem;
}

const iCommandLine *commandLine_App(void) {
    return &app_.args;
}

iGmCerts *certs_App(void) {
    return app_.certs;
}

iVisited *visited_App(void) {
    return app_.visited;
}

iBookmarks *bookmarks_App(void) {
    return app_.bookmarks;
}

static void updatePrefsThemeButtons_(iWidget *d) {
    for (size_t i = 0; i < max_ColorTheme; i++) {
        setFlags_Widget(findChild_Widget(d, format_CStr("prefs.theme.%u", i)),
                        selected_WidgetFlag,
                        colorTheme_App() == i);
    }
    for (size_t i = 0; i < max_ColorAccent; i++) {
        setFlags_Widget(findChild_Widget(d, format_CStr("prefs.accent.%u", i)),
                        selected_WidgetFlag,
                        prefs_App()->accent == i);
    }
}

static void updatePrefsPinSplitButtons_(iWidget *d, int value) {
    for (int i = 0; i < 3; i++) {
        setFlags_Widget(findChild_Widget(d, format_CStr("prefs.pinsplit.%d", i)),
                        selected_WidgetFlag,
                        i == value);
    }
}

static void updatePrefsToolBarActionButton_(iWidget *prefs, int buttonIndex, int action) {
    updateDropdownSelection_LabelWidget(
        findChild_Widget(prefs, format_CStr("prefs.toolbaraction%d", buttonIndex + 1)),
        format_CStr(" arg:%d button:%d", action, buttonIndex));    
}

static void updateScrollSpeedButtons_(iWidget *d, enum iScrollType type, const int value) {
    const char *typeStr = (type == mouse_ScrollType ? "mouse" : "keyboard");
    for (int i = 0; i <= 40; i++) {
        setFlags_Widget(findChild_Widget(d, format_CStr("prefs.scrollspeed.%s.%d", typeStr, i)),
                        selected_WidgetFlag,
                        i == value);
    }
}

static void updateColorThemeButton_(iLabelWidget *button, int theme) {
    /* TODO: These three functions are all the same? Cleanup? */
    if (!button) return;
    updateDropdownSelection_LabelWidget(button, format_CStr(".set arg:%d", theme));
}

static void updateFontButton_(iLabelWidget *button, const iString *fontId) {
    if (!button || isEmpty_String(fontId)) return;
    updateDropdownSelection_LabelWidget(button, format_CStr(":%s", cstr_String(fontId)));
}

static void updateImageStyleButton_(iLabelWidget *button, int style) {
    if (!button) return;
    updateDropdownSelection_LabelWidget(button, format_CStr(".set arg:%d", style));
}

static iBool handlePrefsCommands_(iWidget *d, const char *cmd) {
    if (equal_Command(cmd, "prefs.dismiss") || equal_Command(cmd, "preferences")) {
        setupSheetTransition_Mobile(d, iFalse);
        setUiScale_Window(get_Window(),
                          toFloat_String(text_InputWidget(findChild_Widget(d, "prefs.uiscale"))));
#if defined (LAGRANGE_ENABLE_DOWNLOAD_EDIT)
        postCommandf_App("downloads path:%s",
                         cstr_String(text_InputWidget(findChild_Widget(d, "prefs.downloads"))));
#endif
        postCommandf_App("customframe arg:%d",
                         isSelected_Widget(findChild_Widget(d, "prefs.customframe")));
        postCommandf_App("window.retain arg:%d",
                         isSelected_Widget(findChild_Widget(d, "prefs.retainwindow")));
        postCommandf_App("smoothscroll arg:%d",
                         isSelected_Widget(findChild_Widget(d, "prefs.smoothscroll")));
        postCommandf_App("imageloadscroll arg:%d",
                         isSelected_Widget(findChild_Widget(d, "prefs.imageloadscroll")));
        postCommandf_App("hidetoolbarscroll arg:%d",
                         isSelected_Widget(findChild_Widget(d, "prefs.hidetoolbarscroll")));
        postCommandf_App("ostheme arg:%d", isSelected_Widget(findChild_Widget(d, "prefs.ostheme")));
        postCommandf_App("font.user path:%s",
                         cstrText_InputWidget(findChild_Widget(d, "prefs.userfont")));
        postCommandf_App("decodeurls arg:%d",
                         isSelected_Widget(findChild_Widget(d, "prefs.decodeurls")));
        postCommandf_App("searchurl address:%s",
                         cstrText_InputWidget(findChild_Widget(d, "prefs.searchurl")));
        postCommandf_App("tabwidth.set arg:%d",
                         toInt_String(text_InputWidget(findChild_Widget(d, "prefs.tabwidth"))));
        postCommandf_App("cachesize.set arg:%d",
                         toInt_String(text_InputWidget(findChild_Widget(d, "prefs.cachesize"))));
        postCommandf_App("memorysize.set arg:%d",
                         toInt_String(text_InputWidget(findChild_Widget(d, "prefs.memorysize"))));
        postCommandf_App("urlsize.set arg:%d",
                         toInt_String(text_InputWidget(findChild_Widget(d, "prefs.urlsize"))));
        postCommandf_App("ca.file path:%s",
                         cstrText_InputWidget(findChild_Widget(d, "prefs.ca.file")));
        postCommandf_App("ca.path path:%s",
                         cstrText_InputWidget(findChild_Widget(d, "prefs.ca.path")));
        postCommandf_App("proxy.gemini address:%s",
                         cstrText_InputWidget(findChild_Widget(d, "prefs.proxy.gemini")));
        postCommandf_App("proxy.gopher address:%s",
                         cstrText_InputWidget(findChild_Widget(d, "prefs.proxy.gopher")));
        postCommandf_App("proxy.http address:%s",
                         cstrText_InputWidget(findChild_Widget(d, "prefs.proxy.http")));
        const iWidget *tabs = findChild_Widget(d, "prefs.tabs");
        if (tabs) {
            postCommandf_App("prefs.dialogtab arg:%u",
                             tabPageIndex_Widget(tabs, currentTabPage_Widget(tabs)));
        }
        destroy_Widget(d);
        postCommand_App("prefs.changed");
        return iTrue;
    }
    else if (equal_Command(cmd, "tabs.changed")) {
        setFlags_Widget(findChild_Widget(d, "prefs.aboutfonts"), hidden_WidgetFlag,
                        !equal_Rangecc(range_Command(cmd, "id"), "prefs.page.fonts"));
        return iFalse;
    }
    else if (equal_Command(cmd, "uilang")) {
        updateDropdownSelection_LabelWidget(findChild_Widget(d, "prefs.uilang"),
                                            cstr_String(string_Command(cmd, "id")));
        return iFalse;
    }
    else if (equal_Command(cmd, "quoteicon.set")) {
        const int arg = arg_Command(cmd);
        setFlags_Widget(findChild_Widget(d, "prefs.quoteicon.0"), selected_WidgetFlag, arg == 0);
        setFlags_Widget(findChild_Widget(d, "prefs.quoteicon.1"), selected_WidgetFlag, arg == 1);
        return iFalse;
    }
    else if (equal_Command(cmd, "returnkey.set")) {
        updateDropdownSelection_LabelWidget(findChild_Widget(d, "prefs.returnkey"),
                                            format_CStr("returnkey.set arg:%d", arg_Command(cmd)));
        return iFalse;
    }
    else if (equal_Command(cmd, "toolbar.action.set")) {
        updatePrefsToolBarActionButton_(d, argLabel_Command(cmd, "button"), arg_Command(cmd));
        return iFalse;
    }
    else if (equal_Command(cmd, "pinsplit.set")) {
        updatePrefsPinSplitButtons_(d, arg_Command(cmd));
        return iFalse;
    }
    else if (equal_Command(cmd, "scrollspeed")) {
        updateScrollSpeedButtons_(d, argLabel_Command(cmd, "type"), arg_Command(cmd));
        return iFalse;
    }
    else if (equal_Command(cmd, "doctheme.dark.set")) {
        updateColorThemeButton_(findChild_Widget(d, "prefs.doctheme.dark"), arg_Command(cmd));
        return iFalse;
    }
    else if (equal_Command(cmd, "doctheme.light.set")) {
        updateColorThemeButton_(findChild_Widget(d, "prefs.doctheme.light"), arg_Command(cmd));
        return iFalse;
    }
    else if (equal_Command(cmd, "imagestyle.set")) {
        updateImageStyleButton_(findChild_Widget(d, "prefs.imagestyle"), arg_Command(cmd));
        return iFalse;
    }
    else if (equal_Command(cmd, "font.set")) {
        updateFontButton_(findChild_Widget(d, "prefs.font.ui"),      string_Command(cmd, "ui"));
        updateFontButton_(findChild_Widget(d, "prefs.font.heading"), string_Command(cmd, "heading"));
        updateFontButton_(findChild_Widget(d, "prefs.font.body"),    string_Command(cmd, "body"));
        updateFontButton_(findChild_Widget(d, "prefs.font.mono"),    string_Command(cmd, "mono"));
        updateFontButton_(findChild_Widget(d, "prefs.font.monodoc"), string_Command(cmd, "monodoc"));
        return iFalse;
    }
    else if (equalArg_Command(cmd, "input.ended", "id", "prefs.linespacing")) {
        /* Apply line spacing changes immediately. */
        const iInputWidget *lineSpacing = findWidget_App("prefs.linespacing");
        postCommandf_App("linespacing.set arg:%f", toFloat_String(text_InputWidget(lineSpacing)));
        return iTrue;
    }
    else if (equal_Command(cmd, "prefs.ostheme.changed")) {
        postCommandf_App("ostheme arg:%d", arg_Command(cmd));
    }
    else if (equal_Command(cmd, "theme.changed")) {
        updatePrefsThemeButtons_(d);
        if (!argLabel_Command(cmd, "auto")) {
            setToggle_Widget(findChild_Widget(d, "prefs.ostheme"), prefs_App()->useSystemTheme);
        }
    }
    else if (equalWidget_Command(cmd, d, "input.resized")) {
        if (d->root->pendingArrange < arg_Command(cmd)) {
            d->root->pendingArrange = arg_Command(cmd);
            postCommand_Root(d->root, "root.arrange");
        }
        return iTrue;
    }
    return iFalse;
}

iDocumentWidget *document_Root(iRoot *d) {
    return iConstCast(iDocumentWidget *, currentTabPage_Widget(findChild_Widget(d->widget, "doctabs")));
}

iDocumentWidget *document_App(void) {
    return document_Root(get_Root());
}

iDocumentWidget *document_Command(const char *cmd) {
    /* Explicitly referenced. */
    iAnyObject *obj = pointerLabel_Command(cmd, "doc");
    if (obj) {
        return obj;
    }
    /* Implicit via source widget. */
    obj = pointer_Command(cmd);
    if (obj && isInstance_Object(obj, &Class_DocumentWidget)) {
        return obj;
    }
    /* Currently visible document. */
    return document_App();
}

iDocumentWidget *newTab_App(const iDocumentWidget *duplicateOf, iBool switchToNew) {
    iWidget *tabs = findWidget_Root("doctabs");
    setFlags_Widget(tabs, hidden_WidgetFlag, iFalse);
    iWidget *newTabButton = findChild_Widget(tabs, "newtab");
    removeChild_Widget(newTabButton->parent, newTabButton);
    iDocumentWidget *doc;
    if (duplicateOf) {
        doc = duplicate_DocumentWidget(duplicateOf);
    }
    else {
        doc = new_DocumentWidget();
    }
    appendTabPage_Widget(tabs, as_Widget(doc), "", 0, 0);
    iRelease(doc); /* now owned by the tabs */
    addTabCloseButton_Widget(tabs, as_Widget(doc), "tabs.close");
    addChild_Widget(findChild_Widget(tabs, "tabs.buttons"), iClob(newTabButton));
    showOrHideNewTabButton_Root(tabs->root);
    if (switchToNew) {
        postCommandf_App("tabs.switch page:%p", doc);
    }
    arrange_Widget(tabs);
    refresh_Widget(tabs);
    postCommandf_Root(get_Root(), "tab.created id:%s", cstr_String(id_Widget(as_Widget(doc))));
    return doc;
}

void closeWindow_App(iMainWindow *win) {
    iApp *d = &app_;
    iForIndices(r, win->base.roots) {
        if (win->base.roots[r]) {
            setTreeFlags_Widget(win->base.roots[r]->widget, destroyPending_WidgetFlag, iTrue);
        }
    }
    collect_Garbage(win, (iDeleteFunc) delete_MainWindow);
    postRefresh_App();
    if (isAppleDesktop_Platform() && size_PtrArray(&d->mainWindows) == 1) {
        /* The one and only window is being closed. On macOS, the app will keep running, which
           means we must save the state of the window now or otherwise it will be lost. A newly
           opened window will use this saved state if it's the only window of the app. */
        saveState_App_(d);        
    }
    if (d->window == win) {
        /* Activate another window. */
        iForEach(PtrArray, i, &d->mainWindows) {
            if (i.ptr != d->window) {
                SDL_RaiseWindow(i.ptr);
                setActiveWindow_App(i.ptr);
                setCurrent_Window(i.ptr);
                break;
            }
        }
    }
}

static iBool handleIdentityCreationCommands_(iWidget *dlg, const char *cmd) {
    iApp *d = &app_;
    if (equal_Command(cmd, "ident.showmore")) {
        iForEach(ObjectList,
                 i,
                 children_Widget(findChild_Widget(
                     dlg, isUsingPanelLayout_Mobile() ? "panel.top" : "headings"))) {
            if (flags_Widget(i.object) & collapse_WidgetFlag) {
                setFlags_Widget(i.object, hidden_WidgetFlag, iFalse);
            }
        }
        iForEach(ObjectList, j, children_Widget(findChild_Widget(dlg, "values"))) {
            if (flags_Widget(j.object) & collapse_WidgetFlag) {
                setFlags_Widget(j.object, hidden_WidgetFlag, iFalse);
            }
        }
        setFlags_Widget(pointer_Command(cmd), disabled_WidgetFlag, iTrue);
        arrange_Widget(dlg);
        refresh_Widget(dlg);
        return iTrue;
    }
    if (equal_Command(cmd, "ident.scope")) {
        iLabelWidget *scope = findChild_Widget(dlg, "ident.scope");
        updateDropdownSelection_LabelWidget(scope, format_CStr(" arg:%d", arg_Command(cmd)));
        updateSize_LabelWidget(scope);
        arrange_Widget(findWidget_App("ident"));
        return iTrue;
    }
    if (equal_Command(cmd, "ident.temp.changed")) {
        setFlags_Widget(
            findChild_Widget(dlg, "ident.temp.note"), hidden_WidgetFlag, !arg_Command(cmd));
        return iFalse;
    }
    if (equal_Command(cmd, "ident.accept") || equal_Command(cmd, "ident.cancel")) {
        if (equal_Command(cmd, "ident.accept")) {
            const iString *commonName   = text_InputWidget (findChild_Widget(dlg, "ident.common"));
            const iString *email        = text_InputWidget (findChild_Widget(dlg, "ident.email"));
            const iString *userId       = text_InputWidget (findChild_Widget(dlg, "ident.userid"));
            const iString *domain       = text_InputWidget (findChild_Widget(dlg, "ident.domain"));
            const iString *organization = text_InputWidget (findChild_Widget(dlg, "ident.org"));
            const iString *country      = text_InputWidget (findChild_Widget(dlg, "ident.country"));
            const iBool    isTemp       = isSelected_Widget(findChild_Widget(dlg, "ident.temp"));
            if (isEmpty_String(commonName)) {
                makeSimpleMessage_Widget(orange_ColorEscape "${heading.newident.missing}",
                                         "${dlg.newindent.missing.commonname}");
                return iTrue;
            }
            iDate until;
            /* Validate the date. */ {
                iZap(until);
                unsigned int val[6];
                iDate today;
                initCurrent_Date(&today);
                const int n =
                    sscanf(cstr_String(text_InputWidget(findChild_Widget(dlg, "ident.until"))),
                           "%04u-%u-%u %u:%u:%u",
                           &val[0], &val[1], &val[2], &val[3], &val[4], &val[5]);
                if (n <= 0) {
                    makeSimpleMessage_Widget(orange_ColorEscape "${heading.newident.date.bad}",
                                             "${dlg.newident.date.example}");
                    return iTrue;
                }
                until.year   = val[0];
                until.month  = n >= 2 ? val[1] : 1;
                until.day    = n >= 3 ? val[2] : 1;
                until.hour   = n >= 4 ? val[3] : 0;
                until.minute = n >= 5 ? val[4] : 0;
                until.second = n == 6 ? val[5] : 0;
                until.gmtOffsetSeconds = today.gmtOffsetSeconds;
                /* In the past? */ {
                    iTime now, t;
                    initCurrent_Time(&now);
                    init_Time(&t, &until);
                    if (cmp_Time(&t, &now) <= 0) {
                        makeSimpleMessage_Widget(orange_ColorEscape "${heading.newident.date.bad}",
                                                 "${dlg.newident.date.past}");
                        return iTrue;
                    }
                }
            }
            /* The input seems fine. */
            iGmIdentity *ident = newIdentity_GmCerts(d->certs,
                                                     isTemp ? temporary_GmIdentityFlag : 0,
                                                     until,
                                                     commonName,
                                                     email,
                                                     userId,
                                                     domain,
                                                     organization,
                                                     country);
            /* Use in the chosen scope. */ {
                int         selScope = 2;
                const char *scopeCmd =
                    selectedDropdownCommand_LabelWidget(findChild_Widget(dlg, "ident.scope"));
                if (startsWith_CStr(scopeCmd, "ident.scope arg:")) {
                    selScope = arg_Command(scopeCmd);
                }
                const iString *docUrl = url_DocumentWidget(document_Root(dlg->root));
                iString *useUrl = NULL;
                switch (selScope) {
                    case 0: /* current domain */
                        useUrl = collectNewFormat_String("gemini://%s",
                                                         cstr_Rangecc(urlHost_String(docUrl)));
                        break;
                    case 1: /* current page */
                        useUrl = collect_String(copy_String(docUrl));
                        break;
                    default: /* not used */
                        break;
                }
                if (useUrl) {
                    signIn_GmCerts(d->certs, ident, useUrl);
                    postCommand_App("navigate.reload");
                }
            }
            postCommandf_App("sidebar.mode arg:%d show:1", identities_SidebarMode);
            postCommand_App("idents.changed");
        }
        setupSheetTransition_Mobile(dlg, dialogTransitionDir_Widget(dlg));
        destroy_Widget(dlg);
        return iTrue;
    }
    return iFalse;
}

iBool willUseProxy_App(const iRangecc scheme) {
    return schemeProxy_App(scheme) != NULL;
}

const iString *searchQueryUrl_App(const iString *queryStringUnescaped) {
    iApp *d = &app_;
    if (isEmpty_String(&d->prefs.strings[searchUrl_PrefsString])) {
        return collectNew_String();
    }
    const iString *escaped = urlEncode_String(queryStringUnescaped);
    return collectNewFormat_String(
        "%s?%s", cstr_String(&d->prefs.strings[searchUrl_PrefsString]), cstr_String(escaped));
}

void resetFonts_App(void) {
    iApp *d = &app_;
    iConstForEach(PtrArray, win, listWindows_App_(d, collectNew_PtrArray())) {
        resetFonts_Text(text_Window(win.ptr));
    }
}

void availableFontsChanged_App(void) {
    iApp *d = &app_;
    iConstForEach(PtrArray, win, listWindows_App_(d, collectNew_PtrArray())) {
        resetMissing_Text(text_Window(win.ptr));
    }    
}

static void invalidateCachedDocuments_App_(void) {
    iForEach(ObjectList, i, iClob(listDocuments_App(NULL))) {
        invalidateCachedLayout_History(history_DocumentWidget(i.object));
    }
}

static void pushClosedTabUrl_App_(iApp *d, const iString *url) {
    pushBack_StringList(d->recentlyClosedTabUrls, url);
    if (size_StringList(d->recentlyClosedTabUrls) > 50) { /* not an infinite number */
        popFront_StringList(d->recentlyClosedTabUrls);
    }
}

static const iString *popClosedTabUrl_App_(iApp *d) {
    if (isEmpty_StringList(d->recentlyClosedTabUrls)) {
        return NULL;
    }
    return collect_String(takeLast_StringList(d->recentlyClosedTabUrls));    
}

static iBool handleNonWindowRelatedCommand_App_(iApp *d, const char *cmd) {
    const iBool isFrozen = !d->window || d->window->isDrawFrozen;
    /* Commands related to preferences. */
    if (equal_Command(cmd, "prefs.changed")) {
        savePrefs_App_(d);
        return iTrue;
    }
    else if (equal_Command(cmd, "prefs.dialogtab")) {
        d->prefs.dialogTab = arg_Command(cmd);
        return iTrue;
    }
    else if (equal_Command(cmd, "uilang")) {
        const iString *lang = string_Command(cmd, "id");
        iString *val = &d->prefs.strings[uiLanguage_PrefsString];
        if (!equal_String(lang, val)) {
            set_String(val, lang);
            setCurrent_Lang(cstr_String(val));
            postCommand_App("lang.changed");
        }
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
    else if (equal_Command(cmd, "toolbar.action.set")) {
        d->prefs.toolbarActions[iClamp(argLabel_Command(cmd, "button"), 0, 1)] =
            iClamp(arg_Command(cmd), 0, max_ToolbarAction - 1);
        if (!isFrozen) {
            postCommand_App("~toolbar.actions.changed");
        }
        return iTrue;        
    }
    else if (equal_Command(cmd, "prefs.bottomnavbar.changed")) {
        d->prefs.bottomNavBar = arg_Command(cmd) != 0;
        if (!isFrozen) {
            postCommand_App("~root.movable");
        }
        return iTrue;
    }
    else if (equal_Command(cmd, "prefs.bottomtabbar.changed")) {
        d->prefs.bottomTabBar = arg_Command(cmd) != 0;
        if (!isFrozen) {
            postCommand_App("~root.movable");
        }
        return iTrue;
    }
    else if (equal_Command(cmd, "prefs.menubar.changed")) {
        d->prefs.menuBar = arg_Command(cmd) != 0;
        if (!isFrozen) {
            postCommand_App("~root.movable");
        }
        return iTrue;
    }
    else if (equal_Command(cmd, "prefs.evensplit.changed")) {
        d->prefs.evenSplit = arg_Command(cmd) != 0;
        if (!isFrozen) {
            iForEach(PtrArray, i, &d->mainWindows) {
                resizeSplits_MainWindow(i.ptr, iTrue);
            }
        }
        return iTrue;
    }
    else if (equal_Command(cmd, "prefs.tui.simple.changed")) {
        d->prefs.simpleChars = arg_Command(cmd) != 0;
#if defined (iPlatformTerminal)
        SDL_SetHint(SDL_HINT_VIDEO_CURSES_SIMPLE_CHARACTERS, d->prefs.simpleChars ? "1" : "0");
        invalidate_Window(d->window);
#endif
        return iTrue;
    }
    else if (equal_Command(cmd, "parentnavskipindex")) {
        d->prefs.skipIndexPageOnParentNavigation = arg_Command(cmd) != 0;
        return iTrue;
    }
    else if (equal_Command(cmd, "translation.languages")) {
        d->prefs.langFrom = argLabel_Command(cmd, "from");
        d->prefs.langTo   = argLabel_Command(cmd, "to");
        return iTrue;
    }
    else if (equal_Command(cmd, "window.retain")) {
        d->prefs.retainWindowSize = arg_Command(cmd);
        return iTrue;
    }
    else if (equal_Command(cmd, "customframe")) {
        d->prefs.customFrame = arg_Command(cmd);
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
            }
            postCommand_App("font.changed");
        }
        if (!isFrozen) {
            postCommand_App("window.unfreeze");
        }
        return iTrue;
    }
    else if (equal_Command(cmd, "prefs.retaintabs.changed")) {
        d->prefs.retainTabs = arg_Command(cmd);
        return iTrue;
    }
    else if (equal_Command(cmd, "prefs.font.smooth.changed")) {
        if (!isFrozen) {
            setFreezeDraw_MainWindow(get_MainWindow(), iTrue);
        }
        d->prefs.fontSmoothing = arg_Command(cmd) != 0;
        if (!isFrozen) {
            resetFontCache_Text(text_Window(get_MainWindow())); /* clear the glyph cache */
            postCommand_App("font.changed");
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
    else if (equal_Command(cmd, "prefs.markdown.viewsource.changed")) {
        d->prefs.markdownAsSource = arg_Command(cmd) != 0;
        return iTrue;
    }
    else if (equal_Command(cmd, "prefs.mono.gemini.changed") ||
             equal_Command(cmd, "prefs.mono.gopher.changed")) {
        const iBool isSet = (arg_Command(cmd) != 0);
        if (!isFrozen) {
            setFreezeDraw_MainWindow(get_MainWindow(), iTrue);
        }
        if (startsWith_CStr(cmd, "prefs.mono.gemini")) {
            d->prefs.monospaceGemini = isSet;
        }
        else {
            d->prefs.monospaceGopher = isSet;
        }
        if (!isFrozen) {
            postCommand_App("font.changed");
            postCommand_App("window.unfreeze");
        }
        return iTrue;
    }
    else if (equal_Command(cmd, "prefs.boldlink.dark.changed") ||
             equal_Command(cmd, "prefs.boldlink.light.changed") ||
             equal_Command(cmd, "prefs.boldlink.visited.changed")) {
        const iBool isSet = (arg_Command(cmd) != 0);
        if (startsWith_CStr(cmd, "prefs.boldlink.visited")) {
            d->prefs.boldLinkVisited = isSet;
        }
        else if (startsWith_CStr(cmd, "prefs.boldlink.dark")) {
            d->prefs.boldLinkDark = isSet;
        }
        else {
            d->prefs.boldLinkLight = isSet;
        }
        if (!d->isLoadingPrefs) {
            postCommand_App("font.changed");
        }
        return iTrue;
    }
    else if (equal_Command(cmd, "prefs.biglede.changed")) {
        d->prefs.bigFirstParagraph = arg_Command(cmd) != 0;
        if (!d->isLoadingPrefs) {
            postCommand_App("document.layout.changed");
        }
        return iTrue;
    }
    else if (equal_Command(cmd, "prefs.justify.changed")) {
        d->prefs.justifyParagraph = arg_Command(cmd) != 0;
        if (!d->isLoadingPrefs) {
            postCommand_App("document.layout.changed");
        }
        return iTrue;
    }
    else if (equal_Command(cmd, "prefs.plaintext.wrap.changed")) {
        d->prefs.plainTextWrap = arg_Command(cmd) != 0;
        if (!d->isLoadingPrefs) {
            postCommand_App("document.layout.changed");
        }
        return iTrue;
    }
    else if (equal_Command(cmd, "prefs.sideicon.changed")) {
        d->prefs.sideIcon = arg_Command(cmd) != 0;
        postRefresh_App();
        return iTrue;
    }
    else if (equal_Command(cmd, "prefs.centershort.changed")) {
        d->prefs.centerShortDocs = arg_Command(cmd) != 0;
        if (!isFrozen) {
            invalidate_Window(d->window);
        }
        return iTrue;
    }
    else if (equal_Command(cmd, "prefs.collapsepreonload.changed")) {
        d->prefs.collapsePreOnLoad = arg_Command(cmd) != 0;
        return iTrue;
    }
    else if (equal_Command(cmd, "prefs.hoverlink.changed")) {
        d->prefs.hoverLink = arg_Command(cmd) != 0;
        postRefresh_App();
        return iTrue;
    }
    else if (equal_Command(cmd, "prefs.hoverlink.toggle")) {
        d->prefs.hoverLink = !d->prefs.hoverLink;
        postRefresh_App();
        return iTrue;
    }
    else if (equal_Command(cmd, "prefs.dataurl.openimages.changed")) {
        d->prefs.openDataUrlImagesOnLoad = arg_Command(cmd) != 0;
        return iTrue;
    }
    else if (equal_Command(cmd, "prefs.archive.openindex.changed")) {
        d->prefs.openArchiveIndexPages = arg_Command(cmd) != 0;
        return iTrue;
    }
    else if (equal_Command(cmd, "prefs.bookmarks.addbottom.changed")) {
        d->prefs.addBookmarksToBottom = arg_Command(cmd) != 0;
        return iTrue;
    }
    else if (equal_Command(cmd, "prefs.font.warnmissing.changed")) {
        d->prefs.warnAboutMissingGlyphs = arg_Command(cmd) != 0;
        return iTrue;
    }
    else if (equal_Command(cmd, "prefs.animate.changed")) {
        d->prefs.uiAnimations = arg_Command(cmd) != 0;
        return iTrue;
    }
    else if (equal_Command(cmd, "prefs.blink.changed")) {
        d->prefs.blinkingCursor = arg_Command(cmd) != 0;
        return iTrue;
    }
    else if (equal_Command(cmd, "prefs.time.24h.changed")) {
        d->prefs.time24h = arg_Command(cmd) != 0;
        return iTrue;
    }
    else if (equal_Command(cmd, "smoothscroll")) {
        d->prefs.smoothScrolling = arg_Command(cmd);
        return iTrue;
    }
    else if (equal_Command(cmd, "scrollspeed")) {
        const int type = argLabel_Command(cmd, "type");
        if (type == keyboard_ScrollType || type == mouse_ScrollType) {
            d->prefs.smoothScrollSpeed[type] = iClamp(arg_Command(cmd), 1, 40);
        }
        return iTrue;
    }
    else if (equal_Command(cmd, "decodeurls")) {
        d->prefs.decodeUserVisibleURLs = arg_Command(cmd);
        return iTrue;
    }
    else if (equal_Command(cmd, "imageloadscroll")) {
        d->prefs.loadImageInsteadOfScrolling = arg_Command(cmd);
        return iTrue;
    }
    else if (equal_Command(cmd, "returnkey.set")) {
        d->prefs.returnKey = arg_Command(cmd);
        return iTrue;
    }
    else if (equal_Command(cmd, "pinsplit.set")) {
        d->prefs.pinSplit = arg_Command(cmd);
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
    else if (equal_Command(cmd, "ostheme")) {
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
        d->prefs.lineWidth = iMax(20, arg_Command(cmd));
        postCommand_App("document.layout.changed");
        return iTrue;
    }
    else if (equal_Command(cmd, "linespacing.set")) {
        d->prefs.lineSpacing = iMax(0.5f, argf_Command(cmd));
        postCommand_App("document.layout.changed redo:1");
        return iTrue;
    }
    else if (equal_Command(cmd, "tabwidth.set")) {
        d->prefs.tabWidth = iMax(1, arg_Command(cmd));
        postCommand_App("document.layout.changed redo:1"); /* spaces need renormalizing */
        return iTrue;
    }
    else if (equal_Command(cmd, "quoteicon.set")) {
        d->prefs.quoteIcon = arg_Command(cmd) != 0;
        postCommand_App("document.layout.changed redo:1");
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
    else if (equal_Command(cmd, "searchurl")) {
        iString *url = &d->prefs.strings[searchUrl_PrefsString];
        setCStr_String(url, suffixPtr_Command(cmd, "address"));
        if (startsWith_String(url, "//")) {
            prependCStr_String(url, "gemini:");
        }
        if (!isEmpty_String(url) && !startsWithCase_String(url, "gemini://")) {
            prependCStr_String(url, "gemini://");
        }
        return iTrue;
    }
    else if (equal_Command(cmd, "proxy.gemini")) {
        setCStr_String(&d->prefs.strings[geminiProxy_PrefsString], suffixPtr_Command(cmd, "address"));
        return iTrue;
    }
    else if (equal_Command(cmd, "proxy.gopher")) {
        setCStr_String(&d->prefs.strings[gopherProxy_PrefsString], suffixPtr_Command(cmd, "address"));
        return iTrue;
    }
    else if (equal_Command(cmd, "proxy.http")) {
        setCStr_String(&d->prefs.strings[httpProxy_PrefsString], suffixPtr_Command(cmd, "address"));
        return iTrue;
    }
#if defined (LAGRANGE_ENABLE_DOWNLOAD_EDIT)
    else if (equal_Command(cmd, "downloads")) {
        setCStr_String(&d->prefs.strings[downloadDir_PrefsString], suffixPtr_Command(cmd, "path"));
        return iTrue;
    }
#endif
    else if (equal_Command(cmd, "downloads.open")) {
        postCommandf_App("open newtab:%d url:%s",
                         argLabel_Command(cmd, "newtab"),
                         cstrCollect_String(makeFileUrl_String(downloadDir_App())));
        return iTrue;
    }
    else if (equal_Command(cmd, "ca.file")) {
        setCStr_String(&d->prefs.strings[caFile_PrefsString], suffixPtr_Command(cmd, "path"));
        if (!argLabel_Command(cmd, "noset")) {
            updateCACertificates_App();
        }
        return iTrue;
    }
    else if (equal_Command(cmd, "ca.path")) {
        setCStr_String(&d->prefs.strings[caPath_PrefsString], suffixPtr_Command(cmd, "path"));
        if (!argLabel_Command(cmd, "noset")) {
            updateCACertificates_App();
        }
        return iTrue;
    }
    else if (equal_Command(cmd, "search")) {
        const int newTab = argLabel_Command(cmd, "newtab");
        const iString *query = collect_String(suffix_Command(cmd, "query"));
        if (!isLikelyUrl_String(query)) {
            const iString *url = searchQueryUrl_App(query);
            if (!isEmpty_String(url)) {
                postCommandf_App("open newtab:%d url:%s", newTab, cstr_String(url));
            }
        }
        else {
            postCommandf_App("open newtab:%d url:%s", newTab, cstr_String(query));
        }
        return iTrue;
    }
    else if (equal_Command(cmd, "reveal")) {
        const iString *path = NULL;
        if (hasLabel_Command(cmd, "path")) {
            path = suffix_Command(cmd, "path");
        }
        else if (hasLabel_Command(cmd, "url")) {
            path = collect_String(localFilePathFromUrl_String(suffix_Command(cmd, "url")));
        }
        if (path) {
            revealPath_App(path);
        }
        return iTrue;
    }
    else if (equal_Command(cmd, "window.new")) {
#if !defined (iPlatformTerminal)
        iMainWindow *newWin = new_MainWindow(initialWindowRect_App_(d, numWindows_App()));
        addWindow_App(newWin); /* takes ownership */
        SDL_ShowWindow(newWin->base.win);
        setCurrent_Window(newWin);        
        if (isAppleDesktop_Platform() && size_PtrArray(mainWindows_App()) == 1) {
            /* Restore the window state as it was before (sidebars, navigation history) when
               opening a window again after all windows have been closed. */
            setActiveWindow_App(newWin);
            loadState_App_(d);
        }
        if (hasLabel_Command(cmd, "url")) {
            postCommandf_Root(newWin->base.roots[0], "~open %s", cmd + 11 /* all arguments passed on */);
        }
        else {
            postCommand_Root(newWin->base.roots[0], "~navigate.home");
        }
        postCommand_Root(newWin->base.roots[0], "~window.unfreeze");
#endif
        return iTrue;
    }
    else if (equal_Command(cmd, "bookmarks.changed")) {
        save_Bookmarks(d->bookmarks, dataDir_App_());
        return iFalse;
    }
    else if (equal_Command(cmd, "bookmarks.sort")) {
        sort_Bookmarks(d->bookmarks, arg_Command(cmd), cmpTitleAscending_Bookmark);
        postCommand_App("bookmarks.changed");
        return iTrue;
    }
    else if (equal_Command(cmd, "bookmarks.reload.remote")) {
        fetchRemote_Bookmarks(bookmarks_App());
        return iTrue;
    }
    else if (equal_Command(cmd, "bookmarks.request.finished")) {
        requestFinished_Bookmarks(bookmarks_App(), pointerLabel_Command(cmd, "req"));
        return iTrue;
    }
    else if (equal_Command(cmd, "feeds.refresh")) {
        refresh_Feeds();
        return iTrue;
    }
    else if (equal_Command(cmd, "visited.changed")) {
        save_Visited(d->visited, dataDir_App_());
        return iFalse;
    }
    else if (equal_Command(cmd, "idents.changed")) {
        saveIdentities_GmCerts(d->certs);
        return iFalse;
    }
    else if (equal_Command(cmd, "ident.signin")) {
        const iString *url = collect_String(suffix_Command(cmd, "url"));
        signIn_GmCerts(
            d->certs,
            findIdentity_GmCerts(d->certs, collect_Block(hexDecode_Rangecc(range_Command(cmd, "ident")))),
            url);
        postCommand_App("navigate.reload");
        postCommand_App("idents.changed");
        return iTrue;
    }
    else if (equal_Command(cmd, "ident.signout")) {
        iGmIdentity *ident = findIdentity_GmCerts(
            d->certs, collect_Block(hexDecode_Rangecc(range_Command(cmd, "ident"))));
        if (arg_Command(cmd)) {
            clearUse_GmIdentity(ident);
        }
        else {
            setUse_GmIdentity(ident, collect_String(suffix_Command(cmd, "url")), iFalse);
        }
        postCommand_App("navigate.reload");
        postCommand_App("idents.changed");
        return iTrue;
    }
    else if (equal_Command(cmd, "os.theme.changed")) {
        const int dark = argLabel_Command(cmd, "dark");
        d->isDarkSystemTheme = dark;
        if (d->prefs.useSystemTheme) {
            const int contrast  = argLabel_Command(cmd, "contrast");
            const int preferred = d->prefs.systemPreferredColorTheme[dark ^ 1];
            postCommandf_App("theme.set arg:%d auto:1",
                             preferred >= 0 ? preferred
                             : dark ? (contrast ? pureBlack_ColorTheme : dark_ColorTheme)
                                            : (contrast ? pureWhite_ColorTheme : light_ColorTheme));
        }
        return iFalse;
    }
    else if (equal_Command(cmd, "updater.check")) {
        checkNow_Updater();
        return iTrue;
    }
    else if (equal_Command(cmd, "fontpack.enable")) {
        const iString *packId = collect_String(suffix_Command(cmd, "id"));
        enablePack_Fonts(packId, arg_Command(cmd));
        postCommand_App("navigate.reload");
        return iTrue;
    }
#if defined (LAGRANGE_ENABLE_IPC)
    else if (equal_Command(cmd, "ipc.list.urls")) {
        iProcessId pid = argLabel_Command(cmd, "pid");
        if (pid) {
            iString *urls = collectNew_String();
            iConstForEach(ObjectList, i, iClob(listDocuments_App(NULL))) {
                append_String(urls, url_DocumentWidget(i.object));
                appendCStr_String(urls, "\n");
            }
            write_Ipc(pid, urls, response_IpcWrite);
        }
        return iTrue;
    }
    else if (equal_Command(cmd, "ipc.active.url")) {
        write_Ipc(argLabel_Command(cmd, "pid"),
                  collectNewFormat_String(
                      "%s\n", d->window ? cstr_String(url_DocumentWidget(document_App())) : ""),
                  response_IpcWrite);
        return iTrue;
    }
    else if (equal_Command(cmd, "ipc.signal")) {
        if (argLabel_Command(cmd, "raise")) {
            if (d->window && d->window->base.win) {
                SDL_RaiseWindow(d->window->base.win);
            }
        }
        signal_Ipc(arg_Command(cmd));
        return iTrue;
    }
#endif /* defined (LAGRANGE_ENABLE_IPC) */
    else if (equal_Command(cmd, "quit")) {
        SDL_Event ev;
        ev.type = SDL_QUIT;
        SDL_PushEvent(&ev);
    }
    return iFalse;
}

iBool handleCommand_App(const char *cmd) {
    iApp *d = &app_;
    const iBool isFrozen   = !d->window || d->window->isDrawFrozen;
    const iBool isHeadless = numWindows_App() == 0;
    if (handleNonWindowRelatedCommand_App_(d, cmd)) {
        return iTrue;
    }
    if (isHeadless) {
        /* All the subsequent commands assume that a window exists. */
        return iFalse;
    }
    /* TODO: Maybe break this up a little bit? There's a very long list of ifs here. */
    if (equal_Command(cmd, "config.error")) {
        makeSimpleMessage_Widget(uiTextCaution_ColorEscape "CONFIG ERROR",
                                 format_CStr("Error in config file: %s\n"
                                             "See \"about:debug\" for details.",
                                             suffixPtr_Command(cmd, "where")));
        return iTrue;
    }
    else if (equal_Command(cmd, "ui.split")) {
        if (argLabel_Command(cmd, "swap")) {
            swapRoots_MainWindow(d->window);
            return iTrue;
        }
        if (argLabel_Command(cmd, "focusother")) {
            iWindow *baseWin = &d->window->base;
            if (baseWin->roots[1]) {
                baseWin->keyRoot =
                    (baseWin->keyRoot == baseWin->roots[1] ? baseWin->roots[0] : baseWin->roots[1]);
            }
        }
        d->window->pendingSplitMode =
            (argLabel_Command(cmd, "axis") ? vertical_WindowSplit : 0) | (arg_Command(cmd) << 1);
        const char *url = suffixPtr_Command(cmd, "url");
        setCStr_String(d->window->pendingSplitUrl, url ? url : "");
        if (hasLabel_Command(cmd, "origin")) {
            set_String(d->window->pendingSplitOrigin, string_Command(cmd, "origin"));
        }
        postRefresh_App();
        return iTrue;
    }
    else if (equal_Command(cmd, "window.maximize")) {
        const size_t winIndex = argU32Label_Command(cmd, "index");
        if (winIndex < size_PtrArray(&d->mainWindows)) {
            iMainWindow *win = at_PtrArray(&d->mainWindows, winIndex);
            if (!argLabel_Command(cmd, "toggle")) {
                setSnap_MainWindow(win, maximized_WindowSnap);
            }
            else {
                setSnap_MainWindow(
                    win, snap_MainWindow(win) == maximized_WindowSnap ? 0 : maximized_WindowSnap);
            }
        }
        return iTrue;
    }
    else if (equal_Command(cmd, "window.fullscreen")) {
        const iBool wasFull = snap_MainWindow(d->window) == fullscreen_WindowSnap;
        setSnap_MainWindow(d->window, wasFull ? 0 : fullscreen_WindowSnap);
        postCommandf_App("window.fullscreen.changed arg:%d", !wasFull);
        return iTrue;
    }
    else if (equal_Command(cmd, "font.reset")) {
        resetFonts_App();
        return iTrue;
    }
    else if (equal_Command(cmd, "font.reload")) {
        reload_Fonts(); /* also does font cache reset, window invalidation */
        return iTrue;
    }
    else if (equal_Command(cmd, "font.find")) {
        searchOnlineLibraryForCharacters_Fonts(string_Command(cmd, "chars"));
        return iTrue;
    }
    else if (equal_Command(cmd, "font.found")) {
        if (hasLabel_Command(cmd, "error")) {
            makeSimpleMessage_Widget("${heading.glyphfinder}",
                                     format_CStr("%d %s",
                                                 argLabel_Command(cmd, "error"),
                                                 suffixPtr_Command(cmd, "msg")));
            return iTrue;
        }
        iString *src = collectNew_String();
        setCStr_String(src, "# ${heading.glyphfinder.results}\n\n");
        iRangecc path = iNullRange;
        iBool isFirst = iTrue;
        while (nextSplit_Rangecc(range_Command(cmd, "packs"), ",", &path)) {
            if (isFirst) {
                appendCStr_String(src, "${glyphfinder.results}\n\n");
            }
            iRangecc fpath = path;
            iRangecc fsize = path;
            fpath.end = strchr(fpath.start, ';');
            fsize.start = fpath.end + 1;
            const uint32_t size = strtoul(fsize.start, NULL, 10);
            appendFormat_String(src, "=> gemini://skyjake.fi/fonts/%s %s (%.1f MB)\n",
                                cstr_Rangecc(fpath),
                                cstr_Rangecc(fpath),
                                (double) size / 1.0e6);
            isFirst = iFalse;
        }
        if (isFirst) {
            appendFormat_String(src, "${glyphfinder.results.empty}\n");
        }
        appendCStr_String(src, "\n=> about:fonts ${menu.fonts}");
        iDocumentWidget *page = newTab_App(NULL, iTrue);
        translate_Lang(src);
        setUrlAndSource_DocumentWidget(page,
                                       collectNewCStr_String(""),
                                       collectNewCStr_String("text/gemini"),
                                       utf8_String(src));
        return iTrue;
    }
    else if (equal_Command(cmd, "zoom.set")) {
        if (!isFrozen) {
            setFreezeDraw_MainWindow(get_MainWindow(), iTrue); /* no intermediate draws before docs updated */
        }
        if (arg_Command(cmd) != d->prefs.zoomPercent) {
            d->prefs.zoomPercent = arg_Command(cmd);
            invalidateCachedDocuments_App_();
        }
        setDocumentFontSize_Text(text_Window(d->window), (float) d->prefs.zoomPercent / 100.0f);
        if (!isFrozen) {
            postCommand_App("font.changed");
            postCommand_App("window.unfreeze");
        }
        return iTrue;
    }
    else if (equal_Command(cmd, "zoom.delta")) {
        if (!isFrozen) {
            setFreezeDraw_MainWindow(get_MainWindow(), iTrue); /* no intermediate draws before docs updated */
        }
        int delta = arg_Command(cmd);
        if (d->prefs.zoomPercent < 100 || (delta < 0 && d->prefs.zoomPercent == 100)) {
            delta /= 2;
        }
        d->prefs.zoomPercent = iClamp(d->prefs.zoomPercent + delta, 50, 200);
        invalidateCachedDocuments_App_();
        setDocumentFontSize_Text(text_Window(d->window), (float) d->prefs.zoomPercent / 100.0f);
        if (!isFrozen) {
            postCommand_App("font.changed");
            postCommand_App("window.unfreeze");
        }
        return iTrue;
    }
    else if (equal_Command(cmd, "hidetoolbarscroll")) {
        d->prefs.hideToolbarOnScroll = arg_Command(cmd);
        if (!d->prefs.hideToolbarOnScroll) {
            showToolbar_Root(get_Root(), iTrue);
        }
        return iTrue;
    }
    else if (equal_Command(cmd, "spartan.input")) {
        const char *value = suffixPtr_Command(cmd, "value");
        iRangecc url = range_Command(cmd, "urlesc");
        postCommand_Widget(
            document_Command(cmd),
            "open newtab:%d newwindow:%d url:%s?%s",
            argLabel_Command(cmd, "newtab"),
            argLabel_Command(cmd, "newwindow"),
            cstr_String(urlQueryStripped_String(collectNewRange_String(url))),
            cstr_String(collect_String(urlEncode_String(collectNewCStr_String(value)))));
        return iTrue;
    }
    else if (equal_Command(cmd, "open")) {
        const char *urlArg = suffixPtr_Command(cmd, "url");
        if (!urlArg) {
            return iTrue; /* invalid command */
        }
        if (argLabel_Command(cmd, "query")) {
            const iString *url = collectNewCStr_String(urlArg);
            iString *spartanCmd = collectNewFormat_String(
                "spartan.input newtab:%d newwindow:%d urlesc:%s",
                argLabel_Command(cmd, "newtab"),
                argLabel_Command(cmd, "newwindow"),
                cstr_String(withSpacesEncoded_String(url)));
            iUrl parts;
            init_Url(&parts, url);
            iWidget *dlg = makeValueInputWithAdditionalActions_Widget(
                as_Widget(document_Command(cmd)),
                NULL,
                cstr_Rangecc((iRangecc){ parts.host.start, parts.path.end }),
                "${spartan.input}",
                "${dlg.input.send}",
                cstr_String(spartanCmd),
                (iMenuItem[]){ { "${dlg.spartan.upload}",
                                 SDLK_u,
                                 KMOD_PRIMARY,
                                 format_CStr("valueinput.upload url:%s",
                                             cstr_String(urlQueryStripped_String(url))) } },
                1);
            setBackupFileName_InputWidget(findChild_Widget(dlg, "input"), "spartanbackup");
            if (!isEmpty_Range(&parts.query)) {
                postCommand_Widget(dlg,
                                   "valueinput.set select:1 text:%s",
                                   cstrCollect_String(urlDecode_String(collectNewRange_String(
                                       (iRangecc){ parts.query.start + 1, parts.query.end }))));
            }
            return iTrue;
        }
        if (argLabel_Command(cmd, "switch")) {
            iDocumentWidget *doc = findDocument_Root(get_Root(), collectNewCStr_String(urlArg));
            if (doc) {
                showTabPage_Widget(findWidget_Root("doctabs"), doc);
                return iTrue;
            }
        }
        if (findWidget_App("prefs")) {
            postCommand_App("prefs.dismiss");
        }
        if (argLabel_Command(cmd, "newwindow")) {
            const iRangecc gotoheading = range_Command(cmd, "gotoheading");
            const iRangecc gotourlheading = range_Command(cmd, "gotourlheading");
            postCommandf_Root(get_Root(), "window.new%s%s%s%s url:%s",
                              isEmpty_Range(&gotoheading) ? "" : " gotoheading:",
                              isEmpty_Range(&gotoheading) ? "" : cstr_Rangecc(gotoheading),
                              isEmpty_Range(&gotourlheading) ? "" : " gotourlheading:",
                              isEmpty_Range(&gotourlheading) ? "" : cstr_Rangecc(gotourlheading),
                              urlArg);
            return iTrue;
        }
        iString    *url     = collectNewCStr_String(urlArg);
        const iBool noProxy = argLabel_Command(cmd, "noproxy") != 0;
        iUrl parts;
        init_Url(&parts, url);
        if (equal_Rangecc(parts.scheme, "about") && equal_Rangecc(parts.path, "command") &&
            !isEmpty_Range(&parts.query)) {
            /* NOTE: Careful here! `about:command` allows issuing UI events via links on the page.
               There is a special set of pages where these are allowed (e.g., "about:fonts").
               On every other page, `about:command` links will not be clickable. */
            iString *query = collectNewRange_String((iRangecc){
                parts.query.start + 1, parts.query.end
            });
            replace_String(query, "%20", " ");
            postCommandString_Root(NULL, query);
            return iTrue;
        }
        if (equalCase_Rangecc(parts.scheme, "titan")) {
            iUploadWidget *upload = new_UploadWidget(titan_UploadProtocol);
            setUrl_UploadWidget(upload, url);
            setResponseViewer_UploadWidget(upload, document_App());
            addChild_Widget(get_Root()->widget, iClob(upload));
            setupSheetTransition_Mobile(as_Widget(upload), iTrue);
            postRefresh_App();
            return iTrue;
        }
        if (argLabel_Command(cmd, "default") || equalCase_Rangecc(parts.scheme, "mailto") ||
            ((noProxy || isEmpty_String(&d->prefs.strings[httpProxy_PrefsString])) &&
             (equalCase_Rangecc(parts.scheme, "http") ||
              equalCase_Rangecc(parts.scheme, "https")))) {
            openInDefaultBrowser_App(url);
            return iTrue;
        }
        iDocumentWidget *doc = document_Command(cmd);
        iAssert(doc);
        iDocumentWidget *origin = doc;
        if (hasLabel_Command(cmd, "origin")) {
            iDocumentWidget *cmdOrig = findWidget_App(cstr_Command(cmd, "origin"));
            if (cmdOrig) {
                origin = cmdOrig;
            }
        }
        const int newTab = argLabel_Command(cmd, "newtab");
        if (newTab & otherRoot_OpenTabFlag && numRoots_Window(get_Window()) == 1) {
            /* Need to split first. */
            const iInt2 winSize = get_Window()->size;
            const int splitMode = argLabel_Command(cmd, "splitmode");
            postCommandf_App("ui.split arg:%d axis:%d origin:%s newtab:%d url:%s",
                             splitMode ? splitMode : 3,
                             (float) winSize.x / (float) winSize.y < 0.7f ? 1 : 0,
                             cstr_String(id_Widget(as_Widget(origin))),
                             newTab & ~otherRoot_OpenTabFlag,
                             cstr_String(url));
            return iTrue;
        }
        iRoot *root = get_Root();
        iRoot *oldRoot = root;
        if (newTab & otherRoot_OpenTabFlag) {
            root = otherRoot_Window(as_Window(d->window), root);
            setKeyRoot_Window(as_Window(d->window), root);
            setCurrent_Root(root); /* need to change for widget creation */
            doc = document_Command(cmd); /* may be different */
        }
        if (newTab & (new_OpenTabFlag | newBackground_OpenTabFlag)) {
            doc = newTab_App(NULL, (newTab & new_OpenTabFlag) != 0); /* `newtab:2` to open in background */
        }
        iHistory   *history       = history_DocumentWidget(doc);
        const iBool isHistory     = argLabel_Command(cmd, "history") != 0;
        int         redirectCount = argLabel_Command(cmd, "redirect");
        if (!isHistory) {
            /* TODO: Shouldn't DocumentWidget manage history on its own? */
            if (redirectCount) {
                replace_History(history, url);
            }
            else {
                add_History(history, url);
            }
        }
        setInitialScroll_DocumentWidget(doc, argfLabel_Command(cmd, "scroll"));
        setRedirectCount_DocumentWidget(doc, redirectCount);
        setOrigin_DocumentWidget(doc, origin);
        showCollapsed_Widget(findWidget_App("document.progress"), iFalse);
        setUrlFlags_DocumentWidget(
            doc,
            url,
            (isHistory ? useCachedContentIfAvailable_DocumentWidgetSetUrlFlag : 0) |
                (argLabel_Command(cmd, "notinline") ? preventInlining_DocumentWidgetSetUrlFlag
                                                    : 0));
        /* Optionally, jump to a text in the document. This will only work if the document
           is already available, e.g., it's from "about:" or restored from cache. */
        const iRangecc gotoHeading = range_Command(cmd, "gotoheading");
        if (gotoHeading.start) {
            postCommandf_Root(root, "document.goto heading:%s", cstr_Rangecc(gotoHeading));
        }
        const iRangecc gotoUrlHeading = range_Command(cmd, "gotourlheading");
        if (gotoUrlHeading.start) {
            postCommandf_Root(root, "document.goto heading:%s",
                             cstrCollect_String(urlDecode_String(
                                 collect_String(newRange_String(gotoUrlHeading)))));
        }
        setCurrent_Root(oldRoot);
    }
    else if (equal_Command(cmd, "file.open")) {
        const char *path = suffixPtr_Command(cmd, "path");
        if (path) {
            postCommandf_App("open temp:%d url:%s",
                             argLabel_Command(cmd, "temp"),
                             makeFileUrl_CStr(path));
            return iTrue;
        }
#if defined (iPlatformAppleMobile)
        pickFile_iOS("file.open");
#endif
#if defined (iPlatformAndroidMobile)
        pickFile_Android("file.open");
#endif
        return iTrue;
    }
    else if (equal_Command(cmd, "file.delete")) {
        const char *path = suffixPtr_Command(cmd, "path");
        if (argLabel_Command(cmd, "confirm")) {
            makeQuestion_Widget(
                uiHeading_ColorEscape "${heading.file.delete}",
                format_CStr("${dlg.file.delete.confirm}\n%s", path),
                (iMenuItem[]){
                    { "${cancel}", 0, 0, NULL },
                    { uiTextCaution_ColorEscape "${dlg.file.delete}", 0, 0,
                      format_CStr("!file.delete path:%s", path) } },
                2);
        }
        else {
            remove(path);
        }
        return iTrue;
    }
    else if (equal_Command(cmd, "document.request.cancelled")) {
        /* TODO: How should cancelled requests be treated in the history? */
#if 0
        if (d->historyPos == 0) {
            iHistoryItem *item = historyItem_App_(d, 0);
            if (item) {
                /* Pop this cancelled URL off history. */
                deinit_HistoryItem(item);
                popBack_Array(&d->history);
                printHistory_App_(d);
            }
        }
#endif
        return iFalse;
    }
    else if (equal_Command(cmd, "tabs.new")) {
        if (argLabel_Command(cmd, "reopen")) {
            const iString *reopenUrl = popClosedTabUrl_App_(d);
            if (reopenUrl) {
                newTab_App(NULL, iTrue);
                postCommandf_App("open url:%s", cstr_String(reopenUrl));
            }
            return iTrue;
        }
        const iBool isDuplicate = argLabel_Command(cmd, "duplicate") != 0;
        newTab_App(isDuplicate ? document_App() : NULL, iTrue);        
        if (!isDuplicate) {
            postCommandf_App("navigate.home focus:%d", deviceType_App() == desktop_AppDeviceType);
        }
        return iTrue;
    }
    else if (equal_Command(cmd, "tabs.close")) {
        iWidget *tabs = findWidget_App("doctabs");
        /* Can't close the last tab on mobile. */
        if (isMobile_Platform() && tabCount_Widget(tabs) == 1 && numRoots_Window(get_Window()) == 1) {
            postCommand_App("navigate.home");
            return iTrue;
        }
        const iRangecc tabId = range_Command(cmd, "id");
        iWidget *      doc   = !isEmpty_Range(&tabId) ? findWidget_App(cstr_Rangecc(tabId))
                                                      : document_App();
        iBool  wasCurrent = (doc == (iWidget *) document_App());
        size_t index      = tabPageIndex_Widget(tabs, doc);
        iBool  wasClosed  = iFalse;
        postCommand_App("document.openurls.changed");
        if (argLabel_Command(cmd, "toright")) {
            while (tabCount_Widget(tabs) > index + 1) {
                iDocumentWidget *closed = (iDocumentWidget *) removeTabPage_Widget(tabs, index + 1);
                pushClosedTabUrl_App_(d, url_DocumentWidget(closed));
                cancelAllRequests_DocumentWidget(closed);
                destroy_Widget(as_Widget(closed));
            }
            wasClosed = iTrue;
        }
        if (argLabel_Command(cmd, "toleft")) {
            while (index-- > 0) {
                iDocumentWidget *closed = (iDocumentWidget *) removeTabPage_Widget(tabs, 0);
                pushClosedTabUrl_App_(d, url_DocumentWidget(closed));
                cancelAllRequests_DocumentWidget(closed);
                destroy_Widget(as_Widget(closed));
            }
            postCommandf_App("tabs.switch page:%p", tabPage_Widget(tabs, 0));
            wasClosed = iTrue;
        }
        if (wasClosed) {
            arrange_Widget(tabs);
            return iTrue;
        }
        const iBool isSplit = numRoots_Window(get_Window()) > 1;
        if (tabCount_Widget(tabs) > 1 || isSplit) {
            iDocumentWidget *closed = (iDocumentWidget *) removeTabPage_Widget(tabs, index);
            pushClosedTabUrl_App_(d, url_DocumentWidget(closed));
            cancelAllRequests_DocumentWidget(closed);
            destroy_Widget(as_Widget(closed)); /* released later */
            if (index == tabCount_Widget(tabs)) {
                index--;
            }
            if (tabCount_Widget(tabs) == 0) {
                iAssert(isSplit);
                postCommand_App("ui.split arg:0");
            }
            else {
                arrange_Widget(tabs);
                if (wasCurrent) {
                    postCommandf_App("tabs.switch page:%p", tabPage_Widget(tabs, index));
                }
            }
        }
#if defined (iPlatformAppleDesktop)
        else {
            closeWindow_App(d->window);
        }
#else
        else if (numWindows_App() > 1) {
            closeWindow_App(d->window);
        }
        else {
            postCommand_App("quit");
        }
#endif
        return iTrue;
    }
    else if (equal_Command(cmd, "keyroot.next")) {
        if (setKeyRoot_Window(as_Window(d->window),
                              otherRoot_Window(as_Window(d->window), d->window->base.keyRoot))) {
            setFocus_Widget(NULL);
        }
        return iTrue;
    }
    else if (equal_Command(cmd, "preferences")) {
        iWidget *dlg = makePreferences_Widget();
        updatePrefsThemeButtons_(dlg);
        setText_InputWidget(findChild_Widget(dlg, "prefs.downloads"), &d->prefs.strings[downloadDir_PrefsString]);
        /* TODO: Use a common table in Prefs to do this more conviently.
           Also see `serializePrefs_App_()`. */
        setToggle_Widget(findChild_Widget(dlg, "prefs.hoverlink"), d->prefs.hoverLink);
        setToggle_Widget(findChild_Widget(dlg, "prefs.retaintabs"), d->prefs.retainTabs);
        setToggle_Widget(findChild_Widget(dlg, "prefs.smoothscroll"), d->prefs.smoothScrolling);
        setToggle_Widget(findChild_Widget(dlg, "prefs.imageloadscroll"), d->prefs.loadImageInsteadOfScrolling);
        setToggle_Widget(findChild_Widget(dlg, "prefs.hidetoolbarscroll"), d->prefs.hideToolbarOnScroll);
        setToggle_Widget(findChild_Widget(dlg, "prefs.bookmarks.addbottom"), d->prefs.addBookmarksToBottom);
        setToggle_Widget(findChild_Widget(dlg, "prefs.font.warnmissing"), d->prefs.warnAboutMissingGlyphs);
        setToggle_Widget(findChild_Widget(dlg, "prefs.dataurl.openimages"), d->prefs.openDataUrlImagesOnLoad);
        setToggle_Widget(findChild_Widget(dlg, "prefs.archive.openindex"), d->prefs.openArchiveIndexPages);
        setToggle_Widget(findChild_Widget(dlg, "prefs.markdown.viewsource"), d->prefs.markdownAsSource);
        setToggle_Widget(findChild_Widget(dlg, "prefs.ostheme"), d->prefs.useSystemTheme);
        setToggle_Widget(findChild_Widget(dlg, "prefs.customframe"), d->prefs.customFrame);
        setToggle_Widget(findChild_Widget(dlg, "prefs.animate"), d->prefs.uiAnimations);
        setToggle_Widget(findChild_Widget(dlg, "prefs.bottomnavbar"), d->prefs.bottomNavBar);
        setToggle_Widget(findChild_Widget(dlg, "prefs.bottomtabbar"), d->prefs.bottomTabBar);
        setToggle_Widget(findChild_Widget(dlg, "prefs.menubar"), d->prefs.menuBar);
        setToggle_Widget(findChild_Widget(dlg, "prefs.blink"), d->prefs.blinkingCursor);
        setToggle_Widget(findChild_Widget(dlg, "prefs.evensplit"), d->prefs.evenSplit);
        updatePrefsPinSplitButtons_(dlg, d->prefs.pinSplit);
        updateScrollSpeedButtons_(dlg, mouse_ScrollType, d->prefs.smoothScrollSpeed[mouse_ScrollType]);
        updateScrollSpeedButtons_(dlg, keyboard_ScrollType, d->prefs.smoothScrollSpeed[keyboard_ScrollType]);
        updateDropdownSelection_LabelWidget(findChild_Widget(dlg, "prefs.uilang"), cstr_String(&d->prefs.strings[uiLanguage_PrefsString]));
        setToggle_Widget(findChild_Widget(dlg, "prefs.time.24h"), d->prefs.time24h);
        updateDropdownSelection_LabelWidget(
            findChild_Widget(dlg, "prefs.returnkey"),
            format_CStr("returnkey.set arg:%d", d->prefs.returnKey));
        updatePrefsToolBarActionButton_(dlg, 0, d->prefs.toolbarActions[0]);
        updatePrefsToolBarActionButton_(dlg, 1, d->prefs.toolbarActions[1]);
        setToggle_Widget(findChild_Widget(dlg, "prefs.retainwindow"), d->prefs.retainWindowSize);
        setText_InputWidget(findChild_Widget(dlg, "prefs.uiscale"),
                            collectNewFormat_String("%g", uiScale_Window(as_Window(d->window))));
        setFlags_Widget(findChild_Widget(dlg, "prefs.mono.gemini"),
                        selected_WidgetFlag,
                        d->prefs.monospaceGemini);
        setFlags_Widget(findChild_Widget(dlg, "prefs.mono.gopher"),
                        selected_WidgetFlag,
                        d->prefs.monospaceGopher);
        setFlags_Widget(findChild_Widget(dlg, "prefs.boldlink.visited"),
                        selected_WidgetFlag,
                        d->prefs.boldLinkVisited);
        setFlags_Widget(findChild_Widget(dlg, "prefs.boldlink.dark"),
                        selected_WidgetFlag,
                        d->prefs.boldLinkDark);
        setFlags_Widget(findChild_Widget(dlg, "prefs.boldlink.light"),
                        selected_WidgetFlag,
                        d->prefs.boldLinkLight);
        setToggle_Widget(findChild_Widget(dlg, "prefs.gemtext.ansi.fg"),
                         d->prefs.gemtextAnsiEscapes & allowFg_AnsiFlag);
        setToggle_Widget(findChild_Widget(dlg, "prefs.gemtext.ansi.bg"),
                         d->prefs.gemtextAnsiEscapes & allowBg_AnsiFlag);
        setToggle_Widget(findChild_Widget(dlg, "prefs.gemtext.ansi.fontstyle"),
                         d->prefs.gemtextAnsiEscapes & allowFontStyle_AnsiFlag);
        setToggle_Widget(findChild_Widget(dlg, "prefs.font.smooth"), d->prefs.fontSmoothing);
        setToggle_Widget(findChild_Widget(dlg, "prefs.tui.simple"), d->prefs.simpleChars);
        setFlags_Widget(
            findChild_Widget(dlg, format_CStr("prefs.linewidth.%d", d->prefs.lineWidth)),
            selected_WidgetFlag,
            iTrue);
        setText_InputWidget(findChild_Widget(dlg, "prefs.linespacing"),
                            collectNewFormat_String("%.2f", d->prefs.lineSpacing));
        setText_InputWidget(findChild_Widget(dlg, "prefs.tabwidth"),
                            collectNewFormat_String("%d", d->prefs.tabWidth));
        setFlags_Widget(
            findChild_Widget(dlg, format_CStr("prefs.quoteicon.%d", d->prefs.quoteIcon)),
            selected_WidgetFlag,
            iTrue);
        setToggle_Widget(findChild_Widget(dlg, "prefs.biglede"), d->prefs.bigFirstParagraph);
        setToggle_Widget(findChild_Widget(dlg, "prefs.justify"), d->prefs.justifyParagraph);
        setToggle_Widget(findChild_Widget(dlg, "prefs.plaintext.wrap"), d->prefs.plainTextWrap);
        setToggle_Widget(findChild_Widget(dlg, "prefs.sideicon"), d->prefs.sideIcon);
        setToggle_Widget(findChild_Widget(dlg, "prefs.centershort"), d->prefs.centerShortDocs);
        setToggle_Widget(findChild_Widget(dlg, "prefs.collapsepreonload"), d->prefs.collapsePreOnLoad);
        updateColorThemeButton_(findChild_Widget(dlg, "prefs.doctheme.dark"), d->prefs.docThemeDark);
        updateColorThemeButton_(findChild_Widget(dlg, "prefs.doctheme.light"), d->prefs.docThemeLight);
        updateImageStyleButton_(findChild_Widget(dlg, "prefs.imagestyle"), d->prefs.imageStyle);
        updateFontButton_(findChild_Widget(dlg, "prefs.font.ui"),      &d->prefs.strings[uiFont_PrefsString]);
        updateFontButton_(findChild_Widget(dlg, "prefs.font.heading"), &d->prefs.strings[headingFont_PrefsString]);
        updateFontButton_(findChild_Widget(dlg, "prefs.font.body"),    &d->prefs.strings[bodyFont_PrefsString]);
        updateFontButton_(findChild_Widget(dlg, "prefs.font.mono"),    &d->prefs.strings[monospaceFont_PrefsString]);
        updateFontButton_(findChild_Widget(dlg, "prefs.font.monodoc"), &d->prefs.strings[monospaceDocumentFont_PrefsString]);
        setFlags_Widget(
            findChild_Widget(
                dlg, format_CStr("prefs.saturation.%d", (int) (d->prefs.saturation * 3.99f))),
            selected_WidgetFlag,
            iTrue);
        setText_InputWidget(findChild_Widget(dlg, "prefs.cachesize"),
                            collectNewFormat_String("%d", d->prefs.maxCacheSize));
        setText_InputWidget(findChild_Widget(dlg, "prefs.memorysize"),
                            collectNewFormat_String("%d", d->prefs.maxMemorySize));
        setText_InputWidget(findChild_Widget(dlg, "prefs.urlsize"),
                            collectNewFormat_String("%d", d->prefs.maxUrlSize));
        setToggle_Widget(findChild_Widget(dlg, "prefs.decodeurls"), d->prefs.decodeUserVisibleURLs);
        setText_InputWidget(findChild_Widget(dlg, "prefs.searchurl"), &d->prefs.strings[searchUrl_PrefsString]);
        setText_InputWidget(findChild_Widget(dlg, "prefs.ca.file"), &d->prefs.strings[caFile_PrefsString]);
        setText_InputWidget(findChild_Widget(dlg, "prefs.ca.path"), &d->prefs.strings[caPath_PrefsString]);
        setText_InputWidget(findChild_Widget(dlg, "prefs.proxy.gemini"), &d->prefs.strings[geminiProxy_PrefsString]);
        setText_InputWidget(findChild_Widget(dlg, "prefs.proxy.gopher"), &d->prefs.strings[gopherProxy_PrefsString]);
        setText_InputWidget(findChild_Widget(dlg, "prefs.proxy.http"), &d->prefs.strings[httpProxy_PrefsString]);
        iWidget *tabs = findChild_Widget(dlg, "prefs.tabs");
        if (tabs) {
            showTabPage_Widget(tabs, tabPage_Widget(tabs, d->prefs.dialogTab));
        }
        setCommandHandler_Widget(dlg, handlePrefsCommands_);
        if (argLabel_Command(cmd, "idents") && deviceType_App() != desktop_AppDeviceType) {
            iWidget *idPanel = panel_Mobile(dlg, 3);
            iWidget *button  = findUserData_Widget(findChild_Widget(dlg, "panel.top"), idPanel);
            postCommand_Widget(button, "panel.open");
        }
    }
    else if (equal_Command(cmd, "navigate.home")) {
        /* Look for bookmarks tagged "homepage". */
        const iPtrArray *homepages =
            list_Bookmarks(d->bookmarks, NULL, filterHomepage_Bookmark, NULL);
        if (isEmpty_PtrArray(homepages)) {
            postCommand_Root(get_Root(), "open url:about:lagrange");
        }
        else {
            iStringSet *urls = iClob(new_StringSet());
            iConstForEach(PtrArray, i, homepages) {
                const iBookmark *bm = i.ptr;
                /* Try to switch to a different bookmark. */
                if (cmpStringCase_String(url_DocumentWidget(document_App()), &bm->url)) {
                    insert_StringSet(urls, &bm->url);
                }
            }
            if (!isEmpty_StringSet(urls)) {
                postCommandf_Root(get_Root(),
                    "open url:%s",
                    cstr_String(constAt_StringSet(urls, iRandoms(0, size_StringSet(urls)))));
            }
        }
        if (argLabel_Command(cmd, "focus")) {
            postCommand_Root(get_Root(), "navigate.focus");
        }
        return iTrue;
    }
    else if (equal_Command(cmd, "bookmark.add")) {
        if (findWidget_Root("bmed.create")) {
            return iTrue;
        }
        iDocumentWidget *doc = document_App();
        const iString *url;
        const iString *title;
        iChar icon = 0;
        if (suffixPtr_Command(cmd, "url")) {
            url          = collect_String(suffix_Command(cmd, "url"));
            iString *str = newRange_String(range_Command(cmd, "title"));
            replace_String(str, "%20", " ");
            title = collect_String(str);
        }
        else {
            url   = url_DocumentWidget(doc);
            title = bookmarkTitle_DocumentWidget(doc);
//            icon  = 0; //siteIcon_GmDocument(document_DocumentWidget(doc));
        }
        const uint32_t existing = findUrl_Bookmarks(bookmarks_App(), url);
        if (existing) {
            /* Editing bookmarks is a sidebar command. */
            postCommand_Widget(findWidget_App("sidebar"), "bookmark.edit id:%u", existing);
            return iTrue;
        }
        makeBookmarkCreation_Widget(url, title, icon);
        if (deviceType_App() == desktop_AppDeviceType) {
            postCommand_App("focus.set id:bmed.title");
        }
        return iTrue;
    }
    else if (equal_Command(cmd, "feeds.subscribe")) {
        const iString *url = url_DocumentWidget(document_App());
        if (isEmpty_String(url)) {
            return iTrue;
        }
        makeFeedSettings_Widget(findUrl_Bookmarks(d->bookmarks, url));
        return iTrue;
    }
    else if (equal_Command(cmd, "bookmarks.addfolder")) {
        const int parentId = argLabel_Command(cmd, "parent");
        if (suffixPtr_Command(cmd, "value")) {
            uint32_t id = add_Bookmarks(d->bookmarks, NULL,
                                        collect_String(suffix_Command(cmd, "value")), NULL, 0);
            if (parentId) {
                get_Bookmarks(d->bookmarks, id)->parentId = parentId;
            }
            postCommandf_App("bookmarks.changed added:%zu", id);
            setRecentFolder_Bookmarks(d->bookmarks, id);
        }
        else {
            iWidget *dlg = makeValueInput_Widget(
                get_Root()->widget, collectNewCStr_String(cstr_Lang("dlg.addfolder.defaulttitle")),
                uiHeading_ColorEscape "${heading.addfolder}", "${dlg.addfolder.prompt}",
                uiTextAction_ColorEscape "${dlg.addfolder}",
                format_CStr("bookmarks.addfolder parent:%d", parentId));
            setSelectAllOnFocus_InputWidget(findChild_Widget(dlg, "input"), iTrue);
        }
        return iTrue;
    }
    else if (startsWith_CStr(cmd, "feeds.update.")) {
        const iWidget *navBar = findChild_Widget(get_Window()->roots[0]->widget, "navbar");
        iAnyObject *prog = findChild_Widget(navBar, "feeds.progress");
        if (equal_Command(cmd, "feeds.update.started") ||
            equal_Command(cmd, "feeds.update.progress")) {
            const int num   = arg_Command(cmd);
            const int total = argLabel_Command(cmd, "total");
            updateTextAndResizeWidthCStr_LabelWidget(prog,
                                                     flags_Widget(navBar) & tight_WidgetFlag ||
                                                             deviceType_App() == phone_AppDeviceType
                                                         ? star_Icon
                                                         : star_Icon " ${status.feeds}");
            showCollapsed_Widget(prog, iTrue);
            setFixedSize_Widget(findChild_Widget(prog, "feeds.progressbar"),
                                init_I2(total ? width_Widget(prog) * num / total : 0, -1));
        }
        else if (equal_Command(cmd, "feeds.update.finished")) {
            showCollapsed_Widget(prog, iFalse);
            refreshFinished_Feeds();
            refresh_Widget(findWidget_App("url"));
            return iFalse;
        }
        return iFalse;
    }
    else if (equal_Command(cmd, "document.changed")) {
        /* Set of open tabs has changed. */
        postCommand_App("document.openurls.changed");
        if (deviceType_App() == phone_AppDeviceType) {
            showToolbar_Root(d->window->base.roots[0], iTrue);
        }
        return iFalse;
    }
    else if (equal_Command(cmd, "ident.new")) {
        iWidget *dlg = makeIdentityCreation_Widget();
        setFocus_Widget(findChild_Widget(dlg, "ident.until"));
        setCommandHandler_Widget(dlg, handleIdentityCreationCommands_);
        return iTrue;
    }
    else if (equal_Command(cmd, "ident.import")) {
        iCertImportWidget *imp = new_CertImportWidget();
        setPageContent_CertImportWidget(imp, sourceContent_DocumentWidget(document_App()));
        addChild_Widget(get_Root()->widget, iClob(imp));
        arrange_Widget(as_Widget(imp));
        setupSheetTransition_Mobile(as_Widget(imp), incoming_TransitionFlag |
                                    dialogTransitionDir_Widget(as_Widget(imp)));
        postRefresh_App();
        return iTrue;
    }
    else if (equal_Command(cmd, "ident.switch")) {
        /* This is different than "ident.signin" in that the currently used identity's activation
           URL is used instead of the current one. */
        const iString     *docUrl = url_DocumentWidget(document_App());
        const iGmIdentity *cur    = identityForUrl_GmCerts(d->certs, docUrl);
        iGmIdentity       *dst    = findIdentity_GmCerts(
            d->certs, collect_Block(hexDecode_Rangecc(range_Command(cmd, "fp"))));
        if (dst && cur != dst) {
            iString *useUrl = copy_String(findUse_GmIdentity(cur, docUrl));
            if (isEmpty_String(useUrl)) {
                useUrl = copy_String(docUrl);
            }
            signIn_GmCerts(d->certs, dst, useUrl);
            postCommand_App("idents.changed");
            postCommand_App("navigate.reload");
            delete_String(useUrl);
        }
        return iTrue;
    }
    else if (equal_Command(cmd, "fontpack.delete")) {
        const iString *packId = collect_String(suffix_Command(cmd, "id"));
        if (isEmpty_String(packId)) {
            return iTrue;
        }
        const iFontPack *pack = pack_Fonts(cstr_String(packId));
        if (pack && loadPath_FontPack(pack)) {
            if (argLabel_Command(cmd, "confirmed")) {
                remove_StringSet(d->prefs.disabledFontPacks, packId);
                remove(cstr_String(loadPath_FontPack(pack)));
                reload_Fonts();
                postCommand_App("navigate.reload");
            }
            else {
                makeQuestion_Widget(
                    uiTextCaution_ColorEscape "${heading.fontpack.delete}",
                    format_Lang("${dlg.fontpack.delete.confirm}",
                                cstr_String(packId)),
                    (iMenuItem[]){ { "${cancel}" },
                                   { uiTextAction_ColorEscape " ${dlg.fontpack.delete}",
                                     0,
                                     0,
                                     format_CStr("!fontpack.delete confirmed:1 id:%s",
                                                 cstr_String(packId)) } },
                    2);
            }
        }
        return iTrue;
    }
    else if (equal_Command(cmd, "export")) {
        iExport *export = new_Export();
        iBuffer *zip    = new_Buffer();
        generate_Export(export);
        openEmpty_Buffer(zip);
        serialize_Archive(archive_Export(export), stream_Buffer(zip));
        iDocumentWidget *expTab = newTab_App(NULL, iTrue);
        iDate now;
        initCurrent_Date(&now);
        setUrlAndSource_DocumentWidget(
            expTab,
            collect_String(format_Date(&now, "file:Lagrange User Data %Y-%m-%d %H%M%S.zip")),
            collectNewCStr_String("application/zip"),
            data_Buffer(zip));
        iRelease(zip);
        delete_Export(export);
#if defined (iPlatformAppleMobile) || defined (iPlatformAndroidMobile)
        /* Straight to the save sheet. */
        postCommand_App("document.save");
#endif
        return iTrue;
    }
    else if (equal_Command(cmd, "import")) {
        const iString *path = collect_String(suffix_Command(cmd, "path"));
        iArchive *zip = iClob(new_Archive());
        if (openFile_Archive(zip, path)) {
            if (!arg_Command(cmd)) {
                makeUserDataImporter_Dialog(path);
                return iTrue;
            }
            const int bookmarks = argLabel_Command(cmd, "bookmarks");
            const int trusted   = argLabel_Command(cmd, "trusted");
            const int idents    = argLabel_Command(cmd, "idents");
            const int visited   = argLabel_Command(cmd, "visited");
            const int siteSpec  = argLabel_Command(cmd, "sitespec");
            iExport *export = new_Export();
            if (load_Export(export, zip)) {
                import_Export(export, bookmarks, idents, trusted, visited, siteSpec);
            }
            else {
                makeSimpleMessage_Widget(uiHeading_ColorEscape "${heading.import.userdata.error}",
                                         format_Lang("${import.userdata.error}", cstr_String(path)));                    
            }
            delete_Export(export);
        }
        else {
            makeSimpleMessage_Widget(uiHeading_ColorEscape "${heading.import.userdata.error}",
                                     format_Lang("${import.userdata.error}", cstr_String(path)));
        }
        return iTrue;
    }
    else {
        return iFalse;
    }
    return iTrue;
}

void openInDefaultBrowser_App(const iString *url) {
    /* URL cleanup: at least Firefox seems to want backslashes to be encoded. */ {
        iString *copy = copy_String(url);
        replace_String(copy, "\\", "%5C");
        url = collect_String(copy);
    }
#if SDL_VERSION_ATLEAST(2, 0, 14)
    if (SDL_OpenURL(cstr_String(url)) == 0) {
        return;
    }
#endif
#if defined (iPlatformAppleMobile)
    if (equalCase_Rangecc(urlScheme_String(url), "file")) {
        revealPath_App(collect_String(localFilePathFromUrl_String(url)));
    }
    return;
#endif
    iProcess *proc = new_Process();
    setArguments_Process(proc, iClob(newStringsCStr_StringList(
#if defined (iPlatformAppleDesktop)
        "/usr/bin/env",
        "open",
        cstr_String(url),
#elif defined (iPlatformLinux) || defined (iPlatformOther) || defined (iPlatformHaiku)
        "/usr/bin/env",
        "xdg-open",
        cstr_String(url),
#elif defined (iPlatformMsys)
        concatPath_CStr(cstr_String(execPath_App()), "../urlopen.bat"),
        cstr_String(url),
        /* TODO: The prompt window is shown momentarily... */
#endif
        NULL))
    );
    start_Process(proc);
    waitForFinished_Process(proc);
    iRelease(proc);
}

#include <the_Foundation/thread.h>

void revealPath_App(const iString *path) {
#if defined (iPlatformAppleDesktop)
    iProcess *proc = new_Process();
    setArguments_Process(
        proc, iClob(newStringsCStr_StringList("/usr/bin/open", "-R", cstr_String(path), NULL)));
    start_Process(proc);
    iRelease(proc);
#elif defined (iPlatformAppleMobile)
    /* Use a share sheet. */
    openFileActivityView_iOS(path);
#elif defined (iPlatformLinux) || defined (iPlatformHaiku)
    iProcess *proc = NULL;
    /* Try with `dbus-send` first. */ {
        proc = new_Process();
        setArguments_Process(
            proc,
            iClob(newStringsCStr_StringList(
                "/usr/bin/dbus-send",
                "--print-reply",
                "--dest=org.freedesktop.FileManager1",
                "/org/freedesktop/FileManager1",
                "org.freedesktop.FileManager1.ShowItems",
                format_CStr("array:string:%s", makeFileUrl_CStr(cstr_String(path))),
                "string:",
                NULL)));
        start_Process(proc);
        waitForFinished_Process(proc);
        const iBool dbusDidSucceed = (exitStatus_Process(proc) == 0);
        iRelease(proc);
        if (dbusDidSucceed) {
            return;
        }
    }
    iFileInfo *inf = iClob(new_FileInfo(path));
    iRangecc target;
    if (isDirectory_FileInfo(inf)) {
        target = range_String(path);
    }
    else {
        target = dirName_Path(path);
    }
    proc = new_Process();
    setArguments_Process(
        proc, iClob(newStringsCStr_StringList("/usr/bin/env", "xdg-open", cstr_Rangecc(target), NULL)));
    start_Process(proc);
    iRelease(proc);
#else
    iAssert(0 /* File revealing not implemented on this platform */);
#endif
}

iObjectList *listDocuments_App(const iRoot *rootOrNull) {
    return listDocuments_MainWindow(get_MainWindow(), rootOrNull);
}

iStringSet *listOpenURLs_App(void) {
    iStringSet *set = new_StringSet();
    iObjectList *docs = listDocuments_App(NULL);
    iConstForEach(ObjectList, i, docs) {
        insert_StringSet(set, canonicalUrl_String(url_DocumentWidget(i.object)));
    }
    iRelease(docs);
    return set;
}

iMainWindow *mainWindow_App(void) {
    return app_.window;
}

void closePopups_App(iBool doForce) {
    iApp *d = &app_;
    const uint32_t now = SDL_GetTicks();
    iForEach(PtrArray, i, &d->popupWindows) {
        iWindow *win = i.ptr;
//        if (doForce) {
//            collect_Garbage(win, (iDeleteFunc) delete_Window);
//        }
//        else
            if (now - win->focusGainedAt > 200) {
            postCommand_Root(((const iWindow *) i.ptr)->roots[0], "cancel");
        }
    }
}
