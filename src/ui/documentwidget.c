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

/* TODO: Move DocumentView into a source file of its own. Consider cleaning up the network
   request handling. */

#include "documentwidget.h"

#include "app.h"
#include "audio/player.h"
#include "banner.h"
#include "bookmarks.h"
#include "command.h"
#include "defs.h"
#include "export.h"
#include "gempub.h"
#include "gmcerts.h"
#include "gmdocument.h"
#include "gmrequest.h"
#include "gmutil.h"
#include "gopher.h"
#include "history.h"
#include "indicatorwidget.h"
#include "inputwidget.h"
#include "keys.h"
#include "labelwidget.h"
#include "linkinfo.h"
#include "media.h"
#include "paint.h"
#include "periodic.h"
#include "root.h"
#include "mediaui.h"
#include "scrollwidget.h"
#include "sitespec.h"
#include "touch.h"
#include "translation.h"
#include "uploadwidget.h"
#include "util.h"
#include "visbuf.h"
#include "visited.h"

#if defined (iPlatformAppleDesktop)
#   include "macos.h"
#endif
#if defined (iPlatformAppleMobile)
#   include "ios.h"
#endif
#if defined (iPlatformAndroidMobile)
#   include "android.h"
#endif

#include <the_Foundation/archive.h>
#include <the_Foundation/file.h>
#include <the_Foundation/fileinfo.h>
#include <the_Foundation/objectlist.h>
#include <the_Foundation/path.h>
#include <the_Foundation/ptrarray.h>
#include <the_Foundation/ptrset.h>
#include <the_Foundation/regexp.h>
#include <the_Foundation/stringarray.h>
#include <SDL_clipboard.h>
#include <SDL_timer.h>
#include <SDL_render.h>
#include <ctype.h>
#include <errno.h>

/*----------------------------------------------------------------------------------------------*/

iDeclareType(PersistentDocumentState)
iDeclareTypeConstruction(PersistentDocumentState)
iDeclareTypeSerialization(PersistentDocumentState)

enum iReloadInterval {
    never_RelodPeriod,
    minute_ReloadInterval,
    fiveMinutes_ReloadInterval,
    fifteenMinutes_ReloadInterval,
    hour_ReloadInterval,
    fourHours_ReloadInterval,
    twicePerDay_ReloadInterval,
    day_ReloadInterval,
    max_ReloadInterval
};

static int seconds_ReloadInterval_(enum iReloadInterval d) {
    static const int mins[] = { 0, 1, 5, 15, 60, 4 * 60, 12 * 60, 24 * 60 };
    if (d < 0 || d >= max_ReloadInterval) return 0;
    return mins[d] * 60;
}

static const char *label_ReloadInterval_(enum iReloadInterval d) {
    switch (d) {
        case never_RelodPeriod:
            return cstr_Lang("reload.never");
        case day_ReloadInterval:
            return cstr_Lang("reload.onceperday");
        case minute_ReloadInterval:
        case fiveMinutes_ReloadInterval:
        case fifteenMinutes_ReloadInterval:
            return formatCStr_Lang("num.minutes.n", seconds_ReloadInterval_(d) / 60);
        default:
            return formatCStr_Lang("num.hours.n", seconds_ReloadInterval_(d) / 3600);
    }
    return "";
}

struct Impl_PersistentDocumentState {
    iHistory *history;
    iString * url;
    enum iReloadInterval reloadInterval;
};

void init_PersistentDocumentState(iPersistentDocumentState *d) {
    d->history        = new_History();
    d->url            = new_String();
    d->reloadInterval = 0;
}

void deinit_PersistentDocumentState(iPersistentDocumentState *d) {
    delete_String(d->url);
    delete_History(d->history);
}

void serialize_PersistentDocumentState(const iPersistentDocumentState *d, iStream *outs) {
    serialize_String(d->url, outs);
    uint16_t params = d->reloadInterval & 7;
    writeU16_Stream(outs, params);
    serialize_History(d->history, outs);
}

void deserialize_PersistentDocumentState(iPersistentDocumentState *d, iStream *ins) {
    deserialize_String(d->url, ins);
    if (indexOfCStr_String(d->url, " ptr:0x") != iInvalidPos) {
        /* Oopsie, this should not have been written; invalid URL. */
        clear_String(d->url);
    }
    const uint16_t params = readU16_Stream(ins);
    d->reloadInterval = params & 7;
    deserialize_History(d->history, ins);
}

iDefineTypeConstruction(PersistentDocumentState)

/*----------------------------------------------------------------------------------------------*/

iDeclareType(DrawBufs)

enum iDrawBufsFlag {
    updateSideBuf_DrawBufsFlag      = iBit(1),
    updateTimestampBuf_DrawBufsFlag = iBit(2),
};

struct Impl_DrawBufs {
    int          flags;
    SDL_Texture *sideIconBuf;
    iTextBuf    *timestampBuf;
    uint32_t     lastRenderTime;
};

static void init_DrawBufs(iDrawBufs *d) {
    d->flags = 0;
    d->sideIconBuf = NULL;
    d->timestampBuf = NULL;
    d->lastRenderTime = 0;
}

static void deinit_DrawBufs(iDrawBufs *d) {
    delete_TextBuf(d->timestampBuf);
    if (d->sideIconBuf) {
        SDL_DestroyTexture(d->sideIconBuf);
    }
}

iDefineTypeConstruction(DrawBufs)

/*----------------------------------------------------------------------------------------------*/

iDeclareType(VisBufMeta)

struct Impl_VisBufMeta {
    iGmRunRange runsDrawn;
};

static void visBufInvalidated_(iVisBuf *d, size_t index) {
    iVisBufMeta *meta = d->buffers[index].user;
    iZap(meta->runsDrawn);
}

/*----------------------------------------------------------------------------------------------*/

enum iRequestState {
    blank_RequestState,
    fetching_RequestState,
    receivedPartialResponse_RequestState,
    ready_RequestState,
};

enum iDocumentWidgetFlag {
    selecting_DocumentWidgetFlag             = iBit(1),
    noHoverWhileScrolling_DocumentWidgetFlag = iBit(2),
    showLinkNumbers_DocumentWidgetFlag       = iBit(3),
    setHoverViaKeys_DocumentWidgetFlag       = iBit(4),
    newTabViaHomeKeys_DocumentWidgetFlag     = iBit(5),
    centerVertically_DocumentWidgetFlag      = iBit(6),
    selectWords_DocumentWidgetFlag           = iBit(7),
    selectLines_DocumentWidgetFlag           = iBit(8),
    pinchZoom_DocumentWidgetFlag             = iBit(9),
    movingSelectMarkStart_DocumentWidgetFlag = iBit(10),
    movingSelectMarkEnd_DocumentWidgetFlag   = iBit(11),
    otherRootByDefault_DocumentWidgetFlag    = iBit(12), /* links open to other root by default */
    urlChanged_DocumentWidgetFlag            = iBit(13),
    drawDownloadCounter_DocumentWidgetFlag   = iBit(14),
    fromCache_DocumentWidgetFlag             = iBit(15), /* don't write anything to cache */
    animationPlaceholder_DocumentWidgetFlag  = iBit(16), /* avoid slow operations */
    invalidationPending_DocumentWidgetFlag   = iBit(17), /* invalidate as soon as convenient */
    leftWheelSwipe_DocumentWidgetFlag        = iBit(18), /* swipe state flags are used on desktop */
    rightWheelSwipe_DocumentWidgetFlag       = iBit(19),
    eitherWheelSwipe_DocumentWidgetFlag      = leftWheelSwipe_DocumentWidgetFlag |
                                               rightWheelSwipe_DocumentWidgetFlag,
    viewSource_DocumentWidgetFlag            = iBit(20),
    preventInlining_DocumentWidgetFlag       = iBit(21),
};

enum iDocumentLinkOrdinalMode {
    numbersAndAlphabet_DocumentLinkOrdinalMode,
    homeRow_DocumentLinkOrdinalMode,
};

enum iWheelSwipeState {
    none_WheelSwipeState,
    direct_WheelSwipeState,
};

/* TODO: DocumentView is supposed to be useful on its own; move to a separate source file. */
iDeclareType(DocumentView)

struct Impl_DocumentView {
    iDocumentWidget *owner; /* TODO: Convert to an abstract provider of metrics? */
    iGmDocument *  doc;
    int            pageMargin;
    iSmoothScroll  scrollY;
    iAnim          sideOpacity;
    iAnim          altTextOpacity;
    iGmRunRange    visibleRuns;
    iPtrArray      visibleLinks;
    iPtrArray      visiblePre;
    iPtrArray      visibleMedia; /* currently playing audio / ongoing downloads */
    iPtrArray      visibleWideRuns; /* scrollable blocks; TODO: merge into `visiblePre` */
    const iGmRun * hoverPre;    /* for clicking */
    const iGmRun * hoverAltPre; /* for drawing alt text */
    const iGmRun * hoverLink;
    iArray         wideRunOffsets;
    iAnim          animWideRunOffset;
    uint16_t       animWideRunId;
    iGmRunRange    animWideRunRange;
    iDrawBufs *    drawBufs; /* dynamic state for drawing */
    iVisBuf *      visBuf;
    iVisBufMeta *  visBufMeta;
    iGmRunRange    renderRuns;
    iPtrSet *      invalidRuns;
};

struct Impl_DocumentWidget {
    iWidget        widget;
    int            flags; /* internal behavior, see enum iDocumentWidgetFlag */

    /* User interface: */
    enum iDocumentLinkOrdinalMode ordinalMode;
    size_t         ordinalBase;
    iRangecc       selectMark;
    iRangecc       initialSelectMark; /* for word/line selection */
    iRangecc       foundMark;
    const iGmRun * grabbedPlayer; /* currently adjusting volume in a player */
    float          grabbedStartVolume;
    int            mediaTimer;
    const iGmRun * contextLink;
    iClick         click;
    iInt2          contextPos; /* coordinates of latest right click */
    int            pinchZoomInitial;
    int            pinchZoomPosted;
    float          swipeSpeed; /* points/sec */
    uint32_t       lastSwipeTime;
    int            wheelSwipeDistance;
    enum iWheelSwipeState wheelSwipeState;
    iString        pendingGotoHeading;
    iString        linePrecedingLink;

    /* Network request: */
    enum iRequestState state;
    iGmRequest *   request;
    iGmLinkId      requestLinkId; /* ID of the link that initiated the current request */
    iAtomicInt     isRequestUpdated; /* request has new content, need to parse it */
    int            certFlags;
    iBlock *       certFingerprint;
    iDate          certExpiry;
    iString *      certSubject;
    int            redirectCount;
    iObjectList *  media; /* inline media requests */

    /* Document: */
    iPersistentDocumentState mod;
    iString *      titleUser;
    enum iGmStatusCode sourceStatus;
    iString        sourceHeader;
    iString        sourceMime;
    iBlock         sourceContent; /* original content as received, for saving; set on request finish */
    iTime          sourceTime;
    iGempub *      sourceGempub; /* NULL unless the page is Gempub content */
    iBanner *      banner;
    float          initNormScrollY;

    /* Rendering: */
    iDocumentView  view;
    iLinkInfo *    linkInfo;

    /* Widget structure: */
    iScrollWidget *scroll;
    iWidget *      footerButtons;
    iWidget *      menu;
    iWidget *      playerMenu;
    iWidget *      copyMenu;
    iTranslation * translation;
    iWidget *      phoneToolbar;
};

iDefineObjectConstruction(DocumentWidget)

/* Sorted by proximity to F and J. */
static const int homeRowKeys_[] = {
    'f', 'd', 's', 'a',
    'j', 'k', 'l',
    'r', 'e', 'w', 'q',
    'u', 'i', 'o', 'p',
    'v', 'c', 'x', 'z',
    'm', 'n',
    'g', 'h',
    'b',
    't', 'y',
};
static int docEnum_ = 0;

static void animate_DocumentWidget_                 (void *ticker);
static void animateMedia_DocumentWidget_            (iDocumentWidget *d);
static void updateSideIconBuf_DocumentWidget_       (const iDocumentWidget *d);
static void prerender_DocumentWidget_               (iAny *);
static void scrollBegan_DocumentWidget_             (iAnyObject *, int, uint32_t);
static void refreshWhileScrolling_DocumentWidget_   (iAny *);
static iBool requestMedia_DocumentWidget_           (iDocumentWidget *d, iGmLinkId linkId, iBool enableFilters);

/* TODO: The following methods are called from DocumentView, which goes the wrong way. */

static iRangecc selectMark_DocumentWidget_(const iDocumentWidget *d) {
    /* Normalize so start < end. */
    iRangecc norm = d->selectMark;
    if (norm.start > norm.end) {
        iSwap(const char *, norm.start, norm.end);
    }
    return norm;
}

static int phoneToolbarHeight_DocumentWidget_(const iDocumentWidget *d) {
    if (!d->phoneToolbar) {
        return 0;
    }
    const iWidget *w = constAs_Widget(d);
    return bottom_Rect(rect_Root(w->root)) - top_Rect(boundsWithoutVisualOffset_Widget(d->phoneToolbar));
}

static int footerHeight_DocumentWidget_(const iDocumentWidget *d) {
    int hgt = height_Widget(d->footerButtons);
    if (isPortraitPhone_App()) {
        hgt += phoneToolbarHeight_DocumentWidget_(d);
    }
    return hgt;
}

static iBool isHoverAllowed_DocumentWidget_(const iDocumentWidget *d) {
    if (!isHover_Widget(d)) {
        return iFalse;
    }
    if (!(d->state == ready_RequestState || d->state == receivedPartialResponse_RequestState)) {
        return iFalse;
    }
    if (d->flags & (noHoverWhileScrolling_DocumentWidgetFlag |
                    drawDownloadCounter_DocumentWidgetFlag)) {
        return iFalse;
    }
    if (d->flags & pinchZoom_DocumentWidgetFlag) {
        return iFalse;
    }
    if (flags_Widget(constAs_Widget(d)) & touchDrag_WidgetFlag) {
        return iFalse;
    }
    if (flags_Widget(constAs_Widget(d->scroll)) & pressed_WidgetFlag) {
        return iFalse;
    }
    return iTrue;
}

static iMediaRequest *findMediaRequest_DocumentWidget_(const iDocumentWidget *d, iGmLinkId linkId) {
    iConstForEach(ObjectList, i, d->media) {
        const iMediaRequest *req = (const iMediaRequest *) i.object;
        if (req->linkId == linkId) {
            return iConstCast(iMediaRequest *, req);
        }
    }
    return NULL;
}

static size_t linkOrdinalFromKey_DocumentWidget_(const iDocumentWidget *d, int key) {
    size_t ord = iInvalidPos;
    if (d->ordinalMode == numbersAndAlphabet_DocumentLinkOrdinalMode) {
        if (key >= '1' && key <= '9') {
            return key - '1';
        }
        if (key < 'a' || key > 'z') {
            return iInvalidPos;
        }
        ord = key - 'a' + 9;
#if defined (iPlatformApple)
        /* Skip keys that would conflict with default system shortcuts: hide, minimize, quit, close. */
        if (key == 'h' || key == 'm' || key == 'q' || key == 'w') {
            return iInvalidPos;
        }
        if (key > 'h') ord--;
        if (key > 'm') ord--;
        if (key > 'q') ord--;
        if (key > 'w') ord--;
#endif
    }
    else {
        iForIndices(i, homeRowKeys_) {
            if (homeRowKeys_[i] == key) {
                return i;
            }
        }
    }
    return ord;
}

static iChar linkOrdinalChar_DocumentWidget_(const iDocumentWidget *d, size_t ord) {
    if (d->ordinalMode == numbersAndAlphabet_DocumentLinkOrdinalMode) {
        if (ord < 9) {
            return '1' + ord;
        }
#if defined (iPlatformApple)
        if (ord < 9 + 22) {
            int key = 'a' + ord - 9;
            if (key >= 'h') key++;
            if (key >= 'm') key++;
            if (key >= 'q') key++;
            if (key >= 'w') key++;
            return 'A' + key - 'a';
        }
#else
        if (ord < 9 + 26) {
            return 'A' + ord - 9;
        }
#endif
    }
    else {
        if (ord < iElemCount(homeRowKeys_)) {
            return 'A' + homeRowKeys_[ord] - 'a';
        }
    }
    return 0;
}

/*----------------------------------------------------------------------------------------------*/

void init_DocumentView(iDocumentView *d) {
    d->owner            = NULL;
    d->doc              = new_GmDocument();
    d->invalidRuns      = new_PtrSet();
    d->drawBufs         = new_DrawBufs();
    d->pageMargin       = 5;
    d->hoverPre      = NULL;
    d->hoverAltPre   = NULL;
    d->hoverLink     = NULL;
    d->animWideRunId = 0;
    init_Anim(&d->animWideRunOffset, 0);
    iZap(d->renderRuns);
    iZap(d->visibleRuns);
    d->visBuf = new_VisBuf(); {
        d->visBufMeta = malloc(sizeof(iVisBufMeta) * numBuffers_VisBuf);
        /* Additional metadata for each buffer. */
        d->visBuf->bufferInvalidated = visBufInvalidated_;
        for (size_t i = 0; i < numBuffers_VisBuf; i++) {
            d->visBuf->buffers[i].user = d->visBufMeta + i;
        }
    }
    init_Anim(&d->sideOpacity, 0);
    init_Anim(&d->altTextOpacity, 0);
    init_PtrArray(&d->visibleLinks);
    init_PtrArray(&d->visiblePre);
    init_PtrArray(&d->visibleWideRuns);
    init_Array(&d->wideRunOffsets, sizeof(int));
    init_PtrArray(&d->visibleMedia);
}

void deinit_DocumentView(iDocumentView *d) {
    delete_DrawBufs(d->drawBufs);
    delete_VisBuf(d->visBuf);
    free(d->visBufMeta);
    delete_PtrSet(d->invalidRuns);
    deinit_Array(&d->wideRunOffsets);
    deinit_PtrArray(&d->visibleMedia);
    deinit_PtrArray(&d->visibleWideRuns);
    deinit_PtrArray(&d->visiblePre);
    deinit_PtrArray(&d->visibleLinks);
    iReleasePtr(&d->doc);
}

static void setOwner_DocumentView_(iDocumentView *d, iDocumentWidget *doc) {
    d->owner = doc;
    init_SmoothScroll(&d->scrollY, as_Widget(doc), scrollBegan_DocumentWidget_);
    if (deviceType_App() != desktop_AppDeviceType) {
        d->scrollY.flags |= pullDownAction_SmoothScrollFlag; /* pull to refresh */
    }
}

static void resetWideRuns_DocumentView_(iDocumentView *d) {
    clear_Array(&d->wideRunOffsets);
    d->animWideRunId = 0;
    init_Anim(&d->animWideRunOffset, 0);
    iZap(d->animWideRunRange);
}

static int documentWidth_DocumentView_(const iDocumentView *d) {
    const iWidget *w        = constAs_Widget(d->owner);
    const iRect    bounds   = bounds_Widget(w);
    const iPrefs * prefs    = prefs_App();
    const int      minWidth = 50 * gap_UI * aspect_UI; /* lines must fit a word at least */
    const float    adjust   = iClamp((float) bounds.size.x / gap_UI / 11 - 12,
                                -1.0f, 10.0f); /* adapt to width */
    //printf("%f\n", adjust); fflush(stdout);
    int prefsWidth = prefs->lineWidth;
    if (isTerminal_Platform()) {
        prefsWidth /= aspect_UI * 0.8f;
    }
    return iMini(iMax(minWidth, bounds.size.x - gap_UI * (d->pageMargin + adjust) * 2),
                 fontSize_UI * prefsWidth * prefs->zoomPercent / 100);
}

static int documentTopPad_DocumentView_(const iDocumentView *d) {
    /* Amount of space between banner and top of the document. */
    return isEmpty_Banner(d->owner->banner) ? 0 : lineHeight_Text(paragraph_FontId);
}

static int documentTopMargin_DocumentView_(const iDocumentView *d) {
    return (isEmpty_Banner(d->owner->banner) ? d->pageMargin * gap_UI : height_Banner(d->owner->banner)) +
           documentTopPad_DocumentView_(d);
}

static int pageHeight_DocumentView_(const iDocumentView *d) {
    return height_Banner(d->owner->banner) + documentTopPad_DocumentView_(d) + size_GmDocument(d->doc).y;
}

static iRect documentBounds_DocumentView_(const iDocumentView *d) {
    const iRect bounds = bounds_Widget(constAs_Widget(d->owner));
    const int   margin = gap_UI * d->pageMargin;
    iRect       rect;
    rect.size.x = documentWidth_DocumentView_(d);
    rect.pos.x  = mid_Rect(bounds).x - rect.size.x / 2;
    rect.pos.y  = top_Rect(bounds) + margin;
    rect.size.y = height_Rect(bounds) - margin;
    iBool wasCentered = iFalse;
    /* TODO: Further separation of View and Widget: configure header and footer heights
       without involving the widget here. */
    if (d->owner->flags & centerVertically_DocumentWidgetFlag) {
        const int docSize = size_GmDocument(d->doc).y +
                            documentTopMargin_DocumentView_(d);
        if (size_GmDocument(d->doc).y == 0) {
            /* Document is empty; maybe just showing an error banner. */
            rect.pos.y = top_Rect(bounds) + height_Rect(bounds) / 2 -
                         documentTopPad_DocumentView_(d) - height_Banner(d->owner->banner) / 2;
            rect.size.y = 0;
            wasCentered = iTrue;
        }
        else if (docSize < rect.size.y - footerHeight_DocumentWidget_(d->owner)) {
            /* TODO: Phone toolbar? */
            /* Center vertically when the document is short. */
            const int relMidY   = (height_Rect(bounds) - footerHeight_DocumentWidget_(d->owner)) / 2;
            const int visHeight = size_GmDocument(d->doc).y;
            const int offset    = -height_Banner(d->owner->banner) - documentTopPad_DocumentView_(d);
            rect.pos.y  = top_Rect(bounds) + iMaxi(0, relMidY - visHeight / 2 + offset);
            rect.size.y = size_GmDocument(d->doc).y + documentTopMargin_DocumentView_(d);
            wasCentered = iTrue;
        }
    }
    if (!wasCentered) {
        /* The banner overtakes the top margin. */
        if (!isEmpty_Banner(d->owner->banner)) {
            rect.pos.y -= margin;
        }
        else {
            rect.size.y -= margin;
        }
    }
    return rect;
}

static int viewPos_DocumentView_(const iDocumentView *d) {
    return height_Banner(d->owner->banner) + documentTopPad_DocumentView_(d) -
           pos_SmoothScroll(&d->scrollY);
}

static iInt2 documentPos_DocumentView_(const iDocumentView *d, iInt2 pos) {
    return addY_I2(sub_I2(pos, topLeft_Rect(documentBounds_DocumentView_(d))),
                   -viewPos_DocumentView_(d));
}

static iRangei visibleRange_DocumentView_(const iDocumentView *d) {
    int top = pos_SmoothScroll(&d->scrollY) - height_Banner(d->owner->banner) -
              documentTopPad_DocumentView_(d);
    if (isEmpty_Banner(d->owner->banner)) {
        /* Top padding is not collapsed. */
        top -= d->pageMargin * gap_UI;
    }
    return (iRangei){ top, top + height_Rect(bounds_Widget(constAs_Widget(d->owner))) };
}

static void addVisible_DocumentView_(void *context, const iGmRun *run) {
    iDocumentView *d = context;
    if (~run->flags & decoration_GmRunFlag && !run->mediaId) {
        if (!d->visibleRuns.start) {
            d->visibleRuns.start = run;
        }
        d->visibleRuns.end = run;
    }
    if (preId_GmRun(run)) {
        pushBack_PtrArray(&d->visiblePre, run);
        if (run->flags & wide_GmRunFlag) {
            pushBack_PtrArray(&d->visibleWideRuns, run);
        }
    }
    /* Image runs are static so they're drawn as part of the content. */
    if (isMedia_GmRun(run) && run->mediaType != image_MediaType) {
        iAssert(run->mediaId);
        pushBack_PtrArray(&d->visibleMedia, run);
    }
    if (run->linkId) {
        pushBack_PtrArray(&d->visibleLinks, run);
    }
}

static const iGmRun *lastVisibleLink_DocumentView_(const iDocumentView *d) {
    iReverseConstForEach(PtrArray, i, &d->visibleLinks) {
        const iGmRun *run = i.ptr;
        if (run->flags & decoration_GmRunFlag && run->linkId) {
            return run;
        }
    }
    return NULL;
}

static float normScrollPos_DocumentView_(const iDocumentView *d) {
    const int docSize = pageHeight_DocumentView_(d);
    if (docSize) {
        float pos = pos_SmoothScroll(&d->scrollY) / (float) docSize;
        return iMax(pos, 0.0f);
    }
    return 0;
}

static int scrollMax_DocumentView_(const iDocumentView *d) {
    const iWidget *w = constAs_Widget(d->owner);
    int sm = pageHeight_DocumentView_(d) +
             (isEmpty_Banner(d->owner->banner) ? 2 : 1) * d->pageMargin * gap_UI + /* top and bottom margins */
             footerHeight_DocumentWidget_(d->owner) - height_Rect(bounds_Widget(w));
    return sm;
}

static void invalidateLink_DocumentView_(iDocumentView *d, iGmLinkId id) {
    /* A link has multiple runs associated with it. */
    iConstForEach(PtrArray, i, &d->visibleLinks) {
        const iGmRun *run = i.ptr;
        if (run->linkId == id) {
            insert_PtrSet(d->invalidRuns, run);
        }
    }
}

static void invalidateVisibleLinks_DocumentView_(iDocumentView *d) {
    iConstForEach(PtrArray, i, &d->visibleLinks) {
        const iGmRun *run = i.ptr;
        if (run->linkId) {
            insert_PtrSet(d->invalidRuns, run);
        }
    }
}

static int runOffset_DocumentView_(const iDocumentView *d, const iGmRun *run) {
    if (preId_GmRun(run) && run->flags & wide_GmRunFlag) {
        if (d->animWideRunId == preId_GmRun(run)) {
            return -value_Anim(&d->animWideRunOffset);
        }
        const size_t numOffsets = size_Array(&d->wideRunOffsets);
        const int *offsets = constData_Array(&d->wideRunOffsets);
        if (preId_GmRun(run) <= numOffsets) {
            return -offsets[preId_GmRun(run) - 1];
        }
    }
    return 0;
}

static void invalidateWideRunsWithNonzeroOffset_DocumentView_(iDocumentView *d) {
    iConstForEach(PtrArray, i, &d->visibleWideRuns) {
        const iGmRun *run = i.ptr;
        if (runOffset_DocumentView_(d, run)) {
            insert_PtrSet(d->invalidRuns, run);
        }
    }
}

static void updateHoverLinkInfo_DocumentView_(iDocumentView *d) {
    if (update_LinkInfo(d->owner->linkInfo,
                        d->doc,
                        d->hoverLink ? d->hoverLink->linkId : 0,
                        width_Widget(constAs_Widget(d->owner)))) {
        animate_DocumentWidget_(d->owner);
    }    
}

static void updateHover_DocumentView_(iDocumentView *d, iInt2 mouse) {
    const iWidget *w            = constAs_Widget(d->owner);
    const iRect    docBounds    = documentBounds_DocumentView_(d);
    const iGmRun * oldHoverLink = d->hoverLink;
    d->hoverPre          = NULL;
    d->hoverLink         = NULL;
    const iInt2 hoverPos = addY_I2(sub_I2(mouse, topLeft_Rect(docBounds)),
                                   -viewPos_DocumentView_(d));
    if (isHoverAllowed_DocumentWidget_(d->owner)) {
        iConstForEach(PtrArray, i, &d->visibleLinks) {
            const iGmRun *run = i.ptr;
            /* Click targets are slightly expanded so there are no gaps between links. */
            if (contains_Rect(expanded_Rect(run->bounds, init1_I2(gap_Text / 2)), hoverPos)) {
                d->hoverLink = run;
                break;
            }
        }
    }
    if (d->hoverLink != oldHoverLink) {
        if (oldHoverLink) {
            invalidateLink_DocumentView_(d, oldHoverLink->linkId);
        }
        if (d->hoverLink) {
            invalidateLink_DocumentView_(d, d->hoverLink->linkId);
        }
        updateHoverLinkInfo_DocumentView_(d);
        refresh_Widget(w);
    }
    /* Hovering over preformatted blocks. */
    if (isHoverAllowed_DocumentWidget_(d->owner)) {
        iConstForEach(PtrArray, j, &d->visiblePre) {
            const iGmRun *run = j.ptr;
            if (contains_Rect(run->bounds, hoverPos)) {
                d->hoverPre    = run;
                d->hoverAltPre = run;
                break;
            }
        }
    }
    if (!d->hoverPre) {
        setValueSpeed_Anim(&d->altTextOpacity, 0.0f, 1.5f);
        if (!isFinished_Anim(&d->altTextOpacity)) {
            animate_DocumentWidget_(d->owner);
        }
    }
    else if (d->hoverPre &&
             preHasAltText_GmDocument(d->doc, preId_GmRun(d->hoverPre)) &&
             ~d->owner->flags & noHoverWhileScrolling_DocumentWidgetFlag) {
        setValueSpeed_Anim(&d->altTextOpacity, 1.0f, 1.5f);
        if (!isFinished_Anim(&d->altTextOpacity)) {
            animate_DocumentWidget_(d->owner);
        }
    }
    if (isHover_Widget(w) && !contains_Widget(constAs_Widget(d->owner->scroll), mouse)) {
        setCursor_Window(get_Window(),
                         d->hoverLink || d->hoverPre ? SDL_SYSTEM_CURSOR_HAND
                                                     : SDL_SYSTEM_CURSOR_IBEAM);
        if (d->hoverLink &&
            linkFlags_GmDocument(d->doc, d->hoverLink->linkId) & permanent_GmLinkFlag) {
            setCursor_Window(get_Window(), SDL_SYSTEM_CURSOR_ARROW); /* not dismissable */
        }
    }
}

static void updateSideOpacity_DocumentView_(iDocumentView *d, iBool isAnimated) {
    float opacity = 0.0f;
    if (!isEmpty_Banner(d->owner->banner) &&
        height_Banner(d->owner->banner) < pos_SmoothScroll(&d->scrollY)) {
        opacity = 1.0f;
    }
    setValue_Anim(&d->sideOpacity, opacity, isAnimated ? (opacity < 0.5f ? 100 : 200) : 0);
    animate_DocumentWidget_(d->owner);
}

static iRangecc currentHeading_DocumentView_(const iDocumentView *d) {
    iRangecc heading = iNullRange;
    if (d->visibleRuns.start) {
        iConstForEach(Array, i, headings_GmDocument(d->doc)) {
            const iGmHeading *head = i.value;
            if (head->level == 0) {
                if (head->text.start <= d->visibleRuns.start->text.start) {
                    heading = head->text;
                }
                if (d->visibleRuns.end && head->text.start > d->visibleRuns.end->text.start) {
                    break;
                }
            }
        }
    }
    return heading;
}

static int updateScrollMax_DocumentView_(iDocumentView *d) {
    arrange_Widget(d->owner->footerButtons); /* scrollMax depends on footer height */
    const int scrollMax = scrollMax_DocumentView_(d);
    setMax_SmoothScroll(&d->scrollY, scrollMax);
    return scrollMax;
}

static void updateVisible_DocumentView_(iDocumentView *d) {
    /* TODO: The concerns of Widget and View are too tangled together here. */
    iChangeFlags(d->owner->flags,
                 centerVertically_DocumentWidgetFlag,
                 prefs_App()->centerShortDocs || startsWithCase_String(d->owner->mod.url, "about:") ||
                     !isSuccess_GmStatusCode(d->owner->sourceStatus));
    iScrollWidget *scrollBar = d->owner->scroll;
    const iRangei visRange  = visibleRange_DocumentView_(d);
    //    printf("visRange: %d...%d\n", visRange.start, visRange.end);
    const iRect   bounds    = bounds_Widget(as_Widget(d->owner));
    const int     scrollMax = updateScrollMax_DocumentView_(d);
    /* Reposition the footer buttons as appropriate. */
    setRange_ScrollWidget(scrollBar, (iRangei){ 0, scrollMax });
    const int docSize = pageHeight_DocumentView_(d) + footerHeight_DocumentWidget_(d->owner);
    const float scrollPos = pos_SmoothScroll(&d->scrollY);
    setThumb_ScrollWidget(scrollBar,
                          pos_SmoothScroll(&d->scrollY),
                          docSize > 0 ? height_Rect(bounds) * size_Range(&visRange) / docSize : 0);
    if (d->owner->footerButtons) {
        const iRect bounds    = bounds_Widget(as_Widget(d->owner));
        const iRect docBounds = documentBounds_DocumentView_(d);
        const int   hPad      = (width_Rect(bounds) - iMin(120 * gap_UI, width_Rect(docBounds))) / 2;
        const int   vPad      = 3 * gap_UI;
        setPadding_Widget(d->owner->footerButtons, hPad, 0, hPad, vPad);
        d->owner->footerButtons->rect.pos.y = height_Rect(bounds) -
                                              footerHeight_DocumentWidget_(d->owner) +
                                              (scrollMax > 0 ? scrollMax - scrollPos : 0);
    }
    clear_PtrArray(&d->visibleLinks);
    clear_PtrArray(&d->visibleWideRuns);
    clear_PtrArray(&d->visiblePre);
    clear_PtrArray(&d->visibleMedia);
    const iRangecc oldHeading = currentHeading_DocumentView_(d);
    /* Scan for visible runs. */ {
        iZap(d->visibleRuns);
        render_GmDocument(d->doc, visRange, addVisible_DocumentView_, d);
    }
    const iRangecc newHeading = currentHeading_DocumentView_(d);
    if (memcmp(&oldHeading, &newHeading, sizeof(oldHeading))) {
        d->drawBufs->flags |= updateSideBuf_DrawBufsFlag;
    }
    updateHover_DocumentView_(d, mouseCoord_Window(get_Window(), 0));
    updateSideOpacity_DocumentView_(d, iTrue);
    animateMedia_DocumentWidget_(d->owner);
    /* Remember scroll positions of recently visited pages. */ {
        iRecentUrl *recent = mostRecentUrl_History(d->owner->mod.history);
        if (recent && docSize && d->owner->state == ready_RequestState &&
            equal_String(&recent->url, d->owner->mod.url)) {
            recent->normScrollY = normScrollPos_DocumentView_(d);
        }
    }
    /* After scrolling/resizing stops, begin pre-rendering the visbuf contents. */ {
        removeTicker_App(prerender_DocumentWidget_, d->owner);
        remove_Periodic(periodic_App(), d);
        if (~d->owner->widget.flags & destroyPending_WidgetFlag) {
            add_Periodic(periodic_App(), d->owner, "document.render");
        }
    }
}

