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

#include "../app.h"
#include "bookmarks.h"
#include "command.h"
#include "defs.h"
#include "resources.h"
#include "keys.h"
#include "labelwidget.h"
#include "documentwidget.h"
#include "sidebarwidget.h"
#include "paint.h"
#include "root.h"
#include "touch.h"
#include "util.h"

#if defined (iPlatformMsys)
#   include "../win32.h"
#endif
#if defined (iPlatformAppleDesktop)
#   include "macos.h"
#endif
#if defined (iPlatformAppleMobile)
#   include "ios.h"
#endif

#include <the_Foundation/file.h>
#include <the_Foundation/path.h>
#include <the_Foundation/regexp.h>
#include <SDL_hints.h>
#include <SDL_timer.h>
#include <SDL_syswm.h>

#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_RESIZE_IMPLEMENTATION
#include "stb_image.h"
#include "stb_image_resize.h"

static iWindow *    theWindow_;
static iMainWindow *theMainWindow_;

static float initialUiScale_ = 1.0f;
static iBool isOpenGLRenderer_;
static iBool isDrawing_;
static iBool isResizing_;

iDefineTypeConstructionArgs(Window,
                            (enum iWindowType type, iRect rect, uint32_t flags),
                            type, rect, flags)
iDefineTypeConstructionArgs(MainWindow, (iRect rect), rect)

static const iMenuItem fileMenuItems_[] = {
    { "${menu.newwindow}", SDLK_n, KMOD_PRIMARY, "window.new" },
    { "${menu.newtab}", SDLK_t, KMOD_PRIMARY, "tabs.new append:1" },
    { "${menu.openlocation}", SDLK_l, KMOD_PRIMARY, "navigate.focus" },
    { "${menu.reopentab}", SDLK_t, KMOD_SECONDARY, "tabs.new reopen:1" },
    { "---" },
    { "${menu.closetab}", 0, 0, "tabs.close" },
    { "${menu.closetab.other}", 0, 0, "tabs.close toleft:1 toright:1" },
    { "---" },
    { saveToDownloads_Label, SDLK_s, KMOD_PRIMARY, "document.save" },
    { "---" },
    { "${menu.downloads}", 0, 0, "downloads.open" },
    { "${menu.export}", 0, 0, "export" },
#if defined (iPlatformPcDesktop)
    { "---" },
    { "${menu.preferences}", preferences_KeyShortcut, "preferences" },
    { "${menu.fonts}", 0, 0, "open newtab:1 switch:1 url:about:fonts" },
#if defined (LAGRANGE_ENABLE_WINSPARKLE)
    { "${menu.update}", 0, 0, "updater.check" },
#endif
    { "---" },
    { "${menu.quit}", 'q', KMOD_PRIMARY, "quit" },
#endif
    { NULL }
};

static const iMenuItem editMenuItems_[] = {
    { "${menu.cut}", SDLK_x, KMOD_PRIMARY, "input.copy cut:1" },
    { "${menu.copy}", SDLK_c, KMOD_PRIMARY, "copy" },
    { "${menu.paste}", SDLK_v, KMOD_PRIMARY, "input.paste" },
    { "---" },
    { "${menu.copy.pagelink}", SDLK_c, KMOD_PRIMARY | KMOD_SHIFT, "document.copylink" },
    { "---" },
    { "${macos.menu.find}", SDLK_f, KMOD_PRIMARY, "focus.set id:find.input" },
    { NULL }
};

static const iMenuItem viewMenuItems_[] = {
    { "${menu.show.bookmarks}", '1', leftSidebarTab_KeyModifier, "sidebar.mode arg:0 toggle:1" },
    { "${menu.show.feeds}", '2', leftSidebarTab_KeyModifier, "sidebar.mode arg:1 toggle:1" },
    { "${menu.show.history}", '3', leftSidebarTab_KeyModifier, "sidebar.mode arg:2 toggle:1" },
    { "${menu.show.identities}", '4', leftSidebarTab_KeyModifier, "sidebar.mode arg:3 toggle:1" },
    { "${menu.show.outline}", '5', leftSidebarTab_KeyModifier, "sidebar.mode arg:4 toggle:1" },
    { "---" },
    { "${menu.sidebar.left}", leftSidebar_KeyShortcut, "sidebar.toggle" },
    { "${menu.sidebar.right}", rightSidebar_KeyShortcut, "sidebar2.toggle" },
    { "---" },
    { "${menu.back}", SDLK_LEFTBRACKET, KMOD_PRIMARY, "navigate.back" },
    { "${menu.forward}", SDLK_RIGHTBRACKET, KMOD_PRIMARY, "navigate.forward" },
    { "${menu.parent}", navigateParent_KeyShortcut, "navigate.parent" },
    { "${menu.root}", navigateRoot_KeyShortcut, "navigate.root" },
    { "${menu.reload}", reload_KeyShortcut, "navigate.reload" },
    { "---" },
    { "${menu.zoom.in}", SDLK_EQUALS, KMOD_PRIMARY, "zoom.delta arg:10" },
    { "${menu.zoom.out}", SDLK_MINUS, KMOD_PRIMARY, "zoom.delta arg:-10" },
    { "${menu.zoom.reset}", SDLK_0, KMOD_PRIMARY, "zoom.set arg:100" },
    { "---" },
    { "${menu.view.split}", SDLK_j, KMOD_PRIMARY, "splitmenu.open" },
    { NULL }
};

static iMenuItem bookmarksMenuItems_[] = {
    { "${menu.page.bookmark}", bookmarkPage_KeyShortcut, "bookmark.add" },
    { "${menu.page.subscribe}", subscribeToPage_KeyShortcut, "feeds.subscribe" },
    { "${menu.newfolder}", 0, 0, "bookmarks.addfolder" },
    { "---" },
    { "${macos.menu.bookmarks.list}", 0, 0, "open url:about:bookmarks" },
    { "${macos.menu.bookmarks.bytag}", 0, 0, "open url:about:bookmarks?tags" },
    { "${macos.menu.bookmarks.bytime}", 0, 0, "open url:about:bookmarks?created" },
    { "---" },
    { "${menu.sort.alpha}", 0, 0, "bookmarks.sort" },
    { "${menu.import.links}", 0, 0, "bookmark.links confirm:1" },
    { "${menu.bookmarks.refresh}", 0, 0, "bookmarks.reload.remote" },
    { "---" },
    { "${menu.feeds.refresh}", refreshFeeds_KeyShortcut, "feeds.refresh" },
    { "${menu.feeds.entrylist}", 0, 0, "open url:about:feeds" },
    { NULL }
};

static const iMenuItem identityMenuItems_[] = {
    { "${menu.identity.new}", newIdentity_KeyShortcut, "ident.new" },
    { "${menu.identity.newdomain}", 0, 0, "ident.new scope:1" },
    { "---" },
    { "${menu.identity.import}", SDLK_m, KMOD_SECONDARY, "ident.import" },
    { NULL }
};

static const iMenuItem windowMenuItems_[] = {
    { "${menu.tab.next}", 0, 0, "tabs.next" },
    { "${menu.tab.prev}", 0, 0, "tabs.prev" },
    { "${menu.duptab}", 0, 0, "tabs.new duplicate:1" },
    { "---" },
    { "${menu.window.min}", 0, 0, "window.minimize" },
    { "${menu.window.max}", 0, 0, "window.maximize" },
    { "${menu.window.full}", 0, 0, "window.fullscreen" },
    { "---" },
    { NULL }
};

static const iMenuItem helpMenuItems_[] = {
#if defined (iPlatformPcDesktop)
    { "${menu.help}", SDLK_F1, 0, "!open newtab:1 switch:1 url:about:help" },
#else
    { "${menu.help}", 0, 0, "!open newtab:1 switch:1 url:about:help" },
#endif
    { "${menu.releasenotes}", 0, 0, "!open newtab:1 switch:1 url:about:version" },
    { "---" },
    { "${menu.aboutpages}", 0, 0, "!open newtab:1 switch:1 url:about:about" },
    { "${menu.debug}", 0, 0, "!open newtab:1 switch:1 url:about:debug" },
#if defined (iPlatformPcDesktop)
    { "---" },
    { "${menu.aboutapp}", 0, 0, "!open newtab:1 switch:1 url:about:lagrange" },
#endif
    { NULL }
};

const iMenuItem topLevelMenus_Window[7] = {
    { "${menu.title.file}", 0, 0, (const void *) fileMenuItems_ },
    { "${menu.title.edit}", 0, 0, (const void *) editMenuItems_ },
    { "${menu.title.view}", 0, 0, (const void *) viewMenuItems_ },
    { "${menu.title.bookmarks}", 0, 0, (const void *) bookmarksMenuItems_ },
    { "${menu.title.identity}", 0, 0, (const void *) identityMenuItems_ },
    { "${menu.title.window}", 0, 0, (const void *) windowMenuItems_ },
    { "${menu.title.help}", 0, 0, (const void *) helpMenuItems_ },
};

#if defined (LAGRANGE_MAC_MENUBAR)

static iBool macMenusInserted_;

static void insertMacMenus_(void) {
    if (macMenusInserted_) {
        return;
    }
    insertMenuItems_MacOS("${menu.title.file}", 1, 0, fileMenuItems_, iElemCount(fileMenuItems_));
    insertMenuItems_MacOS("${menu.title.edit}", 2, 0, editMenuItems_, iElemCount(editMenuItems_));
    insertMenuItems_MacOS("${menu.title.view}", 3, 0, viewMenuItems_, iElemCount(viewMenuItems_));
    insertMenuItems_MacOS("${menu.title.bookmarks}", 4, 0, bookmarksMenuItems_, iElemCount(bookmarksMenuItems_));
    insertMenuItems_MacOS("${menu.title.identity}", 5, 0, identityMenuItems_, iElemCount(identityMenuItems_));
    insertMenuItems_MacOS("${menu.title.help}", 7, 0, helpMenuItems_, iElemCount(helpMenuItems_));
    macMenusInserted_ = iTrue;
}

static void removeMacMenus_(void) {
    if (!macMenusInserted_) {
        return;
    }
    removeMenu_MacOS(7);
    removeMenu_MacOS(5);
    removeMenu_MacOS(4);
    removeMenu_MacOS(3);
    removeMenu_MacOS(2);
    removeMenu_MacOS(1);
    macMenusInserted_ = iFalse;
}

#endif /* LAGRANGE_MAC_MENUBAR */

int numRoots_Window(const iWindow *d) {
    int num = 0;
    iForIndices(i, d->roots) {
        if (d->roots[i]) num++;
    }
    return num;
}

