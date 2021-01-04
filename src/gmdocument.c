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

#include "gmdocument.h"
#include "gmutil.h"
#include "ui/color.h"
#include "ui/text.h"
#include "ui/metrics.h"
#include "ui/window.h"
#include "visited.h"
#include "app.h"

#include <the_Foundation/ptrarray.h>
#include <the_Foundation/regexp.h>

#include <ctype.h>

iDeclareType(GmLink)

struct Impl_GmLink {
    iString url;
    iRangecc urlRange; /* URL in the source */
    iTime when;
    int flags;
};

void init_GmLink(iGmLink *d) {
    init_String(&d->url);
    d->urlRange = iNullRange;
    iZap(d->when);
    d->flags = 0;
}

void deinit_GmLink(iGmLink *d) {
    deinit_String(&d->url);
}

iDefineTypeConstruction(GmLink)

/*----------------------------------------------------------------------------------------------*/

struct Impl_GmDocument {
    iObject object;
    enum iGmDocumentFormat format;
    iString   source;
    iString   url; /* for resolving relative links */
    iString   localHost;
    iInt2     size;
    iArray    layout; /* contents of source, laid out in document space */
    iPtrArray links;
    enum iGmDocumentBanner bannerType;
    iString   bannerText;
    iString   title; /* the first top-level title */
    iArray    headings;
    uint32_t  themeSeed;
    iChar     siteIcon;
    iMedia *  media;
};

iDefineObjectConstruction(GmDocument)

enum iGmLineType {
    text_GmLineType,
    bullet_GmLineType,
    preformatted_GmLineType,
    quote_GmLineType,
    heading1_GmLineType,
    heading2_GmLineType,
    heading3_GmLineType,
    link_GmLineType,
    max_GmLineType,
};

static enum iGmLineType lineType_GmDocument_(const iGmDocument *d, const iRangecc line) {
    if (d->format == plainText_GmDocumentFormat) {
        return text_GmLineType;
    }
    if (isEmpty_Range(&line)) {
        return text_GmLineType;
    }
    if (startsWith_Rangecc(line, "=>")) {
        return link_GmLineType;
    }
    if (startsWith_Rangecc(line, "###")) {
        return heading3_GmLineType;
    }
    if (startsWith_Rangecc(line, "##")) {
        return heading2_GmLineType;
    }
    if (startsWith_Rangecc(line, "#")) {
        return heading1_GmLineType;
    }
    if (startsWith_Rangecc(line, "```")) {
        return preformatted_GmLineType;
    }
    if (*line.start == '>') {
        return quote_GmLineType;
    }
    if (size_Range(&line) >= 2 && line.start[0] == '*' && isspace(line.start[1])) {
        return bullet_GmLineType;
    }
    return text_GmLineType;
}

static void trimLine_Rangecc_(iRangecc *line, enum iGmLineType type) {
    static const unsigned int skip[max_GmLineType] = { 0, 2, 3, 1, 1, 2, 3, 0 };
    line->start += skip[type];
    trim_Rangecc(line);
}

static int lastVisibleRunBottom_GmDocument_(const iGmDocument *d) {
    iReverseConstForEach(Array, i, &d->layout) {
        const iGmRun *run = i.value;
        if (isEmpty_Range(&run->text)) {
            continue;
        }
        return bottom_Rect(run->bounds);
    }
    return 0;
}

static iInt2 measurePreformattedBlock_GmDocument_(const iGmDocument *d, const char *start, int font) {
    const iRangecc content = { start, constEnd_String(&d->source) };
    iRangecc line = iNullRange;
    nextSplit_Rangecc(content, "\n", &line);
    iAssert(startsWith_Rangecc(line, "```"));
    iRangecc preBlock = { line.end + 1, line.end + 1 };
    while (nextSplit_Rangecc(content, "\n", &line)) {
        if (startsWith_Rangecc(line, "```")) {
            break;
        }
        preBlock.end = line.end;
    }
    return measureRange_Text(font, preBlock);
}

static iRangecc addLink_GmDocument_(iGmDocument *d, iRangecc line, iGmLinkId *linkId) {
    static iRegExp *pattern_;
    if (!pattern_) {
        pattern_ = new_RegExp("=>\\s*([^\\s]+)(\\s.*)?", caseInsensitive_RegExpOption);
    }
    iRegExpMatch m;
    init_RegExpMatch(&m);
    if (matchRange_RegExp(pattern_, line, &m)) {
        iGmLink *link = new_GmLink();
        link->urlRange = capturedRange_RegExpMatch(&m, 1);
        setRange_String(&link->url, link->urlRange);
        set_String(&link->url, absoluteUrl_String(&d->url, &link->url));
        /* Check the URL. */ {
            iUrl parts;
            init_Url(&parts, &link->url);
            if (!equalCase_Rangecc(parts.host, cstr_String(&d->localHost))) {
                link->flags |= remote_GmLinkFlag;
            }
            if (startsWithCase_Rangecc(parts.scheme, "gemini")) {
                link->flags |= gemini_GmLinkFlag;
            }
            else if (startsWithCase_Rangecc(parts.scheme, "http")) {
                link->flags |= http_GmLinkFlag;
            }
            else if (equalCase_Rangecc(parts.scheme, "gopher")) {
                link->flags |= gopher_GmLinkFlag;
                if (startsWith_Rangecc(parts.path, "/7")) {
                    link->flags |= query_GmLinkFlag;
                }
            }
            else if (equalCase_Rangecc(parts.scheme, "file")) {
                link->flags |= file_GmLinkFlag;
            }
            else if (equalCase_Rangecc(parts.scheme, "data")) {
                link->flags |= data_GmLinkFlag;
            }
            else if (equalCase_Rangecc(parts.scheme, "about")) {
                link->flags |= about_GmLinkFlag;
            }
            else if (equalCase_Rangecc(parts.scheme, "mailto")) {
                link->flags |= mailto_GmLinkFlag;
            }
            /* Check the file name extension, if present. */
            if (!isEmpty_Range(&parts.path)) {
                iString *path = newRange_String(parts.path);
                if (endsWithCase_String(path, ".gif")  || endsWithCase_String(path, ".jpg") ||
                    endsWithCase_String(path, ".jpeg") || endsWithCase_String(path, ".png") ||
                    endsWithCase_String(path, ".tga")  || endsWithCase_String(path, ".psd") ||
                    endsWithCase_String(path, ".hdr")  || endsWithCase_String(path, ".pic")) {
                    link->flags |= imageFileExtension_GmLinkFlag;
                }
                else if (endsWithCase_String(path, ".mp3") || endsWithCase_String(path, ".wav") ||
                         endsWithCase_String(path, ".mid") || endsWithCase_String(path, ".ogg")) {
                    link->flags |= audioFileExtension_GmLinkFlag;
                }
                delete_String(path);
            }
            /* Check if visited. */
            if (cmpString_String(&link->url, &d->url)) {
                link->when = urlVisitTime_Visited(visited_App(), &link->url);
                if (isValid_Time(&link->when)) {
                    link->flags |= visited_GmLinkFlag;
                }
            }
        }
        pushBack_PtrArray(&d->links, link);
        *linkId = size_PtrArray(&d->links); /* index + 1 */
        iRangecc desc = capturedRange_RegExpMatch(&m, 2);
        trim_Rangecc(&desc);
        if (!isEmpty_Range(&desc)) {
            line = desc; /* Just show the description. */
            link->flags |= humanReadable_GmLinkFlag;
        }
        else {
            line = capturedRange_RegExpMatch(&m, 1); /* Show the URL. */
        }
    }
    return line;
}

static void clearLinks_GmDocument_(iGmDocument *d) {
    iForEach(PtrArray, i, &d->links) {
        delete_GmLink(i.ptr);
    }
    clear_PtrArray(&d->links);
}

static iBool isForcedMonospace_GmDocument_(const iGmDocument *d) {
    const iRangecc scheme = urlScheme_String(&d->url);
    if (equalCase_Rangecc(scheme, "gemini")) {
        return prefs_App()->monospaceGemini;
    }
    if (equalCase_Rangecc(scheme, "gopher")) {
        return prefs_App()->monospaceGopher;
    }
    return iFalse;
}