static void swap_DocumentView_(iDocumentView *d, iDocumentView *swapBuffersWith) {
    d->scrollY        = swapBuffersWith->scrollY;
    d->scrollY.widget = as_Widget(d->owner);
    iSwap(iVisBuf *,     d->visBuf,     swapBuffersWith->visBuf);
    iSwap(iVisBufMeta *, d->visBufMeta, swapBuffersWith->visBufMeta);
    iSwap(iDrawBufs *,   d->drawBufs,   swapBuffersWith->drawBufs);
    updateVisible_DocumentView_(d);
    updateVisible_DocumentView_(swapBuffersWith);
}

static void updateTimestampBuf_DocumentView_(const iDocumentView *d) {
    if (!isExposed_Window(get_Window())) {
        return;
    }
    if (d->drawBufs->timestampBuf) {
        delete_TextBuf(d->drawBufs->timestampBuf);
        d->drawBufs->timestampBuf = NULL;
    }
    if (isValid_Time(&d->owner->sourceTime)) {
        iString *fmt = timeFormatHourPreference_Lang("page.timestamp");
        d->drawBufs->timestampBuf = newRange_TextBuf(
            uiLabel_FontId,
            white_ColorId,
            range_String(collect_String(format_Time(&d->owner->sourceTime, cstr_String(fmt)))));
        delete_String(fmt);
    }
    d->drawBufs->flags &= ~updateTimestampBuf_DrawBufsFlag;
}

static void invalidate_DocumentView_(iDocumentView *d) {
    invalidate_VisBuf(d->visBuf);
    clear_PtrSet(d->invalidRuns);
}

static void documentRunsInvalidated_DocumentView_(iDocumentView *d) {
    d->hoverPre    = NULL;
    d->hoverAltPre = NULL;
    d->hoverLink   = NULL;
    clear_PtrArray(&d->visibleMedia);
    iZap(d->visibleRuns);
    iZap(d->renderRuns);
}

static void resetScroll_DocumentView_(iDocumentView *d) {
    reset_SmoothScroll(&d->scrollY);
    init_Anim(&d->sideOpacity, 0);
    init_Anim(&d->altTextOpacity, 0);
    resetWideRuns_DocumentView_(d);
}

static void updateWidth_DocumentView_(iDocumentView *d) {
    updateWidth_GmDocument(d->doc, documentWidth_DocumentView_(d), width_Widget(d->owner));
}

static void updateWidthAndRedoLayout_DocumentView_(iDocumentView *d) {
    setWidth_GmDocument(d->doc, documentWidth_DocumentView_(d), width_Widget(d->owner));
}

static void clampScroll_DocumentView_(iDocumentView *d) {
    move_SmoothScroll(&d->scrollY, 0);
}

static void immediateScroll_DocumentView_(iDocumentView *d, int offset) {
    move_SmoothScroll(&d->scrollY, offset);
}

static void smoothScroll_DocumentView_(iDocumentView *d, int offset, int duration) {
    moveSpan_SmoothScroll(&d->scrollY, offset, duration);
}

static void scrollTo_DocumentView_(iDocumentView *d, int documentY, iBool centered) {
    if (!isEmpty_Banner(d->owner->banner)) {
        documentY += height_Banner(d->owner->banner) + documentTopPad_DocumentView_(d);
    }
    else {
        documentY += documentTopPad_DocumentView_(d) + d->pageMargin * gap_UI;
    }
    init_Anim(&d->scrollY.pos,
              documentY - (centered ? documentBounds_DocumentView_(d).size.y / 2
                                    : lineHeight_Text(paragraph_FontId)));
    clampScroll_DocumentView_(d);
}

static void scrollToHeading_DocumentView_(iDocumentView *d, const char *heading) {
    iConstForEach(Array, h, headings_GmDocument(d->doc)) {
        const iGmHeading *head = h.value;
        if (startsWithCase_Rangecc(head->text, heading)) {
            postCommandf_Root(as_Widget(d->owner)->root, "document.goto loc:%p", head->text.start);
            break;
        }
    }
}

static iBool scrollWideBlock_DocumentView_(iDocumentView *d, iInt2 mousePos, int delta,
                                           int duration) {
    if (delta == 0 || d->owner->flags & eitherWheelSwipe_DocumentWidgetFlag) {
        return iFalse;
    }
    const iInt2 docPos = documentPos_DocumentView_(d, mousePos);
    iConstForEach(PtrArray, i, &d->visibleWideRuns) {
        const iGmRun *run = i.ptr;
        if (docPos.y >= top_Rect(run->bounds) && docPos.y <= bottom_Rect(run->bounds)) {
            /* We can scroll this run. First find out how much is allowed. */
            const iGmRunRange range = findPreformattedRange_GmDocument(d->doc, run);
            int maxWidth = 0;
            for (const iGmRun *r = range.start; r != range.end; r++) {
                maxWidth = iMax(maxWidth, width_Rect(r->visBounds));
            }
            const int maxOffset = maxWidth - documentWidth_DocumentView_(d) + d->pageMargin * gap_UI;
            if (size_Array(&d->wideRunOffsets) <= preId_GmRun(run)) {
                resize_Array(&d->wideRunOffsets, preId_GmRun(run) + 1);
            }
            int *offset = at_Array(&d->wideRunOffsets, preId_GmRun(run) - 1);
            const int oldOffset = *offset;
            *offset = iClamp(*offset + delta, 0, maxOffset);
            /* Make sure the whole block gets redraw. */
            if (oldOffset != *offset) {
                for (const iGmRun *r = range.start; r != range.end; r++) {
                    insert_PtrSet(d->invalidRuns, r);
                }
                refresh_Widget(d->owner);
                d->owner->selectMark = iNullRange;
                d->owner->foundMark  = iNullRange;
            }
            if (duration) {
                if (d->animWideRunId != preId_GmRun(run) || isFinished_Anim(&d->animWideRunOffset)) {
                    d->animWideRunId = preId_GmRun(run);
                    init_Anim(&d->animWideRunOffset, oldOffset);
                }
                setValueEased_Anim(&d->animWideRunOffset, *offset, duration);
                d->animWideRunRange = range;
                addTicker_App(refreshWhileScrolling_DocumentWidget_, d->owner);
            }
            else {
                d->animWideRunId = 0;
                init_Anim(&d->animWideRunOffset, 0);
            }
            return iTrue;
        }
    }
    return iFalse;
}

static iRangecc sourceLoc_DocumentView_(const iDocumentView *d, iInt2 pos) {
    return findLoc_GmDocument(d->doc, documentPos_DocumentView_(d, pos));
}

iDeclareType(MiddleRunParams)

struct Impl_MiddleRunParams {
    int midY;
    const iGmRun *closest;
    int distance;
};

static void find_MiddleRunParams_(void *params, const iGmRun *run) {
    iMiddleRunParams *d = params;
    if (isEmpty_Rect(run->bounds)) {
        return;
    }
    const int distance = iAbs(mid_Rect(run->bounds).y - d->midY);
    if (!d->closest || distance < d->distance) {
        d->closest  = run;
        d->distance = distance;
    }
}

static const iGmRun *middleRun_DocumentView_(const iDocumentView *d) {
    iRangei visRange = visibleRange_DocumentView_(d);
    iMiddleRunParams params = { (visRange.start + visRange.end) / 2, NULL, 0 };
    render_GmDocument(d->doc, visRange, find_MiddleRunParams_, &params);
    return params.closest;
}

static void allocVisBuffer_DocumentView_(const iDocumentView *d) {
    const iWidget *w         = constAs_Widget(d->owner);
    const iBool    isVisible = isVisible_Widget(w);
    const iInt2    size      = bounds_Widget(w).size;
    if (isVisible) {
        alloc_VisBuf(d->visBuf, size, 1);
    }
    else {
        dealloc_VisBuf(d->visBuf);
    }
}

static size_t visibleLinkOrdinal_DocumentView_(const iDocumentView *d, iGmLinkId linkId) {
    size_t ord = 0;
    const iRangei visRange = visibleRange_DocumentView_(d);
    iConstForEach(PtrArray, i, &d->visibleLinks) {
        const iGmRun *run = i.ptr;
        if (top_Rect(run->visBounds) >= visRange.start + gap_UI * d->pageMargin * 4 / 5) {
            if (run->flags & decoration_GmRunFlag && run->linkId) {
                if (run->linkId == linkId) return ord;
                ord++;
            }
        }
    }
    return iInvalidPos;
}

static void documentRunsInvalidated_DocumentWidget_(iDocumentWidget *d) {
    d->foundMark     = iNullRange;
    d->selectMark    = iNullRange;
    d->contextLink   = NULL;
    documentRunsInvalidated_DocumentView_(&d->view);
}

static iBool updateDocumentWidthRetainingScrollPosition_DocumentView_(iDocumentView *d,
                                                                      iBool keepCenter) {
    const int newWidth = documentWidth_DocumentView_(d);
    if (newWidth == size_GmDocument(d->doc).x && !keepCenter /* not a font change */) {
        return iFalse;
    }
    /* Font changes (i.e., zooming) will keep the view centered, otherwise keep the top
       of the visible area fixed. */
    const iGmRun *run     = keepCenter ? middleRun_DocumentView_(d) : d->visibleRuns.start;
    const char *  runLoc  = (run ? run->text.start : NULL);
    int           voffset = 0;
    if (!keepCenter && run) {
        /* Keep the first visible run visible at the same position. */
        /* TODO: First *fully* visible run? */
        voffset = visibleRange_DocumentView_(d).start - top_Rect(run->visBounds);
    }
    setWidth_GmDocument(d->doc, newWidth, width_Widget(d->owner));
    setWidth_Banner(d->owner->banner, newWidth);
    documentRunsInvalidated_DocumentWidget_(d->owner);
    if (runLoc && !keepCenter) {
        run = findRunAtLoc_GmDocument(d->doc, runLoc);
        if (run) {
            scrollTo_DocumentView_(
                d, top_Rect(run->visBounds) + lineHeight_Text(paragraph_FontId) + voffset, iFalse);
        }
    }
    else if (runLoc && keepCenter) {
        run = findRunAtLoc_GmDocument(d->doc, runLoc);
        if (run) {
            scrollTo_DocumentView_(d, mid_Rect(run->bounds).y, iTrue);
        }
    }
    return iTrue;
}

static iRect runRect_DocumentView_(const iDocumentView *d, const iGmRun *run) {
    const iRect docBounds = documentBounds_DocumentView_(d);
    return moved_Rect(run->bounds, addY_I2(topLeft_Rect(docBounds), viewPos_DocumentView_(d)));
}

iDeclareType(DrawContext)

struct Impl_DrawContext {
    const iDocumentView *view;
    iRect widgetBounds;
    iRect docBounds;
    iRangei vis;
    iInt2 viewPos; /* document area origin */
    iPaint paint;
    iBool inSelectMark;
    iBool inFoundMark;
    iBool showLinkNumbers;
    iRect firstMarkRect;
    iRect lastMarkRect;
    iGmRunRange runsDrawn;
};

static int measureAdvanceToLoc_(const iGmRun *run, const char *end) {
    iWrapText wt = { .text     = run->text,
                     .mode     = word_WrapTextMode,
                     .maxWidth = iAbsi(drawBoundWidth_GmRun(run)),
                     .justify  = isJustified_GmRun(run),
                     .hitChar  = end };
    measure_WrapText(&wt, run->font);
    return wt.hitAdvance_out.x;
}

static void fillRange_DrawContext_(iDrawContext *d, const iGmRun *run, enum iColorId color,
                                   iRangecc mark, iBool *isInside) {
    if (mark.start > mark.end) {
        /* Selection may be done in either direction. */
        iSwap(const char *, mark.start, mark.end);
    }
    if (*isInside || (contains_Range(&run->text, mark.start) ||
                      contains_Range(&mark, run->text.start))) {
        int x = 0;
        if (!*isInside) {
            x = measureAdvanceToLoc_(run, iMax(run->text.start, mark.start));
        }
        const int boundWidth = iAbsi(drawBoundWidth_GmRun(run));
        int w = boundWidth - x;
        if (contains_Range(&run->text, mark.end) || mark.end < run->text.start) {
            iRangecc mk =
                !*isInside ? mark : (iRangecc){ run->text.start, iMax(run->text.start, mark.end) };
            mk.start  = iMax(mk.start, run->text.start);
            int x1    = measureAdvanceToLoc_(run, mk.start);
            w         = measureAdvanceToLoc_(run, mk.end) - x1;
            *isInside = iFalse;
        }
        else {
            *isInside = iTrue; /* at least until the next run */
        }
        if (w > boundWidth - x) {
            w = boundWidth - x;
        }        
        if (~run->flags & decoration_GmRunFlag) {
            const iInt2 visPos =
                add_I2(run->bounds.pos, addY_I2(d->viewPos, viewPos_DocumentView_(d->view)));
            const iRect rangeRect = { addX_I2(visPos, x), init_I2(w, height_Rect(run->bounds)) };
            if (rangeRect.size.x) {
                fillRect_Paint(&d->paint, rangeRect, color);
                /* Keep track of the first and last marked rects. */
                if (d->firstMarkRect.size.x == 0) {
                    d->firstMarkRect = rangeRect;
                }
                d->lastMarkRect = rangeRect;
            }
        }
    }
    /* Link URLs are not part of the visible document, so they are ignored above. Handle
       these ranges as a special case. */
    if (run->linkId && run->flags & decoration_GmRunFlag) {
        const iRangecc url = linkUrlRange_GmDocument(d->view->doc, run->linkId);
        if (contains_Range(&url, mark.start) &&
            (contains_Range(&url, mark.end) || url.end == mark.end)) {
            fillRect_Paint(
                &d->paint,
                moved_Rect(run->visBounds, addY_I2(d->viewPos, viewPos_DocumentView_(d->view))),
                color);
        }
    }
}

static void drawMark_DrawContext_(void *context, const iGmRun *run) {
    iDrawContext *d = context;
    if (!isMedia_GmRun(run)) {
        fillRange_DrawContext_(d, run, uiMatching_ColorId, d->view->owner->foundMark, &d->inFoundMark);
        fillRange_DrawContext_(d, run, uiMarked_ColorId, d->view->owner->selectMark, &d->inSelectMark);
    }
}

static void drawRun_DrawContext_(void *context, const iGmRun *run) {
    iDrawContext *d      = context;
    const iInt2   origin = d->viewPos;
    /* Keep track of the drawn visible runs. */ {
        if (!d->runsDrawn.start || run < d->runsDrawn.start) {
            d->runsDrawn.start = run;
        }
        if (!d->runsDrawn.end || run > d->runsDrawn.end) {
            d->runsDrawn.end = run;
        }
    }
    if (run->mediaType == image_MediaType) {
        SDL_Texture *tex = imageTexture_Media(media_GmDocument(d->view->doc), mediaId_GmRun(run));
        const iRect dst = moved_Rect(run->visBounds, origin);
        if (tex) {
            fillRect_Paint(&d->paint, dst, tmBackground_ColorId); /* in case the image has alpha */
            SDL_RenderCopy(d->paint.dst->render, tex, NULL,
                           &(SDL_Rect){ dst.pos.x, dst.pos.y, dst.size.x, dst.size.y });
        }
        else {
            drawRect_Paint(&d->paint, dst, tmQuoteIcon_ColorId);
            drawCentered_Text(uiLabel_FontId,
                              dst,
                              iFalse,
                              tmQuote_ColorId,
                              explosion_Icon "  Error Loading Image");
        }
        return;
    }
    else if (isMedia_GmRun(run)) {
        /* Media UIs are drawn afterwards as a dynamic overlay. */
        return;
    }
    enum iColorId      fg        = run->color;
    const iGmDocument *doc       = d->view->doc;
    const int          linkFlags = linkFlags_GmDocument(doc, run->linkId);
    /* Hover state of a link. */
    const iBool isPartOfHover = (run->linkId && d->view->hoverLink &&
                                 run->linkId == d->view->hoverLink->linkId);
    iBool isHover = (isPartOfHover && ~run->flags & decoration_GmRunFlag);
    /* Visible (scrolled) position of the run. */
    const iInt2 visPos = addX_I2(add_I2(run->visBounds.pos, origin),
                                 /* Preformatted runs can be scrolled. */
                                 runOffset_DocumentView_(d->view, run));
    const iRect visRect = { visPos, run->visBounds.size };
    /* Fill the background. */ {
#if 0
        iBool isInlineImageCaption = run->linkId && linkFlags & content_GmLinkFlag &&
                                     ~linkFlags & permanent_GmLinkFlag;
        if (run->flags & decoration_GmRunFlag && ~run->flags & startOfLine_GmRunFlag) {
            /* This is the metadata. */
            isInlineImageCaption = iFalse;
        }
#endif
        iBool isMobileHover = deviceType_App() != desktop_AppDeviceType &&
            (isPartOfHover || contains_PtrSet(d->view->invalidRuns, run)) &&
            (~run->flags & decoration_GmRunFlag || run->flags & startOfLine_GmRunFlag
             /* highlight link icon but not image captions */);
        /* While this is consistent, it's a bit excessive to indicate that an inlined image
           is open: the image itself is the indication. */
        const iBool isInlineImageCaption = iFalse;
        if (run->linkId && (linkFlags & isOpen_GmLinkFlag || isInlineImageCaption || isMobileHover)) {
            /* Open links get a highlighted background. */
            int       bg       = tmBackgroundOpenLink_ColorId;
            if (isMobileHover && !isPartOfHover) {
                bg = tmBackground_ColorId; /* hover ended and was invalidated */
            }
//            const int frame    = tmFrameOpenLink_ColorId;
            const int pad      = gap_Text;
            iRect     wideRect = { init_I2(origin.x - pad, visPos.y),
                               init_I2(d->docBounds.size.x + 2 * pad,
                                       height_Rect(run->visBounds)) };
            adjustEdges_Rect(&wideRect,
                             run->flags & startOfLine_GmRunFlag ? -pad * 3 / 4 : 0, 0,
                             run->flags & endOfLine_GmRunFlag ? pad * 3 / 4 : 0, 0);
            /* The first line is composed of two runs that may be drawn in either order, so
               only draw half of the background. */
            if (run->flags & decoration_GmRunFlag) {
                wideRect.size.x = right_Rect(visRect) - left_Rect(wideRect);
            }
            else if (run->flags & startOfLine_GmRunFlag) {
                wideRect.size.x = right_Rect(wideRect) - left_Rect(visRect);
                wideRect.pos.x  = left_Rect(visRect);
            }
            fillRect_Paint(&d->paint, wideRect, bg);
        }
        else {
            /* Normal background for other runs. There are cases when runs get drawn multiple times,
               e.g., at the buffer boundary, and there are slightly overlapping characters in
               monospace blocks. Clearing the background here ensures a cleaner visual appearance
               since only one glyph is visible at any given point. */
            fillRect_Paint(&d->paint, visRect, tmBackground_ColorId);
        }
    }
    if (run->linkId) {
        if (run->flags & decoration_GmRunFlag && run->flags & startOfLine_GmRunFlag) {
            /* Link icon. */
            if (linkFlags & content_GmLinkFlag) {
                fg = linkColor_GmDocument(doc, run->linkId, textHover_GmLinkPart);
            }
        }
        else if (~run->flags & decoration_GmRunFlag) {
            fg = linkColor_GmDocument(doc, run->linkId, isHover ? textHover_GmLinkPart : text_GmLinkPart);
            if (linkFlags & content_GmLinkFlag) {
                fg = linkColor_GmDocument(doc, run->linkId, textHover_GmLinkPart); /* link is inactive */
            }
        }
    }
    if (run->flags & altText_GmRunFlag) {
        const iInt2 margin = preRunMargin_GmDocument(doc, preId_GmRun(run));
        fillRect_Paint(&d->paint, (iRect){ visPos, run->visBounds.size }, tmBackgroundAltText_ColorId);
        drawRect_Paint(&d->paint, (iRect){ visPos, run->visBounds.size }, tmFrameAltText_ColorId);
        drawWrapRange_Text(run->font,
                           add_I2(visPos, margin),
                           run->visBounds.size.x - 2 * margin.x,
                           run->color,
                           run->text);
    }
    else {
        if (d->showLinkNumbers && run->linkId && run->flags & decoration_GmRunFlag &&
            ~run->flags & caption_GmRunFlag) {
            const size_t ord = visibleLinkOrdinal_DocumentView_(d->view, run->linkId);
            if (ord >= d->view->owner->ordinalBase) {
                const iChar ordChar =
                    linkOrdinalChar_DocumentWidget_(d->view->owner, ord - d->view->owner->ordinalBase);
                if (ordChar) {
                    const char *circle = "\u25ef"; /* Large Circle */
                    const int   circleFont = FONT_ID(default_FontId, regular_FontStyle, contentRegular_FontSize);
                    iRect nbArea = { init_I2(d->viewPos.x - gap_UI / 3, visPos.y),
                                     init_I2(3.95f * gap_Text, 1.0f * lineHeight_Text(circleFont)) };
                    if (isTerminal_Platform()) {
                        nbArea.pos.x += 1;
                    }
                    drawRange_Text(
                        circleFont, topLeft_Rect(nbArea), tmQuote_ColorId, range_CStr(circle));
                    iRect circleArea = visualBounds_Text(circleFont, range_CStr(circle));
                    addv_I2(&circleArea.pos, topLeft_Rect(nbArea));
                    drawCentered_Text(FONT_ID(default_FontId, regular_FontStyle, contentSmall_FontSize),
                                      circleArea,
                                      iTrue,
                                      tmQuote_ColorId,
                                      "%lc",
                                      (int) ordChar);
                    goto runDrawn;
                }
            }
        }
        if (run->flags & ruler_GmRunFlag) {
            if (height_Rect(run->visBounds) > 0 &&
                height_Rect(run->visBounds) <= width_Rect(run->visBounds)) {
                /* This is used for block quotes. */
                drawVLine_Paint(&d->paint,
                                addX_I2(visPos,
                                        !run->isRTL
                                            ? -gap_Text * 5 / 2
                                            : (width_Rect(run->visBounds) + gap_Text * 5 / 2)),
                                height_Rect(run->visBounds),
                                tmQuoteIcon_ColorId);
            }
            else {
                drawHLine_Paint(&d->paint, visPos, width_Rect(run->visBounds), tmQuoteIcon_ColorId);
            }
        }
        /* Base attributes. */ {
            int f, c;
            runBaseAttributes_GmDocument(doc, run, &f, &c);
            setBaseAttributes_Text(f, c);
        }
        /* Fancy date in Gemini feed links. */ {
            if (run->linkId && run->flags & startOfLine_GmRunFlag && ~run->flags & decoration_GmRunFlag) {
                static iRegExp *datePattern_;
                if (!datePattern_) {
                    datePattern_ = new_RegExp("^[12][0-9][0-9][0-9]-[01][0-9]-[0-3][0-9]\\s", 0);
                }
                iRegExpMatch m;
                init_RegExpMatch(&m);
                if (matchRange_RegExp(datePattern_, run->text, &m)) {
                    /* The date uses regular weight and a dimmed color. */
                    iString styled;
                    initRange_String(&styled, run->text);
                    insertData_Block(&styled.chars, 10, "\x1b[0m", 4); /* restore */
                    iBlock buf;
                    init_Block(&buf, 0);
                    appendCStr_Block(&buf, "\x1b[10m"); /* regular font weight */
                    appendCStr_Block(&buf, escape_Color(isHover ? fg : tmLinkFeedEntryDate_ColorId));
                    insertData_Block(&styled.chars, 0, constData_Block(&buf), size_Block(&buf));
                    deinit_Block(&buf);
                    const int oldAnsi = ansiFlags_Text();
                    setAnsiFlags_Text(oldAnsi | allowFontStyle_AnsiFlag);
                    setBaseAttributes_Text(run->font, fg);
                    drawBoundRange_Text(run->font,
                                        visPos,
                                        drawBoundWidth_GmRun(run),
                                        isJustified_GmRun(run),
                                        fg,
                                        range_String(&styled));
                    setAnsiFlags_Text(oldAnsi);
                    deinit_String(&styled);
                    goto runDrawn;
                }
            }
        }
        drawBoundRange_Text(run->font,
                            visPos,
                            drawBoundWidth_GmRun(run),
                            isJustified_GmRun(run),
                            fg,
                            run->text);
    runDrawn:;
        setBaseAttributes_Text(-1, -1);
    }
    /* Presentation of links. */
    if (run->linkId && ~run->flags & decoration_GmRunFlag) {
        const int metaFont = paragraph_FontId;
        /* TODO: Show status of an ongoing media request. */
        const int flags = linkFlags;
        const iRect linkRect = moved_Rect(run->visBounds, origin);
        iMediaRequest *mr = NULL;
        /* Show metadata about inline content. */
        if (flags & content_GmLinkFlag && run->flags & endOfLine_GmRunFlag) {
            fg = linkColor_GmDocument(doc, run->linkId, textHover_GmLinkPart);
            iString text;
            init_String(&text);
            const iMediaId linkMedia = findMediaForLink_Media(constMedia_GmDocument(doc),
                                                              run->linkId, none_MediaType);
            iAssert(linkMedia.type != none_MediaType);
            iGmMediaInfo info;
            info_Media(constMedia_GmDocument(doc), linkMedia, &info);
            switch (linkMedia.type) {
                case image_MediaType: {
                    /* There's a separate decorative GmRun for the metadata. */
                    break;
                }
                case audio_MediaType:
                    format_String(&text, "%s", info.type);
                    break;
                case download_MediaType:
                    format_String(&text, "%s", info.type);
                    break;
                default:
                    break;
            }
            if (linkMedia.type != download_MediaType && /* can't cancel downloads currently */
                                                            linkMedia.type != image_MediaType &&
                findMediaRequest_DocumentWidget_(d->view->owner, run->linkId)) {
                appendFormat_String(
                    &text, "  %s" close_Icon, isHover ? escape_Color(tmLinkText_ColorId) : "");
            }
            const iInt2 size = measureRange_Text(metaFont, range_String(&text)).bounds.size;
            if (size.x) {
                fillRect_Paint(
                    &d->paint,
                    (iRect){ add_I2(origin, addX_I2(topRight_Rect(run->bounds), -size.x - gap_UI)),
                             addX_I2(size, 2 * gap_UI) },
                    tmBackground_ColorId);
                drawAlign_Text(metaFont,
                               add_I2(topRight_Rect(run->bounds), origin),
                               fg,
                               right_Alignment,
                               "%s", cstr_String(&text));
            }
            deinit_String(&text);
        }
        else if (run->flags & endOfLine_GmRunFlag &&
                 (mr = findMediaRequest_DocumentWidget_(d->view->owner, run->linkId)) != NULL) {
            if (!isFinished_GmRequest(mr->req)) {
                draw_Text(metaFont,
                          topRight_Rect(linkRect),
                          tmInlineContentMetadata_ColorId,
                          translateCStr_Lang(" \u2014 ${doc.fetching}\u2026 (%.1f ${mb})"),
                          (float) bodySize_GmRequest(mr->req) / 1.0e6f);
            }
        }
    }
    if (0) {
        drawRect_Paint(&d->paint, (iRect){ visPos, run->bounds.size }, green_ColorId);
        drawRect_Paint(&d->paint, (iRect){ visPos, run->visBounds.size }, red_ColorId);
    }
}

static int drawSideRect_(iPaint *p, iRect rect) {
    int bg = tmBannerBackground_ColorId;
    int fg = tmBannerIcon_ColorId;
    if (equal_Color(get_Color(bg), get_Color(tmBackground_ColorId))) {
        bg = tmBannerIcon_ColorId;
        fg = tmBannerBackground_ColorId;
    }
    fillRect_Paint(p, rect, bg);
    return fg;
}

static int sideElementAvailWidth_DocumentView_(const iDocumentView *d) {
    return left_Rect(documentBounds_DocumentView_(d)) -
           left_Rect(bounds_Widget(constAs_Widget(d->owner))) - 2 * d->pageMargin * gap_UI;
}

iLocalDef int minBannerSize_(void) {
    return iMaxi(lineHeight_Text(banner_FontId) * 2, 5);
}

static iBool isSideHeadingVisible_DocumentView_(const iDocumentView *d) {
    return sideElementAvailWidth_DocumentView_(d) >= minBannerSize_() * 2.25f / aspect_UI;
}

static void updateSideIconBuf_DocumentView_(const iDocumentView *d) {
    if (!isExposed_Window(get_Window())) {
        return;
    }
    iDrawBufs *dbuf = d->drawBufs;
    dbuf->flags &= ~updateSideBuf_DrawBufsFlag;
    if (dbuf->sideIconBuf) {
        SDL_DestroyTexture(dbuf->sideIconBuf);
        dbuf->sideIconBuf = NULL;
    }
    //    const iGmRun *banner = siteBanner_GmDocument(d->doc);
    if (isEmpty_Banner(d->owner->banner)) {
        return;
    }
    const int   margin           = gap_UI * d->pageMargin;
    const int   minBannerSize    = minBannerSize_();
    const iChar icon             = siteIcon_GmDocument(d->doc);
    const int   avail            = sideElementAvailWidth_DocumentView_(d) - margin;
    iBool       isHeadingVisible = isSideHeadingVisible_DocumentView_(d);
    /* Determine the required size. */
    iInt2 bufSize = init_I2(minBannerSize / aspect_UI, minBannerSize);
    const int sideHeadingFont = FONT_ID(documentHeading_FontId, regular_FontStyle, contentBig_FontSize);
    if (isHeadingVisible) {
        const iInt2 headingSize = measureWrapRange_Text(sideHeadingFont, avail,
                                                        currentHeading_DocumentView_(d)).bounds.size;
        if (headingSize.x > 0) {
            bufSize.y += gap_Text + headingSize.y;
            bufSize.x = iMax(bufSize.x, headingSize.x);
        }
        else {
            isHeadingVisible = iFalse;
        }
    }
    SDL_Renderer *render = renderer_Window(get_Window());
    dbuf->sideIconBuf = SDL_CreateTexture(render,
                                          SDL_PIXELFORMAT_RGBA4444,
                                          SDL_TEXTUREACCESS_STATIC | SDL_TEXTUREACCESS_TARGET,
                                          bufSize.x, bufSize.y);
    iPaint p;
    init_Paint(&p);
    beginTarget_Paint(&p, dbuf->sideIconBuf);
    const iColor back = get_Color(tmBannerSideTitle_ColorId);
    SDL_SetRenderDrawColor(render, back.r, back.g, back.b, 0); /* better blending of the edge */
    SDL_RenderClear(render);
    const iRect iconRect = { zero_I2(), init_I2(minBannerSize / aspect_UI, minBannerSize) };
    int fg = drawSideRect_(&p, iconRect);
    iString str;
    initUnicodeN_String(&str, &icon, 1);
    drawCentered_Text(banner_FontId, iconRect, iTrue, fg, "%s", cstr_String(&str));
    deinit_String(&str);
    if (isHeadingVisible) {
        iRangecc    text = currentHeading_DocumentView_(d);
        iInt2       pos  = addY_I2(bottomLeft_Rect(iconRect), gap_Text);
        const int   font = sideHeadingFont;
        drawWrapRange_Text(font, pos, avail, tmBannerSideTitle_ColorId, text);
    }
    endTarget_Paint(&p);
    SDL_SetTextureBlendMode(dbuf->sideIconBuf, SDL_BLENDMODE_BLEND);
}

static void drawSideElements_DocumentView_(const iDocumentView *d) {
    const iWidget *w         = constAs_Widget(d->owner);
    const iRect    bounds    = bounds_Widget(w);
    const iRect    docBounds = documentBounds_DocumentView_(d);
    const int      margin    = gap_UI * d->pageMargin;
    float          opacity   = value_Anim(&d->sideOpacity);
    const int      avail     = left_Rect(docBounds) - left_Rect(bounds) - 2 * margin;
    iDrawBufs *    dbuf      = d->drawBufs;
    iPaint         p;
    init_Paint(&p);
    setClip_Paint(&p, boundsWithoutVisualOffset_Widget(w));
    /* Side icon and current heading. */
    if (prefs_App()->sideIcon && opacity > 0 && dbuf->sideIconBuf) {
        const iInt2 texSize = size_SDLTexture(dbuf->sideIconBuf);
        if (avail > texSize.x) {
            const int minBannerSize = lineHeight_Text(banner_FontId) * 2;
            iInt2 pos = addY_I2(add_I2(topLeft_Rect(bounds), init_I2(margin, 0)),
                                height_Rect(bounds) / 2 - minBannerSize / 2 -
                                    (texSize.y > minBannerSize
                                                       ? (gap_Text + lineHeight_Text(heading3_FontId)) / 2
                                                       : 0));
            SDL_SetTextureAlphaMod(dbuf->sideIconBuf, 255 * opacity);
            SDL_RenderCopy(renderer_Window(get_Window()),
                           dbuf->sideIconBuf, NULL,
                           &(SDL_Rect){ pos.x, pos.y, texSize.x, texSize.y });
        }
    }
    /* Reception timestamp. */
    if (dbuf->timestampBuf && dbuf->timestampBuf->size.x <= avail) {
        draw_TextBuf(
            dbuf->timestampBuf,
            add_I2(
                bottomLeft_Rect(bounds),
                init_I2(margin,
                        -margin + -dbuf->timestampBuf->size.y +
                            iMax(0, d->scrollY.max - pos_SmoothScroll(&d->scrollY)))),
            tmQuoteIcon_ColorId);
    }
    unsetClip_Paint(&p);
}

static void drawMedia_DocumentView_(const iDocumentView *d, iPaint *p) {
    iConstForEach(PtrArray, i, &d->visibleMedia) {
        const iGmRun * run = i.ptr;
        if (run->mediaType == audio_MediaType) {
            iPlayerUI ui;
            init_PlayerUI(&ui,
                          audioPlayer_Media(media_GmDocument(d->doc), mediaId_GmRun(run)),
                          runRect_DocumentView_(d, run));
            draw_PlayerUI(&ui, p);
        }
        else if (run->mediaType == download_MediaType) {
            iDownloadUI ui;
            init_DownloadUI(&ui, constMedia_GmDocument(d->doc), run->mediaId,
                            runRect_DocumentView_(d, run));
            draw_DownloadUI(&ui, p);
        }
    }
}

static void extend_GmRunRange_(iGmRunRange *runs) {
    if (runs->start) {
        runs->start--;
        runs->end++;
    }
}

