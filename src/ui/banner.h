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

#pragma once

#include "../gmutil.h"

#include <the_Foundation/vec2.h>
#include <SDL_events.h>

iDeclareType(Banner)
iDeclareTypeConstruction(Banner)

iDeclareType(DocumentWidget)
    
enum iBannerType {
    warning_BannerType,
    error_BannerType,
};

void    setOwner_Banner     (iBanner *, iDocumentWidget *owner);
void    setWidth_Banner     (iBanner *, int width);
void    setPos_Banner       (iBanner *, iInt2 pos);

int     height_Banner       (const iBanner *);
size_t  numItems_Banner     (const iBanner *);
iBool   contains_Banner     (const iBanner *, iInt2 coord);

iLocalDef iBool isEmpty_Banner(const iBanner *d) {
    return height_Banner(d) == 0;
}

void    clear_Banner        (iBanner *);
void    setSite_Banner      (iBanner *, iRangecc site, iChar icon);
void    add_Banner          (iBanner *, enum iBannerType type, enum iGmStatusCode code,
                             const iString *message,
                             const iString *details);
void    remove_Banner       (iBanner *, enum iGmStatusCode code);

iBool   processEvent_Banner (iBanner *, const SDL_Event *ev);
void    draw_Banner         (const iBanner *);
