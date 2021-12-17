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
#include "resources.h"
#include "feeds.h"
#include "mimehooks.h"
#include "gmcerts.h"
#include "gmdocument.h"
#include "gmutil.h"
#include "history.h"
#include "ipc.h"
#include "periodic.h"
#include "sitespec.h"
#include "updater.h"
#include "ui/certimportwidget.h"
#include "ui/color.h"
#include "ui/command.h"
#include "ui/documentwidget.h"
#include "ui/inputwidget.h"
#include "ui/keys.h"
#include "ui/labelwidget.h"
#include "ui/root.h"
#include "ui/sidebarwidget.h"
#include "ui/uploadwidget.h"
#include "ui/text.h"
#include "ui/util.h"
#include "ui/window.h"
#include "visited.h"

#include <the_Foundation/commandline.h>
#include <the_Foundation/file.h>
#include <the_Foundation/fileinfo.h>
#include <the_Foundation/path.h>
#include <the_Foundation/process.h>
#include <the_Foundation/sortedarray.h>
#include <the_Foundation/time.h>
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
#if defined (iPlatformLinux) || defined (iPlatformOther)
#define EMB_BIN  "../../share/lagrange/resources.lgr"
#define EMB_BIN2  "../../../share/lagrange/resources.lgr"
static const char *defaultDataDir_App_ = "~/.config/lagrange";
#endif
#if defined (iPlatformHaiku)
#define EMB_BIN "./resources.lgr"
static const char *defaultDataDir_App_ = "~/config/settings/lagrange";
#endif
#define EMB_BIN_EXEC "../resources.lgr" /* fallback from build/executable dir */
static const char *prefsFileName_App_      = "prefs.cfg";
static const char *oldStateFileName_App_   = "state.binary";
static const char *stateFileName_App_      = "state.lgr";
static const char *tempStateFileName_App_  = "state.lgr.tmp";
static const char *defaultDownloadDir_App_ = "~/Downloads";

static const int idleThreshold_App_ = 1000; /* ms */

struct Impl_App {
    iCommandLine args;
    iString *    execPath;
    iMimeHooks * mimehooks;
    iGmCerts *   certs;
    iVisited *   visited;
    iBookmarks * bookmarks;    
    iMainWindow *window;
    iPtrArray    popupWindows;
    iSortedArray tickers; /* per-frame callbacks, used for animations */
    uint32_t     lastTickerTime;
    uint32_t     elapsedSinceLastTicker;
    iBool        isRunning;
    iBool        isRunningUnderWindowSystem;
    iBool        isDarkSystemTheme;
#if defined (LAGRANGE_ENABLE_IDLE_SLEEP)
    iBool        isIdling;
    uint32_t     lastEventTime;
    int          sleepTimer;
#endif
    iAtomicInt   pendingRefresh;
    iBool        isLoadingPrefs;
    iStringList *launchCommands;
    iBool        isFinishedLaunching;
    iTime        lastDropTime; /* for detecting drops of multiple items */
    int          autoReloadTimer;
    iPeriodic    periodic;
    int          warmupFrames; /* forced refresh just after resuming from background; FIXME: shouldn't be needed */
    /* Preferences: */
    iBool        commandEcho;         /* --echo */
    iBool        forceSoftwareRender; /* --sw */
    iRect        initialWindowRect;
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
        x = d->window->place.normalRect.pos.x;
        y = d->window->place.normalRect.pos.y;
        w = d->window->place.normalRect.size.x;
        h = d->window->place.normalRect.size.y;
        appendFormat_String(str, "window.setrect width:%d height:%d coord:%d %d\n", w, h, x, y);
        /* On macOS, maximization should be applied at creation time or the window will take
           a moment to animate to its maximized size. */
#if defined (LAGRANGE_ENABLE_CUSTOM_FRAME)
        if (snap_MainWindow(d->window)) {
            if (snap_MainWindow(d->window) == maximized_WindowSnap) {
                appendFormat_String(str, "~window.maximize\n");
            }
            else if (~SDL_GetWindowFlags(d->window->base.win) & SDL_WINDOW_MINIMIZED) {
                /* Save the actual visible window position, too, because snapped windows may
                   still be resized/moved without affecting normalRect. */
                SDL_GetWindowPosition(d->window->base.win, &x, &y);
                SDL_GetWindowSize(d->window->base.win, &w, &h);
                appendFormat_String(
                    str, "~window.setrect snap:%d width:%d height:%d coord:%d %d\n",
                    snap_MainWindow(d->window), w, h, x, y);
            }
        }
#elif !defined (iPlatformApple)
        if (snap_MainWindow(d->window) == maximized_WindowSnap) {
            appendFormat_String(str, "~window.maximize\n");
        }
#endif
    }
    appendFormat_String(str, "uilang id:%s\n", cstr_String(&d->prefs.strings[uiLanguage_PrefsString]));
    appendFormat_String(str, "uiscale arg:%f\n", uiScale_Window(as_Window(d->window)));
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
    appendFormat_String(str, "decodeurls arg:%d\n", d->prefs.decodeUserVisibleURLs);
    appendFormat_String(str, "linewidth.set arg:%d\n", d->prefs.lineWidth);
    appendFormat_String(str, "linespacing.set arg:%f\n", d->prefs.lineSpacing);
    appendFormat_String(str, "returnkey.set arg:%d\n", d->prefs.returnKey);
    iConstForEach(StringSet, fp, d->prefs.disabledFontPacks) {
        appendFormat_String(str, "fontpack.disable id:%s\n", cstr_String(fp.value));
    }
    appendFormat_String(str, "ansiescape arg:%d\n", d->prefs.gemtextAnsiEscapes);
    /* TODO: This array belongs in Prefs. It can then be used for command handling as well. */
    const struct {
        const char * id;
        const iBool *value;
    } boolPrefs[] = {
        { "prefs.time.24h", &d->prefs.time24h },
        { "prefs.animate", &d->prefs.uiAnimations },
        { "prefs.font.smooth", &d->prefs.fontSmoothing },
        { "prefs.mono.gemini", &d->prefs.monospaceGemini },
        { "prefs.mono.gopher", &d->prefs.monospaceGopher },
        { "prefs.boldlink.visited", &d->prefs.boldLinkVisited },
        { "prefs.boldlink.dark", &d->prefs.boldLinkDark },
        { "prefs.boldlink.light", &d->prefs.boldLinkLight },
        { "prefs.biglede", &d->prefs.bigFirstParagraph },
        { "prefs.plaintext.wrap", &d->prefs.plainTextWrap },
        { "prefs.sideicon", &d->prefs.sideIcon },
        { "prefs.centershort", &d->prefs.centerShortDocs },
        { "prefs.collapsepreonload", &d->prefs.collapsePreOnLoad },
        { "prefs.hoverlink", &d->prefs.hoverLink },
        { "prefs.bookmarks.addbottom", &d->prefs.addBookmarksToBottom },
        { "prefs.archive.openindex", &d->prefs.openArchiveIndexPages },
        { "prefs.font.warnmissing", &d->prefs.warnAboutMissingGlyphs },
        { "prefs.blink", &d->prefs.blinkingCursor },
    };
    iForIndices(i, boolPrefs) {
        appendFormat_String(str, "%s.changed arg:%d\n", boolPrefs[i].id, *boolPrefs[i].value);
    }
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
    return defaultDataDir_App_;
}

