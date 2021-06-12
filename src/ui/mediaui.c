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

#include "mediaui.h"
#include "media.h"
#include "documentwidget.h"
#include "gmdocument.h"
#include "audio/player.h"
#include "paint.h"
#include "util.h"
#include "lang.h"

#include <the_Foundation/path.h>

static const char *volumeChar_(float volume) {
    if (volume <= 0) {
        return "\U0001f507";
    }
    if (volume < 0.4f) {
        return "\U0001f508";
    }
    if (volume < 0.8f) {
        return "\U0001f509";
    }
    return "\U0001f50a";
}

void init_PlayerUI(iPlayerUI *d, const iPlayer *player, iRect bounds) {
    d->player = player;
    d->bounds = bounds;
    const int height = height_Rect(bounds);
    d->playPauseRect = (iRect){ addX_I2(topLeft_Rect(bounds), gap_UI / 2), init_I2(3 * height / 2, height) };
    d->rewindRect    = (iRect){ topRight_Rect(d->playPauseRect), init1_I2(height) };
    d->menuRect      = (iRect){ addX_I2(topRight_Rect(bounds), -height - gap_UI / 2), init1_I2(height) };
    d->volumeRect    = (iRect){ addX_I2(topLeft_Rect(d->menuRect), -height), init1_I2(height) };
    d->volumeAdjustRect = d->volumeRect;
    adjustEdges_Rect(&d->volumeAdjustRect, 0, 0, 0, -35 * gap_UI);
    d->scrubberRect  = initCorners_Rect(topRight_Rect(d->rewindRect), bottomLeft_Rect(d->volumeRect));
    /* Volume slider. */ {
        d->volumeSlider = shrunk_Rect(d->volumeAdjustRect, init_I2(gap_UI / 2, gap_UI));
        adjustEdges_Rect(&d->volumeSlider, 0, -width_Rect(d->volumeRect) - 2 * gap_UI, 0, 5 * gap_UI);
    }
}

static void drawPlayerButton_(iPaint *p, iRect rect, const char *label, int font) {
    const iInt2 mouse     = mouseCoord_Window(get_Window());
    const iBool isHover   = contains_Rect(rect, mouse);
    const iBool isPressed = isHover && (SDL_GetMouseState(NULL, NULL) & SDL_BUTTON_LEFT) != 0;
    const int frame = (isPressed ? uiTextCaution_ColorId : isHover ? uiHeading_ColorId : uiAnnotation_ColorId);
    iRect frameRect = shrunk_Rect(rect, init_I2(gap_UI / 2, gap_UI));
    drawRect_Paint(p, frameRect, frame);
    if (isPressed) {
        fillRect_Paint(
            p,
            adjusted_Rect(shrunk_Rect(frameRect, divi_I2(gap2_UI, 2)), zero_I2(), one_I2()),
            frame);
    }
    const int fg = isPressed ? (permanent_ColorId | uiBackground_ColorId) : uiHeading_ColorId;
    drawCentered_Text(font, frameRect, iTrue, fg, "%s", label);
}

static const uint32_t sevenSegmentDigit_ = 0x1fbf0;

static const char *sevenSegmentStr_ = "\U0001fbf0";

static int drawSevenSegmentTime_(iInt2 pos, int color, int align, int seconds) { /* returns width */
    const int hours = seconds / 3600;
    const int mins  = (seconds / 60) % 60;
    const int secs  = seconds % 60;
    const int font  = defaultBig_FontId;
    iString   num;
    init_String(&num);
    if (hours) {
        appendChar_String(&num, sevenSegmentDigit_ + (hours % 10));
        appendChar_String(&num, ':');
    }
    appendChar_String(&num, sevenSegmentDigit_ + (mins / 10) % 10);
    appendChar_String(&num, sevenSegmentDigit_ + (mins % 10));
    appendChar_String(&num, ':');
    appendChar_String(&num, sevenSegmentDigit_ + (secs / 10) % 10);
    appendChar_String(&num, sevenSegmentDigit_ + (secs % 10));
    iInt2 size = advanceRange_Text(font, range_String(&num));
    if (align == right_Alignment) {
        pos.x -= size.x;
    }
    drawRange_Text(font, addY_I2(pos, -gap_UI / 8), color, range_String(&num));
    deinit_String(&num);
    return size.x;
}

