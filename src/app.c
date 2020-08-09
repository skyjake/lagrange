#include "app.h"
#include "embedded.h"
#include "gmcerts.h"
#include "gmutil.h"
#include "history.h"
#include "visited.h"
#include "ui/command.h"
#include "ui/window.h"
#include "ui/inputwidget.h"
#include "ui/labelwidget.h"
#include "ui/documentwidget.h"
#include "ui/util.h"
#include "ui/text.h"
#include "ui/color.h"

#include <the_Foundation/commandline.h>
#include <the_Foundation/file.h>
#include <the_Foundation/fileinfo.h>
#include <the_Foundation/path.h>
#include <the_Foundation/process.h>
#include <the_Foundation/sortedarray.h>
#include <the_Foundation/time.h>
#include <SDL_events.h>
#include <SDL_render.h>
#include <SDL_video.h>

#include <stdio.h>
#include <stdarg.h>
#include <errno.h>

#if defined (iPlatformApple) && !defined (iPlatformIOS)
#   include "ui/macos.h"
#endif

iDeclareType(App)

#if defined (iPlatformApple)
#define EMB_BIN "../../Resources/resources.bin"
static const char *dataDir_App_ = "~/Library/Application Support/fi.skyjake.Lagrange";
#endif
#if defined (iPlatformMsys)
#define EMB_BIN "../resources.bin"
static const char *dataDir_App_ = "~/AppData/Roaming/fi.skyjake.Lagrange";
#endif
#if defined (iPlatformLinux)
#define EMB_BIN  "../../share/lagrange/resources.bin"
#define EMB_BIN2 "../resources.bin" /* try from build dir as well */
static const char *dataDir_App_ = "~/.config/lagrange";
#endif
static const char *prefsFileName_App_   = "prefs.cfg";

