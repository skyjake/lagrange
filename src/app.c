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
#include <SDL_events.h>
#include <SDL_filesystem.h>
#include <SDL_render.h>
#include <SDL_timer.h>
#include <SDL_video.h>
#include <SDL_version.h>

#include <stdio.h>
#include <stdarg.h>
#include <errno.h>

#if defined (iPlatformApple) && !defined (iPlatformIOS)
#   include "macos.h"
#endif
#if defined (iPlatformMsys)
#   include "win32.h"
#endif
#if SDL_VERSION_ATLEAST(2, 0, 14)
#   include <SDL_misc.h>
#endif

iDeclareType(App)

#if defined (iPlatformApple)
#define EMB_BIN "../../Resources/resources.lgr"
static const char *dataDir_App_ = "~/Library/Application Support/fi.skyjake.Lagrange";
#endif
#if defined (iPlatformMsys)
#define EMB_BIN "../resources.lgr"
static const char *dataDir_App_ = "~/AppData/Roaming/fi.skyjake.Lagrange";
#endif
#if defined (iPlatformLinux) || defined (iPlatformOther)
#define EMB_BIN  "../../share/lagrange/resources.lgr"
static const char *dataDir_App_ = "~/.config/lagrange";
#endif
#if defined (LAGRANGE_EMB_BIN) /* specified in build config */
#  undef EMB_BIN
#  define EMB_BIN LAGRANGE_EMB_BIN
#endif
#define EMB_BIN2 "../resources.lgr" /* fallback from build/executable dir */
static const char *prefsFileName_App_    = "prefs.cfg";
static const char *oldStateFileName_App_ = "state.binary";
static const char *stateFileName_App_    = "state.lgr";
static const char *downloadDir_App_      = "~/Downloads";

static const int idleThreshold_App_ = 1000; /* ms */

struct Impl_App {
    iCommandLine args;
    iString *    execPath;
    iMimeHooks * mimehooks;
    iGmCerts *   certs;
    iVisited *   visited;
    iBookmarks * bookmarks;
    iWindow *    window;
    iSortedArray tickers;
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
    /* Preferences: */
    iBool        commandEcho;         /* --echo */
    iBool        forceSoftwareRender; /* --sw */
    iRect        initialWindowRect;
    iPrefs       prefs;
};

static iApp app_;

iDeclareType(Ticker)

struct Impl_Ticker {
    iAny *context;
    void (*callback)(iAny *);
};

