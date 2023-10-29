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

#include "text.h"
#include "defs.h"
#include <SDL_version.h>

iDeclareType(Font)

/* TODO: Include this in text_stb.c as an runtime option. */

iLocalDef iBool isWrapPunct_(iChar c) {
    /* Punctuation that participates in word-wrapping. */
    return (c == '/' || c == '\\' || c == '=' || c == '-' || c == ',' || c == ';' || c == '.' || c == ':' || c == 0xad);
}

iLocalDef iBool isClosingBracket_(iChar c) {
    return (c == ')' || c == ']' || c == '}' || c == '>');
}

iLocalDef iBool isWrapBoundary_(iChar prevC, iChar c) {
    /* Line wrapping boundaries are determined by looking at a character and the
       last character processed. We want to wrap at natural word boundaries where
       possible, so normally we wrap at a space followed a non-space character. As
       an exception, we also wrap after punctuation used to break up words, so we
       can wrap text like foo/bar/baz-abc-def.xyz at any puncation boundaries,
       without wrapping on other punctuation used for expressive purposes like
       emoticons :-) */
    if (isClosingBracket_(prevC) && !isWrapPunct_(c)) {
        return iTrue;
    }
    if (isSpace_Char(prevC)) {
        return iFalse;
    }
    if ((prevC == '/' || prevC == '\\' || prevC == '-' || prevC == '_' || prevC == '+') &&
        !isWrapPunct_(c)) {
        return iTrue;
    }
    return isSpace_Char(c);
}

iLocalDef iBool isMeasuring_(enum iRunMode mode) {
    return (mode & modeMask_RunMode) == measure_RunMode;
}

