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

#include "parser.h"
#include "markdown.h"
#include "gmutil.h"
#include "app.h"

#include <lagrange/visited.h>
#include <the_Foundation/regexp.h>
#include <the_Foundation/stringset.h>

static void setScheme_GmLink_(iGmLink *d, enum iGmLinkScheme scheme) {
    d->flags &= ~supportedScheme_GmLinkFlag;
    d->flags |= scheme;
}

static iBool isRegionalIndicatorLetter_Char_(iChar c) {
    return c >= 0x1f1e6 && c <= 0x1f1ff;
}

static iBool isAllowedLinkIcon_Char_(iChar icon) {
    if (isFitzpatrickType_Char(icon)) {
        return iFalse;
    }
    return isPictograph_Char(icon) || isEmoji_Char(icon) ||
           isRegionalIndicatorLetter_Char_(icon) ||
           /* TODO: Add range(s) of 0x2nnn symbols. */
           icon == 0x2022 /* bullet */ ||
           icon == 0x2139 /* info */ ||
           (icon >= 0x2190 && icon <= 0x21ff /* arrows */) ||
           icon == 0x29bf /* circled bullet */ ||
           icon == 0x2a2f /* close X */ ||
           (icon >= 0x2b00 && icon <= 0x2bff) ||
           icon == 0x20bf /* bitcoin */ ||
           (icon >= 0x1f191 && icon <= 0x1f19a) /* enclosed signs */;
}