static iBool render_DocumentView_(const iDocumentView *d, iDrawContext *ctx, iBool prerenderExtra) {
    iBool       didDraw = iFalse;
    const iRect bounds  = bounds_Widget(constAs_Widget(d->owner));
    const iRect ctxWidgetBounds =
        init_Rect(0,
                  0,
                  width_Rect(bounds) - constAs_Widget(d->owner->scroll)->rect.size.x,
                  height_Rect(bounds));
    const iRangei full          = { 0, size_GmDocument(d->doc).y };
    const iRangei vis           = ctx->vis;
    iVisBuf      *visBuf        = d->visBuf; /* will be updated now */
    d->drawBufs->lastRenderTime = SDL_GetTicks();
    /* Swap buffers around to have room available both before and after the visible region. */
    allocVisBuffer_DocumentView_(d);
    reposition_VisBuf(visBuf, vis);
    /* Redraw the invalid ranges. */
    if (~flags_Widget(constAs_Widget(d->owner)) & destroyPending_WidgetFlag) {
        iPaint *p = &ctx->paint;
        init_Paint(p);
        iForIndices(i, visBuf->buffers) {
            iVisBufTexture *buf         = &visBuf->buffers[i];
            iVisBufMeta    *meta        = buf->user;
            const iRangei   bufRange    = intersect_Rangei(bufferRange_VisBuf(visBuf, i), full);
            const iRangei   bufVisRange = intersect_Rangei(bufRange, vis);
            ctx->widgetBounds = moved_Rect(ctxWidgetBounds, init_I2(0, -buf->origin));
            ctx->viewPos      = init_I2(left_Rect(ctx->docBounds) - left_Rect(bounds), -buf->origin);
            //            printf("  buffer %zu: buf vis range %d...%d\n", i, bufVisRange.start, bufVisRange.end);
            if (!prerenderExtra && !isEmpty_Range(&bufVisRange)) {
                if (isEmpty_Rangei(buf->validRange)) {
                    /* Fill the required currently visible range (vis). */
                    const iRangei bufVisRange = intersect_Rangei(bufRange, vis);
                    if (!isEmpty_Range(&bufVisRange)) {
                        beginTarget_Paint(p, buf->texture);
                        fillRect_Paint(p, (iRect){ zero_I2(), visBuf->texSize }, tmBackground_ColorId);
                        iZap(ctx->runsDrawn);
                        render_GmDocument(d->doc, bufVisRange, drawRun_DrawContext_, ctx);
                        meta->runsDrawn = ctx->runsDrawn;
                        extend_GmRunRange_(&meta->runsDrawn);
                        buf->validRange = bufVisRange;
                        //                printf("  buffer %zu valid %d...%d\n", i, bufRange.start, bufRange.end);
                        didDraw = iTrue;
                    }
                }
                else {
                    /* Progressively fill the required runs. */
                    if (meta->runsDrawn.start && buf->validRange.start > bufRange.start) {
                        beginTarget_Paint(p, buf->texture);
                        iZap(ctx->runsDrawn);
                        const iGmRun *newStart = renderProgressive_GmDocument(d->doc,
                                                                              meta->runsDrawn.start,
                                                                              -1,
                                                                              iInvalidSize,
                                                                              bufVisRange,
                                                                              drawRun_DrawContext_,
                                                                              ctx);
                        if (ctx->runsDrawn.start) {
                            /* Something was actually drawn, so update the valid range. */
                            const int newTop = top_Rect(ctx->runsDrawn.start->visBounds);
                            if (newTop != buf->validRange.start) {
                                didDraw = iTrue;
//                                printf("render: valid:%d->%d run:%p->%p\n",
//                                       buf->validRange.start, newTop,
//                                       meta->runsDrawn.start,
//                                       ctx->runsDrawn.start); fflush(stdout);
                                buf->validRange.start = newTop;
                            }
                            meta->runsDrawn.start = newStart;
                        }
                    }
                    if (meta->runsDrawn.end) {
                        beginTarget_Paint(p, buf->texture);
                        iZap(ctx->runsDrawn);
                        meta->runsDrawn.end = renderProgressive_GmDocument(d->doc, meta->runsDrawn.end,
                                                                           +1, iInvalidSize,
                                                                           bufVisRange,
                                                                           drawRun_DrawContext_,
                                                                           ctx);
                        if (ctx->runsDrawn.start) {
                            didDraw = iTrue;
                        }
                        buf->validRange.end = bufVisRange.end;
                    }
                }
            }
            /* Progressively draw the rest of the buffer if it isn't fully valid. */
            if (prerenderExtra && !equal_Rangei(bufRange, buf->validRange)) {
                const iGmRun *next;
                //                printf("%zu: prerenderExtra (start:%p end:%p)\n", i, meta->runsDrawn.start, meta->runsDrawn.end);
                if (meta->runsDrawn.start == NULL) {
                    /* Haven't drawn anything yet in this buffer, so let's try seeding it. */
                    const int rh = lineHeight_Text(paragraph_FontId);
                    const int y = i >= iElemCount(visBuf->buffers) / 2 ? bufRange.start : (bufRange.end - rh);
                    beginTarget_Paint(p, buf->texture);
                    fillRect_Paint(p, (iRect){ zero_I2(), visBuf->texSize }, tmBackground_ColorId);
                    buf->validRange = (iRangei){ y, y + rh };
                    iZap(ctx->runsDrawn);
                    render_GmDocument(d->doc, buf->validRange, drawRun_DrawContext_, ctx);
                    meta->runsDrawn = ctx->runsDrawn;
                    extend_GmRunRange_(&meta->runsDrawn);
                    //                    printf("%zu: seeded, next %p:%p\n", i, meta->runsDrawn.start, meta->runsDrawn.end);
                    didDraw = iTrue;
                }
                else {
                    if (meta->runsDrawn.start) {
                        const iRangei upper = intersect_Rangei(bufRange, (iRangei){ full.start, buf->validRange.start });
                        if (upper.end > upper.start) {
                            beginTarget_Paint(p, buf->texture);
                            next = renderProgressive_GmDocument(d->doc, meta->runsDrawn.start,
                                                                -1, 1, upper,
                                                                drawRun_DrawContext_,
                                                                ctx);
                            if (next && meta->runsDrawn.start != next) {
                                meta->runsDrawn.start = next;
                                buf->validRange.start = bottom_Rect(next->visBounds);
                                didDraw = iTrue;
                            }
                            else {
                                buf->validRange.start = bufRange.start;
                            }
                        }
                    }
                    if (!didDraw && meta->runsDrawn.end) {
                        const iRangei lower = intersect_Rangei(bufRange, (iRangei){ buf->validRange.end, full.end });
                        if (lower.end > lower.start) {
                            beginTarget_Paint(p, buf->texture);
                            next = renderProgressive_GmDocument(d->doc, meta->runsDrawn.end,
                                                                +1, 1, lower,
                                                                drawRun_DrawContext_,
                                                                ctx);
                            if (next && meta->runsDrawn.end != next) {
                                meta->runsDrawn.end = next;
                                buf->validRange.end = top_Rect(next->visBounds);
                                didDraw = iTrue;
                            }
                            else {
                                buf->validRange.end = bufRange.end;
                            }
                        }
                    }
                }
            }
            /* Draw any invalidated runs that fall within this buffer. */
            if (!prerenderExtra) {
                const iRangei bufRange = { buf->origin, buf->origin + visBuf->texSize.y };
                /* Clear full-width backgrounds first in case there are any dynamic elements. */ {
                    iConstForEach(PtrSet, r, d->invalidRuns) {
                        const iGmRun *run = *r.value;
                        if (isOverlapping_Rangei(bufRange, ySpan_Rect(run->visBounds))) {
                            beginTarget_Paint(p, buf->texture);
                            fillRect_Paint(p,
                                           init_Rect(0,
                                                     run->visBounds.pos.y - buf->origin,
                                                     visBuf->texSize.x,
                                                     run->visBounds.size.y),
                                           tmBackground_ColorId);
                        }
                    }
                }
                setAnsiFlags_Text(ansiEscapes_GmDocument(d->doc));
                iConstForEach(PtrSet, r, d->invalidRuns) {
                    const iGmRun *run = *r.value;
                    if (isOverlapping_Rangei(bufRange, ySpan_Rect(run->visBounds))) {
                        beginTarget_Paint(p, buf->texture);
                        drawRun_DrawContext_(ctx, run);
                    }
                }
                setAnsiFlags_Text(allowAll_AnsiFlag);
            }
            endTarget_Paint(p);
            if (prerenderExtra && didDraw) {
                /* Just a run at a time. */
                break;
            }
        }
        if (!prerenderExtra) {
            clear_PtrSet(d->invalidRuns);
        }
    }
    return didDraw;
}

static void draw_DocumentView_(const iDocumentView *d) {
    const iWidget *w                   = constAs_Widget(d->owner);
    const iRect    bounds              = bounds_Widget(w);
    const iRect    boundsWithoutVisOff = boundsWithoutVisualOffset_Widget(w);
    const iRect    clipBounds          = intersect_Rect(bounds, boundsWithoutVisOff);
    /* Each document has its own palette, but the drawing routines rely on a global one.
       As we're now drawing a document, ensure that the right palette is in effect.
       Document theme colors can be used elsewhere, too, but first a document's palette
       must be made global. */
    makePaletteGlobal_GmDocument(d->doc);
    if (d->drawBufs->flags & updateTimestampBuf_DrawBufsFlag) {
        updateTimestampBuf_DocumentView_(d);
    }
    if (d->drawBufs->flags & updateSideBuf_DrawBufsFlag) {
        updateSideIconBuf_DocumentView_(d);
    }
    const iRect   docBounds = documentBounds_DocumentView_(d);
    const iRangei vis       = visibleRange_DocumentView_(d);
    iDrawContext  ctx       = {
                                .view            = d,
                                .docBounds       = docBounds,
                                .vis             = vis,
                                .showLinkNumbers = (d->owner->flags & showLinkNumbers_DocumentWidgetFlag) != 0,
                         };
    init_Paint(&ctx.paint);
    render_DocumentView_(d, &ctx, iFalse /* just the mandatory parts */);
    iBanner    *banner           = d->owner->banner;
    int         yTop             = docBounds.pos.y + viewPos_DocumentView_(d);
    const iBool isDocEmpty       = size_GmDocument(d->doc).y == 0;
    const iBool isTouchSelecting = (flags_Widget(w) & touchDrag_WidgetFlag) != 0;
    if (!isDocEmpty || !isEmpty_Banner(banner)) {
        const int docBgColor = isDocEmpty ? tmBannerBackground_ColorId : tmBackground_ColorId;
        setClip_Paint(&ctx.paint, clipBounds);
        if (!isDocEmpty) {
            draw_VisBuf(d->visBuf, init_I2(bounds.pos.x, yTop), ySpan_Rect(bounds));
        }
        /* Text markers. */
        if (!isEmpty_Range(&d->owner->foundMark) || !isEmpty_Range(&d->owner->selectMark)) {
            SDL_Renderer *render = renderer_Window(get_Window());
            ctx.firstMarkRect = zero_Rect();
            ctx.lastMarkRect = zero_Rect();
            SDL_SetRenderDrawBlendMode(render,
                                       isDark_ColorTheme(colorTheme_App()) ? SDL_BLENDMODE_ADD
                                                                           : SDL_BLENDMODE_BLEND);
            ctx.viewPos = topLeft_Rect(docBounds);
            /* Marker starting outside the visible range? */
            if (d->visibleRuns.start) {
                if (!isEmpty_Range(&d->owner->selectMark) &&
                    d->owner->selectMark.start < d->visibleRuns.start->text.start &&
                    d->owner->selectMark.end > d->visibleRuns.start->text.start) {
                    ctx.inSelectMark = iTrue;
                }
                if (isEmpty_Range(&d->owner->foundMark) &&
                    d->owner->foundMark.start < d->visibleRuns.start->text.start &&
                    d->owner->foundMark.end > d->visibleRuns.start->text.start) {
                    ctx.inFoundMark = iTrue;
                }
            }
            render_GmDocument(d->doc, vis, drawMark_DrawContext_, &ctx);
            SDL_SetRenderDrawBlendMode(render, SDL_BLENDMODE_NONE);
            /* Selection range pins. */
            if (isTouchSelecting) {
                drawPin_Paint(&ctx.paint, ctx.firstMarkRect, 0, tmQuote_ColorId);
                drawPin_Paint(&ctx.paint, ctx.lastMarkRect,  1, tmQuote_ColorId);
            }
        }
        drawMedia_DocumentView_(d, &ctx.paint);
        /* Fill the top and bottom, in case the document is short. */
        if (yTop > top_Rect(bounds)) {
            fillRect_Paint(&ctx.paint,
                           (iRect){ bounds.pos, init_I2(bounds.size.x, yTop - top_Rect(bounds)) },
                           !isEmpty_Banner(banner) ? tmBannerBackground_ColorId
                                                   : docBgColor);
        }
        /* Banner. */
        if (!isDocEmpty || numItems_Banner(banner) > 0) {
            /* Fill the part between the banner and the top of the document. */
            if (documentTopPad_DocumentView_(d) > 0) {
                fillRect_Paint(&ctx.paint,
                               (iRect){ init_I2(left_Rect(bounds),
                                                top_Rect(docBounds) + viewPos_DocumentView_(d) -
                                                    documentTopPad_DocumentView_(d)),
                                        init_I2(bounds.size.x, documentTopPad_DocumentView_(d)) },
                               docBgColor);
            }
            setPos_Banner(banner, addY_I2(topLeft_Rect(docBounds),
                                          floorf(-pos_SmoothScroll(&d->scrollY))));
            draw_Banner(banner);
        }
        const int yBottom = yTop + size_GmDocument(d->doc).y;
        if (yBottom < bottom_Rect(bounds)) {
            fillRect_Paint(&ctx.paint,
                           init_Rect(bounds.pos.x, yBottom, bounds.size.x, bottom_Rect(bounds) - yBottom),
                           !isDocEmpty ? docBgColor : tmBannerBackground_ColorId);
        }
        unsetClip_Paint(&ctx.paint);
        drawSideElements_DocumentView_(d);
        /* Alt text. */
        const float altTextOpacity = value_Anim(&d->altTextOpacity) * 6 - 5;
        if (d->hoverAltPre && altTextOpacity > 0) {
            const iGmPreMeta *meta = preMeta_GmDocument(d->doc, preId_GmRun(d->hoverAltPre));
            if (meta->flags & topLeft_GmPreMetaFlag && ~meta->flags & decoration_GmRunFlag &&
                !isEmpty_Range(&meta->altText)) {
                const int   margin   = 3 * gap_UI / 2;
                const int   altFont  = uiLabel_FontId;
                const int   wrap     = docBounds.size.x - 2 * margin;
                iInt2 pos            = addY_I2(add_I2(docBounds.pos, meta->pixelRect.pos),
                                    viewPos_DocumentView_(d));
                const iInt2 textSize = measureWrapRange_Text(altFont, wrap, meta->altText).bounds.size;
                pos.y -= textSize.y + gap_UI;
                pos.y               = iMax(pos.y, top_Rect(bounds));
                const iRect altRect = { pos, init_I2(docBounds.size.x, textSize.y) };
                ctx.paint.alpha     = altTextOpacity * 255;
                if (altTextOpacity < 1) {
                    SDL_SetRenderDrawBlendMode(renderer_Window(get_Window()), SDL_BLENDMODE_BLEND);
                }
                fillRect_Paint(&ctx.paint, altRect, tmBackgroundAltText_ColorId);
                drawRect_Paint(&ctx.paint, altRect, tmFrameAltText_ColorId);
                setOpacity_Text(altTextOpacity);
                drawWrapRange_Text(altFont, addX_I2(pos, margin), wrap,
                                   tmQuote_ColorId, meta->altText);
                SDL_SetRenderDrawBlendMode(renderer_Window(get_Window()), SDL_BLENDMODE_NONE);
                setOpacity_Text(1.0f);
            }
        }
        /* Touch selection indicator. */
        if (isTouchSelecting) {
            iRect rect = { topLeft_Rect(bounds),
                           init_I2(width_Rect(bounds), lineHeight_Text(uiLabelBold_FontId)) };
            fillRect_Paint(&ctx.paint, rect, uiTextAction_ColorId);
            const iRangecc mark = selectMark_DocumentWidget_(d->owner);
            drawCentered_Text(uiLabelBold_FontId,
                              rect,
                              iFalse,
                              uiBackground_ColorId,
                              "%zu bytes selected", /* TODO: i18n */
                              size_Range(&mark));
        }
    }
}

/*----------------------------------------------------------------------------------------------*/

static void enableActions_DocumentWidget_(iDocumentWidget *d, iBool enable) {
    /* Actions are invisible child widgets of the DocumentWidget. */
    iForEach(ObjectList, i, children_Widget(d)) {
        if (isAction_Widget(i.object)) {
            setFlags_Widget(i.object, disabled_WidgetFlag, !enable);
        }
    }
}

static void setLinkNumberMode_DocumentWidget_(iDocumentWidget *d, iBool set) {
    if (((d->flags & showLinkNumbers_DocumentWidgetFlag) != 0) != set) {
        iChangeFlags(d->flags, showLinkNumbers_DocumentWidgetFlag, set);
        /* Children have priority when handling events. */
        enableActions_DocumentWidget_(d, !set);
#if defined (iPlatformAppleDesktop)
        enableMenuItemsOnHomeRow_MacOS(!set);
#endif
        /* Ensure all keyboard events come here first. */
        setKeyboardGrab_Widget(set ? as_Widget(d) : NULL);
        if (d->menu) {
            setFlags_Widget(d->menu, disabled_WidgetFlag, set);
        }
    }
}

static void requestUpdated_DocumentWidget_(iAnyObject *obj) {
    iDocumentWidget *d = obj;
    const int wasUpdated = exchange_Atomic(&d->isRequestUpdated, iTrue);
    if (!wasUpdated) {
        postCommand_Widget(obj,
                           "document.request.updated doc:%p reqid:%u request:%p",
                           d,
                           id_GmRequest(d->request),
                           d->request);
    }
}

static void requestFinished_DocumentWidget_(iAnyObject *obj) {
    iDocumentWidget *d = obj;
    postCommand_Widget(obj,
                       "document.request.finished doc:%p reqid:%u request:%p",
                       d,
                       id_GmRequest(d->request),
                       d->request);
}

static void animate_DocumentWidget_(void *ticker) {
    iDocumentWidget *d = ticker;
    iAssert(isInstance_Object(d, &Class_DocumentWidget));
    refresh_Widget(d);
    if (!isFinished_Anim(&d->view.sideOpacity) || !isFinished_Anim(&d->view.altTextOpacity) ||
        (d->linkInfo && !isFinished_Anim(&d->linkInfo->opacity))) {
        addTicker_App(animate_DocumentWidget_, d);
    }
}

static uint32_t mediaUpdateInterval_DocumentWidget_(const iDocumentWidget *d) {
    if (document_App() != d) {
        return 0;
    }
    if (as_MainWindow(window_Widget(d))->isDrawFrozen) {
        return 0;
    }
    static const uint32_t invalidInterval_ = ~0u;
    uint32_t interval = invalidInterval_;
    iConstForEach(PtrArray, i, &d->view.visibleMedia) {
        const iGmRun *run = i.ptr;
        if (run->mediaType == audio_MediaType) {
#if defined (LAGRANGE_ENABLE_AUDIO)
            iPlayer *plr = audioPlayer_Media(media_GmDocument(d->view.doc), mediaId_GmRun(run));
            if (flags_Player(plr) & adjustingVolume_PlayerFlag ||
                (isStarted_Player(plr) && !isPaused_Player(plr))) {
                interval = iMin(interval, 1000 / 15);
            }
#endif
        }
        else if (run->mediaType == download_MediaType) {
            interval = iMin(interval, 1000);
        }
    }
    return interval != invalidInterval_ ? interval : 0;
}

static uint32_t postMediaUpdate_DocumentWidget_(uint32_t interval, void *context) {
    /* Called in timer thread; don't access the widget. */
    iUnused(context);
    postCommand_App("media.player.update");
    return interval;
}

static void updateMedia_DocumentWidget_(iDocumentWidget *d) {
    if (document_App() == d) {
        refresh_Widget(d);
        iConstForEach(PtrArray, i, &d->view.visibleMedia) {
            const iGmRun *run = i.ptr;
            if (run->mediaType == audio_MediaType) {
#if defined (LAGRANGE_ENABLE_AUDIO)
                iPlayer *plr = audioPlayer_Media(media_GmDocument(d->view.doc), mediaId_GmRun(run));
                if (idleTimeMs_Player(plr) > 3000 && ~flags_Player(plr) & volumeGrabbed_PlayerFlag &&
                    flags_Player(plr) & adjustingVolume_PlayerFlag) {
                    setFlags_Player(plr, adjustingVolume_PlayerFlag, iFalse);
                }
#endif
            }
        }
    }
    if (d->mediaTimer && mediaUpdateInterval_DocumentWidget_(d) == 0) {
        SDL_RemoveTimer(d->mediaTimer);
        d->mediaTimer = 0;
    }
}

static void animateMedia_DocumentWidget_(iDocumentWidget *d) {
    if (document_App() != d) {
        if (d->mediaTimer) {
            SDL_RemoveTimer(d->mediaTimer);
            d->mediaTimer = 0;
        }
        return;
    }
    uint32_t interval = mediaUpdateInterval_DocumentWidget_(d);
    if (interval && !d->mediaTimer) {
        d->mediaTimer = SDL_AddTimer(interval, postMediaUpdate_DocumentWidget_, d);
    }
}

static void updateWindowTitle_DocumentWidget_(const iDocumentWidget *d) {
    iLabelWidget *tabButton = tabPageButton_Widget(findChild_Widget(root_Widget(constAs_Widget(d)),
                                                                    "doctabs"), d);
    if (!tabButton) {
        /* Not part of the UI at the moment. */
        return;
    }
    iStringArray *title = iClob(new_StringArray());
    if (!isEmpty_String(title_GmDocument(d->view.doc))) {
        pushBack_StringArray(title, title_GmDocument(d->view.doc));
    }
    if (!isEmpty_String(d->titleUser)) {
        pushBack_StringArray(title, d->titleUser);
    }
    else {
        iUrl parts;
        init_Url(&parts, d->mod.url);
        if (equalCase_Rangecc(parts.scheme, "about")) {
            if (!findWidget_App("winbar")) {
                pushBackCStr_StringArray(title, "Lagrange");
            }
        }
        else if (!isEmpty_Range(&parts.host)) {
            pushBackRange_StringArray(title, parts.host);
        }
        else if (!isEmpty_Range(&parts.path)) {
            iRangecc name = baseNameSep_Path(collectNewRange_String(parts.path), "/");
            if (!isEmpty_Range(&name)) {
                pushBack_StringArray(
                    title, collect_String(urlDecode_String(collectNewRange_String(name))));
            }
        }
    }
    if (isEmpty_StringArray(title)) {
        pushBackCStr_StringArray(title, "Lagrange");
    }
    /* Remove redundant parts. */ {
        for (size_t i = 0; i < size_StringArray(title) - 1; i++) {
            if (equal_String(at_StringArray(title, i), at_StringArray(title, i + 1))) {
                remove_StringArray(title, i + 1);
            }
        }   
    }    
    /* Take away parts if it doesn't fit. */
    const int avail     = bounds_Widget(as_Widget(tabButton)).size.x - 7 * gap_UI;
    iBool     setWindow = (document_App() == d && isUnderKeyRoot_Widget(d));
    const int font      = uiLabel_FontId;
    for (;;) {
        iString *text = collect_String(joinCStr_StringArray(title, " \u2014 "));
        if (setWindow) {
            /* Longest version for the window title, and omit the icon. */
            setTitle_MainWindow(get_MainWindow(), text);
            setWindow = iFalse;
        }
        const iChar siteIcon = siteIcon_GmDocument(d->view.doc);
        if (siteIcon) {
            if (!isEmpty_String(text)) {
                prependCStr_String(text, "  " restore_ColorEscape);
            }
            prependChar_String(text, siteIcon);
            prependCStr_String(text, escape_Color(uiIcon_ColorId));
        }
        const int width = measureRange_Text(font, range_String(text)).advance.x;
        const int ellipsisWidth = measure_Text(font, "...").advance.x;
        setTextColor_LabelWidget(tabButton, none_ColorId);
        iWidget *tabCloseButton = child_Widget(as_Widget(tabButton), 0);
        const iBool tabCloseVisible = avail > width_Widget(tabCloseButton);
        if (deviceType_App() == tablet_AppDeviceType) {
            iChangeFlags(as_Widget(tabCloseButton)->flags2, visibleOnParentSelected_WidgetFlag2,
                         tabCloseVisible);
        }
        else {
            setFlags_Widget(tabCloseButton, visibleOnParentHover_WidgetFlag,
                            tabCloseVisible);
        }
        if (width <= avail || isEmpty_StringArray(title)) {
            updateText_LabelWidget(tabButton, text);
            break;
        }
        if (size_StringArray(title) == 1) {
            /* Just truncate to fit. */
            if (siteIcon && avail <= 4 * ellipsisWidth) {
                updateText_LabelWidget(tabButton, collect_String(newUnicodeN_String(&siteIcon, 1)));
                setTextColor_LabelWidget(tabButton, uiIcon_ColorId);
                break;
            }
            const char *endPos;
            tryAdvanceNoWrap_Text(font, range_String(text), avail - ellipsisWidth, &endPos);
            updateText_LabelWidget(
                tabButton,
                collectNewFormat_String(
                    "%s...", cstr_Rangecc((iRangecc){ constBegin_String(text), endPos })));
            break;
        }
        remove_StringArray(title, size_StringArray(title) - 1);
    }
}

static void invalidate_DocumentWidget_(iDocumentWidget *d) {
    if (flags_Widget(as_Widget(d)) & destroyPending_WidgetFlag) {
        return;
    }
    if (d->flags & invalidationPending_DocumentWidgetFlag) {
        return;
    }
    if (isAffectedByVisualOffset_Widget(as_Widget(d))) {
        d->flags |= invalidationPending_DocumentWidgetFlag;
        return;
    }
    d->flags &= ~invalidationPending_DocumentWidgetFlag;
    invalidate_DocumentView_(&d->view);
//    printf("[%p] '%s' invalidated\n", d, cstr_String(id_Widget(as_Widget(d))));
}

static iRangecc siteText_DocumentWidget_(const iDocumentWidget *d) {
    return isEmpty_String(d->titleUser) ? urlHost_String(d->mod.url)
                                        : range_String(d->titleUser);
}

static iBool isPinned_DocumentWidget_(const iDocumentWidget *d) {
    if (deviceType_App() == phone_AppDeviceType) {
        return iFalse;
    }
    if (d->flags & otherRootByDefault_DocumentWidgetFlag) {
        return iTrue;
    }
    const iWidget *w = constAs_Widget(d);
    const iWindow *win = get_Window();
    if (numRoots_Window(win) == 1) {
        return iFalse;
    }
    const iPrefs *prefs = prefs_App();
    return (prefs->pinSplit == 1 && w->root == win->roots[0]) ||
           (prefs->pinSplit == 2 && w->root == win->roots[1]);
}

static void showOrHideIndicators_DocumentWidget_(iDocumentWidget *d) {
    iWidget *w = as_Widget(d);
    if (d != document_Root(w->root)) {
        return;
    }
    iWidget *navBar = findChild_Widget(root_Widget(w), "navbar");
    showCollapsed_Widget(findChild_Widget(navBar, "document.pinned"),
                         isPinned_DocumentWidget_(d));
    const iBool isBookmarked = findUrl_Bookmarks(bookmarks_App(), d->mod.url) != 0;
    iLabelWidget *bmPin = findChild_Widget(navBar, "document.bookmarked");
    setOutline_LabelWidget(bmPin, !isBookmarked);
    setTextColor_LabelWidget(bmPin, isBookmarked ? uiTextAction_ColorId : uiText_ColorId);
}

static void updateBanner_DocumentWidget_(iDocumentWidget *d) {
    setSite_Banner(d->banner, siteText_DocumentWidget_(d), siteIcon_GmDocument(d->view.doc));
}

static void documentWasChanged_DocumentWidget_(iDocumentWidget *d) {
    iChangeFlags(d->flags, selecting_DocumentWidgetFlag, iFalse);
    setFlags_Widget(as_Widget(d), touchDrag_WidgetFlag, iFalse);
    d->requestLinkId = 0;
    updateVisitedLinks_GmDocument(d->view.doc);
    documentRunsInvalidated_DocumentWidget_(d);
    updateWindowTitle_DocumentWidget_(d);
    updateBanner_DocumentWidget_(d);
    updateVisible_DocumentView_(&d->view);
    d->view.drawBufs->flags |= updateSideBuf_DrawBufsFlag;
    invalidate_DocumentWidget_(d);
    refresh_Widget(as_Widget(d));
    /* Check for special bookmark tags. */
    d->flags &= ~otherRootByDefault_DocumentWidgetFlag;
    const uint16_t bmid = findUrl_Bookmarks(bookmarks_App(), d->mod.url);
    if (bmid) {
        const iBookmark *bm = get_Bookmarks(bookmarks_App(), bmid);
        if (bm->flags & linkSplit_BookmarkFlag) {
            d->flags |= otherRootByDefault_DocumentWidgetFlag;
        }
    }
    showOrHideIndicators_DocumentWidget_(d);
    if (~d->flags & fromCache_DocumentWidgetFlag) {
        setCachedDocument_History(d->mod.history, d->view.doc /* keeps a ref */);
    }
}

static void replaceDocument_DocumentWidget_(iDocumentWidget *d, iGmDocument *newDoc) {
    pauseAllPlayers_Media(media_GmDocument(d->view.doc), iTrue);
    iRelease(d->view.doc);
    d->view.doc = ref_Object(newDoc);
    documentWasChanged_DocumentWidget_(d);
}

static void updateTheme_DocumentWidget_(iDocumentWidget *d) {
    if (document_App() != d || category_GmStatusCode(d->sourceStatus) == categoryInput_GmStatusCode) {
        return;
    }
    d->view.drawBufs->flags |= updateTimestampBuf_DrawBufsFlag;
    updateBanner_DocumentWidget_(d);
}

static void makeFooterButtons_DocumentWidget_(iDocumentWidget *d, const iMenuItem *items, size_t count) {
    iWidget *w = as_Widget(d);
    destroy_Widget(d->footerButtons);
    d->footerButtons = NULL;
    if (count == 0) {
        return;
    }
    d->footerButtons = new_Widget();
    setFlags_Widget(d->footerButtons,
                    unhittable_WidgetFlag | arrangeVertical_WidgetFlag |
                        resizeWidthOfChildren_WidgetFlag | arrangeHeight_WidgetFlag |
                        fixedPosition_WidgetFlag | resizeToParentWidth_WidgetFlag,
                    iTrue);
    for (size_t i = 0; i < count; ++i) {
        iLabelWidget *button = addChildFlags_Widget(
            d->footerButtons,
            iClob(newKeyMods_LabelWidget(
                items[i].label, items[i].key, items[i].kmods, items[i].command)),
            alignLeft_WidgetFlag | drawKey_WidgetFlag | extraPadding_WidgetFlag);
        setPadding1_Widget(as_Widget(button), gap_UI / 2);
        checkIcon_LabelWidget(button);
        setFont_LabelWidget(button, uiContent_FontId);
        setBackgroundColor_Widget(as_Widget(button), uiBackgroundSidebar_ColorId);
    }
    addChild_Widget(as_Widget(d), iClob(d->footerButtons));
    arrange_Widget(d->footerButtons);
    arrange_Widget(w);
    updateVisible_DocumentView_(&d->view); /* final placement for the buttons */
}

static void showErrorPage_DocumentWidget_(iDocumentWidget *d, enum iGmStatusCode code,
                                          const iString *meta) {
    iString        *src = collectNew_String();
    const iGmError *msg = get_GmError(code);
    destroy_Widget(d->footerButtons);
    d->footerButtons = NULL;
    const iString *serverErrorMsg = NULL;
    if (meta) {
        switch (code) {
            case schemeChangeRedirect_GmStatusCode:
            case tooManyRedirects_GmStatusCode:
                appendFormat_String(src, "=> %s\n", cstr_String(meta));
                break;
            case tlsServerCertificateExpired_GmStatusCode:
                makeFooterButtons_DocumentWidget_(
                    d,
                    (iMenuItem[]){ { rightArrowhead_Icon " ${menu.unexpire}",
                                       SDLK_RETURN, 0, "server.unexpire"
                                   },
                                   { info_Icon " ${menu.pageinfo}",
                                     SDLK_i,
                                     KMOD_PRIMARY,
                                     "document.info" } },
                    2);
                break;
            case tlsServerCertificateNotVerified_GmStatusCode:
                makeFooterButtons_DocumentWidget_(
                    d,
                    (iMenuItem[]){ { info_Icon " ${menu.pageinfo}",
                                     SDLK_i,
                                     KMOD_PRIMARY,
                                     "document.info" } },
                    1);
                break;
            case failedToOpenFile_GmStatusCode:
            case certificateNotValid_GmStatusCode:
//                appendFormat_String(src, "%s", cstr_String(meta));
                break;
            case unsupportedMimeType_GmStatusCode: {
                iString *key = collectNew_String();
                toString_Sym(SDLK_s, KMOD_PRIMARY, key);
//                appendFormat_String(src, "\n```\n%s\n```\n", cstr_String(meta));
                const char *mtype = mediaTypeFromFileExtension_String(d->mod.url);
                iArray items;
                init_Array(&items, sizeof(iMenuItem));
                if (iCmpStr(mtype, "application/octet-stream")) {
                    pushBack_Array(
                        &items,
                        &(iMenuItem){ translateCStr_Lang(format_CStr("View as \"%s\"", mtype)),
                                      SDLK_RETURN,
                                      0,
                                      format_CStr("document.setmediatype mime:%s", mtype) });
                }
                pushBack_Array(&items,
                               &(iMenuItem){ export_Icon " ${menu.open.external}",
                                             SDLK_RETURN,
                                             KMOD_PRIMARY,
                                             "document.save extview:1" });
                pushBack_Array(
                    &items,
                    &(iMenuItem){ translateCStr_Lang(download_Icon " " saveToDownloads_Label),
                                  0,
                                  0,
                                  "document.save" });
                makeFooterButtons_DocumentWidget_(d, data_Array(&items), size_Array(&items));
                deinit_Array(&items);
                serverErrorMsg = collectNewFormat_String("%s (%s)", msg->title, cstr_String(meta));
                break;
            }
            default:
                if (!isEmpty_String(meta)) {
                    serverErrorMsg = meta;
                }
                break;
        }
    }
    if (category_GmStatusCode(code) == categoryClientCertificate_GmStatus) {
        makeFooterButtons_DocumentWidget_(
            d,
            (iMenuItem[]){
                { leftHalf_Icon " ${menu.show.identities}",
                  '4',
                  KMOD_PRIMARY,
                  deviceType_App() == desktop_AppDeviceType ? "sidebar.mode arg:3 show:1"
                                                            : "preferences idents:1" },
                { person_Icon " ${menu.identity.new}", newIdentity_KeyShortcut, "ident.new" } },
            2);
    }
    /* Make a new document for the error page.*/
    iGmDocument *errorDoc = new_GmDocument();
    setWidth_GmDocument(errorDoc, documentWidth_DocumentView_(&d->view), width_Widget(d));
    setUrl_GmDocument(errorDoc, d->mod.url);
    setFormat_GmDocument(errorDoc, gemini_SourceFormat);
    replaceDocument_DocumentWidget_(d, errorDoc);
    iRelease(errorDoc);
    clear_Banner(d->banner);
    add_Banner(d->banner, error_BannerType, code, serverErrorMsg, NULL);
    d->state = ready_RequestState;
    setSource_DocumentWidget(d, src);
    updateTheme_DocumentWidget_(d);
    resetScroll_DocumentView_(&d->view);
}

