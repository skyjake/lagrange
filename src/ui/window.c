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
#include "embedded.h"
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

#if defined (iPlatformApple) || defined (iPlatformLinux) || defined (iPlatformOther)
static float initialUiScale_ = 1.0f;
#else
static float initialUiScale_ = 1.1f;
#endif

static iBool isOpenGLRenderer_;

iDefineTypeConstructionArgs(Window,
                            (enum iWindowType type, iRect rect, uint32_t flags),
                            type, rect, flags)
iDefineTypeConstructionArgs(MainWindow, (iRect rect), rect)

/* TODO: Define menus per platform. */

#if defined (iHaveNativeMenus)
/* Using native menus. */
static const iMenuItem fileMenuItems_[] = {
    { "${menu.newtab}", SDLK_t, KMOD_PRIMARY, "tabs.new" },
    { "${menu.openlocation}", SDLK_l, KMOD_PRIMARY, "navigate.focus" },
    { "---", 0, 0, NULL },
    { saveToDownloads_Label, SDLK_s, KMOD_PRIMARY, "document.save" },
    { "---", 0, 0, NULL },
    { "${menu.downloads}", 0, 0, "downloads.open" },
};

static const iMenuItem editMenuItems_[] = {
    { "${menu.cut}", SDLK_x, KMOD_PRIMARY, "input.copy cut:1" },
    { "${menu.copy}", SDLK_c, KMOD_PRIMARY, "copy" },
    { "${menu.paste}", SDLK_v, KMOD_PRIMARY, "input.paste" },
    { "---", 0, 0, NULL },
    { "${menu.copy.pagelink}", SDLK_c, KMOD_PRIMARY | KMOD_SHIFT, "document.copylink" },
    { "---", 0, 0, NULL },
    { "${macos.menu.find}", SDLK_f, KMOD_PRIMARY, "focus.set id:find.input" },
};

static const iMenuItem viewMenuItems_[] = {
    { "${menu.show.bookmarks}", '1', KMOD_PRIMARY, "sidebar.mode arg:0 toggle:1" },
    { "${menu.show.feeds}", '2', KMOD_PRIMARY, "sidebar.mode arg:1 toggle:1" },
    { "${menu.show.history}", '3', KMOD_PRIMARY, "sidebar.mode arg:2 toggle:1" },
    { "${menu.show.identities}", '4', KMOD_PRIMARY, "sidebar.mode arg:3 toggle:1" },
    { "${menu.show.outline}", '5', KMOD_PRIMARY, "sidebar.mode arg:4 toggle:1" },
    { "${menu.sidebar.left}", SDLK_l, KMOD_PRIMARY | KMOD_SHIFT, "sidebar.toggle" },
    { "${menu.sidebar.right}", SDLK_p, KMOD_PRIMARY | KMOD_SHIFT, "sidebar2.toggle" },
    { "---", 0, 0, NULL },
    { "${menu.back}", SDLK_LEFTBRACKET, KMOD_PRIMARY, "navigate.back" },
    { "${menu.forward}", SDLK_RIGHTBRACKET, KMOD_PRIMARY, "navigate.forward" },
    { "${menu.parent}", navigateParent_KeyShortcut, "navigate.parent" },
    { "${menu.root}", navigateRoot_KeyShortcut, "navigate.root" },
    { "${menu.reload}", reload_KeyShortcut, "navigate.reload" },
    { "---", 0, 0, NULL },
    { "${menu.zoom.in}", SDLK_EQUALS, KMOD_PRIMARY, "zoom.delta arg:10" },
    { "${menu.zoom.out}", SDLK_MINUS, KMOD_PRIMARY, "zoom.delta arg:-10" },
    { "${menu.zoom.reset}", SDLK_0, KMOD_PRIMARY, "zoom.set arg:100" },
    { "${menu.view.split}", SDLK_j, KMOD_PRIMARY, "splitmenu.open" },
};

