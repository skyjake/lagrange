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

#include "widget.h"

#include "app.h"
#include "periodic.h"
#include "touch.h"
#include "command.h"
#include "paint.h"
#include "root.h"
#include "util.h"
#include "window.h"

#include "labelwidget.h"
#include "inputwidget.h"

#include <the_Foundation/ptrarray.h>
#include <the_Foundation/ptrset.h>
#include <SDL_mouse.h>
#include <SDL_timer.h>
#include <stdarg.h>

#if defined (iPlatformAppleMobile)
#   include "../ios.h"
#endif

struct Impl_WidgetDrawBuffer {
    SDL_Texture *texture;
    iInt2        size;
    iBool        isValid;
    SDL_Texture *oldTarget;
    iInt2        oldOrigin;
};

iDeclareType(RecentlyDeleted)

/* Keep track of widgets that were recently deleted, so events related to them can be ignored. */
struct Impl_RecentlyDeleted {
    iMutex    mtx; /* async callbacks must not post events related to deleted widgets */
    iPtrSet * objs;
};
static iRecentlyDeleted recentlyDeleted_;

static void maybeInit_RecentlyDeleted_(iRecentlyDeleted *d) {
    if (!d->objs) {
        init_Mutex(&d->mtx);
        d->objs = new_PtrSet();
    }
}

static iBool contains_RecentlyDeleted_(iRecentlyDeleted *d, const iAnyObject *obj) {
    if (d->objs && obj) {
        lock_Mutex(&d->mtx);
        const iBool wasDel = contains_PtrSet(d->objs, obj);
        unlock_Mutex(&d->mtx);
        return wasDel;
    }
    return iFalse;
}

static void init_WidgetDrawBuffer(iWidgetDrawBuffer *d) {
    d->texture   = NULL;
    d->size      = zero_I2();
    d->isValid   = iFalse;
    d->oldTarget = NULL;
}

static void deinit_WidgetDrawBuffer(iWidgetDrawBuffer *d) {
    SDL_DestroyTexture(d->texture);
}

iDefineTypeConstruction(WidgetDrawBuffer)
    
static void realloc_WidgetDrawBuffer(iWidgetDrawBuffer *d, SDL_Renderer *render, iInt2 size) {
    if (!isEqual_I2(d->size, size)) {
        d->size = size;
        if (d->texture) {
            SDL_DestroyTexture(d->texture);
        }
        d->texture = SDL_CreateTexture(render,
                                       SDL_PIXELFORMAT_RGBA8888,
                                       SDL_TEXTUREACCESS_STATIC | SDL_TEXTUREACCESS_TARGET,
                                       size.x,
                                       size.y);
        SDL_SetTextureBlendMode(d->texture, SDL_BLENDMODE_BLEND);
        d->isValid = iFalse;
    }
}

static void release_WidgetDrawBuffer(iWidgetDrawBuffer *d) {
    if (d->texture) {
        SDL_DestroyTexture(d->texture);
        d->texture = NULL;
    }
    d->size = zero_I2();
    d->isValid = iFalse;
}

static iRect boundsForDraw_Widget_(const iWidget *d) {
    iRect bounds = bounds_Widget(d);
    if (d->flags & drawBackgroundToBottom_WidgetFlag) {
        bounds.size.y = iMax(bounds.size.y, size_Root(d->root).y);
    }
    return bounds;
}

static iBool checkDrawBuffer_Widget_(const iWidget *d) {
    return d->drawBuf && d->drawBuf->isValid &&
           isEqual_I2(d->drawBuf->size, boundsForDraw_Widget_(d).size);
}

/*----------------------------------------------------------------------------------------------*/

static void printInfo_Widget_(const iWidget *);

void releaseChildren_Widget(iWidget *d) {
    iForEach(ObjectList, i, d->children) {
        iWidget *child = i.object;
        child->parent = NULL; /* the actual reference being held */
        if (child->flags & keepOnTop_WidgetFlag) {
            removeOne_PtrArray(onTop_Root(child->root), child);
            child->flags &= ~keepOnTop_WidgetFlag;
        }
    }
    iReleasePtr(&d->children);
}

iDefineObjectConstruction(Widget)

void init_Widget(iWidget *d) {
    init_String(&d->id);
    d->root           = get_Root();
    d->flags          = 0;
    d->flags2         = 0;
    d->rect           = zero_Rect();
    d->oldSize        = zero_I2();
    d->minSize        = zero_I2();
    d->sizeRef        = NULL;
    d->offsetRef      = NULL;
    d->bgColor        = none_ColorId;
    d->frameColor     = none_ColorId;
    init_Anim(&d->visualOffset, 0.0f);
    d->children       = NULL;
    d->parent         = NULL;
    d->commandHandler = NULL;
    d->drawBuf        = NULL;
    init_Anim(&d->overflowScrollOpacity, 0.0f);
    init_String(&d->data);
    iZap(d->padding);
}

static void visualOffsetAnimation_Widget_(void *ptr) {
    iWidget *d = ptr;
    postRefresh_App();
    d->root->didAnimateVisualOffsets = iTrue;
#if 0
    printf("'%s' visoffanim: fin:%d val:%f\n", cstr_String(&d->id),
           isFinished_Anim(&d->visualOffset), value_Anim(&d->visualOffset)); fflush(stdout);
#endif
    if (!isFinished_Anim(&d->visualOffset)) {
        addTickerRoot_App(visualOffsetAnimation_Widget_, d->root, ptr);
    }
    else {
        d->flags &= ~visualOffset_WidgetFlag;
    }
}

static void animateOverflowScrollOpacity_Widget_(void *ptr) {
    iWidget *d = ptr;
    postRefresh_App();
    if (!isFinished_Anim(&d->overflowScrollOpacity)) {
        addTickerRoot_App(animateOverflowScrollOpacity_Widget_, d->root, ptr);
    }
}

static int treeSize_Widget_(const iWidget *d, int n) {
    iConstForEach(ObjectList, i, d->children) {
        n = treeSize_Widget_(i.object, n);
    }
    return n + size_ObjectList(d->children);
}

void deinit_Widget(iWidget *d) {
    addRecentlyDeleted_Widget(d);
    if (d->flags2 & usedAsPeriodicContext_WidgetFlag2) {
        remove_Periodic(periodic_App(), d); /* periodic context being deleted */
    }
//    const int nt = treeSize_Widget_(d, 0);
//    const int no = totalCount_Object();
    releaseChildren_Widget(d);
//    printf("deinit_Widget %p (%s):\ttreesize=%d\td_obj=%d\n", d, class_Widget(d)->name, nt, totalCount_Object() - no);
    delete_WidgetDrawBuffer(d->drawBuf);
#if 0 && !defined (NDEBUG)
    if (cmp_String(&d->id, "")) {
        printf("widget %p (%s) deleted (on top:%d)\n", d, cstr_String(&d->id),
               d->flags & keepOnTop_WidgetFlag ? 1 : 0);
    }
#endif
    deinit_String(&d->data);
    deinit_String(&d->id);
    if (d->flags & keepOnTop_WidgetFlag) {
        removeAll_PtrArray(onTop_Root(d->root), d);
    }
    if (d->flags & visualOffset_WidgetFlag) {
        removeTicker_App(visualOffsetAnimation_Widget_, d);
    }
    if (d->flags & overflowScrollable_WidgetFlag) {
        removeTicker_App(animateOverflowScrollOpacity_Widget_, d);
    }
    iWindow *win = d->root->window;
    iAssert(win);
    if (win->lastHover == d) {
        win->lastHover = NULL;
    }
    if (win->hover == d) {
        win->hover = NULL;
    }
    if (d->flags & nativeMenu_WidgetFlag) {
        releaseNativeMenu_Widget(d);
    }
    widgetDestroyed_Touch(d);
    iAssert(!contains_Periodic(periodic_App(), d));
    d->root = NULL;
}

static void aboutToBeDestroyed_Widget_(iWidget *d) {
    d->flags |= destroyPending_WidgetFlag;
    remove_Periodic(periodic_App(), d);
    iWindow *win = get_Window();
    if (isHover_Widget(d)) {
        win->hover = NULL;
    }
    if (win->lastHover == d) {
        win->lastHover = NULL;
    }
    iForEach(ObjectList, i, d->children) {
        aboutToBeDestroyed_Widget_(as_Widget(i.object));
    }
}

iLocalDef iBool isRoot_Widget_(const iWidget *d) {
    return d && d->root && d->root->widget == d;
}

void destroy_Widget(iWidget *d) {
    if (d) {
        iAssert(!isRoot_Widget_(d));
        if (isVisible_Widget(d)) {
            postRefresh_App();
        }
        aboutToBeDestroyed_Widget_(d);
        if (!d->root->pendingDestruction) {
            d->root->pendingDestruction = new_PtrSet();
        }
        insert_PtrSet(d->root->pendingDestruction, d);
        if (focus_Widget() && (focus_Widget() == d || hasParent_Widget(focus_Widget(), d))) {
            setFocus_Widget(NULL);
        }
    }
}

void setId_Widget(iWidget *d, const char *id) {
    setCStr_String(&d->id, id);
}

const iString *id_Widget(const iWidget *d) {
    return d ? &d->id : collectNew_String();
}

int64_t flags_Widget(const iWidget *d) {
    return d ? d->flags : 0;
}

void setFlags_Widget(iWidget *d, int64_t flags, iBool set) {
    if (d) {
        if (deviceType_App() != desktop_AppDeviceType) {
            /* TODO: Tablets should detect if a hardware keyboard is available. */
            flags &= ~drawKey_WidgetFlag;
        }
        const int64_t oldFlags = d->flags;  
        iChangeFlags(d->flags, flags, set);
        if (flags & keepOnTop_WidgetFlag && !isRoot_Widget_(d)) {
            iPtrArray *onTop = onTop_Root(d->root);
            if (set) {
                if (oldFlags & keepOnTop_WidgetFlag) {
                    raise_Widget(d);
                }
                else {
                    pushBack_PtrArray(onTop, d);
                }
            }
            else {
                removeOne_PtrArray(onTop, d);
                iAssert(indexOf_PtrArray(onTop, d) == iInvalidPos);
            }
        }
#if !defined (NDEBUG)
        if (d->flags & arrangeWidth_WidgetFlag &&
            d->flags & resizeToParentWidth_WidgetFlag) {
            printf("[Widget] Conflicting flags for ");
            identify_Widget(d);
        }
#endif
    }
}

void setTreeFlags_Widget(iWidget *d, int64_t flags, iBool set) {
    if (d) {
        setFlags_Widget(d, flags, iTrue);
        iForEach(ObjectList, i, d->children) {
            setTreeFlags_Widget(i.object, flags, set);
        }
    }
}

void setPos_Widget(iWidget *d, iInt2 pos) {
    d->rect.pos = pos;
    setFlags_Widget(d, fixedPosition_WidgetFlag, iTrue);
}

void setFixedSize_Widget(iWidget *d, iInt2 fixedSize) {
    if (!d) return;
    int flags = fixedSize_WidgetFlag;
    if (fixedSize.x < 0) {
        fixedSize.x = d->rect.size.x;
        flags &= ~fixedWidth_WidgetFlag;
    }
    if (fixedSize.y < 0) {
        fixedSize.y = d->rect.size.y;
        flags &= ~fixedHeight_WidgetFlag;
    }
    d->rect.size = fixedSize;
    setFlags_Widget(d, flags, iTrue);
}

void setMinSize_Widget(iWidget *d, iInt2 minSize) {
    d->minSize = minSize;
    /* rearranging needed to apply this */
}

void setPadding_Widget(iWidget *d, int left, int top, int right, int bottom) {
    if (d) {
        d->padding[0] = left * aspect_UI;
        d->padding[1] = top * aspect_UI;
        d->padding[2] = right * aspect_UI;
        d->padding[3] = bottom * aspect_UI;
    }
}

iWidget *root_Widget(const iWidget *d) {
    return d ? d->root->widget : NULL;
}

iWindow *window_Widget(const iAnyObject *d) {
    return constAs_Widget(d)->root->window;
}

void showCollapsed_Widget(iWidget *d, iBool show) {
    if (!d) return;
    const iBool isVisible = !(d->flags & hidden_WidgetFlag);
    if ((isVisible && !show) || (!isVisible && show)) {
        setFlags_Widget(d, hidden_WidgetFlag, !show);
        /* The entire UI may be affected, if parents are resized due to the (un)collapsing. */
        arrange_Widget(root_Widget(d));
        refresh_Widget(d);
    }
}