void draw_PlayerUI(iPlayerUI *d, iPaint *p) {
    const int   playerBackground_ColorId = uiBackground_ColorId;
    const int   playerFrame_ColorId      = uiSeparator_ColorId;
    const iBool isAdjusting = (flags_Player(d->player) & adjustingVolume_PlayerFlag) != 0;
    fillRect_Paint(p, d->bounds, playerBackground_ColorId);
    drawRect_Paint(p, d->bounds, playerFrame_ColorId);
    drawPlayerButton_(p,
                      d->playPauseRect,
                      isPaused_Player(d->player) ? "\U0001f782" : "\u23f8",
                      uiContent_FontId);
    drawPlayerButton_(p, d->rewindRect, "\u23ee", uiContent_FontId);
    drawPlayerButton_(p, d->menuRect, menu_Icon, uiContent_FontId);
    if (!isAdjusting) {
        drawPlayerButton_(
            p, d->volumeRect, volumeChar_(volume_Player(d->player)), uiContentSymbols_FontId);
    }
    const int   hgt       = lineHeight_Text(defaultBig_FontId);
    const int   yMid      = mid_Rect(d->scrubberRect).y;
    const float playTime  = time_Player(d->player);
    const float totalTime = duration_Player(d->player);
    const int   bright    = uiHeading_ColorId;
    const int   dim       = uiAnnotation_ColorId;
    int leftWidth = drawSevenSegmentTime_(
        init_I2(left_Rect(d->scrubberRect) + 2 * gap_UI, yMid - hgt / 2),
        isPaused_Player(d->player) ? dim : bright,
        left_Alignment,
        iRound(playTime));
    int rightWidth = 0;
    if (totalTime > 0) {
        rightWidth =
            drawSevenSegmentTime_(init_I2(right_Rect(d->scrubberRect) - 2 * gap_UI, yMid - hgt / 2),
                                  dim,
                                  right_Alignment,
                                  iRound(totalTime));
    }
    /* Scrubber. */
    const int   s1       = left_Rect(d->scrubberRect) + leftWidth + 6 * gap_UI;
    const int   s2       = right_Rect(d->scrubberRect) - rightWidth - 6 * gap_UI;
    const float normPos  = totalTime > 0 ? playTime / totalTime : 0.0f;
    const int   part     = (s2 - s1) * normPos;
    const int   scrubMax = (s2 - s1) * streamProgress_Player(d->player);
    drawHLine_Paint(p, init_I2(s1, yMid), part, bright);
    drawHLine_Paint(p, init_I2(s1 + part, yMid), scrubMax - part, dim);
    const char *dot = "\u23fa";
    const int dotWidth = advance_Text(uiLabel_FontId, dot).x;
    draw_Text(uiLabel_FontId,
              init_I2(s1 * (1.0f - normPos) + s2 * normPos - dotWidth / 2,
                      yMid - lineHeight_Text(uiLabel_FontId) / 2),
              isPaused_Player(d->player) ? dim : bright,
              dot);
    /* Volume adjustment. */
    if (isAdjusting) {
        const iInt2 mouse   = mouseCoord_Window(get_Window());
        const iBool isHover = contains_Rect(d->volumeRect, mouse) &&
                              ~flags_Player(d->player) & volumeGrabbed_PlayerFlag;
        const iBool isPressed = (SDL_GetMouseState(NULL, NULL) & SDL_BUTTON_LEFT) != 0;
        iRect adjRect = shrunk_Rect(d->volumeAdjustRect, init_I2(gap_UI / 2, gap_UI));
        fillRect_Paint(p, adjRect, playerBackground_ColorId);
        drawRect_Paint(p, adjRect, bright);
        if (isHover) {
            fillRect_Paint(
                p,
                shrunk_Rect(d->volumeRect, init_I2(gap_UI / 2 + gap_UI / 2, 3 * gap_UI / 2)),
                isPressed ? uiTextCaution_ColorId : bright);
        }
        drawCentered_Text(uiContentSymbols_FontId,
                          d->volumeRect,
                          iTrue,
                          isHover ? playerBackground_ColorId : bright,
                          volumeChar_(volume_Player(d->player)));
        const int volColor =
            flags_Player(d->player) & volumeGrabbed_PlayerFlag ? uiTextCaution_ColorId : bright;
        const int volPart = volume_Player(d->player) * width_Rect(d->volumeSlider);
        const iInt2 volPos = init_I2(left_Rect(d->volumeSlider), mid_Rect(d->volumeSlider).y);
        drawHLine_Paint(p, volPos, volPart, volColor);
        drawHLine_Paint(p,
                        addX_I2(volPos, volPart),
                        width_Rect(d->volumeSlider) - volPart,
                        dim);
        draw_Text(uiLabel_FontId,
                  init_I2(left_Rect(d->volumeSlider) + volPart - dotWidth / 2,
                          yMid - lineHeight_Text(uiLabel_FontId) / 2),
                  volColor,
                  dot);
    }
}

