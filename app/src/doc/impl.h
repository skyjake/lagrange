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

/* Private declarations shared by DocumentWidget components. */

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
iDeclareType(TextBuf)
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
    struct {
        iBool active;
        int   initial; /* zoom percentage */
        int   posted;
    } pinch;
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
    iInt2          swipeUpStart;   /* finger position where the swipe began */
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

/* Reader mode (doc/reader.c). */
iBool           updateReaderNavButtons_DocumentWidget   (iDocumentWidget *);
iBool           updateReaderButtons_DocumentWidget      (iDocumentWidget *);
const iArray *  readerFooterItems_DocumentWidget        (const iDocumentWidget *, const iMenuItem *items,
                                                         size_t count);
iBool           openAdjacentFeedEntry_DocumentWidget    (iDocumentWidget *, int dir);
void            readerPullAction_DocumentWidget         (iDocumentWidget *, int dir);
iBool           handleReaderCommand_DocumentWidget      (iDocumentWidget *, const char *cmd);
void            drawReaderPullActions_DocumentWidget    (const iDocumentWidget *);

/* Tools, translation, and uploads (doc/tools.c). */
void            updateToolsMenu_DocumentWidget          (iDocumentWidget *);
iBool           handleToolCommand_DocumentWidget        (iDocumentWidget *, const char *cmd);

/*-----------------------------------------------------------------------------------------------*/
/* Document drawing. */

struct Impl_DrawBufs {
    int          flags;
    SDL_Texture *sideIconBuf;
    SDL_Texture *curlBufs[2]; /* front and back of a curling page */
    iTextBuf    *timestampBuf;
    uint32_t     lastRenderTime;
};

struct Impl_VisBufMeta {
    iGmRunRange runsDrawn;
};

iScrollWidget * hostScrollBar_DocumentView              (const iDocumentView *);
const iDocPrefs *docPrefs_DocumentView                  (const iDocumentView *);
int             runOffset_DocumentView                  (const iDocumentView *, const iGmRun *run);
iRangecc        currentHeading_DocumentView             (const iDocumentView *);
void            allocDrawnVisBuffer_DocumentView        (const iDocumentView *);