static iMenuItem bookmarksMenuItems_[] = {
    { "${menu.page.bookmark}", SDLK_d, KMOD_PRIMARY, "bookmark.add" },
    { "${menu.page.subscribe}", subscribeToPage_KeyModifier, "feeds.subscribe" },
    { "${menu.newfolder}", 0, 0, "bookmarks.addfolder" },
    { "---", 0, 0, NULL },
    { "${menu.import.links}", 0, 0, "bookmark.links confirm:1" },
    { "---", 0, 0, NULL },
    { "${macos.menu.bookmarks.list}", 0, 0, "open url:about:bookmarks" },
    { "${macos.menu.bookmarks.bytag}", 0, 0, "open url:about:bookmarks?tags" },
    { "${macos.menu.bookmarks.bytime}", 0, 0, "open url:about:bookmarks?created" },
    { "${menu.feeds.entrylist}", 0, 0, "open url:about:feeds" },
    { "---", 0, 0, NULL },
    { "${menu.sort.alpha}", 0, 0, "bookmarks.sort" },
    { "---", 0, 0, NULL },
    { "${menu.bookmarks.refresh}", 0, 0, "bookmarks.reload.remote" },
    { "${menu.feeds.refresh}", SDLK_r, KMOD_PRIMARY | KMOD_SHIFT, "feeds.refresh" },
};

static const iMenuItem identityMenuItems_[] = {
    { "${menu.identity.new}", SDLK_n, KMOD_PRIMARY | KMOD_SHIFT, "ident.new" },
    { "---", 0, 0, NULL },
    { "${menu.identity.import}", SDLK_i, KMOD_PRIMARY | KMOD_SHIFT, "ident.import" },
};

static const iMenuItem helpMenuItems_[] = {
    { "${menu.help}", 0, 0, "!open url:about:help" },
    { "${menu.releasenotes}", 0, 0, "!open url:about:version" },
    { "---", 0, 0, NULL },
    { "${menu.aboutpages}", 0, 0, "!open url:about:about" },
    { "${menu.debug}", 0, 0, "!open url:about:debug" },
};

static void insertMacMenus_(void) {
    insertMenuItems_MacOS("${menu.title.file}", 1, fileMenuItems_, iElemCount(fileMenuItems_));
    insertMenuItems_MacOS("${menu.title.edit}", 2, editMenuItems_, iElemCount(editMenuItems_));
    insertMenuItems_MacOS("${menu.title.view}", 3, viewMenuItems_, iElemCount(viewMenuItems_));
    insertMenuItems_MacOS("${menu.title.bookmarks}", 4, bookmarksMenuItems_, iElemCount(bookmarksMenuItems_));
    insertMenuItems_MacOS("${menu.title.identity}", 5, identityMenuItems_, iElemCount(identityMenuItems_));
    insertMenuItems_MacOS("${menu.title.help}", 7, helpMenuItems_, iElemCount(helpMenuItems_));
}

static void removeMacMenus_(void) {
    removeMenu_MacOS(7);
    removeMenu_MacOS(5);
    removeMenu_MacOS(4);
    removeMenu_MacOS(3);
    removeMenu_MacOS(2);
    removeMenu_MacOS(1);
}
#endif

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
    const int weights[2] = {
        d->base.roots[0] ? (d->splitMode & twoToOne_WindowSplit ? 2 : 1) : 0,
        d->base.roots[1] ? (d->splitMode & oneToTwo_WindowSplit ? 2 : 1) : 0,
    };
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
            updatePadding_Root(root);
            arrange_Widget(root->widget);
        }
    }
}

