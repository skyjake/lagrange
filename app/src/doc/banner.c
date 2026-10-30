/* Copyright 2021 Jaakko Keränen <jaakko.keranen@iki.fi>

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

#include "banner.h"
#include "impl.h"

#include "ui/command.h"
#include <lagrange/lang.h>
#include "render/paint.h"
#include "ui/util.h"
#include "app.h"
#include "feeds.h"

iDeclareType(BannerItem)

struct Impl_BannerItem {
    enum iBannerType type;
    enum iGmStatusCode code;
    iString text; /* Entire message in presentation form. */
    iString command; /* posted when clicked */
    int height;
};

static void init_BannerItem(iBannerItem *d) {
    init_String(&d->text);
    init_String(&d->command);
    d->height = 0;
}

static void deinit_BannerItem(iBannerItem *d) {
    deinit_String(&d->command);
    deinit_String(&d->text);
}

/*----------------------------------------------------------------------------------------------*/

struct Impl_Banner {
    iDocumentWidget *doc;
    iRect rect;
    iString site;
    iString icon;
    int siteHeight;
    iArray items;
    iBool isHover;
    size_t hoverIndex;
    iBool isClick;
    iBool isCentered;
};

iDefineTypeConstruction(Banner)

#if defined (iPlatformTerminal)
#   define itemGap_Banner_     (1 * gap_UI)
#   define itemVPad_Banner_    (1 * gap_UI)
#   define itemHPad_Banner_    (2 * gap_UI)
#   define bottomPad_Banner_   (1 * gap_UI)
#else
#   define itemGap_Banner_     (3 * gap_UI)
#   define itemVPad_Banner_    (2 * gap_UI)
#   define itemHPad_Banner_    (3 * gap_UI)
#   define bottomPad_Banner_   (4 * gap_UI)
#endif
#define noSiteTopPad_Banner_   bottomPad_Banner_

static int iconFont_Banner_(const iBanner *d) {
    return d->isCentered ? fontWithSize_Text(banner_FontId, contentHuge_FontSize) : banner_FontId;
}

static void updateHeight_Banner_(iBanner *d) {
    d->rect.size.y = 0;
    if (!isEmpty_String(&d->site)) {
        d->siteHeight = iMax(3, lineHeight_Text(banner_FontId) * 2);
        if (d->isCentered) {
            d->siteHeight += lineHeight_Text(iconFont_Banner_(d));
        }
        d->rect.size.y += d->siteHeight;
    }
    const size_t numItems = size_Array(&d->items);
    if (numItems) {
        if (isEmpty_String(&d->site)) {
            d->rect.size.y += noSiteTopPad_Banner_;
        }
        iConstForEach(Array, i, &d->items) {
            const iBannerItem *item = i.value;
            d->rect.size.y += item->height;
        }
        d->rect.size.y += (numItems - 1) * itemGap_Banner_;
        d->rect.size.y += bottomPad_Banner_;
    }
}

void init_Banner(iBanner *d) {
    d->doc = NULL;
    d->rect = zero_Rect();
    init_String(&d->site);
    init_String(&d->icon);
    init_Array(&d->items, sizeof(iBannerItem));
    d->isClick = iFalse;
    d->isHover = iFalse;
    d->hoverIndex = iInvalidPos;
    d->isCentered = iFalse;
}

void deinit_Banner(iBanner *d) {
    clear_Banner(d);
    deinit_Array(&d->items);
    deinit_String(&d->icon);
    deinit_String(&d->site);
}

void setOwner_Banner(iBanner *d, iDocumentWidget *owner) {
    d->doc = owner;
}

static void updateItemHeight_Banner_(const iBanner *d, iBannerItem *item) {
    item->height = measureWrapRange_Text(uiContent_FontId,
                                         width_Rect(d->rect) - 2 * itemHPad_Banner_,
                                         range_String(&item->text))
                       .bounds.size.y + 2 * itemVPad_Banner_;
}

void setWidth_Banner(iBanner *d, int width) {
    d->rect.size.x = width;
    iForEach(Array, i, &d->items) {
        updateItemHeight_Banner_(d, i.value);
    }
    updateHeight_Banner_(d);
}

void setCentered_Banner(iBanner *d, iBool isCentered) {
    if (d->isCentered != isCentered) {
        d->isCentered = isCentered;
        updateHeight_Banner_(d);
    }
}

void setPos_Banner(iBanner *d, iInt2 pos) {
    d->rect.pos = pos;
}

int height_Banner(const iBanner *d) {
    return d->rect.size.y;
}