static void doLayout_GmDocument_(iGmDocument *d) {
    const iBool isMono = isForcedMonospace_GmDocument_(d);
    /* TODO: Collect these parameters into a GmTheme. */
    const int fonts[max_GmLineType] = {
        isMono ? regularMonospace_FontId : paragraph_FontId,
        isMono ? regularMonospace_FontId : paragraph_FontId, /* bullet */
        preformatted_FontId,
        isMono ? regularMonospace_FontId : quote_FontId,
        heading1_FontId,
        heading2_FontId,
        heading3_FontId,
        isMono ? regularMonospace_FontId : regular_FontId,
    };
    static const int colors[max_GmLineType] = {
        tmParagraph_ColorId,
        tmParagraph_ColorId,
        tmPreformatted_ColorId,
        tmQuote_ColorId,
        tmHeading1_ColorId,
        tmHeading2_ColorId,
        tmHeading3_ColorId,
        tmLinkText_ColorId,
    };
    static const int indents[max_GmLineType] = {
        5, 10, 5, 10, 0, 0, 0, 5
    };
    static const float topMargin[max_GmLineType] = {
        0.0f, 0.333f, 1.0f, 0.5f, 2.0f, 1.5f, 1.0f, 0.5f
    };
    static const float bottomMargin[max_GmLineType] = {
        0.0f, 0.333f, 1.0f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f
    };
    static const char *arrow           = "\u27a4";
    static const char *envelope        = "\U0001f4e7";
    static const char *bullet          = "\u2022";
    static const char *folder          = "\U0001f4c1";
    static const char *globe           = "\U0001f310";
    static const char *quote           = "\u201c";
    static const char *magnifyingGlass = "\U0001f50d";
    const float midRunSkip = 0; /*0.120f;*/ /* extra space between wrapped text/quote lines */
    const iPrefs *prefs = prefs_App();
    clear_Array(&d->layout);
    clearLinks_GmDocument_(d);
    clear_Array(&d->headings);
    clear_String(&d->title);
    clear_String(&d->bannerText);
    if (d->size.x <= 0 || isEmpty_String(&d->source)) {
        return;
    }
    const iRangecc   content       = range_String(&d->source);
    iRangecc         contentLine   = iNullRange;
    iInt2            pos           = zero_I2();
    iBool            isFirstText   = prefs->bigFirstParagraph;
    iBool            addQuoteIcon  = prefs->quoteIcon;
    iBool            isPreformat   = iFalse;
    iRangecc         preAltText    = iNullRange;
    int              preFont       = preformatted_FontId;
    uint16_t         preId         = 0;
    iBool            enableIndents = iFalse;
    iBool            addSiteBanner = d->bannerType != none_GmDocumentBanner;
    enum iGmLineType prevType      = text_GmLineType;
    if (d->format == plainText_GmDocumentFormat) {
        isPreformat = iTrue;
        isFirstText = iFalse;
    }
    while (nextSplit_Rangecc(content, "\n", &contentLine)) {
        iRangecc line = contentLine; /* `line` will be trimmed later; would confuse nextSplit */
        iGmRun run = { .color = white_ColorId };
        enum iGmLineType type;
        int indent = 0;
        /* Detect the type of the line. */
        if (!isPreformat) {
            type = lineType_GmDocument_(d, line);
            if (contentLine.start == content.start) {
                prevType = type;
            }
            indent = indents[type];
            if (type == preformatted_GmLineType) {
                isPreformat = iTrue;
                preId++;
                preFont = preformatted_FontId;
                /* Use a smaller font if the block contents are wide. */
                if (measurePreformattedBlock_GmDocument_(d, line.start, preFont).x >
                    d->size.x - indents[preformatted_GmLineType]) {
                    preFont = preformattedSmall_FontId;
                }
                trimLine_Rangecc_(&line, type);
                preAltText = line;
                /* TODO: store and link the alt text to this run */
                continue;
            }
            else if (type == link_GmLineType) {
                line = addLink_GmDocument_(d, line, &run.linkId);
                if (!run.linkId) {
                    /* Invalid formatting. */
                    type = text_GmLineType;
                }
            }
            trimLine_Rangecc_(&line, type);
            run.font = fonts[type];
            /* Remember headings for the document outline. */
            if (type == heading1_GmLineType || type == heading2_GmLineType || type == heading3_GmLineType) {
                pushBack_Array(
                    &d->headings,
                    &(iGmHeading){ .text = line, .level = type - heading1_GmLineType });
            }
        }
        else {
            /* Preformatted line. */
            type = preformatted_GmLineType;
            if (contentLine.start == content.start) {
                prevType = type;
            }
            if (d->format == gemini_GmDocumentFormat &&
                startsWithSc_Rangecc(line, "```", &iCaseSensitive)) {
                isPreformat = iFalse;
                preAltText = iNullRange;
                addSiteBanner = iFalse; /* overrides the banner */
                continue;
            }
            run.preId = preId;
            run.font = (d->format == plainText_GmDocumentFormat ? regularMonospace_FontId : preFont);
            indent = indents[type];
        }
        if (addSiteBanner) {
            addSiteBanner = iFalse;
            const iRangecc bannerText = urlHost_String(&d->url);
            if (!isEmpty_Range(&bannerText)) {
                setRange_String(&d->bannerText, bannerText);
                iGmRun banner    = { .flags = decoration_GmRunFlag | siteBanner_GmRunFlag };
                banner.bounds    = zero_Rect();
                banner.visBounds = init_Rect(0, 0, d->size.x, lineHeight_Text(banner_FontId) * 2);
                if (d->bannerType == certificateWarning_GmDocumentBanner) {
                    banner.visBounds.size.y += iMaxi(6000 * lineHeight_Text(uiLabel_FontId) /
                                                         d->size.x, lineHeight_Text(uiLabel_FontId) * 5);
                }
                banner.font      = banner_FontId;
                banner.text      = bannerText;
                banner.color     = tmBannerTitle_ColorId;
                pushBack_Array(&d->layout, &banner);
                pos.y += height_Rect(banner.visBounds) + lineHeight_Text(paragraph_FontId);
            }
        }
        /* Empty lines don't produce text runs. */
        if (isEmpty_Range(&line)) {
            if (type == quote_GmLineType && !prefs->quoteIcon) {
                /* For quote indicators we still need to produce a run. */
                run.visBounds.pos  = addX_I2(pos, indents[type] * gap_Text);
                run.visBounds.size = init_I2(gap_Text, lineHeight_Text(run.font));
                run.bounds         = zero_Rect(); /* just visual */
                run.flags          = quoteBorder_GmRunFlag | decoration_GmRunFlag;
                run.text           = iNullRange;
                pushBack_Array(&d->layout, &run);
            }
            pos.y += lineHeight_Text(run.font);
            prevType = type;
            if (type != quote_GmLineType) {
                addQuoteIcon = prefs->quoteIcon;
            }
            /* TODO: Extra skip needed here? */
            continue;
        }
        /* Begin indenting after the first preformatted block. */
        if (type != preformatted_GmLineType || prevType != preformatted_GmLineType) {
            enableIndents = iTrue;
        }
        if (!enableIndents) {
            indent = 0;
        }
        /* Check the margin vs. previous run. */
        if (!isPreformat || (prevType != preformatted_GmLineType)) {
            int required =
                iMax(topMargin[type], bottomMargin[prevType]) * lineHeight_Text(paragraph_FontId);
            if ((type == link_GmLineType && prevType == link_GmLineType) ||
                (type == quote_GmLineType && prevType == quote_GmLineType)) {
                /* No margin between consecutive links/quote lines. */
                required =
                    (type == link_GmLineType ? midRunSkip * lineHeight_Text(paragraph_FontId) : 0);
            }
            if (isEmpty_Array(&d->layout)) {
                required = 0; /* top of document */
            }
            int delta = pos.y - lastVisibleRunBottom_GmDocument_(d);
            if (delta < required) {
                pos.y += required - delta;
            }
        }
        /* Save the document title (first high-level heading). */
        if ((type == heading1_GmLineType || type == heading2_GmLineType) &&
            isEmpty_String(&d->title)) {
            setRange_String(&d->title, line);
        }
        /* List bullet. */
        run.color = colors[type];
        if (type == bullet_GmLineType) {
            iGmRun bulRun = run;
            bulRun.color = tmQuote_ColorId;
            bulRun.visBounds.pos  = addX_I2(pos, indent * gap_Text);
            bulRun.visBounds.size = advance_Text(run.font, bullet);
            bulRun.visBounds.pos.x -= 4 * gap_Text - width_Rect(bulRun.visBounds) / 2;
            bulRun.bounds = zero_Rect(); /* just visual */
            bulRun.text   = range_CStr(bullet);
            bulRun.flags |= decoration_GmRunFlag;
            pushBack_Array(&d->layout, &bulRun);
        }
        /* Quote icon. */
        if (type == quote_GmLineType && addQuoteIcon) {
            addQuoteIcon    = iFalse;
            iGmRun quoteRun = run;
            quoteRun.font   = heading1_FontId;
            quoteRun.text   = range_CStr(quote);
            quoteRun.color  = tmQuoteIcon_ColorId;
            iRect vis       = visualBounds_Text(quoteRun.font, quoteRun.text);
            quoteRun.visBounds.size = advance_Text(quoteRun.font, quote);
            quoteRun.visBounds.pos =
                add_I2(pos,
                       init_I2(indents[text_GmLineType] * gap_Text,
                               lineHeight_Text(quote_FontId) / 2 - bottom_Rect(vis)));
            quoteRun.bounds = zero_Rect(); /* just visual */
            quoteRun.flags |= decoration_GmRunFlag;
            pushBack_Array(&d->layout, &quoteRun);
        }
        else if (type != quote_GmLineType) {
            addQuoteIcon = prefs->quoteIcon;
        }
        /* Link icon. */
        if (type == link_GmLineType) {
            iGmRun icon = run;
            icon.visBounds.pos  = pos;
            icon.visBounds.size = init_I2(indent * gap_Text, lineHeight_Text(run.font));
            icon.bounds         = zero_Rect(); /* just visual */
            const iGmLink *link = constAt_PtrArray(&d->links, run.linkId - 1);
            icon.text           = range_CStr(link->flags & query_GmLinkFlag    ? magnifyingGlass
                                             : link->flags & file_GmLinkFlag   ? folder
                                             : link->flags & mailto_GmLinkFlag ? envelope
                                             : link->flags & remote_GmLinkFlag ? globe
                                                                               : arrow);
            icon.font = regular_FontId;
            if (link->flags & remote_GmLinkFlag) {
                icon.visBounds.pos.x -= gap_Text / 2;
            }
            icon.color = linkColor_GmDocument(d, run.linkId, icon_GmLinkPart);
            icon.flags |= decoration_GmRunFlag;
            pushBack_Array(&d->layout, &icon);
        }
        run.color = colors[type];
        if (d->format == plainText_GmDocumentFormat) {
            run.color = colors[text_GmLineType];
        }
        /* Special formatting for the first paragraph (e.g., subtitle, introduction, or lede). */
        int bigCount = 0;
        if (type == text_GmLineType && isFirstText) {
            if (!isMono) run.font = firstParagraph_FontId;
            run.color = tmFirstParagraph_ColorId;
            bigCount = 15; /* max lines -- what if the whole document is one paragraph? */
            isFirstText = iFalse;
        }
        else if (type != heading1_GmLineType) {
            isFirstText = iFalse;
        }
        iRangecc runLine = line;
        /* Create one or more text runs for this line. */
        run.flags |= startOfLine_GmRunFlag;
        if (!prefs->quoteIcon && type == quote_GmLineType) {
            run.flags |= quoteBorder_GmRunFlag;
        }
        iAssert(!isEmpty_Range(&runLine)); /* must have something at this point */
        while (!isEmpty_Range(&runLine)) {
            /* Little bit of breathing space between wrapped lines. */
            if ((type == text_GmLineType || type == quote_GmLineType ||
                 type == bullet_GmLineType) &&
                runLine.start != line.start) {
                pos.y += midRunSkip * lineHeight_Text(run.font);
            }
            run.bounds.pos = addX_I2(pos, indent * gap_Text);
            const char *contPos;
            const int   avail = isPreformat ? 0 : (d->size.x - run.bounds.pos.x);
            const iInt2 dims  = tryAdvance_Text(run.font, runLine, avail, &contPos);
            iChangeFlags(run.flags, wide_GmRunFlag, (isPreformat && dims.x > d->size.x));
            run.bounds.size.x = iMax(avail, dims.x); /* Extends to the right edge for selection. */
            run.bounds.size.y = dims.y;
            run.visBounds     = run.bounds;
            run.visBounds.size.x = dims.x;
            if (contPos > runLine.start) {
                run.text = (iRangecc){ runLine.start, contPos };
            }
            else {
                run.text = runLine;
                contPos = runLine.end;
            }
            pushBack_Array(&d->layout, &run);
            run.flags &= ~startOfLine_GmRunFlag;
            runLine.start = contPos;
            trimStart_Rangecc(&runLine);
            pos.y += lineHeight_Text(run.font);
            if (--bigCount == 0) {
                run.font = fonts[text_GmLineType];
                run.color = colors[text_GmLineType];
            }
        }
        /* Flag the end of line, too. */
        ((iGmRun *) back_Array(&d->layout))->flags |= endOfLine_GmRunFlag;
        /* Image or audio content. */
        if (type == link_GmLineType) {
            const iMediaId imageId = findLinkImage_Media(d->media, run.linkId);
            const iMediaId audioId = !imageId ? findLinkAudio_Media(d->media, run.linkId) : 0;
            if (imageId) {
                iGmImageInfo img;
                imageInfo_Media(d->media, imageId, &img);
                /* Mark the link as having content. */ {
                    iGmLink *link = at_PtrArray(&d->links, run.linkId - 1);
                    link->flags |= content_GmLinkFlag;
                    if (img.isPermanent) {
                        link->flags |= permanent_GmLinkFlag;
                    }
                }
                const int margin = lineHeight_Text(paragraph_FontId) / 2;
                pos.y += margin;
                run.bounds.pos = pos;
                run.bounds.size.x = d->size.x;
                const float aspect = (float) img.size.y / (float) img.size.x;
                run.bounds.size.y = d->size.x * aspect;
                run.visBounds = run.bounds;
                const iInt2 maxSize = mulf_I2(img.size, get_Window()->pixelRatio);
                if (width_Rect(run.visBounds) > maxSize.x) {
                    /* Don't scale the image up. */
                    run.visBounds.size.y = run.visBounds.size.y * maxSize.x / width_Rect(run.visBounds);
                    run.visBounds.size.x = maxSize.x;
                    run.visBounds.pos.x = run.bounds.size.x / 2 - width_Rect(run.visBounds) / 2;
                    run.bounds.size.y = run.visBounds.size.y;
                }
                run.text    = iNullRange;
                run.font    = 0;
                run.color   = 0;
                run.imageId = imageId;
                pushBack_Array(&d->layout, &run);
                pos.y += run.bounds.size.y + margin;
            }
            else if (audioId) {
                iGmAudioInfo info;
                audioInfo_Media(d->media, audioId, &info);
                /* Mark the link as having content. */ {
                    iGmLink *link = at_PtrArray(&d->links, run.linkId - 1);
                    link->flags |= content_GmLinkFlag;
                    if (info.isPermanent) {
                        link->flags |= permanent_GmLinkFlag;
                    }
                }
                const int margin = lineHeight_Text(paragraph_FontId) / 2;
                pos.y += margin;
                run.bounds.pos    = pos;
                run.bounds.size.x = d->size.x;
                run.bounds.size.y = lineHeight_Text(uiContent_FontId) + 3 * gap_UI;
                run.visBounds     = run.bounds;
                run.text          = iNullRange;
                run.color         = 0;
                run.audioId       = audioId;
                pushBack_Array(&d->layout, &run);
                pos.y += run.bounds.size.y + margin;
            }
        }
        prevType = type;
    }
    d->size.y = pos.y;
    /* Go over the preformatted blocks and mark them wide if at least one run is wide. */ {
        /* TODO: Store the dimensions and ranges for later access. */
        iForEach(Array, i, &d->layout) {
            iGmRun *run = i.value;
            if (run->preId && run->flags & wide_GmRunFlag) {
                iGmRunRange block = findPreformattedRange_GmDocument(d, run);
                for (const iGmRun *j = block.start; j != block.end; j++) {
                    iConstCast(iGmRun *, j)->flags |= wide_GmRunFlag;
                }
                /* Skip to the end of the block. */
                i.pos = block.end - (const iGmRun *) constData_Array(&d->layout) - 1;
            }
        }
    }
}

