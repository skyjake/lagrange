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
#include "banner.h"
#include "defs.h"
#include "media/media.h"
#include "pagedview.h"
#include "render/paint.h"
#include "render/text.h"
#include "ui/mediaui.h"
#include "ui/root.h"
#include "ui/util.h"
#include "ui/window.h"

#include <lagrange/gmrequest.h>

#include <the_Foundation/regexp.h>
#include <SDL3/SDL_timer.h>
#include <SDL3/SDL_render.h>
#include <math.h>

static iBool hostIsBlank_DocumentView_(const iDocumentView *d) {
    return d->host && d->host->isBlank ? d->host->isBlank(d->owner) : iFalse;
}

static iBool hostIsPrerenderingAllowed_DocumentView_(const iDocumentView *d) {
    return d->host && d->host->isPrerenderingAllowed ? d->host->isPrerenderingAllowed(d->owner)
                                                     : iTrue;
}

static iBool hostIsShowingLinkNumbers_DocumentView_(const iDocumentView *d) {
    return d->host && d->host->isShowingLinkNumbers ? d->host->isShowingLinkNumbers(d->owner)
                                                    : iFalse;
}

static size_t hostOrdinalBase_DocumentView_(const iDocumentView *d) {
    return d->host && d->host->ordinalBase ? d->host->ordinalBase(d->owner) : 0;
}

static iChar hostLinkOrdinalChar_DocumentView_(const iDocumentView *d, size_t ord) {
    return d->host && d->host->linkOrdinalChar ? d->host->linkOrdinalChar(d->owner, ord) : 0;
}

static iRangecc hostSelectionMark_DocumentView_(const iDocumentView *d) {
    return d->host && d->host->selectionMark ? d->host->selectionMark(d->owner) : iNullRange;
}

static iTime hostSourceTime_DocumentView_(const iDocumentView *d) {
    if (d->host && d->host->sourceTime) {
        return d->host->sourceTime(d->owner);
    }
    iTime none;
    iZap(none);
    return none;
}

static iMediaRequest *hostFindMediaRequest_DocumentView_(const iDocumentView *d, iGmLinkId linkId) {
    return d->host && d->host->findMediaRequest ? d->host->findMediaRequest(d->owner, linkId) : NULL;
}

static iString *timeFormatHourPreference_Lang(const char *formatMsgId) {
    iString *str = newCStr_String(cstr_Lang(formatMsgId));
    translate_Lang(str);
    if (prefs_App()->time24h) {
        replace_String(str, "%I", "%H");
        replace_String(str, " %p", "");
        replace_String(str, "%p", "");
    }
    else {
        replace_String(str, "%H:%M", "%I:%M %p");
    }
    return str;
}

static void updateTimestampBuf_DocumentView_(const iDocumentView *d) {
    if (!isExposed_Window(get_Window())) {
        return;
    }
    if (d->drawBufs->timestampBuf) {
        delete_TextBuf(d->drawBufs->timestampBuf);
        d->drawBufs->timestampBuf = NULL;
    }
    iTime sourceTime = hostSourceTime_DocumentView_(d);
    if (isValid_Time(&sourceTime)) {
        iString *fmt = timeFormatHourPreference_Lang("page.timestamp");
        replace_String(fmt, "\n", " "); /* TODO: update original lang strings */
        d->drawBufs->timestampBuf = newRange_TextBuf(
            uiLabel_FontId,
            white_ColorId,
            range_String(collect_String(format_Time(&sourceTime, cstr_String(fmt)))));
        delete_String(fmt);
    }
    d->drawBufs->flags &= ~updateTimestampBuf_DrawBufsFlag;
}

iDeclareType(DrawContext)
iDeclareType(MarkStyle)

struct Impl_MarkStyle {
    int           color;
    int           alpha;
    SDL_BlendMode blend;
};

static iMarkStyle markStyle_(int color, SDL_BlendMode blend) {
    const iColor bg   = get_Color(tmBackground_ColorId);
    const iColor mark = get_Color(color);
    const iColor marked =
        blend == SDL_BLENDMODE_ADD
            ? (iColor){ iMin(255, bg.r + mark.r * mark.a / 255),
                        iMin(255, bg.g + mark.g * mark.a / 255),
                        iMin(255, bg.b + mark.b * mark.a / 255),
                        255 }
            : mix_Color(bg, mark, mark.a / 255.0f);
    if (perceptualDelta_Color(bg, marked) >= 64) {
        return (iMarkStyle){ color, 255, blend };
    }
    /* Not enough contrast against the background. */
    return (iMarkStyle){ tmParagraph_ColorId, mark.a / 2, SDL_BLENDMODE_BLEND };
}

struct Impl_DrawContext {
    const iDocumentView *view;
    iRect       widgetBounds;
    int         widgetFullWidth; /* including area behind scrollbar */
    iRect       docBounds;
    iRangei     vis;
    iInt2       viewPos; /* document area origin */
    iPaint      paint;
    iBool       inSelectMark;
    iBool       inFoundMark;
    iMarkStyle  selectMarkStyle;
    iMarkStyle  foundMarkStyle;
    iBool       showLinkNumbers;
    iRect       firstMarkRect;
    iRect       lastMarkRect;
    int         drawDir; /* -1 for progressive reverse direction */
    iGmRunRange runsDrawn;
};

static int measureAdvanceToLoc_(const iGmRun *run, const char *end) {
    iWrapText wt = { .text     = run->text,
                     .mode     = anyCharacter_WrapTextMode,
                     .maxWidth = isJustified_GmRun(run) ? iAbsi(drawBoundWidth_GmRun(run)) : 0,
                     .justify  = isJustified_GmRun(run),
                     .hitChar  = end };
    measure_WrapText(&wt, run->font);
    return wt.hitAdvance_out.x;
}

static int linkNumberFont_DrawContext_(const iDrawContext *d) {
    const enum iFontSize fontSize = size_FontId(font_GmDocument(d->view->doc, link_GmLineType));
    return FONT_ID(default_FontId,
                   regular_FontStyle,
                   fontSize == contentRegular_FontSize ? contentSmall_FontSize
                                                       : contentTiny_FontSize);
}

static iRect linkNumberArea_DrawContext_(const iDrawContext *d, iRect visRect, iRect *frame_out) {
    /* Numbers are always in the left margin, even if the link icon is elsewhere.
       Returns the full area to erase. */
    iRect area = { init_I2(d->viewPos.x - gap_UI / 3, top_Rect(visRect)),
                   init_I2(3.95f * gap_Text, height_Rect(visRect)) };
    if (isTerminal_Platform()) {
        area.pos.x += 1;
        if (frame_out) {
            *frame_out = area;
        }
        return area;
    }
    const iRect frame = initCentered_Rect(
        mid_Rect(area), init1_I2(lineHeight_Text(linkNumberFont_DrawContext_(d)) + gap_UI / 2));
    if (frame_out) {
        *frame_out = frame;
    }
    return union_Rect(area, frame);
}

static void fillMark_DrawContext_(iDrawContext *d, iRect rect, const iMarkStyle *style) {
    const int oldAlpha = d->paint.alpha;
    SDL_SetRenderDrawBlendMode(d->paint.dst->render, style->blend);
    d->paint.alpha = style->alpha;
    fillRect_Paint(&d->paint, rect, style->color);
    d->paint.alpha = oldAlpha;
}

static void fillRange_DrawContext_(iDrawContext *d, const iGmRun *run, const iMarkStyle *style,
                                   iRangecc mark, iBool *isInside) {
    if (mark.start > mark.end) {
        /* Selection may be done in either direction. */
        iSwap(const char *, mark.start, mark.end);
    }
    if (*isInside || (contains_Range(&run->text, mark.start) ||
                      contains_Range(&mark, run->text.start))) {
        int x = 0;
        if (!*isInside) {
            x = measureAdvanceToLoc_(run, iMax(run->text.start, mark.start));
        }
        const int boundWidth = iAbsi(drawBoundWidth_GmRun(run));
        int w = boundWidth - x;
        if (contains_Range(&run->text, mark.end) || mark.end < run->text.start) {
            iRangecc mk =
                !*isInside ? mark : (iRangecc){ run->text.start, iMax(run->text.start, mark.end) };
            mk.start  = iMax(mk.start, run->text.start);
            int x1    = measureAdvanceToLoc_(run, mk.start);
            w         = measureAdvanceToLoc_(run, mk.end) - x1;
            *isInside = iFalse;
        }
        else {
            *isInside = iTrue; /* at least until the next run */
        }
        if (w > boundWidth - x) {
            w = boundWidth - x;
        }
        if (~run->flags & decoration_GmRunFlag) {
            const iInt2 visPos = init_I2(left_Rect(run->visBounds) + d->viewPos.x,
                                         top_Rect(run->bounds) + d->viewPos.y);
            const iRect rangeRect = { addX_I2(visPos, x), init_I2(w, height_Rect(run->bounds)) };
            if (rangeRect.size.x) {
                fillMark_DrawContext_(d, rangeRect, style);
                /* Keep track of the first and last marked rects. */
                if (d->firstMarkRect.size.x == 0) {
                    d->firstMarkRect = rangeRect;
                }
                d->lastMarkRect = rangeRect;
            }
        }
    }
    /* Link URLs are not part of the visible document, so they are ignored above. Handle
       these ranges as a special case. */
    if (run->linkId && run->flags & decoration_GmRunFlag) {
        const iRangecc url = linkUrlRange_GmDocument(d->view->doc, run->linkId);
        if (contains_Range(&url, mark.start) &&
            (contains_Range(&url, mark.end) || url.end == mark.end)) {
            fillMark_DrawContext_(d, moved_Rect(run->visBounds, d->viewPos), style);
        }
    }
}

