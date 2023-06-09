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

#include "listwidget.h"
#include "scrollwidget.h"
#include "paint.h"
#include "util.h"
#include "command.h"
#include "touch.h"
#include "visbuf.h"
#include "app.h"

#include <the_Foundation/intset.h>

void init_ListItem(iListItem *d) {
    d->isSeparator  = iFalse;
    d->isSelected   = iFalse;
    d->isDraggable  = iFalse;
    d->isDropTarget = iFalse;
}

void deinit_ListItem(iListItem *d) {
    iUnused(d);
}

iDefineObjectConstruction(ListItem)
iDefineClass(ListItem)

/*----------------------------------------------------------------------------------------------*/

iDefineObjectConstruction(ListWidget)

static void refreshWhileScrolling_ListWidget_(iAnyObject *any) {
    iListWidget *d = any;
    updateVisible_ListWidget(d);
    refresh_Widget(d);
    if (!isFinished_SmoothScroll(&d->scrollY)) {
        addTicker_App(refreshWhileScrolling_ListWidget_, any);
    }
}

static void scrollBegan_ListWidget_(iAnyObject *any, int offset, uint32_t span) {
    iListWidget *d = any;
    iUnused(span);
    if (offset) {
        if (d->hoverItem != iInvalidPos) {
            invalidateItem_ListWidget(d, d->hoverItem);
            d->hoverItem = iInvalidPos;
        }
        d->noHoverWhileScrolling = iTrue;
    }
    refreshWhileScrolling_ListWidget_(d);
}

static void visBufferInvalidated_ListWidget_(iVisBuf *d, size_t index) {
    /* Clear a texture to background color when invalidated. */
    iVisBufTexture *vbuf = &d->buffers[index];
    const iListWidget *list = vbuf->user;
    iPaint p;
    init_Paint(&p);
    beginTarget_Paint(&p, vbuf->texture);
    fillRect_Paint(&p, (iRect){ zero_I2(), d->texSize }, list->widget.bgColor);
    endTarget_Paint(&p);
}

void init_ListWidget(iListWidget *d) {
    iWidget *w = as_Widget(d);
    init_Widget(w);
    setId_Widget(w, "list");
    setBackgroundColor_Widget(w, uiBackground_ColorId); /* needed for filling visbuffer */
    setFlags_Widget(w, hover_WidgetFlag | focusable_WidgetFlag, iTrue);
    addChild_Widget(w, iClob(d->scroll = new_ScrollWidget()));
    setThumb_ScrollWidget(d->scroll, 0, 0);
    init_SmoothScroll(&d->scrollY, w, scrollBegan_ListWidget_);
    d->itemHeight = 0;
    d->scrollMode = normal_ScrollMode;
    d->noHoverWhileScrolling = iFalse;
    init_PtrArray(&d->items);
    d->cursorItem = iInvalidPos;
    d->hoverItem = iInvalidPos;
    d->dragItem = iInvalidPos;
    d->dragOrigin = zero_I2();
    d->dragHandleWidth = 0;
    initButtons_Click(&d->click, d, SDL_BUTTON_LMASK | SDL_BUTTON_MMASK);
    init_IntSet(&d->invalidItems);
    d->visBuf = new_VisBuf();
    iForIndices(i, d->visBuf->buffers) {
        d->visBuf->buffers[i].user = d;
    }
    d->visBuf->bufferInvalidated = visBufferInvalidated_ListWidget_;
}

void deinit_ListWidget(iListWidget *d) {
    removeTicker_App(refreshWhileScrolling_ListWidget_, d);
    clear_ListWidget(d);
    deinit_PtrArray(&d->items);
    delete_VisBuf(d->visBuf);
}

void invalidate_ListWidget(iListWidget *d) {
    invalidate_VisBuf(d->visBuf);
    clear_IntSet(&d->invalidItems); /* all will be drawn */
    refresh_Widget(as_Widget(d));
}

void invalidateItem_ListWidget(iListWidget *d, size_t index) {
    insert_IntSet(&d->invalidItems, index);
    refresh_Widget(d);
}

void clear_ListWidget(iListWidget *d) {
    iForEach(PtrArray, i, &d->items) {
        deref_Object(i.ptr);
    }
    clear_PtrArray(&d->items);
    d->hoverItem = iInvalidPos;
}