static void addLink_GmDocument_(iGmDocument *d, iRangecc line) {
    /* Links are numbered in source order; invalid links are not added. */
    static iRegExp *pattern_;
    static iRegExp *spartanQueryPattern_;
    if (!pattern_) {
        pattern_ = newGemtextLink_RegExp();
    }
    if (d->flags.isSpartan && !spartanQueryPattern_) {
        spartanQueryPattern_ = new_RegExp("=:\\s*([^\\s]+)(\\s.*)?", 0);
    }
    iGmLink *link = NULL;
    iRegExpMatch m;
    init_RegExpMatch(&m);
    if (d->flags.isSpartan && matchRange_RegExp(spartanQueryPattern_, line, &m)) {
        link = new_GmLink();
        link->urlRange = capturedRange_RegExpMatch(&m, 1);
        link->flags = query_GmLinkFlag;
        setScheme_GmLink_(link, spartan_GmLinkScheme);
        setRange_String(&link->url, link->urlRange);
        set_String(&link->url, canonicalUrl_String(absoluteUrl_String(&d->url, &link->url)));
        if (startsWithCaseFast_String(&link->url, "local:") && !d->parser.isLocalOrInternal) {
            delete_GmLink(link);
            return;
        }
    }
    if (!link) {
        init_RegExpMatch(&m);
    }
    if (!link && matchRange_RegExp(pattern_, line, &m)) {
        link = new_GmLink();
        link->urlRange = capturedRange_RegExpMatch(&m, 1);
        setRange_String(&link->url, link->urlRange);
        set_String(&link->url, canonicalUrl_String(absoluteUrl_String(&d->url, &link->url)));
        if (d->flags.isNex) {
            link->flags |= inline_GmLinkFlag;
        }
        /* If invalid, disregard the link. */
        if ((d->format == gemini_SourceFormat &&
             size_String(&link->url) > prefs_App()->maxUrlSize) ||
            (startsWithCaseFast_String(&link->url, "about:command")
             /* this is a special internal page that allows submitting UI events */
             && !d->flags.enableCommandLinks) ||
            (startsWithCaseFast_String(&link->url, "local:") && !d->parser.isLocalOrInternal)) {
            delete_GmLink(link);
            return;
        }
        /* Check the URL. */ {
            iUrl parts;
            init_Url(&parts, &link->url);
            if (!equalCaseFast_Rangecc(parts.host, cstr_String(&d->localHost))) {
                link->flags |= remote_GmLinkFlag;
            }
            if (equalCaseFast_Rangecc(parts.scheme, "gemini")) {
                setScheme_GmLink_(link, gemini_GmLinkScheme);
            }
            else if (equalCaseFast_Rangecc(parts.scheme, "titan")) {
                setScheme_GmLink_(link, titan_GmLinkScheme);
            }
            else if (startsWithCaseFast_Rangecc(parts.scheme, "http")) {
                setScheme_GmLink_(link, http_GmLinkScheme);
            }
            else if (isGopherScheme_Rangecc(parts.scheme)) {
                setScheme_GmLink_(link, gopher_GmLinkScheme);
                if (startsWith_Rangecc(parts.path, "/7")) {
                    link->flags |= query_GmLinkFlag;
                }
            }
            else if (equalCaseFast_Rangecc(parts.scheme, "finger")) {
                setScheme_GmLink_(link, finger_GmLinkScheme);
            }
            else if (equalCaseFast_Rangecc(parts.scheme, "spartan")) {
                setScheme_GmLink_(link, spartan_GmLinkScheme);
            }
            else if (equalCaseFast_Rangecc(parts.scheme, "nex")) {
                setScheme_GmLink_(link, nex_GmLinkScheme);
            }
            else if (equalCaseFast_Rangecc(parts.scheme, "guppy")) {
                setScheme_GmLink_(link, guppy_GmLinkScheme);
            }
            else if (isLocalFileScheme_Rangecc(parts.scheme)) {
                setScheme_GmLink_(link, file_GmLinkScheme);
            }
            else if (equalCaseFast_Rangecc(parts.scheme, "data")) {
                setScheme_GmLink_(link, data_GmLinkScheme);
                if (startsWith_Rangecc(parts.path, "image/png") ||
                    startsWith_Rangecc(parts.path, "image/jpg") ||
                    startsWith_Rangecc(parts.path, "image/jpeg") ||
                    startsWith_Rangecc(parts.path, "image/jxl") ||
                    startsWith_Rangecc(parts.path, "image/webp") ||
                    startsWith_Rangecc(parts.path, "image/gif")) {
                    link->flags |= imageFileExtension_GmLinkFlag;
                }
            }
            else if (equalCaseFast_Rangecc(parts.scheme, "about")) {
                setScheme_GmLink_(link, about_GmLinkScheme);
            }
            else if (equalCaseFast_Rangecc(parts.scheme, "mailto")) {
                setScheme_GmLink_(link, mailto_GmLinkScheme);
            }
            else if (equalCaseFast_Rangecc(parts.scheme, "misfin")) {
                setScheme_GmLink_(link, misfin_GmLinkScheme);
            }
            /* Check the file name extension, if present. */
            if (!isEmpty_Range(&parts.path)) {
                iString *path = newRange_String(parts.path);
                if (endsWithCaseFast_String(path, ".gif")  || endsWithCaseFast_String(path, ".jpg") ||
                    endsWithCaseFast_String(path, ".jpeg") || endsWithCaseFast_String(path, ".png") ||
                    endsWithCaseFast_String(path, ".tga")  || endsWithCaseFast_String(path, ".psd") ||
                    endsWithCaseFast_String(path, ".hdr")  ||
#if defined (LAGRANGE_ENABLE_JXL)
                    endsWithCaseFast_String(path, ".jxl") ||
#endif
#if defined (LAGRANGE_ENABLE_WEBP)
                    endsWithCaseFast_String(path, ".webp") ||
#endif
                    endsWithCaseFast_String(path, ".pic")) {
                    link->flags |= imageFileExtension_GmLinkFlag;
                }
                else if (endsWithCaseFast_String(path, ".mp3") || endsWithCaseFast_String(path, ".wav") ||
                         endsWithCaseFast_String(path, ".ogg") ||
#if defined (LAGRANGE_ENABLE_OPUS)
                         endsWithCaseFast_String(path, ".opus") ||
#endif
                         endsWithCaseFast_String(path, ".mid")) {
                    link->flags |= audioFileExtension_GmLinkFlag;
                }
                else if (endsWithCaseFast_String(path, ".fontpack")) {
                    link->flags |= fontpackFileExtension_GmLinkFlag;
                }
                delete_String(path);
            }
        }
    }
    if (link) {
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
        pushBack_PtrArray(&d->links, link);
        iRangecc desc = capturedRange_RegExpMatch(&m, 2);
        trim_Rangecc(&desc);
        link->labelRange = desc;
        link->labelIcon = iNullRange;
        if (d->flags.isNex) {
            link->labelIcon = (iRangecc){ line.start, link->urlRange.start };
            link->labelRange = capturedRange_RegExpMatch(&m, 2);
            line = link->urlRange;
        }
        else if (!isEmpty_Range(&desc)) {
            line = desc; /* Just show the description. */
            link->flags |= humanReadable_GmLinkFlag;
            /* Check for a custom icon. */
            enum iGmLinkScheme scheme = scheme_GmLinkFlag(link->flags);
            if ((scheme == gemini_GmLinkScheme && ~link->flags & remote_GmLinkFlag) ||
                scheme == about_GmLinkScheme || scheme == file_GmLinkScheme ||
                scheme == mailto_GmLinkScheme || scheme == misfin_GmLinkScheme ||
                scheme == 0 /* unsupported */) {
                iChar icon = 0;
                int   len  = 0;
                if ((len = decodeBytes_MultibyteChar(desc.start, desc.end, &icon)) > 0) {
                    if (((scheme != mailto_GmLinkScheme && scheme != misfin_GmLinkScheme &&
                          isAllowedLinkIcon_Char_(icon)) ||
                         ((scheme == mailto_GmLinkScheme || scheme == misfin_GmLinkScheme) &&
                          icon == 0x1f4e7 /* envelope */))) {
                        if (isRegionalIndicatorLetter_Char_(icon)) {
                            iChar combo;
                            int   len2 =
                                decodeBytes_MultibyteChar(desc.start + len, desc.end, &combo);
                            if (isRegionalIndicatorLetter_Char_(combo)) {
                                len += len2;
                            }
                        }
                        iRangecc iconRange = (iRangecc){ desc.start, desc.start + len };
                        iRangecc remain    = (iRangecc){ iconRange.end, line.end };
                        trim_Rangecc(&remain);
                        if (!isEmpty_Range(&remain)) {
                            /* Check the next character as well. If there are multiple Emoji,
                               perhaps the first one wasn't meant to have special significance. */
                            iChar nextChar;
                            len = decodeBytes_MultibyteChar(remain.start, remain.end, &nextChar);
                            if (len > 0 && !isAllowedLinkIcon_Char_(nextChar)) {
                                link->flags |= iconFromLabel_GmLinkFlag;
                                link->labelIcon = iconRange;
                                line.start      = iconRange.end;
                                trimStart_Rangecc(&line);
                            }
                        }
                        // printf("custom icon: %x (%s)\n", icon, cstr_Rangecc(link->labelIcon));
                        // fflush(stdout);
                    }
                }
            }
        }
        else {
            line = capturedRange_RegExpMatch(&m, 1); /* Show the URL. */
        }
        link->textRange = line;
    }
}