static void drawMark_DrawContext_(void *context, const iGmRun *run) {
    iDrawContext *d = context;
    if (!isMedia_GmRun(run)) {
        fillRange_DrawContext_(d, run, &d->foundMarkStyle, *d->view->foundMark, &d->inFoundMark);
        fillRange_DrawContext_(d, run, &d->selectMarkStyle, *d->view->selectMark, &d->inSelectMark);
    }
}

static void drawRun_DrawContext_(void *context, const iGmRun *run) {
    iDrawContext *d      = context;
    const iInt2   origin = d->viewPos;
    /* Keep track of the drawn visible runs. */ {
        if (!d->runsDrawn.start || run < d->runsDrawn.start) {
            d->runsDrawn.start = run;
        }
        if (!d->runsDrawn.end || run > d->runsDrawn.end) {
            d->runsDrawn.end = run;
        }
    }
    if (run->mediaType == image_MediaType) {
        iMedia      *media = media_GmDocument(d->view->doc);
        SDL_Texture *tex   = imageTexture_Media(media, mediaId_GmRun(run));
        const iRect  dst   = moved_Rect(run->visBounds, origin);
        if (tex) {
            fillRect_Paint(&d->paint, dst, tmBackground_ColorId); /* in case the image has alpha */
            SDL_RenderTexture(d->paint.dst->render, tex, NULL,
                        &(SDL_FRect){ dst.pos.x, dst.pos.y, dst.size.x, dst.size.y });
            return;
        }
        else if (imageFailed_Media(media, mediaId_GmRun(run))) {
            drawRect_Paint(&d->paint, dst, tmQuoteIcon_ColorId);
            drawCentered_Text(uiLabel_FontId,
                                dst,
                                iFalse,
                                tmQuote_ColorId,
                                explosion_Icon "  Error Loading Image");
            return;
        }
    }
    else if (isMedia_GmRun(run)) {
        /* Media UIs are drawn afterwards as a dynamic overlay. */
        return;
    }
    enum iColorId      fg        = run->color;
    const iGmDocument *doc       = d->view->doc;
    const int          linkFlags = linkFlags_GmDocument(doc, run->linkId);
    /* Hover state of a link. */
    const iBool isPartOfHover = (run->linkId && d->view->hoverLink &&
                                 run->linkId == d->view->hoverLink->linkId &&
                                 ~linkFlags & content_GmLinkFlag);
    iBool isHover = (isPartOfHover && ~run->flags & decoration_GmRunFlag);
    /* Visible (scrolled) position of the run. */
    const iInt2 visPos = addX_I2(add_I2(run->visBounds.pos, origin),
                                 /* Preformatted runs can be scrolled. */
                                 runOffset_DocumentView(d->view, run));
    const iRect visRect = { visPos, run->visBounds.size };
    /* Fill the background. */ {
        iBool isMobileHover = deviceType_App() != desktop_AppDeviceType &&
                              (isPartOfHover || contains_PtrSet(d->view->invalidRuns, run)) &&
                              (~run->flags & decoration_GmRunFlag || run->flags & startOfLine_GmRunFlag
                               /* highlight link icon but not image captions */);
        /* While this is consistent, it's a bit excessive to indicate that an inlined image
           is open: the image itself is the indication. */
        const iBool isInlineImageCaption = iFalse;
        if (run->linkId && (linkFlags & isOpen_GmLinkFlag || isInlineImageCaption || isMobileHover)) {
            const int pad = gap_Text;
            int       bg  = tmBackgroundOpenLink_ColorId;
            int       corners = all_RoundCorner;
            iRect     wideRect;
            /* Open links get a highlighted background. */
            if (isMobileHover && !isPartOfHover) {
                bg = tmBackground_ColorId; /* hover ended and was invalidated */
            }
            if (linkFlags & inline_GmLinkFlag) {
                wideRect = visRect;
            }
            else {
                wideRect =
                    (iRect){ init_I2(origin.x - pad, visPos.y),
                             init_I2(d->docBounds.size.x + 2 * pad, height_Rect(run->visBounds)) };
                adjustEdges_Rect(&wideRect,
                                 run->flags & startOfLine_GmRunFlag ? -pad * 3 / 4 : 0, 0,
                                 run->flags & endOfLine_GmRunFlag ? pad * 3 / 4 : 0, 0);
                /* The first line is composed of two runs that may be drawn in either order, so
                   only draw half of the background. */
                if (run->flags & decoration_GmRunFlag) {
                    wideRect.size.x = right_Rect(visRect) - left_Rect(wideRect);
                    corners &= ~(topRight_RoundCorner | bottomRight_RoundCorner);
                }
                else if (run->flags & startOfLine_GmRunFlag) {
                    wideRect.size.x = right_Rect(wideRect) - left_Rect(visRect);
                    wideRect.pos.x  = left_Rect(visRect);
                    corners &= ~(topLeft_RoundCorner | bottomLeft_RoundCorner);
                }
                /* Only the outer corners of the whole link are rounded. */
                if (~run->flags & startOfLine_GmRunFlag) {
                    corners &= ~(topLeft_RoundCorner | topRight_RoundCorner);
                }
                if (~run->flags & endOfLine_GmRunFlag) {
                    corners &= ~(bottomLeft_RoundCorner | bottomRight_RoundCorner);
                }
            }
            if (bg != tmBackground_ColorId) {
                /* Clear first so repeated draws don't accumulate on the antialiased edges. */
                fillRect_Paint(&d->paint, wideRect, tmBackground_ColorId);
                drawRoundRect_Paint(
                    &d->paint, wideRect, corners, 1, bg, none_ColorId, none_ColorId);
            }
            else {
                fillRect_Paint(&d->paint, wideRect, bg);
            }
        }
        else if (run->flags & wide_GmRunFlag) {
            /* Wide runs may move any amount horizontally. */
            iRect wideRect  = visRect;
            wideRect.pos.x  = 0;
            wideRect.size.x = d->widgetFullWidth;
            if (!isTerminal_Platform()) {
                /* Due to adaptive scaling of monospace fonts to fit a non-fractional pixel grid,
                   there may be a slight overdraw on the edges if glyphs extend to their maximum
                   bounds (e.g., box drawing). Ensure that the edges of the preformatted block
                   remain clean. (GmDocument leaves empty padding around blocks.) */
                adjustEdges_Rect(&wideRect,
                                 run->flags & startOfLine_GmRunFlag ? -1 : 0, 0,
                                 run->flags & endOfLine_GmRunFlag ? 1 : 0, 0);
            }
            fillRect_Paint(&d->paint, wideRect, tmBackground_ColorId);
        }
        else {
            /* Normal background for other runs. There are cases when runs get drawn multiple
               times, e.g., at the buffer boundary, and there are slightly overlapping characters
               in monospace blocks. Clearing the background here ensures a cleaner visual
               appearance since only one glyph is visible at any given point. */
            fillRect_Paint(&d->paint, visRect, tmBackground_ColorId);
            if (run->linkId && run->flags & decoration_GmRunFlag &&
                run->flags & startOfLine_GmRunFlag && ~run->flags & caption_GmRunFlag) {
                /* Erase any previously drawn link number. */
                fillRect_Paint(
                    &d->paint, linkNumberArea_DrawContext_(d, visRect, NULL), tmBackground_ColorId);
            }
        }
    }
    if (run->linkId) {
        if (run->flags & decoration_GmRunFlag && run->flags & startOfLine_GmRunFlag) {
            /* Link icon. */
            if (linkFlags & content_GmLinkFlag) {
                fg = linkColor_GmDocument(doc, run->linkId, textHover_GmLinkPart);
            }
        }
        else if (~run->flags & decoration_GmRunFlag) {
            fg = linkColor_GmDocument(doc, run->linkId, isHover ? textHover_GmLinkPart : text_GmLinkPart);
            if (linkFlags & content_GmLinkFlag) {
                fg = linkColor_GmDocument(doc, run->linkId, textHover_GmLinkPart); /* link is inactive */
            }
        }
    }
    if (isAltText_GmRun(run)) {
        const iInt2 margin = preRunMargin_GmDocument(doc, preId_GmRun(run));
        fillRect_Paint(&d->paint, (iRect){ visPos, run->visBounds.size }, tmBackgroundAltText_ColorId);
        drawRect_Paint(&d->paint, (iRect){ visPos, run->visBounds.size }, tmFrameAltText_ColorId);
        drawWrapRange_Text(run->font,
                           add_I2(visPos, margin),
                           run->visBounds.size.x - 2 * margin.x,
                           run->color,
                           run->text);
    }
    else {
        if (d->showLinkNumbers && run->linkId && run->flags & decoration_GmRunFlag &&
            ~run->flags & caption_GmRunFlag) {
            const size_t ord = visibleLinkOrdinal_DocumentView(d->view, run->linkId);
            if (ord >= hostOrdinalBase_DocumentView_(d->view)) {
                const iChar ordChar =
                    hostLinkOrdinalChar_DocumentView_(d->view,
                                                   ord - hostOrdinalBase_DocumentView_(d->view));
                if (ordChar) {
                    iRect frame;
                    linkNumberArea_DrawContext_(d, visRect, &frame);
                    if (!isTerminal_Platform()) {
                        drawRoundRectRadius_Paint(&d->paint,
                                                  frame,
                                                  gap_Text * 3 / 2,
                                                  all_RoundCorner,
                                                  frame_UI,
                                                  none_ColorId,
                                                  tmQuote_ColorId,
                                                  tmQuote_ColorId);
                    }
                    drawCentered_Text(linkNumberFont_DrawContext_(d),
                                      frame,
                                      iTrue,
                                      tmQuote_ColorId,
                                      "%lc",
                                      (int) ordChar);
                    goto runDrawn;
                }
            }
        }
        if (run->flags & ruler_GmRunFlag) {
            if (height_Rect(run->visBounds) > 0) {
                /* This is used for block quotes. */
                drawVLine_Paint(&d->paint,
                                addX_I2(visPos,
                                        !run->isRTL
                                            ? -gap_Text * 5 / 2
                                            : (width_Rect(run->visBounds) + gap_Text * 5 / 2)),
                                height_Rect(run->visBounds),
                                tmQuoteIcon_ColorId);
            }
            else {
                drawHLine_Paint(&d->paint, visPos, width_Rect(run->visBounds), tmQuoteIcon_ColorId);
            }
        }
        /* Base attributes. */ {
            int f, c;
            runBaseAttributes_GmDocument(doc, run, &f, &c);
            setBaseAttributes_Text(f, c);
        }
        /* Fancy date in Gemini feed links. */ {
            if (run->linkId && run->flags & startOfLine_GmRunFlag && ~run->flags & decoration_GmRunFlag) {
                static iRegExp *datePattern_;
                if (!datePattern_) {
                    datePattern_ = new_RegExp("^[12][0-9][0-9][0-9]-[01][0-9]-[0-3][0-9]\\s", 0);
                }
                iRegExpMatch m;
                init_RegExpMatch(&m);
                if (matchRange_RegExp(datePattern_, run->text, &m)) {
                    /* The date uses regular weight and a dimmed color. */
                    iString styled;
                    initRange_String(&styled, run->text);
                    insertData_Block(&styled.chars, 10, "\x1b[0m", 4); /* restore */
                    iBlock buf;
                    init_Block(&buf, 0);
                    appendCStr_Block(&buf, "\x1b[10m"); /* regular font weight */
                    appendCStr_Block(&buf, escape_Color(isHover ? fg : tmLinkFeedEntryDate_ColorId));
                    insertData_Block(&styled.chars, 0, constData_Block(&buf), size_Block(&buf));
                    deinit_Block(&buf);
                    const int oldAnsi = ansiFlags_Text();
                    setAnsiFlags_Text(oldAnsi | allowFontStyle_AnsiFlag);
                    setBaseAttributes_Text(run->font, fg);
                    drawBoundRange_Text(run->font,
                                        visPos,
                                        drawBoundWidth_GmRun(run),
                                        isJustified_GmRun(run),
                                        fg,
                                        range_String(&styled));
                    setAnsiFlags_Text(oldAnsi);
                    deinit_String(&styled);
                    goto runDrawn;
                }
            }
        }
        drawBoundRange_Text(run->font,
                            visPos,
                            drawBoundWidth_GmRun(run),
                            isJustified_GmRun(run),
                            fg,
                            run->text);
    runDrawn:;
        if (isHyphenated_GmRun(run)) {
            /* The hyphen is not part of the source. */
            drawRange_Text(
                run->font, addX_I2(visPos, drawBoundWidth_GmRun(run)), fg, range_CStr("-"));
        }
        setBaseAttributes_Text(-1, -1);
    }
    /* Presentation of links. */
    if (run->linkId && ~run->flags & decoration_GmRunFlag) {
        const int metaFont = paragraph_FontId;
        /* TODO: Show status of an ongoing media request. */
        const int flags = linkFlags;
        const iRect linkRect = moved_Rect(run->visBounds, origin);
        iMediaRequest *mr = NULL;
        /* Show metadata about inline content. */
        if (flags & content_GmLinkFlag && run->flags & endOfLine_GmRunFlag) {
            fg = linkColor_GmDocument(doc, run->linkId, textHover_GmLinkPart);
            iString text;
            init_String(&text);
            const iMediaId linkMedia = findMediaForLink_Media(constMedia_GmDocument(doc),
                                                              run->linkId, none_MediaType);
            iAssert(linkMedia.type != none_MediaType);
            iGmMediaInfo info;
            info_Media(constMedia_GmDocument(doc), linkMedia, &info);
            switch (linkMedia.type) {
            case image_MediaType: {
                /* There's a separate decorative GmRun for the metadata. */
                break;
            }
            case audio_MediaType:
                format_String(&text, "%s", info.type);
                break;
            case download_MediaType:
                format_String(&text, "%s", info.type);
                break;
            default:
                break;
            }
            if (linkMedia.type != download_MediaType && /* can't cancel downloads currently */
                linkMedia.type != image_MediaType &&
                hostFindMediaRequest_DocumentView_(d->view, run->linkId)) {
                appendFormat_String(
                    &text, "  %s" close_Icon, isHover ? escape_Color(tmLinkText_ColorId) : "");
            }
            const iInt2 size = measureRange_Text(metaFont, range_String(&text)).bounds.size;
            if (size.x) {
                const iInt2 topRight = init_I2(right_Rect(run->bounds), top_Rect(run->visBounds));
                fillRect_Paint(
                    &d->paint,
                    (iRect){ add_I2(origin, addX_I2(topRight, -size.x - gap_UI)),
                            addX_I2(size, 2 * gap_UI) },
                    tmBackground_ColorId);
                drawAlign_Text(metaFont,
                               add_I2(topRight, origin),
                               fg,
                               right_Alignment,
                               "%s", cstr_String(&text));
            }
            deinit_String(&text);
        }
        else if ((run->flags & endOfLine_GmRunFlag ||
                  run->mediaType == image_MediaType) && // show download progress for images that are incomplete
                 (mr = hostFindMediaRequest_DocumentView_(d->view, run->linkId)) != NULL) {
            if (!isFinished_GmRequest(mr->req)) {
                fillRect_Paint(&d->paint,
                               (iRect){ topRight_Rect(linkRect),
                                        init_I2(d->widgetFullWidth - right_Rect(linkRect),
                                                lineHeight_Text(metaFont)) },
                               tmBackground_ColorId);
                draw_Text(metaFont,
                          topRight_Rect(linkRect),
                          tmInlineContentMetadata_ColorId,
                          translateCStr_Lang(" \u2014 ${doc.fetching}\u2026 (%.1f ${mb})"),
                          (float) bodySize_GmRequest(mr->req) / 1.0e6f);
            }
        }
    }
    /* Debug. */
    if (0) {
        drawRect_Paint(&d->paint, (iRect){ visPos, run->bounds.size }, green_ColorId);
        drawRect_Paint(&d->paint, (iRect){ visPos, run->visBounds.size },
                       run->linkId ? orange_ColorId : red_ColorId);
    }
}

