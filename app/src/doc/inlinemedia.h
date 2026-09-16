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

#pragma once

#include "../gmdocument.h"
#include "../media/media.h"
#include <the_Foundation/objectlist.h>
#include <SDL3/SDL_events.h>

iDeclareType(DocumentWidget)
iDeclareType(Widget)

iDeclareType(InlineMedia)
iDeclareTypeConstruction(InlineMedia)

struct Impl_InlineMedia {
    iDocumentWidget *owner;
    iObjectList *    media;       /* ongoing media requests */
    uint32_t         lastMediaInterval;
    const iGmRun *   grabbedPlayer; /* currently adjusting volume in a player */
    float            grabbedStartVolume;
    int              mediaTimer;
    iWidget *        playerMenu;
};

void            setOwner_InlineMedia            (iInlineMedia *, iDocumentWidget *owner);

void            update_InlineMedia              (iInlineMedia *);
void            animate_InlineMedia             (iInlineMedia *);
void            clearRequests_InlineMedia       (iInlineMedia *);
void            cancelRequests_InlineMedia      (iInlineMedia *);
void            addRequest_InlineMedia          (iInlineMedia *, iMediaRequest *);
void            remove_InlineMedia              (iInlineMedia *, iGmLinkId linkId);
iBool           request_InlineMedia             (iInlineMedia *, iGmLinkId linkId, iBool enableFilters);
iBool           fetchNextUnfetchedImage_InlineMedia(iInlineMedia *);
iBool           handleCommand_InlineMedia       (iInlineMedia *, const char *cmd);
iBool           processEvent_InlineMedia        (iInlineMedia *, const SDL_Event *);

void            setGrabbedPlayer_InlineMedia    (iInlineMedia *, const iGmRun *run); /* NULL releases */
iBool           dragGrabbedPlayer_InlineMedia   (iInlineMedia *, int deltaX);

iMediaRequest * findRequest_InlineMedia          (const iInlineMedia *, iGmLinkId linkId);

iLocalDef iBool isGrabbedPlayer_InlineMedia(const iInlineMedia *d) {
    return d->grabbedPlayer != NULL;
}
