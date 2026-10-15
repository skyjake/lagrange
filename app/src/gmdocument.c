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

#include "gmdoc/impl.h"
#include "gmdoc/palette.h"
#include "gmdoc/typesetter.h"
#include "gmutil.h"
#include "gempub.h"
#include <lagrange/lang.h>
#include "color.h"
#include "render/text.h"
#include "ui/metrics.h"
#include "ui/mediaui.h"
#include "ui/window.h"
#include <lagrange/visited.h>
#include "bookmarks.h"
#include "books.h"
#include "feeds.h"
#include "app.h"
#include "defs.h"

#include <the_Foundation/intset.h>
#include <the_Foundation/path.h>
#include <the_Foundation/ptrarray.h>
#include <the_Foundation/regexp.h>
#include <the_Foundation/stringarray.h>
#include <the_Foundation/stringset.h>

#include <ctype.h>

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

iDefineObjectConstruction(GmDocument)

static void import_GmDocument_(iGmDocument *);

iBool isForcedMonospace_GmDocument_(const iGmDocument *d) {
    if (d->flags.isNex) {
        return iTrue;
    }
    if (d->flags.isConvertedMarkdown) {
        return iFalse;
    }
    if (d->flags.isCoverPage) {
        return iFalse;
    }
    if (equalCase_Rangecc(urlScheme_String(&d->url), "finger")) {
        return iTrue;
    }
    return d->docPrefs.monospaceDocument.value;
}

iBool isGopher_GmDocument_(const iGmDocument *d) {
    const iRangecc scheme = urlScheme_String(&d->url);
    return (isGopherScheme_Rangecc(scheme) || equalCase_Rangecc(scheme, "finger"));
}

enum iGmLineType lineType_GmDocument_(const iGmDocument *d, const iRangecc line) {
    if (d->flags.isNex) {
        return startsWith_Rangecc(line, "=> ") ? link_GmLineType : text_GmLineType;
    }
    if (d->format == plainText_SourceFormat) {
        return text_GmLineType;
    }
    if (d->flags.isSpartan && startsWith_Rangecc(line, "=:")) {
        return link_GmLineType;
    }
    return lineType_Rangecc(line);
}

