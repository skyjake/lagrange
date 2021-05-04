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

static iWindow *theWindow_ = NULL;

#if defined (iPlatformApple) || defined (iPlatformLinux)
static float initialUiScale_ = 1.0f;
#else
static float initialUiScale_ = 1.1f;
#endif

iDefineTypeConstructionArgs(Window, (iRect rect), rect)

/* TODO: Define menus per platform. */

#if defined (iPlatformAppleDesktop)
#  define iHaveNativeMenus
#endif

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
    { "---", 0, 0, NULL },
    { "${menu.import.links}", 0, 0, "bookmark.links confirm:1" },
    { "---", 0, 0, NULL },
    { "${macos.menu.bookmarks.list}", 0, 0, "open url:about:bookmarks" },
    { "${macos.menu.bookmarks.bytag}", 0, 0, "open url:about:bookmarks?tags" },
    { "${macos.menu.bookmarks.bytime}", 0, 0, "open url:about:bookmarks?created" },
    { "${menu.feeds.entrylist}", 0, 0, "open url:about:feeds" },
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

static void setupUserInterface_Window(iWindow *d) {
#if defined (iPlatformAppleDesktop)
    insertMacMenus_();
#endif
    /* One root is created by default. */
    d->roots[0] = new_Root();
    setCurrent_Root(d->roots[0]);
    createUserInterface_Root(d->roots[0]);
    setCurrent_Root(NULL);
    /* One of the roots always has keyboard input focus. */
    d->keyRoot = d->roots[0];
}