void init_GmDocument(iGmDocument *d) {
    d->format = gemini_GmDocumentFormat;
    init_String(&d->source);
    init_String(&d->url);
    init_String(&d->localHost);
    d->bannerType = siteDomain_GmDocumentBanner;
    d->size = zero_I2();
    init_Array(&d->layout, sizeof(iGmRun));
    init_PtrArray(&d->links);
    init_String(&d->bannerText);
    init_String(&d->title);
    init_Array(&d->headings, sizeof(iGmHeading));
    d->themeSeed = 0;
    d->siteIcon = 0;
    d->media = new_Media();
}

void deinit_GmDocument(iGmDocument *d) {
    delete_Media(d->media);
    deinit_String(&d->bannerText);
    deinit_String(&d->title);
    clearLinks_GmDocument_(d);
    deinit_PtrArray(&d->links);
    deinit_Array(&d->headings);
    deinit_Array(&d->layout);
    deinit_String(&d->localHost);
    deinit_String(&d->url);
    deinit_String(&d->source);
}

iMedia *media_GmDocument(iGmDocument *d) {
    return d->media;
}

const iMedia *constMedia_GmDocument(const iGmDocument *d) {
    return d->media;
}

void reset_GmDocument(iGmDocument *d) {
    clear_Media(d->media);
    clearLinks_GmDocument_(d);
    clear_Array(&d->layout);
    clear_Array(&d->headings);
    clear_String(&d->url);
    clear_String(&d->localHost);
    d->themeSeed = 0;
}