static void windowSizeChanged_MainWindow_(iMainWindow *d) {
    const int numRoots = numRoots_Window(as_Window(d));
    const iInt2 rootSize = d->base.size;
    int weights[2] = {
        d->base.roots[0] ? (d->splitMode & twoToOne_WindowSplit ? 2 : 1) : 0,
        d->base.roots[1] ? (d->splitMode & oneToTwo_WindowSplit ? 2 : 1) : 0,
    };
#if defined (iPlatformDesktop)
    if (prefs_App()->evenSplit && numRoots > 1 && !(d->splitMode & vertical_WindowSplit)) {
        /* Include sidebars in the weights. */
        iWidget *sidebars[2][2];
        iZap(sidebars);
        int avail = rootSize.x;
        iForIndices(r, d->base.roots) {
            if (d->base.roots[r]) {
                iForIndices(sb, sidebars[r]) {
                    iWidget *bar = findChild_Widget(d->base.roots[r]->widget,
                                                    sb == 0 ? "sidebar" : "sidebar2");
                    if (isVisible_Widget(bar)) {
                        avail -= width_Widget(bar);
                    }
                    else {
                        bar = NULL;
                    }
                    sidebars[r][sb] = bar;
                }
            }
        }
        float balance[2] = {
            (d->splitMode & equal_WindowSplit) == equal_WindowSplit ? 0.5f
            : d->splitMode & twoToOne_WindowSplit                   ? 0.667f
                                                                    : 0.333f,
            (d->splitMode & equal_WindowSplit) == equal_WindowSplit ? 0.5f
            : d->splitMode & twoToOne_WindowSplit                   ? 0.333f
                                                                    : 0.667f,
        };
        weights[0] = balance[0] * avail + width_Widget(sidebars[0][0]) + width_Widget(sidebars[0][1]);
        weights[1] = balance[1] * avail + width_Widget(sidebars[1][0]) + width_Widget(sidebars[1][1]);
    }
#endif
    const int totalWeight = weights[0] + weights[1];
    int w = 0;
    iForIndices(i, d->base.roots) {
        iRoot *root = d->base.roots[i];
        if (root) {
            iRect *rect = &root->widget->rect;
            /* Horizontal split frame. */
            if (d->splitMode & vertical_WindowSplit) {
                rect->pos  = init_I2(0, rootSize.y * w / totalWeight);
                rect->size = init_I2(rootSize.x, rootSize.y * (w + weights[i]) / totalWeight - rect->pos.y);
            }
            else {
                rect->pos  = init_I2(rootSize.x * w / totalWeight, 0);
                rect->size = init_I2(rootSize.x * (w + weights[i]) / totalWeight - rect->pos.x, rootSize.y);
            }
            w += weights[i];
            root->widget->minSize = rect->size;
            setCurrent_Root(root);
            updatePadding_Root(root);
            arrange_Widget(root->widget);
        }
    }
}

void resizeSplits_MainWindow(iMainWindow *d, iBool updateDocumentSize) {
    windowSizeChanged_MainWindow_(d);
    if (updateDocumentSize) {
        iForIndices(i, d->base.roots) {
            iRoot *root = d->base.roots[i];
            if (root) {
                updateSize_DocumentWidget(document_Root(root));
            }
        }
    }
}

static void setupUserInterface_MainWindow(iMainWindow *d) {
#if defined (LAGRANGE_MAC_MENUBAR)
    insertMacMenus_(); /* TODO: Shouldn't this be in the App? */
#endif
    /* One root is created by default. */
    d->base.roots[0] = new_Root();
    d->base.roots[0]->window = as_Window(d);
    setCurrent_Root(d->base.roots[0]);
    createUserInterface_Root(d->base.roots[0]);
    setCurrent_Root(NULL);
    /* One of the roots always has keyboard input focus. */
    d->base.keyRoot = d->base.roots[0];
}

static iBool updateSize_MainWindow_(iMainWindow *d, iBool notifyAlways) {
    iInt2 *size = &d->base.size;
    const iInt2 oldSize = *size;
    SDL_GetRendererOutputSize(d->base.render, &size->x, &size->y);
    size->y -= d->keyboardHeight;
    const iBool hasChanged = !isEqual_I2(oldSize, *size);
    if (hasChanged) {
        windowSizeChanged_MainWindow_(d);
        postRefresh_App();
    }
    if (!isResizing_ && (hasChanged || notifyAlways)) {
        if (!isEqual_I2(*size, d->place.lastNotifiedSize)) {
            const iBool isHoriz = (d->place.lastNotifiedSize.x != size->x);
            const iBool isVert  = (d->place.lastNotifiedSize.y != size->y);
            postCommandf_App("window.resized width:%d height:%d horiz:%d vert:%d",
                             size->x,
                             size->y,
                             isHoriz,
                             isVert);
            postCommand_App("widget.overflow"); /* check bounds with updated sizes */
            postRefresh_App();
        }
        d->place.lastNotifiedSize = *size;
    }
    return hasChanged;
}

void drawWhileResizing_MainWindow(iMainWindow *d, int w, int h) {
    if (!isDrawing_) {
        isResizing_ = iTrue;
        setCurrent_Window(d);
        draw_MainWindow(d);
        isResizing_ = iFalse;
    }
}

static float pixelRatio_Window_(const iWindow *d) {
    int dx, x;
    SDL_GetRendererOutputSize(d->render, &dx, NULL);
    SDL_GetWindowSize(d->win, &x, NULL);
    return (float) dx / (float) x;
}

#if defined (iPlatformApple)
#   define baseDPI_Window   113.5f
#else
#   define baseDPI_Window   96.0f
#endif

#if defined (iPlatformAndroidMobile)
float displayDensity_Android(void);
#endif

static float displayScale_Window_(const iWindow *d) {
#if !defined (iPlatformTerminal)
    /* The environment variable LAGRANGE_OVERRIDE_DPI can be used to override the automatic
       display DPI detection. If not set, or is an empty string, ignore it.
       Note: the same value used for all displays. */
    const char *LAGRANGE_OVERRIDE_DPI = getenv("LAGRANGE_OVERRIDE_DPI");
    if (LAGRANGE_OVERRIDE_DPI && *LAGRANGE_OVERRIDE_DPI) {
        /* If the user has set the env var, but it is not an int, atoi */
        /* will return 0, in which case we just guess and return 96 DPI.  */
        const int envDpi = atoi(LAGRANGE_OVERRIDE_DPI);
        if (envDpi > 0) {
            return ((float) envDpi) / baseDPI_Window;
        }
        fprintf(stderr, "[window] WARNING: failed to parse LAGRANGE_OVERRIDE_DPI='%s', "
                "ignoring it\n", LAGRANGE_OVERRIDE_DPI);
        /* To avoid showing the warning multiple times, overwrite
         LAGRANGE_OVERRIDE_DPI with the empty string. */
        setenv("LAGRANGE_OVERRIDE_DPI", "", 1);
    }
#endif
#if defined (iPlatformApple) || defined (iPlatformTerminal)
    iUnused(d);
    /* Apple UI sizes are fixed and only scaled by pixel ratio. */
    /* TODO: iOS text size setting? */
    return 1.0f;
#elif defined (iPlatformMsys)
    iUnused(d);
    return desktopDPI_Win32();
#elif defined (iPlatformAndroidMobile)
    return displayDensity_Android();
#else
    if (isRunningUnderWindowSystem_App()) {
        float vdpi = 0.0f;
        SDL_GetDisplayDPI(SDL_GetWindowDisplayIndex(d->win), NULL, NULL, &vdpi);
//      printf("DPI: %f\n", vdpi);
        const float factor = vdpi / baseDPI_Window / pixelRatio_Window_(d);
        return iMax(1.0f, factor);
    }
    return 1.0f;
#endif
}

static void drawBlank_Window_(iWindow *d) {
    const iColor bg = default_Color(uiBackground_ColorId);
    SDL_SetRenderDrawColor(d->render, bg.r, bg.g, bg.b, 255);
    SDL_RenderClear(d->render);
    SDL_RenderPresent(d->render);
}

static iRoot *rootAt_Window_(const iWindow *d, iInt2 coord) {
    iForIndices(i, d->roots) {
        iRoot *root = d->roots[i];
        if (root && contains_Rect(rect_Root(root), coord)) {
            return root;
        }
    }
    return d->roots[0];
}

#if defined (LAGRANGE_ENABLE_CUSTOM_FRAME)
static SDL_HitTestResult hitTest_MainWindow_(SDL_Window *win, const SDL_Point *pos, void *data) {
    iMainWindow *d = data;
    iAssert(d->base.win == win);
    if (SDL_GetWindowFlags(win) & (SDL_WINDOW_MOUSE_CAPTURE | SDL_WINDOW_FULLSCREEN_DESKTOP)) {
        return SDL_HITTEST_NORMAL;
    }
    const int snap = snap_MainWindow(d);
    int w, h;
    SDL_GetWindowSize(win, &w, &h);
    setCurrent_Window(as_Window(d));
    /* TODO: Check if inside the caption label widget. */
    const iBool isLeft   = pos->x < gap_UI;
    const iBool isRight  = pos->x >= w - gap_UI;
    const iBool isTop    = pos->y < gap_UI && snap != yMaximized_WindowSnap;
    const iBool isBottom = pos->y >= h - gap_UI && snap != yMaximized_WindowSnap;
    const int   captionHeight = lineHeight_Text(uiContent_FontId) + gap_UI * 2;
    const int   rightEdge     = left_Rect(bounds_Widget(findChild_Widget(
                                    rootAt_Window_(as_Window(d), init_I2(pos->x, pos->y))->widget,
                                    "winbar.min")));
    setCurrent_Window(NULL);                                    
    d->place.lastHit = SDL_HITTEST_NORMAL;
    if (snap != maximized_WindowSnap) {
        if (isLeft) {
            return pos->y < captionHeight       ? SDL_HITTEST_RESIZE_TOPLEFT
                   : pos->y > h - captionHeight ? SDL_HITTEST_RESIZE_BOTTOMLEFT
                                                : (d->place.lastHit = SDL_HITTEST_RESIZE_LEFT);
        }
        if (isRight) {
            return pos->y < captionHeight       ? SDL_HITTEST_RESIZE_TOPRIGHT
                   : pos->y > h - captionHeight ? SDL_HITTEST_RESIZE_BOTTOMRIGHT
                                                : (d->place.lastHit = SDL_HITTEST_RESIZE_RIGHT);
        }
        if (isTop) {
            return pos->x < captionHeight       ? SDL_HITTEST_RESIZE_TOPLEFT
                   : pos->x > w - captionHeight ? SDL_HITTEST_RESIZE_TOPRIGHT
                                                : (d->place.lastHit = SDL_HITTEST_RESIZE_TOP);
        }
        if (isBottom) {
            return pos->x < captionHeight       ? SDL_HITTEST_RESIZE_BOTTOMLEFT
                   : pos->x > w - captionHeight ? SDL_HITTEST_RESIZE_BOTTOMRIGHT
                                                : (d->place.lastHit = SDL_HITTEST_RESIZE_BOTTOM);
        }
    }
    if (pos->x < rightEdge && pos->y < captionHeight) {
        return SDL_HITTEST_DRAGGABLE;
    }
    return SDL_HITTEST_NORMAL;
}

SDL_HitTestResult hitTest_MainWindow(const iMainWindow *d, iInt2 pos) {
    return hitTest_MainWindow_(d->base.win, &(SDL_Point){ pos.x, pos.y }, iConstCast(void *, d));
}
#endif

