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

struct Impl_ListWidget {
    iWidget widget;
    iScrollWidget *scroll;
    int scrollY;
    int itemHeight;
    iPtrArray items;
    size_t hoverItem;
    iClick click;
    iIntSet invalidItems;
    SDL_Texture *visBuffer[2];
    int visBufferIndex;
    int visBufferScrollY;
    enum iBufferValidity visBufferValid;
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
    iZap(d->visBuffer);
    d->visBufferIndex = 0;
    d->visBufferValid = none_BufferValidity;
    d->visBufferScrollY = 0;
}

void deinit_ListWidget(iListWidget *d) {
    clear_ListWidget(d);
    deinit_PtrArray(&d->items);
    SDL_DestroyTexture(d->visBuffer[0]);
    SDL_DestroyTexture(d->visBuffer[1]);
}

void invalidate_ListWidget(iListWidget *d) {
    d->visBufferValid = none_BufferValidity;
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
    d->visBufferValid = partial_BufferValidity;
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
        d->hoverItem = iInvalidPos;
        updateVisible_ListWidget(d);
        d->visBufferValid = partial_BufferValidity;
        refresh_Widget(as_Widget(d));
    }
}

static int visCount_ListWidget_(const iListWidget *d) {
    return iMin(height_Rect(innerBounds_Widget(constAs_Widget(d))) / d->itemHeight + 2,
                (int) size_PtrArray(&d->items));
}

static iRanges visRange_ListWidget_(const iListWidget *d) {
    if (d->itemHeight == 0) {
        return (iRanges){ 0, 0 };
    }
    iRanges vis = { d->scrollY / d->itemHeight, 0 };
    vis.end = iMin(size_PtrArray(&d->items), vis.start + visCount_ListWidget_(d));
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
    const iInt2 size = innerBounds_Widget(as_Widget(d)).size;
    if (!d->visBuffer[0] || !isEqual_I2(size_SDLTexture(d->visBuffer[0]), size)) {
        iForIndices(i, d->visBuffer) {
            if (d->visBuffer[i]) {
                SDL_DestroyTexture(d->visBuffer[i]);
            }
            d->visBuffer[i] = SDL_CreateTexture(renderer_Window(get_Window()),
                                                SDL_PIXELFORMAT_RGBA8888,
                                                SDL_TEXTUREACCESS_STATIC | SDL_TEXTUREACCESS_TARGET,
                                                size.x,
                                                size.y);
            SDL_SetTextureBlendMode(d->visBuffer[i], SDL_BLENDMODE_NONE);
        }
        d->visBufferValid = none_BufferValidity;
    }
}

static void drawItem_ListWidget_(const iListWidget *d, iPaint *p, size_t index, iInt2 pos) {
    const iWidget *  w         = constAs_Widget(d);
    const iRect      bounds    = innerBounds_Widget(w);
    const iRect      bufBounds = { zero_I2(), bounds.size };
    const iListItem *item      = constAt_PtrArray(&d->items, index);
    const iRect      itemRect  = { pos, init_I2(width_Rect(bounds), d->itemHeight) };
    setClip_Paint(p, intersect_Rect(itemRect, bufBounds));
    if (d->visBufferValid) {
        fillRect_Paint(p, itemRect, w->bgColor);
    }
    class_ListItem(item)->draw(item, p, itemRect, d);
    unsetClip_Paint(p);
}

static const iListItem *item_ListWidget_(const iListWidget *d, size_t pos) {
    return constAt_PtrArray(&d->items, pos);
}

static void draw_ListWidget_(const iListWidget *d) {
    const iWidget *w      = constAs_Widget(d);
    const iRect    bounds = innerBounds_Widget(w);
    if (!bounds.size.y || !bounds.size.x) return;
    iPaint p;
    init_Paint(&p);
    SDL_Renderer *render = renderer_Window(get_Window());
    drawBackground_Widget(w);
    if (d->visBufferValid != full_BufferValidity || !isEmpty_IntSet(&d->invalidItems)) {
        iListWidget *m = iConstCast(iListWidget *, d);
        allocVisBuffer_ListWidget_(m);
        iAssert(d->visBuffer);
        const int vbSrc = d->visBufferIndex;
        const int vbDst = d->visBufferIndex ^ 1;
        beginTarget_Paint(&p, d->visBuffer[vbDst]);
        const iRect bufBounds = (iRect){ zero_I2(), bounds.size };
        iRanges invalidRange = { 0, 0 };
        if (!d->visBufferValid) {
            fillRect_Paint(&p, bufBounds, w->bgColor);
        }
        else if (d->visBufferValid == partial_BufferValidity) {
            /* Copy previous contents. */
            const int delta = d->scrollY - d->visBufferScrollY;
            SDL_RenderCopy(
                render,
                d->visBuffer[vbSrc],
                NULL,
                &(SDL_Rect){ 0, -delta, bounds.size.x, bounds.size.y });
            if (delta > 0) {
                /* Scrolling down. */
                invalidRange.start = (d->visBufferScrollY + bounds.size.y) / d->itemHeight;
                invalidRange.end   = (d->scrollY + bounds.size.y) / d->itemHeight + 1;
            }
            else if (delta < 0) {
                /* Scrolling up. */
                invalidRange.start = d->scrollY / d->itemHeight;
                invalidRange.end   = d->visBufferScrollY / d->itemHeight + 1;
            }
#if 0
            /* Separators may consist of multiple items. */
            if (item_ListWidget_(d, invalidRange.start)->isSeparator) {
                invalidRange.start--;
            }
            if (item_ListWidget_(d, invalidRange.end)->isSeparator) {
                invalidRange.end++;
            }
#endif
        }
        /* Draw items. */ {
            const iRanges visRange = visRange_ListWidget_(d);
            iInt2         pos      = init_I2(0, -(d->scrollY % d->itemHeight));
            for (size_t i = visRange.start; i < visRange.end; i++) {
                /* TODO: Refactor to loop through invalidItems only. */
                if (!d->visBufferValid || contains_Range(&invalidRange, i) ||
                    contains_IntSet(&d->invalidItems, i)) {
                    const iListItem *item     = constAt_PtrArray(&d->items, i);
                    const iRect      itemRect = { pos, init_I2(width_Rect(bounds), d->itemHeight) };
                    setClip_Paint(&p, intersect_Rect(itemRect, bufBounds));
                    if (d->visBufferValid) {
                        fillRect_Paint(&p, itemRect, w->bgColor);
                    }
                    class_ListItem(item)->draw(item, &p, itemRect, d);
                    /* Clear under the scrollbar. */
                    if (isVisible_Widget(d->scroll)) {
                        fillRect_Paint(
                            &p,
                            (iRect){ addX_I2(topRight_Rect(itemRect), -width_Widget(d->scroll)),
                                     bottomRight_Rect(itemRect) },
                            w->bgColor);
                    }
                    unsetClip_Paint(&p);                    
                }
                pos.y += d->itemHeight;
            }
        }
        endTarget_Paint(&p);
        /* Update state. */
        m->visBufferValid = iTrue;
        m->visBufferScrollY = m->scrollY;
        m->visBufferIndex = vbDst;
        clear_IntSet(&m->invalidItems);
    }
    SDL_RenderCopy(render, d->visBuffer[d->visBufferIndex], NULL, (const SDL_Rect *) &bounds);
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
