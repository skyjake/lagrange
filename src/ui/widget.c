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
#include "touch.h"
#include "command.h"
#include "paint.h"
#include "util.h"
#include "window.h"

#include <the_Foundation/ptrarray.h>
#include <the_Foundation/ptrset.h>
#include <SDL_mouse.h>
#include <stdarg.h>

#if defined (iPlatformAppleMobile)
#   include "../ios.h"
#endif

iDeclareType(RootData)

struct Impl_RootData {
    iWidget *hover;
    iWidget *mouseGrab;
    iWidget *focus;
    iPtrArray *onTop; /* order is important; last one is topmost */
    iPtrSet *pendingDestruction;
};

static iRootData rootData_;

iPtrArray *onTop_RootData_(void) {
    if (!rootData_.onTop) {
        rootData_.onTop = new_PtrArray();
    }
    return rootData_.onTop;
}

void destroyPending_Widget(void) {
    iForEach(PtrSet, i, rootData_.pendingDestruction) {
        iWidget *widget = *i.value;
        if (!isFinished_Anim(&widget->visualOffset)) {
            continue;
        }
        if (widget->parent) {
            removeChild_Widget(widget->parent, widget);
        }
        iAssert(widget->parent == NULL);
//        iAssert(widget->object.refCount == 1); /* ref could be held in garbage still */
        iRelease(widget);
        remove_PtrSetIterator(&i);
    }
}

void releaseChildren_Widget(iWidget *d) {
    iForEach(ObjectList, i, d->children) {
        ((iWidget *) i.object)->parent = NULL; /* the actual reference being held */
    }
    iReleasePtr(&d->children);
}

iDefineObjectConstruction(Widget)

void init_Widget(iWidget *d) {
    init_String(&d->id);
    d->flags          = 0;
    d->rect           = zero_Rect();
    d->bgColor        = none_ColorId;
    d->frameColor     = none_ColorId;
    init_Anim(&d->visualOffset, 0.0f);
    d->children       = NULL;
    d->parent         = NULL;
    d->commandHandler = NULL;
    iZap(d->padding);
}

static void visualOffsetAnimation_Widget_(void *ptr) {
    iWidget *d = ptr;
    postRefresh_App();
    if (!isFinished_Anim(&d->visualOffset)) {
        addTicker_App(visualOffsetAnimation_Widget_, ptr);
    }
    else {
        setFlags_Widget(d, visualOffset_WidgetFlag, iFalse);
    }
}

void deinit_Widget(iWidget *d) {
    releaseChildren_Widget(d);
//#if !defined (NDEBUG)
//    printf("widget %p (%s) deleted (on top:%d)\n", d, cstr_String(&d->id),
//           d->flags & keepOnTop_WidgetFlag ? 1 : 0);
//#endif
    deinit_String(&d->id);
    if (d->flags & keepOnTop_WidgetFlag) {
        removeAll_PtrArray(onTop_RootData_(), d);
    }
    if (d->flags & visualOffset_WidgetFlag) {
        removeTicker_App(visualOffsetAnimation_Widget_, d);
    }
    widgetDestroyed_Touch(d);
}

static void aboutToBeDestroyed_Widget_(iWidget *d) {
    if (isFocused_Widget(d)) {
        setFocus_Widget(NULL);
        return;
    }
    if (flags_Widget(d) & keepOnTop_WidgetFlag) {
        removeOne_PtrArray(onTop_RootData_(), d);
    }
    if (isHover_Widget(d)) {
        rootData_.hover = NULL;
    }
    iForEach(ObjectList, i, d->children) {
        aboutToBeDestroyed_Widget_(as_Widget(i.object));
    }
}

void destroy_Widget(iWidget *d) {
    if (d) {
        if (isVisible_Widget(d)) {
            postRefresh_App();
        }
        aboutToBeDestroyed_Widget_(d);
        if (!rootData_.pendingDestruction) {
            rootData_.pendingDestruction = new_PtrSet();
        }
        insert_PtrSet(rootData_.pendingDestruction, d);
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
        if (deviceType_App() == phone_AppDeviceType) {
            /* Phones rarely have keyboards attached so don't bother with the shortcuts. */
            flags &= ~drawKey_WidgetFlag;
        }
        iChangeFlags(d->flags, flags, set);
        if (flags & keepOnTop_WidgetFlag) {
            iPtrArray *onTop = onTop_RootData_();
            if (set) {
                iAssert(indexOf_PtrArray(onTop, d) == iInvalidPos);
                pushBack_PtrArray(onTop, d);
            }
            else {
                removeOne_PtrArray(onTop, d);
            }
        }
    }
}