static int cmp_Ticker_(const void *a, const void *b) {
    const iTicker *elems[2] = { a, b };
    return iCmp(elems[0]->context, elems[1]->context);
}

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
    appendFormat_String(str, "window.retain arg:%d\n", d->prefs.retainWindowSize);
    if (d->prefs.retainWindowSize) {
        const iBool isMaximized = (SDL_GetWindowFlags(d->window->win) & SDL_WINDOW_MAXIMIZED) != 0;
        int w, h, x, y;
        x = d->window->lastRect.pos.x;
        y = d->window->lastRect.pos.y;
        w = d->window->lastRect.size.x;
        h = d->window->lastRect.size.y;
        appendFormat_String(str, "window.setrect width:%d height:%d coord:%d %d\n", w, h, x, y);
        appendFormat_String(str, "sidebar.width arg:%d\n", width_SidebarWidget(sidebar));
        appendFormat_String(str, "sidebar2.width arg:%d\n", width_SidebarWidget(sidebar2));
        /* On macOS, maximization should be applied at creation time or the window will take
           a moment to animate to its maximized size. */
#if !defined (iPlatformApple)
        if (isMaximized) {
            appendFormat_String(str, "~window.maximize\n");
        }
#else
        iUnused(isMaximized);
#endif
    }
    /* Sidebars. */ {
        if (isVisible_Widget(sidebar)) {
            appendCStr_String(str, "sidebar.toggle\n");
        }
        appendFormat_String(str, "sidebar.mode arg:%d\n", mode_SidebarWidget(sidebar));
        if (isVisible_Widget(sidebar2)) {
            appendCStr_String(str, "sidebar2.toggle\n");
        }
        appendFormat_String(str, "sidebar2.mode arg:%d\n", mode_SidebarWidget(sidebar2));
    }
    appendFormat_String(str, "uiscale arg:%f\n", uiScale_Window(d->window));
    appendFormat_String(str, "prefs.dialogtab arg:%d\n", d->prefs.dialogTab);
    appendFormat_String(str, "font.set arg:%d\n", d->prefs.font);
    appendFormat_String(str, "headingfont.set arg:%d\n", d->prefs.headingFont);
    appendFormat_String(str, "prefs.mono.gemini.changed arg:%d\n", d->prefs.monospaceGemini);
    appendFormat_String(str, "prefs.mono.gopher.changed arg:%d\n", d->prefs.monospaceGopher);
    appendFormat_String(str, "zoom.set arg:%d\n", d->prefs.zoomPercent);
    appendFormat_String(str, "smoothscroll arg:%d\n", d->prefs.smoothScrolling);
    appendFormat_String(str, "imageloadscroll arg:%d\n", d->prefs.loadImageInsteadOfScrolling);
    appendFormat_String(str, "decodeurls arg:%d\n", d->prefs.decodeUserVisibleURLs);
    appendFormat_String(str, "linewidth.set arg:%d\n", d->prefs.lineWidth);
    appendFormat_String(str, "prefs.biglede.changed arg:%d\n", d->prefs.bigFirstParagraph);
    appendFormat_String(str, "prefs.sideicon.changed arg:%d\n", d->prefs.sideIcon);
    appendFormat_String(str, "quoteicon.set arg:%d\n", d->prefs.quoteIcon ? 1 : 0);
    appendFormat_String(str, "prefs.hoverlink.changed arg:%d\n", d->prefs.hoverLink);
    appendFormat_String(str, "theme.set arg:%d auto:1\n", d->prefs.theme);
    appendFormat_String(str, "ostheme arg:%d\n", d->prefs.useSystemTheme);
    appendFormat_String(str, "doctheme.dark.set arg:%d\n", d->prefs.docThemeDark);
    appendFormat_String(str, "doctheme.light.set arg:%d\n", d->prefs.docThemeLight);
    appendFormat_String(str, "saturation.set arg:%d\n", (int) ((d->prefs.saturation * 100) + 0.5f));
    appendFormat_String(str, "proxy.gemini address:%s\n", cstr_String(&d->prefs.geminiProxy));
    appendFormat_String(str, "proxy.gopher address:%s\n", cstr_String(&d->prefs.gopherProxy));
    appendFormat_String(str, "proxy.http address:%s\n", cstr_String(&d->prefs.httpProxy));
    appendFormat_String(str, "downloads path:%s\n", cstr_String(&d->prefs.downloadDir));
    return str;
}

static const iString *prefsFileName_(void) {
    return collect_String(concatCStr_Path(&iStringLiteral(dataDir_App_), prefsFileName_App_));
}

static void loadPrefs_App_(iApp *d) {
    iUnused(d);
    /* Create the data dir if it doesn't exist yet. */
    makeDirs_Path(collectNewCStr_String(dataDir_App_));
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
            else if (equal_Command(cmd, "window.setrect")) {
                const iInt2 pos = coord_Command(cmd);
                d->initialWindowRect = init_Rect(
                    pos.x, pos.y, argLabel_Command(cmd, "width"), argLabel_Command(cmd, "height"));
            }
            else {
                postCommandString_App(&cmdStr);
            }
            deinit_String(&cmdStr);
        }
        delete_String(str);
    }
    else {
        /* default preference values */
    }
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
    const char *oldPath = concatPath_CStr(dataDir_App_, oldStateFileName_App_);
    const char *path    = concatPath_CStr(dataDir_App_, stateFileName_App_);
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
    iFile *f = newCStr_File(concatPath_CStr(dataDir_App_, stateFileName_App_));
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
    iRelease(f);
}

#if defined (LAGRANGE_IDLE_SLEEP)
static uint32_t checkAsleep_App_(uint32_t interval, void *param) {
    iApp *d = param;
    SDL_Event ev = { .type = SDL_USEREVENT };
    ev.user.code = asleep_UserEventCode;
    SDL_PushEvent(&ev);
    return interval;
}
#endif

