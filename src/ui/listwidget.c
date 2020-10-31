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
#include "visbuf.h"

#include <the_Foundation/intset.h>

void init_ListItem(iListItem *d) {
    d->isSeparator = iFalse;
    d->isSelected  = iFalse;
}

void deinit_ListItem(iListItem *d) {
    iUnused(d);
}

iDefineObjectConstruction(ListItem)
iDefineClass(ListItem)

/*----------------------------------------------------------------------------------------------*/

iDefineObjectConstruction(ListWidget)

struct Impl_ListWidget {
    iWidget widget;
    iScrollWidget *scroll;
    int scrollY;
    int itemHeight;
    iPtrArray items;
    size_t hoverItem;
    iClick click;
    iIntSet invalidItems;
    iVisBuf *visBuf;
};

void init_ListWidget(iListWidget *d) {
    iWidget *w = as_Widget(d);
    init_Widget(w);
    setId_Widget(w, "list");
    setBackgroundColor_Widget(w, uiBackground_ColorId); /* needed for filling visbuffer */
    setFlags_Widget(w, hover_WidgetFlag, iTrue);
    addChild_Widget(w, iClob(d->scroll = new_ScrollWidget()));
    setThumb_ScrollWidget(d->scroll, 0, 0);
    d->scrollY = 0;
    init_PtrArray(&d->items);
    d->hoverItem = iInvalidPos;
    init_Click(&d->click, d, SDL_BUTTON_LEFT);
    init_IntSet(&d->invalidItems);
    d->visBuf = new_VisBuf();
}

void deinit_ListWidget(iListWidget *d) {
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
    if (area_Rect(bounds) == 0) {
        return;
    }
    setRange_ScrollWidget(d->scroll, (iRangei){ 0, scrollMax_ListWidget_(d) });
    setThumb_ScrollWidget(d->scroll,
                          d->scrollY,
                          contentSize > 0 ? height_Rect(bounds_Widget(as_Widget(d->scroll))) *
                                                height_Rect(bounds) / contentSize
                                          : 0);
    if (wasVisible != isVisible_Widget(d->scroll)) {
        invalidate_ListWidget(d); /* clip margins changed */
    }
}

void setItemHeight_ListWidget(iListWidget *d, int itemHeight) {
    d->itemHeight = itemHeight;
    invalidate_ListWidget(d);
}

int itemHeight_ListWidget(const iListWidget *d) {
    return d->itemHeight;
}

int scrollPos_ListWidget(const iListWidget *d) {
    return d->scrollY;
}

void setScrollPos_ListWidget(iListWidget *d, int pos) {
    d->scrollY = pos;
    d->hoverItem = iInvalidPos;
    refresh_Widget(as_Widget(d));
}

void scrollOffset_ListWidget(iListWidget *d, int offset) {
    const int oldScroll = d->scrollY;
    d->scrollY += offset;
    if (d->scrollY < 0) {
        d->scrollY = 0;
    }
    const int scrollMax = scrollMax_ListWidget_(d);
    d->scrollY = iMin(d->scrollY, scrollMax);
    if (oldScroll != d->scrollY) {
        if (d->hoverItem != iInvalidPos) {
            invalidateItem_ListWidget(d, d->hoverItem);
            d->hoverItem = iInvalidPos;
        }
        updateVisible_ListWidget(d);
        refresh_Widget(as_Widget(d));
    }
}

void scrollToItem_ListWidget(iListWidget *d, size_t index) {
    const iRect rect    = innerBounds_Widget(as_Widget(d));
    int         yTop    = d->itemHeight * index - d->scrollY;
    int         yBottom = yTop + d->itemHeight;
    if (yBottom > height_Rect(rect)) {
        scrollOffset_ListWidget(d, yBottom - height_Rect(rect));
    }
    else if (yTop < 0) {
        scrollOffset_ListWidget(d, yTop);
    }
}

int visCount_ListWidget(const iListWidget *d) {
    return iMin(height_Rect(innerBounds_Widget(constAs_Widget(d))) / d->itemHeight,
                (int) size_PtrArray(&d->items));
}

static iRanges visRange_ListWidget_(const iListWidget *d) {
    if (d->itemHeight == 0) {
        return (iRanges){ 0, 0 };
    }
    iRanges vis = { d->scrollY / d->itemHeight, 0 };
    vis.end = iMin(size_PtrArray(&d->items), vis.start + visCount_ListWidget(d) + 1);
    return vis;
}