static void updateFetchProgress_DocumentWidget_(iDocumentWidget *d) {
    iLabelWidget *prog   = findChild_Widget(root_Widget(as_Widget(d)), "document.progress");
    const size_t  dlSize = d->request ? bodySize_GmRequest(d->request) : 0;
    showCollapsed_Widget(as_Widget(prog), dlSize >= 250000);
    if (isVisible_Widget(prog)) {
        updateText_LabelWidget(prog,
                               collectNewFormat_String("%s%.3f ${mb}",
                                                       isFinished_GmRequest(d->request)
                                                           ? uiHeading_ColorEscape
                                                           : uiTextCaution_ColorEscape,
                                                       dlSize / 1.0e6f));
    }
}

static const char *zipPageHeading_(const iRangecc mime) {
    if (equalCase_Rangecc(mime, "application/gpub+zip")) {
        return book_Icon " Gempub";
    }
    else if (equalCase_Rangecc(mime, mimeType_FontPack)) {
        return fontpack_Icon " Fontpack";
    }
    else if (equalCase_Rangecc(mime, mimeType_Export)) {
        return package_Icon " ${heading.archive.userdata}";
    }
    iRangecc type = iNullRange;
    nextSplit_Rangecc(mime, "/", &type); /* skip the part before the slash */
    nextSplit_Rangecc(mime, "/", &type);
    if (startsWithCase_Rangecc(type, "x-")) {
        type.start += 2;
    }
    iString *heading = upper_String(collectNewRange_String(type));
    appendCStr_String(heading, " Archive");
    prependCStr_String(heading, folder_Icon " ");
    return cstrCollect_String(heading);
}

static void postProcessRequestContent_DocumentWidget_(iDocumentWidget *d, iBool isCached) {
    iWidget *w = as_Widget(d);
    /* Embedded images in data links can be shown immediately as they are already fetched
       data that is part of the document. */
    if (prefs_App()->openDataUrlImagesOnLoad) {
        iGmDocument *doc = d->view.doc;
        for (size_t linkId = 1; ; linkId++) {
            const int      linkFlags = linkFlags_GmDocument(doc, linkId);
            const iString *linkUrl   = linkUrl_GmDocument(doc, linkId);
            if (!linkUrl) break;
            if (scheme_GmLinkFlag(linkFlags) == data_GmLinkScheme &&
                (linkFlags & imageFileExtension_GmLinkFlag)) {
                requestMedia_DocumentWidget_(d, linkId, 0);
            }
        }               
    }
    /* Gempub page behavior and footer actions. */ {
        /* TODO: move this to gempub.c */
        delete_Gempub(d->sourceGempub);
        d->sourceGempub = NULL;
        if (!cmpCase_String(&d->sourceMime, "application/octet-stream") ||
            !cmpCase_String(&d->sourceMime, mimeType_Gempub) ||
            endsWithCase_String(d->mod.url, ".gpub")) {
            iGempub *gempub = new_Gempub();
            if (open_Gempub(gempub, &d->sourceContent)) {
                setBaseUrl_Gempub(gempub, d->mod.url);
                setSource_DocumentWidget(d, collect_String(coverPageSource_Gempub(gempub)));
                setCStr_String(&d->sourceMime, mimeType_Gempub);
                d->sourceGempub = gempub;
            }
            else {
                delete_Gempub(gempub);
            }
        }
        if (!d->sourceGempub) {
            const iString *localPath = collect_String(localFilePathFromUrl_String(d->mod.url));
            iBool isInside = iFalse;
            if (localPath && !fileExists_FileInfo(localPath)) {
                /* This URL may refer to a file inside the archive. */
                localPath = findContainerArchive_Path(localPath);
                isInside = iTrue;
            }
            if (localPath && equal_CStr(mediaType_Path(localPath), mimeType_Gempub)) {
                iGempub *gempub = new_Gempub();
                if (openFile_Gempub(gempub, localPath)) {
                    setBaseUrl_Gempub(gempub, collect_String(makeFileUrl_String(localPath)));
                    if (!isInside) {
                        setSource_DocumentWidget(d, collect_String(coverPageSource_Gempub(gempub)));
                        setCStr_String(&d->sourceMime, mimeType_Gempub);
                    }
                    d->sourceGempub = gempub;
                }
                else {
                    delete_Gempub(gempub);
                }
            }
        }
        if (d->sourceGempub) {
            if (equal_String(d->mod.url, coverPageUrl_Gempub(d->sourceGempub))) {
                if (!isRemote_Gempub(d->sourceGempub)) {
                    iArray *items = collectNew_Array(sizeof(iMenuItem));
                    pushBack_Array(
                        items,
                        &(iMenuItem){ book_Icon " ${gempub.cover.view}",
                                      0,
                                      0,
                                      format_CStr("!open url:%s",
                                                  cstr_String(indexPageUrl_Gempub(d->sourceGempub))) });
                    if (navSize_Gempub(d->sourceGempub) > 0) {
                        pushBack_Array(
                            items,
                            &(iMenuItem){
                                format_CStr(forwardArrow_Icon " %s",
                                            cstr_String(navLinkLabel_Gempub(d->sourceGempub, 0))),
                                SDLK_RIGHT,
                                0,
                                format_CStr("!open url:%s",
                                            cstr_String(navLinkUrl_Gempub(d->sourceGempub, 0))) });
                    }
                    makeFooterButtons_DocumentWidget_(d, constData_Array(items), size_Array(items));
                }
                else {
                    makeFooterButtons_DocumentWidget_(
                        d,
                        (iMenuItem[]){ { book_Icon " ${menu.save.downloads.open}",
                                         SDLK_s,
                                         KMOD_PRIMARY | KMOD_SHIFT,
                                         "document.save open:1" },
                                       { download_Icon " " saveToDownloads_Label,
                                         SDLK_s,
                                         KMOD_PRIMARY,
                                         "document.save" } },
                        2);
                }
                if (preloadCoverImage_Gempub(d->sourceGempub, d->view.doc)) {
                    redoLayout_GmDocument(d->view.doc);
                    updateVisible_DocumentView_(&d->view);
                    invalidate_DocumentWidget_(d);
                }
            }
            else if (equal_String(d->mod.url, indexPageUrl_Gempub(d->sourceGempub))) {
                makeFooterButtons_DocumentWidget_(
                    d,
                    (iMenuItem[]){ { format_CStr(book_Icon " %s",
                                                 cstr_String(property_Gempub(d->sourceGempub,
                                                                             title_GempubProperty))),
                                     SDLK_LEFT,
                                     0,
                                     format_CStr("!open url:%s",
                                                 cstr_String(coverPageUrl_Gempub(d->sourceGempub))) } },
                    1);
            }
            else {
                /* Navigation buttons. */
                iArray *items = collectNew_Array(sizeof(iMenuItem));
                const size_t navIndex = navIndex_Gempub(d->sourceGempub, d->mod.url);
                if (navIndex != iInvalidPos) {
                    if (navIndex < navSize_Gempub(d->sourceGempub) - 1) {
                        pushBack_Array(
                            items,
                            &(iMenuItem){
                                format_CStr(forwardArrow_Icon " %s",
                                            cstr_String(navLinkLabel_Gempub(d->sourceGempub, navIndex + 1))),
                                SDLK_RIGHT,
                                0,
                                format_CStr("!open url:%s",
                                            cstr_String(navLinkUrl_Gempub(d->sourceGempub, navIndex + 1))) });
                    }
                    if (navIndex > 0) {
                        pushBack_Array(
                            items,
                            &(iMenuItem){
                                format_CStr(backArrow_Icon " %s",
                                            cstr_String(navLinkLabel_Gempub(d->sourceGempub, navIndex - 1))),
                                SDLK_LEFT,
                                0,
                                format_CStr("!open url:%s",
                                            cstr_String(navLinkUrl_Gempub(d->sourceGempub, navIndex - 1))) });
                    }
                    else if (!equalCase_String(d->mod.url, indexPageUrl_Gempub(d->sourceGempub))) {
                        pushBack_Array(
                            items,
                            &(iMenuItem){
                                format_CStr(book_Icon " %s",
                                            cstr_String(property_Gempub(d->sourceGempub, title_GempubProperty))),
                                SDLK_LEFT,
                                0,
                                format_CStr("!open url:%s",
                                            cstr_String(coverPageUrl_Gempub(d->sourceGempub))) });
                    }
                }
                if (!isEmpty_Array(items)) {
                    makeFooterButtons_DocumentWidget_(d, constData_Array(items), size_Array(items));
                }
            }
            if (!isCached && prefs_App()->pinSplit &&
                equal_String(d->mod.url, indexPageUrl_Gempub(d->sourceGempub))) {
                const iString *navStart = navStartLinkUrl_Gempub(d->sourceGempub);
                if (navStart) {
                    iWindow *win = get_Window();
                    /* Auto-split to show index and the first navigation link. */
                    if (numRoots_Window(win) == 2) {
                        /* This document is showing the index page. */
                        iRoot *other = otherRoot_Window(win, w->root);
                        postCommandf_Root(other, "open url:%s", cstr_String(navStart));
                        if (prefs_App()->pinSplit == 1 && w->root == win->roots[1]) {
                            /* On the wrong side. */
                            postCommand_App("ui.split swap:1");
                        }
                    }
                    else {
                        postCommandf_App(
                            "open splitmode:1 newtab:%d url:%s", otherRoot_OpenTabFlag, cstr_String(navStart));
                    }
                }
            }
        }
    }
}

static void updateDocument_DocumentWidget_(iDocumentWidget *d,
                                           const iGmResponse *response,
                                           iGmDocument *cachedDoc,
                                           const iBool isInitialUpdate) {
    if (d->state == ready_RequestState) {
        return;
    }
    const iBool isRequestFinished = isFinished_GmRequest(d->request);
    /* TODO: Do document update in the background. However, that requires a text metrics calculator
       that does not try to cache the glyph bitmaps. */
    const enum iGmStatusCode statusCode = response->statusCode;
    if (category_GmStatusCode(statusCode) != categoryInput_GmStatusCode) {
        iBool setSource = iTrue;
        iString str;
        invalidate_DocumentWidget_(d);
        if (document_App() == d) {
            updateTheme_DocumentWidget_(d);
        }
        clear_String(&d->sourceMime);
        d->sourceTime = response->when;
        d->view.drawBufs->flags |= updateTimestampBuf_DrawBufsFlag;
        initBlock_String(&str, &response->body); /* Note: Body may be megabytes in size. */
        if (isSuccess_GmStatusCode(statusCode)) {
            /* Check the MIME type. */
            iRangecc charset = range_CStr("utf-8");
            enum iSourceFormat docFormat = undefined_SourceFormat;
            const iString *mimeStr = collect_String(lower_String(&response->meta)); /* for convenience */
            set_String(&d->sourceMime, mimeStr);
            iRangecc mime = range_String(mimeStr);
            iRangecc seg = iNullRange;
            while (nextSplit_Rangecc(mime, ";", &seg)) {
                iRangecc param = seg;
                trim_Rangecc(&param);
                if (isRequestFinished) {
                    /* Format autodetection. */
                    if (equal_Rangecc(param, "application/octet-stream")) {
                        /* Detect fontpacks even if the server doesn't use the right media type. */
                        if (detect_FontPack(&response->body)) {
                            param = range_CStr(mimeType_FontPack);
                        }
                        else if (isUtf8_Rangecc(range_Block(&response->body))) {
                            param = range_CStr("text/plain");
                        }
                    }
                    if (equal_Rangecc(param, "text/plain")) {
                        iUrl parts;
                        init_Url(&parts, d->mod.url);
                        const iRangecc fileName = baseNameSep_Path(collectNewRange_String(parts.path), "/");
                        if (endsWithCase_Rangecc(fileName, ".md") ||
                            endsWithCase_Rangecc(fileName, ".mdown") ||
                            endsWithCase_Rangecc(fileName, ".markdown")) {
                            param = range_CStr("text/markdown");
                        }
                        else if (endsWithCase_Rangecc(fileName, ".gmi") ||
                                 endsWithCase_Rangecc(fileName, ".gemini")) {
                            param = range_CStr("text/gemini");
                        }
                    }
                }
                if (equal_Rangecc(param, "text/gemini")) {
                    docFormat = gemini_SourceFormat;
                    setRange_String(&d->sourceMime, param);
                }
                else if (equal_Rangecc(param, "text/markdown")) {
                    docFormat = markdown_SourceFormat;
                    setRange_String(&d->sourceMime, param);
                    postCommand_Widget(d, "document.viewformat arg:%d", !prefs_App()->markdownAsSource);
                }
                else if (startsWith_Rangecc(param, "text/") ||
                         equal_Rangecc(param, "application/json") ||
                         equal_Rangecc(param, "application/x-pem-file") ||
                         equal_Rangecc(param, "application/pem-certificate-chain")) {
                    docFormat = plainText_SourceFormat;
                    setRange_String(&d->sourceMime, param);
                }
                else if (isRequestFinished && equal_Rangecc(param, "font/ttf")) {
                    clear_String(&str);
                    docFormat = gemini_SourceFormat;
                    setRange_String(&d->sourceMime, param);
                    format_String(&str, "# TrueType Font\n");
                    iString *decUrl      = collect_String(urlDecode_String(d->mod.url));
                    iRangecc name        = baseNameSep_Path(decUrl, "/");
                    iBool    isInstalled = iFalse;
                    if (startsWith_String(collect_String(localFilePathFromUrl_String(d->mod.url)),
                                          cstr_String(dataDir_App()))) {
                        isInstalled = iTrue;
                    }
                    appendCStr_String(&str, "## ");
                    appendRange_String(&str, name);
                    appendCStr_String(&str, "\n\n");
                    appendCStr_String(
                        &str, cstr_Lang(isInstalled ? "truetype.help.installed" : "truetype.help"));
                    appendCStr_String(&str, "\n");
                    if (!isInstalled) {
                        makeFooterButtons_DocumentWidget_(
                            d,
                            (iMenuItem[]){
                                { add_Icon " ${fontpack.install.ttf}",
                                  SDLK_RETURN,
                                  0,
                                  format_CStr("!fontpack.install ttf:1 name:%s",
                                              cstr_Rangecc(name)) },
                                { folder_Icon " ${fontpack.open.fontsdir}",
                                  SDLK_d,
                                  0,
                                  format_CStr("!open url:%s/fonts",
                                              cstrCollect_String(makeFileUrl_String(dataDir_App())))
                                }
                            }, 2);
                    }
                }
                else if (isRequestFinished &&
                         (equal_Rangecc(param, "application/zip") ||
                         (startsWith_Rangecc(param, "application/") &&
                          endsWithCase_Rangecc(param, "+zip")))) {
                    iArray *footerItems = collectNew_Array(sizeof(iMenuItem));
                    clear_String(&str);
                    docFormat = gemini_SourceFormat;
                    setRange_String(&d->sourceMime, param);
                    iArchive *zip = new_Archive();
                    openData_Archive(zip, &response->body);
                    if (equal_Rangecc(param, mimeType_FontPack)) {
                        /* Show some information about fontpacks, and set up footer actions. */
                        if (isOpen_Archive(zip)) {
                            iFontPack *fp = new_FontPack();
                            setUrl_FontPack(fp, d->mod.url);
                            setStandalone_FontPack(fp, iTrue);
                            if (loadArchive_FontPack(fp, zip)) {
                                appendFormat_String(&str, "# " fontpack_Icon "%s\n%s",
                                                    cstr_String(id_FontPack(fp).id),
                                                    cstrCollect_String(infoText_FontPack(fp, iTrue)));
                            }
                            appendCStr_String(&str, "\n");
                            appendCStr_String(&str, cstr_Lang("fontpack.help"));
                            appendCStr_String(&str, "\n");
//                            footerItems = actions_FontPack(fp, iTrue);
//                            const iArray *actions =;
                            iConstForEach(Array, a, actions_FontPack(fp, iTrue)) {
                                pushBack_Array(footerItems, a.value);
                            }
//                            makeFooterButtons_DocumentWidget_(d, constData_Array(actions),
//                                                              size_Array(actions));
                            delete_FontPack(fp);
                        }
                    }
                    else {
                        if (detect_Export(zip)) {
                            setCStr_String(&d->sourceMime, mimeType_Export);
                            if (!isMobile_Platform()) {
                                pushBack_Array(footerItems,
                                               &(iMenuItem){ openExt_Icon " ${menu.open.external}",
                                                             SDLK_RETURN,
                                                             KMOD_PRIMARY,
                                                             "document.save extview:1" });
                            }
                        }
                        format_String(&str, "# %s\n", zipPageHeading_(range_String(&d->sourceMime)));
                        appendFormat_String(
                            &str,
                            cstr_Lang("doc.archive"),
                            cstr_Rangecc(baseNameSep_Path(collect_String(urlDecode_String(
                                                              urlQueryStripped_String(d->mod.url))),
                                                          "/")));
                        appendCStr_String(&str, "\n");
                    }
                    iRelease(zip);
                    appendCStr_String(&str, "\n");
                    iString *localPath = localFilePathFromUrl_String(d->mod.url);
                    if (!localPath || !fileExists_FileInfo(localPath)) {
                        iString *key = collectNew_String();
                        toString_Sym(SDLK_s, KMOD_PRIMARY, key);
                        appendFormat_String(&str, "%s\n\n",
                                            format_CStr(cstr_Lang("error.unsupported.suggestsave"),
                                                        cstr_String(key),
                                                        saveToDownloads_Label));
                        if (findCommand_MenuItem(data_Array(footerItems),
                                                 size_Array(footerItems),
                                                 "document.save") == iInvalidPos) {
                            pushBack_Array(
                                footerItems,
                                &(iMenuItem){
                                    translateCStr_Lang(download_Icon " " saveToDownloads_Label),
                                    0,
                                    0,
                                    "document.save" });
                        }
                    }
                    if (!cmp_String(&d->sourceMime, mimeType_Export)) {
                        appendFormat_String(&str, "%s\n", cstr_Lang("userdata.help"));
                    }
                    if (localPath && fileExists_FileInfo(localPath)) {
                        if (!cmp_String(&d->sourceMime, mimeType_Export)) {
                            pushFront_Array(footerItems,
                                            &(iMenuItem){ import_Icon " " uiTextAction_ColorEscape
                                                                      "\x1b[1m${menu.import}",
                                                          SDLK_RETURN,
                                                          0,
                                                          format_CStr("!import path:%s",
                                                                      cstr_String(localPath)) });
                        }
                        appendFormat_String(&str,
                                            "=> %s/ " folder_Icon " ${doc.archive.view}\n",
                                            cstr_String(withSpacesEncoded_String(d->mod.url)));
                    }
                    delete_String(localPath);
                    translate_Lang(&str);
                    makeFooterButtons_DocumentWidget_(
                        d, constData_Array(footerItems), size_Array(footerItems));
                }
                else if (!isTerminal_Platform() && (startsWith_Rangecc(param, "image/") ||
                                                    startsWith_Rangecc(param, "audio/"))) {
                    const iBool isAudio = startsWith_Rangecc(param, "audio/");
                    /* Make a simple document with an image or audio player. */
                    clear_String(&str);
                    docFormat = gemini_SourceFormat;
                    setRange_String(&d->sourceMime, param);
                    const iGmLinkId imgLinkId = 1; /* there's only the one link */
                    /* TODO: Do the image loading in `postProcessRequestContent_DocumentWidget_()` */
                    if ((isAudio && isInitialUpdate) || (!isAudio && isRequestFinished)) {
                        const char *linkTitle = cstr_Lang(
                            startsWith_String(mimeStr, "image/") ? "media.untitled.image"
                                                                 : "media.untitled.audio");
                        iUrl parts;
                        init_Url(&parts, d->mod.url);
                        if (!isEmpty_Range(&parts.path) && !equalCase_Rangecc(parts.scheme, "data")) {
                            linkTitle =
                                baseName_Path(collect_String(newRange_String(parts.path))).start;
                        }
                        format_String(&str, "=> %s %s\n",
                                      cstr_String(canonicalUrl_String(d->mod.url)),
                                      linkTitle);
                        setData_Media(media_GmDocument(d->view.doc),
                                      imgLinkId,
                                      mimeStr,
                                      &response->body,
                                      !isRequestFinished ? partialData_MediaFlag : 0);
                        redoLayout_GmDocument(d->view.doc);
                    }
                    else if (isAudio && !isInitialUpdate) {
                        /* Update the audio content. */
                        setData_Media(media_GmDocument(d->view.doc),
                                      imgLinkId,
                                      mimeStr,
                                      &response->body,
                                      !isRequestFinished ? partialData_MediaFlag : 0);
                        refresh_Widget(d);
                        setSource = iFalse;
                    }
                    else {
                        clear_String(&str);
                    }
                }
                else if (startsWith_Rangecc(param, "charset=")) {
                    charset = (iRangecc){ param.start + 8, param.end };
                    /* Remove whitespace and quotes. */
                    trim_Rangecc(&charset);
                    if (*charset.start == '"' && *charset.end == '"') {
                        charset.start++;
                        charset.end--;
                    }
                }
            }
            if (docFormat == undefined_SourceFormat) {
                if (isRequestFinished) {
                    d->flags &= ~drawDownloadCounter_DocumentWidgetFlag;
                    showErrorPage_DocumentWidget_(d, unsupportedMimeType_GmStatusCode, &response->meta);
                    deinit_String(&str);
                    return;
                }
                d->flags |= drawDownloadCounter_DocumentWidgetFlag;
                clear_PtrSet(d->view.invalidRuns);
                documentRunsInvalidated_DocumentWidget_(d);
                deinit_String(&str);
                return;
            }
            setFormat_GmDocument(d->view.doc, docFormat);
            /* Convert the source to UTF-8 if needed. */
            if (!equalCase_Rangecc(charset, "utf-8")) {
                set_String(&str,
                           collect_String(decode_Block(&str.chars, cstr_Rangecc(charset))));
            }
        }
        if (cachedDoc) {
            replaceDocument_DocumentWidget_(d, cachedDoc);
            updateWidth_DocumentView_(&d->view);
        }
        else if (setSource) {
            setSource_DocumentWidget(d, &str);
        }
        deinit_String(&str);
    }
}

static void fetch_DocumentWidget_(iDocumentWidget *d) {
    iAssert(~d->flags & animationPlaceholder_DocumentWidgetFlag);
    /* Forget the previous request. */
    if (d->request) {
        iRelease(d->request);
        d->request = NULL;
    }
    postCommandf_Root(as_Widget(d)->root,
                      "document.request.started doc:%p url:%s",
                      d,
                      cstr_String(d->mod.url));
    setLinkNumberMode_DocumentWidget_(d, iFalse);
    d->flags &= ~drawDownloadCounter_DocumentWidgetFlag;
    d->state = fetching_RequestState;
    set_Atomic(&d->isRequestUpdated, iFalse);
    d->request = new_GmRequest(certs_App());
    setUrl_GmRequest(d->request, d->mod.url);
    iConnect(GmRequest, d->request, updated, d, requestUpdated_DocumentWidget_);
    iConnect(GmRequest, d->request, finished, d, requestFinished_DocumentWidget_);
    submit_GmRequest(d->request);
}

static void updateTrust_DocumentWidget_(iDocumentWidget *d, const iGmResponse *response) {
    if (response) {
        d->certFlags  = response->certFlags;
        d->certExpiry = response->certValidUntil;
        set_Block(d->certFingerprint, &response->certFingerprint);
        set_String(d->certSubject, &response->certSubject);
    }
    iLabelWidget *lock = findChild_Widget(root_Widget(as_Widget(d)), "navbar.lock");
    if (~d->certFlags & available_GmCertFlag) {
        setFlags_Widget(as_Widget(lock), disabled_WidgetFlag, iTrue);
        updateTextCStr_LabelWidget(lock, openLock_Icon);
        setTextColor_LabelWidget(lock, gray50_ColorId);
        return;
    }
    setFlags_Widget(as_Widget(lock), disabled_WidgetFlag, iFalse);
    const iBool isDarkMode = isDark_ColorTheme(colorTheme_App());
    if (~d->certFlags & domainVerified_GmCertFlag ||
        ~d->certFlags & trusted_GmCertFlag) {
        updateTextCStr_LabelWidget(lock, warning_Icon);
        setTextColor_LabelWidget(lock, red_ColorId);
    }
    else if (~d->certFlags & timeVerified_GmCertFlag) {
        updateTextCStr_LabelWidget(lock, warning_Icon);
        setTextColor_LabelWidget(lock, isDarkMode ? orange_ColorId : black_ColorId);
    }
    else {
        updateTextCStr_LabelWidget(lock, closedLock_Icon);
        setTextColor_LabelWidget(lock, green_ColorId);
    }
}

static void parseUser_DocumentWidget_(iDocumentWidget *d) {
    const iRangecc scheme = urlScheme_String(d->mod.url);
    if (equalCase_Rangecc(scheme, "gemini") || equalCase_Rangecc(scheme, "titan") ||
        equalCase_Rangecc(scheme, "spartan") || equalCase_Rangecc(scheme, "gopher")) {
        setRange_String(d->titleUser, urlUser_String(d->mod.url));
    }
    else {
        clear_String(d->titleUser);
    }
}

static void cacheRunGlyphs_(void *data, const iGmRun *run) {
    iUnused(data);
    if (!isEmpty_Range(&run->text)) {
        cache_Text(run->font, run->text);
    }
}

static void cacheDocumentGlyphs_DocumentWidget_(const iDocumentWidget *d) {
    if (isFinishedLaunching_App() && isExposed_Window(get_Window()) &&
        ~d->flags & animationPlaceholder_DocumentWidgetFlag) {
        /* Just cache the top of the document, since this is what we usually need. */
        int maxY = height_Widget(&d->widget) * 2;
        if (maxY == 0) {
            maxY = size_GmDocument(d->view.doc).y;
        }
        render_GmDocument(d->view.doc, (iRangei){ 0, maxY }, cacheRunGlyphs_, NULL);
    }
}

static void addBannerWarnings_DocumentWidget_(iDocumentWidget *d) {
    updateBanner_DocumentWidget_(d);
    /* Warnings are not shown on internal pages. */
    if (equalCase_Rangecc(urlScheme_String(d->mod.url), "about")) {
        clear_Banner(d->banner);
        return;
    }
    /* Warnings related to certificates and trust. */
    const int certFlags = d->certFlags;
    const int req = timeVerified_GmCertFlag | domainVerified_GmCertFlag | trusted_GmCertFlag;
    if (certFlags & available_GmCertFlag && (certFlags & req) != req &&
        numItems_Banner(d->banner) == 0) {
        iString *title = collectNewCStr_String(cstr_Lang("dlg.certwarn.title"));
        iString *str   = collectNew_String();
        if (certFlags & timeVerified_GmCertFlag && certFlags & domainVerified_GmCertFlag) {
            iUrl parts;
            init_Url(&parts, d->mod.url);
            const iTime oldUntil =
                domainValidUntil_GmCerts(certs_App(), parts.host, port_Url(&parts));
            iDate exp;
            init_Date(&exp, &oldUntil);
            iTime now;
            initCurrent_Time(&now);
            const int days = secondsSince_Time(&oldUntil, &now) / 3600 / 24;
            if (days <= 30) {
                appendCStr_String(str,
                                  format_CStr(cstrCount_Lang("dlg.certwarn.mayberenewed.n", days),
                                              cstrCollect_String(format_Date(&exp, "%Y-%m-%d")),
                                              days));
            }
            else {
                appendCStr_String(str, cstr_Lang("dlg.certwarn.different"));
            }
        }
        else if (certFlags & domainVerified_GmCertFlag) {
            setCStr_String(title, get_GmError(tlsServerCertificateExpired_GmStatusCode)->title);
            appendFormat_String(str, cstr_Lang("dlg.certwarn.expired"),
                                cstrCollect_String(format_Date(&d->certExpiry, "%Y-%m-%d")));
        }
        else if (certFlags & timeVerified_GmCertFlag) {
            appendFormat_String(str, cstr_Lang("dlg.certwarn.domain"),
                                cstr_String(d->certSubject));
        }
        else {
            appendCStr_String(str, cstr_Lang("dlg.certwarn.domain.expired"));
        }
        add_Banner(d->banner, warning_BannerType, none_GmStatusCode, title, str);
    }
    /* Warnings related to page contents. */
    int dismissed =
        value_SiteSpec(collectNewRange_String(urlRoot_String(d->mod.url)),
                       dismissWarnings_SiteSpecKey) |
        (!prefs_App()->warnAboutMissingGlyphs ? missingGlyphs_GmDocumentWarning : 0);
    /* File pages don't allow dismissing warnings, so skip it. */
    if (equalCase_Rangecc(urlScheme_String(d->mod.url), "file")) {
        dismissed |= ansiEscapes_GmDocumentWarning;
    }
    const int warnings = warnings_GmDocument(d->view.doc) & ~dismissed;
    if (warnings & missingGlyphs_GmDocumentWarning) {
        add_Banner(d->banner, warning_BannerType, missingGlyphs_GmStatusCode, NULL, NULL);
        /* TODO: List one or more of the missing characters and/or their Unicode blocks? */
    }
    if (warnings & ansiEscapes_GmDocumentWarning) {
        add_Banner(d->banner, warning_BannerType, ansiEscapes_GmStatusCode, NULL, NULL);
    }
}

static void updateFromCachedResponse_DocumentWidget_(iDocumentWidget *d, float normScrollY,
                                                     const iGmResponse *resp, iGmDocument *cachedDoc) {
//    iAssert(width_Widget(d) > 0); /* must be laid out by now */
    setLinkNumberMode_DocumentWidget_(d, iFalse);
    clear_ObjectList(d->media);
    delete_Gempub(d->sourceGempub);
    d->sourceGempub = NULL;
    pauseAllPlayers_Media(media_GmDocument(d->view.doc), iTrue);
    destroy_Widget(d->footerButtons);
    d->footerButtons = NULL;
    iRelease(d->view.doc);
    d->view.doc = new_GmDocument();
    d->state = fetching_RequestState;
    d->flags |= fromCache_DocumentWidgetFlag;
    /* Do the fetch. */ {
        d->initNormScrollY = normScrollY;
        /* Use the cached response data. */
        updateTrust_DocumentWidget_(d, resp);
        d->sourceTime   = resp->when;
        d->sourceStatus = success_GmStatusCode;
        format_String(&d->sourceHeader, cstr_Lang("pageinfo.header.cached"));
        set_Block(&d->sourceContent, &resp->body);
        if (!cachedDoc) {
            updateWidthAndRedoLayout_DocumentView_(&d->view);
        }
        updateDocument_DocumentWidget_(d, resp, cachedDoc, iTrue);
        clear_Banner(d->banner);
        updateBanner_DocumentWidget_(d);
        addBannerWarnings_DocumentWidget_(d);
    }
    d->state = ready_RequestState;
    postProcessRequestContent_DocumentWidget_(d, iTrue);
    resetScroll_DocumentView_(&d->view);
    init_Anim(&d->view.scrollY.pos, d->initNormScrollY * pageHeight_DocumentView_(&d->view));
    updateVisible_DocumentView_(&d->view);
    moveSpan_SmoothScroll(&d->view.scrollY, 0, 0); /* clamp position to new max */
    updateSideOpacity_DocumentView_(&d->view, iFalse);
    cacheDocumentGlyphs_DocumentWidget_(d);
    d->view.drawBufs->flags |= updateTimestampBuf_DrawBufsFlag | updateSideBuf_DrawBufsFlag;
    d->flags &= ~(urlChanged_DocumentWidgetFlag | drawDownloadCounter_DocumentWidgetFlag);
    postCommandf_Root(
        as_Widget(d)->root, "document.changed doc:%p url:%s", d, cstr_String(d->mod.url));
}

static iBool updateFromHistory_DocumentWidget_(iDocumentWidget *d) {
    const iRecentUrl *recent = constMostRecentUrl_History(d->mod.history);
    if (recent && recent->cachedResponse && equalCase_String(&recent->url, d->mod.url)) {
        updateFromCachedResponse_DocumentWidget_(
            d, recent->normScrollY, recent->cachedResponse, recent->cachedDoc);
        if (!recent->cachedDoc) {
            /* We have a cached copy now. */
            setCachedDocument_History(d->mod.history, d->view.doc);
        }
        return iTrue;
    }
    else if (!isEmpty_String(d->mod.url)) {
        fetch_DocumentWidget_(d);
    }
    if (recent) {
        /* Retain scroll position in refetched content as well. */
        d->initNormScrollY = recent->normScrollY;
    }
    return iFalse;
}

static void refreshWhileScrolling_DocumentWidget_(iAny *ptr) {
    iAssert(isInstance_Object(ptr, &Class_DocumentWidget));
    iDocumentWidget *d = ptr;
    iDocumentView *view = &d->view;
    updateVisible_DocumentView_(view);
    refresh_Widget(d);
    if (view->animWideRunId) {
        for (const iGmRun *r = view->animWideRunRange.start; r != view->animWideRunRange.end; r++) {
            insert_PtrSet(view->invalidRuns, r);
        }
    }
    if (isFinished_Anim(&view->animWideRunOffset)) {
        view->animWideRunId = 0;
    }
    if (!isFinished_SmoothScroll(&view->scrollY) || !isFinished_Anim(&view->animWideRunOffset)) {
        addTicker_App(refreshWhileScrolling_DocumentWidget_, d);
    }
    if (isFinished_SmoothScroll(&view->scrollY)) {
        iChangeFlags(d->flags, noHoverWhileScrolling_DocumentWidgetFlag, iFalse);
        updateHover_DocumentView_(view, mouseCoord_Window(get_Window(), 0));
    }
}