static void setDerivedThemeColors_(enum iGmDocumentTheme theme) {
    set_Color(tmQuoteIcon_ColorId,
              mix_Color(get_Color(tmQuote_ColorId), get_Color(tmBackground_ColorId), 0.55f));
    set_Color(tmBannerSideTitle_ColorId,
              mix_Color(get_Color(tmBannerTitle_ColorId), get_Color(tmBackground_ColorId),
                        theme == colorfulDark_GmDocumentTheme ? 0.55f : 0));
    set_Color(tmOutlineHeadingAbove_ColorId, get_Color(white_ColorId));
    set_Color(tmOutlineHeadingBelow_ColorId, get_Color(black_ColorId));
    switch (theme) {
        case colorfulDark_GmDocumentTheme:
            set_Color(tmOutlineHeadingBelow_ColorId, get_Color(tmBannerTitle_ColorId));
            if (equal_Color(get_Color(tmOutlineHeadingAbove_ColorId),
                            get_Color(tmOutlineHeadingBelow_ColorId))) {
                set_Color(tmOutlineHeadingBelow_ColorId, get_Color(tmHeading3_ColorId));
            }
            break;
        case colorfulLight_GmDocumentTheme:
        case sepia_GmDocumentTheme:
            set_Color(tmOutlineHeadingAbove_ColorId, get_Color(black_ColorId));
            set_Color(tmOutlineHeadingBelow_ColorId, mix_Color(get_Color(tmBackground_ColorId), get_Color(black_ColorId), 0.6f));
            break;
        case gray_GmDocumentTheme:
            set_Color(tmOutlineHeadingBelow_ColorId, get_Color(gray75_ColorId));
            break;
        case white_GmDocumentTheme:
            set_Color(tmOutlineHeadingBelow_ColorId, mix_Color(get_Color(tmBannerIcon_ColorId), get_Color(white_ColorId), 0.6f));
            break;
        case highContrast_GmDocumentTheme:
            set_Color(tmOutlineHeadingAbove_ColorId, get_Color(black_ColorId));
            break;
        default:
            break;
    }
}

