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

#pragma once

#include "../media.h"

#include <the_Foundation/rect.h>
#include <SDL_events.h>

iDeclareType(Paint)
iDeclareType(Player)
iDeclareType(PlayerUI)

void    drawSevenSegmentBytes_MediaUI   (int font, iInt2 pos, int majorColor, int minorColor, size_t numBytes);

struct Impl_PlayerUI {
    const iPlayer *player;
    iRect bounds;
    iRect playPauseRect;
    iRect rewindRect;
    iRect scrubberRect;
    iRect volumeRect;
    iRect volumeAdjustRect;
    iRect volumeSlider;
    iRect menuRect;
};

void    init_PlayerUI   (iPlayerUI *, const iPlayer *player, iRect bounds);
void    draw_PlayerUI   (iPlayerUI *, iPaint *p);

/*----------------------------------------------------------------------------------------------*/

iDeclareType(DownloadUI)

struct Impl_DownloadUI {
    const iMedia *media;
    uint16_t mediaId;
    iRect bounds;
};

void    init_DownloadUI         (iDownloadUI *, const iMedia *media, uint16_t mediaId, iRect bounds);
iBool   processEvent_DownloadUI (iDownloadUI *, const SDL_Event *ev);
void    draw_DownloadUI         (const iDownloadUI *, iPaint *p);