static void beginPreformatted_GmDocument_(iGmDocument *d, iRangecc line, const iArray *oldPreMeta) {
    iGmParser *p = &d->parser;
    const size_t preIndex = size_Array(&d->preMeta);
    iGmPreMeta meta = { .bounds = line };
    meta.contents = (iRangecc){ line.end + 1, line.end + 1 };
    meta.altText  = line;
    trimLine_Rangecc(&meta.altText, preformatted_GmLineType, d->parser.isNormalized);
    /* Fold state is kept over reparsing. */
    if (oldPreMeta && preIndex < size_Array(oldPreMeta)) {
        meta.flags = constValue_Array(oldPreMeta, preIndex, iGmPreMeta).flags & folded_GmPreMetaFlag;
    }
    else if (d->docPrefs.collapsePre.value >= byDefault_Collapse && !isGopher_GmDocument_(d)) {
        meta.flags |= folded_GmPreMetaFlag;
    }
    pushBack_Array(&d->preMeta, &meta);
    p->isPreformat = iTrue;
    p->preId       = size_Array(&d->preMeta);
}

static void updateContext_GmParser_(iGmParser *d, size_t offset, iBool enablesIndents,
                                    iBool endsFirstText) {
    if (enablesIndents && d->indentPos == iInvalidPos) {
        d->indentPos = offset;
    }
    if (endsFirstText && d->firstTextPos == iInvalidPos) {
        d->firstTextPos = offset;
    }
}