void addItem_ListWidget(iListWidget *d, iAnyObject *item) {
    pushBack_PtrArray(&d->items, ref_Object(item));
}

iScrollWidget *scroll_ListWidget(iListWidget *d) {
    return d->scroll;
}

size_t numItems_ListWidget(const iListWidget *d) {
    return size_PtrArray(&d->items);
}

static int scrollMax_ListWidget_(const iListWidget *d) {
    return iMax(0,
                (int) size_PtrArray(&d->items) * d->itemHeight -
                    height_Rect(innerBounds_Widget(constAs_Widget(d))));
}

void updateVisible_ListWidget(iListWidget *d) {
    const int   contentSize = size_PtrArray(&d->items) * d->itemHeight;
    const iRect bounds      = innerBounds_Widget(as_Widget(d));
    const iBool wasVisible  = isVisible_Widget(d->scroll);
    if (width_Rect(bounds) <= 0 || height_Rect(bounds) <= 0) {
        return;
    }
    /* The scroll widget's visibility depends on it having a valid non-zero size.
       However, this may be called during arrangement (sizeChanged_ListWidget_),
       which means the child hasn't been arranged yet. The child cannot update
       its visibility unless it knows its correct size. */
    arrange_Widget(as_Widget(d->scroll));
    setMax_SmoothScroll(&d->scrollY, scrollMax_ListWidget_(d));
    setRange_ScrollWidget(d->scroll, (iRangei){ 0, d->scrollY.max });    
    setThumb_ScrollWidget(d->scroll,
                          pos_SmoothScroll(&d->scrollY),
                          contentSize > 0 ? height_Rect(bounds_Widget(as_Widget(d->scroll))) *
                                                height_Rect(bounds) / contentSize
                                          : 0);
    if (wasVisible != isVisible_Widget(d->scroll)) {
        invalidate_ListWidget(d); /* clip margins changed */
    }
}

void setItemHeight_ListWidget(iListWidget *d, int itemHeight) {
    if (deviceType_App() != desktop_AppDeviceType) {
        itemHeight += 1.5 * gap_UI;
    }
    if (d->itemHeight != itemHeight) {
        d->itemHeight = itemHeight;
        invalidate_ListWidget(d);
    }
}

int scrollBarWidth_ListWidget(const iListWidget *d) {
    return isVisible_Widget(d->scroll) ? width_Widget(d->scroll) : 0;
}

int itemHeight_ListWidget(const iListWidget *d) {
    return d->itemHeight;
}

int scrollPos_ListWidget(const iListWidget *d) {
    return targetValue_Anim(&d->scrollY.pos);
}

void setScrollPos_ListWidget(iListWidget *d, int pos) {
    setValue_Anim(&d->scrollY.pos, pos, 0);
    d->hoverItem = iInvalidPos;
    refresh_Widget(as_Widget(d));
}

void setScrollMode_ListWidget(iListWidget *d, enum iScrollMode mode) {
    d->scrollMode = mode;
}

void setDragHandleWidth_ListWidget(iListWidget *d, int dragHandleWidth) {
    d->dragHandleWidth = dragHandleWidth;
    if (dragHandleWidth == 0) {
        setFlags_Widget(as_Widget(d), touchDrag_WidgetFlag, iFalse); /* mobile drag handles */
    }
}

void scrollOffset_ListWidget(iListWidget *d, int offset) {
    moveSpan_SmoothScroll(&d->scrollY, offset, 0);
}

void scrollOffsetSpan_ListWidget(iListWidget *d, int offset, uint32_t span) {
    moveSpan_SmoothScroll(&d->scrollY, offset, span);
}

void scrollToItem_ListWidget(iListWidget *d, size_t index, uint32_t span) {
    if (index >= size_PtrArray(&d->items)) {
        return;
    }
    stop_Anim(&d->scrollY.pos);
    const iRect rect    = innerBounds_Widget(as_Widget(d));
    int         yTop    = d->itemHeight * index - pos_SmoothScroll(&d->scrollY);
    int         yBottom = yTop + d->itemHeight;
    if (yBottom > height_Rect(rect)) {
        scrollOffsetSpan_ListWidget(d, yBottom - height_Rect(rect), span);
    }
    else if (yTop < 0) {
        scrollOffsetSpan_ListWidget(d, yTop, span);
    }
}