static void runSimple_Font_(iFont *d, const iRunArgs *args) {
    /* This function shapes text using a simplified, incomplete algorithm. It works for English
       and other non-complex LTR scripts. Composed glyphs are not supported (must rely on text
       being in a pre-composed form). This algorithm is used if HarfBuzz is not available. */
    const iInt2 orig        = args->pos;
    iTextAttrib attrib      = { .fgColorId = args->color };
    iRect       bounds      = { orig, init_I2(0, d->font.height) };
    float       xpos        = orig.x;
    float       xposMax     = xpos;
    float       monoAdvance = 0;
    int         ypos        = orig.y;
    size_t      maxLen      = args->maxLen ? args->maxLen : iInvalidSize;
    float       xposExtend  = orig.x; /* allows wide glyphs to use more space; restored by whitespace */
    iWrapText * wrap        = args->wrap;
    int         wrapAdvance = 0;
    const int   xposLimit   = (wrap && wrap->maxWidth ? orig.x + wrap->maxWidth : 0);
    const enum iRunMode mode        = args->mode;
    const char *        lastWordEnd = args->text.start;
    SDL_Renderer *render = current_Text()->render;
#if defined (LAGRANGE_ENABLE_STB_TRUETYPE)
    SDL_Texture *cache = current_StbText_()->cache;
#endif
    iAssert(args->text.end >= args->text.start);
    if (wrap) {
        wrap->wrapRange_        = args->text;
        wrap->hitAdvance_out    = zero_I2();
        wrap->hitChar_out       = NULL;
        wrap->hitGlyphNormX_out = 0.0f;
    }
    const iBool checkHitPoint = wrap && !isEqual_I2(wrap->hitPoint, zero_I2());
    const iBool checkHitChar  = wrap && wrap->hitChar;
    const iBool isMonospaced  = isMonospaced_Font(d) && !(mode & alwaysVariableWidthFlag_RunMode);
    if (isMonospaced) {
        monoAdvance = glyph_Font_(d, 'M')->advance;
    }
    /* The default text foreground color. */
    if (mode & draw_RunMode) {
        const iColor clr = get_Color(args->color);
#if defined (LAGRANGE_ENABLE_STB_TRUETYPE)
        SDL_SetTextureColorMod(cache, clr.r, clr.g, clr.b);
#endif
#if defined (SDL_SEAL_CURSES)
        const enum iFontStyle style = style_FontId(fontId_Text(d));
        SDL_SetRenderTextColor(render, clr.r, clr.g, clr.b);
        SDL_SetRenderTextAttributes(
            render,
            (style == bold_FontStyle || style == semiBold_FontStyle ? SDL_TEXT_ATTRIBUTE_BOLD : 0) |
                (style == italic_FontStyle ? SDL_TEXT_ATTRIBUTE_ITALIC : 0) |
                (mode & underline_RunMode ? SDL_TEXT_ATTRIBUTE_BOLD | SDL_TEXT_ATTRIBUTE_UNDERLINE : 0));
#endif
        if (args->mode & fillBackground_RunMode) {
            const iColor initial = get_Color(args->color);
            SDL_SetRenderDrawColor(render, initial.r, initial.g, initial.b, 0);
//#if defined (SDL_SEAL_CURSES)
//            SDL_SetRenderTextFillColor(render, initial.r, initial.g, initial.b, 255);
//#endif
        }
    }
    /* Text rendering is not very straightforward! Let's dive in... */
    iChar       prevCh = 0;
    const char *chPos;
    for (chPos = args->text.start; chPos != args->text.end; ) {
        iAssert(chPos < args->text.end);
        const char *currentPos = chPos;
        const iBool isHitPointOnThisLine = (checkHitPoint && wrap->hitPoint.y >= ypos &&
                                            wrap->hitPoint.y < ypos + d->font.height);
        if (checkHitChar && currentPos == wrap->hitChar) {
            wrap->hitAdvance_out = sub_I2(init_I2(xpos, ypos), orig);
        }
        /* Check if the hit point is on the left side of the line. */
        if (isHitPointOnThisLine && !wrap->hitChar_out && wrap->hitPoint.x < orig.x) {
            wrap->hitChar_out = currentPos;
            wrap->hitGlyphNormX_out = 0.0f;
        }
        if (*chPos == 0x1b) { /* ANSI escape. */
            chPos++;
            iRegExpMatch m;
            init_RegExpMatch(&m);
            if (match_RegExp(current_Text()->ansiEscape, chPos, args->text.end - chPos, &m)) {
                if (mode & draw_RunMode && ~mode & permanentColorFlag_RunMode) {
                    /* Change the color. */
                    iColor clr = get_Color(args->color);
                    ansiColors_Color(capturedRange_RegExpMatch(&m, 1),
                                     current_Text()->baseFgColorId,
                                     none_ColorId,
                                     iFalse,
                                     &clr,
                                     NULL,
                                     NULL);
#if defined (LAGRANGE_ENABLE_STB_TRUETYPE)
                    SDL_SetTextureColorMod(cache, clr.r, clr.g, clr.b);
#endif
#if defined (SDL_SEAL_CURSES)
                    SDL_SetRenderTextColor(render, clr.r, clr.g, clr.b);
#endif
                    if (args->mode & fillBackground_RunMode) {
                        SDL_SetRenderDrawColor(render, clr.r, clr.g, clr.b, 0);
                    }
                }
                chPos = end_RegExpMatch(&m);
                continue;
            }
        }
        iChar ch = nextChar_(&chPos, args->text.end);
        iBool isEmoji = isEmoji_Char(ch);
        if (ch == 0x200d) { /* zero-width joiner */
            /* We don't have the composited Emojis. */
            if (isEmoji_Char(prevCh)) {
                /* skip */
                nextChar_(&chPos, args->text.end);
                ch = nextChar_(&chPos, args->text.end);
            }
        }
        if (isVariationSelector_Char(ch)) {
            ch = nextChar_(&chPos, args->text.end); /* skip it */
        }
        /* Special instructions. */ {
            if (ch == 0xad) { /* soft hyphen */
                lastWordEnd = chPos;
                if (isMeasuring_(mode)) {
                    if (xposLimit > 0) {
                        const char *postHyphen = chPos;
                        iChar       nextCh     = nextChar_(&postHyphen, args->text.end);
                        if ((int) xpos + glyph_Font_(d, ch)->rect[0].size.x +
                            glyph_Font_(d, nextCh)->rect[0].size.x > xposLimit) {
                            /* Wraps after hyphen, should show it. */
                        }
                        else continue;
                    }
                    else continue;
                }
                else {
                    /* Only show it at the end. */
                    if (chPos != args->text.end) {
                        continue;
                    }
                }
            }
            /* TODO: Check out if `uc_wordbreak_property()` from libunistring can be used here. */
            if (ch == '\n') {
                /* Notify about the wrap. */
                if (!notify_WrapText(wrap, chPos, attrib, 0, iMax(xpos, xposExtend) - orig.x)) {
                    break;
                }
                lastWordEnd = NULL;
                xpos = xposExtend = orig.x;
                ypos += d->font.height;
                prevCh = ch;
                continue;
            }
            if (ch == '\t') {
//                const int tabStopWidth = d->height * 10;
//                const int halfWidth    = (xposLimit - orig.x) / 2;
                const int xRel         = xpos - orig.x;
#if 0
                /* First stop is always to half width. */
                if (halfWidth > 0 && xRel < halfWidth) {
                    xpos = orig.x + halfWidth;
                }
                else if (halfWidth > 0 && xRel < halfWidth * 3 / 2) {
                    xpos = orig.x + halfWidth * 3 / 2;
                }
                else {
                    xpos = orig.x + ((xRel / tabStopWidth) + 1) * tabStopWidth;
                }
#endif
                xpos = orig.x + nextTabStop_Font_(d, xRel);
                xposExtend = iMax(xposExtend, xpos);
                prevCh = 0;
                continue;
            }
            if (ch == '\v') { /* color change */
                iChar esc = nextChar_(&chPos, args->text.end);
                int colorNum = args->color;
                if (esc == '\v') { /* Extended range. */
                    esc = nextChar_(&chPos, args->text.end) + asciiExtended_ColorEscape;
                    colorNum = esc - asciiBase_ColorEscape;
                }
                else if (esc != 0x24) { /* ASCII Cancel */
                    colorNum = esc - asciiBase_ColorEscape;
                }
                if (mode & draw_RunMode && ~mode & permanentColorFlag_RunMode) {
                    const iColor clr = get_Color(colorNum);
#if defined (LAGRANGE_ENABLE_STB_TRUETYPE)
                    SDL_SetTextureColorMod(cache, clr.r, clr.g, clr.b);
#endif
                    if (args->mode & fillBackground_RunMode) {
                        SDL_SetRenderDrawColor(render, clr.r, clr.g, clr.b, 0);
                    }
                }
                prevCh = 0;
                continue;
            }
            if (isDefaultIgnorable_Char(ch) || isFitzpatrickType_Char(ch)) {
                continue;
            }
        }
        const iGlyph *glyph = glyph_Font_(d, ch);
        int x1 = iMax(xpos, xposExtend);
        /* Which half of the pixel the glyph falls on? */
        const int hoff = enableHalfPixelGlyphs_Text ? (xpos - x1 > 0.5f ? 1 : 0) : 0;
        if (mode & draw_RunMode && ch != 0x20 && ch != 0 && !isRasterized_Glyph_(glyph, hoff)) {
            /* Need to pause here and make sure all glyphs have been cached in the text. */
//            printf("[Text] missing from cache: %lc (%x)\n", (int) ch, ch);
            //cacheTextGlyphs_Font_(d, args->text);
            cacheSingleGlyph_Font_(glyph->font, index_Glyph_(glyph));
            glyph = glyph_Font_(d, ch); /* cache may have been reset */
        }
        int x2 = x1 + glyph->rect[hoff].size.x;
        if (isHitPointOnThisLine) {
            if (wrap->hitPoint.x >= x1) { /* may also be off to the right */
                wrap->hitChar_out = currentPos;
                wrap->hitGlyphNormX_out =
                    wrap->hitPoint.x < x2 ? (wrap->hitPoint.x - x1) / glyph->advance : 1.0f;
            }
        }
        /* Out of the allotted space on the line? */
        if (xposLimit > 0 && x2 > xposLimit) {
            iAssert(wrap);
            const char *wrapPos = currentPos;
            int advance = x1 - orig.x;
            if (lastWordEnd && wrap->mode == word_WrapTextMode) {
                wrapPos = skipSpace_CStr(lastWordEnd); /* go back */
                wrapPos = iMin(wrapPos, args->text.end);
                advance = wrapAdvance;
                if (checkHitPoint && wrap->hitChar_out > lastWordEnd - 1) {
                    wrap->hitChar_out = iMax(args->text.start, lastWordEnd - 1);
                }
            }
            if (!notify_WrapText(wrap, wrapPos, attrib, 0, advance)) {
                break;
            }
            lastWordEnd = NULL;
            xpos = xposExtend = orig.x;
            ypos += d->font.height;
            prevCh = 0;
            chPos = wrapPos;
            continue;
        }
        const int yLineMax = ypos + d->font.height;
        SDL_Rect dst = { x1 + glyph->d[hoff].x,
                         ypos + glyph->font->baseline + glyph->d[hoff].y,
                         glyph->rect[hoff].size.x,
                         glyph->rect[hoff].size.y };
        if (glyph->font != d) {
            if (glyph->font->font.height > d->font.height) {
                /* Center-align vertically so the baseline isn't totally offset. */
                dst.y -= (glyph->font->font.height - d->font.height) / 2;
            }
        }
        /* Update the bounding box. */
        if (mode & visualFlag_RunMode) {
            if (isEmpty_Rect(bounds)) {
                bounds = init_Rect(dst.x, dst.y, dst.w, dst.h);
            }
            else {
                bounds = union_Rect(bounds, init_Rect(dst.x, dst.y, dst.w, dst.h));
            }
        }
        else {
            bounds.size.x = iMax(bounds.size.x, x2 - orig.x);
            bounds.size.y = iMax(bounds.size.y, ypos + glyph->font->font.height - orig.y);
        }
        /* Symbols and emojis are NOT monospaced, so must conform when the primary font
           is monospaced. Except with Japanese script, that's larger than the normal monospace. */
        const iBool useMonoAdvance = monoAdvance > 0; // && !isJapanese_FontId(fontId_Text_(glyph->font));
        const float advance = (useMonoAdvance && glyph->advance > 0 ? monoAdvance : glyph->advance);
        if (!isMeasuring_(mode) &&
            (ch != 0x20 /* don't bother rendering spaces */ || (isTerminal_Platform() && dst.h == 2))) {
            if (useMonoAdvance && dst.w > advance && glyph->font != d && !isEmoji) {
                /* Glyphs from a different font may need recentering to look better. */
                dst.x -= (dst.w - advance) / 2;
            }
            SDL_Rect src;
            memcpy(&src, &glyph->rect[hoff], sizeof(SDL_Rect));
            /* Clip the glyphs to the font's height. This is useful when the font's line spacing
               has been reduced or when the glyph is from a different font. */
            if (dst.y + dst.h > yLineMax) {
                const int over = dst.y + dst.h - yLineMax;
                src.h -= over;
                dst.h -= over;
            }
            if (dst.y < ypos) {
                const int over = ypos - dst.y;
                dst.y += over;
                dst.h -= over;
                src.y += over;
                src.h -= over;
            }
            dst.x += origin_Paint.x;
            dst.y += origin_Paint.y;
            if (args->mode & fillBackground_RunMode) {
                /* Alpha blending looks much better if the RGB components don't change in
                   the partially transparent pixels. */
                SDL_RenderFillRect(render, &dst);
            }
#if defined (LAGRANGE_ENABLE_STB_TRUETYPE)
            SDL_RenderCopy(render, cache, &src, &dst);
#endif
#if defined (SDL_SEAL_CURSES)
            SDL_RenderDrawUnicode(render, dst.x, dst.y, ch);
            if (src.h == 2) {
                /* "Big" font, used for titles: underline it. */
                for (int ux = 0; ux < dst.w; ux++) {
                    SDL_RenderDrawUnicode(render, dst.x + ux, dst.y + 1,
                                          0x2500 /* box drawings light horizontal */);
                }
            }
#endif
        }
        xpos += advance;
        if (!isSpace_Char(ch)) {
            xposExtend += isEmoji ? glyph->advance : advance;
        }
#if defined (LAGRANGE_ENABLE_KERNING) && defined (LAGRANGE_ENABLE_STB_TRUETYPE)
        /* Check the next character. */
        if (!isMonospaced && glyph->font == d) {
            /* TODO: No need to decode the next char twice; check this on the next iteration. */
            const char *peek = chPos;
            const iChar next = nextChar_(&peek, args->text.end);
            if (enableKerning_Text && next) {
                const uint32_t nextGlyphIndex = glyphIndex_Font_(glyph->font, next);
                int kern = stbtt_GetGlyphKernAdvance(
                    &glyph->font->font.file->stbInfo, index_Glyph_(glyph), nextGlyphIndex);
                /* Nunito needs some kerning fixes. */
                if (glyph->font->font.spec->flags & fixNunitoKerning_FontSpecFlag) {
                    if (ch == 'W' && (next == 'i' || next == 'h')) {
                        kern = -30;
                    }
                    else if (ch == 'T' && next == 'h') {
                        kern = -15;
                    }
                    else if (ch == 'V' && next == 'i') {
                        kern = -15;
                    }
                }
                if (kern) {
//                    printf("%lc(%u) -> %lc(%u): kern %d (%f)\n", ch, glyph->glyphIndex, next,
//                           nextGlyphIndex,
//                           kern, d->xScale * kern);
                    xpos       += glyph->font->xScale * kern;
                    xposExtend += glyph->font->xScale * kern;
                }
            }
        }
#endif
        xposExtend = iMax(xposExtend, xpos);
        xposMax    = iMax(xposMax, xposExtend);
        if ((wrap && wrap->mode == anyCharacter_WrapTextMode) || isWrapBoundary_(prevCh, ch)) {
            lastWordEnd = currentPos; /* mark word wrap position */
            wrapAdvance = x2 - orig.x;
        }
        prevCh = ch;
        if (--maxLen == 0) {
            break;
        }
    }
    notify_WrapText(wrap, chPos, attrib, 0, xpos - orig.x);
    if (checkHitChar && wrap->hitChar == args->text.end) {
        wrap->hitAdvance_out = sub_I2(init_I2(xpos, ypos), orig);
    }
    if (args->metrics_out) {
        args->metrics_out->advance = sub_I2(init_I2(xpos, ypos), orig);
        args->metrics_out->bounds = bounds;
    }
//    if (args->runAdvance_out) {
//        *args->runAdvance_out = xposMax - orig.x;
//    }
#if defined (SDL_SEAL_CURSES)
    if (mode & draw_RunMode) {
        SDL_SetRenderTextFillColor(render, 0, 0, 0, 0);
    }
#endif
}

