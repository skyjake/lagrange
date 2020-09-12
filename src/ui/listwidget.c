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

enum iBufferValidity {
    none_BufferValidity,
    partial_BufferValidity,
    full_BufferValidity,
};

#define numVisBuffers_ListWidget_   3

iDeclareType(ListVisBuffer)

struct Impl_ListVisBuffer {
    SDL_Texture *texture;
    int origin;
    iRangei validRange;
};

struct Impl_ListWidget {
    iWidget widget;
    iScrollWidget *scroll;
    int scrollY;
    int itemHeight;
    iPtrArray items;
    size_t hoverItem;
    iClick click;
    iIntSet invalidItems;
    iInt2 visBufSize;
    iListVisBuffer visBuffers[numVisBuffers_ListWidget_];
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
    d->visBufSize = zero_I2();
    iZap(d->visBuffers);
}

void deinit_ListWidget(iListWidget *d) {
    clear_ListWidget(d);
    deinit_PtrArray(&d->items);
    iForIndices(i, d->visBuffers) {
        SDL_DestroyTexture(d->visBuffers[i].texture);
    }
}

void invalidate_ListWidget(iListWidget *d) {
    iForIndices(i, d->visBuffers) {
        iZap(d->visBuffers[i].validRange);
    }
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

static void redrawHoverItem_ListWidget_(iListWidget *d) {
    insert_IntSet(&d->invalidItems, d->hoverItem);
    refresh_Widget(as_Widget(d));
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

static void allocVisBuffer_ListWidget_(iListWidget *d) {
    /* Make sure two buffers cover the entire visible area. */
    const iRect inner = innerBounds_Widget(as_Widget(d));
    const iInt2 size = init_I2(inner.size.x, (inner.size.y / 2 / d->itemHeight + 1) * d->itemHeight);
    if (!d->visBuffers[0].texture || !isEqual_I2(size, d->visBufSize)) {
        d->visBufSize = size;
        iForIndices(i, d->visBuffers) {
            if (d->visBuffers[i].texture) {
                SDL_DestroyTexture(d->visBuffers[i].texture);
            }
            d->visBuffers[i].texture =
                SDL_CreateTexture(renderer_Window(get_Window()),
                                  SDL_PIXELFORMAT_RGBA8888,
                                  SDL_TEXTUREACCESS_STATIC | SDL_TEXTUREACCESS_TARGET,
                                  size.x,
                                  size.y);
            SDL_SetTextureBlendMode(d->visBuffers[i].texture, SDL_BLENDMODE_NONE);
            d->visBuffers[i].origin = i * size.y;
            iZap(d->visBuffers[i].validRange);
        }
    }
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
    const iWidget *w         = constAs_Widget(d);
    const iRect    bounds    = innerBounds_Widget(w);
    if (!bounds.size.y || !bounds.size.x) return;
    iPaint p;
    init_Paint(&p);
    SDL_Renderer *render = renderer_Window(get_Window());
    drawBackground_Widget(w);
    iListWidget *m = iConstCast(iListWidget *, d);
    allocVisBuffer_ListWidget_(m);
    /* Update invalid regions/items. */
    /* TODO: This seems to draw two items per each shift of the visible region, even though
       one should be enough. Probably an off-by-one error in the calculation of the
       invalid range. */
    if (d->visBufSize.y > 0) {
        iAssert(d->visBuffers[0].texture);
        iAssert(d->visBuffers[1].texture);
        iAssert(d->visBuffers[2].texture);
        const int bg[3] = { w->bgColor, w->bgColor, w->bgColor };
//        const int bg[3] = { red_ColorId, magenta_ColorId, blue_ColorId };
        const int bottom = numItems_ListWidget(d) * d->itemHeight;
        const iRangei vis = { d->scrollY / d->itemHeight * d->itemHeight,
                             ((d->scrollY + bounds.size.y) / d->itemHeight + 1) * d->itemHeight };
        iRangei good = { 0, 0 };
//        printf("visBufSize.y = %d\n", d->visBufSize.y);
        size_t avail[3], numAvail = 0;
        /* Check which buffers are available for reuse. */ {
            iForIndices(i, d->visBuffers) {
                iListVisBuffer *buf = m->visBuffers + i;
                const iRangei region = { buf->origin, buf->origin + d->visBufSize.y };
                if (region.start >= vis.end || region.end <= vis.start) {
                    avail[numAvail++] = i;
                    iZap(buf->validRange);
                }
                else {
                    good = union_Rangei(good, region);
                }
            }
        }
        if (numAvail == numVisBuffers_ListWidget_) {
            /* All buffers are outside the visible range, do a reset. */
            m->visBuffers[0].origin = vis.start;
            m->visBuffers[1].origin = vis.start + d->visBufSize.y;
        }
        else {
            /* Extend to cover the visible range. */
            while (vis.start < good.start) {
                iAssert(numAvail > 0);
                m->visBuffers[avail[--numAvail]].origin = good.start - d->visBufSize.y;
                good.start -= d->visBufSize.y;
            }
            while (vis.end > good.end) {
                iAssert(numAvail > 0);
                m->visBuffers[avail[--numAvail]].origin = good.end;
                good.end += d->visBufSize.y;
            }
        }
        /* Check which parts are invalid. */
        iRangei invalidRange[3];
        iForIndices(i, d->visBuffers) {
            const iListVisBuffer *buf = d->visBuffers + i;
            const iRangei region = intersect_Rangei(vis, (iRangei){ buf->origin, buf->origin + d->visBufSize.y });
            const iRangei before = { 0, buf->validRange.start };
            const iRangei after  = { buf->validRange.end, bottom };
            invalidRange[i] = intersect_Rangei(before, region);
            if (isEmpty_Rangei(invalidRange[i])) {
                invalidRange[i] = intersect_Rangei(after, region);
            }
        }
        iForIndices(i, d->visBuffers) {
            iListVisBuffer *buf = m->visBuffers + i;
//            printf("%zu: orig %d, invalid %d ... %d\n", i, buf->origin, invalidRange[i].start, invalidRange[i].end);
            iRanges drawItems = { iMax(0, buf->origin) / d->itemHeight,
                                  iMax(0, buf->origin + d->visBufSize.y) / d->itemHeight + 1 };
            iBool isTargetSet = iFalse;
            if (isEmpty_Rangei(buf->validRange)) {
                isTargetSet = iTrue;
                beginTarget_Paint(&p, buf->texture);
                fillRect_Paint(&p, (iRect){ zero_I2(), d->visBufSize }, bg[i]);
            }
            iConstForEach(IntSet, v, &d->invalidItems) {
                const size_t index = *v.value;
                if (contains_Range(&drawItems, index)) {
                    const iListItem *item = constAt_PtrArray(&d->items, index);
                    const iRect      itemRect = { init_I2(0, index * d->itemHeight - buf->origin),
                                                  init_I2(d->visBufSize.x, d->itemHeight) };
                    if (!isTargetSet) {
                        beginTarget_Paint(&p, buf->texture);
                        isTargetSet = iTrue;
                    }
                    fillRect_Paint(&p, itemRect, bg[i]);
                    class_ListItem(item)->draw(item, &p, itemRect, d);
//                    printf("- drawing invalid item %zu\n", index);
                }
            }
            /* Visible range is not fully covered. Fill in the new items. */
            if (!isEmpty_Rangei(invalidRange[i])) {
                if (!isTargetSet) {
                    beginTarget_Paint(&p, buf->texture);
                    isTargetSet = iTrue;
                }
                drawItems.start = invalidRange[i].start / d->itemHeight;
                drawItems.end   = invalidRange[i].end   / d->itemHeight + 1;
                for (size_t j = drawItems.start; j < drawItems.end && j < size_PtrArray(&d->items); j++) {
                    const iListItem *item     = constAt_PtrArray(&d->items, j);
                    const iRect      itemRect = { init_I2(0, j * d->itemHeight - buf->origin),
                                                  init_I2(d->visBufSize.x, d->itemHeight) };
                    fillRect_Paint(&p, itemRect, bg[i]);
                    class_ListItem(item)->draw(item, &p, itemRect, d);
//                    printf("- drawing item %zu\n", j);
                }
            }
            if (isTargetSet) {
                endTarget_Paint(&p);
            }
            buf->validRange =
                intersect_Rangei(vis, (iRangei){ buf->origin, buf->origin + d->visBufSize.y });
//            fflush(stdout);
        }
        clear_IntSet(&m->invalidItems);
    }
    setClip_Paint(&p, bounds_Widget(w));
    iForIndices(i, d->visBuffers) {
        const iListVisBuffer *buf = d->visBuffers + i;
        SDL_RenderCopy(render,
                       buf->texture,
                       NULL,
                       &(SDL_Rect){ left_Rect(bounds),
                                    top_Rect(bounds) - d->scrollY + buf->origin,
                                    d->visBufSize.x,
                                    d->visBufSize.y });
    }
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
iEndDefineSubclass(ListWidget)