enum iGmLineType lineType_Rangecc(const iRangecc line) {
    if (isEmpty_Range(&line)) {
        return text_GmLineType;
    }
    if (startsWith_Rangecc(line, "=>")) {
        iRangecc trim = line;
        trim_Rangecc(&trim);
        if (size_Range(&trim) > 2) {
            return link_GmLineType;
        }
        return text_GmLineType;
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

iRangecc addLink_GmDocument_(iGmDocument *d, iRangecc line, iGmLinkId *linkId) {
    /* Returns the human-readable label of the link. */
    static iRegExp *pattern_;
    static iRegExp *spartanQueryPattern_;
    if (!pattern_) {
        pattern_ = newGemtextLink_RegExp();
    }
    if (d->flags.isSpartan && !spartanQueryPattern_) {
        spartanQueryPattern_ = new_RegExp("=:\\s*([^\\s]+)(\\s.*)?", 0);
    }
    *linkId = 0;
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
            return line;
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
            return line;
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
        *linkId = size_PtrArray(&d->links); /* index + 1 */
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
    }
    return line;
}

void clearLinks_GmDocument_(iGmDocument *d) {
    iForEach(PtrArray, i, &d->links) {
        delete_GmLink(i.ptr);
    }
    clear_PtrArray(&d->links);
}

iBool isGopherMenu_GmDocument(const iGmDocument *d) {
    if (d->flags.isGopherMenu) {
        /* We know from the URL that it's a menu. */
        return iTrue;
    }
    return isGopher_GmDocument_(d) && d->format == gemini_SourceFormat && !d->flags.isCoverPage;
}

enum iFontId font_GmDocument(const iGmDocument *d, enum iGmLineType lineType) {
    return d->style.fonts[lineType];
}

iBool shouldBeNormalized_GmDocument_(const iGmDocument *d) {
    const iDocPrefs *prefs = &d->docPrefs;
    if (d->format == plainText_SourceFormat) {
        return iFalse; /* plain text is always shown as-is */
    }
    if (d->flags.isNex) {
        return iFalse;
    }
    if (prefs->monospaceDocument.value) {
        return iFalse;
    }
    if (isGopherScheme_Rangecc(urlScheme_String(&d->url)) && !prefs->geminiStyledGopher.value) {
        return iFalse;
    }
    return iTrue;
}

void updateOpenURLs_GmDocument_(iGmDocument *d) {
    if (d->openURLs) {
        iReleasePtr(&d->openURLs);
    }
    d->openURLs = listOpenURLs_App();
}

void init_GmDocument(iGmDocument *d) {
    d->origFormat = gemini_SourceFormat; /* format of `origSource` */
    d->format     = gemini_SourceFormat; /* format of `source` */
    d->viewFormat = gemini_SourceFormat; /* user's preference */
    init_String(&d->origSource);
    init_String(&d->source);
    init_String(&d->url);
    init_String(&d->themeUrl);
    init_String(&d->fixedTitle);
    init_String(&d->mimeType);
    init_String(&d->localHost);
    init_ScopeKey(&d->scopeKey);
    initCopy_DocPrefs(&d->docPrefs, activeDocPrefs_App());
    iZap(d->metrics);
    iZap(d->requestedMetrics);
    d->numFilledColumns = 0;
    iZap(d->style);
    d->wrapWidth = 0;
    d->size = zero_I2();
    init_Array(&d->layout, sizeof(iGmRun));
    init_StringArray(&d->auxText);
    init_PtrArray(&d->links);
    init_String(&d->title);
    init_String(&d->bookTitle);
    init_String(&d->chapterTitle);
    init_Array(&d->headings, sizeof(iGmHeading));
    init_Array(&d->pageBands, sizeof(iGmPageBands));
    d->chapter     = 0;
    d->numChapters = 0;
    d->coverImage  = 0;
    init_Array(&d->preMeta, sizeof(iGmPreMeta));
    d->themeSeed = 0;
    d->layoutFontKey = 0;
    d->siteIcon = 0;
    d->media = new_Media();
    d->openURLs = NULL;
    d->warnings = 0;
    iZap(d->palette);
    d->flags.enableCommandLinks = iFalse;
    d->flags.isSpartan = iFalse;
    d->flags.isNex = iFalse;
    d->flags.isLayoutInvalidated = iFalse;
    d->flags.isPaletteValid = iFalse;
    d->flags.isConvertedMarkdown = iFalse;
    d->flags.isCoverPage = iFalse;
    d->flags.isBookCover = iFalse;
    d->flags.isBookContents = iFalse;
    d->flags.isUiTheme   = iFalse;
    d->flags.isDocPrefsPinned = iFalse;
    d->flags.isPageViewInverted = iFalse;
}

void deinit_GmDocument(iGmDocument *d) {
    iReleasePtr(&d->openURLs);
    delete_Media(d->media);
    deinit_String(&d->chapterTitle);
    deinit_String(&d->bookTitle);
    deinit_String(&d->title);
    clearLinks_GmDocument_(d);
    deinit_PtrArray(&d->links);
    deinit_Array(&d->preMeta);
    deinit_Array(&d->pageBands);
    deinit_Array(&d->headings);
    deinit_StringArray(&d->auxText);
    deinit_Array(&d->layout);
    deinit_DocPrefs(&d->docPrefs);
    deinit_ScopeKey(&d->scopeKey);
    deinit_String(&d->localHost);
    deinit_String(&d->mimeType);
    deinit_String(&d->themeUrl);
    deinit_String(&d->fixedTitle);
    deinit_String(&d->url);
    deinit_String(&d->source);
    deinit_String(&d->origSource);
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

const iDocPrefs *docPrefs_GmDocument(const iGmDocument *d) {
    return &d->docPrefs;
}

const iScopeKey *scopeKey_GmDocument(const iGmDocument *d) {
    return &d->scopeKey;
}

static uint32_t applyDocPrefs_GmDocument_(iGmDocument *d, const iDocPrefs *prefs) {
    const uint32_t diff = diff_DocPrefs(&d->docPrefs, prefs);
    if (diff) {
        set_DocPrefs(&d->docPrefs, prefs);
        if (diff & palette_DocPrefsSpecFlag) {
            invalidatePalette_GmDocument(d);
        }
        if (diff & (relayout_DocPrefsSpecFlag | reparse_DocPrefsSpecFlag)) {
            d->wrapWidth = 0;
            invalidateLayout_GmDocument(d);
        }
    }
    return diff;
}

void setDocPrefs_GmDocument(iGmDocument *d, const iDocPrefs *pinned) {
    d->flags.isDocPrefsPinned = iTrue;
    applyDocPrefs_GmDocument_(d, pinned);
}

uint32_t updateDocPrefs_GmDocument(iGmDocument *d) {
    if (d->flags.isDocPrefsPinned) {
        return 0;
    }
    iDocPrefs resolved;
    init_DocPrefs(&resolved);
    resolve_DocPrefs(&resolved, &d->scopeKey, &d->url,
                     !isEmpty_String(&d->mimeType) ? &d->mimeType : NULL);
    if (d->flags.isPageViewInverted) {
        resolved.pageView.value = !resolved.pageView.value;
    }
    const uint32_t diff = applyDocPrefs_GmDocument_(d, &resolved);
    deinit_DocPrefs(&resolved);
    return diff;
}

void setPageViewInverted_GmDocument(iGmDocument *d, iBool invert) {
    d->flags.isPageViewInverted = invert;
}

void setMimeType_GmDocument(iGmDocument *d, const iString *mimeType) {
    set_String(&d->mimeType, mimeType);
}

void setMode_GmDocument(iGmDocument *d, enum iGmDocumentMode mode) {
    d->flags.isCoverPage = (mode == coverPage_GmDocumentMode);
    d->flags.isUiTheme   = (mode == uiTheme_GmDocumentMode);
    invalidatePalette_GmDocument(d);
}

void setFormat_GmDocument(iGmDocument *d, enum iSourceFormat format) {
    d->origFormat = format;
    d->viewFormat = (format == plainText_SourceFormat ? format : gemini_SourceFormat);
}

iBool setViewFormat_GmDocument(iGmDocument *d, enum iSourceFormat viewFormat) {
    if (d->viewFormat != viewFormat) {
        d->viewFormat = viewFormat;
        import_GmDocument_(d);
        return iTrue;
    }
    return iFalse;
}

enum iSourceFormat viewFormat_GmDocument(const iGmDocument *d) {
    return d->viewFormat;
}

iBool equal_GmDocumentMetrics(const iGmDocumentMetrics *d, const iGmDocumentMetrics *other) {
    return d->width == other->width && d->canvasWidth == other->canvasWidth &&
           d->maxContentWidth == other->maxContentWidth && d->pageHeight == other->pageHeight &&
           d->columnsPerPage == other->columnsPerPage &&
           d->firstPageInset == other->firstPageInset && d->lastPageInset == other->lastPageInset &&
           d->topInset == other->topInset && d->bottomInset == other->bottomInset &&
           d->singleColumnWidth == other->singleColumnWidth &&
           d->singleColumnMaxContentWidth == other->singleColumnMaxContentWidth;
}

void setMetrics_GmDocument(iGmDocument *d, const iGmDocumentMetrics *metrics) {
    d->requestedMetrics = *metrics;
    d->wrapWidth        = 0;
    doLayout_GmDocument_(d); /* TODO: just flag need-layout and do it later */
}

iBool updateMetrics_GmDocument(iGmDocument *d, const iGmDocumentMetrics *metrics) {
    if (!equal_GmDocumentMetrics(&d->requestedMetrics, metrics) || isLayoutOutdated_GmDocument(d)) {
        setMetrics_GmDocument(d, metrics);
        return iTrue;
    }
    return iFalse;
}

int contentWidth_GmDocument(const iGmDocument *d) {
    return d->contentWidth;
}

const iGmDocumentMetrics *metrics_GmDocument(const iGmDocument *d) {
    return &d->metrics;
}

const iGmDocumentMetrics *requestedMetrics_GmDocument(const iGmDocument *d) {
    return &d->requestedMetrics;
}

iBool isCoverPage_GmDocument(const iGmDocument *d) {
    return d->flags.isCoverPage;
}

iBool isPaged_GmDocument(const iGmDocument *d) {
    return d->metrics.pageHeight > 0;
}

int pageHeight_GmDocument(const iGmDocument *d) {
    return d->metrics.pageHeight;
}

size_t numColumns_GmDocument(const iGmDocument *d) {
    if (!isPaged_GmDocument(d)) {
        return 1;
    }
    return iMax(1, d->size.y / d->metrics.pageHeight);
}

int columnsPerPage_GmDocument(const iGmDocument *d) {
    return isPaged_GmDocument(d) ? iMax(1, d->metrics.columnsPerPage) : 1;
}

size_t numPages_GmDocument(const iGmDocument *d) {
    return iMax(1, numColumns_GmDocument(d) / columnsPerPage_GmDocument(d));
}

iBool setBook_GmDocument(iGmDocument *d, const iGmBookPosition *pos) {
    if (equal_String(&d->bookTitle, pos->title) &&
        equal_String(&d->chapterTitle, pos->chapterTitle) && d->chapter == pos->chapter &&
        d->numChapters == pos->numChapters && d->flags.isBookCover == pos->isCover &&
        d->flags.isBookContents == pos->isContents) {
        return iFalse;
    }
    set_String(&d->bookTitle, pos->title);
    set_String(&d->chapterTitle, pos->chapterTitle);
    d->chapter            = pos->chapter;
    d->numChapters        = pos->numChapters;
    d->flags.isBookContents = pos->isContents;
    if (d->flags.isBookCover != pos->isCover) {
        d->flags.isBookCover = pos->isCover;
        invalidatePalette_GmDocument(d);
    }
    return iTrue;
}

iBool setCoverImage_GmDocument(iGmDocument *d, iGmLinkId linkId) {
    if (d->coverImage == linkId) {
        return iFalse;
    }
    d->coverImage = linkId;
    return iTrue;
}

int chapter_GmDocument(const iGmDocument *d, int *numChapters_out) {
    if (numChapters_out) {
        *numChapters_out = d->numChapters;
    }
    return d->chapter;
}

iRangecc pageHeader_GmDocument(const iGmDocument *d, size_t page, iBool isLeftPage) {
    if (page >= size_Array(&d->pageBands)) {
        return iNullRange;
    }
    const iGmPageBands *bands = constAt_Array(&d->pageBands, page);
    return isLeftPage && !isEmpty_Range(&bands->bookHeader) ? bands->bookHeader : bands->header;
}

iRangecc pageFooter_GmDocument(const iGmDocument *d, size_t page) {
    return page < size_Array(&d->pageBands)
               ? ((const iGmPageBands *) constAt_Array(&d->pageBands, page))->footer
               : iNullRange;
}

size_t columnAt_GmDocument(const iGmDocument *d, int documentY) {
    if (!isPaged_GmDocument(d) || documentY < 0) {
        return 0;
    }
    return iMin((size_t) (documentY / d->metrics.pageHeight), numColumns_GmDocument(d) - 1);
}

void redoLayout_GmDocument(iGmDocument *d) {
    doLayout_GmDocument_(d);
}

void invalidateLayout_GmDocument(iGmDocument *d) {
    d->flags.isLayoutInvalidated = iTrue;
}

iBool isLayoutOutdated_GmDocument(const iGmDocument *d) {
    return d->flags.isLayoutInvalidated || d->layoutFontKey != fontKey_App();
}

static void markLinkRunsVisited_GmDocument_(iGmDocument *d, const iIntSet *linkIds) {
    iForEach(Array, r, &d->layout) {
        iGmRun *run = r.value;
        if (run->linkId && !run->mediaId && contains_IntSet(linkIds, run->linkId)) {
            /* TODO: Does this even work? The font IDs may be different. */
            if (run->font == bold_FontId) {
                run->font = d->style.fonts[text_GmLineType];
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
    iBool wasNormalized = iFalse;
    iRegExp *ansiCursorFwdPattern = new_RegExp("^\x1b\\[([0-9]+)C", 0);
    while (nextSplit_Rangecc(src, "\n", &line)) {
        if (isPreformat) {
            for (const char *ch = line.start; ch != line.end; ch++) {
                if (*ch == 0x1b) {
                    /* We can emulate an ANSI cursor forward sequence by adding spaces. */
                    iRegExpMatch m;
                    init_RegExpMatch(&m);
                    if (matchRange_RegExp(ansiCursorFwdPattern, (iRangecc){ ch, line.end }, &m)) {
                        int num = strtoul(capturedRange_RegExpMatch(&m, 1).start, NULL, 10);
                        if (num > 0 && num < 200 /* arbitrary sanity limit */) {
                            for (int i = 0; i < num; i++) {
                                appendData_Block(&normalized->chars, " ", 1);
                            }
                        }
                        ch = end_RegExpMatch(&m) - 1;
                        wasNormalized = iTrue;
                        continue;
                    }
                }
                if (*ch != '\v') {
                    appendCStrN_String(normalized, ch, 1);
                }
                else {
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
    iUnused(wasNormalized);
    iRelease(ansiCursorFwdPattern);
//    printf("wasNormalized: %d\n", wasNormalized);
//    fflush(stdout);
    set_String(&d->source, collect_String(normalized));
    //normalize_String(&d->source); /* NFC */
//    printf("orig:%zu norm:%zu\n", size_String(&d->origSource), size_String(&d->source));
}

void setUrl_GmDocument(iGmDocument *d, const iString *url) {
    url = canonicalUrl_String(url);
    set_String(&d->url, url);
    deinit_ScopeKey(&d->scopeKey);
    initUrl_ScopeKey(&d->scopeKey, url);
    clear_String(&d->themeUrl);
    /* Pages of a local Gempub have a scope of their own, and each book is a site. */ {
        const iString *archive = localArchiveUrl_Gempub(url);
        if (archive) {
            setCStr_String(&d->scopeKey.scheme, "gempub");
            set_String(&d->scopeKey.site, archive);
            const iString *source =
                sourceUrl_Books(collect_String(localFilePathFromUrl_String(archive)));
            if (source && !isLocalFileScheme_Rangecc(urlScheme_String(source))) {
                set_String(&d->themeUrl, source);
            }
        }
    }
    /* Tinylog entries are themed and scoped as the subscribed tinylog. */ {
        const uint32_t   bmId = tinylogBookmark_Feeds(url);
        const iBookmark *bm   = bmId ? get_Bookmarks(bookmarks_App(), bmId) : NULL;
        d->flags.isTinylogEntry = (bm != NULL);
        clear_String(&d->fixedTitle);
        if (bm) {
            deinit_ScopeKey(&d->scopeKey);
            initUrl_ScopeKey(&d->scopeKey, &bm->url);
            set_String(&d->themeUrl, &bm->url);
            set_String(&d->fixedTitle, &bm->title);
        }
    }
    updateDocPrefs_GmDocument(d);
    setThemeSeed_GmDocument(
        d, paletteSeed_GmDocument_(d), urlThemeSeed_String(themeUrl_GmDocument_(d)));
    iUrl parts;
    init_Url(&parts, url);
    setRange_String(&d->localHost, parts.host);
    updateIconBasedOnUrl_GmDocument_(d);
    if (!cmp_String(url, "about:fonts")) {
        /* This is an interactive internal page. */
        d->flags.enableCommandLinks = iTrue;
    }
    d->flags.isSpartan = equalCase_Rangecc(parts.scheme, "spartan");
    d->flags.isNex     = equalCase_Rangecc(parts.scheme, "nex") &&
                         (isEmpty_Range(&parts.path) || endsWith_Rangecc(parts.path, "/"));
    d->flags.isGopherMenu = isGopherScheme_Rangecc(parts.scheme) &&
                            startsWith_Rangecc(parts.path, "/1");
}

iDeclareType(PendingLink)
struct Impl_PendingLink {
    iString *url;
    iString *title;
};

static void addPendingLink_(void *context, const iRegExpMatch *m) {
    pushBack_Array(context, &(iPendingLink){
        .url   = captured_RegExpMatch(m, 2),
        .title = captured_RegExpMatch(m, 1)
    });
}

static void addPendingNamedLink_(void *context, const iRegExpMatch *m) {
    pushBack_Array(context, &(iPendingLink){
        .url   = newFormat_String("[]%s", cstr_Rangecc(capturedRange_RegExpMatch(m, 2))),
        .title = captured_RegExpMatch(m, 1)
    });
}

static void flushPendingLinks_(iArray *links, const iString *source, iString *out) {
    iRegExp *namePattern = new_RegExp("\n\\s*\\[(.+?)\\]\\s*:\\s*([^\n]+)", 0);
    if (!endsWith_String(out, "\n")) {
        appendCStr_String(out, "\n");
    }
    iForEach(Array, i, links) {
        iPendingLink *pending = i.value;
        const char *url = cstr_String(pending->url);
        if (startsWith_CStr(url, "[]")) {
            /* Find the matching named link. */
            iRegExpMatch m;
            init_RegExpMatch(&m);
            while (matchString_RegExp(namePattern, source, &m)) {
                if (equal_Rangecc(capturedRange_RegExpMatch(&m, 1), url + 2)) {
                    url = cstrCollect_String(captured_RegExpMatch(&m, 2));
                    break;
                }
            }
        }
        appendFormat_String(out, "\n=> %s %s", url, cstr_String(pending->title));
        delete_String(pending->url);
        delete_String(pending->title);
    }
    clear_Array(links);
    iRelease(namePattern);
}

static void convertMarkdownToGemtext_GmDocument_(iGmDocument *d) {
    iAssert(d->origFormat == markdown_SourceFormat);
    /* Get rid of indented preformats. */ {
        iArray        *pendingLinks     = collectNew_Array(sizeof(iPendingLink));
        const iRegExp *imageLinkPattern = iClob(new_RegExp("\n?!\\[(.+)\\]\\(([^)]+)\\)\n?", 0));
        const iRegExp *linkPattern      = iClob(new_RegExp("\\[(.+?)\\]\\(([^)]+)\\)", 0));
        const iRegExp *standaloneLinkPattern = iClob(new_RegExp("^[\\s*_]*\\[(.+?)\\]\\(([^)]+)\\)[\\s*_]*$", 0));
        const iRegExp *namedLinkPattern = iClob(new_RegExp("\\[(.+?)\\]\\[(.+?)\\]", 0));
        const iRegExp *namePattern      = iClob(new_RegExp("\\s*\\[(.+?)\\]\\s*:\\s*([^\n]+)", 0));
        iString result;
        init_String(&result);
        replace_String(&d->source, "&nbsp;", "\u00a0");
        replaceRegExp_String(&d->source, iClob(new_RegExp("```", 0)), "\n```\n", NULL, NULL);
        iRangecc line = iNullRange;
        iBool isPre = iFalse;
        iBool isBlock = iFalse;
        iBool isLastEmpty = iFalse;
        while (nextSplit_Rangecc(range_String(&d->source), "\n", &line)) {
            if (!isPre && !isBlock) {
                if (equal_Rangecc(line, "```")) {
                    isBlock = iTrue;
                    appendCStr_String(&result, "\n```");
                    continue;
                }
                if (*line.start == '#') {
                    flushPendingLinks_(pendingLinks, &d->source, &result);
                }
                if (isEmpty_Range(&line)) {
                    isLastEmpty = iTrue;
                    continue;
                }
                if (isLastEmpty) {
                    appendCStr_String(&result, "\n\n");
                }
                else if (size_Range(&line) >= 2 && isdigit(line.start[0]) &&
                         (line.start[1] == '.' ||
                          (isdigit(line.start[1]) && line.start[2] == '.'))) {
                    appendCStr_String(&result, "\n\n");
                }
                else if (endsWith_String(&result, "  ") ||
                         *line.start == '*' || *line.start == '>' || *line.start == '#' ||
                         (*line.start == '|' && endsWith_String(&result, "|"))) {
                    appendCStr_String(&result, "\n");
                }
                else {
                    appendCStr_String(&result, " ");
                }
                isLastEmpty = iFalse;
            }
            else if (isBlock) {
                if (equal_Rangecc(line, "```")) {
                    isBlock = iFalse;
                    appendCStr_String(&result, "\n```\n");
                }
                else {
                    appendCStr_String(&result, "\n");
                    appendRange_String(&result, line);
                }
                continue;
            }
            if (startsWith_Rangecc(line, "    ")) {
                line.start += 4;
                if (!isPre) {
                    while (!isEmpty_String(&result) &&
                           (last_String(&result) == ' ' || last_String(&result) == '\t')) {
                        removeEnd_String(&result, 1);
                    }
                    appendCStr_String(&result, "```\n");
                    isPre = iTrue;
                }
            }
            else if (isPre) {
                if (!endsWith_String(&result, "\n")) {
                    appendCStr_String(&result, "\n");
                }
                appendCStr_String(&result, "```\n");
                if (equal_Rangecc(line, "```")) {
                    line.start = line.end; /* don't repeat it */
                }
                isPre = iFalse;
            }
            if (isPre) {
                appendRange_String(&result, line);
                appendCStr_String(&result, "\n");
            }
            else {
                iString ln;
                initRange_String(&ln, line);
                replaceRegExp_String(&ln, namePattern, "", NULL, 0);
                replaceRegExp_String(&ln, standaloneLinkPattern, "\n=> \\2 \\1", NULL, NULL);
                replaceRegExp_String(&ln, imageLinkPattern, "\n=> \\2 \\1\n", NULL, NULL);
                replaceRegExp_String(&ln, namedLinkPattern, "\\1", addPendingNamedLink_, pendingLinks);
                replaceRegExp_String(&ln, linkPattern, "\\1", addPendingLink_, pendingLinks);
                replaceRegExp_String(&ln, iClob(new_RegExp("\\*\\*(.+?)\\*\\*", 0)), "\x1b[1m\\1\x1b[0m", NULL, NULL);
                replaceRegExp_String(&ln, iClob(new_RegExp("__(.+?)__", 0)), "\x1b[1m\\1\x1b[0m", NULL, NULL);
                replaceRegExp_String(&ln, iClob(new_RegExp("\\*(.+?)\\*", 0)), "\x1b[3m\\1\x1b[0m", NULL, NULL);
                replaceRegExp_String(&ln, iClob(new_RegExp("\\b_([^_]+?)_\\b", 0)), "\x1b[3m\\1\x1b[0m", NULL, NULL);
                replaceRegExp_String(&ln, iClob(new_RegExp("(?<!`)`([^`]+?)`(?!`)", 0)), "\x1b[11m\\1\x1b[0m", NULL, NULL);
                replace_String(&ln, "\\_", "_");
                append_String(&result, &ln);
                deinit_String(&ln);
            }
        }
        flushPendingLinks_(pendingLinks, &d->source, &result);
        set_String(&d->source, &result);
        deinit_String(&result);
    }
    /* Replace Markdown syntax with equivalent Gemtext, where possible. */
    replaceRegExp_String(&d->source, iClob(new_RegExp("(\\s*\n){2,}", 0)), "\n\n", NULL, NULL); /* normalize paragraph breaks */
    // printf("Converted:\n%s", cstr_String(&d->source));
    d->format = gemini_SourceFormat;
}

static void import_GmDocument_(iGmDocument *d) {
    d->format = d->origFormat;
    d->flags.isConvertedMarkdown = iFalse;
    set_String(&d->source, &d->origSource);
    replace_String(&d->source, "\r\n", "\n");
    /* Remove any null characters. */ {
        const char *ch = constBegin_String(&d->source);
        for (size_t pos = 0; pos < size_String(&d->source); pos++, ch++) {
            if (*ch == 0) {
                remove_Block(&d->source.chars, pos, 1);
                pos--;
                ch = constBegin_String(&d->source) + pos;
            }
        }
    }
    /* Detect use of ANSI escapes. */ {
        iRegExp *ansiEsc = new_RegExp("\x1b[[()]([0-9;AB]*?)[ABCDEFGHJKSTfimn]", 0);
        iRegExpMatch m;
        init_RegExpMatch(&m);
        const iBool found = matchString_RegExp(ansiEsc, &d->origSource, &m);
        iChangeFlags(d->warnings, ansiEscapes_GmDocumentWarning, found);
        iRelease(ansiEsc);
    }
    if (d->viewFormat == plainText_SourceFormat) {
        d->format = plainText_SourceFormat;
        d->ansiEscapes = allowAll_AnsiFlag;
        return;
    }
    /* Do an internal format conversion to Gemtext. */
    iAssert(d->viewFormat == gemini_SourceFormat);
    if (d->format == gemini_SourceFormat) {
        d->ansiEscapes = d->docPrefs.ansiEscapes.value;
    }
    else if (d->format == markdown_SourceFormat) {
        convertMarkdownToGemtext_GmDocument_(d);
        d->flags.isConvertedMarkdown = iTrue;
        d->ansiEscapes = allowAll_AnsiFlag; /* escapes are used for styling */
    }
    else {
        d->ansiEscapes = allowAll_AnsiFlag;
    }
    if (shouldBeNormalized_GmDocument_(d)) {
        normalize_GmDocument(d);
    }
}

void setSource_GmDocument(iGmDocument *d, const iString *source,
                          const iGmDocumentMetrics *metrics, enum iGmDocumentUpdate updateType) {
    /* TODO: This API has been set up to allow partial/progressive updating of the content.
       Currently the entire source is replaced every time, though. */
//    printf("[GmDocument] source update (%zu bytes), width:%d, final:%d\n",
//           size_String(source), width, updateType == final_GmDocumentUpdate);
    if (size_String(source) == size_String(&d->origSource)) {
        iAssert(equal_String(source, &d->origSource));
//        printf("[GmDocument] source is unchanged!\n");
        updateMetrics_GmDocument(d, metrics);
        return; /* Nothing to do. */
    }
    /* Normalize and convert to Gemtext if needed. */
    set_String(&d->origSource, source);
    import_GmDocument_(d);
    setMetrics_GmDocument(d, metrics); /* re-do layout */
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

size_t numPre_GmDocument(const iGmDocument *d) {
    return size_Array(&d->preMeta);
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
    setAnsiFlags_Text(d->ansiEscapes);
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
    setAnsiFlags_Text(allowAll_AnsiFlag);
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
    setAnsiFlags_Text(d->ansiEscapes);
    const iGmRun *run = first;
    while (isValidRun_GmDocument_(d, run)) {
        if ((dir < 0 && bottom_Rect(run->visBounds) <= visRangeY.start) ||
            (dir > 0 && top_Rect(run->visBounds) >= visRangeY.end)) {
            break;
        }
        if (maxCount-- == 0) {
            break;
        }
        render(context, run);
        run += dir;
    }
    setAnsiFlags_Text(allowAll_AnsiFlag);
    return isValidRun_GmDocument_(d, run) ? run : NULL;
}

enum iSourceFormat format_GmDocument(const iGmDocument *d) {
    return d->origFormat;
}

iInt2 size_GmDocument(const iGmDocument *d) {
    return d->size;
}

const iArray *headings_GmDocument(const iGmDocument *d) {
    return &d->headings;
}

const iString *source_GmDocument(const iGmDocument *d) {
    return &d->source;
}

iGmRunRange runRange_GmDocument(const iGmDocument *d) {
    return (iGmRunRange){ constFront_Array(&d->layout), constEnd_Array(&d->layout) };
}

size_t memorySize_GmDocument(const iGmDocument *d) {
    return size_String(&d->origSource) +
           size_String(&d->source) +
           size_Array(&d->layout) * sizeof(iGmRun) +
           size_Array(&d->links)  * sizeof(iGmLink) +
           memorySize_Media(d->media);
}

void setWarning_GmDocument(iGmDocument *d, int warning, iBool set) {
    iChangeFlags(d->warnings, warning, set);
}

int warnings_GmDocument(const iGmDocument *d) {
    return d->warnings;
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
    iAssert(preId_GmRun(run));
    iGmRunRange range = { run, run };
    /* Find the beginning. */
    while (range.start > (const iGmRun *) constData_Array(&d->layout)) {
        const iGmRun *prev = range.start - 1;
        if (preId_GmRun(prev) != preId_GmRun(run)) break;
        range.start = prev;
    }
    /* Find the ending. */
    while (range.end < (const iGmRun *) constEnd_Array(&d->layout)) {
        if (preId_GmRun(range.end) != preId_GmRun(run)) break;
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
        if (run->flags & decoration_GmRunFlag) {
            continue;
        }
        const iRangei span = ySpan_Rect(run->bounds);
        if (contains_Range(&span, pos.y) &&
            pos.x >= left_Rect(run->bounds) && pos.x < right_Rect(run->bounds)) {
            last = run;
            break;
        }
        if (isFirstNonDecoration && pos.y < top_Rect(run->bounds)) {
            last = run;
            break;
        }
        if (top_Rect(run->bounds) >= pos.y) {
            break; /* Below the point. */
        }
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

const iGmRun *precedingRun_GmDocument(const iGmDocument *d, const iGmRun *run) {
    run--;
    if (indexOf_Array(&d->layout, run) == iInvalidPos) {
        return NULL;
    }
    return run;
}

const iGmRun *findInputPromptRun_GmDocument(const iGmDocument *d, iGmLinkId linkId) {
    const iGmRunRange range = runRange_GmDocument(d);
    for (const iGmRun *run = range.start; run != range.end; run++) {
        if (run->mediaType == inputPrompt_MediaType && run->linkId == linkId) {
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

size_t numLinks_GmDocument(const iGmDocument *d) {
    return size_PtrArray(&d->links);
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

iLocalDef iBool isWWW_GmLinkScheme(enum iGmLinkScheme d) {
    return d == http_GmLinkScheme || d == mailto_GmLinkScheme;
}

iLocalDef iBool isOldSchool_GmLinkScheme(enum iGmLinkScheme d) {
    return d == gopher_GmLinkScheme || d == finger_GmLinkScheme;
}

enum iColorId linkColor_GmDocument(const iGmDocument *d, iGmLinkId linkId, enum iGmLinkPart part) {
    const iGmLink *link = link_GmDocument_(d, linkId);
    if (!link) {
        return none_ColorId;
    }
//    const int www_GmLinkFlag            = http_GmLinkFlag | mailto_GmLinkFlag;
//    const int gopherOrFinger_GmLinkFlag = gopher_GmLinkFlag | finger_GmLinkFlag;
    const enum iGmLinkScheme scheme = scheme_GmLinkFlag(link->flags);
    if (link) {
        const iBool isUnsupported = (link->flags & supportedScheme_GmLinkFlag) == 0;
        if (part == icon_GmLinkPart) {
            if (isUnsupported) {
                return tmBadLink_ColorId;
            }
            if (scheme != mailto_GmLinkScheme && link->flags & iconFromLabel_GmLinkFlag) {
                return link->flags & visited_GmLinkFlag ? tmLinkCustomIconVisited_ColorId
                                                        : tmLinkIcon_ColorId;
            }
            if (link->flags & visited_GmLinkFlag) {
                return isWWW_GmLinkScheme(scheme)         ? tmHypertextLinkIconVisited_ColorId
                       : isOldSchool_GmLinkScheme(scheme) ? tmGopherLinkIconVisited_ColorId
                                                          : tmLinkIconVisited_ColorId;
            }
            return isWWW_GmLinkScheme(scheme)         ? tmHypertextLinkIcon_ColorId
                   : isOldSchool_GmLinkScheme(scheme) ? tmGopherLinkIcon_ColorId
                                                      : tmLinkIcon_ColorId;
        }
        if (part == text_GmLinkPart) {
            return isWWW_GmLinkScheme(scheme)         ? tmHypertextLinkText_ColorId
                   : isOldSchool_GmLinkScheme(scheme) ? tmGopherLinkText_ColorId
                                                      : tmLinkText_ColorId;
        }
        if (part == textHover_GmLinkPart) {
            return isWWW_GmLinkScheme(scheme)         ? tmHypertextLinkTextHover_ColorId
                   : isOldSchool_GmLinkScheme(scheme) ? tmGopherLinkTextHover_ColorId
                                                      : tmLinkTextHover_ColorId;
        }
    }
    return tmLinkText_ColorId;
}

iBool isMediaLink_GmDocument(const iGmDocument *d, iGmLinkId linkId) {
    if (isTerminal_Platform()) {
        return iFalse; /* can't show/play media (TODO: image rendering?) */
    }
    /* We may already have media for this link. */
    if (findMediaForLink_Media(d->media, linkId, none_MediaType).type) {
        return iTrue;
    }
    /* Check the URL if it appears like a potential media link. */
    const iString *dstUrl = absoluteUrl_String(&d->url, linkUrl_GmDocument(d, linkId));
    const iRangecc scheme = urlScheme_String(dstUrl);
    if (equalCase_Rangecc(scheme, "gemini") || isGopherScheme_Rangecc(scheme) ||
        equalCase_Rangecc(scheme, "spartan") || equalCase_Rangecc(scheme, "nex") ||
        equalCase_Rangecc(scheme, "finger") || isLocalFileScheme_Rangecc(scheme) ||
        willUseProxy_App(scheme)) {
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

int ansiEscapes_GmDocument(const iGmDocument *d) {
    return d->ansiEscapes;
}

void runBaseAttributes_GmDocument(const iGmDocument *d, const iGmRun *run, int *fontId_out,
                                  int *colorId_out) {
    /* Font and color according to the line type. These are needed because each GmRun is
       a segment of a paragraph, and if the font or color changes inside the run, each wrapped
       segment needs to know both the current font/color and ALSO the base font/color, so
       the default attributes can be restored. */
    if (run->isLede) {
        *fontId_out  = firstParagraph_FontId;
        *colorId_out = tmFirstParagraph_ColorId;
    }
    else {
        *fontId_out  = fontWithSize_Text(d->style.fonts[run->lineType], run->font % max_FontSize); /* retain size */
        *colorId_out = d->style.colors[run->lineType];
    }
}

iBool isJustified_GmRun(const iGmRun *d) {
    return (d->flags & (notJustified_GmRunFlag | endOfLine_GmRunFlag)) == 0;
}

int drawBoundWidth_GmRun(const iGmRun *d) {
    return (d->isRTL ? -1 : 1) * width_Rect(isJustified_GmRun(d) ? d->bounds : d->visBounds);
}

iRangecc findLoc_GmRun(const iGmRun *d, iInt2 pos) {
    if (pos.y < top_Rect(d->bounds)) {
        return (iRangecc){ d->text.start, d->text.start };
    }
    if (pos.y > bottom_Rect(d->bounds)) {
        return (iRangecc){ d->text.end, d->text.end };
    }
    const int x = pos.x - left_Rect(d->bounds);
    if (x <= 0) {
        return (iRangecc){ d->text.start, d->text.start };
    }
    if (x > d->bounds.size.x) {
        return (iRangecc){ d->text.end, d->text.end };
    }
    iRangecc loc;
    iWrapText wt = { .text     = d->text,
                     .mode     = anyCharacter_WrapTextMode,
                     .maxWidth = isJustified_GmRun(d) ? drawBoundWidth_GmRun(d) : 0,
                     .justify  = isJustified_GmRun(d),
                     .hitPoint = init_I2(x, 0) };
    measure_WrapText(&wt, d->font);
    loc.start = loc.end = wt.hitChar_out;
    if (!contains_Range(&d->text, loc.start) && loc.start != d->text.end) {
        return iNullRange; /* it's some other text */
    }
    iChar ch;
    if (d->text.end && d->text.end != loc.start) {
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

#if !defined (NDEBUG)
static void appendRange_GmDocument_(const iGmDocument *d, iString *out, iRangecc range) {
    const iRangecc src = range_String(&d->source);
    if (!range.start) {
        appendCStr_String(out, " -");
    }
    else if (range.start >= src.start && range.end <= src.end) {
        appendFormat_String(out, " %zu:%zu", (size_t) (range.start - src.start), size_Range(&range));
    }
    else {
        appendFormat_String(out, " \"%s\"", cstr_Rangecc(range));
    }
}

void layoutDigest_GmDocument(const iGmDocument *d, iString *out) {
    /* Everything that a layout produces, for comparing layouts. */
    appendFormat_String(out, "size %d %d contentWidth %d title \"%s\"\n",
                        d->size.x, d->size.y, d->contentWidth, cstr_String(&d->title));
    iConstForEach(PtrArray, i, &d->links) {
        const iGmLink *link = i.ptr;
        appendFormat_String(out, "link %zu %s %x", index_PtrArrayConstIterator(&i) + 1,
                            cstr_String(&link->url), link->flags);
        appendRange_GmDocument_(d, out, link->urlRange);
        appendRange_GmDocument_(d, out, link->labelRange);
        appendRange_GmDocument_(d, out, link->labelIcon);
        appendCStr_String(out, "\n");
    }
    iConstForEach(Array, h, &d->headings) {
        const iGmHeading *head = h.value;
        appendFormat_String(out, "heading %d", head->level);
        appendRange_GmDocument_(d, out, head->text);
        appendCStr_String(out, "\n");
    }
    iConstForEach(Array, p, &d->preMeta) {
        const iGmPreMeta *meta = p.value;
        appendFormat_String(out, "pre %x %d %d %d %d", meta->flags, meta->pixelRect.pos.x,
                            meta->pixelRect.pos.y, meta->pixelRect.size.x, meta->pixelRect.size.y);
        appendRange_GmDocument_(d, out, meta->bounds);
        appendRange_GmDocument_(d, out, meta->altText);
        appendRange_GmDocument_(d, out, meta->contents);
        appendFormat_String(out, " runs %zd %zd\n",
                            meta->runRange.start ? meta->runRange.start - (const iGmRun *) constData_Array(&d->layout) : -1,
                            meta->runRange.end ? meta->runRange.end - (const iGmRun *) constData_Array(&d->layout) : -1);
    }
    iConstForEach(Array, r, &d->layout) {
        const iGmRun *run = r.value;
        appendFormat_String(out, "run %d %d %d %d / %d %d %d %d L%u F%x R%u C%u f%u m%u:%u t%u e%u",
                            run->bounds.pos.x, run->bounds.pos.y,
                            run->bounds.size.x, run->bounds.size.y,
                            run->visBounds.pos.x, run->visBounds.pos.y,
                            run->visBounds.size.x, run->visBounds.size.y,
                            run->linkId, run->flags, run->isRTL, run->color, run->font,
                            run->mediaType, run->mediaId, run->lineType, run->isLede);
        appendRange_GmDocument_(d, out, run->text);
        appendCStr_String(out, "\n");
    }
}

void dumpLayout_GmDocument_(const iGmDocument *d) {
    /* Set LAGRANGE_LAYOUT_DUMP to a file path to append each finished layout there. */
    const char *path = getenv("LAGRANGE_LAYOUT_DUMP");
    if (!path || !*path) {
        return;
    }
    iString *out = collectNewFormat_String("=== %s\n", cstr_String(&d->url));
    layoutDigest_GmDocument(d, out);
    FILE *f = fopen(path, "a");
    if (f) {
        fwrite(cstr_String(out), 1, size_String(out), f);
        fclose(f);
    }
}
#endif

iDefineClass(GmDocument)
