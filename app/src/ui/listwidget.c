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
#include "app.h"
#include "command.h"
#include "gamepad.h"
#include "keys.h"
#include "render/paint.h"
#include "scrollwidget.h"
#include "touch.h"
#include "util.h"
#include "visbuf.h"

#include <the_Foundation/intset.h>

void init_ListItem(iListItem *d) {
    iZap(d->flags);
    d->height = 0;
}

void deinit_ListItem(iListItem *d) {
    iUnused(d);
}

iDefineObjectConstruction(ListItem)
iDefineClass(ListItem)

/*----------------------------------------------------------------------------------------------*/

iDefineObjectConstruction(ListWidget)

static void selectRangeAt_ListWidget_(iListWidget *d, iInt2 pos);

static void refreshWhileScrolling_ListWidget_(iAnyObject *any) {
    iListWidget *d = any;
    updateVisible_ListWidget(d);
    if (d->rangeSelect == mouse_ListRangeSelect) {
        selectRangeAt_ListWidget_(d, mouseCoord_Window(get_Window(), 0));
    }
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
    d->itemPadding = 0;
    d->wheelStep = 0;
    init_Array(&d->itemTops, sizeof(int));
    d->isLayoutValid = iFalse;
    d->isResizing = iFalse;
    d->isLayoutPartial = iFalse;
    d->scrollMode = normal_ScrollMode;
    d->noHoverWhileScrolling = iFalse;
    d->hideItemOnDrag = iFalse;
    d->isMultiSelectEnabled = iFalse;
    d->numMultiSelected = 0;
    d->rangeSelect = none_ListRangeSelect;
    d->rangeAnchor = iInvalidPos;
    d->isShiftClick = iFalse;
    d->isMultiDrag = iFalse;
    init_PtrArray(&d->items);
    d->cursorItem = iInvalidPos;
    d->hoverItem = iInvalidPos;
    d->dragItem = iInvalidPos;
    d->dragOrigin = zero_I2();
    d->dragHandleWidth = 0;
    d->minVisBufHeight = 0;
    d->lastWidth = -1;
    init_Click(&d->click, d, SDL_BUTTON_LEFT);
    init_Click(&d->midClick, d, SDL_BUTTON_MIDDLE);
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
    deinit_IntSet(&d->invalidItems);
    deinit_PtrArray(&d->items);
    deinit_Array(&d->itemTops);
    delete_VisBuf(d->visBuf);
}

void invalidate_ListWidget(iListWidget *d) {
    invalidate_VisBuf(d->visBuf);
    clear_IntSet(&d->invalidItems); /* all will be drawn */
    refresh_Widget(as_Widget(d));
}

void invalidateItem_ListWidget(iListWidget *d, size_t index) {
    insert_IntSet(&d->invalidItems, (int) index);
    refresh_Widget(d);
}

void invalidateLayout_ListWidget(iListWidget *d) {
    d->isLayoutValid   = iFalse;
    d->isLayoutPartial = iFalse;
}

void clear_ListWidget(iListWidget *d) {
    iForEach(PtrArray, i, &d->items) {
        deref_Object(i.ptr);
    }
    clear_PtrArray(&d->items);
    d->hoverItem = iInvalidPos;
    d->numMultiSelected = 0;
    d->rangeSelect = none_ListRangeSelect;
    invalidateLayout_ListWidget(d);
}

void addItem_ListWidget(iListWidget *d, iAnyObject *item) {
    pushBack_PtrArray(&d->items, ref_Object(item));
    invalidateLayout_ListWidget(d);
}

iScrollWidget *scroll_ListWidget(iListWidget *d) {
    return d->scroll;
}

size_t numItems_ListWidget(const iListWidget *d) {
    return size_PtrArray(&d->items);
}

static int listWidth_ListWidget_(const iListWidget *d) {
    return width_Rect(innerBounds_Widget(constAs_Widget(d)));
}

static int heightAt_ListWidget_(const iListWidget *d, size_t index) {
    const iListItem *item = constAt_PtrArray(&d->items, index);
    return item->height > 0 ? item->height : d->itemHeight;
}

static void measureItem_ListWidget_(iListWidget *d, size_t index, int width) {
    iListItem *item = at_PtrArray(&d->items, index);
    if (class_ListItem(item)->measureHeight) {
        const int height = class_ListItem(item)->measureHeight(item, width, d);
        item->height     = height > 0 ? height + d->itemPadding : 0;
    }
}

static iBool isCursorSkipped_ListWidget_(const iListWidget *d, size_t index) {
    const iListItem *item = constItem_ListWidget(d, index);
    return item->flags.isSeparator;
}

static size_t nearestCursor_ListWidget_(const iListWidget *d, size_t index, int dir) {
    const int count = (int) size_PtrArray(&d->items);
    if (count == 0) {
        return iInvalidPos;
    }
    index = iMin(index, (size_t) count - 1);
    for (int pass = 0; pass < 2; pass++, dir = -dir) {
        for (int i = (int) index; i >= 0 && i < count; i += dir) {
            if (!isCursorSkipped_ListWidget_(d, i)) {
                return i;
            }
        }
    }
    return iInvalidPos;
}