void setVisualOffset_Widget(iWidget *d, int value, uint32_t span, int animFlags) {
    setFlags_Widget(d, visualOffset_WidgetFlag, iTrue);
    if (span == 0) {
        init_Anim(&d->visualOffset, value);
        if (value == 0) {
            setFlags_Widget(d, visualOffset_WidgetFlag, iFalse); /* offset is being reset */
        }
    }
    else {
        setValue_Anim(&d->visualOffset, value, span);
        d->visualOffset.flags = animFlags;
        addTickerRoot_App(visualOffsetAnimation_Widget_, d->root, d);
    }
}

void setBackgroundColor_Widget(iWidget *d, int bgColor) {
    if (d) {
        d->bgColor = bgColor;
    }
}

void setFrameColor_Widget(iWidget *d, int frameColor) {
    d->frameColor = frameColor;
}

void setCommandHandler_Widget(iWidget *d, iBool (*handler)(iWidget *, const char *)) {
    d->commandHandler = handler;
}

void setRoot_Widget(iWidget *d, iRoot *root) {
    if (d->flags & keepOnTop_WidgetFlag) {
        iAssert(indexOf_PtrArray(onTop_Root(root), d) == iInvalidPos);
        /* Move it over the new root's onTop list. */
        removeOne_PtrArray(onTop_Root(d->root), d);
        if (d != root->widget) {
            iAssert(indexOf_PtrArray(onTop_Root(d->root), d) == iInvalidPos);
            pushBack_PtrArray(onTop_Root(root), d);
        }
    }
    if (d->root != root) {
        d->root = root;
        if (class_Widget(d)->rootChanged) {
            class_Widget(d)->rootChanged(d);
        }
    }
    iForEach(ObjectList, i, d->children) {
        setRoot_Widget(i.object, root);
    }
}

iLocalDef iBool isCollapsed_Widget_(const iWidget *d) {
    return (d->flags & (hidden_WidgetFlag | collapse_WidgetFlag)) ==
           (hidden_WidgetFlag | collapse_WidgetFlag);
}

iLocalDef iBool isArrangedPos_Widget_(const iWidget *d) {
    return (d->flags & fixedPosition_WidgetFlag) == 0;
}

iLocalDef iBool isArrangedSize_Widget_(const iWidget *d) {
    return !isCollapsed_Widget_(d) && isArrangedPos_Widget_(d) &&
           !(d->flags & parentCannotResize_WidgetFlag);
}

iLocalDef iBool doesAffectSizing_Widget_(const iWidget *d) {
    return !isCollapsed_Widget_(d) && isArrangedPos_Widget_(d);
}

static int numExpandingChildren_Widget_(const iWidget *d) {
    int count = 0;
    iConstForEach(ObjectList, i, d->children) {
        const iWidget *child = constAs_Widget(i.object);
        if (flags_Widget(child) & expand_WidgetFlag && doesAffectSizing_Widget_(child)) {
            count++;
        }
    }
    return count;
}

static int widestChild_Widget_(const iWidget *d) {
    int width = 0;
    iConstForEach(ObjectList, i, d->children) {
        const iWidget *child = constAs_Widget(i.object);
        width = iMax(width, child->rect.size.x);
    }
    return width;
}

static void arrange_Widget_(iWidget *);
static const iBool tracing_ = iFalse;

iLocalDef iBool inTraceScope_(const iWidget *d) {
    /*for (const iWidget *w = d; w; w = w->parent) {
        if (!cmp_String(&w->id, "prefs")) {
            return iTrue;
        }
    }*/
    return iFalse;
}

#define TRACE(d, ...)   if (tracing_ && inTraceScope_(d)) { printf_Widget_(d, __VA_ARGS__); }

static int depth_Widget_(const iWidget *d) {
    int depth = 0;
    for (const iWidget *w = d->parent; w; w = w->parent) {
        depth++;
    }
    return depth;
}

static void printf_Widget_(const iWidget *d, const char *format, ...) {
    va_list args;
    va_start(args, format);
    iString *msg = new_String();
    for (size_t i = 0; i < depth_Widget_(d); ++i) {
        appendCStr_String(msg, "|   ");
    }
    appendFormat_String(msg, "[%p] %s(%s) ", d, class_Widget(d)->name, cstr_String(id_Widget(d)));
    while (size_String(msg) < 44 + depth_Widget_(d) * 4) {
        appendCStr_String(msg, " ");
    }
    iBlock *msg2 = new_Block(0);
    vprintf_Block(msg2, format, args);
    va_end(args);
    printf("%s%s\n", cstr_String(msg), cstr_Block(msg2));
    delete_Block(msg2);
    delete_String(msg);
}

static iBool setWidth_Widget_(iWidget *d, int width) {
    iAssert(width >= 0);
    TRACE(d, "attempt to set width to %d (current: %d, min width: %d)", width, d->rect.size.x, d->minSize.x);
    width = iMax(width, d->minSize.x);
    if (~d->flags & fixedWidth_WidgetFlag) {
        if (d->rect.size.x != width) {
            d->rect.size.x = width;
            TRACE(d, "width has changed to %d", width);
//            if (~d->flags2 & undefinedWidth_WidgetFlag2 && class_Widget(d)->sizeChanged) {
//                class_Widget(d)->sizeChanged(d);
//            }
//            d->flags2 &= ~undefinedWidth_WidgetFlag2;
            return iTrue;
        }
    }
    else {
        TRACE(d, "changing width not allowed; flags: %x", d->flags);
    }
    return iFalse;
}

static iBool setHeight_Widget_(iWidget *d, int height) {
    iAssert(height >= 0);
    if (d->sizeRef) {
        return iFalse; /* height defined by another widget */
    }
    TRACE(d, "attempt to set height to %d (current: %d, min height: %d)", height, d->rect.size.y, d->minSize.y);
    height = iMax(height, d->minSize.y);
    if (~d->flags & fixedHeight_WidgetFlag) {
        if (d->rect.size.y != height) {
            d->rect.size.y = height;
            TRACE(d, "height has changed to %d", height);
//            if (~d->flags2 & undefinedHeight_WidgetFlag2 && class_Widget(d)->sizeChanged) {
//                class_Widget(d)->sizeChanged(d);
//            }
//            d->flags2 &= ~undefinedHeight_WidgetFlag2;
            return iTrue;
        }
    }
    else {
        TRACE(d, "changing height not allowed; flags: %x", d->flags);
    }
    return iFalse;
}

iLocalDef iRect innerRect_Widget_(const iWidget *d) {
    return init_Rect(d->padding[0],
                     d->padding[1],
                     iMaxi(0, width_Rect(d->rect) - d->padding[0] - d->padding[2]),
                     iMaxi(0, height_Rect(d->rect) - d->padding[1] - d->padding[3]));
}

iRect innerBounds_Widget(const iWidget *d) {
    iRect ib = adjusted_Rect(bounds_Widget(d),
                             init_I2(d->padding[0], d->padding[1]),
                             init_I2(-d->padding[2], -d->padding[3]));
    ib.size = max_I2(zero_I2(), ib.size);
    return ib;
}

iRect innerBoundsWithoutVisualOffset_Widget(const iWidget *d) {
    iRect ib = adjusted_Rect(boundsWithoutVisualOffset_Widget(d),
                             init_I2(d->padding[0], d->padding[1]),
                             init_I2(-d->padding[2], -d->padding[3]));
    ib.size = max_I2(zero_I2(), ib.size);
    return ib;
}

static size_t numArrangedChildren_Widget_(const iWidget *d) {
    size_t count = 0;
    iConstForEach(ObjectList, i, d->children) {
        if (isArrangedPos_Widget_(i.object)) {
            count++;
        }
    }
    return count;
}

static void centerHorizontal_Widget_(iWidget *d) {
    const int width          = width_Rect(d->rect);
    const int containerWidth = d->parent ? width_Rect(innerRect_Widget_(d->parent))
                                         : size_Root(d->root).x;
    d->rect.pos.x = (containerWidth - width) / 2;
    TRACE(d, "center horizontally: %d", d->rect.pos.x);
}

static void boundsOfChildren_Widget_(const iWidget *d, iRect *bounds_out) {
    *bounds_out = zero_Rect();
    iConstForEach(ObjectList, i, d->children) {
        const iWidget *child = constAs_Widget(i.object);
        if (isCollapsed_Widget_(child)) {
            continue;
        }
        iRect childRect = child->rect;
        if (child->flags & ignoreForParentWidth_WidgetFlag) {
            childRect.size.x = 0;
            childRect.pos.x  = bounds_out->pos.x;
        }
        if (child->flags & ignoreForParentHeight_WidgetFlag) {
            childRect.size.y = 0;
            childRect.pos.y  = bounds_out->pos.y;
        }
        if (isEmpty_Rect(*bounds_out)) {
            *bounds_out = childRect;
        }
        else {
            *bounds_out = union_Rect(*bounds_out, childRect);
        }
    }
#if !defined (NDEBUG)
    if (tracing_) {
        if (bounds_out->size.x && bounds_out->size.y == 0) {
            printf("SUSPECT CHILD BOUNDS?\n");
            puts  ("---------------------");
            printTree_Widget(d);
            puts  ("---------------------");
        }
    }
#endif
}

