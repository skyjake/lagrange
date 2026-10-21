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

/* Private declarations shared by the DocumentWidget sources. The core is in
   ui/documentwidget.c, and the components it owns are in the other sources under doc. */

#include "ui/documentwidget.h"
#include "ui/util.h"
#include "documentview.h"
#include "persistentstate.h"
#include "swipe.h"

#include <SDL3/SDL_timer.h>

iDeclareType(Banner)
iDeclareType(DocumentBook)
iDeclareType(DocumentFetch)
iDeclareType(IndicatorWidget)
iDeclareType(InlineMedia)
iDeclareType(LinkInfo)
iDeclareType(MenuItem)
iDeclareType(Query)
iDeclareType(ScrollWidget)
iDeclareType(ToolJob)
iDeclareType(Translation)
iDeclareType(ViewToolbar)

enum iDocumentLinkOrdinalMode {
    numbersAndAlphabet_DocumentLinkOrdinalMode,
    homeRow_DocumentLinkOrdinalMode,
};

enum iLinkUpdateFlags {
    visited_LinkUpdateFlag = iBit(1),
    open_LinkUpdateFlag    = iBit(2),
};

enum iDocumentWidgetFlag {
    selecting_DocumentWidgetFlag             = iBit(1),
    noHoverWhileScrolling_DocumentWidgetFlag = iBit(2),
    showLinkNumbers_DocumentWidgetFlag       = iBit(3),
    setHoverViaKeys_DocumentWidgetFlag       = iBit(4),
    newTabViaHomeKeys_DocumentWidgetFlag     = iBit(5),
    selectWords_DocumentWidgetFlag           = iBit(6),
    selectLines_DocumentWidgetFlag           = iBit(7),
    movingSelectMarkStart_DocumentWidgetFlag = iBit(8),
    movingSelectMarkEnd_DocumentWidgetFlag   = iBit(9),
    otherRootByDefault_DocumentWidgetFlag    = iBit(10), /* links open to other root by default */
    urlChanged_DocumentWidgetFlag            = iBit(11),
    drawDownloadCounter_DocumentWidgetFlag   = iBit(12),
    viewSource_DocumentWidgetFlag            = iBit(13),
    proxyRequest_DocumentWidgetFlag          = iBit(14),
    unseen_DocumentWidgetFlag                = iBit(15), /* user has not seen the contents */
    openedExternally_DocumentWidgetFlag      = iBit(16), /* opened via an external URL event */
    historySwipe_DocumentWidgetFlag          = iBit(17), /* paginated swipe moves the views */
    swipedUp_DocumentWidgetFlag              = iBit(18), /* touch gesture has opened the sidebar */
};

enum iDocumentIndicator {
    none_DocumentIndicator,
    pinchZoom_DocumentIndicator,
    viewMode_DocumentIndicator,
};

/* Note the following about swipes and keeping inline input prompts positioned:

   An inline input prompt is a real widget, similar to what appears in a modal
   dialog. During a back/forward swipe, up to two DocumentViews are visible at
   once (the current view and DocumentSwipe's outgoing view, one drawn sliding over
   the other), and either
   one may have one or more prompt widgets as children. The prompts need to keep
   tracking their documents as they shift, and all prompts need to be drawn at
   the correct Z order alongside their view's content. This means a normal
   `drawChildren_Widget()` is not possible, because children are drawn last, on
   top of everything.

   We use `deferredDraw_WidgetFlag2` that excludes a child from
   `drawChildren_Widget()`. Each flagged bar is drawn manually so they appear in
   the correct order vs. the DocumentViews.

   The incoming/outgoing prompts are handled differently because of how the
   underlying document is swapped:

   - Incoming: d->view's own prompts are normal findable widgets, except when
     covered by the outgoing view during a back-swipe, or offset by rubber-banding
     at the ends of history.

   - Outgoing: the outgoing view's prompts. `releaseViewDocument_DocumentWidget_()`
     allocates a new DocumentView for the incoming document while the swipe
     animates, and link IDs are per-document, so the incoming view may reuse the
     same "inputpromptN" IDs. Therefore, the outgoing prompt widgets are renamed
     to avoid clashing with the incoming live prompts. The horizontal positions
     are driven by `visualOffset` rather than by moving `rect` itself, since
     there is no longer a live document layout to reference. The prompts are
     kept around, hidden and unfindable, until the swipe finishes.
*/

struct Impl_DocumentWidget {
    iWidget        widget;
    int            flags; /* internal behavior (enum iDocumentWidgetFlag) */

    /* Components: */
    iDocumentFetch *fetch;
    iDocumentView *view;
    iBanner *      banner;
    iInlineMedia * media;
    iQuery *       query;
    iDocumentSwipe *swipe;
    iDocumentBook *book;
    iViewToolbar  *viewToolbar;
    SDL_TimerID     readingBarsTimer; /* toggle unless the click becomes a double click */
    SDL_TimerID     viewModeTimer;    /* view mode indicator is shown while running */
    enum iDocumentIndicator indicator; /* remains set while fading out */
    iAnim           indicatorOpacity;
    iLinkInfo *    linkInfo;