int visCount_ListWidget(const iListWidget *d) {
    return iMin(height_Rect(innerBounds_Widget(constAs_Widget(d))) / d->itemHeight,
                (int) size_PtrArray(&d->items));
}

#if 0
static iRanges visRange_ListWidget_(const iListWidget *d) {
    if (d->itemHeight == 0) {
        return (iRanges){ 0, 0 };
    }
    iRanges vis = { d->scrollY / d->itemHeight, 0 };
    vis.end = iMin(size_PtrArray(&d->items), vis.start + visCount_ListWidget(d) + 1);
    return vis;
}
#endif

size_t itemIndex_ListWidget(const iListWidget *d, iInt2 pos) {
    const iRect bounds = innerBounds_Widget(constAs_Widget(d));
    pos.y -= top_Rect(bounds) - pos_SmoothScroll(&d->scrollY);
    if (pos.y < 0 || !d->itemHeight) return iInvalidPos;
    size_t index = pos.y / d->itemHeight;
    if (index >= size_Array(&d->items)) return iInvalidPos;
    return index;
}

const iAnyObject *constItem_ListWidget(const iListWidget *d, size_t index) {
    if (index < size_PtrArray(&d->items)) {
        return constAt_PtrArray(&d->items, index);
    }
    return NULL;
}

const iAnyObject *constDragItem_ListWidget(const iListWidget *d) {
    return constItem_ListWidget(d, d->dragItem);
}

const iAnyObject *constHoverItem_ListWidget(const iListWidget *d) {
    return constItem_ListWidget(d, d->hoverItem);
}

const iAnyObject *constCursorItem_ListWidget(const iListWidget *d) {
    return constItem_ListWidget(d, d->cursorItem);
}

iAnyObject *item_ListWidget(iListWidget *d, size_t index) {
    if (index < size_PtrArray(&d->items)) {
        return at_PtrArray(&d->items, index);
    }
    return NULL;
}

iAnyObject *hoverItem_ListWidget(iListWidget *d) {
    return item_ListWidget(d, d->hoverItem);
}

size_t hoverItemIndex_ListWidget(const iListWidget *d) {
    return d->hoverItem;
}

void setHoverItem_ListWidget(iListWidget *d, size_t index) {
    if (index < size_PtrArray(&d->items)) {
        const iListItem *item = at_PtrArray(&d->items, index);
        if (item->isSeparator) {
            index = iInvalidPos;
        }
    }
    if (d->hoverItem != index) {
        insert_IntSet(&d->invalidItems, d->hoverItem);
        insert_IntSet(&d->invalidItems, index);
        d->hoverItem = index;
        refresh_Widget(as_Widget(d));
    }
}

static void moveCursor_ListWidget_(iListWidget *d, int dir, uint32_t animSpan) {
    const size_t oldCursor = d->cursorItem;
    if (isEmpty_ListWidget(d)) {
        d->cursorItem = iInvalidPos;
    }
    else {
        const int maxItem = numItems_ListWidget(d) - 1;
        if (d->cursorItem == iInvalidPos) {
            d->cursorItem = 0;
        }
        d->cursorItem = iClamp((int) d->cursorItem + dir, 0, maxItem);
        while (((const iListItem *) constItem_ListWidget(d, d->cursorItem))->isSeparator &&
               ((d->cursorItem < maxItem && dir >= 0) || (d->cursorItem > 0 && dir < 0))) {
            d->cursorItem += (dir >= 0 ? 1 : -1); /* Skip separators. */
        }
    }    
    if (oldCursor != d->cursorItem) {
        invalidateItem_ListWidget(d, oldCursor);
        invalidateItem_ListWidget(d, d->cursorItem);
    }
    if (d->cursorItem != iInvalidPos) {
        scrollToItem_ListWidget(d, d->cursorItem, prefs_App()->uiAnimations ? animSpan : 0);
    }
}

void setCursorItem_ListWidget(iListWidget *d, size_t index) {
    invalidateItem_ListWidget(d, d->cursorItem);
    d->cursorItem = 0;
    moveCursor_ListWidget_(d, 0, 0);
}

void updateMouseHover_ListWidget(iListWidget *d) {
    const iInt2 mouse = mouseCoord_Window(
        get_Window(), deviceType_App() == desktop_AppDeviceType ? 0 : SDL_TOUCH_MOUSEID);
    setHoverItem_ListWidget(d, itemIndex_ListWidget(d, mouse));
}