void setThemeSeed_GmDocument(iGmDocument *d, const iBlock *seed) {
    const iPrefs *        prefs = prefs_App();
    enum iGmDocumentTheme theme =
        (isDark_ColorTheme(colorTheme_App()) ? prefs->docThemeDark : prefs->docThemeLight);
    static const iChar siteIcons[] = {
        0x203b,  0x2042,  0x205c,  0x2182,  0x25ed,  0x2600,  0x2601,  0x2604,  0x2605,  0x2606,
        0x265c,  0x265e,  0x2690,  0x2691,  0x2693,  0x2698,  0x2699,  0x26f0,  0x270e,  0x2728,
        0x272a,  0x272f,  0x2731,  0x2738,  0x273a,  0x273e,  0x2740,  0x2742,  0x2744,  0x2748,
        0x274a,  0x2751,  0x2756,  0x2766,  0x27bd,  0x27c1,  0x27d0,  0x2b19,  0x1f300, 0x1f303,
        0x1f306, 0x1f308, 0x1f30a, 0x1f319, 0x1f31f, 0x1f320, 0x1f340, 0x1f4cd, 0x1f4e1, 0x1f531,
        0x1f533, 0x1f657, 0x1f659, 0x1f665, 0x1f668, 0x1f66b, 0x1f78b, 0x1f796, 0x1f79c,
    };
    /* Default colors. These are used on "about:" pages and local files, for example. */ {
        /* Link colors are generally the same in all themes. */
        set_Color(tmBadLink_ColorId, get_Color(red_ColorId));
        if (isDark_GmDocumentTheme(theme)) {
            set_Color(tmInlineContentMetadata_ColorId, get_Color(cyan_ColorId));
            set_Color(tmLinkText_ColorId, get_Color(white_ColorId));
            set_Color(tmLinkIcon_ColorId, get_Color(cyan_ColorId));
            set_Color(tmLinkTextHover_ColorId, get_Color(cyan_ColorId));
            set_Color(tmLinkIconVisited_ColorId, get_Color(teal_ColorId));
            set_Color(tmLinkDomain_ColorId, get_Color(teal_ColorId));
            set_Color(tmLinkLastVisitDate_ColorId, get_Color(cyan_ColorId));
            set_Color(tmHypertextLinkText_ColorId, get_Color(white_ColorId));
            set_Color(tmHypertextLinkIcon_ColorId, get_Color(orange_ColorId));
            set_Color(tmHypertextLinkTextHover_ColorId, get_Color(orange_ColorId));
            set_Color(tmHypertextLinkIconVisited_ColorId, get_Color(brown_ColorId));
            set_Color(tmHypertextLinkDomain_ColorId, get_Color(brown_ColorId));
            set_Color(tmHypertextLinkLastVisitDate_ColorId, get_Color(orange_ColorId));
            set_Color(tmGopherLinkText_ColorId, get_Color(white_ColorId));
            set_Color(tmGopherLinkIcon_ColorId, get_Color(magenta_ColorId));
            set_Color(tmGopherLinkTextHover_ColorId, get_Color(blue_ColorId));
            set_Color(tmGopherLinkIconVisited_ColorId, get_Color(blue_ColorId));
            set_Color(tmGopherLinkDomain_ColorId, get_Color(magenta_ColorId));
            set_Color(tmGopherLinkLastVisitDate_ColorId, get_Color(blue_ColorId));
        }
        else {
            set_Color(tmInlineContentMetadata_ColorId, get_Color(brown_ColorId));
            set_Color(tmLinkText_ColorId, get_Color(black_ColorId));
            set_Color(tmLinkIcon_ColorId, get_Color(teal_ColorId));
            set_Color(tmLinkTextHover_ColorId, get_Color(teal_ColorId));
            set_Color(tmLinkIconVisited_ColorId, get_Color(cyan_ColorId));
            set_Color(tmLinkDomain_ColorId, get_Color(cyan_ColorId));
            set_Color(tmLinkLastVisitDate_ColorId, get_Color(teal_ColorId));
            set_Color(tmHypertextLinkText_ColorId, get_Color(black_ColorId));
            set_Color(tmHypertextLinkIcon_ColorId, get_Color(brown_ColorId));
            set_Color(tmHypertextLinkTextHover_ColorId, get_Color(brown_ColorId));
            set_Color(tmHypertextLinkIconVisited_ColorId, get_Color(orange_ColorId));
            set_Color(tmHypertextLinkDomain_ColorId, get_Color(orange_ColorId));
            set_Color(tmHypertextLinkLastVisitDate_ColorId, get_Color(brown_ColorId));
            set_Color(tmGopherLinkText_ColorId, get_Color(black_ColorId));
            set_Color(tmGopherLinkIcon_ColorId, get_Color(magenta_ColorId));
            set_Color(tmGopherLinkTextHover_ColorId, get_Color(blue_ColorId));
            set_Color(tmGopherLinkIconVisited_ColorId, get_Color(blue_ColorId));
            set_Color(tmGopherLinkDomain_ColorId, get_Color(magenta_ColorId));
            set_Color(tmGopherLinkLastVisitDate_ColorId, get_Color(blue_ColorId));
        }
        /* Set the non-link default colors. Note that some/most of these are overwritten later
           if a theme seed if available. */
        if (theme == colorfulDark_GmDocumentTheme) {
            const iHSLColor base = { 200, 0, 0.15f, 1.0f };
            setHsl_Color(tmBackground_ColorId, base);
            set_Color(tmParagraph_ColorId, get_Color(gray75_ColorId));
            setHsl_Color(tmFirstParagraph_ColorId, addSatLum_HSLColor(base, 0, 0.75f));
            set_Color(tmQuote_ColorId, get_Color(cyan_ColorId));
            set_Color(tmPreformatted_ColorId, get_Color(cyan_ColorId));
            set_Color(tmHeading1_ColorId, get_Color(white_ColorId));
            setHsl_Color(tmHeading2_ColorId, addSatLum_HSLColor(base, 0.5f, 0.5f));
            setHsl_Color(tmHeading3_ColorId, addSatLum_HSLColor(base, 1.0f, 0.4f));
            setHsl_Color(tmBannerBackground_ColorId, addSatLum_HSLColor(base, 0, -0.05f));
            set_Color(tmBannerTitle_ColorId, get_Color(white_ColorId));
            set_Color(tmBannerIcon_ColorId, get_Color(orange_ColorId));
        }
        else if (theme == colorfulLight_GmDocumentTheme) {
            const iHSLColor base = addSatLum_HSLColor(get_HSLColor(teal_ColorId), -0.3f, 0.5f);
            setHsl_Color(tmBackground_ColorId, base);
            set_Color(tmParagraph_ColorId, get_Color(black_ColorId));
            set_Color(tmFirstParagraph_ColorId, get_Color(black_ColorId));
            setHsl_Color(tmQuote_ColorId, addSatLum_HSLColor(base, 0, -0.25f));
            setHsl_Color(tmPreformatted_ColorId, addSatLum_HSLColor(base, 0, -0.3f));
            set_Color(tmHeading1_ColorId, get_Color(white_ColorId));
            set_Color(tmHeading2_ColorId, mix_Color(get_Color(tmBackground_ColorId), get_Color(black_ColorId), 0.67f));
            set_Color(tmHeading3_ColorId, mix_Color(get_Color(tmBackground_ColorId), get_Color(black_ColorId), 0.55f));
            setHsl_Color(tmBannerBackground_ColorId, addSatLum_HSLColor(base, 0, -0.1f));
            setHsl_Color(tmBannerIcon_ColorId, addSatLum_HSLColor(base, 0, -0.2f));
            setHsl_Color(tmBannerTitle_ColorId, addSatLum_HSLColor(base, 0, -0.2f));
            setHsl_Color(tmLinkIcon_ColorId, addSatLum_HSLColor(get_HSLColor(teal_ColorId), 0, 0));
            set_Color(tmLinkIconVisited_ColorId, mix_Color(get_Color(tmBackground_ColorId), get_Color(teal_ColorId), 0.35f));
            set_Color(tmLinkDomain_ColorId, get_Color(teal_ColorId));
            setHsl_Color(tmHypertextLinkIcon_ColorId, get_HSLColor(white_ColorId));
            set_Color(tmHypertextLinkIconVisited_ColorId, mix_Color(get_Color(tmBackground_ColorId), get_Color(white_ColorId), 0.5f));
            set_Color(tmHypertextLinkDomain_ColorId, get_Color(brown_ColorId));
            setHsl_Color(tmGopherLinkIcon_ColorId, addSatLum_HSLColor(get_HSLColor(tmGopherLinkIcon_ColorId), 0, -0.25f));
            setHsl_Color(tmGopherLinkTextHover_ColorId, addSatLum_HSLColor(get_HSLColor(tmGopherLinkTextHover_ColorId), 0, -0.3f));
        }
        else if (theme == black_GmDocumentTheme) {
            set_Color(tmBackground_ColorId, get_Color(black_ColorId));
            set_Color(tmParagraph_ColorId, get_Color(gray75_ColorId));
            set_Color(tmFirstParagraph_ColorId, mix_Color(get_Color(gray75_ColorId), get_Color(white_ColorId), 0.5f));
            set_Color(tmQuote_ColorId, get_Color(orange_ColorId));
            set_Color(tmPreformatted_ColorId, get_Color(orange_ColorId));
            set_Color(tmHeading1_ColorId, get_Color(cyan_ColorId));
            set_Color(tmHeading2_ColorId, mix_Color(get_Color(cyan_ColorId), get_Color(white_ColorId), 0.66f));
            set_Color(tmHeading3_ColorId, get_Color(white_ColorId));
            set_Color(tmBannerBackground_ColorId, get_Color(black_ColorId));
            set_Color(tmBannerTitle_ColorId, get_Color(teal_ColorId));
            set_Color(tmBannerIcon_ColorId, get_Color(teal_ColorId));
        }
        else if (theme == gray_GmDocumentTheme) {
            set_Color(tmBackground_ColorId, mix_Color(get_Color(gray25_ColorId), get_Color(black_ColorId), 0.25f));
            set_Color(tmParagraph_ColorId, mix_Color(get_Color(gray75_ColorId), get_Color(white_ColorId), 0.25f));
            set_Color(tmFirstParagraph_ColorId, mix_Color(get_Color(gray75_ColorId), get_Color(white_ColorId), 0.5f));
            set_Color(tmQuote_ColorId, get_Color(orange_ColorId));
            set_Color(tmPreformatted_ColorId, get_Color(orange_ColorId));
            set_Color(tmHeading1_ColorId, get_Color(cyan_ColorId));
            set_Color(tmHeading2_ColorId, mix_Color(get_Color(cyan_ColorId), get_Color(white_ColorId), 0.66f));
            set_Color(tmHeading3_ColorId, get_Color(white_ColorId));
            set_Color(tmBannerBackground_ColorId, mix_Color(get_Color(gray25_ColorId), get_Color(black_ColorId), 0.5f));
            set_Color(tmBannerTitle_ColorId, get_Color(teal_ColorId));
            set_Color(tmBannerIcon_ColorId, get_Color(teal_ColorId));
        }
        else if (theme == sepia_GmDocumentTheme) {
            const iHSLColor base = { 40, 0.6f, 0.9f, 1.0f };
            setHsl_Color(tmBackground_ColorId, base);
            set_Color(tmParagraph_ColorId, get_Color(black_ColorId));
            set_Color(tmFirstParagraph_ColorId, get_Color(black_ColorId));
            set_Color(tmQuote_ColorId, get_Color(brown_ColorId));
            set_Color(tmPreformatted_ColorId, get_Color(brown_ColorId));
            set_Color(tmHeading1_ColorId, get_Color(brown_ColorId));
            set_Color(tmHeading2_ColorId, mix_Color(get_Color(brown_ColorId), get_Color(black_ColorId), 0.5f));
            set_Color(tmHeading3_ColorId, get_Color(black_ColorId));
            set_Color(tmBannerBackground_ColorId, mix_Color(get_Color(tmBackground_ColorId), get_Color(brown_ColorId), 0.15f));
            set_Color(tmBannerTitle_ColorId, get_Color(brown_ColorId));
            set_Color(tmBannerIcon_ColorId, get_Color(brown_ColorId));
        }
        else if (theme == white_GmDocumentTheme) {
            const iHSLColor base = { 40, 0, 1.0f, 1.0f };
            setHsl_Color(tmBackground_ColorId, base);
            set_Color(tmParagraph_ColorId, get_Color(gray25_ColorId));
            set_Color(tmFirstParagraph_ColorId, get_Color(black_ColorId));
            set_Color(tmQuote_ColorId, get_Color(brown_ColorId));
            set_Color(tmPreformatted_ColorId, get_Color(brown_ColorId));
            set_Color(tmHeading1_ColorId, get_Color(black_ColorId));
            setHsl_Color(tmHeading2_ColorId, addSatLum_HSLColor(base, 0.15f, -0.7f));
            setHsl_Color(tmHeading3_ColorId, addSatLum_HSLColor(base, 0.3f, -0.6f));
            set_Color(tmBannerBackground_ColorId, get_Color(white_ColorId));
            set_Color(tmBannerTitle_ColorId, get_Color(gray50_ColorId));
            set_Color(tmBannerIcon_ColorId, get_Color(teal_ColorId));
        }
        else if (theme == highContrast_GmDocumentTheme) {
            set_Color(tmBackground_ColorId, get_Color(white_ColorId));
            set_Color(tmParagraph_ColorId, get_Color(black_ColorId));
            set_Color(tmFirstParagraph_ColorId, get_Color(black_ColorId));
            set_Color(tmQuote_ColorId, get_Color(black_ColorId));
            set_Color(tmPreformatted_ColorId, get_Color(black_ColorId));
            set_Color(tmHeading1_ColorId, get_Color(black_ColorId));
            set_Color(tmHeading2_ColorId, get_Color(black_ColorId));
            set_Color(tmHeading3_ColorId, get_Color(black_ColorId));
            set_Color(tmBannerBackground_ColorId, mix_Color(get_Color(gray75_ColorId), get_Color(white_ColorId), 0.75f));
            set_Color(tmBannerTitle_ColorId, get_Color(black_ColorId));
            set_Color(tmBannerIcon_ColorId, get_Color(black_ColorId));
        }
        /* Apply the saturation setting. */
        for (int i = tmFirst_ColorId; i < max_ColorId; i++) {
            if (!isLink_ColorId(i)) {
                iHSLColor color = get_HSLColor(i);
                color.sat *= prefs->saturation;
                setHsl_Color(i, color);
            }
        }
    }
    if (seed && !isEmpty_Block(seed)) {
        d->themeSeed = crc32_Block(seed);
        d->siteIcon  = siteIcons[(d->themeSeed >> 7) % iElemCount(siteIcons)];
    }
    else {
        d->themeSeed = 0;
        d->siteIcon  = 0;
    }
    /* Set up colors. */
    if (d->themeSeed) {
        enum iHue {
            red_Hue,
            reddishOrange_Hue,
            yellowishOrange_Hue,
            yellow_Hue,
            greenishYellow_Hue,
            green_Hue,
            bluishGreen_Hue,
            cyan_Hue,
            skyBlue_Hue,
            blue_Hue,
            violet_Hue,
            pink_Hue
        };
        static const float hues[] = { 5, 25, 40, 56, 80, 120, 160, 180, 208, 231, 270, 324 };
        static const struct {
            int index[2];
        } altHues[iElemCount(hues)] = {
            { 2, 4 },  /* red */
            { 8, 3 },  /* reddish orange */
            { 7, 9 },  /* yellowish orange */
            { 5, 7 },  /* yellow */
            { 11, 2 }, /* greenish yellow */
            { 1, 3 },  /* green */
            { 2, 4 },  /* bluish green */
            { 2, 11 }, /* cyan */
            { 6, 10 }, /* sky blue */
            { 3, 11 }, /* blue */
            { 8, 9 },  /* violet */
            { 7, 8 },  /* pink */
        };
        const size_t primIndex = d->themeSeed ? (d->themeSeed & 0xff) % iElemCount(hues) : 2;

        const int   altIndex[2] = { (d->themeSeed & 0x4) != 0, (d->themeSeed & 0x40) != 0 };
        const float altHue      = hues[d->themeSeed ? altHues[primIndex].index[altIndex[0]] : 8];
        const float altHue2     = hues[d->themeSeed ? altHues[primIndex].index[altIndex[1]] : 8];

        const iBool isBannerLighter = (d->themeSeed & 0x4000) != 0;
        const iBool isDarkBgSat =
            (d->themeSeed & 0x200000) != 0 && (primIndex < 1 || primIndex > 4);

        static const float normLum[] = { 0.8f, 0.7f, 0.675f, 0.65f, 0.55f,
                                         0.6f, 0.475f, 0.475f, 0.75f, 0.8f,
                                         0.85f, 0.85f };

        if (theme == colorfulDark_GmDocumentTheme) {
            iHSLColor base    = { hues[primIndex],
                                  0.8f * (d->themeSeed >> 24) / 255.0f,
                                  0.06f + 0.09f * ((d->themeSeed >> 5) & 0x7) / 7.0f,
                                  1.0f };
            iHSLColor altBase = { altHue, base.sat, base.lum, 1 };

            setHsl_Color(tmBackground_ColorId, base);

            setHsl_Color(tmBannerBackground_ColorId, addSatLum_HSLColor(base, 0.1f, 0.04f * (isBannerLighter ? 1 : -1)));
            setHsl_Color(tmBannerTitle_ColorId, setLum_HSLColor(addSatLum_HSLColor(base, 0.1f, 0), 0.55f));
            setHsl_Color(tmBannerIcon_ColorId, setLum_HSLColor(addSatLum_HSLColor(base, 0.35f, 0), 0.65f));

//            printf("primHue: %zu  alts: %d %d\n",
//                   primIndex,
//                   altHues[primIndex].index[altIndex[0]],
//                   altHues[primIndex].index[altIndex[1]]);

            const float titleLum = 0.2f * ((d->themeSeed >> 17) & 0x7) / 7.0f;
            setHsl_Color(tmHeading1_ColorId, setLum_HSLColor(altBase, titleLum + 0.80f));
            setHsl_Color(tmHeading2_ColorId, setLum_HSLColor(altBase, titleLum + 0.70f));
            setHsl_Color(tmHeading3_ColorId, setLum_HSLColor(altBase, titleLum + 0.60f));

            setHsl_Color(tmParagraph_ColorId, addSatLum_HSLColor(base, 0.1f, 0.6f));

//            printf("heading3: %d,%d,%d\n", get_Color(tmHeading3_ColorId).r,  get_Color(tmHeading3_ColorId).g,  get_Color(tmHeading3_ColorId).b);
//            printf("paragr  : %d,%d,%d\n", get_Color(tmParagraph_ColorId).r, get_Color(tmParagraph_ColorId).g, get_Color(tmParagraph_ColorId).b);
//            printf("delta   : %d\n", delta_Color(get_Color(tmHeading3_ColorId), get_Color(tmParagraph_ColorId)));

            if (delta_Color(get_Color(tmHeading3_ColorId), get_Color(tmParagraph_ColorId)) <= 80) {
                /* Smallest headings may be too close to body text color. */
//                iHSLColor clr = get_HSLColor(tmParagraph_ColorId);
//                clr.lum       = iMax(0.5f, clr.lum - 0.15f);
                //setHsl_Color(tmParagraph_ColorId, clr);
                setHsl_Color(tmHeading3_ColorId,
                             addSatLum_HSLColor(get_HSLColor(tmHeading3_ColorId), 0, 0.15f));
            }

            setHsl_Color(tmFirstParagraph_ColorId, addSatLum_HSLColor(base, 0.2f, 0.72f));
            setHsl_Color(tmPreformatted_ColorId, (iHSLColor){ altHue2, 1.0f, 0.75f, 1.0f });
            set_Color(tmQuote_ColorId, get_Color(tmPreformatted_ColorId));
            set_Color(tmInlineContentMetadata_ColorId, get_Color(tmHeading3_ColorId));
        }
        else if (theme == colorfulLight_GmDocumentTheme) {
//            static int primIndex = 0;
//            primIndex = (primIndex + 1) % iElemCount(hues);
            iHSLColor base = { hues[primIndex], 1.0f, normLum[primIndex], 1.0f };
//            printf("prim:%d norm:%f\n", primIndex, normLum[primIndex]); fflush(stdout);
            static const float normSat[] = {
                0.85f, 0.9f, 1, 0.65f, 0.65f,
                0.65f, 0.9f, 0.9f, 1, 0.9f,
                1, 0.75f
            };
            iBool darkHeadings = iTrue;
            base.sat *= normSat[primIndex] * 0.8f;
            setHsl_Color(tmBackground_ColorId, base);
            set_Color(tmParagraph_ColorId, get_Color(black_ColorId));
            set_Color(tmFirstParagraph_ColorId, get_Color(black_ColorId));
            setHsl_Color(tmQuote_ColorId, addSatLum_HSLColor(base, 0, -base.lum * 0.67f));
            setHsl_Color(tmPreformatted_ColorId, addSatLum_HSLColor(base, 0, -base.lum * 0.75f));
            set_Color(tmHeading1_ColorId, get_Color(white_ColorId));
            set_Color(tmHeading2_ColorId, mix_Color(get_Color(tmBackground_ColorId), get_Color(darkHeadings ? black_ColorId : white_ColorId), 0.7f));
            set_Color(tmHeading3_ColorId, mix_Color(get_Color(tmBackground_ColorId), get_Color(darkHeadings ? black_ColorId : white_ColorId), 0.6f));
            setHsl_Color(tmBannerBackground_ColorId, addSatLum_HSLColor(base, 0, -0.04f));
            setHsl_Color(tmBannerIcon_ColorId, addSatLum_HSLColor(base, 0, -0.3f));
            setHsl_Color(tmBannerTitle_ColorId, addSatLum_HSLColor(base, 0, -0.25f));
            set_Color(tmLinkIconVisited_ColorId, mix_Color(get_Color(tmBackground_ColorId), get_Color(teal_ColorId), 0.3f));
        }
        else if (theme == white_GmDocumentTheme) {
            iHSLColor base    = { hues[primIndex], 1.0f, 0.3f, 1.0f };
            iHSLColor altBase = { altHue, base.sat, base.lum - 0.1f, 1 };

            set_Color(tmBackground_ColorId, get_Color(white_ColorId));
            set_Color(tmBannerBackground_ColorId, get_Color(white_ColorId));
            setHsl_Color(tmBannerTitle_ColorId, addSatLum_HSLColor(base, -0.6f, 0.25f));
            setHsl_Color(tmBannerIcon_ColorId, addSatLum_HSLColor(base, 0, 0));

            setHsl_Color(tmHeading1_ColorId, base);
            set_Color(tmHeading2_ColorId, mix_Color(rgb_HSLColor(base), rgb_HSLColor(altBase), 0.5f));
            setHsl_Color(tmHeading3_ColorId, altBase);

            setHsl_Color(tmParagraph_ColorId, addSatLum_HSLColor(base, 0, -0.25f));
            setHsl_Color(tmFirstParagraph_ColorId, addSatLum_HSLColor(base, 0, -0.1f));
            setHsl_Color(tmPreformatted_ColorId, (iHSLColor){ altHue2, 1.0f, 0.25f, 1.0f });
            set_Color(tmQuote_ColorId, get_Color(tmPreformatted_ColorId));
            set_Color(tmInlineContentMetadata_ColorId, get_Color(tmHeading3_ColorId));
        }
        else if (theme == black_GmDocumentTheme || theme == gray_GmDocumentTheme) {
            const float primHue        = hues[primIndex];
            const iHSLColor primBright = { primHue, 1, 0.6f, 1 };
            const iHSLColor primDim    = { primHue, 1, normLum[primIndex] + (theme == gray_GmDocumentTheme ? 0.0f : -0.3f), 1};
            const iHSLColor altBright  = { altHue, 1, normLum[altIndex[0]] + (theme == gray_GmDocumentTheme ? 0.1f : 0.0f), 1 };
            setHsl_Color(tmQuote_ColorId, altBright);
            setHsl_Color(tmPreformatted_ColorId, altBright);
            setHsl_Color(tmHeading1_ColorId, primBright);
            set_Color(tmHeading2_ColorId, mix_Color(get_Color(tmHeading1_ColorId), get_Color(white_ColorId), 0.66f));
            setHsl_Color(tmBannerTitle_ColorId, primDim);
            setHsl_Color(tmBannerIcon_ColorId, primDim);
        }

        /* Adjust colors based on light/dark mode. */
        for (int i = tmFirst_ColorId; i < max_ColorId; i++) {
            iHSLColor color = hsl_Color(get_Color(i));
            if (theme == colorfulDark_GmDocumentTheme) { /* dark mode */
                if (!isLink_ColorId(i)) {
                    if (isDarkBgSat) {
                        /* Saturate background, desaturate text. */
                        if (isBackground_ColorId(i)) {
                            if (primIndex != green_Hue) {
                                color.sat = (color.sat + 1) / 2;
                            }
                            else {
                                color.sat *= 0.5f;
                            }
                            color.lum *= 0.75f;
                        }
                        else if (isText_ColorId(i)) {
                            color.lum = (color.lum + 1) / 2;
                        }
                    }
                    else {
                        /* Desaturate background, saturate text. */
                        if (isBackground_ColorId(i)) {
                            color.sat *= 0.333f;
                        }
                        else if (isText_ColorId(i)) {
                            color.sat = (color.sat + 2) / 3;
                            color.lum = (2 * color.lum + 1) / 3;
                        }
                    }
                }
            }
            /* Modify overall saturation. */
            if (!isLink_ColorId(i)) {
                color.sat *= prefs->saturation;
            }
            setHsl_Color(i, color);
        }
    }
    /* Derived colors. */
    setDerivedThemeColors_(theme);
    /* Special exceptions. */
    if (seed) {
        if (equal_CStr(cstr_Block(seed), "gemini.circumlunar.space")) {
            d->siteIcon = 0x264a; /* gemini symbol */
        }
    }
#if 0
    for (int i = tmFirst_ColorId; i < max_ColorId; ++i) {
        const iColor tc = get_Color(i);
        printf("%02i: #%02x%02x%02x\n", i, tc.r, tc.g, tc.b);
    }
    printf("---\n");
#endif
}

