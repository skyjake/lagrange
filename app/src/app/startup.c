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
#include <lagrange/gmrequest.h>
#include <lagrange/mimehooks.h>
#include <lagrange/resources.h>
#include <lagrange/sitespec.h>
#include <lagrange/snippets.h>
#include <lagrange/visited.h>
#include "gempub.h"
#include "gmutil.h"
#include "history.h"
#include "ipc.h"
#include "misfin.h"
#include "periodic.h"
#include "render/text.h"
#include "ui/command.h"
#include "ui/documentwidget.h"
#include "ui/gamepad.h"
#include "ui/keys.h"
#include "ui/root.h"
#include "ui/touch.h"
#include "ui/util.h"
#include "ui/window.h"
#include "updater.h"

#include <the_Foundation/buffer.h>
#include <the_Foundation/commandline.h>
#include <the_Foundation/file.h>
#include <the_Foundation/fileinfo.h>
#include <the_Foundation/garbage.h>
#include <the_Foundation/path.h>
#include <the_Foundation/process.h>
#include <the_Foundation/socket.h>
#include <the_Foundation/sortedarray.h>
#include <the_Foundation/stringset.h>
#include <the_Foundation/thread.h>
#include <the_Foundation/version.h>

#include <stdio.h>
#include <errno.h>

#if defined (iPlatformAppleDesktop)
#   include "platform/macos.h"
#endif
#if defined (iPlatformAppleMobile)
#   include "platform/ios.h"
#   include <CoreFoundation/CoreFoundation.h>
#endif
#if defined (iPlatformAndroidMobile)
#   include "platform/android.h"
#   include <SDL3/SDL_log.h>
#   include <fcntl.h>
#   include <unistd.h>
#endif
#if defined (iPlatformMsys) || defined (iPlatformWindows)
#   include "platform/win32.h"
#endif
#if defined (LAGRANGE_ENABLE_X11_XLIB)
#   include "platform/x11.h"
#endif
#if SDL_VERSION_ATLEAST(2, 0, 14)
#   include <SDL3/SDL_misc.h>
#endif

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
            iCommandLineArg *arg = iClob(argument_CommandLineConstIterator(&i));
            if (!isEmpty_StringList(&arg->values)) {
                appendFormat_String(cmds, "open newtab:1 url:%s\n",
                                    cstr_String(constAt_StringList(&arg->values, 0)));
            }
            else {
                appendCStr_String(cmds, "tabs.new\n");
            }
            requestRaise = iTrue;
        }
        else if (equal_CommandLineConstIterator(&i, "close-tab")) {
            appendCStr_String(cmds, "tabs.close\n");
        }
        else if (equal_CommandLineConstIterator(&i, uiTheme_CommandLineOption)) {
            iCommandLineArg *arg = iClob(argument_CommandLineConstIterator(&i));
            if (arg) {
                static const char *themeNames[] = {
                    "black", "dark", "light", "white"
                };
                iForIndices(n, themeNames) {
                    if (!cmp_String(value_CommandLineArg(arg, 0), themeNames[n])) {
                        appendFormat_String(cmds, "theme.set arg:%u\n", n);
                        break;
                    }
                }
            }
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
    static const char *schemes[] = { "gemini:", "gopher:", "finger:", "spartan:", "nex:",
                                     "misfin:", "guppy:",  "file:",   "data:",    "about:" };
    iForIndices(i, schemes) {
        if (startsWithCase_Rangecc(uri, schemes[i])) {
            return iTrue;
        }
    }
    return iFalse;
}

static const iString *openableCommandLineArgUriValue_(const iString *arg) {
    /* URLs and file paths are accepted. */
    /* NOTE: Invalid contents in arg will cause a fatal error, terminating the app. */
    if (hasCommandLineOpenableScheme_(range_String(arg))) {
        return urlDecodeExclude_String(arg, "/?#:");
    }
    else if (fileExists_FileInfo(arg)) {
        return makeFileUrl_String(arg);
    }
    else {
        fprintf(stderr, "Invalid URL/file: %s\n", cstr_String(arg));
        terminate_App_(1);
        return NULL; /* unreachable */
    }
}

