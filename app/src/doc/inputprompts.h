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
#include <the_Foundation/ptrarray.h>
#include <the_Foundation/rect.h>

iDeclareType(DocumentView)
iDeclareType(DocumentWidget)
iDeclareType(GmResponse)
iDeclareType(Widget)

iDeclareType(InputPrompts)
iDeclareTypeConstruction(InputPrompts)

struct Impl_InputPrompts {
    iDocumentWidget *owner;
    iPtrArray        outgoing; /* bars of the outgoing document during a swipe */
    iPtrArray        covered;  /* bars hidden underneath the outgoing view */
};

void        setOwner_InputPrompts           (iInputPrompts *, iDocumentWidget *owner);
iWidget *   create_InputPrompts             (iInputPrompts *, iGmLinkId linkId, const iString *url,
                                             iBool isSensitive, const iString *promptLabel);
void        show_InputPrompts               (iInputPrompts *, iGmLinkId linkId, const iString *baseUrl,
                                             const iGmResponse *, enum iGmStatusCode);
void        showForPromptUrls_InputPrompts  (iInputPrompts *);
void        destroy_InputPrompts            (iInputPrompts *, iGmLinkId linkId);
void        destroyAll_InputPrompts         (iInputPrompts *);
void        setEnabled_InputPrompts         (iInputPrompts *, iGmLinkId linkId, iBool enabled);
void        reenableAll_InputPrompts        (iInputPrompts *);
void        ensureVisible_InputPrompts      (iInputPrompts *, iGmLinkId linkId);
void        refreshAfterChange_InputPrompts (iInputPrompts *);
void        deferOutgoing_InputPrompts      (iInputPrompts *); /* take the outgoing bars aside */
void        resetAfterSwipe_InputPrompts    (iInputPrompts *);
void        reposition_InputPrompts         (iInputPrompts *, iDocumentView *);
void        repositionOutgoing_InputPrompts (iInputPrompts *);

iWidget *   findBar_InputPrompts            (const iInputPrompts *, iGmLinkId linkId);
void        drawOutgoing_InputPrompts       (const iInputPrompts *, const iRect *clipBounds);
void        drawCovered_InputPrompts        (const iInputPrompts *, const iRect *clipBounds);
