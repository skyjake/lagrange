#include "window.h"

#include "embedded.h"
#include "app.h"
#include "command.h"
#include "paint.h"
#include "text.h"
#include "util.h"
#include "labelwidget.h"
#include "inputwidget.h"
#include "documentwidget.h"
#if defined (iPlatformMsys)
#   include "../win32.h"
#endif
#if defined (iPlatformApple) && !defined (iPlatformIOS)
#   include "macos.h"
#endif

#include <the_Foundation/file.h>
#include <the_Foundation/path.h>
#include <SDL_hints.h>
#include <SDL_timer.h>
#include <SDL_syswm.h>

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

static iWindow *theWindow_ = NULL;

#if defined (iPlatformApple)
static float initialUiScale_ = 1.0f;
#else
static float initialUiScale_ = 1.1f;
#endif

iDefineTypeConstruction(Window)

static iBool handleRootCommands_(iWidget *root, const char *cmd) {
    iUnused(root);
    if (equal_Command(cmd, "menu.open")) {
        iWidget *button = pointer_Command(cmd);
        iWidget *menu = findChild_Widget(button, "menu");
        iAssert(menu);
        if (!isVisible_Widget(menu)) {
            openMenu_Widget(menu, init_I2(0, button->rect.size.y));
        }
        else {
            closeMenu_Widget(menu);
        }
        return iTrue;
    }
    else if (equal_Command(cmd, "focus.set")) {
        setFocus_Widget(findWidget_App(cstr_String(string_Command(cmd, "id"))));
        return iTrue;
    }
    else if (handleCommand_App(cmd)) {
        return iTrue;
    }
    return iFalse;
}

static const iMenuItem fileMenuItems[] = {
#if !defined (iPlatformApple)
    { "Quit Lagrange", 'q', KMOD_PRIMARY, "quit" }
#endif
};

static const iMenuItem editMenuItems[] = {
#if !defined (iPlatformApple)
    { "Preferences...", SDLK_COMMA, KMOD_PRIMARY, "preferences" }
#endif
};

static const iMenuItem viewMenuItems[] = {
};

static const char *reloadCStr_ = "\u25cb";
static const char *stopCStr_   = orange_ColorEscape "\u00d7";

static iBool handleNavBarCommands_(iWidget *navBar, const char *cmd) {
    if (equal_Command(cmd, "input.ended")) {
        iInputWidget *url = findChild_Widget(navBar, "url");
        if (arg_Command(cmd) && pointer_Command(cmd) == url) {
            postCommandf_App(
                "open url:%s",
                cstr_String(text_InputWidget(url)));
            return iTrue;
        }
    }
    else if (equal_Command(cmd, "document.changed")) {
        iInputWidget *url = findWidget_App("url");
        setTextCStr_InputWidget(url, valuePtr_Command(cmd, "url"));
        setTitle_Window(get_Window(), text_InputWidget(url));
        updateTextCStr_LabelWidget(findChild_Widget(navBar, "reload"), reloadCStr_);
        return iFalse;
    }
    else if (equal_Command(cmd, "document.request.cancelled")) {
        updateTextCStr_LabelWidget(findChild_Widget(navBar, "reload"), reloadCStr_);
        return iFalse;
    }
    else if (equal_Command(cmd, "document.request.started")) {
        updateTextCStr_LabelWidget(findChild_Widget(navBar, "reload"), stopCStr_);
        return iFalse;
    }
    else if (equal_Command(cmd, "navigate.reload")) {
        iDocumentWidget *doc = findWidget_App("document");
        if (isRequestOngoing_DocumentWidget(doc)) {
            postCommand_App("document.stop");
        }
        else {
            postCommand_App("document.reload");
        }
        return iTrue;
    }
    return iFalse;
}