static int drawSideRect_(iPaint *p, iRect rect) {
    int bg = tmBannerBackground_ColorId;
    int fg = tmBannerIcon_ColorId;
    if (equal_Color(get_Color(bg), get_Color(tmBackground_ColorId))) {
        bg = tmBannerIcon_ColorId;
        fg = tmBannerBackground_ColorId;
    }
    fillRect_Paint(p, rect, bg);
    return fg;
}

static int sideElementAvailWidth_DocumentView_(const iDocumentView *d) {
    return left_Rect(documentBounds_DocumentView(d)) -
           left_Rect(bounds_Widget(constAs_Widget(d->owner))) - 2 * d->pageMargin * gap_UI;
}

iLocalDef int minBannerSize_(void) {
    return iMaxi(lineHeight_Text(banner_FontId) * 2, 5);
}

static iBool isSideHeadingVisible_DocumentView_(const iDocumentView *d) {
    return sideElementAvailWidth_DocumentView_(d) >= minBannerSize_() * 2.25f / aspect_UI;
}

static void updateSideIconBuf_DocumentView_(const iDocumentView *d) {
    if (!isExposed_Window(get_Window())) {
        return;
    }
    iDrawBufs *dbuf = d->drawBufs;
    dbuf->flags &= ~updateSideBuf_DrawBufsFlag;
    if (dbuf->sideIconBuf) {
        SDL_DestroyTexture(dbuf->sideIconBuf);
        dbuf->sideIconBuf = NULL;
    }
    //    const iGmRun *banner = siteBanner_GmDocument(d->doc);
    if (isEmpty_Banner(d->banner)) {
        return;
    }
    const int   margin           = gap_UI * d->pageMargin;
    const int   minBannerSize    = minBannerSize_();
    const iChar icon             = siteIcon_GmDocument(d->doc);
    const int   avail            = sideElementAvailWidth_DocumentView_(d) - margin;
    iBool       isHeadingVisible = isSideHeadingVisible_DocumentView_(d);
    /* Determine the required size. */
    iInt2 bufSize = init_I2(minBannerSize / aspect_UI, minBannerSize);
    const int sideHeadingFont = FONT_ID(documentHeading_FontId, regular_FontStyle, contentBig_FontSize);
    if (isHeadingVisible) {
        const iInt2 headingSize = measureWrapRange_Text(sideHeadingFont, avail,
                                                        currentHeading_DocumentView(d)).bounds.size;
        if (headingSize.x > 0) {
            bufSize.y += gap_Text + headingSize.y;
            bufSize.x = iMax(bufSize.x, headingSize.x);
        }
        else {
            isHeadingVisible = iFalse;
        }
    }
    SDL_Renderer *render = renderer_Window(get_Window());
    dbuf->sideIconBuf = SDL_CreateTexture(render,
                                          SDL_PIXELFORMAT_ARGB8888,
                                          SDL_TEXTUREACCESS_STATIC | SDL_TEXTUREACCESS_TARGET,
                                          bufSize.x, bufSize.y);
    iPaint p;
    init_Paint(&p);
    beginTarget_Paint(&p, dbuf->sideIconBuf);
    const iColor back = get_Color(tmBannerSideTitle_ColorId);
    SDL_SetRenderDrawColor(render, back.r, back.g, back.b, 0); /* better blending of the edge */
    SDL_RenderClear(render);
    const iRect iconRect = { zero_I2(), init_I2(minBannerSize / aspect_UI, minBannerSize) };
    int fg = drawSideRect_(&p, iconRect);
    iString str;
    initUnicodeN_String(&str, &icon, 1);
    drawCentered_Text(banner_FontId, iconRect, iTrue, fg, "%s", cstr_String(&str));
    if (isHeadingVisible) {
        iRangecc    text = currentHeading_DocumentView(d);
        iInt2       pos  = addY_I2(bottomLeft_Rect(iconRect), gap_Text);
        const int   font = sideHeadingFont;
        /* If the heading starts with the same symbol as we have in the icon, there's no
           point in repeating. The icon is always a non-alphabetic symbol like Emoji so
           we aren't cutting any words off here. */
        if (startsWith_Rangecc(text, cstr_String(&str)) &&
            size_Range(&text) > size_String(&str)) {
            text.start += size_String(&str);
            trimStart_Rangecc(&text);
        }
        iTextMetrics metrics = measureWrapRange_Text(font, avail, text);
        int xOff = 0;
        if (width_Rect(metrics.bounds) < width_Rect(iconRect)) {
            /* Very short captions should be centered under the icon. */
            xOff = (width_Rect(iconRect) - width_Rect(metrics.bounds)) / 2;
        }
        drawWrapRange_Text(font, addX_I2(pos, xOff), avail, tmBannerSideTitle_ColorId, text);
    }
    deinit_String(&str);
    endTarget_Paint(&p);
    SDL_SetTextureBlendMode(dbuf->sideIconBuf, SDL_BLENDMODE_BLEND);
}