static iMutex     *dumpMutex_;
static iCondition *dumpFinishedCondition_;
static int         dumpCount_;

static void dumpRequestFinished_App_(void *obj, iGmRequest *req) {
    iUnused(obj);
    lock_Mutex(dumpMutex_);
    const iBlock *body = body_GmRequest(req);
    fprintf(stderr,
            "URL: %s\nLength: %zu\nHeader: %d %s\n",
            cstr_String(url_GmRequest(req)),
            size_Block(body),
            status_GmRequest(req),
            cstr_String(meta_GmRequest(req)));
    fwrite(constData_Block(body), size_Block(body), 1, stdout);
    if (--dumpCount_ == 0) {
        signal_Condition(dumpFinishedCondition_);
    }
    unlock_Mutex(dumpMutex_);
}

static const iBlock *aboutDebugPage_(iRangecc path, iRangecc query) {
    iUnused(query);
    if (equalCase_Rangecc(path, "debug")) {
        return utf8_String(debugInfo_App());
    }
    return NULL;
}

static const iBlock *aboutBlankPage_(iRangecc path, iRangecc query) {
    iUnused(query);
    if (equalCase_Rangecc(path, "blank")) {
        return utf8_String(collectNewCStr_String("\n"));
    }
    return NULL;
}

static const iBlock *aboutLagrangePage_(iRangecc path, iRangecc query) {
    if (!equalCase_Rangecc(path, "lagrange")) {
        return NULL;
    }
    /* The "Powered by" line names the libraries that this build actually uses. */
    iString *powered = collect_String(copy_String(string_Lang("about.powered")));
    replace_String(powered, "OpenSSL", libraryName_TlsRequest());
    replace_String(powered,
                   "SDL 2",
                   isTerminal_Platform()
                       ? "ncurses"
                       : format_CStr("SDL %d", SDL_VERSIONNUM_MAJOR(SDL_GetVersion())));
    iString *page = collect_String(newBlock_String(aboutPageSource_Resources(path, query)));
    replace_String(page, "${about.powered}", cstr_String(powered));
    return utf8_String(page);
}

#if defined (LAGRANGE_HANDLE_SIGTERM)
static void postQuitOnSigTerm_(int sig) {
    iUnused(sig);
    SDL_PushEvent(&(SDL_Event){ .type = SDL_EVENT_QUIT });
}
#endif

static void init_App_(iApp *d, int argc, char **argv) {
    iBool doDump = iFalse;
    if (!saveMutex_App_) {
        saveMutex_App_ = new_Mutex();
    }
#if defined (iPlatformAndroid)
    /* Internal storage may be limited in size. */
    migrateInternalUserDirToExternalStorage_App_(d);
#endif
#if defined (iPlatformLinux) && !defined (iPlatformAndroid) && !defined (iPlatformTerminal)
    d->isRunningUnderWayland      = !iCmpStr(SDL_GetCurrentVideoDriver(), "wayland");
    d->isRunningUnderWindowSystem = d->isRunningUnderWayland ||
                                    !iCmpStr(SDL_GetCurrentVideoDriver(), "x11");
#else
    d->isRunningUnderWayland      = iFalse;
    d->isRunningUnderWindowSystem = iTrue;
#endif
    d->isTextInputActive = iFalse;
    d->isDarkSystemTheme = iTrue; /* will be updated by system later on, if supported */
    d->isSuspended = iFalse;
    d->tempFilesPendingDeletion = new_StringSet();
    d->recentlyClosedTabUrls = new_StringList();
    d->recentlySubmittedInput = new_StringArray();
    d->savedWidths = new_StringHash();
    d->overrideDataPath = NULL;
    d->didCheckDataPathOption = iFalse;
    init_Array(&d->initialWindowRects, sizeof(iRect));
    init_Array(&d->initialWindowDesktops, sizeof(int));
    init_CommandLine(&d->args, argc, argv);
    /* Where was the app started from? We ask SDL first because the command line alone
       cannot be relied on (behavior differs depending on OS). */ {
        const char *exec = SDL_GetBasePath();
        if (exec) {
            d->execPath = newCStr_String(concatPath_CStr(
                exec, cstr_Rangecc(baseName_Path(executablePath_CommandLine(&d->args)))));
        }
        else {
            d->execPath = copy_String(executablePath_CommandLine(&d->args));
        }
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
#if defined (EMB_BIN3) /* another alternative location */
            concatPath_CStr(execPath, EMB_BIN3),
#endif
            concatPath_CStr(execPath, EMB_BIN),
            "resources.lgr" /* cwd */
        };
        iBool wasLoaded = iFalse;
#if defined (iPlatformAndroidMobile)
        /* Resources are APK assets; must be read via SDL_IOStream. */
        iForIndices(i, paths) {
            SDL_IOStream *io = SDL_IOFromFile(paths[i], "rb");
            if (io) {
                iBlock buf;
                init_Block(&buf, (size_t) SDL_GetIOSize(io));
                SDL_ReadIO(io, data_Block(&buf), size_Block(&buf));
                SDL_CloseIO(io);
                wasLoaded = initData_Resources(&buf);
                deinit_Block(&buf);
                if (wasLoaded) break;
            }
        }
#else
        iForIndices(i, paths) {
            if (init_Resources(paths[i])) {
                wasLoaded = iTrue;
                break;
            }
        }
#endif
        if (!wasLoaded) {
            fprintf(stderr, "failed to load resources: %s\n", strerror(errno));
            exit(-1);
        }
    }
    init_Lang();
    /* Register "about:" page handlers. The Lagrange page must be checked before the
       generic resource handler, which would also serve it. */
    addAboutHandler_GmRequest(aboutLagrangePage_);
    addAboutHandler_GmRequest(aboutPageSource_Resources);
    addAboutHandler_GmRequest(aboutDebugPage_);
    addAboutHandler_GmRequest(aboutBlankPage_);
    iStringList *openCmds = new_StringList();