static void arrange_Widget_(iWidget *d) {
    TRACE(d, "arranging...");
    if (d->sizeRef) {
        d->rect.size.y = height_Widget(d->sizeRef);
        TRACE(d, "use referenced height: %d", d->rect.size.y);
    }
    if (d->flags & moveToParentLeftEdge_WidgetFlag) {
        d->rect.pos.x = d->padding[0]; /* FIXME: Shouldn't this be d->parent->padding[0]? */
        TRACE(d, "move to parent left edge: %d", d->rect.pos.x);
    }
    else if (d->flags & moveToParentRightEdge_WidgetFlag) {
        d->rect.pos.x = width_Rect(innerRect_Widget_(d->parent)) - width_Rect(d->rect);
        TRACE(d, "move to parent right edge: %d", d->rect.pos.x);
    }
    else if (d->flags & moveToParentBottomEdge_WidgetFlag) {
        if (d->parent) {
            d->rect.pos.y = height_Rect(innerRect_Widget_(d->parent)) - height_Rect(d->rect);
            const int minY = (d->parent->parent ? 0 : topSafeInset_Mobile());
            d->rect.pos.y = iMax(minY, d->rect.pos.y);
            TRACE(d, "move to parent bottom edge: %d", d->rect.pos.y);
        }
    }
    else if (d->flags & centerHorizontal_WidgetFlag) {
        centerHorizontal_Widget_(d);
    }
    if (d->flags & resizeToParentWidth_WidgetFlag && d->parent) {
        iRect childBounds = zero_Rect();
        if (flags_Widget(d->parent) & arrangeWidth_WidgetFlag) {
            /* Can't go narrower than what the children require, though. */
            boundsOfChildren_Widget_(d, &childBounds);
        }
        TRACE(d, "resize to parent width; child bounds width %d", childBounds.size.x, childBounds.size.y);
        setWidth_Widget_(d, iMaxi(width_Rect(innerRect_Widget_(d->parent)),
                                  width_Rect(childBounds)));
    }
    if (d->flags & resizeToParentHeight_WidgetFlag && d->parent) {
        TRACE(d, "resize to parent height");
        setHeight_Widget_(d, height_Rect(innerRect_Widget_(d->parent)));
    }
    if (d->flags & safePadding_WidgetFlag) {
#if defined (iPlatformAppleMobile)
        float left, top, right, bottom;
        safeAreaInsets_iOS(&left, &top, &right, &bottom);
        setPadding_Widget(d, left, top, right, bottom);
#endif
    }
    /* The rest of the arrangement depends on child widgets. */
    if (!d->children) {
        TRACE(d, "no children => END");
        return;
    }
    const size_t childCount = numArrangedChildren_Widget_(d);
    TRACE(d, "%d arranged children", childCount);
    const int expCount = numExpandingChildren_Widget_(d);
    TRACE(d, "%d expanding children", expCount);
    /* Resize children to fill the parent widget. */
    iAssert((d->flags & (resizeToParentWidth_WidgetFlag | arrangeWidth_WidgetFlag)) !=
            (resizeToParentWidth_WidgetFlag | arrangeWidth_WidgetFlag));
    if (d->flags & resizeChildren_WidgetFlag) {
        const iInt2 dirs = init_I2((d->flags & resizeWidthOfChildren_WidgetFlag) != 0,
                                   (d->flags & resizeHeightOfChildren_WidgetFlag) != 0);
#if !defined (NDEBUG)
        /* Check for conflicting flags. */
        if (dirs.x) {
            if (d->flags & arrangeWidth_WidgetFlag) {
                identify_Widget(d);
            }
            iAssert(~d->flags & arrangeWidth_WidgetFlag);
        }
        if (dirs.y) iAssert(~d->flags & arrangeHeight_WidgetFlag);
#endif
        TRACE(d, "resize children, x:%d y:%d (own size: %dx%d)", dirs.x, dirs.y,
              d->rect.size.x, d->rect.size.y);
        if (expCount > 0) {
            /* There are expanding children, so all non-expanding children will retain their
               current size. */
            iInt2 avail = innerRect_Widget_(d).size;
            TRACE(d, "inner size: %dx%d", avail.x, avail.y);
            iConstForEach(ObjectList, i, d->children) {
                const iWidget *child = constAs_Widget(i.object);
                if (doesAffectSizing_Widget_(child)) {
                    if (~child->flags & expand_WidgetFlag) {
                        subv_I2(&avail, child->rect.size);
                    }
                }
            }
            avail = divi_I2(max_I2(zero_I2(), avail), expCount);
            TRACE(d, "changing child sizes...");
            iForEach(ObjectList, j, d->children) {
                iWidget *child = as_Widget(j.object);
                if (!isArrangedSize_Widget_(child)) {
                    TRACE(d, "child %p size is not arranged", child);
                    continue;
                }
                if (~child->flags & expand_WidgetFlag) {
#if 0
                    if (d->flags & arrangeHorizontal_WidgetFlag) {
                        if (dirs.x) setWidth_Widget_(child, avail.x);
                        if (dirs.y) setHeight_Widget_(child, height_Rect(innerRect_Widget_(d)));
                    }
                    else if (d->flags & arrangeVertical_WidgetFlag) {
                        if (dirs.x) setWidth_Widget_(child, width_Rect(innerRect_Widget_(d)));
                        if (dirs.y) setHeight_Widget_(child, avail.y);
                    }
                }
                else {
#endif
                    /* Fill the off axis, though. */
                    if (d->flags & arrangeHorizontal_WidgetFlag) {
                        if (dirs.y) setHeight_Widget_(child, height_Rect(innerRect_Widget_(d)));
                    }
                    else if (d->flags & arrangeVertical_WidgetFlag) {
                        if (dirs.x) setWidth_Widget_(child, width_Rect(innerRect_Widget_(d)));
                    }
                }
            }
            TRACE(d, "...done changing child sizes");
        }
        else {
            /* Evenly size all children. */
            iInt2 childSize = innerRect_Widget_(d).size;
            iInt2 unpaddedChildSize = d->rect.size;
            if (d->flags & arrangeHorizontal_WidgetFlag) {
                childSize.x /= childCount;
                unpaddedChildSize.x /= childCount;
            }
            else if (d->flags & arrangeVertical_WidgetFlag) {
                childSize.y /= childCount;
                unpaddedChildSize.y /= childCount;
            }
            TRACE(d, "begin changing child sizes (EVEN mode)...");
            iForEach(ObjectList, i, d->children) {
                iWidget *child = as_Widget(i.object);
                if (isArrangedSize_Widget_(child)) {
                    if (dirs.x) {
                        setWidth_Widget_(child, child->flags & unpadded_WidgetFlag ? unpaddedChildSize.x : childSize.x);
                    }
                    if (dirs.y && ~child->flags & parentCannotResizeHeight_WidgetFlag) {
                        setHeight_Widget_(child, child->flags & unpadded_WidgetFlag ? unpaddedChildSize.y : childSize.y);
                    }
                }
                else {
                    TRACE(d, "child %p cannot be resized (collapsed: %d, arrangedPos: %d, parentCannotResize: %d)", child,
                          isCollapsed_Widget_(child),
                          isArrangedPos_Widget_(child),
                          (child->flags & parentCannotResize_WidgetFlag) != 0);
                }
            }
            TRACE(d, "...done changing child sizes (EVEN mode)");
        }
    }
    /* Children arrange themselves. */ {
        iForEach(ObjectList, i, d->children) {
            iWidget *child = as_Widget(i.object);
            arrange_Widget_(child);
        }
    }
    /* Resize the expanding children to fill the remaining available space. */
    if (expCount > 0 && (d->flags & (arrangeHorizontal_WidgetFlag | arrangeVertical_WidgetFlag))) {
        TRACE(d, "%d expanding children, resizing them %s...", expCount,
              d->flags & arrangeHorizontal_WidgetFlag ? "horizontally" : "vertically");
        const iRect innerRect = innerRect_Widget_(d);
        iInt2 avail = innerRect.size;
        iConstForEach(ObjectList, i, d->children) {
            const iWidget *child = constAs_Widget(i.object);
            if (doesAffectSizing_Widget_(child)) {
                if (~child->flags & expand_WidgetFlag) {
                    subv_I2(&avail, child->rect.size);
                }
            }
        }
        /* Keep track of the fractional pixels so a large number to children will cover 
           the full area. */
        const iInt2 totalAvail = avail;
        avail = divi_I2(max_I2(zero_I2(), avail), expCount);
        float availFract[2] = { 
            iMax(0, (totalAvail.x - avail.x * expCount) / (float) expCount),
            iMax(0, (totalAvail.y - avail.y * expCount) / (float) expCount)
        };
        TRACE(d, "available for expansion (per child): %d\n", d->flags & arrangeHorizontal_WidgetFlag ? avail.x : avail.y);
        float fract[2] = { 0, 0 };
        iForEach(ObjectList, j, d->children) {
            iWidget *child = as_Widget(j.object);
            if (!isArrangedSize_Widget_(child)) {
                TRACE(d, "child %p size is not arranged", child);
                continue;
            }
            iBool sizeChanged = iFalse;
            if (child->flags & expand_WidgetFlag) {
                if (d->flags & arrangeHorizontal_WidgetFlag) {
                    const int fracti = (int) (fract[0] += availFract[0]);
                    fract[0] -= fracti;
                    sizeChanged |= setWidth_Widget_(child, avail.x + fracti);
                    sizeChanged |= setHeight_Widget_(child, height_Rect(innerRect));
                }
                else if (d->flags & arrangeVertical_WidgetFlag) {
                    sizeChanged |= setWidth_Widget_(child, width_Rect(innerRect));
                    const int fracti = (int) (fract[1] += availFract[1]);
                    fract[1] -= fracti;
                    sizeChanged |= setHeight_Widget_(child, avail.y + fracti);
                }
            }
            if (sizeChanged) {
                arrange_Widget_(child); /* its children may need rearranging */
            }
        }
    }
    if (d->flags & resizeChildrenToWidestChild_WidgetFlag) {
        const int widest = widestChild_Widget_(d);
        TRACE(d, "resizing children to widest child (%d)...", widest);
        iForEach(ObjectList, i, d->children) {
            iWidget *child = as_Widget(i.object);
            if (isArrangedSize_Widget_(child)) {
                if (setWidth_Widget_(child, widest)) {
                    arrange_Widget_(child); /* its children may need rearranging */
                }
            }
            else {
                TRACE(d, "child %p cannot be resized (parentCannotResize: %d)", child,
                      (child->flags & parentCannotResize_WidgetFlag) != 0);
            }
        }
        TRACE(d, "...done resizing children to widest child");
    }
    iInt2 pos = initv_I2(d->padding);
    TRACE(d, "begin positioning children from %d,%d (flags:%s%s)...", pos.x, pos.y,
          d->flags & arrangeHorizontal_WidgetFlag ? " horiz" : "",
          d->flags & arrangeVertical_WidgetFlag ? " vert" : "");
    iForEach(ObjectList, i, d->children) {
        iWidget *child = as_Widget(i.object);
        if (isCollapsed_Widget_(child) || !isArrangedPos_Widget_(child)) {
            TRACE(d, "child %p arranging prohibited", child);
            continue;
        }
        if (child->flags & centerHorizontal_WidgetFlag) {
            TRACE(d, "child %p is centered, skipping", child);
            continue;
        }
        if (d->flags & (arrangeHorizontal_WidgetFlag | arrangeVertical_WidgetFlag)) {
            if (child->flags &
                (moveToParentLeftEdge_WidgetFlag | moveToParentRightEdge_WidgetFlag)) {
                TRACE(d, "child %p is attached an edge, skipping", child);
                continue; /* Not part of the sequential arrangement .*/
            }
            child->rect.pos = pos;
            TRACE(d, "child %p set position to %d,%d", child, pos.x, pos.y);
            if (d->flags & arrangeHorizontal_WidgetFlag) {
                pos.x += child->rect.size.x;
            }
            else {
                pos.y += child->rect.size.y;
            }
        }
        else if ((d->flags & resizeChildren_WidgetFlag) == resizeChildren_WidgetFlag &&
                 ~child->flags & moveToParentBottomEdge_WidgetFlag) {
            child->rect.pos = pos;
            TRACE(d, "child %p set position to %d,%d (not sequential, children being resized)", child, pos.x, pos.y);
        }
        else if (d->flags & resizeWidthOfChildren_WidgetFlag) {
            child->rect.pos.x = pos.x;
            TRACE(d, "child %p set X to %d (not sequential, children being resized)", child, pos.x);
        }
    }
    /* Center children vertically inside a known parent height. */
    if (childCount &&
        d->flags2 & centerChildrenVertical_WidgetFlag2 &&
        ~d->flags & arrangeHeight_WidgetFlag) {
        /* Move children down to be in the center. */
        const int top    = d->padding[1];
        const int bottom = pos.y;
        const int extra  = bottom_Rect(innerRect_Widget_(d)) - bottom - top;
        iForEach(ObjectList, i, d->children) {
            iWidget *child = as_Widget(i.object);
            if (isCollapsed_Widget_(child) || !isArrangedPos_Widget_(child)) {
                continue;
            }
            child->rect.pos.y += extra / 2;
        }
    }
    TRACE(d, "...done positioning children");
    /* Update the size of the widget according to the arrangement. */
    if (d->flags & arrangeSize_WidgetFlag) {
        iRect bounds;
        boundsOfChildren_Widget_(d, &bounds);
        TRACE(d, "begin arranging own size; bounds of children: %d,%d %dx%d",
              bounds.pos.x, bounds.pos.y, bounds.size.x, bounds.size.y);
        adjustEdges_Rect(&bounds, -d->padding[1], d->padding[2], d->padding[3], -d->padding[0]);
        if (d->flags & arrangeWidth_WidgetFlag) {
            setWidth_Widget_(d, bounds.size.x);
            /* Parent size changed, must update the children.*/
            iForEach(ObjectList, j, d->children) {
                iWidget *child = as_Widget(j.object);
                if (child->flags &
                    (resizeToParentWidth_WidgetFlag |
                     moveToParentLeftEdge_WidgetFlag |
                     moveToParentRightEdge_WidgetFlag)) {
                    TRACE(d, "rearranging child %p because its size or position depends on parent width", child);
                    arrange_Widget_(child);
                }
            }
            if (d->flags & moveToParentRightEdge_WidgetFlag) {
                /* TODO: Fix this: not DRY. See beginning of method. */
                d->rect.pos.x = width_Rect(innerRect_Widget_(d->parent)) - width_Rect(d->rect);
                TRACE(d, "after width change moving to right edge of parent, set X to %d", d, d->rect.pos.x);
            }
        }
        if (d->flags & arrangeHeight_WidgetFlag) {
            setHeight_Widget_(d, bounds.size.y);
            /* Parent size changed, must update the children.*/
            iForEach(ObjectList, j, d->children) {
                iWidget *child = as_Widget(j.object);
                if (child->flags & (resizeToParentHeight_WidgetFlag |
                                    moveToParentBottomEdge_WidgetFlag)) {
                    TRACE(d, "rearranging child %p because its size or position depends on parent height", child);
                    arrange_Widget_(child);
                }
            }
        }
//        if (d->flags & moveToParentBottomEdge_WidgetFlag) {
//            /* TODO: Fix this: not DRY. See beginning of method. */
//            d->rect.pos.y = height_Rect(innerRect_Widget_(d->parent)) - height_Rect(d->rect);
//        }
        if (d->flags & centerHorizontal_WidgetFlag) {
            centerHorizontal_Widget_(d);
        }
        TRACE(d, "...done arranging own size");
    }
    TRACE(d, "END");
}

static void resetArrangement_Widget_(iWidget *d) {
    d->oldSize = d->rect.size;
    if (d->flags & resizeToParentWidth_WidgetFlag) {
        d->rect.size.x = 0;
    }
    if (d->flags & resizeToParentHeight_WidgetFlag) {
        d->rect.size.y = 0;
    }
    iForEach(ObjectList, i, children_Widget(d)) {
        iWidget *child = as_Widget(i.object);
        resetArrangement_Widget_(child);
        if (isArrangedPos_Widget_(child)) {
            if (d->flags & arrangeHorizontal_WidgetFlag) {
                child->rect.pos.x = 0;
            }
            if (d->flags & resizeWidthOfChildren_WidgetFlag && child->flags & expand_WidgetFlag &&
                ~child->flags & fixedWidth_WidgetFlag) {
                child->rect.size.x = 0;
            }
            if (d->flags & resizeChildrenToWidestChild_WidgetFlag) {
                if (isInstance_Object(child, &Class_LabelWidget)) {
                    updateSize_LabelWidget((iLabelWidget *) child);
                }
                else {
                    child->rect.size.x = 0;
                }
            }
            if (d->flags & arrangeVertical_WidgetFlag) {
                child->rect.pos.y = 0;
            }
            if (d->flags & resizeHeightOfChildren_WidgetFlag && child->flags & expand_WidgetFlag &&
                ~child->flags & fixedHeight_WidgetFlag) {
                child->rect.size.y = 0;
            }
        }
    }
}

