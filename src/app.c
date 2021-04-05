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
#include "embedded.h"
#include "feeds.h"
#include "mimehooks.h"
#include "gmcerts.h"
#include "gmdocument.h"
#include "gmutil.h"
#include "history.h"
#include "ipc.h"
#include "periodic.h"
#include "ui/certimportwidget.h"
#include "ui/color.h"
#include "ui/command.h"
#include "ui/documentwidget.h"
#include "ui/inputwidget.h"
#include "ui/keys.h"
#include "ui/labelwidget.h"
#include "ui/sidebarwidget.h"
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
#include <SDL.h>

#include <stdio.h>
#include <stdarg.h>
#include <errno.h>

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
static const char *defaultDataDir_App_ = "~/.config/lagrange";
#endif
#if defined (iPlatformHaiku)
#define EMB_BIN "./resources.lgr"
static const char *defaultDataDir_App_ = "~/config/settings";
#endif
#if defined (LAGRANGE_EMB_BIN) /* specified in build config */
#  undef EMB_BIN
#  define EMB_BIN LAGRANGE_EMB_BIN
#endif
#define EMB_BIN2 "../resources.lgr" /* fallback from build/executable dir */
static const char *prefsFileName_App_      = "prefs.cfg";
static const char *oldStateFileName_App_   = "state.binary";
static const char *stateFileName_App_      = "state.lgr";
static const char *defaultDownloadDir_App_ = "~/Downloads";

static const int idleThreshold_App_ = 1000; /* ms */

struct Impl_App {
    iCommandLine args;
    iString *    execPath;
    iMimeHooks * mimehooks;
    iGmCerts *   certs;
    iVisited *   visited;
    iBookmarks * bookmarks;
    iWindow *    window;
    iSortedArray tickers; /* per-frame callbacks, used for animations */
    uint32_t     lastTickerTime;
    uint32_t     elapsedSinceLastTicker;
    iBool        isRunning;
#if defined (LAGRANGE_IDLE_SLEEP)
    iBool        isIdling;
    uint32_t     lastEventTime;
    int          sleepTimer;
#endif
    iAtomicInt   pendingRefresh;
    int          tabEnum;
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
    void (*callback)(iAny *);
};

static int cmp_Ticker_(const void *a, const void *b) {
    const iTicker *elems[2] = { a, b };
    return iCmp(elems[0]->context, elems[1]->context);
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
    const iSidebarWidget *sidebar  = findWidget_App("sidebar");
    const iSidebarWidget *sidebar2 = findWidget_App("sidebar2");
#if defined (LAGRANGE_CUSTOM_FRAME)
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
        appendFormat_String(str, "sidebar.width arg:%d\n", width_SidebarWidget(sidebar));
        appendFormat_String(str, "sidebar2.width arg:%d\n", width_SidebarWidget(sidebar2));
        /* On macOS, maximization should be applied at creation time or the window will take
           a moment to animate to its maximized size. */
#if defined (LAGRANGE_CUSTOM_FRAME)
        if (snap_Window(d->window)) {
            if (~SDL_GetWindowFlags(d->window->win) & SDL_WINDOW_MINIMIZED) {
                /* Save the actual visible window position, too, because snapped windows may
                   still be resized/moved without affecting normalRect. */
                SDL_GetWindowPosition(d->window->win, &x, &y);
                SDL_GetWindowSize(d->window->win, &w, &h);
                appendFormat_String(
                    str, "~window.setrect snap:%d width:%d height:%d coord:%d %d\n",
                    snap_Window(d->window), w, h, x, y);
            }
        }
#elif !defined (iPlatformApple)
        if (snap_Window(d->window) == maximized_WindowSnap) {
            appendFormat_String(str, "~window.maximize\n");
        }
#endif
    }
    /* Sidebars. */ {
        if (isVisible_Widget(sidebar) && deviceType_App() != phone_AppDeviceType) {
            appendCStr_String(str, "sidebar.toggle\n");
        }
        appendFormat_String(str, "sidebar.mode arg:%d\n", mode_SidebarWidget(sidebar));
        if (isVisible_Widget(sidebar2) && deviceType_App() != phone_AppDeviceType) {
            appendCStr_String(str, "sidebar2.toggle\n");
        }
        appendFormat_String(str, "sidebar2.mode arg:%d\n", mode_SidebarWidget(sidebar2));
    }
    appendFormat_String(str, "uilang id:%s\n", cstr_String(&d->prefs.uiLanguage));
    appendFormat_String(str, "uiscale arg:%f\n", uiScale_Window(d->window));
    appendFormat_String(str, "prefs.dialogtab arg:%d\n", d->prefs.dialogTab);
    appendFormat_String(str, "font.set arg:%d\n", d->prefs.font);
    appendFormat_String(str, "headingfont.set arg:%d\n", d->prefs.headingFont);
    appendFormat_String(str, "zoom.set arg:%d\n", d->prefs.zoomPercent);
    appendFormat_String(str, "smoothscroll arg:%d\n", d->prefs.smoothScrolling);
    appendFormat_String(str, "imageloadscroll arg:%d\n", d->prefs.loadImageInsteadOfScrolling);
    appendFormat_String(str, "cachesize.set arg:%d\n", d->prefs.maxCacheSize);
    appendFormat_String(str, "decodeurls arg:%d\n", d->prefs.decodeUserVisibleURLs);
    appendFormat_String(str, "linewidth.set arg:%d\n", d->prefs.lineWidth);
    /* TODO: Set up an array of booleans in Prefs and do these in a loop. */
    appendFormat_String(str, "prefs.mono.gemini.changed arg:%d\n", d->prefs.monospaceGemini);
    appendFormat_String(str, "prefs.mono.gopher.changed arg:%d\n", d->prefs.monospaceGopher);
    appendFormat_String(str, "prefs.boldlink.dark.changed arg:%d\n", d->prefs.boldLinkDark);
    appendFormat_String(str, "prefs.boldlink.light.changed arg:%d\n", d->prefs.boldLinkLight);
    appendFormat_String(str, "prefs.biglede.changed arg:%d\n", d->prefs.bigFirstParagraph);
    appendFormat_String(str, "prefs.plaintext.wrap.changed arg:%d\n", d->prefs.plainTextWrap);
    appendFormat_String(str, "prefs.sideicon.changed arg:%d\n", d->prefs.sideIcon);
    appendFormat_String(str, "prefs.centershort.changed arg:%d\n", d->prefs.centerShortDocs);
    appendFormat_String(str, "prefs.collapsepreonload.changed arg:%d\n", d->prefs.collapsePreOnLoad);
    appendFormat_String(str, "prefs.hoverlink.changed arg:%d\n", d->prefs.hoverLink);
    appendFormat_String(str, "quoteicon.set arg:%d\n", d->prefs.quoteIcon ? 1 : 0);
    appendFormat_String(str, "theme.set arg:%d auto:1\n", d->prefs.theme);
    appendFormat_String(str, "accent.set arg:%d\n", d->prefs.accent);
    appendFormat_String(str, "ostheme arg:%d\n", d->prefs.useSystemTheme);
    appendFormat_String(str, "doctheme.dark.set arg:%d\n", d->prefs.docThemeDark);
    appendFormat_String(str, "doctheme.light.set arg:%d\n", d->prefs.docThemeLight);
    appendFormat_String(str, "saturation.set arg:%d\n", (int) ((d->prefs.saturation * 100) + 0.5f));
    appendFormat_String(str, "ca.file noset:1 path:%s\n", cstr_String(&d->prefs.caFile));
    appendFormat_String(str, "ca.path path:%s\n", cstr_String(&d->prefs.caPath));
    appendFormat_String(str, "proxy.gemini address:%s\n", cstr_String(&d->prefs.geminiProxy));
    appendFormat_String(str, "proxy.gopher address:%s\n", cstr_String(&d->prefs.gopherProxy));
    appendFormat_String(str, "proxy.http address:%s\n", cstr_String(&d->prefs.httpProxy));
    appendFormat_String(str, "downloads path:%s\n", cstr_String(&d->prefs.downloadDir));
    appendFormat_String(str, "searchurl address:%s\n", cstr_String(&d->prefs.searchUrl));
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
                const char *id = cstr_Rangecc(range_Command(cmd, "id"));
                setCStr_String(&d->prefs.uiLanguage, id);
                setCurrent_Lang(id);
            }
            else if (equal_Command(cmd, "ca.file") || equal_Command(cmd, "ca.path")) {
                /* Background requests may be started before these commands would get
                   handled via the event loop. */
                handleCommand_App(cmd);
            }
            else if (equal_Command(cmd, "customframe")) {
                d->prefs.customFrame = arg_Command(cmd);
            }
            else if (equal_Command(cmd, "window.setrect") && !argLabel_Command(cmd, "snap")) {
                const iInt2 pos = coord_Command(cmd);
                d->initialWindowRect = init_Rect(
                    pos.x, pos.y, argLabel_Command(cmd, "width"), argLabel_Command(cmd, "height"));
            }
