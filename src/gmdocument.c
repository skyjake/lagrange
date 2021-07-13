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
#include "gmtypesetter.h"
#include "gmutil.h"
#include "lang.h"
#include "ui/color.h"
#include "ui/text.h"
#include "ui/metrics.h"
#include "ui/window.h"
#include "visited.h"
#include "bookmarks.h"
#include "app.h"
#include "defs.h"

#include <the_Foundation/intset.h>
#include <the_Foundation/ptrarray.h>
#include <the_Foundation/regexp.h>
#include <the_Foundation/stringset.h>

#include <ctype.h>

iBool isDark_GmDocumentTheme(enum iGmDocumentTheme d) {
    if (d == gray_GmDocumentTheme) {
        return isDark_ColorTheme(colorTheme_App());
    }
    return d == colorfulDark_GmDocumentTheme || d == black_GmDocumentTheme;
}

iDeclareType(GmLink)

struct Impl_GmLink {
    iString url; /* resolved */
    iRangecc urlRange; /* URL in the source */
    iRangecc labelRange; /* label in the source */
    iRangecc labelIcon; /* special icon defined in the label text */
    iTime when;
    int flags;
};

void init_GmLink(iGmLink *d) {
    init_String(&d->url);
    d->urlRange = iNullRange;
    d->labelRange = iNullRange;
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
    enum iSourceFormat format;
    iString   unormSource; /* unnormalized source */
    iString   source;      /* normalized source */
    iString   url;         /* for resolving relative links */
    iString   localHost;
    iInt2     size;
    iArray    layout; /* contents of source, laid out in document space */
    iPtrArray links;
    enum iGmDocumentBanner bannerType;
    iString   bannerText;
    iString   title; /* the first top-level title */
    iArray    headings;
    iArray    preMeta; /* metadata about preformatted blocks */
    uint32_t  themeSeed;
    iChar     siteIcon;
    iMedia *  media;
    iStringSet *openURLs; /* currently open URLs for highlighting links */
    iBool     isPaletteValid;
    iColor    palette[tmMax_ColorId]; /* copy of the color palette */
};

iDefineObjectConstruction(GmDocument)

static enum iGmLineType lineType_GmDocument_(const iGmDocument *d, const iRangecc line) {
    if (d->format == plainText_SourceFormat) {
        return text_GmLineType;
    }
    return lineType_Rangecc(line);
}