static void notifyArrangement_Widget_(iWidget *d) {
    if (d->flags & destroyPending_WidgetFlag) {
        return;
    }
    if (class_Widget(d)->sizeChanged && !isEqual_I2(d->rect.size, d->oldSize)) {
        class_Widget(d)->sizeChanged(d);
    }
    iForEach(ObjectList, child, d->children) {
        notifyArrangement_Widget_(child.object);
    }
}

static void clampCenteredInRoot_Widget_(iWidget *d) {
    /* When arranging, we don't yet know if centered widgets will end up outside the root
       area, because the parent sizes and positions may change. */
    if (d->flags & centerHorizontal_WidgetFlag) {
        iRect rootRect = safeRect_Root(d->root);
        iRect bounds = boundsWithoutVisualOffset_Widget(d);
        if (width_Rect(bounds) <= width_Rect(rootRect)) {
            int excess = left_Rect(rootRect) - left_Rect(bounds);
            if (excess > 0) {
                d->rect.pos.x += excess;
            }
            excess = right_Rect(bounds) - right_Rect(rootRect);
            if (excess > 0) {
                d->rect.pos.x -= excess;
            }
        }
    }
    iForEach(ObjectList, i, d->children) {
        clampCenteredInRoot_Widget_(i.object);
    }
}

void arrange_Widget(iWidget *d) {
    if (d) {
#if !defined (NDEBUG)
        if (tracing_) {
            puts("\n==== NEW WIDGET ARRANGEMENT ====\n");
        }
#endif
        resetArrangement_Widget_(d); /* back to initial default sizes */
        arrange_Widget_(d);
        clampCenteredInRoot_Widget_(d);
        notifyArrangement_Widget_(d);
        d->root->didChangeArrangement = iTrue;
        if (type_Window(window_Widget(d)) == extra_WindowType &&
            (d == root_Widget(d) || d->parent == root_Widget(d))) {
            /* Size of extra windows will change depending on the contents. */
            iWindow *win = window_Widget(d);
            SDL_SetWindowSize(win->win,
                              width_Widget(d) / win->pixelRatio,
                              height_Widget(d) / win->pixelRatio);
            win->size = d->rect.size;
        }
    }
}

iBool isBeingVisuallyOffsetByReference_Widget(const iWidget *d) {
    return visualOffsetByReference_Widget(d) != 0;
}

int visualOffsetByReference_Widget(const iWidget *d) {
    if (d->offsetRef && d->flags & refChildrenOffset_WidgetFlag) {
        int offX = 0;
        iConstForEach(ObjectList, i, children_Widget(d->offsetRef)) {
            const iWidget *child = i.object;
            if (child == d) continue;
            if (child->flags & (visualOffset_WidgetFlag | dragged_WidgetFlag)) {
//                const float factor = width_Widget(d) / (float) size_Root(d->root).x;
                const int invOff = width_Widget(d) - iRound(value_Anim(&child->visualOffset));
                offX -= invOff / 4;
#if 0
                if (invOff) {
                    printf("  [%p] %s (%p, fin:%d visoff:%d drag:%d): invOff %d\n", d, cstr_String(&child->id), child,
                           isFinished_Anim(&child->visualOffset),
                           (child->flags & visualOffset_WidgetFlag) != 0,
                           (child->flags & dragged_WidgetFlag) != 0, invOff); fflush(stdout);
                }
#endif
            }
        }
        return offX;
    }
    return 0;
}

static void applyVisualOffset_Widget_(const iWidget *d, iInt2 *pos) {
    if (d->flags & (visualOffset_WidgetFlag | dragged_WidgetFlag) ||
        (d->flags2 & permanentVisualOffset_WidgetFlag2)) {
        const int off = iRound(value_Anim(&d->visualOffset));
        if (d->flags & horizontalOffset_WidgetFlag) {
            pos->x += off;
        }
        else {
            pos->y += off;
        }
    }
    if (d->flags & refChildrenOffset_WidgetFlag) {
        pos->x += visualOffsetByReference_Widget(d);
    }
}

iRect bounds_Widget(const iWidget *d) {
    iRect bounds = d->rect;
    bounds.pos = localToWindow_Widget(d, bounds.pos);
    return bounds;
}

iInt2 localToWindow_Widget(const iWidget *d, iInt2 localCoord) {
    iInt2 window = localCoord;
    applyVisualOffset_Widget_(d, &window);
    for (const iWidget *w = d->parent; w; w = w->parent) {
        iInt2 pos = w->rect.pos;
        applyVisualOffset_Widget_(w, &pos);
        addv_I2(&window, pos);
    }
    return window;
}

iInt2 windowToLocal_Widget(const iWidget *d, iInt2 windowCoord) {
    iInt2 local = windowCoord;
    for (const iWidget *w = d->parent; w; w = w->parent) {
        subv_I2(&local, w->rect.pos);
    }
    return local;
}

iRect boundsWithoutVisualOffset_Widget(const iWidget *d) {
    iRect bounds = d->rect;
    for (const iWidget *w = d->parent; w; w = w->parent) {
        addv_I2(&bounds.pos, w->rect.pos);
    }
    return bounds;
}

iInt2 innerToWindow_Widget(const iWidget *d, iInt2 innerCoord) {
    for (const iWidget *w = d; w; w = w->parent) {
        iInt2 pos = w->rect.pos;
        applyVisualOffset_Widget_(w, &pos);
        addv_I2(&innerCoord, pos);
    }
    return innerCoord;
}

iInt2 windowToInner_Widget(const iWidget *d, iInt2 windowCoord) {
    for (const iWidget *w = d; w; w = w->parent) {
        subv_I2(&windowCoord, w->rect.pos);
    }
    return windowCoord;
}

iBool contains_Widget(const iWidget *d, iInt2 windowCoord) {
    return containsExpanded_Widget(d, windowCoord, 0);
}

iBool containsExpanded_Widget(const iWidget *d, iInt2 windowCoord, int expand) {
    iRect bounds = {
        innerToWindow_Widget(d, zero_I2()),
        addY_I2(d->rect.size,
                d->flags & drawBackgroundToBottom_WidgetFlag ? size_Root(d->root).y : 0)
    };
    return contains_Rect(expand ? expanded_Rect(bounds, init1_I2(expand)) : bounds,
                         windowCoord);
}

iLocalDef iBool isKeyboardEvent_(const SDL_Event *ev) {
    return (ev->type == SDL_KEYUP || ev->type == SDL_KEYDOWN || ev->type == SDL_TEXTINPUT);
}

iLocalDef iBool isMouseEvent_(const SDL_Event *ev) {
    return (ev->type == SDL_MOUSEWHEEL || ev->type == SDL_MOUSEMOTION ||
            ev->type == SDL_MOUSEBUTTONUP || ev->type == SDL_MOUSEBUTTONDOWN);
}

iLocalDef iBool isHidden_Widget_(const iWidget *d) {
    if (d->flags & visibleOnParentHover_WidgetFlag &&
        (isHover_Widget(d) || isHover_Widget(d->parent))) {
        return iFalse;
    }
    if (d->flags2 & visibleOnParentSelected_WidgetFlag2 && isSelected_Widget(d->parent)) {
        return iFalse;
    }
    return (d->flags & hidden_WidgetFlag) != 0;
}

iLocalDef iBool isDrawn_Widget_(const iWidget *d) {
    return !isHidden_Widget_(d) || (d->flags & visualOffset_WidgetFlag &&
                                    ~d->flags2 & permanentVisualOffset_WidgetFlag2);
}

static iBool filterEvent_Widget_(const iWidget *d, const SDL_Event *ev) {
    if (d->flags & destroyPending_WidgetFlag) {
        /* Only allow cleanup while waiting for destruction. */
        return isCommand_UserEvent(ev, "focus.lost");
    }   
    const iBool isKey   = isKeyboardEvent_(ev);
    const iBool isMouse = isMouseEvent_(ev);
    if ((d->flags & disabled_WidgetFlag) || (isHidden_Widget_(d) &&
                                             d->flags & disabledWhenHidden_WidgetFlag)) {
        if (isKey || isMouse) return iFalse;
    }
    if (isHidden_Widget_(d)) {
        if (isMouse) return iFalse;
    }
    return iTrue;
}

void unhover_Widget(void) {
    iWidget **hover = &get_Window()->hover;
    if (*hover) {
        refresh_Widget(*hover);
    }
    *hover = NULL;
}

iLocalDef iBool redispatchEvent_Widget_(iWidget *d, iWidget *dst, const SDL_Event *ev) {
    if (d != dst) {
        return dispatchEvent_Widget(dst, ev);
    }
    return iFalse;
}

iBool dispatchEvent_Widget(iWidget *d, const SDL_Event *ev) {
    if (!d->parent) {
        if (window_Widget(d)->focus && window_Widget(d)->focus->root == d->root &&
            (isKeyboardEvent_(ev) || ev->type == SDL_USEREVENT)) {
            /* Root dispatches keyboard events directly to the focused widget. */
            if (redispatchEvent_Widget_(d, window_Widget(d)->focus, ev)) {
                return iTrue;
            }
        }
        /* Root offers events first to widgets on top. */
        iReverseForEach(PtrArray, i, d->root->onTop) {
            iWidget *widget = *i.value;
            if (isVisible_Widget(widget) && redispatchEvent_Widget_(d, widget, ev)) {
#if 0
                if (ev->type == SDL_TEXTINPUT) {
                    printf("[%p] %s:'%s' (on top) ate text input\n",
                           widget, class_Widget(widget)->name,
                           cstr_String(id_Widget(widget)));
                    fflush(stdout);
                }
#endif
#if 0
                if (ev->type == SDL_KEYDOWN) {
                    printf("[%p] %s:'%s' (on top) ate the key\n",
                           widget, class_Widget(widget)->name,
                           cstr_String(id_Widget(widget)));
                    fflush(stdout);
                }
#endif
#if 0
                if (ev->type == SDL_MOUSEBUTTONDOWN) {
                    printf("[%p] %s:'%s' (on top) ate the button\n",
                           widget, class_Widget(widget)->name,
                           cstr_String(id_Widget(widget)));
                    fflush(stdout);
                }
#endif
#if 0
                if (ev->type == SDL_MOUSEMOTION) {
                    printf("[%p] %s:'%s' (on top) ate the motion\n",
                           widget, class_Widget(widget)->name,
                           cstr_String(id_Widget(widget)));
                    fflush(stdout);
                }
#endif
                return iTrue;
            }
        }
    }
    else if (ev->type == SDL_MOUSEMOTION &&
             ev->motion.windowID == id_Window(window_Widget(d)) &&
             (!window_Widget(d)->hover || hasParent_Widget(d, window_Widget(d)->hover)) &&
             flags_Widget(d) & hover_WidgetFlag && !isHidden_Widget_(d) &&
             ~flags_Widget(d) & disabled_WidgetFlag) {
        if (contains_Widget(d, init_I2(ev->motion.x, ev->motion.y))) {
            setHover_Widget(d);
#if 0
            printf("set hover to [%p] %s:'%s'\n",
                   d, class_Widget(d)->name,
                   cstr_String(id_Widget(d)));
            fflush(stdout);
#endif
        }
    }
    if (filterEvent_Widget_(d, ev)) {
        /* Children may handle it first. Done in reverse so children drawn on top get to
           handle the events first. */
        iReverseForEach(ObjectList, i, d->children) {
            iWidget *child = as_Widget(i.object);
            iAssert(child != d); /* cannot be child of self */
            iAssert(child->root == d->root);
            if (child == window_Widget(d)->focus &&
                (isKeyboardEvent_(ev) || ev->type == SDL_USEREVENT)) {
                continue; /* Already dispatched. */
            }
            if (isVisible_Widget(child) && child->flags & keepOnTop_WidgetFlag) {
                /* Already dispatched. */
                continue;
            }
            if (dispatchEvent_Widget(child, ev)) {
#if 0
                if (ev->type == SDL_TEXTINPUT) {
                    printf("[%p] %s:'%s' ate text input\n",
                           child, class_Widget(child)->name,
                           cstr_String(id_Widget(child)));
                    fflush(stdout);
                }
#endif
#if 0
                if (ev->type == SDL_KEYDOWN) {
                    printf("[%p] %s:'%s' ate the key\n",
                           child, class_Widget(child)->name,
                           cstr_String(id_Widget(child)));
                    identify_Widget(child);
                    fflush(stdout);
                }
#endif
#if 0
                if (ev->type == SDL_MOUSEMOTION) {
                    printf("[%p] %s:'%s' ate the motion\n",
                           child, class_Widget(child)->name,
                           cstr_String(id_Widget(child)));
                    fflush(stdout);
                }
#endif
#if 0
                if (ev->type == SDL_MOUSEWHEEL) {
                    printf("[%p] %s:'%s' ate the wheel\n",
                           child, class_Widget(child)->name,
                           cstr_String(id_Widget(child)));
                    fflush(stdout);
                }
#endif
#if 0
                if (ev->type == SDL_MOUSEBUTTONDOWN) {
                    printf("widget %p ('%s' class:%s) ate the mouse down\n",
                           child, cstr_String(id_Widget(child)),
                           class_Widget(child)->name);
                    fflush(stdout);
                }
#endif
                return iTrue;
            }
        }
        //iAssert(get_Root() == d->root);
        if (class_Widget(d)->processEvent(d, ev)) {
            //iAssert(get_Root() == d->root);
            return iTrue;
        }
    }
    //iAssert(get_Root() == d->root);
    return iFalse;
}