int siteTitleMid_Banner(const iBanner *d) {
    if (isEmpty_String(&d->site)) {
        return 0;
    }
    /* Same as in `draw_Banner`. */
    const int lineHeight = lineHeight_Text(banner_FontId);
    int       mid        = iMax(1, lineHeight / 2) + lineHeight / 2;
    if (d->isCentered && !isEmpty_String(&d->icon)) {
        mid += lineHeight_Text(iconFont_Banner_(d));
    }
    return mid;
}

size_t numItems_Banner(const iBanner *d) {
    return size_Array(&d->items);
}

iBool hasError_Banner(const iBanner *d) {
    iConstForEach(Array, i, &d->items) {
        if (((const iBannerItem *) i.value)->type == error_BannerType) {
            return iTrue;
        }
    }
    return iFalse;
}

iBool contains_Banner(const iBanner *d, iInt2 coord) {
    return contains_Rect(d->rect, coord);
}

const iString *message_Banner(const iBanner *d, size_t itemIndex) {
    return &constValue_Array(&d->items, itemIndex, iBannerItem).text;
}

void clear_Banner(iBanner *d) {
    iForEach(Array, i, &d->items) {
        deinit_BannerItem(i.value);
    }
    clear_Array(&d->items);
    clear_String(&d->site);
    clear_String(&d->icon);
    d->rect.size.y = 0;
}

void setSite_Banner(iBanner *d, iRangecc site, iChar icon) {
    clear_String(&d->site);
    clear_String(&d->icon);
    if (icon) {
        setRange_String(&d->site, site);
        appendChar_String(&d->icon, icon);
    }
    updateHeight_Banner_(d);
}

static void setText_BannerItem_(iBannerItem *d, iChar icon, const char *title,
                                const char *details, iBool isTitleBold) {
    clear_String(&d->text);
    appendCStr_String(&d->text, escape_Color(tmBannerIcon_ColorId));
    appendChar_String(&d->text, icon);
    appendCStr_String(&d->text, restore_ColorEscape);
    const char *sep = startsWith_CStr(details, "\n") ? "" : " \u2014 ";
    appendFormat_String(&d->text, "  %s%s%s%s%s%s%s",
                        isTitleBold ? "\x1b[1m" : "",
                        escape_Color(isTitleBold ? tmBannerItemTitle_ColorId
                                                 : tmBannerItemText_ColorId),
                        title,
                        isTitleBold ? "\x1b[0m" : "",
                        escape_Color(tmBannerItemText_ColorId),
                        sep,
                        details);
}

static void insertItem_Banner_(iBanner *d, size_t pos, iBannerItem *item) {
    updateItemHeight_Banner_(d, item);
    insert_Array(&d->items, pos, item);
    updateHeight_Banner_(d);
}

void add_Banner(iBanner *d, enum iBannerType type, enum iGmStatusCode code,
                const iString *message, const iString *details) {
    /* If there already is a matching item, don't add a second one. */
    iConstForEach(Array, i, &d->items) {
        const iBannerItem *item = i.value;
        if (item->type == type && item->code == code) {
            return;
        }
    }
    iBannerItem item;
    init_BannerItem(&item);
    item.type = type;
    item.code = code;
    const iGmError *error = get_GmError(code);
    setText_BannerItem_(&item,
                        code && error->icon ? error->icon : 0x26a0, /* /!\ */
                        !isEmpty_String(message) ? cstr_String(message) : error->title,
                        !isEmpty_String(details) ? cstr_String(details) : error->info,
                        iTrue);
    translate_Lang(&item.text);
    insertItem_Banner_(d, size_Array(&d->items), &item);
}

void addNotice_Banner(iBanner *d, iChar icon, const iString *title, const iString *details,
                      const char *command, iBool isTitleBold) {
    size_t pos = 0;
    iConstForEach(Array, i, &d->items) {
        const iBannerItem *item = i.value;
        if (item->type != notice_BannerType) {
            break;
        }
        if (!cmp_String(&item->command, command)) {
            return;
        }
        pos++;
    }
    /* Notices are placed before other items. */
    iBannerItem item;
    init_BannerItem(&item);
    item.type = notice_BannerType;
    item.code = none_GmStatusCode;
    setCStr_String(&item.command, command);
    setText_BannerItem_(&item, icon, cstr_String(title), cstr_String(details), isTitleBold);
    insertItem_Banner_(d, pos, &item);
}

void remove_Banner(iBanner *d, enum iGmStatusCode code) {
    iForEach(Array, i, &d->items) {
        iBannerItem *item = i.value;
        if (item->code == code) {
            deinit_BannerItem(item);
            remove_ArrayIterator(&i);
        }
    }
    updateHeight_Banner_(d);
}

