/* Copyright 2026 Jaakko Keränen <jaakko.keranen@iki.fi>

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

#include "impl.h"

#include "app.h"
#include "gmutil.h"
#include "render/paint.h"
#include "render/text.h"
#include "ui/listwidget.h"
#include "ui/util.h"

#include <lagrange/lang.h>

static void draw_SidebarItem_(const iSidebarItem *d, iPaint *p, iRect itemRect,
                              const iListWidget *list);
static int  measureHeight_SidebarItem_(const iSidebarItem *d, int width, const iListWidget *list);

iBeginDefineSubclass(SidebarItem, ListItem)
    .draw          = (iAny *) draw_SidebarItem_,
    .measureHeight = (iAny *) measureHeight_SidebarItem_,
iEndDefineSubclass(SidebarItem)

iDefineObjectConstruction(SidebarItem)

void init_SidebarItem(iSidebarItem *d) {
    init_ListItem(&d->listItem);
    d->id     = 0;
    d->indent = 0;
    d->icon   = 0;
    d->isBold = iFalse;
    init_String(&d->label);
    init_String(&d->meta);
    init_String(&d->url);
    d->count = 0;
    iZap(d->ts);
    d->progress = -1.0f;
    d->size     = 0;
    init_String(&d->bookId);
}

void deinit_SidebarItem(iSidebarItem *d) {
    deinit_String(&d->bookId);
    deinit_String(&d->url);
    deinit_String(&d->meta);
    deinit_String(&d->label);
}

static int wrapLines_(int font, iRangecc *text, int width, iRangecc *lines, int maxLines) {
    /* Consumes wrapped lines from `text`. */
    int numLines = 0;
    while (numLines < maxLines && !isEmpty_Range(text)) {
        const char *endPos;
        tryAdvance_Text(font, *text, width, &endPos);
        if (endPos == text->start) {
            tryAdvanceNoWrap_Text(font, *text, width, &endPos);
            if (endPos == text->start) break;
        }
        lines[numLines++] = (iRangecc){ text->start, endPos };
        text->start = endPos;
        trimStart_Rangecc(text);
    }
    return numLines;
}

static void drawLines_(int font, iInt2 pos, int color, const iRangecc *lines, int numLines,
                       iRangecc rest, int width) {
    /* If there is text remaining, the last line is truncated with an ellipsis. */
    for (int i = 0; i < numLines; i++) {
        if (i == numLines - 1 && !isEmpty_Range(&rest)) {
            const char *endPos;
            tryAdvanceNoWrap_Text(font,
                                  (iRangecc){ lines[i].start, rest.end },
                                  width - measure_Text(font, "…").advance.x,
                                  &endPos);
            iRangecc line = { lines[i].start, endPos };
            trimEnd_Rangecc(&line);
            iString str;
            initRange_String(&str, line);
            appendCStr_String(&str, "…");
            drawRange_Text(font, pos, color, range_String(&str));
            deinit_String(&str);
        }
        else {
            drawRange_Text(font, pos, color, lines[i]);
        }
        pos.y += lineHeight_Text(font);
    }
}

static int estimateLines_(int font, const iString *text, int width, int maxLines) {
    /* Undershooting is preferable to leaving empty lines. */
    if (width <= 0) {
        return 1;
    }
    const int textWidth = (int) length_String(text) * measure_Text(font, "x").advance.x * 4 / 5;
    return iClamp(1 + textWidth / width, 1, maxLines);
}

#define maxOutlineLines_SidebarItem_    3
#define maxTinylogLines_SidebarItem_    3 /* excerpt; the author line is separate */
#define feedIconPad_SidebarItem_        (12 * gap_UI)

static int outlineTextWidth_SidebarItem_(const iSidebarItem *d, const iSidebarWidget *sidebar,
                                         int listWidth) {
    /* Room for the scroll bar is always reserved so its visibility doesn't affect wrapping. */
    const int scrollBar = isApple_Platform() ? 0 : width_Widget(sidebar->list->scroll);
    return listWidth - (5 * gap_UI + d->indent) * aspect_UI - scrollBar;
}