static void setupUserInterface_Window(iWindow *d) {
    /* Children of root cover the entire window. */
    setFlags_Widget(d->root, resizeChildren_WidgetFlag, iTrue);
    setCommandHandler_Widget(d->root, handleRootCommands_);

    iWidget *div = makeVDiv_Widget();
    setId_Widget(div, "navdiv");
    addChild_Widget(d->root, iClob(div));

    /* Navigation bar. */ {
        iWidget *navBar = new_Widget();
        setId_Widget(navBar, "navbar");
        setFlags_Widget(navBar,
                        arrangeHeight_WidgetFlag | resizeChildren_WidgetFlag |
                            arrangeHorizontal_WidgetFlag,
                        iTrue);
        addChild_Widget(div, iClob(navBar));
        setCommandHandler_Widget(navBar, handleNavBarCommands_);
        setBackgroundColor_Widget(navBar, gray25_ColorId);

        addChild_Widget(navBar, iClob(new_LabelWidget(" \u25c4 ", 0, 0, "navigate.back")));
        addChild_Widget(navBar, iClob(new_LabelWidget(" \u25ba ", 0, 0, "navigate.forward")));
        addChild_Widget(navBar, iClob(new_LabelWidget("Home", 0, 0, "navigate.home")));
        iInputWidget *url = new_InputWidget(0);
        setId_Widget(as_Widget(url), "url");
        setTextCStr_InputWidget(url, "gemini://");
        addChildFlags_Widget(navBar, iClob(url), expand_WidgetFlag);
        setId_Widget(
            addChild_Widget(navBar, iClob(new_LabelWidget(reloadCStr_, 0, 0, "navigate.reload"))),
            "reload");
    }

    addChildFlags_Widget(div, iClob(new_DocumentWidget()), expand_WidgetFlag);

#if 0
    iWidget *mainDiv = makeHDiv_Widget();
    setId_Widget(mainDiv, "maindiv");
    addChild_Widget(d->root, iClob(mainDiv));

    iWidget *sidebar = makeVDiv_Widget();
    setFlags_Widget(sidebar, arrangeWidth_WidgetFlag, iTrue);
    setId_Widget(sidebar, "sidebar");
    addChild_Widget(mainDiv, iClob(sidebar));

    /* Menus. */ {
#if defined (iPlatformApple) && !defined (iPlatformIOS)
        /* Use the native menus. */
        insertMenuItems_MacOS("File", fileMenuItems, iElemCount(fileMenuItems));
        insertMenuItems_MacOS("Edit", editMenuItems, iElemCount(editMenuItems));
        insertMenuItems_MacOS("View", viewMenuItems, iElemCount(viewMenuItems));
#else
        iWidget *menubar = new_Widget();
        setBackgroundColor_Widget(menubar, gray25_ColorId);
        setFlags_Widget(menubar, arrangeHorizontal_WidgetFlag | arrangeHeight_WidgetFlag, iTrue);
        addChild_Widget(menubar, iClob(makeMenuButton_LabelWidget("File", fileMenuItems, iElemCount(fileMenuItems))));
        addChild_Widget(menubar, iClob(makeMenuButton_LabelWidget("Edit", editMenuItems, iElemCount(editMenuItems))));
        addChild_Widget(menubar, iClob(makeMenuButton_LabelWidget("View", viewMenuItems, iElemCount(viewMenuItems))));
        addChild_Widget(sidebar, iClob(menubar));
#endif
    }
    /* Tracker info. */ {
        iWidget *trackerInfo = addChild_Widget(sidebar, iClob(new_Widget()));
        setId_Widget(trackerInfo, "trackerinfo");        
        trackerInfo->rect.size.y = lineHeight_Text(default_FontId) + 2 * gap_UI;
        setFlags_Widget(trackerInfo, arrangeHorizontal_WidgetFlag | resizeChildren_WidgetFlag, iTrue);
        setId_Widget(
            addChild_Widget(trackerInfo, iClob(new_LabelWidget("", 'p', KMOD_PRIMARY, "pattern.goto arg:-1"))),
            "trackerinfo.current");
        iLabelWidget *dims = new_LabelWidget("", 'r', KMOD_PRIMARY | KMOD_ALT, "pattern.resize");
        setId_Widget(addChild_Widget(trackerInfo, iClob(dims)), "trackerinfo.dims");
    }

    iLibraryWidget *lib = new_LibraryWidget();
    setId_Widget(as_Widget(lib), "library");
    addChildFlags_Widget(sidebar, iClob(lib), expand_WidgetFlag);

    iPlaybackWidget *play = new_PlaybackWidget();
    setId_Widget(as_Widget(play), "playback");
    addChild_Widget(sidebar, iClob(play));

    iWidget *mainTabs = makeTabs_Widget(mainDiv);
    setId_Widget(mainTabs, "maintabs");
    setFlags_Widget(mainTabs, expand_WidgetFlag, iTrue);

    /* Optional sidebar on the right. */
    iWidget *sidebar2 = new_Widget();
    setId_Widget(addChild_Widget(mainDiv, iClob(sidebar2)), "sidebar2");
    setFlags_Widget(
        sidebar2, fixedWidth_WidgetFlag | frameless_WidgetFlag | resizeChildren_WidgetFlag, iTrue);

    /* Pattern sequence. */ {
        iSequenceWidget *seq = new_SequenceWidget();
        appendTabPage_Widget(mainTabs, iClob(seq), "SEQUENCE", 0, 0);
    }
    /* Tracker. */ {
        iTrackerWidget *tracker = new_TrackerWidget();
        appendTabPage_Widget(mainTabs, as_Widget(tracker), "PATTERN", 0, 0);
    }
    /* Voice editor. */ {
        iWidget *voice = as_Widget(new_VoiceWidget());
        setId_Widget(voice, "voicelayers");
        appendTabPage_Widget(mainTabs, iClob(voice), "VOICE", '3', KMOD_PRIMARY);
    }
    /* Song information. */ {
        iWidget *songPage = new_Widget();
        setId_Widget(songPage, "songinfo");
        setFlags_Widget(songPage, arrangeHorizontal_WidgetFlag, iTrue);
        iWidget *headings =
            addChildFlags_Widget(songPage,
                                 iClob(new_Widget()),
                                 resizeToParentHeight_WidgetFlag | resizeChildren_WidgetFlag |
                                     arrangeVertical_WidgetFlag | arrangeWidth_WidgetFlag);
        iWidget *values = addChildFlags_Widget(
            songPage, iClob(new_Widget()), arrangeVertical_WidgetFlag | arrangeSize_WidgetFlag);

        setId_Widget(addChild_Widget(headings, iClob(makePadding_Widget(2 * gap_UI))), "headings.padding");
        setId_Widget(addChild_Widget(values, iClob(makePadding_Widget(2 * gap_UI))), "values.padding");

        addChild_Widget(headings, iClob(makeHeading_Widget(cyan_ColorEscape "SONG PROPERTIES")));
        addChild_Widget(values, iClob(makeHeading_Widget("")));

        const int fieldWidth = advance_Text(monospace_FontId, "A").x * 40;
        iWidget *field;

        addChild_Widget(headings, iClob(makeHeading_Widget("Title:")));
        setId_Widget(field = addChild_Widget(values, iClob(new_InputWidget(0))), "info.title");
        field->rect.size.x = fieldWidth;

        addChild_Widget(headings, iClob(makeHeading_Widget("Author:")));
        setId_Widget(field = addChild_Widget(values, iClob(new_InputWidget(0))), "info.author");
        field->rect.size.x = fieldWidth;

        addChild_Widget(headings, iClob(makeHeading_Widget("Tempo:")));
        setId_Widget(addChild_Widget(values, iClob(new_InputWidget(3))), "info.tempo");

        addChild_Widget(headings, iClob(makeHeading_Widget("Events per Beat:")));
        setId_Widget(addChild_Widget(values, iClob(new_InputWidget(2))), "info.eventsperbeat");

        addChild_Widget(headings, iClob(makeHeading_Widget("Num of Tracks:")));
        setId_Widget(addChild_Widget(values, iClob(new_InputWidget(2))), "info.numtracks");

        addChild_Widget(headings, iClob(makePadding_Widget(2 * gap_UI)));
        addChild_Widget(values, iClob(makePadding_Widget(2 * gap_UI)));

        addChild_Widget(headings, iClob(makeHeading_Widget(cyan_ColorEscape "SONG METADATA")));
        addChild_Widget(values, iClob(makeHeading_Widget("")));

        addChild_Widget(headings, iClob(makeHeading_Widget("Duration:")));
        setId_Widget(addChildFlags_Widget(values, iClob(newEmpty_LabelWidget()),
                                          alignLeft_WidgetFlag | frameless_WidgetFlag),
                     "info.duration");
        addChild_Widget(headings, iClob(makeHeading_Widget("Statistics:\n\n ")));
        setId_Widget(addChildFlags_Widget(values,
                                          iClob(newEmpty_LabelWidget()),
                                          alignLeft_WidgetFlag | frameless_WidgetFlag),
                     "info.statistics");
        addChild_Widget(headings, iClob(makeHeading_Widget("Created on:")));
        setId_Widget(addChildFlags_Widget(values,
                                          iClob(newEmpty_LabelWidget()),
                                          alignLeft_WidgetFlag | frameless_WidgetFlag),
                     "info.created");

        addChild_Widget(headings, iClob(makeHeading_Widget("Last Modified on:")));
        setId_Widget(addChildFlags_Widget(values,
                                          iClob(newEmpty_LabelWidget()),
                                          alignLeft_WidgetFlag | frameless_WidgetFlag),
                     "info.lastmodified");
        /* App info in the bottom. */ {
            addChildFlags_Widget(headings, iClob(new_Widget()), expand_WidgetFlag);
            addChildFlags_Widget(
                headings,
                iClob(new_LabelWidget(gray50_ColorEscape "Version " BWH_APP_VERSION, 0, 0, NULL)),
                frameless_WidgetFlag | alignLeft_WidgetFlag);
        }
        appendTabPage_Widget(mainTabs, iClob(songPage), "INFO", '4', KMOD_PRIMARY);                
    }
    /* Application status. */ {
        iWidget *status = addChildFlags_Widget(d->root, iClob(newEmpty_LabelWidget()), 0);
        setFont_LabelWidget((iLabelWidget *) status, monospace_FontId);
        setFlags_Widget(status, frameless_WidgetFlag | alignRight_WidgetFlag, iTrue);
        setId_Widget(status, "status");
    }
#endif
    /* Glboal keyboard shortcuts. */ {
        // addAction_Widget(d->root, SDLK_LEFTBRACKET, KMOD_SHIFT | KMOD_PRIMARY, "tabs.prev");
        addAction_Widget(d->root, 'l', KMOD_PRIMARY, "focus.set id:url");
    }
}