static void updateTops_ListWidget_(iListWidget *d) {
    const size_t count = size_PtrArray(&d->items);
    resize_Array(&d->itemTops, count + 1);
    int *tops = data_Array(&d->itemTops);
    tops[0] = 0;
    for (size_t i = 0; i < count; i++) {
        tops[i + 1] = tops[i] + heightAt_ListWidget_(d, i);
    }
    d->isLayoutValid = iTrue;
    /* Items may have changed under the cursor. */
    if (d->cursorItem != iInvalidPos) {
        d->cursorItem = nearestCursor_ListWidget_(d, d->cursorItem, 1);
    }
}

static void updateLayout_ListWidget_(iListWidget *d) {
    const int width = listWidth_ListWidget_(d);
    if (width > 0) {
        for (size_t i = 0; i < size_PtrArray(&d->items); i++) {
            measureItem_ListWidget_(d, i, width);
        }
        d->isLayoutPartial = iFalse;
    }
    updateTops_ListWidget_(d);
}

static const int *tops_ListWidget_(const iListWidget *d) {
    if (!d->isLayoutValid) {
        updateLayout_ListWidget_(iConstCast(iListWidget *, d));
    }
    return constData_Array(&d->itemTops);
}

static int contentHeight_ListWidget_(const iListWidget *d) {
    return tops_ListWidget_(d)[size_PtrArray(&d->items)];
}

static iRangei itemSpan_ListWidget_(const iListWidget *d, size_t index) {
    /* The position after the last item is valid, too. */
    const size_t count = size_PtrArray(&d->items);
    if (index > count) {
        return (iRangei){ 0, 0 };
    }
    const int *tops = tops_ListWidget_(d);
    return (iRangei){ tops[index], index < count ? tops[index + 1] : tops[index] + d->itemHeight };
}

static size_t indexAt_ListWidget_(const iListWidget *d, int y) {
    const int   *tops  = tops_ListWidget_(d);
    const size_t count = size_PtrArray(&d->items);
    if (y < 0 || y >= tops[count]) {
        return iInvalidPos;
    }
    size_t lo = 0, hi = count;
    while (hi - lo > 1) {
        const size_t mid = (lo + hi) / 2;
        if (tops[mid] <= y) {
            lo = mid;
        }
        else {
            hi = mid;
        }
    }
    return lo;
}

static int scrollMax_ListWidget_(const iListWidget *d) {
    return iMax(0,
                contentHeight_ListWidget_(d) -
                    height_Rect(innerBounds_Widget(constAs_Widget(d))));
}

static void reflow_ListWidget_(iListWidget *d, iBool visibleOnly) {
    /* The first visible item keeps its position. */
    const int    width   = listWidth_ListWidget_(d);
    const int    scrollY = scrollPos_ListWidget(d);
    const size_t anchor  = indexAt_ListWidget_(d, iMax(0, scrollY));
    if (anchor == iInvalidPos) {
        updateLayout_ListWidget_(d);
        return;
    }
    const int oldTop    = itemSpan_ListWidget_(d, anchor).start;
    const int oldHeight = heightAt_ListWidget_(d, anchor);
    if (visibleOnly) {
        const int visEnd = scrollY - oldTop + height_Rect(innerBounds_Widget(as_Widget(d)));
        int y = 0;
        for (size_t i = anchor; i < size_PtrArray(&d->items) && y < visEnd; i++) {
            measureItem_ListWidget_(d, i, width);
            y += heightAt_ListWidget_(d, i);
        }
        d->isLayoutPartial = iTrue;
        updateTops_ListWidget_(d);
    }
    else {
        updateLayout_ListWidget_(d);
    }
    const int offset = oldHeight ? (scrollY - oldTop) * heightAt_ListWidget_(d, anchor) / oldHeight : 0;
    updateVisible_ListWidget(d);
    setScrollPos_ListWidget(
        d, iClamp(itemSpan_ListWidget_(d, anchor).start + offset, 0, scrollMax_ListWidget_(d)));
}

void updateVisible_ListWidget(iListWidget *d) {
    const int   contentSize = contentHeight_ListWidget_(d);
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
        invalidateLayout_ListWidget(d);
        invalidate_ListWidget(d);
    }
}

void setItemPadding_ListWidget(iListWidget *d, int itemPadding) {
    if (d->itemPadding != itemPadding) {
        d->itemPadding = itemPadding;
        invalidateLayout_ListWidget(d);
        invalidate_ListWidget(d);
    }
}

void setWheelStep_ListWidget(iListWidget *d, int wheelStep) {
    d->wheelStep = wheelStep;
}

void setResizing_ListWidget(iListWidget *d, iBool isResizing) {
    d->isResizing = isResizing;
    if (!isResizing && d->isLayoutPartial && d->isLayoutValid) {
        reflow_ListWidget_(d, iFalse);
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
        setFlags_Widget(d, touchDrag_WidgetFlag, iFalse); /* mobile drag handles */
    }
}