static int contentBottom_DocumentView_(const iDocumentView *d) {
    return bottom_Rect(bounds_Widget(constAs_Widget(d->owner))) -
           (!prefs_App()->hideToolbarOnScroll ? hostPhoneBarsHeight_DocumentView(d) : 0) +
           d->scrollY.max - pos_SmoothScroll(&d->scrollY);
}

static void drawSideElements_DocumentView_(const iDocumentView *d, int horizOffset) {
    if (size_GmDocument(d->doc).y == 0) {
        return;
    }
    const iWidget *w         = constAs_Widget(d->owner);
    const iRect    bounds    = bounds_Widget(w);
    const iRect    docBounds = documentBounds_DocumentView(d);
    const int      margin    = gap_UI * d->pageMargin;
    float          opacity   = value_Anim(&d->sideOpacity);
    const int      avail     = left_Rect(docBounds) - left_Rect(bounds) - 2 * margin;
    iDrawBufs *    dbuf      = d->drawBufs;
    iPaint         p;
    init_Paint(&p);
    setClip_Paint(&p, boundsWithoutVisualOffset_Widget(w));
    /* Side icon and current heading. */
    if (docPrefs_DocumentView(d)->sideIcon.value && opacity > 0 && dbuf->sideIconBuf) {
        const iInt2 texSize = size_SDLTexture(dbuf->sideIconBuf);
        if (avail > texSize.x) {
            const int minBannerSize = lineHeight_Text(banner_FontId) * 2;
            iInt2 pos = addY_I2(add_I2(topLeft_Rect(bounds), init_I2(margin, 0)),
                                height_Rect(bounds) / 2 - minBannerSize / 2 -
                                    (texSize.y > minBannerSize
                                         ? (gap_Text + lineHeight_Text(heading3_FontId)) / 2
                                         : 0));
            SDL_SetTextureAlphaMod(dbuf->sideIconBuf, 255 * opacity);
            SDL_RenderTexture(renderer_Window(get_Window()),
                           dbuf->sideIconBuf, NULL,
                           &(SDL_FRect){ pos.x + horizOffset, pos.y, texSize.x, texSize.y });
        }
    }
    /* Reception timestamp. On mobile, it's below the footer in the overscroll area. */
    if (dbuf->timestampBuf) {
        draw_TextBuf(
            dbuf->timestampBuf,
            init_I2(horizOffset + mid_Rect(docBounds).x - dbuf->timestampBuf->size.x / 2,
                    contentBottom_DocumentView_(d) +
                        (deviceType_App() != phone_AppDeviceType
                             ? -margin + -dbuf->timestampBuf->size.y : 0)),
            tmQuoteIcon_ColorId);
    }
    unsetClip_Paint(&p);
}

static void drawMedia_DocumentView_(const iDocumentView *d, iPaint *p, const iRangei *columns) {
    iConstForEach(PtrArray, i, &d->visibleMedia) {
        const iGmRun * run = i.ptr;
        if (columns) {
            const int col = (int) columnAt_GmDocument(d->doc, top_Rect(run->visBounds));
            if (col < columns->start || col >= columns->end) {
                continue;
            }
        }
        if (run->mediaType == audio_MediaType) {
            iPlayerUI ui;
            init_PlayerUI(&ui,
                          audioPlayer_Media(media_GmDocument(d->doc), mediaId_GmRun(run)),
                          runRect_DocumentView(d, run));
            draw_PlayerUI(&ui, p);
        }
        else if (run->mediaType == download_MediaType) {
            iDownloadUI ui;
            init_DownloadUI(&ui, constMedia_GmDocument(d->doc), run->mediaId,
                            runRect_DocumentView(d, run));
            draw_DownloadUI(&ui, p);
        }
    }
}

static void extend_GmRunRange_(iGmRunRange *runs) {
    if (runs->start) {
        runs->start--;
        runs->end++;
    }
}

static iRangei drawRange_DocumentView_(const iDocumentView *d, iRangei range) {
    /* Runs may draw slightly outside their visual bounds (e.g., open link backgrounds). */
    const int margin = isPaged_DocumentView(d) ? 0 : gap_Text;
    return (iRangei){ range.start - margin, range.end + margin };
}