void create_Window_(iWindow *d, iRect rect, uint32_t flags) {
    flags |= SDL_WINDOW_ALLOW_HIGHDPI | SDL_WINDOW_HIDDEN;
    if (d->type == main_WindowType) {
        flags |= SDL_WINDOW_RESIZABLE;
#if defined (LAGRANGE_ENABLE_CUSTOM_FRAME)
        if (prefs_App()->customFrame) {
            /* We are drawing a custom frame so hide the default one. */
            flags |= SDL_WINDOW_BORDERLESS;
        }
#endif
    }
    const iBool setPos = left_Rect(rect) >= 0 || top_Rect(rect) >= 0;
    d->win = SDL_CreateWindow("",
                              setPos ? left_Rect(rect) : SDL_WINDOWPOS_CENTERED,
                              setPos ? top_Rect(rect) : SDL_WINDOWPOS_CENTERED,
                              width_Rect(rect),
                              height_Rect(rect),
                              flags);
    if (!d->win) {
        if (flags & SDL_WINDOW_OPENGL) {
            /* Try without OpenGL support, then. */
            setForceSoftwareRender_App(iTrue);
            d->win = SDL_CreateWindow("",
                                      setPos ? left_Rect(rect) : SDL_WINDOWPOS_CENTERED,
                                      setPos ? top_Rect(rect) : SDL_WINDOWPOS_CENTERED,
                                      width_Rect(rect),
                                      height_Rect(rect),
                                      flags & ~SDL_WINDOW_OPENGL);
        }
        if (!d->win) {
            fprintf(stderr, "[window] failed to create window: %s\n", SDL_GetError());
            exit(-3);
        }
    }
    if (forceSoftwareRender_App()) {
        SDL_SetHint(SDL_HINT_RENDER_DRIVER, "software");
    }
    d->render = SDL_CreateRenderer(
        d->win,
        -1,
        (forceSoftwareRender_App() ? SDL_RENDERER_SOFTWARE : SDL_RENDERER_ACCELERATED) |
            SDL_RENDERER_PRESENTVSYNC | SDL_RENDERER_TARGETTEXTURE);
    if (!d->render) {
        /* Try a basic software rendering instead. */
        SDL_SetHint(SDL_HINT_RENDER_DRIVER, "software");
        d->render = SDL_CreateRenderer(d->win, -1, SDL_RENDERER_SOFTWARE);
        if (!d->render) {
            /* This shouldn't fail.-..? */
            fprintf(stderr, "[window] failed to create renderer: %s\n", SDL_GetError());
            exit(-4);
        }
    }
#if defined(LAGRANGE_ENABLE_CUSTOM_FRAME)
    if (type_Window(d) == main_WindowType && prefs_App()->customFrame) {
        /* Register a handler for window hit testing (drag, resize). */
        SDL_SetWindowHitTest(d->win, hitTest_MainWindow_, d);
        SDL_SetWindowResizable(d->win, SDL_TRUE);
    }
#endif
}

static SDL_Surface *loadImage_(const iBlock *data, int resized) {
    int      w = 0, h = 0, num = 4;
    stbi_uc *pixels = stbi_load_from_memory(
        constData_Block(data), (int) size_Block(data), &w, &h, &num, STBI_rgb_alpha);
    if (resized) {
        stbi_uc *rsPixels = malloc(num * resized * resized);
        stbir_resize_uint8(pixels, w, h, 0, rsPixels, resized, resized, 0, num);
        free(pixels);
        pixels = rsPixels;
        w = h = resized;
    }
    return SDL_CreateRGBSurfaceWithFormatFrom(
        pixels, w, h, 8 * num, w * num, SDL_PIXELFORMAT_RGBA32);
}

static void updateMetrics_Window_(const iWindow *d) {
    setScale_Metrics(d->pixelRatio * d->displayScale * d->uiScale);
}

static iAtomicInt windowSerialNumber_;

void init_Window(iWindow *d, enum iWindowType type, iRect rect, uint32_t flags) {
    d->type          = type;
    d->serial        = add_Atomic(&windowSerialNumber_, 1);
    d->win           = NULL;
    d->size          = zero_I2(); /* will be updated below */
    d->hover         = NULL;
    d->lastHover     = NULL;
    d->mouseGrab     = NULL;
    d->focus         = NULL;
    d->pendingCursor = NULL;
    d->isExposed     = (deviceType_App() != desktop_AppDeviceType);
    d->isMinimized   = iFalse;
    d->isInvalidated = iFalse; /* set when posting event, to avoid repeated events */
    d->isMouseInside = iTrue;
    set_Atomic(&d->isRefreshPending, iTrue);
    d->ignoreClick   = iFalse;
    d->focusGainedAt = SDL_GetTicks();
    d->frameTime     = SDL_GetTicks();
    d->keyRoot       = NULL;
    d->borderShadow  = NULL;
    d->frameCount    = 0;
    iZap(d->roots);
    iZap(d->cursors);
    create_Window_(d, rect, flags);
    SDL_GetRendererOutputSize(d->render, &d->size.x, &d->size.y);
#if !defined (iPlatformTerminal)
    /* Renderer info. */ {
        SDL_RendererInfo info;
        SDL_GetRendererInfo(d->render, &info);
#if !defined (NDEBUG)
        printf("[window] renderer: %s%s\n",
               info.name,
               info.flags & SDL_RENDERER_ACCELERATED ? " (accelerated)" : "");
#endif
    }
#endif /* !iPlatformTerminal */
#if defined (iPlatformMsys)
    if (type == extra_WindowType) {
        enableDarkMode_SDLWindow(d->win);
    }
#endif
    drawBlank_Window_(d);
    d->pixelRatio   = pixelRatio_Window_(d); /* point/pixel conversion */
    d->displayScale = displayScale_Window_(d);
    d->uiScale      = initialUiScale_;
    if (d->type == main_WindowType) {
        updateMetrics_Window_(d);
    }
    setCurrent_Window(d); /* Text assumes global state is up-to-date */
    d->text = new_Text(d->render, (float) prefs_App()->zoomPercent / 100.0f);
}

static void deinitRoots_Window_(iWindow *d) {
    iRecycle();
    iForIndices(i, d->roots) {
        if (d->roots[i]) {
            setCurrent_Root(d->roots[i]);
            delete_Root(d->roots[i]);
            d->roots[i] = NULL;
        }
    }
    setCurrent_Root(NULL);
}

void deinit_Window(iWindow *d) {
    setCurrent_Window(d);
    if (d->type == popup_WindowType) {
        removePopup_App(d);
    }
    else if (d->type == extra_WindowType) {
        removeExtraWindow_App(d);
    }
    deinitRoots_Window_(d);
    delete_Text(d->text);
    SDL_DestroyRenderer(d->render);
    SDL_DestroyWindow(d->win);
    iForIndices(i, d->cursors) {
        if (d->cursors[i]) {
            SDL_FreeCursor(d->cursors[i]);
        }
    }
    setCurrent_Window(NULL);
}

static void setWindowIcon_Window_(iWindow *d) {
#if defined (iPlatformMsys)
    useExecutableIconResource_SDLWindow(d->win);
#endif
#if defined (iPlatformLinux) && !defined (iPlatformTerminal)
    SDL_Surface *surf = loadImage_(&imageLagrange64_Resources, 0);
    SDL_SetWindowIcon(d->win, surf);
    free(surf->pixels);
    SDL_FreeSurface(surf);
#endif
    iUnused(d); /* other platforms */
}

void init_MainWindow(iMainWindow *d, iRect rect) {
    theWindow_ = &d->base;
    theMainWindow_ = d;
    d->enableBackBuf = iFalse;
    uint32_t flags = 0;
#if defined (iPlatformAppleDesktop)
    SDL_SetHint(SDL_HINT_RENDER_DRIVER, shouldDefaultToMetalRenderer_MacOS() ? "metal" : "opengl");
    flags |= shouldDefaultToMetalRenderer_MacOS() ? SDL_WINDOW_METAL : SDL_WINDOW_OPENGL;
    if (flags & SDL_WINDOW_METAL) {
        /* There are some really odd refresh glitches that only occur with the Metal 
           backend. It's perhaps related to it not expecting refresh to stop intermittently
           to wait for input events. If forcing constant refreshing at full frame rate, the
           problems seem to go away... Rendering everything to a separate render target
           appears to sidestep some of the glitches. */
        d->enableBackBuf = iTrue;
    }
#elif defined (iPlatformAppleMobile)
    SDL_SetHint(SDL_HINT_RENDER_DRIVER, "metal");
    flags |= SDL_WINDOW_METAL;
    d->base.isExposed = iTrue;
#elif defined (iPlatformAndroidMobile)
    d->base.isExposed = iTrue;
#else
    if (!forceSoftwareRender_App()) {
        flags |= SDL_WINDOW_OPENGL;
    }
#endif
    SDL_SetHint(SDL_HINT_RENDER_VSYNC, "1");
    init_Window(&d->base, main_WindowType, rect, flags);
    d->isDrawFrozen           = iTrue;
#if defined (iPlatformMsys)
    /* It is less glitchy to allow drawing as early as possible, to avoid white
       flashes when windows are created. */
    d->isDrawFrozen           = iFalse;
#endif
    d->splitMode              = 0;
    d->pendingSplitMode       = 0;
    d->pendingSplitUrl        = new_String();
    d->pendingSplitOrigin     = new_String();
    d->pendingSplitSetIdent   = new_String();
    d->place.initialPos       = rect.pos;
    d->place.normalRect       = rect;
    d->place.lastNotifiedSize = zero_I2();
    d->place.snap             = 0;
    d->keyboardHeight         = 0;
    d->backBuf                = NULL;
    const iInt2 minSize =
        (isMobile_Platform() ? zero_I2() /* windows aren't independently resizable */
                             : init_I2(425, 325));
    SDL_SetWindowMinimumSize(d->base.win, minSize.x, minSize.y);
    SDL_SetWindowTitle(d->base.win, "Lagrange");
    /* Some info. */ {
        SDL_RendererInfo info;
        SDL_GetRendererInfo(d->base.render, &info);
        isOpenGLRenderer_ = !iCmpStr(info.name, "opengl");
#if !defined(NDEBUG) && !defined (iPlatformTerminal)
        printf("[window] max texture size: %d x %d\n",
               info.max_texture_width,
               info.max_texture_height);
        for (size_t i = 0; i < info.num_texture_formats; ++i) {
            printf("[window] supported texture format: %s\n",
                   SDL_GetPixelFormatName(info.texture_formats[i]));
        }
#endif
    }
#if defined (iPlatformMsys)
    SDL_SetWindowMinimumSize(d->base.win, minSize.x * d->base.displayScale, minSize.y * d->base.displayScale);
    enableDarkMode_SDLWindow(d->base.win);
#endif
#if defined (iPlatformLinux) && !defined (iPlatformTerminal)
    SDL_SetWindowMinimumSize(d->base.win, minSize.x * d->base.pixelRatio, minSize.y * d->base.pixelRatio);
#endif
#if defined (iPlatformAppleMobile)
    setupWindow_iOS(as_Window(d));
#endif
    setWindowIcon_Window_(as_Window(d));
    setCurrent_Text(d->base.text);
    SDL_GetRendererOutputSize(d->base.render, &d->base.size.x, &d->base.size.y);
    d->maxDrawableHeight = d->base.size.y;
    setupUserInterface_MainWindow(d);
    postCommand_App("~bindings.changed"); /* update from bindings */
    /* Load the border shadow texture. */ {
        SDL_Surface *surf = loadImage_(&imageShadow_Resources, 0);
        d->base.borderShadow = SDL_CreateTextureFromSurface(d->base.render, surf);
        SDL_SetTextureBlendMode(d->base.borderShadow, SDL_BLENDMODE_BLEND);
        free(surf->pixels);
        SDL_FreeSurface(surf);
    }
    d->appIcon = NULL;
#if defined (LAGRANGE_ENABLE_CUSTOM_FRAME)
    /* Load the app icon for drawing in the title bar. */
    if (prefs_App()->customFrame) {
        SDL_Surface *surf = loadImage_(&imageLagrange64_Resources, appIconSize_Root());
        SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");
        d->appIcon = SDL_CreateTextureFromSurface(d->base.render, surf);
        free(surf->pixels);
        SDL_FreeSurface(surf);
        /* We need to observe non-client-area events. */
        SDL_EventState(SDL_SYSWMEVENT, SDL_TRUE);
    }
#endif
#if defined (iPlatformDesktop) && !defined (iPlatformTerminal)
    SDL_HideWindow(d->base.win);
#else
    SDL_ShowWindow(d->base.win);
#endif
}