void setFormat_GmDocument(iGmDocument *d, enum iGmDocumentFormat format) {
    d->format = format;
}

void setBanner_GmDocument(iGmDocument *d, enum iGmDocumentBanner type) {
    d->bannerType = type;
}

void setWidth_GmDocument(iGmDocument *d, int width) {
    d->size.x = width;
    doLayout_GmDocument_(d); /* TODO: just flag need-layout and do it later */
}

void redoLayout_GmDocument(iGmDocument *d) {
    doLayout_GmDocument_(d);
}

iLocalDef iBool isNormalizableSpace_(char ch) {
    return ch == ' ' || ch == '\t';
}

static void normalize_GmDocument(iGmDocument *d) {
    iString *normalized = new_String();
    iRangecc src = range_String(&d->source);
    iRangecc line = iNullRange;
    iBool isPreformat = iFalse;
    if (d->format == plainText_GmDocumentFormat) { // || isGopher_GmDocument_(d)) {
        isPreformat = iTrue; /* Cannot be turned off. */
    }
    const int preTabWidth = 4; /* TODO: user-configurable parameter */
    while (nextSplit_Rangecc(src, "\n", &line)) {
        if (isPreformat) {
            /* Replace any tab characters with spaces for visualization. */
            for (const char *ch = line.start; ch != line.end; ch++) {
                if (*ch == '\t') {
                    int column = ch - line.start;
                    int numSpaces = (column / preTabWidth + 1) * preTabWidth - column;
                    while (numSpaces-- > 0) {
                        appendCStrN_String(normalized, " ", 1);
                    }
                }
                else if (*ch != '\r') {
                    appendCStrN_String(normalized, ch, 1);
                }
            }
            appendCStr_String(normalized, "\n");
            if (lineType_GmDocument_(d, line) == preformatted_GmLineType) {
                isPreformat = iFalse;
            }
            continue;
        }
        if (lineType_GmDocument_(d, line) == preformatted_GmLineType) {
            isPreformat = iTrue;
            appendRange_String(normalized, line);
            appendCStr_String(normalized, "\n");
            continue;
        }
        iBool isPrevSpace = iFalse;
        int spaceCount = 0;
        for (const char *ch = line.start; ch != line.end; ch++) {
            char c = *ch;
            if (c == '\r') continue;
            if (isNormalizableSpace_(c)) {
                if (isPrevSpace) {
                    if (++spaceCount == 8) {
                        /* There are several consecutive space characters. The author likely
                           really wants to have some space here, so normalize to a tab stop. */
                        popBack_Block(&normalized->chars);
                        pushBack_Block(&normalized->chars, '\t');
                    }
                    continue; /* skip repeated spaces */
                }
                c = ' ';
                isPrevSpace = iTrue;
            }
            else {
                isPrevSpace = iFalse;
                spaceCount = 0;
            }
            appendCStrN_String(normalized, &c, 1);
        }
        appendCStr_String(normalized, "\n");
    }
    set_String(&d->source, collect_String(normalized));
}