static void setupUserInterface_MainWindow(iMainWindow *d) {
#if defined (iHaveNativeMenus)
    insertMacMenus_();
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

static void updateSize_MainWindow_(iMainWindow *d, iBool notifyAlways) {
    iInt2 *size = &d->base.size;
    const iInt2 oldSize = *size;
    SDL_GetRendererOutputSize(d->base.render, &size->x, &size->y);
    size->y -= d->keyboardHeight;
    if (notifyAlways || !isEqual_I2(oldSize, *size)) {
        windowSizeChanged_MainWindow_(d);
        if (!isEqual_I2(*size, d->place.lastNotifiedSize)) {
            const iBool isHoriz = (d->place.lastNotifiedSize.x != size->x);
            const iBool isVert  = (d->place.lastNotifiedSize.y != size->y);
            postCommandf_App("window.resized width:%d height:%d horiz:%d vert:%d",
                             size->x,
                             size->y,
                             isHoriz,
                             isVert);
            postCommand_App("widget.overflow"); /* check bounds with updated sizes */
        }
        postRefresh_App();
        d->place.lastNotifiedSize = *size;
    }
}

void drawWhileResizing_MainWindow(iMainWindow *d, int w, int h) {
    draw_MainWindow(d);
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

static float displayScale_Window_(const iWindow *d) {
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
#if defined (iPlatformApple)
    iUnused(d);
    /* Apple UI sizes are fixed and only scaled by pixel ratio. */
    /* TODO: iOS text size setting? */
    return 1.0f;
#elif defined (iPlatformMsys)
    iUnused(d);
    return desktopDPI_Win32();
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
//    const iColor bg = get_Color(uiBackground_ColorId);
    const iColor bg = { 128, 128, 128, 255 }; /* TODO: Have no root yet. */
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
    /* TODO: Check if inside the caption label widget. */
    const iBool isLeft   = pos->x < gap_UI;
    const iBool isRight  = pos->x >= w - gap_UI;
    const iBool isTop    = pos->y < gap_UI && snap != yMaximized_WindowSnap;
    const iBool isBottom = pos->y >= h - gap_UI && snap != yMaximized_WindowSnap;
    const int   captionHeight = lineHeight_Text(uiContent_FontId) + gap_UI * 2;
    const int   rightEdge     = left_Rect(bounds_Widget(findChild_Widget(
                                    rootAt_Window_(as_Window(d), init_I2(pos->x, pos->y))->widget,
                                    "winbar.min")));
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

iBool create_Window_(iWindow *d, iRect rect, uint32_t flags) {
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
#if 0
    if (SDL_CreateWindowAndRenderer(
            width_Rect(rect), height_Rect(rect), flags, &d->win, &d->render)) {
        return iFalse;
    }
#endif
    const iBool setPos = left_Rect(rect) >= 0 || top_Rect(rect) >= 0;
    d->win = SDL_CreateWindow("",
                              setPos ? left_Rect(rect) : SDL_WINDOWPOS_CENTERED,
                              setPos ? top_Rect(rect) : SDL_WINDOWPOS_CENTERED,
                              width_Rect(rect),
                              height_Rect(rect),
                              flags);
    if (!d->win) {
        fprintf(stderr, "[window] failed to create window: %s\n", SDL_GetError());
        exit(-3);
    }
    d->render = SDL_CreateRenderer(
        d->win,
        -1,
        (forceSoftwareRender_App() ? SDL_RENDERER_SOFTWARE : SDL_RENDERER_ACCELERATED) |
            SDL_RENDERER_PRESENTVSYNC | SDL_RENDERER_TARGETTEXTURE);
    if (!d->render) {
        fprintf(stderr, "[window] failed to create renderer: %s\n", SDL_GetError());
        exit(-4);
    }
#if defined(LAGRANGE_ENABLE_CUSTOM_FRAME)
    if (type_Window(d) == main_WindowType && prefs_App()->customFrame) {
        /* Register a handler for window hit testing (drag, resize). */
        SDL_SetWindowHitTest(d->win, hitTest_MainWindow_, d);
        SDL_SetWindowResizable(d->win, SDL_TRUE);
    }
#endif
    return iTrue;
}

static SDL_Surface *loadImage_(const iBlock *data, int resized) {
    int      w = 0, h = 0, num = 4;
    stbi_uc *pixels = stbi_load_from_memory(
        constData_Block(data), size_Block(data), &w, &h, &num, STBI_rgb_alpha);
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

void init_Window(iWindow *d, enum iWindowType type, iRect rect, uint32_t flags) {
    d->type          = type;
    d->win           = NULL;
    d->size          = zero_I2(); /* will be updated below */
    d->hover         = NULL;
    d->lastHover     = NULL;
    d->mouseGrab     = NULL;
    d->focus         = NULL;
    d->pendingCursor = NULL;
    d->isExposed     = iFalse;
    d->isMinimized   = iFalse;
    d->isInvalidated = iFalse; /* set when posting event, to avoid repeated events */
    d->isMouseInside = iTrue;
    d->ignoreClick   = iFalse;
    d->focusGainedAt = 0;
    d->presentTime   = 0.0;
    d->frameTime     = SDL_GetTicks();
    d->keyRoot       = NULL;
    d->borderShadow  = NULL;
    iZap(d->roots);
    iZap(d->cursors);
    /* First try SDL's default renderer that should be the best option. */
    if (forceSoftwareRender_App() || !create_Window_(d, rect, flags)) {
        /* No luck, maybe software only? This should always work as long as there is a display. */
        SDL_SetHint(SDL_HINT_RENDER_DRIVER, "software");
        flags &= ~SDL_WINDOW_OPENGL;
        start_PerfTimer(create_Window_);
        if (!create_Window_(d, rect, flags)) {
            fprintf(stderr, "Error when creating window: %s\n", SDL_GetError());
            exit(-2);
        }
        stop_PerfTimer(create_Window_);
    }
//    start_PerfTimer(setPos);
//    if (left_Rect(rect) >= 0 || top_Rect(rect) >= 0) {
//        SDL_SetWindowPosition(d->win, left_Rect(rect), top_Rect(rect));
//    }
//    stop_PerfTimer(setPos);
    SDL_GetRendererOutputSize(d->render, &d->size.x, &d->size.y);
    /* Renderer info. */ {
        SDL_RendererInfo info;
        SDL_GetRendererInfo(d->render, &info);
        printf("[window] renderer: %s%s\n",
               info.name,
               info.flags & SDL_RENDERER_ACCELERATED ? " (accelerated)" : "");        
    }
    drawBlank_Window_(d);
    d->pixelRatio   = pixelRatio_Window_(d); /* point/pixel conversion */
    d->displayScale = displayScale_Window_(d);
    d->uiScale      = initialUiScale_;
    /* TODO: Ratios, scales, and metrics must be window-specific, not global. */
    setScale_Metrics(d->pixelRatio * d->displayScale * d->uiScale);
    d->text = new_Text(d->render);
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
    if (d->type == popup_WindowType) {
        removePopup_App(d);
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
}

void init_MainWindow(iMainWindow *d, iRect rect) {
    theWindow_ = &d->base;
    theMainWindow_ = d;
    uint32_t flags = 0;
#if defined (iPlatformAppleDesktop)
    SDL_SetHint(SDL_HINT_RENDER_DRIVER, shouldDefaultToMetalRenderer_MacOS() ? "metal" : "opengl");
    if (shouldDefaultToMetalRenderer_MacOS()) {
        flags |= SDL_WINDOW_METAL;
    }
#elif defined (iPlatformAppleMobile)
    SDL_SetHint(SDL_HINT_RENDER_DRIVER, "metal");
    flags |= SDL_WINDOW_METAL;
#else
    if (!forceSoftwareRender_App()) {
        flags |= SDL_WINDOW_OPENGL;
    }
#endif
    SDL_SetHint(SDL_HINT_RENDER_VSYNC, "1");
    init_Window(&d->base, main_WindowType, rect, flags);
    d->isDrawFrozen           = iTrue;
    d->splitMode              = 0;
    d->pendingSplitMode       = 0;
    d->pendingSplitUrl        = new_String();
    d->place.initialPos       = rect.pos;
    d->place.normalRect       = rect;
    d->place.lastNotifiedSize = zero_I2();
    d->place.snap             = 0;
    d->keyboardHeight         = 0;
#if defined(iPlatformMobile)
    const iInt2 minSize = zero_I2(); /* windows aren't independently resizable */
#else
    const iInt2 minSize = init_I2(425, 325);
#endif
    SDL_SetWindowMinimumSize(d->base.win, minSize.x, minSize.y);
    SDL_SetWindowTitle(d->base.win, "Lagrange");
    /* Some info. */ {
        SDL_RendererInfo info;
        SDL_GetRendererInfo(d->base.render, &info);
        isOpenGLRenderer_ = !iCmpStr(info.name, "opengl");
#if !defined(NDEBUG)
        printf("[window] max texture size: %d x %d\n",
               info.max_texture_width,
               info.max_texture_height);
        for (size_t i = 0; i < info.num_texture_formats; ++i) {
            printf("[window] supported texture format: %s\n",
                   SDL_GetPixelFormatName(info.texture_formats[i]));
        }
#endif
    }
#if defined(iPlatformMsys)
    SDL_SetWindowMinimumSize(d->base.win, minSize.x * d->base.displayScale, minSize.y * d->base.displayScale);
    useExecutableIconResource_SDLWindow(d->base.win);
#endif
#if defined (iPlatformLinux)
    SDL_SetWindowMinimumSize(d->base.win, minSize.x * d->base.pixelRatio, minSize.y * d->base.pixelRatio);
    /* Load the window icon. */ {
        SDL_Surface *surf = loadImage_(&imageLagrange64_Embedded, 0);
        SDL_SetWindowIcon(d->base.win, surf);
        free(surf->pixels);
        SDL_FreeSurface(surf);
    }
#endif
#if defined (iPlatformAppleMobile)
    setupWindow_iOS(as_Window(d));
#endif
    setCurrent_Text(d->base.text);
    SDL_GetRendererOutputSize(d->base.render, &d->base.size.x, &d->base.size.y);
    setupUserInterface_MainWindow(d);
    postCommand_App("~bindings.changed"); /* update from bindings */
    /* Load the border shadow texture. */ {
        SDL_Surface *surf = loadImage_(&imageShadow_Embedded, 0);
        d->base.borderShadow = SDL_CreateTextureFromSurface(d->base.render, surf);
        SDL_SetTextureBlendMode(d->base.borderShadow, SDL_BLENDMODE_BLEND);
        free(surf->pixels);
        SDL_FreeSurface(surf);
    }
    d->appIcon = NULL;
#if defined (LAGRANGE_ENABLE_CUSTOM_FRAME)
    /* Load the app icon for drawing in the title bar. */
    if (prefs_App()->customFrame) {
        SDL_Surface *surf = loadImage_(&imageLagrange64_Embedded, appIconSize_Root());
        SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");
        d->appIcon = SDL_CreateTextureFromSurface(d->base.render, surf);
        free(surf->pixels);
        SDL_FreeSurface(surf);
        /* We need to observe non-client-area events. */
        SDL_EventState(SDL_SYSWMEVENT, SDL_TRUE);
    }
#endif
    SDL_HideWindow(d->base.win);
}

void deinit_MainWindow(iMainWindow *d) {
    deinitRoots_Window_(as_Window(d));
    if (theWindow_ == as_Window(d)) {
        theWindow_ = NULL;
    }
    if (theMainWindow_ == d) {
        theMainWindow_ = NULL;
    }
    delete_String(d->pendingSplitUrl);
    deinit_Window(&d->base);
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

static void invalidate_MainWindow_(iMainWindow *d, iBool forced) {
    if (d && (!d->base.isInvalidated || forced)) {
        d->base.isInvalidated = iTrue;
        resetFonts_Text(text_Window(d));
        postCommand_App("theme.changed auto:1"); /* forces UI invalidation */
    }
}

void invalidate_Window(iAnyWindow *d) {
    if (type_Window(d) == main_WindowType) {
        invalidate_MainWindow_(as_MainWindow(d), iFalse);
    }
    else {
        iAssert(type_Window(d) == main_WindowType);
    }
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
    setScale_Metrics(d->pixelRatio * d->displayScale * d->uiScale);
    resetFonts_Text(d->text);
    postCommand_App("metrics.changed");
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
        case SDL_WINDOWEVENT_EXPOSED:
            d->isExposed = iTrue;
            postRefresh_App();
            return iTrue;
        case SDL_WINDOWEVENT_RESTORED:
        case SDL_WINDOWEVENT_SHOWN:
            postRefresh_App();
            return iTrue;
        case SDL_WINDOWEVENT_FOCUS_LOST:
            /* Popup windows are currently only used for menus. */
            closeMenu_Widget(d->roots[0]->widget);
            return iTrue;
        case SDL_WINDOWEVENT_LEAVE:
            unhover_Widget();
            d->isMouseInside = iFalse;
            postRefresh_App();
            return iTrue;
        case SDL_WINDOWEVENT_ENTER:
            d->isMouseInside = iTrue;
            return iTrue;
    }
    return iFalse;
}

static iBool handleWindowEvent_MainWindow_(iMainWindow *d, const SDL_WindowEvent *ev) {
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
                int bx, by;
                SDL_GetWindowBordersSize(d->base.win, &by, &bx, NULL, NULL);
                SDL_SetWindowPosition(
                    d->base.win, d->place.initialPos.x + bx, d->place.initialPos.y + by);
                d->place.initialPos = init1_I2(-1);
            }
#endif
            return iFalse;
        case SDL_WINDOWEVENT_MOVED: {
            if (d->base.isMinimized) {
                return iFalse;
            }
            closePopups_App();
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
            if (isNormalPlacement_MainWindow_(d)) {
                d->place.normalRect.pos = newPos;
                // printf("normal rect set (move)\n"); fflush(stdout);
                iInt2 border = zero_I2();
#if !defined(iPlatformApple)
                SDL_GetWindowBordersSize(d->base.win, &border.y, &border.x, NULL, NULL);
#endif
                d->place.normalRect.pos =
                    max_I2(zero_I2(), sub_I2(d->place.normalRect.pos, border));
            }
            return iTrue;
        }
        case SDL_WINDOWEVENT_RESIZED:
            if (d->base.isMinimized) {
                // updateSize_Window_(d, iTrue);
                return iTrue;
            }
            closePopups_App();
            if (unsnap_MainWindow_(d, NULL)) {
                return iTrue;
            }
            if (isNormalPlacement_MainWindow_(d)) {
                d->place.normalRect.size = init_I2(ev->data1, ev->data2);
                // printf("normal rect set (resize)\n"); fflush(stdout);
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
        case SDL_WINDOWEVENT_MINIMIZED:
            d->base.isMinimized = iTrue;
            closePopups_App();
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
            SDL_SetWindowInputFocus(d->base.win);
            postCommand_App("window.mouse.entered");
            return iTrue;
        case SDL_WINDOWEVENT_FOCUS_GAINED:
            d->base.focusGainedAt = SDL_GetTicks();
            setCapsLockDown_Keys(iFalse);
            postCommand_App("window.focus.gained");
            d->base.isExposed = iTrue;
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
            closePopups_App();
            return iFalse;
        case SDL_WINDOWEVENT_TAKE_FOCUS:
            SDL_SetWindowInputFocus(d->base.win);
            postRefresh_App();
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

iBool processEvent_Window(iWindow *d, const SDL_Event *ev) {
    iMainWindow *mw = (type_Window(d) == main_WindowType ? as_MainWindow(d) : NULL);
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
            if (mw) {
                invalidate_MainWindow_(mw, iTrue /* force full reset */);
            }
            break;
        }
        default: {
            SDL_Event event = *ev;
            if (event.type == SDL_USEREVENT && isCommand_UserEvent(ev, "window.unfreeze") && mw) {
                mw->isDrawFrozen = iFalse;
                if (SDL_GetWindowFlags(d->win) & SDL_WINDOW_HIDDEN) {
                    SDL_ShowWindow(d->win);
                }
                draw_MainWindow(mw); /* don't show a frame of placeholder content */
                postCommand_App("media.player.update"); /* in case a player needs updating */
                return iTrue;
            }
            if (processEvent_Touch(&event)) {
                return iTrue;
            }
            if (event.type == SDL_KEYDOWN && SDL_GetTicks() - d->focusGainedAt < 10) {
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
                    setCurrent_Root(findRoot_Window(d, grabbed));
                    wasUsed = dispatchEvent_Widget(grabbed, &event);
                }
            }
            /* Dispatch the event to the tree of widgets. */
            if (!wasUsed) {
                wasUsed = dispatchEvent_Window(d, &event);
            }
            if (!wasUsed) {
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
            if (isCommand_UserEvent(&event, "lang.changed") && mw) {
#if defined (iHaveNativeMenus)
                /* Retranslate the menus. */
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

iBool dispatchEvent_Window(iWindow *d, const SDL_Event *ev) {
    if (ev->type == SDL_MOUSEMOTION) {
        /* Hover widget may change. */
        setHover_Widget(NULL);
    }
    iRoot *order[2];
    rootOrder_App(order);
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
            const iBool wasUsed = dispatchEvent_Widget(root->widget, ev);
            if (wasUsed) {
                if (ev->type == SDL_MOUSEBUTTONDOWN ||
                    ev->type == SDL_MOUSEWHEEL) {
                    setKeyRoot_Window(d, root);
                }
                return iTrue;
            }
        }
    }
    return iFalse;
}

iAnyObject *hitChild_Window(const iWindow *d, iInt2 coord) {
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
    if (SDL_GetWindowFlags(d->win) & SDL_WINDOW_HIDDEN) {
        return;
    }
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
        draw_Text(defaultBold_FontId, safeRect_Root(root).pos, red_ColorId, "%d", drawCount_);
        drawCount_ = 0;
#endif
    }
    drawRectThickness_Paint(
        &p, (iRect){ zero_I2(), sub_I2(d->size, one_I2()) }, gap_UI / 4, uiSeparator_ColorId);
    setCurrent_Root(NULL);
    SDL_RenderPresent(d->render);
}

void draw_MainWindow(iMainWindow *d) {
    /* TODO: Try to make this a specialization of `draw_Window`? */
    iWindow *w = as_Window(d);
    if (d->isDrawFrozen) {
        return;
    }
    setCurrent_Text(d->base.text);
    /* Check if root needs resizing. */ {
        iInt2 renderSize;
        SDL_GetRendererOutputSize(w->render, &renderSize.x, &renderSize.y);
        if (!isEqual_I2(renderSize, w->size)) {
            updateSize_MainWindow_(d, iTrue);
            processEvents_App(postedEventsOnly_AppEventMode);
        }
    }
    const int   winFlags = SDL_GetWindowFlags(d->base.win);
    const iBool gotFocus = (winFlags & SDL_WINDOW_INPUT_FOCUS) != 0;
    iPaint p;
    init_Paint(&p);
    /* Clear the window. The clear color is visible as a border around the window
       when the custom frame is being used. */ {
        setCurrent_Root(w->roots[0]);
#if defined (iPlatformMobile)
        iColor back = get_Color(uiBackground_ColorId);
        if (deviceType_App() == phone_AppDeviceType) {
            /* Page background extends to safe area, so fill it completely. */
            back = get_Color(tmBackground_ColorId);
        }
#else
        const iColor back = get_Color(gotFocus && d->place.snap != maximized_WindowSnap &&
                                              ~winFlags & SDL_WINDOW_FULLSCREEN_DESKTOP
                                          ? uiAnnotation_ColorId
                                          : uiSeparator_ColorId);
#endif
        unsetClip_Paint(&p); /* update clip to full window */
        SDL_SetRenderDrawColor(w->render, back.r, back.g, back.b, 255);
        SDL_RenderClear(w->render);
    }
    /* Draw widgets. */
    w->frameTime = SDL_GetTicks();
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
        draw_Text(defaultBold_FontId, safeRect_Root(w->roots[0]).pos, red_ColorId, "%d", drawCount_);
        drawCount_ = 0;
#endif
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

void setTitle_MainWindow(iMainWindow *d, const iString *title) {
    SDL_SetWindowTitle(d->base.win, cstr_String(title));
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
    else {
        initialUiScale_ = uiScale;
    }
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
    if (d->keyboardHeight != height) {
        d->keyboardHeight = height;
        postCommandf_App("keyboard.changed arg:%d", height);
        postRefresh_App();
    }
}

void checkPendingSplit_MainWindow(iMainWindow *d) {
    if (d->splitMode != d->pendingSplitMode) {
        setSplitMode_MainWindow(d, d->pendingSplitMode);
    }
}

void swapRoots_MainWindow(iMainWindow *d) {
    iWindow *w = as_Window(d);
    if (numRoots_Window(w) == 2) {
        iSwap(iRoot *, w->roots[0], w->roots[1]);
        updateSize_MainWindow_(d, iTrue);
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
            setFocus_Widget(NULL);
            delete_Root(w->roots[1]);
            w->roots[1] = NULL;
            w->keyRoot = w->roots[0];
            /* Move the deleted root's tabs to the first root. */
            setCurrent_Root(w->roots[0]);
            iWidget *docTabs = findWidget_Root("doctabs");
            iForEach(ObjectList, j, tabs) {
                appendTabPage_Widget(docTabs, j.object, "", 0, 0);
            }
            /* The last child is the [+] button for adding a tab. */
            moveTabButtonToEnd_Widget(findChild_Widget(docTabs, "newtab"));
            iRelease(tabs);
            postCommandf_App("tabs.switch id:%s", cstr_String(id_Widget(constAs_Widget(curPage))));
        }
        else if (splitMode && oldCount == 1) {
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
                postCommandf_Root(w->roots[newRootIndex], "open url:%s",
                                  cstr_String(d->pendingSplitUrl));
                clear_String(d->pendingSplitUrl);
            }
            else if (~splitFlags & noEvents_WindowSplit) {
                iWidget *docTabs0 = findChild_Widget(w->roots[newRootIndex ^ 1]->widget, "doctabs");
                iWidget *docTabs1 = findChild_Widget(w->roots[newRootIndex]->widget, "doctabs");
                /* If the old root has multiple tabs, move the current one to the new split. */
                if (tabCount_Widget(docTabs0) >= 2) {
                    int movedIndex = tabPageIndex_Widget(docTabs0, moved);
                    removeTabPage_Widget(docTabs0, movedIndex);
                    showTabPage_Widget(docTabs0, tabPage_Widget(docTabs0, iMax(movedIndex - 1, 0)));
                    iRelease(removeTabPage_Widget(docTabs1, 0)); /* delete the default tab */
                    setRoot_Widget(as_Widget(moved), w->roots[newRootIndex]);
                    prependTabPage_Widget(docTabs1, iClob(moved), "", 0, 0);
                    postCommandf_App("tabs.switch page:%p", moved);
                }
                else {
                    postCommand_Root(w->roots[newRootIndex], "navigate.home");
                }
            }
            setCurrent_Root(NULL);
        }
        d->splitMode = splitMode;
        postCommand_App("window.resized");
#if defined (LAGRANGE_ENABLE_CUSTOM_FRAME)
        /* Update custom frame controls. */{
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
        if (newRect.size.x) {
            SDL_SetWindowPosition(d->base.win, newRect.pos.x, newRect.pos.y);
            SDL_SetWindowSize(d->base.win, newRect.size.x, newRect.size.y);
            postCommand_App("window.resized");
        }
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
    iPerfTimer pt; init_PerfTimer(&pt);
    const iBool oldSw = forceSoftwareRender_App();
    setForceSoftwareRender_App(iTrue);
    iWindow *win =
        new_Window(popup_WindowType,
                   (iRect){ screenPos, divf_I2(rootWidget->rect.size, get_Window()->pixelRatio) },
                   SDL_WINDOW_ALWAYS_ON_TOP |
#if !defined (iPlatformAppleDesktop)
                   SDL_WINDOW_BORDERLESS |
#endif
                   SDL_WINDOW_POPUP_MENU |
                   SDL_WINDOW_SKIP_TASKBAR);
#if defined (iPlatformAppleDesktop)
    hideTitleBar_MacOS(win); /* make it a borderless window, but retain shadow */
#endif
    iRoot *root   = new_Root();
    win->roots[0] = root;
    win->keyRoot  = root;
    root->widget  = rootWidget;
    root->window  = win;
    setRoot_Widget(rootWidget, root);
    setForceSoftwareRender_App(oldSw);
    stop_PerfTimer(newPopup_Window);
    return win;
}