#if !defined (LAGRANGE_DOWNLOAD_EDIT)
            else if (equal_Command(cmd, "downloads")) {
                continue; /* can't change downloads directory */
            }
#endif
            else {
                postCommandString_App(&cmdStr);
            }
            deinit_String(&cmdStr);
        }
        delete_String(str);
    }
    else {
        /* default preference values */
        setCACertificates_TlsRequest(&d->prefs.caFile, &d->prefs.caPath);
    }
#if !defined (LAGRANGE_CUSTOM_FRAME)
    d->prefs.customFrame = iFalse;
#endif
    iRelease(f);
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
static const char *magicTabDocument_App_ = "tabd";

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
        iDocumentWidget *doc = document_App();
        iDocumentWidget *current = NULL;
        while (!atEnd_File(f)) {
            readData_File(f, 4, magic);
            if (!memcmp(magic, magicTabDocument_App_, 4)) {
                if (!doc) {
                    doc = newTab_App(NULL, iTrue);
                }
                if (read8_File(f)) {
                    current = doc;
                }
                deserializeState_DocumentWidget(doc, stream_File(f));
                doc = NULL;
            }
            else {
                printf("%s: unrecognized data\n", cstr_String(path_File(f)));
                return iFalse;
            }
        }
        postCommandf_App("tabs.switch page:%p", current);
        return iTrue;
    }
    return iFalse;
}

iObjectList *listDocuments_App(void) {
    iObjectList *docs = new_ObjectList();
    const iWidget *tabs = findWidget_App("doctabs");
    iForEach(ObjectList, i, children_Widget(findChild_Widget(tabs, "tabs.pages"))) {
        if (isInstance_Object(i.object, &Class_DocumentWidget)) {
            pushBack_ObjectList(docs, i.object);
        }
    }
    return docs;
}

static void saveState_App_(const iApp *d) {
    iUnused(d);
    trimCache_App();
    iFile *f = newCStr_File(concatPath_CStr(dataDir_App_(), stateFileName_App_));
    if (open_File(f, writeOnly_FileMode)) {
        writeData_File(f, magicState_App_, 4);
        writeU32_File(f, latest_FileVersion); /* version */
        iConstForEach(ObjectList, i, iClob(listDocuments_App())) {
            iAssert(isInstance_Object(i.object, &Class_DocumentWidget));
            writeData_File(f, magicTabDocument_App_, 4);
            write8_File(f, document_App() == i.object ? 1 : 0);
            serializeState_DocumentWidget(i.object, stream_File(f));
        }
    }
    else {
        fprintf(stderr, "[App] failed to save state: %s\n", strerror(errno));
    }
    iRelease(f);
}

#if defined (LAGRANGE_IDLE_SLEEP)
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
    postCommand_App("document.autoreload");
    return interval;
}

static void terminate_App_(int rc) {
    SDL_Quit();
    deinit_Foundation();
    exit(rc);
}