void deinit_MainWindow(iMainWindow *d) {
    setCurrent_Window(as_Window(d));
    removeWindow_App(d);
    if (d->backBuf) {
        SDL_DestroyTexture(d->backBuf);
    }
    deinitRoots_Window_(as_Window(d));
    if (theWindow_ == as_Window(d)) {
        theWindow_ = NULL;
    }
    if (theMainWindow_ == d) {
        theMainWindow_ = NULL;
    }
    delete_String(d->pendingSplitSetIdent);
    delete_String(d->pendingSplitOrigin);
    delete_String(d->pendingSplitUrl);
    deinit_Window(&d->base);
    setCurrent_Window(NULL);
}

SDL_Renderer *renderer_Window(const iWindow *d) {
    return d->render;
}

iInt2 maxTextureSize_Window(const iWindow *d) {
    SDL_RendererInfo info;
    SDL_GetRendererInfo(d->render, &info);
    return init_I2(info.max_texture_width, info.max_texture_height);
}

iBool isFullscreen_MainWindow(const iMainWindow *d) {
    return snap_MainWindow(d) == fullscreen_WindowSnap;
}

iRoot *findRoot_Window(const iWindow *d, const iWidget *widget) {
    
    while (widget->parent) {
        widget = widget->parent;
    }
    iForIndices(i, d->roots) {
        if (d->roots[i] && d->roots[i]->widget == widget) {
            return d->roots[i];
        }
    }
    return NULL;
}

iRoot *otherRoot_Window(const iWindow *d, iRoot *root) {
    return root == d->roots[0] && d->roots[1] ? d->roots[1] : d->roots[0];
}

void rootOrder_Window(const iWindow *d, iRoot *roots[2]) {
    if (d) {
        roots[0] = d->keyRoot;
        roots[1] = (roots[0] == d->roots[0] ? d->roots[1] : d->roots[0]);
    }
    else {
        roots[0] = roots[1] = NULL;
    }
}

static void invalidate_Window_(iAnyWindow *d, iBool forced) {
    iWindow *w = as_Window(d);
    if (w && (!w->isInvalidated || forced)) {
        w->isInvalidated = iTrue;
        if (w->type == main_WindowType) {
            iMainWindow *mw = as_MainWindow(w);
            if (mw->enableBackBuf && mw->backBuf) {
                SDL_DestroyTexture(mw->backBuf);
                mw->backBuf = NULL;
            }
        }
        resetFontCache_Text(text_Window(w));
        postCommand_App("theme.changed auto:1"); /* forces UI invalidation */
    }
}

void invalidate_Window(iAnyWindow *d) {
    invalidate_Window_(d, iFalse);
}

static void invalidate_MainWindow_(iMainWindow *d, iBool forced) {
    invalidate_Window_(as_Window(d), forced);
}

static iBool isNormalPlacement_MainWindow_(const iMainWindow *d) {
    if (d->isDrawFrozen) return iFalse;
#if defined (iPlatformApple)
    /* Maximized mode is not special on macOS. */
    if (snap_MainWindow(d) == maximized_WindowSnap) {
        return iTrue;
    }
#endif
    if (snap_MainWindow(d)) return iFalse;
    return !(SDL_GetWindowFlags(d->base.win) & SDL_WINDOW_MINIMIZED);
}

static iBool unsnap_MainWindow_(iMainWindow *d, const iInt2 *newPos) {
#if defined (LAGRANGE_ENABLE_CUSTOM_FRAME)
    if (!prefs_App()->customFrame) {
        return iFalse;
    }
    const int snap = snap_MainWindow(d);
    if (snap == yMaximized_WindowSnap || snap == left_WindowSnap || snap == right_WindowSnap) {
        if (!newPos || (d->place.lastHit == SDL_HITTEST_RESIZE_LEFT ||
                        d->place.lastHit == SDL_HITTEST_RESIZE_RIGHT)) {
            return iFalse;
        }
        if (newPos) {
            SDL_Rect usable;
            SDL_GetDisplayUsableBounds(SDL_GetWindowDisplayIndex(d->base.win), &usable);
            /* Snap to top. */
            if (snap == yMaximized_WindowSnap &&
                iAbs(newPos->y - usable.y) < lineHeight_Text(uiContent_FontId) * 2) {
                setSnap_MainWindow(d, redo_WindowSnap | yMaximized_WindowSnap);
                return iFalse;
            }
        }
    }
    if (snap && snap != fullscreen_WindowSnap) {
        if (snap_MainWindow(d) == yMaximized_WindowSnap && newPos) {
            d->place.normalRect.pos = *newPos;
        }
        //printf("unsnap\n"); fflush(stdout);
        setSnap_MainWindow(d, none_WindowSnap);
        return iTrue;
    }
#endif
    return iFalse;
}

static void notifyMetricsChange_Window_(const iWindow *d) {
    /* Dynamic UI metrics change. Widgets need to update themselves. */
    updateMetrics_Window_(d);
    resetFonts_Text(d->text);
    iForIndices(i, d->roots) {
        if (d->roots[i]) {
            postCommand_Root(d->roots[i], "metrics.changed");
        }
    }
}

static void checkPixelRatioChange_Window_(iWindow *d) {
    iBool wasChanged = iFalse;
    const float ratio = pixelRatio_Window_(d);
    if (iAbs(ratio - d->pixelRatio) > 0.001f) {
        d->pixelRatio = ratio;
        wasChanged = iTrue;
    }
    const float scale = displayScale_Window_(d);
    if (iAbs(scale - d->displayScale) > 0.001f) {
        d->displayScale = scale;
        wasChanged = iTrue;
    }
    if (wasChanged) {
        notifyMetricsChange_Window_(d);
    }
}

static iBool handleWindowEvent_Window_(iWindow *d, const SDL_WindowEvent *ev) {
    if (ev->windowID != SDL_GetWindowID(d->win)) {
        return iFalse;
    }
    switch (ev->event) {
#if SDL_VERSION_ATLEAST(2, 0, 18)
        case SDL_WINDOWEVENT_DISPLAY_CHANGED:
            checkPixelRatioChange_Window_(as_Window(d));
            return iTrue;
#endif
        case SDL_WINDOWEVENT_CLOSE:
            if (d->type == extra_WindowType) {
                closeWindow_App(d);
                return iTrue;
            }
            return iFalse;
        case SDL_WINDOWEVENT_EXPOSED:
            d->isExposed = iTrue;
            if (d->type == extra_WindowType) {
                checkPixelRatioChange_Window_(d);
            }
            postRefresh_App();
            return iTrue;
        case SDL_WINDOWEVENT_RESTORED:
        case SDL_WINDOWEVENT_SHOWN:
            postRefresh_App();
            return iTrue;
        case SDL_WINDOWEVENT_MOVED:
            if (d->type == extra_WindowType) {
                checkPixelRatioChange_Window_(d);
            }
            return iFalse;
        case SDL_WINDOWEVENT_FOCUS_GAINED:
            if (d->type == extra_WindowType) {
                d->focusGainedAt = SDL_GetTicks();
                setCapsLockDown_Keys(iFalse);
                postCommand_App("window.focus.gained");
                d->isExposed = iTrue;
                setActiveWindow_App(d);
#if !defined (iPlatformDesktop)
                /* Returned to foreground, may have lost buffered content. */
                invalidate_Window(d);
                postCommand_App("window.unfreeze");
#endif                
            }
            return iFalse;
        case SDL_WINDOWEVENT_TAKE_FOCUS:
            if (d->type == extra_WindowType) {
                SDL_SetWindowInputFocus(d->win);
                postRefresh_App();
                return iTrue;
            }
            return iFalse;
        case SDL_WINDOWEVENT_FOCUS_LOST:
            if (d->type == popup_WindowType) {
                /* Popup windows are currently only used for menus. */
                closeMenu_Widget(d->roots[0]->widget);
            }
            else {
                postCommand_App("window.focus.lost");
                closePopups_App(iTrue);
            }
            return iTrue;
        case SDL_WINDOWEVENT_LEAVE:
            unhover_Widget();
            d->isMouseInside = iFalse;
            if (d->type == extra_WindowType) {
                postCommand_App("window.mouse.exited");
            }
            postRefresh_App();
            return iTrue;
        case SDL_WINDOWEVENT_ENTER:
            d->isMouseInside = iTrue;
            if (d->type == extra_WindowType) {
                postCommand_App("window.mouse.entered");
            }
            return iTrue;
    }
    return iFalse;
}

static void savePlace_MainWindow_(iAny *mainWindow) {
    iMainWindow *d = mainWindow;
    if (isNormalPlacement_MainWindow_(d)) {
        iInt2 newPos;
        SDL_GetWindowPosition(d->base.win, &newPos.x, &newPos.y);
        d->place.normalRect.pos = newPos;
        iInt2 border = zero_I2();
#if !defined(iPlatformApple) && !defined (iPlatformTerminal)
        SDL_GetWindowBordersSize(d->base.win, &border.y, &border.x, NULL, NULL);
        iAssert(~SDL_GetWindowFlags(d->base.win) & SDL_WINDOW_MAXIMIZED);
#endif
        d->place.normalRect.pos =
            max_I2(zero_I2(), sub_I2(d->place.normalRect.pos, border));        
    }
}

static void setHoverUnderCursor_Window_(iWindow *d) {
    if (isDesktop_Platform()) {
        iWidget *hover = hitChild_Window(d, mouseCoord_Window(d, 0));
        if (hover) {
            setHover_Widget(hover);
        }
    }
}

