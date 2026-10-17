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
#include "palette.h"
#include "app.h"
#include "bookmarks.h"
#include "feeds.h"
#include "ui/root.h"

enum iGmDocumentTheme currentTheme_GmDocument_(const iGmDocument *d) {
    return docTheme_DocPrefs(&d->docPrefs, isDark_ColorTheme(colorTheme_App()));
}

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
        const int bannerItemFg = isDark_GmDocumentTheme(theme) ? white_ColorId : black_ColorId;
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

void updateIconBasedOnUrl_GmDocument_(iGmDocument *d) {
    if (d->flags.isTinylogEntry) {
        iTinylogInfo info;
        init_TinylogInfo(&info);
        if (tinylogInfo_Feeds(&d->url, &info) && !isEmpty_String(&info.avatar)) {
            d->siteIcon = first_String(&info.avatar);
        }
        else {
            const iBookmark *bm = get_Bookmarks(bookmarks_App(), info.bookmarkId);
            if (bm && bm->icon) {
                d->siteIcon = bm->icon;
            }
        }
        deinit_TinylogInfo(&info);
        return;
    }
    const iChar userIcon = siteIcon_Bookmarks(bookmarks_App(), &d->url);
    if (userIcon) {
        d->siteIcon = userIcon;
    }
}

static void setUiThemeColors_(void) {
    /* Documents shown as part of the UI use UI colors. */
    static const int map[][2] = {
        { tmBackground_ColorId, uiBackgroundSidebar_ColorId },
        { tmParagraph_ColorId, uiText_ColorId },
        { tmFirstParagraph_ColorId, uiText_ColorId },
        { tmQuote_ColorId, uiTextDim_ColorId },
        { tmPreformatted_ColorId, uiTextDim_ColorId },
        { tmHeading1_ColorId, uiHeading_ColorId },
        { tmHeading2_ColorId, uiHeading_ColorId },
        { tmHeading3_ColorId, uiSubheading_ColorId },
        { tmBannerBackground_ColorId, uiBackgroundSidebar_ColorId },
        { tmBannerTitle_ColorId, uiTextStrong_ColorId },
        { tmBannerIcon_ColorId, uiIcon_ColorId },
        { tmInlineContentMetadata_ColorId, uiAnnotation_ColorId },
        { tmBadLink_ColorId, uiTextCaution_ColorId },
        { tmLinkIcon_ColorId, uiIcon_ColorId },
        { tmLinkIconVisited_ColorId, uiIcon_ColorId },
        { tmLinkText_ColorId, uiTextStrong_ColorId },
        { tmLinkTextHover_ColorId, uiTextAction_ColorId },
        { tmHypertextLinkIcon_ColorId, uiIcon_ColorId },
        { tmHypertextLinkIconVisited_ColorId, uiIcon_ColorId },
        { tmHypertextLinkText_ColorId, uiTextStrong_ColorId },
        { tmHypertextLinkTextHover_ColorId, uiTextAction_ColorId },
        { tmGopherLinkIcon_ColorId, uiIcon_ColorId },
        { tmGopherLinkIconVisited_ColorId, uiIcon_ColorId },
        { tmGopherLinkText_ColorId, uiTextStrong_ColorId },
        { tmGopherLinkTextHover_ColorId, uiTextAction_ColorId },
    };
    iForIndices(i, map) {
        set_Color(map[i][0], get_Color(map[i][1]));
    }
    setDerivedThemeColors_(isDark_ColorTheme(colorTheme_App()) ? black_GmDocumentTheme
                                                               : white_GmDocumentTheme);
}

const iString *themeUrl_GmDocument_(const iGmDocument *d) {
    return isEmpty_String(&d->themeUrl) ? &d->url : &d->themeUrl;
}

const iBlock *paletteSeed_GmDocument_(const iGmDocument *d) {
    if (!isEmpty_String(&d->themeUrl)) {
        return urlPaletteSeed_String(&d->themeUrl);
    }
    const iString *seed = d->docPrefs.paletteSeed;
    if (!isEmpty_String(seed) && !isLocalFileScheme_Rangecc(urlScheme_String(&d->url))) {
        return utf8_String(seed);
    }
    return urlThemeSeed_String(&d->url);
}