void sort_ListWidget(iListWidget *d, int (*cmp)(const iListItem **item1, const iListItem **item2)) {
    sort_Array(&d->items, (iSortedArrayCompareElemFunc) cmp);
}

static void redrawHoverItem_ListWidget_(iListWidget *d) {
    insert_IntSet(&d->invalidItems, d->hoverItem);
    refresh_Widget(as_Widget(d));
}

static void sizeChanged_ListWidget_(iListWidget *d) {
    updateVisible_ListWidget(d);
    invalidate_ListWidget(d);
}

static void updateHover_ListWidget_(iListWidget *d, const iInt2 mouse) {
    size_t hover = iInvalidPos;
    if (!d->noHoverWhileScrolling &&
        !contains_Widget(constAs_Widget(d->scroll), mouse) &&
        contains_Widget(constAs_Widget(d), mouse)) {
        hover = itemIndex_ListWidget(d, mouse);
    }
    setHoverItem_ListWidget(d, hover);
}

enum iDragDestination {
    before_DragDestination,
    on_DragDestination,
    after_DragDestination
};

static size_t resolveDragDestination_ListWidget_(const iListWidget *d, iInt2 dstPos,
                                                 enum iDragDestination *dstKind) {
    size_t           index = itemIndex_ListWidget(d, dstPos);
    const iListItem *item  = constItem_ListWidget(d, index);
    if (!item) {
        index = (dstPos.y < mid_Rect(bounds_Widget(constAs_Widget(d))).y ? 0 : (numItems_ListWidget(d) - 1));
        item = constItem_ListWidget(d, index);
    }
    const iRect   rect = itemRect_ListWidget(d, index);
    const iRangei span = ySpan_Rect(rect);
    if (item->isDropTarget) {
        const int pad = size_Range(&span) / 4;
        if (dstPos.y >= span.start + pad && dstPos.y < span.end - pad) {
            *dstKind = on_DragDestination;
            return index;
        }
    }
    int delta = dstPos.y - top_Rect(rect);
    if (dstPos.y - span.start > span.end - dstPos.y) {
        index++;
        delta -= d->itemHeight;
    }
    index = iMin(index, numItems_ListWidget(d));
    *dstKind = delta < 0 && index > 0 ? after_DragDestination : before_DragDestination;
    return index;
}

static iBool endDrag_ListWidget_(iListWidget *d, iInt2 endPos) {
    if (d->dragItem == iInvalidPos) {
        return iFalse;
    }
    setFlags_Widget(as_Widget(d), touchDrag_WidgetFlag, iFalse); /* mobile drag handles */
    stop_Anim(&d->scrollY.pos);
    enum iDragDestination dstKind;
    const size_t index = resolveDragDestination_ListWidget_(d, endPos, &dstKind);
    if (index != d->dragItem) {
        if (dstKind == on_DragDestination) {
            postCommand_Widget(d, "list.dragged arg:%zu onto:%zu", d->dragItem, index);
        }
        else {
            postCommand_Widget(d, "list.dragged arg:%zu %s:%zu", d->dragItem, 
                               dstKind == after_DragDestination ? "after" : "before",
                               dstKind == after_DragDestination ? index - 1 : index);
        }
    }
    invalidateItem_ListWidget(d, d->dragItem);
    d->dragItem = iInvalidPos;
    return iTrue;
}

static void abortDrag_ListWidget_(iListWidget *d) {
    if (d->dragItem != iInvalidPos) {
        stop_Anim(&d->scrollY.pos);
        invalidateItem_ListWidget(d, d->dragItem);
        d->dragItem = iInvalidPos;
        redrawHoverItem_ListWidget_(d);
        setFlags_Widget(as_Widget(d), touchDrag_WidgetFlag, iFalse); /* mobile drag handles */
    }
}

static iBool isScrollDisabled_ListWidget_(const iListWidget *d, const SDL_Event *ev) {
    int dir = 0;
    if (ev->type == SDL_MOUSEWHEEL) {
        dir = iSign(ev->wheel.y);
    }
    switch (d->scrollMode) {
        case disabled_ScrollMode:
            return iTrue;
        case disabledAtTopBothDirections_ScrollMode:
            return scrollPos_ListWidget(d) <= 0;
        case disabledAtTopUpwards_ScrollMode:
            return scrollPos_ListWidget(d) <= 0 && dir > 0;
        default:
            break;
    }
    return iFalse;
}