void setPos_Widget(iWidget *d, iInt2 pos) {
    d->rect.pos = pos;
    setFlags_Widget(d, fixedPosition_WidgetFlag, iTrue);
}

void setSize_Widget(iWidget *d, iInt2 size) {
    int flags = fixedSize_WidgetFlag;
    if (size.x < 0) {
        size.x = d->rect.size.x;
        flags &= ~fixedWidth_WidgetFlag;
    }
    if (size.y < 0) {
        size.y = d->rect.size.y;
        flags &= ~fixedHeight_WidgetFlag;
    }
    d->rect.size = size;
    setFlags_Widget(d, flags, iTrue);
}

void setPadding_Widget(iWidget *d, int left, int top, int right, int bottom) {
    d->padding[0] = left;
    d->padding[1] = top;
    d->padding[2] = right;
    d->padding[3] = bottom;
}

void setVisualOffset_Widget(iWidget *d, int value, uint32_t span, int animFlags) {
    setFlags_Widget(d, visualOffset_WidgetFlag, iTrue);
    if (span == 0) {
        init_Anim(&d->visualOffset, value);
    }
    else {
        setValue_Anim(&d->visualOffset, value, span);
        d->visualOffset.flags = animFlags;
        addTicker_App(visualOffsetAnimation_Widget_, d);
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

static int numExpandingChildren_Widget_(const iWidget *d) {
    int count = 0;
    iConstForEach(ObjectList, i, d->children) {
        const iWidget *child = constAs_Widget(i.object);
        if (flags_Widget(child) & expand_WidgetFlag) {
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

static void setWidth_Widget_(iWidget *d, int width) {
    iAssert(width >= 0);
    if (~d->flags & fixedWidth_WidgetFlag || d->flags & collapse_WidgetFlag) {
        if (d->rect.size.x != width) {
            d->rect.size.x = width;
            if (class_Widget(d)->sizeChanged) {
                const int oldHeight = d->rect.size.y;
                class_Widget(d)->sizeChanged(d);
                if (d->rect.size.y != oldHeight) {
                    /* Widget updated its height. */
                    arrange_Widget(d->parent);
                }
            }
        }
    }
}

static void setHeight_Widget_(iWidget *d, int height) {
    iAssert(height >= 0);
    if (~d->flags & fixedHeight_WidgetFlag || d->flags & collapse_WidgetFlag) {
        if (d->rect.size.y != height) {
            d->rect.size.y = height;
            if (class_Widget(d)->sizeChanged) {
                class_Widget(d)->sizeChanged(d);
            }
        }
    }
}

iLocalDef iBool isCollapsed_Widget_(const iWidget *d) {
    return (d->flags & (hidden_WidgetFlag | collapse_WidgetFlag)) ==
           (hidden_WidgetFlag | collapse_WidgetFlag);
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

iLocalDef iBool isArranged_Widget_(const iWidget *d) {
    return !isCollapsed_Widget_(d) && ~d->flags & fixedPosition_WidgetFlag;
}

static size_t numArrangedChildren_Widget_(const iWidget *d) {
    size_t count = 0;
    iConstForEach(ObjectList, i, d->children) {
        if (isArranged_Widget_(d)) {
            count++;
        }
    }
    return count;
}

static void centerHorizontal_Widget_(iWidget *d) {
    d->rect.pos.x = ((d->parent ? width_Rect(innerRect_Widget_(d->parent))
                                : rootSize_Window(get_Window()).x) -
                     width_Rect(d->rect)) /
                    2;
}

void arrange_Widget(iWidget *d) {
    if (isCollapsed_Widget_(d)) {
        setFlags_Widget(d, wasCollapsed_WidgetFlag, iTrue);
        return;
    }
    if (d->flags & moveToParentLeftEdge_WidgetFlag) {
        d->rect.pos.x = d->padding[0];
    }
    else if (d->flags & moveToParentRightEdge_WidgetFlag) {
        d->rect.pos.x = width_Rect(innerRect_Widget_(d->parent)) - width_Rect(d->rect);
    }
    else if (d->flags & centerHorizontal_WidgetFlag) {
        centerHorizontal_Widget_(d);
    }
    if (d->flags & resizeToParentWidth_WidgetFlag) {
        setWidth_Widget_(d, width_Rect(innerRect_Widget_(d->parent)));
    }
    if (d->flags & resizeToParentHeight_WidgetFlag) {
        setHeight_Widget_(d, height_Rect(innerRect_Widget_(d->parent)));
    }
    /* The rest of the arrangement depends on child widgets. */
    if (!d->children) {
        return;
    }
    /* Resize children to fill the parent widget. */
    const size_t childCount = numArrangedChildren_Widget_(d);
    if (childCount == 0) {
        return;
    }
    if (d->flags & resizeChildren_WidgetFlag) {
        const iInt2 dirs = init_I2((d->flags & resizeWidthOfChildren_WidgetFlag) != 0,
                                   (d->flags & resizeHeightOfChildren_WidgetFlag) != 0);
        /* Collapse hidden children. */
        iBool uncollapsed = iFalse;
        iForEach(ObjectList, c, d->children) {
            iWidget *child = as_Widget(c.object);
            if (isCollapsed_Widget_(child)) {
                if (d->flags & arrangeHorizontal_WidgetFlag) {
                    setWidth_Widget_(child, 0);
                }
                if (d->flags & arrangeVertical_WidgetFlag) {
                    setHeight_Widget_(child, 0);
                }
            }
            else if (child->flags & wasCollapsed_WidgetFlag) {
                setFlags_Widget(child, wasCollapsed_WidgetFlag, iFalse);
                /* Undo collapse and determine the normal size again. */
                if (child->flags & arrangeSize_WidgetFlag) {
                    arrange_Widget(d);
                    uncollapsed = iTrue;
                }
            }
        }
        if (uncollapsed) {
            arrange_Widget(d); /* Redo with the new child sizes. */
            return;
        }
        const int expCount = numExpandingChildren_Widget_(d);
        /* Only resize the expanding children, not touching the others. */
        if (expCount > 0) {
            iInt2 avail = innerRect_Widget_(d).size;
            iConstForEach(ObjectList, i, d->children) {
                const iWidget *child = constAs_Widget(i.object);
                if (!isArranged_Widget_(child)) {
                    continue;
                }
                if (~child->flags & expand_WidgetFlag) {
                    subv_I2(&avail, child->rect.size);
                }
            }
            avail = divi_I2(max_I2(zero_I2(), avail), expCount);
            iForEach(ObjectList, j, d->children) {
                iWidget *child = as_Widget(j.object);
                if (!isArranged_Widget_(child)) {
                    continue;
                }
                if (child->flags & expand_WidgetFlag) {
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
                    /* Fill the off axis, though. */
                    if (d->flags & arrangeHorizontal_WidgetFlag) {
                        if (dirs.y) setHeight_Widget_(child, height_Rect(innerRect_Widget_(d)));
                    }
                    else if (d->flags & arrangeVertical_WidgetFlag) {
                        if (dirs.x) setWidth_Widget_(child, width_Rect(innerRect_Widget_(d)));
                    }
                }
            }
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
            iForEach(ObjectList, i, d->children) {
                iWidget *child = as_Widget(i.object);
                if (isArranged_Widget_(child)) {
                    if (dirs.x) setWidth_Widget_(child, child->flags & unpadded_WidgetFlag ? unpaddedChildSize.x : childSize.x);
                    if (dirs.y) setHeight_Widget_(child, child->flags & unpadded_WidgetFlag ? unpaddedChildSize.y : childSize.y);
                }
            }
        }
    }
    if (d->flags & resizeChildrenToWidestChild_WidgetFlag) {
        const int widest = widestChild_Widget_(d);
        iForEach(ObjectList, i, d->children) {
            if (isArranged_Widget_(i.object)) {
                setWidth_Widget_(as_Widget(i.object), widest);
            }
        }
    }
    iInt2 pos = initv_I2(d->padding);
    iForEach(ObjectList, i, d->children) {
        iWidget *child = as_Widget(i.object);
        arrange_Widget(child);
        if (!isArranged_Widget_(child)) {
            continue;
        }
        if (child->flags & centerHorizontal_WidgetFlag) {
            continue;
        }
        if (d->flags & (arrangeHorizontal_WidgetFlag | arrangeVertical_WidgetFlag)) {
            if (child->flags &
                (moveToParentLeftEdge_WidgetFlag | moveToParentRightEdge_WidgetFlag)) {
                continue; /* Not part of the sequential arrangement .*/
            }
            child->rect.pos = pos;
            if (d->flags & arrangeHorizontal_WidgetFlag) {
                pos.x += child->rect.size.x;
            }
            else {
                pos.y += child->rect.size.y;
            }
        }
        else if ((d->flags & resizeChildren_WidgetFlag) == resizeChildren_WidgetFlag) {
            child->rect.pos = pos;
        }
    }
    /* Update the size of the widget according to the arrangement. */
    if (d->flags & arrangeSize_WidgetFlag) {
        iRect bounds = zero_Rect();
        iConstForEach(ObjectList, i, d->children) {
            const iWidget *child = constAs_Widget(i.object);
            if (isCollapsed_Widget_(child)) {
                continue;
            }
            if (isEmpty_Rect(bounds)) {
                bounds = child->rect;
            }
            else {
                bounds = union_Rect(bounds, child->rect);
            }
        }
        adjustEdges_Rect(&bounds, -d->padding[1], d->padding[2], d->padding[3], -d->padding[0]);
        if (d->flags & arrangeWidth_WidgetFlag) {
            setWidth_Widget_(d, bounds.size.x);
            /* Parent size changed, must update the children.*/
            iForEach(ObjectList, j, d->children) {
                iWidget *child = as_Widget(j.object);
                if (child->flags &
                    (resizeToParentWidth_WidgetFlag | moveToParentLeftEdge_WidgetFlag |
                     moveToParentRightEdge_WidgetFlag)) {
                    arrange_Widget(child);
                }
            }
        }
        if (d->flags & arrangeHeight_WidgetFlag) {
            setHeight_Widget_(d, bounds.size.y);
            /* Parent size changed, must update the children.*/
            iForEach(ObjectList, j, d->children) {
                iWidget *child = as_Widget(j.object);
                if (child->flags & resizeToParentHeight_WidgetFlag) {
                    arrange_Widget(child);
                }
            }
        }
        if (d->flags & centerHorizontal_WidgetFlag) {
            centerHorizontal_Widget_(d);
        }
    }
}

static void applyVisualOffset_Widget_(const iWidget *d, iInt2 *pos) {
    if (d->flags & (visualOffset_WidgetFlag | dragged_WidgetFlag)) {
        const int off = iRound(value_Anim(&d->visualOffset));
        if (d->flags & horizontalOffset_WidgetFlag) {
            pos->x += off;
        }
        else {
            pos->y += off;
        }
    }
}

iRect bounds_Widget(const iWidget *d) {
    iRect bounds = d->rect;
    applyVisualOffset_Widget_(d, &bounds.pos);
    for (const iWidget *w = d->parent; w; w = w->parent) {
        iInt2 pos = w->rect.pos;
        applyVisualOffset_Widget_(w, &pos);
        addv_I2(&bounds.pos, pos);
    }
#if defined (iPlatformMobile)
    bounds.pos.y += value_Anim(&get_Window()->rootOffset);
#endif
    return bounds;
}

iInt2 localCoord_Widget(const iWidget *d, iInt2 coord) {
    for (const iWidget *w = d; w; w = w->parent) {
        subv_I2(&coord, w->rect.pos);
    }
    return coord;
}

iBool contains_Widget(const iWidget *d, iInt2 coord) {
    const iRect bounds = { zero_I2(), addY_I2(d->rect.size,
                                              d->flags & drawBackgroundToBottom_WidgetFlag ?
                                                rootSize_Window(get_Window()).y : 0) };
    return contains_Rect(bounds, localCoord_Widget(d, coord));
}

iLocalDef iBool isKeyboardEvent_(const SDL_Event *ev) {
    return (ev->type == SDL_KEYUP || ev->type == SDL_KEYDOWN || ev->type == SDL_TEXTINPUT);
}

iLocalDef iBool isMouseEvent_(const SDL_Event *ev) {
    return (ev->type == SDL_MOUSEWHEEL || ev->type == SDL_MOUSEMOTION ||
            ev->type == SDL_MOUSEBUTTONUP || ev->type == SDL_MOUSEBUTTONDOWN);
}

static iBool filterEvent_Widget_(const iWidget *d, const SDL_Event *ev) {
    const iBool isKey   = isKeyboardEvent_(ev);
    const iBool isMouse = isMouseEvent_(ev);
    if (d->flags & disabled_WidgetFlag) {
        if (isKey || isMouse) return iFalse;
    }
    if (d->flags & hidden_WidgetFlag) {
        if (isMouse) return iFalse;
    }
    return iTrue;
}

void unhover_Widget(void) {
    rootData_.hover = NULL;
}

iBool dispatchEvent_Widget(iWidget *d, const SDL_Event *ev) {
    if (!d->parent) {
        if (ev->type == SDL_MOUSEMOTION) {
            /* Hover widget may change. */
            setHover_Widget(NULL);
        }
        if (rootData_.focus && isKeyboardEvent_(ev)) {
            /* Root dispatches keyboard events directly to the focused widget. */
            if (dispatchEvent_Widget(rootData_.focus, ev)) {
                return iTrue;
            }
        }
        /* Root offers events first to widgets on top. */
        iReverseForEach(PtrArray, i, rootData_.onTop) {
            iWidget *widget = *i.value;
            if (isVisible_Widget(widget) && dispatchEvent_Widget(widget, ev)) {
                return iTrue;
            }
        }
    }
    else if (ev->type == SDL_MOUSEMOTION && !rootData_.hover &&
             flags_Widget(d) & hover_WidgetFlag && ~flags_Widget(d) & hidden_WidgetFlag &&
             ~flags_Widget(d) & disabled_WidgetFlag) {
        if (contains_Widget(d, init_I2(ev->motion.x, ev->motion.y))) {
            setHover_Widget(d);
        }
    }
    if (filterEvent_Widget_(d, ev)) {
        /* Children may handle it first. Done in reverse so children drawn on top get to
           handle the events first. */
        iReverseForEach(ObjectList, i, d->children) {
            iWidget *child = as_Widget(i.object);
            if (child == rootData_.focus && isKeyboardEvent_(ev)) {
                continue; /* Already dispatched. */
            }
            if (isVisible_Widget(child) && child->flags & keepOnTop_WidgetFlag) {
                /* Already dispatched. */
                continue;
            }
            if (dispatchEvent_Widget(child, ev)) {
#if 0
                if (ev->type == SDL_MOUSEBUTTONDOWN) {
                    printf("[%p] %s:'%s' ate the button %d\n",
                           child, class_Widget(child)->name,
                           cstr_String(id_Widget(child)), ev->button.button);
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
        if (class_Widget(d)->processEvent(d, ev)) {
            return iTrue;
        }
    }
    return iFalse;
}

iBool processEvent_Widget(iWidget *d, const SDL_Event *ev) {
    if (ev->type == SDL_KEYDOWN) {
        if (ev->key.keysym.sym == SDLK_TAB) {
            setFocus_Widget(findFocusable_Widget(focus_Widget(),
                                                 ev->key.keysym.mod & KMOD_SHIFT
                                                     ? backward_WidgetFocusDir
                                                     : forward_WidgetFocusDir));
            return iTrue;
        }
    }
    else if (d->flags & commandOnClick_WidgetFlag &&
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
    else if (d->flags & overflowScrollable_WidgetFlag && ev->type == SDL_MOUSEWHEEL &&
             ~d->flags & visualOffset_WidgetFlag) {
        iRect bounds = bounds_Widget(d);
        const iInt2 rootSize = rootSize_Window(get_Window());
        const iRect winRect = safeRootRect_Window(get_Window());
        const int yTop = top_Rect(winRect);
        const int yBottom = bottom_Rect(winRect);
        const int safeBottom = rootSize.y - yBottom;
        if (height_Rect(bounds) > height_Rect(winRect)) {
            int step = ev->wheel.y;
            if (!isPerPixel_MouseWheelEvent(&ev->wheel)) {
                step *= lineHeight_Text(uiLabel_FontId);
            }
            bounds.pos.y += step;
            if (step > 0) {
                bounds.pos.y = iMin(bounds.pos.y, yTop);
            }
            else {
                bounds.pos.y = iMax(bounds.pos.y, rootSize.y + safeBottom - height_Rect(bounds));
            }
            d->rect.pos = localCoord_Widget(d->parent, bounds.pos);
            refresh_Widget(d);
            return iTrue;
        }
    }
    switch (ev->type) {
        case SDL_USEREVENT: {
            if (ev->user.code == command_UserEventCode && d->commandHandler &&
                d->commandHandler(d, ev->user.data1)) {
                return iTrue;
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
    }
    if (d->flags & mouseModal_WidgetFlag && isMouseEvent_(ev)) {
        if ((ev->type == SDL_MOUSEBUTTONDOWN || ev->type == SDL_MOUSEBUTTONUP) &&
            d->flags & commandOnClick_WidgetFlag) {
            postCommand_Widget(d,
                               "mouse.clicked arg:%d button:%d coord:%d %d",
                               ev->type == SDL_MOUSEBUTTONDOWN ? 1 : 0,
                               ev->button.button,
                               ev->button.x,
                               ev->button.y);
        }
        setCursor_Window(get_Window(), SDL_SYSTEM_CURSOR_ARROW);
        return iTrue;
    }
    return iFalse;
}

void drawBackground_Widget(const iWidget *d) {
    if (d->flags & noBackground_WidgetFlag) {
        return;
    }
    if (d->flags & hidden_WidgetFlag && ~d->flags & visualOffset_WidgetFlag) {
        return;
    }
    /* Popup menus have a shadowed border. */
    iBool shadowBorder   = (d->flags & keepOnTop_WidgetFlag && ~d->flags & mouseModal_WidgetFlag) != 0;
    iBool fadeBackground = (d->bgColor >= 0 || d->frameColor >= 0) &&
                                 d->flags & mouseModal_WidgetFlag;
    if (deviceType_App() == phone_AppDeviceType) {
        if (shadowBorder) {
            fadeBackground = iTrue;
            shadowBorder = iFalse;
        }
    }
    if (shadowBorder) {
        iPaint p;
        init_Paint(&p);
        const iBool isLight = isLight_ColorTheme(colorTheme_App());
        p.alpha = isLight ? 0xc : 0x20;
        SDL_SetRenderDrawBlendMode(renderer_Window(get_Window()), SDL_BLENDMODE_BLEND);
        iRect shadowRect = expanded_Rect(bounds_Widget(d), mulf_I2(gap2_UI, 1));
        shadowRect.pos.y += gap_UI / 4;
        fillRect_Paint(&p, shadowRect, /*isLight ? white_ColorId :*/ black_ColorId);
        SDL_SetRenderDrawBlendMode(renderer_Window(get_Window()), SDL_BLENDMODE_NONE);
    }
    if (fadeBackground) {
        iPaint p;
        init_Paint(&p);
        p.alpha = 0x50;
        SDL_SetRenderDrawBlendMode(renderer_Window(get_Window()), SDL_BLENDMODE_BLEND);
        int fadeColor;
        switch (colorTheme_App()) {
            default:
                fadeColor = black_ColorId;
                break;
            case light_ColorTheme:
                fadeColor = gray25_ColorId;
                break;
            case pureWhite_ColorTheme:
                fadeColor = gray50_ColorId;
                break;
        }
        fillRect_Paint(&p,
                       initCorners_Rect(zero_I2(), rootSize_Window(get_Window())),
                       fadeColor);
        SDL_SetRenderDrawBlendMode(renderer_Window(get_Window()), SDL_BLENDMODE_NONE);
    }
    if (d->bgColor >= 0 || d->frameColor >= 0) {
        iRect rect = bounds_Widget(d);
        if (d->flags & drawBackgroundToBottom_WidgetFlag) {
            rect.size.y = rootSize_Window(get_Window()).y - top_Rect(rect);
        }
        iPaint p;
        init_Paint(&p);
        if (d->bgColor >= 0) {
#if defined (iPlatformAppleMobile)
            if (d->flags & (drawBackgroundToHorizontalSafeArea_WidgetFlag |
                            drawBackgroundToVerticalSafeArea_WidgetFlag)) {
                const iInt2 rootSize = rootSize_Window(get_Window());
                const iInt2 center = divi_I2(rootSize, 2);
                int top = 0, right = 0, bottom = 0, left = 0;
                if (d->flags & drawBackgroundToHorizontalSafeArea_WidgetFlag) {
                    const iBool isWide = width_Rect(rect) > rootSize.x * 9 / 10;
                    if (isWide || mid_Rect(rect).x < center.x) {
                        left = -left_Rect(rect);
                    }
                    if (isWide || mid_Rect(rect).x > center.x) {
                        right = rootSize.x - right_Rect(rect);
                    }
                }
                if (d->flags & drawBackgroundToVerticalSafeArea_WidgetFlag) {
                    if (top_Rect(rect) > center.y) {
                        bottom = rootSize.y - bottom_Rect(rect);
                    }
                    if (bottom_Rect(rect) < center.y) {
                        top = -top_Rect(rect);
                    }
                }
                adjustEdges_Rect(&rect, top, right, bottom, left);
            }
#endif
            fillRect_Paint(&p, rect, d->bgColor);
        }
        if (d->frameColor >= 0 && ~d->flags & frameless_WidgetFlag) {
            drawRectThickness_Paint(&p, rect, gap_UI / 4, d->frameColor);
        }
    }
    if (d->flags & (borderTop_WidgetFlag | borderBottom_WidgetFlag)) {
        const iRect rect = bounds_Widget(d);
        iPaint p;
        init_Paint(&p);
        if (d->flags & borderTop_WidgetFlag) {
            drawHLine_Paint(&p, topLeft_Rect(rect), width_Rect(rect),
                            uiBackgroundFramelessHover_ColorId);
        }
        if (d->flags & borderBottom_WidgetFlag) {
            drawHLine_Paint(&p, addY_I2(bottomLeft_Rect(rect), -1), width_Rect(rect),
                            uiBackgroundFramelessHover_ColorId);
        }
    }
}

iLocalDef iBool isDrawn_Widget_(const iWidget *d) {
    return ~d->flags & hidden_WidgetFlag || d->flags & visualOffset_WidgetFlag;
}

void drawChildren_Widget(const iWidget *d) {
    if (!isDrawn_Widget_(d)) {
        return;
    }
    iConstForEach(ObjectList, i, d->children) {
        const iWidget *child = constAs_Widget(i.object);
        if (~child->flags & keepOnTop_WidgetFlag && isDrawn_Widget_(child)) {
            class_Widget(child)->draw(child);
        }
    }
    /* Root draws the on-top widgets on top of everything else. */
    if (!d->parent) {
        iConstForEach(PtrArray, i, onTop_RootData_()) {
            const iWidget *top = *i.value;
            draw_Widget(top);
        }
    }
}

void draw_Widget(const iWidget *d) {
    drawBackground_Widget(d);
    drawChildren_Widget(d);
}

iAny *addChild_Widget(iWidget *d, iAnyObject *child) {
    return addChildPos_Widget(d, child, back_WidgetAddPos);
}

iAny *addChildPos_Widget(iWidget *d, iAnyObject *child, enum iWidgetAddPos addPos) {
    iAssert(child);
    iAssert(d != child);
    iWidget *widget = as_Widget(child);
    iAssert(!widget->parent);
    if (!d->children) {
        d->children = new_ObjectList();
    }
    if (addPos == back_WidgetAddPos) {
        pushBack_ObjectList(d->children, widget); /* ref */
    }
    else {
        pushFront_ObjectList(d->children, widget); /* ref */
    }
    widget->parent = d;
    return child;
}

iAny *insertChildAfter_Widget(iWidget *d, iAnyObject *child, size_t afterIndex) {
    iAssert(child);
    iAssert(d != child);
    iWidget *widget = as_Widget(child);
    iAssert(!widget->parent);
    iAssert(d->children);
    iAssert(afterIndex < size_ObjectList(d->children));
    iForEach(ObjectList, i, d->children) {
        if (afterIndex-- == 0) {
            insertAfter_ObjectList(d->children, i.value, child);
            break;
        }
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
    iAssert(child);
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
    ((iWidget *) child)->parent = NULL;
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

size_t childIndex_Widget(const iWidget *d, const iAnyObject *child) {
    size_t index = 0;
    iConstForEach(ObjectList, i, d->children) {
        if (i.object == child) {
            return index;
        }
        index++;
    }
    return iInvalidPos;
}

iAny *hitChild_Widget(const iWidget *d, iInt2 coord) {
    if (d->flags & hidden_WidgetFlag) {
        return NULL;
    }
    /* Check for on-top widgets first. */
    if (!d->parent) {
        iReverseForEach(PtrArray, i, onTop_RootData_()) {
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
    if (cmp_String(id_Widget(d), id) == 0) {
        return iConstCast(iAny *, d);
    }
    iConstForEach(ObjectList, i, d->children) {
        iAny *found = findChild_Widget(constAs_Widget(i.object), id);
        if (found) return found;
    }
    return NULL;
}

iAny *findParentClass_Widget(const iWidget *d, const iAnyClass *class) {
    if (!d) return NULL;
    iWidget *i = d->parent;
    while (i && !isInstance_Object(i, class)) {
        i = i->parent;
    }
    return i;
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
    return rootData_.focus == d;
}

iBool isHover_Widget(const iAnyObject *d) {
    iAssert(isInstance_Object(d, &Class_Widget));
    return rootData_.hover == d;
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
        return src == widget || hasParent_Widget(src, widget);
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

void setFocus_Widget(iWidget *d) {
    if (rootData_.focus != d) {
        if (rootData_.focus) {
            iAssert(!contains_PtrSet(rootData_.pendingDestruction, rootData_.focus));
            postCommand_Widget(rootData_.focus, "focus.lost");
        }
        rootData_.focus = d;
        if (d) {
            iAssert(flags_Widget(d) & focusable_WidgetFlag);
            postCommand_Widget(d, "focus.gained");
        }
    }
}

iWidget *focus_Widget(void) {
    return rootData_.focus;
}

void setHover_Widget(iWidget *d) {
    rootData_.hover = d;
}

iWidget *hover_Widget(void) {
    return rootData_.hover;
}

static const iWidget *findFocusable_Widget_(const iWidget *d, const iWidget *startFrom,
                                            iBool *getNext, enum iWidgetFocusDir focusDir) {
    if (startFrom == d) {
        *getNext = iTrue;
        return NULL;
    }
    if ((d->flags & focusable_WidgetFlag) && isVisible_Widget(d) && !isDisabled_Widget(d) &&
        *getNext) {
        return d;
    }
    if (focusDir == forward_WidgetFocusDir) {
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

static const iWidget *findFocusRoot_Widget_(const iWidget *d) {
    iForEach(ObjectList, i, d->children) {
        const iWidget *root = findFocusRoot_Widget_(constAs_Widget(i.object));
        if (root) {
            return root;
        }
    }
    if (d->flags & focusRoot_WidgetFlag) {
        return d;
    }
    return NULL;
}

iAny *findFocusable_Widget(const iWidget *startFrom, enum iWidgetFocusDir focusDir) {
    const iWidget *root = findFocusRoot_Widget_(get_Window()->root);
    iAssert(root != NULL);
    iBool getNext = (startFrom ? iFalse : iTrue);
    const iWidget *found = findFocusable_Widget_(root, startFrom, &getNext, focusDir);
    if (!found && startFrom) {
        getNext = iTrue;
        found = findFocusable_Widget_(root, NULL, &getNext, focusDir);
    }
    return iConstCast(iWidget *, found);
}

void setMouseGrab_Widget(iWidget *d) {
    if (rootData_.mouseGrab != d) {
        rootData_.mouseGrab = d;
        SDL_CaptureMouse(d != NULL);
    }
}

iWidget *mouseGrab_Widget(void) {
    return rootData_.mouseGrab;
}

void postCommand_Widget(const iAnyObject *d, const char *cmd, ...) {
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
        appendFormat_String(&str, " ptr:%p", d);
    }
    postCommandString_App(&str);
    deinit_String(&str);
}

void refresh_Widget(const iAnyObject *d) {
    /* TODO: Could be widget specific, if parts of the tree are cached. */
    /* TODO: The visbuffer in DocumentWidget and ListWidget could be moved to be a general
       purpose feature of Widget. */
    iAssert(isInstance_Object(d, &Class_Widget));
    iUnused(d);
    postRefresh_App();
}

#include "labelwidget.h"
static void printTree_Widget_(const iWidget *d, int indent) {
    for (int i = 0; i < indent; ++i) {
        fwrite("    ", 4, 1, stdout);
    }
    printf("[%p] %s:\"%s\" ", d, class_Widget(d)->name, cstr_String(&d->id));
    if (isInstance_Object(d, &Class_LabelWidget)) {
        printf("(%s|%s) ",
               cstr_String(text_LabelWidget((const iLabelWidget *) d)),
               cstr_String(command_LabelWidget((const iLabelWidget *) d)));
    }
    printf("size:%dx%d [%d..%d %d:%d] flags:%08llx%s\n", d->rect.size.x, d->rect.size.y,
           d->padding[0], d->padding[2], d->padding[1], d->padding[3],
           (long long unsigned int) d->flags, d->flags & tight_WidgetFlag ? " tight" : "");
    iConstForEach(ObjectList, i, d->children) {
        printTree_Widget_(i.object, indent + 1);
    }
}

void raise_Widget(iWidget *d) {
    iPtrArray *onTop = onTop_RootData_();
    if (d->flags & keepOnTop_WidgetFlag) {
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

void printTree_Widget(const iWidget *d) {
    if (!d) {
        printf("[NULL]\n");
        return;
    }
    printTree_Widget_(d, 0);
}

iBeginDefineClass(Widget)
    .processEvent = processEvent_Widget,
    .draw         = draw_Widget,
iEndDefineClass(Widget)