void setMinVisBufHeight_ListWidget(iListWidget *d, int height) {
    d->minVisBufHeight = height;
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
    const iRect   rect     = innerBoundsWithoutVisualOffset_Widget(as_Widget(d));
    const iRangei itemSpan = itemSpan_ListWidget_(d, index);
    const int     yTop     = itemSpan.start - pos_SmoothScroll(&d->scrollY);
    const int     yBottom  = itemSpan.end - pos_SmoothScroll(&d->scrollY);
    if (height_Rect(rect) > size_Range(&itemSpan)) {
        if (yBottom > height_Rect(rect)) {
            scrollOffsetSpan_ListWidget(d, yBottom - height_Rect(rect), span);
        }
        else if (yTop < 0) {
            scrollOffsetSpan_ListWidget(d, yTop, span);
        }
    }
    else {
        scrollOffsetSpan_ListWidget(d, yTop, span);
    }
}

void scrollToItemTop_ListWidget(iListWidget *d, size_t index, int offset) {
    /* The item's top will be `offset` below the top of the list. */
    if (index < size_PtrArray(&d->items)) {
        setScrollPos_ListWidget(
            d, iClamp(itemSpan_ListWidget_(d, index).start - offset, 0, scrollMax_ListWidget_(d)));
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
    return indexAt_ListWidget_(d, pos.y - top_Rect(bounds) + pos_SmoothScroll(&d->scrollY));
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
        if (item->flags.isSeparator) {
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

static iBool moveCursor_ListWidget_(iListWidget *d, int dir, uint32_t animSpan) {
    const size_t oldCursor = d->cursorItem;
    if (isEmpty_ListWidget(d)) {
        d->cursorItem = iInvalidPos;
    }
    else {
        const int maxItem = numItems_ListWidget(d) - 1;
        if (d->cursorItem == iInvalidPos) {
            d->cursorItem = 0;
        }
        d->cursorItem = nearestCursor_ListWidget_(
            d, iClamp((int) d->cursorItem + dir, 0, maxItem), dir >= 0 ? 1 : -1);
    }
    if (oldCursor != d->cursorItem) {
        invalidateItem_ListWidget(d, oldCursor);
        invalidateItem_ListWidget(d, d->cursorItem);
    }
    if (d->cursorItem != iInvalidPos) {
        scrollToItem_ListWidget(d, d->cursorItem, prefs_App()->uiAnimations ? animSpan : 0);
        if (isConnected_Gamepad(gamepad_App())) {
            /* The gamepad pointer is moved to match the selected list item position. */
            movePointer_Gamepad(gamepad_App(), mid_Rect(itemRectWithoutVisualOffset_ListWidget(d,
                d->cursorItem)), 0);
        }
    }
    return d->cursorItem != oldCursor;
}

void setCursorItem_ListWidget(iListWidget *d, size_t index) {
    invalidateItem_ListWidget(d, d->cursorItem);
    d->cursorItem = index;
    moveCursor_ListWidget_(d, 0, 0);
}

static void setCursorWithoutScroll_ListWidget_(iListWidget *d, size_t index) {
    if (d->cursorItem != index) {
        invalidateItem_ListWidget(d, d->cursorItem);
        d->cursorItem = index;
        invalidateItem_ListWidget(d, d->cursorItem);
    }
}

void restoreCursorItem_ListWidget(iListWidget *d, size_t index) {
    /* Validated against the items on the next layout update. */
    d->cursorItem = index;
    invalidateLayout_ListWidget(d);
    invalidate_ListWidget(d);
}

size_t cursorItemIndex_ListWidget(const iListWidget *d) {
    return d->cursorItem;
}

void setMultiSelectEnabled_ListWidget(iListWidget *d, iBool enabled) {
    d->isMultiSelectEnabled = enabled;
    if (!enabled) {
        clearMultiSelection_ListWidget(d);
    }
}

void setMultiSelected_ListWidget(iListWidget *d, size_t index, iBool isSelected) {
    iListItem *item = item_ListWidget(d, index);
    if (item && item->flags.isMultiSelected != isSelected) {
        item->flags.isMultiSelected = isSelected;
        if (isSelected) {
            d->numMultiSelected++;
        }
        else {
            d->numMultiSelected--;
        }
        invalidateItem_ListWidget(d, index);
    }
}

void clearMultiSelection_ListWidget(iListWidget *d) {
    for (size_t i = 0; d->numMultiSelected && i < size_PtrArray(&d->items); i++) {
        setMultiSelected_ListWidget(d, i, iFalse);
    }
}

size_t numMultiSelected_ListWidget(const iListWidget *d) {
    return d->numMultiSelected;
}

static iBool isMultiSelected_ListWidget_(const iListWidget *d, size_t index) {
    const iListItem *item = constItem_ListWidget(d, index);
    return item && item->flags.isMultiSelected;
}

static void selectRange_ListWidget_(iListWidget *d, size_t end) {
    if (d->rangeAnchor == iInvalidPos || end == iInvalidPos) {
        return;
    }
    const size_t first = iMin(d->rangeAnchor, end);
    const size_t last  = iMax(d->rangeAnchor, end);
    for (size_t i = 0; i < size_PtrArray(&d->items); i++) {
        const iListItem *item = constAt_PtrArray(&d->items, i);
        setMultiSelected_ListWidget(d,
                                    i,
                                    i >= first && i <= last && !item->flags.isSeparator &&
                                        !item->flags.isHidden);
    }
}

static void selectRangeAt_ListWidget_(iListWidget *d, iInt2 pos) {
    /* Positions outside the list are clamped to the first or last item. */
    const int y = pos.y - top_Rect(innerBounds_Widget(as_Widget(d))) + pos_SmoothScroll(&d->scrollY);
    selectRange_ListWidget_(d, indexAt_ListWidget_(d, iClamp(y, 0, contentHeight_ListWidget_(d) - 1)));
}

static iBool isShiftMultiSelect_ListWidget_(const iListWidget *d, uint32_t device) {
    return d->isMultiSelectEnabled && device != SDL_TOUCH_MOUSEID &&
           keyMods_Sym(modState_Keys()) == SDL_KMOD_SHIFT;
}

void updateMouseHover_ListWidget(iListWidget *d) {
    const iInt2 mouse = mouseCoord_Window(
        get_Window(), deviceType_App() == desktop_AppDeviceType ? 0 : SDL_TOUCH_MOUSEID);
    setHoverItem_ListWidget(d, itemIndex_ListWidget(d, mouse));
}

void sort_ListWidget(iListWidget *d, int (*cmp)(const iListItem **item1, const iListItem **item2)) {
    sort_Array(&d->items, (iSortedArrayCompareElemFunc) cmp);
    invalidateLayout_ListWidget(d);
}

static void redrawHoverItem_ListWidget_(iListWidget *d) {
    insert_IntSet(&d->invalidItems, d->hoverItem);
    refresh_Widget(as_Widget(d));
}

static void sizeChanged_ListWidget_(iListWidget *d) {
    /* Item drawing only depends on the list's width, not its height, so a pure height
       change (e.g., while a sliding sheet is animating) doesn't need to invalidate
       already-rendered items; newly revealed rows are drawn incrementally as usual. */
    const int width = listWidth_ListWidget_(d);
    if (d->lastWidth != width) {
        d->lastWidth = width;
        if (d->isLayoutValid && width > 0) {
            reflow_ListWidget_(d, d->isResizing);
        }
        invalidate_ListWidget(d);
    }
    updateVisible_ListWidget(d);
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
        const iWidget *w            = constAs_Widget(d);
        const int      widgetHeight = height_Rect(bounds_Widget(w));
        const int      visHeight    = iMin(contentHeight_ListWidget_(d), widgetHeight);
        /* Clamp to first/last item. */
        index =
            (dstPos.y < top_Rect(bounds_Widget(w)) + visHeight / 2 ? 0
                                                                   : (numItems_ListWidget(d) - 1));
        item = constItem_ListWidget(d, index);
    }
    const iRect   rect = itemRect_ListWidget(d, index);
    const iRangei span = ySpan_Rect(rect);
    if (item->flags.isDropTarget) {
        const int pad = size_Range(&span) / 4;
        if (dstPos.y >= span.start + pad && dstPos.y < span.end - pad) {
            *dstKind = on_DragDestination;
            return index;
        }
    }
    int delta = dstPos.y - top_Rect(rect);
    if (dstPos.y - span.start > span.end - dstPos.y) {
        index++;
        delta -= size_Range(&span);
    }
    index = iMin(index, numItems_ListWidget(d));
    *dstKind = delta < 0 && index > 0 ? after_DragDestination : before_DragDestination;
    return index;
}

static iWidget *dropTargetAt_(iInt2 pos) {
    for (iWidget *w = hitChild_Window(get_Window(), pos); w; w = w->parent) {
        if (w->flags2 & dropTarget_WidgetFlag2) {
            return ~w->flags & disabled_WidgetFlag ? w : NULL;
        }
    }
    return NULL;
}

static void setDropTarget_ListWidget_(iListWidget *d, iWidget *dropTarget) {
    iWindow *win = get_Window();
    if (win->dropTarget != dropTarget) {
        win->dropTarget = dropTarget;
        refresh_Widget(d);
    }
}

static iBool endDrag_ListWidget_(iListWidget *d, iInt2 endPos) {
    if (d->dragItem == iInvalidPos) {
        return iFalse;
    }
    setFlags_Widget(d, touchDrag_WidgetFlag, iFalse); /* mobile drag handles */
    stop_Anim(&d->scrollY.pos);
    const iWidget *dropTarget = dropTargetAt_(endPos);
    setDropTarget_ListWidget_(d, NULL);
    enum iDragDestination dstKind;
    const size_t index = resolveDragDestination_ListWidget_(d, endPos, &dstKind);
    if (dropTarget) {
        postCommand_Widget(d, "list.dragged arg:%zu target:%s", d->dragItem,
                           cstr_String(id_Widget(dropTarget)));
    }
    else if (index != d->dragItem) {
        if (d->hideItemOnDrag) {
            iForEach(PtrArray, i, &d->items) {
                iListItem *item = i.ptr;
                if (index_PtrArrayIterator(&i) == d->dragItem ||
                    (d->isMultiDrag && item->flags.isMultiSelected)) {
                    item->flags.isHidden = iTrue;
                }
            }
        }
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
    if (d->isMultiDrag) {
        d->isMultiDrag = iFalse;
        invalidate_ListWidget(d);
    }
    return iTrue;
}

static void abortDrag_ListWidget_(iListWidget *d) {
    if (d->dragItem != iInvalidPos) {
        stop_Anim(&d->scrollY.pos);
        setDropTarget_ListWidget_(d, NULL);
        invalidateItem_ListWidget(d, d->dragItem);
        d->dragItem = iInvalidPos;
        if (d->isMultiDrag) {
            d->isMultiDrag = iFalse;
            invalidate_ListWidget(d);
        }
        redrawHoverItem_ListWidget_(d);
        setFlags_Widget(d, touchDrag_WidgetFlag, iFalse); /* mobile drag handles */
    }
}

static iBool endRangeSelect_ListWidget_(iListWidget *d) {
    if (d->rangeSelect == mouse_ListRangeSelect) {
        d->rangeSelect = none_ListRangeSelect;
        stop_Anim(&d->scrollY.pos);
        return iTrue;
    }
    return iFalse;
}

void cancelDrag_ListWidget(iListWidget *d) {
    cancel_Click(&d->click);
    abortDrag_ListWidget_(d);
    endRangeSelect_ListWidget_(d);
}

static iBool isScrollDisabled_ListWidget_(const iListWidget *d, const SDL_Event *ev) {
    int dir = 0;
    if (ev->type == SDL_EVENT_MOUSE_WHEEL) {
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
        case SDLK_PAGEDOWN: {
            if (isEmpty_ListWidget(d)) {
                return dir;
            }
            const size_t cursor = d->cursorItem != iInvalidPos ? d->cursorItem : 0;
            const int    y      = itemSpan_ListWidget_(d, cursor).start +
                           dir * iMax(1, height_Rect(innerBounds_Widget(w)) - d->itemHeight);
            const size_t dst =
                indexAt_ListWidget_(d, iClamp(y, 0, contentHeight_ListWidget_(d) - 1));
            const int step = (int) dst - (int) cursor;
            return step ? step : dir;
        }
        case SDLK_HOME:
        case SDLK_END:
            return dir * numItems_ListWidget(d);
    }
    return 0;
}

static iBool processEvent_ListWidget_(iListWidget *d, const SDL_Event *ev) {
    iWidget *w = as_Widget(d);
    if (isMetricsChange_UserEvent(ev)) {
        invalidateLayout_ListWidget(d);
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
            if (d->rangeSelect == keyboard_ListRangeSelect) {
                d->rangeSelect = none_ListRangeSelect;
            }
            invalidateItem_ListWidget(d, d->cursorItem);
            refresh_Widget(d);
            return iFalse;
        }
    }
    else if (ev->type == SDL_EVENT_USER && ev->user.code == widgetTapBegins_UserEventCode) {
        d->noHoverWhileScrolling = iFalse;
    }
    if (ev->type == SDL_EVENT_KEY_UP && d->rangeSelect == keyboard_ListRangeSelect &&
        (ev->key.key == SDLK_LSHIFT || ev->key.key == SDLK_RSHIFT)) {
        d->rangeSelect = none_ListRangeSelect;
    }
    if (ev->type == SDL_EVENT_KEY_DOWN && isFocused_Widget(w)) {
        if (d->isMultiSelectEnabled && d->scrollMode == normal_ScrollMode &&
            keyMods_Sym(ev->key.mod) == SDL_KMOD_SHIFT) {
            const int step = cursorKeyStep_ListWidget_(d, ev->key.key);
            if (step) {
                if (d->rangeSelect != keyboard_ListRangeSelect) {
                    clearMultiSelection_ListWidget(d);
                    moveCursor_ListWidget_(d, 0, 0);
                    d->rangeAnchor = d->cursorItem;
                    d->rangeSelect = keyboard_ListRangeSelect;
                }
                moveCursor_ListWidget_(d, step, iAbs(step) == 1 ? 0 : 150);
                selectRange_ListWidget_(d, d->cursorItem);
                return iTrue;
            }
        }
        if (d->rangeSelect == keyboard_ListRangeSelect && !isMod_Sym(ev->key.key)) {
            d->rangeSelect = none_ListRangeSelect; /* Shift release may go unnoticed */
        }
        if (ev->key.mod == 0) {
            const int key = ev->key.key;
            switch (key) {
                case SDLK_UP:
                case SDLK_DOWN:
                case SDLK_PAGEUP:
                case SDLK_PAGEDOWN:
                case SDLK_HOME:
                case SDLK_END: {
                    if (d->scrollMode == normal_ScrollMode) {
                        const int step = cursorKeyStep_ListWidget_(d, key);
                        const iBool wasChanged = moveCursor_ListWidget_(d, step, iAbs(step) == 1 ? 0 : 150);
                        if (!wasChanged && (key == SDLK_UP || key == SDLK_DOWN)) {
                            moveFocusWithArrows_App(ev);
                        }
                        return iTrue;
                    }
                    return iFalse;
                }
                case SDLK_LEFT:
                case SDLK_RIGHT:
                    return moveFocusWithArrows_App(ev);
                case SDLK_RETURN:
                case SDLK_KP_ENTER:
                case SDLK_SPACE:
                    if (d->cursorItem != iInvalidPos) {
                        postCommand_Widget(w,
                                           "list.clicked arg:%zu item:%p button:%d keyboard:1",
                                           d->cursorItem,
                                           constCursorItem_ListWidget(d),
                                           SDL_BUTTON_LEFT);
                    }
                    return iTrue;
                case SDLK_BACKSPACE:
                    if (d->cursorItem != iInvalidPos) {
                        postCommand_Widget(w, "list.delete arg:%zu item:%p",
                                           d->cursorItem, constCursorItem_ListWidget(d));
                    }
                    return iTrue;
                case SDLK_ESCAPE:
                    if (d->numMultiSelected) {
                        clearMultiSelection_ListWidget(d);
                        return iTrue;
                    }
                    break;
            }
        }
    }
    if (ev->type == SDL_EVENT_MOUSE_MOTION) {
        const iInt2 mousePos = init_I2(ev->motion.x, ev->motion.y);
        if (ev->motion.state == 0 /* not dragging */) {
            if (ev->motion.which != SDL_TOUCH_MOUSEID) {
                d->noHoverWhileScrolling = iFalse;
            }
            updateHover_ListWidget_(d, mousePos);
        }
        else if (d->dragItem != iInvalidPos || d->rangeSelect == mouse_ListRangeSelect) {
            if (d->rangeSelect == mouse_ListRangeSelect) {
                selectRangeAt_ListWidget_(d, mousePos);
            }
            else {
                setDropTarget_ListWidget_(d, dropTargetAt_(mousePos));
            }
            /* Start scrolling if near the ends. */
            const int zone = 2 * d->itemHeight;
            const iRect bounds = bounds_Widget(w);
            float scrollSpeed = 0.0f;
            if (get_Window()->dropTarget) {
                /* Don't scroll while over a drop target. */
            }
            else if (mousePos.y > bottom_Rect(bounds) - zone) {
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
    if (ev->type == SDL_EVENT_MOUSE_WHEEL && isHover_Widget(w) && ev->wheel.x == 0) {
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
        extern iWidgetClass Class_SidebarWidget;
        if (isScrollDisabled_ListWidget_(d, ev)) {
            if (ev->wheel.which == SDL_TOUCH_MOUSEID) {
                /* TODO: Could generalize this selection of the scrollable parent. */
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
            if (isMobile_Platform() && !hasAffinity_Touch(w) &&
                ev->wheel.which == SDL_TOUCH_MOUSEID && !isScrollDisabled_ListWidget_(d, ev)) {
                transferAffinity_Touch(NULL, w);
            }
        }
        else {
            /* Traditional mouse wheel. */
            amount *= d->wheelStep ? d->wheelStep : 3 * d->itemHeight;
            moveSpan_SmoothScroll(
                &d->scrollY, amount, 600 * scrollSpeedFactor_Prefs(prefs_App(), mouse_ScrollType));
        }
        return iTrue;
    }
    if (ev->type == SDL_EVENT_MOUSE_WHEEL && isHover_Widget(w) && ev->wheel.y == 0 &&
        isPerPixel_MouseWheelEvent(&ev->wheel) && !isInertia_MouseWheelEvent(&ev->wheel)) {
        iInt2 coord = mouseCoord_SDLEvent(ev);
        postCommand_Widget(w, "listswipe.moved arg:%d coord:%d %d", (int) ev->wheel.x, coord.x, coord.y);
        return iTrue;
    }
    /* Left button: item selection, dragging. */
    switch (processEvent_Click(&d->click, ev)) {
        case started_ClickResult:
            d->noHoverWhileScrolling = iFalse;
            d->isShiftClick = isShiftMultiSelect_ListWidget_(d, ev->button.which);
            updateHover_ListWidget_(d, mouseCoord_Window(get_Window(), ev->button.which));
            redrawHoverItem_ListWidget_(d);
            return iTrue;
        case aborted_ClickResult:
            /* Released outside the list. */
            if (endRangeSelect_ListWidget_(d)) {
                return iTrue;
            }
            if (dropTargetAt_(pos_Click(&d->click)) && endDrag_ListWidget_(d, pos_Click(&d->click))) {
                return iTrue;
            }
            abortDrag_ListWidget_(d);
            break;
        case drag_ClickResult:
            if (d->dragItem == iInvalidPos && d->rangeSelect != mouse_ListRangeSelect &&
                length_I2(delta_Click(&d->click)) > gap_UI) {
                const size_t over = itemIndex_ListWidget(d, d->click.startPos);
                if (d->isShiftClick) {
                    if (over != iInvalidPos) {
                        clearMultiSelection_ListWidget(d);
                        d->rangeAnchor = over;
                        d->rangeSelect = mouse_ListRangeSelect;
                        selectRangeAt_ListWidget_(d, pos_Click(&d->click));
                    }
                }
                else if (over != iInvalidPos &&
                    ((const iListItem *) item_ListWidget(d, over))->flags.isDraggable) {
                    d->isMultiDrag = isMultiSelected_ListWidget_(d, over) && d->numMultiSelected > 1;
                    if (d->isMultiDrag) {
                        invalidate_ListWidget(d); /* all selected items are lifted */
                    }
                    else {
                        clearMultiSelection_ListWidget(d);
                    }
                    d->dragItem = over;
                    d->dragOrigin = sub_I2(topLeft_Rect(itemRect_ListWidget(d, over)),
                                           d->click.startPos);
                    invalidateItem_ListWidget(d, d->dragItem);
                }
            }
            return d->dragItem != iInvalidPos || d->rangeSelect == mouse_ListRangeSelect;
        case finished_ClickResult:
            if (endDrag_ListWidget_(d, pos_Click(&d->click)) || endRangeSelect_ListWidget_(d)) {
                return iTrue;
            }
            redrawHoverItem_ListWidget_(d);
            if (!d->isShiftClick) {
                clearMultiSelection_ListWidget(d);
            }
            if (contains_Rect(adjusted_Rect(itemRect_ListWidget(d, d->hoverItem),
                                            zero_I2(), init_I2(-d->dragHandleWidth, 0)),
                              pos_Click(&d->click)) &&
                d->hoverItem != iInvalidPos) {
                setCursorWithoutScroll_ListWidget_(d, d->hoverItem);
                if (d->isShiftClick) {
                    setMultiSelected_ListWidget(
                        d, d->hoverItem, !isMultiSelected_ListWidget_(d, d->hoverItem));
                }
                else {
                    postCommand_Widget(w, "list.clicked arg:%zu button:%d item:%p device:%u",
                                       d->hoverItem,
                                       d->click.clickButton,
                                       constHoverItem_ListWidget(d),
                                       ev->button.which);
                }
            }
            return iTrue;
        default:
            break;
    }
    /* Middle button: secondary item action. */
    switch (processEvent_Click(&d->midClick, ev)) {
        case started_ClickResult:
            updateHover_ListWidget_(d, mouseCoord_Window(get_Window(), ev->button.which));
            return iTrue;
        case finished_ClickResult:
            if (!d->midClick.isDragging && d->hoverItem != iInvalidPos) {
                setCursorWithoutScroll_ListWidget_(d, d->hoverItem);
                postCommand_Widget(w, "list.clicked arg:%zu button:%d item:%p device:%u",
                                   d->hoverItem,
                                   SDL_BUTTON_MIDDLE,
                                   constHoverItem_ListWidget(d),
                                   ev->button.which);
            }
            return iTrue;
        case aborted_ClickResult:
            return iTrue;
        default:
            break;
    }
    return processEvent_Widget(w, ev);
}

static iRect itemRectAt_ListWidget_(const iListWidget *d, size_t index, iRect bounds,
                                    int scrollY) {
    const iRangei span = itemSpan_ListWidget_(d, index);
    return (iRect){ addY_I2(topLeft_Rect(bounds), span.start - scrollY),
                    init_I2(width_Rect(bounds), size_Range(&span)) };
}

iRect itemRect_ListWidget(const iListWidget *d, size_t index) {
    return itemRectAt_ListWidget_(
        d, index, innerBounds_Widget(constAs_Widget(d)), pos_SmoothScroll(&d->scrollY));
}

iRect itemRectWithoutVisualOffset_ListWidget(const iListWidget *d, size_t index) {
    return itemRectAt_ListWidget_(d,
                                  index,
                                  innerBoundsWithoutVisualOffset_Widget(constAs_Widget(d)),
                                  targetValue_Anim(&d->scrollY.pos));
}

static void drawItem_ListWidget_(const iListWidget *d, iPaint *p, size_t index,
                                 const iVisBufTexture *buf, int bg, int blankWidth) {
    /* The entire item is drawn even if it extends past the buffer; the texture clips it. */
    const iListItem *item = constAt_PtrArray(&d->items, index);
    if (!item->flags.isHidden) {
        const iRangei span     = itemSpan_ListWidget_(d, index);
        const iRect   itemRect = { init_I2(0, span.start - buf->origin),
                                   init_I2(d->visBuf->texSize.x, size_Range(&span)) };
        fillRect_Paint(p, itemRect, bg);
        if (index != d->dragItem && !(d->isMultiDrag && item->flags.isMultiSelected)) {
            class_ListItem(item)->draw(item, p, itemRect, d);
        }
        fillRect_Paint(p,
                       (iRect){ init_I2(d->visBuf->texSize.x - blankWidth, top_Rect(itemRect)),
                                init_I2(blankWidth, height_Rect(itemRect)) },
                       bg);
    }
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
    iInt2 visBufSize = bounds.size;
    if (d->minVisBufHeight > visBufSize.y) {
        visBufSize.y = d->minVisBufHeight;
    }
    alloc_VisBuf(d->visBuf, visBufSize, d->itemHeight);
    /* Update invalid regions/items. */ {
        /* TODO: This seems to draw two items per each shift of the visible region, even though
           one should be enough. Probably an off-by-one error in the calculation of the
           invalid range. */
        for (size_t i = 0; i < d->visBuf->numBuffers; i++) {
            iAssert(d->visBuf->buffers[i].texture);
        }
        const int bg[maxBuffers_VisBuf] = {
            w->bgColor, w->bgColor, w->bgColor, w->bgColor /* Debug: Separate BG color for buffers. */
        };
        const size_t count  = size_PtrArray(&d->items);
        const int    bottom = contentHeight_ListWidget_(d);
        iRangei      vis    = { scrollY, scrollY + bounds.size.y };
        /* Snapping to item boundaries avoids redrawing partially valid items. */ {
            const size_t first = indexAt_ListWidget_(d, vis.start);
            const size_t last  = indexAt_ListWidget_(d, vis.end - 1);
            const iRangei snapped = { first != iInvalidPos ? itemSpan_ListWidget_(d, first).start
                                                           : vis.start,
                                      last != iInvalidPos ? itemSpan_ListWidget_(d, last).end
                                                          : vis.end };
            if (size_Range(&snapped) <=
                (int) (d->visBuf->numBuffers - 1) * d->visBuf->texSize.y) {
                vis = snapped;
            }
        }
        reposition_VisBuf(d->visBuf, vis);
        /* Check which parts are invalid. */
        iRangei invalidRange[maxBuffers_VisBuf];
        invalidRanges_VisBuf(d->visBuf, (iRangei){ 0, bottom }, invalidRange);
        for (size_t i = 0; i < d->visBuf->numBuffers; i++) {
            iVisBufTexture *buf      = &d->visBuf->buffers[i];
            const iRangei   bufRange = bufferRange_VisBuf(d->visBuf, i);
#if defined (iPlatformApple) || defined (iPlatformAndroid)
            const int blankWidth = 0; /* scrollbars fade away */
#else
            const int blankWidth = scrollBarWidth_ListWidget(d);
#endif
            iConstForEach(IntSet, v, &d->invalidItems) {
                const size_t index = *v.value;
                if (index < count &&
                    !isEmpty_Rangei(intersect_Rangei(itemSpan_ListWidget_(d, index), bufRange))) {
                    beginTarget_Paint(&p, buf->texture);
                    drawItem_ListWidget_(d, &p, index, buf, bg[i], blankWidth);
                }
            }
            /* Visible range is not fully covered. Fill in the new items. */
            if (!isEmpty_Rangei(invalidRange[i])) {
                beginTarget_Paint(&p, buf->texture);
                size_t j = indexAt_ListWidget_(d, iMax(0, invalidRange[i].start));
                for (; j < count && itemSpan_ListWidget_(d, j).start < invalidRange[i].end; j++) {
                    drawItem_ListWidget_(d, &p, j, buf, bg[i], blankWidth);
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
    /* The dragged item is drawn independently of the rest. */
    if (d->dragItem != iInvalidPos && (isMobile || contains_Rect(bounds, mousePos))) {
        iInt2 pos = add_I2(mousePos, d->dragOrigin);
        const iListItem *item = constAt_PtrArray(&d->items, d->dragItem);
        const iRect itemRect = { init_I2(left_Rect(bounds), pos.y),
                                 init_I2(d->visBuf->texSize.x, heightAt_ListWidget_(d, d->dragItem)) };
        SDL_SetRenderDrawBlendMode(renderer_Window(get_Window()), SDL_BLENDMODE_BLEND);
        enum iDragDestination dstKind;
        const size_t dstIndex = resolveDragDestination_ListWidget_(d, mousePos, &dstKind);
        if (dstIndex != d->dragItem && !get_Window()->dropTarget) {
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
        if (d->isMultiDrag) {
            /* Number of dragged items. */
            const int    font  = uiLabelBold_FontId;
            const char  *count = format_CStr("%zu", d->numMultiSelected);
            const iInt2  size  = init_I2(measure_Text(font, count).advance.x + 3 * gap_UI,
                                         lineHeight_Text(font) + gap_UI);
            const iRect  badge = { init_I2(right_Rect(itemRect) - size.x - 3 * gap_UI -
                                               scrollBarWidth_ListWidget(d),
                                           mid_Rect(itemRect).y - size.y / 2),
                                   size };
            p.alpha = 0xff;
            fillRect_Paint(&p, badge, uiBackgroundSelected_ColorId);
            drawCentered_Text(font, badge, iTrue, uiTextSelected_ColorId, "%s", count);
        }
        SDL_SetRenderDrawBlendMode(renderer_Window(get_Window()), SDL_BLENDMODE_NONE);
    }
    unsetClip_Paint(&p);
    drawBorders_Widget(w); /* background overdraws the normal borders */
    if (isEmpty_ListWidget(d) && isFocused_Widget(d)) {
        drawRectThickness_Paint(
            &p,
            adjusted_Rect(bounds, zero_I2(), init_I2(isTerminal_Platform() ? -2 : -gap_UI, 0)),
            gap_UI / 4,
            uiTextAction_ColorId);
    }
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