void scrollInfo_Widget(const iWidget *d, iWidgetScrollInfo *info) {
    iRect       bounds  = boundsWithoutVisualOffset_Widget(d);
    iRect       visBounds = bounds_Widget(d);
    const iRect winRect = adjusted_Rect(safeRect_Root(d->root),
                                        zero_I2(),
                                        init_I2(0, -get_MainWindow()->keyboardHeight));
    info->height      = bounds.size.y;
    info->avail       = height_Rect(winRect);
    if (info->avail >= info->height) {
        info->normScroll  = 0.0f;
        info->thumbY      = 0;
        info->thumbHeight = 0;
    }
    else {
        int scroll        = top_Rect(winRect) - top_Rect(bounds);
        info->normScroll  = scroll / (float) (info->height - info->avail);
        info->normScroll  = iClamp(info->normScroll, 0.0f, 1.0f);
        info->thumbHeight = iMin(info->avail / 2, info->avail * info->avail / info->height);
        info->thumbY      = top_Rect(winRect) + (info->avail - info->thumbHeight) * info->normScroll;
        /* Clamp it. */
        const iRangei ySpan = ySpan_Rect(visBounds);
        if (info->thumbY < ySpan.start) {
            info->thumbHeight += info->thumbY - ySpan.start;
            info->thumbY = ySpan.start;
            info->thumbHeight = iMax(7 * gap_UI, info->thumbHeight);
        }
        else if (info->thumbY + info->thumbHeight > ySpan.end) {
            info->thumbHeight = ySpan.end - info->thumbY;
        }
    }
}

static iBool isOverflowScrollPossible_Widget_(const iWidget *d, int delta) {
    if (~d->flags & overflowScrollable_WidgetFlag) {
        return iFalse;
    }
    iRect       bounds  = boundsWithoutVisualOffset_Widget(d);
    const iRect winRect = visibleRect_Root(d->root);
    const int   yTop    = iMaxi(0, top_Rect(winRect));
    const int   yBottom = bottom_Rect(winRect);
    if (delta == 0) {
        if (top_Rect(bounds) >= yTop && bottom_Rect(bounds) <= yBottom) {
            return iFalse; /* fits inside just fine */
        }
    }
    else if (delta > 0) {
        return top_Rect(bounds) < yTop;
    }
    return bottom_Rect(bounds) > yBottom;
}

iBool scrollOverflow_Widget(iWidget *d, int delta) {
    if (!isOverflowScrollPossible_Widget_(d, delta)) {
        return iFalse;
    }
    iRect       bounds        = boundsWithoutVisualOffset_Widget(d);  
    const iRect winRect       = visibleRect_Root(d->root);
    /* TODO: This needs some fixing on mobile, probably. */
//    const int   yTop          = iMaxi(0, top_Rect(winRect));
//    const int   yBottom       = bottom_Rect(winRect);
    iRangei     validPosRange = { bottom_Rect(winRect) - height_Rect(bounds),
                                  iMaxi(0, top_Rect(winRect)) };
//    if (validPosRange.end < validPosRange.start) {
//        validPosRange.end = validPosRange.start; /* no room to scroll */
//    }
//    if ((!isTopOver && delta < 0) || (!isBottomOver && delta > 0)) {
//        delta = 0;
//    }
    if (delta) {
//        if (delta < 0 && bounds.pos.y < validPosRange.start) {
//            delta = 0;
//        }
//        if (delta > 0 && bounds.pos.y > validPosRange.end) {
//            delta = 0;
//        }
//        printf("delta:%d  validPosRange:%d...%d\n", delta, validPosRange.start, validPosRange.end); fflush(stdout);
        bounds.pos.y += delta;
        if (delta < 0) {
            bounds.pos.y = iMax(bounds.pos.y, validPosRange.start);
        }
        else if (delta > 0) {
            bounds.pos.y = iMin(bounds.pos.y, validPosRange.end);
        }
        if (delta) {
            d->root->didChangeArrangement = iTrue; /* ensure that widgets update if needed */
        }
    }
    else {
        /* TODO: This is used on mobile. */
        
//        printf("clamping validPosRange:%d...%d\n", validPosRange.start, validPosRange.end); fflush(stdout);
//        bounds.pos.y = iClamp(bounds.pos.y, validPosRange.start, validPosRange.end);
    }
    const iInt2 newPos = windowToInner_Widget(d->parent, bounds.pos);
    if (!isEqual_I2(newPos, d->rect.pos)) {
        d->rect.pos = newPos;
        postRefresh_App();
    }
    return height_Rect(bounds) > height_Rect(winRect);
}

static uint32_t lastHoverOverflowMotionTime_;

static void overflowHoverAnimation_(iAny *widget) {
    iWindow *win = window_Widget(widget);
    iInt2 coord = mouseCoord_Window(win, 0);
    /* A motion event will cause an overflow window to scroll. */
    SDL_MouseMotionEvent ev = {
        .type     = SDL_MOUSEMOTION,
        .windowID = SDL_GetWindowID(win->win),
        .x        = coord.x / win->pixelRatio,
        .y        = coord.y / win->pixelRatio,
    };
    SDL_PushEvent((SDL_Event *) &ev);
}

static void unfadeOverflowScrollIndicator_Widget_(iWidget *d) {
    remove_Periodic(periodic_App(), d);
    add_Periodic(periodic_App(), d, format_CStr("overflow.fade time:%u ptr:%p", SDL_GetTicks(), d));
    setValue_Anim(&d->overflowScrollOpacity, 1.0f, 70);
    animateOverflowScrollOpacity_Widget_(d);    
}

iBool processEvent_Widget(iWidget *d, const SDL_Event *ev) {
    if (d->flags & commandOnClick_WidgetFlag &&
             (ev->type == SDL_MOUSEBUTTONDOWN || ev->type == SDL_MOUSEBUTTONUP) &&
             (mouseGrab_Widget() == d || contains_Widget(d, init_I2(ev->button.x, ev->button.y)))) {
        postCommand_Widget(d,
                           "mouse.clicked arg:%d button:%d coord:%d %d",
                           ev->type == SDL_MOUSEBUTTONDOWN ? 1 : 0,
                           ev->button.button,
                           ev->button.x,
                           ev->button.y);
        return iTrue;
    }
    else if (d->flags & commandOnClick_WidgetFlag &&
             mouseGrab_Widget() == d && ev->type == SDL_MOUSEMOTION) {
        postCommand_Widget(d, "mouse.moved coord:%d %d", ev->motion.x, ev->motion.y);
        return iTrue;
    }
    else if (d->flags & overflowScrollable_WidgetFlag && ~d->flags & visualOffset_WidgetFlag) {
        if (ev->type == SDL_MOUSEWHEEL && !ev->wheel.x) {
            int step = ev->wheel.y;
            if (!isPerPixel_MouseWheelEvent(&ev->wheel)) {
                step *= lineHeight_Text(uiLabel_FontId);
            }
            if (scrollOverflow_Widget(d, step)) {
                unfadeOverflowScrollIndicator_Widget_(d);
                return iTrue;
            }
        }
        else if (ev->type == SDL_MOUSEMOTION && ev->motion.which != SDL_TOUCH_MOUSEID &&
                 ev->motion.y >= 0) {
            /* TODO: Motion events occur frequently. Maybe it would help if these were handled
               via audiences that specifically register to listen for motion, to minimize the
               number of widgets that need to process them. */
            const int hoverScrollLimit = 3.0f * lineHeight_Text(default_FontId);
            float speed = 0.0f;
            if (ev->motion.y < hoverScrollLimit) {
                speed = (hoverScrollLimit - ev->motion.y) / (float) hoverScrollLimit;
            }
            else {
                const iWindow *win = window_Widget(d);
                //SDL_Rect usable;
                //SDL_GetDisplayUsableBounds(SDL_GetWindowDisplayIndex(win->win),
                //                           &usable);
                const int bottomLimit =
                /*iMin(*/ bottom_Rect(visibleRect_Root(d->root)) /*, usable.h * win->pixelRatio) */
                    - hoverScrollLimit;
                if (ev->motion.y > bottomLimit) {
                    speed = -(ev->motion.y - bottomLimit) / (float) hoverScrollLimit;
                }
            }
            const int dir = speed > 0 ? 1 : -1;
            if (speed != 0.0f && isOverflowScrollPossible_Widget_(d, dir)) {
//                speed = dir * powf(speed, 1.5f);
                const uint32_t nowTime = SDL_GetTicks();
                uint32_t elapsed = nowTime - lastHoverOverflowMotionTime_;
                if (elapsed > 100) {
                    elapsed = 16;    
                }
                int step = elapsed * gap_UI / 8 * iClamp(speed, -1.0f, 1.0f);
                if (step != 0) { 
                    lastHoverOverflowMotionTime_ = nowTime;
                    scrollOverflow_Widget(d, step);
                    unfadeOverflowScrollIndicator_Widget_(d);
                }
                addTicker_App(overflowHoverAnimation_, d);
            }
        }
    }
    switch (ev->type) {
        case SDL_USEREVENT: {
            if (d->flags & overflowScrollable_WidgetFlag &&
                ~d->flags & visualOffset_WidgetFlag &&
                isCommand_UserEvent(ev, "widget.overflow")) {
                scrollOverflow_Widget(d, 0); /* check bounds */
            }
            if (ev->user.code == command_UserEventCode) {
                const char *cmd = command_UserEvent(ev);
                if (d->drawBuf && equal_Command(cmd, "theme.changed")) {
                    d->drawBuf->isValid = iFalse;
                }
                else if (equalWidget_Command(cmd, d, "overflow.fade")) {
                    if (SDL_GetTicks() - argLabel_Command(cmd, "time") > 750) {
                        remove_Periodic(periodic_App(), d);
                        setValue_Anim(&d->overflowScrollOpacity, 0, 200);
                        animateOverflowScrollOpacity_Widget_(d);
                    }
                    return iTrue;
                }
                if (d->flags & (leftEdgeDraggable_WidgetFlag | rightEdgeDraggable_WidgetFlag) &&
                    isVisible_Widget(d) && ~d->flags & disabled_WidgetFlag &&
                    equal_Command(cmd, "edgeswipe.moved")) {
                    if (!prefs_App()->edgeSwipe && argLabel_Command(cmd, "edge")) {
                        return iTrue; /* edge swiping should be ignored */
                    }
                    /* Check the side. */
                    const int side = argLabel_Command(cmd, "side");
                    if ((side == 1 && d->flags & leftEdgeDraggable_WidgetFlag) ||
                        (side == 2 && d->flags & rightEdgeDraggable_WidgetFlag)) {
                        if (~d->flags & dragged_WidgetFlag) {
                            setFlags_Widget(d, dragged_WidgetFlag, iTrue);
                        }
                        setVisualOffset_Widget(d, arg_Command(command_UserEvent(ev)) *
                                               width_Widget(d) / size_Root(d->root).x,
                                               10, 0);
                        return iTrue;
                    }
                }
                if (d->flags & dragged_WidgetFlag && equal_Command(cmd, "edgeswipe.ended")) {
                    if (argLabel_Command(cmd, "abort")) {
                        setVisualOffset_Widget(d, 0, 200, easeOut_AnimFlag);
                    }
                    else {
                        postCommand_Widget(
                            d, argLabel_Command(cmd, "side") == 1 ? "swipe.back" : "swipe.forward");
                        /* Something will happen soon as a result of the finished swipe, so
                           don't deactivate the offset like normally would happen after the
                           animation ends. (A 10 ms animation was started above.) */
                        removeTicker_App(visualOffsetAnimation_Widget_, d);
                        d->flags |= visualOffset_WidgetFlag;
                    }
                    setFlags_Widget(d, dragged_WidgetFlag, iFalse);
                    return iTrue;
                }
                if (d->commandHandler && d->commandHandler(d, ev->user.data1)) {
                    return iTrue;
                }
            }
            break;
        }
    }
    if (d->flags & commandOnMouseMiss_WidgetFlag && ev->type == SDL_MOUSEBUTTONDOWN &&
        !contains_Widget(d, init_I2(ev->button.x, ev->button.y))) {
        postCommand_Widget(d,
                           "mouse.missed arg:%d button:%d coord:%d %d",
                           ev->type == SDL_MOUSEBUTTONDOWN ? 1 : 0,
                           ev->button.button,
                           ev->button.x,
                           ev->button.y);
        return iTrue;
    }
    if (d->flags & mouseModal_WidgetFlag && isMouseEvent_(ev) &&
        contains_Rect(rect_Root(d->root), mouseCoord_SDLEvent(ev))) {
        if ((ev->type == SDL_MOUSEBUTTONDOWN || ev->type == SDL_MOUSEBUTTONUP) &&
            d->flags & commandOnClick_WidgetFlag) {
            postCommand_Widget(d,
                               "mouse.clicked arg:%d button:%d coord:%d %d",
                               ev->type == SDL_MOUSEBUTTONDOWN ? 1 : 0,
                               ev->button.button,
                               ev->button.x,
                               ev->button.y);
        }
        setCursor_Window(window_Widget(d), SDL_SYSTEM_CURSOR_ARROW);
        return iTrue;
    }
    return iFalse;
}

