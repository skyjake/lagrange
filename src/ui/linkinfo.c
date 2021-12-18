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

iBool update_LinkInfo(iLinkInfo *d, const iGmDocument *doc, iGmLinkId linkId, int maxWidth) {
    if (!d) {
        return iFalse;
    }
    if (d->linkId != linkId || d->maxWidth != maxWidth) {
        d->linkId   = linkId;
        d->maxWidth = maxWidth;
        invalidate_LinkInfo(d);
        if (linkId) {
            /* Measure and draw. */
            if (targetValue_Anim(&d->opacity) < 1) {
                setValue_Anim(&d->opacity, 1, 75);
            }
            const int avail = iMax(minWidth_LinkInfo_, maxWidth) - 2 * hPad_LinkInfo_;
            const iString *url = linkUrl_GmDocument(doc, linkId);
            iUrl parts;
            init_Url(&parts, url);
            const int flags = linkFlags_GmDocument(doc, linkId);
            const enum iGmLinkScheme scheme = scheme_GmLinkFlag(flags);
            const iBool showHost  = (flags & humanReadable_GmLinkFlag &&
                                    (!isEmpty_Range(&parts.host) ||
                                     scheme == mailto_GmLinkScheme));
            const iBool showImage = (flags & imageFileExtension_GmLinkFlag) != 0;
            const iBool showAudio = (flags & audioFileExtension_GmLinkFlag) != 0;
//            int fg                = linkColor_GmDocument(doc, linkId, textHover_GmLinkPart);
            iString str;
            init_String(&str);
            if ((showHost ||
                 (flags & (imageFileExtension_GmLinkFlag | audioFileExtension_GmLinkFlag))) &&
                scheme != mailto_GmLinkScheme) {
                if (!isEmpty_String(&str)) {
                    appendCStr_String(&str, "\n");
                }
                if (showHost  && scheme != gemini_GmLinkScheme) {
                    append_String(
                        &str, collect_String(upper_String(collectNewRange_String(parts.scheme))));
                    appendCStr_String(&str, " \u2014 ");
                }
                if (showHost) {
                    appendFormat_String(&str, "\x1b[1m%s\x1b[0m", cstr_Rangecc(parts.host));
                }
                if (showImage || showAudio) {
                    appendFormat_String(
                        &str,
                        "%s%s",
                        showHost ? " \u2014" : "",
                        format_CStr(showImage ? photo_Icon " %s " : "\U0001f3b5 %s",
                                    cstr_Lang(showImage ? "link.hint.image" : "link.hint.audio")));
                }
            }
            if (flags & visited_GmLinkFlag) {
                iDate date;
                init_Date(&date, linkTime_GmDocument(doc, linkId));
                if (!isEmpty_String(&str)) {
                    appendCStr_String(&str, " \u2014 ");
                }
//                appendCStr_String(&str, escape_Color(tmQuoteIcon_ColorId));
                iString *dateStr = format_Date(&date, "%b %d");
                append_String(&str, dateStr);
                delete_String(dateStr);
            }
            /* Identity that will be used. */
            const iGmIdentity *ident = identityForUrl_GmCerts(certs_App(), url);
            if (ident) {
                if (!isEmpty_String(&str)) {
                    appendCStr_String(&str, " \u2014 ");
                }
                appendFormat_String(&str, person_Icon " %s",
                                                             //escape_Color(tmBannerItemTitle_ColorId),
                                    cstr_String(name_GmIdentity(ident)));                
            }
            /* Show scheme and host. */            
            if (!isEmpty_String(&str)) {
                appendCStr_String(&str, "\n");
            }
            appendRange_String(&str, range_String(url));
            /* Draw the text. */            
            iWrapText wt = { .text = range_String(&str), .maxWidth = avail, .mode = word_WrapTextMode };
            d->buf = new_TextBuf(&wt, uiLabel_FontId, tmQuote_ColorId);
            deinit_String(&str);
        }
        else {
            if (targetValue_Anim(&d->opacity) > 0) {
                setValue_Anim(&d->opacity, 0, 150);
            }            
        }
        return iTrue;
    }
    return iFalse;
}

void invalidate_LinkInfo(iLinkInfo *d) {
    if (targetValue_Anim(&d->opacity) > 0) {
        setValue_Anim(&d->opacity, 0, 150);
    }            
    
    //    if (d->buf) {
//        delete_TextBuf(d->buf);
//        d->buf = NULL;
//    }
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