static iBool handleWindowEvent_MainWindow_(iMainWindow *d, const SDL_WindowEvent *ev) {
    if (ev->windowID != SDL_GetWindowID(d->base.win)) {
        return iFalse;
    }
    switch (ev->event) {
#if defined(iPlatformDesktop)
        case SDL_WINDOWEVENT_EXPOSED:
            d->base.isExposed = iTrue;
            /* Since we are manually controlling when to redraw the window, we are responsible
               for ensuring that window contents get redrawn after expose events. Under certain
               circumstances (e.g., under openbox), not doing this would mean that the window
               is missing contents until other events trigger a refresh. */
            postRefresh_App();
#if defined(LAGRANGE_ENABLE_WINDOWPOS_FIX)
            if (d->place.initialPos.x >= 0) {
                /* Must not move a maximized window. */
                if (snap_MainWindow(d) == 0) {
                    int bx, by;
                    SDL_GetWindowBordersSize(d->base.win, &by, &bx, NULL, NULL);
                    // printf("EXPOSED sets position %d %d\n", d->place.initialPos.x, d->place.initialPos.y);
                    SDL_SetWindowPosition(
                        d->base.win, d->place.initialPos.x + bx, d->place.initialPos.y + by);
                }
                d->place.initialPos = init1_I2(-1);
            }
#endif
            return iFalse;
        case SDL_WINDOWEVENT_MOVED: {
            if (d->base.isMinimized) {
                return iFalse;
            }
            closePopups_App(iFalse);
            checkPixelRatioChange_Window_(as_Window(d));
            const iInt2 newPos = init_I2(ev->data1, ev->data2);
            if (isEqual_I2(newPos, init1_I2(-32000))) { /* magic! */
                /* Maybe minimized? Seems like a Windows constant of some kind. */
                d->base.isMinimized = iTrue;
                return iFalse;
            }
#if defined(LAGRANGE_ENABLE_CUSTOM_FRAME)
            /* Set the snap position depending on where the mouse cursor is. */
            if (prefs_App()->customFrame) {
                SDL_Rect usable;
                iInt2    mouse = cursor_Win32(); /* SDL is unaware of the current cursor pos */
                SDL_GetDisplayUsableBounds(SDL_GetWindowDisplayIndex(d->base.win), &usable);
                const iBool isTop    = iAbs(mouse.y - usable.y) < gap_UI * 20;
                const iBool isBottom = iAbs(usable.y + usable.h - mouse.y) < gap_UI * 20;
                if (iAbs(mouse.x - usable.x) < gap_UI) {
                    setSnap_MainWindow(d,
                                       redo_WindowSnap | left_WindowSnap |
                                           (isTop ? topBit_WindowSnap : 0) |
                                           (isBottom ? bottomBit_WindowSnap : 0));
                    return iTrue;
                }
                if (iAbs(mouse.x - usable.x - usable.w) < gap_UI) {
                    setSnap_MainWindow(d,
                                       redo_WindowSnap | right_WindowSnap |
                                           (isTop ? topBit_WindowSnap : 0) |
                                           (isBottom ? bottomBit_WindowSnap : 0));
                    return iTrue;
                }
                if (iAbs(mouse.y - usable.y) < 2) {
                    setSnap_MainWindow(d,
                                       redo_WindowSnap | (d->place.lastHit == SDL_HITTEST_RESIZE_TOP
                                                              ? yMaximized_WindowSnap
                                                              : maximized_WindowSnap));
                    return iTrue;
                }
            }
#endif /* defined LAGRANGE_ENABLE_CUSTOM_FRAME */
            if (unsnap_MainWindow_(d, &newPos)) {
                return iTrue;
            }
            addTicker_App(savePlace_MainWindow_, d);
            return iTrue;
        }
        case SDL_WINDOWEVENT_RESIZED:
            if (d->base.isMinimized) {
                // updateSize_Window_(d, iTrue);
                return iTrue;
            }
            closePopups_App(iFalse);
            if (unsnap_MainWindow_(d, NULL)) {
                return iTrue;
            }
            if (isNormalPlacement_MainWindow_(d)) {
                // printf("RESIZED sets normalRect\n");
                d->place.normalRect.size = init_I2(ev->data1, ev->data2);
            }
            checkPixelRatioChange_Window_(as_Window(d));
            postRefresh_App();
            return iTrue;
        case SDL_WINDOWEVENT_RESTORED:
        case SDL_WINDOWEVENT_SHOWN:
            updateSize_MainWindow_(d, iTrue);
            invalidate_MainWindow_(d, iTrue);
            d->base.isMinimized = iFalse;
            postRefresh_App();
            return iTrue;
        case SDL_WINDOWEVENT_MAXIMIZED:
            return iTrue;
        case SDL_WINDOWEVENT_MINIMIZED:
            d->base.isMinimized = iTrue;
            closePopups_App(iTrue);
            return iTrue;
#else /* if defined (!iPlatformDesktop) */
        case SDL_WINDOWEVENT_RESIZED:
            /* On mobile, this occurs when the display is rotated. */
            invalidate_Window(d);
            postRefresh_App();
            return iTrue;
#endif
        case SDL_WINDOWEVENT_LEAVE:
            unhover_Widget();
            d->base.isMouseInside = iFalse;
            postCommand_App("window.mouse.exited");
            return iTrue;
        case SDL_WINDOWEVENT_ENTER:
            d->base.isMouseInside = iTrue;
            //SDL_SetWindowInputFocus(d->base.win); /* BUG? */
            postCommand_App("window.mouse.entered");
            setHoverUnderCursor_Window_(as_Window(d));
            return iTrue;
        case SDL_WINDOWEVENT_FOCUS_GAINED:
            d->base.focusGainedAt = SDL_GetTicks();
            setCapsLockDown_Keys(iFalse);
            postCommand_App("window.focus.gained");
            d->base.isExposed = iTrue;
            setActiveWindow_App(d);
            setHoverUnderCursor_Window_(as_Window(d));
#if !defined (iPlatformDesktop)
            /* Returned to foreground, may have lost buffered content. */
            invalidate_MainWindow_(d, iTrue);
            postCommand_App("window.unfreeze");
#endif
            return iFalse;
        case SDL_WINDOWEVENT_FOCUS_LOST:
            postCommand_App("window.focus.lost");
#if !defined (iPlatformDesktop)
            setFreezeDraw_MainWindow(d, iTrue);
#endif
            closePopups_App(iTrue);
            return iFalse;
        case SDL_WINDOWEVENT_TAKE_FOCUS:
            SDL_SetWindowInputFocus(d->base.win);
            postRefresh_App();
            return iTrue;
        case SDL_WINDOWEVENT_CLOSE:
#if defined (iPlatformAppleDesktop)
            closeWindow_App(as_Window(d));
#else
            if (numWindows_App() == 1) {
                postCommand_App("quit");
            }
            else {
                closeWindow_App(as_Window(d));   
            }
#endif            
            return iTrue;
        default:
            break;
    }
    return iFalse;
}

static void applyCursor_Window_(iWindow *d) {
    if (d->pendingCursor) {
        SDL_SetCursor(d->pendingCursor);
        d->pendingCursor = NULL;
    }
}

void updateHover_Window(iWindow *d) {
    d->hover = hitChild_Window(d, mouseCoord_Window(d, 0));
}

iBool processEvent_Window(iWindow *d, const SDL_Event *ev) {
    iMainWindow *mw     = (type_Window(d) == main_WindowType ? as_MainWindow(d) : NULL);
    iWindow *    extraw = (type_Window(d) == extra_WindowType ? d : NULL);
    switch (ev->type) {
#if defined (LAGRANGE_ENABLE_CUSTOM_FRAME)
        case SDL_SYSWMEVENT: {
            /* We observe native Win32 messages for better user interaction with the
               window frame. Mouse clicks especially will not generate normal SDL
               events if they happen on the custom hit-tested regions. These events
               are processed only there; the UI widgets do not get involved. */
            processNativeEvent_Win32(ev->syswm.msg, d);
            break;
        }
#endif
        case SDL_WINDOWEVENT: {
            if (mw) {
                return handleWindowEvent_MainWindow_(mw, &ev->window);
            }
            else {
                return handleWindowEvent_Window_(d, &ev->window);
            }
        }
        case SDL_RENDER_TARGETS_RESET:
        case SDL_RENDER_DEVICE_RESET: {            
            if (mw || extraw) {
                invalidate_Window_(d, iTrue /* force full reset */);
            }
            break;
        }
        default: {
            SDL_Event event = *ev;
            if (isCommand_UserEvent(ev, "window.unfreeze") && mw) {
                if (SDL_GetWindowFlags(d->win) & SDL_WINDOW_HIDDEN) {
                    mw->isDrawFrozen = iTrue; /* don't trigger a redraw now */
                    SDL_ShowWindow(d->win);
                }
                mw->isDrawFrozen = iFalse;
                draw_MainWindow(mw); /* don't show a frame of placeholder content */
                postCommand_App("media.player.update"); /* in case a player needs updating */
                return iFalse; /* unfreeze all frozen windows */
            }
#if 0
            if (event.type == SDL_USEREVENT && isCommand_UserEvent(ev, "window.sysframe") && mw) {
                /* This command is sent on Android to update the keyboard height. */
                const char *cmd = command_UserEvent(ev);
                /*
                    0
                    |
                 top
                 |  |
                 | bottom (top of keyboard)   :
                 |  |                         : keyboardHeight
                 maxDrawableHeight            :
                    |
                   fullheight
                 */
                const int top    = argLabel_Command(cmd, "top");
                const int bottom = argLabel_Command(cmd, "bottom");
                const int full   = argLabel_Command(cmd, "fullheight");
                //if (!SDL_IsScreenKeyboardShown(mw->base.win)) {
                if (bottom == full) {
                    mw->maxDrawableHeight = bottom - top;
                }
                setKeyboardHeight_MainWindow(mw, top + mw->maxDrawableHeight - bottom);
                return iTrue;
            }
#endif
            if (processEvent_Touch(&event)) {
                return iTrue;
            }
            if (event.type == SDL_KEYDOWN && SDL_GetTicks() - d->focusGainedAt < 100) {
                /* Suspiciously close to when input focus was received. For example under openbox,
                   closing xterm with Ctrl+D will cause the keydown event to "spill" over to us.
                   As a workaround, ignore these events. */
                return iTrue; /* won't go to bindings, either */
            }
            if (event.type == SDL_MOUSEBUTTONDOWN && d->ignoreClick) {
                d->ignoreClick = iFalse;
                return iTrue;
            }
            /* Map mouse pointer coordinate to our coordinate system. */
            if (event.type == SDL_MOUSEMOTION) {
                setCursor_Window(d, SDL_SYSTEM_CURSOR_ARROW); /* default cursor */
                const iInt2 pos = coord_Window(d, event.motion.x, event.motion.y);
                event.motion.x = pos.x;
                event.motion.y = pos.y;
            }
            else if (event.type == SDL_MOUSEBUTTONUP || event.type == SDL_MOUSEBUTTONDOWN) {
                const iInt2 pos = coord_Window(d, event.button.x, event.button.y);
                event.button.x = pos.x;
                event.button.y = pos.y;
                if (event.type == SDL_MOUSEBUTTONDOWN) {
                    /* Button clicks will change keyroot. */
                    if (numRoots_Window(d) > 1) {
                        const iInt2 click = init_I2(event.button.x, event.button.y);
                        iForIndices(i, d->roots) {
                            iRoot *root = d->roots[i];
                            if (root != d->keyRoot && contains_Rect(rect_Root(root), click)) {
                                setKeyRoot_Window(d, root);
                                break;
                            }
                        }
                    }
                }
            }
//            const iWidget *oldHover = d->hover;
            iBool wasUsed = iFalse;
            /* Dispatch first to the mouse-grabbed widget. */
//            iWidget *widget = d->root.widget;
            if (event.type == SDL_MOUSEMOTION || event.type == SDL_MOUSEWHEEL ||
                event.type == SDL_MOUSEBUTTONUP || event.type == SDL_MOUSEBUTTONDOWN) {
                if (mouseGrab_Widget()) {
                    iWidget *grabbed = mouseGrab_Widget();
                    setCurrent_Root(grabbed->root /* findRoot_Window(d, grabbed)*/);
                    wasUsed = dispatchEvent_Widget(grabbed, &event);
                }
            }
            /* Dispatch the event to the tree of widgets. */
            if (!wasUsed) {
                wasUsed = dispatchEvent_Window(d, &event);
            }
            if (!wasUsed) {
                if (event.type == SDL_MOUSEBUTTONDOWN) {
                    closePopups_App(iFalse);
                }
                /* As a special case, clicking the middle mouse button can be used for pasting
                   from the clipboard. */
                if (event.type == SDL_MOUSEBUTTONDOWN && event.button.button == SDL_BUTTON_MIDDLE) {
                    SDL_Event paste;
                    iZap(paste);
                    paste.type           = SDL_KEYDOWN;
                    paste.key.keysym.sym = SDLK_v;
                    paste.key.keysym.mod = KMOD_PRIMARY;
                    paste.key.state      = SDL_PRESSED;
                    paste.key.timestamp  = SDL_GetTicks();
                    wasUsed = dispatchEvent_Window(d, &paste);
                }
                if (event.type == SDL_MOUSEBUTTONDOWN && event.button.button == SDL_BUTTON_RIGHT) {
                    if (postContextClick_Window(d, &event.button)) {
                        wasUsed = iTrue;
                    }
                }
            }
            if (isMetricsChange_UserEvent(&event)) {
                iForIndices(i, d->roots) {
                    updateMetrics_Root(d->roots[i]);
                }
            }
            if (isCommand_UserEvent(&event, "lang.changed") && (mw || extraw)) {
#if defined (LAGRANGE_MAC_MENUBAR)
                /* Retranslate the menus. */
                /* TODO: Instead of removing, just update the labels. */
                removeMacMenus_();
                insertMacMenus_();
#endif
                invalidate_Window(d);
                iForIndices(i, d->roots) {
                    if (d->roots[i]) {
                        updatePreferencesLayout_Widget(findChild_Widget(d->roots[i]->widget, "prefs"));
                        arrange_Widget(d->roots[i]->widget);
                    }
                }
            }
            if (event.type == SDL_MOUSEMOTION) {
                applyCursor_Window_(d);
            }
            return wasUsed;
        }
    }
    return iFalse;
}