static iBool render_DocumentView_(const iDocumentView *d, iDrawContext *ctx, iBool prerenderExtra) {
    iBool       didDraw = iFalse;
    const iRect bounds  = bounds_Widget(constAs_Widget(d->owner));
    const iRect ctxWidgetBounds =
        init_Rect(0,
                  0,
                  width_Rect(bounds) - (hostScrollBar_DocumentView(d)
                                            ? constAs_Widget(hostScrollBar_DocumentView(d))->rect.size.x
                                            : 0),
                  height_Rect(bounds));
    const iRangei full          = { 0, size_GmDocument(d->doc).y };
    const iRangei vis           = ctx->vis;
    iVisBuf      *visBuf        = d->visBuf; /* will be updated now */
    if (isEmpty_Range(&full)) {
        return didDraw;
    }
    d->drawBufs->lastRenderTime = SDL_GetTicks();
    /* A hidden widget is still drawn while it has a visual offset (e.g., sliding out). */
    if (prerenderExtra) {
        allocVisBuffer_DocumentView(d);
    }
    else {
        allocDrawnVisBuffer_DocumentView(d);
    }
    if (!visBuf->buffers[0].texture) {
        return didDraw;
    }
    /* Swap buffers around to have room available both before and after the visible region. */
    if (!isPaged_DocumentView(d)) {
        reposition_VisBuf(visBuf, vis);
    }
    /* Pages are centered in their buffers. */
    const int docLeft = isPaged_DocumentView(d)
                            ? (visBuf->texSize.x - size_GmDocument(d->doc).x) / 2
                            : left_Rect(ctx->docBounds) - left_Rect(bounds);
    /* Redraw the invalid ranges. */
    if (~flags_Widget(d->owner) & destroyPending_WidgetFlag) {
        iPaint *p = &ctx->paint;
        init_Paint(p);
        for (size_t i = 0; i < visBuf->numBuffers; i++) {
            iVisBufTexture *buf         = &visBuf->buffers[i];
            iVisBufMeta    *meta        = buf->user;
            const iRangei   bufRange    = intersect_Rangei(bufferRange_VisBuf(visBuf, i), full);
            const iRangei   bufVisRange = intersect_Rangei(bufRange, vis);
            ctx->widgetBounds = moved_Rect(ctxWidgetBounds, init_I2(0, -buf->origin));
            ctx->viewPos      = init_I2(docLeft, -buf->origin);
            //            printf("  buffer %zu: buf vis range %d...%d\n", i, bufVisRange.start, bufVisRange.end);
            if (!prerenderExtra && !isEmpty_Range(&bufVisRange)) {
                if (isEmpty_Rangei(buf->validRange)) {
                    /* Fill the required currently visible range (vis). */
                    const iRangei bufVisRange = intersect_Rangei(bufRange, vis);
                    if (!isEmpty_Range(&bufVisRange)) {
                        beginTarget_Paint(p, buf->texture);
                        fillRect_Paint(p, (iRect){ zero_I2(), visBuf->texSize }, tmBackground_ColorId);
                        iZap(ctx->runsDrawn);
                        render_GmDocument(d->doc,
                                          drawRange_DocumentView_(d, bufVisRange),
                                          drawRun_DrawContext_,
                                          ctx);
                        meta->runsDrawn = ctx->runsDrawn;
                        extend_GmRunRange_(&meta->runsDrawn);
                        buf->validRange = bufVisRange;
                        //                printf("  buffer %zu valid %d...%d\n", i, bufRange.start, bufRange.end);
                        didDraw = iTrue;
                    }
                }
                else {
                    /* Progressively fill the required runs. */
                    if (meta->runsDrawn.start && buf->validRange.start > bufRange.start) {
                        beginTarget_Paint(p, buf->texture);
                        iZap(ctx->runsDrawn);
                        ctx->drawDir = -1;
                        const iGmRun *newStart = renderProgressive_GmDocument(d->doc,
                                                                              meta->runsDrawn.start,
                                                                              ctx->drawDir,
                                                                              iInvalidSize,
                                                                              drawRange_DocumentView_(d, bufVisRange),
                                                                              drawRun_DrawContext_,
                                                                              ctx);
                        if (ctx->runsDrawn.start) {
                            /* Something was actually drawn, so update the valid range. */
                            const int newTop = top_Rect(ctx->runsDrawn.start->visBounds);
                            if (newTop != buf->validRange.start) {
                                didDraw = iTrue;
                                // printf("render: valid:%d->%d run:%p->%p\n",
                                //        buf->validRange.start, newTop,
                                //        meta->runsDrawn.start,
                                //        ctx->runsDrawn.start); fflush(stdout);
                                buf->validRange.start = newTop;
                            }
                            meta->runsDrawn.start = newStart;
                        }
                    }
                    if (meta->runsDrawn.end) {
                        beginTarget_Paint(p, buf->texture);
                        iZap(ctx->runsDrawn);
                        ctx->drawDir = +1;
                        meta->runsDrawn.end = renderProgressive_GmDocument(d->doc, meta->runsDrawn.end,
                                                                           ctx->drawDir, iInvalidSize,
                                                                           drawRange_DocumentView_(d, bufVisRange),
                                                                           drawRun_DrawContext_,
                                                                           ctx);
                        if (ctx->runsDrawn.start) {
                            didDraw = iTrue;
                        }
                        buf->validRange.end = bufVisRange.end;
                    }
                }
            }
            /* Progressively draw the rest of the buffer if it isn't fully valid. Buffers
               past the end of the document (e.g., pages of the last spread) have nothing. */
            if (prerenderExtra && !isEmpty_Range(&bufRange) &&
                !equal_Rangei(bufRange, buf->validRange)) {
                const iGmRun *next;
                //                printf("%zu: prerenderExtra (start:%p end:%p)\n", i, meta->runsDrawn.start, meta->runsDrawn.end);
                if (meta->runsDrawn.start == NULL) {
                    /* Haven't drawn anything yet in this buffer, so let's try seeding it. */
                    const int rh = lineHeight_Text(paragraph_FontId);
                    const int y = i >= visBuf->numBuffers / 2 ? bufRange.start : (bufRange.end - rh);
                    beginTarget_Paint(p, buf->texture);
                    fillRect_Paint(p, (iRect){ zero_I2(), visBuf->texSize }, tmBackground_ColorId);
                    buf->validRange = (iRangei){ y, y + rh };
                    iZap(ctx->runsDrawn);
                    render_GmDocument(d->doc,
                                      drawRange_DocumentView_(d, buf->validRange),
                                      drawRun_DrawContext_,
                                      ctx);
                    meta->runsDrawn = ctx->runsDrawn;
                    extend_GmRunRange_(&meta->runsDrawn);
                    //                    printf("%zu: seeded, next %p:%p\n", i, meta->runsDrawn.start, meta->runsDrawn.end);
                    didDraw = iTrue;
                }
                else {
                    if (meta->runsDrawn.start) {
                        const iRangei upper = intersect_Rangei(bufRange, (iRangei){ full.start, buf->validRange.start });
                        if (upper.end > upper.start) {
                            beginTarget_Paint(p, buf->texture);
                            ctx->drawDir = -1;
                            next = renderProgressive_GmDocument(d->doc, meta->runsDrawn.start,
                                                                ctx->drawDir, 1,
                                                                drawRange_DocumentView_(d, upper),
                                                                drawRun_DrawContext_,
                                                                ctx);
                            if (next && meta->runsDrawn.start != next) {
                                meta->runsDrawn.start = next;
                                buf->validRange.start = bottom_Rect(next->visBounds);
                                didDraw = iTrue;
                            }
                            else {
                                buf->validRange.start = bufRange.start;
                            }
                        }
                    }
                    if (!didDraw && meta->runsDrawn.end) {
                        const iRangei lower = intersect_Rangei(bufRange, (iRangei){ buf->validRange.end, full.end });
                        if (lower.end > lower.start) {
                            beginTarget_Paint(p, buf->texture);
                            ctx->drawDir = +1;
                            next = renderProgressive_GmDocument(d->doc, meta->runsDrawn.end,
                                                                ctx->drawDir, 1,
                                                                drawRange_DocumentView_(d, lower),
                                                                drawRun_DrawContext_,
                                                                ctx);
                            if (next && meta->runsDrawn.end != next) {
                                meta->runsDrawn.end = next;
                                buf->validRange.end = top_Rect(next->visBounds);
                                didDraw = iTrue;
                            }
                            else {
                                buf->validRange.end = bufRange.end;
                            }
                        }
                    }
                }
            }
            /* Draw any invalidated runs that fall within this buffer. */
            if (!prerenderExtra) {
                const iRangei bufRange = { buf->origin, buf->origin + visBuf->texSize.y };
                /* Clear full-width backgrounds first in case there are any dynamic elements. */ {
                    iConstForEach(PtrSet, r, d->invalidRuns) {
                        const iGmRun *run = *r.value;
                        if (isOverlapping_Rangei(bufRange, ySpan_Rect(run->visBounds))) {
                            beginTarget_Paint(p, buf->texture);
                            fillRect_Paint(p,
                                           moved_Rect(run->visBounds, init_I2(0, -buf->origin)),/*
                                           init_Rect(0,
                                                     run->visBounds.pos.y - buf->origin,
                                                     visBuf->texSize.x,
                                                     run->visBounds.size.y),*/
                                           tmBackground_ColorId);
                        }
                    }
                }
                setAnsiFlags_Text(ansiEscapes_GmDocument(d->doc));
                const iRangei drawRange = drawRange_DocumentView_(d, bufRange);
                iConstForEach(PtrSet, r, d->invalidRuns) {
                    const iGmRun *run = *r.value;
                    if (isOverlapping_Rangei(drawRange, ySpan_Rect(run->visBounds))) {
                        beginTarget_Paint(p, buf->texture);
                        drawRun_DrawContext_(ctx, run);
                    }
                }
                setAnsiFlags_Text(allowAll_AnsiFlag);
            }
            endTarget_Paint(p);
            if (prerenderExtra && didDraw) {
                /* Just a run at a time. */
                break;
            }
        }
        if (!prerenderExtra) {
            clear_PtrSet(d->invalidRuns);
        }
    }
    return didDraw;
}

int overscrollBottom_DocumentView(const iDocumentView *d) {
    const iTextBuf *ts = d->drawBufs->timestampBuf;
    return contentBottom_DocumentView_(d) +
           (ts && deviceType_App() == phone_AppDeviceType ? ts->size.y : 0);
}

uint32_t lastRenderTime_DocumentView(const iDocumentView *d) {
    return d->drawBufs->lastRenderTime;
}

void prerender_DocumentView(iAny *context) {
    //iAssert(isInstance_Object(context, &Class_DocumentWidget));
    iDocumentView *d = context;
    if (current_Root() == NULL) {
        /* The widget has probably been removed from the widget tree, pending destruction.
           Tickers are not cancelled until the widget is actually destroyed. */
        return;
    }
    //const iDocumentWidget *d = context;
    iDrawContext ctx = {
        .view            = d,
        .docBounds       = documentBounds_DocumentView(d),
        .vis             = visibleRange_DocumentView(d),
        .showLinkNumbers = hostIsShowingLinkNumbers_DocumentView_(d),
        .drawDir         = +1,
    };
    //    printf("%u prerendering\n", SDL_GetTicks());
    if (hostIsPrerenderingAllowed_DocumentView_(d)) {
        makePaletteGlobal_GmDocument(d->doc);
        if (render_DocumentView_(d, &ctx, iTrue /* just fill up progressively */)) {
            /* Something was drawn, should check later if there is still more to do. */
            addTicker_App(prerender_DocumentView, d);
        }
    }
}

iBool isCoveringTopSafeArea_DocumentView(const iDocumentView *d) {
    return isMobile_Platform() && ~d->flags & embedded_DocumentViewFlag &&
           prefs_App()->bottomNavBar &&
           (isPortraitPhone_App() || (deviceType_App() == tablet_AppDeviceType &&
                                      prefs_App()->bottomTabBar));
}

static void initMarks_DrawContext_(iDrawContext *d) {
    const SDL_BlendMode blend =
        isDark_ColorTheme(colorTheme_App()) ? SDL_BLENDMODE_ADD : SDL_BLENDMODE_BLEND;
    d->firstMarkRect   = zero_Rect();
    d->lastMarkRect    = zero_Rect();
    d->foundMarkStyle  = markStyle_(uiMatching_ColorId, blend);
    d->selectMarkStyle = markStyle_(uiMarked_ColorId, blend);
}

static void drawTouchSelectionIndicator_DocumentView_(const iDocumentView *d, iPaint *p) {
    const iRect bounds = bounds_Widget(constAs_Widget(d->owner));
    iRect rect = { topLeft_Rect(bounds),
                   init_I2(width_Rect(bounds), lineHeight_Text(uiLabelBold_FontId)) };
    fillRect_Paint(p, rect, uiTextAction_ColorId);
    const iRangecc mark = hostSelectionMark_DocumentView_(d);
    drawCentered_Text(uiLabelBold_FontId,
                      rect,
                      iFalse,
                      uiBackground_ColorId,
                      "%zu bytes selected", /* TODO: i18n */
                      size_Range(&mark));
}

static void fillTopSafeArea_DocumentView_(const iDocumentView *d, iPaint *p, int horizOffset,
                                          int color) {
    if (isCoveringTopSafeArea_DocumentView(d) && topSafeInset_Mobile() > 0) {
        const iRect topSafeArea =
            initCorners_Rect(zero_I2(), topRight_Rect(safeRect_Root(constAs_Widget(d->owner)->root)));
        fillRect_Paint(p, moved_Rect(topSafeArea, init_I2(horizOffset, 0)), color);
    }
}

