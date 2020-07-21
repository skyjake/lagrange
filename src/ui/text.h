#pragma once

#include <the_Foundation/rect.h>
#include <the_Foundation/string.h>

#include <SDL_render.h>

enum iFontId {
    default_FontId,
    uiShortcuts_FontId,
    uiInput_FontId,
    /* Document fonts: */
    paragraph_FontId,
    firstParagraph_FontId,
    preformatted_FontId,
    quote_FontId,
    header1_FontId,
    header2_FontId,
    header3_FontId,
    max_FontId
};

#define specialSymbol_Text  0x10

enum iSpecialSymbol {
    silence_SpecialSymbol,
};

void    init_Text           (SDL_Renderer *);
void    deinit_Text         (void);

int     lineHeight_Text     (int font);
iInt2   measure_Text        (int font, const char *text);
iInt2   advance_Text        (int font, const char *text);
iInt2   advanceN_Text       (int font, const char *text, size_t n); /* `n` in characters */

void    draw_Text           (int font, iInt2 pos, int color, const char *text, ...); /* negative pos to switch alignment */
void    drawString_Text     (int font, iInt2 pos, int color, const iString *text);
void    drawCentered_Text   (int font, iRect rect, int color, const char *text, ...);

SDL_Texture *   glyphCache_Text     (void);

/*-----------------------------------------------------------------------------------------------*/

iDeclareType(TextBuf)
iDeclareTypeConstructionArgs(TextBuf, int font, const char *text)

struct Impl_TextBuf {
    SDL_Texture *texture;
    iInt2        size;
};

void    draw_TextBuf        (const iTextBuf *, iInt2 pos, int color);