static void scrollBegan_DocumentWidget_(iAnyObject *any, int offset, uint32_t duration) {
    iDocumentWidget *d = any;
    /* Get rid of link numbers when scrolling. */
    if (offset && d->flags & showLinkNumbers_DocumentWidgetFlag) {
        setLinkNumberMode_DocumentWidget_(d, iFalse);
        invalidateVisibleLinks_DocumentView_(&d->view);
    }
    /* Show and hide toolbar on scroll. */
    if (deviceType_App() == phone_AppDeviceType) {
        const float normPos = normScrollPos_DocumentView_(&d->view);
        if (prefs_App()->hideToolbarOnScroll && iAbs(offset) > 5 && normPos >= 0) {
            showToolbar_Root(as_Widget(d)->root, offset < 0);
        }
    }
    updateVisible_DocumentView_(&d->view);
    refresh_Widget(as_Widget(d));
    if (duration > 0) {
        iChangeFlags(d->flags, noHoverWhileScrolling_DocumentWidgetFlag, iTrue);
        addTicker_App(refreshWhileScrolling_DocumentWidget_, d);
    }
}

static void togglePreFold_DocumentWidget_(iDocumentWidget *d, uint16_t preId) {
    d->view.hoverPre    = NULL;
    d->view.hoverAltPre = NULL;
    d->selectMark       = iNullRange;
    foldPre_GmDocument(d->view.doc, preId);
    redoLayout_GmDocument(d->view.doc);
    clampScroll_DocumentView_(&d->view);
    updateHover_DocumentView_(&d->view, mouseCoord_Window(get_Window(), 0));
    invalidate_DocumentWidget_(d);
    refresh_Widget(as_Widget(d));
}

static iString *makeQueryUrl_DocumentWidget_(const iDocumentWidget *d,
                                             const iString *userEnteredText) {
    iString *url = copy_String(d->mod.url);
    /* Remove the existing query string. */
    const size_t qPos = indexOfCStr_String(url, "?");
    if (qPos != iInvalidPos) {
        remove_Block(&url->chars, qPos, iInvalidSize);
    }
    appendCStr_String(url, "?");
    iString *cleaned = copy_String(userEnteredText);
    if (deviceType_App() != desktop_AppDeviceType) {
        trimEnd_String(cleaned); /* autocorrect may insert an extra space */
        if (isEmpty_String(cleaned)) {
            set_String(cleaned, userEnteredText); /* user wanted just spaces? */
        }
    }
    append_String(url, collect_String(urlEncode_String(cleaned)));
    delete_String(cleaned);
    return url;
}

static void inputQueryValidator_(iInputWidget *input, void *context) {
    iDocumentWidget *d = context;
    iString *url = makeQueryUrl_DocumentWidget_(d, text_InputWidget(input));
    iWidget *dlg = parent_Widget(input);
    iLabelWidget *counter = findChild_Widget(dlg, "valueinput.counter");
    iAssert(counter);
    int avail = 1024 - (int) size_String(url);
    setFlags_Widget(findChild_Widget(dlg, "default"), disabled_WidgetFlag, avail < 0);
    setEnterKeyEnabled_InputWidget(input, avail >= 0);
    int len = length_String(text_InputWidget(input));
    if (len > 1024) {
        iString *trunc = copy_String(text_InputWidget(input));
        truncate_String(trunc, 1024);
        setText_InputWidget(input, trunc);
        delete_String(trunc);
    }
    setTextCStr_LabelWidget(counter, format_CStr("%d", avail)); /* Gemini URL maxlen */
    setTextColor_LabelWidget(counter,
                             avail < 0   ? uiTextCaution_ColorId :
                             avail < 128 ? uiTextStrong_ColorId
                                         : uiTextDim_ColorId);
    delete_String(url);
    arrange_Widget(findChild_Widget(dlg, "dialogbuttons"));
}

static const char *humanReadableStatusCode_(enum iGmStatusCode code) {
    if (code <= 0) {
        return "";
    }
    return format_CStr("%d ", code);
}

static iBool setUrl_DocumentWidget_(iDocumentWidget *d, const iString *url) {
    url = canonicalUrl_String(url);
    if (!equal_String(d->mod.url, url)) {
        d->flags |= urlChanged_DocumentWidgetFlag;
        set_String(d->mod.url, url);
        return iTrue;
    }
    return iFalse;
}

static void checkResponse_DocumentWidget_(iDocumentWidget *d) {
    if (!d->request) {
        return;
    }
    enum iGmStatusCode statusCode = status_GmRequest(d->request);
    if (statusCode == none_GmStatusCode) {
        return;
    }
    iGmResponse *resp = lockResponse_GmRequest(d->request);
    if (d->state == fetching_RequestState) {
        /* Under certain conditions, inline any image response into the current document. */
        if (!isTerminal_Platform() &&
            ~d->flags & preventInlining_DocumentWidgetFlag &&
            d->requestLinkId &&
            isSuccess_GmStatusCode(d->sourceStatus) &&
            startsWithCase_String(&d->sourceMime, "text/gemini") &&
            isSuccess_GmStatusCode(statusCode) &&
            startsWithCase_String(&resp->meta, "image/")) {
            /* This request is turned into a new media request in the current document. */
            iDisconnect(GmRequest, d->request, updated, d, requestUpdated_DocumentWidget_);
            iDisconnect(GmRequest, d->request, finished, d, requestFinished_DocumentWidget_);
            iMediaRequest *mr = newReused_MediaRequest(d, d->requestLinkId, d->request);
            unlockResponse_GmRequest(d->request);
            d->request = NULL; /* ownership moved */
            if (!isFinished_GmRequest(mr->req)) {
                postCommand_Widget(d, "document.request.cancelled doc:%p", d);
            }
            pushBack_ObjectList(d->media, mr);
            iRelease(mr);
            /* Reset the fetch state, returning to the originating page. */
            d->state = ready_RequestState;
            if (equal_String(&mostRecentUrl_History(d->mod.history)->url, url_GmRequest(mr->req))) {
                undo_History(d->mod.history);
            }
            if (setUrl_DocumentWidget_(d, url_GmDocument(d->view.doc))) {
                postCommand_Widget(d, "!document.changed doc:%p url:%s", d, cstr_String(d->mod.url));
            }
            updateFetchProgress_DocumentWidget_(d);
            postCommand_Widget(d, "media.updated link:%u request:%p", d->requestLinkId, mr);
            if (isFinished_GmRequest(mr->req)) {
                postCommand_Widget(d, "media.finished link:%u request:%p", d->requestLinkId, mr);
            }
            return;
        }
        /* Get ready for the incoming new document. */
        d->state = receivedPartialResponse_RequestState;
        d->flags &= ~fromCache_DocumentWidgetFlag;
        clear_ObjectList(d->media);
        updateTrust_DocumentWidget_(d, resp);
        if (isSuccess_GmStatusCode(statusCode)) {
            clear_Banner(d->banner);
            updateTheme_DocumentWidget_(d);
        }
        if (~d->certFlags & trusted_GmCertFlag &&
            isSuccess_GmStatusCode(statusCode) &&
            equalCase_Rangecc(urlScheme_String(d->mod.url), "gemini")) {
            statusCode = tlsServerCertificateNotVerified_GmStatusCode;
        }
        init_Anim(&d->view.sideOpacity, 0);
        init_Anim(&d->view.altTextOpacity, 0);
        format_String(&d->sourceHeader,
                      "%s%s",
                      humanReadableStatusCode_(statusCode),
                      isEmpty_String(&resp->meta) && !isSuccess_GmStatusCode(statusCode)
                          ? get_GmError(statusCode)->title
                          : cstr_String(&resp->meta));
        d->sourceStatus = statusCode;
        switch (category_GmStatusCode(statusCode)) {
            case categoryInput_GmStatusCode: {
                /* Let the navigation history know that we have been to this URL even though
                   it is only displayed as an input dialog. */
                visitUrl_Visited(visited_App(), d->mod.url, transient_VisitedUrlFlag);
                iUrl parts;
                init_Url(&parts, d->mod.url);
                iWidget *dlg = makeValueInput_Widget(
                    as_Widget(d),
                    NULL,
                    format_CStr(uiHeading_ColorEscape "%s", cstr_Rangecc(parts.host)),
                    isEmpty_String(&resp->meta)
                        ? format_CStr(cstr_Lang("dlg.input.prompt"), cstr_Rangecc(parts.path))
                        : cstr_String(&resp->meta),
                    uiTextAction_ColorEscape "${dlg.input.send}",
                    format_CStr("!document.input.submit doc:%p", d));
                iWidget *buttons = findChild_Widget(dlg, "dialogbuttons");
                iLabelWidget *lineBreak = NULL;
                if (statusCode != sensitiveInput_GmStatusCode) {
                    /* The line break and URL length counters are positioned differently on mobile.
                       There is no line breaks in sensitive input. */
                    if (deviceType_App() == desktop_AppDeviceType) {
                        iString *keyStr = collectNew_String();
                        toString_Sym(SDLK_RETURN,
                                     lineBreakKeyMod_ReturnKeyBehavior(prefs_App()->returnKey),
                                     keyStr);
                        lineBreak = new_LabelWidget(
                            format_CStr("${dlg.input.linebreak}" uiTextAction_ColorEscape "  %s",
                                        cstr_String(keyStr)),
                            NULL);
                        insertChildAfter_Widget(buttons, iClob(lineBreak), 0);
                    }
                    if (lineBreak) {
                        setFlags_Widget(as_Widget(lineBreak), frameless_WidgetFlag, iTrue);
                        setTextColor_LabelWidget(lineBreak, uiTextDim_ColorId);
                    }
                }
                iWidget *counter = (iWidget *) new_LabelWidget("", NULL);
                setId_Widget(counter, "valueinput.counter");
                setFlags_Widget(counter, frameless_WidgetFlag | resizeToParentHeight_WidgetFlag, iTrue);
                if (deviceType_App() == desktop_AppDeviceType) {
                    addChildPos_Widget(buttons, iClob(counter), front_WidgetAddPos);
                }
                else {
                    insertChildAfter_Widget(buttons, iClob(counter), 1);
                }
                if (lineBreak && deviceType_App() != desktop_AppDeviceType) {
                    addChildPos_Widget(buttons, iClob(lineBreak), front_WidgetAddPos);
                }
                /* Menu for additional actions, past entries. */ {
                    const iBinding *bind = findCommand_Keys("input.precedingline");
                    iMenuItem items[] = { { "${menu.input.precedingline}",
                                            bind->key,
                                            bind->mods,
                                            format_CStr("!valueinput.set ptr:%p text:%s",
                                                        buttons,
                                                        cstr_String(&d->linePrecedingLink)) } };
                    iLabelWidget *menu = makeMenuButton_LabelWidget(midEllipsis_Icon, items, 1);
                    if (deviceType_App() == desktop_AppDeviceType) {
                        addChildPos_Widget(buttons, iClob(menu), front_WidgetAddPos);
                    }
                    else {
                        insertChildAfterFlags_Widget(buttons, iClob(menu), 0,
                                                     frameless_WidgetFlag | noBackground_WidgetFlag);
                        setFont_LabelWidget(menu, font_LabelWidget((iLabelWidget *) lastChild_Widget(buttons)));
                        setTextColor_LabelWidget(menu, uiTextAction_ColorId);
                    }
                }
                iInputWidget *input = findChild_Widget(dlg, "input");
                setValidator_InputWidget(input, inputQueryValidator_, d);
                setBackupFileName_InputWidget(input, "inputbackup");
                setSelectAllOnFocus_InputWidget(input, iTrue);
                setSensitiveContent_InputWidget(input, statusCode == sensitiveInput_GmStatusCode);
                if (document_App() != d) {
                    postCommandf_App("tabs.switch page:%p", d);
                }
                else {
                    updateTheme_DocumentWidget_(d);
                }
                break;
            }
            case categorySuccess_GmStatusCode:
                if (d->flags & urlChanged_DocumentWidgetFlag) {
                    /* Keep scroll position when reloading the same page. */
                    resetScroll_DocumentView_(&d->view);
                }
                d->view.scrollY.pullActionTriggered = 0;
                pauseAllPlayers_Media(media_GmDocument(d->view.doc), iTrue);
                iReleasePtr(&d->view.doc); /* new content incoming */
                delete_Gempub(d->sourceGempub);
                d->sourceGempub = NULL;
                destroy_Widget(d->footerButtons);
                d->footerButtons = NULL;
                d->view.doc = new_GmDocument();
                resetWideRuns_DocumentView_(&d->view);
                updateDocument_DocumentWidget_(d, resp, NULL, iTrue);
                break;
            case categoryRedirect_GmStatusCode:
                if (isEmpty_String(&resp->meta)) {
                    showErrorPage_DocumentWidget_(d, invalidRedirect_GmStatusCode, NULL);
                }
                else {
                    /* Only accept redirects that use gemini scheme. */
                    const iString *dstUrl    = absoluteUrl_String(d->mod.url, &resp->meta);
                    const iRangecc srcScheme = urlScheme_String(d->mod.url);
                    const iRangecc dstScheme = urlScheme_String(dstUrl);
                    if (d->redirectCount >= 5) {
                        showErrorPage_DocumentWidget_(d, tooManyRedirects_GmStatusCode, dstUrl);
                    }
                    /* Redirects with the same scheme are automatic, and switching automatically
                       between "gemini" and "titan" is allowed. */
                    else if (equalRangeCase_Rangecc(dstScheme, srcScheme) ||
                             (equalCase_Rangecc(srcScheme, "titan") &&
                              equalCase_Rangecc(dstScheme, "gemini")) ||
                             (equalCase_Rangecc(srcScheme, "gemini") &&
                              equalCase_Rangecc(dstScheme, "titan"))) {
                        visitUrl_Visited(visited_App(), d->mod.url, transient_VisitedUrlFlag);
                        postCommandf_Root(as_Widget(d)->root,
                            "open doc:%p redirect:%d url:%s", d, d->redirectCount + 1, cstr_String(dstUrl));
                    }
                    else {
                        /* Scheme changes must be manually approved. */
                        showErrorPage_DocumentWidget_(d, schemeChangeRedirect_GmStatusCode, dstUrl);
                    }
                    unlockResponse_GmRequest(d->request);
                    iReleasePtr(&d->request);
                }
                break;
            default:
                if (isDefined_GmError(statusCode)) {
                    showErrorPage_DocumentWidget_(d, statusCode, &resp->meta);
                }
                else if (category_GmStatusCode(statusCode) ==
                         categoryTemporaryFailure_GmStatusCode) {
                    showErrorPage_DocumentWidget_(
                        d, temporaryFailure_GmStatusCode, &resp->meta);
                }
                else if (category_GmStatusCode(statusCode) ==
                         categoryPermanentFailure_GmStatusCode) {
                    showErrorPage_DocumentWidget_(
                        d, permanentFailure_GmStatusCode, &resp->meta);
                }
                else {
                    showErrorPage_DocumentWidget_(d, unknownStatusCode_GmStatusCode, &resp->meta);
                }
                break;
        }
    }
    else if (d->state == receivedPartialResponse_RequestState) {
        d->flags &= ~fromCache_DocumentWidgetFlag;
        switch (category_GmStatusCode(statusCode)) {
            case categorySuccess_GmStatusCode:
                /* More content available. */
                updateDocument_DocumentWidget_(d, resp, NULL, iFalse);
                break;
            default:
                break;
        }
    }
    unlockResponse_GmRequest(d->request);
}

static void removeMediaRequest_DocumentWidget_(iDocumentWidget *d, iGmLinkId linkId) {
    iForEach(ObjectList, i, d->media) {
        iMediaRequest *req = (iMediaRequest *) i.object;
        if (req->linkId == linkId) {
            remove_ObjectListIterator(&i);
            break;
        }
    }
}

static iBool requestMedia_DocumentWidget_(iDocumentWidget *d, iGmLinkId linkId, iBool enableFilters) {
    if (!findMediaRequest_DocumentWidget_(d, linkId)) {
        const iString *mediaUrl = absoluteUrl_String(d->mod.url, linkUrl_GmDocument(d->view.doc, linkId));
        pushBack_ObjectList(d->media, iClob(new_MediaRequest(d, linkId, mediaUrl, enableFilters)));
        invalidate_DocumentWidget_(d);
        return iTrue;
    }
    return iFalse;
}

static iBool isDownloadRequest_DocumentWidget(const iDocumentWidget *d, const iMediaRequest *req) {
    return findMediaForLink_Media(constMedia_GmDocument(d->view.doc), req->linkId, download_MediaType).type != 0;
}

static iBool handleMediaCommand_DocumentWidget_(iDocumentWidget *d, const char *cmd) {
    iMediaRequest *req = pointerLabel_Command(cmd, "request");
    iBool isOurRequest = iFalse;
    /* This request may already be deleted so treat the pointer with caution. */
    iConstForEach(ObjectList, m, d->media) {
        if (m.object == req) {
            isOurRequest = iTrue;
            break;
        }
    }
    if (!isOurRequest) {
        return iFalse;
    }
    if (equal_Command(cmd, "media.updated")) {
        /* Pass new data to media players. */
        const enum iGmStatusCode code = status_GmRequest(req->req);
        if (isSuccess_GmStatusCode(code)) {
            iGmResponse *resp = lockResponse_GmRequest(req->req);
            if (isDownloadRequest_DocumentWidget(d, req) ||
                startsWith_String(&resp->meta, "audio/")) {
                /* TODO: Use a helper? This is same as below except for the partialData flag. */
                if (setData_Media(media_GmDocument(d->view.doc),
                                  req->linkId,
                                  &resp->meta,
                                  &resp->body,
                                  partialData_MediaFlag | allowHide_MediaFlag)) {
                    redoLayout_GmDocument(d->view.doc);
                }
                updateVisible_DocumentView_(&d->view);
                invalidate_DocumentWidget_(d);
                refresh_Widget(as_Widget(d));
            }
            unlockResponse_GmRequest(req->req);
        }
        /* Update the link's progress. */
        invalidateLink_DocumentView_(&d->view, req->linkId);
        refresh_Widget(d);
        return iTrue;
    }
    else if (equal_Command(cmd, "media.finished")) {
        const enum iGmStatusCode code = status_GmRequest(req->req);
        /* Give the media to the document for presentation. */
        if (isSuccess_GmStatusCode(code)) {
            if (isDownloadRequest_DocumentWidget(d, req) ||
                startsWith_String(meta_GmRequest(req->req), "image/") ||
                startsWith_String(meta_GmRequest(req->req), "audio/")) {
                setData_Media(media_GmDocument(d->view.doc),
                              req->linkId,
                              meta_GmRequest(req->req),
                              body_GmRequest(req->req),
                              allowHide_MediaFlag);
                redoLayout_GmDocument(d->view.doc);
                iZap(d->view.visibleRuns); /* pointers invalidated */
                updateVisible_DocumentView_(&d->view);
                invalidate_DocumentWidget_(d);
                refresh_Widget(as_Widget(d));
            }
        }
        else {
            const iGmError *err = get_GmError(code);
            makeSimpleMessage_Widget(format_CStr(uiTextCaution_ColorEscape "%s", err->title), err->info);
            removeMediaRequest_DocumentWidget_(d, req->linkId);
        }
        return iTrue;
    }
    return iFalse;
}

static iBool fetchNextUnfetchedImage_DocumentWidget_(iDocumentWidget *d) {
    iConstForEach(PtrArray, i, &d->view.visibleLinks) {
        const iGmRun *run = i.ptr;
        if (run->linkId && run->mediaType == none_MediaType &&
            ~run->flags & decoration_GmRunFlag) {
            const int linkFlags = linkFlags_GmDocument(d->view.doc, run->linkId);
            if (isMediaLink_GmDocument(d->view.doc, run->linkId) &&
                linkFlags & imageFileExtension_GmLinkFlag &&
                ~linkFlags & content_GmLinkFlag && ~linkFlags & permanent_GmLinkFlag ) {
                if (requestMedia_DocumentWidget_(d, run->linkId, iTrue)) {
                    return iTrue;
                }
            }
        }
    }
    return iFalse;
}

static iBool saveToFile_(const iString *savePath, const iBlock *content, iBool showDialog) {
    iBool ok = iFalse;
    /* Write the file. */ {
        iFile *f = new_File(savePath);
        if (open_File(f, writeOnly_FileMode)) {
            write_File(f, content);
            close_File(f);
            const size_t size   = size_Block(content);
            const iBool  isMega = size >= 1000000;
#if defined (iPlatformAppleMobile)
            exportDownloadedFile_iOS(savePath);
#elif defined (iPlatformAndroidMobile)
            exportDownloadedFile_Android(savePath);
#else
            if (showDialog) {
                const iMenuItem items[2] = {
                    { "${dlg.save.opendownload}", 0, 0,
                        format_CStr("!open url:%s", cstrCollect_String(makeFileUrl_String(savePath))) },
                    { "${dlg.message.ok}", 0, 0, "message.ok" },
                };
                makeMessage_Widget(uiHeading_ColorEscape "${heading.save}",
                                       format_CStr("%s\n${dlg.save.size} %.3f %s",
                                                   cstr_String(path_File(f)),
                                               isMega ? size / 1.0e6f : (size / 1.0e3f),
                                               isMega ? "${mb}" : "${kb}"),
                                       items,
                                       iElemCount(items));
            }
#endif
            ok = iTrue;
        }
        else {
            makeSimpleMessage_Widget(uiTextCaution_ColorEscape "${heading.save.error}",
                                     strerror(errno));
        }
        iRelease(f);
    }
    return ok;
}

static const iString *saveToDownloads_(const iString *url, const iString *mime, const iBlock *content,
                                       iBool showDialog) {
    const iString *savePath = downloadPathForUrl_App(url, mime);
    if (!saveToFile_(savePath, content, showDialog)) {
        return collectNew_String();
    }
    return savePath;
}

static void addAllLinks_(void *context, const iGmRun *run) {
    iPtrArray *links = context;
    if (~run->flags & decoration_GmRunFlag && run->linkId) {
        pushBack_PtrArray(links, run);
    }
}

static iBool handlePinch_DocumentWidget_(iDocumentWidget *d, const char *cmd) {
    if (equal_Command(cmd, "pinch.began")) {
        d->pinchZoomInitial = d->pinchZoomPosted = prefs_App()->zoomPercent;
        d->flags |= pinchZoom_DocumentWidgetFlag;
        refresh_Widget(d);
    }
    else if (equal_Command(cmd, "pinch.moved")) {
        const float rel = argf_Command(cmd);
        int zoom = iRound(d->pinchZoomInitial * rel / 5.0f) * 5;
        zoom = iClamp(zoom, 50, 200);
        if (d->pinchZoomPosted != zoom) {
#if defined (iPlatformAppleMobile)
            if (zoom == 100) {
                playHapticEffect_iOS(tap_HapticEffect);
            }
#endif
            d->pinchZoomPosted = zoom;
            postCommandf_App("zoom.set arg:%d", zoom);
        }
    }
    else if (equal_Command(cmd, "pinch.ended")) {
        d->flags &= ~pinchZoom_DocumentWidgetFlag;
        refresh_Widget(d);
    }
    return iTrue;
}

static void swap_DocumentWidget_(iDocumentWidget *d, iGmDocument *doc,
                                 iDocumentWidget *swapBuffersWith) {
    if (doc) {
        iAssert(isInstance_Object(doc, &Class_GmDocument));
        replaceDocument_DocumentWidget_(d, doc);
        iSwap(iBanner *, d->banner, swapBuffersWith->banner);
        setOwner_Banner(d->banner, d);
        setOwner_Banner(swapBuffersWith->banner, swapBuffersWith);
        swap_DocumentView_(&d->view, &swapBuffersWith->view);
//        invalidate_DocumentWidget_(swapBuffersWith);
    }
}

static iWidget *swipeParent_DocumentWidget_(iDocumentWidget *d) {
    return findChild_Widget(as_Widget(d)->root->widget, "doctabs");
}

static void setupSwipeOverlay_DocumentWidget_(iDocumentWidget *d, iWidget *overlay) {
    iWidget *w = as_Widget(d);
    iWidget *swipeParent = swipeParent_DocumentWidget_(d);
    iAssert(overlay);
    /* The target takes the old document and jumps on top. */
    overlay->rect.pos = windowToInner_Widget(swipeParent, innerToWindow_Widget(w, zero_I2()));
    /* Note: `innerToWindow_Widget` does not apply visual offset. */
    overlay->rect.size = w->rect.size;
    setFlags_Widget(overlay, fixedPosition_WidgetFlag | fixedSize_WidgetFlag, iTrue);
    setFlags_Widget(as_Widget(d), refChildrenOffset_WidgetFlag, iTrue);
    as_Widget(d)->offsetRef = swipeParent;
    /* `overlay` animates off the screen to the right. */
    const int fromPos = value_Anim(&w->visualOffset);
    const int toPos   = width_Widget(overlay);
    setVisualOffset_Widget(overlay, fromPos, 0, 0);
    /* Bigger screen, faster swipes. */
    if (deviceType_App() == desktop_AppDeviceType) {
        setVisualOffset_Widget(overlay, toPos, 250, easeOut_AnimFlag | softer_AnimFlag);
    }
    else {
        const float devFactor = (deviceType_App() == phone_AppDeviceType ? 1.0f : 2.0f);
        float swipe = iClamp(d->swipeSpeed, devFactor * 400, devFactor * 1000) * gap_UI;
        uint32_t span = ((toPos - fromPos) / swipe) * 1000;
    //    printf("from:%d to:%d swipe:%f span:%u\n", fromPos, toPos, d->swipeSpeed, span);
        setVisualOffset_Widget(overlay, toPos, span, deviceType_App() == tablet_AppDeviceType ?
                               easeOut_AnimFlag : 0);
    }
    setVisualOffset_Widget(w, 0, 0, 0);
}

static iBool handleSwipe_DocumentWidget_(iDocumentWidget *d, const char *cmd) {
    /* TODO: Cleanup
     
       If DocumentWidget is refactored to split the document presentation from state
       and request management (a new DocumentView class), plain views could be used for this
       animation without having to mess with the complete state of the DocumentWidget. That
       seems like a less error-prone approach -- the current implementation will likely break
       down (again) if anything is changed in the document internals.
       
       2022-03-16: Yeah, something did break, again. "swipeout" is not found if the tab bar
       is moved to the bottom, when swiping back.
    */
    iWidget *w = as_Widget(d);
    /* The swipe animation is implemented in a rather complex way. It utilizes both cached
       GmDocument content and temporary underlay/overlay DocumentWidgets. Depending on the
       swipe direction, the DocumentWidget `d` may wait until the finger is released to actually
       perform the navigation action. */
    if (equal_Command(cmd, "edgeswipe.moved")) {
        //printf("[%p] responds to edgeswipe.moved\n", d);
        as_Widget(d)->offsetRef = NULL;
        const int side = argLabel_Command(cmd, "side");
        const int offset = arg_Command(cmd);
        if (side == 1) { /* left edge */
            if (atOldest_History(d->mod.history)) {
                return iTrue;
            }
            iWidget *swipeParent = swipeParent_DocumentWidget_(d);
            if (findChild_Widget(swipeParent, "swipeout")) {
                return iTrue; /* too fast, previous animation hasn't finished */
            }
            /* The temporary "swipein" will display the previous page until the finger is lifted. */
            iDocumentWidget *swipeIn = findChild_Widget(swipeParent, "swipein");
            if (!swipeIn) {
                swipeIn = new_DocumentWidget();
                swipeIn->flags |= animationPlaceholder_DocumentWidgetFlag;
                setId_Widget(as_Widget(swipeIn), "swipein");
                setFlags_Widget(as_Widget(swipeIn),
                                disabled_WidgetFlag | refChildrenOffset_WidgetFlag |
                                fixedPosition_WidgetFlag | fixedSize_WidgetFlag, iTrue);
                setFlags_Widget(findChild_Widget(as_Widget(swipeIn), "scroll"), hidden_WidgetFlag, iTrue);
                swipeIn->widget.rect.pos = windowToInner_Widget(swipeParent, localToWindow_Widget(w, w->rect.pos));
                swipeIn->widget.rect.size = d->widget.rect.size;
                swipeIn->widget.offsetRef = parent_Widget(w);
                /* Use a cached document for the layer underneath. */ {
                    lock_History(d->mod.history);
                    iRecentUrl *recent = precedingLocked_History(d->mod.history);
                    if (recent && recent->cachedResponse) {
                        setUrl_DocumentWidget_(swipeIn, &recent->url);
                        updateFromCachedResponse_DocumentWidget_(swipeIn,
                                                                 recent->normScrollY,
                                                                 recent->cachedResponse,
                                                                 recent->cachedDoc);
                        parseUser_DocumentWidget_(swipeIn);
                        updateBanner_DocumentWidget_(swipeIn);
                    }
                    else {
                        setUrlAndSource_DocumentWidget(swipeIn, &recent->url,
                                                       collectNewCStr_String("text/gemini"),
                                                       collect_Block(new_Block(0)));
                    }
                    unlock_History(d->mod.history);
                }
                addChildPos_Widget(swipeParent, iClob(swipeIn), front_WidgetAddPos);
            }
        }
        if (side == 2) { /* right edge */
            if (offset < -get_Window()->pixelRatio * 10) {
                int animSpan = 10;
                if (!atNewest_History(d->mod.history) && ~flags_Widget(w) & dragged_WidgetFlag) {
                    iWidget *swipeParent = swipeParent_DocumentWidget_(d);
                    if (findChild_Widget(swipeParent, "swipeout")) {
                        return iTrue; /* too fast, previous animation hasn't finished */
                    }
                    /* Setup the drag. `d` will be moving with the finger. */
                    animSpan = 0;
                    postCommand_Widget(d, "navigate.forward");
                    setFlags_Widget(w, dragged_WidgetFlag, iTrue);
                    /* Set up the swipe dummy. */
                    iDocumentWidget *target = new_DocumentWidget();
                    target->flags |= animationPlaceholder_DocumentWidgetFlag;
                    setId_Widget(as_Widget(target), "swipeout");
                    /* "swipeout" takes `d`'s document and goes underneath. */
                    target->widget.rect.pos = windowToInner_Widget(swipeParent, localToWindow_Widget(w, w->rect.pos));
                    target->widget.rect.size = d->widget.rect.size;
                    setFlags_Widget(as_Widget(target), fixedPosition_WidgetFlag | fixedSize_WidgetFlag, iTrue);
                    swap_DocumentWidget_(target, d->view.doc, d);
                    addChildPos_Widget(swipeParent, iClob(target), front_WidgetAddPos);
                    setFlags_Widget(as_Widget(target), refChildrenOffset_WidgetFlag, iTrue);
                    as_Widget(target)->offsetRef = parent_Widget(w);
                    /* Mark it for deletion after animation finishes. */
                    destroy_Widget(as_Widget(target));
                    /* The `d` document will now navigate forward and be replaced with a cached
                       copy. However, if a cached response isn't available, we'll need to show a
                       blank page. */
                    setUrlAndSource_DocumentWidget(d,
                                                   collectNewCStr_String("about:blank"),
                                                   collectNewCStr_String("text/gemini"),
                                                   collect_Block(new_Block(0)));
                }
                if (flags_Widget(w) & dragged_WidgetFlag) {
                    setVisualOffset_Widget(w, width_Widget(w) +
                                           width_Widget(d) * offset / size_Root(w->root).x,
                                           animSpan, 0);
                }
                else {
                    setVisualOffset_Widget(w, offset / 4, animSpan, 0);
                }
            }
            return iTrue;
        }
    }
    if (equal_Command(cmd, "edgeswipe.ended") && argLabel_Command(cmd, "side") == 2) {
        if (argLabel_Command(cmd, "abort") && flags_Widget(w) & dragged_WidgetFlag) {
            setFlags_Widget(w, dragged_WidgetFlag, iFalse);
            postCommand_Widget(d, "navigate.back");
            /* We must now undo the swap that was done when the drag started. */
            /* TODO: Currently not animated! What exactly is the appropriate thing to do here? */
            iWidget *swipeParent = swipeParent_DocumentWidget_(d);
            iDocumentWidget *swipeOut = findChild_Widget(swipeParent, "swipeout");
            swap_DocumentWidget_(d, swipeOut->view.doc, swipeOut);
//            const int visOff = visualOffsetByReference_Widget(w);
            w->offsetRef = NULL;
//            setVisualOffset_Widget(w, visOff, 0, 0);
//            setVisualOffset_Widget(w, 0, 150, 0);
            setVisualOffset_Widget(w, 0, 0, 0);
            /* Make it an overlay instead. */
//            removeChild_Widget(swipeParent, swipeOut);
//            addChildPos_Widget(swipeParent, iClob(swipeOut), back_WidgetAddPos);
//            setupSwipeOverlay_DocumentWidget_(d, as_Widget(swipeOut));
            return iTrue;
        }
        iAssert(~d->flags & animationPlaceholder_DocumentWidgetFlag);
        setFlags_Widget(w, dragged_WidgetFlag, iFalse);
        setVisualOffset_Widget(w, 0, 250, easeOut_AnimFlag | softer_AnimFlag);
        return iTrue;
    }
    if (equal_Command(cmd, "edgeswipe.ended") && argLabel_Command(cmd, "side") == 1) {
        iWidget *swipeParent = swipeParent_DocumentWidget_(d);
        iDocumentWidget *swipeIn = findChild_Widget(swipeParent, "swipein");
        d->swipeSpeed = argLabel_Command(cmd, "speed") / gap_UI;
        /* "swipe.back" will soon follow. The `d` document will do the actual back navigation,
            switching immediately to a cached page. However, if one is not available, we'll need
            to show a blank page for a while. */
        if (swipeIn) {
            if (!argLabel_Command(cmd, "abort")) {
                iWidget *swipeParent = swipeParent_DocumentWidget_(d);
                /* What was being shown in the `d` document is now being swapped to
                   the outgoing page animation. */
                iDocumentWidget *target = new_DocumentWidget();
                target->flags |= animationPlaceholder_DocumentWidgetFlag;
                addChildPos_Widget(swipeParent, iClob(target), back_WidgetAddPos);
                setId_Widget(as_Widget(target), "swipeout");
                setFlags_Widget(as_Widget(target), disabled_WidgetFlag, iTrue);
                swap_DocumentWidget_(target, d->view.doc, d);
                setUrlAndSource_DocumentWidget(d,
                                               swipeIn->mod.url,
                                               collectNewCStr_String("text/gemini"),
                                               collect_Block(new_Block(0)));
                as_Widget(swipeIn)->offsetRef = NULL;
            }
            destroy_Widget(as_Widget(swipeIn));
        }
    }
    if (equal_Command(cmd, "swipe.back")) {
        iWidget *swipeParent = swipeParent_DocumentWidget_(d);
        iDocumentWidget *target = findChild_Widget(swipeParent, "swipeout");
        if (atOldest_History(d->mod.history)) {
            setVisualOffset_Widget(w, 0, 100, 0);
            if (target) {
                destroy_Widget(as_Widget(target)); /* didn't need it after all */
            }
            return iTrue;
        }
        setupSwipeOverlay_DocumentWidget_(d, as_Widget(target));
        destroy_Widget(as_Widget(target)); /* will be actually deleted after animation finishes */
        postCommand_Widget(d, "navigate.back");
        return iTrue;
    }
    return iFalse;
}

