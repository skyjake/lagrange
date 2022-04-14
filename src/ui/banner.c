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

#include "command.h"
#include "documentwidget.h"
#include "lang.h"
#include "paint.h"
#include "util.h"
#include "app.h"

iDeclareType(BannerItem)
    
struct Impl_BannerItem {
    enum iBannerType type;
    enum iGmStatusCode code;
    iString text; /* Entire message in presentation form. */
    int height;
};

static void init_BannerItem(iBannerItem *d) {
    init_String(&d->text);
    d->height = 0;
}

static void deinit_BannerItem(iBannerItem *d) {
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

static void updateHeight_Banner_(iBanner *d) {
    d->rect.size.y = 0;
    if (!isEmpty_String(&d->site)) {
        d->siteHeight = iMax(3, lineHeight_Text(banner_FontId) * 2);
        d->rect.size.y += d->siteHeight;
    }
    const size_t numItems = size_Array(&d->items);
    if (numItems) {
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

void setPos_Banner(iBanner *d, iInt2 pos) {
    d->rect.pos = pos;
}

int height_Banner(const iBanner *d) {
    return d->rect.size.y;
}

size_t numItems_Banner(const iBanner *d) {
    return size_Array(&d->items);
}

iBool contains_Banner(const iBanner *d, iInt2 coord) {
    return contains_Rect(d->rect, coord);
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
    iChar icon = code && error->icon ? error->icon : 0x26a0; /* /!\ */
    appendCStr_String(&item.text, escape_Color(tmBannerIcon_ColorId));
    appendChar_String(&item.text, icon);
    appendCStr_String(&item.text, restore_ColorEscape);
    appendFormat_String(&item.text, "  \x1b[1m%s%s\x1b[0m \u2014 %s%s",
                        escape_Color(tmBannerItemTitle_ColorId),
                        !isEmpty_String(message) ? cstr_String(message) : error->title,
                        escape_Color(tmBannerItemText_ColorId),
                        !isEmpty_String(details) ? cstr_String(details) : error->info);
    translate_Lang(&item.text);
    updateItemHeight_Banner_(d, &item);
    pushBack_Array(&d->items, &item);
    updateHeight_Banner_(d);
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
    /* Draw the icon. */
    if (!isEmpty_String(&d->icon)) {
        const int   font     = banner_FontId;
        const iRect iconRect = visualBounds_Text(font, range_String(&d->icon));
        drawRange_Text(font,
                       addY_I2(pos, -mid_Rect(iconRect).y + lineHeight_Text(font) / 2),
                       tmBannerIcon_ColorId,
                       range_String(&d->icon));
        pos.x += right_Rect(iconRect) + 3 * gap_Text;
    }
    /* Draw the site name. */
    if (!isEmpty_String(&d->site)) {
        drawRange_Text(banner_FontId, pos, tmBannerTitle_ColorId, range_String(&d->site));
        pos.y += (int) ceilf(lineHeight_Text(banner_FontId) * 3.0f / 2.0f);
    }
    else {
        pos.y = top_Rect(bounds);
    }
//    const int innerPad = gap_UI;
    pos.x = left_Rect(bounds);
    iConstForEach(Array, i, &d->items) {
        const iBannerItem *item = i.value;
        const iRect itemRect = { pos, init_I2(d->rect.size.x, item->height) };
        fillRect_Paint(&p, itemRect, tmBannerItemBackground_ColorId);
        drawRect_Paint(&p,
                       itemRect,
                       item->type == warning_BannerType && d->isHover &&
                               d->hoverIndex == index_ArrayConstIterator(&i)
                           ? tmBannerItemText_ColorId
                           : tmBannerItemFrame_ColorId);
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

static size_t itemAtCoord_Banner_(const iBanner *d, iInt2 coord) {
    iInt2 pos = addY_I2(topLeft_Rect(d->rect), lineHeight_Text(banner_FontId) * 2);
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
    if (ev->type == SDL_MOUSEMOTION || ev->type == SDL_MOUSEBUTTONDOWN ||
        ev->type == SDL_MOUSEBUTTONDOWN) {
        iInt2 coord;
        if (ev->type == SDL_MOUSEMOTION) {
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
        case SDL_MOUSEMOTION: {
            const iInt2 coord = init_I2(ev->motion.x, ev->motion.y);
            const iBool isInside = contains_Rect(d->rect, coord);
            if (isInside) {
                setCursor_Window(window_Widget(w), SDL_SYSTEM_CURSOR_HAND);
            }
            if (isInside ^ d->isHover) {
                d->isHover = isInside;
            }
            const size_t at = d->isHover ? itemAtCoord_Banner_(d, coord) : iInvalidPos;
            if (at != d->hoverIndex) {
                d->hoverIndex = at;
                refresh_Widget(w);
            }
            break;
        }
        case SDL_MOUSEBUTTONDOWN:
        case SDL_MOUSEBUTTONUP:
            /* Clicking on the top/side banner navigates to site root. */
            if (ev->button.button == SDL_BUTTON_LEFT) {
                const iInt2 coord = init_I2(ev->button.x, ev->button.y);
                const iBool isInside = contains_Rect(d->rect, coord);
                if (isInside && ev->button.state == SDL_PRESSED) {
                    d->isClick = iTrue;
                    return iTrue;
                }
                else if (ev->button.state == SDL_RELEASED) {
                    if (d->isClick && isInside) {
                        const size_t index = itemAtCoord_Banner_(d, coord);
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
                                            (iMenuItem[]){
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
    return iFalse;
}

#if 0
static void drawBannerRun_DrawContext_(iDrawContext *d, const iGmRun *run, iInt2 visPos) {
    const iGmDocument *doc  = d->widget->doc;
    const iChar        icon = siteIcon_GmDocument(doc);
    iString            str;
    init_String(&str);
    iInt2 bpos = add_I2(visPos, init_I2(0, lineHeight_Text(banner_FontId) / 2));
    if (icon) {
        appendChar_String(&str, icon);
        const iRect iconRect = visualBounds_Text(run->font, range_String(&str));
        drawRange_Text(
            run->font,
            addY_I2(bpos, -mid_Rect(iconRect).y + lineHeight_Text(run->font) / 2),
            tmBannerIcon_ColorId,
            range_String(&str));
        bpos.x += right_Rect(iconRect) + 3 * gap_Text;
    }
    drawRange_Text(run->font,
                   bpos,
                   tmBannerTitle_ColorId,
                   bannerText_DocumentWidget_(d->widget));
    if (bannerType_GmDocument(doc) == certificateWarning_GmDocumentBanner) {
        const int domainHeight = lineHeight_Text(banner_FontId) * 2;
        iRect rect = { add_I2(visPos, init_I2(0, domainHeight)),
                       addY_I2(run->visBounds.size, -domainHeight - lineHeight_Text(uiContent_FontId)) };
        format_String(&str, "${heading.certwarn}");
        const int certFlags = d->widget->certFlags;
        if (certFlags & timeVerified_GmCertFlag && certFlags & domainVerified_GmCertFlag) {
            iUrl parts;
            init_Url(&parts, d->widget->mod.url);
            const iTime oldUntil =
                domainValidUntil_GmCerts(certs_App(), parts.host, port_Url(&parts));
            iDate exp;
            init_Date(&exp, &oldUntil);
            iTime now;
            initCurrent_Time(&now);
            const int days = secondsSince_Time(&oldUntil, &now) / 3600 / 24;
            appendCStr_String(&str, "\n");
            if (days <= 30) {
                appendCStr_String(&str,
                                  format_CStr(cstrCount_Lang("dlg.certwarn.mayberenewed.n", days),
                                              cstrCollect_String(format_Date(&exp, "%Y-%m-%d")),
                                              days));
            }
            else {
                appendCStr_String(&str, cstr_Lang("dlg.certwarn.different"));
            }
        }
        else if (certFlags & domainVerified_GmCertFlag) {
            appendCStr_String(&str, "\n");
            appendFormat_String(&str, cstr_Lang("dlg.certwarn.expired"),
                                cstrCollect_String(format_Date(&d->widget->certExpiry, "%Y-%m-%d")));
        }
        else if (certFlags & timeVerified_GmCertFlag) {
            appendCStr_String(&str, "\n");
            appendFormat_String(&str, cstr_Lang("dlg.certwarn.domain"),
                                cstr_String(d->widget->certSubject));
        }
        else {
            appendCStr_String(&str, "\n");
            appendCStr_String(&str, cstr_Lang("dlg.certwarn.domain.expired"));
        }
        const iInt2 dims = measureWrapRange_Text(
            uiContent_FontId, width_Rect(rect) - 16 * gap_UI, range_String(&str)).bounds.size;
        const int warnHeight = run->visBounds.size.y - domainHeight;
        const int yOff = (lineHeight_Text(uiLabelLarge_FontId) -
                          lineHeight_Text(uiContent_FontId)) / 2;
        const iRect bgRect =
            init_Rect(0, visPos.y + domainHeight, d->widgetBounds.size.x, warnHeight);
        fillRect_Paint(&d->paint, bgRect, orange_ColorId);
        if (!isDark_ColorTheme(colorTheme_App())) {
            drawHLine_Paint(&d->paint,
                            topLeft_Rect(bgRect), width_Rect(bgRect), tmBannerTitle_ColorId);
            drawHLine_Paint(&d->paint,
                            bottomLeft_Rect(bgRect), width_Rect(bgRect), tmBannerTitle_ColorId);
        }
        const int fg = black_ColorId;
        adjustEdges_Rect(&rect, warnHeight / 2 - dims.y / 2 - yOff, 0, 0, 0);
        bpos = topLeft_Rect(rect);
        draw_Text(uiLabelLarge_FontId, bpos, fg, "\u26a0");
        adjustEdges_Rect(&rect, 0, -8 * gap_UI, 0, 8 * gap_UI);
        translate_Lang(&str);
        drawWrapRange_Text(uiContent_FontId,
                           addY_I2(topLeft_Rect(rect), yOff),
                           width_Rect(rect),
                           fg,
                           range_String(&str));
    }
    deinit_String(&str);
}
#endif