enum iGmLineType lineType_Rangecc(const iRangecc line) {
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

void trimLine_Rangecc(iRangecc *line, enum iGmLineType type, iBool normalize) {
    static const unsigned int skip[max_GmLineType] = { 0, 2, 3, 1, 1, 2, 3, 0 };
    line->start += skip[type];
    if (normalize || (type >= heading1_GmLineType && type <= heading3_GmLineType)) {
        trim_Rangecc(line);
    }
}

static int lastVisibleRunBottom_GmDocument_(const iGmDocument *d) {
    iReverseConstForEach(Array, i, &d->layout) {
        const iGmRun *run = i.value;
        if (isEmpty_Range(&run->text)) {
            continue;
        }
        return top_Rect(run->bounds) + height_Rect(run->bounds) * prefs_App()->lineSpacing;
    }
    return 0;
}

static iInt2 measurePreformattedBlock_GmDocument_(const iGmDocument *d, const char *start, int font,
                                                  iRangecc *contents, const char **endPos) {
    const iRangecc content = { start, constEnd_String(&d->source) };
    iRangecc line = iNullRange;
    nextSplit_Rangecc(content, "\n", &line);
    iAssert(startsWith_Rangecc(line, "```"));
    *contents = (iRangecc){ line.end + 1, line.end + 1 };
    while (nextSplit_Rangecc(content, "\n", &line)) {
        if (startsWith_Rangecc(line, "```")) {
            if (endPos) *endPos = line.end;
            break;
        }
        contents->end = line.end;
    }
    return measureRange_Text(font, *contents).bounds.size;
}

static iRangecc addLink_GmDocument_(iGmDocument *d, iRangecc line, iGmLinkId *linkId) {
    /* Returns the human-readable label of the link. */
    static iRegExp *pattern_;
    if (!pattern_) {
        pattern_ = newGemtextLink_RegExp();
    }
    iRegExpMatch m;
    init_RegExpMatch(&m);
    if (matchRange_RegExp(pattern_, line, &m)) {
        iGmLink *link = new_GmLink();
        link->urlRange = capturedRange_RegExpMatch(&m, 1);
        setRange_String(&link->url, link->urlRange);
        set_String(&link->url, canonicalUrl_String(absoluteUrl_String(&d->url, &link->url)));
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
            else if (equalCase_Rangecc(parts.scheme, "finger")) {
                link->flags |= finger_GmLinkFlag;
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
                if (contains_StringSet(d->openURLs, &link->url)) {
                    link->flags |= isOpen_GmLinkFlag;
                }
            }
        }
        pushBack_PtrArray(&d->links, link);
        *linkId = size_PtrArray(&d->links); /* index + 1 */
        iRangecc desc = capturedRange_RegExpMatch(&m, 2);
        trim_Rangecc(&desc);
        link->labelRange = desc;
        link->labelIcon = iNullRange;
        if (!isEmpty_Range(&desc)) {
            line = desc; /* Just show the description. */
            link->flags |= humanReadable_GmLinkFlag;
            /* Check for a custom icon. */
            if ((link->flags & gemini_GmLinkFlag && ~link->flags & remote_GmLinkFlag) ||
                link->flags & file_GmLinkFlag) {
                iChar icon = 0;
                int len = 0;
                if ((len = decodeBytes_MultibyteChar(desc.start, desc.end, &icon)) > 0) {
                    if (desc.start + len < desc.end &&
                        (isPictograph_Char(icon) || isEmoji_Char(icon) ||
                         /* TODO: Add range(s) of 0x2nnn symbols. */
                         icon == 0x2139 /* info */ ||
                         icon == 0x2191 /* up arrow */ ||
                         icon == 0x2022 /* bullet */ ||
                         icon == 0x2a2f /* close X */ ||
                         icon == 0x2b50) &&
                        !isFitzpatrickType_Char(icon)) {
                        link->flags |= iconFromLabel_GmLinkFlag;
                        link->labelIcon = (iRangecc){ desc.start, desc.start + len };
                        line.start += len;
                        trimStart_Rangecc(&line);
//                        printf("custom icon: %x (%s)\n", icon, cstr_Rangecc(link->labelIcon));
//                        fflush(stdout);
                    }
                }
            }
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

static iBool isGopher_GmDocument_(const iGmDocument *d) {
    const iRangecc scheme = urlScheme_String(&d->url);
    return (equalCase_Rangecc(scheme, "gopher") ||
            equalCase_Rangecc(scheme, "finger"));
}

static iBool isForcedMonospace_GmDocument_(const iGmDocument *d) {
    const iRangecc scheme = urlScheme_String(&d->url);
    if (equalCase_Rangecc(scheme, "gemini")) {
        return prefs_App()->monospaceGemini;
    }
    if (equalCase_Rangecc(scheme, "gopher") ||
        equalCase_Rangecc(scheme, "finger")) {
        return prefs_App()->monospaceGopher;
    }
    return iFalse;
}

static void linkContentWasLaidOut_GmDocument_(iGmDocument *d, const iGmMediaInfo *mediaInfo,
                                              uint16_t linkId) {
    iGmLink *link = at_PtrArray(&d->links, linkId - 1);
    link->flags |= content_GmLinkFlag;
    if (mediaInfo && mediaInfo->isPermanent) {
        link->flags |= permanent_GmLinkFlag;
    }
}

static iBool isNormalized_GmDocument_(const iGmDocument *d) {
    const iPrefs *prefs = prefs_App();
    if (d->format == plainText_SourceFormat) {
        return iTrue; /* tabs are always normalized in plain text */
    }
    if (startsWithCase_String(&d->url, "gemini:") && prefs->monospaceGemini) {
        return iFalse;
    }
    if (startsWithCase_String(&d->url, "gopher:") && prefs->monospaceGopher) {
        return iFalse;
    }
    return iTrue;
}

static enum iGmDocumentTheme currentTheme_(void) {
    return (isDark_ColorTheme(colorTheme_App()) ? prefs_App()->docThemeDark
                                                : prefs_App()->docThemeLight);
}

static void alignDecoration_GmRun_(iGmRun *run, iBool isCentered) {
    const iRect visBounds = visualBounds_Text(run->font, run->text);
    const int   visWidth  = width_Rect(visBounds);
    int         xAdjust   = 0;
    if (!isCentered) {
        /* Keep the icon aligned to the left edge. */
        const int alignWidth = width_Rect(run->visBounds) * 4 / 5;
        xAdjust -= left_Rect(visBounds);
        if (visWidth > alignWidth) {
            /* ...unless it's a wide icon, in which case move it to the left. */
            xAdjust -= visWidth - alignWidth;
        }
        else if (visWidth < alignWidth) {
            /* ...or a narrow icon, which needs to be centered but leave a gap. */
            xAdjust += (alignWidth - visWidth) / 2;
        }
    }
    else {
        /* Centered. */
        xAdjust += (width_Rect(run->visBounds) - visWidth) / 2;
    }
    run->visBounds.pos.x  += xAdjust;
    run->visBounds.size.x -= xAdjust;
}

static void updateOpenURLs_GmDocument_(iGmDocument *d) {
    if (d->openURLs) {
        iReleasePtr(&d->openURLs);
    }
    d->openURLs = listOpenURLs_App();
}

iDeclareType(RunTypesetter)
    
struct Impl_RunTypesetter {
    iArray layout;
    iGmRun run;
    iInt2  pos;
    float  lineHeightReduction;
    int    indent;
    int    layoutWidth;
    int    rightMargin;
    iBool  isWordWrapped;
    iBool  isPreformat;
    const int *fonts;
};
    
static void init_RunTypesetter_(iRunTypesetter *d) {
    iZap(*d);
    init_Array(&d->layout, sizeof(iGmRun));
}

static void deinit_RunTypesetter_(iRunTypesetter *d) {
    deinit_Array(&d->layout);
}

static void clear_RunTypesetter_(iRunTypesetter *d) {
    clear_Array(&d->layout);
}

static void commit_RunTypesetter_(iRunTypesetter *d, iGmDocument *doc) {
    pushBackN_Array(&doc->layout, constData_Array(&d->layout), size_Array(&d->layout));
    clear_RunTypesetter_(d);
}

static const int maxLedeLines_ = 10;

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

static iBool typesetOneLine_RunTypesetter_(iWrapText *wrap, iRangecc wrapRange, int origin, int advance) {
    iAssert(wrapRange.start <= wrapRange.end);
    trimEnd_Rangecc(&wrapRange);
//    printf("typeset: {%s}\n", cstr_Rangecc(wrapRange));
    iRunTypesetter *d = wrap->context;
    const int fontId = d->run.font;
    d->run.text = wrapRange;
    if (~d->run.flags & startOfLine_GmRunFlag && d->lineHeightReduction > 0.0f) {
        d->pos.y -= d->lineHeightReduction * lineHeight_Text(fontId);
    }
    d->run.bounds.pos = addX_I2(d->pos, origin + d->indent);
    const iInt2 dims = init_I2(advance, lineHeight_Text(fontId));
    iChangeFlags(d->run.flags, wide_GmRunFlag, (d->isPreformat && dims.x > d->layoutWidth));
    d->run.bounds.size.x    = iMax(wrap->maxWidth, dims.x) - origin; /* Extends to the right edge for selection. */
    d->run.bounds.size.y    = dims.y;
    d->run.visBounds        = d->run.bounds;
    d->run.visBounds.size.x = dims.x;
    pushBack_Array(&d->layout, &d->run);
    d->run.flags &= ~startOfLine_GmRunFlag;
    d->pos.y += lineHeight_Text(fontId) * prefs_App()->lineSpacing;
    return iTrue; /* continue to next wrapped line */
}

static void doLayout_GmDocument_(iGmDocument *d) {
    const iPrefs *prefs             = prefs_App();
    const iBool   isMono            = isForcedMonospace_GmDocument_(d);
    const iBool   isGopher          = isGopher_GmDocument_(d);
    const iBool   isNarrow          = d->size.x < 90 * gap_Text;
    const iBool   isVeryNarrow      = d->size.x <= 70 * gap_Text;
    const iBool   isExtremelyNarrow = d->size.x <= 60 * gap_Text;
    const iBool   isDarkBg          = isDark_GmDocumentTheme(
        isDark_ColorTheme(colorTheme_App()) ? prefs->docThemeDark : prefs->docThemeLight);
    /* TODO: Collect these parameters into a GmTheme. */
    const int fonts[max_GmLineType] = {
        isMono ? regularMonospace_FontId : paragraph_FontId,
        isMono ? regularMonospace_FontId : paragraph_FontId, /* bullet */
        preformatted_FontId,
        isMono ? regularMonospace_FontId : quote_FontId,
        heading1_FontId,
        heading2_FontId,
        heading3_FontId,
        isMono ? regularMonospace_FontId
        : ((isDarkBg && prefs->boldLinkDark) || (!isDarkBg && prefs->boldLinkLight))
            ? bold_FontId
            : paragraph_FontId,
    };
    float indents[max_GmLineType] = {
        5, 10, 5, isNarrow ? 5 : 10, 0, 0, 0, 5
    };
    if (isExtremelyNarrow) {
        /* Further reduce the margins. */
        indents[text_GmLineType] -= 5;
        indents[bullet_GmLineType] -= 5;
        indents[preformatted_GmLineType] -= 5;
    }
    if (isGopher) {
        indents[preformatted_GmLineType] = indents[text_GmLineType];
    }
    static const float topMargin[max_GmLineType] = {
        0.0f, 0.25f, 1.0f, 0.5f, 2.0f, 1.5f, 1.25f, 0.25f
    };
    static const float bottomMargin[max_GmLineType] = {
        0.0f, 0.25f, 1.0f, 0.5f, 0.5f, 0.5f, 0.5f, 0.25f
    };
    static const char *arrow           = rightArrowhead_Icon;
    static const char *envelope        = "\U0001f4e7";
    static const char *bullet          = "\u2022";
    static const char *folder          = "\U0001f4c1";
    static const char *globe           = "\U0001f310";
    static const char *quote           = "\u201c";
    static const char *magnifyingGlass = "\U0001f50d";
    static const char *pointingFinger  = "\U0001f449";
    clear_Array(&d->layout);
    clearLinks_GmDocument_(d);
    clear_Array(&d->headings);
    const iArray *oldPreMeta = collect_Array(copy_Array(&d->preMeta)); /* remember fold states */
    clear_Array(&d->preMeta);
    clear_String(&d->title);
    clear_String(&d->bannerText);
    if (d->size.x <= 0 || isEmpty_String(&d->source)) {
        return;
    }
    updateOpenURLs_GmDocument_(d);
    const iRangecc   content       = range_String(&d->source);
    iRangecc         contentLine   = iNullRange;
    iInt2            pos           = zero_I2();
    iBool            isFirstText   = prefs->bigFirstParagraph;
    iBool            addQuoteIcon  = prefs->quoteIcon;
    iBool            isPreformat   = iFalse;
    int              preFont       = preformatted_FontId;
    uint16_t         preId         = 0;
    iBool            enableIndents = iFalse;
    iBool            addSiteBanner = d->bannerType != none_GmDocumentBanner;
    const iBool      isNormalized  = isNormalized_GmDocument_(d);
    enum iGmLineType prevType      = text_GmLineType;
    enum iGmLineType prevNonBlankType = text_GmLineType;
    iBool            followsBlank  = iFalse;
    if (d->format == plainText_SourceFormat) {
        isPreformat = iTrue;
        isFirstText = iFalse;
    }
    while (nextSplit_Rangecc(content, "\n", &contentLine)) {
        iRangecc line = contentLine; /* `line` will be trimmed; modifying would confuse `nextSplit_Rangecc` */
        if (*line.end == '\r') {
            line.end--; /* trim CR always */
        }
        iGmRun run = { .color = white_ColorId };
        enum iGmLineType type;
        float indent = 0.0f;
        /* Detect the type of the line. */
        if (!isPreformat) {
            type = lineType_GmDocument_(d, line);
            if (contentLine.start == content.start) {
                prevType = type;
            }
            indent = indents[type];
            if (type == preformatted_GmLineType) {
                /* Begin a new preformatted block. */
                isPreformat = iTrue;
                const size_t preIndex = preId++;
                preFont = preformatted_FontId;
                /* Use a smaller font if the block contents are wide. */
                iGmPreMeta meta = { .bounds = line };
                meta.pixelRect.size = measurePreformattedBlock_GmDocument_(
                    d, line.start, preFont, &meta.contents, &meta.bounds.end);
                if (meta.pixelRect.size.x >
                    d->size.x - (enableIndents ? indents[preformatted_GmLineType] : 0) * gap_Text) {
                    preFont = preformattedSmall_FontId;
                    meta.pixelRect.size = measureRange_Text(preFont, meta.contents).bounds.size;
                }
                trimLine_Rangecc(&line, type, isNormalized);
                meta.altText = line; /* without the ``` */
                /* Reuse previous state. */
                if (preIndex < size_Array(oldPreMeta)) {
                    meta.flags = constValue_Array(oldPreMeta, preIndex, iGmPreMeta).flags &
                                 folded_GmPreMetaFlag;
                }
                else if (prefs->collapsePreOnLoad && !isGopher) {
                    meta.flags |= folded_GmPreMetaFlag;
                }
                pushBack_Array(&d->preMeta, &meta);
                continue;
            }
            else if (type == link_GmLineType) {
                line = addLink_GmDocument_(d, line, &run.linkId);
                if (!run.linkId) {
                    /* Invalid formatting. */
                    type = text_GmLineType;
                }
            }
            trimLine_Rangecc(&line, type, isNormalized);
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
            if (d->format == gemini_SourceFormat &&
                startsWithSc_Rangecc(line, "```", &iCaseSensitive)) {
                isPreformat = iFalse;
                addSiteBanner = iFalse; /* overrides the banner */
                continue;
            }
            run.preId = preId;
            run.font = (d->format == plainText_SourceFormat ? regularMonospace_FontId : preFont);
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
                pos.y += height_Rect(banner.visBounds) +
                         lineHeight_Text(paragraph_FontId) * prefs->lineSpacing;
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
            pos.y += lineHeight_Text(run.font) * prefs->lineSpacing;
            prevType = type;
            if (type != quote_GmLineType) {
                addQuoteIcon = prefs->quoteIcon;
            }
            followsBlank = iTrue;
            continue;
        }
        /* Begin indenting after the first preformatted block. */
        if (type != preformatted_GmLineType || prevType != preformatted_GmLineType) {
            enableIndents = iTrue;
        }
        /* Gopher: Always indent preformatted blocks. */
        if (isGopher && type == preformatted_GmLineType) {
            enableIndents = iTrue;
        }
        if (!enableIndents) {
            indent = 0;
        }
        /* Check the margin vs. previous run. */
        if (!isPreformat || (prevType != preformatted_GmLineType)) {
            int required =
                iMax(topMargin[type], bottomMargin[prevType]) * lineHeight_Text(paragraph_FontId);
            if (type == link_GmLineType && prevNonBlankType == link_GmLineType && followsBlank) {
                required = 1.25f * lineHeight_Text(paragraph_FontId);
            }
            if (type == quote_GmLineType && prevType == quote_GmLineType) {
                /* No margin between consecutive quote lines. */
                required = 0;
            }
            if (isEmpty_Array(&d->layout)) {
                required = 0; /* top of document */
            }
            required *= prefs->lineSpacing;
            int delta = pos.y - lastVisibleRunBottom_GmDocument_(d);
            if (delta < required) {
                pos.y += (required - delta);
            }
        }
        /* Folded blocks are represented by a single run with the alt text. */
        if (isPreformat && d->format != plainText_SourceFormat) {
            const iGmPreMeta *meta = constAt_Array(&d->preMeta, preId - 1);
            if (meta->flags & folded_GmPreMetaFlag) {
                const iBool isBlank = isEmpty_Range(&meta->altText);
                iGmRun altText = { .font  = paragraph_FontId,
                                   .flags = (isBlank ? decoration_GmRunFlag : 0) | altText_GmRunFlag };
                const iInt2 margin = preRunMargin_GmDocument(d, 0);
                altText.color      = tmQuote_ColorId;
                altText.text       = isBlank ? range_Lang(range_CStr("doc.pre.nocaption"))
                                             : meta->altText;
                iInt2 size = measureWrapRange_Text(altText.font, d->size.x - 2 * margin.x,
                                                   altText.text).bounds.size;
                altText.bounds = altText.visBounds = init_Rect(pos.x, pos.y, d->size.x,
                                                               size.y + 2 * margin.y);
                altText.preId = preId;
                pushBack_Array(&d->layout, &altText);
                pos.y += height_Rect(altText.bounds);
                contentLine = meta->bounds; /* Skip the whole thing. */
                isPreformat = iFalse;
                prevType = preformatted_GmLineType;
                continue;
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
            /* TODO: Literata bullet is broken? */
            iGmRun bulRun = run;
            if (prefs->font == literata_TextFont) {
                /* Something wrong this the glyph in Literata, looks cropped. */
                bulRun.font = defaultContentRegular_FontId;
            }
            bulRun.color = tmQuote_ColorId;
            bulRun.visBounds.pos = addX_I2(pos, (indents[text_GmLineType] - 0.55f) * gap_Text);
            bulRun.visBounds.size =
                init_I2((indents[bullet_GmLineType] - indents[text_GmLineType]) * gap_Text,
                        lineHeight_Text(bulRun.font));
            //            bulRun.visBounds.pos.x -= 4 * gap_Text - width_Rect(bulRun.visBounds) / 2;
            bulRun.bounds = zero_Rect(); /* just visual */
            bulRun.text   = range_CStr(bullet);
            bulRun.flags |= decoration_GmRunFlag;
            alignDecoration_GmRun_(&bulRun, iTrue);
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
            quoteRun.visBounds.size = measure_Text(quoteRun.font, quote).bounds.size;
            quoteRun.visBounds.pos =
                add_I2(pos,
                       init_I2((indents[quote_GmLineType] - 5) * gap_Text,
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
                                             : link->flags & finger_GmLinkFlag ? pointingFinger
                                             : link->flags & mailto_GmLinkFlag ? envelope
                                             : link->flags & remote_GmLinkFlag ? globe
                                                                               : arrow);
            /* Custom link icon is shown on local Gemini links only. */
            if (!isEmpty_Range(&link->labelIcon)) {
                icon.text = link->labelIcon;
            }
            /* TODO: List bullets needs the same centering logic. */
            /* Special exception for the tiny bullet operator. */
            icon.font = equal_Rangecc(link->labelIcon, "\u2219") ? regularMonospace_FontId
                                                                 : regular_FontId;
            alignDecoration_GmRun_(&icon, iFalse);
            icon.color = linkColor_GmDocument(d, run.linkId, icon_GmLinkPart);
            icon.flags |= decoration_GmRunFlag;
            pushBack_Array(&d->layout, &icon);
        }
        run.color = colors[type];
        if (d->format == plainText_SourceFormat) {
            run.color = colors[text_GmLineType];
        }
        /* Special formatting for the first paragraph (e.g., subtitle, introduction, or lede). */
//        int bigCount = 0;
        iBool isLedeParagraph = iFalse;
        if (type == text_GmLineType && isFirstText) {
            if (!isMono) run.font = firstParagraph_FontId;
            run.color = tmFirstParagraph_ColorId;
//            bigCount = 15; /* max lines -- what if the whole document is one paragraph? */
            isLedeParagraph = iTrue;
            isFirstText = iFalse;
        }
        else if (type != heading1_GmLineType) {
            isFirstText = iFalse;
        }
        if (isPreformat && d->format != plainText_SourceFormat) {
            /* Remember the top left coordinates of the block (first line of block). */
            iGmPreMeta *meta = at_Array(&d->preMeta, preId - 1);
            if (~meta->flags & topLeft_GmPreMetaFlag) {
                meta->pixelRect.pos = pos;
                meta->flags |= topLeft_GmPreMetaFlag;
            }
        }
        iAssert(!isEmpty_Range(&line)); /* must have something at this point */
        /* Typeset the paragraph. */ {
            iRunTypesetter rts;
            init_RunTypesetter_(&rts);
            rts.run           = run;
            rts.pos           = pos;
            rts.fonts         = fonts;
            rts.isWordWrapped = (d->format == plainText_SourceFormat ? prefs->plainTextWrap
                                                                     : !isPreformat);
            rts.isPreformat   = isPreformat;
            rts.layoutWidth   = d->size.x;
            rts.indent        = indent * gap_Text;
            /* The right margin is used for balancing lines horizontally. */
            if (isVeryNarrow) {
                rts.rightMargin = 0;
            }
            else {
                rts.rightMargin = (type == text_GmLineType || type == bullet_GmLineType ||
                                           type == quote_GmLineType
                                       ? 4 : 0) * gap_Text;
            }
            if (!isMono) {
                /* Upper-level headings are typeset a bit tighter. */
                if (type == heading1_GmLineType) {
                    rts.lineHeightReduction = 0.10f;
                }
                else if (type == heading2_GmLineType) {
                    rts.lineHeightReduction = 0.06f;
                }
                /* Visited links are never bold. */
                if (run.linkId && linkFlags_GmDocument(d, run.linkId) & visited_GmLinkFlag) {
                    rts.run.font = paragraph_FontId;
                }
            }
            if (!prefs->quoteIcon && type == quote_GmLineType) {
                rts.run.flags |= quoteBorder_GmRunFlag;
            }
            for (;;) { /* may need to retry */
                rts.run.flags |= startOfLine_GmRunFlag;
                measure_WrapText(&(iWrapText){ .text     = line,
                                               .maxWidth = rts.isWordWrapped
                                                               ? d->size.x - run.bounds.pos.x -
                                                                     rts.indent - rts.rightMargin
                                                               : 0 /* unlimited */,
                                               .mode     = word_WrapTextMode,
                                               .wrapFunc = typesetOneLine_RunTypesetter_,
                                               .context  = &rts },
                                 run.font);
                if (!isLedeParagraph || size_Array(&rts.layout) <= maxLedeLines_) {
                    commit_RunTypesetter_(&rts, d);
                    break;
                }
                clear_RunTypesetter_(&rts);
                rts.pos         = pos;
                rts.run.font    = rts.fonts[text_GmLineType];
                rts.run.color   = colors   [text_GmLineType];
                isLedeParagraph = iFalse;
            }
            pos = rts.pos;
            deinit_RunTypesetter_(&rts);
        }
        /* Flag the end of line, too. */
        ((iGmRun *) back_Array(&d->layout))->flags |= endOfLine_GmRunFlag;
        /* Image or audio content. */
        if (type == link_GmLineType) {
            const iMediaId imageId = findLinkImage_Media(d->media, run.linkId);
            const iMediaId audioId = !imageId ? findLinkAudio_Media(d->media, run.linkId) : 0;
            const iMediaId downloadId = !imageId && !audioId ? findLinkDownload_Media(d->media, run.linkId) : 0;
            if (imageId) {
                iGmMediaInfo img;
                imageInfo_Media(d->media, imageId, &img);
                const iInt2 imgSize = imageSize_Media(d->media, imageId);
                linkContentWasLaidOut_GmDocument_(d, &img, run.linkId);
                const int margin = lineHeight_Text(paragraph_FontId) / 2;
                pos.y += margin;
                run.bounds.pos = pos;
                run.bounds.size.x = d->size.x;
                const float aspect = (float) imgSize.y / (float) imgSize.x;
                run.bounds.size.y = d->size.x * aspect;
                run.visBounds = run.bounds;
                const iInt2 maxSize = mulf_I2(imgSize, get_Window()->pixelRatio);
                if (width_Rect(run.visBounds) > maxSize.x) {
                    /* Don't scale the image up. */
                    run.visBounds.size.y =
                        run.visBounds.size.y * maxSize.x / width_Rect(run.visBounds);
                    run.visBounds.size.x = maxSize.x;
                    run.visBounds.pos.x  = run.bounds.size.x / 2 - width_Rect(run.visBounds) / 2;
                    run.bounds.size.y    = run.visBounds.size.y;
                }
                run.text      = iNullRange;
                run.font      = 0;
                run.color     = 0;
                run.mediaType = image_GmRunMediaType;
                run.mediaId   = imageId;
                pushBack_Array(&d->layout, &run);
                pos.y += run.bounds.size.y + margin;
            }
            else if (audioId) {
                iGmMediaInfo info;
                audioInfo_Media(d->media, audioId, &info);
                linkContentWasLaidOut_GmDocument_(d, &info, run.linkId);
                const int margin = lineHeight_Text(paragraph_FontId) / 2;
                pos.y += margin;
                run.bounds.pos    = pos;
                run.bounds.size.x = d->size.x;
                run.bounds.size.y = lineHeight_Text(uiContent_FontId) + 3 * gap_UI;
                run.visBounds     = run.bounds;
                run.text          = iNullRange;
                run.color         = 0;
                run.mediaType     = audio_GmRunMediaType;
                run.mediaId       = audioId;
                pushBack_Array(&d->layout, &run);
                pos.y += run.bounds.size.y + margin;
            }
            else if (downloadId) {
                iGmMediaInfo info;
                downloadInfo_Media(d->media, downloadId, &info);
                linkContentWasLaidOut_GmDocument_(d, &info, run.linkId);
                const int margin = lineHeight_Text(paragraph_FontId) / 2;
                pos.y += margin;
                run.bounds.pos    = pos;
                run.bounds.size.x = d->size.x;
                run.bounds.size.y = 2 * lineHeight_Text(uiContent_FontId) + 4 * gap_UI;
                run.visBounds     = run.bounds;
                run.text          = iNullRange;
                run.color         = 0;
                run.mediaType     = download_GmRunMediaType;
                run.mediaId       = downloadId;
                pushBack_Array(&d->layout, &run);
                pos.y += run.bounds.size.y + margin;
            }
        }
        prevType = type;
        prevNonBlankType = type;
        followsBlank = iFalse;
    }
#if 0
    /* Footer. */
    if (siteBanner_GmDocument(d)) {
        iGmRun footer = { .flags = decoration_GmRunFlag | footer_GmRunFlag };
        footer.visBounds = (iRect){ pos, init_I2(d->size.x, lineHeight_Text(banner_FontId) * 1) };
        pushBack_Array(&d->layout, &footer);
        pos.y += footer.visBounds.size.y;
    }
#endif
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
    printf("[GmDocument] layout size: %zu runs (%zu bytes)\n",
           size_Array(&d->layout), size_Array(&d->layout) * sizeof(iGmRun));        
}

void init_GmDocument(iGmDocument *d) {
    d->format = gemini_SourceFormat;
    init_String(&d->unormSource);
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
    init_Array(&d->preMeta, sizeof(iGmPreMeta));
    d->themeSeed = 0;
    d->siteIcon = 0;
    d->media = new_Media();
    d->openURLs = NULL;
    d->isPaletteValid = iFalse;
    iZap(d->palette);
}

void deinit_GmDocument(iGmDocument *d) {
    iReleasePtr(&d->openURLs);
    delete_Media(d->media);
    deinit_String(&d->bannerText);
    deinit_String(&d->title);
    clearLinks_GmDocument_(d);
    deinit_PtrArray(&d->links);
    deinit_Array(&d->preMeta);
    deinit_Array(&d->headings);
    deinit_Array(&d->layout);
    deinit_String(&d->localHost);
    deinit_String(&d->url);
    deinit_String(&d->source);
    deinit_String(&d->unormSource);
}

iMedia *media_GmDocument(iGmDocument *d) {
    return d->media;
}

const iMedia *constMedia_GmDocument(const iGmDocument *d) {
    return d->media;
}

const iString *url_GmDocument(const iGmDocument *d) {
    return &d->url;
}

#if 0
void reset_GmDocument(iGmDocument *d) {
    clear_Media(d->media);
    clearLinks_GmDocument_(d);
    clear_Array(&d->layout);
    clear_Array(&d->headings);
    clear_Array(&d->preMeta);
    clear_String(&d->url);
    clear_String(&d->localHost);
    clear_String(&d->source);
    clear_String(&d->unormSource);
    d->themeSeed = 0;
}
#endif

static void setDerivedThemeColors_(enum iGmDocumentTheme theme) {
    set_Color(tmQuoteIcon_ColorId,
              mix_Color(get_Color(tmQuote_ColorId), get_Color(tmBackground_ColorId), 0.55f));
    set_Color(tmBannerSideTitle_ColorId,
              mix_Color(get_Color(tmBannerTitle_ColorId),
                        get_Color(tmBackground_ColorId),
                        theme == colorfulDark_GmDocumentTheme ? 0.55f : 0));
    set_Color(tmBackgroundAltText_ColorId,
              mix_Color(get_Color(tmQuoteIcon_ColorId), get_Color(tmBackground_ColorId), 0.85f));
    set_Color(tmBackgroundOpenLink_ColorId,
              mix_Color(get_Color(tmLinkText_ColorId), get_Color(tmBackground_ColorId), 0.90f));
    set_Color(tmFrameOpenLink_ColorId,
              mix_Color(get_Color(tmLinkText_ColorId), get_Color(tmBackground_ColorId), 0.75f));
    if (theme == colorfulDark_GmDocumentTheme) {
        /* Ensure paragraph text and link text aren't too similarly colored. */
        if (delta_Color(get_Color(tmLinkText_ColorId), get_Color(tmParagraph_ColorId)) < 100) {
            setHsl_Color(tmParagraph_ColorId,
                         addSatLum_HSLColor(get_HSLColor(tmParagraph_ColorId), 0.3f, -0.025f));
        }
    }
    set_Color(tmLinkCustomIconVisited_ColorId,
              mix_Color(get_Color(tmLinkIconVisited_ColorId), get_Color(tmLinkIcon_ColorId), 0.20f));
}

static void updateIconBasedOnUrl_GmDocument_(iGmDocument *d) {
    const iChar userIcon = siteIcon_Bookmarks(bookmarks_App(), &d->url);
    if (userIcon) {
        d->siteIcon = userIcon;
    }
}

void setThemeSeed_GmDocument(iGmDocument *d, const iBlock *seed) {
    const iPrefs *        prefs = prefs_App();
    enum iGmDocumentTheme theme = currentTheme_();
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
            setHsl_Color(tmBannerIcon_ColorId, addSatLum_HSLColor(base, 0, -0.4f));
            setHsl_Color(tmBannerTitle_ColorId, addSatLum_HSLColor(base, 0, -0.4f));
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
            if (isDark_ColorTheme(colorTheme_App())) {
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
            else {
                set_Color(tmBackground_ColorId, mix_Color(get_Color(gray75_ColorId), get_Color(gray50_ColorId), 0.33f));
                set_Color(tmFirstParagraph_ColorId, mix_Color(get_Color(gray25_ColorId), get_Color(black_ColorId), 0.5f));
                set_Color(tmParagraph_ColorId, get_Color(black_ColorId));
                set_Color(tmQuote_ColorId, get_Color(teal_ColorId));
                set_Color(tmPreformatted_ColorId, get_Color(brown_ColorId));
                set_Color(tmHeading1_ColorId, get_Color(brown_ColorId));
                set_Color(tmHeading2_ColorId, mix_Color(get_Color(brown_ColorId), get_Color(black_ColorId), 0.5f));
                set_Color(tmHeading3_ColorId, get_Color(black_ColorId));
                set_Color(tmBannerBackground_ColorId, mix_Color(get_Color(gray75_ColorId), get_Color(gray50_ColorId), 0.12f));
                set_Color(tmBannerTitle_ColorId, get_Color(teal_ColorId));
                set_Color(tmBannerIcon_ColorId, get_Color(teal_ColorId));
                set_Color(tmLinkIconVisited_ColorId, mix_Color(get_Color(cyan_ColorId), get_Color(black_ColorId), 0.20f));
                set_Color(tmLinkDomain_ColorId, mix_Color(get_Color(cyan_ColorId), get_Color(black_ColorId), 0.33f));
                set_Color(tmHypertextLinkIconVisited_ColorId, mix_Color(get_Color(orange_ColorId), get_Color(black_ColorId), 0.33f));
                set_Color(tmHypertextLinkDomain_ColorId, mix_Color(get_Color(orange_ColorId), get_Color(black_ColorId), 0.33f));
            }
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
            set_Color(tmLinkText_ColorId, get_Color(tmHeading2_ColorId));
            set_Color(tmHypertextLinkText_ColorId, get_Color(tmHeading2_ColorId));
            set_Color(tmGopherLinkText_ColorId, get_Color(tmHeading2_ColorId));
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
        static const float hues[] = { 5, 25, 40, 56, 80 + 15, 120, 160, 180, 208, 231, 270, 324 + 10 };
        static const struct {
            int index[2];
        } altHues[iElemCount(hues)] = {
            { 2, 4 },  /* red */
            { 8, 3 },  /* reddish orange */
            { 7, 9 },  /* yellowish orange */
            { 5, 7 },  /* yellow */
            { 6, 2 },  /* greenish yellow */
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

//            printf("primHue: %zu  alts: %d %d  isDarkBgSat: %d\n",
//                   primIndex,
//                   altHues[primIndex].index[altIndex[0]],
//                   altHues[primIndex].index[altIndex[1]],
//                   isDarkBgSat);

            const float titleLum = 0.2f * ((d->themeSeed >> 17) & 0x7) / 7.0f;
            setHsl_Color(tmHeading1_ColorId, setLum_HSLColor(altBase, titleLum + 0.80f));
            setHsl_Color(tmHeading2_ColorId, setLum_HSLColor(altBase, titleLum + 0.70f));
            setHsl_Color(tmHeading3_ColorId, setLum_HSLColor(altBase, titleLum + 0.60f));

//            printf("titleLum: %f\n", titleLum);

            setHsl_Color(tmParagraph_ColorId, addSatLum_HSLColor(base, 0.1f, 0.6f));

//            printf("heading3: %d,%d,%d\n", get_Color(tmHeading3_ColorId).r,  get_Color(tmHeading3_ColorId).g,  get_Color(tmHeading3_ColorId).b);
//            printf("paragr  : %d,%d,%d\n", get_Color(tmParagraph_ColorId).r, get_Color(tmParagraph_ColorId).g, get_Color(tmParagraph_ColorId).b);
//            printf("delta   : %d\n", delta_Color(get_Color(tmHeading3_ColorId), get_Color(tmParagraph_ColorId)));

            if (delta_Color(get_Color(tmHeading3_ColorId), get_Color(tmParagraph_ColorId)) <= 80) {
                /* Smallest headings may be too close to body text color. */
                setHsl_Color(tmHeading2_ColorId, addSatLum_HSLColor(get_HSLColor(tmHeading2_ColorId), 0.4f, -0.12f));
                setHsl_Color(tmHeading3_ColorId, addSatLum_HSLColor(get_HSLColor(tmHeading3_ColorId), 0.4f, -0.2f));
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
            setHsl_Color(
                tmBannerBackground_ColorId,
                addSatLum_HSLColor(base, 0, isDark_ColorTheme(colorTheme_App()) ? -0.04f : 0.06f));
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
        else if (theme == black_GmDocumentTheme ||
                 (theme == gray_GmDocumentTheme && isDark_ColorTheme(colorTheme_App()))) {
            const float primHue        = hues[primIndex];
            const iHSLColor primBright = { primHue, 1, 0.6f, 1 };
            const iHSLColor primDim    = { primHue, 1, normLum[primIndex] + (theme == gray_GmDocumentTheme ? 0.0f : -0.25f), 1};
            const iHSLColor altBright  = { altHue, 1, normLum[altIndex[0]] + (theme == gray_GmDocumentTheme ? 0.1f : 0.0f), 1 };
            setHsl_Color(tmQuote_ColorId, altBright);
            setHsl_Color(tmPreformatted_ColorId, altBright);
            setHsl_Color(tmHeading1_ColorId, primBright);
            set_Color(tmHeading2_ColorId, mix_Color(get_Color(tmHeading1_ColorId), get_Color(white_ColorId), 0.66f));
            setHsl_Color(tmBannerTitle_ColorId, primDim);
            setHsl_Color(tmBannerIcon_ColorId, primDim);
        }
        else if (theme == gray_GmDocumentTheme) { /* Light gray. */
            const float primHue        = hues[primIndex];
            const iHSLColor primBright = { primHue, 1, 0.3f, 1 };
            const iHSLColor primDim    = { primHue, 1, normLum[primIndex] * 0.33f, 1 };
            const iHSLColor altBright  = { altHue, 1, normLum[altIndex[0]] * 0.27f, 1 };
            setHsl_Color(tmQuote_ColorId, altBright);
            setHsl_Color(tmPreformatted_ColorId, altBright);
            setHsl_Color(tmHeading1_ColorId, primBright);
            set_Color(tmHeading2_ColorId, mix_Color(get_Color(tmHeading1_ColorId), get_Color(black_ColorId), 0.4f));
            setHsl_Color(tmBannerTitle_ColorId, primDim);
            setHsl_Color(tmBannerIcon_ColorId, primDim);
        }
        /* Tone down the link colors a bit because bold white is quite strong to look at. */
        if (isDark_GmDocumentTheme(theme) || theme == white_GmDocumentTheme) {
            iHSLColor base = { hues[primIndex], 1.0f, normLum[primIndex], 1.0f };
            if (theme == black_GmDocumentTheme || theme == gray_GmDocumentTheme) {
                setHsl_Color(tmLinkText_ColorId,
                             addSatLum_HSLColor(get_HSLColor(tmLinkText_ColorId), 0.0f, -0.15f));
            }
            else {
                /* Tinted with base color. */
                set_Color(tmLinkText_ColorId, mix_Color(get_Color(tmLinkText_ColorId),
                                                        rgb_HSLColor(base), 0.25f));
            }
            set_Color(tmHypertextLinkText_ColorId, get_Color(tmLinkText_ColorId));
            set_Color(tmGopherLinkText_ColorId, get_Color(tmLinkText_ColorId));
        }
        /* Adjust colors based on light/dark mode. */
        for (int i = tmFirst_ColorId; i < max_ColorId; i++) {
            iHSLColor color = hsl_Color(get_Color(i));
            if (theme == colorfulDark_GmDocumentTheme) { /* dark mode */
                if (!isLink_ColorId(i)) {
                    if (isDarkBgSat) {
                        /* Saturate background, desaturate text. */
                        if (isBackground_ColorId(i)) {
                            if (primIndex == pink_Hue) {
                                color.sat = (4 * color.sat + 1) / 5;
                            }
                            else if (primIndex != green_Hue) {
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
                            if (primIndex == pink_Hue) {
                                color.sat *= 0.5f;
                            }
                            if (primIndex == greenishYellow_Hue || primIndex == green_Hue) {
                                color.sat *= 0.333f;
                            }
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
        updateIconBasedOnUrl_GmDocument_(d);
    }
#if 0
    for (int i = tmFirst_ColorId; i < max_ColorId; ++i) {
        const iColor tc = get_Color(i);
        printf("%02i: #%02x%02x%02x\n", i, tc.r, tc.g, tc.b);
    }
    printf("---\n");
#endif
    /* Color functions operate on the global palette for convenience, but we may need to switch
       palettes on the fly if more than one GmDocument is being displayed simultaneously. */
    memcpy(d->palette, get_Root()->tmPalette, sizeof(d->palette));
    d->isPaletteValid = iTrue;
}

void makePaletteGlobal_GmDocument(const iGmDocument *d) {
    if (d->isPaletteValid) {
        memcpy(get_Root()->tmPalette, d->palette, sizeof(d->palette));
    }
}

void invalidatePalette_GmDocument(iGmDocument *d) {
    d->isPaletteValid = iFalse;
}

void setFormat_GmDocument(iGmDocument *d, enum iSourceFormat format) {
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

static void markLinkRunsVisited_GmDocument_(iGmDocument *d, const iIntSet *linkIds) {
    iForEach(Array, r, &d->layout) {
        iGmRun *run = r.value;
        if (run->linkId && !run->mediaId && contains_IntSet(linkIds, run->linkId)) {
            if (run->font == bold_FontId) {
                run->font = paragraph_FontId;
            }
            else if (run->flags & decoration_GmRunFlag) {
                run->color = linkColor_GmDocument(d, run->linkId, icon_GmLinkPart);
            }
        }
    }
}

iBool updateOpenURLs_GmDocument(iGmDocument *d) {
    iBool wasChanged = iFalse;
    updateOpenURLs_GmDocument_(d);
    iIntSet linkIds;
    init_IntSet(&linkIds);
    iForEach(PtrArray, i, &d->links) {
        iGmLink *link = i.ptr;
        if (!equal_String(&link->url, &d->url)) {
            const iBool isOpen = contains_StringSet(d->openURLs, &link->url);
            if (isOpen ^ ((link->flags & isOpen_GmLinkFlag) != 0)) {
                iChangeFlags(link->flags, isOpen_GmLinkFlag, isOpen);
                if (isOpen) {
                    link->flags |= visited_GmLinkFlag;
                    insert_IntSet(&linkIds, index_PtrArrayIterator(&i) + 1);
                }
                wasChanged = iTrue;
            }
        }
    }
    markLinkRunsVisited_GmDocument_(d, &linkIds);
    deinit_IntSet(&linkIds);
    return wasChanged;
}

iLocalDef iBool isNormalizableSpace_(char ch) {
    return ch == ' ' || ch == '\t';
}

static void normalize_GmDocument(iGmDocument *d) {
    iString *normalized = new_String();
    iRangecc src = range_String(&d->source);
    /* Check for a BOM. In UTF-8, the BOM can just be skipped if present. */ {
        iChar ch = 0;
        decodeBytes_MultibyteChar(src.start, src.end, &ch);
        if (ch == 0xfeff) /* zero-width non-breaking space */ {
            src.start += 3;
        }
    }
    iRangecc line = iNullRange;
    iBool isPreformat = iFalse;
    if (d->format == plainText_SourceFormat) {
        isPreformat = iTrue; /* Cannot be turned off. */
    }
    const int preTabWidth = 4; /* TODO: user-configurable parameter */
    iBool wasNormalized = iFalse;
    iBool hasTabs = iFalse;
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
                    hasTabs = iTrue;
                    wasNormalized = iTrue;
                }
                else if (*ch != '\v') {
                    appendCStrN_String(normalized, ch, 1);
                }
                else {
                    hasTabs = iTrue;
                    wasNormalized = iTrue;
                }
            }
            appendCStr_String(normalized, "\n");
            if (d->format == gemini_SourceFormat &&
                lineType_GmDocument_(d, line) == preformatted_GmLineType) {
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
            if (c == '\v') {
                wasNormalized = iTrue;
                continue;
            }
            if (isNormalizableSpace_(c)) {
                if (isPrevSpace) {
                    if (++spaceCount == 8) {
                        /* There are several consecutive space characters. The author likely
                           really wants to have some space here, so normalize to a tab stop. */
                        popBack_Block(&normalized->chars);
                        pushBack_Block(&normalized->chars, '\t');
                    }
                    wasNormalized = iTrue;
                    continue; /* skip repeated spaces */
                }
                if (c != ' ') {
                    c = ' ';
                    wasNormalized = iTrue;
                }
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
    printf("hasTabs: %d\n", hasTabs);
    printf("wasNormalized: %d\n", wasNormalized);
    fflush(stdout);
    set_String(&d->source, collect_String(normalized));
    //normalize_String(&d->source); /* NFC */
    printf("orig:%zu norm:%zu\n", size_String(&d->unormSource), size_String(&d->source));
    /* normalized source has an extra newline at the end */
//    iAssert(wasNormalized || equal_String(&d->unormSource, &d->source));
}

void setUrl_GmDocument(iGmDocument *d, const iString *url) {
    url = canonicalUrl_String(url);
    set_String(&d->url, url);
    iUrl parts;
    init_Url(&parts, url);
    setRange_String(&d->localHost, parts.host);
    updateIconBasedOnUrl_GmDocument_(d);
}

void setSource_GmDocument(iGmDocument *d, const iString *source, int width,
                          enum iGmDocumentUpdate updateType) {
    printf("[GmDocument] source update (%zu bytes), width:%d, final:%d\n",
           size_String(source), width, updateType == final_GmDocumentUpdate);
    if (size_String(source) == size_String(&d->unormSource)) {
        iAssert(equal_String(source, &d->unormSource));
        printf("[GmDocument] source is unchanged!\n");
        return; /* Nothing to do. */
    }
    set_String(&d->unormSource, source);
    /* Normalize. */
    set_String(&d->source, &d->unormSource);
    if (isNormalized_GmDocument_(d)) {
        normalize_GmDocument(d);
    }
    setWidth_GmDocument(d, width); /* re-do layout */
}

void foldPre_GmDocument(iGmDocument *d, uint16_t preId) {
    if (preId > 0 && preId <= size_Array(&d->preMeta)) {
        iGmPreMeta *meta = at_Array(&d->preMeta, preId - 1);
        meta->flags ^= folded_GmPreMetaFlag;
    }
}

void updateVisitedLinks_GmDocument(iGmDocument *d) {
    iIntSet linkIds;
    init_IntSet(&linkIds);
    iForEach(PtrArray, i, &d->links) {
        iGmLink *link = i.ptr;
        if (~link->flags & visited_GmLinkFlag) {
            iTime visitTime = urlVisitTime_Visited(visited_App(), &link->url);
            if (isValid_Time(&visitTime)) {
                link->flags |= visited_GmLinkFlag;
                insert_IntSet(&linkIds, index_PtrArrayIterator(&i) + 1);
            }
        }
    }
    markLinkRunsVisited_GmDocument_(d, &linkIds);
    deinit_IntSet(&linkIds);
}

const iGmPreMeta *preMeta_GmDocument(const iGmDocument *d, uint16_t preId) {
    if (preId > 0 && preId <= size_Array(&d->preMeta)) {
        return constAt_Array(&d->preMeta, preId - 1);
    }
    return NULL;
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

static iBool isValidRun_GmDocument_(const iGmDocument *d, const iGmRun *run) {
    if (isEmpty_Array(&d->layout)) {
        return iFalse;
    }
    return run >= (const iGmRun *) constAt_Array(&d->layout, 0) &&
           run < (const iGmRun *) constEnd_Array(&d->layout);
}

const iGmRun *renderProgressive_GmDocument(const iGmDocument *d, const iGmRun *first, int dir,
                                           size_t maxCount,
                                           iRangei visRangeY, iGmDocumentRenderFunc render,
                                           void *context) {
    const iGmRun *run = first;
    while (isValidRun_GmDocument_(d, run)) {
        if ((dir < 0 && bottom_Rect(run->visBounds) < visRangeY.start) ||
            (dir > 0 && top_Rect(run->visBounds) >= visRangeY.end)) {
            break;
        }
        if (maxCount-- == 0) {
            break;
        }
        render(context, run);
        run += dir;
    }
    return isValidRun_GmDocument_(d, run) ? run : NULL;
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

size_t memorySize_GmDocument(const iGmDocument *d) {
    return size_String(&d->unormSource) +
           size_String(&d->source) +
           size_Array(&d->layout) * sizeof(iGmRun) +
           size_Array(&d->links)  * sizeof(iGmLink) +
           memorySize_Media(d->media);
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

iRangecc findLoc_GmDocument(const iGmDocument *d, iInt2 pos) {
    const iGmRun *run = findRun_GmDocument(d, pos);
    if (run) {
        return findLoc_GmRun(run, pos);
    }
    return iNullRange;
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

iRangecc linkLabel_GmDocument(const iGmDocument *d, iGmLinkId linkId) {
    const iGmLink *link = link_GmDocument_(d, linkId);
    if (isEmpty_Range(&link->labelRange)) {
        return link->urlRange;
    }
    return link->labelRange;
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
    const int www_GmLinkFlag            = http_GmLinkFlag | mailto_GmLinkFlag;
    const int gopherOrFinger_GmLinkFlag = gopher_GmLinkFlag | finger_GmLinkFlag;
    if (link) {
        const iBool isUnsupported = (link->flags & supportedProtocol_GmLinkFlag) == 0;
        if (part == icon_GmLinkPart) {
            if (isUnsupported) {
                return tmBadLink_ColorId;
            }
            if (link->flags & iconFromLabel_GmLinkFlag) {
                return link->flags & visited_GmLinkFlag ? tmLinkCustomIconVisited_ColorId
                                                        : tmLinkIcon_ColorId;
            }
            if (link->flags & visited_GmLinkFlag) {
                return link->flags & www_GmLinkFlag ? tmHypertextLinkIconVisited_ColorId
                       : link->flags & gopherOrFinger_GmLinkFlag ? tmGopherLinkIconVisited_ColorId
                                                                 : tmLinkIconVisited_ColorId;
            }
            return link->flags & www_GmLinkFlag              ? tmHypertextLinkIcon_ColorId
                   : link->flags & gopherOrFinger_GmLinkFlag ? tmGopherLinkIcon_ColorId
                                                             : tmLinkIcon_ColorId;
        }
        if (part == text_GmLinkPart) {
            return link->flags & www_GmLinkFlag              ? tmHypertextLinkText_ColorId
                   : link->flags & gopherOrFinger_GmLinkFlag ? tmGopherLinkText_ColorId
                                                             : tmLinkText_ColorId;
        }
        if (part == textHover_GmLinkPart) {
            return link->flags & www_GmLinkFlag              ? tmHypertextLinkTextHover_ColorId
                   : link->flags & gopherOrFinger_GmLinkFlag ? tmGopherLinkTextHover_ColorId
                                                             : tmLinkTextHover_ColorId;
        }
        if (part == domain_GmLinkPart) {
            if (isUnsupported) {
                return tmBadLink_ColorId;
            }
            return link->flags & www_GmLinkFlag              ? tmHypertextLinkDomain_ColorId
                   : link->flags & gopherOrFinger_GmLinkFlag ? tmGopherLinkDomain_ColorId
                                                             : tmLinkDomain_ColorId;
        }
        if (part == visited_GmLinkPart) {
            return link->flags & www_GmLinkFlag              ? tmHypertextLinkLastVisitDate_ColorId
                   : link->flags & gopherOrFinger_GmLinkFlag ? tmGopherLinkLastVisitDate_ColorId
                                                             : tmLinkLastVisitDate_ColorId;
        }
    }
    return tmLinkText_ColorId;
}

iBool isMediaLink_GmDocument(const iGmDocument *d, iGmLinkId linkId) {
    const iString *dstUrl = absoluteUrl_String(&d->url, linkUrl_GmDocument(d, linkId));
    const iRangecc scheme = urlScheme_String(dstUrl);
    if (equalCase_Rangecc(scheme, "gemini") || equalCase_Rangecc(scheme, "gopher") ||
        equalCase_Rangecc(scheme, "finger") ||
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

iRangecc findLoc_GmRun(const iGmRun *d, iInt2 pos) {
    if (pos.y < top_Rect(d->bounds)) {
        return (iRangecc){ d->text.start, d->text.start };
    }
    const int x = pos.x - left_Rect(d->bounds);
    if (x <= 0) {
        return (iRangecc){ d->text.start, d->text.start };
    }
    iRangecc loc;
    tryAdvanceNoWrap_Text(d->font, d->text, x, &loc.start);
    loc.end = loc.start;
    iChar ch;
    if (d->text.end != loc.start) {
        int chLen = decodeBytes_MultibyteChar(loc.start, d->text.end, &ch);
        if (chLen > 0) {
            /* End after the character. */
            loc.end += chLen;
        }
    }
    return loc;
}

iInt2 preRunMargin_GmDocument(const iGmDocument *d, uint16_t preId) {
    iUnused(d, preId);
    return init_I2(3 * gap_Text, 2 * gap_Text);
}

iBool preIsFolded_GmDocument(const iGmDocument *d, uint16_t preId) {
    const iGmPreMeta *meta = preMeta_GmDocument(d, preId);
    return meta && (meta->flags & folded_GmPreMetaFlag) != 0;
}

iBool preHasAltText_GmDocument(const iGmDocument *d, uint16_t preId) {
    const iGmPreMeta *meta = preMeta_GmDocument(d, preId);
    return meta && !isEmpty_Range(&meta->altText);
}

iDefineClass(GmDocument)