iBool isDark_GmDocumentTheme(enum iGmDocumentTheme d) {
    if (d == gray_GmDocumentTheme || d == oceanic_GmDocumentTheme ||
        d == arboreal_GmDocumentTheme || d == sepia_GmDocumentTheme) {
        return isDark_ColorTheme(colorTheme_App());
    }
    return d == colorfulDark_GmDocumentTheme || d == black_GmDocumentTheme;
}

void setThemeSeed_GmDocument(iGmDocument *d, const iBlock *paletteSeed, const iBlock *iconSeed) {
    if (d->flags.isUiTheme) {
        /* No seed is used for a document that is part of the UI. */
        d->siteIcon = 0;
        setUiThemeColors_();
        memcpy(d->palette, get_Root()->tmPalette, sizeof(d->palette));
        d->flags.isPaletteValid = iTrue;
        return;
    }
    const iDocPrefs *     prefs = &d->docPrefs;
    enum iGmDocumentTheme theme = currentTheme_GmDocument_(d);
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
            set_Color(tmHypertextLinkText_ColorId, get_Color(white_ColorId));
            set_Color(tmHypertextLinkIcon_ColorId, get_Color(orange_ColorId));
            set_Color(tmHypertextLinkTextHover_ColorId, get_Color(orange_ColorId));
            set_Color(tmHypertextLinkIconVisited_ColorId, get_Color(brown_ColorId));
            set_Color(tmGopherLinkText_ColorId, get_Color(white_ColorId));
            set_Color(tmGopherLinkIcon_ColorId, get_Color(green_ColorId));
            set_Color(tmGopherLinkIconVisited_ColorId, get_Color(darkGreen_ColorId));
            set_Color(tmGopherLinkTextHover_ColorId, get_Color(green_ColorId));
        }
        else {
            set_Color(tmInlineContentMetadata_ColorId, get_Color(brown_ColorId));
            set_Color(tmLinkText_ColorId, get_Color(black_ColorId));
            set_Color(tmLinkIcon_ColorId, get_Color(teal_ColorId));
            set_Color(tmLinkTextHover_ColorId, get_Color(teal_ColorId));
            set_Color(tmLinkIconVisited_ColorId, get_Color(cyan_ColorId));
            set_Color(tmHypertextLinkText_ColorId, get_Color(black_ColorId));
            set_Color(tmHypertextLinkTextHover_ColorId, get_Color(brown_ColorId));
            set_Color(tmHypertextLinkIcon_ColorId, get_Color(brown_ColorId));
            set_Color(tmHypertextLinkIconVisited_ColorId, get_Color(orange_ColorId));
            set_Color(tmGopherLinkText_ColorId, get_Color(black_ColorId));
            set_Color(tmGopherLinkTextHover_ColorId, get_Color(darkGreen_ColorId));
            set_Color(tmGopherLinkIcon_ColorId, get_Color(darkGreen_ColorId));
            set_Color(tmGopherLinkIconVisited_ColorId, get_Color(green_ColorId));
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
        else if (theme == colorfulLight_GmDocumentTheme ||
                 theme == vibrantLight_GmDocumentTheme) {
            const iHSLColor base = addSatLum_HSLColor(get_HSLColor(teal_ColorId), -0.3f, 0.5f);
            setHsl_Color(tmBackground_ColorId, base);
            set_Color(tmParagraph_ColorId, get_Color(black_ColorId));
            set_Color(tmFirstParagraph_ColorId, get_Color(black_ColorId));
            setHsl_Color(tmQuote_ColorId, addSatLum_HSLColor(base, 0, -0.25f));
            setHsl_Color(tmPreformatted_ColorId, addSatLum_HSLColor(base, 0, -0.3f));
            setHsl_Color(tmHeading1_ColorId, addSatLum_HSLColor(base, 1.0f, -0.37f));
            set_Color(tmHeading2_ColorId, mix_Color(get_Color(tmHeading1_ColorId), get_Color(black_ColorId), 0.5f));
            set_Color(tmHeading3_ColorId, mix_Color(get_Color(tmBackground_ColorId), get_Color(black_ColorId), 0.4f));
            setHsl_Color(tmBannerBackground_ColorId, addSatLum_HSLColor(base, 0, -0.1f));
            setHsl_Color(tmBannerIcon_ColorId, addSatLum_HSLColor(base, 0, -0.4f));
            setHsl_Color(tmBannerTitle_ColorId, addSatLum_HSLColor(base, 0, -0.4f));
            setHsl_Color(tmLinkIcon_ColorId, addSatLum_HSLColor(get_HSLColor(teal_ColorId), 0, 0));
            set_Color(tmLinkIconVisited_ColorId, mix_Color(get_Color(tmBackground_ColorId), get_Color(teal_ColorId), 0.35f));
            setHsl_Color(tmHypertextLinkIcon_ColorId, get_HSLColor(white_ColorId));
            set_Color(tmHypertextLinkIconVisited_ColorId, mix_Color(get_Color(tmBackground_ColorId), get_Color(white_ColorId), 0.5f));
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
                set_Color(tmBannerBackground_ColorId, mix_Color(get_Color(gray25_ColorId), get_Color(black_ColorId), 0.4f));
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
                set_Color(tmHypertextLinkIconVisited_ColorId, mix_Color(get_Color(orange_ColorId), get_Color(black_ColorId), 0.33f));
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
        else if (theme == paperwhite_GmDocumentTheme) {
            const iHSLColor base     = { 40, 0.22f, 0.965f, 1.0f };
            const iHSLColor textBase = { 30, 0.10f, 0.21f, 1.0f };
            setHsl_Color(tmBackground_ColorId, base);
            setHsl_Color(tmParagraph_ColorId, textBase);
            setHsl_Color(tmFirstParagraph_ColorId, addSatLum_HSLColor(textBase, 0.05f, -0.06f));
            setHsl_Color(tmQuote_ColorId, (iHSLColor){ 32, 0.18f, 0.40f, 1.0f });
            set_Color(tmPreformatted_ColorId, get_Color(tmQuote_ColorId));
            setHsl_Color(tmHeading1_ColorId, (iHSLColor){ 28, 0.50f, 0.28f, 1.0f });
            set_Color(tmHeading2_ColorId, getMixed_Color(tmHeading1_ColorId, tmParagraph_ColorId, 0.4f));
            setHsl_Color(tmHeading3_ColorId, (iHSLColor){ 30, 0.06f, 0.36f, 1.0f });
            setHsl_Color(tmBannerBackground_ColorId, setLum_HSLColor(base, base.lum * 0.97f));
            setHsl_Color(tmBannerTitle_ColorId, (iHSLColor){ 32, 0.12f, 0.42f, 1.0f });
            setHsl_Color(tmBannerIcon_ColorId, (iHSLColor){ 28, 0.45f, 0.45f, 1.0f });
            setHsl_Color(tmInlineContentMetadata_ColorId, (iHSLColor){ 28, 0.40f, 0.40f, 1.0f });
            /* Links in muted sepia and gray tones. */
            setHsl_Color(tmLinkText_ColorId, addSatLum_HSLColor(textBase, 0.05f, -0.08f));
            set_Color(tmHypertextLinkText_ColorId, get_Color(tmLinkText_ColorId));
            set_Color(tmGopherLinkText_ColorId, get_Color(tmLinkText_ColorId));
            setHsl_Color(tmLinkIcon_ColorId, (iHSLColor){ 28, 0.55f, 0.42f, 1.0f });
            setHsl_Color(tmHypertextLinkIcon_ColorId, (iHSLColor){ 15, 0.45f, 0.45f, 1.0f });
            setHsl_Color(tmGopherLinkIcon_ColorId, (iHSLColor){ 60, 0.25f, 0.36f, 1.0f });
            set_Color(tmLinkTextHover_ColorId, get_Color(tmLinkIcon_ColorId));
            set_Color(tmHypertextLinkTextHover_ColorId, get_Color(tmHypertextLinkIcon_ColorId));
            set_Color(tmGopherLinkTextHover_ColorId, get_Color(tmGopherLinkIcon_ColorId));
            set_Color(tmLinkIconVisited_ColorId, getMixed_Color(tmLinkIcon_ColorId, tmBackground_ColorId, 0.5f));
            set_Color(tmHypertextLinkIconVisited_ColorId,
                      getMixed_Color(tmHypertextLinkIcon_ColorId, tmBackground_ColorId, 0.5f));
            set_Color(tmGopherLinkIconVisited_ColorId,
                      getMixed_Color(tmGopherLinkIcon_ColorId, tmBackground_ColorId, 0.5f));
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
                color.sat *= prefs->saturation.value;
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
    if (d->themeSeed || theme == oceanic_GmDocumentTheme || theme == arboreal_GmDocumentTheme) {
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
            { 7, 6 },  /*  2: yellowish orange */
            { 5, 7 },  /*  3: yellow */
            { 8, 2 },  /*  4: greenish yellow */
            { 2, 3 },  /*  5: green */
            { 2, 8 },  /*  6: bluish green */
            { 2, 5 },  /*  7: cyan */
            { 6, 10 }, /*  8: sky blue */
            { 3, 11 }, /*  9: blue */
            { 8, 9 },  /* 10: violet */
            { 7, 8 },  /* 11: pink */
        };
#if 1
        if (d->themeSeed & 0xc00000) {
            /* Hue shift for more variability. */
            iForIndices(i, hues) {
                hues[i] += (d->themeSeed & 0x200000 ? 10 : -10);
            }
        }
        size_t primIndex = d->themeSeed ? (d->themeSeed & 0xff) % iElemCount(hues) : 2;
#else
        /* Sequentially switch between hues for testing. */
        static int testing_ = 0;
        iForIndices(i, hues) {
            hues[i] += 10 * ((testing_ % 3) - 1);
        }
        size_t primIndex = (testing_++) / 3 % iElemCount(hues);
#endif
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

        static float normLums_[iElemCount(hues)];
        if (normLums_[0] == 0.0f) {
            iForIndices(i, normLums_) {
    //            float L = luma_HSLColor((iHSLColor){ hues[primIndex], 0.75f, 0.5f, 1.0f});
    //            float nL = 1.0f - luma_HSLColor((iHSLColor){ hues[primIndex], 0.75f, 0.5f, 1.0f}) / 2.0f;
                normLums_[i] = 1.0f - luma_HSLColor((iHSLColor){ hues[i], 0.75f, 0.5f, 1.0f}) / 2.0f;
            }
        }
        /*float normLum[] = { 0.8f, 0.75f, 0.625f, 0.65f, 0.60f,
                                         0.65f, 0.625f, 0.65f, 0.75f, 0.8f,
                                         0.825f, 0.8f };*/

//        float L = luma_HSLColor((iHSLColor){ hues[primIndex], 0.75f, 0.5f, 1.0f});
//        float nL = 1 - L/2;
        const float normLum = normLums_[primIndex];

//        printf("prim:%2u normLum:%.3f\n", primIndex, normLum[primIndex]); fflush(stdout);
        /*float diff = normLum[primIndex] - nL;
        printf("prim:%2u normLum:%.3f luma:%g (%g)\n", primIndex, normLum[primIndex],
               L, diff); fflush(stdout);*/

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
        else if (theme == colorfulLight_GmDocumentTheme ||
                 theme == vibrantLight_GmDocumentTheme) {

            const iBool isVibrant = (theme == vibrantLight_GmDocumentTheme);
//            static int primIndex = 0;
//            primIndex = (primIndex + 1) % iElemCount(hues);
            iHSLColor base = { hues[primIndex], 1.0f, normLum, 1.0f };
            iHSLColor h1   = { hues[primIndex], 1.0f, normLum - 0.37f, 1.0f };
            if (isVibrant) {
                base.lum = 0.5f;
                float offset = luma_HSLColor(base) - 0.8f;
                base.lum -= offset * 0.5f;
                h1 = (iHSLColor){ 0, 1.0f, 1.0f, 1.0f };
            }
//            printf("prim:%d norm:%f\n", primIndex, normLum[primIndex]); fflush(stdout);
            static const float normSat[] = { 0.85f, 0.90f, 1.00f, 0.65f, 0.65f, 0.65f,
                                             0.90f, 0.90f, 1.00f, 0.90f, 1.00f, 0.75f };
            iBool darkHeadings = iTrue;
            base.sat *= normSat[primIndex] * (!isVibrant ? 0.8f : 1.0f);
            setHsl_Color(tmBackground_ColorId, base);
            set_Color(tmParagraph_ColorId, get_Color(black_ColorId));
            set_Color(tmFirstParagraph_ColorId, get_Color(black_ColorId));
            setHsl_Color(tmQuote_ColorId, addSatLum_HSLColor(base, 0, -base.lum * 0.67f));
            setHsl_Color(tmPreformatted_ColorId, addSatLum_HSLColor(base, 0, -base.lum * 0.75f));
            setHsl_Color(tmHeading1_ColorId, h1);
            setHsl_Color(tmHeading2_ColorId,
                         !isVibrant ? addSatLum_HSLColor(h1, 0, -0.1f)
                                    : hsl_Color(mix_Color(
                                          get_Color(tmBackground_ColorId),
                                          get_Color(darkHeadings ? black_ColorId : white_ColorId),
                                          0.7f)));
            set_Color(tmHeading3_ColorId,
                      mix_Color(get_Color(!isVibrant ? tmHeading1_ColorId : tmBackground_ColorId),
                                get_Color(darkHeadings ? black_ColorId : white_ColorId),
                                0.6f));
            setHsl_Color(
                tmBannerBackground_ColorId,
                addSatLum_HSLColor(base, 0, isDarkUI ? -0.2f * (1 - normLum) : 0.2f * (1 - normLum)));
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

            setHsl_Color(tmParagraph_ColorId, addSatLum_HSLColor(base, -0.9f, 0.1f));
            setHsl_Color(tmFirstParagraph_ColorId, addSatLum_HSLColor(base, 0, -0.1f));
            setHsl_Color(tmPreformatted_ColorId, (iHSLColor){ altHue2, 1.0f, 0.25f, 1.0f });
            set_Color(tmQuote_ColorId, get_Color(tmPreformatted_ColorId));
            set_Color(tmInlineContentMetadata_ColorId, get_Color(tmHeading3_ColorId));
        }
        else if (theme == black_GmDocumentTheme || (theme == gray_GmDocumentTheme && isDarkUI)) {
            const float     primHue    = hues[primIndex];
            const iHSLColor primBright = { primHue, 1, 0.6f, 1 };
            const iHSLColor primDim    = { primHue, 1, normLum + (theme == gray_GmDocumentTheme ? 0.0f : -0.15f), 1};
            const iHSLColor altBright  = { altHue, 1, normLums_[altIndex[0]] + (theme == gray_GmDocumentTheme ? 0.1f : 0.0f), 1 };
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
            const iHSLColor primDim    = { primHue, 1, normLums_[primIndex] * 0.33f, 1 };
            const iHSLColor altBright  = { altHue, 1, normLums_[altIndex[0]] * 0.27f, 1 };
            setHsl_Color(tmQuote_ColorId, altBright);
            setHsl_Color(tmPreformatted_ColorId, altBright);
            setHsl_Color(tmHeading1_ColorId, primBright);
            set_Color(tmHeading2_ColorId, mix_Color(get_Color(tmHeading1_ColorId), get_Color(black_ColorId), 0.4f));
            setHsl_Color(tmBannerTitle_ColorId, primDim);
            setHsl_Color(tmBannerIcon_ColorId, primDim);
        }
        else if (theme == oceanic_GmDocumentTheme || theme == arboreal_GmDocumentTheme) {
            /* Two background hues and an accent hue. */
            static const float themeHues[2][3] = {
                { 195, 210, 30 }, /* oceanic */
                { 100, 145, 42 }, /* arboreal */
            };
            const iBool  isArboreal = (theme == arboreal_GmDocumentTheme);
            const float *hues       = themeHues[isArboreal];
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
            if (isArboreal) {
                /* Saturated greens are overpowering. */
                base.sat *= isDarkUI ? 0.6f : 0.75f;
            }
            if (!isDarkUI) {
                base.sat *= 0.66f;
                if (altIndex == 2) {
//                    altBase.sat = 0.5f;
                    if (isArboreal) {
                        /* Brown accent. */
                        altBase.sat *= 0.6f;
                        altBase.lum -= 0.05f;
                        altBase.hue -= 12;
                    }
                    else {
                        altBase.sat *= 0.8f;
                        altBase.lum += 0.1f;
                        altBase.hue -= 40;
                    }
                }
                if (isArboreal) {
                    /* Green and yellow have high luma, so darken them for contrast. */
                    if (altIndex != 2) {
                        altBase.sat *= 0.55f;
                        altBase.lum *= 0.55f;
                    }
                    if (preBase.hue == hues[2]) {
                        preBase.sat *= 0.6f;
                        preBase.hue -= 6;
                    }
                    else {
                        preBase.sat *= 0.55f;
                    }
                    preBase.lum *= 0.75f;
                }
            }
            setHsl_Color(tmBackground_ColorId, base);
            setHsl_Color(tmBannerBackground_ColorId, addSatLum_HSLColor(base, 0.1f, isDarkUI ? 0.04f * (isBannerLighter ? 1 : -1) : 0.05f));
//            set_Color(tmBannerBackground_ColorId, getMixed_Color(tmBackground_ColorId, uiBackground_ColorId, 0.5f));
            setHsl_Color(tmBannerIcon_ColorId, addSatLum_HSLColor(base, 1.0f, isDarkUI ? 0.5f : -0.5f));
            setHsl_Color(tmBannerTitle_ColorId, addSatLum_HSLColor(base, 0.1f, isDarkUI ? 0.3f : -0.5f));
            if (isArboreal && !isDarkUI) {
                const float bgLum = iMin(base.lum, 1.0f);
                setHsl_Color(tmBannerIcon_ColorId, (iHSLColor){ base.hue, 0.45f, bgLum * 0.47f, 1.0f });
                setHsl_Color(tmBannerTitle_ColorId, (iHSLColor){ base.hue, 0.2f, bgLum * 0.44f, 1.0f });
            }
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
            theme != oceanic_GmDocumentTheme && theme != arboreal_GmDocumentTheme &&
            theme != sepia_GmDocumentTheme) {
            iHSLColor base = { hues[primIndex], 1.0f, normLums_[primIndex], 1.0f };
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
                        else if (i == tmParagraph_ColorId && (primIndex == green_Hue ||
                                                              primIndex == greenishYellow_Hue)) {
                            color.sat *= 0.4f;
                            color.lum += 0.1f;
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
                color.sat *= prefs->saturation.value;
            }
            setHsl_Color(i, color);
        }
    }
    if (d->flags.isBookCover) {
        int tint = tmHeading1_ColorId;
        for (int i = tmHeading1_ColorId; i <= tmHeading3_ColorId; i++) {
            const iHSLColor hsl = get_HSLColor(i);
            if (hsl.lum > 0.1f && hsl.sat > 0.1f) {
                tint = i;
                break;
            }
        }
        set_Color(tmBackground_ColorId, getMixed_Color(tmBackground_ColorId, tint, 0.2f));
    }
    /* Derived colors. */
    setDerivedThemeColors_(theme);
    /* Special exceptions. */
    if (iconSeed) {
        if (equal_CStr(cstr_Block(iconSeed), "geminiprotocol.net")) {
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
    d->flags.isPaletteValid = iTrue;
}

void makePaletteGlobal_GmDocument(const iGmDocument *d) {
    if (!d->flags.isPaletteValid) {
        /* Recompute the palette since it's needed now. */
        setThemeSeed_GmDocument((iGmDocument *) d,
                                paletteSeed_GmDocument_(d),
                                urlThemeSeed_String(themeUrl_GmDocument_(d)));
    }
    iAssert(d->flags.isPaletteValid);
    memcpy(get_Root()->tmPalette, d->palette, sizeof(d->palette));
}

void invalidatePalette_GmDocument(iGmDocument *d) {
    d->flags.isPaletteValid = iFalse;
}