static void drawPageBands_DocumentView_(const iDocumentView *d, iRangei cols) {
    /* Each page has a header and a footer that span all of its columns. */
    const iGmDocument *doc      = d->doc;
    const int          perPage  = columnsPerPage_GmDocument(doc);
    const int          font     = pageBandFont_GmDocument(doc);
    const size_t       numPages = numPages_GmDocument(doc);
    for (size_t page = cols.start / perPage; page < numPages && (int) (page * perPage) < cols.end;
         page++) {
        const iRect    rect   = pageTextRect_DocumentView(d, page);
        /* In a two-page spread, a book's title is on the left. */
        const iRangecc header = pageHeader_GmDocument(doc, page, numPages_DocumentView(d) == 2);
        const iRangecc footer = pageFooter_GmDocument(doc, page);
        if (!isEmpty_Range(&header)) {
            drawRange_Text(font,
                           init_I2(mid_Rect(rect).x - measureRange_Text(font, header).bounds.size.x / 2,
                                   top_Rect(rect) + pageHeaderTextTop_DocumentView(d)),
                           tmQuoteIcon_ColorId,
                           header);
        }
        if (!isEmpty_Range(&footer)) {
            drawRange_Text(font,
                           init_I2(mid_Rect(rect).x - measureRange_Text(font, footer).bounds.size.x / 2,
                                   top_Rect(rect) + pageFooterTextTop_DocumentView(d)),
                           tmQuoteIcon_ColorId,
                           footer);
        }
    }
}

/*-----------------------------------------------------------------------------------------------*/

static iColor pageShade_DocumentView_(const iDocumentView *d, iBool *isDarkPage_out) {
    /* A darker shade of the page itself. */
    const iBool isDarkPage = isDark_GmDocumentTheme(docTheme_DocPrefs(
        docPrefs_GmDocument(d->doc), isDark_ColorTheme(colorTheme_App())));
    iHSLColor   shade      = get_HSLColor(tmBackground_ColorId);
    shade                  = setLum_HSLColor(shade, shade.lum * (isDarkPage ? 0.55f : 0.82f));
    if (isDarkPage_out) {
        *isDarkPage_out = isDarkPage;
    }
    return rgb_HSLColor(shade);
}

static void drawSpreads_DocumentView_(const iDocumentView *d, iDrawContext *ctx, iRect area,
                                      iRangei cols) {
    const iWidget *w                = constAs_Widget(d->owner);
    const iRect    bounds           = bounds_Widget(w);
    const iRect    viewport         = pageViewport_DocumentView(d);
    const int      pageHeight       = pageHeight_GmDocument(d->doc);
    const int      numCols          = (int) numColumns_GmDocument(d->doc);
    const int      perPage          = columnsPerPage_GmDocument(d->doc);
    const iBool    isTouchSelecting = (flags_Widget(w) & touchDrag_WidgetFlag) != 0;
    const iVisBuf *visBuf           = d->visBuf;
    iPaint        *p                = &ctx->paint;
    /* Spreads have a gap between them during a turn. */
    fillRect_Paint(p, area, uiBackgroundSidebar_ColorId);
    for (int col = cols.start; col < cols.end; col += numColumns_DocumentView(d)) {
        fillRect_Paint(p,
                       init_Rect(columnLeft_DocumentView(d, col), top_Rect(area),
                                 width_Rect(viewport), height_Rect(area)),
                       tmBackground_ColorId);
    }
    for (size_t i = 0; i < visBuf->numBuffers; i++) {
        const int col = visBuf->buffers[i].origin / pageHeight;
        if (col >= cols.start && col < cols.end && col < numCols) {
            /* Adjacent columns may be closer than the buffer width. */
            const iRangei span = columnSpan_DocumentView(d, col);
            setClip_Paint(p, init_Rect(span.start, top_Rect(area), size_Range(&span), height_Rect(area)));
            drawBuffer_VisBuf(visBuf,
                              i,
                              init_I2(columnLeft_DocumentView(d, col) + columnShift_DocumentView(d, col),
                                      top_Rect(viewport)));
            unsetClip_Paint(p);
        }
    }
    drawPageBands_DocumentView_(d, cols);
    /* Text markers. */
    if (!isEmpty_Range(d->foundMark) || !isEmpty_Range(d->selectMark)) {
        initMarks_DrawContext_(ctx);
        for (int col = cols.start; col < cols.end && col < numCols; col++) {
            ctx->viewPos      = columnOrigin_DocumentView(d, col);
            ctx->inSelectMark = iFalse;
            ctx->inFoundMark  = iFalse;
            render_GmDocument(d->doc,
                              (iRangei){ col * pageHeight, (col + 1) * pageHeight },
                              drawMark_DrawContext_,
                              ctx);
        }
        SDL_SetRenderDrawBlendMode(renderer_Window(get_Window()), SDL_BLENDMODE_NONE);
        if (isTouchSelecting) {
            drawPin_Paint(p, ctx->firstMarkRect, 0, tmQuote_ColorId);
            drawPin_Paint(p, ctx->lastMarkRect,  1, tmQuote_ColorId);
        }
    }
    drawMedia_DocumentView_(d, p, &cols);
    /* The banner is at the top of the first page. */
    if (cols.start == 0 && !isEmpty_Banner(d->banner)) {
        setPos_Banner(d->banner,
                      init_I2(mid_Rect(pageTextRect_DocumentView(d, 0)).x - size_GmDocument(d->doc).x / 2,
                              top_Rect(viewport) + pageHeaderHeight_GmDocument(d->doc)));
        draw_Banner(d->banner);
    }
    /* Crease between the pages of a spread, and slight curvature at the outer edges. */
    if (numPages_DocumentView(d) == 2) {
        const iRangei ySpan = ySpan_Rect(viewport);
        iBool         isDarkPage;
        const iColor  shadeColor = pageShade_DocumentView_(d, &isDarkPage);
        const int     shadeAlpha = isDarkPage ? 90 : 110;
        for (int col = cols.start; col < cols.end && col < numCols; col += 2 * perPage) {
            const int crease = columnLeft_DocumentView(d, col + perPage);
            drawSoftCrease_Paint(p, crease, ySpan, width_Rect(bounds) / 20, shadeColor, shadeAlpha);
            /* The two sheets meet at a thin seam. */
            const int seamWidth = iMax(1, gap_UI / 4);
            SDL_SetRenderDrawBlendMode(renderer_Window(get_Window()), SDL_BLENDMODE_BLEND);
            p->alpha = shadeAlpha;
            fillRectColor_Paint(p,
                                init_Rect(crease - seamWidth / 2, ySpan.start, seamWidth,
                                          size_Range(&ySpan)),
                                shadeColor);
            p->alpha = 255;
            SDL_SetRenderDrawBlendMode(renderer_Window(get_Window()), SDL_BLENDMODE_NONE);
        }
    }
}

enum { numCurlStrips_ = 10 };

static const float curlRadius_ = 0.25f; /* relative to page width */

static float curlStart_(float axis, iBool isHinged) {
    return isHinged ? iMax(0.0f, axis) : axis;
}

static float curlEnd_(float axis, iBool isHinged) {
    const float end = axis + iMathPif * curlRadius_;
    return isHinged ? iMax(0.0f, end) : end;
}

static float rolledEdge_(float u, float axis, iBool isHinged) {
    /* Position of a point on a page that is flat up to `axis` and then curves over a cylinder
       until it lies upside down on top of itself. When the axis is past the hinge, the curved
       part starts at the hinge. */
    const float r     = curlRadius_;
    const float start = curlStart_(axis, isHinged);
    const float end   = curlEnd_(axis, isHinged);
    float       x     = iMin(u, start);
    if (u > start) {
        x += r * (sinf((iMin(u, end) - axis) / r) - sinf((start - axis) / r));
    }
    if (u > end) {
        x -= u - end;
    }
    return x;
}

static float rolledExtent_(float axis, iBool isHinged) {
    float extent = rolledEdge_(0.0f, axis, isHinged);
    for (int i = 1; i <= numCurlStrips_; i++) {
        extent = iMax(extent, rolledEdge_((float) i / numCurlStrips_, axis, isHinged));
    }
    return extent;
}

static float edgeProgress_(float axis) {
    return (1.0f - rolledEdge_(1.0f, axis, iTrue)) / 2;
}

static float rollProgress_(float axis, iBool isHinged) {
    /* A hinged page progresses as its edge moves over to the other side. Once the curve is
       past the hinge the edge barely moves, so the lowering of the page counts as well.
       A loose page progresses as it uncovers the page under it. */
    if (!isHinged) {
        return 1.0f - rolledExtent_(axis, iFalse);
    }
    const float edge = edgeProgress_(axis);
    if (axis >= 0.0f) {
        return edge;
    }
    const float atHinge = edgeProgress_(0.0f);
    return atHinge +
           ((edge - atHinge) + (1.0f - atHinge) * -axis / (iMathPif * curlRadius_)) / 2;
}

static float rollAxis_(float t, iBool isHinged) {
    /* A hinged page turns until it lies flat on the other side of the hinge. A loose page
       rolls until it no longer covers anything. */
    float lo = isHinged ? -iMathPif * curlRadius_ : -curlRadius_, hi = 1.0f;
    for (int i = 0; i < 24; i++) {
        const float mid = (lo + hi) / 2;
        if (rollProgress_(mid, isHinged) < t) {
            hi = mid;
        }
        else {
            lo = mid;
        }
    }
    return (lo + hi) / 2;
}

static SDL_Texture *curlBuf_DocumentView_(const iDocumentView *d, int index, iInt2 size) {
    SDL_Texture **buf = &d->drawBufs->curlBufs[index];
    if (*buf && !isEqual_I2(size_SDLTexture(*buf), size)) {
        SDL_DestroyTexture(*buf);
        *buf = NULL;
    }
    if (!*buf) {
        *buf = SDL_CreateTexture(renderer_Window(get_Window()),
                                 SDL_PIXELFORMAT_RGBA8888,
                                 SDL_TEXTUREACCESS_TARGET,
                                 size.x,
                                 size.y);
        SDL_SetTextureBlendMode(*buf, SDL_BLENDMODE_NONE);
        SDL_SetTextureScaleMode(*buf, SDL_SCALEMODE_LINEAR);
    }
    return *buf;
}

