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
#include "ui/mediaui.h"
#include "ui/window.h"
#include "visited.h"
#include "bookmarks.h"
#include "app.h"
#include "defs.h"

#include <the_Foundation/intset.h>
#include <the_Foundation/ptrarray.h>
#include <the_Foundation/regexp.h>
#include <the_Foundation/stringarray.h>
#include <the_Foundation/stringset.h>

#include <ctype.h>

iBool isDark_GmDocumentTheme(enum iGmDocumentTheme d) {
    if (d == gray_GmDocumentTheme || d == oceanic_GmDocumentTheme || d == sepia_GmDocumentTheme) {
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

iDeclareType(GmTheme)

struct Impl_GmTheme {
    int ansiEscapes;
    int colors[max_GmLineType];
    int fonts[max_GmLineType];
};

static uint32_t themeHash_(const iBlock *data) {
    /* This is equivalent to a regular CRC-32, but the initial value is zero and the result
       is not inverted. GmDocument originally used a broken implementation of the CRC-32
       function, so replicating the behavior with this. */
    static const uint32_t crc32_tab[] = {
        0x00000000L, 0x77073096L, 0xee0e612cL, 0x990951baL, 0x076dc419L,
        0x706af48fL, 0xe963a535L, 0x9e6495a3L, 0x0edb8832L, 0x79dcb8a4L,
        0xe0d5e91eL, 0x97d2d988L, 0x09b64c2bL, 0x7eb17cbdL, 0xe7b82d07L,
        0x90bf1d91L, 0x1db71064L, 0x6ab020f2L, 0xf3b97148L, 0x84be41deL,
        0x1adad47dL, 0x6ddde4ebL, 0xf4d4b551L, 0x83d385c7L, 0x136c9856L,
        0x646ba8c0L, 0xfd62f97aL, 0x8a65c9ecL, 0x14015c4fL, 0x63066cd9L,
        0xfa0f3d63L, 0x8d080df5L, 0x3b6e20c8L, 0x4c69105eL, 0xd56041e4L,
        0xa2677172L, 0x3c03e4d1L, 0x4b04d447L, 0xd20d85fdL, 0xa50ab56bL,
        0x35b5a8faL, 0x42b2986cL, 0xdbbbc9d6L, 0xacbcf940L, 0x32d86ce3L,
        0x45df5c75L, 0xdcd60dcfL, 0xabd13d59L, 0x26d930acL, 0x51de003aL,
        0xc8d75180L, 0xbfd06116L, 0x21b4f4b5L, 0x56b3c423L, 0xcfba9599L,
        0xb8bda50fL, 0x2802b89eL, 0x5f058808L, 0xc60cd9b2L, 0xb10be924L,
        0x2f6f7c87L, 0x58684c11L, 0xc1611dabL, 0xb6662d3dL, 0x76dc4190L,
        0x01db7106L, 0x98d220bcL, 0xefd5102aL, 0x71b18589L, 0x06b6b51fL,
        0x9fbfe4a5L, 0xe8b8d433L, 0x7807c9a2L, 0x0f00f934L, 0x9609a88eL,
        0xe10e9818L, 0x7f6a0dbbL, 0x086d3d2dL, 0x91646c97L, 0xe6635c01L,
        0x6b6b51f4L, 0x1c6c6162L, 0x856530d8L, 0xf262004eL, 0x6c0695edL,
        0x1b01a57bL, 0x8208f4c1L, 0xf50fc457L, 0x65b0d9c6L, 0x12b7e950L,
        0x8bbeb8eaL, 0xfcb9887cL, 0x62dd1ddfL, 0x15da2d49L, 0x8cd37cf3L,
        0xfbd44c65L, 0x4db26158L, 0x3ab551ceL, 0xa3bc0074L, 0xd4bb30e2L,
        0x4adfa541L, 0x3dd895d7L, 0xa4d1c46dL, 0xd3d6f4fbL, 0x4369e96aL,
        0x346ed9fcL, 0xad678846L, 0xda60b8d0L, 0x44042d73L, 0x33031de5L,
        0xaa0a4c5fL, 0xdd0d7cc9L, 0x5005713cL, 0x270241aaL, 0xbe0b1010L,
        0xc90c2086L, 0x5768b525L, 0x206f85b3L, 0xb966d409L, 0xce61e49fL,
        0x5edef90eL, 0x29d9c998L, 0xb0d09822L, 0xc7d7a8b4L, 0x59b33d17L,
        0x2eb40d81L, 0xb7bd5c3bL, 0xc0ba6cadL, 0xedb88320L, 0x9abfb3b6L,
        0x03b6e20cL, 0x74b1d29aL, 0xead54739L, 0x9dd277afL, 0x04db2615L,
        0x73dc1683L, 0xe3630b12L, 0x94643b84L, 0x0d6d6a3eL, 0x7a6a5aa8L,
        0xe40ecf0bL, 0x9309ff9dL, 0x0a00ae27L, 0x7d079eb1L, 0xf00f9344L,
        0x8708a3d2L, 0x1e01f268L, 0x6906c2feL, 0xf762575dL, 0x806567cbL,
        0x196c3671L, 0x6e6b06e7L, 0xfed41b76L, 0x89d32be0L, 0x10da7a5aL,
        0x67dd4accL, 0xf9b9df6fL, 0x8ebeeff9L, 0x17b7be43L, 0x60b08ed5L,
        0xd6d6a3e8L, 0xa1d1937eL, 0x38d8c2c4L, 0x4fdff252L, 0xd1bb67f1L,
        0xa6bc5767L, 0x3fb506ddL, 0x48b2364bL, 0xd80d2bdaL, 0xaf0a1b4cL,
        0x36034af6L, 0x41047a60L, 0xdf60efc3L, 0xa867df55L, 0x316e8eefL,
        0x4669be79L, 0xcb61b38cL, 0xbc66831aL, 0x256fd2a0L, 0x5268e236L,
        0xcc0c7795L, 0xbb0b4703L, 0x220216b9L, 0x5505262fL, 0xc5ba3bbeL,
        0xb2bd0b28L, 0x2bb45a92L, 0x5cb36a04L, 0xc2d7ffa7L, 0xb5d0cf31L,
        0x2cd99e8bL, 0x5bdeae1dL, 0x9b64c2b0L, 0xec63f226L, 0x756aa39cL,
        0x026d930aL, 0x9c0906a9L, 0xeb0e363fL, 0x72076785L, 0x05005713L,
        0x95bf4a82L, 0xe2b87a14L, 0x7bb12baeL, 0x0cb61b38L, 0x92d28e9bL,
        0xe5d5be0dL, 0x7cdcefb7L, 0x0bdbdf21L, 0x86d3d2d4L, 0xf1d4e242L,
        0x68ddb3f8L, 0x1fda836eL, 0x81be16cdL, 0xf6b9265bL, 0x6fb077e1L,
        0x18b74777L, 0x88085ae6L, 0xff0f6a70L, 0x66063bcaL, 0x11010b5cL,
        0x8f659effL, 0xf862ae69L, 0x616bffd3L, 0x166ccf45L, 0xa00ae278L,
        0xd70dd2eeL, 0x4e048354L, 0x3903b3c2L, 0xa7672661L, 0xd06016f7L,
        0x4969474dL, 0x3e6e77dbL, 0xaed16a4aL, 0xd9d65adcL, 0x40df0b66L,
        0x37d83bf0L, 0xa9bcae53L, 0xdebb9ec5L, 0x47b2cf7fL, 0x30b5ffe9L,
        0xbdbdf21cL, 0xcabac28aL, 0x53b39330L, 0x24b4a3a6L, 0xbad03605L,
        0xcdd70693L, 0x54de5729L, 0x23d967bfL, 0xb3667a2eL, 0xc4614ab8L,
        0x5d681b02L, 0x2a6f2b94L, 0xb40bbe37L, 0xc30c8ea1L, 0x5a05df1bL,
        0x2d02ef8dL
    };
    const uint8_t *bytes = constData_Block(data);
    uint32_t crc32 = 0;
    for (size_t i = 0; i < size_Block(data); ++i) {
        crc32 = crc32_tab[(crc32 ^ bytes[i]) & 0xff] ^ (crc32 >> 8);
    }
    return crc32;
}

/*----------------------------------------------------------------------------------------------*/

struct Impl_GmDocument {
    iObject object;
    enum iSourceFormat origFormat;
    enum iSourceFormat viewFormat; /* what the user prefers to see */
    enum iSourceFormat format;
    iString   origSource; /* original (unnormalized) source */
    iString   source;     /* normalized (possibly converted) source */
    iString   url;        /* for resolving relative links */
    iString   localHost;
    iInt2     size;
    int       outsideMargin;
    iBool     enableCommandLinks; /* `about:command?` only allowed on selected pages */
    iBool     isSpartan;
    iBool     isLayoutInvalidated;
    iArray    layout; /* contents of source, laid out in document space */
    iStringArray auxText; /* generated text that appears on the page but is not part of the source */
    iPtrArray links;
    iString   title; /* the first top-level title */
    iArray    headings;
    iArray    preMeta; /* metadata about preformatted blocks */
    iGmTheme  theme;
    uint32_t  themeSeed;
    iChar     siteIcon;
    iMedia *  media;
    iStringSet *openURLs; /* currently open URLs for highlighting links */
    int       warnings;
    iBool     isPaletteValid;
    iColor    palette[tmMax_ColorId]; /* copy of the color palette */
};

iDefineObjectConstruction(GmDocument)
    
static void import_GmDocument_(iGmDocument *);

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

static void initTheme_GmDocument_(iGmDocument *d) {
    static const int defaultColors[max_GmLineType] = {
        tmParagraph_ColorId,
        tmParagraph_ColorId, /* bullet */
        tmPreformatted_ColorId,
        tmQuote_ColorId,
        tmHeading1_ColorId,
        tmHeading2_ColorId,
        tmHeading3_ColorId,
        tmLinkText_ColorId,
    };
    iGmTheme *theme = &d->theme;
    memcpy(theme->colors, defaultColors, sizeof(theme->colors));
    const iPrefs *prefs    = prefs_App();
    const iBool   isMono   = isForcedMonospace_GmDocument_(d);
    const iBool   isDarkBg = isDark_GmDocumentTheme(
        isDark_ColorTheme(colorTheme_App()) ? prefs->docThemeDark : prefs->docThemeLight);
    const enum iFontId headingFont = isMono ? documentMonospace_FontId : documentHeading_FontId;
    const enum iFontId bodyFont    = isMono ? documentMonospace_FontId : documentBody_FontId;
    theme->fonts[text_GmLineType] = FONT_ID(bodyFont, regular_FontStyle, contentRegular_FontSize);
    theme->fonts[bullet_GmLineType] = FONT_ID(bodyFont, regular_FontStyle, contentRegular_FontSize);
    theme->fonts[preformatted_GmLineType] = preformatted_FontId;
    theme->fonts[quote_GmLineType] = isMono ? monospaceParagraph_FontId : quote_FontId;
    theme->fonts[heading1_GmLineType] = FONT_ID(headingFont, bold_FontStyle, contentHuge_FontSize);
    theme->fonts[heading2_GmLineType] = FONT_ID(headingFont, regular_FontStyle, contentLarge_FontSize);
    theme->fonts[heading3_GmLineType] = FONT_ID(headingFont, bold_FontStyle, contentBig_FontSize);
    theme->fonts[link_GmLineType] = FONT_ID(
        bodyFont,
        ((isDarkBg && prefs->boldLinkDark) || (!isDarkBg && prefs->boldLinkLight)) ? semiBold_FontStyle
                                                                                   : regular_FontStyle,
        contentRegular_FontSize);
}

static enum iGmLineType lineType_GmDocument_(const iGmDocument *d, const iRangecc line) {
    if (d->format == plainText_SourceFormat) {
        return text_GmLineType;
    }
    if (d->isSpartan && startsWith_Rangecc(line, "=:")) {
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
           icon == 0x2a2f /* close X */ ||
           (icon >= 0x2b00 && icon <= 0x2bff) ||
           icon == 0x20bf /* bitcoin */;
}

static iRangecc addLink_GmDocument_(iGmDocument *d, iRangecc line, iGmLinkId *linkId) {
    /* Returns the human-readable label of the link. */
    static iRegExp *pattern_;
    static iRegExp *spartanQueryPattern_;
    if (!pattern_) {
        pattern_ = newGemtextLink_RegExp();
    }
    if (d->isSpartan && !spartanQueryPattern_) {
        spartanQueryPattern_ = new_RegExp("=:\\s*([^\\s]+)(\\s.*)?", 0);
    }
    iGmLink *link = NULL;
    iRegExpMatch m;
    init_RegExpMatch(&m);
    if (d->isSpartan && matchRange_RegExp(spartanQueryPattern_, line, &m)) {
        link = new_GmLink();
        link->urlRange = capturedRange_RegExpMatch(&m, 1);
        link->flags = query_GmLinkFlag;
        setScheme_GmLink_(link, spartan_GmLinkScheme);
        setRange_String(&link->url, link->urlRange);
        set_String(&link->url, canonicalUrl_String(absoluteUrl_String(&d->url, &link->url)));
    }
    if (!link) {
        init_RegExpMatch(&m);
    }
    if (!link && matchRange_RegExp(pattern_, line, &m)) {
        link = new_GmLink();
        link->urlRange = capturedRange_RegExpMatch(&m, 1);
        setRange_String(&link->url, link->urlRange);
        set_String(&link->url, canonicalUrl_String(absoluteUrl_String(&d->url, &link->url)));
        /* If invalid, disregard the link. */
        if ((d->format == gemini_SourceFormat && size_String(&link->url) > prefs_App()->maxUrlSize) ||
            (startsWithCase_String(&link->url, "about:command")
             /* this is a special internal page that allows submitting UI events */
             && !d->enableCommandLinks)) {
            delete_GmLink(link);
            *linkId = 0;
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
            else if (equalCase_Rangecc(parts.scheme, "gopher")) {
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
            else if (equalCase_Rangecc(parts.scheme, "file")) {
                setScheme_GmLink_(link, file_GmLinkScheme);                
            }
            else if (equalCase_Rangecc(parts.scheme, "data")) {
                setScheme_GmLink_(link, data_GmLinkScheme);
                if (startsWith_Rangecc(parts.path, "image/png") ||
                    startsWith_Rangecc(parts.path, "image/jpg") ||
                    startsWith_Rangecc(parts.path, "image/jpeg") ||
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
            /* Check the file name extension, if present. */
            if (!isEmpty_Range(&parts.path)) {
                iString *path = newRange_String(parts.path);
                if (endsWithCase_String(path, ".gif")  || endsWithCase_String(path, ".jpg") ||
                    endsWithCase_String(path, ".jpeg") || endsWithCase_String(path, ".png") ||
                    endsWithCase_String(path, ".tga")  || endsWithCase_String(path, ".psd") ||
#if defined (LAGRANGE_ENABLE_WEBP)
                    endsWithCase_String(path, ".webp") ||
#endif
                    endsWithCase_String(path, ".hdr")  || endsWithCase_String(path, ".pic")) {
                    link->flags |= imageFileExtension_GmLinkFlag;
                }
                else if (endsWithCase_String(path, ".mp3") || endsWithCase_String(path, ".wav") ||
                         endsWithCase_String(path, ".mid") || endsWithCase_String(path, ".ogg")) {
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
        if (!isEmpty_Range(&desc)) {
            line = desc; /* Just show the description. */
            link->flags |= humanReadable_GmLinkFlag;
            /* Check for a custom icon. */
            enum iGmLinkScheme scheme = scheme_GmLinkFlag(link->flags);
            if ((scheme == gemini_GmLinkScheme && ~link->flags & remote_GmLinkFlag) ||
                scheme == about_GmLinkScheme || scheme == file_GmLinkScheme ||
                scheme == mailto_GmLinkScheme || scheme == 0 /* unsupported */) {
                iChar icon = 0;
                int len = 0;
                if ((len = decodeBytes_MultibyteChar(desc.start, desc.end, &icon)) > 0) {
                    if (desc.start + len < desc.end &&
                        ((scheme != mailto_GmLinkScheme && isAllowedLinkIcon_Char_(icon)) ||
                         (scheme == mailto_GmLinkScheme && icon == 0x1f4e7 /* envelope */))) {
                        if (isRegionalIndicatorLetter_Char_(icon)) {
                            iChar combo;
                            int len2 = decodeBytes_MultibyteChar(desc.start + len, desc.end, &combo);
                            if (isRegionalIndicatorLetter_Char_(combo)) {
                                len += len2;
                            }
                        }
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

static void linkContentWasLaidOut_GmDocument_(iGmDocument *d, const iGmMediaInfo *mediaInfo,
                                              uint16_t linkId) {
    iGmLink *link = at_PtrArray(&d->links, linkId - 1);
    link->flags |= content_GmLinkFlag;
    if (mediaInfo && mediaInfo->isPermanent) {
        link->flags |= permanent_GmLinkFlag;
    }
}

static iBool shouldBeNormalized_GmDocument_(const iGmDocument *d) {
    const iPrefs *prefs = prefs_App();
    if (d->format == plainText_SourceFormat) {
        return iFalse; /* plain text is always shown as-is */
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
    return docTheme_Prefs(prefs_App());
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
    int    baseFont;
    int    baseColor;
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

static size_t commit_RunTypesetter_(iRunTypesetter *d, iGmDocument *doc) {
    const size_t n = size_Array(&d->layout);
    pushBackN_Array(&doc->layout, constData_Array(&d->layout), size_Array(&d->layout));
    clear_RunTypesetter_(d);
    return n;
}

static const int maxLedeLines_ = 10;

static void applyAttributes_RunTypesetter_(iRunTypesetter *d, iTextAttrib attrib) {
    /* WARNING: This is duplicated in run_Font_(). Make sure they behave identically. */
    if (attrib.monospace) {
        d->run.font = fontWithFamily_Text(d->baseFont, monospace_FontId);
        d->run.color = tmPreformatted_ColorId;
    }
    else if (attrib.italic) {
        d->run.font = fontWithStyle_Text(d->baseFont, italic_FontStyle);
    }
    else if (attrib.regular) {
        d->run.font = fontWithStyle_Text(d->baseFont, regular_FontStyle);
    }
    else if (attrib.bold) {
        d->run.font = fontWithStyle_Text(d->baseFont, bold_FontStyle);
        d->run.color = tmFirstParagraph_ColorId;
    }
    else if (attrib.light) {
        d->run.font = fontWithStyle_Text(d->baseFont, light_FontStyle);
    }
    else {
        d->run.font  = d->baseFont;
        d->run.color = d->baseColor;
    }    
}

static iBool typesetOneLine_RunTypesetter_(iWrapText *wrap, iRangecc wrapRange, iTextAttrib attrib,
                                           int origin, int advance) {
    iAssert(wrapRange.start <= wrapRange.end);
    trimEnd_Rangecc(&wrapRange);
    iRunTypesetter *d = wrap->context;
    d->run.text = wrapRange;
    applyAttributes_RunTypesetter_(d, attrib);
#if 0
    const int msr = measureRange_Text(d->run.font, wrapRange).advance.x;
    if (iAbs(msr - advance) > 3) {
        printf("\n[RunTypesetter] wrong wrapRange advance! actual:%d wrapped:%d\n\n", msr, advance);
    }
#endif
    if (~d->run.flags & startOfLine_GmRunFlag && d->lineHeightReduction > 0.0f) {
        d->pos.y -= d->lineHeightReduction * lineHeight_Text(d->baseFont);
    }
    d->run.bounds.pos = addX_I2(d->pos, origin + d->indent);
    const iInt2 dims = init_I2(advance, lineHeight_Text(d->baseFont));
    iChangeFlags(d->run.flags, wide_GmRunFlag, (d->isPreformat && dims.x > d->layoutWidth));
    d->run.bounds.size.x    = iMax(wrap->maxWidth, dims.x) - origin; /* Extends to the right edge for selection. */
    d->run.bounds.size.y    = dims.y;
    d->run.visBounds        = d->run.bounds;
    d->run.visBounds.size.x = dims.x;
    d->run.isRTL            = attrib.isBaseRTL;
//    printf("origin:%d isRTL:%d\n{%s}\n", origin, attrib.isBaseRTL, cstr_Rangecc(wrapRange));
    pushBack_Array(&d->layout, &d->run);
    d->run.flags &= ~startOfLine_GmRunFlag;
    d->pos.y += lineHeight_Text(d->baseFont) * prefs_App()->lineSpacing;
    return iTrue; /* continue to next wrapped line */
}

static iBool isHRule_(iRangecc line) {
    if (!startsWith_Rangecc(line, "---")) {
        return iFalse;
    }
    size_t n = 0;
    for (const char *ch = line.start; ch < line.end; ch++) {
        if (*ch != '-') {
            return iFalse;
        }
        n++;
    }
    return n >= 3;
}

static void doLayout_GmDocument_(iGmDocument *d) {
    static iRegExp *ansiPattern_;
    if (!ansiPattern_) {
        ansiPattern_ = makeAnsiEscapePattern_Text(iTrue /* with ESC */);
    }
    const iPrefs *prefs             = prefs_App();
    const iBool   isMono            = isForcedMonospace_GmDocument_(d);
    const iBool   isGopher          = isGopher_GmDocument_(d);
    const iBool   isNarrow          = d->size.x < 90 * gap_Text * aspect_UI;
    const iBool   isVeryNarrow      = d->size.x <= 70 * gap_Text * aspect_UI;
    const iBool   isExtremelyNarrow = d->size.x <= 60 * gap_Text * aspect_UI;
    const iBool   isFullWidthImages = (d->outsideMargin < 5 * gap_UI * aspect_UI);
    
    initTheme_GmDocument_(d);
    d->isLayoutInvalidated = iFalse;
    /* TODO: Collect these parameters into a GmTheme. */
    float indents[max_GmLineType] = { 5, 10, 5, isNarrow ? 5 : 10, 0, 0, 5, 5 };
    if (isExtremelyNarrow) {
        /* Further reduce the margins. */
        indents[text_GmLineType] -= 5;
        indents[heading3_GmLineType] -= 5;
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
        0.0f, 0.25f, 1.0f, 0.5f, 1.5f, 0.5f, 0.25f, 0.25f
    };
    static const char *arrow           = rightArrowhead_Icon;
    static const char *envelope        = envelope_Icon;
    static const char *bullet          = "\u2022";
    static const char *folder          = file_Icon;
    static const char *globe           = globe_Icon;
    static const char *quote           = "\u201c";
    static const char *magnifyingGlass = "\U0001f50d";
    static const char *pointingFinger  = "\U0001f449";
    static const char *uploadArrow     = upload_Icon;
    static const char *image           = photo_Icon;
    clear_Array(&d->layout);
    clear_StringArray(&d->auxText);
    clearLinks_GmDocument_(d);
    clear_Array(&d->headings);
    const iArray *oldPreMeta = collect_Array(copy_Array(&d->preMeta)); /* remember fold states */
    clear_Array(&d->preMeta);
    clear_String(&d->title);
    if (d->size.x <= 0 || isEmpty_String(&d->source)) {
        return;
    }
    updateOpenURLs_GmDocument_(d);
    const iRangecc   content       = range_String(&d->source);
    iRangecc         contentLine   = iNullRange;
    iInt2            pos           = zero_I2();
    iBool            isFirstText   = prefs->bigFirstParagraph && !isTerminal_Platform();
    iBool            addQuoteIcon  = prefs->quoteIcon;
    iBool            isPreformat   = iFalse;
    int              preFont       = preformatted_FontId;
    uint16_t         preId         = 0;
    iBool            enableIndents = iFalse;
    const iBool      isNormalized  = shouldBeNormalized_GmDocument_(d);
    const iBool      isJustified   = prefs->justifyParagraph;
    enum iGmLineType prevType      = text_GmLineType;
    enum iGmLineType prevNonBlankType = text_GmLineType;
    iBool            followsBlank  = iFalse;
    if (d->format == plainText_SourceFormat) {
        isPreformat = iTrue;
        isFirstText = iFalse;
    }
    d->warnings &= ~missingGlyphs_GmDocumentWarning;
    checkMissing_Text(); /* clear the flag */
    setAnsiFlags_Text(d->theme.ansiEscapes);
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
            if (d->origFormat == markdown_SourceFormat) {
                if (isHRule_(line)) {
                    iGmRun hrule = run;
                    const int leftIndent = (isVeryNarrow ? 0 : 5) * gap_Text;
                    const int rightIndent = leftIndent + (isJustified ? -1 : 0) * gap_Text;
                    hrule.visBounds.pos  = add_I2(pos, init_I2(leftIndent, gap_Text * aspect_UI));
                    hrule.visBounds.size = init_I2(d->size.x - leftIndent - rightIndent, 0);
                    hrule.bounds         = zero_Rect(); /* just visual */
                    hrule.text           = iNullRange;
                    hrule.flags          = ruler_GmRunFlag | decoration_GmRunFlag;
                    pushBack_Array(&d->layout, &hrule);
                    pos.y += gap_Text * (1 + aspect_UI);
                    continue;
                }
            }
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
                const float oversizeRatio =
                    meta.pixelRect.size.x /
                    (float) (d->size.x -
                             (enableIndents ? indents[preformatted_GmLineType] : 0) * gap_Text);
                if (oversizeRatio > 1.0f) {
                    preFont--; /* one notch smaller in the font size */
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
                iGmLinkId linkId;
                line = addLink_GmDocument_(d, line, &linkId);
                run.linkId = linkId;
                if (!run.linkId) {
                    /* Invalid formatting. */
                    type = text_GmLineType;
                }
            }
            trimLine_Rangecc(&line, type, isNormalized);
            run.font = d->theme.fonts[type];
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
                continue;
            }
            run.mediaType = max_MediaType; /* preformatted block */
            run.mediaId = preId;
            run.font = (d->format == plainText_SourceFormat ? plainText_FontId : preFont);
            indent = indents[type];
        }
        /* Empty lines don't produce text runs. */
        if (isEmpty_Range(&line)) {
            if (type == quote_GmLineType && !prefs->quoteIcon) {
                /* For quote indicators we still need to produce a run. */
                run.visBounds.pos  = addX_I2(pos, indents[type] * gap_Text);
                run.visBounds.size = init_I2(gap_Text, lineHeight_Text(run.font));
                run.bounds         = zero_Rect(); /* just visual */
                run.text           = iNullRange;
                run.flags          = ruler_GmRunFlag | decoration_GmRunFlag;
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
                iGmRun      altText = { .font  = paragraph_FontId,
                                        .color = tmQuote_ColorId,
                                        .flags = (isBlank ? decoration_GmRunFlag : 0) | altText_GmRunFlag
                };
                const iInt2 margin = preRunMargin_GmDocument(d, 0);
                altText.text       = isBlank ? range_Lang(range_CStr("doc.pre.nocaption"))
                                             : meta->altText;
                iInt2 size = measureWrapRange_Text(altText.font, d->size.x - 2 * margin.x,
                                                   altText.text).bounds.size;
                altText.bounds = altText.visBounds = init_Rect(pos.x, pos.y, d->size.x,
                                                               size.y + 2 * margin.y);
                altText.mediaType = max_MediaType; /* preformatted */
                altText.mediaId = preId;
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
            /* Get rid of ANSI escapes. */
            replaceRegExp_String(&d->title, ansiPattern_, "", NULL, NULL);
        }
        /* List bullet. */
        if (type == bullet_GmLineType) {
            /* TODO: Literata bullet is broken? */
            iGmRun bulRun = run;
            bulRun.color = tmQuote_ColorId;
            bulRun.visBounds.pos = addX_I2(
                pos, (indents[text_GmLineType] - (isTerminal_Platform() ? 0.0f : 0.55f)) * gap_Text);
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
                       init_I2((indents[quote_GmLineType] - 5 * aspect_UI) * gap_Text,
                               !isTerminal_Platform()
                                   ? (lineHeight_Text(quote_FontId) / 2 - bottom_Rect(vis))
                                   : 0));
            quoteRun.bounds = zero_Rect(); /* just visual */
            quoteRun.flags |= decoration_GmRunFlag;
            if (isTerminal_Platform()) {
                quoteRun.font = paragraph_FontId;
            }
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
            const enum iGmLinkScheme scheme = scheme_GmLinkFlag(link->flags);
            icon.text           = range_CStr(link->flags & query_GmLinkFlag    ? (d->isSpartan ? upload_Icon : magnifyingGlass)
                                             : scheme == titan_GmLinkScheme    ? uploadArrow
                                             : scheme == finger_GmLinkScheme   ? pointingFinger
                                             : (scheme == spartan_GmLinkScheme && !d->isSpartan)
                                                                               ? spartan_Icon 
                                             : scheme == mailto_GmLinkScheme   ? envelope
                                             : scheme == data_GmLinkScheme     ? paperclip_Icon
                                             : link->flags & remote_GmLinkFlag ? globe
                                             : link->flags & imageFileExtension_GmLinkFlag ? image
                                             : link->flags & fontpackFileExtension_GmLinkFlag ? fontpack_Icon
                                             : scheme == file_GmLinkScheme     ? folder
                                                                               : arrow);
            /* Custom link icon is shown on local Gemini links only. */
            if (!isEmpty_Range(&link->labelIcon)) {
                icon.text = link->labelIcon;
            }
            /* TODO: List bullets needs the same centering logic. */
            /* Special exception for the tiny bullet operator. */
            icon.font = equal_Rangecc(link->labelIcon, "\u2219") ? preformatted_FontId
                                                                 : paragraph_FontId;
            alignDecoration_GmRun_(&icon, iFalse);
            icon.color = linkColor_GmDocument(d, run.linkId, icon_GmLinkPart);
            icon.flags |= decoration_GmRunFlag | startOfLine_GmRunFlag;
            pushBack_Array(&d->layout, &icon);
        }
        run.lineType = type;
        run.color    = d->theme.colors[type];
        if (d->format == plainText_SourceFormat) {
            run.color = d->theme.colors[text_GmLineType];
        }
        /* Special formatting for the first paragraph (e.g., subtitle, introduction, or lede). */
//        int bigCount = 0;
        if (type == text_GmLineType && isFirstText) {
            if (!isMono) run.font = firstParagraph_FontId;
            run.color   = tmFirstParagraph_ColorId;
            run.isLede  = iTrue;
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
        size_t numRunsAdded = 0;
        const iBool isTextType =
            (type == text_GmLineType || type == bullet_GmLineType || type == quote_GmLineType ||
             type == link_GmLineType);
        const iBool isParagraphJustified = isJustified && isTextType;
        /* Typeset the paragraph. */ {
            iRunTypesetter rts;
            init_RunTypesetter_(&rts);
            rts.run           = run;
            rts.pos           = pos;
            //rts.fonts         = fonts;
            rts.isWordWrapped = (d->format == plainText_SourceFormat ? prefs->plainTextWrap
                                                                     : !isPreformat);
            rts.isPreformat   = isPreformat;
            rts.layoutWidth   = d->size.x;
            rts.indent        = indent * gap_Text;
            /* The right margin is used for balancing lines horizontally. */
            if (isVeryNarrow || isFullWidthImages) {
                rts.rightMargin =
                    gap_Text * (!isExtremelyNarrow && isParagraphJustified && isTextType ? 1 : 0);
            }
            else {
                rts.rightMargin = gap_Text * (isTextType ? 4 : 0);
            }
            if (!isMono) {
#if 0
                /* Upper-level headings are typeset a bit tighter. */
                if (type == heading1_GmLineType) {
                    rts.lineHeightReduction = 0.10f;
                }
                else if (type == heading2_GmLineType) {
                    rts.lineHeightReduction = 0.06f;
                }
#endif
                /* Visited links are never bold. */
                if (run.linkId && !prefs->boldLinkVisited &&
                    linkFlags_GmDocument(d, run.linkId) & visited_GmLinkFlag) {
                    rts.run.font = paragraph_FontId;
                }
            }
            if (!prefs->quoteIcon && type == quote_GmLineType) {
                rts.run.flags |= ruler_GmRunFlag;
            }
            for (;;) { /* need to retry if the font needs changing */
                rts.run.flags |= startOfLine_GmRunFlag;
                if (!isParagraphJustified) {
                    rts.run.flags |= notJustified_GmRunFlag;
                }
                rts.baseFont  = rts.run.font;
                rts.baseColor = rts.run.color;
                iWrapText wrapText = { .text     = line,
                                       .maxWidth = rts.isWordWrapped
                                                       ? d->size.x - run.bounds.pos.x -
                                                             rts.indent - rts.rightMargin
                                                       : 0 /* unlimited */,
                                       .mode     = word_WrapTextMode,
                                       .wrapFunc = typesetOneLine_RunTypesetter_,
                                       .context  = &rts };
                measure_WrapText(&wrapText, rts.run.font);
                if (!rts.run.isLede || size_Array(&rts.layout) <= maxLedeLines_) {
                    if (wrapText.baseDir < 0) {
                        /* Right-aligned paragraphs need margins and decorations to be flipped. */
                        iForEach(Array, pr, &rts.layout) {
                            iGmRun *prun = pr.value;
                            const int offset = rts.rightMargin - rts.indent;
                            prun->bounds.pos.x    += offset;
                            prun->visBounds.pos.x += offset;                            
                        }
                        if (type == bullet_GmLineType || type == link_GmLineType ||
                            (type == quote_GmLineType && prefs->quoteIcon)) {
                            iGmRun *decor = back_Array(&d->layout);
                            iAssert(decor->flags & decoration_GmRunFlag);
                            decor->visBounds.pos.x = d->size.x - width_Rect(decor->visBounds) -
                                                     decor->visBounds.pos.x +
                                                     gap_Text * (type == bullet_GmLineType  ? 1.5f
                                                                 : type == quote_GmLineType ? 0.0f
                                                                                            : 1.0f);
                        }
                    }
                    numRunsAdded = commit_RunTypesetter_(&rts, d);
                    break;
                }
                /* Try again... */
                clear_RunTypesetter_(&rts);
                rts.pos         = pos;
                rts.run.font    = rts.baseFont  = d->theme.fonts[text_GmLineType];
                rts.run.color   = rts.baseColor = d->theme.colors[text_GmLineType];
                rts.run.isLede  = iFalse;
            }
            pos = rts.pos;
            deinit_RunTypesetter_(&rts);
        }
        /* Flag the end of line, too. */
        if (numRunsAdded == 0) {
            pos.y += lineHeight_Text(run.font) * prefs->lineSpacing;
            followsBlank = iTrue;
            continue;
        }
        iGmRun *lastRun = back_Array(&d->layout);
        if (numRunsAdded == 2) {
            /* The last line isn't justified in any case, and justifying just the first line may
               look inappropriate if there isn't other justified paragraphs next to it. */
            lastRun[ 0].flags |= notJustified_GmRunFlag;
            lastRun[-1].flags |= notJustified_GmRunFlag;
        }
        lastRun->flags |= endOfLine_GmRunFlag;
        if (lastRun->linkId && lastRun->flags & startOfLine_GmRunFlag) {
            /* Single-run link: the icon should also be marked endOfLine. */
            lastRun[-1].flags |= endOfLine_GmRunFlag;
        }
        /* Image or audio content. */
        if (type == link_GmLineType) {
            /* TODO: Cleanup here? Move to a function of its own. */
//            enum iMediaType mediaType = none_MediaType;
            const iMediaId media = findMediaForLink_Media(d->media, run.linkId, none_MediaType);
            iGmMediaInfo info;
            info_Media(d->media, media, &info);
            run.mediaType = media.type;
            run.mediaId   = media.id;
            run.text      = iNullRange;
            run.font      = uiLabel_FontId;
            run.color     = 0;
            const int margin = lineHeight_Text(paragraph_FontId) / 2;
            if (media.type) {
                pos.y += margin;
                run.bounds.size.y = 0;
                linkContentWasLaidOut_GmDocument_(d, &info, run.linkId);
            }
            switch (media.type) {
                case image_MediaType: {
                    const iInt2 imgSize = imageSize_Media(d->media, media);
                    run.bounds.pos = pos;
                    run.bounds.size.x = d->size.x;
                    const float aspect = (float) imgSize.y / (float) imgSize.x;
                    run.bounds.size.y = d->size.x * aspect;
                    /* Extend the image to full width, including outside margin, if the viewport
                       is narrow enough. */
                    if (isFullWidthImages) {
                        run.bounds.size.x += d->outsideMargin * 2;
                        run.bounds.size.y += d->outsideMargin * 2 * aspect;
                        run.bounds.pos.x  -= d->outsideMargin;
                    }
                    run.visBounds = run.bounds;
                    const iInt2 maxSize = mulf_I2(
                        imgSize,
                        get_Window()->pixelRatio * iMax(1.0f, (prefs_App()->zoomPercent / 100.0f)));
                    if (width_Rect(run.visBounds) > maxSize.x) {
                        /* Don't scale the image up. */
                        run.visBounds.size.y =
                            run.visBounds.size.y * maxSize.x / width_Rect(run.visBounds);
                        run.visBounds.size.x = maxSize.x;
                        run.visBounds.pos.x  = run.bounds.size.x / 2 - width_Rect(run.visBounds) / 2;
                        run.bounds.size.y    = run.visBounds.size.y;
                    }
                    pushBack_Array(&d->layout, &run);
                    pos.y += run.bounds.size.y + margin / 2;
                    /* Image metadata caption. */ {
                        run.font = FONT_ID(documentBody_FontId, semiBold_FontStyle, contentSmall_FontSize);
                        run.color = tmQuoteIcon_ColorId;
                        run.flags = decoration_GmRunFlag | caption_GmRunFlag;
                        run.mediaId = 0;
                        run.mediaType = 0;
                        run.visBounds.pos.y = pos.y;
                        run.visBounds.size.y = lineHeight_Text(run.font);
                        run.bounds = zero_Rect();
                        iString caption;
                        init_String(&caption);
                        format_String(&caption,
                                      "%s \u2014 %d x %d \u2014 %.1f%s",
                                      info.type,
                                      imgSize.x,
                                      imgSize.y,
                                      info.numBytes / 1.0e6f,
                                      cstr_Lang("mb"));
                        pushBack_StringArray(&d->auxText, &caption);
                        run.text = range_String(&caption);
                        /* Center it. */
                        run.visBounds.size.x = measureRange_Text(run.font, range_String(&caption)).bounds.size.x;
                        run.visBounds.pos.x = d->size.x / 2 - run.visBounds.size.x / 2;
                        deinit_String(&caption);
                        pushBack_Array(&d->layout, &run);
                        pos.y += run.visBounds.size.y + margin;
                    }
                    break;
                }
                case audio_MediaType: {
                    run.bounds.pos    = pos;
                    run.bounds.size.x = d->size.x;
                    run.bounds.size.y = lineHeight_Text(uiContent_FontId) + 3 * gap_UI;
                    run.visBounds     = run.bounds;
                    pushBack_Array(&d->layout, &run);
                    break;
                }
                case download_MediaType: {
                    run.bounds.pos    = pos;
                    run.bounds.size.x = d->size.x;
                    run.bounds.size.y = 2 * lineHeight_Text(uiContent_FontId) + 4 * gap_UI;
                    run.visBounds     = run.bounds;
                    pushBack_Array(&d->layout, &run);
                    break;
                }
                default:
                    break;
            }
            if (media.type && run.bounds.size.y) {
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
        footer.visBounds = (iRect){ pos, init_I2(d->size.x, lineHeight_Text(banner_FontId) * 2) };
        pushBack_Array(&d->layout, &footer);
        pos.y += footer.visBounds.size.y;
    }
#endif
    d->size.y = pos.y;
    if (checkMissing_Text()) {
        d->warnings |= missingGlyphs_GmDocumentWarning;
    }
    /* Go over the preformatted blocks and mark them wide if at least one run is wide. */ {
        /* TODO: Store the dimensions and ranges for later access. */
        iForEach(Array, i, &d->layout) {
            iGmRun *run = i.value;
            if (preId_GmRun(run) && run->flags & wide_GmRunFlag) {
                iGmRunRange block = findPreformattedRange_GmDocument(d, run);
                for (const iGmRun *j = block.start; j != block.end; j++) {
                    iConstCast(iGmRun *, j)->flags |= wide_GmRunFlag;
                }
                /* Skip to the end of the block. */
                i.pos = block.end - (const iGmRun *) constData_Array(&d->layout) - 1;
            }
        }
    }
    setAnsiFlags_Text(allowAll_AnsiFlag);
//    printf("[GmDocument] layout size: %zu runs (%zu bytes)\n",
//           size_Array(&d->layout), size_Array(&d->layout) * sizeof(iGmRun));        
}

void init_GmDocument(iGmDocument *d) {
    d->origFormat = gemini_SourceFormat; /* format of `origSource` */
    d->format     = gemini_SourceFormat; /* format of `source` */
    d->viewFormat = gemini_SourceFormat; /* user's preference */
    init_String(&d->origSource);
    init_String(&d->source);
    init_String(&d->url);
    init_String(&d->localHost);
    d->outsideMargin = 0;
    d->size = zero_I2();
    d->enableCommandLinks = iFalse;
    d->isSpartan = iFalse;
    d->isLayoutInvalidated = iFalse;
    init_Array(&d->layout, sizeof(iGmRun));
    init_StringArray(&d->auxText);
    init_PtrArray(&d->links);
    init_String(&d->title);
    init_Array(&d->headings, sizeof(iGmHeading));
    init_Array(&d->preMeta, sizeof(iGmPreMeta));
    d->themeSeed = 0;
    d->siteIcon = 0;
    d->media = new_Media();
    d->openURLs = NULL;
    d->warnings = 0;
    d->isPaletteValid = iFalse;
    iZap(d->palette);
}

void deinit_GmDocument(iGmDocument *d) {
    iReleasePtr(&d->openURLs);
    delete_Media(d->media);
    deinit_String(&d->title);
    clearLinks_GmDocument_(d);
    deinit_PtrArray(&d->links);
    deinit_Array(&d->preMeta);
    deinit_Array(&d->headings);
    deinit_StringArray(&d->auxText);
    deinit_Array(&d->layout);
    deinit_String(&d->localHost);
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

static void setDerivedThemeColors_(enum iGmDocumentTheme theme) {
    set_Color(tmQuoteIcon_ColorId,
              mix_Color(get_Color(tmQuote_ColorId), get_Color(tmBackground_ColorId), 0.55f));
    set_Color(tmBannerSideTitle_ColorId,
              mix_Color(get_Color(tmBannerTitle_ColorId),
                        get_Color(tmBackground_ColorId),
                        theme == colorfulDark_GmDocumentTheme ? 0.55f : 0));
    /* Banner colors. */
    if (theme == highContrast_GmDocumentTheme) {
        set_Color(tmBannerItemBackground_ColorId, get_Color(tmBannerBackground_ColorId));
        set_Color(tmBannerItemFrame_ColorId, get_Color(tmBannerIcon_ColorId));
        set_Color(tmBannerItemTitle_ColorId, get_Color(tmBannerTitle_ColorId));
        set_Color(tmBannerItemText_ColorId, get_Color(tmBannerTitle_ColorId));
    }
    else {
        const int bannerItemFg = isDark_GmDocumentTheme(currentTheme_()) ? white_ColorId : black_ColorId;
        set_Color(tmBannerItemBackground_ColorId, mix_Color(get_Color(tmBannerBackground_ColorId),
                                                            get_Color(tmBannerTitle_ColorId), 0.1f));
        set_Color(tmBannerItemFrame_ColorId, mix_Color(get_Color(tmBannerBackground_ColorId),
                                                       get_Color(tmBannerTitle_ColorId), 0.4f));
        set_Color(tmBannerItemText_ColorId, mix_Color(get_Color(tmBannerTitle_ColorId),
                                                      get_Color(bannerItemFg), 0.5f));
        set_Color(tmBannerItemTitle_ColorId, get_Color(bannerItemFg));
    }
    /* Modified backgrounds. */
    set_Color(tmBackgroundAltText_ColorId,
              mix_Color(get_Color(tmQuoteIcon_ColorId), get_Color(tmBackground_ColorId), 0.85f));
    set_Color(tmFrameAltText_ColorId,
              mix_Color(get_Color(tmQuoteIcon_ColorId), get_Color(tmBackground_ColorId), 0.4f));
    set_Color(tmBackgroundOpenLink_ColorId,
              mix_Color(get_Color(tmLinkText_ColorId), get_Color(tmBackground_ColorId), 0.90f));
    set_Color(tmLinkFeedEntryDate_ColorId,
              mix_Color(get_Color(tmLinkText_ColorId), get_Color(tmBackground_ColorId), 0.25f));
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

void setThemeSeed_GmDocument(iGmDocument *d, const iBlock *paletteSeed, const iBlock *iconSeed) {
    const iPrefs *        prefs = prefs_App();
    enum iGmDocumentTheme theme = currentTheme_();
    static const iChar siteIcons[] = {
        0x203b,  0x2042,  0x205c,  0x2182,  0x25ed,  0x2600,  0x2601,  0x2604,  0x2605,  0x2606,
        0x265c,  0x265e,  0x2690,  0x2691,  0x2693,  0x2698,  0x2699,  0x26f0,  0x270e,  0x2728,
        0x272a,  0x272f,  0x2731,  0x2738,  0x273a,  0x273e,  0x2740,  0x2742,  0x2744,  0x2748,
        0x274a,  0x2318,  0x2756,  0x2766,  0x27bd,  0x27c1,  0x27d0,  0x2b19,  0x1f300, 0x1f303,
        0x1f306, 0x1f308, 0x1f30a, 0x1f319, 0x1f31f, 0x1f320, 0x1f340, 0x1f4cd, 0x1f4e1, 0x1f531,
        0x1f533, 0x1f657, 0x1f659, 0x1f665, 0x1f668, 0x1f66b, 0x1f78b, 0x1f796, 0x1f79c,
    };
    if (!iconSeed) {
        iconSeed = paletteSeed;
    }
    if (iconSeed && !isEmpty_Block(iconSeed)) {
        const uint32_t seedHash = themeHash_(iconSeed);
        d->siteIcon = siteIcons[(seedHash >> 7) % iElemCount(siteIcons)];
    }
    else {
        d->siteIcon = 0;        
    }
    const iBool isDarkUI = isDark_ColorTheme(colorTheme_App());
    /* Default colors. These are used on "about:" pages and local files, for example. */ {
        /* Link colors are generally the same in all themes. */
        set_Color(tmBadLink_ColorId, get_Color(red_ColorId));
        if (isDark_GmDocumentTheme(theme)) {
            set_Color(tmInlineContentMetadata_ColorId, get_Color(cyan_ColorId));
            set_Color(tmLinkText_ColorId, get_Color(white_ColorId));
            set_Color(tmLinkIcon_ColorId, get_Color(cyan_ColorId));
            set_Color(tmLinkTextHover_ColorId, get_Color(cyan_ColorId));
            set_Color(tmLinkIconVisited_ColorId, get_Color(teal_ColorId));
//            set_Color(tmLinkDomain_ColorId, get_Color(teal_ColorId));
//            set_Color(tmLinkLastVisitDate_ColorId, get_Color(cyan_ColorId));
            set_Color(tmHypertextLinkText_ColorId, get_Color(white_ColorId));
            set_Color(tmHypertextLinkIcon_ColorId, get_Color(orange_ColorId));
            set_Color(tmHypertextLinkTextHover_ColorId, get_Color(orange_ColorId));
            set_Color(tmHypertextLinkIconVisited_ColorId, get_Color(brown_ColorId));
//            set_Color(tmHypertextLinkDomain_ColorId, get_Color(brown_ColorId));
//            set_Color(tmHypertextLinkLastVisitDate_ColorId, get_Color(orange_ColorId));
            set_Color(tmGopherLinkText_ColorId, get_Color(white_ColorId));
            set_Color(tmGopherLinkIcon_ColorId, get_Color(green_ColorId));
            set_Color(tmGopherLinkIconVisited_ColorId, get_Color(darkGreen_ColorId));
            set_Color(tmGopherLinkTextHover_ColorId, get_Color(green_ColorId));
            //set_Color(tmGopherLinkDomain_ColorId, get_Color(magenta_ColorId));
//            set_Color(tmGopherLinkLastVisitDate_ColorId, get_Color(blue_ColorId));
        }
        else {
            set_Color(tmInlineContentMetadata_ColorId, get_Color(brown_ColorId));
            set_Color(tmLinkText_ColorId, get_Color(black_ColorId));
            set_Color(tmLinkIcon_ColorId, get_Color(teal_ColorId));
            set_Color(tmLinkTextHover_ColorId, get_Color(teal_ColorId));
            set_Color(tmLinkIconVisited_ColorId, get_Color(cyan_ColorId));
//            set_Color(tmLinkDomain_ColorId, get_Color(cyan_ColorId));
//            set_Color(tmLinkLastVisitDate_ColorId, get_Color(teal_ColorId));
            set_Color(tmHypertextLinkText_ColorId, get_Color(black_ColorId));
            set_Color(tmHypertextLinkTextHover_ColorId, get_Color(brown_ColorId));
            set_Color(tmHypertextLinkIcon_ColorId, get_Color(brown_ColorId));
            set_Color(tmHypertextLinkIconVisited_ColorId, get_Color(orange_ColorId));
//            set_Color(tmHypertextLinkDomain_ColorId, get_Color(orange_ColorId));
//            set_Color(tmHypertextLinkLastVisitDate_ColorId, get_Color(brown_ColorId));
            set_Color(tmGopherLinkText_ColorId, get_Color(black_ColorId));
            set_Color(tmGopherLinkTextHover_ColorId, get_Color(darkGreen_ColorId));
            set_Color(tmGopherLinkIcon_ColorId, get_Color(darkGreen_ColorId));
            set_Color(tmGopherLinkIconVisited_ColorId, get_Color(green_ColorId));
//            set_Color(tmGopherLinkDomain_ColorId, get_Color(magenta_ColorId));
//            set_Color(tmGopherLinkLastVisitDate_ColorId, get_Color(blue_ColorId));
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
//            set_Color(tmLinkDomain_ColorId, get_Color(teal_ColorId));
            setHsl_Color(tmHypertextLinkIcon_ColorId, get_HSLColor(white_ColorId));
            set_Color(tmHypertextLinkIconVisited_ColorId, mix_Color(get_Color(tmBackground_ColorId), get_Color(white_ColorId), 0.5f));
//            set_Color(tmHypertextLinkDomain_ColorId, get_Color(brown_ColorId));
            setHsl_Color(tmGopherLinkIcon_ColorId, addSatLum_HSLColor(get_HSLColor(tmGopherLinkIcon_ColorId), 0, -0.25f));
            setHsl_Color(tmGopherLinkTextHover_ColorId, addSatLum_HSLColor(get_HSLColor(tmGopherLinkTextHover_ColorId), 0, -0.3f));
        }
        else if (theme == black_GmDocumentTheme) {
            set_Color(tmBackground_ColorId, get_Color(black_ColorId));
            set_Color(tmParagraph_ColorId, mix_Color(get_Color(gray75_ColorId), get_Color(white_ColorId), 0.1f));
            set_Color(tmFirstParagraph_ColorId, mix_Color(get_Color(gray75_ColorId), get_Color(white_ColorId), 0.5f));
            set_Color(tmQuote_ColorId, get_Color(orange_ColorId));
            set_Color(tmPreformatted_ColorId, get_Color(orange_ColorId));
            set_Color(tmHeading1_ColorId, get_Color(cyan_ColorId));
            set_Color(tmHeading2_ColorId, mix_Color(get_Color(cyan_ColorId), get_Color(white_ColorId), 0.66f));
            set_Color(tmHeading3_ColorId, get_Color(white_ColorId));
            set_Color(tmBannerBackground_ColorId, get_Color(black_ColorId));
            set_Color(tmBannerTitle_ColorId, get_Color(cyan_ColorId));
            set_Color(tmBannerIcon_ColorId, get_Color(cyan_ColorId));
        }
        else if (theme == gray_GmDocumentTheme) {
            if (isDarkUI) {
                set_Color(tmBackground_ColorId, mix_Color(get_Color(gray25_ColorId), get_Color(black_ColorId), 0.25f));
                set_Color(tmParagraph_ColorId, mix_Color(get_Color(gray75_ColorId), get_Color(white_ColorId), 0.25f));
                set_Color(tmFirstParagraph_ColorId, mix_Color(get_Color(gray75_ColorId), get_Color(white_ColorId), 0.5f));
                set_Color(tmQuote_ColorId, get_Color(orange_ColorId));
                set_Color(tmPreformatted_ColorId, get_Color(orange_ColorId));
                set_Color(tmHeading1_ColorId, get_Color(cyan_ColorId));
                set_Color(tmHeading2_ColorId, mix_Color(get_Color(cyan_ColorId), get_Color(white_ColorId), 0.66f));
                set_Color(tmHeading3_ColorId, get_Color(white_ColorId));
                set_Color(tmBannerBackground_ColorId, mix_Color(get_Color(gray25_ColorId), get_Color(black_ColorId), 0.5f));
                set_Color(tmBannerTitle_ColorId, get_Color(cyan_ColorId));
                set_Color(tmBannerIcon_ColorId, get_Color(cyan_ColorId));
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
//                set_Color(tmLinkDomain_ColorId, mix_Color(get_Color(cyan_ColorId), get_Color(black_ColorId), 0.33f));
                set_Color(tmHypertextLinkIconVisited_ColorId, mix_Color(get_Color(orange_ColorId), get_Color(black_ColorId), 0.33f));
//                set_Color(tmHypertextLinkDomain_ColorId, mix_Color(get_Color(orange_ColorId), get_Color(black_ColorId), 0.33f));
            }
        }
        else if (theme == sepia_GmDocumentTheme) {
            iHSLColor base = { 40, 0.30f, 0.9f, 1.0f };
            if (isDarkUI) {
                base.lum = 0.15f;
                base.sat = 0.05f;
                iHSLColor textBase = addSatLum_HSLColor(base, 0.6f, 0.60f);
                setHsl_Color(tmBackground_ColorId, base);
                setHsl_Color(tmParagraph_ColorId, textBase);
                setHsl_Color(tmFirstParagraph_ColorId, addSatLum_HSLColor(textBase, 0.0, 0.07f));
                setHsl_Color(tmQuote_ColorId, addSatLum_HSLColor(textBase, 0.7f, -0.05f));
                set_Color(tmPreformatted_ColorId, get_Color(tmQuote_ColorId));
                setHsl_Color(tmHeading1_ColorId, addSatLum_HSLColor(textBase, 1.0f, 0.2f));
                set_Color(tmHeading2_ColorId, getMixed_Color(tmHeading1_ColorId, tmParagraph_ColorId, 0.25f));
                set_Color(tmHeading3_ColorId, getMixed_Color(tmHeading1_ColorId, tmParagraph_ColorId, 0.75f));
                setHsl_Color(tmBannerTitle_ColorId, addSatLum_HSLColor(base, 0.1f, 0.25f));
                setHsl_Color(tmBannerIcon_ColorId, addSatLum_HSLColor(base, 0.1f, 0.35f));
                set_Color(tmLinkText_ColorId, get_Color(tmHeading2_ColorId));
                set_Color(tmHypertextLinkText_ColorId, get_Color(tmHeading2_ColorId));
                set_Color(tmGopherLinkText_ColorId, get_Color(tmHeading2_ColorId));
            }
            else {
                iHSLColor textBase = addSatLum_HSLColor(base, 0.3f, -0.725f);
                setHsl_Color(tmBackground_ColorId, base);
                setHsl_Color(tmParagraph_ColorId, textBase);
                setHsl_Color(tmFirstParagraph_ColorId, textBase);
                setHsl_Color(tmQuote_ColorId, addSatLum_HSLColor(textBase, 0.4f, 0.05f));
                set_Color(tmPreformatted_ColorId, get_Color(tmQuote_ColorId));
                setHsl_Color(tmHeading1_ColorId, addSatLum_HSLColor(textBase, 0.2f, 0.0f));
                set_Color(tmHeading2_ColorId, get_Color(tmHeading1_ColorId));
                set_Color(tmHeading3_ColorId, get_Color(tmParagraph_ColorId));
                setHsl_Color(tmBannerTitle_ColorId, addSatLum_HSLColor(base, 0.0f, -0.35f));
                setHsl_Color(tmBannerIcon_ColorId, addSatLum_HSLColor(base, 0.1f, -0.45f));
                set_Color(tmLinkText_ColorId, get_Color(tmHeading2_ColorId));
                set_Color(tmHypertextLinkText_ColorId, get_Color(tmHeading2_ColorId));
                set_Color(tmGopherLinkText_ColorId, get_Color(tmHeading2_ColorId));
            }
            setHsl_Color(tmBannerBackground_ColorId, setLum_HSLColor(base, base.lum * 0.93f));
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
    if (paletteSeed && !isEmpty_Block(paletteSeed)) {
        d->themeSeed = themeHash_(paletteSeed);
    }
    else {
        d->themeSeed = 0;
    }
    /* Set up colors. */
    if (d->themeSeed || theme == oceanic_GmDocumentTheme) {
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
        float hues[] = { 5, 25, 40, 56, 95, 120, 160, 180, 208, 231, 270, 334 };
        static const struct {
            int index[2];
        } altHues[iElemCount(hues)] = {
            { 2, 3 },  /*  0: red */
            { 8, 3 },  /*  1: reddish orange */
            { 0, 7 },  /*  2: yellowish orange */
            { 5, 7 },  /*  3: yellow */
            { 6, 2 },  /*  4: greenish yellow */
            { 1, 3 },  /*  5: green */
            { 2, 8 },  /*  6: bluish green */
            { 2, 5 },  /*  7: cyan */
            { 6, 10 }, /*  8: sky blue */
            { 3, 11 }, /*  9: blue */
            { 8, 9 },  /* 10: violet */
            { 7, 8 },  /* 11: pink */
        };
        if (d->themeSeed & 0xc00000) {
            /* Hue shift for more variability. */
            iForIndices(i, hues) {
                hues[i] += (d->themeSeed & 0x200000 ? 10 : -10);
            }
        }        
        size_t primIndex = d->themeSeed ? (d->themeSeed & 0xff) % iElemCount(hues) : 2;
        
        if (d->themeSeed && primIndex == 11 && d->themeSeed & 0x4000000) {
            /* De-pink some sites. */
            primIndex = (primIndex + d->themeSeed & 0xf) % 12;
        }        
        
        const int   altIndex[2] = { (d->themeSeed & 0x4) != 0, (d->themeSeed & 0x40) != 0 };
        float       altHue      = hues[d->themeSeed ? altHues[primIndex].index[altIndex[0]] : 8];
        float       altHue2     = hues[d->themeSeed ? altHues[primIndex].index[altIndex[1]] : 8];

        const iBool isBannerLighter = (d->themeSeed & 0x4000) != 0 || !isDarkUI;
        const iBool isDarkBgSat =
            (d->themeSeed & 0x200000) != 0 && (primIndex < 1 || primIndex > 4);

        static const float normLum[] = { 0.8f, 0.7f, 0.675f, 0.65f, 0.55f,
                                         0.6f, 0.475f, 0.475f, 0.75f, 0.8f,
                                         0.85f, 0.85f };

        if (theme == colorfulDark_GmDocumentTheme) {
            iHSLColor base    = { hues[primIndex],
                                  0.8f * (d->themeSeed >> 24) / 255.0f + minSat_HSLColor,
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
//                   isDarkBgSat); fflush(stdout);
            
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
                addSatLum_HSLColor(base, 0, isDarkUI ? -0.04f : 0.06f));
            setHsl_Color(tmBannerIcon_ColorId, addSatLum_HSLColor(base, 0, isDarkUI ? -0.6f : -0.3f));
            setHsl_Color(tmBannerTitle_ColorId, addSatLum_HSLColor(base, 0, isDarkUI ? -0.5f : -0.25f));
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
        else if (theme == black_GmDocumentTheme || (theme == gray_GmDocumentTheme && isDarkUI)) {
            const float     primHue    = hues[primIndex];
            const iHSLColor primBright = { primHue, 1, 0.6f, 1 };
            const iHSLColor primDim    = { primHue, 1, normLum[primIndex] + (theme == gray_GmDocumentTheme ? 0.0f : -0.15f), 1};
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
        else if (theme == oceanic_GmDocumentTheme) {
            const float hues[3] = {
                195, 210, 30    
            };
            const int bgIndex  = primIndex % 2;
            const int altIndex = (d->themeSeed >> 7) & 1 ? 2 : bgIndex;            
            const float lum    = ((d->themeSeed >> 19) & 0xff) / (float) 255.0f;
            const float lum2   = ((d->themeSeed >> 25) & 0xff) / (float) 255.0f;
            const float sat    = ((d->themeSeed >> 8) & 0xff) / (float) 255.0f;;
            iHSLColor base     = { hues[bgIndex],
                                   0.5f + sat * 0.5f,
                                   isDarkUI ? 0.05f + lum * 0.15f : (0.75f + lum * 0.3f),
                                   1.0f };
            iHSLColor altBase  = { hues[altIndex],
                                  0.75f + sat * 0.25f,
                                  isDarkUI ? 0.5f + lum * 0.5f : (0.35f + lum * 0.2f),
                                   1.0f };
            iHSLColor preBase  = { hues[d->themeSeed & 0x100 ? bgIndex : altIndex],
                                   0.75f + sat * 0.25f,
                                   isDarkUI ? 0.5f + lum2 * 0.5f : (0.25f + lum2 * 0.2f),
                                   1.0f };
            if (!isDarkUI) {
                base.sat *= 0.66f;
                if (altIndex == 2) {
//                    altBase.sat = 0.5f;
                    altBase.sat *= 0.8f;
                    altBase.lum += 0.1f;
                    altBase.hue -= 40;
                }
            }
            setHsl_Color(tmBackground_ColorId, base);
            setHsl_Color(tmBannerBackground_ColorId, addSatLum_HSLColor(base, 0.1f, isDarkUI ? 0.04f * (isBannerLighter ? 1 : -1) : 0.05f));
//            set_Color(tmBannerBackground_ColorId, getMixed_Color(tmBackground_ColorId, uiBackground_ColorId, 0.5f));
            setHsl_Color(tmBannerIcon_ColorId, addSatLum_HSLColor(base, 1.0f, isDarkUI ? 0.5f : -0.5f));
            setHsl_Color(tmBannerTitle_ColorId, addSatLum_HSLColor(base, 0.1f, isDarkUI ? 0.3f : -0.5f));
//            setHsl_Color(tmBannerSideTitle_ColorId, addSatLum_HSLColor(base, 0.1f, 0.04f * (isBannerLighter ? 1 : -1)));
            setHsl_Color(tmParagraph_ColorId, addSatLum_HSLColor(base, -0.3f, isDarkUI ? (0.5f + lum * 0.1f) : -0.6f));
            setHsl_Color(tmPreformatted_ColorId, preBase); //addSatLum_HSLColor(preBase, 0.4f, isDarkUI ? 0.4f : -0.2f));
            set_Color(tmQuote_ColorId, get_Color(tmPreformatted_ColorId));
            setHsl_Color(tmLinkText_ColorId,
                         addSatLum_HSLColor(get_HSLColor(tmParagraph_ColorId), 0, isDarkUI ? 0.2f : -0.2f));
            if (!isDarkUI) {
                setHsl_Color(tmLinkIconVisited_ColorId,
                             addSatLum_HSLColor(get_HSLColor(tmLinkIconVisited_ColorId), 0.0f, -0.25f * (1-lum)));
            }
            setHsl_Color(tmHypertextLinkText_ColorId,
                         addSatLum_HSLColor(get_HSLColor(tmHypertextLinkIcon_ColorId), 0, lum * (isDarkUI ? 0.2f : -0.2f)));
            set_Color(tmHypertextLinkText_ColorId,
                      getMixed_Color(tmHypertextLinkText_ColorId, tmParagraph_ColorId, 0.66f));
            set_Color(tmGopherLinkText_ColorId, getMixed_Color(tmLinkText_ColorId, tmGopherLinkTextHover_ColorId, 0.2f));
            setHsl_Color(tmHeading1_ColorId, altBase);
            set_Color(tmHeading2_ColorId, get_Color(tmHeading1_ColorId));
            set_Color(tmHeading3_ColorId, get_Color(tmParagraph_ColorId));
            setHsl_Color(tmFirstParagraph_ColorId, addSatLum_HSLColor(get_HSLColor(tmParagraph_ColorId), 0.0f, isDarkUI ? 0.1f : -0.2f));
            set_Color(tmInlineContentMetadata_ColorId, get_Color(tmHeading3_ColorId));
        }
        /* Tone down the link colors a bit because bold white is quite strong to look at. */
        if ((isDark_GmDocumentTheme(theme) || theme == white_GmDocumentTheme) &&
            theme != oceanic_GmDocumentTheme && theme != sepia_GmDocumentTheme) {
            iHSLColor base = { hues[primIndex], 1.0f, normLum[primIndex], 1.0f };
            if (theme == gray_GmDocumentTheme) {
                setHsl_Color(tmLinkText_ColorId,
                             addSatLum_HSLColor(get_HSLColor(tmLinkText_ColorId), 0.0f, -0.15f));
                set_Color(tmLinkText_ColorId, mix_Color(get_Color(tmLinkText_ColorId),
                                                        rgb_HSLColor(base), 0.1f));
            }
            else {
                /* Tinted with base color. */
                set_Color(tmLinkText_ColorId, mix_Color(get_Color(tmLinkText_ColorId),
                                                        rgb_HSLColor(base), 0.25f));
                if (theme == black_GmDocumentTheme) {
                    /* With a full-on black background, links can have a bit of tint color. */
                    setHsl_Color(
                        tmLinkText_ColorId,
                        addSatLum_HSLColor(get_HSLColor(tmLinkText_ColorId), -0.5f, -0.1f));
                }
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
    if (iconSeed) {
        if (equal_CStr(cstr_Block(iconSeed), "gemini.circumlunar.space")) {
            d->siteIcon = 0x264a; /* gemini symbol */
        }
        else if (equal_CStr(cstr_Block(iconSeed), "spartan.mozz.us")) {
            d->siteIcon = 0x1f4aa; /* arm flex */
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
    if (!d->isPaletteValid) {
        /* Recompute the palette since it's needed now. */
        setThemeSeed_GmDocument(
            (iGmDocument *) d, urlPaletteSeed_String(&d->url), urlThemeSeed_String(&d->url));
    }
    iAssert(d->isPaletteValid);
    memcpy(get_Root()->tmPalette, d->palette, sizeof(d->palette));
}

void invalidatePalette_GmDocument(iGmDocument *d) {
    d->isPaletteValid = iFalse;
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

void setWidth_GmDocument(iGmDocument *d, int width, int canvasWidth) {
    d->size.x        = width;
    d->outsideMargin = iMax(0, (canvasWidth - width) / 2); /* distance to edge of the canvas */
    doLayout_GmDocument_(d); /* TODO: just flag need-layout and do it later */
}

iBool updateWidth_GmDocument(iGmDocument *d, int width, int canvasWidth) {
    if (d->size.x != width || d->isLayoutInvalidated) {
        setWidth_GmDocument(d, width, canvasWidth);
        return iTrue;
    }
    return iFalse;
}

void redoLayout_GmDocument(iGmDocument *d) {
    doLayout_GmDocument_(d);
}

void invalidateLayout_GmDocument(iGmDocument *d) {
    d->isLayoutInvalidated = iTrue;
}

static void markLinkRunsVisited_GmDocument_(iGmDocument *d, const iIntSet *linkIds) {
    iForEach(Array, r, &d->layout) {
        iGmRun *run = r.value;
        if (run->linkId && !run->mediaId && contains_IntSet(linkIds, run->linkId)) {
            /* TODO: Does this even work? The font IDs may be different. */
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
    iBool wasNormalized = iFalse;
    while (nextSplit_Rangecc(src, "\n", &line)) {
        if (isPreformat) {
            for (const char *ch = line.start; ch != line.end; ch++) {
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
//    printf("wasNormalized: %d\n", wasNormalized);
//    fflush(stdout);
    set_String(&d->source, collect_String(normalized));
    //normalize_String(&d->source); /* NFC */
//    printf("orig:%zu norm:%zu\n", size_String(&d->origSource), size_String(&d->source));
}

void setUrl_GmDocument(iGmDocument *d, const iString *url) {
    url = canonicalUrl_String(url);
    set_String(&d->url, url);
    setThemeSeed_GmDocument(d, urlPaletteSeed_String(url), urlThemeSeed_String(url));
    iUrl parts;
    init_Url(&parts, url);
    d->isSpartan = equalCase_Rangecc(parts.scheme, "spartan");
    setRange_String(&d->localHost, parts.host);
    updateIconBasedOnUrl_GmDocument_(d);
    if (!cmp_String(url, "about:fonts")) {
        /* This is an interactive internal page. */
        d->enableCommandLinks = iTrue;
    }
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
//    printf("Converted:\n%s", cstr_String(&d->source));
    d->format = gemini_SourceFormat;
}

static void import_GmDocument_(iGmDocument *d) {
    d->format = d->origFormat;
    set_String(&d->source, &d->origSource);
    replace_String(&d->source, "\r\n", "\n");
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
        d->theme.ansiEscapes = allowAll_AnsiFlag;
        return;
    }
    /* Do an internal format conversion to Gemtext. */
    iAssert(d->viewFormat == gemini_SourceFormat);
    if (d->format == gemini_SourceFormat) {
        d->theme.ansiEscapes = prefs_App()->gemtextAnsiEscapes;
    }
    else if (d->format == markdown_SourceFormat) {
        convertMarkdownToGemtext_GmDocument_(d);
        d->theme.ansiEscapes = allowAll_AnsiFlag; /* escapes are used for styling */
    }
    else {
        d->theme.ansiEscapes = allowAll_AnsiFlag;
    }
    if (shouldBeNormalized_GmDocument_(d)) {
        normalize_GmDocument(d);
    }
}

void setSource_GmDocument(iGmDocument *d, const iString *source, int width, int canvasWidth,
                          enum iGmDocumentUpdate updateType) {
    /* TODO: This API has been set up to allow partial/progressive updating of the content.
       Currently the entire source is replaced every time, though. */
//    printf("[GmDocument] source update (%zu bytes), width:%d, final:%d\n",
//           size_String(source), width, updateType == final_GmDocumentUpdate);
    if (size_String(source) == size_String(&d->origSource)) {
        iAssert(equal_String(source, &d->origSource));
//        printf("[GmDocument] source is unchanged!\n");
        updateWidth_GmDocument(d, width, canvasWidth);
        return; /* Nothing to do. */
    }
    /* Normalize and convert to Gemtext if needed. */
    set_String(&d->origSource, source);
    import_GmDocument_(d);
    setWidth_GmDocument(d, width, canvasWidth); /* re-do layout */    
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
    setAnsiFlags_Text(d->theme.ansiEscapes);
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
    setAnsiFlags_Text(d->theme.ansiEscapes);
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

#if 0
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
#endif

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
        /*
        if (part == domain_GmLinkPart) {
            if (isUnsupported) {
                return tmBadLink_ColorId;
            }
            return isWWW_GmLinkScheme(scheme)         ? tmHypertextLinkDomain_ColorId
                   : isOldSchool_GmLinkScheme(scheme) ? tmGopherLinkDomain_ColorId
                                                      : tmLinkDomain_ColorId;
        }
        if (part == visited_GmLinkPart) {
            return isWWW_GmLinkScheme(scheme)         ? tmHypertextLinkLastVisitDate_ColorId
                   : isOldSchool_GmLinkScheme(scheme) ? tmGopherLinkLastVisitDate_ColorId
                                                      : tmLinkLastVisitDate_ColorId;
        }
        */
    }
    return tmLinkText_ColorId;
}

iBool isMediaLink_GmDocument(const iGmDocument *d, iGmLinkId linkId) {
    if (isTerminal_Platform()) {
        return iFalse; /* can't show/play media (TODO: image rendering?) */
    }
    const iString *dstUrl = absoluteUrl_String(&d->url, linkUrl_GmDocument(d, linkId));
    const iRangecc scheme = urlScheme_String(dstUrl);
    if (equalCase_Rangecc(scheme, "gemini") || equalCase_Rangecc(scheme, "gopher") ||
        equalCase_Rangecc(scheme, "spartan") || equalCase_Rangecc(scheme, "finger") ||
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

int ansiEscapes_GmDocument(const iGmDocument *d) {
    return d->theme.ansiEscapes;
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
        *fontId_out  = fontWithSize_Text(d->theme.fonts[run->lineType], run->font % max_FontSize); /* retain size */
        *colorId_out = d->theme.colors[run->lineType];
    }
}

iBool isJustified_GmRun(const iGmRun *d) {
    return prefs_App()->justifyParagraph &&
           (d->flags & (notJustified_GmRunFlag | endOfLine_GmRunFlag)) == 0;
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
                     .maxWidth = drawBoundWidth_GmRun(d),
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

iDefineClass(GmDocument)