static void updateRootSize_Window_(iWindow *d) {
    iInt2 *size = &d->root->rect.size;
    SDL_GetRendererOutputSize(d->render, &size->x, &size->y);
    arrange_Widget(d->root);
    postCommandf_App("window.resized width:%d height:%d", size->x, size->y);
    postRefresh_App();
}

static float pixelRatio_Window_(const iWindow *d) {
    int dx, x;
    SDL_GetRendererOutputSize(d->render, &dx, NULL);
    SDL_GetWindowSize(d->win, &x, NULL);
    return (float) dx / (float) x;
}

void init_Window(iWindow *d) {
    theWindow_ = d;
    uint32_t flags = SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI;
#if defined (iPlatformApple)
    SDL_SetHint(SDL_HINT_RENDER_DRIVER, "metal");
#else
    flags |= SDL_WINDOW_OPENGL;
#endif
    SDL_SetHint(SDL_HINT_RENDER_VSYNC, "1");
    if (SDL_CreateWindowAndRenderer(800, 500, flags, &d->win, &d->render)) {
        fprintf(stderr, "Error when creating window: %s\n", SDL_GetError());
        exit(-2);
    }
    SDL_SetWindowMinimumSize(d->win, 320, 240);
    SDL_SetWindowTitle(d->win, "Lagrange");
    /* Some info. */ {
        SDL_RendererInfo info;
        SDL_GetRendererInfo(d->render, &info);
        printf("[window] renderer: %s\n", info.name);
    }
    d->uiScale = initialUiScale_;
    d->pixelRatio = pixelRatio_Window_(d);
    setPixelRatio_Metrics(d->pixelRatio * d->uiScale);
#if defined (iPlatformMsys)
    useExecutableIconResource_SDLWindow(d->win);
#endif
#if defined (iPlatformLinux)
    /* Load the window icon. */ {
#if 0
        int w, h, num;
        const iBlock *icon = &imageAppicon64_Embedded;
        stbi_uc *pixels = stbi_load_from_memory(constData_Block(icon),
                                                size_Block(icon),
                                                &w,
                                                &h,
                                                &num,
                                                STBI_rgb_alpha);
        SDL_Surface *surf =
            SDL_CreateRGBSurfaceWithFormatFrom(pixels, w, h, 32, 4 * w, SDL_PIXELFORMAT_RGBA32);
        SDL_SetWindowIcon(d->win, surf);
        SDL_FreeSurface(surf);
        stbi_image_free(pixels);
#endif
    }
#endif
    d->root = new_Widget();
    d->presentTime = 0.0;
    setId_Widget(d->root, "root");
    init_Text(d->render);
#if defined (iPlatformApple) && !defined (iPlatformIOS)
    setupApplication_MacOS();
#endif
    setupUserInterface_Window(d);
    updateRootSize_Window_(d);
}