static void parseLine_GmDocument_(iGmDocument *d, iRangecc line, const iArray *oldPreMeta) {
    /* The context updates mirror how the typesetter handles each line. */
    iGmParser   *p      = &d->parser;
    const size_t offset = line.start - constBegin_String(&d->source);
    if (d->flags.isNex) {
        if (lineType_GmDocument_(d, line) == link_GmLineType) {
            addLink_GmDocument_(d, line);
        }
        if (*line.end == '\r') {
            line.end--;
        }
        if (!isEmpty_Range(&line)) {
            updateContext_GmParser_(p, offset, iTrue, iTrue);
        }
        return;
    }
    if (p->isPreformat) {
        iGmPreMeta *meta = p->preId ? at_Array(&d->preMeta, p->preId - 1) : NULL;
        if (d->format == gemini_SourceFormat && startsWith_Rangecc(line, "```")) {
            p->isPreformat = iFalse;
            p->preId       = 0;
        }
        else {
            if (meta) {
                meta->contents.end = line.end;
            }
            iRangecc text = line;
            if (*text.end == '\r') {
                text.end--;
            }
            if (line.start == constBegin_String(&d->source)) {
                p->isPrevPre = iTrue;
            }
            if (!isEmpty_Range(&text)) {
                updateContext_GmParser_(p, offset, !p->isPrevPre || isGopher_GmDocument_(d), iTrue);
            }
            p->isPrevPre = iTrue;
        }
        if (meta) {
            /* An unterminated block extends to the end of the source. */
            meta->bounds.end = line.end;
        }
        return;
    }
    if (*line.end == '\r') {
        line.end--;
    }
    enum iGmLineType type = lineType_GmDocument_(d, line);
    if (line.start == constBegin_String(&d->source)) {
        p->isPrevPre = (type == preformatted_GmLineType);
    }
    if (type == preformatted_GmLineType) {
        beginPreformatted_GmDocument_(d, line, oldPreMeta);
        return;
    }
    iRangecc text = line;
    if (type == link_GmLineType) {
        const size_t numLinks = size_PtrArray(&d->links);
        addLink_GmDocument_(d, line);
        if (size_PtrArray(&d->links) > numLinks) {
            text = ((const iGmLink *) back_PtrArray(&d->links))->textRange;
        }
        else {
            type = text_GmLineType;
        }
    }
    else if (type >= heading1_GmLineType && type <= heading3_GmLineType) {
        iRangecc heading = line;
        trimLine_Rangecc(&heading, type, d->parser.isNormalized);
        pushBack_Array(&d->headings,
                       &(iGmHeading){ .text = heading, .level = type - heading1_GmLineType });
    }
    if (d->origFormat == markdown_SourceFormat && isHRule_Rangecc_(line)) {
        return;
    }
    trimLine_Rangecc(&text, type, d->parser.isNormalized);
    if (!isEmpty_Range(&text)) {
        updateContext_GmParser_(p, offset, iTrue, type != heading1_GmLineType);
    }
    p->isPrevPre = iFalse;
}

static void begin_GmParser_(iGmDocument *d) {
    iGmParser *p = &d->parser;
    if (!p->isValid) {
        iArray *oldPreMeta = copy_Array(&d->preMeta);
        clearLinks_GmDocument_(d);
        clear_Array(&d->headings);
        clear_Array(&d->preMeta);
        deinit_GmParser(p);
        init_GmParser(p);
        p->isValid      = iTrue;
        p->isPreformat  = (d->format == plainText_SourceFormat);
        p->isNormalized = shouldBeNormalized_GmDocument_(d);
        /* Remote pages may not refer to the user's data directory. */ {
            const iRangecc scheme = urlScheme_String(&d->url);
            p->isLocalOrInternal =
                isLocalFileScheme_Rangecc(scheme) || equalCaseFast_Rangecc(scheme, "about");
        }
        p->isPrevPre    = iFalse;
        p->indentPos    = iInvalidPos;
        p->firstTextPos = iInvalidPos;
        p->oldPreMeta   = oldPreMeta;
        updateOpenURLs_GmDocument_(d);
    }
}

static iBool parseNextLine_GmDocument_(iGmDocument *d) {
    iGmParser *p = &d->parser;
    if (nextLine_Rangecc_(range_String(&d->source), &p->line)) {
        parseLine_GmDocument_(d, p->line, p->oldPreMeta);
        return iTrue;
    }
    return iFalse;
}