#if !defined (iPlatformAndroidMobile)
    /* Configure the valid command line options. */ {
        defineValues_CommandLine(&d->args, "capslock", 0);
        defineValues_CommandLine(&d->args, "close-tab", 0);
        defineValues_CommandLine(&d->args, dump_CommandLineOption, 0);
        defineValues_CommandLine(&d->args, dumpIdentity_CommandLineOption, 1);
        defineValues_CommandLine(&d->args, "echo;E", 0);
        defineValues_CommandLine(&d->args, "go-home", 0);
        defineValues_CommandLine(&d->args, "help", 0);
        defineValues_CommandLine(&d->args, listTabUrls_CommandLineOption, 0);
        defineValuesN_CommandLine(&d->args, "new-tab", 0, 1);
        defineValues_CommandLine(&d->args, openUrlOrSearch_CommandLineOption, 1);
        defineValues_CommandLine(&d->args, "prefs-sheet", 0);
        defineValues_CommandLine(&d->args, replaceTab_CommandLineOption, 1);
        defineValues_CommandLine(&d->args, "sw", 0);
        defineValues_CommandLine(&d->args, "tab-url", 0);
        defineValues_CommandLine(&d->args, uiTheme_CommandLineOption, 1);
        defineValues_CommandLine(&d->args, userDataDir_CommandLineOption, 1);
        defineValues_CommandLine(&d->args, "version;V", 0);
        defineValues_CommandLine(&d->args, windowHeight_CommandLineOption, 1);
        defineValues_CommandLine(&d->args, windowWidth_CommandLineOption, 1);
    }
    doDump = checkArgument_CommandLine(&d->args, dump_CommandLineOption);
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
                pushBack_StringList(
                    openCmds,
                    collectNewFormat_String(
                        "open newtab:1 url:%s",
                        cstr_String(openableCommandLineArgUriValue_(collectNewRange_String(arg)))));
            }
            else if (equal_CommandLineConstIterator(&i, "capslock")) {
                d->prefs.capsLockKeyModifier = iTrue;
            }
            else if (equal_CommandLineConstIterator(&i, replaceTab_CommandLineOption)) {
                /* Replace the current tab's URL. */
                const iCommandLineArg *arg = iClob(argument_CommandLineConstIterator(&i));
                const iString *input = value_CommandLineArg(arg, 0);
                pushBack_StringList(
                    openCmds,
                    collectNewFormat_String("open url:%s",
                                            cstr_String(openableCommandLineArgUriValue_(input))));
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
       are not shareable. */
    if (!doDump) {
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
    if (!doDump) {
        puts("Lagrange: A Beautiful Gemini Client");
    }
    const iBool isFirstRun =
        !fileExistsCStr_FileInfo(cleanedPath_CStr(concatPath_CStr(dataDir_App_(), prefsFileName_App_)));
    d->isFinishedLaunching = iFalse;
    d->isLoadingPrefs      = iFalse;
    d->warmupFrames        = 0;
    d->launchCommands      = new_StringList();
    iZap(d->lastDropTime);
    init_SortedArray(&d->tickers, sizeof(iTicker), cmp_Ticker_);
    d->lastTickerTime         = SDL_GetTicks();
    d->elapsedSinceLastTicker = 0;
    d->commandEcho            = contains_CommandLine(&d->args, "echo;E");
#if defined (iPlatformAndroidMobile)
    d->lastBackButtonTime = 0;
# ifndef NDEBUG
    d->commandEcho = iTrue;
# endif
#endif
    d->forceSoftwareRender    = contains_CommandLine(&d->args, "sw");
#if defined (iPlatformMsys) || defined (iPlatformWindows)
    if (d->commandEcho) {
        enableConsoleOutput_Win32();
    }
#endif
    init_Prefs(&d->prefs);
    d->prefs.detachedPrefs = !contains_CommandLine(&d->args, "prefs-sheet");
    init_SiteSpec(dataDir_App_());
    init_Snippets(dataDir_App_());
    init_Misfin(dataDir_App_());
    setCStr_String(&d->prefs.strings[downloadDir_PrefsString], downloadDir_App_());
    set_Atomic(&d->pendingRefresh, iFalse);
    d->isRunning = iFalse;
    d->window    = NULL;
    d->mimehooks = new_MimeHooks();
    d->certs     = new_GmCerts(dataDir_App_());
    d->visited   = new_Visited();
    d->bookmarks = new_Bookmarks();
    d->lastVisitedSaveTime = 0;
    d->pendingVisitedSave  = iFalse;
    setup_Gempub();
    /* Dumping requested pages. */
    if (doDump) {
        const iGmIdentity *ident = NULL;
        const iCommandLineArg *arg =
            iClob(checkArgumentValues_CommandLine(&d->args, dumpIdentity_CommandLineOption, 1));
        if (arg) {
            ident = findIdentityFuzzy_GmCerts(d->certs, value_CommandLineArg(arg, 0));
            if (ident) {
                fprintf(stderr, "Identity: %s\n", cstr_String(name_GmIdentity(ident)));
            }
        }
        dumpMutex_ = new_Mutex();
        dumpFinishedCondition_ = new_Condition();
        dumpCount_ = size_StringList(openCmds);
        if (dumpCount_ == 0) {
            deinit_Foundation();
            exit(0);
        }
        iForEach(StringList, i, openCmds) {
            iGmRequest *req = iClob(new_GmRequest(d->certs));
            setUrl_GmRequest(req, collect_String(suffix_Command(cstr_String(i.value), "url")));
            setIdentity_GmRequest(req, ident);
            enableFilters_GmRequest(req, iFalse);
            iConnect(GmRequest, req, finished, req, dumpRequestFinished_App_);
            submit_GmRequest(req);
        }
        /* Wait for requests to finish. */
        iGuardMutex(dumpMutex_, {
            if (dumpCount_ > 0) {
                wait_Condition(dumpFinishedCondition_, dumpMutex_);
            }
        });
        deinit_Foundation();
        exit(0);
    }
    init_Periodic(&d->periodic);
#if defined (LAGRANGE_HANDLE_SIGTERM)
    signal(SIGTERM, postQuitOnSigTerm_);
#endif
#if defined (iPlatformAppleDesktop)
    setupApplication_MacOS();
# if defined (LAGRANGE_NATIVE_MENU)
    d->submenuRoot = newOffscreen_Root();
# endif
#endif
#if defined (iPlatformAppleMobile)
    setupApplication_iOS();
#endif
#if defined (iPlatformAndroidMobile)
    setupApplication_Android();
#endif
#if defined (iPlatformMsys) || defined (iPlatformWindows)
    setupApplication_Win32();
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
#if defined (iPlatformAppleDesktop)
    localizeApplicationMenu_MacOS();
#endif
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
    init_PtrArray(&d->extraWindows);
    init_PtrArray(&d->popupWindows);
    load_Bookmarks(d->bookmarks, dataDir_App_());
    d->window = (iWindow *) new_MainWindow(*winRect0); /* first window is always created */
    as_MainWindow(d->window)->place.placementIndex = 0;
    addWindow_App(as_MainWindow(d->window));
#if defined (LAGRANGE_ENABLE_X11_XLIB)
    int desk = -1;
    if (size_Array(&d->initialWindowDesktops) > 0) {
        desk = *(const int *) at_Array(&d->initialWindowDesktops, 0);
    }
    if (desk >= 0) {
        iMainWindow *mw = as_MainWindow(d->window);
        mw->place.desktop = desk;
    }
#endif
    load_Visited(d->visited, dataDir_App_());
    if (!load_MimeHooks(d->mimehooks, dataDir_App_())) {
        postCommand_App("~config.error where:mimehooks.txt");
    }
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
        notify_App("~bookmarks.changed");
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
    notify_Root(NULL, "document.autoreload");
#if defined (LAGRANGE_ENABLE_IDLE_SLEEP)
    /* Initialize idle sleep. */ {
        d->isIdling      = iFalse;
        d->lastEventTime = 0;
        d->sleepTimer    = SDL_AddTimer(1000, checkAsleep_App_, d);
# if defined (iPlatformTerminal)
        d->idleSleepDelayMs = 1000 / 60;
# else
        const SDL_DisplayMode *dispMode =
            SDL_GetCurrentDisplayMode(SDL_GetDisplayForWindow(d->window->win));
        if (dispMode && dispMode->refresh_rate > 0) {
            d->idleSleepDelayMs = (unsigned int) (1000 / dispMode->refresh_rate);
        }
        else {
            d->idleSleepDelayMs = 1000 / 60;
        }
# endif
        d->idleSleepDelayMs *= 0.9f;
    }
#endif
    d->gamepad = (prefs_App()->useGamepad ? new_Gamepad() : NULL);
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
    if (deviceType_App() != desktop_AppDeviceType && d->window == main_WindowType) {
        /* HACK: Force a resize so widgets update their state. */
        resize_MainWindow(as_MainWindow(d->window), -1, -1);
    }
#if defined (iPlatformAndroid)
    /* See if there is something to import from backup. */
    javaCommand_Android("backup.load");
#endif
#if defined (LAGRANGE_ENABLE_X11_XLIB)
    if (d->window) {
        iMainWindow *mw = as_MainWindow(d->window);
        if (mw->place.desktop >= 0) {
            /* Use a delayed command to set workspace after everything is ready. */
            postCommandf_App("~window.setdesktop window:%u arg:%d",
                             id_Window(d->window), mw->place.desktop);
        }
    }
#endif
}

static void deinit_App(iApp *d) {
    if (d->tempFilesPendingDeletion == NULL) {
        return; /* already deinitialized */
    }
    delete_Gamepad(d->gamepad);
#if defined (iPlatformAppleDesktop) && defined (LAGRANGE_NATIVE_MENU)
    delete_Root(d->submenuRoot);
#endif
    iReverseForEach(PtrArray, i, &d->popupWindows) {
        iAssert(d->window != i.ptr);
        delete_Window(i.ptr);
    }
    iAssert(isEmpty_PtrArray(&d->popupWindows));
    deinit_PtrArray(&d->popupWindows);
    iReverseForEach(PtrArray, k, &d->extraWindows) {
        delete_Window(k.ptr);
        iAssert(d->window != k.ptr);
    }
    iAssert(isEmpty_PtrArray(&d->extraWindows));
    deinit_PtrArray(&d->extraWindows);
#if defined (LAGRANGE_ENABLE_IDLE_SLEEP)
    SDL_RemoveTimer(d->sleepTimer);
#endif
    SDL_RemoveTimer(d->autoReloadTimer);
    saveState_App_(d, iTrue);
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
    save_Snippets(dataDir_App_());
    deinit_Misfin();
    deinit_Snippets();
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
        removePath_CStr(cstr_String(tmp.value));
    }
    deinit_Array(&d->initialWindowRects);
    deinit_Array(&d->initialWindowDesktops);
    iRelease(d->savedWidths);
    iRelease(d->recentlySubmittedInput);
    iRelease(d->recentlyClosedTabUrls);
    iRelease(d->tempFilesPendingDeletion);
    d->tempFilesPendingDeletion = NULL;
}

