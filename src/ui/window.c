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

#include "window.h"

#include "embedded.h"
#include "app.h"
#include "command.h"
#include "paint.h"
#include "text.h"
#include "util.h"
#include "../visited.h"
#include "labelwidget.h"
#include "inputwidget.h"
#include "documentwidget.h"
#include "sidebarwidget.h"
#include "gmutil.h"
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

iDefineTypeConstructionArgs(Window, (iRect rect), rect)

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

#if defined (iPlatformApple)
#  define iHaveNativeMenus
#endif

#if !defined (iHaveNativeMenus)
static const iMenuItem navMenuItems[] = {
    { "New Tab", 't', KMOD_PRIMARY, "tabs.new" },
    { "Open Location...", SDLK_l, KMOD_PRIMARY, "focus.set id:url" },
    { "---", 0, 0, NULL },
    { "Copy Source Text", SDLK_c, KMOD_PRIMARY, "copy" },
    { "Bookmark This Page", SDLK_d, KMOD_PRIMARY, "bookmark.add" },
    { "---", 0, 0, NULL },
    { "Toggle Sidebar", SDLK_l, KMOD_PRIMARY | KMOD_SHIFT, "sidebar.toggle" },
    { "Zoom In", SDLK_EQUALS, KMOD_PRIMARY, "zoom.delta arg:10" },
    { "Zoom Out", SDLK_MINUS, KMOD_PRIMARY, "zoom.delta arg:-10" },
    { "Reset Zoom", SDLK_0, KMOD_PRIMARY, "zoom.set arg:100" },
    { "---", 0, 0, NULL },
    { "Preferences...", SDLK_COMMA, KMOD_PRIMARY, "preferences" },
    { "Help", 0, 0, "!open url:about:help" },
    { "Release Notes", 0, 0, "!open url:about:version" },
    { "---", 0, 0, NULL },
    { "Quit Lagrange", 'q', KMOD_PRIMARY, "quit" }
};
#endif

#if defined (iHaveNativeMenus)
/* Using native menus. */
static const iMenuItem fileMenuItems[] = {
    { "New Tab", SDLK_t, KMOD_PRIMARY, "tabs.new" },
    { "Open Location...", SDLK_l, KMOD_PRIMARY, "focus.set id:url" },
};

static const iMenuItem editMenuItems[] = {
    { "Copy Source Text", SDLK_c, KMOD_PRIMARY, "copy" },
    { "Copy Link to Page", SDLK_c, KMOD_PRIMARY | KMOD_SHIFT, "document.copylink" },
    { "---", 0, 0, NULL },
    { "Bookmark This Page...", SDLK_d, KMOD_PRIMARY, "bookmark.add" },
};

static const iMenuItem viewMenuItems[] = {
    { "Show Bookmarks", '1', KMOD_PRIMARY, "sidebar.mode arg:0 show:1" },
    { "Show History", '2', KMOD_PRIMARY, "sidebar.mode arg:1 show:1" },
    { "Show Identities", '3', KMOD_PRIMARY, "sidebar.mode arg:2 show:1" },
    { "Show Page Outline", '4', KMOD_PRIMARY, "sidebar.mode arg:3 show:1" },
    { "Toggle Sidebar", SDLK_l, KMOD_PRIMARY | KMOD_SHIFT, "sidebar.toggle" },
    { "---", 0, 0, NULL },
    { "Go Back", SDLK_LEFTBRACKET, KMOD_PRIMARY, "navigate.back" },
    { "Go Forward", SDLK_RIGHTBRACKET, KMOD_PRIMARY, "navigate.forward" },
    { "Reload Page", 'r', KMOD_PRIMARY, "navigate.reload" },
    { "---", 0, 0, NULL },
    { "Zoom In", SDLK_EQUALS, KMOD_PRIMARY, "zoom.delta arg:10" },
    { "Zoom Out", SDLK_MINUS, KMOD_PRIMARY, "zoom.delta arg:-10" },
    { "Reset Zoom", SDLK_0, KMOD_PRIMARY, "zoom.set arg:100" },
};