static const char *downloadDir_App_(void) {
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
                const iInt2 pos = coord_Command(cmd);
                d->initialWindowRect = init_Rect(
                    pos.x, pos.y, argLabel_Command(cmd, "width"), argLabel_Command(cmd, "height"));
            }
            else if (equal_Command(cmd, "fontpack.disable")) {
                insert_StringSet(d->prefs.disabledFontPacks,
                                 collect_String(suffix_Command(cmd, "id")));
            }
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
        /* Default CA setup. */
        setCACertificates_TlsRequest(&d->prefs.strings[caFile_PrefsString],
                                     &d->prefs.strings[caPath_PrefsString]);
    }
    iRelease(f);
    /* Upgrade checks. */
    if (cmp_Version(&upgradedFromAppVersion, &(iVersion){ 1, 8, 0 }) < 0) {
#if !defined (iPlatformAppleMobile)
        /* When upgrading to v1.8.0, the old hardcoded font library is gone and that means
           UI strings may not have the right fonts available for the UI to remain
           usable. */
        postCommandf_App("uilang id:en");
        postCommand_App("~fontpack.suggest.classic");
#endif
    }
#if !defined (LAGRANGE_ENABLE_CUSTOM_FRAME)
    d->prefs.customFrame = iFalse;
#endif
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
    current_DocumentStateFlag     = iBit(1),
    rootIndex1_DocumentStateFlag = iBit(2)
};

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
        iDocumentWidget *doc           = NULL;
        iDocumentWidget *current[2]    = { NULL, NULL };
        iBool            isFirstTab[2] = { iTrue, iTrue };
        while (!atEnd_File(f)) {
            readData_File(f, 4, magic);
            if (!memcmp(magic, magicWindow_App_, 4)) {
                const int splitMode = read32_File(f);
                const int keyRoot   = read32_File(f);
                d->window->pendingSplitMode = splitMode;
                setSplitMode_MainWindow(d->window, splitMode | noEvents_WindowSplit);
                d->window->base.keyRoot = d->window->base.roots[keyRoot];
            }
            else if (!memcmp(magic, magicSidebar_App_, 4)) {
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
                iRoot *root = d->window->base.roots[rootIndex];
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
                const int8_t flags = read8_File(f);
                int rootIndex = flags & rootIndex1_DocumentStateFlag ? 1 : 0;
                if (rootIndex > numRoots_Window(as_Window(d->window)) - 1) {
                    rootIndex = 0;
                }
                setCurrent_Root(d->window->base.roots[rootIndex]);
                if (isFirstTab[rootIndex]) {
                    isFirstTab[rootIndex] = iFalse;
                    /* There is one pre-created tab in each root. */
                    doc = document_Root(get_Root());
                }
                else {
                    doc = newTab_App(NULL, iFalse /* no switching */);
                }
                if (flags & current_DocumentStateFlag) {
                    current[rootIndex] = doc;
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
        if (d->window->splitMode) {
            /* Update root placement. */
            resize_MainWindow(d->window, -1, -1);
        }
        iForIndices(i, current) {
            postCommandf_Root(NULL, "tabs.switch page:%p", current[i]);
        }
        setCurrent_Root(NULL);
        return iTrue;
    }
    return iFalse;
}

static void saveState_App_(const iApp *d) {
    iUnused(d);
    trimCache_App();
    iMainWindow *win = d->window;
    /* UI state is saved in binary because it is quite complex (e.g.,
       navigation history, cached content) and depends closely on the widget
       tree. The data is largely not reorderable and should not be modified
       by the user manually. */
    iFile *f = newCStr_File(concatPath_CStr(dataDir_App_(), tempStateFileName_App_));
    if (open_File(f, writeOnly_FileMode)) {
        writeData_File(f, magicState_App_, 4);
        writeU32_File(f, latest_FileVersion); /* version */
        /* Begin with window state. */ {
            writeData_File(f, magicWindow_App_, 4);
            writeU32_File(f, win->splitMode);
            writeU32_File(f, win->base.keyRoot == win->base.roots[0] ? 0 : 1);
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
    if (!isEmpty_String(cmds)) {
        iString *result = communicate_Ipc(cmds, requestRaise);
        if (result) {
            fwrite(cstr_String(result), 1, size_String(result), stdout);
            fflush(stdout);
        }
        delete_String(result);
    }
    iUnused(instance);
//    else {
//        printf("Lagrange already running (PID %d)\n", instance);
//    }
    terminate_App_(0);
}
#endif /* defined (LAGRANGE_ENABLE_IPC) */

static iBool hasCommandLineOpenableScheme_(const iRangecc uri) {
    static const char *schemes[] = {
        "gemini:", "gopher:", "finger:", "file:", "data:", "about:"
    };
    iForIndices(i, schemes) {
        if (startsWithCase_Rangecc(uri, schemes[i])) {
            return iTrue;
        }
    }
    return iFalse;
}

static void init_App_(iApp *d, int argc, char **argv) {
#if defined (iPlatformLinux)
    d->isRunningUnderWindowSystem = !iCmpStr(SDL_GetCurrentVideoDriver(), "x11") ||
                                    !iCmpStr(SDL_GetCurrentVideoDriver(), "wayland");
#else
    d->isRunningUnderWindowSystem = iTrue;
#endif
    d->isDarkSystemTheme = iTrue; /* will be updated by system later on, if supported */
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
    iStringList *openCmds = new_StringList();
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
    d->initialWindowRect      = init_Rect(-1, -1, 900, 560);
#if defined (iPlatformMsys)
    /* Must scale by UI scaling factor. */
    mulfv_I2(&d->initialWindowRect.size, desktopDPI_Win32());
#endif
#if defined (iPlatformLinux)
    /* Scale by the primary (?) monitor DPI. */
    if (isRunningUnderWindowSystem_App()) {
        float vdpi;
        SDL_GetDisplayDPI(0, NULL, NULL, &vdpi);
        const float factor = vdpi / 96.0f;
        mulfv_I2(&d->initialWindowRect.size, iMax(factor, 1.0f));
    }
#endif
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
    init_Keys();
    init_Fonts(dataDir_App_());
    loadPalette_Color(dataDir_App_());
    setThemePalette_Color(d->prefs.theme); /* default UI colors */
    loadPrefs_App_(d);
    updateActive_Fonts();
    load_Keys(dataDir_App_());
    /* See if the user wants to override the window size. */ {
        iCommandLineArg *arg = iClob(checkArgument_CommandLine(&d->args, windowWidth_CommandLineOption));
        if (arg) {
            d->initialWindowRect.size.x = toInt_String(value_CommandLineArg(arg, 0));
        }
        arg = iClob(checkArgument_CommandLine(&d->args, windowHeight_CommandLineOption));
        if (arg) {
            d->initialWindowRect.size.y = toInt_String(value_CommandLineArg(arg, 0));
        }
    }
    init_PtrArray(&d->popupWindows);
    d->window = new_MainWindow(d->initialWindowRect);
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
    postCommand_Root(NULL, "~window.unfreeze");
    postCommand_Root(NULL, "font.reset");
    d->autoReloadTimer = SDL_AddTimer(60 * 1000, postAutoReloadCommand_App_, NULL);
    postCommand_Root(NULL, "document.autoreload");
#if defined (LAGRANGE_ENABLE_IDLE_SLEEP)
    d->isIdling      = iFalse;
    d->lastEventTime = 0;
    d->sleepTimer    = SDL_AddTimer(1000, checkAsleep_App_, d);
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
    delete_MainWindow(d->window);
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
    d->window = NULL;
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

const iString *downloadPathForUrl_App(const iString *url, const iString *mime) {
    /* Figure out a file name from the URL. */
    iUrl parts;
    init_Url(&parts, url);
    while (startsWith_Rangecc(parts.path, "/")) {
        parts.path.start++;
    }
    while (endsWith_Rangecc(parts.path, "/")) {
        parts.path.end--;
    }
    iString *name = collectNewCStr_String("pagecontent");
    if (isEmpty_Range(&parts.path)) {
        if (!isEmpty_Range(&parts.host)) {
            setRange_String(name, parts.host);
            replace_Block(&name->chars, '.', '_');
        }
    }
    else {
        const size_t slashPos = lastIndexOfCStr_Rangecc(parts.path, "/");
        iRangecc fn = { parts.path.start + (slashPos != iInvalidPos ? slashPos + 1 : 0),
                        parts.path.end };
        if (!isEmpty_Range(&fn)) {
            setRange_String(name, fn);
        }
    }
    if (startsWith_String(name, "~")) {
        /* This would be interpreted as a reference to a home directory. */
        remove_Block(&name->chars, 0, 1);
    }
    iString *savePath = concat_Path(downloadDir_App(), name);
    if (lastIndexOfCStr_String(savePath, ".") == iInvalidPos) {
        /* No extension specified in URL. */
        if (startsWith_String(mime, "text/gemini")) {
            appendCStr_String(savePath, ".gmi");
        }
        else if (startsWith_String(mime, "text/")) {
            appendCStr_String(savePath, ".txt");
        }
        else if (startsWith_String(mime, "image/")) {
            appendCStr_String(savePath, cstr_String(mime) + 6);
        }
    }
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

void trimCache_App(void) {
    iApp *d = &app_;
    size_t cacheSize = 0;
    const size_t limit = d->prefs.maxCacheSize * 1000000;
    iObjectList *docs = listDocuments_App(NULL);
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
    iObjectList *docs = listDocuments_App(NULL);
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

#if 0
iBool findCachedContent_App(const iString *url, iString *mime_out, iBlock *data_out) {
    /* Cached content can be found in MediaRequests of DocumentWidgets (loaded on the currently
       open page) and the DocumentWidget itself. `Media` doesn't store source data, only
       presentation data. */
    iConstForEach(ObjectList, i, iClob(listDocuments_App(NULL))) {
        if (findCachedContent_DocumentWidget(i.object, url, mime_out, data_out)) {
            return iTrue;
        }
    }
    return iFalse;
}
#endif

iLocalDef iBool isWaitingAllowed_App_(iApp *d) {
    if (!isEmpty_Periodic(&d->periodic)) {
        return iFalse;
    }
    if (d->warmupFrames > 0) {
        return iFalse;
    }
#if defined (LAGRANGE_ENABLE_IDLE_SLEEP)
    if (d->isIdling) {
        return iFalse;
    }
#endif
    return !value_Atomic(&d->pendingRefresh) && isEmpty_SortedArray(&d->tickers);
}

static iBool nextEvent_App_(iApp *d, enum iAppEventMode eventMode, SDL_Event *event) {
    if (eventMode == waitForNewEvents_AppEventMode && isWaitingAllowed_App_(d)) {
        /* If there are periodic commands pending, wait only for a short while. */
        if (!isEmpty_Periodic(&d->periodic)) {
            return SDL_WaitEventTimeout(event, 500);
        }
        /* We may be allowed to block here until an event comes in. */
        if (isWaitingAllowed_App_(d)) {
            return SDL_WaitEvent(event);
        }
    }
    return SDL_PollEvent(event);
}

static iPtrArray *listWindows_App_(const iApp *d, iPtrArray *windows) {
    clear_PtrArray(windows);
    iReverseConstForEach(PtrArray, i, &d->popupWindows) {
        pushBack_PtrArray(windows, i.ptr);
    }
    pushBack_PtrArray(windows, d->window);
    return windows;
}

void processEvents_App(enum iAppEventMode eventMode) {
    iApp *d = &app_;
    iRoot *oldCurrentRoot = current_Root(); /* restored afterwards */
    SDL_Event ev;
    iBool gotEvents = iFalse;
    iPtrArray windows;
    init_PtrArray(&windows);
    while (nextEvent_App_(d, eventMode, &ev)) {
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
                break;
            case SDL_APP_DIDENTERFOREGROUND:
                gotEvents = iTrue;
                d->warmupFrames = 5;
#if defined (LAGRANGE_ENABLE_IDLE_SLEEP)
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
#endif
                gotEvents = iTrue;
                /* Keyboard modifier mapping. */
                if (ev.type == SDL_KEYDOWN || ev.type == SDL_KEYUP) {
                    /* Track Caps Lock state as a modifier. */
                    if (ev.key.keysym.sym == SDLK_CAPSLOCK) {
                        setCapsLockDown_Keys(ev.key.state == SDL_PRESSED);
                    }
                    ev.key.keysym.mod = mapMods_Keys(ev.key.keysym.mod & ~KMOD_CAPS);
                }
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
                /* Per-window processing. */
                iBool wasUsed = iFalse;
                listWindows_App_(d, &windows);
                iConstForEach(PtrArray, iter, &windows) {
                    iWindow *window = iter.ptr;
                    setCurrent_Window(window);
                    window->lastHover = window->hover;
                    wasUsed = processEvent_Window(window, &ev);
                    if (ev.type == SDL_MOUSEMOTION || ev.type == SDL_MOUSEBUTTONDOWN) {
                        break;
                    }
                    if (wasUsed) break;
                }
                setCurrent_Window(d->window);
                if (!wasUsed) {
                    /* There may be a key binding for this. */
                    wasUsed = processEvent_Keys(&ev);
                }
                if (!wasUsed) {
                    /* Focus cycling. */
                    if (ev.type == SDL_KEYDOWN && ev.key.keysym.sym == SDLK_TAB) {
                        setFocus_Widget(findFocusable_Widget(focus_Widget(),
                                                             ev.key.keysym.mod & KMOD_SHIFT
                                                                 ? backward_WidgetFocusDir
                                                                 : forward_WidgetFocusDir));
                        wasUsed = iTrue;
                    }
                }
                if (ev.type == SDL_USEREVENT && ev.user.code == command_UserEventCode) {
#if defined (iPlatformAppleDesktop)
                    handleCommand_MacOS(command_UserEvent(&ev));
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
        /* This is where we spend most of our time when idle. 60 Hz still quite a lot but we
           can't wait too long after the user tries to interact again with the app. In any
           case, on macOS SDL_WaitEvent() seems to use 10x more CPU time than sleeping. */
        SDL_Delay(1000 / 60);
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
    postRefresh_App();
    iConstForEach(Array, i, &pending->values) {
        const iTicker *ticker = i.value;
        if (ticker->callback) {
            setCurrent_Root(ticker->root); /* root might be NULL */
            ticker->callback(ticker->context);
        }
    }
    setCurrent_Root(NULL);
    delete_SortedArray(pending);
    if (isEmpty_SortedArray(&d->tickers)) {
        d->lastTickerTime = 0;
    }
//    iForIndices(i, d->window->base.roots) {
//        iRoot *root = d->window->base.roots[i];
//        if (root) {
//            notifyVisualOffsetChange_Root(root);
//        }
//    }
}

static int resizeWatcher_(void *user, SDL_Event *event) {
    iApp *d = user;
    if (event->type == SDL_WINDOWEVENT && event->window.event == SDL_WINDOWEVENT_SIZE_CHANGED) {
        const SDL_WindowEvent *winev = &event->window;
#if defined (iPlatformMsys)
        resetFonts_Text(text_Window(d->window)); {
            SDL_Event u = { .type = SDL_USEREVENT };
            u.user.code = command_UserEventCode;
            u.user.data1 = strdup("theme.changed auto:1");
            dispatchEvent_Window(as_Window(d->window), &u);
        }
#endif
        drawWhileResizing_MainWindow(d->window, winev->data1, winev->data2);
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
        dispatchCommands_Periodic(&d->periodic);
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
    if (exchange_Atomic(&d->pendingRefresh, iFalse)) {
        /* Draw each window. */
        iConstForEach(PtrArray, j, &windows) {
            iWindow *win = j.ptr;
            setCurrent_Window(win);
            switch (win->type) {
                case main_WindowType:
    //                iTime draw;
    //                initCurrent_Time(&draw);
                    draw_MainWindow(as_MainWindow(win));
    //                printf("draw: %lld \u03bcs\n", (long long) (elapsedSeconds_Time(&draw) * 1000000));
    //                fflush(stdout);
                    break;
                default:
                    draw_Window(win);
                    break;
            }
        }
    }
    if (d->warmupFrames > 0) {
        d->warmupFrames--;
    }
    deinit_PtrArray(&windows);
}

iBool isRefreshPending_App(void) {
    return value_Atomic(&app_.pendingRefresh);
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
    const iBool wasPending = exchange_Atomic(&d->pendingRefresh, iTrue);
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
    /*ev.user.windowID = id_Window(get_Window());*/
    ev.user.data1 = strdup(command);
    ev.user.data2 = d; /* all events are root-specific */
    SDL_PushEvent(&ev);
    if (app_.commandEcho) {
        iWindow *win = get_Window();
        printf("%s[command] {%d} %s\n",
               app_.isLoadingPrefs ? "[Prefs] " : "",
               (d == NULL || win == NULL ? 0 : d == win->roots[0] ? 1 : 2),
               command); fflush(stdout);
    }
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
    roots[0] = win->keyRoot;
    roots[1] = (roots[0] == win->roots[0] ? win->roots[1] : win->roots[0]);
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
#else
    return desktop_AppDeviceType;
#endif
}

iBool isRunningUnderWindowSystem_App(void) {
    return app_.isRunningUnderWindowSystem;
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
        postCommandf_App("cachesize.set arg:%d",
                         toInt_String(text_InputWidget(findChild_Widget(d, "prefs.cachesize"))));
        postCommandf_App("memorysize.set arg:%d",
                         toInt_String(text_InputWidget(findChild_Widget(d, "prefs.memorysize"))));
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
    else if (startsWith_CStr(cmd, "input.ended id:prefs.linespacing")) {
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
        if (!d->root->pendingArrange) {
            d->root->pendingArrange = iTrue;
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
    //iApp *d = &app_;
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
    if (switchToNew) {
        postCommandf_App("tabs.switch page:%p", doc);
    }
    arrange_Widget(tabs);
    refresh_Widget(tabs);
    postCommandf_Root(get_Root(), "tab.created id:%s", cstr_String(id_Widget(as_Widget(doc))));
    return doc;
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
        setText_LabelWidget(scope,
                            text_LabelWidget(child_Widget(
                                findChild_Widget(as_Widget(scope), "menu"), arg_Command(cmd))));
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
                const iLabelWidget *scope    = findChild_Widget(dlg, "ident.scope");
                const iString *     selLabel = text_LabelWidget(scope);
                int                 selScope = 0;
                iConstForEach(ObjectList,
                              i,
                              children_Widget(findChild_Widget(constAs_Widget(scope), "menu"))) {
                    if (isInstance_Object(i.object, &Class_LabelWidget)) {
                        const iLabelWidget *item = i.object;
                        if (equal_String(text_LabelWidget(item), selLabel)) {
                            break;
                        }
                        selScope++;
                    }
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
        setupSheetTransition_Mobile(dlg, iFalse);
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

static void invalidateCachedDocuments_App_(void) {
    iForEach(ObjectList, i, iClob(listDocuments_App(NULL))) {
        invalidateCachedLayout_History(history_DocumentWidget(i.object));
    }
}

iBool handleCommand_App(const char *cmd) {
    iApp *d = &app_;
    const iBool isFrozen = !d->window || d->window->isDrawFrozen;
    /* TODO: Maybe break this up a little bit? There's a very long list of ifs here. */
    if (equal_Command(cmd, "config.error")) {
        makeSimpleMessage_Widget(uiTextCaution_ColorEscape "CONFIG ERROR",
                                 format_CStr("Error in config file: %s\n"
                                             "See \"about:debug\" for details.",
                                             suffixPtr_Command(cmd, "where")));
        return iTrue;
    }
    else if (equal_Command(cmd, "fontpack.suggest.classic")) {
        /* TODO: Don't use this when system fonts are accessible. */
        if (!isInstalled_Fonts("classic-set") && !isInstalled_Fonts("cjk")) {
            makeQuestion_Widget(
                uiHeading_ColorEscape "${heading.fontpack.classic}",
                "${dlg.fontpack.classic.msg}",
                (iMenuItem[]){
                    { "${cancel}" },
                    { uiTextAction_ColorEscape "${dlg.fontpack.classic}",
                      0,
                      0,
                      "!open newtab:1 url:gemini://skyjake.fi/fonts/classic-set.fontpack" } },
                2);
        }
        return iTrue;
    }
    else if (equal_Command(cmd, "prefs.changed")) {
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
    else if (equal_Command(cmd, "translation.languages")) {
        d->prefs.langFrom = argLabel_Command(cmd, "from");
        d->prefs.langTo   = argLabel_Command(cmd, "to");
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
    else if (equal_Command(cmd, "window.retain")) {
        d->prefs.retainWindowSize = arg_Command(cmd);
        return iTrue;
    }
    else if (equal_Command(cmd, "customframe")) {
        d->prefs.customFrame = arg_Command(cmd);
        return iTrue;
    }
    else if (equal_Command(cmd, "window.maximize")) {
        if (!argLabel_Command(cmd, "toggle")) {
            setSnap_MainWindow(d->window, maximized_WindowSnap);
        }
        else {
            setSnap_MainWindow(d->window, snap_MainWindow(d->window) == maximized_WindowSnap ? 0 :
                           maximized_WindowSnap);
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
#if 0
    else if (equal_Command(cmd, "font.user")) {
        const char *path = suffixPtr_Command(cmd, "path");
        if (cmp_String(&d->prefs.symbolFontPath, path)) {
            if (!isFrozen) {
                setFreezeDraw_MainWindow(get_MainWindow(), iTrue);
            }
            setCStr_String(&d->prefs.symbolFontPath, path);
            loadUserFonts_Text();
            resetFonts_App(d);
            if (!isFrozen) {
                postCommand_App("font.changed");
                postCommand_App("window.unfreeze");
            }
        }
        return iTrue;
    }
#endif
    else if (equal_Command(cmd, "font.set")) {
        if (!isFrozen) {
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
            resetFonts_Text(text_Window(get_MainWindow()));
        }
        if (!isFrozen) {
            postCommand_App("font.changed");
            postCommand_App("window.unfreeze");
        }
        return iTrue;
    }
#if 0
    else if (equal_Command(cmd, "headingfont.set")) {
        if (!isFrozen) {
            setFreezeDraw_MainWindow(get_MainWindow(), iTrue);
        }
        d->prefs.headingFont = arg_Command(cmd);
        setHeadingFont_Text(text_Window(d->window), d->prefs.headingFont);
        if (!isFrozen) {
            postCommand_App("font.changed");
            postCommand_App("window.unfreeze");
        }
        return iTrue;
    }
#endif
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
    else if (equal_Command(cmd, "hidetoolbarscroll")) {
        d->prefs.hideToolbarOnScroll = arg_Command(cmd);
        if (!d->prefs.hideToolbarOnScroll) {
            showToolbar_Root(get_Root(), iTrue);
        }
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
    else if (equal_Command(cmd, "quoteicon.set")) {
        d->prefs.quoteIcon = arg_Command(cmd) != 0;
        postCommand_App("document.layout.changed redo:1");
        return iTrue;
    }
    else if (equal_Command(cmd, "prefs.font.smooth.changed")) {
        if (!isFrozen) {
            setFreezeDraw_MainWindow(get_MainWindow(), iTrue);
        }
        d->prefs.fontSmoothing = arg_Command(cmd) != 0;
        if (!isFrozen) {
            resetFonts_Text(text_Window(get_MainWindow())); /* clear the glyph cache */
            postCommand_App("font.changed");
            postCommand_App("window.unfreeze");
        }
        return iTrue;
    }
    else if (equal_Command(cmd, "ansiescape")) {
        d->prefs.gemtextAnsiEscapes = arg_Command(cmd);
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
            //resetFonts_Text(); /* clear the glyph cache */
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
        postCommandf_App("open url:%s", cstrCollect_String(makeFileUrl_String(downloadDir_App())));
        return iTrue;
    }
    else if (equal_Command(cmd, "ca.file")) {
        setCStr_String(&d->prefs.strings[caFile_PrefsString], suffixPtr_Command(cmd, "path"));
        if (!argLabel_Command(cmd, "noset")) {
            setCACertificates_TlsRequest(&d->prefs.strings[caFile_PrefsString], &d->prefs.strings[caPath_PrefsString]);
        }
        return iTrue;
    }
    else if (equal_Command(cmd, "ca.path")) {
        setCStr_String(&d->prefs.strings[caPath_PrefsString], suffixPtr_Command(cmd, "path"));
        if (!argLabel_Command(cmd, "noset")) {
            setCACertificates_TlsRequest(&d->prefs.strings[caFile_PrefsString], &d->prefs.strings[caPath_PrefsString]);
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
    else if (equal_Command(cmd, "open")) {
        const char *urlArg = suffixPtr_Command(cmd, "url");
        if (!urlArg) {
            return iTrue; /* invalid command */
        }
        if (findWidget_App("prefs")) {
            postCommand_App("prefs.dismiss");        
        }
        iString    *url         = collectNewCStr_String(urlArg);
        const iBool noProxy     = argLabel_Command(cmd, "noproxy") != 0;
        const iBool fromSidebar = argLabel_Command(cmd, "fromsidebar") != 0;
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
            iUploadWidget *upload = new_UploadWidget();
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
            postCommandf_App("ui.split arg:3 axis:%d origin:%s newtab:%d url:%s",
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
        }
        if (newTab & (new_OpenTabFlag | newBackground_OpenTabFlag)) {
            doc = newTab_App(NULL, (newTab & new_OpenTabFlag) != 0); /* `newtab:2` to open in background */
        }
        iHistory *history = history_DocumentWidget(doc);
        const iBool isHistory = argLabel_Command(cmd, "history") != 0;
        int redirectCount = argLabel_Command(cmd, "redirect");
        if (!isHistory) {
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
        if (prefs_App()->decodeUserVisibleURLs) {
            urlDecodePath_String(url);
        }
        else {
            urlEncodePath_String(url);
        }            
        setUrlFlags_DocumentWidget(doc, url,
           (isHistory   ? useCachedContentIfAvailable_DocumentWidgetSetUrlFlag : 0) |
           (fromSidebar ? openedFromSidebar_DocumentWidgetSetUrlFlag : 0));
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
        pickFileForOpening_iOS();
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
        const iBool isDuplicate = argLabel_Command(cmd, "duplicate") != 0;
        newTab_App(isDuplicate ? document_App() : NULL, iTrue);
        if (!isDuplicate) {
            postCommand_App("navigate.home focus:1");
        }
        return iTrue;
    }
    else if (equal_Command(cmd, "tabs.close")) {
        iWidget *tabs = findWidget_App("doctabs");
#if defined (iPlatformMobile)
        /* Can't close the last tab on mobile. */
        if (tabCount_Widget(tabs) == 1 && numRoots_Window(get_Window()) == 1) {
            postCommand_App("navigate.home");
            return iTrue;
        }
#endif
        const iRangecc tabId = range_Command(cmd, "id");
        iWidget *      doc   = !isEmpty_Range(&tabId) ? findWidget_App(cstr_Rangecc(tabId))
                                                      : document_App();
        iBool  wasCurrent = (doc == (iWidget *) document_App());
        size_t index      = tabPageIndex_Widget(tabs, doc);
        iBool  wasClosed  = iFalse;
        postCommand_App("document.openurls.changed");
        if (argLabel_Command(cmd, "toright")) {
            while (tabCount_Widget(tabs) > index + 1) {
                destroy_Widget(removeTabPage_Widget(tabs, index + 1));
            }
            wasClosed = iTrue;
        }
        if (argLabel_Command(cmd, "toleft")) {
            while (index-- > 0) {
                destroy_Widget(removeTabPage_Widget(tabs, 0));
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
            iWidget *closed = removeTabPage_Widget(tabs, index);
            cancelAllRequests_DocumentWidget((iDocumentWidget *) closed);
            destroy_Widget(closed); /* released later */
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
        else {
            postCommand_App("quit");
        }
        return iTrue;
    }
    else if (equal_Command(cmd, "keyroot.next")) {
        if (setKeyRoot_Window(as_Window(d->window),
                              otherRoot_Window(as_Window(d->window), d->window->base.keyRoot))) {
            setFocus_Widget(NULL);
        }
        return iTrue;
    }
    else if (equal_Command(cmd, "quit")) {
        SDL_Event ev;
        ev.type = SDL_QUIT;
        SDL_PushEvent(&ev);
    }
    else if (equal_Command(cmd, "preferences")) {
        iWidget *dlg = makePreferences_Widget();
        updatePrefsThemeButtons_(dlg);
        setText_InputWidget(findChild_Widget(dlg, "prefs.downloads"), &d->prefs.strings[downloadDir_PrefsString]);
        /* TODO: Use a common table in Prefs to do this more conviently.
           Also see `serializePrefs_App_()`. */
        setToggle_Widget(findChild_Widget(dlg, "prefs.hoverlink"), d->prefs.hoverLink);
        setToggle_Widget(findChild_Widget(dlg, "prefs.smoothscroll"), d->prefs.smoothScrolling);
        setToggle_Widget(findChild_Widget(dlg, "prefs.imageloadscroll"), d->prefs.loadImageInsteadOfScrolling);
        setToggle_Widget(findChild_Widget(dlg, "prefs.hidetoolbarscroll"), d->prefs.hideToolbarOnScroll);
        setToggle_Widget(findChild_Widget(dlg, "prefs.bookmarks.addbottom"), d->prefs.addBookmarksToBottom);
        setToggle_Widget(findChild_Widget(dlg, "prefs.font.warnmissing"), d->prefs.warnAboutMissingGlyphs);
        setToggle_Widget(findChild_Widget(dlg, "prefs.archive.openindex"), d->prefs.openArchiveIndexPages);
        setToggle_Widget(findChild_Widget(dlg, "prefs.ostheme"), d->prefs.useSystemTheme);
        setToggle_Widget(findChild_Widget(dlg, "prefs.customframe"), d->prefs.customFrame);
        setToggle_Widget(findChild_Widget(dlg, "prefs.animate"), d->prefs.uiAnimations);
        setToggle_Widget(findChild_Widget(dlg, "prefs.blink"), d->prefs.blinkingCursor);
        updatePrefsPinSplitButtons_(dlg, d->prefs.pinSplit);
        updateScrollSpeedButtons_(dlg, mouse_ScrollType, d->prefs.smoothScrollSpeed[mouse_ScrollType]);
        updateScrollSpeedButtons_(dlg, keyboard_ScrollType, d->prefs.smoothScrollSpeed[keyboard_ScrollType]);
        updateDropdownSelection_LabelWidget(findChild_Widget(dlg, "prefs.uilang"), cstr_String(&d->prefs.strings[uiLanguage_PrefsString]));
        setToggle_Widget(findChild_Widget(dlg, "prefs.time.24h"), d->prefs.time24h);
        updateDropdownSelection_LabelWidget(
            findChild_Widget(dlg, "prefs.returnkey"),
            format_CStr("returnkey.set arg:%d", d->prefs.returnKey));
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
        setFlags_Widget(
            findChild_Widget(dlg, format_CStr("prefs.linewidth.%d", d->prefs.lineWidth)),
            selected_WidgetFlag,
            iTrue);
        setText_InputWidget(findChild_Widget(dlg, "prefs.linespacing"),
                            collectNewFormat_String("%.2f", d->prefs.lineSpacing));
        setFlags_Widget(
            findChild_Widget(dlg, format_CStr("prefs.quoteicon.%d", d->prefs.quoteIcon)),
            selected_WidgetFlag,
            iTrue);
        setToggle_Widget(findChild_Widget(dlg, "prefs.biglede"), d->prefs.bigFirstParagraph);
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
            iWidget *idPanel = panel_Mobile(dlg, 2);
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
        iDocumentWidget *doc = document_App();
        if (suffixPtr_Command(cmd, "url")) {
            iString *title = collect_String(newRange_String(range_Command(cmd, "title")));
            replace_String(title, "%20", " ");
            makeBookmarkCreation_Widget(collect_String(suffix_Command(cmd, "url")),
                                        title,
                                        0x1f588 /* pin */);
        }
        else {
            makeBookmarkCreation_Widget(url_DocumentWidget(doc),
                                        bookmarkTitle_DocumentWidget(doc),
                                        siteIcon_GmDocument(document_DocumentWidget(doc)));
        }
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
    else if (equal_Command(cmd, "bookmarks.changed")) {
        save_Bookmarks(d->bookmarks, dataDir_App_());
        return iFalse;
    }
    else if (equal_Command(cmd, "feeds.refresh")) {
        refresh_Feeds();
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
    else if (equal_Command(cmd, "visited.changed")) {
        save_Visited(d->visited, dataDir_App_());
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
//        finalizeSheet_Mobile(as_Widget(imp));
        arrange_Widget(as_Widget(imp));
        setupSheetTransition_Mobile(as_Widget(imp), iTrue);
        postRefresh_App();
        return iTrue;
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
    else if (equal_Command(cmd, "idents.changed")) {
        saveIdentities_GmCerts(d->certs);
        return iFalse;
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
                                   { uiTextCaution_ColorEscape " ${dlg.fontpack.delete}",
                                     0,
                                     0,
                                     format_CStr("!fontpack.delete confirmed:1 id:%s",
                                                 cstr_String(packId)) } },
                    2);
            }
        }
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
                  collectNewFormat_String("%s\n", cstr_String(url_DocumentWidget(document_App()))),
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
    else {
        return iFalse;
    }
    return iTrue;
}

void openInDefaultBrowser_App(const iString *url) {
#if SDL_VERSION_ATLEAST(2, 0, 14)
    if (SDL_OpenURL(cstr_String(url)) == 0) {
        return;
    }
#endif
#if !defined (iPlatformAppleMobile)
    iProcess *proc = new_Process();
    setArguments_Process(proc,
#if defined (iPlatformAppleDesktop)
                         iClob(newStringsCStr_StringList("/usr/bin/env", "open", cstr_String(url), NULL))
#elif defined (iPlatformLinux) || defined (iPlatformOther) || defined (iPlatformHaiku)
                         iClob(newStringsCStr_StringList("/usr/bin/env", "xdg-open", cstr_String(url), NULL))
#elif defined (iPlatformMsys)
        iClob(newStringsCStr_StringList(
            concatPath_CStr(cstr_String(execPath_App()), "../urlopen.bat"),
            cstr_String(url),
            NULL))
        /* TODO: The prompt window is shown momentarily... */
#endif
    );
    start_Process(proc);
    waitForFinished_Process(proc); /* TODO: test on Windows */
    iRelease(proc);
#endif
}

void revealPath_App(const iString *path) {
#if defined (iPlatformAppleDesktop)
    const char *scriptPath = concatPath_CStr(dataDir_App_(), "revealfile.scpt");
    iFile *f = newCStr_File(scriptPath);
    if (open_File(f, writeOnly_FileMode | text_FileMode)) {
        /* AppleScript to select a specific file. */
        write_File(f, collect_Block(newCStr_Block("on run argv\n"
                                                  "  tell application \"Finder\"\n"
                                                  "    activate\n"
                                                  "    reveal POSIX file (item 1 of argv) as text\n"
                                                  "  end tell\n"
                                                  "end run\n")));
        close_File(f);
        iProcess *proc = new_Process();
        setArguments_Process(
            proc,
            iClob(newStringsCStr_StringList(
                "/usr/bin/osascript", scriptPath, cstr_String(path), NULL)));
        start_Process(proc);
        iRelease(proc);
    }
    iRelease(f);
#elif defined (iPlatformLinux) || defined (iPlatformHaiku)
    iFileInfo *inf = iClob(new_FileInfo(path));
    iRangecc target;
    if (isDirectory_FileInfo(inf)) {
        target = range_String(path);
    }
    else {
        target = dirName_Path(path);
    }
    iProcess *proc = new_Process();
    setArguments_Process(
        proc, iClob(newStringsCStr_StringList("/usr/bin/env", "xdg-open", cstr_Rangecc(target), NULL)));
    start_Process(proc);
    iRelease(proc);
#else
    iAssert(0 /* File revealing not implemented on this platform */);
#endif
}

iObjectList *listDocuments_App(const iRoot *rootOrNull) {
    iWindow *win = get_Window();
    iObjectList *docs = new_ObjectList();
    iForIndices(i, win->roots) {
        iRoot *root = win->roots[i];
        if (!root) continue;
        if (!rootOrNull || root == rootOrNull) {
            const iWidget *tabs = findChild_Widget(root->widget, "doctabs");
            iForEach(ObjectList, i, children_Widget(findChild_Widget(tabs, "tabs.pages"))) {
                if (isInstance_Object(i.object, &Class_DocumentWidget)) {
                    pushBack_ObjectList(docs, i.object);
                }
            }
        }
    }
    return docs;
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

void closePopups_App(void) {
    iApp *d = &app_;
    const uint32_t now = SDL_GetTicks();
    iConstForEach(PtrArray, i, &d->popupWindows) {
        const iWindow *win = i.ptr;
        if (now - win->focusGainedAt > 200) {
            postCommand_Root(((const iWindow *) i.ptr)->roots[0], "cancel");
        }
    }
}