static int outlineSeparatorHeight_SidebarItem_(const iSidebarWidget *sidebar) {
    return lineHeight_Text(sidebar->itemFonts[0]) / (isTerminal_Platform() ? 1 : 2);
}

static int feedTextWidth_SidebarItem_(int itemWidth) {
    return itemWidth - feedIconPad_SidebarItem_ - 3 * gap_UI;
}

static int measureHeight_SidebarItem_(const iSidebarItem *d, int width, const iListWidget *list) {
    const iSidebarWidget *sidebar =
        findParentClass_Widget(constAs_Widget(list), &Class_SidebarWidget);
    if (!sidebar || d->listItem.flags.isSeparator) {
        return 0;
    }
    if (sidebar->mode == documentOutline_SidebarMode) {
        /* Top-level headings have a separator above. */
        const int font = sidebar->itemFonts[d->isBold ? 1 : 0];
        return estimateLines_(font,
                              &d->label,
                              outlineTextWidth_SidebarItem_(d, sidebar, width),
                              maxOutlineLines_SidebarItem_) *
                   lineHeight_Text(font) +
               (d->isBold ? outlineSeparatorHeight_SidebarItem_(sidebar) : 0);
    }
    if (sidebar->mode == feedEntries_SidebarMode) {
        const iBool isUnread  = (d->indent & 1) != 0;
        const iBool isTinylog = (d->indent & 2) != 0;
        const int   titleFont = sidebar->itemFonts[isUnread ? 1 : 0];
        const int   h1        = lineHeight_Text(uiLabel_FontId);
        const int   h2        = lineHeight_Text(titleFont);
        if (isTinylog) {
            return h1 + estimateLines_(titleFont,
                                       &d->label,
                                       feedTextWidth_SidebarItem_(width),
                                       maxTinylogLines_SidebarItem_) *
                            h2;
        }
        return iMax(h1, h2) + h2;
    }
    return 0;
}

static void drawDragHandle_(iPaint *p, iRect *itemRect, int bg) {
    const int itemHeight = height_Rect(*itemRect);
    iRect dragRect = { addX_I2(topRight_Rect(*itemRect), -itemHeight * 3 / 2),
                       init_I2(itemHeight * 3 / 2, itemHeight) };
    fillRect_Paint(p, dragRect, bg);
    drawVLine_Paint(p, topLeft_Rect(dragRect), height_Rect(dragRect), uiSeparator_ColorId);
    /* Matches the size of native iOS drag handles. */
    drawCentered_Text(isAppleMobile_Platform() ? uiLabelLarge_FontId : uiContent_FontId,
                      dragRect, iTrue, uiAnnotation_ColorId, menu_Icon);
    /* Adjust the item rectangle to account for the handle. */
    adjustEdges_Rect(itemRect, 0, -width_Rect(dragRect), 0, 0);
}