struct Impl_App {
    iCommandLine args;
    iBool        running;
    iWindow *    window;
    iSortedArray tickers;
    iBool        pendingRefresh;
    iGmCerts *   certs;
    iVisited *   visited;
    /* Preferences: */
    iBool        retainWindowSize;
    float        uiScale;
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
    iWindow *win = get_Window();
    if (d->retainWindowSize) {
        int w, h, x, y;
        SDL_GetWindowSize(d->window->win, &w, &h);
        SDL_GetWindowPosition(d->window->win, &x, &y);
        appendFormat_String(str, "restorewindow width:%d height:%d coord:%d %d\n", w, h, x, y);
    }
    appendFormat_String(str, "uiscale arg:%f\n", uiScale_Window(d->window));
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
        while (nextSplit_Rangecc(&src, "\n", &line)) {
            iString cmd;
            initRange_String(&cmd, line);
            if (equal_Command(cstr_String(&cmd), "uiscale")) {
                /* Must be handled before the window is created. */
                setUiScale_Window(get_Window(), argf_Command(cstr_String(&cmd)));
            }
            else {
                postCommandString_App(&cmd);
            }
            deinit_String(&cmd);
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

static void init_App_(iApp *d, int argc, char **argv) {
    init_CommandLine(&d->args, argc, argv);
    init_SortedArray(&d->tickers, sizeof(iTicker), cmp_Ticker_);
    d->running          = iFalse;
    d->window           = NULL;
    d->retainWindowSize = iTrue;
    d->pendingRefresh   = iFalse;
    d->certs            = new_GmCerts(dataDir_App_);
    d->visited          = new_Visited();
    loadPrefs_App_(d);
    load_Visited(d->visited, dataDir_App_);
#if defined (iHaveLoadEmbed)
    /* Load the resources from a file. */ {
        if (!load_Embed(concatPath_CStr(cstr_String(execPath_App()), "../resources.bin"))) {
            if (!load_Embed(concatPath_CStr(cstr_String(execPath_App()), EMB_BIN))) {
                fprintf(stderr, "failed to load resources.bin: %s\n", strerror(errno));
                exit(-1);
            }
        }
    }
#endif
    d->window = new_Window();
    /* Widget state init. */ {
        postCommand_App("navigate.home");
    }
}

static void deinit_App(iApp *d) {
    savePrefs_App_(d);
    save_Visited(d->visited, dataDir_App_);
    delete_Visited(d->visited);
    delete_GmCerts(d->certs);
    deinit_SortedArray(&d->tickers);
    delete_Window(d->window);
    d->window = NULL;
    deinit_CommandLine(&d->args);    
}

const iString *execPath_App(void) {
    return executablePath_CommandLine(&app_.args);
}

const iString *dataDir_App(void) {
    return collect_String(cleanedCStr_Path(dataDir_App_));
}

void processEvents_App(enum iAppEventMode eventMode) {
    iApp *d = &app_;
    SDL_Event ev;
    while (
        (!d->pendingRefresh && eventMode == waitForNewEvents_AppEventMode && SDL_WaitEvent(&ev)) ||
        ((d->pendingRefresh || eventMode == postedEventsOnly_AppEventMode) && SDL_PollEvent(&ev))) {
        switch (ev.type) {
            case SDL_QUIT:
                d->running = iFalse;
                goto backToMainLoop;
            case SDL_DROPFILE:
                postCommandf_App("open url:file://%s", ev.drop.file);
                break;
            default: {
                iBool wasUsed = processEvent_Window(d->window, &ev);
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
backToMainLoop:;
}

static void runTickers_App_(iApp *d) {
    /* Tickers may add themselves again, so we'll run off a copy. */
    iSortedArray *pending = copy_SortedArray(&d->tickers);
    clear_SortedArray(&d->tickers);
    if (!isEmpty_SortedArray(pending)) {
        postRefresh_App();
    }
    iConstForEach(Array, i, &pending->values) {
        const iTicker *ticker = i.value;
        if (ticker->callback) {
            ticker->callback(ticker->context);
        }
    }
    delete_SortedArray(pending);
}

static int run_App_(iApp *d) {
    arrange_Widget(findWidget_App("root"));
    d->running = iTrue;
    SDL_EventState(SDL_DROPFILE, SDL_ENABLE); /* open files via drag'n'drop */
    while (d->running) {
        runTickers_App_(d);
        processEvents_App(waitForNewEvents_AppEventMode);
        refresh_App();
        recycle_Garbage();
    }
    return 0;
}

void refresh_App(void) {
    iApp *d = &app_;
    destroyPending_Widget();
    draw_Window(d->window);
    d->pendingRefresh = iFalse;
}

int run_App(int argc, char **argv) {
    init_App_(&app_, argc, argv);
    const int rc = run_App_(&app_);
    deinit_App(&app_);
    return rc;
}

void postRefresh_App(void) {
    iApp *d = &app_;
    if (!d->pendingRefresh) {
        d->pendingRefresh = iTrue;
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
    SDL_Event ev;
    ev.user.type     = SDL_USEREVENT;
    ev.user.code     = command_UserEventCode;
    ev.user.windowID = get_Window() ? SDL_GetWindowID(get_Window()->win) : 0;
    ev.user.data1    = strdup(command);
    ev.user.data2    = NULL;
    SDL_PushEvent(&ev);
#if !defined (NDEBUG)
    printf("[command] %s\n", command); fflush(stdout);
#endif
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
    return findChild_Widget(app_.window->root, id);
}

void addTicker_App(void (*ticker)(iAny *), iAny *context) {
    iApp *d = &app_;
    insert_SortedArray(&d->tickers, &(iTicker){ context, ticker });
}

iGmCerts *certs_App(void) {
    return app_.certs;
}

iVisited *visited_App(void) {
    return app_.visited;
}

static iBool handlePrefsCommands_(iWidget *d, const char *cmd) {
    if (equal_Command(cmd, "prefs.dismiss") || equal_Command(cmd, "preferences")) {
        setUiScale_Window(get_Window(),
                          toFloat_String(text_InputWidget(findChild_Widget(d, "prefs.uiscale"))));
        destroy_Widget(d);
        return iTrue;
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

iBool handleCommand_App(const char *cmd) {
    iApp *d = &app_;
    if (equal_Command(cmd, "open")) {
        const iString *url = collect_String(newCStr_String(suffixPtr_Command(cmd, "url")));
        iUrl parts;
        init_Url(&parts, url);
        if (equalCase_Rangecc(&parts.protocol, "http") ||
            equalCase_Rangecc(&parts.protocol, "https")) {
            openInDefaultBrowser_App(url);
            return iTrue;
        }
        iDocumentWidget *doc = document_Command(cmd);
        iHistory *history = history_DocumentWidget(doc);
        const iBool isHistory = argLabel_Command(cmd, "history") != 0;
        if (!isHistory) {
            if (argLabel_Command(cmd, "redirect")) {
                replace_History(history, url);
            }
            else {
                add_History(history, url);
            }
        }
        visitUrl_Visited(d->visited, url);
        setInitialScroll_DocumentWidget(doc, argLabel_Command(cmd, "scroll") * gap_UI);
        setUrlFromCache_DocumentWidget(doc, url, isHistory);
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
        iWidget *tabs = findWidget_App("doctabs");
        iWidget *newTabButton = findChild_Widget(tabs, "newtab");
        removeChild_Widget(newTabButton->parent, newTabButton);
        iDocumentWidget *newDoc = new_DocumentWidget();
        setId_Widget(as_Widget(newDoc), format_CStr("document%03d", tabCount_Widget(tabs)));
        appendTabPage_Widget(tabs, iClob(newDoc), "", 0, 0);
        addChild_Widget(findChild_Widget(tabs, "tabs.buttons"), iClob(newTabButton));
        postCommandf_App("tabs.switch page:%p", newDoc);
        postCommand_App("navigate.home");
        arrange_Widget(tabs);
        refresh_Widget(tabs);
        return iTrue;
    }
    else if (equal_Command(cmd, "tabs.close")) {
        iWidget *tabs = findWidget_App("doctabs");
        if (tabCount_Widget(tabs) > 1) {
            size_t index = tabPageIndex_Widget(tabs, document_App());
            iWidget *closed = removeTabPage_Widget(tabs, index);
            destroy_Widget(closed); /* released later */
            if (index == tabCount_Widget(tabs)) {
                index--;
            }
            arrange_Widget(tabs);
            postCommandf_App("tabs.switch page:%p", tabPage_Widget(tabs, index));
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
        iWindow *win = get_Window();
        iWidget *dlg = makePreferences_Widget();
        setToggle_Widget(findChild_Widget(dlg, "prefs.retainwindow"), d->retainWindowSize);
        setText_InputWidget(findChild_Widget(dlg, "prefs.uiscale"),
                            collectNewFormat_String("%g", uiScale_Window(get_Window())));
        setCommandHandler_Widget(dlg, handlePrefsCommands_);
    }
    else if (equal_Command(cmd, "restorewindow")) {
        d->retainWindowSize = iTrue;
        resize_Window(d->window, argLabel_Command(cmd, "width"), argLabel_Command(cmd, "height"));
        const iInt2 pos = coord_Command(cmd);
        SDL_SetWindowPosition(d->window->win, pos.x, pos.y);
    }
    else if (equal_Command(cmd, "navigate.home")) {
        postCommand_App("open url:about:home");
        return iTrue;
    }
    else if (equal_Command(cmd, "font.setfactor")) {
        setContentFontSize_Text((float) arg_Command(cmd) / 100.0f);
        postCommand_App("font.changed");
        refresh_App();
        return iTrue;
    }
    else {
        return iFalse;
    }
    return iTrue;
}

void openInDefaultBrowser_App(const iString *url) {
    iProcess *proc = new_Process();
    setArguments_Process(proc,
#if defined (iPlatformApple)
                         iClob(newStringsCStr_StringList("/usr/bin/open", cstr_String(url), NULL))
#elif defined (iPlatformLinux)
                         iClob(newStringsCStr_StringList("/usr/bin/x-www-browser", cstr_String(url), NULL))
#elif defined (iPlatformMsys)
                         iClob(newStringsCStr_StringList("start", cstr_String(url), NULL))
#endif
    );
    start_Process(proc);
    iRelease(proc);
}