void setUrl_GmDocument(iGmDocument *d, const iString *url) {
    set_String(&d->url, url);
    iUrl parts;
    init_Url(&parts, url);
    setRange_String(&d->localHost, parts.host);
}

void setSource_GmDocument(iGmDocument *d, const iString *source, int width) {
    set_String(&d->source, source);
    normalize_GmDocument(d);
    setWidth_GmDocument(d, width); /* re-do layout */
}

void render_GmDocument(const iGmDocument *d, iRangei visRangeY, iGmDocumentRenderFunc render,
                       void *context) {
    iBool isInside = iFalse;
    /* TODO: Check lookup table for quick starting position. */
    iConstForEach(Array, i, &d->layout) {
        const iGmRun *run = i.value;
        if (isInside) {
            if (top_Rect(run->visBounds) > visRangeY.end) {
                break;
            }
            render(context, run);
        }
        else if (bottom_Rect(run->visBounds) >= visRangeY.start) {
            isInside = iTrue;
            render(context, run);
        }
    }
}

iInt2 size_GmDocument(const iGmDocument *d) {
    return d->size;
}

enum iGmDocumentBanner bannerType_GmDocument(const iGmDocument *d) {
    return d->bannerType;
}

iBool hasSiteBanner_GmDocument(const iGmDocument *d) {
    return siteBanner_GmDocument(d) != NULL;
}

const iGmRun *siteBanner_GmDocument(const iGmDocument *d) {
    if (isEmpty_Array(&d->layout)) {
        return iFalse;
    }
    const iGmRun *first = constFront_Array(&d->layout);
    if (first->flags & siteBanner_GmRunFlag) {
        return first;
    }
    return NULL;
}