void draw_Banner(const iBanner *d) {
    if (isEmpty_Banner(d)) {
        return;
    }
    iRect  bounds = d->rect;
    /* TODO: use d->siteHeight */
    iInt2  pos    = addY_I2(topLeft_Rect(bounds), iMax(1, lineHeight_Text(banner_FontId) / 2));
    iPaint p;
    init_Paint(&p);
//    drawRect_Paint(&p, bounds, red_ColorId);
    if (d->isCentered) {
        setOpacity_Text(0.5f);
    }
    /* Draw the icon. */
    if (!isEmpty_String(&d->icon)) {
        const int   font     = iconFont_Banner_(d);
        const iRect iconRect = visualBounds_Text(font, range_String(&d->icon));
        iInt2       iconPos  = addY_I2(pos, -mid_Rect(iconRect).y + lineHeight_Text(font) / 2);
        if (d->isCentered) {
            iconPos.x = mid_Rect(bounds).x - mid_Rect(iconRect).x;
        }
        drawRange_Text(font, iconPos, tmBannerIcon_ColorId, range_String(&d->icon));
        if (d->isCentered) {
            pos.y += lineHeight_Text(font);
        }
        else {
            pos.x += right_Rect(iconRect) + 3 * gap_Text;
        }
    }
    /* Draw the site name. */
    if (!isEmpty_String(&d->site)) {
        if (d->isCentered) {
            pos.x = mid_Rect(bounds).x -
                    measureRange_Text(banner_FontId, range_String(&d->site)).advance.x / 2;
        }
        drawRange_Text(banner_FontId, pos, tmBannerTitle_ColorId, range_String(&d->site));
        pos.y += (int) ceilf(lineHeight_Text(banner_FontId) * 3.0f / 2.0f);
    }
    else {
        pos.y = top_Rect(bounds) + noSiteTopPad_Banner_;
    }
    setOpacity_Text(1.0f);
//    const int innerPad = gap_UI;
    pos.x = left_Rect(bounds);
    iConstForEach(Array, i, &d->items) {
        const iBannerItem *item = i.value;
        const iRect itemRect = { pos, init_I2(d->rect.size.x, item->height) };
        const int frame = item->type != error_BannerType && d->isHover &&
                                  d->hoverIndex == index_ArrayConstIterator(&i)
                              ? tmBannerItemText_ColorId
                              : tmBannerItemFrame_ColorId;
        if (isTerminal_Platform()) {
            fillRect_Paint(&p, itemRect, tmBannerItemBackground_ColorId);
            drawRect_Paint(&p, itemRect, frame);
        }
        else {
            drawRoundRect_Paint(
                &p, itemRect, all_RoundCorner, 1, tmBannerItemBackground_ColorId, frame, frame);
        }
        setBaseAttributes_Text(uiContent_FontId, tmBannerItemText_ColorId);
        iWrapText wt = {
            .text = range_String(&item->text),
            .maxWidth = width_Rect(itemRect) - 2 * itemHPad_Banner_,
            .mode = word_WrapTextMode
        };
        draw_WrapText(&wt, uiContent_FontId, add_I2(pos, init_I2(itemHPad_Banner_, itemVPad_Banner_)),
                      tmBannerItemText_ColorId);
        pos.y += item->height + itemGap_Banner_;
    }
    setBaseAttributes_Text(-1, -1);
}

size_t itemAtCoord_Banner(const iBanner *d, iInt2 coord) {
    iInt2 pos = addY_I2(topLeft_Rect(d->rect),
                        isEmpty_String(&d->site) ? noSiteTopPad_Banner_ : d->siteHeight);
    iConstForEach(Array, i, &d->items) {
        const iBannerItem *item = i.value;
        if (contains_Rect((iRect){ pos, init_I2(d->rect.size.x, item->height)}, coord)) {
            return index_ArrayConstIterator(&i);
        }
        pos.y += itemGap_Banner_ + item->height;
    }
    return iInvalidPos;
}

static iBool isInside_Banner_(const iBanner *d, const SDL_Event *ev) {
    if (ev->type == SDL_EVENT_MOUSE_MOTION || ev->type == SDL_EVENT_MOUSE_BUTTON_DOWN ||
        ev->type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
        iInt2 coord;
        if (ev->type == SDL_EVENT_MOUSE_MOTION) {
            coord = init_I2(ev->motion.x, ev->motion.y);
        }
        else {
            coord = init_I2(ev->button.x, ev->button.y);
        }
        return contains_Rect(bounds_Widget(constAs_Widget(d->doc)), coord);
    }
    return iTrue;
}

