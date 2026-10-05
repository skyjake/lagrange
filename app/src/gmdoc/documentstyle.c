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

#include "documentstyle.h"
#include "color.h"
#include "render/text.h"
#include "ui/metrics.h"

void init_DocumentStyle(iDocumentStyle *d, const iDocumentStyleParams *params) {
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
    static const float topMargins[max_GmLineType] = {
        0.0f, 0.25f, 1.0f, 0.5f, 2.0f, 1.5f, 1.25f, 0.25f
    };
    static const float bottomMargins[max_GmLineType] = {
        0.0f, 0.25f, 1.0f, 0.5f, 1.0f, 0.5f, 0.25f, 0.25f
    };
    const iDocPrefs   *prefs             = params->prefs;
    const iBool        isMono            = params->isMonospace;
    const iBool        isMonoGopher      = isMono && params->isGopher;
    const iBool        isPaged           = params->isPaged; /* narrower lines, so smaller monospace */
    const iBool        isDarkBg          = params->isDarkBackground;
    const iBool        isNarrow          = params->width < 90 * gap_Text * aspect_UI;
    const iBool        isVeryNarrow      = params->width <= 70 * gap_Text * aspect_UI;
    const iBool        isExtremelyNarrow = params->width <= 60 * gap_Text * aspect_UI;
    const enum iFontId headingFont       = isMono ? documentMonospace_FontId : documentHeading_FontId;
    const enum iFontId bodyFont          = isMono ? documentMonospace_FontId : documentBody_FontId;
    const int          bodySize          = isMonoGopher ? (isPaged ? contentTiny_FontSize : contentSmall_FontSize)
                                           : isMono && isPaged ? contentSmall_FontSize
                                                               : contentRegular_FontSize;
    iZap(*d);
    /* Fonts and colors. */
    memcpy(d->colors, defaultColors, sizeof(d->colors));
    d->fonts[text_GmLineType] = FONT_ID(bodyFont, regular_FontStyle, bodySize);
    d->fonts[bullet_GmLineType] = FONT_ID(bodyFont, regular_FontStyle, bodySize);
    d->fonts[preformatted_GmLineType] = isPaged ? preformattedSmall_FontId : preformatted_FontId;
    d->fonts[quote_GmLineType] = isMono                     ? monospaceParagraph_FontId
                                 : prefs->italicQuote.value ? quote_FontId
                                                            : paragraph_FontId;
    d->fonts[heading1_GmLineType] = FONT_ID(headingFont, bold_FontStyle, contentHuge_FontSize);
    d->fonts[heading2_GmLineType] = FONT_ID(headingFont, regular_FontStyle, contentLarge_FontSize);
    d->fonts[heading3_GmLineType] = FONT_ID(headingFont, bold_FontStyle, contentBig_FontSize);
    d->fonts[link_GmLineType] =
        FONT_ID(bodyFont,
                ((isDarkBg && prefs->boldLinkDark.value) || (!isDarkBg && prefs->boldLinkLight.value))
                    ? semiBold_FontStyle
                    : regular_FontStyle,
                bodySize);
    d->hasLede = prefs->bigFirstParagraph.value && !isMono && !isTerminal_Platform() &&
                 (!params->isGopher || prefs->geminiStyledGopher.value);
    d->ledeFont     = firstParagraph_FontId;
    if (params->isUiTheme) {
        /* Shown as part of the UI. */
        d->fonts[text_GmLineType]         = uiContent_FontId;
        d->fonts[bullet_GmLineType]       = uiContent_FontId;
        d->fonts[preformatted_GmLineType] = FONT_ID(monospace_FontId, regular_FontStyle, uiSmall_FontSize);
        d->fonts[quote_GmLineType]        = uiContent_FontId;
        d->fonts[heading1_GmLineType]     = uiLabelLargeBold_FontId;
        d->fonts[heading2_GmLineType]     = uiLabelBigBold_FontId;
        d->fonts[heading3_GmLineType]     = uiContentBold_FontId;
        d->fonts[link_GmLineType]         = uiContent_FontId;
        d->hasLede                        = iFalse;
    }
    d->ledeColor    = tmFirstParagraph_ColorId;
    d->maxLedeLines = 10;
    /* Indents. */ {
        const float indents[max_GmLineType] = { 5, 10, 5, isNarrow ? 5 : 10, 0, 0, 5, 5 };
        memcpy(d->indents, indents, sizeof(d->indents));
    }
    if (isExtremelyNarrow) {
        /* Further reduce the margins. */
        d->indents[text_GmLineType] -= 5;
        d->indents[heading3_GmLineType] -= 5;
        d->indents[bullet_GmLineType] -= 5;
        d->indents[preformatted_GmLineType] -= 5;
    }
    if (params->isGopher) {
        d->indents[preformatted_GmLineType] = d->indents[text_GmLineType];
    }
    if (params->isPlainText) {
        iForIndices(i, d->indents) {
            d->indents[i] = 0;
        }
    }
    d->ruleIndent = isVeryNarrow ? 0 : 5;
    /* The right margin is used for balancing lines horizontally. */
    d->isFullWidthImages = !isPaged && (params->outsideMargin < 5 * gap_UI * aspect_UI);
    for (int i = 0; i < max_GmLineType; i++) {
        const iBool isTextType = (i == text_GmLineType || i == bullet_GmLineType ||
                                  i == quote_GmLineType || i == link_GmLineType);
        if (isVeryNarrow || d->isFullWidthImages) {
            d->rightMargins[i] =
                !isExtremelyNarrow && prefs->justifyParagraph.value && isTextType ? 1 : 0;
        }
        else {
            d->rightMargins[i] = isTextType ? 4 : 0;
        }
    }
    /* Vertical margins. */
    memcpy(d->topMargins, topMargins, sizeof(d->topMargins));
    memcpy(d->bottomMargins, bottomMargins, sizeof(d->bottomMargins));
    d->linkGroupMargin = 1.25f;
    if (isPaged) {
        /* Level 1 headings begin a new page lower down, with more space below them. */
        d->topMargins[heading1_GmLineType] *= 3;
        d->bottomMargins[heading1_GmLineType] *= 2;
        d->flags[heading1_GmLineType] |= centered_DocumentStyleFlag | keepBottomMargin_DocumentStyleFlag;
    }
    if (params->isUiTheme) {
        /* Only the empty lines of the source separate the lines, except for links that need
           room for the open link highlight. */
        iZap(d->topMargins);
        iZap(d->bottomMargins);
        d->topMargins[link_GmLineType]    = topMargins[link_GmLineType];
        d->bottomMargins[link_GmLineType] = bottomMargins[link_GmLineType];
        d->linkGroupMargin = 0;
        d->verticalPad     = 1;
    }
}
