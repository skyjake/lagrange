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

iDeclareType(Query)
iDeclareTypeConstruction(Query)

/* Note the following about swipes and keeping inline input prompts positioned:

   An inline input prompt is a real widget, similar to modal input dialogs.
   During a back/forward swipe, up to two DocumentViews are visible at
   once (the current view and DocumentSwipe's outgoing view, one drawn sliding over
   the other), and either one may have one or more prompt widgets as children.
   The prompts need to keep tracking their documents as they shift, and all
   prompts need to be drawn at the correct Z order alongside their view's content.
   This means a normal `drawChildren_Widget()` is not possible, because children
   are drawn last, on top of everything.

   We use `deferredDraw_WidgetFlag2` that excludes a child from
   `drawChildren_Widget()`. Each flagged bar is drawn manually so they appear in
   the correct order vs. the DocumentViews.

   The incoming/outgoing prompts are handled differently because of how the
   underlying document is swapped:

   - Incoming: A DocumentWidget's current view's own prompts are normal findable
     widgets, except when covered by the outgoing view during a back-swipe
     (`covered` in Query).

   - Outgoing: The outgoing view's prompts (`outgoing` in Query).
     `releaseViewDocument_DocumentWidget_()` allocates a new DocumentView for
     the incoming document while the swipe animates, and link IDs are per-document,
     so the incoming view may reuse the same "inputpromptN" IDs. Therefore, the
     outgoing prompt widgets are renamed to avoid clashing with the incoming live
     prompts. The horizontal positions are shifted by `visualOffset`.
     The prompts are kept around, hidden and unfindable, until the swipe finishes.
*/

struct Impl_Query {
    iDocumentWidget *owner;
    iPtrArray        outgoing; /* bars of the outgoing document during a swipe */
    iPtrArray        covered;  /* bars covered by the outgoing view */
};

void        setOwner_Query          (iQuery *, iDocumentWidget *owner);
iWidget *   create_Query            (iQuery *, iGmLinkId linkId, const iString *url,
                                     iBool isSensitive, const iString *promptLabel);
iWidget *   makeModal_Query         (iQuery *, const iString *url, iBool isSensitive,
                                     const char *promptLabel, const char *acceptCommand);
/* Returns iTrue if the command was fully handled; some prompt commands are handled but
   still let the owner continue processing. */
iBool       handleCommand_Query     (iQuery *, const char *cmd);
void        show_Query              (iQuery *, iGmLinkId linkId, const iString *baseUrl,
                                     const iGmResponse *, enum iGmStatusCode);
void        showForPromptUrls_Query (iQuery *);
void        destroy_Query           (iQuery *, iGmLinkId linkId);
void        destroyAll_Query        (iQuery *);
void        setEnabled_Query        (iQuery *, iGmLinkId linkId, iBool enabled);
void        reenableAll_Query       (iQuery *);
void        ensureVisible_Query     (iQuery *, iGmLinkId linkId);
void        refreshAfterChange_Query(iQuery *);
void        deferOutgoing_Query     (iQuery *); /* take the outgoing bars aside */
void        resetAfterSwipe_Query   (iQuery *);
void        reposition_Query        (iQuery *, iDocumentView *);
void        repositionOutgoing_Query(iQuery *);

iWidget *   findBar_Query           (const iQuery *, iGmLinkId linkId);
void        drawOutgoing_Query      (const iQuery *, const iRect *clipBounds);
void        drawCovered_Query       (const iQuery *, const iRect *clipBounds);
