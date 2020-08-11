#include "sidebarwidget.h"
#include "labelwidget.h"
#include "scrollwidget.h"
#include "documentwidget.h"
#include "paint.h"
#include "util.h"
#include "command.h"
#include "../gmdocument.h"
#include "app.h"

#include <the_Foundation/array.h>

iDeclareType(SidebarItem)

struct Impl_SidebarItem {
    int     indent;
    iChar   icon;
    iString label;
    iString meta;
    iString url;
    size_t  index;
};

void init_SidebarItem(iSidebarItem *d) {
    d->indent = 0;
    d->icon   = 0;
    d->index  = 0;
    init_String(&d->label);
    init_String(&d->meta);
    init_String(&d->url);
}

void deinit_SidebarItem(iSidebarItem *d) {
    deinit_String(&d->url);
    deinit_String(&d->meta);
    deinit_String(&d->label);
}

iDefineTypeConstruction(SidebarItem)

/*----------------------------------------------------------------------------------------------*/

struct Impl_SidebarWidget {
    iWidget widget;
    enum iSidebarMode mode;
    iScrollWidget *scroll;
    int scrollY;
    iLabelWidget *modeButtons[max_SidebarMode];
    int itemHeight;
    iArray items;
    size_t hoverItem;
    iClick click;
};

iDefineObjectConstruction(SidebarWidget)

static void clearItems_SidebarWidget_(iSidebarWidget *d) {
    iForEach(Array, i, &d->items) {
        deinit_SidebarItem(i.value);
    }
    clear_Array(&d->items);
}

static void updateItems_SidebarWidget_(iSidebarWidget *d) {
    clearItems_SidebarWidget_(d);
    switch (d->mode) {
        case documentOutline_SidebarMode: {
            const iGmDocument *doc = document_DocumentWidget(document_App());
            iConstForEach(Array, i, headings_GmDocument(doc)) {
                const iGmHeading *head = i.value;
                iSidebarItem item;
                init_SidebarItem(&item);
                item.index = index_ArrayConstIterator(&i);
                setRange_String(&item.label, head->text);
                item.indent = head->level * 4 * gap_UI;
                pushBack_Array(&d->items, &item);
            }
            break;
        }
        case bookmarks_SidebarMode:
            break;
        case history_SidebarMode:
            break;
        default:
            break;
    }
    refresh_Widget(as_Widget(d));
}

void setMode_SidebarWidget(iSidebarWidget *d, enum iSidebarMode mode) {
    if (d->mode == mode) return;
    d->mode = mode;
    for (enum iSidebarMode i = 0; i < max_SidebarMode; i++) {
        setFlags_Widget(as_Widget(d->modeButtons[i]), selected_WidgetFlag, i == d->mode);
    }
    const float heights[max_SidebarMode] = { 1.2f, 2, 3, 3 };
    d->itemHeight = heights[mode] * lineHeight_Text(default_FontId);
}

void init_SidebarWidget(iSidebarWidget *d) {
    iWidget *w = as_Widget(d);
    init_Widget(w);
    setBackgroundColor_Widget(w, none_ColorId);
    setFlags_Widget(w, hover_WidgetFlag | arrangeHorizontal_WidgetFlag, iTrue);
    d->scrollY = 0;
    d->mode = -1;
    w->rect.size.x = 100 * gap_UI;
    init_Array(&d->items, sizeof(iSidebarItem));
    d->hoverItem = iInvalidPos;
    init_Click(&d->click, d, SDL_BUTTON_LEFT);
    setFlags_Widget(w, fixedWidth_WidgetFlag, iTrue);
    const char *buttonLabels[max_SidebarMode] = {
        "\U0001f5b9 Outline",
        "\U0001f588 Bookmarks",
        "\U0001f553 History",
        "\U0001f464 Identities",
    };
    for (int i = 0; i < max_SidebarMode; i++) {
        d->modeButtons[i] = addChildFlags_Widget(
            w,
            iClob(new_LabelWidget(buttonLabels[i], 0, 0, format_CStr("sidebar.mode arg:%d", i))),
            frameless_WidgetFlag);
    }
    addChild_Widget(w, iClob(d->scroll = new_ScrollWidget()));
    setThumb_ScrollWidget(d->scroll, 0, 0);
    setMode_SidebarWidget(d, documentOutline_SidebarMode);
}

void deinit_SidebarWidget(iSidebarWidget *d) {
    clearItems_SidebarWidget_(d);
    deinit_Array(&d->items);
}

static iRect contentBounds_SidebarWidget_(const iSidebarWidget *d) {
    iRect bounds = bounds_Widget(constAs_Widget(d));
    adjustEdges_Rect(&bounds, as_Widget(d->modeButtons[0])->rect.size.y + gap_UI, 0, 0, 0);
    return bounds;
}

static int visCount_SidebarWidget_(const iSidebarWidget *d) {
    return iMin(height_Rect(bounds_Widget(constAs_Widget(d))) / d->itemHeight,
                (int) size_Array(&d->items));
}

static iRanges visRange_SidebarWidget_(const iSidebarWidget *d) {
    iRanges vis = { d->scrollY / d->itemHeight, 0 };
    vis.end = iMin(size_Array(&d->items), vis.start + visCount_SidebarWidget_(d));
    return vis;
}

