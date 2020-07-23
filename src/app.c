#include "app.h"
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
static const char *dataDir_App_        = "~/Library/Application Support/fi.skyjake.Lagrange";
#endif
#if defined (iPlatformMsys)
static const char *dataDir_App_        = "~/AppData/Roaming/fi.skyjake.Lagrange";
#endif
#if defined (iPlatformLinux)
static const char *dataDir_App_        = "~/.config/lagrange";
#endif
static const char *prefsFileName_App_  = "prefs.cfg";

struct Impl_App {
    iCommandLine args;
    iBool        running;
    iWindow *    window;
    iSortedArray tickers;
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
    loadPrefs_App_(d);
    d->window = new_Window();
    /* Widget state init. */ {
        iString *homePath = newCStr_String(dataDir_App_);
        clean_Path(homePath);
        append_Path(homePath, &iStringLiteral("home.gmi"));
        prependCStr_String(homePath, "file://");
        setUrl_DocumentWidget(findWidget_App("document"), homePath);
        delete_String(homePath);
    }
}

static void deinit_App(iApp *d) {
    savePrefs_App_(d);
    deinit_SortedArray(&d->tickers);
    delete_Window(d->window);
    d->window = NULL;
    deinit_CommandLine(&d->args);
}

const iString *execPath_App(void) {
    return executablePath_CommandLine(&app_.args);
}

void processEvents_App(void) {
    iApp *d = &app_;
    SDL_Event ev;
    while (SDL_WaitEvent(&ev)) {
        switch (ev.type) {
            case SDL_QUIT:
                // if (isModified_Song(d->song)) {
                //     save_App_(d, autosavePath_App_(d));
                // }
                d->running = iFalse;
                goto backToMainLoop;
            case SDL_DROPFILE:
                postCommandf_App("open url:file://%s", ev.drop.file);
                break;
            default: {
                if (ev.type == SDL_USEREVENT && ev.user.code == refresh_UserEventCode) {
                    goto backToMainLoop;
                }
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
        processEvents_App(); /* may wait here for a while */
        refresh_App();
    }
    return 0;
}

void refresh_App(void) {
    iApp *d = &app_;
    destroyPending_Widget();
    draw_Window(d->window);
    recycle_Garbage();
}

int run_App(int argc, char **argv) {
    init_App_(&app_, argc, argv);
    const int rc = run_App_(&app_);
    deinit_App(&app_);
    return rc;
}

void postRefresh_App(void) {
    SDL_Event ev;
    ev.user.type     = SDL_USEREVENT;
    ev.user.code     = refresh_UserEventCode;
    ev.user.windowID = get_Window() ? SDL_GetWindowID(get_Window()->win) : 0;
    ev.user.data1    = NULL;
    ev.user.data2    = NULL;
    SDL_PushEvent(&ev);
}

void postCommand_App(const char *command) {
    SDL_Event ev;
    ev.user.type     = SDL_USEREVENT;
    ev.user.code     = command_UserEventCode;
    ev.user.windowID = get_Window() ? SDL_GetWindowID(get_Window()->win) : 0;
    ev.user.data1    = strdup(command);
    ev.user.data2    = NULL;
    SDL_PushEvent(&ev);
    printf("[command] %s\n", command); fflush(stdout);
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

static iBool handlePrefsCommands_(iWidget *d, const char *cmd) {
    if (equal_Command(cmd, "prefs.dismiss") || equal_Command(cmd, "preferences")) {
        setUiScale_Window(get_Window(),
                          toFloat_String(text_InputWidget(findChild_Widget(d, "prefs.uiscale"))));
        destroy_Widget(d);
        return iTrue;
    }
    return iFalse;
}

iBool handleCommand_App(const char *cmd) {
    iApp *d = &app_;
    iWidget *root = d->window->root;
    if (equal_Command(cmd, "open")) {
        setUrl_DocumentWidget(findChild_Widget(root, "document"),
                              collect_String(newCStr_String(valuePtr_Command(cmd, "url"))));
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
    else {
        return iFalse;
    }
    return iTrue;
}