static void init_App_(iApp *d, int argc, char **argv) {
    const iBool isFirstRun = !fileExistsCStr_FileInfo(cleanedPath_CStr(dataDir_App_));
    d->isFinishedLaunching = iFalse;
    d->launchCommands      = new_StringList();
    iZap(d->lastDropTime);
    init_CommandLine(&d->args, argc, argv);
    /* Where was the app started from? */ {
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
    init_SortedArray(&d->tickers, sizeof(iTicker), cmp_Ticker_);
    d->lastTickerTime         = SDL_GetTicks();
    d->elapsedSinceLastTicker = 0;
    d->commandEcho            = checkArgument_CommandLine(&d->args, "echo") != NULL;
    d->forceSoftwareRender    = checkArgument_CommandLine(&d->args, "sw") != NULL;
    d->initialWindowRect      = init_Rect(-1, -1, 900, 560);
#if defined (iPlatformMsys)
    /* Must scale by UI scaling factor. */
    mulfv_I2(&d->initialWindowRect.size, desktopDPI_Win32());
#endif
    init_Prefs(&d->prefs);
    setCStr_String(&d->prefs.downloadDir, downloadDir_App_);
    d->isRunning         = iFalse;
    d->window            = NULL;
    set_Atomic(&d->pendingRefresh, iFalse);
    d->mimehooks         = new_MimeHooks();
    d->certs             = new_GmCerts(dataDir_App_);
    d->visited           = new_Visited();
    d->bookmarks         = new_Bookmarks();
    d->tabEnum           = 0; /* generates unique IDs for tab pages */
    setThemePalette_Color(d->prefs.theme);
#if defined (LAGRANGE_IDLE_SLEEP)
    d->isIdling      = iFalse;
    d->lastEventTime = 0;
    d->sleepTimer    = SDL_AddTimer(1000, checkAsleep_App_, d);
#endif
#if defined (iPlatformApple)
    setupApplication_MacOS();
#endif
    init_Keys();
    loadPrefs_App_(d);
    load_Keys(dataDir_App_);
    load_Visited(d->visited, dataDir_App_);
    load_Bookmarks(d->bookmarks, dataDir_App_);
    load_MimeHooks(d->mimehooks, dataDir_App_);
    if (isFirstRun) {
        /* Create the default bookmarks for a quick start. */
        add_Bookmarks(d->bookmarks,
                      collectNewCStr_String("gemini://gemini.circumlunar.space/"),
                      collectNewCStr_String("Project Gemini"),
                      NULL,
                      0x264a /* Gemini symbol */);
        add_Bookmarks(d->bookmarks,
                      collectNewCStr_String("gemini://gemini.circumlunar.space/capcom/"),
                      collectNewCStr_String("CAPCOM Geminispace aggregator"),
                      NULL,
                      0x264a /* Gemini symbol */);
        add_Bookmarks(d->bookmarks,
                      collectNewCStr_String("gemini://gus.guru/"),
                      collectNewCStr_String("GUS - Gemini Universal Search"),
                      NULL,
                      0x2690);
        add_Bookmarks(d->bookmarks,
                      collectNewCStr_String("gemini://skyjake.fi/lagrange/"),
                      collectNewCStr_String("Lagrange"),
                      NULL,
                      0x1f306);
    }
#if defined (iHaveLoadEmbed)
    /* Load the resources from a file. */ {
        if (!load_Embed(concatPath_CStr(cstr_String(execPath_App()), EMB_BIN))) {
            if (!load_Embed(concatPath_CStr(cstr_String(execPath_App()), EMB_BIN2))) {
                fprintf(stderr, "failed to load resources: %s\n", strerror(errno));
                exit(-1);
            }
        }
    }
#endif
    d->window = new_Window(d->initialWindowRect);
    init_Feeds(dataDir_App_);
    /* Widget state init. */
    processEvents_App(postedEventsOnly_AppEventMode);
    if (!loadState_App_(d)) {
        postCommand_App("navigate.home");
    }
    postCommand_App("window.unfreeze");
    d->isFinishedLaunching = iTrue;
    /* Run any commands that were pending completion of launch. */ {
        iForEach(StringList, i, d->launchCommands) {
            postCommandString_App(i.value);
        }
    }
    /* URLs from the command line. */ {
        iBool newTab = iFalse;
        for (size_t i = 1; i < size_StringList(args_CommandLine(&d->args)); i++) {
            const iString *arg = constAt_StringList(args_CommandLine(&d->args), i);
            const iBool    isKnownScheme =
                startsWithCase_String(arg, "gemini:") || startsWithCase_String(arg, "gopher:") ||
                startsWithCase_String(arg, "file:")   || startsWithCase_String(arg, "data:")   ||
                startsWithCase_String(arg, "about:");
            if (isKnownScheme || fileExists_FileInfo(arg)) {
                postCommandf_App("open newtab:%d url:%s",
                                 newTab,
                                 isKnownScheme ? cstr_String(arg)
                                               : cstrCollect_String(makeFileUrl_String(arg)));
                newTab = iTrue;
            }
        }
    }
}

static void deinit_App(iApp *d) {
    saveState_App_(d);
    deinit_Feeds();
    save_Keys(dataDir_App_);
    deinit_Keys();
    savePrefs_App_(d);
    deinit_Prefs(&d->prefs);
    save_Bookmarks(d->bookmarks, dataDir_App_);
    delete_Bookmarks(d->bookmarks);
    save_Visited(d->visited, dataDir_App_);
    delete_Visited(d->visited);
    delete_GmCerts(d->certs);
    save_MimeHooks(d->mimehooks);
    delete_MimeHooks(d->mimehooks);
    deinit_SortedArray(&d->tickers);
    delete_Window(d->window);
    d->window = NULL;
    deinit_CommandLine(&d->args);
    iRelease(d->launchCommands);
    delete_String(d->execPath);
    iRecycle();
}

const iString *execPath_App(void) {
    return app_.execPath;
}

const iString *dataDir_App(void) {
    return collect_String(cleanedCStr_Path(dataDir_App_));
}

const iString *downloadDir_App(void) {
    return collect_String(cleaned_Path(&app_.prefs.downloadDir));
}

const iString *debugInfo_App(void) {
    iApp *d = &app_;
    iString *msg = collectNew_String();
    format_String(msg, "# Debug information\n");
    appendFormat_String(msg, "## Documents\n");
    iForEach(ObjectList, k, listDocuments_App()) {
        iDocumentWidget *doc = k.object;
        appendFormat_String(msg, "### Tab %zu: %s\n",
                            childIndex_Widget(constAs_Widget(doc)->parent, k.object),
                            cstr_String(bookmarkTitle_DocumentWidget(doc)));
        append_String(msg, collect_String(debugInfo_History(history_DocumentWidget(doc))));
    }
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

iLocalDef iBool isWaitingAllowed_App_(iApp *d) {
#if defined (LAGRANGE_IDLE_SLEEP)
    if (d->isIdling) {
        return iFalse;
    }
#endif
    return !value_Atomic(&d->pendingRefresh) && isEmpty_SortedArray(&d->tickers);
}

void processEvents_App(enum iAppEventMode eventMode) {
    iApp *d = &app_;
    SDL_Event ev;
    iBool gotEvents = iFalse;
    while ((isWaitingAllowed_App_(d) && eventMode == waitForNewEvents_AppEventMode &&
            SDL_WaitEvent(&ev)) ||
           ((!isWaitingAllowed_App_(d) || eventMode == postedEventsOnly_AppEventMode) &&
            SDL_PollEvent(&ev))) {
        switch (ev.type) {
            case SDL_QUIT:
                d->isRunning = iFalse;
                goto backToMainLoop;
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
                    if (SDL_GetTicks() - d->lastEventTime > idleThreshold_App_) {
                        if (!d->isIdling) {
//                            printf("[App] idling...\n");
                            fflush(stdout);
                        }
                        d->isIdling = iTrue;
                    }
                    continue;
                }
                d->lastEventTime = SDL_GetTicks();
                if (d->isIdling) {
//                    printf("[App] ...woke up\n");
                    fflush(stdout);
                }
                d->isIdling = iFalse;
#endif
                gotEvents = iTrue;
                iBool wasUsed = processEvent_Window(d->window, &ev);
                if (!wasUsed) {
                    /* There may be a key bindings for this. */
                    wasUsed = processEvent_Keys(&ev);
                }
                if (ev.type == SDL_USEREVENT && ev.user.code == command_UserEventCode) {
#if defined (iPlatformApple) && !defined (iPlatformIOS)
                    handleCommand_MacOS(command_UserEvent(&ev));
#endif
                    if (isCommand_UserEvent(&ev, "metrics.changed")) {
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
    if (d->isIdling && !gotEvents) {
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
            u.user.windowID = SDL_GetWindowID(d->window->win);
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
    SDL_AddEventWatch(resizeWatcher_, d);
    while (d->isRunning) {
        processEvents_App(waitForNewEvents_AppEventMode);
        runTickers_App_(d);
        refresh_App();
        recycle_Garbage();
    }
    return 0;
}

void refresh_App(void) {
    iApp *d = &app_;
#if defined (LAGRANGE_IDLE_SLEEP)
    if (d->isIdling) return;
#endif
    destroyPending_Widget();
    draw_Window(d->window);
    set_Atomic(&d->pendingRefresh, iFalse);
}

iBool isRefreshPending_App(void) {
    return value_Atomic(&app_.pendingRefresh);
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
        SDL_Event ev;
        ev.user.type     = SDL_USEREVENT;
        ev.user.code     = refresh_UserEventCode;
        ev.user.windowID = get_Window() ? SDL_GetWindowID(get_Window()->win) : 0;
        ev.user.data1    = NULL;
        ev.user.data2    = NULL;
        SDL_PushEvent(&ev);
    }
}

void postCommand_App(const char *command) {
    iApp *d = &app_;
    iAssert(command);
    SDL_Event ev;
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
    ev.user.type     = SDL_USEREVENT;
    ev.user.code     = command_UserEventCode;
    ev.user.windowID = get_Window() ? SDL_GetWindowID(get_Window()->win) : 0;
    ev.user.data1    = strdup(command);
    ev.user.data2    = NULL;
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
}

static void updateColorThemeButton_(iLabelWidget *button, int theme) {
    const char *mode    = strstr(cstr_String(id_Widget(as_Widget(button))), ".dark") ? "dark" : "light";
    const char *command = format_CStr("doctheme.%s.set arg:%d", mode, theme);
    iForEach(ObjectList, i, children_Widget(findChild_Widget(as_Widget(button), "menu"))) {
        iLabelWidget *item = i.object;
        if (!cmp_String(command_LabelWidget(item), command)) {
            updateText_LabelWidget(button, text_LabelWidget(item));
            break;
        }
    }
}

static iBool handlePrefsCommands_(iWidget *d, const char *cmd) {
    if (equal_Command(cmd, "prefs.dismiss") || equal_Command(cmd, "preferences")) {
        setUiScale_Window(get_Window(),
                          toFloat_String(text_InputWidget(findChild_Widget(d, "prefs.uiscale"))));
        postCommandf_App("downloads path:%s",
                         cstr_String(text_InputWidget(findChild_Widget(d, "prefs.downloads"))));
        postCommandf_App("window.retain arg:%d",
                         isSelected_Widget(findChild_Widget(d, "prefs.retainwindow")));
        postCommandf_App("smoothscroll arg:%d",
                         isSelected_Widget(findChild_Widget(d, "prefs.smoothscroll")));
        postCommandf_App("imageloadscroll arg:%d",
                         isSelected_Widget(findChild_Widget(d, "prefs.imageloadscroll")));
        postCommandf_App("ostheme arg:%d",
                         isSelected_Widget(findChild_Widget(d, "prefs.ostheme")));
        postCommandf_App("decodeurls arg:%d",
                         isSelected_Widget(findChild_Widget(d, "prefs.decodeurls")));
        postCommandf_App("proxy.gemini address:%s",
                         cstr_String(text_InputWidget(findChild_Widget(d, "prefs.proxy.gemini"))));
        postCommandf_App("proxy.gopher address:%s",
                         cstr_String(text_InputWidget(findChild_Widget(d, "prefs.proxy.gopher"))));
        postCommandf_App("proxy.http address:%s",
                         cstr_String(text_InputWidget(findChild_Widget(d, "prefs.proxy.http"))));
        const iWidget *tabs = findChild_Widget(d, "prefs.tabs");
        postCommandf_App("prefs.dialogtab arg:%u",
                         tabPageIndex_Widget(tabs, currentTabPage_Widget(tabs)));
        destroy_Widget(d);
        return iTrue;
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
                if (n <= 0 || val[0] < (unsigned) today.year) {
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

iBool handleCommand_App(const char *cmd) {
    iApp *d = &app_;
    if (equal_Command(cmd, "config.error")) {
        makeMessage_Widget(uiTextCaution_ColorEscape "CONFIG ERROR",
                           format_CStr("Error in config file: %s\nSee \"about:debug\" for details.",
                                       suffixPtr_Command(cmd, "where")));
        return iTrue;
    }
    else if (equal_Command(cmd, "prefs.dialogtab")) {
        d->prefs.dialogTab = arg_Command(cmd);
        return iTrue;
    }
    else if (equal_Command(cmd, "window.retain")) {
        d->prefs.retainWindowSize = arg_Command(cmd);
        return iTrue;
    }
    else if (equal_Command(cmd, "window.maximize")) {
        SDL_MaximizeWindow(d->window->win);
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
    else if (equal_Command(cmd, "prefs.biglede.changed")) {
        d->prefs.bigFirstParagraph = arg_Command(cmd) != 0;
        postCommand_App("document.layout.changed");
        return iTrue;
    }
    else if (equal_Command(cmd, "prefs.sideicon.changed")) {
        d->prefs.sideIcon = arg_Command(cmd) != 0;
        postRefresh_App();
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
            doc = newTab_App(NULL, (newTab & 1) != 0); /* "newtab:2" to open in background */
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
        setFlags_Widget(findWidget_App("document.progress"), hidden_WidgetFlag, iTrue);
        if (prefs_App()->decodeUserVisibleURLs) {
            urlDecodePath_String(url);
        }
        else {
            urlEncodePath_String(url);
        }
       
        /* Prevent address bar spoofing (mentioned as IDN homograph attack
        in issue 73) */
        punyEncodeUrlHost_String(url);
       
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
        iWidget *      tabs  = findWidget_App("doctabs");
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
        setToggle_Widget(findChild_Widget(dlg, "prefs.ostheme"), d->prefs.useSystemTheme);
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
        setFlags_Widget(
            findChild_Widget(dlg, format_CStr("prefs.linewidth.%d", d->prefs.lineWidth)),
            selected_WidgetFlag,
            iTrue);
        setFlags_Widget(
            findChild_Widget(dlg, format_CStr("prefs.quoteicon.%d", d->prefs.quoteIcon)),
            selected_WidgetFlag,
            iTrue);
        setToggle_Widget(findChild_Widget(dlg, "prefs.biglede"), d->prefs.bigFirstParagraph);
        setToggle_Widget(findChild_Widget(dlg, "prefs.sideicon"), d->prefs.sideIcon);
        updateColorThemeButton_(findChild_Widget(dlg, "prefs.doctheme.dark"), d->prefs.docThemeDark);
        updateColorThemeButton_(findChild_Widget(dlg, "prefs.doctheme.light"), d->prefs.docThemeLight);
        setFlags_Widget(
            findChild_Widget(
                dlg, format_CStr("prefs.saturation.%d", (int) (d->prefs.saturation * 3.99f))),
            selected_WidgetFlag,
            iTrue);
        setToggle_Widget(findChild_Widget(dlg, "prefs.decodeurls"), d->prefs.decodeUserVisibleURLs);
        setText_InputWidget(findChild_Widget(dlg, "prefs.proxy.gemini"), &d->prefs.geminiProxy);
        setText_InputWidget(findChild_Widget(dlg, "prefs.proxy.gopher"), &d->prefs.gopherProxy);
        setText_InputWidget(findChild_Widget(dlg, "prefs.proxy.http"), &d->prefs.httpProxy);
        iWidget *tabs = findChild_Widget(dlg, "prefs.tabs");
        showTabPage_Widget(tabs, tabPage_Widget(tabs, d->prefs.dialogTab));
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
        save_Bookmarks(d->bookmarks, dataDir_App_);
        return iFalse;
    }
    else if (equal_Command(cmd, "feeds.refresh")) {
        refresh_Feeds();
        return iTrue;
    }
    else if (equal_Command(cmd, "feeds.update.started")) {
        setFlags_Widget(findWidget_App("feeds.progress"), hidden_WidgetFlag, iFalse);
        postRefresh_App();
        return iFalse;
    }
    else if (equal_Command(cmd, "feeds.update.finished")) {
        setFlags_Widget(findWidget_App("feeds.progress"), hidden_WidgetFlag, iTrue);
        refreshFinished_Feeds();
        postRefresh_App();
        return iFalse;
    }
    else if (equal_Command(cmd, "visited.changed")) {
        save_Visited(d->visited, dataDir_App_);
        return iFalse;
    }
    else if (equal_Command(cmd, "ident.new")) {
        iWidget *dlg = makeIdentityCreation_Widget();
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
    iProcess *proc = new_Process();
    setArguments_Process(proc,
#if defined (iPlatformApple)
                         iClob(newStringsCStr_StringList("/usr/bin/env", "open", cstr_String(url), NULL))
#elif defined (iPlatformLinux) || defined (iPlatformOther)
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
}

void revealPath_App(const iString *path) {
#if defined (iPlatformApple)
    const char *scriptPath = concatPath_CStr(dataDir_App_, "revealfile.scpt");
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
#elif defined (iPlatformLinux)
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
