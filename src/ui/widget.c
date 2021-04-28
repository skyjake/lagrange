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
#include "root.h"
#include "util.h"
#include "window.h"

#include <the_Foundation/ptrarray.h>
#include <the_Foundation/ptrset.h>
#include <SDL_mouse.h>
#include <stdarg.h>

#if defined (iPlatformAppleMobile)
#   include "../ios.h"
#endif

void releaseChildren_Widget(iWidget *d) {
    iForEach(ObjectList, i, d->children) {
        ((iWidget *) i.object)->parent = NULL; /* the actual reference being held */
    }
    iReleasePtr(&d->children);
}

iDefineObjectConstruction(Widget)

void init_Widget(iWidget *d) {
    init_String(&d->id);
    d->root           = get_Root(); /* never changes after this */
    d->flags          = 0;
    d->rect           = zero_Rect();
    d->minSize        = zero_I2();
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
        d->flags &= ~visualOffset_WidgetFlag;
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
        removeAll_PtrArray(onTop_Root(d->root), d);
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
        removeOne_PtrArray(onTop_Root(d->root), d);
    }
    if (isHover_Widget(d)) {
        get_Window()->hover = NULL;
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
        if (!d->root->pendingDestruction) {
            d->root->pendingDestruction = new_PtrSet();
        }
        insert_PtrSet(d->root->pendingDestruction, d);
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
        iChangeFlags(d->flags, flags, set);
        if (flags & keepOnTop_WidgetFlag) {
            iPtrArray *onTop = onTop_Root(d->root);
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

void setFixedSize_Widget(iWidget *d, iInt2 fixedSize) {
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
    d->padding[0] = left;
    d->padding[1] = top;
    d->padding[2] = right;
    d->padding[3] = bottom;
}

iWidget *root_Widget(const iWidget *d) {
    return d ? d->root->widget : NULL;
}

void showCollapsed_Widget(iWidget *d, iBool show) {
    const iBool isVisible = !(d->flags & hidden_WidgetFlag);
    if ((isVisible && !show) || (!isVisible && show)) {
        setFlags_Widget(d, hidden_WidgetFlag, !show);
        /* The entire UI may be affected, if parents are resized due to the (un)collapsing. */
        arrange_Widget(root_Widget(d));
        postRefresh_App();
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

static void arrange_Widget_(iWidget *);
static const iBool tracing_ = iFalse;

#define TRACE(d, ...)   if (tracing_) { printf_Widget_(d, __VA_ARGS__); }

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

static void setWidth_Widget_(iWidget *d, int width) {
    iAssert(width >= 0);
    TRACE(d, "attempt to set width to %d (current: %d, min width: %d)", width, d->rect.size.x, d->minSize.x);
    width = iMax(width, d->minSize.x);
    if (~d->flags & fixedWidth_WidgetFlag || d->flags & collapse_WidgetFlag) {
        if (d->rect.size.x != width) {
            d->rect.size.x = width;
            TRACE(d, "width has changed to %d", width);
            if (class_Widget(d)->sizeChanged) {
                const int oldHeight = d->rect.size.y;
                class_Widget(d)->sizeChanged(d);
                if (d->rect.size.y != oldHeight) {
                    TRACE(d, "sizeChanged() cuased height change to %d; redoing parent", d->rect.size.y);
                    /* Widget updated its height. */
                    arrange_Widget_(d->parent);
                    TRACE(d, "parent layout redone");
                }
            }
        }
    }
    else {
        TRACE(d, "changing width not allowed; flags: %x", d->flags);
    }
}

static void setHeight_Widget_(iWidget *d, int height) {
    iAssert(height >= 0);
    TRACE(d, "attempt to set height to %d (current: %d, min height: %d)", height, d->rect.size.y, d->minSize.y);
    height = iMax(height, d->minSize.y);
    if (~d->flags & fixedHeight_WidgetFlag || d->flags & collapse_WidgetFlag) {
        if (d->rect.size.y != height) {
            d->rect.size.y = height;
            TRACE(d, "height has changed to %d", height);
            if (class_Widget(d)->sizeChanged) {
                class_Widget(d)->sizeChanged(d);
            }
        }
    }
    else {
        TRACE(d, "changing height not allowed; flags: %x", d->flags);
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
        if (isArranged_Widget_(i.object)) {
            count++;
        }
    }
    return count;
}

static void centerHorizontal_Widget_(iWidget *d) {
    d->rect.pos.x = ((d->parent ? width_Rect(innerRect_Widget_(d->parent))
                                : size_Root(d->root).x) -
                     width_Rect(d->rect)) /
                    2;
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
        }
        if (isEmpty_Rect(*bounds_out)) {
            *bounds_out = childRect;
        }
        else {
            *bounds_out = union_Rect(*bounds_out, childRect);
        }
    }
}

static void arrange_Widget_(iWidget *d) {
    TRACE(d, "arranging...");
    if (isCollapsed_Widget_(d)) {
        TRACE(d, "collapsed => END");
        setFlags_Widget(d, wasCollapsed_WidgetFlag, iTrue);
        return;
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
        d->rect.pos.y = height_Rect(innerRect_Widget_(d->parent)) - height_Rect(d->rect);
        TRACE(d, "move to parent bottom edge: %d", d->rect.pos.y);
    }
    else if (d->flags & centerHorizontal_WidgetFlag) {
        centerHorizontal_Widget_(d);
    }
    if (d->flags & resizeToParentWidth_WidgetFlag) {
        iRect childBounds = zero_Rect();
        if (flags_Widget(d->parent) & arrangeWidth_WidgetFlag) {
            /* Can't go narrower than what the children require, though. */
            boundsOfChildren_Widget_(d, &childBounds);
        }
        TRACE(d, "resize to parent width; child bounds width %d", childBounds.size.x, childBounds.size.y);
        setWidth_Widget_(d, iMaxi(width_Rect(innerRect_Widget_(d->parent)),
                                  width_Rect(childBounds)));
    }
    if (d->flags & resizeToParentHeight_WidgetFlag) {
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
    /* Resize children to fill the parent widget. */
    if (d->flags & resizeChildren_WidgetFlag) {
        const iInt2 dirs = init_I2((d->flags & resizeWidthOfChildren_WidgetFlag) != 0,
                                   (d->flags & resizeHeightOfChildren_WidgetFlag) != 0);
        TRACE(d, "resize children, x:%d y:%d", dirs.x, dirs.y);
        /* Collapse hidden children. */
        iBool collapseChanged = iFalse;
        iForEach(ObjectList, c, d->children) {
            iWidget *child = as_Widget(c.object);
            if (!isCollapsed_Widget_(child) && child->flags & wasCollapsed_WidgetFlag) {
                setFlags_Widget(child, wasCollapsed_WidgetFlag, iFalse);
                TRACE(d, "child %p is uncollapsed", child);
                /* Undo collapse and determine the normal size again. */
                arrange_Widget_(d);
                collapseChanged = iTrue;
            }
            else if (isCollapsed_Widget_(child) && ~child->flags & wasCollapsed_WidgetFlag) {
                setFlags_Widget(child, wasCollapsed_WidgetFlag, iTrue);
                collapseChanged = iTrue;
                TRACE(d, "child %p flagged as collapsed", child);
            }
        }
        if (collapseChanged) {
            TRACE(d, "redoing arrangement due to changes in child collapse state");
            arrange_Widget_(d); /* Redo with the new child sizes. */
            return;
        }
        const int expCount = numExpandingChildren_Widget_(d);
        TRACE(d, "%d expanding children", expCount);
        /* Only resize the expanding children, not touching the others. */
        if (expCount > 0) {
            iInt2 avail = innerRect_Widget_(d).size;
            TRACE(d, "inner size: %dx%d", avail.x, avail.y);
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
            TRACE(d, "changing child sizes (expand mode)...");
            iForEach(ObjectList, j, d->children) {
                iWidget *child = as_Widget(j.object);
                if (!isArranged_Widget_(child)) {
                    TRACE(d, "child %p is not arranged", child);
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
            TRACE(d, "...done changing child sizes (expand mode)");
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
                if (isArranged_Widget_(child) && ~child->flags & parentCannotResize_WidgetFlag) {
                    if (dirs.x) {
                        setWidth_Widget_(child, child->flags & unpadded_WidgetFlag ? unpaddedChildSize.x : childSize.x);
                    }
                    if (dirs.y && ~child->flags & parentCannotResizeHeight_WidgetFlag) {
                        setHeight_Widget_(child, child->flags & unpadded_WidgetFlag ? unpaddedChildSize.y : childSize.y);
                    }
                }
                else {
                    TRACE(d, "child %p cannot be resized (parentCannotResize: %d)", child,
                          (child->flags & parentCannotResize_WidgetFlag) != 0);
                }
            }
            TRACE(d, "...done changing child sizes (EVEN mode)");
        }
    }
    if (d->flags & resizeChildrenToWidestChild_WidgetFlag) {
        const int widest = widestChild_Widget_(d);
        TRACE(d, "resizing children to widest child (%d)...", widest);
        iForEach(ObjectList, i, d->children) {
            iWidget *child = as_Widget(i.object);
            if (isArranged_Widget_(child) && ~child->flags & parentCannotResize_WidgetFlag) {
                setWidth_Widget_(child, widest);
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
        arrange_Widget_(child);
        if (!isArranged_Widget_(child)) {
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

void resetSize_Widget(iWidget *d) {
    if (~d->flags & fixedWidth_WidgetFlag) {
        d->rect.size.x = d->minSize.x;
    }
    if (~d->flags & fixedHeight_WidgetFlag) {
        d->rect.size.y = d->minSize.y;
    }
    iForEach(ObjectList, i, children_Widget(d)) {
        iWidget *child = as_Widget(i.object);
        if (isArranged_Widget_(child)) {
            resetSize_Widget(child);
        }
    }
}

void arrange_Widget(iWidget *d) {
    //resetSize_Widget_(d); /* back to initial default sizes */
    arrange_Widget_(d);
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
#if defined (iPlatformMobile)
    window.y += value_Anim(&get_Window()->rootOffset);
#endif
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
        addv_I2(&innerCoord, w->rect.pos);
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
    const iRect bounds = {
        zero_I2(),
        addY_I2(d->rect.size,
                d->flags & drawBackgroundToBottom_WidgetFlag ? size_Root(d->root).y : 0)
    };
    return contains_Rect(expand ? expanded_Rect(bounds, init1_I2(expand)) : bounds,
                         windowToInner_Widget(d, windowCoord));
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
    get_Window()->hover = NULL;
}

iBool dispatchEvent_Widget(iWidget *d, const SDL_Event *ev) {
    iAssert(d->root == get_Root());
    if (!d->parent) {
        if (get_Window()->focus && get_Window()->focus->root == d->root && isKeyboardEvent_(ev)) {
            /* Root dispatches keyboard events directly to the focused widget. */
            if (dispatchEvent_Widget(get_Window()->focus, ev)) {
                return iTrue;
            }
        }
        /* Root offers events first to widgets on top. */
        iReverseForEach(PtrArray, i, d->root->onTop) {
            iWidget *widget = *i.value;
            if (isVisible_Widget(widget) && dispatchEvent_Widget(widget, ev)) {
#if 0
                if (ev->type == SDL_KEYDOWN) {
                    printf("[%p] %s:'%s' (on top) ate the key\n",
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
             (!get_Window()->hover || hasParent_Widget(d, get_Window()->hover)) &&
             flags_Widget(d) & hover_WidgetFlag && ~flags_Widget(d) & hidden_WidgetFlag &&
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
            iAssert(child->root == d->root);
            if (child == get_Window()->focus && isKeyboardEvent_(ev)) {
                continue; /* Already dispatched. */
            }
            if (isVisible_Widget(child) && child->flags & keepOnTop_WidgetFlag) {
                /* Already dispatched. */
                continue;
            }
            if (dispatchEvent_Widget(child, ev)) {
#if 0
                if (ev->type == SDL_KEYDOWN) {
                    printf("[%p] %s:'%s' ate the key\n",
                           child, class_Widget(child)->name,
                           cstr_String(id_Widget(child)));
                    fflush(stdout);
                }
#endif
#if 0
                if (ev->type == SDL_MOUSEMOTION) {
                    printf("[%p] %s:'%s' (on top) ate the motion\n",
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
        if (class_Widget(d)->processEvent(d, ev)) {
            return iTrue;
        }
    }
    return iFalse;
}

static iBool scrollOverflow_Widget_(iWidget *d, int delta) {
    iRect bounds = bounds_Widget(d);
    const iInt2 rootSize = size_Root(d->root);
    const iRect winRect = safeRect_Root(d->root);
    const int yTop = top_Rect(winRect);
    const int yBottom = bottom_Rect(winRect);
    //const int safeBottom = rootSize.y - yBottom;
    bounds.pos.y += delta;
    const iRangei range = { bottom_Rect(winRect) - height_Rect(bounds), yTop };
//    printf("range: %d ... %d\n", range.start, range.end);
    if (range.start >= range.end) {
        bounds.pos.y = range.end;
    }
    else {
        bounds.pos.y = iClamp(bounds.pos.y, range.start, range.end);
    }
//    if (delta >= 0) {
//        bounds.pos.y = iMin(bounds.pos.y, yTop);
//    }
//    else {
//        bounds.pos.y = iMax(bounds.pos.y, );
//    }
    const iInt2 newPos = windowToInner_Widget(d->parent, bounds.pos);
    if (!isEqual_I2(newPos, d->rect.pos)) {
        d->rect.pos = newPos;
        refresh_Widget(d);
    }
    return height_Rect(bounds) > height_Rect(winRect);
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
    else if (d->flags & overflowScrollable_WidgetFlag && ev->type == SDL_MOUSEWHEEL &&
             ~d->flags & visualOffset_WidgetFlag) {
        int step = ev->wheel.y;
        if (!isPerPixel_MouseWheelEvent(&ev->wheel)) {
            step *= lineHeight_Text(uiLabel_FontId);
        }
        if (scrollOverflow_Widget_(d, step)) {
            return iTrue;
        }
    }
    switch (ev->type) {
        case SDL_USEREVENT: {
            if (d->flags & overflowScrollable_WidgetFlag &&
                ~d->flags & visualOffset_WidgetFlag &&
                isCommand_UserEvent(ev, "widget.overflow")) {
                scrollOverflow_Widget_(d, 0); /* check bounds */
            }
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
        return iTrue;
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
    iBool fadeBackground = (d->bgColor >= 0 || d->frameColor >= 0) && d->flags & mouseModal_WidgetFlag;
    if (deviceType_App() == phone_AppDeviceType) {
        if (shadowBorder) {
            fadeBackground = iTrue;
            shadowBorder = iFalse;
        }
    }
    if (shadowBorder) {
        iPaint p;
        init_Paint(&p);
        drawSoftShadow_Paint(&p, bounds_Widget(d), 12 * gap_UI, black_ColorId, 30);
    }
    if (fadeBackground && ~d->flags & noFadeBackground_WidgetFlag) {
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
                       rect_Root(d->root),
                       fadeColor);
        SDL_SetRenderDrawBlendMode(renderer_Window(get_Window()), SDL_BLENDMODE_NONE);
    }
    if (d->bgColor >= 0 || d->frameColor >= 0) {
        iRect rect = bounds_Widget(d);
        if (d->flags & drawBackgroundToBottom_WidgetFlag) {
            rect.size.y = size_Root(d->root).y - top_Rect(rect);
        }
        iPaint p;
        init_Paint(&p);
        if (d->bgColor >= 0) {
#if defined (iPlatformAppleMobile)
            if (d->flags & (drawBackgroundToHorizontalSafeArea_WidgetFlag |
                            drawBackgroundToVerticalSafeArea_WidgetFlag)) {
                const iInt2 rootSize = size_Root(d->root);
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
        iConstForEach(PtrArray, i, onTop_Root(d->root)) {
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
    return get_Window()->focus == d;
}

iBool isHover_Widget(const iAnyObject *d) {
    iAssert(isInstance_Object(d, &Class_Widget));
    return get_Window()->hover == d;
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

iBool isAffectedByVisualOffset_Widget(const iWidget *d) {
    for (const iWidget *w = d; w; w = w->parent) {
        if (w->flags & visualOffset_WidgetFlag) {
            return iTrue;
        }
    }
    return iFalse;
}

void setFocus_Widget(iWidget *d) {
    iWindow *win = get_Window();
    if (win->focus != d) {
        if (win->focus) {
            iAssert(!contains_PtrSet(win->focus->root->pendingDestruction, win->focus));
            postCommand_Widget(win->focus, "focus.lost");
        }
        win->focus = d;
        if (d) {
            iAssert(flags_Widget(d) & focusable_WidgetFlag);
            setKeyRoot_Window(get_Window(), d->root);
            postCommand_Widget(d, "focus.gained");
        }
    }
}

iWidget *focus_Widget(void) {
    return get_Window()->focus;
}

void setHover_Widget(iWidget *d) {
    get_Window()->hover = d;
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
    iRoot *uiRoot = (startFrom ? startFrom->root : get_Window()->keyRoot);
    const iWidget *focusRoot = findFocusRoot_Widget_(uiRoot->widget);
    iAssert(focusRoot != NULL);
    iBool getNext = (startFrom ? iFalse : iTrue);
    const iWidget *found = findFocusable_Widget_(focusRoot, startFrom, &getNext, focusDir);
    if (!found && startFrom) {
        getNext = iTrue;
        /* Switch to the next root, if available. */
        found = findFocusable_Widget_(findFocusRoot_Widget_(otherRoot_Window(get_Window(),
                                                                             uiRoot)->widget),
                                      NULL, &getNext, focusDir);
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
    postCommandString_Root(((const iWidget *) d)->root, &str);
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

void raise_Widget(iWidget *d) {
    iPtrArray *onTop = onTop_Root(d->root);
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
    printf("size:%dx%d {min:%dx%d} [%d..%d %d:%d] flags:%08llx%s%s%s%s%s\n",
           d->rect.size.x, d->rect.size.y,
           d->minSize.x, d->minSize.y,
           d->padding[0], d->padding[2],
           d->padding[1], d->padding[3],
           (long long unsigned int) d->flags,
           d->flags & expand_WidgetFlag ? " exp" : "",
           d->flags & tight_WidgetFlag ? " tight" : "",
           d->flags & fixedWidth_WidgetFlag ? " fixW" : "",
           d->flags & fixedHeight_WidgetFlag ? " fixH" : "",
           d->flags & resizeToParentWidth_WidgetFlag ? " rsPrnW" : "");
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

void identify_Widget(const iWidget *d) {
    if (!d) {
        puts("[NULL}");
        return;
    }
    int indent = 0;
    for (const iWidget *w = d; w; w = w->parent, indent++) {
        if (indent > 0) {
            for (int i = 0; i < indent; ++i) {
                fwrite("  ", 2, 1, stdout);
            }
        }
        printInfo_Widget_(w);
    }
    fflush(stdout);
}