iBool setKeyRoot_Window(iWindow *d, iRoot *root) {
    if (d->keyRoot != root) {
        d->keyRoot = root;
        postCommand_App("keyroot.changed");
        postRefresh_App();
        return iTrue;
    }
    return iFalse;
}

iLocalDef iBool isEscapeKeypress_(const SDL_Event *ev) {
    return (ev->type == SDL_KEYDOWN || ev->type == SDL_KEYUP) && ev->key.keysym.sym == SDLK_ESCAPE;
}

static uint32_t windowId_SDLEvent_(const SDL_Event *ev) {
    switch (ev->type) {
        case SDL_MOUSEBUTTONDOWN:
        case SDL_MOUSEBUTTONUP:
            return ev->button.windowID;
        case SDL_MOUSEMOTION:
            return ev->motion.windowID;
        case SDL_MOUSEWHEEL:
            return ev->wheel.windowID;
        case SDL_KEYDOWN:
        case SDL_KEYUP:
            return ev->key.windowID;
        case SDL_TEXTINPUT:
            return ev->text.windowID;
        case SDL_USEREVENT:
            return ev->user.windowID;
        default:
            return 0;
    }
}

iBool dispatchEvent_Window(iWindow *d, const SDL_Event *ev) {
    /* For the right window? */
    const uint32_t evWin = windowId_SDLEvent_(ev);
    if (evWin && evWin != id_Window(d)) {
        return iFalse; /* Meant for a different window. */
    }
    const iWidget *oldHover = d->hover;
    if (ev->type == SDL_MOUSEMOTION) {
        /* Hover widget may change. */
        setHover_Widget(NULL);
    }
    iBool wasUsed = iFalse;
    iRoot *order[2];
    rootOrder_Window(d, order);
    iForIndices(i, order) {
        iRoot *root = order[i];
        if (root) {
            if (isCommand_SDLEvent(ev) && ev->user.data2 && ev->user.data2 != root) {
                continue; /* Not meant for this root. */
            }
            if ((ev->type == SDL_KEYDOWN || ev->type == SDL_KEYUP || ev->type == SDL_TEXTINPUT)
                     && d->keyRoot != root) {
                if (!isEscapeKeypress_(ev)) {
                    /* Key events go only to the root with keyboard focus, with the exception
                       of Escape that will also affect the entire window. */
                    continue;
                }
            }
            if (ev->type == SDL_MOUSEWHEEL && !contains_Rect(rect_Root(root),
                                                             coord_MouseWheelEvent(&ev->wheel))) {
                continue; /* Only process the event in the relevant split. */
            }
            if (!root->widget) {
                continue;
            }
            setCurrent_Root(root);
            wasUsed = dispatchEvent_Widget(root->widget, ev);
            if (wasUsed) {
                if (ev->type == SDL_MOUSEBUTTONDOWN ||
                    ev->type == SDL_MOUSEWHEEL) {
                    setKeyRoot_Window(d, root);
                }
                break;
            }
        }
    }
    if (d->hover != oldHover) {
        refresh_Widget(d->hover); /* Note: oldHover may have been deleted */
        if (d->hover && d->hover->flags2 & commandOnHover_WidgetFlag2) {
            SDL_UserEvent notif = { .type      = SDL_USEREVENT,
                                    .timestamp = SDL_GetTicks(),
                                    .code      = command_UserEventCode,
                                    .data1     = (void *) format_CStr("mouse.hovered ptr:%p arg:1",
                                                                  d->hover) };
            dispatchEvent_Widget(d->hover, (SDL_Event *) &notif);
        }
    }
    return wasUsed;
}

iAnyObject *hitChild_Window(const iWindow *d, iInt2 coord) {
    if (coord.x < 0 || coord.y < 0) {
        return NULL;
    }
    iForIndices(i, d->roots) {
        if (d->roots[i]) {
            iAnyObject *hit = hitChild_Widget(d->roots[i]->widget, coord);
            if (hit) {
                return hit;
            }
        }
    }
    return NULL;
}

iBool postContextClick_Window(iWindow *d, const SDL_MouseButtonEvent *ev) {
    /* A context menu may still get triggered here. */
    const iWidget *hit = hitChild_Window(d, init_I2(ev->x, ev->y));
    while (hit && isEmpty_String(id_Widget(hit))) {
        hit = parent_Widget(hit);
    }
    if (hit) {
        postCommandf_App("contextclick id:%s ptr:%p coord:%d %d",
                         cstr_String(id_Widget(hit)), hit,
                         ev->x, ev->y);
        return iTrue;
    }
    return iFalse;
}

void draw_Window(iWindow *d) {
    if (isDrawing_ || SDL_GetWindowFlags(d->win) & SDL_WINDOW_HIDDEN) {
        return;
    }
    isDrawing_ = iTrue;
    iPaint p;
    init_Paint(&p);
    iRoot *root = d->roots[0];
    setCurrent_Root(root);
    unsetClip_Paint(&p); /* update clip to full window */
    const iColor back = get_Color(uiBackground_ColorId);
    SDL_SetRenderDrawColor(d->render, back.r, back.g, back.b, 255);
    SDL_RenderClear(d->render);
    d->frameTime = SDL_GetTicks();
    if (isExposed_Window(d)) {
        d->isInvalidated = iFalse;
        extern int drawCount_;
        drawRoot_Widget(root->widget);
#if !defined (NDEBUG)
        draw_Text(uiLabelBold_FontId, safeRect_Root(root).pos, red_ColorId, "%d", drawCount_);
        drawCount_ = 0;
#endif
    }
    if (type_Window(d) == popup_WindowType) {
        drawRectThickness_Paint(&p, (iRect){ zero_I2(), sub_I2(d->size, one_I2()) }, gap_UI / 4,
                                root->widget->frameColor);
    }
    setCurrent_Root(NULL);
    SDL_RenderPresent(d->render);
    isDrawing_ = iFalse;
}

void drawQuick_MainWindow(iMainWindow *d) {
    /* Just present what was drawn previously. */
    if (d->backBuf) {
        SDL_Renderer *render = d->base.render;
        SDL_RenderCopy(render, d->backBuf, NULL, NULL);
        SDL_RenderPresent(render);
    }
}

void draw_MainWindow(iMainWindow *d) {
    if (isDrawing_) {
        /* Already drawing! */
        return;
    }
    /* TODO: Try to make this a specialization of `draw_Window`? */
    iWindow *w = as_Window(d);
    if (d->isDrawFrozen) {
        return;
    }
    isDrawing_ = iTrue;
    if (deviceType_App() == desktop_AppDeviceType) {
        checkPixelRatioChange_Window_(&d->base);
    }
    setCurrent_Text(d->base.text);
    /* Check if root needs resizing. */ {
        const iBool wasPortrait = isPortrait_App();
//        iInt2 renderSize;
//        SDL_GetRendererOutputSize(w->render, &renderSize.x, &renderSize.y);
        if (updateSize_MainWindow_(d, iTrue)) {
            //processEvents_App(postedEventsOnly_AppEventMode);
            if (isPortrait_App() != wasPortrait) {
                d->maxDrawableHeight = w->size.y; // renderSize.y;
            }
        }
        /* TODO: On macOS, a detached popup window will mess up the main window's rendering
           completely. Looks like a render target mixup. macOS builds normally use native menus,
           though, so leaving it in. */
        if (d->enableBackBuf) { 
            /* Possible resize the backing buffer. */
            if (!d->backBuf || !isEqual_I2(size_SDLTexture(d->backBuf), w->size)) {
                if (d->backBuf) {
                    SDL_DestroyTexture(d->backBuf);
                }
                d->backBuf = SDL_CreateTexture(d->base.render,
                                               SDL_PIXELFORMAT_RGB888,
                                               SDL_TEXTUREACCESS_TARGET,
                                               w->size.x,
                                               w->size.y);
//                printf("NEW BACKING: %dx%d %p\n", renderSize.x, renderSize.y, d->backBuf); fflush(stdout);
            }
        }
    }
    setCurrent_Window(d);
    const int   winFlags = SDL_GetWindowFlags(d->base.win);
    const iBool gotFocus = (winFlags & SDL_WINDOW_INPUT_FOCUS) != 0;
    iPaint p;
    init_Paint(&p);
    if (d->backBuf) {
        SDL_SetRenderTarget(d->base.render, d->backBuf);
    }
    /* Clear the window. The clear color is visible as a border around the window
       when the custom frame is being used. */ {
        setCurrent_Root(w->roots[0]);
        iColor back;
        if (isMobile_Platform()) {
            back = get_Color(uiBackground_ColorId);
            if (deviceType_App() == phone_AppDeviceType) {
                /* Page background extends to safe area, so fill it completely. */
                back = get_Color(tmBackground_ColorId);
            }
        }
        else {
#if defined (LAGRANGE_ENABLE_CUSTOM_FRAME)
            back = get_Color(gotFocus && d->place.snap != maximized_WindowSnap &&
                                     ~winFlags & SDL_WINDOW_FULLSCREEN_DESKTOP
                                 ? uiAnnotation_ColorId
                                 : uiSeparator_ColorId);
#else
            back = get_Color(uiBackground_ColorId);
#endif
        }
        unsetClip_Paint(&p); /* update clip to full window */
        SDL_SetRenderDrawColor(w->render, back.r, back.g, back.b, 255);
        SDL_RenderClear(w->render);
    }
    /* Draw widgets. */
    w->frameTime = SDL_GetTicks();
    iForIndices(i, d->base.roots) {
        iRoot *root = d->base.roots[i];
        if (root) {
            /* Some widgets may need a just-in-time visual update. */
            if (root->didChangeArrangement && root->window->type == extra_WindowType) {
                iWindow *x = root->window;
                SDL_SetWindowSize(x->win, x->size.x / x->pixelRatio, x->size.y / x->pixelRatio);
            }
            notifyVisualOffsetChange_Root(root);
            if (root->didChangeArrangement) {
                iNotifyAudience(root, arrangementChanged, RootArrangementChanged)
            }
            root->didChangeArrangement = iFalse;
        }
    }
    if (isExposed_Window(w)) {
        w->isInvalidated = iFalse;
        extern int drawCount_;
        iForIndices(i, w->roots) {
            iRoot *root = w->roots[i];
            if (root) {
                setCurrent_Root(root);
                unsetClip_Paint(&p); /* update clip to current root */
                drawRoot_Widget(root->widget);
#if defined (LAGRANGE_ENABLE_CUSTOM_FRAME)
                /* App icon. */
                const iWidget *appIcon = findChild_Widget(root->widget, "winbar.icon");
                if (isVisible_Widget(appIcon)) {
                    const int   size    = appIconSize_Root();
                    const iRect rect    = bounds_Widget(appIcon);
                    const iInt2 mid     = mid_Rect(rect);
                    const iBool isLight = isLight_ColorTheme(colorTheme_App());
                    iColor iconColor    = get_Color(gotFocus || isLight ? white_ColorId : uiAnnotation_ColorId);
                    SDL_SetTextureColorMod(d->appIcon, iconColor.r, iconColor.g, iconColor.b);
                    SDL_SetTextureAlphaMod(d->appIcon, gotFocus || !isLight ? 255 : 92);
                    SDL_RenderCopy(
                        w->render,
                        d->appIcon,
                        NULL,
                        &(SDL_Rect){ left_Rect(rect) + gap_UI * 1.25f, mid.y - size / 2, size, size });
                }
#endif
                /* Root separator and keyboard focus indicator. */
                if (numRoots_Window(w) > 1){
                    const iRect bounds = bounds_Widget(root->widget);
                    if (i == 1) {
                        fillRect_Paint(&p, (iRect){
                            addX_I2(topLeft_Rect(bounds), -gap_UI / 8),
                            init_I2(gap_UI / 4, height_Rect(bounds))
                        }, uiSeparator_ColorId);
                    }
                    if (root == w->keyRoot) {
                        const iBool isDark = isDark_ColorTheme(colorTheme_App());
                        fillRect_Paint(&p, (iRect){
                            topLeft_Rect(bounds),
                            init_I2(width_Rect(bounds), gap_UI / 2)
                        }, isDark ? uiBackgroundSelected_ColorId
                                  : uiIcon_ColorId);
                    }
                }
            }
        }
        setCurrent_Root(NULL);
#if !defined (NDEBUG)
        draw_Text(uiLabelBold_FontId,
                  safeRect_Root(w->roots[0]).pos,
                  d->base.frameCount & 1 ? red_ColorId : white_ColorId,
                  "%d",
                  drawCount_);
        drawCount_ = 0;
#endif
    }
    if (d->backBuf) {
        SDL_SetRenderTarget(d->base.render, NULL);
        SDL_RenderCopy(d->base.render, d->backBuf, NULL, NULL);
    }
#if 0
    /* Text cache debugging. */ {
        SDL_Rect rect = { d->roots[0]->widget->rect.size.x - 640, 0, 640, 2.5 * 640 };
        SDL_SetRenderDrawColor(d->render, 0, 0, 0, 255);
        SDL_RenderFillRect(d->render, &rect);
        SDL_RenderCopy(d->render, glyphCache_Text(), NULL, &rect);
    }
#endif
    SDL_RenderPresent(w->render);
    isDrawing_ = iFalse;
}