static int cursorKeyStep_ListWidget_(const iListWidget *d, int sym) {
    const iWidget *w = constAs_Widget(d);
    const int dir = (sym == SDLK_UP || sym == SDLK_PAGEUP || sym == SDLK_HOME ? -1 : 1);
    switch (sym) {
        case SDLK_UP:
        case SDLK_DOWN:
            return dir;
        case SDLK_PAGEUP:
        case SDLK_PAGEDOWN:
            if (d->itemHeight) {
                return dir * (height_Rect(innerBounds_Widget(w)) / d->itemHeight - 1);
            }
            return dir;
        case SDLK_HOME:
        case SDLK_END:
            return dir * numItems_ListWidget(d);
    }
    return 0;
}

static iBool processEvent_ListWidget_(iListWidget *d, const SDL_Event *ev) {
    iWidget *w = as_Widget(d);
    if (isMetricsChange_UserEvent(ev)) {
        invalidate_ListWidget(d);
    }
    else if (!isScrollDisabled_ListWidget_(d, ev) && processEvent_SmoothScroll(&d->scrollY, ev)) {
        return iTrue;
    }
    else if (isCommand_SDLEvent(ev)) {
        const char *cmd = command_UserEvent(ev);
        if (equal_Command(cmd, "theme.changed")) {
            invalidate_ListWidget(d);
        }
        else if (isCommand_Widget(w, ev, "scroll.moved")) {
            setScrollPos_ListWidget(d, arg_Command(cmd));
            return iTrue;
        }
        else if (equal_Command(cmd, "contextkey") && isFocused_Widget(w) &&
                 d->cursorItem != iInvalidPos) {
            emulateMouseClickPos_Widget(
                w, SDL_BUTTON_RIGHT, mid_Rect(itemRect_ListWidget(d, d->cursorItem)));
            setHoverItem_ListWidget(d, d->cursorItem);
            return iTrue;
        }
        else if (isCommand_Widget(w, ev, "focus.gained")) {
            moveCursor_ListWidget_(d, 0, 0); /* clamp */
            invalidateItem_ListWidget(d, d->cursorItem);
            refresh_Widget(d);
            return iFalse;
        }
        else if (isCommand_Widget(w, ev, "focus.lost")) {
            invalidateItem_ListWidget(d, d->cursorItem);
            refresh_Widget(d);
            return iFalse;
        }
    }        
    else if (ev->type == SDL_USEREVENT && ev->user.code == widgetTapBegins_UserEventCode) {
        d->noHoverWhileScrolling = iFalse;
    }
    if (ev->type == SDL_KEYDOWN && isFocused_Widget(w)) {
        if (ev->key.keysym.mod == 0) {
            const int key = ev->key.keysym.sym;
            switch (key) {
                case SDLK_UP:
                case SDLK_DOWN:
                case SDLK_PAGEUP:
                case SDLK_PAGEDOWN:
                case SDLK_HOME:
                case SDLK_END: {
                    if (d->scrollMode == normal_ScrollMode) {
                        const int step = cursorKeyStep_ListWidget_(d, key);
                        moveCursor_ListWidget_(d, step, iAbs(step) == 1 ? 0 : 150);
                        return iTrue;
                    }
                    return iFalse;
                }
                case SDLK_RETURN:
                case SDLK_KP_ENTER:
                case SDLK_SPACE:
                    if (d->cursorItem != iInvalidPos) {
                        postCommand_Widget(w,
                                           "list.clicked arg:%zu item:%p button:%d",
                                           d->cursorItem,
                                           constCursorItem_ListWidget(d),
                                           SDL_BUTTON_LEFT);
                    }
                    return iTrue;
            }
        }
    }    
    if (ev->type == SDL_MOUSEMOTION) {
        const iInt2 mousePos = init_I2(ev->motion.x, ev->motion.y);
        if (ev->motion.state == 0 /* not dragging */) {
            if (ev->motion.which != SDL_TOUCH_MOUSEID) {
                d->noHoverWhileScrolling = iFalse;
            }
            updateHover_ListWidget_(d, mousePos);
        }
        else if (d->dragItem != iInvalidPos) {
            /* Start scrolling if near the ends. */
            const int zone = 2 * d->itemHeight;
            const iRect bounds = bounds_Widget(w);
            float scrollSpeed = 0.0f;
            if (mousePos.y > bottom_Rect(bounds) - zone) {
                scrollSpeed = (mousePos.y - bottom_Rect(bounds) + zone) / (float) zone;
            }
            else if (mousePos.y < top_Rect(bounds) + zone) {
                scrollSpeed = -(top_Rect(bounds) + zone - mousePos.y) / (float) zone;
            }
            scrollSpeed = iClamp(scrollSpeed, -1.0f, 1.0f);
            if (iAbs(scrollSpeed) < 0.001f) {
                stop_Anim(&d->scrollY.pos);
                refresh_Widget(d);
            }
            else {
                setFlags_Anim(&d->scrollY.pos, easeBoth_AnimFlag, iFalse);
                setValueSpeed_Anim(&d->scrollY.pos, scrollSpeed < 0 ? 0 : scrollMax_ListWidget_(d),
                                   scrollSpeed * scrollSpeed * gap_UI * 400);
                refreshWhileScrolling_ListWidget_(d);
            }
        }
    }
    if (ev->type == SDL_MOUSEWHEEL && isHover_Widget(w) && ev->wheel.x == 0) {
        if (d->dragHandleWidth) {
            if (d->dragItem == iInvalidPos) {
                const iInt2 wpos = coord_MouseWheelEvent(&ev->wheel);
                if (contains_Widget(w, wpos) &&
                    wpos.x >= right_Rect(boundsWithoutVisualOffset_Widget(w)) - d->dragHandleWidth) {
                    setFlags_Widget(w, touchDrag_WidgetFlag, iTrue);
//                    printf("[%p] touch drag started\n", d);
                    return iTrue;
                }
            }
        }
        if (isScrollDisabled_ListWidget_(d, ev)) {
            if (ev->wheel.which == SDL_TOUCH_MOUSEID) {
                /* TODO: Could generalize this selection of the scrollable parent. */
                extern iWidgetClass Class_SidebarWidget;
                iWidget *sidebar = findParentClass_Widget(w, &Class_SidebarWidget);
                if (sidebar) {
                    transferAffinity_Touch(w, sidebar);
                    d->noHoverWhileScrolling = iTrue;
                }
            }
            return iFalse;
        }
        int amount = -ev->wheel.y;
        if (isPerPixel_MouseWheelEvent(&ev->wheel)) {
            stop_Anim(&d->scrollY.pos);
            moveSpan_SmoothScroll(&d->scrollY, amount, 0);
        }
        else {
            /* Traditional mouse wheel. */
            amount *= 3 * d->itemHeight;
            moveSpan_SmoothScroll(
                &d->scrollY, amount, 600 * scrollSpeedFactor_Prefs(prefs_App(), mouse_ScrollType));
        }
        return iTrue;
    }
    if (ev->type == SDL_MOUSEWHEEL && isHover_Widget(w) && ev->wheel.y == 0 &&
        isPerPixel_MouseWheelEvent(&ev->wheel) && !isInertia_MouseWheelEvent(&ev->wheel)) {
        iInt2 coord = mouseCoord_SDLEvent(ev);
        postCommand_Widget(w, "listswipe.moved arg:%d coord:%d %d", ev->wheel.x, coord.x, coord.y);
        return iTrue;
    }
    switch (processEvent_Click(&d->click, ev)) {
        case started_ClickResult:
            d->noHoverWhileScrolling = iFalse;
            updateHover_ListWidget_(d, mouseCoord_Window(get_Window(), ev->button.which));
            redrawHoverItem_ListWidget_(d);
            return iTrue;
        case aborted_ClickResult:
            abortDrag_ListWidget_(d);
            break;
        case drag_ClickResult:
            if (d->click.clickButton != SDL_BUTTON_LEFT) {
                return iFalse;
            }
            if (d->dragItem == iInvalidPos && length_I2(delta_Click(&d->click)) > gap_UI) {
                const size_t over = itemIndex_ListWidget(d, d->click.startPos);
                if (over != iInvalidPos &&
                    ((const iListItem *) item_ListWidget(d, over))->isDraggable) {
                    d->dragItem = over;
                    d->dragOrigin = sub_I2(topLeft_Rect(itemRect_ListWidget(d, over)),
                                           d->click.startPos);
                    invalidateItem_ListWidget(d, d->dragItem);
                }
            }
            return d->dragItem != iInvalidPos;
        case finished_ClickResult:
            if (endDrag_ListWidget_(d, pos_Click(&d->click))) {
                return iTrue;
            }
            redrawHoverItem_ListWidget_(d);
            if (contains_Rect(adjusted_Rect(itemRect_ListWidget(d, d->hoverItem),
                                            zero_I2(), init_I2(-d->dragHandleWidth, 0)),
                              pos_Click(&d->click)) &&
                d->hoverItem != iInvalidPos) {
                postCommand_Widget(w, "list.clicked arg:%zu button:%d item:%p",
                                   d->hoverItem,
                                   d->click.clickButton,
                                   constHoverItem_ListWidget(d));
            }
            return iTrue;
        default:
            break;
    }
    return processEvent_Widget(w, ev);
}