void deinit_Window(iWindow *d) {
    if (theWindow_ == d) {
        theWindow_ = NULL;
    }
    iReleasePtr(&d->root);
    deinit_Text();
    SDL_DestroyRenderer(d->render);
    SDL_DestroyWindow(d->win);
}

SDL_Renderer *renderer_Window(const iWindow *d) {
    return d->render;
}

static iBool handleWindowEvent_Window_(iWindow *d, const SDL_WindowEvent *ev) {
    switch (ev->event) {
        case SDL_WINDOWEVENT_MOVED:
            /* No need to do anything. */
            return iTrue;
        case SDL_WINDOWEVENT_RESIZED:
        case SDL_WINDOWEVENT_SIZE_CHANGED:
            updateRootSize_Window_(d);
            return iTrue;
        case SDL_WINDOWEVENT_LEAVE:
            unhover_Widget();
            return iTrue;
        default:
            break;
    }
    return iFalse;
}

iBool processEvent_Window(iWindow *d, const SDL_Event *ev) {
    switch (ev->type) {
        case SDL_WINDOWEVENT: {
            return handleWindowEvent_Window_(d, &ev->window);
        }
        default: {
            SDL_Event event = *ev;
            /* Map mouse pointer coordinate to our coordinate system. */
            if (event.type == SDL_MOUSEMOTION) {
                const iInt2 pos = coord_Window(d, event.motion.x, event.motion.y);
                event.motion.x = pos.x;
                event.motion.y = pos.y;
            }
            else if (event.type == SDL_MOUSEBUTTONUP || event.type == SDL_MOUSEBUTTONDOWN) {
                const iInt2 pos = coord_Window(d, event.button.x, event.button.y);
                event.button.x = pos.x;
                event.button.y = pos.y;
            }
            iWidget *widget = d->root;
            if (event.type == SDL_MOUSEMOTION || event.type == SDL_MOUSEWHEEL ||
                event.type == SDL_MOUSEBUTTONUP || event.type == SDL_MOUSEBUTTONDOWN) {
                if (mouseGrab_Widget()) {
                    widget = mouseGrab_Widget();
                }
            }
            iWidget *oldHover = hover_Widget();
            /* Dispatch the event to the tree of widgets. */
            iBool wasUsed = dispatchEvent_Widget(widget, &event);
            if (oldHover != hover_Widget()) {
                postRefresh_App();
            }
            return wasUsed;
        }
    }
    return iFalse;
}