void resize_MainWindow(iMainWindow *d, int w, int h) {
    if (w > 0 && h > 0) {
        SDL_SetWindowSize(d->base.win, w, h);
        updateSize_MainWindow_(d, iFalse);
    }
    else {
        updateSize_MainWindow_(d, iTrue); /* notify always */
    }
}

void setTitle_Window(iWindow *d, const iString *title) {
    SDL_SetWindowTitle(d->win, cstr_String(title));
    iLabelWidget *bar = findChild_Widget(get_Root()->widget, "winbar.title");
    if (bar) {
        updateText_LabelWidget(bar, title);
    }
}

void setUiScale_Window(iWindow *d, float uiScale) {
    if (uiScale <= 0.0f) {
        uiScale = 1.0f;
    }
    uiScale = iClamp(uiScale, 0.5f, 4.0f);
    if (d) {
        if (iAbs(d->uiScale - uiScale) > 0.0001f) {
            d->uiScale = uiScale;
            notifyMetricsChange_Window_(d);
        }
    }
    initialUiScale_ = uiScale; /* used for new windows */
}

void setFreezeDraw_MainWindow(iMainWindow *d, iBool freezeDraw) {
    d->isDrawFrozen = freezeDraw;
}

void setCursor_Window(iWindow *d, int cursor) {
    if (!d->cursors[cursor]) {
        d->cursors[cursor] = SDL_CreateSystemCursor(cursor);
    }
    d->pendingCursor = d->cursors[cursor];
}

uint32_t id_Window(const iWindow *d) {
    return d && d->win ? SDL_GetWindowID(d->win) : 0;
}

iInt2 size_Window(const iWindow *d) {
    return d ? d->size : zero_I2();
}

iInt2 coord_Window(const iWindow *d, int x, int y) {
    return mulf_I2(init_I2(x, y), d->pixelRatio);
}

iInt2 mouseCoord_Window(const iWindow *d, int whichDevice) {
    if (whichDevice == SDL_TOUCH_MOUSEID) {
        /* At least on iOS the coordinates returned by SDL_GetMouseState() do no match up with
           our touch coordinates on the Y axis (?). Maybe a pixel ratio thing? */
        iUnused(d);
        return latestPosition_Touch();
    }
    if (!d->isMouseInside) {
        return init_I2(-1000000, -1000000);
    }
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
    /* TODO: This should be thread-specific. */
    return theWindow_;
}

void setCurrent_Window(iAnyWindow *d) {
    theWindow_ = d;
    if (type_Window(d) == main_WindowType) {
        theMainWindow_ = d;
    }
    if (d) {
        setCurrent_Text(theWindow_->text);
        setCurrent_Root(theWindow_->keyRoot);
        updateMetrics_Window_(d);
    }
    else {
        setCurrent_Text(NULL);
        setCurrent_Root(NULL);
    }
}

iMainWindow *get_MainWindow(void) {
    return theMainWindow_;
}

iBool isOpenGLRenderer_Window(void) {
    return isOpenGLRenderer_;
}

void setKeyboardHeight_MainWindow(iMainWindow *d, int height) {
    height = iMax(0, height);
    if (d->keyboardHeight != height) {
        d->keyboardHeight = height;
        postCommandf_App("keyboard.changed arg:%d", height);
        postRefresh_App();
    }
}

iBool isAnyDocumentRequestOngoing_MainWindow(iMainWindow *d) {
    iForIndices(i, d->base.roots) {
        iRoot *root = d->base.roots[i];
        if (!root) continue;
        const iWidget *tabs = findChild_Widget(root->widget, "doctabs");
        iForEach(ObjectList, i, children_Widget(findChild_Widget(tabs, "tabs.pages"))) {
            if (isInstance_Object(i.object, &Class_DocumentWidget)) {
                if (isRequestOngoing_DocumentWidget(i.object)) {
                    return iTrue;
                }
            }
        }
    }
    return iFalse;
}

