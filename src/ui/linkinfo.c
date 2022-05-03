/* Copyright 2021 Jaakko Keränen <jaakko.keranen@iki.fi>

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

#include "linkinfo.h"
#include "metrics.h"
#include "paint.h"
#include "../gmcerts.h"
#include "../app.h"

#include <SDL_render.h>

iDefineTypeConstruction(LinkInfo)

#define minWidth_LinkInfo_  (40 * gap_UI)
#define hPad_LinkInfo_      (2 * gap_UI)
#define vPad_LinkInfo_      (1 * gap_UI)

void init_LinkInfo(iLinkInfo *d) {
    d->buf = NULL;
    init_Anim(&d->opacity, 0.0f);
    d->isAltPos = iFalse;
}

void deinit_LinkInfo(iLinkInfo *d) {
    delete_TextBuf(d->buf);
}

iInt2 size_LinkInfo(const iLinkInfo *d) {
    if (!d->buf) {
        return zero_I2();
    }
    return add_I2(d->buf->size, init_I2(2 * hPad_LinkInfo_, 2 * vPad_LinkInfo_));
}

void infoText_LinkInfo(const iGmDocument *doc, iGmLinkId linkId, iString *text_out) {
    const iString *url = linkUrl_GmDocument(doc, linkId);
    iUrl parts;
    init_Url(&parts, url);
    const int                flags   = linkFlags_GmDocument(doc, linkId);
    const enum iGmLinkScheme scheme  = scheme_GmLinkFlag(flags);
    const iBool              isImage = (flags & imageFileExtension_GmLinkFlag) != 0;
    const iBool              isAudio = (flags & audioFileExtension_GmLinkFlag) != 0;
    /* Most important info first: the identity that will be used. */
    const iGmIdentity *ident = identityForUrl_GmCerts(certs_App(), url);
    if (ident) {
        appendFormat_String(text_out, person_Icon " %s",
                                                     //escape_Color(tmBannerItemTitle_ColorId),
                            cstr_String(name_GmIdentity(ident)));
    }
    /* Possibly inlined content. */
    if (isImage || isAudio) {
        if (!isEmpty_String(text_out)) {
            appendCStr_String(text_out, "\n");
        }
        appendCStr_String(
                          text_out,
            format_CStr(isImage ? photo_Icon " %s " : "\U0001f3b5 %s",
                        cstr_Lang(isImage ? "link.hint.image" : "link.hint.audio")));
    }
    if (!isEmpty_String(text_out)) {
        appendCStr_String(text_out, " \u2014 ");
    }
    /* Indicate non-Gemini schemes. */
    if (scheme == mailto_GmLinkScheme) {
        appendCStr_String(text_out, envelope_Icon " ");
        append_String(text_out, url);
    }
    else if (scheme != gemini_GmLinkScheme && !isEmpty_Range(&parts.host)) {
        appendCStr_String(text_out, globe_Icon " \x1b[1m");
        appendRange_String(text_out, (iRangecc){ constBegin_String(url),
                                             parts.host.end });
        appendCStr_String(text_out, "\x1b[0m");
        appendRange_String(text_out, (iRangecc){ parts.path.start, constEnd_String(url) });
    }
    else if (scheme == data_GmLinkScheme) {
        appendCStr_String(text_out, paperclip_Icon " ");
        append_String(text_out, prettyDataUrl_String(url, none_ColorId));
    }
    else if (scheme != gemini_GmLinkScheme) {
        const size_t maxDispLen = 300;
        appendCStr_String(text_out, scheme == file_GmLinkScheme ? "" : globe_Icon " ");
        appendCStrN_String(text_out, cstr_String(url), iMin(maxDispLen, size_String(url)));
        if (size_String(url) > maxDispLen) {
            appendCStr_String(text_out, "...");
        }
    }
    else {
        appendCStr_String(text_out, "\x1b[1m");
        appendRange_String(text_out, parts.host);
        if (!isEmpty_Range(&parts.port)) {
            appendCStr_String(text_out, ":");
            appendRange_String(text_out, parts.port);
        }
        appendCStr_String(text_out, "\x1b[0m");
        appendRange_String(text_out, (iRangecc){ parts.path.start, constEnd_String(url) });
    }
    /* Date of last visit. */
    if (flags & visited_GmLinkFlag) {
        iDate date;
        init_Date(&date, linkTime_GmDocument(doc, linkId));
        if (!isEmpty_String(text_out)) {
            appendCStr_String(text_out, " \u2014 ");
        }
        iString *dateStr = format_Date(&date, "%b %d");
        append_String(text_out, dateStr);
        delete_String(dateStr);
    }
}

iBool update_LinkInfo(iLinkInfo *d, const iGmDocument *doc, iGmLinkId linkId, int maxWidth) {    
    if (!d) {
        return iFalse;
    }
    const iBool isAnimated = prefs_App()->uiAnimations && !isTerminal_Platform();
    if (d->linkId != linkId || d->maxWidth != maxWidth) {
        d->linkId   = linkId;
        d->maxWidth = maxWidth;
        invalidate_LinkInfo(d);
        if (linkId) {
            iString str;
            init_String(&str);
            infoText_LinkInfo(doc, linkId, &str);
            if (targetValue_Anim(&d->opacity) < 1) {
                setValue_Anim(&d->opacity, 1, isAnimated ? 75 : 0);
            }
            /* Draw to a buffer, wrapped. */
            const int avail = iMax(minWidth_LinkInfo_, maxWidth) - 2 * hPad_LinkInfo_;
            iWrapText wt = { .text = range_String(&str), .maxWidth = avail, .mode = word_WrapTextMode };
            d->buf = new_TextBuf(&wt, uiLabel_FontId, tmQuote_ColorId);
            deinit_String(&str);
        }
        else {
            if (targetValue_Anim(&d->opacity) > 0) {
                setValue_Anim(&d->opacity, 0, isAnimated ? 150 : 0);
            }            
        }
        return iTrue;
    }
    return iFalse;
}

void invalidate_LinkInfo(iLinkInfo *d) {
    if (targetValue_Anim(&d->opacity) > 0) {
        setValue_Anim(&d->opacity, 0, prefs_App()->uiAnimations ? 150 : 0);
    }            
}

void draw_LinkInfo(const iLinkInfo *d, iInt2 topLeft) {
    const float opacity = value_Anim(&d->opacity);
    if (!d->buf || opacity <= 0.01f) {
        return;
    }
    iPaint p;
    init_Paint(&p);
    iInt2 size = size_LinkInfo(d);
    iRect rect = { topLeft, size };
    SDL_SetRenderDrawBlendMode(renderer_Window(get_Window()), SDL_BLENDMODE_BLEND);
    p.alpha = 255 * opacity;
    fillRect_Paint(&p, rect, tmBackgroundAltText_ColorId);
    drawRect_Paint(&p, rect, tmFrameAltText_ColorId);
    SDL_SetTextureAlphaMod(d->buf->texture, p.alpha);
    draw_TextBuf(d->buf, add_I2(topLeft, init_I2(hPad_LinkInfo_, vPad_LinkInfo_)),
                 white_ColorId);
    SDL_SetRenderDrawBlendMode(renderer_Window(get_Window()), SDL_BLENDMODE_NONE);
}