static void communicateWithRunningInstance_App_(iApp *d, iProcessId instance,
                                                const iStringList *openCmds) {
    iString *cmds = new_String();
    const iProcessId pid = currentId_Process();
    iConstForEach(CommandLine, i, &d->args) {
        if (i.argType == value_CommandLineArgType) {
            continue;
        }
        if (equal_CommandLineConstIterator(&i, "go-home")) {
            appendCStr_String(cmds, "navigate.home\n");
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
        }
        else if (equal_CommandLineConstIterator(&i, "close-tab")) {
            appendCStr_String(cmds, "tabs.close\n");
        }
        else if (equal_CommandLineConstIterator(&i, listTabUrls_CommandLineOption)) {
            appendFormat_String(cmds, "ipc.list.urls pid:%d\n", pid);
        }
    }
    if (!isEmpty_StringList(openCmds)) {
        append_String(cmds, collect_String(joinCStr_StringList(openCmds, "\n")));
    }
    if (isEmpty_String(cmds)) {
        /* By default open a new tab. */
        appendCStr_String(cmds, "tabs.new\n");
    }
    if (!isEmpty_String(cmds)) {
        iString *result = communicate_Ipc(cmds);
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

static void init_App_(iApp *d, int argc, char **argv) {
    init_CommandLine(&d->args, argc, argv);
    /* Where was the app started from? We ask SDL first because the command line alone is
       not a reliable source of this information, particularly when it comes to different
       operating systems. */ {
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
#if defined (iHaveLoadEmbed)
    /* Load the resources from a file. */ {
        if (!load_Embed(concatPath_CStr(cstr_String(execPath_App()), EMB_BIN))) {
            if (!load_Embed(concatPath_CStr(cstr_String(execPath_App()), EMB_BIN2))) {
                if (!load_Embed("resources.lgr")) {
                    fprintf(stderr, "failed to load resources: %s\n", strerror(errno));
                    exit(-1);
                }
            }
        }
    }
#endif
    init_Lang();
    /* Configure the valid command line options. */ {
        defineValues_CommandLine(&d->args, "close-tab", 0);
        defineValues_CommandLine(&d->args, "echo;E", 0);
        defineValues_CommandLine(&d->args, "go-home", 0);
        defineValues_CommandLine(&d->args, "help", 0);
        defineValues_CommandLine(&d->args, listTabUrls_CommandLineOption, 0);
        defineValuesN_CommandLine(&d->args, "new-tab", 0, 1);
        defineValues_CommandLine(&d->args, "sw", 0);
        defineValues_CommandLine(&d->args, "version;V", 0);
    }
    iStringList *openCmds = new_StringList();
    /* Handle command line options. */ {
        if (contains_CommandLine(&d->args, "help")) {
            puts(cstr_Block(&blobArghelp_Embedded));
            terminate_App_(0);
        }
        if (contains_CommandLine(&d->args, "version;V")) {
            printf("Lagrange version " LAGRANGE_APP_VERSION "\n");
            terminate_App_(0);
        }
        /* Check for URLs. */
        iBool newTab = iFalse;
        iConstForEach(CommandLine, i, &d->args) {
            const iRangecc arg = i.entry;
            if (i.argType == value_CommandLineArgType) {
                /* URLs and file paths accepted. */
                const iBool isKnownScheme =
                    startsWithCase_Rangecc(arg, "gemini:") || startsWithCase_Rangecc(arg, "gopher:") ||
                    startsWithCase_Rangecc(arg, "finger:") || startsWithCase_Rangecc(arg, "file:")   ||
                    startsWithCase_Rangecc(arg, "data:")   || startsWithCase_Rangecc(arg, "about:");
                if (isKnownScheme || fileExistsCStr_FileInfo(cstr_Rangecc(arg))) {
                    iString *decUrl =
                        isKnownScheme ? urlDecodeExclude_String(collectNewRange_String(arg), "/?#:")
                                      : makeFileUrl_String(collectNewRange_String(arg));
                    pushBack_StringList(openCmds,
                                        collectNewFormat_String(
                                            "open newtab:%d url:%s", newTab, cstr_String(decUrl)));
                    delete_String(decUrl);
                    newTab = iTrue;
                }
                else {
                    fprintf(stderr, "Invalid URL/file: %s\n", cstr_Rangecc(arg));
                    terminate_App_(1);
                }
            }
            else if (!isDefined_CommandLine(&d->args, collectNewRange_String(i.entry))) {
                fprintf(stderr, "Unknown option: %s\n", cstr_Rangecc(arg));
                terminate_App_(1);
            }
        }
    }
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
    printf("Lagrange: A Beautiful Gemini Client\n");
    const iBool isFirstRun =
        !fileExistsCStr_FileInfo(cleanedPath_CStr(concatPath_CStr(dataDir_App_(), "prefs.cfg")));
    d->isFinishedLaunching = iFalse;
    d->warmupFrames        = 0;
    d->launchCommands      = new_StringList();
    iZap(d->lastDropTime);
    init_SortedArray(&d->tickers, sizeof(iTicker), cmp_Ticker_);
    d->lastTickerTime         = SDL_GetTicks();
    d->elapsedSinceLastTicker = 0;
    d->commandEcho            = checkArgument_CommandLine(&d->args, "echo;E") != NULL;
    d->forceSoftwareRender    = checkArgument_CommandLine(&d->args, "sw") != NULL;
    d->initialWindowRect      = init_Rect(-1, -1, 900, 560);
#if defined (iPlatformMsys)
    /* Must scale by UI scaling factor. */
    mulfv_I2(&d->initialWindowRect.size, desktopDPI_Win32());
#endif
#if defined (iPlatformLinux)
    /* Scale by the primary (?) monitor DPI. */ {
        float vdpi;
        SDL_GetDisplayDPI(0, NULL, NULL, &vdpi);
        const float factor = vdpi / 96.0f;
        mulfv_I2(&d->initialWindowRect.size, iMax(factor, 1.0f));
    }
#endif
    init_Prefs(&d->prefs);
    setCStr_String(&d->prefs.downloadDir, downloadDir_App_());
    d->isRunning         = iFalse;
    d->window            = NULL;
    set_Atomic(&d->pendingRefresh, iFalse);
    d->mimehooks         = new_MimeHooks();
    d->certs             = new_GmCerts(dataDir_App_());
    d->visited           = new_Visited();
    d->bookmarks         = new_Bookmarks();
    d->tabEnum           = 0; /* generates unique IDs for tab pages */
    init_Periodic(&d->periodic);
    setThemePalette_Color(d->prefs.theme);
#if defined (iPlatformAppleDesktop)
    setupApplication_MacOS();
#endif
#if defined (iPlatformAppleMobile)
    setupApplication_iOS();
#endif
    init_Keys();
    loadPrefs_App_(d);
    load_Keys(dataDir_App_());
    d->window = new_Window(d->initialWindowRect);
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
        postCommand_App("open url:about:help");
    }
    postCommand_App("window.unfreeze");
    d->autoReloadTimer = SDL_AddTimer(60 * 1000, postAutoReloadCommand_App_, NULL);
    postCommand_App("document.autoreload");
#if defined (LAGRANGE_IDLE_SLEEP)
    d->isIdling      = iFalse;
    d->lastEventTime = 0;
    d->sleepTimer    = SDL_AddTimer(1000, checkAsleep_App_, d);
#endif
    d->isFinishedLaunching = iTrue;
    /* Run any commands that were pending completion of launch. */ {
        iForEach(StringList, i, d->launchCommands) {
            postCommandString_App(i.value);
        }
    }
    /* URLs from the command line. */ {
        iConstForEach(StringList, i, openCmds) {
            postCommandString_App(i.value);
        }
        iRelease(openCmds);
    }
    fetchRemote_Bookmarks(d->bookmarks);
}

static void deinit_App(iApp *d) {
#if defined (LAGRANGE_IDLE_SLEEP)
    SDL_RemoveTimer(d->sleepTimer);
#endif
    SDL_RemoveTimer(d->autoReloadTimer);
    saveState_App_(d);
    deinit_Feeds();
    save_Keys(dataDir_App_());
    deinit_Keys();
    savePrefs_App_(d);
    deinit_Prefs(&d->prefs);
    save_Bookmarks(d->bookmarks, dataDir_App_());
    delete_Bookmarks(d->bookmarks);
    save_Visited(d->visited, dataDir_App_());
    delete_Visited(d->visited);
    delete_GmCerts(d->certs);
    save_MimeHooks(d->mimehooks);
    delete_MimeHooks(d->mimehooks);
    delete_Window(d->window);
    d->window = NULL;
    deinit_CommandLine(&d->args);
    iRelease(d->launchCommands);
    delete_String(d->execPath);
    deinit_Ipc();
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
    return collect_String(cleaned_Path(&app_.prefs.downloadDir));
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
    format_String(msg, "# Debug information\n");
    appendFormat_String(msg, "## Documents\n");
    iForEach(ObjectList, k, iClob(listDocuments_App())) {
        iDocumentWidget *doc = k.object;
        appendFormat_String(msg, "### Tab %zu: %s\n",
                            childIndex_Widget(constAs_Widget(doc)->parent, k.object),
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
    iForEach(ObjectList, i, iClob(listDocuments_App())) {
        clearCache_History(history_DocumentWidget(i.object));
    }
}

void trimCache_App(void) {
    iApp *d = &app_;
    size_t cacheSize = 0;
    const size_t limit = d->prefs.maxCacheSize * 1000000;
    iObjectList *docs = listDocuments_App();
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

iLocalDef iBool isWaitingAllowed_App_(iApp *d) {
    if (!isEmpty_Periodic(&d->periodic)) {
        return iFalse;
    }
    if (d->warmupFrames > 0) {
        return iFalse;
    }
#if defined (LAGRANGE_IDLE_SLEEP)
    if (d->isIdling) {
        return iFalse;
    }
#endif
#if defined (iPlatformMobile)
    if (!isFinished_Anim(&d->window->rootOffset)) {
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

void processEvents_App(enum iAppEventMode eventMode) {
    iApp *d = &app_;
    SDL_Event ev;
    iBool gotEvents = iFalse;
    postCommands_Periodic(&d->periodic);
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
                    postCommand_App("prefs.dismiss");
                    processEvents_App(postedEventsOnly_AppEventMode);
                }
                goto backToMainLoop;
            case SDL_APP_LOWMEMORY:
                clearCache_App_();
                break;
            case SDL_APP_DIDENTERFOREGROUND:
                gotEvents = iTrue;
                d->warmupFrames = 5;
#if defined (LAGRANGE_IDLE_SLEEP)
                d->isIdling = iFalse;
                d->lastEventTime = SDL_GetTicks();
#endif
                postRefresh_App();
                break;
            case SDL_APP_WILLENTERBACKGROUND:
            case SDL_APP_TERMINATING:
                savePrefs_App_(d);
                saveState_App_(d);
                break;
            case SDL_DROPFILE: {
                iBool wasUsed = processEvent_Window(d->window, &ev);
                if (!wasUsed) {
                    iBool newTab = iFalse;
                    if (elapsedSeconds_Time(&d->lastDropTime) < 0.1) {
                        /* Each additional drop gets a new tab. */
                        newTab = iTrue;
                    }
                    d->lastDropTime = now_Time();
                    if (startsWithCase_CStr(ev.drop.file, "gemini:") ||
                        startsWithCase_CStr(ev.drop.file, "file:")) {
                        postCommandf_App("~open newtab:%d url:%s", newTab, ev.drop.file);
                    }
                    else {
                        postCommandf_App(
                            "~open newtab:%d url:%s", newTab, makeFileUrl_CStr(ev.drop.file));
                    }
                }
                break;
            }
            default: {
#if defined (LAGRANGE_IDLE_SLEEP)
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
#if defined (iPlatformAppleDesktop)
                    /* On macOS, we handle both trackpad and mouse events. We expect SDL to identify
                       which device is sending the event. */
                    if (ev.wheel.which == 0) {
                        /* Trackpad with precise scrolling w/inertia (points). */
                        setPerPixel_MouseWheelEvent(&ev.wheel, iTrue);
                        ev.wheel.x *= -d->window->pixelRatio;
                        ev.wheel.y *= d->window->pixelRatio;
                        /* Only scroll on one axis at a time. */
                        if (iAbs(ev.wheel.x) > iAbs(ev.wheel.y)) {
                            ev.wheel.y = 0;
                        }
                        else {
                            ev.wheel.x = 0;
                        }
                    }
                    else {
                        /* Disregard wheel acceleration applied by the OS. */
                        ev.wheel.x = -ev.wheel.x;
                        ev.wheel.y = iSign(ev.wheel.y);
                    }
#endif
#if defined (iPlatformMsys)
                    ev.wheel.x = -ev.wheel.x;
#endif
                }
                iBool wasUsed = processEvent_Window(d->window, &ev);
                if (!wasUsed) {
                    /* There may be a key bindings for this. */
                    wasUsed = processEvent_Keys(&ev);
                }
                if (ev.type == SDL_USEREVENT && ev.user.code == command_UserEventCode) {
#if defined (iPlatformAppleDesktop)
                    handleCommand_MacOS(command_UserEvent(&ev));
#endif
                    if (isMetricsChange_UserEvent(&ev)) {
                        arrange_Widget(d->window->root);
                    }
                    if (!wasUsed) {
                        /* No widget handled the command, so we'll do it. */
                        handleCommand_App(ev.user.data1);
                    }
                    /* Allocated by postCommand_Apps(). */
                    free(ev.user.data1);
                }
                break;
            }
        }
    }
#if defined (LAGRANGE_IDLE_SLEEP)
    if (d->isIdling && !gotEvents && isFinished_Anim(&d->window->rootOffset)) {
        /* This is where we spend most of our time when idle. 60 Hz still quite a lot but we
           can't wait too long after the user tries to interact again with the app. In any
           case, on macOS SDL_WaitEvent() seems to use 10x more CPU time than sleeping. */
        SDL_Delay(1000 / 60);
    }
#endif
backToMainLoop:;
}

static void runTickers_App_(iApp *d) {
    const uint32_t now = SDL_GetTicks();
    d->elapsedSinceLastTicker = (d->lastTickerTime ? now - d->lastTickerTime : 0);
    d->lastTickerTime = now;
    if (isEmpty_SortedArray(&d->tickers)) {
        d->lastTickerTime = 0;
        return;
    }
    /* Tickers may add themselves again, so we'll run off a copy. */
    iSortedArray *pending = copy_SortedArray(&d->tickers);
    clear_SortedArray(&d->tickers);
    postRefresh_App();
    iConstForEach(Array, i, &pending->values) {
        const iTicker *ticker = i.value;
        if (ticker->callback) {
            ticker->callback(ticker->context);
        }
    }
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
        resetFonts_Text(); {
            SDL_Event u = { .type = SDL_USEREVENT };
            u.user.code = command_UserEventCode;
            u.user.data1 = strdup("theme.changed");
            /*u.user.windowID = id_Window(d->window);*/
            dispatchEvent_Widget(d->window->root, &u);
        }
#endif
        drawWhileResizing_Window(d->window, winev->data1, winev->data2);
    }
    return 0;
}

static int run_App_(iApp *d) {
    arrange_Widget(findWidget_App("root"));
    d->isRunning = iTrue;
    SDL_EventState(SDL_DROPFILE, SDL_ENABLE); /* open files via drag'n'drop */
#if defined (iPlatformDesktop)
    SDL_AddEventWatch(resizeWatcher_, d); /* redraw window during resizing */
#endif
    while (d->isRunning) {
        processEvents_App(waitForNewEvents_AppEventMode);
        runTickers_App_(d);
        refresh_App();
        recycle_Garbage();
    }
    SDL_DelEventWatch(resizeWatcher_, d);
    return 0;
}

void refresh_App(void) {
    iApp *d = &app_;
    destroyPending_Widget();
#if defined (LAGRANGE_IDLE_SLEEP)
    if (d->warmupFrames == 0 && d->isIdling) {
        return;
    }
#endif
    if (!exchange_Atomic(&d->pendingRefresh, iFalse)) {
        /* Refreshing wasn't pending. */
        if (isFinished_Anim(&d->window->rootOffset)) {
            return;
        }
    }
//    iTime draw;
//    initCurrent_Time(&draw);
    draw_Window(d->window);
//    printf("draw: %lld \u03bcs\n", (long long) (elapsedSeconds_Time(&draw) * 1000000));
//    fflush(stdout);
    if (d->warmupFrames > 0) {
        d->warmupFrames--;
    }
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

enum iColorTheme colorTheme_App(void) {
    return app_.prefs.theme;
}

const iString *schemeProxy_App(iRangecc scheme) {
    iApp *d = &app_;
    const iString *proxy = NULL;
    if (equalCase_Rangecc(scheme, "gemini")) {
        proxy = &d->prefs.geminiProxy;
    }
    else if (equalCase_Rangecc(scheme, "gopher")) {
        proxy = &d->prefs.gopherProxy;
    }
    else if (equalCase_Rangecc(scheme, "http") || equalCase_Rangecc(scheme, "https")) {
        proxy = &d->prefs.httpProxy;
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
#if defined (LAGRANGE_IDLE_SLEEP)
    d->isIdling = iFalse;
#endif
    const iBool wasPending = exchange_Atomic(&d->pendingRefresh, iTrue);
    if (!wasPending) {
        SDL_Event ev = { .type = SDL_USEREVENT };
        ev.user.code = refresh_UserEventCode;
        //ev.user.windowID = id_Window(get_Window());
        SDL_PushEvent(&ev);
    }
}

void postCommand_App(const char *command) {
    iApp *d = &app_;
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
        if (!d->isFinishedLaunching) {
            pushBackCStr_StringList(d->launchCommands, command);
            return;
        }
    }
    SDL_Event ev = { .type = SDL_USEREVENT };
    ev.user.code = command_UserEventCode;
    /*ev.user.windowID = id_Window(get_Window());*/
    ev.user.data1 = strdup(command);
    SDL_PushEvent(&ev);
    if (app_.commandEcho) {
        printf("[command] %s\n", command); fflush(stdout);
    }
}

void postCommandf_App(const char *command, ...) {
    iBlock chars;
    init_Block(&chars, 0);
    va_list args;
    va_start(args, command);
    vprintf_Block(&chars, command, args);
    va_end(args);
    postCommand_App(cstr_Block(&chars));
    deinit_Block(&chars);
}

iAny *findWidget_App(const char *id) {
    if (!*id) return NULL;
    return findChild_Widget(app_.window->root, id);
}

void addTicker_App(iTickerFunc ticker, iAny *context) {
    iApp *d = &app_;
    insert_SortedArray(&d->tickers, &(iTicker){ context, ticker });
    postRefresh_App();
}

void removeTicker_App(iTickerFunc ticker, iAny *context) {
    iApp *d = &app_;
    remove_SortedArray(&d->tickers, &(iTicker){ context, ticker });
}

iMimeHooks *mimeHooks_App(void) {
    return app_.mimehooks;
}

iPeriodic *periodic_App(void) {
    return &app_.periodic;
}

iBool isLandscape_App(void) {
    const iInt2 size = rootSize_Window(get_Window());
    return size.x > size.y;
}

enum iAppDeviceType deviceType_App(void) {
#if defined (iPlatformAppleMobile)
    return isPhone_iOS() ? phone_AppDeviceType : tablet_AppDeviceType;
#else
    return desktop_AppDeviceType;
#endif
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

static void updateDropdownSelection_(iLabelWidget *dropButton, const char *selectedCommand) {
    iWidget *menu = findChild_Widget(as_Widget(dropButton), "menu");
    iForEach(ObjectList, i, children_Widget(menu)) {
        if (isInstance_Object(i.object, &Class_LabelWidget)) {
            iLabelWidget *item = i.object;
            const iBool isSelected = endsWith_String(command_LabelWidget(item), selectedCommand);
            setFlags_Widget(as_Widget(item), selected_WidgetFlag, isSelected);
            if (isSelected) {
                updateText_LabelWidget(dropButton, sourceText_LabelWidget(item));
            }
        }
    }
}

static void updateColorThemeButton_(iLabelWidget *button, int theme) {
    if (!button) return;
    updateDropdownSelection_(button, format_CStr(".set arg:%d", theme));
}

static void updateFontButton_(iLabelWidget *button, int font) {
    if (!button) return;
    updateDropdownSelection_(button, format_CStr(".set arg:%d", font));
}

static iBool handlePrefsCommands_(iWidget *d, const char *cmd) {
    if (equal_Command(cmd, "prefs.dismiss") || equal_Command(cmd, "preferences")) {
        setUiScale_Window(get_Window(),
                          toFloat_String(text_InputWidget(findChild_Widget(d, "prefs.uiscale"))));
#if defined (LAGRANGE_DOWNLOAD_EDIT)
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
        postCommandf_App("ostheme arg:%d",
                         isSelected_Widget(findChild_Widget(d, "prefs.ostheme")));
        postCommandf_App("decodeurls arg:%d",
                         isSelected_Widget(findChild_Widget(d, "prefs.decodeurls")));
        postCommandf_App("searchurl address:%s",
                         cstr_String(text_InputWidget(findChild_Widget(d, "prefs.searchurl"))));
        postCommandf_App("cachesize.set arg:%d",
                         toInt_String(text_InputWidget(findChild_Widget(d, "prefs.cachesize"))));
        postCommandf_App("ca.file path:%s",
                         cstr_String(text_InputWidget(findChild_Widget(d, "prefs.ca.file"))));
        postCommandf_App("ca.path path:%s",
                         cstr_String(text_InputWidget(findChild_Widget(d, "prefs.ca.path"))));
        postCommandf_App("proxy.gemini address:%s",
                         cstr_String(text_InputWidget(findChild_Widget(d, "prefs.proxy.gemini"))));
        postCommandf_App("proxy.gopher address:%s",
                         cstr_String(text_InputWidget(findChild_Widget(d, "prefs.proxy.gopher"))));
        postCommandf_App("proxy.http address:%s",
                         cstr_String(text_InputWidget(findChild_Widget(d, "prefs.proxy.http"))));
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
        updateDropdownSelection_(findChild_Widget(d, "prefs.uilang"),
                                 cstr_String(string_Command(cmd, "id")));
        return iFalse;
    }
    else if (equal_Command(cmd, "quoteicon.set")) {
        const int arg = arg_Command(cmd);
        setFlags_Widget(findChild_Widget(d, "prefs.quoteicon.0"), selected_WidgetFlag, arg == 0);
        setFlags_Widget(findChild_Widget(d, "prefs.quoteicon.1"), selected_WidgetFlag, arg == 1);
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
    else if (equal_Command(cmd, "font.set")) {
        updateFontButton_(findChild_Widget(d, "prefs.font"), arg_Command(cmd));
        return iFalse;
    }
    else if (equal_Command(cmd, "headingfont.set")) {
        updateFontButton_(findChild_Widget(d, "prefs.headingfont"), arg_Command(cmd));
        return iFalse;
    }
    else if (equal_Command(cmd, "prefs.ostheme.changed")) {
        postCommandf_App("ostheme arg:%d", arg_Command(cmd));
    }
    else if (equal_Command(cmd, "theme.changed")) {
        updatePrefsThemeButtons_(d);
        if (!argLabel_Command(cmd, "auto")) {
            setToggle_Widget(findChild_Widget(d, "prefs.ostheme"), iFalse);
        }
    }
    return iFalse;
}

iDocumentWidget *document_App(void) {
    return iConstCast(iDocumentWidget *, currentTabPage_Widget(findWidget_App("doctabs")));
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
    iApp *d = &app_;
    iWidget *tabs = findWidget_App("doctabs");
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
    setId_Widget(as_Widget(doc), format_CStr("document%03d", ++d->tabEnum));
    appendTabPage_Widget(tabs, as_Widget(doc), "", 0, 0);
    iRelease(doc); /* now owned by the tabs */
    addChild_Widget(findChild_Widget(tabs, "tabs.buttons"), iClob(newTabButton));
    if (switchToNew) {
        postCommandf_App("tabs.switch page:%p", doc);
    }
    arrange_Widget(tabs);
    refresh_Widget(tabs);
    postCommandf_App("tab.created id:%s", cstr_String(id_Widget(as_Widget(doc))));
    return doc;
}

static iBool handleIdentityCreationCommands_(iWidget *dlg, const char *cmd) {
    iApp *d = &app_;
    if (equal_Command(cmd, "ident.temp.changed")) {
        setFlags_Widget(
            findChild_Widget(dlg, "ident.temp.note"), hidden_WidgetFlag, !arg_Command(cmd));
        return iFalse;
    }
    if (equal_Command(cmd, "ident.accept") || equal_Command(cmd, "cancel")) {
        if (equal_Command(cmd, "ident.accept")) {
            const iString *commonName   = text_InputWidget (findChild_Widget(dlg, "ident.common"));
            const iString *email        = text_InputWidget (findChild_Widget(dlg, "ident.email"));
            const iString *userId       = text_InputWidget (findChild_Widget(dlg, "ident.userid"));
            const iString *domain       = text_InputWidget (findChild_Widget(dlg, "ident.domain"));
            const iString *organization = text_InputWidget (findChild_Widget(dlg, "ident.org"));
            const iString *country      = text_InputWidget (findChild_Widget(dlg, "ident.country"));
            const iBool    isTemp       = isSelected_Widget(findChild_Widget(dlg, "ident.temp"));
            if (isEmpty_String(commonName)) {
                makeMessage_Widget(orange_ColorEscape "MISSING INFO",
                                   "A \"Common name\" must be specified.");
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
                    makeMessage_Widget(orange_ColorEscape "INVALID DATE",
                                       "Please check the \"Valid until\" date. Examples:\n"
                                       "\u2022 2030\n"
                                       "\u2022 2025-06-30\n"
                                       "\u2022 2021-12-31 23:59:59");
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
                        makeMessage_Widget(orange_ColorEscape "INVALID DATE",
                                           "Expiration date must be in the future.");
                        return iTrue;
                    }
                }
            }
            /* The input seems fine. */
            newIdentity_GmCerts(d->certs, isTemp ? temporary_GmIdentityFlag : 0,
                                until, commonName, email, userId, domain, organization, country);
            postCommandf_App("sidebar.mode arg:%d show:1", identities_SidebarMode);
            postCommand_App("idents.changed");
        }
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
    if (isEmpty_String(&d->prefs.searchUrl)) {
        return collectNew_String();
    }
    const iString *escaped = urlEncode_String(queryStringUnescaped);
    return collectNewFormat_String("%s?%s", cstr_String(&d->prefs.searchUrl), cstr_String(escaped));
}

iBool handleCommand_App(const char *cmd) {
    iApp *d = &app_;
    if (equal_Command(cmd, "config.error")) {
        makeMessage_Widget(uiTextCaution_ColorEscape "CONFIG ERROR",
                           format_CStr("Error in config file: %s\nSee \"about:debug\" for details.",
                                       suffixPtr_Command(cmd, "where")));
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
        if (!equal_String(lang, &d->prefs.uiLanguage)) {
            set_String(&d->prefs.uiLanguage, lang);
            setCurrent_Lang(cstr_String(&d->prefs.uiLanguage));
            postCommand_App("lang.changed");
        }
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
    else if (equal_Command(cmd, "window.maximize")) {
        if (!argLabel_Command(cmd, "toggle")) {
            setSnap_Window(d->window, maximized_WindowSnap);
        }
        else {
            setSnap_Window(d->window, snap_Window(d->window) == maximized_WindowSnap ? 0 :
                           maximized_WindowSnap);
        }
        return iTrue;
    }
    else if (equal_Command(cmd, "window.fullscreen")) {
        const iBool wasFull = snap_Window(d->window) == fullscreen_WindowSnap;
        setSnap_Window(d->window, wasFull ? 0 : fullscreen_WindowSnap);
        postCommandf_App("window.fullscreen.changed arg:%d", !wasFull);
        return iTrue;
    }
    else if (equal_Command(cmd, "font.set")) {
        setFreezeDraw_Window(get_Window(), iTrue);
        d->prefs.font = arg_Command(cmd);
        setContentFont_Text(d->prefs.font);
        postCommand_App("font.changed");
        postCommand_App("window.unfreeze");
        return iTrue;
    }
    else if (equal_Command(cmd, "headingfont.set")) {
        setFreezeDraw_Window(get_Window(), iTrue);
        d->prefs.headingFont = arg_Command(cmd);
        setHeadingFont_Text(d->prefs.headingFont);
        postCommand_App("font.changed");
        postCommand_App("window.unfreeze");
        return iTrue;
    }
    else if (equal_Command(cmd, "zoom.set")) {
        setFreezeDraw_Window(get_Window(), iTrue); /* no intermediate draws before docs updated */
        d->prefs.zoomPercent = arg_Command(cmd);
        setContentFontSize_Text((float) d->prefs.zoomPercent / 100.0f);
        postCommand_App("font.changed");
        postCommand_App("window.unfreeze");
        return iTrue;
    }
    else if (equal_Command(cmd, "zoom.delta")) {
        setFreezeDraw_Window(get_Window(), iTrue); /* no intermediate draws before docs updated */
        int delta = arg_Command(cmd);
        if (d->prefs.zoomPercent < 100 || (delta < 0 && d->prefs.zoomPercent == 100)) {
            delta /= 2;
        }
        d->prefs.zoomPercent = iClamp(d->prefs.zoomPercent + delta, 50, 200);
        setContentFontSize_Text((float) d->prefs.zoomPercent / 100.0f);
        postCommand_App("font.changed");
        postCommand_App("window.unfreeze");
        return iTrue;
    }
    else if (equal_Command(cmd, "smoothscroll")) {
        d->prefs.smoothScrolling = arg_Command(cmd);
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
            showToolbars_Window(d->window, iTrue);
        }
        return iTrue;
    }
    else if (equal_Command(cmd, "theme.set")) {
        const int isAuto = argLabel_Command(cmd, "auto");
        d->prefs.theme = arg_Command(cmd);
        if (!isAuto) {
            postCommand_App("ostheme arg:0");
        }
        setThemePalette_Color(d->prefs.theme);
        postCommandf_App("theme.changed auto:%d", isAuto);
        return iTrue;
    }
    else if (equal_Command(cmd, "accent.set")) {
        d->prefs.accent = arg_Command(cmd);
        setThemePalette_Color(d->prefs.theme);
        postCommandf_App("theme.changed auto:1");
        return iTrue;
    }
    else if (equal_Command(cmd, "ostheme")) {
        d->prefs.useSystemTheme = arg_Command(cmd);
        return iTrue;
    }
    else if (equal_Command(cmd, "doctheme.dark.set")) {
        d->prefs.docThemeDark = arg_Command(cmd);
        postCommand_App("theme.changed auto:1");
        return iTrue;
    }
    else if (equal_Command(cmd, "doctheme.light.set")) {
        d->prefs.docThemeLight = arg_Command(cmd);
        postCommand_App("theme.changed auto:1");
        return iTrue;
    }
    else if (equal_Command(cmd, "linewidth.set")) {
        d->prefs.lineWidth = iMax(20, arg_Command(cmd));
        postCommand_App("document.layout.changed");
        return iTrue;
    }
    else if (equal_Command(cmd, "quoteicon.set")) {
        d->prefs.quoteIcon = arg_Command(cmd) != 0;
        postCommand_App("document.layout.changed");
        return iTrue;
    }
    else if (equal_Command(cmd, "prefs.mono.gemini.changed") ||
             equal_Command(cmd, "prefs.mono.gopher.changed")) {
        const iBool isSet = (arg_Command(cmd) != 0);
        setFreezeDraw_Window(d->window, iTrue);
        if (startsWith_CStr(cmd, "prefs.mono.gemini")) {
            d->prefs.monospaceGemini = isSet;
        }
        else {
            d->prefs.monospaceGopher = isSet;
        }
        resetFonts_Text(); /* clear the glyph cache */
        postCommand_App("font.changed");
        postCommand_App("window.unfreeze");
        return iTrue;
    }
    else if (equal_Command(cmd, "prefs.boldlink.dark.changed") ||
             equal_Command(cmd, "prefs.boldlink.light.changed")) {
        const iBool isSet = (arg_Command(cmd) != 0);
        if (startsWith_CStr(cmd, "prefs.boldlink.dark")) {
            d->prefs.boldLinkDark = isSet;
        }
        else {
            d->prefs.boldLinkLight = isSet;
        }
        resetFonts_Text(); /* clear the glyph cache */
        postCommand_App("font.changed");
        return iTrue;
    }
    else if (equal_Command(cmd, "prefs.biglede.changed")) {
        d->prefs.bigFirstParagraph = arg_Command(cmd) != 0;
        postCommand_App("document.layout.changed");
        return iTrue;
    }
    else if (equal_Command(cmd, "prefs.plaintext.wrap.changed")) {
        d->prefs.plainTextWrap = arg_Command(cmd) != 0;
        postCommand_App("document.layout.changed");
        return iTrue;
    }
    else if (equal_Command(cmd, "prefs.sideicon.changed")) {
        d->prefs.sideIcon = arg_Command(cmd) != 0;
        postRefresh_App();
        return iTrue;
    }
    else if (equal_Command(cmd, "prefs.centershort.changed")) {
        d->prefs.centerShortDocs = arg_Command(cmd) != 0;
        postCommand_App("theme.changed");
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
    else if (equal_Command(cmd, "saturation.set")) {
        d->prefs.saturation = (float) arg_Command(cmd) / 100.0f;
        postCommandf_App("theme.changed auto:1");
        return iTrue;
    }
    else if (equal_Command(cmd, "cachesize.set")) {
        d->prefs.maxCacheSize = arg_Command(cmd);
        if (d->prefs.maxCacheSize <= 0) {
            d->prefs.maxCacheSize = 0;
        }
        return iTrue;
    }
    else if (equal_Command(cmd, "searchurl")) {
        iString *url = &d->prefs.searchUrl;
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
        setCStr_String(&d->prefs.geminiProxy, suffixPtr_Command(cmd, "address"));
        return iTrue;
    }
    else if (equal_Command(cmd, "proxy.gopher")) {
        setCStr_String(&d->prefs.gopherProxy, suffixPtr_Command(cmd, "address"));
        return iTrue;
    }
    else if (equal_Command(cmd, "proxy.http")) {
        setCStr_String(&d->prefs.httpProxy, suffixPtr_Command(cmd, "address"));
        return iTrue;
    }
    else if (equal_Command(cmd, "downloads")) {
        setCStr_String(&d->prefs.downloadDir, suffixPtr_Command(cmd, "path"));
        return iTrue;
    }
    else if (equal_Command(cmd, "ca.file")) {
        setCStr_String(&d->prefs.caFile, suffixPtr_Command(cmd, "path"));
        if (!argLabel_Command(cmd, "noset")) {
            setCACertificates_TlsRequest(&d->prefs.caFile, &d->prefs.caPath);
        }
        return iTrue;
    }
    else if (equal_Command(cmd, "ca.path")) {
        setCStr_String(&d->prefs.caPath, suffixPtr_Command(cmd, "path"));
        if (!argLabel_Command(cmd, "noset")) {
            setCACertificates_TlsRequest(&d->prefs.caFile, &d->prefs.caPath);
        }
        return iTrue;
    }
    else if (equal_Command(cmd, "open")) {
        iString *url = collectNewCStr_String(suffixPtr_Command(cmd, "url"));
        const iBool noProxy = argLabel_Command(cmd, "noproxy");
        iUrl parts;
        init_Url(&parts, url);
        if (argLabel_Command(cmd, "default") || equalCase_Rangecc(parts.scheme, "mailto") ||
            ((noProxy || isEmpty_String(&d->prefs.httpProxy)) &&
             (equalCase_Rangecc(parts.scheme, "http") ||
              equalCase_Rangecc(parts.scheme, "https")))) {
            openInDefaultBrowser_App(url);
            return iTrue;
        }
        iDocumentWidget *doc = document_Command(cmd);
        const int newTab = argLabel_Command(cmd, "newtab");
        if (newTab) {
            doc = newTab_App(NULL, (newTab & 1) != 0); /* `newtab:2` to open in background */
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
        showCollapsed_Widget(findWidget_App("document.progress"), iFalse);
        if (prefs_App()->decodeUserVisibleURLs) {
            urlDecodePath_String(url);
        }
        else {
            urlEncodePath_String(url);
        }
        setUrlFromCache_DocumentWidget(doc, url, isHistory);
        /* Optionally, jump to a text in the document. This will only work if the document
           is already available, e.g., it's from "about:" or restored from cache. */
        const iRangecc gotoHeading = range_Command(cmd, "gotoheading");
        if (gotoHeading.start) {
            postCommandf_App("document.goto heading:%s", cstr_Rangecc(gotoHeading));
        }
        const iRangecc gotoUrlHeading = range_Command(cmd, "gotourlheading");
        if (gotoUrlHeading.start) {
            postCommandf_App("document.goto heading:%s",
                             cstrCollect_String(urlDecode_String(
                                 collect_String(newRange_String(gotoUrlHeading)))));
        }
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
#if defined (iPlatformAppleMobile)
        /* Can't close the last on mobile. */
        if (tabCount_Widget(tabs) == 1) {
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
        if (tabCount_Widget(tabs) > 1) {
            iWidget *closed = removeTabPage_Widget(tabs, index);
            destroy_Widget(closed); /* released later */
            if (index == tabCount_Widget(tabs)) {
                index--;
            }
            arrange_Widget(tabs);
            if (wasCurrent) {
                postCommandf_App("tabs.switch page:%p", tabPage_Widget(tabs, index));
            }
        }
        else {
            postCommand_App("quit");
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
        setText_InputWidget(findChild_Widget(dlg, "prefs.downloads"), &d->prefs.downloadDir);
        setToggle_Widget(findChild_Widget(dlg, "prefs.hoverlink"), d->prefs.hoverLink);
        setToggle_Widget(findChild_Widget(dlg, "prefs.smoothscroll"), d->prefs.smoothScrolling);
        setToggle_Widget(findChild_Widget(dlg, "prefs.imageloadscroll"), d->prefs.loadImageInsteadOfScrolling);
        setToggle_Widget(findChild_Widget(dlg, "prefs.hidetoolbarscroll"), d->prefs.hideToolbarOnScroll);
        setToggle_Widget(findChild_Widget(dlg, "prefs.ostheme"), d->prefs.useSystemTheme);
        setToggle_Widget(findChild_Widget(dlg, "prefs.customframe"), d->prefs.customFrame);
        updateDropdownSelection_(findChild_Widget(dlg, "prefs.uilang"), cstr_String(&d->prefs.uiLanguage));
        setToggle_Widget(findChild_Widget(dlg, "prefs.retainwindow"), d->prefs.retainWindowSize);
        setText_InputWidget(findChild_Widget(dlg, "prefs.uiscale"),
                            collectNewFormat_String("%g", uiScale_Window(d->window)));
        setFlags_Widget(findChild_Widget(dlg, format_CStr("prefs.font.%d", d->prefs.font)),
                        selected_WidgetFlag,
                        iTrue);
        setFlags_Widget(
            findChild_Widget(dlg, format_CStr("prefs.headingfont.%d", d->prefs.headingFont)),
            selected_WidgetFlag,
            iTrue);
        setFlags_Widget(findChild_Widget(dlg, "prefs.mono.gemini"),
                        selected_WidgetFlag,
                        d->prefs.monospaceGemini);
        setFlags_Widget(findChild_Widget(dlg, "prefs.mono.gopher"),
                        selected_WidgetFlag,
                        d->prefs.monospaceGopher);
        setFlags_Widget(findChild_Widget(dlg, "prefs.boldlink.dark"),
                        selected_WidgetFlag,
                        d->prefs.boldLinkDark);
        setFlags_Widget(findChild_Widget(dlg, "prefs.boldlink.light"),
                        selected_WidgetFlag,
                        d->prefs.boldLinkLight);
        setFlags_Widget(
            findChild_Widget(dlg, format_CStr("prefs.linewidth.%d", d->prefs.lineWidth)),
            selected_WidgetFlag,
            iTrue);
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
        updateFontButton_(findChild_Widget(dlg, "prefs.font"), d->prefs.font);
        updateFontButton_(findChild_Widget(dlg, "prefs.headingfont"), d->prefs.headingFont);
        setFlags_Widget(
            findChild_Widget(
                dlg, format_CStr("prefs.saturation.%d", (int) (d->prefs.saturation * 3.99f))),
            selected_WidgetFlag,
            iTrue);
        setText_InputWidget(findChild_Widget(dlg, "prefs.cachesize"),
                            collectNewFormat_String("%d", d->prefs.maxCacheSize));
        setToggle_Widget(findChild_Widget(dlg, "prefs.decodeurls"), d->prefs.decodeUserVisibleURLs);
        setText_InputWidget(findChild_Widget(dlg, "prefs.searchurl"), &d->prefs.searchUrl);
        setText_InputWidget(findChild_Widget(dlg, "prefs.ca.file"), &d->prefs.caFile);
        setText_InputWidget(findChild_Widget(dlg, "prefs.ca.path"), &d->prefs.caPath);
        setText_InputWidget(findChild_Widget(dlg, "prefs.proxy.gemini"), &d->prefs.geminiProxy);
        setText_InputWidget(findChild_Widget(dlg, "prefs.proxy.gopher"), &d->prefs.gopherProxy);
        setText_InputWidget(findChild_Widget(dlg, "prefs.proxy.http"), &d->prefs.httpProxy);
        iWidget *tabs = findChild_Widget(dlg, "prefs.tabs");
        if (tabs) {
            showTabPage_Widget(tabs, tabPage_Widget(tabs, d->prefs.dialogTab));
        }
        setCommandHandler_Widget(dlg, handlePrefsCommands_);
    }
    else if (equal_Command(cmd, "navigate.home")) {
        /* Look for bookmarks tagged "homepage". */
        iRegExp *pattern = iClob(new_RegExp("\\bhomepage\\b", caseInsensitive_RegExpOption));
        const iPtrArray *homepages =
            list_Bookmarks(d->bookmarks, NULL, filterTagsRegExp_Bookmarks, pattern);
        if (isEmpty_PtrArray(homepages)) {
            postCommand_App("open url:about:lagrange");
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
                postCommandf_App(
                    "open url:%s",
                    cstr_String(constAt_StringSet(urls, iRandoms(0, size_StringSet(urls)))));
            }
        }
        if (argLabel_Command(cmd, "focus")) {
            postCommand_App("navigate.focus");
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
        postCommand_App("focus.set id:bmed.title");
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
    else if (equal_Command(cmd, "feeds.update.started")) {
        showCollapsed_Widget(findWidget_App("feeds.progress"), iTrue);
        return iFalse;
    }
    else if (equal_Command(cmd, "feeds.update.finished")) {
        showCollapsed_Widget(findWidget_App("feeds.progress"), iFalse);
        refreshFinished_Feeds();
        postRefresh_App();
        return iFalse;
    }
    else if (equal_Command(cmd, "visited.changed")) {
        save_Visited(d->visited, dataDir_App_());
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
        addChild_Widget(d->window->root, iClob(imp));
        postRefresh_App();
        return iTrue;
    }
    else if (equal_Command(cmd, "ident.signin")) {
        const iString *url = collect_String(suffix_Command(cmd, "url"));
        signIn_GmCerts(
            d->certs,
            findIdentity_GmCerts(d->certs, collect_Block(hexDecode_Rangecc(range_Command(cmd, "ident")))),
            url);
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
        postCommand_App("idents.changed");
        return iTrue;
    }
    else if (equal_Command(cmd, "idents.changed")) {
        saveIdentities_GmCerts(d->certs);
        return iFalse;
    }
    else if (equal_Command(cmd, "os.theme.changed")) {
        if (d->prefs.useSystemTheme) {
            const int dark     = argLabel_Command(cmd, "dark");
            const int contrast = argLabel_Command(cmd, "contrast");
            postCommandf_App("theme.set arg:%d auto:1",
                             dark ? (contrast ? pureBlack_ColorTheme : dark_ColorTheme)
                                  : (contrast ? pureWhite_ColorTheme : light_ColorTheme));
        }
        return iFalse;
    }
    else if (equal_Command(cmd, "ipc.list.urls")) {
        iProcessId pid = argLabel_Command(cmd, "pid");
        if (pid) {
            iString *urls = collectNew_String();
            iConstForEach(ObjectList, i, iClob(listDocuments_App())) {
                append_String(urls, url_DocumentWidget(i.object));
                appendCStr_String(urls, "\n");
            }
            write_Ipc(pid, urls, response_IpcWrite);
        }
        return iTrue;
    }
    else if (equal_Command(cmd, "ipc.signal")) {
        signal_Ipc(arg_Command(cmd));
        return iTrue;
    }
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