static void releaseCurlBufs_DocumentView_(const iDocumentView *d) {
    iForIndices(i, d->drawBufs->curlBufs) {
        if (d->drawBufs->curlBufs[i]) {
            SDL_DestroyTexture(d->drawBufs->curlBufs[i]);
            d->drawBufs->curlBufs[i] = NULL;
        }
    }
}

static SDL_Texture *drawCurlBuf_DocumentView_(const iDocumentView *d, int index,
                                              iDrawContext *ctx, iRangei cols, iRect rect) {
    /* Pages are drawn normally into a buffer to be copied in strips. */
    iPaint      *p   = &ctx->paint;
    SDL_Texture *buf = curlBuf_DocumentView_(d, index, rect.size);
    makePaletteGlobal_GmDocument(ctx->view->doc);
    beginTarget_Paint(p, buf);
    origin_Paint = neg_I2(rect.pos);
    setClip_Paint(p, rect);
    drawSpreads_DocumentView_(ctx->view, ctx, rect, cols);
    unsetClip_Paint(p);
    endTarget_Paint(p);
    return buf;
}

static void drawCurl_DocumentView_(iDrawContext *earlierCtx, iRangei earlier,
                                   iDrawContext *laterCtx, iRangei later, float t, iRect area) {
    /* The last page of the earlier spread rolls to the left over a cylinder, revealing the
       later spread. A facing page is hinged at the crease and its back side is the left page
       of the later spread, while a single page rolls away with a blank back side. The page is
       drawn in vertical strips. The spreads may be in different views. */
    const iDocumentView *d        = earlierCtx->view;
    const iDocumentView *dl       = laterCtx->view;
    iPaint              *p        = &earlierCtx->paint;
    SDL_Renderer        *render   = renderer_Window(get_Window());
    const int            n        = numCurlStrips_;
    const iBool          isHinged = numPages_DocumentView(d) == 2;
    const int            left     = columnLeft_DocumentView(d, earlier.start);
    const int            pageLeft =
        columnLeft_DocumentView(d, earlier.end - columnsPerPage_GmDocument(d->doc));
    const iRect          page     = init_Rect(pageLeft,
                                              top_Rect(area),
                                              left + spreadWidth_DocumentView(d) - pageLeft,
                                              height_Rect(area));
    const float          axis     = rollAxis_(t, isHinged);
    float                edges[numCurlStrips_ + 1];
    float                extent   = pageLeft;
    for (int i = 0; i <= n; i++) {
        edges[i] = pageLeft + width_Rect(page) * rolledEdge_((float) i / n, axis, isHinged);
        extent   = iMax(extent, edges[i]);
    }
    const int fold = (int) extent;
    /* Shadows fade in as the curl begins and out as the page settles. */
    makePaletteGlobal_GmDocument(d->doc);
    iBool         isDarkPage;
    const iColor  shade       = pageShade_DocumentView_(d, &isDarkPage);
    const int     shadeAlpha  = isDarkPage ? 90 : 110;
    const iColor  shadow      = { 0, 0, 0, 255 };
    const int     shadowAlpha = isDarkPage ? 140 : 90;
    const float   fadeIn      = iMin(1.0f, t / 0.15f);
    const float   fade        = fadeIn * iMin(1.0f, (1.0f - t) / 0.15f);
    if (pageLeft > left_Rect(area)) {
        setClip_Paint(p, init_Rect(left_Rect(area), top_Rect(area),
                                   pageLeft - left_Rect(area), height_Rect(area)));
        drawSpreads_DocumentView_(d, earlierCtx, area, earlier);
        unsetClip_Paint(p);
    }
    if (fold < right_Rect(area)) {
        setClip_Paint(p, init_Rect(fold, top_Rect(area), right_Rect(area) - fold,
                                   height_Rect(area)));
        makePaletteGlobal_GmDocument(dl->doc);
        drawSpreads_DocumentView_(dl, laterCtx, area, later);
        makePaletteGlobal_GmDocument(d->doc);
        /* The curling page casts a shadow on the revealed spread. */
        const int   shadowWidth = iMax(width_Rect(page) / 3, 50 * gap_UI);
        const float widthFactor = iClamp(3 * shadowWidth / (250.0f * gap_UI), 0.3f, 1.0f);
        drawSoftEdge_Paint(p,
                           fold,
                           ySpan_Rect(area),
                           shadowWidth,
                           shadow,
                           shadowAlpha * fade * 0.5f * widthFactor);
        unsetClip_Paint(p);
    }
    if (fold <= left) {
        return;
    }
    SDL_Texture *front = drawCurlBuf_DocumentView_(d, 0, earlierCtx, earlier, page);
    SDL_Texture *back  = NULL;
    iRect        backRect = zero_Rect();
    if (isHinged) {
        const int laterLeft = columnLeft_DocumentView(dl, later.start);
        backRect = init_Rect(laterLeft,
                             top_Rect(area),
                             columnLeft_DocumentView(
                                 dl, later.start + columnsPerPage_GmDocument(dl->doc)) -
                                 laterLeft,
                             height_Rect(area));
        back = drawCurlBuf_DocumentView_(d, 1, laterCtx, later, backRect);
    }
    makePaletteGlobal_GmDocument(d->doc);
    setClip_Paint(p, init_Rect(left, top_Rect(area), fold - left, height_Rect(area)));
    const iColor blank = get_Color(tmBackground_ColorId);
    SDL_SetRenderDrawBlendMode(render, SDL_BLENDMODE_NONE);
    SDL_SetRenderDrawColor(render, blank.r, blank.g, blank.b, 255);
    for (int i = 0; i < n; i++) {
        const SDL_FRect dst = { iMin(edges[i], edges[i + 1]) + origin_Paint.x,
                                top_Rect(area) + origin_Paint.y,
                                fabsf(edges[i + 1] - edges[i]),
                                height_Rect(area) };
        if (edges[i + 1] >= edges[i]) {
            const float w = (float) width_Rect(page) / n;
            SDL_RenderTexture(
                render, front, &(SDL_FRect){ i * w, 0, w, height_Rect(area) }, &dst);
        }
        else if (back) {
            /* Lands face up on the left. */
            const float w = (float) width_Rect(backRect) / n;
            SDL_RenderTexture(
                render, back, &(SDL_FRect){ (n - 1 - i) * w, 0, w, height_Rect(area) }, &dst);
        }
        else {
            SDL_RenderFillRect(render, &dst);
        }
    }
    /* The page darkens where it curves over, most where it is the steepest. Past the hinge,
       the steepest visible part of the curve is at the hinge. */
    const float curveStart = curlStart_(axis, isHinged);
    const float curveLeft  = iMin(rolledEdge_(curveStart, axis, isHinged),
                                  rolledEdge_(iMin(1.0f, curlEnd_(axis, isHinged)), axis, isHinged));
    const float steepAngle = (curveStart - axis) / curlRadius_;
    const float steepness  = steepAngle > iMathPif / 2 ? sinf(steepAngle) * sinf(steepAngle)
                                                       : 1.0f;
    drawSoftEdge_Paint(p,
                       fold,
                       ySpan_Rect(area),
                       -iMax(1, fold - (int) (pageLeft + curveLeft * width_Rect(page))),
                       shade,
                       shadeAlpha * fadeIn * steepness);
    /* The edge of the flipped part shadows the page under it. The shadow grows as the edge
       moves away from the fold. A hinged page is lowered to the other side as the curve
       passes the hinge. */
    const int edgeShadowWidth = width_Rect(page) / 6;
    if (edges[n] < edges[n - 1] && edgeShadowWidth > 0) {
        /* Some separation remains toward the end. */
        const float liftAxis = t < 0.9f ? axis : rollAxis_(0.9f, isHinged);
        const float lift     = isHinged && liftAxis < 0.0f ? cosf(liftAxis / curlRadius_ / 2)
                                                           : 1.0f;
        const float grow     = iMin(1.0f, (extent - edges[n]) / edgeShadowWidth);
        drawFalloffShadow_Paint(p,
                                (int) edges[n],
                                ySpan_Rect(area),
                                -iMax(1, (int) (edgeShadowWidth * grow)),
                                shadow,
                                shadowAlpha * fadeIn * 0.3f * lift * grow);
    }
    unsetClip_Paint(p);
}

static void drawPaged_DocumentView_(const iDocumentView *d, iDrawContext *ctx, int horizOffset) {
    const iWidget *w        = constAs_Widget(d->owner);
    iPaint        *p        = &ctx->paint;
    /* In reading mode, the pages extend under the bars. */
    const iRect    area     = union_Rect(bounds_Widget(w), pageViewport_DocumentView(d));
    setClip_Paint(p, area);
    iAssert(isEqual_I2(origin_Paint, zero_I2()));
    origin_Paint = init_I2(horizOffset, 0);
    if (isCurling_DocumentView(d)) {
        const int first = (int) iMin(d->paged.column, d->paged.turnFrom);
        const int n     = numColumns_DocumentView(d);
        drawCurl_DocumentView_(ctx, (iRangei){ first, first + n },
                               ctx, (iRangei){ first + n, first + 2 * n },
                               curlProgress_DocumentView(d), area);
    }
    else {
        releaseCurlBufs_DocumentView_(d);
        drawSpreads_DocumentView_(d, ctx, area, visibleColumns_DocumentView(d));
    }
    origin_Paint = zero_I2();
    unsetClip_Paint(p);
    if (flags_Widget(w) & touchDrag_WidgetFlag) {
        drawTouchSelectionIndicator_DocumentView_(d, p);
    }
}