static size_t itemIndex_SidebarWidget_(const iSidebarWidget *d, iInt2 pos) {
    const iRect bounds = contentBounds_SidebarWidget_(d);
    pos.y -= top_Rect(bounds) - d->scrollY;
    if (pos.y < 0) return iInvalidPos;
    size_t index = pos.y / d->itemHeight;
    if (index >= size_Array(&d->items)) return iInvalidPos;
    return index;
}

static void itemClicked_SidebarWidget_(iSidebarWidget *d, size_t index) {
    const iSidebarItem *item = constAt_Array(&d->items, index);
    switch (d->mode) {
        case documentOutline_SidebarMode: {
            const iGmDocument *doc = document_DocumentWidget(document_App());
            const iGmHeading *head = constAt_Array(headings_GmDocument(doc), item->index);
            postCommandf_App("document.goto loc:%p", head->text.start);
            break;
        }
    }
}

static void scroll_SidebarWidget_(iSidebarWidget *d, int offset) {
    d->scrollY += offset;
    if (d->scrollY < 0) {
        d->scrollY = 0;
    }
    const int scrollMax = iMax(0,
                               (int) size_Array(&d->items) * d->itemHeight -
                                   height_Rect(contentBounds_SidebarWidget_(d)));
    d->scrollY = iMin(d->scrollY, scrollMax);
    refresh_Widget(as_Widget(d));
}

static iBool processEvent_SidebarWidget_(iSidebarWidget *d, const SDL_Event *ev) {
    iWidget *w = as_Widget(d);
    /* Handle commands. */
    if (ev->type == SDL_USEREVENT && ev->user.code == command_UserEventCode) {
        const char *cmd = command_UserEvent(ev);
        if (isCommand_Widget(w, ev, "sidebar.mode")) {
            setMode_SidebarWidget(d, arg_Command(cmd));
            updateItems_SidebarWidget_(d);
            return iTrue;
        }
        else if (equal_Command(cmd, "tabs.changed") || equal_Command(cmd, "document.changed")) {
            updateItems_SidebarWidget_(d);
        }
    }
    if (ev->type == SDL_MOUSEMOTION) {
        const iInt2 mouse = init_I2(ev->motion.x, ev->motion.y);
        size_t hover = iInvalidPos;
        if (contains_Widget(w, mouse)) {
            hover = itemIndex_SidebarWidget_(d, mouse);
        }
        if (hover != d->hoverItem) {
            d->hoverItem = hover;
            refresh_Widget(w);
        }
    }
    if (ev->type == SDL_MOUSEWHEEL && isHover_Widget(w)) {
#if defined (iPlatformApple)
        /* Momentum scrolling. */
        scroll_SidebarWidget_(d, -ev->wheel.y * get_Window()->pixelRatio);
#endif
        d->hoverItem = iInvalidPos;
        refresh_Widget(w);
        return iTrue;
    }
    switch (processEvent_Click(&d->click, ev)) {
        case started_ClickResult:
            refresh_Widget(w);
            break;
        case finished_ClickResult:
            if (contains_Rect(contentBounds_SidebarWidget_(d), pos_Click(&d->click)) &&
                d->hoverItem != iInvalidSize) {
                itemClicked_SidebarWidget_(d, d->hoverItem);
            }
            refresh_Widget(w);
            break;
        default:
            break;
    }
    return processEvent_Widget(w, ev);
}

static void draw_SidebarWidget_(const iSidebarWidget *d) {
    const iWidget *w      = constAs_Widget(d);
    const iRect    bounds = contentBounds_SidebarWidget_(d);
    const iBool    isPressing = d->click.isActive && contains_Rect(bounds, pos_Click(&d->click));
    iPaint p;
    init_Paint(&p);
    /* Draw the items. */ {
        const int font = default_FontId;
//        /iConstForEach(Array, i, &d->items) {
        const iRanges visRange = visRange_SidebarWidget_(d);
        iInt2 pos = addY_I2(topLeft_Rect(bounds), -(d->scrollY % d->itemHeight));
        for (size_t i = visRange.start; i < visRange.end; i++) {
            const iSidebarItem *item = constAt_Array(&d->items, i);
            const iRect itemRect = { pos, init_I2(width_Rect(bounds), d->itemHeight) };
            const iBool isHover = (d->hoverItem == i);
            if (isHover) {
                fillRect_Paint(&p, itemRect, isPressing ? orange_ColorId : teal_ColorId);
            }
            setClip_Paint(&p, itemRect);
            const int fg = isHover ? (isPressing ? black_ColorId : white_ColorId) : gray75_ColorId;
            if (d->mode == documentOutline_SidebarMode) {
                drawRange_Text(font, init_I2(pos.x + 3 * gap_UI + item->indent,
                                        mid_Rect(itemRect).y - lineHeight_Text(font) / 2),
                               fg, range_String(&item->label));
            }
            unsetClip_Paint(&p);
            pos.y += d->itemHeight;
        }
    }
    draw_Widget(w);
}

iBeginDefineSubclass(SidebarWidget, Widget)
    .processEvent = (iAny *) processEvent_SidebarWidget_,
    .draw         = (iAny *) draw_SidebarWidget_,
iEndDefineSubclass(SidebarWidget)