size_t itemIndex_ListWidget(const iListWidget *d, iInt2 pos) {
    const iRect bounds = innerBounds_Widget(constAs_Widget(d));
    pos.y -= top_Rect(bounds) - d->scrollY;
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

const iAnyObject *constHoverItem_ListWidget(const iListWidget *d) {
    return constItem_ListWidget(d, d->hoverItem);
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

static void setHoverItem_ListWidget_(iListWidget *d, size_t index) {
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

void updateMouseHover_ListWidget(iListWidget *d) {
    const iInt2 mouse = mouseCoord_Window(get_Window());
    setHoverItem_ListWidget_(d, itemIndex_ListWidget(d, mouse));
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

static iBool processEvent_ListWidget_(iListWidget *d, const SDL_Event *ev) {
    iWidget *w = as_Widget(d);
    if (isCommand_SDLEvent(ev)) {
        const char *cmd = command_UserEvent(ev);
        if (equal_Command(cmd, "theme.changed")) {
            invalidate_ListWidget(d);
        }
        else if (isCommand_Widget(w, ev, "scroll.moved")) {
            setScrollPos_ListWidget(d, arg_Command(cmd));
            return iTrue;
        }
    }
    if (ev->type == SDL_MOUSEMOTION) {
        const iInt2 mouse = init_I2(ev->motion.x, ev->motion.y);
        size_t hover = iInvalidPos;
        if (!contains_Widget(constAs_Widget(d->scroll), mouse) &&
            contains_Widget(w, mouse)) {
            hover = itemIndex_ListWidget(d, mouse);
        }
        setHoverItem_ListWidget_(d, hover);
    }
    if (ev->type == SDL_MOUSEWHEEL && isHover_Widget(w)) {
#if defined (iPlatformApple)
        /* Momentum scrolling. */
        scrollOffset_ListWidget(d, -ev->wheel.y * get_Window()->pixelRatio);
#else
        scrollOffset_ListWidget(d, -ev->wheel.y * 3 * d->itemHeight);
#endif
        return iTrue;
    }
    switch (processEvent_Click(&d->click, ev)) {
        case started_ClickResult:
            redrawHoverItem_ListWidget_(d);
            return iTrue;
        case aborted_ClickResult:
            redrawHoverItem_ListWidget_(d);
            break;
        case finished_ClickResult:
        case double_ClickResult:
            redrawHoverItem_ListWidget_(d);
            if (contains_Rect(innerBounds_Widget(w), pos_Click(&d->click)) &&
                d->hoverItem != iInvalidSize) {
                postCommand_Widget(w, "list.clicked arg:%zu item:%p",
                                   d->hoverItem, constHoverItem_ListWidget(d));
            }
            return iTrue;
        default:
            break;
    }
    return processEvent_Widget(w, ev);
}

static void drawItem_ListWidget_(const iListWidget *d, iPaint *p, size_t index, iInt2 pos) {
    const iWidget *  w         = constAs_Widget(d);
    const iRect      bounds    = innerBounds_Widget(w);
    const iListItem *item      = constAt_PtrArray(&d->items, index);
    const iRect      itemRect  = { pos, init_I2(width_Rect(bounds), d->itemHeight) };
    class_ListItem(item)->draw(item, p, itemRect, d);
}

static const iListItem *item_ListWidget_(const iListWidget *d, size_t pos) {
    return constAt_PtrArray(&d->items, pos);
}

static void draw_ListWidget_(const iListWidget *d) {
    const iWidget *w      = constAs_Widget(d);
    const iRect    bounds = innerBounds_Widget(w);
    if (!bounds.size.y || !bounds.size.x || !d->itemHeight) {
        return;
    }
    iPaint p;
    init_Paint(&p);
    drawBackground_Widget(w);
    alloc_VisBuf(d->visBuf, bounds.size, d->itemHeight);
    /* Update invalid regions/items. */ {
        /* TODO: This seems to draw two items per each shift of the visible region, even though
           one should be enough. Probably an off-by-one error in the calculation of the
           invalid range. */
        iAssert(d->visBuf->buffers[0].texture);
        iAssert(d->visBuf->buffers[1].texture);
        iAssert(d->visBuf->buffers[2].texture);
        const int bg[3] = { w->bgColor, w->bgColor, w->bgColor };
//        const int bg[3] = { red_ColorId, magenta_ColorId, blue_ColorId };
        const int bottom = numItems_ListWidget(d) * d->itemHeight;
        const iRangei vis = { d->scrollY / d->itemHeight * d->itemHeight,
                             ((d->scrollY + bounds.size.y) / d->itemHeight + 1) * d->itemHeight };
        reposition_VisBuf(d->visBuf, vis);
        /* Check which parts are invalid. */
        iRangei invalidRange[3];
        invalidRanges_VisBuf(d->visBuf, (iRangei){ 0, bottom }, invalidRange);
        iForIndices(i, d->visBuf->buffers) {
            iVisBufTexture *buf = &d->visBuf->buffers[i];
            iRanges drawItems = { iMax(0, buf->origin) / d->itemHeight,
                                  iMax(0, buf->origin + d->visBuf->texSize.y) / d->itemHeight };
            if (isEmpty_Rangei(buf->validRange)) {
                beginTarget_Paint(&p, buf->texture);
                fillRect_Paint(&p, (iRect){ zero_I2(), d->visBuf->texSize }, bg[i]);
            }
            iConstForEach(IntSet, v, &d->invalidItems) {
                const size_t index = *v.value;
                if (contains_Range(&drawItems, index)) {
                    const iListItem *item = constAt_PtrArray(&d->items, index);
                    const iRect      itemRect = { init_I2(0, index * d->itemHeight - buf->origin),
                                                  init_I2(d->visBuf->texSize.x, d->itemHeight) };
                    beginTarget_Paint(&p, buf->texture);
                    fillRect_Paint(&p, itemRect, bg[i]);
                    class_ListItem(item)->draw(item, &p, itemRect, d);
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
                    class_ListItem(item)->draw(item, &p, itemRect, d);
                }
            }
            endTarget_Paint(&p);
        }
        validate_VisBuf(d->visBuf);
        clear_IntSet(&iConstCast(iListWidget *, d)->invalidItems);
    }
    setClip_Paint(&p, bounds_Widget(w));
    draw_VisBuf(d->visBuf, addY_I2(topLeft_Rect(bounds), -d->scrollY));
    unsetClip_Paint(&p);
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
