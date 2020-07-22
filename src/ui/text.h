#pragma once

#include <the_Foundation/rect.h>
#include <the_Foundation/string.h>

#include <SDL_render.h>

enum iFontId {
    default_FontId,
    monospace_FontId,
    monospaceSmall_FontId,
    medium_FontId,
    italic_FontId,
    bold_FontId,
    mediumBold_FontId,
    largeBold_FontId,
    hugeBold_FontId,
    max_FontId,
    /* UI fonts: */
    uiInput_FontId = monospace_FontId,
    /* Document fonts: */
    paragraph_FontId         = default_FontId,
    firstParagraph_FontId    = medium_FontId,
    preformatted_FontId      = monospace_FontId,
    preformattedSmall_FontId = monospaceSmall_FontId,
    quote_FontId             = italic_FontId,
    header1_FontId           = hugeBold_FontId,
    header2_FontId           = largeBold_FontId,
    header3_FontId           = mediumBold_FontId,
    uiShortcuts_FontId       = default_FontId,
};

#define specialSymbol_Text  0x10

enum iSpecialSymbol {
    silence_SpecialSymbol,
};

void    init_Text           (SDL_Renderer *);
void    deinit_Text         (void);

int     lineHeight_Text     (int fontId);
iInt2   measure_Text        (int fontId, const char *text);
iInt2   measureRange_Text   (int fontId, iRangecc text);
iInt2   advance_Text        (int fontId, const char *text);
iInt2   advanceN_Text       (int fontId, const char *text, size_t n); /* `n` in characters */
iInt2   advanceRange_Text   (int fontId, iRangecc text);
iInt2   tryAdvanceRange_Text(int fontId, iRangecc text, int width, const char **endPos);

void    draw_Text           (int fontId, iInt2 pos, int color, const char *text, ...); /* negative pos to switch alignment */
void    drawString_Text     (int fontId, iInt2 pos, int color, const iString *text);
void    drawCentered_Text   (int fontId, iRect rect, int color, const char *text, ...);

SDL_Texture *   glyphCache_Text     (void);

/*-----------------------------------------------------------------------------------------------*/

iDeclareType(TextBuf)
iDeclareTypeConstructionArgs(TextBuf, int font, const char *text)

struct Impl_TextBuf {
    SDL_Texture *texture;
    iInt2        size;
};

void    draw_TextBuf        (const iTextBuf *, iInt2 pos, int color);