iRect itemRect_ListWidget(const iListWidget *d, size_t index) {
    const iRect bounds  = innerBounds_Widget(constAs_Widget(d));
    const int   scrollY = pos_SmoothScroll(&d->scrollY);
    return (iRect){ addY_I2(topLeft_Rect(bounds), d->itemHeight * (int) index - scrollY),
                    init_I2(width_Rect(bounds), d->itemHeight) };
}

static void draw_ListWidget_(const iListWidget *d) {
    const iWidget *w      = constAs_Widget(d);
    const iRect    bounds = innerBounds_Widget(w);
    if (!bounds.size.y || !bounds.size.x || !d->itemHeight) {
        return;
    }
    const int scrollY = pos_SmoothScroll(&d->scrollY);
    iPaint p;
    init_Paint(&p);
    drawLayerEffects_Widget(w);
    drawBackground_Widget(w);
    alloc_VisBuf(d->visBuf, bounds.size, d->itemHeight);
    /* Update invalid regions/items. */ {
        /* TODO: This seems to draw two items per each shift of the visible region, even though
           one should be enough. Probably an off-by-one error in the calculation of the
           invalid range. */
        iForIndices(i, d->visBuf->buffers) {
            iAssert(d->visBuf->buffers[i].texture);
        }
        const int bg[iElemCount(d->visBuf->buffers)] = {
            w->bgColor, w->bgColor, w->bgColor, w->bgColor /* Debug: Separate BG color for buffers. */
        };
        const int bottom = numItems_ListWidget(d) * d->itemHeight;
        const iRangei vis = { scrollY / d->itemHeight * d->itemHeight,
                             ((scrollY + bounds.size.y) / d->itemHeight + 1) * d->itemHeight };
        reposition_VisBuf(d->visBuf, vis);
        /* Check which parts are invalid. */
        iRangei invalidRange[iElemCount(d->visBuf->buffers)];
        invalidRanges_VisBuf(d->visBuf, (iRangei){ 0, bottom }, invalidRange);
        iForIndices(i, d->visBuf->buffers) {
            iVisBufTexture *buf = &d->visBuf->buffers[i];
            iRanges drawItems = { iMax(0, buf->origin) / d->itemHeight,
                                  iMax(0, buf->origin + d->visBuf->texSize.y) / d->itemHeight };
#if 0
            if (isEmpty_Rangei(buf->validRange)) {
                beginTarget_Paint(&p, buf->texture);
                fillRect_Paint(&p, (iRect){ zero_I2(), d->visBuf->texSize }, bg[i]);
            }
#endif
#if defined (iPlatformApple)
            const int blankWidth = 0; /* scrollbars fade away */
#else
            const int blankWidth = scrollBarWidth_ListWidget(d);
#endif
            const iRect sbBlankRect = { init_I2(d->visBuf->texSize.x - blankWidth, 0),
                                        init_I2(blankWidth, d->itemHeight) };
            iConstForEach(IntSet, v, &d->invalidItems) {
                const size_t index = *v.value;
                if (contains_Range(&drawItems, index) && index < size_PtrArray(&d->items)) {
                    const iListItem *item = constAt_PtrArray(&d->items, index);
                    const iRect      itemRect = { init_I2(0, index * d->itemHeight - buf->origin),
                                                  init_I2(d->visBuf->texSize.x, d->itemHeight) };
                    beginTarget_Paint(&p, buf->texture);
                    fillRect_Paint(&p, itemRect, bg[i]);
                    if (index != d->dragItem) {
                        class_ListItem(item)->draw(item, &p, itemRect, d);
                    }
                    fillRect_Paint(&p, moved_Rect(sbBlankRect, init_I2(0, top_Rect(itemRect))), bg[i]);
                }
            }
            /* Visible range is not fully covered. Fill in the new items. */
            if (!isEmpty_Rangei(invalidRange[i])) {
                beginTarget_Paint(&p, buf->texture);
                drawItems.start = invalidRange[i].start / d->itemHeight;
                drawItems.end   = invalidRange[i].end   / d->itemHeight + 1;
                for (size_t j = drawItems.start; j < drawItems.end && j < size_PtrArray(&d->items); j++) {
                    const iListItem *item     = constAt_PtrArray(&d->items, j);
                    const iRect      itemRect = { init_I2(0, j * d->itemHeight - buf->origin),
                                                  init_I2(d->visBuf->texSize.x, d->itemHeight) };
                    fillRect_Paint(&p, itemRect, bg[i]);
                    if (j != d->dragItem) {
                        class_ListItem(item)->draw(item, &p, itemRect, d);
                    }
                    fillRect_Paint(&p, moved_Rect(sbBlankRect, init_I2(0, top_Rect(itemRect))), bg[i]);
                }
            }
            endTarget_Paint(&p);
        }
        validate_VisBuf(d->visBuf);
        clear_IntSet(&iConstCast(iListWidget *, d)->invalidItems);
    }
    setClip_Paint(&p, bounds_Widget(w));
    draw_VisBuf(d->visBuf, addY_I2(topLeft_Rect(bounds), -scrollY), ySpan_Rect(bounds));
    const iBool isMobile = (deviceType_App() != desktop_AppDeviceType);
    const iInt2 mousePos = mouseCoord_Window(get_Window(), isMobile ? SDL_TOUCH_MOUSEID : 0);
    if (d->dragItem != iInvalidPos && (isMobile || contains_Rect(bounds, mousePos))) {
        iInt2 pos = add_I2(mousePos, d->dragOrigin);
        const iListItem *item = constAt_PtrArray(&d->items, d->dragItem);
        const iRect itemRect = { init_I2(left_Rect(bounds), pos.y),
                                 init_I2(d->visBuf->texSize.x, d->itemHeight) };
        SDL_SetRenderDrawBlendMode(renderer_Window(get_Window()), SDL_BLENDMODE_BLEND);
        enum iDragDestination dstKind;
        const size_t dstIndex = resolveDragDestination_ListWidget_(d, mousePos, &dstKind);
        if (dstIndex != d->dragItem) {
            const iRect dstRect = itemRect_ListWidget(d, dstIndex);
            p.alpha = 0xff;
            if (dstKind == on_DragDestination) {
                drawRectThickness_Paint(&p, dstRect, gap_UI / 2, uiTextAction_ColorId);
            }
            else if (dstIndex != d->dragItem + 1) {
                fillRect_Paint(&p, (iRect){ addY_I2(dstRect.pos, -gap_UI / 4),
                                            init_I2(width_Rect(dstRect), gap_UI / 2) },
                               uiTextAction_ColorId);
            }                        
        }
        p.alpha = 0x80;
        setOpacity_Text(0.5f);
        class_ListItem(item)->draw(item, &p, itemRect, d);
        setOpacity_Text(1.0f);
        SDL_SetRenderDrawBlendMode(renderer_Window(get_Window()), SDL_BLENDMODE_NONE);
    }
    unsetClip_Paint(&p);
    drawBorders_Widget(w); /* background overdraws the normal borders */
    drawChildren_Widget(w);
}

iBool isMouseDown_ListWidget(const iListWidget *d) {
    return d->click.isActive &&
           contains_Rect(innerBounds_Widget(constAs_Widget(d)), pos_Click(&d->click));
}

iBeginDefineSubclass(ListWidget, Widget)
    .processEvent = (iAny *) processEvent_ListWidget_,
    .draw         = (iAny *) draw_ListWidget_,
    .sizeChanged  = (iAny *) sizeChanged_ListWidget_,
iEndDefineSubclass(ListWidget)
