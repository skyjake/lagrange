/* Copyright 2020-2026 Jaakko Keränen <jaakko.keranen@iki.fi>
   Copyright 2026 Sidney Cammeresi <sac@cheesecake.org>

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

#if LAGRANGE_INPUT_PREEDIT

void initPreedit_InputWidget_(iInputWidget *d) {
    init_String(&d->preedit.text);
    d->preedit.cursor  = 0;
    d->preedit.length  = 0;
    d->preedit.advance = 0;
}

void deinitPreedit_InputWidget_(iInputWidget *d) {
    deinit_String(&d->preedit.text);
}

void clearPreedit_InputWidget_(iInputWidget *d) {
    clear_String(&d->preedit.text);
    d->preedit.cursor  = 0;
    d->preedit.length  = 0;
    d->preedit.advance = 0;
}

/* Convert a character (codepoint) offset to a byte offset within a UTF-8 string. */
static int charToByteOffset_(const char *s, const char *end, int charOff) {
    const char *p = s;
    for (int i = 0; i < charOff && p < end; i++) {
        iChar ch;
        int n = decodeBytes_MultibyteChar(p, end, &ch);
        p += iMax(n, 1);
    }
    return (int)(p - s);
}

/* Store preedit state from an SDL TEXTEDITING or TEXTEDITING_EXT event.
   Converts the cursor/length from character offsets (as SDL provides) to
   byte offsets for internal use. */
static void setPreedit_InputWidget_(iInputWidget *d, const char *text, int start, int length) {
    iInputPreedit *pe = &d->preedit;
    setCStr_String(&pe->text, text);
    const iRangecc range = range_String(&pe->text);
    pe->cursor  = charToByteOffset_(range.start, range.end, start);
    pe->length  = charToByteOffset_(range.start + pe->cursor, range.end, length);
    pe->advance = isEmpty_String(&pe->text) ? 0 : measureRange_Text(d->font, range).advance.x;
}

/* Finalize any in-progress IME composition by inserting the preedit text. Called
   when the widget loses focus or editing ends, so partially composed
   text (e.g., an uncommitted Korean syllable) isn't silently discarded. */
void commitPreedit_InputWidget_(iInputWidget *d) {
    if (!isEmpty_String(&d->preedit.text)) {
        pushUndo_EditBuf(&d->buf);
        insertRange_InputWidget_(d, range_String(&d->preedit.text));
        contentsWereChanged_InputWidget_(d);
        clearPreedit_InputWidget_(d);
    }
}

iBool processTextEditing_InputWidget_(iInputWidget *d, const SDL_Event *ev) {
    if (ev->type == SDL_EVENT_TEXT_EDITING && isFocused_Widget(d)) {
        /* IME is composing text that hasn't been committed yet (e.g., assembling a
           Korean syllable from individual jamo keystrokes). We store it separately
           and draw it inline at the cursor position. SDL3's composition string has
           no length limit, so there is no separate "extended" event to handle. */
        const char *compText  = ev->edit.text;
        const int   compStart = ev->edit.start;
        const int   compLen   = ev->edit.length;
        if (compText[0] == '\0') {
            /* Empty composition: IME cancelled or composition ended without commit. */
            clearPreedit_InputWidget_(d);
        }
        else {
            if (isEmpty_String(&d->preedit.text)) {
                /* First event of a new composition. Delete any selected text. */
                pushUndo_EditBuf(&d->buf);
                deleteMarked_InputWidget_(d);
            }
            setPreedit_InputWidget_(d, compText, compStart, compLen);
        }
        showCursor_InputWidget_(d);
        updateAllLinesAndResizeHeight_InputWidget_(d);
        updateTextInputRect_InputWidget_(d);
        d->inFlags |= needUpdateBuffer_InputWidgetFlag;
        refresh_Widget(d);
        return iTrue;
    }
    return iFalse;
}

/* Draws an underline under the preedit text. `endCoord` is where the preedit ends. */
void drawPreedit_InputWidget_(const iInputWidget *d, iPaint *p, iInt2 origin,
                              const iString *preeditLine, iInt2 endCoord) {
    const iInputPreedit *pe = &d->preedit;
    const iInt2 cursor = d->buf.cursor;
    iWrapText wt = wrapParams_InputWidget_(d);
    wt.text = range_String(preeditLine);
    const int lh = lineHeight_Text(d->font);
    const int mw = wt.maxWidth;
    /* Find where the preedit starts in the wrapped layout. */
    wt.hitChar = wt.text.start + cursor.x;
    measure_WrapText(&wt, d->font);
    const iInt2 compCoord = wt.hitAdvance_out;
    if (pe->length > 0 && pe->cursor + pe->length <= size_String(&pe->text)) {
        /* The preedit has three regions: "before" (thin underline),
           "active" (double underline for the clause being converted),
           and "after" (thin underline). Compute interior boundary coords. */
        wt.hitChar = wt.text.start + cursor.x + pe->cursor;
        measure_WrapText(&wt, d->font);
        const iInt2 actStart = wt.hitAdvance_out;
        wt.hitChar = wt.text.start + cursor.x + pe->cursor + pe->length;
        measure_WrapText(&wt, d->font);
        const iInt2 actEnd = wt.hitAdvance_out;
        /* "Before" segment: thin underline. */
        if (pe->cursor > 0) {
            for (int ly = compCoord.y; ly <= actStart.y; ly += lh) {
                const int x0 = (ly == compCoord.y ? compCoord.x : 0);
                const int x1 = (ly == actStart.y ? actStart.x : mw);
                if (x1 > x0) {
                    drawHLine_Paint(p, add_I2(origin, init_I2(x0, ly + lh - 1)),
                                    x1 - x0, uiInputCursor_ColorId);
                }
            }
        }
        /* "Active" segment: double underline. */
        for (int ly = actStart.y; ly <= actEnd.y; ly += lh) {
            const int x0 = (ly == actStart.y ? actStart.x : 0);
            const int x1 = (ly == actEnd.y ? actEnd.x : mw);
            if (x1 > x0) {
                drawHLine_Paint(p, add_I2(origin, init_I2(x0, ly + lh - 1)),
                                x1 - x0, uiInputCursor_ColorId);
                drawHLine_Paint(p, add_I2(origin, init_I2(x0, ly + lh - 2)),
                                x1 - x0, uiInputCursor_ColorId);
            }
        }
        /* "After" segment: thin underline. */
        if (pe->cursor + pe->length < (int) size_String(&pe->text)) {
            for (int ly = actEnd.y; ly <= endCoord.y; ly += lh) {
                const int x0 = (ly == actEnd.y ? actEnd.x : 0);
                const int x1 = (ly == endCoord.y ? endCoord.x : mw);
                if (x1 > x0) {
                    drawHLine_Paint(p, add_I2(origin, init_I2(x0, ly + lh - 1)),
                                    x1 - x0, uiInputCursor_ColorId);
                }
            }
        }
    }
    else {
        /* Simple case: single thin underline for entire preedit. */
        for (int ly = compCoord.y; ly <= endCoord.y; ly += lh) {
            const int x0 = (ly == compCoord.y ? compCoord.x : 0);
            const int x1 = (ly == endCoord.y ? endCoord.x : mw);
            if (x1 > x0) {
                drawHLine_Paint(p, add_I2(origin, init_I2(x0, ly + lh - 1)),
                                x1 - x0, uiInputCursor_ColorId);
            }
        }
    }
}

#endif /* LAGRANGE_INPUT_PREEDIT */