void invalidateParse_GmDocument_(iGmDocument *d) {
    d->parser.isValid = iFalse;
}

iBool isPreOpen_GmDocument_(const iGmDocument *d, uint16_t preId) {
    return d->parser.isPreformat && d->parser.preId == preId;
}

void ensureParsed_GmDocument_(iGmDocument *d) {
    begin_GmParser_(d);
    while (parseNextLine_GmDocument_(d)) {}
}

void parseUntil_GmDocument_(iGmDocument *d, const char *pos) {
    /* Parsing is done just ahead of the layout. */
    iGmParser *p = &d->parser;
    begin_GmParser_(d);
    while ((!p->line.start || p->line.end < pos) && parseNextLine_GmDocument_(d)) {}
}

void parsePre_GmDocument_(iGmDocument *d, uint16_t preId) {
    while (isPreOpen_GmDocument_(d, preId) && parseNextLine_GmDocument_(d)) {}
}

size_t preBefore_GmDocument_(const iGmDocument *d, const char *pos) {
    /* Number of preformatted blocks that begin before `pos`. */
    size_t lo = 0, hi = size_Array(&d->preMeta);
    while (lo < hi) {
        const size_t mid = (lo + hi) / 2;
        if (constValue_Array(&d->preMeta, mid, iGmPreMeta).bounds.start < pos) {
            lo = mid + 1;
        }
        else {
            hi = mid;
        }
    }
    return lo;
}

size_t linksBefore_GmDocument_(const iGmDocument *d, const char *pos) {
    size_t lo = 0, hi = size_PtrArray(&d->links);
    while (lo < hi) {
        const size_t mid = (lo + hi) / 2;
        if (((const iGmLink *) constAt_PtrArray(&d->links, mid))->urlRange.start < pos) {
            lo = mid + 1;
        }
        else {
            hi = mid;
        }
    }
    return lo;
}

const char *findLeadIn_GmDocument_(const iGmDocument *d, const char *lineStart) {
    /* The nearest preceding line that produces a text run. After it, the typesetter state is
       the same regardless of what came before, apart from the long-range parser context. */
    const iRangecc src        = range_String(&d->source);
    const iBool    normalized = d->parser.isNormalized;
    const char    *pos        = lineStart;
    for (int count = 0; pos > src.start && count < 1000; count++) {
        const char *start = lineStart_Rangecc_(src, pos - 1);
        iRangecc    line  = { start, pos - 1 };
        pos = start;
        if (line.end > line.start && line.end[-1] == '\r') {
            line.end--;
        }
        if (d->format == plainText_SourceFormat || d->flags.isNex) {
            if (!isEmpty_Range(&line)) {
                return start;
            }
            continue;
        }
        const size_t numPre = preBefore_GmDocument_(d, start);
        if (numPre) {
            const iGmPreMeta *meta = constAt_Array(&d->preMeta, numPre - 1);
            if (start <= meta->bounds.end) {
                if (meta->flags & folded_GmPreMetaFlag) {
                    pos = meta->bounds.start; /* the whole block is a single run */
                    continue;
                }
                iRangecc text = line;
                trim_Rangecc(&text);
                if (!startsWith_Rangecc(line, "```") && !isEmpty_Range(&text)) {
                    return start;
                }
                continue;
            }
        }
        enum iGmLineType type = lineType_GmDocument_(d, line);
        if (type == preformatted_GmLineType ||
            (d->origFormat == markdown_SourceFormat && isHRule_Rangecc_(line))) {
            continue;
        }
        iRangecc text = line;
        if (type == link_GmLineType) {
            const size_t index = linksBefore_GmDocument_(d, start);
            const iGmLink *link =
                index < size_PtrArray(&d->links) ? constAt_PtrArray(&d->links, index) : NULL;
            if (link && link->urlRange.start <= line.end) {
                text = link->textRange;
            }
            else {
                type = text_GmLineType;
            }
        }
        trimLine_Rangecc(&text, type, normalized);
        trim_Rangecc(&text);
        if (!isEmpty_Range(&text)) {
            return start;
        }
    }
    return NULL;
}

void init_GmParser(iGmParser *d) {
    iZap(*d);
}

void deinit_GmParser(iGmParser *d) {
    delete_Array(d->oldPreMeta);
    d->oldPreMeta = NULL;
}
