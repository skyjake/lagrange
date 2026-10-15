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

static iBool isLocalOrInternal_GmDocument_(const iGmDocument *d) {
    /* Remote pages may not refer to the user's data directory. */
    const iRangecc scheme = urlScheme_String(&d->url);
    return isLocalFileScheme_Rangecc(scheme) || equalCase_Rangecc(scheme, "about");
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
        if (startsWithCase_String(&link->url, "local:") && !isLocalOrInternal_GmDocument_(d)) {
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
            (startsWithCase_String(&link->url, "about:command")
             /* this is a special internal page that allows submitting UI events */
             && !d->flags.enableCommandLinks) ||
            (startsWithCase_String(&link->url, "local:") && !isLocalOrInternal_GmDocument_(d))) {
            delete_GmLink(link);
            return;
        }
        /* Check the URL. */ {
            iUrl parts;
            init_Url(&parts, &link->url);
            if (!equalCase_Rangecc(parts.host, cstr_String(&d->localHost))) {
                link->flags |= remote_GmLinkFlag;
            }
            if (equalCase_Rangecc(parts.scheme, "gemini")) {
                setScheme_GmLink_(link, gemini_GmLinkScheme);
            }
            else if (equalCase_Rangecc(parts.scheme, "titan")) {
                setScheme_GmLink_(link, titan_GmLinkScheme);
            }
            else if (startsWithCase_Rangecc(parts.scheme, "http")) {
                setScheme_GmLink_(link, http_GmLinkScheme);
            }
            else if (isGopherScheme_Rangecc(parts.scheme)) {
                setScheme_GmLink_(link, gopher_GmLinkScheme);
                if (startsWith_Rangecc(parts.path, "/7")) {
                    link->flags |= query_GmLinkFlag;
                }
            }
            else if (equalCase_Rangecc(parts.scheme, "finger")) {
                setScheme_GmLink_(link, finger_GmLinkScheme);
            }
            else if (equalCase_Rangecc(parts.scheme, "spartan")) {
                setScheme_GmLink_(link, spartan_GmLinkScheme);
            }
            else if (equalCase_Rangecc(parts.scheme, "nex")) {
                setScheme_GmLink_(link, nex_GmLinkScheme);
            }
            else if (equalCase_Rangecc(parts.scheme, "guppy")) {
                setScheme_GmLink_(link, guppy_GmLinkScheme);
            }
            else if (isLocalFileScheme_Rangecc(parts.scheme)) {
                setScheme_GmLink_(link, file_GmLinkScheme);
            }
            else if (equalCase_Rangecc(parts.scheme, "data")) {
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
            else if (equalCase_Rangecc(parts.scheme, "about")) {
                setScheme_GmLink_(link, about_GmLinkScheme);
            }
            else if (equalCase_Rangecc(parts.scheme, "mailto")) {
                setScheme_GmLink_(link, mailto_GmLinkScheme);
            }
            else if (equalCase_Rangecc(parts.scheme, "misfin")) {
                setScheme_GmLink_(link, misfin_GmLinkScheme);
            }
            /* Check the file name extension, if present. */
            if (!isEmpty_Range(&parts.path)) {
                iString *path = newRange_String(parts.path);
                if (endsWithCase_String(path, ".gif")  || endsWithCase_String(path, ".jpg") ||
                    endsWithCase_String(path, ".jpeg") || endsWithCase_String(path, ".png") ||
                    endsWithCase_String(path, ".tga")  || endsWithCase_String(path, ".psd") ||
                    endsWithCase_String(path, ".hdr")  ||
#if defined (LAGRANGE_ENABLE_JXL)
                    endsWithCase_String(path, ".jxl") ||
#endif
#if defined (LAGRANGE_ENABLE_WEBP)
                    endsWithCase_String(path, ".webp") ||
#endif
                    endsWithCase_String(path, ".pic")) {
                    link->flags |= imageFileExtension_GmLinkFlag;
                }
                else if (endsWithCase_String(path, ".mp3") || endsWithCase_String(path, ".wav") ||
                         endsWithCase_String(path, ".ogg") ||
#if defined (LAGRANGE_ENABLE_OPUS)
                         endsWithCase_String(path, ".opus") ||
#endif
                         endsWithCase_String(path, ".mid")) {
                    link->flags |= audioFileExtension_GmLinkFlag;
                }
                else if (endsWithCase_String(path, ".fontpack")) {
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

void init_GmParser(iGmParser *d) {
    iZap(*d);
}

void invalidateParse_GmDocument_(iGmDocument *d) {
    d->parser.isValid = iFalse;
}

static void beginPreformatted_GmDocument_(iGmDocument *d, iRangecc line, const iArray *oldPreMeta) {
    iGmParser *p = &d->parser;
    const size_t preIndex = size_Array(&d->preMeta);
    iGmPreMeta meta = { .bounds = line };
    meta.contents = (iRangecc){ line.end + 1, line.end + 1 };
    meta.altText  = line;
    trimLine_Rangecc(&meta.altText, preformatted_GmLineType, shouldBeNormalized_GmDocument_(d));
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

static void parseLine_GmDocument_(iGmDocument *d, iRangecc line, const iArray *oldPreMeta) {
    iGmParser *p = &d->parser;
    if (d->flags.isNex) {
        if (lineType_GmDocument_(d, line) == link_GmLineType) {
            addLink_GmDocument_(d, line);
        }
        return;
    }
    if (p->isPreformat) {
        iGmPreMeta *meta = p->preId ? at_Array(&d->preMeta, p->preId - 1) : NULL;
        if (d->format == gemini_SourceFormat && startsWith_Rangecc(line, "```")) {
            p->isPreformat = iFalse;
            p->preId       = 0;
        }
        else if (meta) {
            meta->contents.end = line.end;
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
    const enum iGmLineType type = lineType_GmDocument_(d, line);
    if (type == preformatted_GmLineType) {
        beginPreformatted_GmDocument_(d, line, oldPreMeta);
    }
    else if (type == link_GmLineType) {
        addLink_GmDocument_(d, line);
    }
    else if (type >= heading1_GmLineType && type <= heading3_GmLineType) {
        trimLine_Rangecc(&line, type, shouldBeNormalized_GmDocument_(d));
        pushBack_Array(&d->headings,
                       &(iGmHeading){ .text = line, .level = type - heading1_GmLineType });
    }
}

iBool isPreOpen_GmDocument_(const iGmDocument *d, uint16_t preId) {
    return d->parser.isPreformat && d->parser.preId == preId;
}

void ensureParsed_GmDocument_(iGmDocument *d) {
    /* Parses lines appended to the source since the last call. */
    iGmParser    *p          = &d->parser;
    const iArray *oldPreMeta = NULL;
    if (!p->isValid) {
        oldPreMeta = collect_Array(copy_Array(&d->preMeta));
        clearLinks_GmDocument_(d);
        clear_Array(&d->headings);
        clear_Array(&d->preMeta);
        init_GmParser(p);
        p->isValid     = iTrue;
        p->isPreformat = (d->format == plainText_SourceFormat);
        updateOpenURLs_GmDocument_(d);
    }
    while (nextLine_Rangecc_(range_String(&d->source), &p->line)) {
        parseLine_GmDocument_(d, p->line, oldPreMeta);
    }
}