const iString *bannerText_GmDocument(const iGmDocument *d) {
    return &d->bannerText;
}

const iArray *headings_GmDocument(const iGmDocument *d) {
    return &d->headings;
}

const iString *source_GmDocument(const iGmDocument *d) {
    return &d->source;
}

iRangecc findText_GmDocument(const iGmDocument *d, const iString *text, const char *start) {
    const char * src      = constBegin_String(&d->source);
    const size_t startPos = (start ? start - src : 0);
    const size_t pos =
        indexOfCStrFromSc_String(&d->source, cstr_String(text), startPos, &iCaseInsensitive);
    if (pos == iInvalidPos) {
        return iNullRange;
    }
    return (iRangecc){ src + pos, src + pos + size_String(text) };
}

iRangecc findTextBefore_GmDocument(const iGmDocument *d, const iString *text, const char *before) {
    iRangecc found = iNullRange;
    const char *start = constBegin_String(&d->source);
    if (!before) before = constEnd_String(&d->source);
    while (start < before) {
        iRangecc range = findText_GmDocument(d, text, start);
        if (range.start == NULL || range.start >= before) break;
        found = range;
        start = range.end;
    }
    return found;
}

iGmRunRange findPreformattedRange_GmDocument(const iGmDocument *d, const iGmRun *run) {
    iAssert(run->preId);
    iGmRunRange range = { run, run };
    /* Find the beginning. */
    while (range.start > (const iGmRun *) constData_Array(&d->layout)) {
        const iGmRun *prev = range.start - 1;
        if (prev->preId != run->preId) break;
        range.start = prev;
    }
    /* Find the ending. */
    while (range.end < (const iGmRun *) constEnd_Array(&d->layout)) {
        if (range.end->preId != run->preId) break;
        range.end++;
    }
    return range;
}

const iGmRun *findRun_GmDocument(const iGmDocument *d, iInt2 pos) {
    /* TODO: Perf optimization likely needed; use a block map? */
    const iGmRun *last = NULL;
    iBool isFirstNonDecoration = iTrue;
    iConstForEach(Array, i, &d->layout) {
        const iGmRun *run = i.value;
        if (run->flags & decoration_GmRunFlag) continue;
        const iRangei span = ySpan_Rect(run->bounds);
        if (contains_Range(&span, pos.y)) {
            last = run;
            break;
        }
        if (isFirstNonDecoration && pos.y < top_Rect(run->bounds)) {
            last = run;
            break;
        }
        if (top_Rect(run->bounds) > pos.y) break; /* Below the point. */
        last = run;
        isFirstNonDecoration = iFalse;
    }
//    if (last) {
//        printf("found run at (%d,%d): %p [%s]\n", pos.x, pos.y, last, cstr_Rangecc(last->text));
//        fflush(stdout);
//    }
    return last;
}

const char *findLoc_GmDocument(const iGmDocument *d, iInt2 pos) {
    const iGmRun *run = findRun_GmDocument(d, pos);
    if (run) {
        return findLoc_GmRun(run, pos);
    }
    return NULL;
}

const iGmRun *findRunAtLoc_GmDocument(const iGmDocument *d, const char *textCStr) {
    iConstForEach(Array, i, &d->layout) {
        const iGmRun *run = i.value;
        if (run->flags & decoration_GmRunFlag) {
            continue;
        }
        if (contains_Range(&run->text, textCStr) || run->text.start > textCStr /* went past */) {
            return run;
        }
    }
    return NULL;
}

static const iGmLink *link_GmDocument_(const iGmDocument *d, iGmLinkId id) {
    if (id > 0 && id <= size_PtrArray(&d->links)) {
        return constAt_PtrArray(&d->links, id - 1);
    }
    return NULL;
}

const iString *linkUrl_GmDocument(const iGmDocument *d, iGmLinkId linkId) {
    const iGmLink *link = link_GmDocument_(d, linkId);
    return link ? &link->url : NULL;
}

iRangecc linkUrlRange_GmDocument(const iGmDocument *d, iGmLinkId linkId) {
    const iGmLink *link = link_GmDocument_(d, linkId);
    return link->urlRange;
}

int linkFlags_GmDocument(const iGmDocument *d, iGmLinkId linkId) {
    const iGmLink *link = link_GmDocument_(d, linkId);
    return link ? link->flags : 0;
}

const iTime *linkTime_GmDocument(const iGmDocument *d, iGmLinkId linkId) {
    const iGmLink *link = link_GmDocument_(d, linkId);
    return link ? &link->when : NULL;
}

iMediaId linkImage_GmDocument(const iGmDocument *d, iGmLinkId linkId) {
    return findLinkImage_Media(d->media, linkId);
}

iMediaId linkAudio_GmDocument(const iGmDocument *d, iGmLinkId linkId) {
    return findLinkAudio_Media(d->media, linkId);
}

enum iColorId linkColor_GmDocument(const iGmDocument *d, iGmLinkId linkId, enum iGmLinkPart part) {
    const iGmLink *link = link_GmDocument_(d, linkId);
    const int www_GmLinkFlag = http_GmLinkFlag | mailto_GmLinkFlag;
    if (link) {
        const iBool isUnsupported = (link->flags & supportedProtocol_GmLinkFlag) == 0;
        if (part == icon_GmLinkPart) {
            if (isUnsupported) {
                return tmBadLink_ColorId;
            }
            if (link->flags & visited_GmLinkFlag) {
                return link->flags & www_GmLinkFlag
                           ? tmHypertextLinkIconVisited_ColorId
                           : link->flags & gopher_GmLinkFlag ? tmGopherLinkIconVisited_ColorId
                                                             : tmLinkIconVisited_ColorId;
            }
            return link->flags & www_GmLinkFlag
                       ? tmHypertextLinkIcon_ColorId
                       : link->flags & gopher_GmLinkFlag ? tmGopherLinkIcon_ColorId
                                                         : tmLinkIcon_ColorId;
        }
        if (part == text_GmLinkPart) {
            return link->flags & www_GmLinkFlag
                       ? tmHypertextLinkText_ColorId
                       : link->flags & gopher_GmLinkFlag ? tmGopherLinkText_ColorId
                                                         : tmLinkText_ColorId;
        }
        if (part == textHover_GmLinkPart) {
            return link->flags & www_GmLinkFlag
                       ? tmHypertextLinkTextHover_ColorId
                       : link->flags & gopher_GmLinkFlag ? tmGopherLinkTextHover_ColorId
                                                         : tmLinkTextHover_ColorId;
        }
        if (part == domain_GmLinkPart) {
            if (isUnsupported) {
                return tmBadLink_ColorId;
            }
            return link->flags & www_GmLinkFlag
                       ? tmHypertextLinkDomain_ColorId
                       : link->flags & gopher_GmLinkFlag ? tmGopherLinkDomain_ColorId
                                                         : tmLinkDomain_ColorId;
        }
        if (part == visited_GmLinkPart) {
            return link->flags & www_GmLinkFlag
                       ? tmHypertextLinkLastVisitDate_ColorId
                       : link->flags & gopher_GmLinkFlag ? tmGopherLinkLastVisitDate_ColorId
                                                         : tmLinkLastVisitDate_ColorId;
        }
    }
    return tmLinkText_ColorId;
}

iBool isMediaLink_GmDocument(const iGmDocument *d, iGmLinkId linkId) {
    const iString *dstUrl = absoluteUrl_String(&d->url, linkUrl_GmDocument(d, linkId));
    const iRangecc scheme = urlScheme_String(dstUrl);
    if (equalCase_Rangecc(scheme, "gemini") || equalCase_Rangecc(scheme, "gopher") ||
        equalCase_Rangecc(scheme, "file") || willUseProxy_App(scheme)) {
        return (linkFlags_GmDocument(d, linkId) &
                (imageFileExtension_GmLinkFlag | audioFileExtension_GmLinkFlag)) != 0;
    }
    return iFalse;
}

const iString *title_GmDocument(const iGmDocument *d) {
    return &d->title;
}

iChar siteIcon_GmDocument(const iGmDocument *d) {
    return d->siteIcon;
}

const char *findLoc_GmRun(const iGmRun *d, iInt2 pos) {
    if (pos.y < top_Rect(d->bounds)) {
        return d->text.start;
    }
    const int x = pos.x - left_Rect(d->bounds);
    if (x <= 0) {
        return d->text.start;
    }
    const char *loc;
    tryAdvanceNoWrap_Text(d->font, d->text, x, &loc);
    return loc;
}

iDefineClass(GmDocument)