static void windowSizeChanged_Window_(iWindow *d) {
    const int numRoots = numRoots_Window(d);
    const iInt2 rootSize = d->size;
    const int weights[2] = {
        d->roots[0] ? (d->splitMode & twoToOne_WindowSplit ? 2 : 1) : 0,
        d->roots[1] ? (d->splitMode & oneToTwo_WindowSplit ? 2 : 1) : 0,
    };
    const int totalWeight = weights[0] + weights[1];
    int w = 0;
    iForIndices(i, d->roots) {
        iRoot *root = d->roots[i];
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

static void updateSize_Window_(iWindow *d, iBool notifyAlways) {
    iInt2 *size = &d->size;
    const iInt2 oldSize = *size;
    SDL_GetRendererOutputSize(d->render, &size->x, &size->y);
    size->y -= d->keyboardHeight;
    if (notifyAlways || !isEqual_I2(oldSize, *size)) {
        windowSizeChanged_Window_(d);
        const iBool isHoriz = (d->place.lastNotifiedSize.x != size->x);
        const iBool isVert  = (d->place.lastNotifiedSize.y != size->y);
        postCommandf_App("window.resized width:%d height:%d horiz:%d vert:%d",
                         size->x,
                         size->y,
                         isHoriz,
                         isVert);
        postCommand_App("widget.overflow"); /* check bounds with updated sizes */
        postRefresh_App();
        d->place.lastNotifiedSize = *size;
    }
}

void drawWhileResizing_Window(iWindow *d, int w, int h) {
    /* This is called while a window resize is in progress, so we can be pretty confident
       the size has actually changed. */
    d->size = coord_Window(d, w, h);
    windowSizeChanged_Window_(d);
    draw_Window(d);
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
        fprintf(stderr, "[Window] WARNING: failed to parse LAGRANGE_OVERRIDE_DPI='%s', "
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
    float vdpi = 0.0f;
    SDL_GetDisplayDPI(SDL_GetWindowDisplayIndex(d->win), NULL, NULL, &vdpi);
//    printf("DPI: %f\n", vdpi);
    const float factor = vdpi / baseDPI_Window / pixelRatio_Window_(d);
    return iMax(1.0f, factor);
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
static SDL_HitTestResult hitTest_Window_(SDL_Window *win, const SDL_Point *pos, void *data) {
    iWindow *d = data;
    iAssert(d->win == win);
    if (SDL_GetWindowFlags(d->win) & (SDL_WINDOW_MOUSE_CAPTURE | SDL_WINDOW_FULLSCREEN_DESKTOP)) {
        return SDL_HITTEST_NORMAL;
    }
    const int snap = snap_Window(d);
    int w, h;
    SDL_GetWindowSize(win, &w, &h);
    /* TODO: Check if inside the caption label widget. */
    const iBool isLeft   = pos->x < gap_UI;
    const iBool isRight  = pos->x >= w - gap_UI;
    const iBool isTop    = pos->y < gap_UI && snap != yMaximized_WindowSnap;
    const iBool isBottom = pos->y >= h - gap_UI && snap != yMaximized_WindowSnap;
    const int   captionHeight = lineHeight_Text(uiContent_FontId) + gap_UI * 2;
    const int   rightEdge     = left_Rect(bounds_Widget(findChild_Widget(
                                    rootAt_Window_(d, init_I2(pos->x, pos->y))->widget,
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

SDL_HitTestResult hitTest_Window(const iWindow *d, iInt2 pos) {
    return hitTest_Window_(d->win, &(SDL_Point){ pos.x, pos.y }, iConstCast(void *, d));
}
#endif

iBool create_Window_(iWindow *d, iRect rect, uint32_t flags) {
    flags |= SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI | SDL_WINDOW_HIDDEN;
#if defined (LAGRANGE_ENABLE_CUSTOM_FRAME)
    if (prefs_App()->customFrame) {
        /* We are drawing a custom frame so hide the default one. */
        flags |= SDL_WINDOW_BORDERLESS;
    }
#endif
    if (SDL_CreateWindowAndRenderer(
            width_Rect(rect), height_Rect(rect), flags, &d->win, &d->render)) {
        return iFalse;
    }
#if defined (LAGRANGE_ENABLE_CUSTOM_FRAME)
    if (prefs_App()->customFrame) {
        /* Register a handler for window hit testing (drag, resize). */
        SDL_SetWindowHitTest(d->win, hitTest_Window_, d);
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

void init_Window(iWindow *d, iRect rect) {
    theWindow_ = d;
    d->win = NULL;
    d->size = zero_I2(); /* will be updated below */
    iZap(d->roots);
    d->splitMode = d->pendingSplitMode = 0;
    d->pendingSplitUrl = new_String();
    d->hover = NULL;
    d->mouseGrab = NULL;
    d->focus = NULL;
    iZap(d->cursors);
    d->place.initialPos = rect.pos;
    d->place.normalRect = rect;
    d->place.lastNotifiedSize = zero_I2();
    d->pendingCursor = NULL;
    d->isDrawFrozen = iTrue;
    d->isExposed = iFalse;
    d->isMinimized = iFalse;
    d->isInvalidated = iFalse; /* set when posting event, to avoid repeated events */
    d->isMouseInside = iTrue;
    d->ignoreClick = iFalse;
    d->focusGainedAt = 0;
    d->keyboardHeight = 0;
    init_Anim(&d->rootOffset, 0.0f);
    uint32_t flags = 0;
#if defined (iPlatformAppleDesktop)
    SDL_SetHint(SDL_HINT_RENDER_DRIVER, shouldDefaultToMetalRenderer_MacOS() ? "metal" : "opengl");
#elif defined (iPlatformAppleMobile)
    SDL_SetHint(SDL_HINT_RENDER_DRIVER, "metal");
#else
    flags |= SDL_WINDOW_OPENGL;
#endif
    SDL_SetHint(SDL_HINT_RENDER_VSYNC, "1");
    /* First try SDL's default renderer that should be the best option. */
    if (forceSoftwareRender_App() || !create_Window_(d, rect, flags)) {
        /* No luck, maybe software only? This should always work as long as there is a display. */
        SDL_SetHint(SDL_HINT_RENDER_DRIVER, "software");
        if (!create_Window_(d, rect, 0)) {
            fprintf(stderr, "Error when creating window: %s\n", SDL_GetError());
            exit(-2);
        }
    }
    if (left_Rect(rect) >= 0 || top_Rect(rect) >= 0) {
        SDL_SetWindowPosition(d->win, left_Rect(rect), top_Rect(rect));
    }
    const iInt2 minSize = init_I2(425, 325);
    SDL_SetWindowMinimumSize(d->win, minSize.x, minSize.y);
    SDL_SetWindowTitle(d->win, "Lagrange");
    /* Some info. */ {
        SDL_RendererInfo info;
        SDL_GetRendererInfo(d->render, &info);
        printf("[window] renderer: %s%s\n", info.name,
               info.flags & SDL_RENDERER_ACCELERATED ? " (accelerated)" : "");
#if !defined (NDEBUG)
        printf("[window] max texture size: %d x %d\n",
               info.max_texture_width,
               info.max_texture_height);
        for (size_t i = 0; i < info.num_texture_formats; ++i) {
            printf("[window] supported texture format: %s\n", SDL_GetPixelFormatName(
                       info.texture_formats[i]));
        }
#endif
    }
    drawBlank_Window_(d);
    d->pixelRatio   = pixelRatio_Window_(d); /* point/pixel conversion */
    d->displayScale = displayScale_Window_(d);
    d->uiScale      = initialUiScale_;
    setScale_Metrics(d->pixelRatio * d->displayScale * d->uiScale);
#if defined (iPlatformMsys)
    SDL_SetWindowMinimumSize(d->win, minSize.x * d->displayScale, minSize.y * d->displayScale);
    useExecutableIconResource_SDLWindow(d->win);
#endif
#if defined (iPlatformLinux)
    SDL_SetWindowMinimumSize(d->win, minSize.x * d->pixelRatio, minSize.y * d->pixelRatio);
    /* Load the window icon. */ {
        SDL_Surface *surf = loadImage_(&imageLagrange64_Embedded, 0);
        SDL_SetWindowIcon(d->win, surf);
        free(surf->pixels);
        SDL_FreeSurface(surf);
    }
#endif
#if defined (iPlatformAppleMobile)
    setupWindow_iOS(d);
#endif
    d->presentTime = 0.0;
    d->frameTime = SDL_GetTicks();
    d->loadAnimTimer = 0;
    init_Text(d->render);
    SDL_GetRendererOutputSize(d->render, &d->size.x, &d->size.y);
    setupUserInterface_Window(d);
    postCommand_App("~bindings.changed"); /* update from bindings */
    updateSize_Window_(d, iFalse);
    /* Load the border shadow texture. */ {
        SDL_Surface *surf = loadImage_(&imageShadow_Embedded, 0);
        d->borderShadow = SDL_CreateTextureFromSurface(d->render, surf);
        SDL_SetTextureBlendMode(d->borderShadow, SDL_BLENDMODE_BLEND);
        free(surf->pixels);
        SDL_FreeSurface(surf);
    }
    d->appIcon = NULL;
#if defined (LAGRANGE_ENABLE_CUSTOM_FRAME)
    /* Load the app icon for drawing in the title bar. */
    if (prefs_App()->customFrame) {
        SDL_Surface *surf = loadImage_(&imageLagrange64_Embedded, appIconSize_Root());
        SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");
        d->appIcon = SDL_CreateTextureFromSurface(d->render, surf);
        free(surf->pixels);
        SDL_FreeSurface(surf);
        /* We need to observe non-client-area events. */
        SDL_EventState(SDL_SYSWMEVENT, SDL_TRUE);
    }
#endif
}

void deinit_Window(iWindow *d) {
    iRecycle();
    if (theWindow_ == d) {
        theWindow_ = NULL;
    }
    iForIndices(i, d->roots) {
        if (d->roots[i]) {
            setCurrent_Root(d->roots[i]);
            deinit_Root(d->roots[i]);
        }
    }
    setCurrent_Root(NULL);
    delete_String(d->pendingSplitUrl);
    deinit_Text();
    SDL_DestroyRenderer(d->render);
    SDL_DestroyWindow(d->win);
    iForIndices(i, d->cursors) {
        if (d->cursors[i]) {
            SDL_FreeCursor(d->cursors[i]);
        }
    }
}

SDL_Renderer *renderer_Window(const iWindow *d) {
    return d->render;
}

iInt2 maxTextureSize_Window(const iWindow *d) {
    SDL_RendererInfo info;
    SDL_GetRendererInfo(d->render, &info);
    return init_I2(info.max_texture_width, info.max_texture_height);
}

iBool isFullscreen_Window(const iWindow *d) {
    return snap_Window(d) == fullscreen_WindowSnap;
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

void invalidate_Window(iWindow *d) {
    if (d && !d->isInvalidated) {
        d->isInvalidated = iTrue;
        resetFonts_Text();
        postCommand_App("theme.changed auto:1"); /* forces UI invalidation */
    }
}

static iBool isNormalPlacement_Window_(const iWindow *d) {
    if (d->isDrawFrozen) return iFalse;
#if defined (iPlatformApple)
    /* Maximized mode is not special on macOS. */
    if (snap_Window(d) == maximized_WindowSnap) {
        return iTrue;
    }
#endif
    if (snap_Window(d)) return iFalse;
    return !(SDL_GetWindowFlags(d->win) & SDL_WINDOW_MINIMIZED);
}

static iBool unsnap_Window_(iWindow *d, const iInt2 *newPos) {
#if defined (LAGRANGE_ENABLE_CUSTOM_FRAME)
    if (!prefs_App()->customFrame) {
        return iFalse;
    }
    const int snap = snap_Window(d);
    if (snap == yMaximized_WindowSnap || snap == left_WindowSnap || snap == right_WindowSnap) {
        if (!newPos || (d->place.lastHit == SDL_HITTEST_RESIZE_LEFT ||
                        d->place.lastHit == SDL_HITTEST_RESIZE_RIGHT)) {
            return iFalse;
        }
        if (newPos) {
            SDL_Rect usable;
            SDL_GetDisplayUsableBounds(SDL_GetWindowDisplayIndex(d->win), &usable);
            /* Snap to top. */
            if (snap == yMaximized_WindowSnap &&
                iAbs(newPos->y - usable.y) < lineHeight_Text(uiContent_FontId) * 2) {
                setSnap_Window(d, redo_WindowSnap | yMaximized_WindowSnap);
                return iFalse;
            }
        }
    }
    if (snap && snap != fullscreen_WindowSnap) {
        if (snap_Window(d) == yMaximized_WindowSnap && newPos) {
            d->place.normalRect.pos = *newPos;
        }
        //printf("unsnap\n"); fflush(stdout);
        setSnap_Window(d, none_WindowSnap);
        return iTrue;
    }
#endif
    return iFalse;
}

static void notifyMetricsChange_Window_(const iWindow *d) {
    /* Dynamic UI metrics change. Widgets need to update themselves. */
    setScale_Metrics(d->pixelRatio * d->displayScale * d->uiScale);
    resetFonts_Text();
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
    switch (ev->event) {
#if defined (iPlatformDesktop)
        case SDL_WINDOWEVENT_EXPOSED:
            if (!d->isExposed) {
                drawBlank_Window_(d); /* avoid showing system-provided contents */
                d->isExposed = iTrue;
            }
            /* Since we are manually controlling when to redraw the window, we are responsible
               for ensuring that window contents get redrawn after expose events. Under certain
               circumstances (e.g., under openbox), not doing this would mean that the window
               is missing contents until other events trigger a refresh. */
            postRefresh_App();
#if defined (LAGRANGE_ENABLE_WINDOWPOS_FIX)
            if (d->place.initialPos.x >= 0) {
                int bx, by;
                SDL_GetWindowBordersSize(d->win, &by, &bx, NULL, NULL);
                SDL_SetWindowPosition(d->win, d->place.initialPos.x + bx, d->place.initialPos.y + by);
                d->place.initialPos = init1_I2(-1);
            }
#endif
            return iFalse;
        case SDL_WINDOWEVENT_MOVED: {
            if (d->isMinimized) {
                return iFalse;
            }
            checkPixelRatioChange_Window_(d);
            const iInt2 newPos = init_I2(ev->data1, ev->data2);
            if (isEqual_I2(newPos, init1_I2(-32000))) { /* magic! */
                /* Maybe minimized? Seems like a Windows constant of some kind. */
                d->isMinimized = iTrue;
                return iFalse;
            }
#if defined (LAGRANGE_ENABLE_CUSTOM_FRAME)
            /* Set the snap position depending on where the mouse cursor is. */
            if (prefs_App()->customFrame) {
                SDL_Rect usable;
                iInt2 mouse = cursor_Win32(); /* SDL is unaware of the current cursor pos */
                SDL_GetDisplayUsableBounds(SDL_GetWindowDisplayIndex(d->win), &usable);
                const iBool isTop = iAbs(mouse.y - usable.y) < gap_UI * 20;
                const iBool isBottom = iAbs(usable.y + usable.h - mouse.y) < gap_UI * 20;
                if (iAbs(mouse.x - usable.x) < gap_UI) {
                    setSnap_Window(d,
                                   redo_WindowSnap | left_WindowSnap |
                                       (isTop ? topBit_WindowSnap : 0) |
                                       (isBottom ? bottomBit_WindowSnap : 0));
                    return iTrue;
                }
                if (iAbs(mouse.x - usable.x - usable.w) < gap_UI) {
                    setSnap_Window(d,
                                   redo_WindowSnap | right_WindowSnap |
                                       (isTop ? topBit_WindowSnap : 0) |
                                       (isBottom ? bottomBit_WindowSnap : 0));
                    return iTrue;
                }
                if (iAbs(mouse.y - usable.y) < 2) {
                    setSnap_Window(d,
                                   redo_WindowSnap | (d->place.lastHit == SDL_HITTEST_RESIZE_TOP
                                                          ? yMaximized_WindowSnap
                                                          : maximized_WindowSnap));
                    return iTrue;
                }
            }
#endif /* defined LAGRANGE_ENABLE_CUSTOM_FRAME */
            //printf("MOVED: %d, %d\n", ev->data1, ev->data2); fflush(stdout);
            if (unsnap_Window_(d, &newPos)) {
                return iTrue;
            }
            if (isNormalPlacement_Window_(d)) {
                d->place.normalRect.pos = newPos;
                //printf("normal rect set (move)\n"); fflush(stdout);
                iInt2 border = zero_I2();
#if !defined (iPlatformApple)
                SDL_GetWindowBordersSize(d->win, &border.y, &border.x, NULL, NULL);
#endif
                d->place.normalRect.pos = max_I2(zero_I2(), sub_I2(d->place.normalRect.pos, border));
            }
            return iTrue;
        }
        case SDL_WINDOWEVENT_RESIZED:
            if (d->isMinimized) {
                updateSize_Window_(d, iTrue);
                return iTrue;
            }
            if (unsnap_Window_(d, NULL)) {
                return iTrue;
            }
            if (isNormalPlacement_Window_(d)) {
                d->place.normalRect.size = init_I2(ev->data1, ev->data2);
                //printf("normal rect set (resize)\n"); fflush(stdout);
            }
            checkPixelRatioChange_Window_(d);
            updateSize_Window_(d, iTrue /* we were already redrawing during the resize */);
            postRefresh_App();
            return iTrue;
        case SDL_WINDOWEVENT_RESTORED:
            updateSize_Window_(d, iTrue);
            invalidate_Window(d);
            d->isMinimized = iFalse;
            postRefresh_App();
            return iTrue;
        case SDL_WINDOWEVENT_MINIMIZED:
            d->isMinimized = iTrue;
            return iTrue;
#endif
        case SDL_WINDOWEVENT_LEAVE:
            unhover_Widget();
            d->isMouseInside = iFalse;
            postCommand_App("window.mouse.exited");
            return iTrue;
        case SDL_WINDOWEVENT_ENTER:
            d->isMouseInside = iTrue;
            postCommand_App("window.mouse.entered");
            return iTrue;
#if defined (iPlatformMobile)
        case SDL_WINDOWEVENT_RESIZED:
            /* On mobile, this occurs when the display is rotated. */
            invalidate_Window_(d);
            postRefresh_App();
            return iTrue;
#endif
        case SDL_WINDOWEVENT_FOCUS_GAINED:
            d->focusGainedAt = SDL_GetTicks();
            setCapsLockDown_Keys(iFalse);
            postCommand_App("window.focus.gained");
#if defined (iPlatformMobile)
            d->isExposed = iTrue; /* no expose event is sent, so now we know it's visible */
            /* Returned to foreground, may have lost buffered content. */
            invalidate_Window_(d);
            postCommand_App("window.unfreeze");
#endif
            return iFalse;
        case SDL_WINDOWEVENT_FOCUS_LOST:
            postCommand_App("window.focus.lost");
#if defined (iPlatformMobile)
            setFreezeDraw_Window(d, iTrue);
#endif
            return iFalse;
        case SDL_WINDOWEVENT_TAKE_FOCUS:
            SDL_SetWindowInputFocus(d->win);
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
            return handleWindowEvent_Window_(d, &ev->window);
        }
        case SDL_RENDER_TARGETS_RESET:
        case SDL_RENDER_DEVICE_RESET: {
            invalidate_Window(d);
            break;
        }
        default: {
            SDL_Event event = *ev;
            if (event.type == SDL_USEREVENT && isCommand_UserEvent(ev, "window.unfreeze")) {
                d->isDrawFrozen = iFalse;
                if (SDL_GetWindowFlags(d->win) & SDL_WINDOW_HIDDEN) {
                    SDL_ShowWindow(d->win);
                }
                postRefresh_App();
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
            }
            const iWidget *oldHover = d->hover;
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
                    postContextClick_Window(d, &event.button);
                }
            }
            if (isMetricsChange_UserEvent(&event)) {
                iForIndices(i, d->roots) {
                    updateMetrics_Root(d->roots[i]);
                }
            }
            if (isCommand_UserEvent(&event, "lang.changed")) {
#if defined (iPlatformAppleDesktop)
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
            if (oldHover != d->hover) {
                postRefresh_App();
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
            else if ((ev->type == SDL_KEYDOWN || ev->type == SDL_KEYUP || ev->type == SDL_TEXTINPUT)
                     && d->keyRoot != root) {
                continue; /* Key events go only to the root with keyboard focus. */
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
    if (d->isDrawFrozen) {
        return;
    }
#if defined (iPlatformMobile)
    /* Check if root needs resizing. */ {
        iInt2 renderSize;
        SDL_GetRendererOutputSize(d->render, &renderSize.x, &renderSize.y);
        if (!isEqual_I2(renderSize, d->root->rect.size)) {
            updateSize_Window_(d, iTrue);
            processEvents_App(postedEventsOnly_AppEventMode);
        }
    }
#endif
    const int   winFlags = SDL_GetWindowFlags(d->win);
    const iBool gotFocus = (winFlags & SDL_WINDOW_INPUT_FOCUS) != 0;
    iPaint p;
    init_Paint(&p);
    /* Clear the window. The clear color is visible as a border around the window
       when the custom frame is being used. */ {
#if defined (iPlatformAppleMobile)
        const iColor back = get_Color(tmBackground_ColorId);
#else
        const iColor back = get_Color(gotFocus && d->place.snap != maximized_WindowSnap &&
                                              ~winFlags & SDL_WINDOW_FULLSCREEN_DESKTOP
                                          ? uiAnnotation_ColorId
                                          : uiSeparator_ColorId);
#endif
        unsetClip_Paint(&p); /* update clip to full window */
        SDL_SetRenderDrawColor(d->render, back.r, back.g, back.b, 255);
        SDL_RenderClear(d->render);
    }
    /* Draw widgets. */
    d->frameTime = SDL_GetTicks();
    if (isExposed_Window(d)) {
        d->isInvalidated = iFalse;
        iForIndices(i, d->roots) {
            iRoot *root = d->roots[i];
            if (root) {
                setCurrent_Root(root);
                unsetClip_Paint(&p); /* update clip to current root */
                draw_Widget(root->widget);
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
                        d->render,
                        d->appIcon,
                        NULL,
                        &(SDL_Rect){ left_Rect(rect) + gap_UI * 1.25f, mid.y - size / 2, size, size });
                }
#endif
                /* Root separator and keyboard focus indicator. */
                if (numRoots_Window(d) > 1){
                    const iRect bounds = bounds_Widget(root->widget);
                    if (i == 1) {
                        fillRect_Paint(&p, (iRect){
                            addX_I2(topLeft_Rect(bounds), -gap_UI / 8),
                            init_I2(gap_UI / 4, height_Rect(bounds))
                        }, uiSeparator_ColorId);
                    }
                    if (root == d->keyRoot) {
                        fillRect_Paint(&p, (iRect){
                            topLeft_Rect(bounds),
                            init_I2(width_Rect(bounds), gap_UI / 2)
                        }, uiBackgroundSelected_ColorId);
                    }
                }
            }
        }
        setCurrent_Root(NULL);
    }
#if 0
    /* Text cache debugging. */ {
        SDL_Rect rect = { d->roots[0]->widget->rect.size.x - 640, 0, 640, 2.5 * 640 };
        SDL_SetRenderDrawColor(d->render, 0, 0, 0, 255);
        SDL_RenderFillRect(d->render, &rect);
        SDL_RenderCopy(d->render, glyphCache_Text(), NULL, &rect);
    }
#endif
    SDL_RenderPresent(d->render);
}

void resize_Window(iWindow *d, int w, int h) {
    if (w > 0 && h > 0) {
        SDL_SetWindowSize(d->win, w, h);
        updateSize_Window_(d, iFalse);
    }
    else {
        updateSize_Window_(d, iTrue); /* notify always */
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

void setFreezeDraw_Window(iWindow *d, iBool freezeDraw) {
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

iInt2 mouseCoord_Window(const iWindow *d) {
#if defined (iPlatformMobile)
    /* At least on iOS the coordinates returned by SDL_GetMouseState() do no match up with
       our touch coordinates on the Y axis (?). Maybe a pixel ratio thing? */
    iUnused(d);
    return latestPosition_Touch();
#else
    if (!d->isMouseInside) {
        return init_I2(-1000000, -1000000);
    }
    int x, y;
    SDL_GetMouseState(&x, &y);
    return coord_Window(d, x, y);
#endif
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

void setKeyboardHeight_Window(iWindow *d, int height) {
    if (d->keyboardHeight != height) {
        d->keyboardHeight = height;
        if (height == 0) {
            setFlags_Anim(&d->rootOffset, easeBoth_AnimFlag, iTrue);
            setValue_Anim(&d->rootOffset, 0, 250);
        }
        postCommandf_App("keyboard.changed arg:%d", height);
        postRefresh_App();
    }
}

void checkPendingSplit_Window(iWindow *d) {
    if (d->splitMode != d->pendingSplitMode) {
        setSplitMode_Window(d, d->pendingSplitMode);
    }
}

void swapRoots_Window(iWindow *d) {
    if (numRoots_Window(d) == 2) {
        iSwap(iRoot *, d->roots[0], d->roots[1]);
        updateSize_Window_(d, iTrue);
    }
}

void setSplitMode_Window(iWindow *d, int splitFlags) {
    const int splitMode = splitFlags & mode_WindowSplit;
    iAssert(current_Root() == NULL);
    if (d->splitMode != splitMode) {
        int oldCount = numRoots_Window(d);
        setFreezeDraw_Window(d, iTrue);
        if (oldCount == 2 && splitMode == 0) {
            /* Keep references to the tabs of the second root. */
            iObjectList *tabs = listDocuments_App(d->roots[1]);
            iForEach(ObjectList, i, tabs) {
                setRoot_Widget(i.object, d->roots[0]);
            }
            delete_Root(d->roots[1]);
            d->roots[1] = NULL;
            d->keyRoot = d->roots[0];
            /* Move the deleted root's tabs to the first root. */
            setCurrent_Root(d->roots[0]);
            iWidget *docTabs = findWidget_Root("doctabs");
            iForEach(ObjectList, j, tabs) {
                appendTabPage_Widget(docTabs, j.object, "", 0, 0);
            }
            /* The last child is the [+] button for adding a tab. */
            moveTabButtonToEnd_Widget(findChild_Widget(docTabs, "newtab"));
            iRelease(tabs);
        }
        else if (splitMode && oldCount == 1) {
            /* Add a second root. */
            iDocumentWidget *moved = document_Root(d->roots[0]);
            iAssert(d->roots[1] == NULL);
            d->roots[1] = new_Root();
            setCurrent_Root(d->roots[1]);
            d->keyRoot = d->roots[1];
            createUserInterface_Root(d->roots[1]);
            if (!isEmpty_String(d->pendingSplitUrl)) {
                postCommandf_Root(d->roots[1], "open url:%s",
                                  cstr_String(d->pendingSplitUrl));
                clear_String(d->pendingSplitUrl);
            }
            else if (~splitFlags & noEvents_WindowSplit) {
                iWidget *docTabs0 = findChild_Widget(d->roots[0]->widget, "doctabs");
                iWidget *docTabs1 = findChild_Widget(d->roots[1]->widget, "doctabs");
                /* If the old root has multiple tabs, move the current one to the new split. */
                if (tabCount_Widget(docTabs0) >= 2) {
                    int movedIndex = tabPageIndex_Widget(docTabs0, moved);
                    removeTabPage_Widget(docTabs0, movedIndex);
                    showTabPage_Widget(docTabs0, tabPage_Widget(docTabs0, iMax(movedIndex - 1, 0)));
                    iRelease(removeTabPage_Widget(docTabs1, 0)); /* delete the default tab */
                    setRoot_Widget(as_Widget(moved), d->roots[1]);
                    prependTabPage_Widget(docTabs1, iClob(moved), "", 0, 0);
                    postCommandf_App("tabs.switch page:%p", moved);
                }
                else {
                    postCommand_Root(d->roots[1], "navigate.home");
                }
            }
            setCurrent_Root(NULL);
        }
        d->splitMode = splitMode;
#if defined (LAGRANGE_ENABLE_CUSTOM_FRAME)
        /* Update custom frame controls. */{
            const iBool hideCtl0 = numRoots_Window(d) != 1;
            iWidget *winBar = findChild_Widget(d->roots[0]->widget, "winbar");
            if (winBar) {
                setFlags_Widget(
                    findChild_Widget(winBar, "winbar.min"), hidden_WidgetFlag, hideCtl0);
                setFlags_Widget(
                    findChild_Widget(winBar, "winbar.max"), hidden_WidgetFlag, hideCtl0);
                setFlags_Widget(
                    findChild_Widget(winBar, "winbar.close"), hidden_WidgetFlag, hideCtl0);
                if (d->roots[1]) {
                    winBar = findChild_Widget(d->roots[1]->widget, "winbar");
                    setFlags_Widget(
                        findChild_Widget(winBar, "winbar.icon"), hidden_WidgetFlag, iTrue);
                    setFlags_Widget(
                        findChild_Widget(winBar, "winbar.app"), hidden_WidgetFlag, iTrue);
                }
            }
        }
#endif
        if (~splitFlags & noEvents_WindowSplit) {
            updateSize_Window_(d, iTrue);
            postCommand_App("window.unfreeze");
        }
    }
}

void setSnap_Window(iWindow *d, int snapMode) {
    if (!prefs_App()->customFrame) {
        if (snapMode == maximized_WindowSnap) {
            SDL_MaximizeWindow(d->win);
        }
        else if (snapMode == fullscreen_WindowSnap) {
            SDL_SetWindowFullscreen(d->win, SDL_WINDOW_FULLSCREEN_DESKTOP);
        }
        else {
            if (snap_Window(d) == fullscreen_WindowSnap) {
                SDL_SetWindowFullscreen(d->win, 0);
            }
            else {
                SDL_RestoreWindow(d->win);
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
    SDL_GetDisplayUsableBounds(SDL_GetWindowDisplayIndex(d->win), &usable);
    if (d->place.snap == fullscreen_WindowSnap) {
        SDL_SetWindowFullscreen(d->win, 0);
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
            SDL_GetWindowSize(d->win, &newRect.size.x, NULL);
            SDL_GetWindowPosition(d->win, &newRect.pos.x, NULL);
            /* Snap the window to left/right edges, if close by. */
            if (iAbs(right_Rect(newRect) - (usable.x + usable.w)) < snapDist) {
                newRect.pos.x = usable.x + usable.w - width_Rect(newRect);
            }
            if (iAbs(newRect.pos.x - usable.x) < snapDist) {
                newRect.pos.x = usable.x;
            }
            break;
        case fullscreen_WindowSnap:
            SDL_SetWindowFullscreen(d->win, SDL_WINDOW_FULLSCREEN_DESKTOP);
            break;
    }
    if (snapMode & (topBit_WindowSnap | bottomBit_WindowSnap)) {
        newRect.size.y /= 2;
    }
    if (snapMode & bottomBit_WindowSnap) {
        newRect.pos.y += newRect.size.y;
    }
    /* Update window controls. */
    iForIndices(rootIndex, d->roots) {
        iRoot *root = d->roots[rootIndex];
        if (!root) continue;
        iWidget *winBar = findChild_Widget(root->widget, "winbar");
        updateTextCStr_LabelWidget(findChild_Widget(winBar, "winbar.max"),
                                   d->place.snap == maximized_WindowSnap ? "\u25a2" : "\u25a1");
        /* Show and hide the title bar. */
        const iBool wasVisible = isVisible_Widget(winBar);
        setFlags_Widget(winBar, hidden_WidgetFlag, d->place.snap == fullscreen_WindowSnap);
        if (newRect.size.x) {
            SDL_SetWindowPosition(d->win, newRect.pos.x, newRect.pos.y);
            SDL_SetWindowSize(d->win, newRect.size.x, newRect.size.y);
            postCommand_App("window.resized");
        }
        if (wasVisible != isVisible_Widget(winBar)) {
            arrange_Widget(root->widget);
            postRefresh_App();
        }
    }
#endif /* defined (LAGRANGE_ENABLE_CUSTOM_FRAME) */
}

int snap_Window(const iWindow *d) {
    if (!prefs_App()->customFrame) {
        const int flags = SDL_GetWindowFlags(d->win);
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