static void draw_SidebarItem_(const iSidebarItem *d, iPaint *p, iRect itemRect,
                              const iListWidget *list) {
    const iSidebarWidget *sidebar =
        findParentClass_Widget(constAs_Widget(list), &Class_SidebarWidget);
    const iBool isListFocus   = isFocused_Widget(list);
    const iBool isMenuVisible = isVisible_Widget(sidebar->menu);
    const iBool isDragging    = constDragItem_ListWidget(list) == d;
    const iBool isEditing     = sidebar->isEditing; /* only on mobile */
    const iBool isPressing    = isMouseDown_ListWidget(list) && !isDragging;
    const iBool isHoverItem   = (!isMenuVisible && isHover_Widget(constAs_Widget(list)) &&
                               constHoverItem_ListWidget(list) == d) ||
                              (isMenuVisible && sidebar->contextItem == d) ||
                              (isFocused_Widget(list) && constCursorItem_ListWidget(list) == d) ||
                              isDragging;
    /* Pressed items use selection colors. */
    const iBool isSel =
        (isHoverItem && isPressing) ||
        (d->listItem.flags.isSelected && (sidebar->mode == feedEntries_SidebarMode ||
                                          sidebar->mode == identities_SidebarMode ||
                                          sidebar->mode == gempub_SidebarMode)) ||
        (sidebar->mode == siteStructure_SidebarMode && d->id & 1) ||
        (sidebar->mode == openDocuments_SidebarMode && d->indent);
    const iBool isHover      = isHoverItem && !isSel;
    const int scrollBarWidth = scrollBarWidth_ListWidget(list);
    const int blankWidth     = isApple_Platform() ? 0 : scrollBarWidth;
    int       sepHeight      = 0;
    if (sidebar->mode == documentOutline_SidebarMode && d->isBold) {
        /* Top-level headings have a separator above, outside the item's highlight. */
        sepHeight = outlineSeparatorHeight_SidebarItem_(sidebar);
        drawHLine_Paint(p,
                        addY_I2(itemRect.pos, sepHeight / 2),
                        width_Rect(itemRect) - blankWidth,
                        uiSeparator_ColorId);
        adjustEdges_Rect(&itemRect, sepHeight, 0, 0, 0);
    }
    const int itemHeight     = height_Rect(itemRect);
    const int font           = sidebar->itemFonts[d->isBold ? 1 : 0];
    const int iconColor = isSel     ? uiTextSelected_ColorId
                          : isHover ? uiIconHover_ColorId
                                    : uiIcon_ColorId;
    /* Draw item background. */
    int bg = uiBackgroundSidebar_ColorId;
    if (isSel) {
        bg = uiBackgroundSelected_ColorId;
        fillRect_Paint(p, itemRect, bg);
    }
    else if (isHover) {
        bg = uiBackgroundFramelessHover_ColorId;
        fillRect_Paint(p, itemRect, bg);
    }
    else if (sidebar->mode == bookmarks_SidebarMode) {
        if (d->indent) /* remote icon */ {
            bg = uiBackgroundFolder_ColorId;
            fillRect_Paint(p, itemRect, bg);
        }
    }
    else if (sidebar->mode == siteStructure_SidebarMode) {
        if (d->indent >= 2) {
            bg = uiBackgroundFolder_ColorId;
            fillRect_Paint(p, itemRect, bg);
        }
    }
    /* Draw item contents. */
    iInt2 pos = itemRect.pos;
    if (sidebar->mode == documentOutline_SidebarMode) {
        const int level = d->indent / (5 * gap_UI);
        const int fg = isSel        ? uiTextSelected_ColorId
                       : isHover    ? uiTextFramelessHover_ColorId
                       : level <= 1 ? uiTextStrong_ColorId
                       : level == 2 ? uiText_ColorId
                                    : uiTextDim_ColorId;
        const int width    = outlineTextWidth_SidebarItem_(d, sidebar, width_Rect(itemRect));
        const int maxLines = iClamp((itemHeight - list->itemPadding) / lineHeight_Text(font),
                                    1,
                                    maxOutlineLines_SidebarItem_);
        iRangecc  lines[maxOutlineLines_SidebarItem_];
        iRangecc  rest     = range_String(&d->label);
        const int numLines = wrapLines_(font, &rest, width, lines, maxLines);
        drawLines_(font,
                   init_I2(pos.x + (3 * gap_UI + d->indent) * aspect_UI,
                           pos.y + (itemHeight - numLines * lineHeight_Text(font)) / 2),
                   fg,
                   lines,
                   numLines,
                   rest,
                   width);
    }
    else if (sidebar->mode == feedEntries_SidebarMode) {
        const int iconPad = feedIconPad_SidebarItem_;
        if (d->listItem.flags.isSeparator) {
            if (d != constItem_ListWidget(list, 0)) {
                drawHLine_Paint(p,
                                addY_I2(pos, 4 * gap_UI),
                                width_Rect(itemRect) - blankWidth,
                                uiSeparator_ColorId);
            }
            drawRange_Text(
                uiLabelLargeBold_FontId,
                add_I2(pos,
                       init_I2(3 * gap_UI * aspect_UI,
                               itemHeight - lineHeight_Text(uiLabelLargeBold_FontId) - 2 * gap_UI)),
                uiIcon_ColorId,
                range_String(&d->meta));
        }
        else {
            const iBool isUnread  = (d->indent & 1) != 0;
            const iBool isTinylog = (d->indent & 2) != 0;
            const int   titleFont = sidebar->itemFonts[isUnread ? 1 : 0];
            const int   h1        = lineHeight_Text(uiLabel_FontId);
            const int   h2        = lineHeight_Text(titleFont);
            /* Select the layout based on how the title fits. */
            int   metaFg    = isSel ? uiTextSelectedDim_ColorId : uiSubheading_ColorId;
            const iInt2 metaSize =
                measureRange_Text(uiLabel_FontId, range_String(&d->meta)).bounds.size;
            pos.x += iconPad * aspect_UI;
            const int avail = feedTextWidth_SidebarItem_(width_Rect(itemRect));
            const int labelFg = isSel      ? uiTextSelected_ColorId | permanent_ColorId
                                : isUnread ? uiTextStrong_ColorId
                                           : uiText_ColorId;
            int      iconHeight = h1 + h2; /* the icon is aligned with the first two lines */
            iRangecc lines[maxTinylogLines_SidebarItem_];
            if (isTinylog) {
                /* Author and feed on one line, followed by the wrapped excerpt. */
                const int maxLines = iClamp((itemHeight - list->itemPadding - h1) / h2,
                                            1, (int) iElemCount(lines));
                iRangecc  rest     = range_String(&d->label);
                const int numLines = wrapLines_(titleFont, &rest, avail, lines, maxLines);
                pos.y += (itemHeight - h1 - numLines * h2) / 2;
                const char *metaEnd;
                tryAdvanceNoWrap_Text(uiLabel_FontId, range_String(&d->meta), avail, &metaEnd);
                drawRange_Text(uiLabel_FontId,
                               pos,
                               metaFg,
                               (iRangecc){ constBegin_String(&d->meta), metaEnd });
                drawLines_(titleFont, addY_I2(pos, h1), labelFg, lines, numLines, rest, avail);
            }
            else {
                iRangecc rest     = range_String(&d->label);
                int      numLines = wrapLines_(titleFont, &rest, avail, lines, 1);
                if (!isEmpty_Range(&rest) && metaSize.x < avail * 0.75f) {
                    /* Title continues after the meta on the first line. */
                    iconHeight = 2 * h2;
                    pos.y += (itemHeight - iconHeight) / 2;
                    draw_Text(uiLabel_FontId,
                              addY_I2(pos, h2 - h1 - gap_UI / 8),
                              metaFg,
                              "%s \u2014 ",
                              cstr_String(&d->meta));
                    const int skip =
                        metaSize.x + measure_Text(uiLabel_FontId, " \u2014 ").advance.x;
                    const char *endPos;
                    rest = range_String(&d->label);
                    tryAdvance_Text(titleFont, rest, avail - skip, &endPos);
                    drawRange_Text(
                        titleFont, addX_I2(pos, skip), labelFg, (iRangecc){ rest.start, endPos });
                    rest.start = endPos;
                    trimStart_Rangecc(&rest);
                    numLines = wrapLines_(titleFont, &rest, avail, lines, 1);
                    drawLines_(titleFont, addY_I2(pos, h2), labelFg, lines, numLines, rest, avail);
                }
                else {
                    pos.y += (itemHeight - h1 - numLines * h2) / 2;
                    drawRange_Text(uiLabel_FontId, pos, metaFg, range_String(&d->meta));
                    drawLines_(titleFont, addY_I2(pos, h1), labelFg, lines, numLines, rest, avail);
                }
            }
            /* Icon. */ {
                /* TODO: Use the primary hue from the theme of this site. */
                const iRect iconArea = { init_I2(left_Rect(itemRect), pos.y),
                                         init_I2(iconPad * aspect_UI, iconHeight) };
                iString str;
                initUnicodeN_String(&str, &d->icon, 1);
                /* TODO: Add to palette. */
                const int unreadIconColor = uiTextCaution_ColorId;
                const int readIconColor =
                    isDark_ColorTheme(colorTheme_App()) ? uiText_ColorId : uiAnnotation_ColorId;
                drawCentered_Text(uiLabelLarge_FontId,
                                  adjusted_Rect(iconArea, init_I2(gap_UI, 0), zero_I2()),
                                  iTrue,
                                  isSel      ? iconColor
                                  : isUnread ? unreadIconColor
                                             : readIconColor,
                                  "%s",
                                  cstr_String(&str));
                deinit_String(&str);
            }
        }
    }
    else if (sidebar->mode == gempub_SidebarMode) {
        /* Title and author on the left, reading progress and size on the right. */
        const int iconPad = feedIconPad_SidebarItem_;
        const int h1      = lineHeight_Text(uiLabel_FontId);
        const int h2      = lineHeight_Text(font);
        const int metaFg  = isSel ? uiTextSelectedDim_ColorId : uiSubheading_ColorId;
        const iBool isDownloaded = isLocalFileScheme_Rangecc(urlScheme_String(&d->url));
        const int labelFg = isSel     ? uiTextSelected_ColorId
                            : isHover ? uiTextFramelessHover_ColorId
                                      : uiText_ColorId;
        const iRect iconArea = { pos, init_I2(iconPad * aspect_UI, itemHeight) };
        iString str;
        initUnicodeN_String(&str, &d->icon, 1);
        drawCentered_Text(uiLabelLarge_FontId,
                          adjusted_Rect(iconArea, init_I2(gap_UI, 0), zero_I2()),
                          iTrue,
                          iconColor,
                          "%s",
                          cstr_String(&str));
        deinit_String(&str);
        iString *progress = collectNew_String();
        if (d->progress >= 0.0f) {
            format_String(progress, "%d%%", (int) (d->progress * 100.0f + 0.5f));
        }
        iString *size = collectNew_String();
        if (isDownloaded) {
            if (d->size < 100000) {
                format_String(size, "%.0f ${kb}", d->size / 1.0e3);
            }
            else {
                format_String(size, "%.1f ${mb}", d->size / 1.0e6);
            }
            translate_Lang(size);
        }
        /* Aligned like the meta icons of bookmarks. */
        const int rightEdge = right_Rect(itemRect) - 2 * gap_UI -
                              (blankWidth ? blankWidth - 1.5f * gap_UI : (gap_UI / 2));
        const int colWidth  = iMax(measureRange_Text(uiLabel_FontId, range_String(progress)).advance.x,
                                   measureRange_Text(uiLabel_FontId, range_String(size)).advance.x);
        const int top = pos.y + (itemHeight - h1 - h2) / 2;
        pos.x += iconPad * aspect_UI;
        const int avail = rightEdge - colWidth - 2 * gap_UI * aspect_UI - pos.x;
        const char *endPos;
        tryAdvanceNoWrap_Text(font, range_String(&d->label), avail, &endPos);
        drawRange_Text(font,
                       init_I2(pos.x, top),
                       labelFg,
                       (iRangecc){ constBegin_String(&d->label), endPos });
        tryAdvanceNoWrap_Text(uiLabel_FontId, range_String(&d->meta), avail, &endPos);
        drawRange_Text(uiLabel_FontId,
                       init_I2(pos.x, top + h2),
                       metaFg,
                       (iRangecc){ constBegin_String(&d->meta), endPos });
        drawAlign_Text(uiLabel_FontId,
                       init_I2(rightEdge, top + (h2 - h1) / 2),
                       isSel ? uiTextSelected_ColorId : uiText_ColorId,
                       right_Alignment,
                       "%s",
                       cstr_String(progress));
        drawAlign_Text(uiLabel_FontId,
                       init_I2(rightEdge, top + h2),
                       metaFg,
                       right_Alignment,
                       "%s",
                       cstr_String(size));
    }
    else if (sidebar->mode == bookmarks_SidebarMode) {
        const iBool isFolder = d->listItem.flags.isDropTarget;
        const int   fg = isSel ? uiTextSelected_ColorId
                         : isHover && (!isFolder || isDark_ColorTheme(prefs_App()->theme))
                             ? uiTextFramelessHover_ColorId
                         : isFolder ? uiHeading_ColorId
                                    : uiText_ColorId;
        /* The icon. */
        iString str;
        init_String(&str);
        appendChar_String(&str, d->icon ? d->icon : 0x1f588);
        const int   leftIndent = d->indent * gap_UI * 4 * aspect_UI;
        const iRect iconArea   = { addX_I2(pos, aspect_UI * gap_UI + leftIndent),
                                   init_I2(!isTerminal_Platform() ? 1.75f * lineHeight_Text(font) : 5,
                                           itemHeight) };
        drawCentered_Text(font,
                          iconArea,
                          iTrue,
                          isSel                            ? iconColor
                          : d->icon == 0x2913 /* remote */ ? uiTextCaution_ColorId
                                                           : iconColor,
                          "%s",
                          cstr_String(&str));
        deinit_String(&str);
        const iInt2 textPos =
            addY_I2(topRight_Rect(iconArea), (itemHeight - lineHeight_Text(font)) / 2);
        drawRange_Text(font,
                       textPos,
                       fg,
                       range_String(sidebar->isShowingUrls && !isEmpty_String(&d->url) ? &d->url
                                                                                       : &d->label));
        const int metaFont      = uiLabel_FontId;
        const int metaIconWidth = 4.5f * gap_UI * aspect_UI;
        if (isEditing) {
            drawDragHandle_(p, &itemRect, bg);
        }
        const iInt2 metaPos =
            init_I2(right_Rect(itemRect) - length_String(&d->meta) * metaIconWidth - 2 * gap_UI -
                        (blankWidth ? blankWidth - 1.5f * gap_UI : (gap_UI / 2)),
                    textPos.y);
        if (!isDragging) {
            fillRect_Paint(p,
                           init_Rect(metaPos.x,
                                     top_Rect(itemRect),
                                     right_Rect(itemRect) - metaPos.x,
                                     height_Rect(itemRect)),
                           bg);
        }
        iInt2                mpos = metaPos;
        iStringConstIterator iter;
        init_StringConstIterator(&iter, &d->meta);
        iRangecc range = { cstr_String(&d->meta), iter.pos };
        while (iter.value) {
            next_StringConstIterator(&iter);
            range.end       = iter.pos;
            iRect iconArea  = { mpos, init_I2(metaIconWidth, lineHeight_Text(metaFont)) };
            iRect visBounds = visualBounds_Text(metaFont, range);
            drawRange_Text(metaFont,
                           sub_I2(mid_Rect(iconArea), mid_Rect(visBounds)),
                           isSel ? fg : uiTextShortcut_ColorId,
                           range);
            mpos.x += metaIconWidth;
            range.start = range.end;
        }
    }
    else if (sidebar->mode == history_SidebarMode) {
        iBeginCollect();
        if (d->listItem.flags.isSeparator) {
            if (!isEmpty_String(&d->meta)) {
                iInt2 drawPos = addY_I2(topLeft_Rect(itemRect), d->id);
                drawHLine_Paint(p,
                                addY_I2(drawPos, -gap_UI * aspect_UI),
                                width_Rect(itemRect) - blankWidth,
                                uiSeparator_ColorId);
                drawRange_Text(
                    uiLabelLargeBold_FontId,
                    add_I2(drawPos,
                           init_I2(3 * gap_UI * aspect_UI,
                                   1 + (itemHeight - lineHeight_Text(uiLabelLargeBold_FontId)) /
                                           2.0f)),
                    uiIcon_ColorId,
                    range_String(&d->meta));
            }
        }
        else {
            const int fg = isSel     ? uiTextSelected_ColorId
                           : isHover ? uiTextFramelessHover_ColorId
                                     : uiTextDim_ColorId;
            iUrl      parts;
            init_Url(&parts, &d->label);
            const iBool isAbout    = equalCase_Rangecc(parts.scheme, "about");
            const iBool isGemini   = equalCase_Rangecc(parts.scheme, "gemini");
            const iBool isData     = equalCase_Rangecc(parts.scheme, "data");
            const int   queryColor = isSel     ? uiTextSelectedDim_ColorId
                                     : isHover ? uiText_ColorId
                                               : uiAnnotation_ColorId;
            const iInt2 textPos =
                add_I2(topLeft_Rect(itemRect),
                       init_I2(3 * gap_UI, (itemHeight - lineHeight_Text(font)) / 2));
            if (isData) {
                drawRange_Text(
                    font, textPos, fg, range_String(prettyDataUrl_String(&d->label, queryColor)));
            }
            else {
                draw_Text(font,
                          textPos,
                          fg,
                          "%s%s%s%s%s%s%s%s",
                          isGemini ? "" : cstr_Rangecc(parts.scheme),
                          isGemini  ? ""
                          : isAbout ? ":"
                                    : "://",
                          escape_Color(isSel     ? uiTextSelected_ColorId
                                       : isHover ? uiTextFramelessHover_ColorId
                                                 : uiTextStrong_ColorId),
                          cstr_Rangecc(parts.host),
                          escape_Color(fg),
                          cstr_Rangecc(parts.path),
                          !isEmpty_Range(&parts.query) ? escape_Color(queryColor) : "",
                          !isEmpty_Range(&parts.query) ? cstr_Rangecc(parts.query) : "");
            }
        }
        iEndCollect();
    }
    else if (sidebar->mode == siteStructure_SidebarMode) {
        const iBool isUnfolded = (d->id & 2) != 0;
        const int fg = isSel     ? uiTextSelected_ColorId
                       : isHover ? uiTextFramelessHover_ColorId
                       : d->indent == 0 || d->isBold || isUnfolded ? uiTextStrong_ColorId
                       : d->count > 0
                           ? (d->indent <= 2 ? uiTextStrong_ColorId : uiTextAction_ColorId)
                           : uiTextDim_ColorId;
        const int fg2 = isSel        ? uiTextSelected_ColorId
                        : isHover    ? uiTextFramelessHover_ColorId
                                     : uiAnnotation_ColorId;
        const int fg3 = isSel        ? uiTextSelected_ColorId
                        : isHover    ? isUnfolded ? uiText_ColorId : uiAnnotation_ColorId
                        : isUnfolded ? uiText_ColorId
                                     : uiAnnotation_ColorId;
        const iInt2 pos =
            add_I2(topLeft_Rect(itemRect),
                   init_I2(3 * gap_UI * aspect_UI + d->indent * 5 * gap_UI * aspect_UI,
                           (itemHeight - lineHeight_Text(font)) / 2));
        const int span = measureRange_Text(font, range_String(&d->label)).advance.x;
        drawRange_Text(font, pos, fg, range_String(&d->label));
        if (d->indent > 0) {
            if (isUnfolded || d->count > 0) {
                drawCenteredRange_Text(
                    font,
                    initCorners_Rect(
                        addX_I2(pos,
                                gap_UI * (isSlidingSheet_SidebarWidget_(sidebar) ? -7 : -6) *
                                    aspect_UI),
                        init_I2(pos.x, pos.y + lineHeight_Text(font))),
                    iTrue,
                    fg3,
                    range_CStr(isUnfolded ? downAngle_Icon : rightAngle_Icon));
            }
            if (d->count > 0) {
                draw_Text(uiLabel_FontId,
                          add_I2(pos,
                                 init_I2(span + gap_UI * aspect_UI,
                                         ascent_Text(font) - ascent_Text(uiLabel_FontId))),
                          fg2,
                          " (%d)",
                          d->count);
            }
        }
    }
    else if (sidebar->mode == openDocuments_SidebarMode) {
        const int fg = isSel       ? uiTextSelected_ColorId
                       : d->isBold ? uiTextStrong_ColorId /* unseen */
                       : isHover   ? uiTextFramelessHover_ColorId
                                   : uiText_ColorId;
        const iInt2 textPos = add_I2(topLeft_Rect(itemRect),
                                     init_I2(3 * gap_UI, (itemHeight - lineHeight_Text(font)) / 2));
        iString     label;
        init_String(&label);
        set_String(&label, &d->label);
        const iRangecc host = urlHost_String(&d->url);
        appendFormat_String(&label,
                            " %s%s%s",
                            escape_Color(isSel       ? uiTextSelected_ColorId
                                         : isHover   ? uiTextFramelessHover_ColorId
                                         : d->isBold ? uiAnnotation_ColorId
                                                     : uiTextShortcut_ColorId),
                            !isEmpty_Range(&host) ? "\u2014 " : "",
                            cstr_Rangecc(host));
        drawRange_Text(font, textPos, fg, range_String(&label));
        deinit_String(&label);
        if (isMobile_Platform()) {
            drawDragHandle_(p, &itemRect, bg);
        }
        /* Status icons appear on the right. */
        const int metaIconWidth = 7.0f * gap_UI * aspect_UI + blankWidth;
        iInt2     metaIconPos =
            init_I2(right_Rect(itemRect) - metaIconWidth + 0.5f * gap_UI * aspect_UI, textPos.y);
        iRect metaIconRect = initCorners_Rect(addX_I2(topRight_Rect(itemRect), -metaIconWidth),
                                              bottomRight_Rect(itemRect));
        if (d->id & 8) { /* Reader tab */
            const int offset = (d->id & 7 ? -metaIconWidth : 0);
            fillRect_Paint(p, moved_Rect(metaIconRect, init_I2(offset, 0)), bg);
            drawRange_Text(font,
                           addX_I2(metaIconPos, offset),
                           isSel ? uiTextSelected_ColorId : uiIcon_ColorId,
                           range_CStr(star_Icon));
        }
        if (d->id & 7) { /* used for status flags */
            const int statusFg = isSel ? uiTextSelected_ColorId : uiTextAction_ColorId;
            fillRect_Paint(p, metaIconRect, bg);
            if (d->id & 4) {
                draw_Text(font, metaIconPos, statusFg, timer_Icon);
            }
            else {
                drawRange_Text(
                    font,
                    metaIconPos,
                    statusFg,
                    range_CStr(
                        d->id & 2 ? "\U0001f50a" /* audio speaker, high volume */ : reload_Icon));
            }
        }
    }
    else if (sidebar->mode == subscriptions_SidebarMode) {
        const int fg1   = isSel       ? uiTextSelected_ColorId
                          : d->isBold ? uiTextStrong_ColorId
                                      : uiText_ColorId;
        const int fg2   = isSel ? uiTextSelectedDim_ColorId : uiText_ColorId;
        const int fg3   = isSel       ? uiTextSelected_ColorId
                          : d->isBold ? uiIcon_ColorId
                                      : uiTextDim_ColorId;
        const int font2 = uiLabel_FontId;
        iString   str;
        init_String(&str);
        appendChar_String(&str, d->icon ? d->icon : 0x1f588);
        iInt2 pos = add_I2(
            topLeft_Rect(itemRect),
            init_I2(10 * gap_UI * aspect_UI,
                    (height_Rect(itemRect) - lineHeight_Text(font) - lineHeight_Text(font2)) / 2));
        const iRect iconRect =
            initCorners_Rect(init_I2(left_Rect(itemRect) + gap_UI * aspect_UI, pos.y),
                             init_I2(pos.x, pos.y + lineHeight_Text(font)));
        drawCenteredRange_Text(font, iconRect, iTrue, fg3, range_String(&str));
        clear_String(&str);
        if (d->count > 0) {
            setCStr_String(&str, formatCStr_Lang("num.entries.n", d->count));
        }
        append_String(&str, &d->meta);
        drawRange_Text(font, pos, fg1, range_String(&d->label));
        pos.y += lineHeight_Text(font);
        drawRange_Text(font2, pos, fg2, range_String(&str));
        deinit_String(&str);
    }
    if (isListFocus && isHoverItem && constCursorItem_ListWidget(list) == d && !isTerminal_Platform()) {
        /* Visualize the keyboard cursor. */
        drawRect_Paint(p, shrunk_Rect(itemRect, one_I2()), uiTextAction_ColorId);
    }
}