iObjectList *listDocuments_MainWindow(iMainWindow *d, const iRoot *rootOrNull) {
    iObjectList *docs = new_ObjectList();
    if (!d) {
        return docs;
    }
    iForIndices(i, d->base.roots) {
        iRoot *root = d->base.roots[i];
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

void checkPendingSplit_MainWindow(iMainWindow *d) {
    if (d && d->splitMode != d->pendingSplitMode) {
        setSplitMode_MainWindow(d, d->pendingSplitMode);
    }
}

void swapRoots_MainWindow(iMainWindow *d) {
    iWindow *w = as_Window(d);
    if (numRoots_Window(w) == 2) {
        iSwap(iRoot *, w->roots[0], w->roots[1]);
        windowSizeChanged_MainWindow_(d); /* re-do layout */
        postRefresh_App();
    }
}

void setSplitMode_MainWindow(iMainWindow *d, int splitFlags) {
    const int splitMode = splitFlags & mode_WindowSplit;
    if (deviceType_App() == phone_AppDeviceType) {
        /* There isn't enough room on the phone. */
        /* TODO: Maybe in landscape only? */
        return;
    }
    iWindow *w = as_Window(d);
    iAssert(current_Root() == NULL);
    setCurrent_Window(w);
    if (d->splitMode != splitMode) {
        int oldCount = numRoots_Window(w);
        setFreezeDraw_MainWindow(d, iTrue);
        if (oldCount == 2 && splitMode == 0) {
            /* Keep references to the tabs of the second root. */
            const iDocumentWidget *curPage = document_Root(w->keyRoot);
            if (!curPage) {
                /* All tabs closed on that side. */
                curPage = document_Root(otherRoot_Window(w, w->keyRoot));
            }
            iObjectList *tabs = listDocuments_App(w->roots[1]);
            iForEach(ObjectList, i, tabs) {
                setRoot_Widget(i.object, w->roots[0]);
            }
            /* We will delete the old tabs immediately, but we're also holding references
               to the pages in `tabs`, so they'll be kept until added to the remaining split. */
            iWidget *deletedDocTabs = findChild_Widget(w->roots[1]->widget, "doctabs");
            iRelease(removeChild_Widget(parent_Widget(deletedDocTabs), deletedDocTabs));
            setFocus_Widget(NULL);
            iRoot *rootBeingDeleted = w->roots[1];
            w->roots[1] = NULL;
            w->keyRoot = w->roots[0];
            /* Move the deleted root's tabs to the first root. */
            setCurrent_Root(w->roots[0]);
            iWidget *docTabs = findWidget_Root("doctabs");
            iForEach(ObjectList, j, tabs) {
                iAssert(parent_Widget(j.object) == NULL); /* old doctabs was already deleted */
                appendTabPage_Widget(docTabs, j.object, "", 0, 0);
                addTabCloseButton_Widget(docTabs, j.object, "tabs.close");
            }
            /* The last child is the [+] button for adding a tab. */
            moveTabButtonToEnd_Widget(findChild_Widget(docTabs, "newtab"));
            setFlags_Widget(findWidget_Root("navbar.unsplit"), hidden_WidgetFlag, iTrue);
            postCommandf_App("tabs.switch id:%s", cstr_String(id_Widget(constAs_Widget(curPage))));
            iRelease(tabs);
            delete_Root(rootBeingDeleted);
        }
        else if (oldCount == 1 && splitMode) {
            /* Add a second root. */
            iDocumentWidget *moved = document_Root(w->roots[0]);
            iAssert(w->roots[1] == NULL);
            const iBool addToLeft = (prefs_App()->pinSplit == 2);
            size_t newRootIndex = 1;
            if (addToLeft) {
                iSwap(iRoot *, w->roots[0], w->roots[1]);
                newRootIndex = 0;
            }
            w->roots[newRootIndex] = new_Root();
            w->keyRoot             = w->roots[newRootIndex];
            w->keyRoot->window     = w;
            setCurrent_Root(w->roots[newRootIndex]);
            createUserInterface_Root(w->roots[newRootIndex]);
            /* Bookmark folder state will match the old root's state. */ {
                for (int sb = 0; sb < 2; sb++) {
                    const char *sbId = (sb == 0 ? "sidebar" : "sidebar2");
                    setClosedFolders_SidebarWidget(
                        findChild_Widget(w->roots[newRootIndex]->widget, sbId),
                        closedFolders_SidebarWidget(
                            findChild_Widget(w->roots[newRootIndex ^ 1]->widget, sbId)));
                }
            }
            if (!isEmpty_String(d->pendingSplitUrl)) {
                postCommandf_Root(
                    w->roots[newRootIndex],
                    "open origin:%s%s url:%s",
                    cstr_String(d->pendingSplitOrigin),
                    isEmpty_String(d->pendingSplitSetIdent)
                        ? ""
                        : format_CStr(" setident:%s", cstr_String(d->pendingSplitSetIdent)),
                    cstr_String(d->pendingSplitUrl));
                clear_String(d->pendingSplitUrl);
                clear_String(d->pendingSplitOrigin);
                clear_String(d->pendingSplitSetIdent);
            }
            else if (~splitFlags & noEvents_WindowSplit) {
                iWidget *docTabs0 = findChild_Widget(w->roots[newRootIndex ^ 1]->widget, "doctabs");
                iWidget *docTabs1 = findChild_Widget(w->roots[newRootIndex]->widget, "doctabs");
                /* If the old root has multiple tabs, move the current one to the new split. */
                if (tabCount_Widget(docTabs0) >= 2) {
                    size_t movedIndex = tabPageIndex_Widget(docTabs0, moved);
                    removeTabPage_Widget(docTabs0, movedIndex);
                    showTabPage_Widget(docTabs0, tabPage_Widget(docTabs0, iMax((int) movedIndex - 1, 0)));
                    iRelease(removeTabPage_Widget(docTabs1, 0)); /* delete the default tab */
                    setRoot_Widget(as_Widget(moved), w->roots[newRootIndex]);
                    prependTabPage_Widget(docTabs1, iClob(moved), "", 0, 0);
                    postCommandf_App("tabs.switch page:%p", moved);
                }
                else {
                    postCommand_Root(w->roots[newRootIndex], "navigate.home");
                }
            }
            /* Show unsplit buttons. */
            for (int i = 0; i < 2; i++) {
                setFlags_Widget(findChild_Widget(w->roots[i]->widget, "navbar.unsplit"),
                                hidden_WidgetFlag, iFalse);
            }
            setCurrent_Root(NULL);
        }
        /* Add some room for the active root indicator. */
        for (int i = 0; i < 2; i++) {
            if (w->roots[i]) {
                w->roots[i]->widget->padding[1] = (splitMode ? 1 : 0);
            }
        }        
        d->splitMode = splitMode;
        windowSizeChanged_MainWindow_(d);
        postCommand_App("window.resized"); /* not really, but widgets may need to change layout */
#if defined (LAGRANGE_ENABLE_CUSTOM_FRAME)
        /* Update custom frame controls. */ {
            const iBool hideCtl0 = numRoots_Window(as_Window(d)) != 1;
            iWidget *winBar = findChild_Widget(d->base.roots[0]->widget, "winbar");
            if (winBar) {
                setFlags_Widget(
                    findChild_Widget(winBar, "winbar.min"), hidden_WidgetFlag, hideCtl0);
                setFlags_Widget(
                    findChild_Widget(winBar, "winbar.max"), hidden_WidgetFlag, hideCtl0);
                setFlags_Widget(
                    findChild_Widget(winBar, "winbar.close"), hidden_WidgetFlag, hideCtl0);
                if (d->base.roots[1]) {
                    winBar = findChild_Widget(d->base.roots[1]->widget, "winbar");
                    setFlags_Widget(
                        findChild_Widget(winBar, "winbar.icon"), hidden_WidgetFlag, iTrue);
                    setFlags_Widget(
                        findChild_Widget(winBar, "winbar.app"), hidden_WidgetFlag, iTrue);
                }
            }
        }
#endif
        if (~splitFlags & noEvents_WindowSplit) {
            updateSize_MainWindow_(d, iTrue);
            postCommand_App("window.unfreeze");
        }
    }
}

void setSnap_MainWindow(iMainWindow *d, int snapMode) {
    if (!prefs_App()->customFrame) {
        if (snapMode == maximized_WindowSnap) {
            SDL_MaximizeWindow(d->base.win);
        }
        else if (snapMode == fullscreen_WindowSnap) {
            SDL_SetWindowFullscreen(d->base.win, SDL_WINDOW_FULLSCREEN_DESKTOP);
        }
        else {
            if (snap_MainWindow(d) == fullscreen_WindowSnap) {
                SDL_SetWindowFullscreen(d->base.win, 0);
            }
            else {
                SDL_RestoreWindow(d->base.win);
            }
        }
        return;
    }
#if defined (LAGRANGE_ENABLE_CUSTOM_FRAME)
    if (d->place.snap == snapMode) {
        return;
    }
    const int snapDist = gap_UI * 4;
    iRect newRect = zero_Rect();
    SDL_Rect usable;
    SDL_GetDisplayUsableBounds(SDL_GetWindowDisplayIndex(d->base.win), &usable);
    if (d->place.snap == fullscreen_WindowSnap) {
        SDL_SetWindowFullscreen(d->base.win, 0);
    }
    d->place.snap = snapMode & ~redo_WindowSnap;
    switch (snapMode & mask_WindowSnap) {
        case none_WindowSnap:
            newRect = intersect_Rect(d->place.normalRect,
                                     init_Rect(usable.x, usable.y, usable.w, usable.h));
            break;
        case left_WindowSnap:
            newRect = init_Rect(usable.x, usable.y, usable.w / 2, usable.h);
            break;
        case right_WindowSnap:
            newRect =
                init_Rect(usable.x + usable.w / 2, usable.y, usable.w - usable.w / 2, usable.h);
            break;
        case maximized_WindowSnap:
            newRect = init_Rect(usable.x, usable.y, usable.w, usable.h);
            break;
        case yMaximized_WindowSnap:
            newRect.pos.y = 0;
            newRect.size.y = usable.h;
            SDL_GetWindowSize(d->base.win, &newRect.size.x, NULL);
            SDL_GetWindowPosition(d->base.win, &newRect.pos.x, NULL);
            /* Snap the window to left/right edges, if close by. */
            if (iAbs(right_Rect(newRect) - (usable.x + usable.w)) < snapDist) {
                newRect.pos.x = usable.x + usable.w - width_Rect(newRect);
            }
            if (iAbs(newRect.pos.x - usable.x) < snapDist) {
                newRect.pos.x = usable.x;
            }
            break;
        case fullscreen_WindowSnap:
            SDL_SetWindowFullscreen(d->base.win, SDL_WINDOW_FULLSCREEN_DESKTOP);
            break;
    }
    if (snapMode & (topBit_WindowSnap | bottomBit_WindowSnap)) {
        newRect.size.y /= 2;
    }
    if (snapMode & bottomBit_WindowSnap) {
        newRect.pos.y += newRect.size.y;
    }
    if (newRect.size.x) {
        // printf("snap:%d newrect:%d,%d %dx%d\n", snapMode, newRect.pos.x, newRect.pos.y, newRect.size.x, newRect.size.y);
        SDL_SetWindowPosition(d->base.win, newRect.pos.x, newRect.pos.y);
        SDL_SetWindowSize(d->base.win, newRect.size.x, newRect.size.y);
        postCommand_App("window.resized");
    }
    /* Update window controls. */
    iForIndices(rootIndex, d->base.roots) {
        iRoot *root = d->base.roots[rootIndex];
        if (!root) continue;
        iWidget *winBar = findChild_Widget(root->widget, "winbar");
        updateTextCStr_LabelWidget(findChild_Widget(winBar, "winbar.max"),
                                   d->place.snap == maximized_WindowSnap ? "\u25a2" : "\u25a1");
        /* Show and hide the title bar. */
        const iBool wasVisible = isVisible_Widget(winBar);
        setFlags_Widget(winBar, hidden_WidgetFlag, d->place.snap == fullscreen_WindowSnap);
        if (wasVisible != isVisible_Widget(winBar)) {
            arrange_Widget(root->widget);
            postRefresh_App();
        }
    }
#endif /* defined (LAGRANGE_ENABLE_CUSTOM_FRAME) */
}

int snap_MainWindow(const iMainWindow *d) {
    if (!prefs_App()->customFrame) {
        const int flags = SDL_GetWindowFlags(d->base.win);
        if (flags & SDL_WINDOW_FULLSCREEN_DESKTOP) {
            return fullscreen_WindowSnap;
        }
        else if (flags & SDL_WINDOW_MAXIMIZED) {
            return maximized_WindowSnap;
        }
        return none_WindowSnap;
    }
    return d->place.snap;
}

/*----------------------------------------------------------------------------------------------*/

iWindow *newPopup_Window(iInt2 screenPos, iWidget *rootWidget) {
    start_PerfTimer(newPopup_Window);
    const iBool oldSw = forceSoftwareRender_App();
#if !defined (iPlatformApple)
    /* On macOS, SDL seems to want to not use HiDPI with software rendering. */
    setForceSoftwareRender_App(iTrue);
#endif
    SDL_Rect usableRect;
    SDL_GetDisplayUsableBounds(SDL_GetWindowDisplayIndex(get_MainWindow()->base.win),
                               &usableRect);
    const float pixelRatio = get_Window()->pixelRatio;
    iRect winRect = (iRect){ screenPos,
                             min_I2(divf_I2(rootWidget->rect.size, pixelRatio),
                                    init_I2(usableRect.w, usableRect.h)) };
    iWindow *win = new_Window(popup_WindowType,
                              winRect,
                              SDL_WINDOW_ALWAYS_ON_TOP |
#if !defined (iPlatformAppleDesktop)
                                  SDL_WINDOW_BORDERLESS |
#endif
                                  SDL_WINDOW_POPUP_MENU | SDL_WINDOW_SKIP_TASKBAR);
#if defined (iPlatformAppleDesktop)
    hideTitleBar_MacOS(win); /* make it a borderless window, but retain shadow */
#endif
    /* At least on macOS, with an external display on the left (negative coordinates), the
       window will not be correct placed during creation. Ensure it ends up on the right display. */
    SDL_SetWindowPosition(win->win, winRect.pos.x, winRect.pos.y);
    SDL_SetWindowSize(win->win, winRect.size.x, winRect.size.y);
    win->pixelRatio      = pixelRatio;
    iRoot *root          = new_Root();
    win->roots[0]        = root;
    win->keyRoot         = root;
    root->widget         = rootWidget;
    root->window         = win;
    rootWidget->rect.pos = zero_I2();
    setRoot_Widget(rootWidget, root);
    setDrawBufferEnabled_Widget(rootWidget, iFalse);
    setForceSoftwareRender_App(oldSw);
#if !defined(NDEBUG)
    stop_PerfTimer(newPopup_Window);
#endif
    return win;
}

iWindow *newExtra_Window(iWidget *rootWidget) {
    iWindow *   oldWin     = get_Window();
    const float pixelRatio = get_Window()->pixelRatio;
    iRect       winRect    = (iRect){ init1_I2(-1), divf_I2(rootWidget->rect.size, pixelRatio) };
    iWindow    *win        = new_Window(extra_WindowType, winRect, 0);
    win->pixelRatio        = pixelRatio;
    iRoot *root            = new_Root();
    win->roots[0]          = root;
    win->keyRoot           = root;
    /* Make a simple root widget that sizes itself according to the actual root. */
    setWindowIcon_Window_(win);
    setCurrent_Window(win);
    iWidget *frameRoot = new_Widget();
    setFlags_Widget(frameRoot, arrangeSize_WidgetFlag | focusRoot_WidgetFlag, iTrue);
    setCommandHandler_Widget(frameRoot, handleRootCommands_Widget);
    setRoot_Widget(rootWidget, root);
    addChild_Widget(frameRoot, rootWidget);
    iRelease(rootWidget);
    arrange_Widget(frameRoot);
    root->widget         = frameRoot;
    root->window         = win;
    rootWidget->rect.pos = zero_I2();
    setDrawBufferEnabled_Widget(frameRoot, iFalse);
    setDrawBufferEnabled_Widget(rootWidget, iFalse);
    setCurrent_Window(oldWin);
    return win;     
}