int backgroundFadeColor_Widget(void) {
    switch (colorTheme_App()) {
        case light_ColorTheme:
            return gray25_ColorId;
        case pureWhite_ColorTheme:
            return gray50_ColorId;
        default:
            return black_ColorId;
    }
}

void drawLayerEffects_Widget(const iWidget *d) {
    /* Layered effects are not buffered, so they are drawn here separately. */
    iAssert(isDrawn_Widget_(d));
    iAssert(window_Widget(d) == get_Window());
    iBool shadowBorder   = (d->flags & keepOnTop_WidgetFlag && ~d->flags & mouseModal_WidgetFlag) != 0;
    iBool fadeBackground = (d->bgColor >= 0 || d->frameColor >= 0) && d->flags & mouseModal_WidgetFlag;
    if (deviceType_App() == phone_AppDeviceType) {
        if (shadowBorder) {
            fadeBackground = iTrue;
            shadowBorder = iFalse;
        }
    }
    const iBool isFaded = (fadeBackground && ~d->flags & noFadeBackground_WidgetFlag) ||
                          (d->flags2 & fadeBackground_WidgetFlag2);
    if (shadowBorder && ~d->flags & noShadowBorder_WidgetFlag) {
        iPaint p;
        init_Paint(&p);
        drawSoftShadow_Paint(&p, bounds_Widget(d), 12 * gap_UI, black_ColorId, 30);
    }
    if (isFaded) {
        iPaint p;
        init_Paint(&p);
        p.alpha = 0x50;
        if (flags_Widget(d) & (visualOffset_WidgetFlag | dragged_WidgetFlag)) {
            const float area        = d->rect.size.x * d->rect.size.y;
            const float rootArea    = area_Rect(rect_Root(d->root));
            const float visibleArea = area_Rect(intersect_Rect(bounds_Widget(d), rect_Root(d->root)));
            if (isPortraitPhone_App() && !cmp_String(&d->id, "sidebar")) {
                p.alpha *= iClamp(visibleArea / rootArea * 2, 0.0f, 1.0f);
            }
            else if (area > 0) {
                p.alpha *= visibleArea / area;
            }
            else {
                p.alpha = 0;
            }
            //printf("area:%f visarea:%f alpha:%d\n", rootArea, visibleArea, p.alpha);
        }
        SDL_SetRenderDrawBlendMode(renderer_Window(get_Window()), SDL_BLENDMODE_BLEND);
        fillRect_Paint(&p, rect_Root(d->root), backgroundFadeColor_Widget());
        SDL_SetRenderDrawBlendMode(renderer_Window(get_Window()), SDL_BLENDMODE_NONE);
    }
#if defined (iPlatformAppleMobile)
    if (d->bgColor >= 0 && d->flags & (drawBackgroundToHorizontalSafeArea_WidgetFlag |
                                       drawBackgroundToVerticalSafeArea_WidgetFlag)) {
        iPaint p;
        init_Paint(&p);
        const iRect rect     = bounds_Widget(d);
        const iInt2 rootSize = size_Root(d->root);
        const iInt2 center   = divi_I2(rootSize, 2);
        int top = 0, right = 0, bottom = 0, left = 0;
        if (d->flags & drawBackgroundToHorizontalSafeArea_WidgetFlag) {
            const iBool isWide = width_Rect(rect) > rootSize.x * 8 / 10;
            if (isWide || mid_Rect(rect).x < center.x) {
                left = -left_Rect(rect);
            }
            if (isWide || mid_Rect(rect).x > center.x) {
                right = rootSize.x - right_Rect(rect);
            }
        }
        if (top_Rect(rect) > center.y * 3 / 2) {
            bottom = rootSize.y - bottom_Rect(rect);
        }
        if (d->flags & drawBackgroundToVerticalSafeArea_WidgetFlag) {
            if (bottom_Rect(rect) < center.y / 2) {
                top = -top_Rect(rect);
            }
        }
        if (top < 0) {
            fillRect_Paint(&p, (iRect){ init_I2(left_Rect(rect), 0),
                                        init_I2(width_Rect(rect), top_Rect(rect)) }, d->bgColor);
        }
        if (left < 0) {
            fillRect_Paint(&p, (iRect){ init_I2(0, top_Rect(rect)),
                                        init_I2(left_Rect(rect), height_Rect(rect) + bottom) }, d->bgColor);
        }
        if (right > 0) {
            fillRect_Paint(&p, (iRect){ init_I2(right_Rect(rect), top_Rect(rect)),
                                        init_I2(right, height_Rect(rect) + bottom) }, d->bgColor);
        }
    }
#endif
}

void drawBorders_Widget(const iWidget *d) {
    if (d->flags & (borderTop_WidgetFlag | borderBottom_WidgetFlag)) {
        const iRect rect = bounds_Widget(d);
        iPaint p;
        init_Paint(&p);
        const int hgt = gap_UI / 4;
        const int borderColor = uiSeparator_ColorId; /* TODO: Add a property to customize? */
        if (d->flags & borderTop_WidgetFlag) {
            fillRect_Paint(&p, (iRect){ topLeft_Rect(rect),
                                        init_I2(width_Rect(rect), hgt) },
                            borderColor);
        }
        if (d->flags & borderBottom_WidgetFlag) {
            fillRect_Paint(&p, (iRect) { addY_I2(bottomLeft_Rect(rect), -hgt),
                                         init_I2(width_Rect(rect), hgt) },
                            borderColor);
        }
    }
}

void drawBackground_Widget(const iWidget *d) {
    if (d->flags & noBackground_WidgetFlag) {
        return;
    }
    if (!isDrawn_Widget_(d)) {
        return;
    }
    /* Popup menus have a shadowed border. */
    if (d->bgColor >= 0 || d->frameColor >= 0) {
        iRect rect = bounds_Widget(d);
        if (d->flags & drawBackgroundToBottom_WidgetFlag) {
            rect.size.y += size_Root(d->root).y;
        }
        iPaint p;
        init_Paint(&p);
        if (d->bgColor >= 0) {
            if (isTerminal_Platform() && d->bgColor == uiSeparator_ColorId && rect.size.y == 1) {
                fillRect_Paint(&p, adjusted_Rect(rect, zero_I2(), init_I2(0, -1)),
                               d->bgColor);
                return;
            }
            fillRect_Paint(&p, rect, d->bgColor);
        }
        if (d->frameColor >= 0 && ~d->flags & frameless_WidgetFlag) {
            drawRectThickness_Paint(&p, adjusted_Rect(rect, zero_I2(), neg_I2(one_I2())),
                                    gap_UI / 4, d->frameColor);
        }
    }
    drawBorders_Widget(d);
}

int drawCount_;

iLocalDef iBool isFullyContainedByOther_Rect(const iRect d, const iRect other) {
    if (isEmpty_Rect(other)) {
        /* Nothing is contained by empty. */
        return iFalse;
    }
    if (isEmpty_Rect(d)) {
        /* Empty is fully contained by anything. */
        return iTrue;
    }
    return equal_Rect(intersect_Rect(d, other), d);
}

static void addToPotentiallyVisible_Widget_(const iWidget *d, iPtrArray *pvs, iRect *fullyMasked) {
    if (isDrawn_Widget_(d)) {
        iRect bounds = bounds_Widget(d);
        if (d->flags & drawBackgroundToBottom_WidgetFlag) {
            bounds.size.y += size_Root(d->root).y;
        }
        if (isFullyContainedByOther_Rect(bounds, *fullyMasked)) {
            return; /* can't be seen */
        }
        pushBack_PtrArray(pvs, d);
        if (d->bgColor >= 0 && ~d->flags & noBackground_WidgetFlag &&
            isFullyContainedByOther_Rect(*fullyMasked, bounds)) {
            *fullyMasked = bounds;
        }
    }    
}

static void findPotentiallyVisible_Widget_(const iWidget *d, iPtrArray *pvs) {
    iRect fullyMasked = zero_Rect();
    if (isRoot_Widget_(d)) {
        iReverseConstForEach(PtrArray, i, onTop_Root(d->root)) {
            const iWidget *top = i.ptr;
            iAssert(top->parent);
            addToPotentiallyVisible_Widget_(top, pvs, &fullyMasked);
        }
    }
    iReverseConstForEach(ObjectList, i, d->children) {
        const iWidget *child = i.object;
        if (~child->flags & keepOnTop_WidgetFlag) {
            addToPotentiallyVisible_Widget_(child, pvs, &fullyMasked);
        }
    }
}

iLocalDef void incrementDrawCount_(const iWidget *d) {
    if (class_Widget(d) != &Class_Widget || d->bgColor >= 0 || d->frameColor >= 0) {
        drawCount_++;
    }
}

void drawChildren_Widget(const iWidget *d) {
    if (!isDrawn_Widget_(d)) {
        return;
    }
    iConstForEach(ObjectList, i, d->children) {
        const iWidget *child = constAs_Widget(i.object);
        if (~child->flags & keepOnTop_WidgetFlag && isDrawn_Widget_(child)) {
            incrementDrawCount_(child);
            class_Widget(child)->draw(child);
        }
    }
}

void drawRoot_Widget(const iWidget *d) {
    iAssert(d == d->root->widget);
    /* Root draws the on-top widgets on top of everything else. */
    iPtrArray pvs;
    init_PtrArray(&pvs);
    findPotentiallyVisible_Widget_(d, &pvs);
    iReverseConstForEach(PtrArray, i, &pvs) {
        incrementDrawCount_(i.ptr);
        class_Widget(i.ptr)->draw(i.ptr);
    }
    deinit_PtrArray(&pvs);
}

void setDrawBufferEnabled_Widget(iWidget *d, iBool enable) {
    if (enable && !d->drawBuf) {
        d->drawBuf = new_WidgetDrawBuffer();        
    }
    else if (!enable && d->drawBuf) {
        delete_WidgetDrawBuffer(d->drawBuf);
        d->drawBuf = NULL;
    }
}

static void beginBufferDraw_Widget_(const iWidget *d) {
    if (d->drawBuf) {
//        printf("[%p] drawbuffer update %d\n", d, d->drawBuf->isValid);
        if (d->drawBuf->isValid) {
            iAssert(!isEqual_I2(d->drawBuf->size, boundsForDraw_Widget_(d).size));
//            printf("  drawBuf:%dx%d boundsForDraw:%dx%d\n",
//                   d->drawBuf->size.x, d->drawBuf->size.y,
//                   boundsForDraw_Widget_(d).size.x,
//                   boundsForDraw_Widget_(d).size.y);
        }
        const iRect bounds = bounds_Widget(d);
        SDL_Renderer *render = renderer_Window(get_Window());
        d->drawBuf->oldTarget = SDL_GetRenderTarget(render);
        d->drawBuf->oldOrigin = origin_Paint;
        realloc_WidgetDrawBuffer(d->drawBuf, render, boundsForDraw_Widget_(d).size);
        SDL_SetRenderTarget(render, d->drawBuf->texture);
//        SDL_SetRenderDrawColor(render, 255, 0, 0, 128);
        SDL_SetRenderDrawColor(render, 0, 0, 0, 0);
        SDL_RenderClear(render);
        origin_Paint = neg_I2(bounds.pos); /* with current visual offset */
//        printf("beginBufferDraw: origin %d,%d\n", origin_Paint.x, origin_Paint.y);
//        fflush(stdout);
    }    
}