static void beginDraw_DocumentView_(const iDocumentView *d, iDrawContext *ctx) {
    /* Each document has its own palette, but the drawing routines rely on a global one.
       As we're now drawing a document, ensure that the right palette is in effect.
       Document theme colors can be used elsewhere, too, but first a document's palette
       must be made global. */
    makePaletteGlobal_GmDocument(d->doc);
    if (d->drawBufs->flags & updateTimestampBuf_DrawBufsFlag) {
        updateTimestampBuf_DocumentView_(d);
    }
    if (d->drawBufs->flags & updateSideBuf_DrawBufsFlag) {
        updateSideIconBuf_DocumentView_(d);
    }
    *ctx = (iDrawContext){ .widgetFullWidth = width_Rect(bounds_Widget(constAs_Widget(d->owner))),
                           .view            = d,
                           .docBounds       = documentBounds_DocumentView(d),
                           .vis             = visibleRange_DocumentView(d),
                           .showLinkNumbers = hostIsShowingLinkNumbers_DocumentView_(d),
                           .drawDir         = +1,
                         };
    init_Paint(&ctx->paint);
    if (isPaged_DocumentView(d)) {
        ctx->widgetFullWidth = metrics_GmDocument(d->doc)->canvasWidth;
    }
    render_DocumentView_(d, ctx, iFalse /* just the mandatory parts */);
}

void drawCurlBetween_DocumentView(const iDocumentView *earlier, const iDocumentView *later,
                                  float progress) {
    iAssert(isPaged_DocumentView(earlier) && isPaged_DocumentView(later));
    iDrawContext laterCtx, earlierCtx;
    beginDraw_DocumentView_(later, &laterCtx);
    beginDraw_DocumentView_(earlier, &earlierCtx);
    const iRect area = union_Rect(bounds_Widget(constAs_Widget(earlier->owner)),
                                  pageViewport_DocumentView(earlier));
    iAssert(isEqual_I2(origin_Paint, zero_I2()));
    setClip_Paint(&earlierCtx.paint, area);
    const int earlierCol = (int) currentColumn_DocumentView(earlier);
    const int laterCol   = (int) currentColumn_DocumentView(later);
    drawCurl_DocumentView_(&earlierCtx,
                           (iRangei){ earlierCol, earlierCol + numColumns_DocumentView(earlier) },
                           &laterCtx,
                           (iRangei){ laterCol, laterCol + numColumns_DocumentView(later) },
                           iClamp(progress, 0.0f, 1.0f),
                           area);
    unsetClip_Paint(&earlierCtx.paint);
}

/*-----------------------------------------------------------------------------------------------*/

void draw_DocumentView(const iDocumentView *d, int horizOffset) {
    const iWidget *w          = constAs_Widget(d->owner);
    const iRect    bounds     = bounds_Widget(w);
    const iRect    clipBounds = bounds;
    iDrawContext   ctx;
    beginDraw_DocumentView_(d, &ctx);
    if (isPaged_DocumentView(d)) {
        drawPaged_DocumentView_(d, &ctx, horizOffset);
        return;
    }
    const iRect   docBounds = ctx.docBounds;
    const iRangei vis       = ctx.vis;
    iBanner    *banner           = d->banner;
    int         yTop             = docBounds.pos.y + viewPos_DocumentView(d);
    const iBool isDocEmpty       = size_GmDocument(d->doc).y == 0;
    const iBool isTouchSelecting = (flags_Widget(w) & touchDrag_WidgetFlag) != 0;
    iBool       didDraw          = iFalse;
    if (!isDocEmpty || !isEmpty_Banner(banner)) {
        didDraw = iTrue;
        const int docBgColor = isDocEmpty ? tmBannerBackground_ColorId : tmBackground_ColorId;
        setClip_Paint(&ctx.paint, clipBounds);
        iAssert(isEqual_I2(origin_Paint, zero_I2()));
        origin_Paint = init_I2(horizOffset, 0);
        if (!isDocEmpty) {
            draw_VisBuf(d->visBuf, init_I2(bounds.pos.x, yTop), ySpan_Rect(bounds));
        }
        /* Text markers. */
        if ((!isEmpty_Range(d->foundMark) || !isEmpty_Range(d->selectMark))) {
            SDL_Renderer *render = renderer_Window(get_Window());
            initMarks_DrawContext_(&ctx);
            ctx.viewPos = addY_I2(topLeft_Rect(docBounds), viewPos_DocumentView(d));
            /* Marker starting outside the visible range? */
            if (d->visibleRuns.start) {
                if (!isEmpty_Range(d->selectMark) &&
                    d->selectMark->start < d->visibleRuns.start->text.start &&
                    d->selectMark->end > d->visibleRuns.start->text.start) {
                    ctx.inSelectMark = iTrue;
                }
                if (isEmpty_Range(d->foundMark) &&
                    d->foundMark->start < d->visibleRuns.start->text.start &&
                    d->foundMark->end > d->visibleRuns.start->text.start) {
                    ctx.inFoundMark = iTrue;
                }
            }
            render_GmDocument(d->doc, vis, drawMark_DrawContext_, &ctx);
            SDL_SetRenderDrawBlendMode(render, SDL_BLENDMODE_NONE);
            /* Selection range pins. */
            if (isTouchSelecting) {
                drawPin_Paint(&ctx.paint, ctx.firstMarkRect, 0, tmQuote_ColorId);
                drawPin_Paint(&ctx.paint, ctx.lastMarkRect,  1, tmQuote_ColorId);
            }
        }
        drawMedia_DocumentView_(d, &ctx.paint, NULL);
        /* Fill the top and bottom, in case the document is short. */
        if (yTop > top_Rect(bounds)) {
            fillRect_Paint(&ctx.paint,
                           (iRect){ bounds.pos, init_I2(bounds.size.x, yTop - top_Rect(bounds)) },
                           !isEmpty_Banner(banner) ? tmBannerBackground_ColorId
                                                   : docBgColor);
        }
        /* Banner. */
        if (!isDocEmpty || numItems_Banner(banner) > 0) {
            /* Fill the part between the banner and the top of the document. */
            if (documentTopPad_DocumentView(d) > 0) {
                fillRect_Paint(&ctx.paint,
                               (iRect){ init_I2(left_Rect(bounds),
                                               top_Rect(docBounds) + viewPos_DocumentView(d) -
                                                   documentTopPad_DocumentView(d)),
                                       init_I2(bounds.size.x, documentTopPad_DocumentView(d)) },
                               docBgColor);
            }
            setPos_Banner(banner, addY_I2(topLeft_Rect(docBounds),
                                          floorf(-pos_SmoothScroll(&d->scrollY))));
            draw_Banner(banner);
        }
        const int yBottom = yTop + size_GmDocument(d->doc).y;
        if (yBottom < bottom_Rect(bounds)) {
            fillRect_Paint(&ctx.paint,
                           init_Rect(bounds.pos.x, yBottom, bounds.size.x, bottom_Rect(bounds) - yBottom),
                           !isDocEmpty ? docBgColor : tmBannerBackground_ColorId);
        }
        origin_Paint = zero_I2();
        unsetClip_Paint(&ctx.paint);
        drawSideElements_DocumentView_(d, horizOffset);
        /* Alt text. */
        const float altTextOpacity = value_Anim(&d->altTextOpacity) * 6 - 5;
        if (d->hoverAltPre && altTextOpacity > 0) {
            const iGmPreMeta *meta = preMeta_GmDocument(d->doc, preId_GmRun(d->hoverAltPre));
            if (meta->flags & topLeft_GmPreMetaFlag && ~meta->flags & decoration_GmRunFlag &&
                !isEmpty_Range(&meta->altText)) {
                const int   margin   = 3 * gap_UI / 2;
                const int   altFont  = uiLabel_FontId;
                const int   wrap     = docBounds.size.x - 2 * margin;
                iInt2 pos            = add_I2(add_I2(docBounds.pos, meta->pixelRect.pos),
                                       init_I2(horizOffset, viewPos_DocumentView(d)));
                const iInt2 textSize = measureWrapRange_Text(altFont, wrap, meta->altText).bounds.size;
                pos.y -= textSize.y + gap_UI;
                pos.y               = iMax(pos.y, top_Rect(bounds));
                const iRect altRect = { pos, init_I2(docBounds.size.x, textSize.y) };
                ctx.paint.alpha     = altTextOpacity * 255;
                if (altTextOpacity < 1) {
                    SDL_SetRenderDrawBlendMode(renderer_Window(get_Window()), SDL_BLENDMODE_BLEND);
                }
                fillRect_Paint(&ctx.paint, altRect, tmBackgroundAltText_ColorId);
                drawRect_Paint(&ctx.paint, altRect, tmFrameAltText_ColorId);
                setOpacity_Text(altTextOpacity);
                drawWrapRange_Text(altFont, addX_I2(pos, margin), wrap,
                                   tmQuote_ColorId, meta->altText);
                SDL_SetRenderDrawBlendMode(renderer_Window(get_Window()), SDL_BLENDMODE_NONE);
                setOpacity_Text(1.0f);
            }
        }
        if (isTouchSelecting) {
            drawTouchSelectionIndicator_DocumentView_(d, &ctx.paint);
        }
    }
    else {
        iRect boundsWithOffset = moved_Rect(bounds, init_I2(horizOffset, 0));
        fillRect_Paint(&ctx.paint, intersect_Rect(boundsWithOffset, clipBounds), uiBackground_ColorId);
        if (hostIsBlank_DocumentView_(d)) {
            drawLogo_MainWindow(get_MainWindow(), boundsWithOffset);
        }
    }
    fillTopSafeArea_DocumentView_(d, &ctx.paint, horizOffset,
                                  !didDraw ? uiBackground_ColorId :
                                  !isEmpty_Banner(d->banner) && docBounds.pos.y + viewPos_DocumentView(d) -
                                       documentTopPad_DocumentView(d) > bounds.pos.y ?
                                  tmBannerBackground_ColorId : tmBackground_ColorId);
}