iBool processEvent_Banner(iBanner *d, const SDL_Event *ev) {
    iWidget *w = as_Widget(d->doc);
    if (!isInside_Banner_(d, ev)) {
        return iFalse;
    }
    switch (ev->type) {
        case SDL_EVENT_MOUSE_MOTION: {
            const iInt2 coord = init_I2(ev->motion.x, ev->motion.y);
            const iBool isInside = contains_Rect(d->rect, coord);
            if (isInside) {
                setCursor_Window(window_Widget(w), SDL_SYSTEM_CURSOR_POINTER);
            }
            if (isInside ^ d->isHover) {
                d->isHover = isInside;
            }
            const size_t at = d->isHover ? itemAtCoord_Banner(d, coord) : iInvalidPos;
            if (at != d->hoverIndex) {
                d->hoverIndex = at;
                refresh_Widget(w);
            }
            break;
        }
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
        case SDL_EVENT_MOUSE_BUTTON_UP: {
            /* Clicking on the top/side banner navigates to site root. */
            const iInt2 coord    = init_I2(ev->button.x, ev->button.y);
            const iBool isInside = contains_Rect(d->rect, coord);
            if (isInside && ev->button.button == SDL_BUTTON_RIGHT &&
                !ev->button.down) {
                const size_t index = itemAtCoord_Banner(d, coord);
                if (index < iInvalidPos) {
                    const iBannerItem *item = constAt_Array(&d->items, index);
                    iArray *menuItems = new_Array(sizeof(iMenuItem));
                    if (item->type == notice_BannerType &&
                        hasResponseLink_Feeds(feedEntryUrl_DocumentWidget(d->doc))) {
                        pushBack_Array(menuItems,
                                       &(iMenuItem){ "${menu.copyresponse}", 0, 0,
                                                     format_CStr("!banner.copyresponse ptr:%p",
                                                                 d->doc) });
                        pushBack_Array(menuItems, &(iMenuItem){ "---" });
                    }
                    pushBack_Array(menuItems, &(iMenuItem){ "${menu.banner.copy}", 0, 0,
                                                            format_CStr("!banner.copy ptr:%p arg:%u",
                                                                        d->doc, index) });
                    openMenu_Widget(makeMenu_Widget(get_Root()->widget, constData_Array(menuItems), size_Array(menuItems)),
                                    mouseCoord_Window(get_Window(), 0));
                    delete_Array(menuItems);
                }
            }
            else if (ev->button.button == SDL_BUTTON_LEFT) {
                if (isInside && ev->button.down) {
                    d->isClick = iTrue;
                    return iTrue;
                }
                else if (!ev->button.down) {
                    if (d->isClick && isInside) {
                        const size_t index = itemAtCoord_Banner(d, coord);
                        if (index == iInvalidPos) {
                            if (coord.y < top_Rect(d->rect) + d->siteHeight) {
                                postCommand_Widget(d->doc, "navigate.root");
                            }
                        }
                        else {
                            const iBannerItem *item = constAt_Array(&d->items, index);
                            d->isHover = iFalse;
                            if (item->type == error_BannerType) {
                                postCommand_Widget(d->doc, "document.info");
                            }
                            else if (item->type == notice_BannerType) {
                                if (!isEmpty_String(&item->command)) {
                                    postCommandString_Root(w->root, &item->command);
                                }
                            }
                            else {
                                switch (item->code) {
                                    case missingGlyphs_GmStatusCode:
                                        makeGlyphFinder_Widget();
                                        break;
                                    case ansiEscapes_GmStatusCode:
                                        makeQuestion_Widget(
                                            uiHeading_ColorEscape "${heading.dismiss.warning}",
                                            format_Lang(
                                                "${dlg.dismiss.ansi}",
                                                format_CStr(uiTextStrong_ColorEscape
                                                            "%s" restore_ColorEscape,
                                                            cstr_Rangecc(urlHost_String(
                                                                url_DocumentWidget(d->doc))))),
                                            (iMenuItem[]) {
                                                { "${cancel}" },
                                                { uiTextAction_ColorEscape "${dlg.dismiss.warning}",
                                                  SDLK_RETURN,
                                                  0,
                                                  format_CStr("!document.dismiss warning:%d ptr:%p",
                                                              ansiEscapes_GmDocumentWarning,
                                                              d->doc) } },
                                            2);
                                        break;
                                    default:
                                        postCommand_Widget(d->doc, "document.info");
                                        break;
                                }
                            }
                        }
                    }
                    d->isClick = iFalse;
                }
                /* Clicking on a warning? */
//                if (bannerType_DocumentWidget_(d) == certificateWarning_GmDocumentBanner &&
//                    pos_Click(&d->click).y - top_Rect(banRect) >
//                    lineHeight_Text(banner_FontId) * 2) {
//                    postCommand_Widget(d, "document.info");
//                }
            }
            break;
        }
    }
    return iFalse;
}