    /* Document: */
    iPersistentDocumentState mod;
    iString *      titleUser;
    float          initNormScrollY;
    iGmScrollAnchor initScrollAnchor;
    enum iLinkUpdateFlags pendingLinkUpdates;

    /* User interface: */
    enum iDocumentLinkOrdinalMode ordinalMode;
    size_t         ordinalBase;
    iRangecc       selectMark;
    iRangecc       initialSelectMark; /* for word/line selection */
    iRangecc       foundMark;
    const iGmRun * contextLink;
    iClick         click;
    iClick         midClick;   /* opens link */
    iInt2          contextPos; /* coordinates of latest right click */
    iString        pendingGotoHeading;
    iString        linePrecedingLink;
    iString        originId;       /* origin document's widget ID, if opened via cross-split nav */
    iBool          originToNewTab; /* ...whether that navigation created a fresh tab */
    iString        readerEntryUrl; /* feed entry being viewed (may have a fragment) */
    int            swipeUpY;       /* vertical movement of the current paginated swipe */
    uint32_t       swipeUpTime;
    uint32_t       layoutHandoffTime; /* when continued layout was last shown */
    size_t         numHeadings;

    /* Widget structure: */
    iScrollWidget *scroll;
    iWidget *      footerButtons;
    iWidget *      menu;
    iWidget *      copyMenu;
    iTranslation * translation;
    iToolJob *     toolJob;
    iIndicatorWidget *progress;
    iWidget *      phoneToolbar;
};

/* DocumentViewHost callbacks. */
iWidget *       footerButtons_DocumentWidget            (const iDocumentWidget *);
int             footerHeight_DocumentWidget             (const iDocumentWidget *);
int             footerClearance_DocumentWidget          (const iDocumentWidget *);
int             phoneToolbarHeight_DocumentWidget       (const iDocumentWidget *);
int             phoneBottomNavbarHeight_DocumentWidget  (const iDocumentWidget *);
iScrollWidget * scrollBar_DocumentWidget                (const iDocumentWidget *);
iBool           isBlank_DocumentWidget                  (const iDocumentWidget *);
iBool           isHoverAllowed_DocumentWidget           (const iDocumentWidget *);
iBool           noHoverWhileScrolling_DocumentWidget    (const iDocumentWidget *);
iBool           isPrerenderingAllowed_DocumentWidget    (const iDocumentWidget *);
iBool           isShowingLinkNumbers_DocumentWidget     (const iDocumentWidget *);
size_t          ordinalBase_DocumentWidget              (const iDocumentWidget *);
iChar           linkOrdinalChar_DocumentWidget          (const iDocumentWidget *, size_t ord);
iRangecc        selectionMark_DocumentWidget            (const iDocumentWidget *);
iTime           sourceTime_DocumentWidget               (const iDocumentWidget *);
iMediaRequest * findMediaRequest_DocumentWidget         (const iDocumentWidget *, iGmLinkId linkId);
enum iWheelSwipeState wheelSwipeState_DocumentWidget    (const iDocumentWidget *);
void            animate_DocumentWidget                  (iAny *); /* ticker */
void            refreshWhileScrolling_DocumentWidget    (iAny *); /* ticker */
void            scrollBegan_DocumentWidget              (iAnyObject *, int, uint32_t);
void            aboutToScrollView_DocumentWidget        (iDocumentWidget *, int scrollMax);
void            didScrollView_DocumentWidget            (iDocumentWidget *);
void            documentRunsInvalidated_DocumentWidget  (iDocumentWidget *);
void            repositionInlinePrompts_DocumentWidget  (iDocumentWidget *, iDocumentView *);
void            updateHoverLinkInfo_DocumentWidget      (iDocumentWidget *, iDocumentView *, uint16_t linkId);
void            pagesMoved_DocumentWidget               (iDocumentWidget *); /* paginated: horizontal position changed */

/* Core (ui/documentwidget.c). */
void            invalidate_DocumentWidget               (iDocumentWidget *);
void            replaceDocument_DocumentWidget          (iDocumentWidget *, iGmDocument *newDoc);
iBool           setDocumentUrl_DocumentWidget           (iDocumentWidget *, const iString *url);
void            updateTheme_DocumentWidget              (iDocumentWidget *);
void            updateBanner_DocumentWidget             (iDocumentWidget *);
void            updateWindowTitle_DocumentWidget        (const iDocumentWidget *);
void            makeFooterButtons_DocumentWidget        (iDocumentWidget *, const iMenuItem *items, size_t count);
void            setLinkNumberMode_DocumentWidget        (iDocumentWidget *, iBool set);
const char *    setIdentArg_DocumentWidget              (const iDocumentWidget *, const iString *dstUrl);
iBool           isSpartanQueryLink_DocumentWidget       (const iDocumentWidget *, iGmLinkId);
void            interactingWithLink_DocumentWidget      (iDocumentWidget *, iGmLinkId);

/* Saving (doc/save.c). */
iBool           handleSaveCommand_DocumentWidget        (iDocumentWidget *, const char *cmd);