const iString *debugInfo_App(void) {
#if !defined (iPlatformWindows)
    extern char **environ; /* The environment variables. */
#endif
    iApp *d = &app_;
    iString *msg = collectNew_String();
    iObjectList *docs = iClob(listDocuments_App(NULL));
    format_String(msg, "# Debug information\n");
    if (isDesktop_Platform()) {
        appendFormat_String(msg, "\n## User directory\n%s\n", cstr_String(dataDir_App()));
        appendFormat_String(msg, "\n## Executable path\n%s\n", cstr_String(execPath_App()));
    }
    appendFormat_String(msg, "\n## Features\n"); {
        const char *enabled[2] = { ballotUnchecked_Icon, ballotChecked_Icon };
        appendFormat_String(msg,
                            "Device type: %s\n\n",
                            isTerminal_Platform()                       ? "terminal"
                            : deviceType_App() == desktop_AppDeviceType ? "desktop"
                            : deviceType_App() == tablet_AppDeviceType  ? "tablet"
                                                                        : "phone");
#if defined (LAGRANGE_ENABLE_MAC_MENUS) && defined (iPlatformAppleDesktop)
        appendFormat_String(msg, "%s Native macOS menus\n", enabled[1]);
#elif defined (iPlatformAppleMobile)
        appendFormat_String(msg, "%s Native iOS menus\n", enabled[1]);
#elif !defined (iPlatformMobile) && !defined (iPlatformTerminal)
        appendFormat_String(msg, "%s Context menus are separate windows\n", enabled[
# if defined (LAGRANGE_ENABLE_POPUP_MENUS)
            1
# else
            0
# endif
            ]);
#endif
        appendFormat_String(msg, "%s Gamepad support\n", enabled[
#if defined (LAGRANGE_ENABLE_GAMEPAD)
            1
#else
            0
#endif
        ]);
        appendFormat_String(msg, "%s TrueType fonts (vector, monochrome)\n", enabled[
#if defined (LAGRANGE_ENABLE_STB_TRUETYPE)
            1
#else
            0
#endif
            ]);
        appendFormat_String(msg, "%s BiDi text\n", enabled[
#if defined (LAGRANGE_ENABLE_FRIBIDI)
            1
#else
            0
#endif
            ]);
        appendFormat_String(msg, "%s HarfBuzz text shaping\n", enabled[
#if defined (LAGRANGE_ENABLE_HARFBUZZ)
            1
#else
            0
#endif
            ]);
        appendFormat_String(msg, "%s MPEG audio\n", enabled[
#if defined (LAGRANGE_ENABLE_MPG123)
            1
#elif defined (iPlatformAndroidMobile)
            1
#else
            0
#endif
            ]);
        appendFormat_String(msg, "%s Opus audio\n", enabled[
#if defined (LAGRANGE_ENABLE_OPUS)
            1
#elif defined (iPlatformAndroidMobile)
            1
#else
            0
#endif
            ]);
        appendFormat_String(msg, "%s JPEG XL images\n", enabled[
#if defined (LAGRANGE_ENABLE_JXL)
            1
#else
            0
#endif
            ]);
        appendFormat_String(msg, "%s WebP images\n", enabled[
#if defined (LAGRANGE_ENABLE_WEBP)
            1
#else
            0
#endif
            ]);
    }
    appendFormat_String(msg, "\n## Memory usage\n"); {
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
    appendFormat_String(msg, "\n## Documents\n");
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

int run_App(int argc, char **argv) {
    init_App_(&app_, argc, argv);
    const int rc = run_App_(&app_);
    deinit_App(&app_);
    return rc;
}