static void endBufferDraw_Widget_(const iWidget *d) {
    if (d->drawBuf) {
        d->drawBuf->isValid = iTrue;
        SDL_SetRenderTarget(renderer_Window(get_Window()), d->drawBuf->oldTarget);
        origin_Paint = d->drawBuf->oldOrigin;
//        printf("endBufferDraw: origin %d,%d\n", origin_Paint.x, origin_Paint.y);
//        fflush(stdout);
    }    
}

void draw_Widget(const iWidget *d) {
    iAssert(window_Widget(d) == get_Window());
    if (!isDrawn_Widget_(d)) {
        if (d->drawBuf) {
//            printf("[%p] drawBuffer released\n", d);
            release_WidgetDrawBuffer(d->drawBuf);
        }
        return;
    }
    drawLayerEffects_Widget(d);
    if (!d->drawBuf || !checkDrawBuffer_Widget_(d)) {
        beginBufferDraw_Widget_(d);
        drawBackground_Widget(d);
        drawChildren_Widget(d);
        endBufferDraw_Widget_(d);
    }
    if (d->drawBuf) {
        //iAssert(d->drawBuf->isValid);
        const iRect bounds = bounds_Widget(d);
        iPaint p;
        init_Paint(&p);
        setClip_Paint(&p, rect_Root(d->root));
        SDL_RenderCopy(renderer_Window(get_Window()), d->drawBuf->texture, NULL,
                       &(SDL_Rect){ bounds.pos.x, bounds.pos.y,
                                    d->drawBuf->size.x, d->drawBuf->size.y });
        unsetClip_Paint(&p);
    }
    if (d->flags & overflowScrollable_WidgetFlag) {
        iWidgetScrollInfo info;
        scrollInfo_Widget(d, &info);
        const float opacity = value_Anim(&d->overflowScrollOpacity);
        if (info.thumbHeight > 0 && opacity > 0) {
            iPaint p;
            init_Paint(&p);
            const int scrollWidth = gap_UI / 2;
            iRect     bounds      = bounds_Widget(d);
            bounds.pos.x          = right_Rect(bounds) - scrollWidth * 3;
            bounds.size.x         = scrollWidth;
            bounds.pos.y          = info.thumbY;
            bounds.size.y         = info.thumbHeight;
            /* Draw the scroll bar with some transparency. */
            SDL_Renderer *rend = renderer_Window(get_Window());
            SDL_SetRenderDrawBlendMode(rend, SDL_BLENDMODE_BLEND);
            p.alpha = (int) (0.5f * opacity * 255 + 0.5f);
            fillRect_Paint(&p, bounds, tmQuote_ColorId);
            SDL_SetRenderDrawBlendMode(rend, SDL_BLENDMODE_NONE);
        }
    }   
}

iAny *addChild_Widget(iWidget *d, iAnyObject *child) {
    return addChildPos_Widget(d, child, back_WidgetAddPos);
}

iAny *addChildPos_Widget(iWidget *d, iAnyObject *child, enum iWidgetAddPos addPos) {
    return addChildPosFlags_Widget(d, child, addPos, 0);
}

iAny *addChildPosFlags_Widget(iWidget *d, iAnyObject *child, enum iWidgetAddPos addPos, int64_t flags) {
    iAssert(child);
    iAssert(d != child);
    iWidget *widget = as_Widget(child);
    iAssert(widget->root == d->root);
    iAssert(!widget->parent);
    if (!d->children) {
        d->children = new_ObjectList();
    }
    if (addPos == back_WidgetAddPos) {
        /* Remove a redundant border flags. */
        if (!isEmpty_ObjectList(d->children) &&
            as_Widget(back_ObjectList(d->children))->flags & borderBottom_WidgetFlag &&
            widget->flags & borderTop_WidgetFlag) {
            widget->flags &= ~borderTop_WidgetFlag;
        }
        pushBack_ObjectList(d->children, widget); /* ref */
    }
    else {
        pushFront_ObjectList(d->children, widget); /* ref */
    }
    widget->parent = d;
    if (flags) {
        setFlags_Widget(child, flags, iTrue);
    }
    return child;
}

iAny *insertChildAfter_Widget(iWidget *d, iAnyObject *child, size_t afterIndex) {
    iAssert(child);
    iAssert(d != child);
    iWidget *widget = as_Widget(child);
    iAssert(!widget->parent);
    iAssert(d->children);
    iAssert(afterIndex < size_ObjectList(d->children));
    iBool wasInserted = iFalse;
    iForEach(ObjectList, i, d->children) {
        if (afterIndex-- == 0) {
            insertAfter_ObjectList(d->children, i.value, child);
            wasInserted = iTrue;
            break;
        }
    }
    if (!wasInserted) {
        /* Someone is confused about the number of children? We still have to add this. */
        pushBack_ObjectList(d->children, child);
    }
    widget->parent = d;
    return child;
}

iAny *insertChildAfterFlags_Widget(iWidget *d, iAnyObject *child, size_t afterIndex, int64_t childFlags) {
    setFlags_Widget(child, childFlags, iTrue);
    return insertChildAfter_Widget(d, child, afterIndex);
}

iAny *addChildFlags_Widget(iWidget *d, iAnyObject *child, int64_t childFlags) {
    setFlags_Widget(child, childFlags, iTrue);
    return addChild_Widget(d, child);
}

iAny *removeChild_Widget(iWidget *d, iAnyObject *child) {
    if (!d || !child) {
        return NULL;
    }
    ref_Object(child); /* we take a reference, parent releases its */
    iBool found = iFalse;
    iForEach(ObjectList, i, d->children) {
        if (i.object == child) {
            remove_ObjectListIterator(&i);
            found = iTrue;
            break;
        }
    }
    iAssert(found);
    iWidget *childWidget = child;
//    if (childWidget->flags & keepOnTop_WidgetFlag) {
//        removeOne_PtrArray(onTop_Root(childWidget->root), childWidget);
//        iAssert(indexOf_PtrArray(onTop_Root(childWidget->root), childWidget) == iInvalidPos);
//    }
//    printf("%s:%d [%p] parent = NULL\n", __FILE__, __LINE__, d);
    childWidget->parent = NULL;
    postRefresh_App();
    return child;
}

iAny *child_Widget(iWidget *d, size_t index) {
    iForEach(ObjectList, i, d->children) {
        if (index-- == 0) {
            return i.object;
        }
    }
    return NULL;
}

size_t indexOfChild_Widget(const iWidget *d, const iAnyObject *child) {
    size_t index = 0;
    iConstForEach(ObjectList, i, d->children) {
        if (i.object == child) {
            return index;
        }
        index++;
    }
    return iInvalidPos;
}

void changeChildIndex_Widget(iWidget *d, iAnyObject *child, size_t newIndex) {
    size_t oldIndex = 0;
    iForEach(ObjectList, i, d->children) {
        if (i.object == child) {
            ref_Object(child); /* we keep a reference */
            remove_ObjectListIterator(&i);
            break;
        }
        oldIndex++;
    }
    iAssert(oldIndex <= size_ObjectList(d->children));
    if (isEmpty_ObjectList(d->children) || newIndex == 0) {
        pushFront_ObjectList(d->children, child);
    }
    else {
        iObjectListIterator iter;
        init_ObjectListIterator(&iter, d->children);
        for (size_t i = 1; i < newIndex; i++, next_ObjectListIterator(&iter)) {}
        insertAfter_ObjectList(d->children, iter.value, child);
    }
    deref_Object(child); /* ObjectList has taken a reference */
}

iAny *hitChild_Widget(const iWidget *d, iInt2 coord) {
    if (isHidden_Widget_(d)) {
        return NULL;
    }
    /* Check for on-top widgets first. */
    if (!d->parent) {
        iReverseForEach(PtrArray, i, onTop_Root(d->root)) {
            iWidget *child = i.ptr;
//            printf("ontop: %s (%s) hidden:%d hittable:%d\n", cstr_String(id_Widget(child)),
//                   class_Widget(child)->name,
//                   child->flags & hidden_WidgetFlag ? 1 : 0,
//                   child->flags & unhittable_WidgetFlag ? 0 : 1);
            iAny *found = hitChild_Widget(constAs_Widget(child), coord);
            if (found) return found;
        }
    }
    iReverseForEach(ObjectList, i, d->children) {
        const iWidget *child = constAs_Widget(i.object);
        if (~child->flags & keepOnTop_WidgetFlag) {
            iAny *found = hitChild_Widget(child, coord);
            if (found) return found;
        }
    }
    if ((d->flags & (overflowScrollable_WidgetFlag | hittable_WidgetFlag) ||
         class_Widget(d) != &Class_Widget || d->flags & mouseModal_WidgetFlag) &&
        ~d->flags & unhittable_WidgetFlag && contains_Widget(d, coord)) {
        return iConstCast(iWidget *, d);
    }
    return NULL;
}

iAny *findChild_Widget(const iWidget *d, const char *id) {
    if (!d) return NULL;
    if (cmp_String(id_Widget(d), id) == 0) {
        return iConstCast(iAny *, d);
    }
    iConstForEach(ObjectList, i, d->children) {
        iAny *found = findChild_Widget(constAs_Widget(i.object), id);
        if (found) return found;
    }
    return NULL;
}

static void addMatchingToArray_Widget_(const iWidget *d, const char *id, iPtrArray *found) {
    if (cmp_String(id_Widget(d), id) == 0) {
        pushBack_PtrArray(found, d);
    }
    iForEach(ObjectList, i, d->children) {
        addMatchingToArray_Widget_(i.object, id, found);
    }
}

const iPtrArray *findChildren_Widget(const iWidget *d, const char *id) {
    iPtrArray *found = new_PtrArray();
    addMatchingToArray_Widget_(d, id, found);
    return collect_PtrArray(found);
}

iAny *findParent_Widget(const iWidget *d, const char *id) {
    if (!d) return NULL;
    iWidget *i = (iWidget *) d;
    while (i && cmp_String(&i->id, id)) {
        i = i->parent;
    }
    return i;
}

iAny *findParentClass_Widget(const iWidget *d, const iAnyClass *class) {
    if (!d) return NULL;
    iWidget *i = d->parent;
    while (i && !isInstance_Object(i, class)) {
        i = i->parent;
    }
    return i;
}

iAny *findOverflowScrollable_Widget(iWidget *d) {
    const iRect rootRect = visibleRect_Root(d->root);
    for (iWidget *w = d; w; w = parent_Widget(w)) {
        if (flags_Widget(w) & overflowScrollable_WidgetFlag) {
            const iRect bounds = boundsWithoutVisualOffset_Widget(w);
            if ((bottom_Rect(bounds) > bottom_Rect(rootRect) ||
                 top_Rect(bounds) < top_Rect(rootRect)) &&
                !hasVisibleChildOnTop_Widget(w)) {
                return w;
            }
            return NULL;
        }
    }
    return NULL;
}

size_t childCount_Widget(const iWidget *d) {
    if (!d->children) return 0;
    return size_ObjectList(d->children);
}

iBool isVisible_Widget(const iAnyObject *d) {
    if (!d) return iFalse;
    iAssert(isInstance_Object(d, &Class_Widget));
    for (const iWidget *w = d; w; w = w->parent) {
        if (w->flags & hidden_WidgetFlag) {
            return iFalse;
        }
    }
    return iTrue;
}

iBool isDisabled_Widget(const iAnyObject *d) {
    iAssert(isInstance_Object(d, &Class_Widget));
    for (const iWidget *w = d; w; w = w->parent) {
        if (w->flags & disabled_WidgetFlag) {
            return iTrue;
        }
    }
    return iFalse;
}

iBool isFocused_Widget(const iAnyObject *d) {
    iAssert(isInstance_Object(d, &Class_Widget));
    return get_Window()->focus == d;
}

iBool isHover_Widget(const iAnyObject *d) {
    iAssert(isInstance_Object(d, &Class_Widget));
    return get_Window()->hover == d;
}

iBool isUnderKeyRoot_Widget(const iAnyObject *d) {
    iAssert(isInstance_Object(d, &Class_Widget));
    const iWidget *w = d;
    return w && get_Window() && w->root == get_Window()->keyRoot;
}

iBool isSelected_Widget(const iAnyObject *d) {
    if (d) {
        iAssert(isInstance_Object(d, &Class_Widget));
        return (flags_Widget(d) & selected_WidgetFlag) != 0;
    }
    return iFalse;
}