#if 0
static void waitPresent_Window_(iWindow *d) {
    const double ticksPerFrame = 1000.0 / 60.0;
    uint32_t nowTime = SDL_GetTicks();
    if (nowTime < d->presentTime) {
        SDL_Delay((uint32_t) (d->presentTime - nowTime));
        nowTime = SDL_GetTicks();
    }
    /* Now it is the presentation time. */
    /* Figure out the next time in the future. */
    if (d->presentTime <= nowTime) {
        d->presentTime += ticksPerFrame * ((int) ((nowTime - d->presentTime) / ticksPerFrame) + 1);
    }
    else {
        d->presentTime = nowTime;
    }
}
#endif

void draw_Window(iWindow *d) {
    /* Clear the window. */
    SDL_SetRenderDrawColor(d->render, 0, 0, 0, 255);
    SDL_RenderClear(d->render);
    /* Draw widgets. */
    d->frameTime = SDL_GetTicks();
    draw_Widget(d->root);
#if 0
    /* Text cache debugging. */ {
        SDL_Texture *cache = glyphCache_Text();
        SDL_Rect rect = { 140, 60, 512, 512 };
        SDL_SetRenderDrawColor(d->render, 0, 0, 0, 255);
        SDL_RenderFillRect(d->render, &rect);
        SDL_RenderCopy(d->render, glyphCache_Text(), NULL, &rect);
    }
#endif
//    waitPresent_Window_(d);
    SDL_RenderPresent(d->render);
}

void resize_Window(iWindow *d, int w, int h) {
    SDL_SetWindowSize(d->win, w, h);
    updateRootSize_Window_(d);
}

void setTitle_Window(iWindow *d, const iString *title) {
    SDL_SetWindowTitle(d->win, cstr_String(title));
}

void setUiScale_Window(iWindow *d, float uiScale) {
    uiScale = iClamp(uiScale, 0.5f, 4.0f);
    if (d) {
        d->uiScale = uiScale;
#if 0
        deinit_Text();
        setPixelRatio_Metrics(d->pixelRatio * d->uiScale);
        init_Text(d->render);
        postCommand_App("metrics.changed");
        /* TODO: Dynamic UI metrics change. Widgets need to update themselves. */
#endif
    }
    else {
        initialUiScale_ = uiScale;
    }
}

iInt2 rootSize_Window(const iWindow *d) {
    return d->root->rect.size;
}

iInt2 coord_Window(const iWindow *d, int x, int y) {
    return mulf_I2(init_I2(x, y), d->pixelRatio);
}

iInt2 mouseCoord_Window(const iWindow *d) {
    int x, y;
    SDL_GetMouseState(&x, &y);
    return coord_Window(d, x, y);
}

float uiScale_Window(const iWindow *d) {
    return d->uiScale;
}

uint32_t frameTime_Window(const iWindow *d) {
    return d->frameTime;
}

iWindow *get_Window(void) {
    return theWindow_;
}