/*----------------------------------------------------------------------------------------------*/

static void drawSevenSegmentBytes_(iInt2 pos, int color, size_t numBytes) {
    iString digits;
    init_String(&digits);
    if (numBytes == 0) {
        appendChar_String(&digits, sevenSegmentDigit_);
    }
    else {
        int magnitude = 0;
        while (numBytes) {
            if (magnitude == 3) {
                prependCStr_String(&digits, "\u2024");
            }
            else if (magnitude == 6) {
                prependCStr_String(&digits, restore_ColorEscape "\u2024");
            }
            else if (magnitude == 9) {
                prependCStr_String(&digits, "\u2024");
            }
            prependChar_String(&digits, sevenSegmentDigit_ + (numBytes % 10));
            numBytes /= 10;
            magnitude++;
        }
        if (magnitude > 6) {
            prependCStr_String(&digits, uiTextStrong_ColorEscape);
        }
    }
    const int font = uiLabel_FontId;
    const iInt2 dims = advanceRange_Text(font, range_String(&digits));
    drawRange_Text(font, addX_I2(pos, -dims.x), color, range_String(&digits));
    deinit_String(&digits);
}

void init_DownloadUI(iDownloadUI *d, const iDocumentWidget *doc, uint16_t mediaId, iRect bounds) {
    d->doc     = doc;
    d->mediaId = mediaId;
    d->bounds  = bounds;
}

iBool processEvent_DownloadUI(iDownloadUI *d, const SDL_Event *ev) {
    return iFalse;
}

void draw_DownloadUI(const iDownloadUI *d, iPaint *p) {
    const iMedia *media = constMedia_GmDocument(document_DocumentWidget(d->doc));
    iGmMediaInfo info;
    float bytesPerSecond;
    const iString *path;
    iBool isFinished;
    downloadInfo_Media(media, d->mediaId, &info);
    downloadStats_Media(media, d->mediaId, &path, &bytesPerSecond, &isFinished);
    fillRect_Paint(p, d->bounds, uiBackground_ColorId);
    drawRect_Paint(p, d->bounds, uiSeparator_ColorId);
    iRect rect = d->bounds;
    shrink_Rect(&rect, init_I2(3 * gap_UI, 0));
    const int fonts[2] = { uiContentBold_FontId, uiLabel_FontId };
    const int contentHeight = lineHeight_Text(fonts[0]) + lineHeight_Text(fonts[1]);
    const int x = left_Rect(rect);
    const int y1 = mid_Rect(rect).y - contentHeight / 2;
    const int y2 = y1 + lineHeight_Text(fonts[1]);
    if (path) {
        drawRange_Text(fonts[0], init_I2(x, y1), uiHeading_ColorId, baseName_Path(path));
    }
    draw_Text(uiLabel_FontId,
              init_I2(x, y2),
              isFinished ? uiTextAction_ColorId : uiTextDim_ColorId,
              cstr_Lang(isFinished ? "media.download.complete" : "media.download.warnclose"));
    const int x2 = right_Rect(rect);
    drawSevenSegmentBytes_(init_I2(x2, y1), uiTextDim_ColorId, info.numBytes);
    const iInt2 pos = init_I2(x2, y2);
    if (bytesPerSecond > 0) {
        drawAlign_Text(uiLabel_FontId, pos, uiTextDim_ColorId, right_Alignment,
                       translateCStr_Lang("%.3f ${mb.per.sec}"),
                       bytesPerSecond / 1.0e6);
    }
    else {
        drawAlign_Text(uiLabel_FontId, pos, uiTextDim_ColorId, right_Alignment,
                       translateCStr_Lang("\u2014 ${mb.per.sec}"));
    }
}