static iBool cancelRequest_DocumentWidget_(iDocumentWidget *d, iBool postBack) {
    if (d->request) {
        iWidget *w = as_Widget(d);
        postCommandf_Root(w->root,
                          "document.request.cancelled doc:%p url:%s", d, cstr_String(d->mod.url));
        iReleasePtr(&d->request);
        if (d->state != ready_RequestState) {
            d->state = ready_RequestState;
            if (postBack) {
                postCommand_Root(w->root, "navigate.back");
            }
        }
        updateFetchProgress_DocumentWidget_(d);
        return iTrue;
    }
    return iFalse;
}

static const int smoothDuration_DocumentWidget_(enum iScrollType type) {
    return 600 /* milliseconds */ * scrollSpeedFactor_Prefs(prefs_App(), type);
}

static iBool handleCommand_DocumentWidget_(iDocumentWidget *d, const char *cmd) {
    iWidget *w = as_Widget(d);
    if (equal_Command(cmd, "document.openurls.changed")) {
        if (d->flags & animationPlaceholder_DocumentWidgetFlag) {
            return iFalse;
        }
        /* When any tab changes its document URL, update the open link indicators. */
        if (updateOpenURLs_GmDocument(d->view.doc)) {
            invalidate_DocumentWidget_(d);
            refresh_Widget(d);
        }
        return iFalse;
    }
    if (equal_Command(cmd, "visited.changed")) {
        updateVisitedLinks_GmDocument(d->view.doc);
        invalidateVisibleLinks_DocumentView_(&d->view);
        return iFalse;
    }
    if (equal_Command(cmd, "document.render")) /* `Periodic` makes direct dispatch to here */ {
//        printf("%u: document.render\n", SDL_GetTicks());
        if (SDL_GetTicks() - d->view.drawBufs->lastRenderTime > 150) {
            remove_Periodic(periodic_App(), d);
            /* Scrolling has stopped, begin filling up the buffer. */
            if (d->view.visBuf->buffers[0].texture) {
                addTicker_App(prerender_DocumentWidget_, d);
            }
        }
        return iTrue;
    }
    else if (equal_Command(cmd, "window.resized") || equal_Command(cmd, "font.changed") ||
             equal_Command(cmd, "keyroot.changed")) {
        if (equal_Command(cmd, "font.changed")) {
            invalidateCachedLayout_History(d->mod.history);
        }
        /* Alt/Option key may be involved in window size changes. */
        setLinkNumberMode_DocumentWidget_(d, iFalse);
        d->phoneToolbar = findWidget_App("bottombar");
        const iBool keepCenter = equal_Command(cmd, "font.changed");
        updateDocumentWidthRetainingScrollPosition_DocumentView_(&d->view, keepCenter);
        d->view.drawBufs->flags |= updateSideBuf_DrawBufsFlag;
        updateVisible_DocumentView_(&d->view);
        invalidate_DocumentWidget_(d);
        dealloc_VisBuf(d->view.visBuf);
        updateWindowTitle_DocumentWidget_(d);
        showOrHideIndicators_DocumentWidget_(d);
        refresh_Widget(w);
    }
    else if (equal_Command(cmd, "window.focus.lost")) {
        if (d->flags & showLinkNumbers_DocumentWidgetFlag) {
            setLinkNumberMode_DocumentWidget_(d, iFalse);
            invalidateVisibleLinks_DocumentView_(&d->view);
            refresh_Widget(w);
        }
        return iFalse;
    }
    else if (equal_Command(cmd, "window.mouse.exited")) {
        return iFalse;
    }
    else if (equal_Command(cmd, "theme.changed")) {
        invalidatePalette_GmDocument(d->view.doc);
        invalidateTheme_History(d->mod.history); /* forget cached color palettes */
        if (document_App() == d) {
            updateTheme_DocumentWidget_(d);
            updateVisible_DocumentView_(&d->view);
            updateTrust_DocumentWidget_(d, NULL);
            d->view.drawBufs->flags |= updateSideBuf_DrawBufsFlag;
            invalidate_DocumentWidget_(d);
            refresh_Widget(w);
        }
    }
    else if (equal_Command(cmd, "document.layout.changed") && document_Root(get_Root()) == d) {
        if (argLabel_Command(cmd, "redo")) {
            redoLayout_GmDocument(d->view.doc);
        }
        updateSize_DocumentWidget(d);
    }
    else if (equal_Command(cmd, "pinsplit.set")) {
        postCommand_App("document.update.pin"); /* prefs value not set yet */
        return iFalse;
    }
    else if (equal_Command(cmd, "document.update.pin")) {
        showOrHideIndicators_DocumentWidget_(d);
        return iFalse;
    }
    else if (equal_Command(cmd, "tabs.changed")) {
        setLinkNumberMode_DocumentWidget_(d, iFalse);
        if (cmp_String(id_Widget(w), suffixPtr_Command(cmd, "id")) == 0) {
            /* Set palette for our document. */
            updateTheme_DocumentWidget_(d);
            updateTrust_DocumentWidget_(d, NULL);
            updateSize_DocumentWidget(d);
            showOrHideIndicators_DocumentWidget_(d);
            updateFetchProgress_DocumentWidget_(d);
            updateHover_Window(window_Widget(w));
        }
        init_Anim(&d->view.sideOpacity, 0);
        init_Anim(&d->view.altTextOpacity, 0);
        updateSideOpacity_DocumentView_(&d->view, iFalse);
        updateWindowTitle_DocumentWidget_(d);
        allocVisBuffer_DocumentView_(&d->view);
        animateMedia_DocumentWidget_(d);
        remove_Periodic(periodic_App(), d);
        removeTicker_App(prerender_DocumentWidget_, d);
        return iFalse;
    }
    else if (equal_Command(cmd, "tab.created")) {
        /* Space for tab buttons has changed. */
        updateWindowTitle_DocumentWidget_(d);
        return iFalse;
    }
    else if (equal_Command(cmd, "document.select") && d == document_App()) {
        /* Touch selection mode. */
        if (!arg_Command(cmd)) {
            d->selectMark = iNullRange;
            setFlags_Widget(w, touchDrag_WidgetFlag, iFalse);
            setFadeEnabled_ScrollWidget(d->scroll, iTrue);
        }
        else {
            setFlags_Widget(w, touchDrag_WidgetFlag, iTrue);
            d->flags |= movingSelectMarkEnd_DocumentWidgetFlag |
                        selectWords_DocumentWidgetFlag; /* finger-based selection is imprecise */
            d->flags &= ~selectLines_DocumentWidgetFlag;
            setFadeEnabled_ScrollWidget(d->scroll, iFalse);
            d->selectMark = sourceLoc_DocumentView_(&d->view, d->contextPos);
            extendRange_Rangecc(&d->selectMark, range_String(source_GmDocument(d->view.doc)),
                                word_RangeExtension | bothStartAndEnd_RangeExtension);
            d->initialSelectMark = d->selectMark;
        }
        return iTrue;
    }
    else if (equal_Command(cmd, "document.info") && d == document_App()) {
        const char *unchecked       = red_ColorEscape "\u2610";
        const char *checked         = green_ColorEscape "\u2611";
        const iBool haveFingerprint = (d->certFlags & haveFingerprint_GmCertFlag) != 0;
        const int   requiredForTrust =
            (available_GmCertFlag | haveFingerprint_GmCertFlag | timeVerified_GmCertFlag);
        const iBool canTrust = ~d->certFlags & trusted_GmCertFlag &&
                               ((d->certFlags & requiredForTrust) == requiredForTrust);
        const iRecentUrl *recent = constMostRecentUrl_History(d->mod.history);
        const iString    *meta   = &d->sourceMime;
        if (recent && recent->cachedResponse) {
            meta = &recent->cachedResponse->meta;
        }
        iString *msg = collectNew_String();
        if (isEmpty_String(&d->sourceHeader)) {
            appendFormat_String(msg,
                                "%s\n%s\n",
                                cstr_String(meta),
                                formatCStrs_Lang("num.bytes.n", size_Block(&d->sourceContent)));
        }
        else {
            appendFormat_String(msg, "%s\n", cstr_String(&d->sourceHeader));
            if (size_Block(&d->sourceContent)) {
                appendFormat_String(
                    msg, "%s\n", formatCStrs_Lang("num.bytes.n", size_Block(&d->sourceContent)));
            }
        }
        if (equalCase_Rangecc(urlScheme_String(d->mod.url), "gemini")) {
            appendFormat_String(
                msg,
                "\n%s${pageinfo.cert.status}\n"
                "%s%s  %s\n"
                "%s%s  %s%s\n"
                "%s%s  %s (%04d-%02d-%02d %02d:%02d:%02d)\n"
                "%s%s  %s",
                uiHeading_ColorEscape,
                d->certFlags & authorityVerified_GmCertFlag ? checked
                                                            : uiText_ColorEscape "\u2610",
                uiText_ColorEscape,
                d->certFlags & authorityVerified_GmCertFlag ? "${pageinfo.cert.ca.verified}"
                                                            : "${pageinfo.cert.ca.unverified}",
                d->certFlags & domainVerified_GmCertFlag ? checked : unchecked,
                uiText_ColorEscape,
                d->certFlags & domainVerified_GmCertFlag ? "${pageinfo.domain.match}"
                                                         : "${pageinfo.domain.mismatch}",
                ~d->certFlags & domainVerified_GmCertFlag
                    ? format_CStr(" (%s)", cstr_String(d->certSubject))
                    : "",
                d->certFlags & timeVerified_GmCertFlag ? checked : unchecked,
                uiText_ColorEscape,
                d->certFlags & timeVerified_GmCertFlag ? "${pageinfo.cert.notexpired}"
                                                       : "${pageinfo.cert.expired}",
                d->certExpiry.year,
                d->certExpiry.month,
                d->certExpiry.day,
                d->certExpiry.hour,
                d->certExpiry.minute,
                d->certExpiry.second,
                d->certFlags & trusted_GmCertFlag ? checked : unchecked,
                uiText_ColorEscape,
                d->certFlags & trusted_GmCertFlag ? "${pageinfo.cert.trusted}"
                                                  : "${pageinfo.cert.untrusted}");
        }
        setFocus_Widget(NULL);
        iArray *items = new_Array(sizeof(iMenuItem));
        if (canTrust) {
            pushBack_Array(items,
                           &(iMenuItem){ uiTextAction_ColorEscape "${dlg.cert.trust}",
                                         SDLK_u,
                                         KMOD_PRIMARY | KMOD_SHIFT,
                                         "server.trustcert" });
        }
        if (haveFingerprint) {
            pushBack_Array(items, &(iMenuItem){ "${dlg.cert.fingerprint}", 0, 0, "server.copycert" });
        }
        const iRangecc root = urlRoot_String(d->mod.url);
        if (!isEmpty_Range(&root)) {
            pushBack_Array(items, &(iMenuItem){ "${pageinfo.settings}", 0, 0, "document.sitespec" });
        }
        if (!isEmpty_Array(items)) {
            pushBack_Array(items, &(iMenuItem){ "---", 0, 0, 0 });
        }
        pushBack_Array(items, &(iMenuItem){ "${close}", 0, 0, "message.ok" });
        iWidget *dlg = makeQuestion_Widget(uiHeading_ColorEscape "${heading.pageinfo}",
                                           cstr_String(msg),
                                           data_Array(items),
                                           size_Array(items));
        delete_Array(items);
        arrange_Widget(dlg);
        addAction_Widget(dlg, SDLK_ESCAPE, 0, "message.ok");
        addAction_Widget(dlg, SDLK_SPACE, 0, "message.ok");
        return iTrue;
    }
    else if (equal_Command(cmd, "document.sitespec") && d == document_App()) {
        if (!findWidget_App("sitespec.palette")) {
            makeSiteSpecificSettings_Widget(d->mod.url);
        }
        return iTrue;
    }
    else if (equal_Command(cmd, "server.unexpire") && document_App() == d) {
        const iRangecc host = urlHost_String(d->mod.url);
        const uint16_t port = urlPort_String(d->mod.url);
        if (!isEmpty_Block(d->certFingerprint) && !isEmpty_Range(&host)) {
            iTime expiry;
            initCurrent_Time(&expiry);
            iTime oneHour; /* One hour is long enough for a single visit (?). */
            initSeconds_Time(&oneHour, 3600);
            add_Time(&expiry, &oneHour);
            iDate expDate;
            init_Date(&expDate, &expiry);
            setTrusted_GmCerts(certs_App(), host, port, d->certFingerprint, &expDate);
            postCommand_Widget(w, "navigate.reload");
        }
        return iTrue;
    }
    else if (equal_Command(cmd, "server.trustcert") && document_App() == d) {
        const iRangecc host = urlHost_String(d->mod.url);
        const uint16_t port = urlPort_String(d->mod.url);
        if (!isEmpty_Block(d->certFingerprint) && !isEmpty_Range(&host)) {
            setTrusted_GmCerts(certs_App(), host, port, d->certFingerprint, &d->certExpiry);
            postCommand_Widget(w, "navigate.reload");
        }
        return iTrue;
    }
    else if (equal_Command(cmd, "server.copycert") && document_App() == d) {
        SDL_SetClipboardText(cstrCollect_String(hexEncode_Block(d->certFingerprint)));
        return iTrue;
    }
    else if (equal_Command(cmd, "copy") && document_App() == d && !focus_Widget()) {
        iString *copied;
        if (d->selectMark.start) {
            iRangecc mark = d->selectMark;
            if (mark.start > mark.end) {
                iSwap(const char *, mark.start, mark.end);
            }
            copied = newRange_String(mark);
        }
        else {
            /* Full document. */
            copied = copy_String(source_GmDocument(d->view.doc));
        }
        if (argLabel_Command(cmd, "share")) {
#if defined (iPlatformAppleMobile)
            openTextActivityView_iOS(copied);
#endif
        }
        else {
            SDL_SetClipboardText(cstr_String(copied));
        }
        delete_String(copied);
        if (flags_Widget(w) & touchDrag_WidgetFlag) {
            postCommand_Widget(w, "document.select arg:0");
        }
        return iTrue;
    }
    else if (equal_Command(cmd, "document.copylink") && document_App() == d) {
        if (d->contextLink) {
            SDL_SetClipboardText(cstr_String(canonicalUrl_String(absoluteUrl_String(
                d->mod.url, linkUrl_GmDocument(d->view.doc, d->contextLink->linkId)))));
        }
        else {
            SDL_SetClipboardText(cstr_String(canonicalUrl_String(d->mod.url)));
        }
        return iTrue;
    }
    else if (equalWidget_Command(cmd, w, "document.downloadlink")) {
        if (d->contextLink) {
            const iGmLinkId linkId = d->contextLink->linkId;
            setUrl_Media(media_GmDocument(d->view.doc),
                         linkId,
                         download_MediaType,
                         linkUrl_GmDocument(d->view.doc, linkId));
            requestMedia_DocumentWidget_(d, linkId, iFalse /* no filters */);
            redoLayout_GmDocument(d->view.doc); /* inline downloader becomes visible */
            updateVisible_DocumentView_(&d->view);
            invalidate_DocumentWidget_(d);
            refresh_Widget(w);
        }
        return iTrue;
    }
    else if (equal_Command(cmd, "document.input.submit") && document_Command(cmd) == d) {
        postCommandf_Root(w->root,
                          /* use the `redirect:1` argument to cause the input query URL to be
                             replaced in History; we don't want to navigate onto it */
                          "open redirect:1 url:%s",
                          cstrCollect_String(makeQueryUrl_DocumentWidget_
                                             (d, collect_String(suffix_Command(cmd, "value")))));
        return iTrue;
    }
    else if (equal_Command(cmd, "valueinput.cancelled") &&
             equal_Rangecc(range_Command(cmd, "id"), "!document.input.submit") && document_App() == d) {
        postCommand_Root(get_Root(), "navigate.back");
        return iTrue;
    }
    else if (equalWidget_Command(cmd, w, "document.request.updated") &&
             id_GmRequest(d->request) == argU32Label_Command(cmd, "reqid")) {
        if (document_App() == d) {
            updateFetchProgress_DocumentWidget_(d);
        }
        checkResponse_DocumentWidget_(d);
        set_Atomic(&d->isRequestUpdated, iFalse); /* ready to be notified again */
        return iFalse;
    }
    else if (equalWidget_Command(cmd, w, "document.request.finished") &&
             id_GmRequest(d->request) == argU32Label_Command(cmd, "reqid")) {
        iChangeFlags(d->flags, fromCache_DocumentWidgetFlag | preventInlining_DocumentWidgetFlag,
                     iFalse);;
        set_Block(&d->sourceContent, body_GmRequest(d->request));
        if (!isSuccess_GmStatusCode(status_GmRequest(d->request))) {
            /* TODO: Why is this here? Can it be removed? */
            format_String(&d->sourceHeader,
                          "%s%s",
                          humanReadableStatusCode_(status_GmRequest(d->request)),
                          cstr_String(meta_GmRequest(d->request)));
        }
        updateFetchProgress_DocumentWidget_(d);
        checkResponse_DocumentWidget_(d);
        if (category_GmStatusCode(status_GmRequest(d->request)) == categorySuccess_GmStatusCode) {
            init_Anim(&d->view.scrollY.pos, d->initNormScrollY * pageHeight_DocumentView_(&d->view));
            /* TODO: unless user already scrolled! */
        }
        addBannerWarnings_DocumentWidget_(d);
        iChangeFlags(d->flags,
                     urlChanged_DocumentWidgetFlag | drawDownloadCounter_DocumentWidgetFlag,
                     iFalse);
        d->state = ready_RequestState;
        postProcessRequestContent_DocumentWidget_(d, iFalse);
        /* The response may be cached. */
        if (d->request) {
            iAssert(~d->flags & animationPlaceholder_DocumentWidgetFlag);
            iAssert(~d->flags & fromCache_DocumentWidgetFlag);
            if (!equal_Rangecc(urlScheme_String(d->mod.url), "about") &&
                (startsWithCase_String(meta_GmRequest(d->request), "text/") ||
                 !cmp_String(&d->sourceMime, mimeType_Gempub))) {
                setCachedResponse_History(d->mod.history, lockResponse_GmRequest(d->request));
                unlockResponse_GmRequest(d->request);
            }
        }
        iReleasePtr(&d->request);
        updateVisible_DocumentView_(&d->view);
        d->view.drawBufs->flags |= updateSideBuf_DrawBufsFlag;
        postCommandf_Root(w->root,
                          "document.changed doc:%p status:%d url:%s",
                          d,
                          d->sourceStatus,
                          cstr_String(d->mod.url));
        /* Check for a pending goto. */
        if (!isEmpty_String(&d->pendingGotoHeading)) {
            scrollToHeading_DocumentView_(&d->view, cstr_String(&d->pendingGotoHeading));
            clear_String(&d->pendingGotoHeading);
        }
        cacheDocumentGlyphs_DocumentWidget_(d);
        return iFalse;
    }
    else if (equal_Command(cmd, "document.translate") && d == document_App()) {
        if (!d->translation) {
            d->translation = new_Translation(d);
//            if (isUsingPanelLayout_Mobile()) {
                //const iRect safe = safeRect_Root(w->root);
                //d->translation->dlg->rect.pos = windowToLocal_Widget(w, zero_I2());
                //d->translation->dlg->rect.size = safe.size;
//            }
        }
        return iTrue;
    }
    else if (startsWith_CStr(cmd, "translation.") && d->translation) {
        const iBool wasHandled = handleCommand_Translation(d->translation, cmd);
        if (isFinished_Translation(d->translation)) {
            delete_Translation(d->translation);
            d->translation = NULL;
        }
        return wasHandled;
    }
    else if (equal_Command(cmd, "document.upload") && d == document_App()) {
        if (findChild_Widget(root_Widget(w), "upload")) {
            return iTrue; /* already open */
        }
        const iString *url = d->mod.url;
        if (hasLabel_Command(cmd, "url")) {
            url = collect_String(suffix_Command(cmd, "url"));
        }
        const iRangecc scheme = urlScheme_String(url);
        if (equalCase_Rangecc(scheme, "gemini") || equalCase_Rangecc(scheme, "titan") ||
            equalCase_Rangecc(scheme, "spartan")) {
            iUploadWidget *upload =
                new_UploadWidget(equalCase_Rangecc(scheme, "spartan") ? spartan_UploadProtocol
                                                                      : titan_UploadProtocol);
            setUrl_UploadWidget(upload, url);
            setResponseViewer_UploadWidget(upload, d);
            addChild_Widget(get_Root()->widget, iClob(upload));
            setupSheetTransition_Mobile(as_Widget(upload), iTrue);
            if (argLabel_Command(cmd, "copy") && isUtf8_Rangecc(range_Block(&d->sourceContent))) {
                iString text;
                initBlock_String(&text, &d->sourceContent);
                setText_UploadWidget(upload, &text);
                deinit_String(&text);
            }
            postRefresh_App();
        }
        return iTrue;
    }
    else if (equal_Command(cmd, "media.updated") || equal_Command(cmd, "media.finished")) {
        return handleMediaCommand_DocumentWidget_(d, cmd);
    }
#if defined (LAGRANGE_ENABLE_AUDIO)
    else if (equal_Command(cmd, "media.player.started")) {
        /* When one media player starts, pause the others that may be playing. */
        const iPlayer *startedPlr = pointerLabel_Command(cmd, "player");
        const iMedia * media  = media_GmDocument(d->view.doc);
        const size_t   num    = numAudio_Media(media);
        for (size_t id = 1; id <= num; id++) {
            iPlayer *plr = audioPlayer_Media(media, (iMediaId){ audio_MediaType, id });
            if (plr != startedPlr) {
                setPaused_Player(plr, iTrue);
            }
        }
    }
#endif
    else if (equal_Command(cmd, "media.player.update")) {
        updateMedia_DocumentWidget_(d);
        return iFalse;
    }
    else if (equal_Command(cmd, "document.stop") && document_App() == d) {
        if (cancelRequest_DocumentWidget_(d, iTrue /* navigate back */)) {
            return iTrue;
        }
    }
    else if (equalWidget_Command(cmd, w, "document.media.save")) {
        const iGmLinkId      linkId = argLabel_Command(cmd, "link");
        const iMediaRequest *media  = findMediaRequest_DocumentWidget_(d, linkId);
        if (media) {
            saveToDownloads_(url_GmRequest(media->req), meta_GmRequest(media->req),
                             body_GmRequest(media->req), iTrue);
        }
    }
    else if (equal_Command(cmd, "document.save") && document_App() == d) {
        if (d->request) {
            makeSimpleMessage_Widget(uiTextCaution_ColorEscape "${heading.save.incomplete}",
                                     "${dlg.save.incomplete}");
        }
        else if (!isEmpty_Block(&d->sourceContent)) {
            if (argLabel_Command(cmd, "extview")) {
                if (equalCase_Rangecc(urlScheme_String(d->mod.url), "file") &&
                    fileExists_FileInfo(collect_String(localFilePathFromUrl_String(d->mod.url)))) {
                    /* Already a file so just open it directly. */
                    postCommandf_Root(w->root, "!open default:1 url:%s", cstr_String(d->mod.url));
                }
                else {
                    const iString *tmpPath = temporaryPathForUrl_App(d->mod.url, &d->sourceMime);
                    if (saveToFile_(tmpPath, &d->sourceContent, iFalse)) {
                        postCommandf_Root(w->root, "!open default:1 url:%s",
                                          cstrCollect_String(makeFileUrl_String(tmpPath)));
                    }
                }
            }
            else {
                const iBool    doOpen   = argLabel_Command(cmd, "open");
                const iString *savePath = saveToDownloads_(d->mod.url, &d->sourceMime,
                                                           &d->sourceContent, !doOpen);
                if (!isEmpty_String(savePath) && doOpen) {
                    postCommandf_Root(
                        w->root, "!open url:%s", cstrCollect_String(makeFileUrl_String(savePath)));
                }
            }
        }
        return iTrue;
    }
    else if (equal_Command(cmd, "document.reload") && document_Command(cmd) == d) {
        d->initNormScrollY = normScrollPos_DocumentView_(&d->view);
        if (equalCase_Rangecc(urlScheme_String(d->mod.url), "titan")) {
            /* Reopen so the Upload dialog gets shown. */
            postCommandf_App("open url:%s", cstr_String(d->mod.url));
            return iTrue;
        }
        fetch_DocumentWidget_(d);
        return iTrue;
    }
    else if (equal_Command(cmd, "document.linkkeys") && document_App() == d) {
        if (argLabel_Command(cmd, "release")) {
            setLinkNumberMode_DocumentWidget_(d, iFalse);
        }
        else if (argLabel_Command(cmd, "more")) {
            if (d->flags & showLinkNumbers_DocumentWidgetFlag &&
                d->ordinalMode == homeRow_DocumentLinkOrdinalMode) {
                const size_t numKeys = iElemCount(homeRowKeys_);
                const iGmRun *last = lastVisibleLink_DocumentView_(&d->view);
                if (!last) {
                    d->ordinalBase = 0;
                }
                else {
                    d->ordinalBase += numKeys;
                    if (visibleLinkOrdinal_DocumentView_(&d->view, last->linkId) < d->ordinalBase) {
                        d->ordinalBase = 0;
                    }
                }
            }
            else if (~d->flags & showLinkNumbers_DocumentWidgetFlag) {
                d->ordinalMode = homeRow_DocumentLinkOrdinalMode;
                d->ordinalBase = 0;
                setLinkNumberMode_DocumentWidget_(d, iTrue);
            }
        }
        else {
            d->ordinalMode = arg_Command(cmd);
            d->ordinalBase = 0;
            setLinkNumberMode_DocumentWidget_(d, iTrue);
            iChangeFlags(d->flags, setHoverViaKeys_DocumentWidgetFlag,
                         argLabel_Command(cmd, "hover") != 0);
            iChangeFlags(d->flags, newTabViaHomeKeys_DocumentWidgetFlag,
                         argLabel_Command(cmd, "newtab") != 0);
        }
        invalidateVisibleLinks_DocumentView_(&d->view);
        refresh_Widget(d);
        return iTrue;
    }
    else if (equal_Command(cmd, "navigate.back") && document_App() == d) {
        if (d->request) {
            postCommandf_Root(w->root,
                "document.request.cancelled doc:%p url:%s", d, cstr_String(d->mod.url));
            iReleasePtr(&d->request);
            updateFetchProgress_DocumentWidget_(d);
        }
        goBack_History(d->mod.history);
        return iTrue;
    }
    else if (equal_Command(cmd, "navigate.forward") && document_App() == d) {
        goForward_History(d->mod.history);
        return iTrue;
    }
    else if (equal_Command(cmd, "navigate.parent") && document_App() == d) {
        iUrl parts;
        init_Url(&parts, d->mod.url);
        if (equalCase_Rangecc(parts.scheme, "gemini")) {
            /* Check for default index pages according to Gemini Best Practices ("Filenames"):
               gemini://gemini.circumlunar.space/docs/best-practices.gmi */
            if (endsWith_Rangecc(parts.path, "/index.gmi")) {
                parts.path.end -= 9; 
            }
            else if (endsWith_Rangecc(parts.path, "/index.gemini")) {
                parts.path.end -= 12;
            }
        }
        /* Remove the last path segment. */
        if (size_Range(&parts.path) > 1) {
            if (parts.path.end[-1] == '/') {
                parts.path.end--;
            }
            while (parts.path.end > parts.path.start) {
                if (parts.path.end[-1] == '/') break;
                parts.path.end--;
            }
            iString *parentUrl = collectNewRange_String((iRangecc){ constBegin_String(d->mod.url),
                                                                    parts.path.end });
            /* Always go to a gophermap. */
            setUrlItemType_Gopher(parentUrl, '1');
            /* Hierarchical navigation doesn't make sense with Titan. */
            if (startsWith_String(parentUrl, "titan://")) {
                /* We have no way of knowing if the corresponding URL is valid for Gemini,
                   but let's try anyway. */                
                set_String(parentUrl, withScheme_String(parentUrl, "gemini"));
                stripUrlPort_String(parentUrl);
            }
            if (!cmpCase_String(parentUrl, "about:")) {
                setCStr_String(parentUrl, "about:about");
            }
            postCommandf_Root(w->root, "open url:%s", cstr_String(parentUrl));
        }
        return iTrue;
    }
    else if (equal_Command(cmd, "navigate.root") && document_App() == d) {
        iString *rootUrl = collectNewRange_String(urlRoot_String(d->mod.url));
        /* Always go to a gophermap. */
        setUrlItemType_Gopher(rootUrl, '1');
        /* Hierarchical navigation doesn't make sense with Titan. */
        if (startsWith_String(rootUrl, "titan://")) {
            /* We have no way of knowing if the corresponding URL is valid for Gemini,
               but let's try anyway. */                
            set_String(rootUrl, withScheme_String(rootUrl, "gemini"));
            stripUrlPort_String(rootUrl);
        }
        if (!cmpCase_String(rootUrl, "about:")) {
            setCStr_String(rootUrl, "about:about");
        }
        else {
            appendCStr_String(rootUrl, "/");
        }
        postCommandf_Root(w->root, "open url:%s", cstr_String(rootUrl));
        return iTrue;
    }
    else if (equalWidget_Command(cmd, w, "scroll.moved")) {
        init_Anim(&d->view.scrollY.pos, arg_Command(cmd));
        updateVisible_DocumentView_(&d->view);
        return iTrue;
    }
    else if (equal_Command(cmd, "scroll.page") && document_App() == d) {
        const int dir = arg_Command(cmd);
        if (dir > 0 && !argLabel_Command(cmd, "repeat") &&
            prefs_App()->loadImageInsteadOfScrolling &&
            fetchNextUnfetchedImage_DocumentWidget_(d)) {
            return iTrue;
        }
        const float amount = argLabel_Command(cmd, "full") != 0 ? 1.0f : 0.5f;
        smoothScroll_DocumentView_(&d->view,
                                   dir * amount *
                                       height_Rect(documentBounds_DocumentView_(&d->view)),
                                   smoothDuration_DocumentWidget_(keyboard_ScrollType));
        return iTrue;
    }
    else if (equal_Command(cmd, "scroll.top") && document_App() == d) {
        if (argLabel_Command(cmd, "smooth")) {
            stopWidgetMomentum_Touch(w);
            smoothScroll_DocumentView_(&d->view, -pos_SmoothScroll(&d->view.scrollY), 500);
            d->view.scrollY.flags |= muchSofter_AnimFlag;
            return iTrue;
        }
        init_Anim(&d->view.scrollY.pos, 0);
        invalidate_VisBuf(d->view.visBuf);
        clampScroll_DocumentView_(&d->view);
        updateVisible_DocumentView_(&d->view);
        refresh_Widget(w);
        return iTrue;
    }
    else if (equal_Command(cmd, "scroll.bottom") && document_App() == d) {
        updateScrollMax_DocumentView_(&d->view); /* scrollY.max might not be fully updated */
        init_Anim(&d->view.scrollY.pos, d->view.scrollY.max);
        invalidate_VisBuf(d->view.visBuf);
        clampScroll_DocumentView_(&d->view);
        updateVisible_DocumentView_(&d->view);
        refresh_Widget(w);
        return iTrue;
    }
    else if (equal_Command(cmd, "scroll.step") && document_App() == d) {
        const int dir = arg_Command(cmd);
        if (dir > 0 && !argLabel_Command(cmd, "repeat") &&
            prefs_App()->loadImageInsteadOfScrolling &&
            fetchNextUnfetchedImage_DocumentWidget_(d)) {
            return iTrue;
        }
        smoothScroll_DocumentView_(&d->view,
                                   3 * lineHeight_Text(paragraph_FontId) * dir,
                                   smoothDuration_DocumentWidget_(keyboard_ScrollType));
        return iTrue;
    }
    else if (equal_Command(cmd, "document.goto") && document_App() == d) {
        const char *heading = suffixPtr_Command(cmd, "heading");
        if (heading) {
            if (isRequestOngoing_DocumentWidget(d)) {
                /* Scroll position set when request finishes. */
                setCStr_String(&d->pendingGotoHeading, heading);
                return iTrue;
            }
            scrollToHeading_DocumentView_(&d->view, heading);
            return iTrue;
        }
        const char *loc = pointerLabel_Command(cmd, "loc");
        const iGmRun *run = findRunAtLoc_GmDocument(d->view.doc, loc);
        if (run) {
            scrollTo_DocumentView_(&d->view, run->visBounds.pos.y, iFalse);
        }
        return iTrue;
    }
    else if ((equal_Command(cmd, "find.next") || equal_Command(cmd, "find.prev")) &&
             document_App() == d) {
        const int dir = equal_Command(cmd, "find.next") ? +1 : -1;
        iRangecc (*finder)(const iGmDocument *, const iString *, const char *) =
            dir > 0 ? findText_GmDocument : findTextBefore_GmDocument;
        iInputWidget *find = findWidget_App("find.input");
        if (isEmpty_String(text_InputWidget(find))) {
            d->foundMark = iNullRange;
        }
        else {
            const iBool wrap = d->foundMark.start != NULL;
            d->foundMark     = finder(d->view.doc, text_InputWidget(find), dir > 0 ? d->foundMark.end
                                                                          : d->foundMark.start);
            if (!d->foundMark.start && wrap) {
                /* Wrap around. */
                d->foundMark = finder(d->view.doc, text_InputWidget(find), NULL);
            }
            if (d->foundMark.start) {
                const iGmRun *found;
                if ((found = findRunAtLoc_GmDocument(d->view.doc, d->foundMark.start)) != NULL) {
                    scrollTo_DocumentView_(&d->view, mid_Rect(found->bounds).y, iTrue);
                }
            }
        }
        if (flags_Widget(w) & touchDrag_WidgetFlag) {
            postCommand_Root(w->root, "document.select arg:0"); /* we can't handle both at the same time */
        }
        invalidateWideRunsWithNonzeroOffset_DocumentView_(&d->view); /* markers don't support offsets */
        resetWideRuns_DocumentView_(&d->view);
        refresh_Widget(w);
        return iTrue;
    }
    else if (equal_Command(cmd, "find.clearmark")) {
        if (d->foundMark.start) {
            d->foundMark = iNullRange;
            refresh_Widget(w);
        }
        return iTrue;
    }
    else if (equal_Command(cmd, "bookmark.links") && document_App() == d) {
        iPtrArray *links = collectNew_PtrArray();
        render_GmDocument(d->view.doc, (iRangei){ 0, size_GmDocument(d->view.doc).y }, addAllLinks_, links);
        /* Find links that aren't already bookmarked. */
        iForEach(PtrArray, i, links) {
            const iGmRun *run = i.ptr;
            uint32_t      bmid;
            if ((bmid = findUrl_Bookmarks(bookmarks_App(),
                                          linkUrl_GmDocument(d->view.doc, run->linkId))) != 0) {
                const iBookmark *bm = get_Bookmarks(bookmarks_App(), bmid);
                /* We can import local copies of remote bookmarks. */
                if (~bm->flags & remote_BookmarkFlag) {
                    remove_PtrArrayIterator(&i);
                }
            }
        }
        if (!isEmpty_PtrArray(links)) {
            if (argLabel_Command(cmd, "confirm")) {
                const size_t count = size_PtrArray(links);
                makeQuestion_Widget(
                    uiHeading_ColorEscape "${heading.import.bookmarks}",
                    formatCStrs_Lang("dlg.import.found.n", count),
                    (iMenuItem[]){ { "${cancel}" },
                                   { format_CStr(cstrCount_Lang("dlg.import.add.n", (int) count),
                                                 uiTextAction_ColorEscape,
                                                 count),
                                     0,
                                     0,
                                     "bookmark.links" } },
                    2);
            }
            else {
                iConstForEach(PtrArray, j, links) {
                    const iGmRun *run = j.ptr;
                    add_Bookmarks(bookmarks_App(),
                                  linkUrl_GmDocument(d->view.doc, run->linkId),
                                  collect_String(newRange_String(run->text)),
                                  NULL,
                                  0x1f588 /* pin */);
                }
                postCommand_App("bookmarks.changed");
            }
        }
        else {
            makeSimpleMessage_Widget(uiHeading_ColorEscape "${heading.import.bookmarks}",
                                     "${dlg.import.notnew}");
        }
        return iTrue;
    }
    else if (equalWidget_Command(cmd, w, "menu.closed")) {
        updateHover_DocumentView_(&d->view, mouseCoord_Window(get_Window(), 0));
    }
    else if (equal_Command(cmd, "bookmarks.changed")) {
        showOrHideIndicators_DocumentWidget_(d);
    }
    else if (equal_Command(cmd, "document.autoreload")) {
        if (d->mod.reloadInterval) {
            if (!isValid_Time(&d->sourceTime) || elapsedSeconds_Time(&d->sourceTime) >=
                    seconds_ReloadInterval_(d->mod.reloadInterval)) {
                postCommand_Widget(w, "document.reload");
            }
        }
    }
    else if (equal_Command(cmd, "document.autoreload.menu") && document_App() == d) {
        iArray *items = collectNew_Array(sizeof(iMenuItem));
        for (int i = 0; i < max_ReloadInterval; ++i) {
            pushBack_Array(items, &(iMenuItem){
                format_CStr("%s%s", ((int) d->mod.reloadInterval == i ? "&" : "*"),
                                     label_ReloadInterval_(i)),
                0,
                0,
                format_CStr("document.autoreload.set arg:%d", i) });
        }
        pushBack_Array(items, &(iMenuItem){ "${cancel}", 0, 0, NULL });
        makeQuestion_Widget(uiTextAction_ColorEscape "${heading.autoreload}",
                            "${dlg.autoreload}",
                            constData_Array(items), size_Array(items));
        return iTrue;
    }
    else if (equal_Command(cmd, "document.autoreload.set") && document_App() == d) {
        d->mod.reloadInterval = arg_Command(cmd);
    }
    else if (equalWidget_Command(cmd, w, "document.dismiss")) {
        const iString *site = collectNewRange_String(urlRoot_String(d->mod.url));
        const int dismissed = value_SiteSpec(site, dismissWarnings_SiteSpecKey);
        const int arg = argLabel_Command(cmd, "warning");
        setValue_SiteSpec(site, dismissWarnings_SiteSpecKey, dismissed | arg);
        if (arg == ansiEscapes_GmDocumentWarning) {
            remove_Banner(d->banner, ansiEscapes_GmStatusCode);
            refresh_Widget(w);
        }
        return iTrue;
    }
    else if (startsWith_CStr(cmd, "pinch.") && document_Command(cmd) == d) {
        return handlePinch_DocumentWidget_(d, cmd);
    }
    else if ((startsWith_CStr(cmd, "edgeswipe.") || startsWith_CStr(cmd, "swipe.")) &&
             document_App() == d) {
        return handleSwipe_DocumentWidget_(d, cmd);
    }
    else if (equal_Command(cmd, "document.setmediatype") && document_App() == d) {
        if (!isRequestOngoing_DocumentWidget(d)) {
            setUrlAndSource_DocumentWidget(d, d->mod.url, string_Command(cmd, "mime"),
                                           &d->sourceContent);
        }
        return iTrue;
    }
    else if (equal_Command(cmd, "document.viewformat") && document_App() == d) {
        const iBool gemtext = hasLabel_Command(cmd, "arg")
                                  ? arg_Command(cmd) != 0 /* set to value */
                                  : (d->flags & viewSource_DocumentWidgetFlag) != 0; /* toggle */
        iChangeFlags(d->flags, viewSource_DocumentWidgetFlag, !gemtext);
        if (setViewFormat_GmDocument(
                d->view.doc, gemtext ? gemini_SourceFormat : plainText_SourceFormat)) {
            documentRunsInvalidated_DocumentWidget_(d);
            updateWidthAndRedoLayout_DocumentView_(&d->view);
            updateSize_DocumentWidget(d);
        }
        return iTrue;
    }
    else if (equal_Command(cmd, "fontpack.install") && document_App() == d) {
        if (argLabel_Command(cmd, "ttf")) {
            iAssert(!cmp_String(&d->sourceMime, "font/ttf"));
            installFontFile_Fonts(collect_String(suffix_Command(cmd, "name")), &d->sourceContent);
            postCommand_App("open switch:1 url:about:fonts");
        }
        else {
            const iString *id = idFromUrl_FontPack(d->mod.url);
            install_Fonts(id, &d->sourceContent);
            postCommandf_App("open gotoheading:%s url:about:fonts", cstr_String(id));
        }
        return iTrue;
    }
    else if (equal_Command(cmd, "contextkey") && document_App() == d) {
        if (!isTerminal_Platform()) {
            d->view.hoverLink = NULL;
        }
        emulateMouseClick_Widget(w, SDL_BUTTON_RIGHT);
        return iTrue;
    }
    return iFalse;
}