iBool equalWidget_Command(const char *cmd, const iWidget *widget, const char *checkCommand) {
    if (equal_Command(cmd, checkCommand)) {
        const iWidget *src = pointer_Command(cmd);
        iAssert(!src || strstr(cmd, " ptr:"));
        if (src == widget || hasParent_Widget(src, widget)) {
            return iTrue;
        }
//        if (src && type_Window(window_Widget(src)) == popup_WindowType) {
//            /* Special case: command was emitted from a popup widget. The popup root widget actually
//               belongs to someone else. */
//            iWidget *realParent = userData_Object(src->root->widget);
//            iAssert(realParent);
//            iAssert(isInstance_Object(realParent, &Class_Widget));
//            return realParent == widget || hasParent_Widget(realParent, widget);
//        }
    }
    return iFalse;
}

iBool isCommand_Widget(const iWidget *d, const SDL_Event *ev, const char *cmd) {
    if (ev->type == SDL_USEREVENT && ev->user.code == command_UserEventCode) {
        return equalWidget_Command(command_UserEvent(ev), d, cmd);
    }
    return iFalse;
}

iBool hasParent_Widget(const iWidget *d, const iWidget *someParent) {
    if (d) {
        for (const iWidget *w = d->parent; w; w = w->parent) {
            if (w == someParent) return iTrue;
        }
    }
    return iFalse;
}

iBool isAffectedByVisualOffset_Widget(const iWidget *d) {
    for (const iWidget *w = d; w; w = w->parent) {
        if (w->flags & visualOffset_WidgetFlag) {
            return iTrue;
        }
        if (visualOffsetByReference_Widget(w) != 0) {
            return iTrue;
        }
    }
    return iFalse;
}

void setFocus_Widget(iWidget *d) {
    iWindow *win = d ? window_Widget(d) : get_Window();
    iAssert(win);
    if (win->focus != d) {
        if (win->focus) {
            iAssert(!contains_PtrSet(win->focus->root->pendingDestruction, win->focus));
            postCommand_Widget(win->focus, "focus.lost");
        }
        if (~flags_Widget(d) & focusable_WidgetFlag) {
            d = NULL; /* focusing this is not allowed */
        }
        win->focus = d;
        if (d) {
            setKeyRoot_Window(get_Window(), d->root);
            postCommand_Widget(d, "focus.gained");
        }
    }
}

void setKeyboardGrab_Widget(iWidget *d) {
    iWindow *win = d ? window_Widget(d) : get_Window();
    iAssert(win);
    win->focus = d;
    /* no notifications sent */
}

iWidget *focus_Widget(void) {
    iWindow *win = get_Window();
    return win ? win->focus : NULL;
}

void setHover_Widget(iWidget *d) {
    iWindow *win = get_Window();
    iAssert(win);
    win->hover = d;
}

iWidget *hover_Widget(void) {
    return get_Window()->hover;
}

static const iWidget *findFocusable_Widget_(const iWidget *d, const iWidget *startFrom,
                                            iBool *getNext, enum iWidgetFocusDir focusDir) {
    if (startFrom == d) {
        *getNext = iTrue;
        return NULL;
    }
    if ((d->flags & focusable_WidgetFlag) && isVisible_Widget(d) && !isDisabled_Widget(d) &&
        ~d->flags & destroyPending_WidgetFlag && *getNext) {
        if ((~focusDir & notInput_WidgetFocusFlag) || !isInstance_Object(d, &Class_InputWidget)) {
            return d;
        }
    }
    if ((focusDir & dirMask_WidgetFocusFlag) == forward_WidgetFocusDir) {
        iConstForEach(ObjectList, i, d->children) {
            const iWidget *found =
                findFocusable_Widget_(constAs_Widget(i.object), startFrom, getNext, focusDir);
            if (found) return found;
        }
    }
    else {
        iReverseConstForEach(ObjectList, i, d->children) {
            const iWidget *found =
                findFocusable_Widget_(constAs_Widget(i.object), startFrom, getNext, focusDir);
            if (found) return found;
        }
    }
    return NULL;
}

const iWidget *findTopmostFocusRoot_Widget_(const iWidget *d) {
    if (d->flags & (hidden_WidgetFlag | disabled_WidgetFlag)) {
        return NULL;
    }
    iReverseConstForEach(ObjectList, i, d->children) {
        const iWidget *child = constAs_Widget(i.object);
        const iWidget *root = findTopmostFocusRoot_Widget_(child);
        if (root) {
            return root;
        }
    }
    if (d->flags & focusRoot_WidgetFlag) {
        return d;
    }
    return NULL;
}

const iWidget *focusRoot_Widget(const iWidget *d) {
    if (d == NULL || isRoot_Widget_(d)) {
        iAssert(get_Window());
        iRoot *root = d ? d->root : get_Window()->keyRoot;
        iReverseConstForEach(PtrArray, i, onTop_Root(root)) {
            const iWidget *root = findTopmostFocusRoot_Widget_(constAs_Widget(i.ptr));
            if (root) {
                return root;
            }
        }
        return findTopmostFocusRoot_Widget_(root->widget);
    }
    /* Focus root of this particular widget `d`. */
    for (const iWidget *w = d; w; w = w->parent) {
        if (flags_Widget(w) & focusRoot_WidgetFlag) {
            return w;
        }
    }
    return root_Widget(d);
}

iAny *findFocusable_Widget(const iWidget *startFrom, enum iWidgetFocusDir focusDir) {
    if (!get_Window()) {
        return NULL;
    }
    const iWidget *focusRoot = focusRoot_Widget(startFrom);
    iAssert(focusRoot != NULL);
    iBool getNext = (startFrom ? iFalse : iTrue);
    const iWidget *found = findFocusable_Widget_(focusRoot, startFrom, &getNext, focusDir);
    if (!found && startFrom) {
        getNext = iTrue;
        /* Switch to the next root, if available. */
        found = findFocusable_Widget_(
            findTopmostFocusRoot_Widget_(otherRoot_Window(get_Window(), focusRoot->root)->widget),
            NULL,
            &getNext,
            focusDir);
    }
    return iConstCast(iWidget *, found);
}

void setMouseGrab_Widget(iWidget *d) {
    if (get_Window()->mouseGrab != d) {
        get_Window()->mouseGrab = d;
        SDL_CaptureMouse(d != NULL);
    }
}

iWidget *mouseGrab_Widget(void) {
    return get_Window()->mouseGrab;
}

void postCommand_Widget(const iAnyObject *d, const char *cmd, ...) {
    if (isRecentlyDeleted_Widget(d)) {
        return; /* invalid context */
    }
    iString str;
    init_String(&str); {
        va_list args;
        va_start(args, cmd);
        vprintf_Block(&str.chars, cmd, args);
        va_end(args);
    }
    iBool isGlobal = iFalse;
    if (*cstr_String(&str) == '!')  {
        isGlobal = iTrue;
        remove_Block(&str.chars, 0, 1);
    }
    if (!isGlobal) {
        iAssert(isInstance_Object(d, &Class_Widget));
        if (type_Window(window_Widget(d)) == popup_WindowType) {
            postCommandf_Root(((const iWidget *) d)->root, "cancel popup:1 ptr:%p", d);
            d = userData_Object(root_Widget(d));
        }
        iString ptrStr;
        init_String(&ptrStr);
        /* Insert the widget pointer as the first argument so possible suffixes are unaffected. */
        format_String(&ptrStr, " ptr:%p", d);
        const size_t insertPos = indexOf_String(&str, ' ');
        if (insertPos == iInvalidPos) {
            append_String(&str, &ptrStr);
        }
        else {
            insertData_Block(&str.chars, insertPos, cstr_String(&ptrStr), size_String(&ptrStr));
        }
        deinit_String(&ptrStr);
    }
    postCommandString_Root(((const iWidget *) d)->root, &str);
    deinit_String(&str);
}

void refresh_Widget(const iAnyObject *d) {
    if (!d) return;
    /* TODO: Could be widget specific, if parts of the tree are cached. */
    /* TODO: The visbuffer in DocumentWidget and ListWidget could be moved to be a general
       purpose feature of Widget. */
    iAssert(isInstance_Object(d, &Class_Widget));
    /* Mark draw buffers invalid. */
    for (const iWidget *w = d; w; w = w->parent) {
        if (w->drawBuf) {
//            if (w->drawBuf->isValid) {
//                printf("[%p] drawbuffer invalidated by %p\n", w, d); fflush(stdout);
//            }
            w->drawBuf->isValid = iFalse;
        }
    }
    postRefresh_App();
}

void raise_Widget(iWidget *d) {
    iPtrArray *onTop = onTop_Root(d->root);
    if (d->flags & keepOnTop_WidgetFlag && !isRoot_Widget_(d)) {
        iAssert(indexOf_PtrArray(onTop, d) != iInvalidPos);
        removeOne_PtrArray(onTop, d);
        pushBack_PtrArray(onTop, d);
    }
}

iBool hasVisibleChildOnTop_Widget(const iWidget *parent) {
    iConstForEach(ObjectList, i, parent->children) {
        const iWidget *child = i.object;
        if (~child->flags & hidden_WidgetFlag && child->flags & keepOnTop_WidgetFlag) {
            return iTrue;
        }
        if (hasVisibleChildOnTop_Widget(child)) {
            return iTrue;
        }
    }
    return iFalse;
}

iBeginDefineClass(Widget)
    .processEvent = processEvent_Widget,
    .draw         = draw_Widget,
iEndDefineClass(Widget)

/*----------------------------------------------------------------------------------------------
   Debug utilities for inspecting widget trees.
*/

#include "labelwidget.h"
static void printInfo_Widget_(const iWidget *d) {
    printf("[%p] %s:\"%s\" ", d, class_Widget(d)->name, cstr_String(&d->id));
    if (isInstance_Object(d, &Class_LabelWidget)) {
        printf("(%s|%s) ",
               cstr_String(text_LabelWidget((const iLabelWidget *) d)),
               cstr_String(command_LabelWidget((const iLabelWidget *) d)));
    }
    printf("pos:%d,%d size:%dx%d {min:%dx%d} [%d..%d %d:%d] flags:%08llx%s%s%s%s%s%s%s\n",
           d->rect.pos.x, d->rect.pos.y,
           d->rect.size.x, d->rect.size.y,
           d->minSize.x, d->minSize.y,
           d->padding[0], d->padding[2],
           d->padding[1], d->padding[3],
           (long long unsigned int) d->flags,
           d->flags & expand_WidgetFlag ? " exp" : "",
           d->flags & tight_WidgetFlag ? " tight" : "",
           d->flags & fixedWidth_WidgetFlag ? " fixW" : "",
           d->flags & fixedHeight_WidgetFlag ? " fixH" : "",
           d->flags & resizeToParentWidth_WidgetFlag ? " prnW" : "",
           d->flags & arrangeWidth_WidgetFlag ? " aW" : "",
           d->flags & resizeWidthOfChildren_WidgetFlag ? " rsWChild" : "");
}

static void printTree_Widget_(const iWidget *d, int indent) {
    for (int i = 0; i < indent; ++i) {
        fwrite("    ", 4, 1, stdout);
    }
    printInfo_Widget_(d);
    iConstForEach(ObjectList, i, d->children) {
        printTree_Widget_(i.object, indent + 1);
    }
}

void printTree_Widget(const iWidget *d) {
    if (!d) {
        puts("[NULL]");
        return;
    }
    printTree_Widget_(d, 0);
}

static void printIndent_(int indent) {
    for (int i = 0; i < indent; ++i) {
        fwrite("  ", 2, 1, stdout);
    }
}

void identify_Widget(const iWidget *d) {
    if (!d) {
        puts("[NULL}");
        return;
    }
    int indent = 0;
    for (const iWidget *w = d; w; w = w->parent, indent++) {
        printIndent_(indent);
        printInfo_Widget_(w);
    }
    printIndent_(indent);
    printf("Root %d: %p\n", 1 + (d->root == get_Window()->roots[1]), d->root);
    fflush(stdout);
}

void addRecentlyDeleted_Widget(iAnyObject *obj) {
    /* We sometimes include pointers to widgets in command events. Before an event is processed,
       it is possible that the referened widget has been destroyed. Keeping track of recently
       deleted widgets allows ignoring these events. */
    maybeInit_RecentlyDeleted_(&recentlyDeleted_);
    iGuardMutex(&recentlyDeleted_.mtx, insert_PtrSet(recentlyDeleted_.objs, obj));
}

void clearRecentlyDeleted_Widget(void) {
    if (recentlyDeleted_.objs) {
        iGuardMutex(&recentlyDeleted_.mtx, clear_PtrSet(recentlyDeleted_.objs));
    }
}

iBool isRecentlyDeleted_Widget(const iAnyObject *obj) {
    return contains_RecentlyDeleted_(&recentlyDeleted_, obj);
}