static const iMenuItem helpMenuItems[] = {
    { "Help", 0, 0, "open url:about:help" },
    { "Release Notes", 0, 0, "open url:about:version" },
};
#endif

static const iMenuItem identityMenuItems[] = {
    { "New Identity...", SDLK_n, KMOD_PRIMARY | KMOD_SHIFT, "ident.new" },
};

static const char *reloadCStr_ = "\U0001f503";
static const char *stopCStr_   = uiTextCaution_ColorEscape "\U0001f310";

static iBool handleNavBarCommands_(iWidget *navBar, const char *cmd) {
    if (equal_Command(cmd, "window.resized")) {
        const iBool isNarrow = width_Rect(bounds_Widget(navBar)) / gap_UI < 140;
        if (isNarrow ^ ((flags_Widget(navBar) & tight_WidgetFlag) != 0)) {
            setFlags_Widget(navBar, tight_WidgetFlag, isNarrow);
            iForEach(ObjectList, i, navBar->children) {
                iWidget *child = as_Widget(i.object);
                setFlags_Widget(
                    child, tight_WidgetFlag, isNarrow || !cmp_String(id_Widget(child), "lock"));
                if (isInstance_Object(i.object, &Class_LabelWidget)) {
                    iLabelWidget *label = i.object;
                    updateSize_LabelWidget(label);
                }
            }
        }
        arrange_Widget(navBar);
        refresh_Widget(navBar);
        return iFalse;
    }
    else if (equal_Command(cmd, "input.ended")) {
        iInputWidget *url = findChild_Widget(navBar, "url");
        if (arg_Command(cmd) && pointer_Command(cmd) == url) {
            postCommandf_App(
                "open url:%s",
                cstr_String(absoluteUrl_String(&iStringLiteral(""), text_InputWidget(url))));
            return iTrue;
        }
    }
    else if (startsWith_CStr(cmd, "document.")) {
        /* React to the current document only. */
        if (document_Command(cmd) == document_App()) {
            iLabelWidget *reloadButton = findChild_Widget(navBar, "reload");
            if (equal_Command(cmd, "document.changed")) {
                iInputWidget *url = findWidget_App("url");
                const iString *urlStr = collect_String(suffix_Command(cmd, "url"));
                setText_InputWidget(url, urlStr);
                updateTextCStr_LabelWidget(reloadButton, reloadCStr_);
                return iFalse;
            }
            else if (equal_Command(cmd, "document.request.cancelled")) {
                updateTextCStr_LabelWidget(reloadButton, reloadCStr_);
                return iFalse;
            }
            else if (equal_Command(cmd, "document.request.started")) {
                iInputWidget *url = findChild_Widget(navBar, "url");
                if (isFocused_Widget(as_Widget(url))) {
                    setFocus_Widget(NULL);
                }
                setTextCStr_InputWidget(url, suffixPtr_Command(cmd, "url"));
                updateTextCStr_LabelWidget(reloadButton, stopCStr_);
                return iFalse;
            }
        }
    }
    else if (equal_Command(cmd, "tabs.changed")) {
        /* Update navbar according to the current tab. */
        iDocumentWidget *doc = document_App();
        if (doc) {
            setText_InputWidget(findChild_Widget(navBar, "url"), url_DocumentWidget(doc));
            updateTextCStr_LabelWidget(findChild_Widget(navBar, "reload"),
                                       isRequestOngoing_DocumentWidget(doc) ? stopCStr_ : reloadCStr_);
        }
    }
    else if (equal_Command(cmd, "mouse.clicked")) {
        iWidget *widget = pointer_Command(cmd);
        iWidget *menu = findWidget_App("doctabs.menu");
        if (isTabButton_Widget(widget)) {
            iWidget *tabs = findWidget_App("doctabs");
            showTabPage_Widget(tabs,
                               tabPage_Widget(tabs, childIndex_Widget(widget->parent, widget)));
            openMenu_Widget(menu, coord_Command(cmd));
        }
    }
    else if (equal_Command(cmd, "navigate.reload")) {
        iDocumentWidget *doc = document_Command(cmd);
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

static iBool handleSearchBarCommands_(iWidget *searchBar, const char *cmd) {
    if (equal_Command(cmd, "input.ended") &&
        cmp_String(string_Command(cmd, "id"), "find.input") == 0) {
        iInputWidget *input = findChild_Widget(searchBar, "find.input");
        if (arg_Command(cmd) && argLabel_Command(cmd, "enter") &&
            isVisible_Widget(as_Widget(input))) {
            postCommand_App("find.next");
            /* Keep focus when pressing Enter. */
            if (!isEmpty_String(text_InputWidget(input))) {
                postCommand_App("focus.set id:find.input");
            }
        }
        else {
            postCommand_App("find.clearmark");
        }
        return iTrue;
    }
    else if (equal_Command(cmd, "focus.gained")) {
        if (pointer_Command(cmd) == findChild_Widget(searchBar, "find.input")) {
            if (!isVisible_Widget(searchBar)) {
                setFlags_Widget(searchBar, hidden_WidgetFlag, iFalse);
                arrange_Widget(get_Window()->root);
                refresh_App();
            }
        }
    }
    else if (equal_Command(cmd, "find.close")) {
        if (isVisible_Widget(searchBar)) {
            setFlags_Widget(searchBar, hidden_WidgetFlag, iTrue);
            arrange_Widget(searchBar->parent);
            if (isFocused_Widget(findChild_Widget(searchBar, "find.input"))) {
                setFocus_Widget(NULL);
            }
            refresh_Widget(searchBar->parent);
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
        setBackgroundColor_Widget(navBar, uiBackground_ColorId);
        setCommandHandler_Widget(navBar, handleNavBarCommands_);
        addChild_Widget(navBar, iClob(newIcon_LabelWidget("\U0001f850", 0, 0, "navigate.back")));
        addChild_Widget(navBar, iClob(newIcon_LabelWidget("\U0001f852", 0, 0, "navigate.forward")));
        addChild_Widget(navBar, iClob(newIcon_LabelWidget("\U0001f3e0", 0, 0, "navigate.home")));
        iLabelWidget *lock =
            addChildFlags_Widget(navBar,
                                 iClob(newIcon_LabelWidget("\U0001f513", 0, 0, "server.showcert")),
                                 frameless_WidgetFlag | tight_WidgetFlag);
        setId_Widget(as_Widget(lock), "navbar.lock");
        setFont_LabelWidget(lock, defaultSymbols_FontId);
        updateTextCStr_LabelWidget(lock, "\U0001f512");
        iInputWidget *url = new_InputWidget(0);
        setId_Widget(as_Widget(url), "url");
        setTextCStr_InputWidget(url, "gemini://");
        addChildFlags_Widget(navBar, iClob(url), expand_WidgetFlag);
        setId_Widget(
            addChild_Widget(navBar, iClob(newIcon_LabelWidget(reloadCStr_, 0, 0, "navigate.reload"))),
            "reload");
        iLabelWidget *idMenu =
            makeMenuButton_LabelWidget("\U0001f464", identityMenuItems, iElemCount(identityMenuItems));
        setAlignVisually_LabelWidget(idMenu, iTrue);
        addChild_Widget(navBar, iClob(idMenu));
        //addChild_Widget(navBar, iClob(newIcon_LabelWidget("\U0001f464", 0, 0, "cert.client")));
#if !defined (iHaveNativeMenus)
        iLabelWidget *navMenu =
            makeMenuButton_LabelWidget("\U0001d362", navMenuItems, iElemCount(navMenuItems));
        setAlignVisually_LabelWidget(navMenu, iTrue);
        addChild_Widget(navBar, iClob(navMenu));
#else
        insertMenuItems_MacOS("File", 1, fileMenuItems, iElemCount(fileMenuItems));
        insertMenuItems_MacOS("Edit", 2, editMenuItems, iElemCount(editMenuItems));
        insertMenuItems_MacOS("Identity", 3, identityMenuItems, iElemCount(identityMenuItems));
        insertMenuItems_MacOS("View", 4, viewMenuItems, iElemCount(viewMenuItems));
        insertMenuItems_MacOS("Help", 6, helpMenuItems, iElemCount(helpMenuItems));
#endif
    }
    /* Tab bar. */ {
        iWidget *tabBar = makeTabs_Widget(div);
        setId_Widget(tabBar, "doctabs");
        setFlags_Widget(tabBar, expand_WidgetFlag, iTrue);
        setBackgroundColor_Widget(tabBar, uiBackground_ColorId);
        appendTabPage_Widget(tabBar, iClob(new_DocumentWidget()), "Document", 0, 0);
        iWidget *buttons = findChild_Widget(tabBar, "tabs.buttons");
        setFlags_Widget(buttons, collapse_WidgetFlag | hidden_WidgetFlag, iTrue);
        setId_Widget(
            addChild_Widget(buttons, iClob(newIcon_LabelWidget("\u2795", 0, 0, "tabs.new"))),
            "newtab");
    }
    /* Side bar. */ {
        iWidget *content = findChild_Widget(d->root, "tabs.content");
        iSidebarWidget *sidebar = new_SidebarWidget();
        addChildPos_Widget(content, iClob(sidebar), front_WidgetAddPos);
    }
    /* Search bar. */ {
        iWidget *searchBar = new_Widget();        
        setId_Widget(searchBar, "search");
        setFlags_Widget(searchBar,
                        hidden_WidgetFlag | collapse_WidgetFlag | arrangeHeight_WidgetFlag |
                            resizeChildren_WidgetFlag | arrangeHorizontal_WidgetFlag,
                        iTrue);
        addChild_Widget(div, iClob(searchBar));
        setBackgroundColor_Widget(searchBar, uiBackground_ColorId);
        setCommandHandler_Widget(searchBar, handleSearchBarCommands_);
        addChild_Widget(searchBar, iClob(new_LabelWidget("\U0001f50d Text", 0, 0, NULL)));
        iInputWidget *input = new_InputWidget(0);
        setId_Widget(addChildFlags_Widget(searchBar, iClob(input), expand_WidgetFlag),
                     "find.input");
        addChild_Widget(searchBar, iClob(newIcon_LabelWidget("  \u2b9f  ", 'g', KMOD_PRIMARY, "find.next")));
        addChild_Widget(searchBar, iClob(newIcon_LabelWidget("  \u2b9d  ", 'g', KMOD_PRIMARY | KMOD_SHIFT, "find.prev")));
        addChild_Widget(searchBar, iClob(newIcon_LabelWidget("\u2a2f", SDLK_ESCAPE, 0, "find.close")));
    }
    iWidget *tabsMenu = makeMenu_Widget(d->root,
                                        (iMenuItem[]){
                                            { "Close Tab", 0, 0, "tabs.close" },
                                            { "Duplicate Tab", 0, 0, "tabs.new duplicate:1" },
                                            { "---", 0, 0, NULL },
                                            { "Close Other Tabs", 0, 0, "tabs.close toleft:1 toright:1" },
                                            { "Close Tabs To Left", 0, 0, "tabs.close toleft:1" },
                                            { "Close Tabs To Right", 0, 0, "tabs.close toright:1" },
                                        },
                                        6);
    setId_Widget(tabsMenu, "doctabs.menu");
    /* Glboal keyboard shortcuts. */ {
        addAction_Widget(d->root, SDLK_LEFTBRACKET, KMOD_SHIFT | KMOD_PRIMARY, "tabs.prev");
        addAction_Widget(d->root, SDLK_RIGHTBRACKET, KMOD_SHIFT | KMOD_PRIMARY, "tabs.next");
        addAction_Widget(d->root, 'l', KMOD_PRIMARY, "focus.set id:url");
        addAction_Widget(d->root, 'f', KMOD_PRIMARY, "focus.set id:find.input");
    }
}

static void updateRootSize_Window_(iWindow *d) {
    iInt2 *size = &d->root->rect.size;
    const iInt2 oldSize = *size;
    SDL_GetRendererOutputSize(d->render, &size->x, &size->y);
    if (!isEqual_I2(oldSize, *size)) {
        arrange_Widget(d->root);
        postCommandf_App("window.resized width:%d height:%d", size->x, size->y);
        postRefresh_App();
    }
}

static float pixelRatio_Window_(const iWindow *d) {
    int dx, x;
    SDL_GetRendererOutputSize(d->render, &dx, NULL);
    SDL_GetWindowSize(d->win, &x, NULL);
    return (float) dx / (float) x;
}

static void drawBlank_Window_(iWindow *d) {
    const iColor bg = get_Color(uiBackground_ColorId);
    SDL_SetRenderDrawColor(d->render, bg.r, bg.g, bg.b, 255);
    SDL_RenderClear(d->render);
    SDL_RenderPresent(d->render);
}

void init_Window(iWindow *d, iRect rect) {
    theWindow_ = d;
    iZap(d->cursors);
    d->isDrawFrozen = iTrue;
    uint32_t flags = SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI;
#if defined (iPlatformApple)
    SDL_SetHint(SDL_HINT_RENDER_DRIVER, "metal");
#else
    flags |= SDL_WINDOW_OPENGL;
#endif
    SDL_SetHint(SDL_HINT_RENDER_VSYNC, "1");
    if (SDL_CreateWindowAndRenderer(
            width_Rect(rect), height_Rect(rect), flags, &d->win, &d->render)) {
        fprintf(stderr, "Error when creating window: %s\n", SDL_GetError());
        exit(-2);
    }
    if (left_Rect(rect) >= 0) {
        SDL_SetWindowPosition(d->win, left_Rect(rect), top_Rect(rect));
    }
    SDL_SetWindowMinimumSize(d->win, 400, 250);
    SDL_SetWindowTitle(d->win, "Lagrange");    
    /* Some info. */ {
        SDL_RendererInfo info;
        SDL_GetRendererInfo(d->render, &info);
        printf("[window] renderer: %s\n", info.name);
    }
    drawBlank_Window_(d);
    d->uiScale = initialUiScale_;
    d->pixelRatio = pixelRatio_Window_(d);
    setPixelRatio_Metrics(d->pixelRatio * d->uiScale);
#if defined (iPlatformMsys)
    useExecutableIconResource_SDLWindow(d->win);
#endif
#if defined (iPlatformLinux)
    /* Load the window icon. */ {
        int w, h, num;
        const iBlock *icon = &imageLagrange64_Embedded;
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
    iForIndices(i, d->cursors) {
        if (d->cursors[i]) {
            SDL_FreeCursor(d->cursors[i]);
        }
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
            if (event.type == SDL_USEREVENT && isCommand_UserEvent(ev, "window.unfreeze")) {
                d->isDrawFrozen = iFalse;
                postRefresh_App();
                return iTrue;
            }
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

void draw_Window(iWindow *d) {
    if (d->isDrawFrozen) {
        return;
    }
    /* Clear the window. */
    SDL_SetRenderDrawColor(d->render, 0, 0, 0, 255);
    SDL_RenderClear(d->render);
    /* Draw widgets. */
    d->frameTime = SDL_GetTicks();
    draw_Widget(d->root);
#if 0
    /* Text cache debugging. */ {
        SDL_Texture *cache = glyphCache_Text();
        SDL_Rect rect = { d->root->rect.size.x - 640, 0, 640, 5 * 640 };
        SDL_SetRenderDrawColor(d->render, 0, 0, 0, 255);
        SDL_RenderFillRect(d->render, &rect);
        SDL_RenderCopy(d->render, glyphCache_Text(), NULL, &rect);
    }
#endif
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

void setFreezeDraw_Window(iWindow *d, iBool freezeDraw) {
    d->isDrawFrozen = freezeDraw;
}

void setCursor_Window(iWindow *d, int cursor) {
    if (!d->cursors[cursor]) {
        d->cursors[cursor] = SDL_CreateSystemCursor(cursor);
    }
    SDL_SetCursor(d->cursors[cursor]);
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