static void setGrabbedPlayer_DocumentWidget_(iDocumentWidget *d, const iGmRun *run) {
#if defined (LAGRANGE_ENABLE_AUDIO)
    if (run && run->mediaType == audio_MediaType) {
        iPlayer *plr = audioPlayer_Media(media_GmDocument(d->view.doc), mediaId_GmRun(run));
        setFlags_Player(plr, volumeGrabbed_PlayerFlag, iTrue);
        d->grabbedStartVolume = volume_Player(plr);
        d->grabbedPlayer      = run;
        refresh_Widget(d);
    }
    else if (d->grabbedPlayer) {
        setFlags_Player(
            audioPlayer_Media(media_GmDocument(d->view.doc), mediaId_GmRun(d->grabbedPlayer)),
            volumeGrabbed_PlayerFlag,
            iFalse);
        d->grabbedPlayer = NULL;
        refresh_Widget(d);
    }
    else {
        iAssert(iFalse);
    }
#endif    
}

static iBool processMediaEvents_DocumentWidget_(iDocumentWidget *d, const SDL_Event *ev) {
    if (ev->type != SDL_MOUSEBUTTONDOWN && ev->type != SDL_MOUSEBUTTONUP &&
        ev->type != SDL_MOUSEMOTION) {
        return iFalse;
    }
    if (d->grabbedPlayer) {
        /* Updated in the drag. */
        return iFalse;
    }
    const iInt2 mouse = init_I2(ev->button.x, ev->button.y);
    iConstForEach(PtrArray, i, &d->view.visibleMedia) {
        const iGmRun *run  = i.ptr;
        if (run->mediaType == download_MediaType) {
            iDownloadUI ui;
            init_DownloadUI(&ui, media_GmDocument(d->view.doc), mediaId_GmRun(run).id,
                            runRect_DocumentView_(&d->view, run));
            if (processEvent_DownloadUI(&ui, ev)) {
                return iTrue;
            }
            continue;
        }
        if (run->mediaType != audio_MediaType) {
            continue;
        }
#if defined (LAGRANGE_ENABLE_AUDIO)        
        if (ev->type == SDL_MOUSEBUTTONDOWN || ev->type == SDL_MOUSEBUTTONUP) {
            if (ev->button.button != SDL_BUTTON_LEFT) {
                return iFalse;
            }
        }
        /* TODO: move this to mediaui.c */
        const iRect rect = runRect_DocumentView_(&d->view, run);
        iPlayer *   plr  = audioPlayer_Media(media_GmDocument(d->view.doc), mediaId_GmRun(run));
        if (contains_Rect(rect, mouse)) {
            iPlayerUI ui;
            init_PlayerUI(&ui, plr, rect);
            if (ev->type == SDL_MOUSEBUTTONDOWN && flags_Player(plr) & adjustingVolume_PlayerFlag &&
                contains_Rect(adjusted_Rect(ui.volumeAdjustRect,
                                            zero_I2(),
                                            init_I2(-height_Rect(ui.volumeAdjustRect), 0)),
                              mouse)) {
                setGrabbedPlayer_DocumentWidget_(d, run);
                processEvent_Click(&d->click, ev);
                /* The rest is done in the DocumentWidget click responder. */
                refresh_Widget(d);
                return iTrue;
            }
            else if (ev->type == SDL_MOUSEBUTTONDOWN || ev->type == SDL_MOUSEMOTION) {
                refresh_Widget(d);
                return iTrue;
            }
            if (contains_Rect(ui.playPauseRect, mouse)) {
                setPaused_Player(plr, !isPaused_Player(plr));
                animateMedia_DocumentWidget_(d);
                return iTrue;
            }
            else if (contains_Rect(ui.rewindRect, mouse)) {
                if (isStarted_Player(plr) && time_Player(plr) > 0.5f) {
                    stop_Player(plr);
                    start_Player(plr);
                    setPaused_Player(plr, iTrue);
                }
                refresh_Widget(d);
                return iTrue;
            }
            else if (contains_Rect(ui.volumeRect, mouse)) {
                setFlags_Player(plr,
                                adjustingVolume_PlayerFlag,
                                !(flags_Player(plr) & adjustingVolume_PlayerFlag));
                animateMedia_DocumentWidget_(d);
                refresh_Widget(d);
                return iTrue;
            }
            else if (contains_Rect(ui.menuRect, mouse)) {
                /* TODO: Add menu items for:
                   - output device
                   - Save to Downloads
                */
                if (d->playerMenu) {
                    destroy_Widget(d->playerMenu);
                    d->playerMenu = NULL;
                    return iTrue;
                }
                d->playerMenu = makeMenu_Widget(
                    as_Widget(d),
                    (iMenuItem[]){
                        { cstrCollect_String(metadataLabel_Player(plr)) },
                    },
                    1);
                openMenu_Widget(d->playerMenu, bottomLeft_Rect(ui.menuRect));
                return iTrue;
            }
        }
#endif /* LAGRANGE_ENABLE_AUDIO */
    }
    return iFalse;
}

static void beginMarkingSelection_DocumentWidget_(iDocumentWidget *d, iInt2 pos) {
    setFocus_Widget(NULL); /* TODO: Focus this document? */
    invalidateWideRunsWithNonzeroOffset_DocumentView_(&d->view);
    resetWideRuns_DocumentView_(&d->view); /* Selections don't support horizontal scrolling. */
    iChangeFlags(d->flags, selecting_DocumentWidgetFlag, iTrue);
    d->initialSelectMark = d->selectMark = sourceLoc_DocumentView_(&d->view, pos);
    refresh_Widget(as_Widget(d));
}

static void interactingWithLink_DocumentWidget_(iDocumentWidget *d, iGmLinkId id) {
    iRangecc loc = linkUrlRange_GmDocument(d->view.doc, id);
    if (!loc.start) {
        clear_String(&d->linePrecedingLink);
        return;
    }
    d->requestLinkId = id;
    const char *start = range_String(source_GmDocument(d->view.doc)).start;
    /* Find the preceding line. This is offered as a prefill option for a possible input query. */
    while (loc.start > start && *loc.start != '\n') {
        loc.start--;
    }
    loc.end = loc.start; /* End of the preceding line. */
    if (loc.start > start) {
        loc.start--;
    }
    while (loc.start > start && *loc.start != '\n') {
        loc.start--;
    }
    if (*loc.start == '\n' && !isEmpty_Range(&loc)) {
        loc.start++; /* Start of the preceding line. */
    }
    setRange_String(&d->linePrecedingLink, loc);
}

static iBool isSpartanQueryLink_DocumentWidget_(const iDocumentWidget *d, iGmLinkId id) {
    const int linkFlags = linkFlags_GmDocument(d->view.doc, id);
    return equalCase_Rangecc(urlScheme_String(d->mod.url), "spartan") &&
                   (linkFlags & query_GmLinkFlag) &&
                   scheme_GmLinkFlag(linkFlags) == spartan_GmLinkScheme
               ? 1
               : 0;
}

iLocalDef int wheelSwipeSide_DocumentWidget_(const iDocumentWidget *d) {
    return (d->flags & rightWheelSwipe_DocumentWidgetFlag  ? 2
            : d->flags & leftWheelSwipe_DocumentWidgetFlag ? 1
                                                           : 0);
}

static void finishWheelSwipe_DocumentWidget_(iDocumentWidget *d) {
    if (d->flags & eitherWheelSwipe_DocumentWidgetFlag &&
        d->wheelSwipeState == direct_WheelSwipeState) {
        const int side = wheelSwipeSide_DocumentWidget_(d);
        int abort = ((side == 1 && d->swipeSpeed < 0) || (side == 2 && d->swipeSpeed > 0));
        if (iAbs(d->wheelSwipeDistance) < width_Widget(d) / 4 && iAbs(d->swipeSpeed) < 4 * gap_UI) {
            abort = 1;
        }
        postCommand_Widget(d, "edgeswipe.ended side:%d abort:%d", side, abort);
        d->flags &= ~eitherWheelSwipe_DocumentWidgetFlag;
    }
}

static iBool handleWheelSwipe_DocumentWidget_(iDocumentWidget *d, const SDL_MouseWheelEvent *ev) {
    iWidget *w = as_Widget(d);
    if (deviceType_App() != desktop_AppDeviceType) {
        return iFalse;
    }
    if (~flags_Widget(w) & horizontalOffset_WidgetFlag) {
        return iFalse;
    }
    iAssert(~d->flags & animationPlaceholder_DocumentWidgetFlag);
//    printf("STATE:%d wheel x:%d inert:%d end:%d\n", d->wheelSwipeState,
//           ev->x, isInertia_MouseWheelEvent(ev),
//           isScrollFinished_MouseWheelEvent(ev));
//    fflush(stdout);
    switch (d->wheelSwipeState) {
        case none_WheelSwipeState:
            /* A new swipe starts. */
            if (!isInertia_MouseWheelEvent(ev) && !isScrollFinished_MouseWheelEvent(ev)) {
                int side = ev->x > 0 ? 1 : 2;
                d->wheelSwipeDistance = ev->x * 2;
                d->flags &= ~eitherWheelSwipe_DocumentWidgetFlag;
                d->flags |= (side == 1 ? leftWheelSwipe_DocumentWidgetFlag
                                       : rightWheelSwipe_DocumentWidgetFlag);
                //        printf("swipe starts at %d, side %d\n", d->wheelSwipeDistance, side);
                d->wheelSwipeState = direct_WheelSwipeState;
                d->swipeSpeed = 0;
                postCommand_Widget(d, "edgeswipe.moved arg:%d side:%d", d->wheelSwipeDistance, side);
                return iTrue;
            }
            break;
        case direct_WheelSwipeState:
            if (isInertia_MouseWheelEvent(ev) || isScrollFinished_MouseWheelEvent(ev)) {
                finishWheelSwipe_DocumentWidget_(d);
                d->wheelSwipeState = none_WheelSwipeState;
            }
            else {
                int step = ev->x * 2;
                d->wheelSwipeDistance += step;
                /* Remember the maximum speed. */
                if (d->swipeSpeed < 0 && step < 0) {
                    d->swipeSpeed = iMin(d->swipeSpeed, step);
                }
                else if (d->swipeSpeed > 0 && step > 0) {
                    d->swipeSpeed = iMax(d->swipeSpeed, step);
                }
                else {
                    d->swipeSpeed = step;
                }
                switch (wheelSwipeSide_DocumentWidget_(d)) {
                    case 1:
                        d->wheelSwipeDistance = iMax(0, d->wheelSwipeDistance);
                        d->wheelSwipeDistance = iMin(width_Widget(d), d->wheelSwipeDistance);
                        break;
                    case 2:
                        d->wheelSwipeDistance = iMin(0, d->wheelSwipeDistance);
                        d->wheelSwipeDistance = iMax(-width_Widget(d), d->wheelSwipeDistance);
                        break;
                }
                /* TODO: calculate speed, rememeber direction */
                //printf("swipe moved to %d, side %d\n", d->wheelSwipeDistance, side);
                postCommand_Widget(d, "edgeswipe.moved arg:%d side:%d", d->wheelSwipeDistance,
                                   wheelSwipeSide_DocumentWidget_(d));
            }
            return iTrue;
    }
    return iFalse;
}

static iBool processEvent_DocumentWidget_(iDocumentWidget *d, const SDL_Event *ev) {
    iWidget       *w    = as_Widget(d);
    iDocumentView *view = &d->view;
    if (isMetricsChange_UserEvent(ev)) {
        updateSize_DocumentWidget(d);
    }
    else if (processEvent_SmoothScroll(&d->view.scrollY, ev)) {
        return iTrue;
    }
    else if (ev->type == SDL_USEREVENT && ev->user.code == command_UserEventCode) {
        if (isCommand_Widget(w, ev, "pullaction")) {
            postCommand_Widget(w, "navigate.reload");
            return iTrue;
        }
        if (!handleCommand_DocumentWidget_(d, command_UserEvent(ev))) {
            /* Base class commands. */
            return processEvent_Widget(w, ev);
        }
        return iTrue;
    }
    if (ev->type == SDL_KEYDOWN) {
        const int key = ev->key.keysym.sym;
        if ((d->flags & showLinkNumbers_DocumentWidgetFlag) &&
            ((key >= '1' && key <= '9') || (key >= 'a' && key <= 'z'))) {
            const size_t ord = linkOrdinalFromKey_DocumentWidget_(d, key) + d->ordinalBase;
            iConstForEach(PtrArray, i, &d->view.visibleLinks) {
                if (ord == iInvalidPos) break;
                const iGmRun *run = i.ptr;
                if (run->flags & decoration_GmRunFlag &&
                    visibleLinkOrdinal_DocumentView_(view, run->linkId) == ord) {
                    if (d->flags & setHoverViaKeys_DocumentWidgetFlag) {
                        view->hoverLink = run;
                        updateHoverLinkInfo_DocumentView_(view);
                    }
                    else {
                        postCommandf_Root(
                            w->root,
                            "open query:%d newtab:%d url:%s",
                            isSpartanQueryLink_DocumentWidget_(d, run->linkId),
                            (isPinned_DocumentWidget_(d) ? otherRoot_OpenTabFlag : 0) ^
                                (d->ordinalMode == numbersAndAlphabet_DocumentLinkOrdinalMode
                                     ? openTabMode_Sym(modState_Keys())
                                     : (d->flags & newTabViaHomeKeys_DocumentWidgetFlag ? 1 : 0)),
                            cstr_String(absoluteUrl_String(
                                d->mod.url, linkUrl_GmDocument(view->doc, run->linkId))));
                        interactingWithLink_DocumentWidget_(d, run->linkId);
                    }
                    setLinkNumberMode_DocumentWidget_(d, iFalse);
                    invalidateVisibleLinks_DocumentView_(view);
                    refresh_Widget(d);
                    return iTrue;
                }
            }
        }
        switch (key) {
            case SDLK_ESCAPE:
                if (d->flags & showLinkNumbers_DocumentWidgetFlag && document_App() == d) {
                    setLinkNumberMode_DocumentWidget_(d, iFalse);
                    invalidateVisibleLinks_DocumentView_(view);
                    refresh_Widget(d);
                    return iTrue;
                }
                break;
#if !defined (NDEBUG)
            case SDLK_KP_1:
            case '`': {
                iBlock *seed = new_Block(64);
                for (size_t i = 0; i < 64; ++i) {
                    setByte_Block(seed, i, iRandom(0, 256));
                }
                setThemeSeed_GmDocument(view->doc, seed, NULL);
                delete_Block(seed);
                invalidate_DocumentWidget_(d);
                refresh_Widget(w);
                break;
            }
#endif
#if 0
            case '0': {
                extern int enableHalfPixelGlyphs_Text;
                enableHalfPixelGlyphs_Text = !enableHalfPixelGlyphs_Text;
                refresh_Widget(w);
                printf("halfpixel: %d\n", enableHalfPixelGlyphs_Text);
                fflush(stdout);
                break;
            }
#endif
#if 0
            case '0': {
                extern int enableKerning_Text;
                enableKerning_Text = !enableKerning_Text;
                invalidate_DocumentWidget_(d);
                refresh_Widget(w);
                printf("kerning: %d\n", enableKerning_Text);
                fflush(stdout);
                break;
            }
#endif
        }
    }
#if defined (iPlatformAppleDesktop)
    else if (ev->type == SDL_MOUSEWHEEL &&
             ev->wheel.y == 0 &&
             d->wheelSwipeState == direct_WheelSwipeState &&
             handleWheelSwipe_DocumentWidget_(d, &ev->wheel)) {
        return iTrue;
    }
#endif
    else if (ev->type == SDL_MOUSEWHEEL && isHover_Widget(w)) {
        const iInt2 mouseCoord = coord_MouseWheelEvent(&ev->wheel);
        if (isPerPixel_MouseWheelEvent(&ev->wheel)) {
            const iInt2 wheel = init_I2(ev->wheel.x, ev->wheel.y);
            stop_Anim(&d->view.scrollY.pos);
            immediateScroll_DocumentView_(view, -wheel.y);
            if (!scrollWideBlock_DocumentView_(view, mouseCoord, -wheel.x, 0) &&
                wheel.x) {
                handleWheelSwipe_DocumentWidget_(d, &ev->wheel);
            }
        }
        else {
            /* Traditional mouse wheel. */
            const int amount = ev->wheel.y;
            if (keyMods_Sym(modState_Keys()) == KMOD_PRIMARY) {
                postCommandf_App("zoom.delta arg:%d", amount > 0 ? 10 : -10);
                return iTrue;
            }
            smoothScroll_DocumentView_(view,
                                       -3 * amount * lineHeight_Text(paragraph_FontId),
                                       smoothDuration_DocumentWidget_(mouse_ScrollType));
            scrollWideBlock_DocumentView_(
                view, mouseCoord, -3 * ev->wheel.x * lineHeight_Text(paragraph_FontId), 167);
        }
        iChangeFlags(d->flags, noHoverWhileScrolling_DocumentWidgetFlag, iTrue);
        return iTrue;
    }
    else if (ev->type == SDL_MOUSEMOTION) {
        if (ev->motion.which != SDL_TOUCH_MOUSEID) {
            iChangeFlags(d->flags, noHoverWhileScrolling_DocumentWidgetFlag, iFalse);
        }
        const iInt2 mpos = init_I2(ev->motion.x, ev->motion.y);
        if (isVisible_Widget(d->menu)) {
            setCursor_Window(get_Window(), SDL_SYSTEM_CURSOR_ARROW);
        }
#if 0
        else if (contains_Rect(siteBannerRect_DocumentWidget_(d), mpos)) {
            setCursor_Window(get_Window(), SDL_SYSTEM_CURSOR_HAND);
        }
#endif
        else {
            if (value_Anim(&view->altTextOpacity) < 0.833f) {
                setValue_Anim(&view->altTextOpacity, 0, 0); /* keep it hidden while moving */
            }
            updateHover_DocumentView_(view, mpos);
        }
    }
    if (ev->type == SDL_USEREVENT && ev->user.code == widgetTapBegins_UserEventCode) {
        iChangeFlags(d->flags, noHoverWhileScrolling_DocumentWidgetFlag, iFalse);
        return iTrue;
    }
    if (processMediaEvents_DocumentWidget_(d, ev)) {
        return iTrue;
    }
    if (ev->type == SDL_MOUSEBUTTONDOWN) {
        if (ev->button.button == SDL_BUTTON_X1) {
            postCommand_Root(w->root, "navigate.back");
            return iTrue;
        }
        if (ev->button.button == SDL_BUTTON_X2) {
            postCommand_Root(w->root, "navigate.forward");
            return iTrue;
        }
        if (ev->button.button == SDL_BUTTON_MIDDLE && view->hoverLink) {
            interactingWithLink_DocumentWidget_(d, view->hoverLink->linkId);
            postCommandf_Root(w->root, "open query:%d newtab:%d url:%s",
                              isSpartanQueryLink_DocumentWidget_(d, view->hoverLink->linkId),
                              (isPinned_DocumentWidget_(d) ? otherRoot_OpenTabFlag : 0) |
                              (modState_Keys() & KMOD_SHIFT ? new_OpenTabFlag : newBackground_OpenTabFlag),
                              cstr_String(linkUrl_GmDocument(view->doc, view->hoverLink->linkId)));
            return iTrue;
        }
        if (ev->button.button == SDL_BUTTON_RIGHT &&
            contains_Widget(w, init_I2(ev->button.x, ev->button.y))) {
            if (!isVisible_Widget(d->menu)) {
                d->contextLink = view->hoverLink;
                d->contextPos = init_I2(ev->button.x, ev->button.y);
                if (d->menu) {
                    destroy_Widget(d->menu);
                    d->menu = NULL;
                }
                setFocus_Widget(NULL);
                iArray items;
                init_Array(&items, sizeof(iMenuItem));
                if (d->contextLink) {
                    /* Construct the link context menu, depending on what kind of link was clicked. */
                    const int spartanQuery = isSpartanQueryLink_DocumentWidget_(d, d->contextLink->linkId);
                    interactingWithLink_DocumentWidget_(d, d->contextLink->linkId); /* perhaps will be triggered */
                    const iString *linkUrl  = linkUrl_GmDocument(view->doc, d->contextLink->linkId);
                    const iRangecc scheme   = urlScheme_String(linkUrl);
                    const iBool    isGemini = equalCase_Rangecc(scheme, "gemini");
                    iBool          isNative = iFalse;
                    if (deviceType_App() != desktop_AppDeviceType) {
                        /* Show the link as the first, non-interactive item. */
                        iString *infoText = collectNew_String();
                        infoText_LinkInfo(d->view.doc, d->contextLink->linkId, infoText);
                        pushBack_Array(&items, &(iMenuItem){
                            format_CStr("```%s", cstr_String(infoText)),
                            0, 0, NULL });
                    }
                    if (isGemini ||
                        willUseProxy_App(scheme) ||
                        equalCase_Rangecc(scheme, "data") ||
                        equalCase_Rangecc(scheme, "file") ||
                        equalCase_Rangecc(scheme, "finger") ||
                        equalCase_Rangecc(scheme, "gopher") ||
                        equalCase_Rangecc(scheme, "spartan")) {
                        isNative = iTrue;
                        /* Regular links that we can open. */
                        pushBackN_Array(&items,
                                        (iMenuItem[]){
                                            { openTab_Icon " ${link.newtab}",
                                              0,
                                              0,
                                              format_CStr("!open query:%d newtab:1 origin:%s url:%s",
                                                          spartanQuery,
                                                          cstr_String(id_Widget(w)),
                                                          cstr_String(linkUrl)) },
                                            { openTabBg_Icon " ${link.newtab.background}",
                                              0,
                                              0,
                                              format_CStr("!open query:%d newtab:2 origin:%s url:%s",
                                                          spartanQuery,
                                                          cstr_String(id_Widget(w)),
                                                          cstr_String(linkUrl)) },
                                            { openWindow_Icon " ${link.newwindow}",
                                              0,
                                              0,
                                              format_CStr("!open query:%d newwindow:1 origin:%s url:%s",
                                                          spartanQuery,
                                                          cstr_String(id_Widget(w)),
                                                          cstr_String(linkUrl)) },
                                            { "${link.side}",
                                              0,
                                              0,
                                              format_CStr("!open query:%d newtab:4 origin:%s url:%s",
                                                          spartanQuery,
                                                          cstr_String(id_Widget(w)),
                                                          cstr_String(linkUrl)) },
                                            { "${link.side.newtab}",
                                              0,
                                              0,
                                              format_CStr("!open query:%d newtab:5 origin:%s url:%s",
                                                          spartanQuery,
                                                          cstr_String(id_Widget(w)),
                                                          cstr_String(linkUrl)) },
                                        },
                                        5);
                        if (deviceType_App() == phone_AppDeviceType) {
                            /* Phones don't do windows or splits. */
                            removeN_Array(&items, size_Array(&items) - 3, iInvalidSize);
                        }
                        else if (deviceType_App() == tablet_AppDeviceType) {
                            /* Tablets only do splits. */
                            removeN_Array(&items, size_Array(&items) - 3, 1);
                        }
                        if (equalCase_Rangecc(scheme, "file")) {
                            pushBack_Array(&items, &(iMenuItem){ "---" });
                            pushBack_Array(&items,
                                           &(iMenuItem){ export_Icon " ${menu.open.external}",
                                                         0,
                                                         0,
                                                         format_CStr("!open default:1 url:%s",
                                                                     cstr_String(linkUrl)) });
#if defined (iPlatformAppleDesktop)
                            pushBack_Array(&items,
                                           &(iMenuItem){ "${menu.reveal.macos}",
                                                         0,
                                                         0,
                                                         format_CStr("!reveal url:%s",
                                                                     cstr_String(linkUrl)) });
#endif
#if defined (iPlatformLinux)
                            pushBack_Array(&items,
                                           &(iMenuItem){ "${menu.reveal.filemgr}",
                                                         0,
                                                         0,
                                                         format_CStr("!reveal url:%s",
                                                                     cstr_String(linkUrl)) });
#endif
                        }
                    }
                    else if (!willUseProxy_App(scheme)) {
                        pushBack_Array(
                            &items,
                            &(iMenuItem){ openExt_Icon " ${link.browser}",
                                          0,
                                          0,
                                          format_CStr("!open default:1 url:%s", cstr_String(linkUrl)) });
                    }
                    if (willUseProxy_App(scheme)) {
                        pushBackN_Array(
                            &items,
                            (iMenuItem[]){
                                { "---" },
                                { isGemini ? "${link.noproxy}" : openExt_Icon " ${link.browser}",
                                  0,
                                  0,
                                  format_CStr("!open origin:%s noproxy:1 url:%s",
                                              cstr_String(id_Widget(w)),
                                              cstr_String(linkUrl)) } },
                            2);
                    }
                    iString *linkLabel = collectNewRange_String(
                        linkLabel_GmDocument(view->doc, d->contextLink->linkId));
                    urlEncodeSpaces_String(linkLabel);
                    pushBackN_Array(&items,
                                    (iMenuItem[]){ { "---" },
                                                   { "${link.copy}", 0, 0, "document.copylink" },
                                                   { bookmark_Icon " ${link.bookmark}",
                                                     0,
                                                     0,
                                                     format_CStr("!bookmark.add title:%s url:%s",
                                                                 cstr_String(linkLabel),
                                                                 cstr_String(linkUrl)) },
                                                   },
                                    3);
                    if (isNative && d->contextLink->mediaType != download_MediaType &&
                        !equalCase_Rangecc(scheme, "file")) {
                        pushBackN_Array(&items, (iMenuItem[]){
                            { "---" },
                            { download_Icon " ${link.download}", 0, 0, "document.downloadlink" },
                        }, 2);
                    }
                    iMediaRequest *mediaReq;
                    if ((mediaReq = findMediaRequest_DocumentWidget_(d, d->contextLink->linkId)) != NULL &&
                        d->contextLink->mediaType != download_MediaType) {
                        if (isFinished_GmRequest(mediaReq->req)) {
                            pushBack_Array(&items,
                                           &(iMenuItem){ download_Icon " " saveToDownloads_Label,
                                                         0,
                                                         0,
                                                         format_CStr("document.media.save link:%u",
                                                                     d->contextLink->linkId) });
                        }
                    }
                    if (equalCase_Rangecc(scheme, "file")) {
                        /* Local files may be deleted. */
                        pushBack_Array(&items, &(iMenuItem){ "---" });
                        pushBack_Array(
                            &items,
                            &(iMenuItem){ delete_Icon " " uiTextCaution_ColorEscape
                                                      "${link.file.delete}",
                                          0,
                                          0,
                                          format_CStr("!file.delete confirm:1 path:%s",
                                                      cstrCollect_String(
                                                          localFilePathFromUrl_String(linkUrl))) });
                    }
                }
                else if (deviceType_App() == desktop_AppDeviceType) {
                    if (!isEmpty_Range(&d->selectMark)) {
                        pushBackN_Array(&items,
                                        (iMenuItem[]){ { "${menu.copy}", 0, 0, "copy" },
                                                       { "---", 0, 0, NULL } },
                                        2);
                    }
                    pushBackN_Array(
                        &items,
                        (iMenuItem[]){
                            { backArrow_Icon " ${menu.back}", navigateBack_KeyShortcut, "navigate.back" },
                            { forwardArrow_Icon " ${menu.forward}", navigateForward_KeyShortcut, "navigate.forward" },
                            { upArrow_Icon " ${menu.parent}", navigateParent_KeyShortcut, "navigate.parent" },
                            { upArrowBar_Icon " ${menu.root}", navigateRoot_KeyShortcut, "navigate.root" },
                            { "---" },
                            { reload_Icon " ${menu.reload}", reload_KeyShortcut, "navigate.reload" },
                            { timer_Icon " ${menu.autoreload}", 0, 0, "document.autoreload.menu" },
                            { "---" },
                            { bookmark_Icon " ${menu.page.bookmark}", bookmarkPage_KeyShortcut, "bookmark.add" },
                            { star_Icon " ${menu.page.subscribe}", subscribeToPage_KeyShortcut, "feeds.subscribe" },
                            { "---" },
                            { book_Icon " ${menu.page.import}", 0, 0, "bookmark.links confirm:1" },
                            { globe_Icon " ${menu.page.translate}", 0, 0, "document.translate" },
                            { upload_Icon " ${menu.page.upload}", 0, 0, "document.upload" },
                            { "${menu.page.upload.edit}", 0, 0, "document.upload copy:1" },
                            { d->flags & viewSource_DocumentWidgetFlag ? "${menu.viewformat.gemini}"
                                                                       : "${menu.viewformat.plain}",
                              0, 0, "document.viewformat" },
                            { "---" },
                            { "${menu.page.copyurl}", 0, 0, "document.copylink" }, },
                        18);
                    if (isEmpty_Range(&d->selectMark)) {
                        pushBackN_Array(
                            &items,
                            (iMenuItem[]){
                                { "${menu.page.copysource}", 'c', KMOD_PRIMARY, "copy" },
                                { download_Icon " " saveToDownloads_Label, SDLK_s, KMOD_PRIMARY, "document.save" } },
                            2);
                    }
                }
                else {
                    /* Mobile text selection menu. */
#if 0
                    pushBackN_Array(
                        &items,
                        (iMenuItem[]){
                            { "${menu.select}", 0, 0, "document.select arg:1" },
                            { "${menu.select.word}", 0, 0, "document.select arg:2" },
                            { "${menu.select.par}", 0, 0, "document.select arg:3" },
                        },
                        3);
#endif
                    postCommand_Root(w->root, "document.select arg:1");
                    return iTrue;
                }
                d->menu = makeMenu_Widget(w, data_Array(&items), size_Array(&items));
                deinit_Array(&items);
                setMenuItemDisabled_Widget(
                    d->menu,
                    "document.upload",
                    !equalCase_Rangecc(urlScheme_String(d->mod.url), "gemini") &&
                    !equalCase_Rangecc(urlScheme_String(d->mod.url), "titan"));
                setMenuItemDisabled_Widget(
                    d->menu,
                    "document.upload copy:1",
                    !equalCase_Rangecc(urlScheme_String(d->mod.url), "gemini") &&
                        !equalCase_Rangecc(urlScheme_String(d->mod.url), "titan"));
            }
            processContextMenuEvent_Widget(d->menu, ev, {});
        }
    }
    if (processEvent_Banner(d->banner, ev)) {
        return iTrue;
    }
    /* The left mouse button. */
    switch (processEvent_Click(&d->click, ev)) {
        case started_ClickResult:
            if (d->grabbedPlayer) {
                return iTrue;
            }
            /* Enable hover state now that scrolling has surely finished. */
            if (d->flags & noHoverWhileScrolling_DocumentWidgetFlag) {
                d->flags &= ~noHoverWhileScrolling_DocumentWidgetFlag;
                updateHover_DocumentView_(view, mouseCoord_Window(get_Window(), ev->button.which));
            }
            if (~flags_Widget(w) & touchDrag_WidgetFlag) {
                iChangeFlags(d->flags, selecting_DocumentWidgetFlag, iFalse);
                iChangeFlags(d->flags, selectWords_DocumentWidgetFlag, d->click.count == 2);
                iChangeFlags(d->flags, selectLines_DocumentWidgetFlag, d->click.count >= 3);
                /* Double/triple clicks marks the selection immediately. */
                if (d->click.count >= 2) {
                    beginMarkingSelection_DocumentWidget_(d, d->click.startPos);
                    extendRange_Rangecc(
                        &d->selectMark,
                        range_String(source_GmDocument(view->doc)),
                        bothStartAndEnd_RangeExtension |
                            (d->click.count == 2 ? word_RangeExtension : line_RangeExtension));
                    d->initialSelectMark = d->selectMark;
                    refresh_Widget(w);
                }
                else {
                    d->initialSelectMark = iNullRange;
                }
            }
            return iTrue;
        case drag_ClickResult: {
#if defined (LAGRANGE_ENABLE_AUDIO)
            if (d->grabbedPlayer) {
                iPlayer *plr =
                    audioPlayer_Media(media_GmDocument(view->doc), mediaId_GmRun(d->grabbedPlayer));
                iPlayerUI ui;
                init_PlayerUI(&ui, plr, runRect_DocumentView_(view, d->grabbedPlayer));
                float off = (float) delta_Click(&d->click).x / (float) width_Rect(ui.volumeSlider);
                setVolume_Player(plr, d->grabbedStartVolume + off);
                refresh_Widget(w);
                return iTrue;
            }
#endif /* LAGRANGE_ENABLE_AUDIO */
            /* Fold/unfold a preformatted block. */
            if (~d->flags & selecting_DocumentWidgetFlag && view->hoverPre &&
                preIsFolded_GmDocument(view->doc, preId_GmRun(view->hoverPre))) {
                return iTrue;
            }
            /* Begin selecting a range of text. */
            if (~d->flags & selecting_DocumentWidgetFlag) {
                beginMarkingSelection_DocumentWidget_(d, d->click.startPos);
            }
            iRangecc loc = sourceLoc_DocumentView_(view, pos_Click(&d->click));
            if (d->selectMark.start == NULL) {
                d->selectMark = loc;
            }
            else if (loc.end) {
                if (flags_Widget(w) & touchDrag_WidgetFlag) {
                    /* Choose which end to move. */
                    if (!(d->flags & (movingSelectMarkStart_DocumentWidgetFlag |
                                      movingSelectMarkEnd_DocumentWidgetFlag))) {
                        const iRangecc mark    = selectMark_DocumentWidget_(d);
                        const char *   midMark = mark.start + size_Range(&mark) / 2;
                        const iRangecc loc     = sourceLoc_DocumentView_(view, pos_Click(&d->click));
                        const iBool    isCloserToStart = d->selectMark.start > d->selectMark.end ?
                            (loc.start > midMark) : (loc.start < midMark);
                        iChangeFlags(d->flags, movingSelectMarkStart_DocumentWidgetFlag, isCloserToStart);
                        iChangeFlags(d->flags, movingSelectMarkEnd_DocumentWidgetFlag, !isCloserToStart);
                    }
                    /* Move the start or the end depending on which is nearer. */
                    if (d->flags & movingSelectMarkStart_DocumentWidgetFlag) {
                        d->selectMark.start = loc.start;
                    }
                    else {
                        d->selectMark.end = (d->selectMark.end > d->selectMark.start ? loc.end : loc.start);
                    }
                }
                else {
                    d->selectMark.end = loc.end;// (d->selectMark.end > d->selectMark.start ? loc.end : loc.start);
                    if (loc.start < d->initialSelectMark.start) {
                        d->selectMark.end = loc.start;
                    }
                    if (isEmpty_Range(&d->selectMark)) {
                        d->selectMark = d->initialSelectMark;
                    }
                }
            }
            iAssert((!d->selectMark.start && !d->selectMark.end) ||
                    ( d->selectMark.start &&  d->selectMark.end));
            /* Extend to full words/paragraphs. */
            if (d->flags & (selectWords_DocumentWidgetFlag | selectLines_DocumentWidgetFlag)) {
                extendRange_Rangecc(
                    &d->selectMark,
                    range_String(source_GmDocument(view->doc)),
                    (d->flags & movingSelectMarkStart_DocumentWidgetFlag ? moveStart_RangeExtension
                                                                         : moveEnd_RangeExtension) |
                        (d->flags & selectWords_DocumentWidgetFlag ? word_RangeExtension
                                                                   : line_RangeExtension));
                if (d->flags & movingSelectMarkStart_DocumentWidgetFlag) {
                    d->initialSelectMark.start =
                        d->initialSelectMark.end = d->selectMark.start;
                }
            }
            if (d->initialSelectMark.start) {
                if (d->selectMark.end > d->selectMark.start) {
                    d->selectMark.start = d->initialSelectMark.start;
                }
                else if (d->selectMark.end < d->selectMark.start) {
                    d->selectMark.start = d->initialSelectMark.end;
                }
            }
//            printf("mark %zu ... %zu {%s}\n", d->selectMark.start - cstr_String(source_GmDocument(d->view.doc)),
//                   d->selectMark.end - cstr_String(source_GmDocument(d->view.doc)),
//                   d->selectMark.end > d->selectMark.start ? cstr_Rangecc(d->selectMark) : "");
//            fflush(stdout);
            refresh_Widget(w);
            return iTrue;
        }
        case finished_ClickResult:
            if (d->grabbedPlayer) {
                setGrabbedPlayer_DocumentWidget_(d, NULL);
                return iTrue;
            }
            if (isVisible_Widget(d->menu)) {
                closeMenu_Widget(d->menu);
            }
            d->flags &= ~(movingSelectMarkStart_DocumentWidgetFlag |
                          movingSelectMarkEnd_DocumentWidgetFlag);
            if (!isMoved_Click(&d->click)) {
                setFocus_Widget(NULL);
                /* Tap in tap selection mode. */
                if (flags_Widget(w) & touchDrag_WidgetFlag) {
                    const iRangecc tapLoc = sourceLoc_DocumentView_(view, pos_Click(&d->click));
                    /* Tapping on the selection will show a menu. */
                    const iRangecc mark = selectMark_DocumentWidget_(d);
                    if (tapLoc.start >= mark.start && tapLoc.end <= mark.end) {
                        if (d->copyMenu) {
                            closeMenu_Widget(d->copyMenu);
                            destroy_Widget(d->copyMenu);
                            d->copyMenu = NULL;
                        }
                        const iMenuItem items[] = {
                            { clipCopy_Icon " ${menu.copy}", 0, 0, "copy" },
#if defined (iPlatformAppleMobile)
                            { export_Icon " ${menu.share}", 0, 0, "copy share:1" },
#endif
                            { "---" },
                            { close_Icon " ${menu.select.clear}", 0, 0, "document.select arg:0" },
                        };
                        d->copyMenu = makeMenu_Widget(w, items, iElemCount(items));
                        setFlags_Widget(d->copyMenu, noFadeBackground_WidgetFlag, iTrue);
                        openMenu_Widget(d->copyMenu, pos_Click(&d->click));
                        return iTrue;
                    }
                    else {
                        /* Tapping elsewhere exits selection mode. */
                        postCommand_Widget(d, "document.select arg:0");
                        return iTrue;
                    }
                }
                if (view->hoverPre) {
                    togglePreFold_DocumentWidget_(d, preId_GmRun(view->hoverPre));
                    return iTrue;
                }
                if (view->hoverLink) {
                    /* TODO: Move this to a method. */
                    const iGmLinkId linkId    = view->hoverLink->linkId;
                    const iMediaId  linkMedia = mediaId_GmRun(view->hoverLink);
                    const int       linkFlags = linkFlags_GmDocument(view->doc, linkId);
                    iAssert(linkId);
                    /* Media links are opened inline by default. */
                    if (isMediaLink_GmDocument(view->doc, linkId)) {
                        if (linkFlags & content_GmLinkFlag && linkFlags & permanent_GmLinkFlag) {
                            /* We have the content and it cannot be dismissed, so nothing
                               further to do. */
                            return iTrue;
                        }
                        if (!requestMedia_DocumentWidget_(d, linkId, iTrue)) {
                            if (linkFlags & content_GmLinkFlag) {
                                /* Dismiss shown content on click. */
                                setData_Media(media_GmDocument(view->doc),
                                              linkId,
                                              NULL,
                                              NULL,
                                              allowHide_MediaFlag);
                                /* Cancel a partially received request. */ {
                                    iMediaRequest *req = findMediaRequest_DocumentWidget_(d, linkId);
                                    if (!isFinished_GmRequest(req->req)) {
                                        cancel_GmRequest(req->req);
                                        removeMediaRequest_DocumentWidget_(d, linkId);
                                        /* Note: Some of the audio IDs have changed now, layout must
                                           be redone. */
                                    }
                                }
                                redoLayout_GmDocument(view->doc);
                                view->hoverLink = NULL;
                                clampScroll_DocumentView_(view);
                                updateVisible_DocumentView_(view);
                                invalidate_DocumentWidget_(d);
                                refresh_Widget(w);
                                return iTrue;
                            }
                            else {
                                /* Show the existing content again if we have it. */
                                iMediaRequest *req = findMediaRequest_DocumentWidget_(d, linkId);
                                if (req) {
                                    setData_Media(media_GmDocument(view->doc),
                                                  linkId,
                                                  meta_GmRequest(req->req),
                                                  body_GmRequest(req->req),
                                                  allowHide_MediaFlag);
                                    redoLayout_GmDocument(view->doc);
                                    updateVisible_DocumentView_(view);
                                    invalidate_DocumentWidget_(d);
                                    refresh_Widget(w);
                                    return iTrue;
                                }
                            }
                        }
                        refresh_Widget(w);
                    }
                    else if (linkMedia.type == download_MediaType ||
                             findMediaRequest_DocumentWidget_(d, linkId)) {
                        /* TODO: What should be done when clicking on an inline download?
                           Maybe dismiss if finished? */
                        return iTrue;
                    }
                    else if (linkFlags & supportedScheme_GmLinkFlag) {
                        int tabMode = openTabMode_Sym(modState_Keys());
                        if (isPinned_DocumentWidget_(d)) {
                            tabMode ^= otherRoot_OpenTabFlag;
                        }
                        interactingWithLink_DocumentWidget_(d, linkId);
                        postCommandf_Root(w->root,
                                          "open query:%d newtab:%d url:%s",
                                          isSpartanQueryLink_DocumentWidget_(d, linkId),
                                          tabMode,
                                          cstr_String(absoluteUrl_String(
                                              d->mod.url, linkUrl_GmDocument(view->doc, linkId))));
                    }
                    else {
                        const iString *url = absoluteUrl_String(
                            d->mod.url, linkUrl_GmDocument(view->doc, linkId));
                        makeQuestion_Widget(
                            uiTextCaution_ColorEscape "${heading.openlink}",
                            format_CStr(
                                cstr_Lang("dlg.openlink.confirm"),
                                uiTextAction_ColorEscape,
                                cstr_String(url)),
                            (iMenuItem[]){
                                { "${cancel}" },
                                { uiTextAction_ColorEscape "${dlg.openlink}",
                                  0, 0, format_CStr("!open default:1 url:%s", cstr_String(url)) } },
                            2);
                    }
                }
                if (d->selectMark.start && !(d->flags & (selectLines_DocumentWidgetFlag |
                                                         selectWords_DocumentWidgetFlag))) {
                    d->selectMark = iNullRange;
                    refresh_Widget(w);
                }
            }
            return iTrue;
        case aborted_ClickResult:
            if (d->grabbedPlayer) {
                setGrabbedPlayer_DocumentWidget_(d, NULL);
                return iTrue;
            }
            return iTrue;
        default:
            break;
    }
    return processEvent_Widget(w, ev);
}

static void checkPendingInvalidation_DocumentWidget_(const iDocumentWidget *d) {
    if (d->flags & invalidationPending_DocumentWidgetFlag &&
        !isAffectedByVisualOffset_Widget(constAs_Widget(d))) {
        //        printf("%p visoff: %d\n", d, left_Rect(bounds_Widget(w)) - left_Rect(boundsWithoutVisualOffset_Widget(w)));
        iDocumentWidget *m = (iDocumentWidget *) d; /* Hrrm, not const... */
        m->flags &= ~invalidationPending_DocumentWidgetFlag;
        invalidate_DocumentWidget_(m);
    }
}

static void prerender_DocumentWidget_(iAny *context) {
    iAssert(isInstance_Object(context, &Class_DocumentWidget));
    if (current_Root() == NULL) {
        /* The widget has probably been removed from the widget tree, pending destruction.
           Tickers are not cancelled until the widget is actually destroyed. */
        return;
    }
    const iDocumentWidget *d = context;
    iDrawContext ctx = {
        .view            = &d->view,
        .docBounds       = documentBounds_DocumentView_(&d->view),
        .vis             = visibleRange_DocumentView_(&d->view),
        .showLinkNumbers = (d->flags & showLinkNumbers_DocumentWidgetFlag) != 0
    };
    //    printf("%u prerendering\n", SDL_GetTicks());    
    if (d->view.visBuf->buffers[0].texture) {
        makePaletteGlobal_GmDocument(d->view.doc);
        if (render_DocumentView_(&d->view, &ctx, iTrue /* just fill up progressively */)) {
            /* Something was drawn, should check later if there is still more to do. */
            addTicker_App(prerender_DocumentWidget_, context);
        }
    }
}

static void draw_DocumentWidget_(const iDocumentWidget *d) {
    const iWidget *w                   = constAs_Widget(d);
    const iRect    bounds              = bounds_Widget(w);
    const iRect    boundsWithoutVisOff = boundsWithoutVisualOffset_Widget(w);
    const iRect    clipBounds          = intersect_Rect(bounds, boundsWithoutVisOff);
    if (width_Rect(bounds) <= 0) {
        return;
    }
    checkPendingInvalidation_DocumentWidget_(d);
    draw_DocumentView_(&d->view);
    iPaint p;
    init_Paint(&p);
    if (colorTheme_App() == pureWhite_ColorTheme &&
        !(prefs_App()->bottomNavBar && prefs_App()->bottomTabBar)) {
        /* A subtle separator between UI and content. */
        drawHLine_Paint(&p, topLeft_Rect(bounds), width_Rect(bounds), uiSeparator_ColorId);
    }
    if ((deviceType_App() == tablet_AppDeviceType && prefs_App()->bottomNavBar &&
         prefs_App()->bottomTabBar) || (isPortraitPhone_App() && prefs_App()->bottomNavBar)) {
        /* Fill the top safe area. */
        if (topSafeInset_Mobile() > 0) {
            const iRect docBounds = documentBounds_DocumentView_(&d->view);
            fillRect_Paint(&p, initCorners_Rect(zero_I2(), topRight_Rect(safeRect_Root(w->root))),
                            !isEmpty_Banner(d->banner) && docBounds.pos.y + viewPos_DocumentView_(&d->view) -
                                documentTopPad_DocumentView_(&d->view) > bounds.pos.y ?
                                tmBannerBackground_ColorId : tmBackground_ColorId);
        }
    }
    /* Pull action indicator. */
    if (deviceType_App() != desktop_AppDeviceType) {
        float pullPos = pullActionPos_SmoothScroll(&d->view.scrollY);
        /* Account for the part where the indicator isn't yet visible. */
        pullPos = (pullPos - 0.2f) / 0.8f;
        iRect indRect = initCentered_Rect(init_I2(mid_Rect(bounds).x,
                                                  top_Rect(bounds) - 5 * gap_UI -
                                                  pos_SmoothScroll(&d->view.scrollY)),
                                          init_I2(20 * gap_UI, gap_UI / 2));
        setClip_Paint(&p, clipBounds);
        int color = pullPos < 1.0f ? tmBannerItemFrame_ColorId : tmBannerItemText_ColorId;
        //drawRect_Paint(&p, indRect, color);
        fillRect_Paint(&p, indRect, color);
        indRect.pos.y += gap_UI / 2;
        indRect.size.y *= 2;
        if (pullPos > 0) {
            //shrink_Rect(&indRect, divi_I2(gap2_UI, 2));
            indRect.size.x *= pullPos;
            fillRect_Paint(&p, indRect, color);
        }
        unsetClip_Paint(&p);
    }
    /* Scroll bar. */
    drawChildren_Widget(w);
    /* Information about the hovered link. */
    if (deviceType_App() == desktop_AppDeviceType && prefs_App()->hoverLink && d->linkInfo) {
        const int pad = 0; /*gap_UI;*/
        update_LinkInfo(d->linkInfo,
                        d->view.doc,
                        d->view.hoverLink ? d->view.hoverLink->linkId : 0,
                        width_Rect(bounds) - 2 * pad);
        const iInt2 infoSize = size_LinkInfo(d->linkInfo);
        iInt2 infoPos = add_I2(bottomLeft_Rect(bounds), init_I2(pad, -infoSize.y - pad));
        if (d->view.hoverLink) {
            const iRect runRect = runRect_DocumentView_(&d->view, d->view.hoverLink);
            d->linkInfo->isAltPos =
                (bottom_Rect(runRect) >= infoPos.y - lineHeight_Text(paragraph_FontId));
        }
        if (d->linkInfo->isAltPos) {
            infoPos.y = top_Rect(bounds) + pad;
        }
        draw_LinkInfo(d->linkInfo, infoPos);
    }
    /* Full-sized download indicator. */
    if (d->flags & drawDownloadCounter_DocumentWidgetFlag && isRequestOngoing_DocumentWidget(d)) {
        const int font = uiLabelLarge_FontId;
        const iInt2 sevenSegWidth = measureRange_Text(font, range_CStr("\U0001fbf0")).bounds.size;
        drawSevenSegmentBytes_MediaUI(font,
                                      add_I2(mid_Rect(bounds),
                                             init_I2(sevenSegWidth.x * 4.5f, -sevenSegWidth.y / 2)),
                                      tmQuote_ColorId, tmQuoteIcon_ColorId,
                                      bodySize_GmRequest(d->request));
    }
    /* Pinch zoom indicator. */
    if (d->flags & pinchZoom_DocumentWidgetFlag) {
        const int   font   = uiLabelLargeBold_FontId;
        const int   height = lineHeight_Text(font) * 2;
        const iInt2 size   = init_I2(height * 2, height);
        const iRect rect   = { sub_I2(mid_Rect(bounds), divi_I2(size, 2)), size };
        fillRect_Paint(&p, rect, d->pinchZoomPosted == 100 ? uiTextCaution_ColorId : uiTextAction_ColorId);
        drawCentered_Text(font, bounds, iFalse, uiBackground_ColorId, "%d %%",
                          d->pinchZoomPosted);
    }
    /* Dimming during swipe animation. */
    if (w->offsetRef) {
        const int offX = visualOffsetByReference_Widget(w);
        if (offX) {
            setClip_Paint(&p, clipBounds);
            SDL_SetRenderDrawBlendMode(renderer_Window(get_Window()), SDL_BLENDMODE_BLEND);
            p.alpha = iAbs(offX) / (float) get_Window()->size.x * 300;
            fillRect_Paint(&p, bounds, backgroundFadeColor_Widget());
            SDL_SetRenderDrawBlendMode(renderer_Window(get_Window()), SDL_BLENDMODE_NONE);
            unsetClip_Paint(&p);
        }
        else {
            /* TODO: Should have a better place to do this; drawing is supposed to be immutable. */
            iWidget *mut = iConstCast(iWidget *, w);
            mut->offsetRef = NULL;
            mut->flags &= ~refChildrenOffset_WidgetFlag;
        }
    }
//    drawRect_Paint(&p, docBounds, red_ColorId);
    if (deviceType_App() == phone_AppDeviceType) {
        /* The phone toolbar uses the palette of the active tab, but there may be other
           documents drawn before the toolbar, causing the colors to be incorrect. */
        makePaletteGlobal_GmDocument(document_App()->view.doc);
    }
}

/*----------------------------------------------------------------------------------------------*/

void init_DocumentWidget(iDocumentWidget *d) {
    iWidget *w = as_Widget(d);
    init_Widget(w);
    setId_Widget(w, format_CStr("document%03d", ++docEnum_));
    setFlags_Widget(w, hover_WidgetFlag | noBackground_WidgetFlag, iTrue);
#if defined (iPlatformAppleDesktop)
    iBool enableSwipeNavigation = iTrue; /* swipes on the trackpad */
#else
    iBool enableSwipeNavigation = (deviceType_App() != desktop_AppDeviceType);
#endif
    if (enableSwipeNavigation) {
        setFlags_Widget(w, leftEdgeDraggable_WidgetFlag | rightEdgeDraggable_WidgetFlag |
                               horizontalOffset_WidgetFlag, iTrue);
    }
    init_PersistentDocumentState(&d->mod);
    d->flags           = 0;
    d->phoneToolbar    = findWidget_App("bottombar");
    d->footerButtons   = NULL;
    iZap(d->certExpiry);
    d->certFingerprint  = new_Block(0);
    d->certFlags        = 0;
    d->certSubject      = new_String();
    d->state            = blank_RequestState;
    d->titleUser        = new_String();
    d->request          = NULL;
    d->requestLinkId    = 0;
    d->isRequestUpdated = iFalse;
    d->media            = new_ObjectList();
    d->banner           = new_Banner();
    setOwner_Banner(d->banner, d);
    d->redirectCount    = 0;
    d->ordinalBase      = 0;
    d->wheelSwipeState  = none_WheelSwipeState;
    d->selectMark       = iNullRange;
    d->foundMark        = iNullRange;
    d->contextLink      = NULL;
    d->sourceStatus = none_GmStatusCode;
    init_String(&d->sourceHeader);
    init_String(&d->sourceMime);
    init_Block(&d->sourceContent, 0);
    iZap(d->sourceTime);
    d->sourceGempub    = NULL;
    d->initNormScrollY = 0;
    d->grabbedPlayer   = NULL;
    d->mediaTimer      = 0;
    init_String(&d->pendingGotoHeading);
    init_String(&d->linePrecedingLink);
    init_Click(&d->click, d, SDL_BUTTON_LEFT);
    d->linkInfo = (deviceType_App() == desktop_AppDeviceType ? new_LinkInfo() : NULL);
    init_DocumentView(&d->view);
    setOwner_DocumentView_(&d->view, d);
    addChild_Widget(w, iClob(d->scroll = new_ScrollWidget()));
    d->menu         = NULL; /* created when clicking */
    d->playerMenu   = NULL;
    d->copyMenu     = NULL;
    d->translation  = NULL;
    addChildFlags_Widget(w,
                         iClob(new_IndicatorWidget()),
                         resizeToParentWidth_WidgetFlag | resizeToParentHeight_WidgetFlag);
#if !defined (iPlatformAppleDesktop) /* in system menu */
    addAction_Widget(w, reload_KeyShortcut, "navigate.reload");
    addAction_Widget(w, closeTab_KeyShortcut, "tabs.close");
    addAction_Widget(w, bookmarkPage_KeyShortcut, "bookmark.add");
    addAction_Widget(w, subscribeToPage_KeyShortcut, "feeds.subscribe");
#endif
    addAction_Widget(w, navigateBack_KeyShortcut, "navigate.back");
    addAction_Widget(w, navigateForward_KeyShortcut, "navigate.forward");
    addAction_Widget(w, navigateParent_KeyShortcut, "navigate.parent");
    addAction_Widget(w, navigateRoot_KeyShortcut, "navigate.root");
}

void cancelAllRequests_DocumentWidget(iDocumentWidget *d) {
    iForEach(ObjectList, i, d->media) {
        iMediaRequest *mr = i.object;
        cancel_GmRequest(mr->req);
    }
    if (d->request) {
        cancel_GmRequest(d->request);
    }
}

void deinit_DocumentWidget(iDocumentWidget *d) {
    cancelAllRequests_DocumentWidget(d);
    pauseAllPlayers_Media(media_GmDocument(d->view.doc), iTrue);
    removeTicker_App(animate_DocumentWidget_, d);
    removeTicker_App(prerender_DocumentWidget_, d);
    removeTicker_App(refreshWhileScrolling_DocumentWidget_, d);
    remove_Periodic(periodic_App(), d);
    delete_Translation(d->translation);
    deinit_DocumentView(&d->view);
    delete_LinkInfo(d->linkInfo);
    iRelease(d->media);
    iRelease(d->request);
    delete_Gempub(d->sourceGempub);
    deinit_String(&d->linePrecedingLink);
    deinit_String(&d->pendingGotoHeading);
    deinit_Block(&d->sourceContent);
    deinit_String(&d->sourceMime);
    deinit_String(&d->sourceHeader);
    delete_Banner(d->banner);
    if (d->mediaTimer) {
        SDL_RemoveTimer(d->mediaTimer);
    }
    delete_Block(d->certFingerprint);
    delete_String(d->certSubject);
    delete_String(d->titleUser);
    deinit_PersistentDocumentState(&d->mod);
}

void setSource_DocumentWidget(iDocumentWidget *d, const iString *source) {
    setUrl_GmDocument(d->view.doc, d->mod.url);
    const int docWidth = documentWidth_DocumentView_(&d->view);
    setSource_GmDocument(d->view.doc,
                         source,
                         docWidth,
                         width_Widget(d),
                         isFinished_GmRequest(d->request) ? final_GmDocumentUpdate
                                                          : partial_GmDocumentUpdate);
    setWidth_Banner(d->banner, docWidth);
    documentWasChanged_DocumentWidget_(d);
}

iHistory *history_DocumentWidget(iDocumentWidget *d) {
    return d->mod.history;
}

const iString *url_DocumentWidget(const iDocumentWidget *d) {
    return d->mod.url;
}

const iGmDocument *document_DocumentWidget(const iDocumentWidget *d) {
    return d->view.doc;
}

const iBlock *sourceContent_DocumentWidget(const iDocumentWidget *d) {
    return &d->sourceContent;
}

int documentWidth_DocumentWidget(const iDocumentWidget *d) {
    return documentWidth_DocumentView_(&d->view);
}

iBool isSourceTextView_DocumentWidget(const iDocumentWidget *d) {
    return (d->flags & viewSource_DocumentWidgetFlag) != 0;
}

const iString *feedTitle_DocumentWidget(const iDocumentWidget *d) {
    if (!isEmpty_String(title_GmDocument(d->view.doc))) {
        return title_GmDocument(d->view.doc);
    }
    return bookmarkTitle_DocumentWidget(d);
}

const iString *bookmarkTitle_DocumentWidget(const iDocumentWidget *d) {
    iStringArray *title = iClob(new_StringArray());
    if (!isEmpty_String(title_GmDocument(d->view.doc))) {
        pushBack_StringArray(title, title_GmDocument(d->view.doc));
    }
    if (!isEmpty_String(d->titleUser)) {
        pushBack_StringArray(title, d->titleUser);
    }
    if (isEmpty_StringArray(title)) {
        iUrl parts;
        init_Url(&parts, d->mod.url);
        if (!isEmpty_Range(&parts.host)) {
            pushBackRange_StringArray(title, parts.host);
        }
    }
    if (isEmpty_StringArray(title)) {
        pushBackCStr_StringArray(title, cstr_Lang("bookmark.title.blank"));
    }
    return collect_String(joinCStr_StringArray(title, " \u2014 "));
}

void serializeState_DocumentWidget(const iDocumentWidget *d, iStream *outs) {
    serialize_PersistentDocumentState(&d->mod, outs);
}

void deserializeState_DocumentWidget(iDocumentWidget *d, iStream *ins) {
    if (d) {
        deserialize_PersistentDocumentState(&d->mod, ins);
        parseUser_DocumentWidget_(d);
        updateFromHistory_DocumentWidget_(d);
    }
    else {
        /* Read and throw away the data. */
        iPersistentDocumentState *dummy = new_PersistentDocumentState();
        deserialize_PersistentDocumentState(dummy, ins);
        delete_PersistentDocumentState(dummy);
    }
}

void setUrlFlags_DocumentWidget(iDocumentWidget *d, const iString *url, int setUrlFlags) {
    const iBool allowCache = (setUrlFlags & useCachedContentIfAvailable_DocumentWidgetSetUrlFlag) != 0;
    iChangeFlags(d->flags, preventInlining_DocumentWidgetFlag,
                 setUrlFlags & preventInlining_DocumentWidgetSetUrlFlag);
    setLinkNumberMode_DocumentWidget_(d, iFalse);
    setUrl_DocumentWidget_(d, urlFragmentStripped_String(url));
    /* See if there a username in the URL. */
    parseUser_DocumentWidget_(d);
    if (!allowCache || !updateFromHistory_DocumentWidget_(d)) {
        fetch_DocumentWidget_(d);
    }
}

void setUrlAndSource_DocumentWidget(iDocumentWidget *d, const iString *url, const iString *mime,
                                    const iBlock *source) {
    setLinkNumberMode_DocumentWidget_(d, iFalse);
    d->flags |= preventInlining_DocumentWidgetFlag;
    setUrl_DocumentWidget_(d, url);
    parseUser_DocumentWidget_(d);
    iGmResponse *resp = new_GmResponse();
    resp->statusCode = success_GmStatusCode;
    initCurrent_Time(&resp->when);
    set_String(&resp->meta, mime);
    set_Block(&resp->body, source);
    updateFromCachedResponse_DocumentWidget_(d, 0, resp, NULL);
    updateBanner_DocumentWidget_(d);
    delete_GmResponse(resp);
}

iDocumentWidget *duplicate_DocumentWidget(const iDocumentWidget *orig) {
    iDocumentWidget *d = new_DocumentWidget();
    delete_History(d->mod.history);
    d->initNormScrollY = normScrollPos_DocumentView_(&d->view);
    d->mod.history = copy_History(orig->mod.history);
    setUrlFlags_DocumentWidget(d, orig->mod.url, useCachedContentIfAvailable_DocumentWidgetSetUrlFlag);
    return d;
}

void setOrigin_DocumentWidget(iDocumentWidget *d, const iDocumentWidget *other) {
    if (d != other) {
        /* TODO: Could remember the other's ID? */
        set_String(&d->linePrecedingLink, &other->linePrecedingLink);
    }
}

void setUrl_DocumentWidget(iDocumentWidget *d, const iString *url) {
    setUrlFlags_DocumentWidget(d, url, 0);
}

void setInitialScroll_DocumentWidget(iDocumentWidget *d, float normScrollY) {
    d->initNormScrollY = normScrollY;
}

void setRedirectCount_DocumentWidget(iDocumentWidget *d, int count) {
    d->redirectCount = count;
}

iBool isRequestOngoing_DocumentWidget(const iDocumentWidget *d) {
    return d->request != NULL;
}

void takeRequest_DocumentWidget(iDocumentWidget *d, iGmRequest *finishedRequest) {
    cancelRequest_DocumentWidget_(d, iFalse /* don't post anything */);
    const iString *url = url_GmRequest(finishedRequest);

    add_History(d->mod.history, url);
    setUrl_DocumentWidget_(d, url);
    d->state = fetching_RequestState;
    iAssert(d->request == NULL);
    d->request = finishedRequest;
    postCommand_Widget(d,
                       "document.request.finished doc:%p reqid:%u request:%p",
                       d,
                       id_GmRequest(d->request),
                       d->request);
}

void updateSize_DocumentWidget(iDocumentWidget *d) {
    iDocumentView *view = &d->view;
    updateDocumentWidthRetainingScrollPosition_DocumentView_(view, iFalse);
    resetWideRuns_DocumentView_(view);
    view->drawBufs->flags |= updateSideBuf_DrawBufsFlag;
    updateVisible_DocumentView_(view);
    setWidth_Banner(d->banner, documentWidth_DocumentView_(view));
    invalidate_DocumentWidget_(d);
    arrange_Widget(d->footerButtons);
}

iBeginDefineSubclass(DocumentWidget, Widget)
    .processEvent = (iAny *) processEvent_DocumentWidget_,
    .draw         = (iAny *) draw_DocumentWidget_,
iEndDefineSubclass(DocumentWidget)
