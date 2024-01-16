/* Copyright 2020-2023 Jaakko Keränen <jaakko.keranen@iki.fi>

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

/* TODO: Consider cleaning up the network request handling. */

#include "documentwidget.h"

#include "app.h"
#include "audio/player.h"
#include "banner.h"
#include "bookmarks.h"
#include "command.h"
#include "defs.h"
#include "documentview.h"
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
#include <the_Foundation/intset.h>
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

static void serializeWithContent_PersistentDocumentState_(const iPersistentDocumentState *,
                                                          iStream *outs, iBool withContent);

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
    iBlock *setIdentity; /* identity (fingerprint) to use for requests, overriding default one */
    enum iReloadInterval reloadInterval;
    int generation;
};

void init_PersistentDocumentState(iPersistentDocumentState *d) {
    d->history        = new_History();
    d->url            = new_String();
    d->setIdentity    = NULL;
    d->reloadInterval = 0;
    d->generation     = 0;
}

void deinit_PersistentDocumentState(iPersistentDocumentState *d) {
    delete_Block(d->setIdentity);
    delete_String(d->url);
    delete_History(d->history);
}

void serialize_PersistentDocumentState(const iPersistentDocumentState *d, iStream *outs) {
    serializeWithContent_PersistentDocumentState_(d, outs, iTrue);
}

void serializeWithContent_PersistentDocumentState_(const iPersistentDocumentState *d, iStream *outs,
                                                   iBool withContent) {
    serialize_String(d->url, outs);
    uint16_t params = (d->reloadInterval & 7) | (iClamp(d->generation, 0, 15) << 4);
    writeU16_Stream(outs, params);
    /* Identity override. */ {
        iBlock empty;
        init_Block(&empty, 0);
        serialize_Block(d->setIdentity ? d->setIdentity : &empty, outs);
        deinit_Block(&empty);
    }
    serializeWithContent_History(d->history, outs, withContent);
}

void deserialize_PersistentDocumentState(iPersistentDocumentState *d, iStream *ins) {
    deserialize_String(d->url, ins);
    if (indexOfCStr_String(d->url, " ptr:0x") != iInvalidPos) {
        /* Oopsie, this should not have been written; invalid URL. */
        clear_String(d->url);
    }
    const uint16_t params = readU16_Stream(ins);
    d->reloadInterval = params & 7;
    d->generation     = params >> 4;
    if (version_Stream(ins) >= documentSetIdentity_FileVersion) {
        iBlock fp;
        init_Block(&fp, 0);
        deserialize_Block(&fp, ins);
        if (!isEmpty_Block(&fp)) {
            d->setIdentity = copy_Block(&fp);
        }
        deinit_Block(&fp);
    }
    deserialize_History(d->history, ins);
}

iDefineTypeConstruction(PersistentDocumentState)

/*----------------------------------------------------------------------------------------------*/

enum iRequestState {
    blank_RequestState,
    fetching_RequestState,
    receivedPartialResponse_RequestState,
    ready_RequestState,
};

/* TODO: Consider moving the swipe flags to a different enum. */
enum iDocumentWidgetFlag {
    selecting_DocumentWidgetFlag             = iBit(1),
    noHoverWhileScrolling_DocumentWidgetFlag = iBit(2),
    showLinkNumbers_DocumentWidgetFlag       = iBit(3),
    setHoverViaKeys_DocumentWidgetFlag       = iBit(4),
    newTabViaHomeKeys_DocumentWidgetFlag     = iBit(5),
    selectWords_DocumentWidgetFlag           = iBit(7),
    selectLines_DocumentWidgetFlag           = iBit(8),
    pinchZoom_DocumentWidgetFlag             = iBit(9),
    movingSelectMarkStart_DocumentWidgetFlag = iBit(10),
    movingSelectMarkEnd_DocumentWidgetFlag   = iBit(11),
    otherRootByDefault_DocumentWidgetFlag    = iBit(12), /* links open to other root by default */
    urlChanged_DocumentWidgetFlag            = iBit(13),
    drawDownloadCounter_DocumentWidgetFlag   = iBit(14),
    fromCache_DocumentWidgetFlag             = iBit(15), /* don't write anything to cache */

    /* Swipe navigation: */
    swipeNavigable_DocumentWidgetFlag        = iBit(16), /* responds to touch swipes (or Mac trackpad) */
    swipeBegun_DocumentWidgetFlag            = iBit(17), /* a swipe is ongoing; swipe events affect
                                                            view offset */
    swipeAborted_DocumentWidgetFlag          = iBit(18), /* swipe was finished by returning
                                                            back to the beginning */
    swipeDeferredFinish_DocumentWidgetFlag   = iBit(19), /* keep swipeView even after animation
                                                            has finished */
    swipeRubberband_DocumentWidgetFlag       = iBit(20),
    swipeViewOverlay_DocumentWidgetFlag      = iBit(21), /* swipeView is drawn over the actual view */
    viewWasSwipedAway_DocumentWidgetFlag     = iBit(22), /* view has been swiped away and should
                                                            be drawn as empty placeholder */
    leftWheelSwipe_DocumentWidgetFlag        = iBit(23), /* swipe state flags are used on desktop */
    rightWheelSwipe_DocumentWidgetFlag       = iBit(24),
    eitherWheelSwipe_DocumentWidgetFlag      = leftWheelSwipe_DocumentWidgetFlag |
                                               rightWheelSwipe_DocumentWidgetFlag,

    viewSource_DocumentWidgetFlag            = iBit(25),
    preventInlining_DocumentWidgetFlag       = iBit(26),
    proxyRequest_DocumentWidgetFlag          = iBit(27),
    waitForIdle_DocumentWidgetFlag           = iBit(28), /* sequential loading; wait for previous
                                                            tabs to finished their requests */
    pendingRedirect_DocumentWidgetFlag       = iBit(29), /* a redirect has been issued */
    goBackOnStop_DocumentWidgetFlag          = iBit(30),
};

enum iDocumentLinkOrdinalMode {
    numbersAndAlphabet_DocumentLinkOrdinalMode,
    homeRow_DocumentLinkOrdinalMode,
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
    uint32_t       lastRequestUpdateAt;
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
    iDocumentView *view;
    iLinkInfo *    linkInfo;
    iAnim          swipeOffset; /* applies to both views */
//    uint32_t       swipeSampleAt;
//    float          swipeSample;
    iDocumentView *swipeView;   /* outgoing old view */
    iBanner *      swipeBanner; /* used by swipeView only */

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

/* Sorted by proximity to F and J. TODO: Add a config file for this sequence. */
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

static void animateMedia_DocumentWidget_            (iDocumentWidget *d);
static void updateSideIconBuf_DocumentWidget_       (const iDocumentWidget *d);
static iBool requestMedia_DocumentWidget_           (iDocumentWidget *d, iGmLinkId linkId, iBool enableFilters);

iRangecc selectionMark_DocumentWidget(const iDocumentWidget *d) {
    /* Normalize so start < end. */
    iRangecc norm = d->selectMark;
    if (norm.start > norm.end) {
        iSwap(const char *, norm.start, norm.end);
    }
    return norm;
}

int phoneToolbarHeight_DocumentWidget(const iDocumentWidget *d) {
    if (!d->phoneToolbar || !isPortraitPhone_App()) {
        return 0;
    }
    const iWidget *w = constAs_Widget(d);
    return bottom_Rect(rect_Root(w->root)) - top_Rect(boundsWithoutVisualOffset_Widget(d->phoneToolbar));
}

int phoneBottomNavbarHeight_DocumentWidget(const iDocumentWidget *d) {
    int height = 0;
    if (isLandscapePhone_App()) {
        const iWidget *w = constAs_Widget(d);
        if (prefs_App()->bottomNavBar) {
            height += height_Widget(findChild_Widget(root_Widget(w), "navbar"));
        }
        else if (prefs_App()->bottomTabBar) {
            height += height_Widget(findChild_Widget(findChild_Widget(root_Widget(w), "doctabs"),
                                                     "tabs.buttons"));
        }
    }
    return height;
}

int footerHeight_DocumentWidget(const iDocumentWidget *d) {
    int hgt = iMaxi(height_Widget(d->footerButtons),
                    /* page footer area (matches top banner, if present) */
                    !isEmpty_Banner(d->banner) && size_GmDocument(d->view->doc).y > 0
                        ? 2 * lineHeight_Text(banner_FontId) : 0);
    hgt += phoneToolbarHeight_DocumentWidget(d); /* in portrait only */
    hgt += phoneBottomNavbarHeight_DocumentWidget(d); /* in landscape only */
    return hgt;
}

iBool noHoverWhileScrolling_DocumentWidget(const iDocumentWidget *d) {
    return (d->flags & noHoverWhileScrolling_DocumentWidgetFlag) != 0;
}

iBool isShowingLinkNumbers_DocumentWidget(const iDocumentWidget *d) {
    return (d->flags & showLinkNumbers_DocumentWidgetFlag) != 0;
}

iBool isBlank_DocumentWidget(const iDocumentWidget *d) {
    return (d->flags & drawDownloadCounter_DocumentWidgetFlag) == 0;
}

iBool isHoverAllowed_DocumentWidget(const iDocumentWidget *d) {
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

iMediaRequest *findMediaRequest_DocumentWidget(const iDocumentWidget *d, iGmLinkId linkId) {
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

iChar linkOrdinalChar_DocumentWidget(const iDocumentWidget *d, size_t ord) {
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

size_t ordinalBase_DocumentWidget(const iDocumentWidget *d) {
    return d->ordinalBase;
}

enum iWheelSwipeState wheelSwipeState_DocumentWidget(const iDocumentWidget *d) {
    return d->wheelSwipeState;
}

void documentRunsInvalidated_DocumentWidget(iDocumentWidget *d) {
    d->foundMark     = iNullRange;
    d->selectMark    = iNullRange;
    d->contextLink   = NULL;
    documentRunsInvalidated_DocumentView(d->view);
}

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
    uint32_t now = SDL_GetTicks();
    if (now - d->lastRequestUpdateAt > 100) {
        d->lastRequestUpdateAt = now;
        postCommand_Widget(obj,
                           "document.request.updated doc:%p reqid:%u request:%p",
                           d,
                           id_GmRequest(d->request),
                           d->request);
    }
    else {
        /* This will tell GmRequest to notify us again when new data comes in. */
        lockResponse_GmRequest(d->request);
        unlockResponse_GmRequest(d->request);
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

static void resetSwipeAnimation_DocumentWidget_(iDocumentWidget *d) {
    if (d->swipeBanner) {
        delete_Banner(d->swipeBanner);
        d->swipeBanner = NULL;
    }
    if (d->view != d->swipeView) {
        delete_DocumentView(d->swipeView);
    }
    d->swipeView = NULL;
    setValue_Anim(&d->swipeOffset, 0, 0);
    iChangeFlags(d->flags,
                 swipeViewOverlay_DocumentWidgetFlag | swipeAborted_DocumentWidgetFlag |
                     swipeDeferredFinish_DocumentWidgetFlag | swipeRubberband_DocumentWidgetFlag,
                 iFalse);
}

static iBool isSwipingBack_DocumentWidget_(const iDocumentWidget *d) {
    return (d->flags & swipeViewOverlay_DocumentWidgetFlag) != 0;
}

static void maybeFinishSwipeAnimation_DocumentWidget_(iDocumentWidget *d) {
    if (~d->flags & swipeBegun_DocumentWidgetFlag &&
        ~d->flags & swipeRubberband_DocumentWidgetFlag &&
        d->swipeView && isFinished_Anim(&d->swipeOffset)) {
        /* When aborting a swipe, we must keep the animation active at the finish until
           the old page has been reloaded. */
        if (d->flags & swipeAborted_DocumentWidgetFlag) {
            if (~d->flags & swipeDeferredFinish_DocumentWidgetFlag) {
                d->flags |= swipeDeferredFinish_DocumentWidgetFlag;
                postCommand_Widget(
                    d, isSwipingBack_DocumentWidget_(d) ? "navigate.forward" : "navigate.back");
            }
        }
        else {
            resetSwipeAnimation_DocumentWidget_(d);
        }
    }
}

static void sampleSwipeSpeed_DocumentWidget_(iDocumentWidget *d) {
    iUnused(d);
#if 0
    const uint32_t now = SDL_GetTicks();
    if (!isFinished_Anim(&d->swipeOffset) && now - d->swipeSampleAt > 100) {
        d->swipeSampleAt = now;
        d->swipeSample = value_Anim(&d->swipeOffset);
    }
#endif
}

void animate_DocumentWidget(void *ticker) {
    iDocumentWidget *d = ticker;
    iAssert(isInstance_Object(d, &Class_DocumentWidget));
    refresh_Widget(d);
    sampleSwipeSpeed_DocumentWidget_(d);
    maybeFinishSwipeAnimation_DocumentWidget_(d);
    if (!isFinished_Anim(&d->view->sideOpacity) || !isFinished_Anim(&d->view->altTextOpacity) ||
        !isFinished_Anim(&d->swipeOffset) ||
        (d->linkInfo && !isFinished_Anim(&d->linkInfo->opacity))) {
        addTicker_App(animate_DocumentWidget, d);
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
    iConstForEach(PtrArray, i, &d->view->visibleMedia) {
        const iGmRun *run = i.ptr;
        if (run->mediaType == audio_MediaType) {
#if defined (LAGRANGE_ENABLE_AUDIO)
            iPlayer *plr = audioPlayer_Media(media_GmDocument(d->view->doc), mediaId_GmRun(run));
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
        iConstForEach(PtrArray, i, &d->view->visibleMedia) {
            const iGmRun *run = i.ptr;
            if (run->mediaType == audio_MediaType) {
#if defined (LAGRANGE_ENABLE_AUDIO)
                iPlayer *plr = audioPlayer_Media(media_GmDocument(d->view->doc), mediaId_GmRun(run));
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
    if (!current_Root() || document_App() != d) {
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
    if (d->flags & waitForIdle_DocumentWidgetFlag) {
        updateTextCStr_LabelWidget(tabButton, midEllipsis_Icon);
        return;
    }
    iStringArray *title = iClob(new_StringArray());
    if (!isEmpty_String(title_GmDocument(d->view->doc))) {
        pushBack_StringArray(title, title_GmDocument(d->view->doc));
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
            setTitle_Window(as_Window(get_MainWindow()), text);
            setWindow = iFalse;
        }
        const iChar siteIcon = siteIcon_GmDocument(d->view->doc);
        /* Remove a redundant icon. */ {
            iStringConstIterator iter;
            init_StringConstIterator(&iter, text);
            if (iter.value == siteIcon) {
                remove_Block(&text->chars, 0, iter.next - cstr_String(text));
                trim_String(text);
            }
        }
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
    invalidate_DocumentView(d->view);
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

uint32_t findBookmarkId_DocumentWidget(const iDocumentWidget *d) {
    return findUrlIdent_Bookmarks(
        bookmarks_App(),
        d->mod.url,
        d->mod.setIdentity ? collect_String(hexEncode_Block(d->mod.setIdentity)) : NULL);
}

static void showOrHideIndicators_DocumentWidget_(iDocumentWidget *d) {
    iWidget *w = as_Widget(d);
    if (d != document_Root(w->root)) {
        return;
    }
    iWidget *navBar = findChild_Widget(root_Widget(w), "navbar");
    showCollapsed_Widget(findChild_Widget(navBar, "document.pinned"),
                         isPinned_DocumentWidget_(d));
    const iBool isBookmarked = findBookmarkId_DocumentWidget(d) != 0;
    iLabelWidget *bmPin = findChild_Widget(navBar, "document.bookmarked");
    setOutline_LabelWidget(bmPin, !isBookmarked);
    setTextColor_LabelWidget(bmPin, isBookmarked ? uiTextAction_ColorId : uiText_ColorId);
}

static void showOrHideInputPrompt_DocumentWidget_(iDocumentWidget *d) {
    iWidget *w = as_Widget(d);
    const iBool show = isVisible_Widget(w);
    iForEach(ObjectList, i, children_Widget(d)) {
        if (startsWith_String(id_Widget(i.object), "!document.input.submit")) {
            setFlags_Widget(i.object, hidden_WidgetFlag, !show);
            iInputWidget *input = findChild_Widget(i.object, "input");
            if (show) {
                setFocus_Widget(as_Widget(input));
            }
            else {
                setSelectAllOnFocus_InputWidget(input, iFalse);
            }
        }
    }
}

static void updateBanner_DocumentWidget_(iDocumentWidget *d) {
    setSite_Banner(d->banner, siteText_DocumentWidget_(d), siteIcon_GmDocument(d->view->doc));
}

static void documentWasChanged_DocumentWidget_(iDocumentWidget *d) {
    iChangeFlags(d->flags, selecting_DocumentWidgetFlag | viewSource_DocumentWidgetFlag, iFalse);
    setFlags_Widget(as_Widget(d), touchDrag_WidgetFlag, iFalse);
    d->requestLinkId = 0;
    updateVisitedLinks_GmDocument(d->view->doc);
    documentRunsInvalidated_DocumentWidget(d);
    updateWindowTitle_DocumentWidget_(d);
    updateBanner_DocumentWidget_(d);
    updateVisible_DocumentView(d->view);
    updateDrawBufs_DocumentView(d->view, updateSideBuf_DrawBufsFlag);
    invalidate_DocumentWidget_(d);
    refresh_Widget(as_Widget(d));
    /* Check for special bookmark tags. */
    d->flags &= ~otherRootByDefault_DocumentWidgetFlag;
    const uint16_t bmid = findBookmarkId_DocumentWidget(d);
    if (bmid) {
        const iBookmark *bm = get_Bookmarks(bookmarks_App(), bmid);
        if (bm->flags & linkSplit_BookmarkFlag) {
            d->flags |= otherRootByDefault_DocumentWidgetFlag;
        }
    }
    showOrHideIndicators_DocumentWidget_(d);
    if (~d->flags & fromCache_DocumentWidgetFlag) {
        setCachedDocument_History(d->mod.history, d->view->doc /* keeps a ref */);
    }
}

static void allocView_DocumentWidget_(iDocumentWidget *d) {
    d->view = new_DocumentView();
    setOwner_DocumentView(d->view, d);
    d->view->banner     = d->banner;
    d->view->selectMark = &d->selectMark;
    d->view->foundMark  = &d->foundMark;
}

static void releaseViewDocument_DocumentWidget_(iDocumentWidget *d) {
    if (d->flags & swipeAborted_DocumentWidgetFlag) {
        resetSwipeAnimation_DocumentWidget_(d);
    }
    if (d->view == d->swipeView) {
        /* The view is being switched away for swiping, so allocate a new one for the
           actual document. */
        d->swipeBanner = d->banner;
        d->banner = new_Banner();
        setOwner_Banner(d->banner, d);
        setWidth_Banner(d->banner, documentWidth_DocumentView(d->view));
        allocView_DocumentWidget_(d);
    }
    iRelease(d->view->doc);
    d->view->doc = NULL;
    iChangeFlags(d->flags, viewWasSwipedAway_DocumentWidgetFlag, iFalse);
}

static void replaceDocument_DocumentWidget_(iDocumentWidget *d, iGmDocument *newDoc) {
    pauseAllPlayers_Media(media_GmDocument(d->view->doc), iTrue);
    releaseViewDocument_DocumentWidget_(d);
    d->view->doc = ref_Object(newDoc);
    documentWasChanged_DocumentWidget_(d);
}

static void updateTheme_DocumentWidget_(iDocumentWidget *d) {
    if (document_App() != d || category_GmStatusCode(d->sourceStatus) == categoryInput_GmStatusCode) {
        return;
    }
    updateDrawBufs_DocumentView(d->view, updateTimestampBuf_DrawBufsFlag);
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
    updateVisible_DocumentView(d->view); /* final placement for the buttons */
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
            case proxyCertificateNotVerified_GmStatusCode:
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
                { person_Icon " ${menu.identity.newdomain}", SDLK_n, 0, "ident.new scope:1" },
                { person_Icon " ${menu.identity.new}", newIdentity_KeyShortcut, "ident.new" },
                { leftHalf_Icon " ${menu.show.identities}",
                  '4',
                  KMOD_PRIMARY,
                  deviceType_App() == desktop_AppDeviceType ? "sidebar.mode arg:3 show:1"
                                                            : "preferences idents:1" } },
            3);
    }
    /* Make a new document for the error page.*/
    iGmDocument *errorDoc = new_GmDocument();
    setWidth_GmDocument(errorDoc, documentWidth_DocumentView(d->view), width_Widget(d));
    setUrl_GmDocument(errorDoc, d->mod.url);
    setFormat_GmDocument(errorDoc, gemini_SourceFormat);
    replaceDocument_DocumentWidget_(d, errorDoc);
    iRelease(errorDoc);
    clear_Banner(d->banner);
    add_Banner(d->banner, error_BannerType, code, serverErrorMsg, NULL);
    d->state = ready_RequestState;
    setSource_DocumentWidget(d, src);
    updateTheme_DocumentWidget_(d);
    resetScroll_DocumentView(d->view);
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
        iGmDocument *doc = d->view->doc;
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
                if (preloadCoverImage_Gempub(d->sourceGempub, d->view->doc)) {
                    redoLayout_GmDocument(d->view->doc);
                    updateVisible_DocumentView(d->view);
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
        updateDrawBufs_DocumentView(d->view, updateTimestampBuf_DrawBufsFlag);
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
                        else if ((endsWithCase_Rangecc(fileName, ".gmi") ||
                                  endsWithCase_Rangecc(fileName, ".gemini")) &&
                                 isEmpty_Range(&parts.query)) {
                            /* The server _probably_ sent us the wrong media type, so assume
                               they meant this is a Gemtext document based on the file extension.
                               However, if the query string is present, the server likely knows
                               what it's doing so only "fix" the type when a query component
                               was not present. */
                            param = range_CStr("text/gemini");
                            /* TODO: A better way to do this would be to preserve the original
                               media type and force a Gemtext view mode on the document.
                               (https://github.com/skyjake/lagrange/issues/359) */
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
                        setData_Media(media_GmDocument(d->view->doc),
                                      imgLinkId,
                                      mimeStr,
                                      &response->body,
                                      !isRequestFinished ? partialData_MediaFlag : 0);
                        redoLayout_GmDocument(d->view->doc);
                    }
                    else if (isAudio && !isInitialUpdate) {
                        /* Update the audio content. */
                        setData_Media(media_GmDocument(d->view->doc),
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
                    if (isUtf8_Rangecc(range_Block(&response->body))) {
                        docFormat = plainText_SourceFormat;
                        charset = range_CStr("utf-8");
                        setWarning_GmDocument(
                            d->view->doc, unsupportedMediaTypeShownAsUtf8_GmDocumentWarning, iTrue);
                    }
                    else {
                        showErrorPage_DocumentWidget_(d, unsupportedMimeType_GmStatusCode, &response->meta);
                        deinit_String(&str);
                        return;
                    }
                }
                else {
                    d->flags |= drawDownloadCounter_DocumentWidgetFlag;
                    clear_PtrSet(d->view->invalidRuns);
                    documentRunsInvalidated_DocumentWidget(d);
                    deinit_String(&str);
                    return;
                }
            }
            setFormat_GmDocument(d->view->doc, docFormat);
            /* Convert the source to UTF-8 if needed. */
            if (equalCase_Rangecc(charset, "utf-8")) {
                /* Verify that it actually is valid UTF-8. */
                if (!isUtf8_Rangecc(range_String(&str))) {
                    if (strstr(cstr_String(&str), "\x1b[")) {
                        charset = range_CStr("cp437"); /* An educated guess. */
                    }
                    else {
                        charset = range_CStr("latin1");
                    }
                }
            }
            if (!equalCase_Rangecc(charset, "utf-8")) {
                set_String(&str,
                           collect_String(decode_Block(&str.chars, cstr_Rangecc(charset))));
            }
        }
        if (cachedDoc) {
            replaceDocument_DocumentWidget_(d, cachedDoc);
            if (updateWidth_DocumentView(d->view)) {
                documentRunsInvalidated_DocumentWidget(d); /* GmRuns reallocated */
            }
        }
        else if (setSource) {
            setSource_DocumentWidget(d, &str);
        }
        deinit_String(&str);
    }
}

static iBool fetch_DocumentWidget_(iDocumentWidget *d) {
    /* We may be instructed to wait before fetching to avoid congestion. */
    if (d->flags & waitForIdle_DocumentWidgetFlag) {
        /* Check all documents in the window. */
        if (isAnyDocumentRequestOngoing_MainWindow(get_MainWindow())) {
            return iFalse; /* have to try again later */
        }
        d->flags &= ~waitForIdle_DocumentWidgetFlag;
    }
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
    d->flags &= ~pendingRedirect_DocumentWidgetFlag;
    d->state = fetching_RequestState;
    d->lastRequestUpdateAt = 0;
    d->request = new_GmRequest(certs_App());
    setUrl_GmRequest(d->request, d->mod.url);
    /* Overriding identity. */
    if (isIdentityPinned_DocumentWidget(d)) {
        const iGmIdentity *ident = identity_DocumentWidget(d);
        if (ident) {
            setIdentity_GmRequest(d->request, ident);
        }
    }
    iConnect(GmRequest, d->request, updated, d, requestUpdated_DocumentWidget_);
    iConnect(GmRequest, d->request, finished, d, requestFinished_DocumentWidget_);
    submit_GmRequest(d->request);
    return iTrue;
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
    if (isFinishedLaunching_App() && isExposed_Window(get_Window())) {
        /* Just cache the top of the document, since this is what we usually need. */
        int maxY = height_Widget(&d->widget) * 2;
        if (maxY == 0) {
            maxY = size_GmDocument(d->view->doc).y;
        }
        render_GmDocument(d->view->doc, (iRangei){ 0, maxY }, cacheRunGlyphs_, NULL);
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
    const int warnings = warnings_GmDocument(d->view->doc) & ~dismissed;
    if (warnings & missingGlyphs_GmDocumentWarning) {
        add_Banner(d->banner, warning_BannerType, missingGlyphs_GmStatusCode, NULL, NULL);
        /* TODO: List one or more of the missing characters and/or their Unicode blocks? */
    }
    if (warnings & ansiEscapes_GmDocumentWarning) {
        add_Banner(d->banner, warning_BannerType, ansiEscapes_GmStatusCode, NULL, NULL);
    }
    if (warnings & unsupportedMediaTypeShownAsUtf8_GmDocumentWarning) {
        add_Banner(d->banner, warning_BannerType, unsupportedMimeTypeShownAsUtf8_GmStatusCode,
                   NULL, NULL);
    }
}

static void updateWidthAndRedoLayout_DocumentWidget_(iDocumentWidget *d) {
    setWidth_GmDocument(d->view->doc, documentWidth_DocumentView(d->view), width_Widget(d));
    documentRunsInvalidated_DocumentWidget(d); /* GmRuns reallocated */
}

static void updateFromCachedResponse_DocumentWidget_(iDocumentWidget *d, float normScrollY,
                                                     const iGmResponse *resp, iGmDocument *cachedDoc) {
//    iAssert(width_Widget(d) > 0); /* must be laid out by now */
    setLinkNumberMode_DocumentWidget_(d, iFalse);
    clear_ObjectList(d->media);
    delete_Gempub(d->sourceGempub);
    d->sourceGempub = NULL;
    pauseAllPlayers_Media(media_GmDocument(d->view->doc), iTrue);
    destroy_Widget(d->footerButtons);
    d->footerButtons = NULL;
    releaseViewDocument_DocumentWidget_(d);
    invalidate_DocumentView(d->view);
    d->view->doc = new_GmDocument();
    d->state = fetching_RequestState;
    d->flags &= ~pendingRedirect_DocumentWidgetFlag;
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
            updateWidthAndRedoLayout_DocumentWidget_(d);
        }
        updateDocument_DocumentWidget_(d, resp, cachedDoc, iTrue);
        clear_Banner(d->banner);
        updateBanner_DocumentWidget_(d);
        addBannerWarnings_DocumentWidget_(d);
    }
    d->state = ready_RequestState;
    postProcessRequestContent_DocumentWidget_(d, iTrue);
    resetScrollPosition_DocumentView(d->view, d->initNormScrollY);
    cacheDocumentGlyphs_DocumentWidget_(d);
    d->flags &= ~(urlChanged_DocumentWidgetFlag | drawDownloadCounter_DocumentWidgetFlag);
    postCommandf_Root(
        as_Widget(d)->root, "document.changed doc:%p url:%s", d, cstr_String(d->mod.url));
}

static iBool updateFromHistory_DocumentWidget_(iDocumentWidget *d, iBool useCachedDoc) {
    const iRecentUrl *recent = constMostRecentUrl_History(d->mod.history);
    setIdentity_DocumentWidget(d, recent ? &recent->setIdentity : NULL);
    if (recent && recent->cachedResponse && equalCase_String(&recent->url, d->mod.url)) {
        iGmDocument *cachedDoc = (useCachedDoc ? recent->cachedDoc : NULL);
        updateFromCachedResponse_DocumentWidget_(
            d, recent->normScrollY, recent->cachedResponse, cachedDoc);
        if (!cachedDoc) {
            /* We now have a cached document. */
            setCachedDocument_History(d->mod.history, d->view->doc);
        }
        return iTrue;
    }
    else if (!isEmpty_String(d->mod.url)) {
        /* IssueID #573: Crash when launching the app on Android. It appears that the TlsRequest
           thread crashes when it does something too early during app launch. As a workaround,
           do not automatically reload the page during app launch if it isn't in the cache. */
        if (!isAndroid_Platform() || isFinishedLaunching_App()) {
            fetch_DocumentWidget_(d);
        }
    }
    if (recent) {
        /* Retain scroll position in refetched content as well. */
        d->initNormScrollY = recent->normScrollY;
    }
    return iFalse;
}

static void continueMarkingSelection_DocumentWidget_(iDocumentWidget *d) {
    iWidget *w = as_Widget(d);
    iRangecc loc = sourceLoc_DocumentView(d->view, pos_Click(&d->click));
    if (d->selectMark.start == NULL) {
        d->selectMark = loc;
    }
    else if (loc.end) {
        if (flags_Widget(w) & touchDrag_WidgetFlag) {
            /* Choose which end to move. */
            if (!(d->flags & (movingSelectMarkStart_DocumentWidgetFlag |
                              movingSelectMarkEnd_DocumentWidgetFlag))) {
                const iRangecc mark    = selectionMark_DocumentWidget(d);
                const char *   midMark = mark.start + size_Range(&mark) / 2;
                const iRangecc loc     = sourceLoc_DocumentView(d->view, pos_Click(&d->click));
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
            d->selectMark.end = loc.end;
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
            range_String(source_GmDocument(d->view->doc)),
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
}

void refreshWhileScrolling_DocumentWidget(iAny *ptr) {
    iAssert(isInstance_Object(ptr, &Class_DocumentWidget));
    iDocumentWidget *d = ptr;
    iDocumentView *view = d->view;
    if (flags_Widget(ptr) & destroyPending_WidgetFlag) {
        return; /* don't waste updating, the widget is being deleted */
    }
    updateVisible_DocumentView(view);
    refresh_Widget(d);
    if (view->animWideRunId) {
        for (const iGmRun *r = view->animWideRunRange.start; r != view->animWideRunRange.end; r++) {
            insert_PtrSet(view->invalidRuns, r);
        }
    }
    if (d->flags & selecting_DocumentWidgetFlag) {
        continueMarkingSelection_DocumentWidget_(d);
    }
    if (isFinished_Anim(&view->animWideRunOffset)) {
        view->animWideRunId = 0;
    }
    if (!isFinished_SmoothScroll(&view->scrollY) || !isFinished_Anim(&view->animWideRunOffset)) {
        addTicker_App(refreshWhileScrolling_DocumentWidget, d);
    }
    if (isFinished_SmoothScroll(&view->scrollY)) {
        iChangeFlags(d->flags, noHoverWhileScrolling_DocumentWidgetFlag, iFalse);
        updateHover_DocumentView(view, mouseCoord_Window(get_Window(), 0));
    }
}

void scrollBegan_DocumentWidget(iAnyObject *any, int offset, uint32_t duration) {
    iDocumentWidget *d = any;
    /* Get rid of link numbers when scrolling. */
    if (offset && d->flags & showLinkNumbers_DocumentWidgetFlag) {
        setLinkNumberMode_DocumentWidget_(d, iFalse);
        invalidateVisibleLinks_DocumentView(d->view);
    }
    /* Show and hide toolbar on scroll. */
    if (deviceType_App() == phone_AppDeviceType) {
        const float normPos = normScrollPos_DocumentView(d->view);
        if (prefs_App()->hideToolbarOnScroll && iAbs(offset) > 5 && normPos >= 0) {
            showToolbar_Root(as_Widget(d)->root, offset < 0 || d->view->scrollY.pos.to <= 0);
        }
    }
    if (offset) {
        updateVisible_DocumentView(d->view);
        refresh_Widget(as_Widget(d));
        if (duration > 0) {
            iChangeFlags(d->flags, noHoverWhileScrolling_DocumentWidgetFlag, iTrue);
            addTicker_App(refreshWhileScrolling_DocumentWidget, d);
        }
    }
}

static void togglePreFold_DocumentWidget_(iDocumentWidget *d, uint16_t preId) {
    const enum iCollapse mode = prefs_App()->collapsePre;
    if (mode == always_Collapse || mode == never_Collapse) {
        return;
    }
    d->view->hoverPre    = NULL;
    d->view->hoverAltPre = NULL;
    d->selectMark        = iNullRange;
    foldPre_GmDocument(d->view->doc, preId);
    redoLayout_GmDocument(d->view->doc);
    clampScroll_DocumentView(d->view);
    updateVisible_DocumentView(d->view);
    updateHover_DocumentView(d->view, mouseCoord_Window(get_Window(), 0));
    invalidate_DocumentWidget_(d);
    refresh_Widget(as_Widget(d));
}

static iString *makeQueryUrl_DocumentWidget_(const iDocumentWidget *d,
                                             const iString *queryUrl,
                                             const iString *userEnteredText) {
    iString *url = copy_String(queryUrl);
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
    iString *url = makeQueryUrl_DocumentWidget_(d, d->mod.url, text_InputWidget(input));
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

iBool isSetIdentityRetained_DocumentWidget(const iDocumentWidget *d, const iString *dstUrl) {
    /* The overriding tab identity is implicitly affecting the entire URL root. */
    return equalRangeCase_Rangecc(urlRoot_String(d->mod.url), urlRoot_String(dstUrl));
}

iBool isAutoReloading_DocumentWidget(const iDocumentWidget *d) {
    return d->mod.reloadInterval != never_RelodPeriod;
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

static void makePastePrecedingLineMenuItem_(iMenuItem *item_out, const iWidget *buttons,
                                            const char *precedingLine) {
    const iBinding *bind = findCommand_Keys("input.precedingline");
    *item_out = (iMenuItem){
        "${menu.input.precedingline}",
         bind->key,
         bind->mods,
         format_CStr("!valueinput.set ptr:%p text:%s", buttons, precedingLine)
    };
}

static const iArray *updateInputPromptMenuItems_(iWidget *menu) {
    const char     *context       = cstr_String(&menu->data);
    const iWidget  *buttons       = pointerLabel_Command(context, "buttons");
    const iString  *url           = string_Command(context, "url");
    const char     *precedingLine = suffixPtr_Command(context, "preceding");
    /* Compose new menu items. */
    iArray *items = collectNew_Array(sizeof(iMenuItem));
    iMenuItem pasteItem;
    makePastePrecedingLineMenuItem_(&pasteItem, buttons, precedingLine);
    pushBack_Array(items, &pasteItem);
    pushBack_Array(items, &(iMenuItem){ "${menu.paste.snippet}", 0, 0, "submenu id:snippetmenu" });
    pushBackN_Array(
        items,
        (iMenuItem[]){
            { "---" },
            { !isPromptUrl_SiteSpec(url) ? "${menu.input.setprompt}" : "${menu.input.unsetprompt}",
              0,
              0,
              format_CStr("!prompturl.toggle url:%s", cstr_String(url)) } },
        2);
    /* Recently submitted input texts can be restored. */ {
        const iStringArray *recentInput = recentlySubmittedInput_App();
        if (!isEmpty_StringArray(recentInput)) {
            pushBack_Array(items, &(iMenuItem){ "---" });
            pushBack_Array(items,
                           &(iMenuItem){ "${menu.input.clear}", 0, 0, "!recentinput.clear" });
            pushBack_Array(items, &(iMenuItem){
                isMobile_Platform() ? "---${ST:menu.input.restore}" : "```${menu.input.restore}"
            });
            iReverseConstForEach(StringArray, i, recentInput) {
                iString *label = collect_String(copy_String(i.value));
                replace_String(label, "\n\n", " ");
                replace_String(label, "\n", " ");
                trim_String(label);
                const size_t maxLen = 45;
                if (length_String(label) > maxLen) {
                    truncate_String(label, maxLen);
                    trim_String(label);
                    appendCStr_String(label, "...");
                }
                pushBack_Array(items,
                               &(iMenuItem){ cstr_String(label),
                                             0,
                                             0,
                                             format_CStr("!valueinput.set ptr:%p text:%s",
                                                         buttons,
                                                         cstr_String(i.value)) });
            }
        }
    }
    return items;
}

iWidget *makeInputPrompt_DocumentWidget(iDocumentWidget *d, const iString *url, iBool isSensitive,
                                        const char *promptLabel, const char *acceptCommand) {
    iUrl parts;
    init_Url(&parts, url);
    iWidget *dlg = makeValueInput_Widget(
        as_Widget(d),
        NULL,
        format_CStr(uiHeading_ColorEscape "%s", cstr_Rangecc(parts.host)),
        promptLabel ? promptLabel
                    : format_CStr(cstr_Lang("dlg.input.prompt"), cstr_Rangecc(parts.path)),
        uiTextAction_ColorEscape "${dlg.input.send}",
        acceptCommand);
    iWidget *buttons = findChild_Widget(dlg, "dialogbuttons");
    iLabelWidget *lineBreak = NULL;
    if (!isSensitive) {
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
    /* Shortcut for the Paste Preceding Line. The menu is dynamic so it won't listen
       for the keys as usual. */ {
        iMenuItem pasteItem;
        makePastePrecedingLineMenuItem_(&pasteItem, buttons, cstr_String(&d->linePrecedingLink));
        addAction_Widget(dlg, pasteItem.key, pasteItem.kmods, pasteItem.command);
    }
    /* Menu for additional actions, past entries. */ {
        iLabelWidget *ellipsisButton =
            makeMenuButton_LabelWidget(midEllipsis_Icon, NULL, 0);
        iWidget *menu = findChild_Widget(as_Widget(ellipsisButton), "menu");
        /* When opening, update the items to reflect the site-specific settings. */
        setMenuUpdateItemsFunc_Widget(menu, updateInputPromptMenuItems_);
        set_String(&menu->data,
                   collectNewFormat_String("context buttons:%p url:%s preceding:%s",
                                           buttons,
                                           cstr_String(canonicalUrl_String(url)),
                                           cstr_String(&d->linePrecedingLink)));
        if (deviceType_App() == desktop_AppDeviceType) {
            addChildPos_Widget(buttons, iClob(ellipsisButton), front_WidgetAddPos);
        }
        else {
            insertChildAfterFlags_Widget(buttons, iClob(ellipsisButton), 0,
                                         frameless_WidgetFlag | noBackground_WidgetFlag);
            setFont_LabelWidget(ellipsisButton, font_LabelWidget((iLabelWidget *) lastChild_Widget(buttons)));
            setTextColor_LabelWidget(ellipsisButton, uiTextAction_ColorId);
        }
    }
    iInputWidget *input = findChild_Widget(dlg, "input");
    setValidator_InputWidget(input, inputQueryValidator_, d);
    setBackupFileName_InputWidget(input, "inputbackup");
    setSelectAllOnFocus_InputWidget(input, iTrue);
    setSensitiveContent_InputWidget(input, isSensitive);
    return dlg;
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
            if (setUrl_DocumentWidget_(d, url_GmDocument(d->view->doc))) {
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
        d->flags &= ~(fromCache_DocumentWidgetFlag | goBackOnStop_DocumentWidgetFlag);
        clear_ObjectList(d->media);
        updateTrust_DocumentWidget_(d, resp);
        if (~d->certFlags & trusted_GmCertFlag &&
            isSuccess_GmStatusCode(statusCode) &&
            equalCase_Rangecc(urlScheme_String(d->mod.url), "gemini")) {
            statusCode = tlsServerCertificateNotVerified_GmStatusCode;
        }
        init_Anim(&d->view->sideOpacity, 0);
        init_Anim(&d->view->altTextOpacity, 0);
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
                makeInputPrompt_DocumentWidget(
                    d,
                    d->mod.url,
                    statusCode == sensitiveInput_GmStatusCode,
                    isEmpty_String(&resp->meta) ? NULL : cstr_String(&resp->meta),
                    format_CStr("!document.input.submit doc:%p", d));
                if (document_App() != d) {
                    postCommandf_App("tabs.switch page:%p", d);
                }
                else {
                    updateTheme_DocumentWidget_(d);
                }
                break;
            }
            case categorySuccess_GmStatusCode: {
                visitUrl_Visited(visited_App(), d->mod.url, 0);
                iGmDocument *newDoc = new_GmDocument();
                replaceDocument_DocumentWidget_(d, newDoc /* keeps ref */);
                iRelease(newDoc);
                clear_Banner(d->banner);
                delete_Gempub(d->sourceGempub);
                d->sourceGempub = NULL;
                destroy_Widget(d->footerButtons);
                d->footerButtons = NULL;
                if (d->flags & urlChanged_DocumentWidgetFlag) {
                    /* Keep scroll position when reloading the same page. */
                    resetScroll_DocumentView(d->view);
                }
                d->view->scrollY.pullActionTriggered = 0;
                updateTheme_DocumentWidget_(d);
                updateDocument_DocumentWidget_(d, resp, NULL, iTrue);
                resetWideRuns_DocumentView(d->view);
                break;
            }
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
                    else if (prefs_App()->allowSchemeChangingRedirect ||
                             equalRangeCase_Rangecc(dstScheme, srcScheme) ||
                             (equalCase_Rangecc(srcScheme, "titan") &&
                              equalCase_Rangecc(dstScheme, "gemini")) ||
                             (equalCase_Rangecc(srcScheme, "gemini") &&
                              equalCase_Rangecc(dstScheme, "titan"))) {
                        visitUrl_Visited(visited_App(), d->mod.url, transient_VisitedUrlFlag);
                        postCommandf_Root(as_Widget(d)->root,
                                          "open doc:%p redirect:%d url:%s",
                                          d,
                                          d->redirectCount + 1,
                                          cstr_String(dstUrl));
                        d->flags |= pendingRedirect_DocumentWidgetFlag;
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
    if (!findMediaRequest_DocumentWidget(d, linkId)) {
        const iString *mediaUrl = absoluteUrl_String(d->mod.url, linkUrl_GmDocument(d->view->doc, linkId));
        pushBack_ObjectList(
            d->media,
            iClob(new_MediaRequest(d,
                                   linkId,
                                   mediaUrl,
                                   enableFilters,
                                   d->mod.setIdentity ? identity_DocumentWidget(d) : NULL)));
        invalidate_DocumentWidget_(d);
        return iTrue;
    }
    return iFalse;
}

static iBool isDownloadRequest_DocumentWidget(const iDocumentWidget *d, const iMediaRequest *req) {
    return findMediaForLink_Media(constMedia_GmDocument(d->view->doc), req->linkId, download_MediaType).type != 0;
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
                if (setData_Media(media_GmDocument(d->view->doc),
                                  req->linkId,
                                  &resp->meta,
                                  &resp->body,
                                  partialData_MediaFlag | allowHide_MediaFlag)) {
                    redoLayout_GmDocument(d->view->doc);
                }
                updateVisible_DocumentView(d->view);
                invalidate_DocumentWidget_(d);
                refresh_Widget(as_Widget(d));
            }
            unlockResponse_GmRequest(req->req);
        }
        /* Update the link's progress. */
        invalidateLink_DocumentView(d->view, req->linkId);
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
                setData_Media(media_GmDocument(d->view->doc),
                              req->linkId,
                              meta_GmRequest(req->req),
                              body_GmRequest(req->req),
                              allowHide_MediaFlag);
                redoLayout_GmDocument(d->view->doc);
                iZap(d->view->visibleRuns); /* pointers invalidated */
                updateVisible_DocumentView(d->view);
                invalidate_DocumentWidget_(d);
                refresh_Widget(as_Widget(d));
                d->redirectCount = 0;
            }
        }
        else if (category_GmStatusCode(code) == categoryRedirect_GmStatusCode) {
            if (d->redirectCount++ < 5) {
                /* Redo the request. */
                iString *url = copy_String(meta_GmRequest(req->req));
                resubmitWithUrl_MediaRequest(req, url);
                delete_String(url);
            }
            else {
                const iGmError *err = get_GmError(tooManyRedirects_GmStatusCode);
                makeSimpleMessage_Widget(format_CStr(uiTextCaution_ColorEscape "%s", err->title), err->info);
                removeMediaRequest_DocumentWidget_(d, req->linkId);
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
    iConstForEach(PtrArray, i, &d->view->visibleLinks) {
        const iGmRun *run = i.ptr;
        if (run->linkId && run->mediaType == none_MediaType &&
            ~run->flags & decoration_GmRunFlag) {
            const int linkFlags = linkFlags_GmDocument(d->view->doc, run->linkId);
            if (isMediaLink_GmDocument(d->view->doc, run->linkId) &&
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

static iBool saveToFile_(const iString *savePath, const iBlock *content, const iString *mime,
                         iBool showDialog) {
    iBool ok = iFalse;
    /* Write the file. */ {
        iFile *f = new_File(savePath);
        postCommandf_App("debug show:%d msg:%s", showDialog, cstr_String(savePath));
        if (open_File(f, writeOnly_FileMode)) {
            write_File(f, content);
            close_File(f);
            const size_t size   = size_Block(content);
            const iBool  isMega = size >= 1000000;
#if defined (iPlatformAppleMobile)
            if (showDialog) {
                exportDownloadedFile_iOS(savePath);
            }
#elif defined (iPlatformAndroidMobile)
            if (showDialog) {
                exportDownloadedFile_Android(savePath, mime);
            }
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
    if (!saveToFile_(savePath, content, mime, showDialog)) {
        return collectNew_String();
    }
    return savePath;
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

static int sidebarSwipeAreaHeight_DocumentWidget_(const iDocumentWidget *d) {
    const iWindow *win = get_Window();
    return iMin(win->size.x, win->size.y) / 4;
}

static iBool checkTabletSwipeVerticalPosition_DocumentWidget_(const iDocumentWidget *d, int swipeY,
                                                              int edge) {
    /* Returns True if the the vertical position is valid for swiping the sidebar. */
    if (deviceType_App() != tablet_AppDeviceType) {
        return iFalse;
    }
    if (edge == 1 && isVisible_Widget(findWidget_App("sidebar"))) {
        return iFalse;
    }
    if (edge == 2 && isVisible_Widget(findWidget_App("sidebar2"))) {
        return iFalse;
    }
    const iWidget *w = constAs_Widget(d);
    const int sidebarSwipeHgt = sidebarSwipeAreaHeight_DocumentWidget_(d);
    if (prefs_App()->bottomNavBar) {
        return swipeY > bottom_Rect(bounds_Widget(w)) - sidebarSwipeHgt;
    }
    else {
        return swipeY < top_Rect(bounds_Widget(w)) + sidebarSwipeHgt;
    }
}

#if 0
static float currentSwipeSpeed_DocumentWidget_(const iDocumentWidget *d) {
    const uint32_t now = SDL_GetTicks();
    if (d->swipeSampleAt < now) {
        const float elapsed = (float) (now - d->swipeSampleAt) / 1000.0f;
        int offset = value_Anim(&d->swipeOffset) - d->swipeSample;
        return fabsf((float) offset / elapsed);
    }
    return gap_UI * 2000;
}
#endif

static iBool handleSwipe_DocumentWidget_(iDocumentWidget *d, const char *cmd) {
    iWidget *w = as_Widget(d);
    if (!prefs_App()->edgeSwipe &&
        startsWith_CStr(cmd, "edgeswipe.") && argLabel_Command(cmd, "edge")) {
        return iFalse;
    }
    if (equal_Command(cmd, "edgeswipe.moved")) {
        /* Edge swipes can also be used to show the sidebars. */
        const int edge = argLabel_Command(cmd, "edge");
        if ((deviceType_App() == tablet_AppDeviceType || isLandscapePhone_App()) &&
            edge &&
            checkTabletSwipeVerticalPosition_DocumentWidget_(d,
                                                             argLabel_Command(cmd, "y"),
                                                             edge)) {
            /* This is an actual swipe from the edge of the device, we should let the sidebars
               handle it. */
            if (edge == 1) {
                transferAffinity_Touch(NULL, findWidget_App("sidebar"));
                return iTrue;
            }
            else if (edge == 2 && deviceType_App() == tablet_AppDeviceType) {
                transferAffinity_Touch(NULL, findWidget_App("sidebar2"));
                return iTrue;
            }
        }
        const int side = argLabel_Command(cmd, "side");
        int offset = arg_Command(cmd);
        if (~d->flags & swipeBegun_DocumentWidgetFlag) {
            if (side == 1) { /* left edge */
                if (atOldest_History(d->mod.history)) {
                    d->flags |= swipeBegun_DocumentWidgetFlag | swipeRubberband_DocumentWidgetFlag;
                    return iTrue;
                }
            }
            if (side == 2) { /* right edge */
                if (offset < -get_Window()->pixelRatio * 10) {
                    if (atNewest_History(d->mod.history)) {
                        d->flags |= swipeBegun_DocumentWidgetFlag | swipeRubberband_DocumentWidgetFlag;
                        return iTrue;
                    }
                }
                else {
                    return iTrue;
                }
            }
            d->flags |= swipeBegun_DocumentWidgetFlag;
            postCommand_Widget(d, side == 1 ? "navigate.back swipe:1" : "navigate.forward swipe:1");
        }
        else if (d->flags & swipeRubberband_DocumentWidgetFlag) {
            setValue_Anim(&d->swipeOffset, offset / 6, 10);
            animate_DocumentWidget(d);
        }
        else if (d->swipeView) {
            if (!isSwipingBack_DocumentWidget_(d)) {
                offset = width_Widget(w) + offset;
            }
            setFlags_Anim(&d->swipeOffset, easeOut_AnimFlag, iFalse);
            setValue_Anim(&d->swipeOffset, offset, 10);
            animate_DocumentWidget(d);
        }
    }
//    const float maxSpeed = gap_UI * 2000;
//    const float minSpeed = gap_UI * 500;
    if (equal_Command(cmd, "edgeswipe.ended")) {
        if (d->flags & swipeRubberband_DocumentWidgetFlag) {
            iChangeFlags(d->flags,
                         swipeRubberband_DocumentWidgetFlag | swipeBegun_DocumentWidgetFlag,
                         iFalse);
            setValue_Anim(&d->swipeOffset, 0, 100);
            animate_DocumentWidget(d);
            return iTrue;
        }
        if (argLabel_Command(cmd, "side") == 2) {
            iChangeFlags(d->flags, swipeBegun_DocumentWidgetFlag, iFalse);
            if (argLabel_Command(cmd, "abort")) {
                d->flags |= swipeAborted_DocumentWidgetFlag;
                setValue_Anim(&d->swipeOffset, width_Widget(w), 100);
                animate_DocumentWidget(d);
                return iTrue;
            }
            setFlags_Anim(&d->swipeOffset, easeOut_AnimFlag, iTrue);
            setValue_Anim(&d->swipeOffset, 0, 150);
    //        float speed = currentSwipeSpeed_DocumentWidget_(d);
    //        speed = iClamp(speed, minSpeed, maxSpeed);
    //        setValueSpeed_Anim(&d->swipeOffset, 0, speed);
            animate_DocumentWidget(d);
            maybeFinishSwipeAnimation_DocumentWidget_(d);
            stopWidgetMomentum_Touch(w);
        }
        else if (argLabel_Command(cmd, "side") == 1) {
            iChangeFlags(d->flags, swipeBegun_DocumentWidgetFlag, iFalse);
            if (argLabel_Command(cmd, "abort")) {
                d->flags |= swipeAborted_DocumentWidgetFlag;
                setValue_Anim(&d->swipeOffset, 0, 100);
                animate_DocumentWidget(d);
                return iTrue;
            }
            setFlags_Anim(&d->swipeOffset, easeOut_AnimFlag, iTrue);
            setValue_Anim(&d->swipeOffset, width_Widget(w), 150);
    //        float speed = currentSwipeSpeed_DocumentWidget_(d);
    //        speed = iClamp(speed, minSpeed, maxSpeed);
    //        setValueSpeed_Anim(&d->swipeOffset, width_Widget(w), speed);
            animate_DocumentWidget(d);
            maybeFinishSwipeAnimation_DocumentWidget_(d);
            stopWidgetMomentum_Touch(w);
        }
        return iTrue;
    }
#if 0
    if (equal_Command(cmd, "swipe.back")) {
        if (atOldest_History(d->mod.history)) {
            return iTrue;
        }
        if (target) { /* we should usually have it...? */
            setupSwipeOverlay_DocumentWidget_(d, as_Widget(target));
            destroy_Widget(as_Widget(target)); /* will be actually deleted after animation finishes */
        }
//        postCommand_Widget(d, "navigate.back");
        return iTrue;
    }
#endif
    return iFalse;
}

static iBool cancelRequest_DocumentWidget_(iDocumentWidget *d, iBool postBack) {
    d->flags &= ~pendingRedirect_DocumentWidgetFlag;
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

static iBool tryWaitingFetch_DocumentWidget_(iDocumentWidget *d) {
    if (d->flags & waitForIdle_DocumentWidgetFlag) {
        if (fetch_DocumentWidget_(d)) {
            return iTrue;
        }
    }
    return iFalse;
}

static const char *setIdentArg_DocumentWidget_(const iDocumentWidget *d, const iString *dstUrl) {
    if (isIdentityPinned_DocumentWidget(d) &&
        isSetIdentityRetained_DocumentWidget(d, dstUrl)) {
        return format_CStr(
            " setident:%s",
            cstrCollect_String(hexEncode_Block(d->mod.setIdentity)));
    }
    return "";
}

iBool isPrerenderingAllowed_DocumentWidget(const iDocumentWidget *d) {
    return d->view != d->swipeView &&
           d->view->visBuf->buffers[0].texture &&
           ~d->flags & swipeBegun_DocumentWidgetFlag;
}

static const iString *selectedText_DocumentWidget_(const iDocumentWidget *d) {
    iRangecc mark = d->selectMark;
    if (mark.start > mark.end) {
        iSwap(const char *, mark.start, mark.end);
    }
    return collect_String(newRange_String(mark));
}

static iBool handleCommand_DocumentWidget_(iDocumentWidget *d, const char *cmd) {
    iWidget *w = as_Widget(d);
    if (equal_Command(cmd, "document.openurls.changed")) {
        /* When any tab changes its document URL, update the open link indicators. */
        if (updateOpenURLs_GmDocument(d->view->doc)) {
            invalidate_DocumentWidget_(d);
            refresh_Widget(d);
        }
        return iFalse;
    }
    if (equal_Command(cmd, "visited.changed")) {
        updateVisitedLinks_GmDocument(d->view->doc);
        invalidateVisibleLinks_DocumentView(d->view);
        return iFalse;
    }
    if (equal_Command(cmd, "document.render")) /* `Periodic` makes direct dispatch to here */ {
//        printf("%u: document.render\n", SDL_GetTicks());
        if (SDL_GetTicks() - lastRenderTime_DocumentView(d->view) > 150) {
            remove_Periodic(periodic_App(), d);
            /* Scrolling has stopped, begin filling up the buffer. */
            if (isPrerenderingAllowed_DocumentWidget(d)) {
                addTicker_App(prerender_DocumentView, d->view);
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
        updateDocumentWidthRetainingScrollPosition_DocumentView(d->view, keepCenter);
        resetWideRuns_DocumentView(d->view);
        updateDrawBufs_DocumentView(d->view, updateSideBuf_DrawBufsFlag);
        updateVisible_DocumentView(d->view);
        invalidate_DocumentWidget_(d);
        dealloc_VisBuf(d->view->visBuf);
        updateWindowTitle_DocumentWidget_(d);
        showOrHideIndicators_DocumentWidget_(d);
        refresh_Widget(w);
    }
    else if (equal_Command(cmd, "window.focus.lost")) {
        if (d->flags & showLinkNumbers_DocumentWidgetFlag) {
            setLinkNumberMode_DocumentWidget_(d, iFalse);
            invalidateVisibleLinks_DocumentView(d->view);
            refresh_Widget(w);
        }
        return iFalse;
    }
    else if (equal_Command(cmd, "window.mouse.exited")) {
        return iFalse;
    }
    else if (equal_Command(cmd, "theme.changed")) {
        invalidatePalette_GmDocument(d->view->doc);
        invalidateTheme_History(d->mod.history); /* forget cached color palettes */
        if (document_App() == d) {
            updateTheme_DocumentWidget_(d);
            updateVisible_DocumentView(d->view);
            updateTrust_DocumentWidget_(d, NULL);
            updateDrawBufs_DocumentView(d->view, updateSideBuf_DrawBufsFlag);
            invalidate_DocumentWidget_(d);
            refresh_Widget(w);
        }
    }
    else if (equal_Command(cmd, "document.layout.changed") && document_Root(get_Root()) == d) {
        if (argLabel_Command(cmd, "redo")) {
            redoLayout_GmDocument(d->view->doc);
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
            set_String(&w->root->tabInsertId, id_Widget(w)); /* insert next to current tab */
        }
        showOrHideInputPrompt_DocumentWidget_(d);
        init_Anim(&d->view->sideOpacity, 0);
        init_Anim(&d->view->altTextOpacity, 0);
        updateSideOpacity_DocumentView(d->view, iFalse);
        updateWindowTitle_DocumentWidget_(d);
        allocVisBuffer_DocumentView(d->view);
        animateMedia_DocumentWidget_(d);
        remove_Periodic(periodic_App(), d);
        removeTicker_App(prerender_DocumentView, d->view);
        return iFalse;
    }
    else if (equal_Command(cmd, "tabs.move")) {
        const iBool dragged = argLabel_Command(cmd, "dragged") != 0;
        if ((!dragged && d == document_App()) ||
            (dragged && /* must be dragging the tab button of this document */
             pointer_Command(cmd) == tabPageButton_Widget(findParent_Widget(w, "doctabs"), d))) {
            int steps = arg_Command(cmd);
            if (steps) {
                iWidget *tabs = findWidget_App("doctabs");
                int tabPos = (int) tabPageIndex_Widget(tabs, d);
                moveTabPage_Widget(tabs, tabPos, iMaxi(0, tabPos + steps));
                refresh_Widget(tabs);
            }
            return iTrue;
        }
        return iFalse;
    }
    else if (equal_Command(cmd, "tabs.swap") && d == document_App()) {
        if (!argLabel_Command(cmd, "newwindow") && numRoots_Window(get_Window()) == 1) {
            /* Duplicate this tab and activate split mode to move the duplicated tab to the
               new split. */
            postCommandf_App("ui.split arg:3 axis:%d",
                             defaultSplitAxis_MainWindow(get_MainWindow()));
            return iTrue;
        }
        iRoot       *oldRoot   = get_Root();
        iMainWindow *oldWin    = get_MainWindow();
        iRoot       *otherRoot = otherRoot_Window(get_Window(), oldRoot);
        iWidget     *docTabs   = findParent_Widget(w, "doctabs");
        size_t       tabIndex  = tabPageIndex_Widget(docTabs, d);
        iMainWindow *newWin    = NULL;
        if (argLabel_Command(cmd, "newwindow")) {
            newWin = newMainWindow_App();
            otherRoot = newWin->base.roots[0];
        }
        iWidget *oldTab = removeTabPage_Widget(docTabs, tabIndex); /* old tab is deleted later */
        iAssert(tabCount_Widget(docTabs) > 0); /* doctabs.menu is not visible with one tab */
        iDocumentWidget *nextTab = (iDocumentWidget *) tabPage_Widget(
            docTabs, iMin(tabIndex, tabCount_Widget(docTabs) - 1));
        showTabPage_Widget(docTabs, nextTab);
        /* Switch to the destination root temporarily so we can create a new tab there. */
        setCurrent_Root(otherRoot);
        if (newWin) {
            setCurrent_Window(newWin);
        }
        newTab_App(d, switchTo_NewTabFlag); /* makes a duplicate */
        setCurrent_Root(oldRoot);
        if (newWin) {
            /* Get rid of the default blank tab. */
            postCommandf_Root(otherRoot,
                              "tabs.close id:%s",
                              cstr_String(id_Widget(tabPage_Widget(
                                  findChild_Widget(otherRoot->widget, "doctabs"), 0))));
            postCommand_Root(otherRoot, "window.unfreeze");
            setCurrent_Window(oldWin);
        }
        arrange_Widget(docTabs);
        destroy_Widget(oldTab);
        return iTrue;
    }
    else if (equal_Command(cmd, "tab.created")) {
        /* Space for tab buttons has changed. */
        updateWindowTitle_DocumentWidget_(d);
        return iFalse;
    }
    else if (equal_Command(cmd, "document.visitlinks") && d == document_App()) {
        const iGmDocument *doc = d->view->doc;
        for (size_t linkId = 1; linkId <= numLinks_GmDocument(doc); linkId++) {
            const iString *url = linkUrl_GmDocument(doc, linkId);
            visitUrl_Visited(visited_App(), url, transient_VisitedUrlFlag);
        }
        updateVisitedLinks_GmDocument(d->view->doc);
        invalidate_DocumentWidget_(d);
        return iTrue;
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
            d->flags |= selecting_DocumentWidgetFlag |
                        movingSelectMarkEnd_DocumentWidgetFlag |
                        selectWords_DocumentWidgetFlag; /* finger-based selection is imprecise */
            d->flags &= ~selectLines_DocumentWidgetFlag;
            setFadeEnabled_ScrollWidget(d->scroll, iFalse);
            d->selectMark = sourceLoc_DocumentView(d->view, d->contextPos);
            extendRange_Rangecc(&d->selectMark, range_String(source_GmDocument(d->view->doc)),
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
        const iRangecc scheme = urlScheme_String(d->mod.url);
        iRangecc host = urlHost_String(d->mod.url);
        uint16_t port = urlPort_String(d->mod.url);
        if (d->flags & proxyRequest_DocumentWidgetFlag &&
            schemeProxy_App(scheme)) {
            const iString *proxyHost;
            schemeProxyHostAndPort_App(scheme, &proxyHost, &port);
            host = range_String(proxyHost);
        }
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
            copied = copy_String(source_GmDocument(d->view->doc));
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
                d->mod.url, linkUrl_GmDocument(d->view->doc, d->contextLink->linkId)))));
        }
        else {
            SDL_SetClipboardText(cstr_String(canonicalUrl_String(d->mod.url)));
        }
        return iTrue;
    }
    else if (equalWidget_Command(cmd, w, "document.downloadlink")) {
        if (d->contextLink) {
            const iGmLinkId linkId = d->contextLink->linkId;
            setUrl_Media(media_GmDocument(d->view->doc),
                         linkId,
                         download_MediaType,
                         linkUrl_GmDocument(d->view->doc, linkId));
            requestMedia_DocumentWidget_(d, linkId, iFalse /* no filters */);
            redoLayout_GmDocument(d->view->doc); /* inline downloader becomes visible */
            updateVisible_DocumentView(d->view);
            invalidate_DocumentWidget_(d);
            refresh_Widget(w);
        }
        return iTrue;
    }
    else if (equal_Command(cmd, "document.input.submit") && document_Command(cmd) == d) {
        const iString *url = d->mod.url;
        if (hasLabel_Command(cmd, "prompturl")) {
            url = string_Command(cmd, "prompturl");
        }
        const iString *userEnteredText = collect_String(suffix_Command(cmd, "value"));
        saveSubmittedInput_App(userEnteredText);
        postCommandf_Root(
            w->root,
            /* use the `redirect:1` argument to cause the input query URL to be
               replaced in History; we don't want to navigate onto it */
            "open redirect:1 url:%s",
            cstrCollect_String(makeQueryUrl_DocumentWidget_(d, url, userEnteredText)));
        return iTrue;
    }
    else if (equal_Command(cmd, "valueinput.cancelled") &&
             equal_Rangecc(range_Command(cmd, "id"), "!document.input.submit") &&
             !hasLabel_Command(cmd, "prompturl") && document_App() == d) {
        postCommand_Root(get_Root(), "navigate.back");
        return iTrue;
    }
    else if (equalWidget_Command(cmd, w, "document.request.updated") &&
             id_GmRequest(d->request) == argU32Label_Command(cmd, "reqid")) {
        if (document_App() == d) {
            updateFetchProgress_DocumentWidget_(d);
        }
        checkResponse_DocumentWidget_(d);
        return iFalse;
    }
    else if (equalWidget_Command(cmd, w, "document.request.finished") &&
             id_GmRequest(d->request) == argU32Label_Command(cmd, "reqid")) {
        iChangeFlags(d->flags, fromCache_DocumentWidgetFlag | preventInlining_DocumentWidgetFlag,
                     iFalse);
        iChangeFlags(d->flags, proxyRequest_DocumentWidgetFlag, isProxy_GmRequest(d->request));
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
        if (category_GmStatusCode(status_GmRequest(d->request)) == categorySuccess_GmStatusCode &&
            !d->view->userHasScrolled) {
            init_Anim(&d->view->scrollY.pos, d->initNormScrollY * pageHeight_DocumentView(d->view));
        }
        addBannerWarnings_DocumentWidget_(d);
        iChangeFlags(d->flags,
                     urlChanged_DocumentWidgetFlag | drawDownloadCounter_DocumentWidgetFlag,
                     iFalse);
        d->state = ready_RequestState;
        postProcessRequestContent_DocumentWidget_(d, iFalse);
        /* The response may be cached. */
        if (d->request) {
            iAssert(~d->flags & fromCache_DocumentWidgetFlag);
            if (!equal_Rangecc(urlScheme_String(d->mod.url), "about") &&
                (startsWithCase_String(meta_GmRequest(d->request), "text/") ||
                 !cmp_String(&d->sourceMime, mimeType_Gempub))) {
                setCachedResponse_History(d->mod.history, lockResponse_GmRequest(d->request));
                unlockResponse_GmRequest(d->request);
            }
        }
        iReleasePtr(&d->request);
        updateVisible_DocumentView(d->view);
        updateDrawBufs_DocumentView(d->view, updateSideBuf_DrawBufsFlag);
        postCommandf_Root(w->root,
                          "document.changed doc:%p status:%d url:%s",
                          d,
                          d->sourceStatus,
                          cstr_String(d->mod.url));
        /* Check for a pending goto. */
        if (!isEmpty_String(&d->pendingGotoHeading)) {
            scrollToHeading_DocumentView(d->view, cstr_String(&d->pendingGotoHeading));
            clear_String(&d->pendingGotoHeading);
        }
        cacheDocumentGlyphs_DocumentWidget_(d);
        /* A redirect response will be considered an ongoing request. */
        if (~d->flags & pendingRedirect_DocumentWidgetFlag) {
            /* Maybe there are other documents waiting to start their requests. */
            if (!isAnyDocumentRequestOngoing_MainWindow(as_MainWindow(window_Widget(w)))) {
                iForEach(ObjectList, i, iClob(listDocuments_App(NULL))) {
                    if (tryWaitingFetch_DocumentWidget_(i.object)) {
                        break;
                    }
                }
            }
        }
        /* Reactivate numbered links mode. */
        if (document_App() == d && isDown_Keys(findCommand_Keys("document.linkkeys arg:0"))) {
            setLinkNumberMode_DocumentWidget_(d, iTrue);
        }
        return iFalse;
    }
    else if (equal_Command(cmd, "document.translate") && d == document_App()) {
        if (!d->translation) {
            d->translation = new_Translation(d);
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
            /* User can resize upload dialogs. */
            setResizeId_Widget(as_Widget(upload), "upload");
            restoreWidth_Widget(as_Widget(upload));
            refresh_Widget(d);
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
        const iMedia * media  = media_GmDocument(d->view->doc);
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
        if (cancelRequest_DocumentWidget_(d, (d->flags & goBackOnStop_DocumentWidgetFlag) != 0)) {
            return iTrue;
        }
    }
    else if (equalWidget_Command(cmd, w, "document.media.save")) {
        const iGmLinkId      linkId = argLabel_Command(cmd, "link");
        const iMediaRequest *media  = findMediaRequest_DocumentWidget(d, linkId);
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
                    if (saveToFile_(tmpPath, &d->sourceContent, &d->sourceMime, iFalse)) {
                        postCommandf_Root(w->root, "!open default:1 mime:%s url:%s",
                                          cstr_String(&d->sourceMime),
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
        d->view->userHasScrolled = iFalse; /* respect the current scroll position */
        d->initNormScrollY = normScrollPos_DocumentView(d->view);
        if (equalCase_Rangecc(urlScheme_String(d->mod.url), "titan")) {
            /* Reopen so the Upload dialog gets shown. */
            postCommandf_App("open url:%s", cstr_String(d->mod.url));
            return iTrue;
        }
        d->flags &= ~goBackOnStop_DocumentWidgetFlag;
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
                const iGmRun *last = lastVisibleLink_DocumentView(d->view);
                if (!last) {
                    d->ordinalBase = 0;
                }
                else {
                    d->ordinalBase += numKeys;
                    if (visibleLinkOrdinal_DocumentView(d->view, last->linkId) < d->ordinalBase) {
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
        invalidateVisibleLinks_DocumentView(d->view);
        refresh_Widget(d);
        return iTrue;
    }
    else if (equal_Command(cmd, "navigate.back") && document_App() == d) {
        cancelRequest_DocumentWidget_(d, iFalse);
        if (argLabel_Command(cmd, "swipe")) {
            resetSwipeAnimation_DocumentWidget_(d);
            iChangeFlags(d->flags, viewWasSwipedAway_DocumentWidgetFlag |
                         swipeViewOverlay_DocumentWidgetFlag, iTrue);
            iAssert(d->swipeView == NULL);
            d->swipeView = d->view; /* Reuse the current view for the animation. */
            sampleSwipeSpeed_DocumentWidget_(d);
        }
        goBack_History(d->mod.history);
        return iTrue;
    }
    else if (equal_Command(cmd, "navigate.forward") && document_App() == d) {
        cancelRequest_DocumentWidget_(d, iFalse);
        if (argLabel_Command(cmd, "swipe")) {
            resetSwipeAnimation_DocumentWidget_(d);
            iChangeFlags(d->flags, viewWasSwipedAway_DocumentWidgetFlag, iTrue);
            setValue_Anim(&d->swipeOffset, width_Widget(w), 0);
            iAssert(d->swipeView == NULL);
            d->swipeView = d->view; /* Reuse the current view for the animation. */
            sampleSwipeSpeed_DocumentWidget_(d);
        }
        goForward_History(d->mod.history);
        return iTrue;
    }
    else if (equal_Command(cmd, "navigate.parent") && document_App() == d) {
        iUrl parts;
        init_Url(&parts, d->mod.url);
        if (equalCase_Rangecc(parts.scheme, "gemini")) {
            /* Check for default index pages according to Gemini Best Practices ("Filenames"):
               gemini://geminiprotocol.net/docs/best-practices.gmi */
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
            cancelRequest_DocumentWidget_(d, iFalse);
            postCommandf_Root(w->root,
                              "open%s url:%s",
                              setIdentArg_DocumentWidget_(d, parentUrl),
                              cstr_String(parentUrl));
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
        cancelRequest_DocumentWidget_(d, iFalse);
        postCommandf_Root(w->root, "open%s url:%s",
                          setIdentArg_DocumentWidget_(d, rootUrl),
                          cstr_String(rootUrl));
        return iTrue;
    }
    else if (equalWidget_Command(cmd, w, "scroll.moved")) {
        init_Anim(&d->view->scrollY.pos, arg_Command(cmd));
        updateVisible_DocumentView(d->view);
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
        smoothScroll_DocumentView(d->view,
                                  dir * amount *
                                      height_Rect(documentBounds_DocumentView(d->view)),
                                  smoothDuration_DocumentWidget_(keyboard_ScrollType));
        return iTrue;
    }
    else if (equal_Command(cmd, "scroll.top") && document_App() == d) {
        if (argLabel_Command(cmd, "smooth")) {
            stopWidgetMomentum_Touch(w);
            smoothScroll_DocumentView(d->view, -pos_SmoothScroll(&d->view->scrollY), 400);
            d->view->scrollY.flags |= muchSofter_AnimFlag;
            return iTrue;
        }
        init_Anim(&d->view->scrollY.pos, 0);
        invalidate_VisBuf(d->view->visBuf);
        clampScroll_DocumentView(d->view);
        updateVisible_DocumentView(d->view);
        refresh_Widget(w);
        return iTrue;
    }
    else if (equal_Command(cmd, "scroll.bottom") && document_App() == d) {
        if (argLabel_Command(cmd, "smooth")) {
            stopWidgetMomentum_Touch(w);
            smoothScroll_DocumentView(d->view, d->view->scrollY.max, 400);
            d->view->scrollY.flags |= muchSofter_AnimFlag;
            return iTrue;
        }
        updateScrollMax_DocumentView(d->view); /* scrollY.max might not be fully updated */
        init_Anim(&d->view->scrollY.pos, d->view->scrollY.max);
        invalidate_VisBuf(d->view->visBuf);
        clampScroll_DocumentView(d->view);
        updateVisible_DocumentView(d->view);
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
        smoothScroll_DocumentView(d->view,
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
            scrollToHeading_DocumentView(d->view, heading);
            return iTrue;
        }
        const char *loc = pointerLabel_Command(cmd, "loc");
        const iGmRun *run = findRunAtLoc_GmDocument(d->view->doc, loc);
        if (run) {
            scrollTo_DocumentView(d->view, run->visBounds.pos.y, iFalse);
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
            d->foundMark     = finder(d->view->doc, text_InputWidget(find), dir > 0 ? d->foundMark.end
                                                                          : d->foundMark.start);
            if (!d->foundMark.start && wrap) {
                /* Wrap around. */
                d->foundMark = finder(d->view->doc, text_InputWidget(find), NULL);
            }
            if (d->foundMark.start) {
                const iGmRun *found;
                if ((found = findRunAtLoc_GmDocument(d->view->doc, d->foundMark.start)) != NULL) {
                    scrollTo_DocumentView(d->view, mid_Rect(found->bounds).y, iTrue);
                    updateVisible_DocumentView(d->view);
                }
            }
        }
        if (flags_Widget(w) & touchDrag_WidgetFlag) {
            postCommand_Root(w->root, "document.select arg:0"); /* we can't handle both at the same time */
        }
        invalidateAndResetWideRunsWithNonzeroOffset_DocumentView(d->view); /* markers don't support offsets */
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
        iIntSet *linkIds = collectNew_IntSet();
        /* Find links that aren't already bookmarked. */
        const iGmDocument *doc = d->view->doc;
        for (size_t linkId = 1; linkId <= numLinks_GmDocument(doc); linkId++) {
            uint32_t bmid;
            if ((bmid = findUrl_Bookmarks(bookmarks_App(), linkUrl_GmDocument(doc, linkId))) != 0) {
                const iBookmark *bm = get_Bookmarks(bookmarks_App(), bmid);
                /* We can import local copies of remote bookmarks. */
                if (~bm->flags & remote_BookmarkFlag) {
                    continue; /* This one is bookmarked. */
                }
            }
            insert_IntSet(linkIds, linkId);
        }
        if (!isEmpty_IntSet(linkIds)) {
            if (argLabel_Command(cmd, "confirm")) {
                const size_t count = size_IntSet(linkIds);
                makeLinkImporter_Widget(count);
            }
            else {
                const uint32_t intoFolder   = argLabel_Command(cmd, "folder");
                const iBool    withHeadings = argLabel_Command(cmd, "headings");
                /* We need to prepare some auxiliary bookkeeping to keep track of the folders
                   that are created for each section of the page. */
                uint32_t parentId = intoFolder;
                uint32_t hierarchy[] = { intoFolder, 0, 0, 0, 0, 0 };
                const iPtrArray  *headings = headings_GmDocument(doc);
                const iGmHeading *head = isEmpty_Array(headings) ? NULL : constData_Array(headings);
                const iGmHeading *headFirst = head;
                const iGmHeading *headEnd   = isEmpty_Array(headings) ? NULL : constEnd_Array(headings);
                uint32_t *headingBookmarkIds = calloc(size_Array(headings), sizeof(uint32_t));
                /* We will create folders as we go and afterwards delete the ones that
                   didn't end up containing any links. */
                iDeclareType(InfoNode);
                struct Impl_InfoNode {
                    iHashNode node;
                    size_t numChildren;
                };
                iHash *folderInfo = new_Hash();
                /* `linkIds` only contains the new links that need to be bookmarked. */
                iConstForEach(IntSet, j, linkIds) {
                    const iGmLinkId linkId = *j.value;
                    iRangecc linkRange = linkUrlRange_GmDocument(doc, linkId);
                    /* Advance in the headings until we reach the one that this link is under. */
                    while (withHeadings && head < headEnd && linkRange.start > head->text.start) {
                        if (!headingBookmarkIds[head - headFirst]) {
                            const int hlev = head->level + 1;
                            parentId       = addToFolder_Bookmarks(bookmarks_App(),
                                                             NULL,
                                                             collectNewRange_String(head->text),
                                                             NULL,
                                                             0,
                                                             hierarchy[hlev - 1]);
                            hierarchy[hlev] = parentId;
                            iInfoNode *info = iMalloc(InfoNode); {
                                info->node.key = parentId;
                                info->numChildren = 0;
                            }
                            insert_Hash(folderInfo, &info->node);
                            headingBookmarkIds[head - headFirst] = parentId;
                            parentId = parentId;
                            /* Keep track of the hierarchy so we know at any time the parent
                               of each heading level. */
                            for (int k = 1; k < hlev; k++) {
                                if (hierarchy[k] == 0) {
                                    hierarchy[k] = parentId;
                                }
                            }
                            hierarchy[hlev + 1] = parentId;
                            hierarchy[hlev + 2] = parentId;
                        }
                        head++;
                    }
                    addToFolder_Bookmarks(bookmarks_App(),
                                          linkUrl_GmDocument(doc, linkId),
                                          collectNewRange_String(linkLabel_GmDocument(doc, linkId)),
                                          NULL,
                                          0x1f588 /* pin */,
                                          withHeadings ? parentId : intoFolder);
                    /* Count children. */
                    if (withHeadings) {
                        for (uint32_t pid = parentId;
                             pid && pid != intoFolder;
                             pid = get_Bookmarks(bookmarks_App(), pid)->parentId) {
                            iInfoNode *n = (iInfoNode *) value_Hash(folderInfo, pid);
                            iAssert(n);
                            n->numChildren++;
                        }
                    }
                }
                iForEach(Hash, iter, folderInfo) {
                    iInfoNode *n = (iInfoNode *) iter.value;
                    if (n->numChildren == 0) {
                        /* This folder was not needed. */
                        remove_Bookmarks(bookmarks_App(), n->node.key);
                    }
                    free(remove_HashIterator(&iter));
                }
                delete_Hash(folderInfo);
                free(headingBookmarkIds);
                postCommand_App("bookmarks.changed");
            }
        }
        else {
            makeSimpleMessage_Widget(uiHeading_ColorEscape "${heading.import.bookmarks}",
                                     numLinks_GmDocument(doc) == 0 ? "${dlg.import.notfound}"
                                                                   : "${dlg.import.notnew}");
        }
        return iTrue;
    }
    else if (equalWidget_Command(cmd, w, "menu.closed")) {
        updateHover_DocumentView(d->view, mouseCoord_Window(get_Window(), 0));
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
        /* Ensure that the indicator gets updated. */
        postCommandf_Root(get_Root(), "window.reload.update root:%p", get_Root());
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
            setUrlAndSource_DocumentWidget(d,
                                           d->mod.url,
                                           string_Command(cmd, "mime"),
                                           &d->sourceContent,
                                           normScrollPos_DocumentView(d->view));
        }
        return iTrue;
    }
    else if (equal_Command(cmd, "document.viewformat") && document_App() == d) {
        const iBool gemtext = hasLabel_Command(cmd, "arg")
                                  ? arg_Command(cmd) != 0 /* set to value */
                                  : (d->flags & viewSource_DocumentWidgetFlag) != 0; /* toggle */
        iChangeFlags(d->flags, viewSource_DocumentWidgetFlag, !gemtext);
        if (setViewFormat_GmDocument(
                d->view->doc, gemtext ? gemini_SourceFormat : plainText_SourceFormat)) {
            documentRunsInvalidated_DocumentWidget(d);
            updateWidthAndRedoLayout_DocumentWidget_(d);
            updateSize_DocumentWidget(d);
        }
        return iTrue;
    }
    else if (equal_Command(cmd, "document.unsetident") && document_App() == d) {
        setIdentity_DocumentWidget(d, NULL);
        postCommand_Widget(w, "document.reload");
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
            d->view->hoverLink = NULL;
        }
        emulateMouseClick_Widget(w, SDL_BUTTON_RIGHT);
        return iTrue;
    }
    return iFalse;
}

static void setGrabbedPlayer_DocumentWidget_(iDocumentWidget *d, const iGmRun *run) {
#if defined (LAGRANGE_ENABLE_AUDIO)
    if (run && run->mediaType == audio_MediaType) {
        iPlayer *plr = audioPlayer_Media(media_GmDocument(d->view->doc), mediaId_GmRun(run));
        setFlags_Player(plr, volumeGrabbed_PlayerFlag, iTrue);
        d->grabbedStartVolume = volume_Player(plr);
        d->grabbedPlayer      = run;
        refresh_Widget(d);
    }
    else if (d->grabbedPlayer) {
        setFlags_Player(
            audioPlayer_Media(media_GmDocument(d->view->doc), mediaId_GmRun(d->grabbedPlayer)),
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
    iConstForEach(PtrArray, i, &d->view->visibleMedia) {
        const iGmRun *run  = i.ptr;
        if (run->mediaType == download_MediaType) {
            iDownloadUI ui;
            init_DownloadUI(&ui, media_GmDocument(d->view->doc), mediaId_GmRun(run).id,
                            runRect_DocumentView(d->view, run));
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
        const iRect rect = runRect_DocumentView(d->view, run);
        iPlayer *   plr  = audioPlayer_Media(media_GmDocument(d->view->doc), mediaId_GmRun(run));
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
    /* Selections don't support horizontal scrolling. */
    invalidateAndResetWideRunsWithNonzeroOffset_DocumentView(d->view);
    iChangeFlags(d->flags, selecting_DocumentWidgetFlag, iTrue);
    d->initialSelectMark = d->selectMark = sourceLoc_DocumentView(d->view, pos);
    refresh_Widget(as_Widget(d));
}

static void interactingWithLink_DocumentWidget_(iDocumentWidget *d, iGmLinkId id) {
    iRangecc loc = linkUrlRange_GmDocument(d->view->doc, id);
    if (!loc.start) {
        clear_String(&d->linePrecedingLink);
        return;
    }
    d->requestLinkId = id;
    const char *start = range_String(source_GmDocument(d->view->doc)).start;
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
    const int linkFlags = linkFlags_GmDocument(d->view->doc, id);
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

static void finishWheelSwipe_DocumentWidget_(iDocumentWidget *d, iBool aborted) {
    if (//d->flags & eitherWheelSwipe_DocumentWidgetFlag &&
        d->wheelSwipeState == direct_WheelSwipeState) {
        const int side = wheelSwipeSide_DocumentWidget_(d);
        int abort = aborted || ((side == 1 && d->swipeSpeed < 0) || (side == 2 && d->swipeSpeed > 0));
        if (iAbs(d->wheelSwipeDistance) < 4 * gap_UI) {
            //printf("ABORTING: dist:%d speed:%f\n", d->wheelSwipeDistance, d->swipeSpeed);
            abort = 1;
        }
        postCommand_Widget(d, "edgeswipe.ended wheel:1 side:%d abort:%d", side, abort);
        d->flags &= ~eitherWheelSwipe_DocumentWidgetFlag;
        d->wheelSwipeState = none_WheelSwipeState;
    }
}

static iBool handleWheelSwipe_DocumentWidget_(iDocumentWidget *d, const SDL_MouseWheelEvent *ev) {
    iWidget *w = as_Widget(d);
    if (~d->flags & swipeNavigable_DocumentWidgetFlag || !prefs_App()->pageSwipe) {
        return iFalse;
    }
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
                finishWheelSwipe_DocumentWidget_(d, iFalse);
            }
            else {
                int step = ev->x * (isMobile_Platform() ? 1 : 2);
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
                    case 0:
                        d->wheelSwipeDistance = iClamp(d->wheelSwipeDistance,
                                                       -width_Widget(d), width_Widget(d));
                        break;
                    case 1:
                        d->wheelSwipeDistance = iMax(0, d->wheelSwipeDistance);
                        d->wheelSwipeDistance = iMin(width_Widget(d), d->wheelSwipeDistance);
                        break;
                    case 2:
                        d->wheelSwipeDistance = iMin(0, d->wheelSwipeDistance);
                        d->wheelSwipeDistance = iMax(-width_Widget(d), d->wheelSwipeDistance);
                        break;
                }
                /* TODO: calculate speed, remember direction */
                //printf("swipe moved to %d, side %d\n", d->wheelSwipeDistance, side);
                postCommand_Widget(d, "edgeswipe.moved arg:%d side:%d", d->wheelSwipeDistance,
                                   wheelSwipeSide_DocumentWidget_(d));
            }
            return iTrue;
    }
    return iFalse;
}

static void postOpenLinkCommand_DocumentWidget_(iDocumentWidget *d, iGmLinkId linkId, int tabMode) {
    const iString *linkUrl = absoluteUrl_String(d->mod.url,
                                                linkUrl_GmDocument(d->view->doc, linkId));
    /* If the user has requested to be prompted for a query string, do so before actually
       opening the link. */
    if (isPromptUrl_SiteSpec(linkUrl)) {
        iUrl url;
        init_Url(&url, linkUrl);
        if (isEmpty_Range(&url.query)) {
            iWidget *dlg = makeInputPrompt_DocumentWidget(
                d,
                linkUrl,
                iFalse,
                NULL,
                format_CStr("!document.input.submit prompturl:%s doc:%p",
                            cstr_String(canonicalUrl_String(linkUrl)),
                            d));
            postCommand_Widget(dlg, "focus.set id:input");
            return;
        }
    }
    postCommandf_Root(d->widget.root,
                      "open query:%d%s newtab:%d%s url:%s",
                      isSpartanQueryLink_DocumentWidget_(d, linkId),
                      tabMode ? format_CStr(" origin:%s", cstr_String(id_Widget(as_Widget(d))))
                              : "",
                      tabMode,
                      setIdentArg_DocumentWidget_(d, linkUrl),
                      cstr_String(linkUrl));
    interactingWithLink_DocumentWidget_(d, linkId);
}

static iBool isScrollableWithWheel_DocumentWidget_(const iDocumentWidget *d) {
    if (isHover_Widget(d)) {
        return iTrue;
    }
    iWindow *win = window_Widget(d);
    iWidget *hover = win->hover;
    if (hasParent_Widget(hover, constAs_Widget(d))) {
        /* Hovering over the scroll widget, for example. */
        return iTrue;
    }
    if (!hover) {
        /* We need the actual mouse coordinates, `mouseCoord_Window()` does not return
           valid coordinates if the mouse is deemed to be outside. */
        int x, y;
        SDL_GetMouseState(&x, &y);
        return hitChild_Window(win, coord_Window(win, x, y)) == d;
    }
    return iFalse;
}

static iWidget *makeLinkContextMenu_DocumentWidget_(iDocumentWidget *d, const iGmRun *link) {
    iWidget *w = as_Widget(d);
    iArray *items = collectNew_Array(sizeof(iMenuItem));
    /* Construct the link context menu, depending on what kind of link was clicked. */
    const int spartanQuery = isSpartanQueryLink_DocumentWidget_(d, link->linkId);
    interactingWithLink_DocumentWidget_(d, link->linkId); /* perhaps will be triggered */
    const iString *linkUrl  = linkUrl_GmDocument(d->view->doc, link->linkId);
    const iRangecc scheme   = urlScheme_String(linkUrl);
    const iBool    isGemini = equalCase_Rangecc(scheme, "gemini");
    iBool          isNative = iFalse;
    if (deviceType_App() != desktop_AppDeviceType) {
        /* Show the link as the first, non-interactive item. */
        iString *infoText = collectNew_String();
        infoText_LinkInfo(d, link->linkId, infoText);
        pushBack_Array(items,
                       &(iMenuItem){ format_CStr("```%s", cstr_String(infoText)), 0, 0, NULL });
    }
    if (isGemini || willUseProxy_App(scheme) || equalCase_Rangecc(scheme, "data") ||
        equalCase_Rangecc(scheme, "file") || equalCase_Rangecc(scheme, "finger") ||
        equalCase_Rangecc(scheme, "gopher") || equalCase_Rangecc(scheme, "spartan")) {
        isNative = iTrue;
        /* Regular links that we can open. */
        pushBackN_Array(items,
                        (iMenuItem[]){
                            { openTab_Icon " ${link.newtab}",
                              0,
                              0,
                              format_CStr("!open query:%d newtab:1 origin:%s%s url:%s",
                                          spartanQuery,
                                          cstr_String(id_Widget(w)),
                                          setIdentArg_DocumentWidget_(d, linkUrl),
                                          cstr_String(linkUrl)) },
                            { openTabBg_Icon " ${link.newtab.background}",
                              0,
                              0,
                              format_CStr("!open query:%d newtab:2 origin:%s%s url:%s",
                                          spartanQuery,
                                          cstr_String(id_Widget(w)),
                                          setIdentArg_DocumentWidget_(d, linkUrl),
                                          cstr_String(linkUrl)) },
                            { openWindow_Icon " ${link.newwindow}",
                              0,
                              0,
                              format_CStr("!open query:%d newwindow:1 origin:%s%s url:%s",
                                          spartanQuery,
                                          cstr_String(id_Widget(w)),
                                          setIdentArg_DocumentWidget_(d, linkUrl),
                                          cstr_String(linkUrl)) },
                            { "${link.side}",
                              0,
                              0,
                              format_CStr("!open query:%d newtab:4 origin:%s%s url:%s",
                                          spartanQuery,
                                          cstr_String(id_Widget(w)),
                                          setIdentArg_DocumentWidget_(d, linkUrl),
                                          cstr_String(linkUrl)) },
                            { "${link.side.newtab}",
                              0,
                              0,
                              format_CStr("!open query:%d newtab:5 origin:%s%s url:%s",
                                          spartanQuery,
                                          cstr_String(id_Widget(w)),
                                          setIdentArg_DocumentWidget_(d, linkUrl),
                                          cstr_String(linkUrl)) },
                        },
                        5);
        if (deviceType_App() == phone_AppDeviceType) {
            /* Phones don't do windows or splits. */
            removeN_Array(items, size_Array(items) - 3, iInvalidSize);
        }
        else if (deviceType_App() == tablet_AppDeviceType) {
            /* Tablets only do splits. */
            removeN_Array(items, size_Array(items) - 3, 1);
        }
        if (equalCase_Rangecc(scheme, "file")) {
            pushBack_Array(items, &(iMenuItem){ "---" });
            pushBack_Array(
                items,
                &(iMenuItem){ export_Icon " ${menu.open.external}",
                              0,
                              0,
                              format_CStr("!open default:1 url:%s", cstr_String(linkUrl)) });
            if (isAppleDesktop_Platform()) {
                pushBack_Array(items,
                               &(iMenuItem){ "${menu.reveal.macos}",
                                             0,
                                             0,
                                             format_CStr("!reveal url:%s", cstr_String(linkUrl)) });
            }
            if (isLinux_Platform()) {
                pushBack_Array(items,
                               &(iMenuItem){ "${menu.reveal.filemgr}",
                                             0,
                                             0,
                                             format_CStr("!reveal url:%s", cstr_String(linkUrl)) });
            }
        }
    }
    else if (!willUseProxy_App(scheme)) {
        pushBack_Array(items,
                       &(iMenuItem){ openExt_Icon " ${link.browser}",
                                     0,
                                     0,
                                     format_CStr("!open default:1 url:%s", cstr_String(linkUrl)) });
    }
    if (willUseProxy_App(scheme)) {
        pushBackN_Array(
            items,
            (iMenuItem[]){ { "---" },
                           { isGemini ? "${link.noproxy}" : openExt_Icon " ${link.browser}",
                             0,
                             0,
                             format_CStr("!open origin:%s noproxy:1 url:%s",
                                         cstr_String(id_Widget(w)),
                                         cstr_String(linkUrl)) } },
            2);
    }
    iString *linkLabel = collectNewRange_String(linkLabel_GmDocument(d->view->doc, link->linkId));
    urlEncodeSpaces_String(linkLabel);
    pushBackN_Array(
        items,
        (iMenuItem[]){
            { "---" },
            { "${link.copy}", 0, 0, "document.copylink" },
            { bookmark_Icon " ${link.bookmark}", 0, 0,
              format_CStr("!bookmark.add title:%s url:%s", cstr_String(linkLabel), cstr_String(linkUrl)) },
            { clipboard_Icon " ${link.snippet}", 0, 0,
              format_CStr("!snippet.add content:%s", cstr_String(linkUrl)) },
            { "---" },
            { magnifyingGlass_Icon " ${link.searchurl}", 0, 0,
              format_CStr("!searchurl address:%s", cstr_String(linkUrl)) },
        },
        6);
    if (isNative && link->mediaType != download_MediaType && !equalCase_Rangecc(scheme, "file")) {
        pushBackN_Array(items,
                        (iMenuItem[]){
                            { "---" },
                            { download_Icon " ${link.download}", 0, 0, "document.downloadlink" },
                        },
                        2);
    }
    iMediaRequest *mediaReq;
    if ((mediaReq = findMediaRequest_DocumentWidget(d, link->linkId)) != NULL &&
        d->contextLink->mediaType != download_MediaType) {
        if (isFinished_GmRequest(mediaReq->req)) {
            pushBack_Array(
                items,
                &(iMenuItem){ download_Icon " " saveToDownloads_Label,
                              0,
                              0,
                              format_CStr("document.media.save link:%u", link->linkId) });
        }
    }
    if (equalCase_Rangecc(scheme, "file")) {
        /* Local files may be deleted. */
        pushBack_Array(items, &(iMenuItem){ "---" });
        pushBack_Array(
            items,
            &(iMenuItem){ delete_Icon " " uiTextCaution_ColorEscape "${link.file.delete}",
                          0,
                          0,
                          format_CStr("!file.delete confirm:1 path:%s",
                                      cstrCollect_String(localFilePathFromUrl_String(linkUrl))) });
    }
    return makeMenu_Widget(w, data_Array(items), size_Array(items));
}

static iBool contains_DocumentWidget_(const iDocumentWidget *d, iInt2 pos) {
    if (!contains_Widget(constAs_Widget(d), pos)) {
        return iFalse;
    }
    if (d->phoneToolbar && contains_Widget(d->phoneToolbar, pos)) {
        return iFalse;
    }
    return iTrue;
}

static iBool processEvent_DocumentWidget_(iDocumentWidget *d, const SDL_Event *ev) {
    iWidget       *w    = as_Widget(d);
    iDocumentView *view = d->view;
    /* Check if a swipe interaction has ended without inertia. */
    if (isMobile_Platform() && d->wheelSwipeState == direct_WheelSwipeState &&
        ev->type == SDL_USEREVENT && ev->user.code == widgetTouchEnds_UserEventCode) {
        finishWheelSwipe_DocumentWidget_(d, iFalse);
    }
    if (isMetricsChange_UserEvent(ev)) {
        updateSize_DocumentWidget(d);
    }
    else if (processEvent_SmoothScroll(&d->view->scrollY, ev)) {
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
            iConstForEach(PtrArray, i, &d->view->visibleLinks) {
                if (ord == iInvalidPos) break;
                const iGmRun *run = i.ptr;
                if (run->flags & decoration_GmRunFlag &&
                    visibleLinkOrdinal_DocumentView(view, run->linkId) == ord) {
                    if (d->flags & setHoverViaKeys_DocumentWidgetFlag) {
                        view->hoverLink = run;
                        updateHoverLinkInfo_DocumentView(view);
                    }
                    else {
                        postOpenLinkCommand_DocumentWidget_(
                            d,
                            run->linkId,
                            (isPinned_DocumentWidget_(d) ? otherRoot_OpenTabFlag : 0) ^
                                (d->ordinalMode == numbersAndAlphabet_DocumentLinkOrdinalMode
                                     ? openTabMode_Sym(modState_Keys())
                                     : (d->flags & newTabViaHomeKeys_DocumentWidgetFlag ? 1 : 0)));
                    }
                    setLinkNumberMode_DocumentWidget_(d, iFalse);
                    invalidateVisibleLinks_DocumentView(view);
                    refresh_Widget(d);
                    return iTrue;
                }
            }
        }
        switch (key) {
            case SDLK_ESCAPE:
                if (d->flags & showLinkNumbers_DocumentWidgetFlag && document_App() == d) {
                    setLinkNumberMode_DocumentWidget_(d, iFalse);
                    invalidateVisibleLinks_DocumentView(view);
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
    else if (d->flags & swipeNavigable_DocumentWidgetFlag &&
             ev->type == SDL_MOUSEWHEEL &&
             ev->wheel.y == 0 &&
             d->wheelSwipeState == direct_WheelSwipeState &&
             handleWheelSwipe_DocumentWidget_(d, &ev->wheel)) {
        return iTrue;
    }
    else if (ev->type == SDL_MOUSEWHEEL && isScrollableWithWheel_DocumentWidget_(d)) {
        const iInt2 mouseCoord = coord_MouseWheelEvent(&ev->wheel);
        if (isPerPixel_MouseWheelEvent(&ev->wheel)) {
            /*if (d->wheelSwipeState != none_WheelSwipeState) {
                finishWheelSwipe_DocumentWidget_(d, iTrue);
            }*/
            const iInt2 wheel = init_I2(ev->wheel.x, ev->wheel.y);
            stop_Anim(&d->view->scrollY.pos);
            immediateScroll_DocumentView(view, -wheel.y);
            if (!scrollWideBlock_DocumentView(view, mouseCoord, -wheel.x, 0, NULL) &&
                wheel.x) {
                handleWheelSwipe_DocumentWidget_(d, &ev->wheel);
            }
        }
        else {
            /* Traditional mouse wheel. */
            iInt2 amount = init_I2(ev->wheel.x, ev->wheel.y);
            const int kmods = keyMods_Sym(modState_Keys());
            if (kmods == KMOD_PRIMARY) {
                postCommandf_App("zoom.delta arg:%d", amount.y > 0 ? 10 : -10);
                return iTrue;
            }
            if (!isApple_Platform() && kmods == KMOD_SHIFT) {
                /* Shift switches to horizontal scrolling mode. (macOS does this for us.) */
                iSwap(int, amount.x, amount.y);
            }
            if (amount.x) {
                scrollWideBlock_DocumentView(view, mouseCoord,
                                             -3 * amount.x * lineHeight_Text(paragraph_FontId),
                                             167, NULL);
            }
            if (amount.y) {
                smoothScroll_DocumentView(view,
                                          -3 * amount.y * lineHeight_Text(paragraph_FontId),
                                          smoothDuration_DocumentWidget_(mouse_ScrollType));
            }
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
            updateHover_DocumentView(view, mpos);
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
            postOpenLinkCommand_DocumentWidget_(
                d,
                view->hoverLink->linkId,
                (isPinned_DocumentWidget_(d) ? otherRoot_OpenTabFlag : 0) |
                    (modState_Keys() & KMOD_SHIFT ? new_OpenTabFlag : newBackground_OpenTabFlag));
            return iTrue;
        }
        if (ev->button.button == SDL_BUTTON_RIGHT &&
            contains_DocumentWidget_(d, init_I2(ev->button.x, ev->button.y))) {
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
                    d->menu = makeLinkContextMenu_DocumentWidget_(d, d->contextLink);
                }
                else {
                    if (deviceType_App() == desktop_AppDeviceType) {
                    if (!isEmpty_Range(&d->selectMark)) {
                            pushBackN_Array(
                                &items,
                                (iMenuItem[]){
                                    { "${menu.copy}", 0, 0, "copy" },
                                    { "${menu.search}", 0, 0,
                                      format_CStr("search newtab:1 query:%s",
                                                  cstr_String(selectedText_DocumentWidget_(d))) },
                                    { "${menu.snippet.add}", 0, 0,
                                      format_CStr("!snippet.add content:%s",
                                                  cstr_String(selectedText_DocumentWidget_(d))) },
                                    { "---", 0, 0, NULL } },
                                4);
                    }
#if defined (iPlatformApple) && defined (LAGRANGE_ENABLE_MAC_MENUS)
                    pushBackN_Array(
                        &items,
                        (iMenuItem[]){
                            { backArrow_Icon " ${menu.back}", navigateBack_KeyShortcut, "navigate.back" },
                            { forwardArrow_Icon " ${menu.forward}", navigateForward_KeyShortcut, "navigate.forward" },
                            { upArrow_Icon " ${menu.parent}", navigateParent_KeyShortcut, "navigate.parent" },
                            { upArrowBar_Icon " ${menu.root}", navigateRoot_KeyShortcut, "navigate.root" }
                        }, 4);
#else
                        /* Compact navigation actions. */
                        pushBackN_Array(
                            &items,
                            (iMenuItem[]){
                                           { ">>>" backArrow_Icon, navigateBack_KeyShortcut, "navigate.back" },
                                           { ">>>" forwardArrow_Icon, navigateForward_KeyShortcut, "navigate.forward" },
                                           { ">>>" upArrow_Icon, navigateParent_KeyShortcut, "navigate.parent" },
                                           { ">>>" upArrowBar_Icon, navigateRoot_KeyShortcut, "navigate.root" },
                                           }, 4);
#endif
                        pushBackN_Array(
                            &items,
                            (iMenuItem[]){
                                { "---" },
                                { reload_Icon " ${menu.reload}", reload_KeyShortcut, "navigate.reload" },
                                { "---" },
                                { bookmark_Icon " ${menu.page.bookmark}", bookmarkPage_KeyShortcut, "bookmark.add" },
                                { star_Icon " ${menu.page.subscribe}", subscribeToPage_KeyShortcut, "feeds.subscribe" },
                                { "---" },
                                { d->flags & viewSource_DocumentWidgetFlag ? "${menu.viewformat.gemini}"
                                                                           : "${menu.viewformat.plain}",
                                  0, 0, "document.viewformat" },
                                { hammer_Icon " ${menu.tools}", 0, 0, "submenu id:toolsmenu" },
                                { "---" },
                                { "${menu.page.copyurl}", 0, 0, "document.copylink" }, },
                            10);
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
                updateHover_DocumentView(view, mouseCoord_Window(get_Window(), ev->button.which));
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
                init_PlayerUI(&ui, plr, runRect_DocumentView(view, d->grabbedPlayer));
                float off = (float) delta_Click(&d->click).x / (float) width_Rect(ui.volumeSlider);
                setVolume_Player(plr, d->grabbedStartVolume + off);
                refresh_Widget(w);
                return iTrue;
            }
#endif /* LAGRANGE_ENABLE_AUDIO */
            /* Fold/unfold a preformatted block. */
            if (~d->flags & selecting_DocumentWidgetFlag && view->hoverPre &&
                prefs_App()->collapsePre != always_Collapse &&
                prefs_App()->collapsePre != never_Collapse &&
                preIsFolded_GmDocument(view->doc, preId_GmRun(view->hoverPre))) {
                return iTrue;
            }
            /* Begin selecting a range of text. */
            if (~d->flags & selecting_DocumentWidgetFlag) {
                beginMarkingSelection_DocumentWidget_(d, d->click.startPos);
            }
            continueMarkingSelection_DocumentWidget_(d);
            /* Set scroll speed depending on position near the top/bottom. */ {
                const iRect bounds = bounds_Widget(w);
                const int autoScrollRegion = gap_UI * (isMobile_Platform() ? 15 : d->view->pageMargin);
                const int y = pos_Click(&d->click).y;
                float delta = 0.0f;
                if (y < top_Rect(bounds) + autoScrollRegion) {
                    delta = (y - top_Rect(bounds) - autoScrollRegion) / (float) autoScrollRegion;
                }
                else if (y > bottom_Rect(bounds) - autoScrollRegion) {
                    delta = (y - bottom_Rect(bounds) + autoScrollRegion) / (float) autoScrollRegion;
                }
                float speed = iClamp(fabsf(delta * delta * delta), 0, 1) * gap_Text * 150;
                if (speed != 0.0f) {
                    setValueSpeed_Anim(&d->view->scrollY.pos,
                                       delta < 0 ? 0.0f : d->view->scrollY.max,
                                       speed);
                    refreshWhileScrolling_DocumentWidget(d);
                }
                else {
                    stop_Anim(&d->view->scrollY.pos);
                }
            }
//            printf("mark %zu ... %zu {%s}\n", d->selectMark.start - cstr_String(source_GmDocument(d->view->doc)),
//                   d->selectMark.end - cstr_String(source_GmDocument(d->view->doc)),
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
            stop_Anim(&d->view->scrollY.pos);
            if (isVisible_Widget(d->menu)) {
                closeMenu_Widget(d->menu);
            }
            d->flags &= ~(movingSelectMarkStart_DocumentWidgetFlag |
                          movingSelectMarkEnd_DocumentWidgetFlag |
                          selecting_DocumentWidgetFlag);
            if (!isMoved_Click(&d->click)) {
                setFocus_Widget(NULL);
                /* Tap in tap selection mode. */
                if (flags_Widget(w) & touchDrag_WidgetFlag) {
                    const iRangecc tapLoc = sourceLoc_DocumentView(view, pos_Click(&d->click));
                    /* Tapping on the selection will show a menu. */
                    const iRangecc mark = selectionMark_DocumentWidget(d);
                    if (tapLoc.start >= mark.start && tapLoc.end <= mark.end) {
                        if (d->copyMenu) {
                            closeMenu_Widget(d->copyMenu);
                            destroy_Widget(d->copyMenu);
                            d->copyMenu = NULL;
                        }
                        const iMenuItem items[] = {
                            { clipCopy_Icon " ${menu.copy}", 0, 0, "copy" },
                            { "---" },
                            { magnifyingGlass_Icon " ${menu.search}", 0, 0,
                                format_CStr("search newtab:1 query:%s",
                                            cstr_String(selectedText_DocumentWidget_(d))) },
                            { add_Icon " ${menu.snippet.add}", 0, 0,
                                format_CStr("!snippet.add content:%s",
                                            cstr_String(selectedText_DocumentWidget_(d))) },
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
                                    iMediaRequest *req = findMediaRequest_DocumentWidget(d, linkId);
                                    if (!isFinished_GmRequest(req->req)) {
                                        cancel_GmRequest(req->req);
                                        removeMediaRequest_DocumentWidget_(d, linkId);
                                        /* Note: Some of the audio IDs have changed now, layout must
                                           be redone. */
                                    }
                                }
                                redoLayout_GmDocument(view->doc);
                                view->hoverLink = NULL;
                                clampScroll_DocumentView(view);
                                updateVisible_DocumentView(view);
                                invalidate_DocumentWidget_(d);
                                refresh_Widget(w);
                                return iTrue;
                            }
                            else {
                                /* Show the existing content again if we have it. */
                                iMediaRequest *req = findMediaRequest_DocumentWidget(d, linkId);
                                if (req) {
                                    setData_Media(media_GmDocument(view->doc),
                                                  linkId,
                                                  meta_GmRequest(req->req),
                                                  body_GmRequest(req->req),
                                                  allowHide_MediaFlag);
                                    redoLayout_GmDocument(view->doc);
                                    updateVisible_DocumentView(view);
                                    invalidate_DocumentWidget_(d);
                                    refresh_Widget(w);
                                    return iTrue;
                                }
                            }
                        }
                        refresh_Widget(w);
                    }
                    else if (linkMedia.type == download_MediaType ||
                             findMediaRequest_DocumentWidget(d, linkId)) {
                        /* TODO: What should be done when clicking on an inline download?
                           Maybe dismiss if finished? */
                        return iTrue;
                    }
                    else if (linkFlags & supportedScheme_GmLinkFlag) {
                        int tabMode = openTabMode_Sym(modState_Keys());
                        if (isPinned_DocumentWidget_(d)) {
                            tabMode ^= otherRoot_OpenTabFlag;
                        }
                        postOpenLinkCommand_DocumentWidget_(d, linkId, tabMode);
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
            iChangeFlags(d->flags, selecting_DocumentWidgetFlag, iFalse);
            stop_Anim(&d->view->scrollY.pos);
            return iTrue;
        default:
            break;
    }
    return processEvent_Widget(w, ev);
}

#if 0
static void checkPendingInvalidation_DocumentWidget_(const iDocumentWidget *d) {
    if (d->flags & invalidationPending_DocumentWidgetFlag &&
        !isAffectedByVisualOffset_Widget(constAs_Widget(d))) {
        //        printf("%p visoff: %d\n", d, left_Rect(bounds_Widget(w)) - left_Rect(boundsWithoutVisualOffset_Widget(w)));
        iDocumentWidget *m = (iDocumentWidget *) d; /* Hrrm, not const... */
        m->flags &= ~invalidationPending_DocumentWidgetFlag;
        invalidate_DocumentWidget_(m);
    }
}
#endif

void updateHoverLinkInfo_DocumentWidget(iDocumentWidget *d, iGmLinkId linkId) {
    if (update_LinkInfo(d->linkInfo,
                        d,
                        linkId,
                        width_Widget(constAs_Widget(d)))) {
        animate_DocumentWidget(d);
    }
}

void aboutToScrollView_DocumentWidget(iDocumentWidget *d, int scrollMax) {
    iChangeFlags(d->view->flags,
                 centerVertically_DocumentViewFlag,
                 prefs_App()->centerShortDocs || startsWithCase_String(d->mod.url, "about:") ||
                     !isSuccess_GmStatusCode(d->sourceStatus));
    iScrollWidget *scrollBar = d->scroll;
    const iRangei visRange  = visibleRange_DocumentView(d->view);
    //    printf("visRange: %d...%d\n", visRange.start, visRange.end);
    const iRect   bounds    = bounds_Widget(as_Widget(d));
    /* Reposition the footer buttons as appropriate. */
    setRange_ScrollWidget(scrollBar, (iRangei){ 0, scrollMax });
    const int docSize = pageHeight_DocumentView(d->view) + footerHeight_DocumentWidget(d);
    const float scrollPos = pos_SmoothScroll(&d->view->scrollY);
    setThumb_ScrollWidget(scrollBar,
                          pos_SmoothScroll(&d->view->scrollY),
                          docSize > 0 ? height_Rect(bounds) * size_Range(&visRange) / docSize : 0);
    if (d->footerButtons) {
        const iRect bounds    = bounds_Widget(as_Widget(d));
        const iRect docBounds = documentBounds_DocumentView(d->view);
        const int   hPad      = (width_Rect(bounds) - iMin(120 * gap_UI, width_Rect(docBounds))) / 2;
        const int   vPad      = 3 * gap_UI;
        setPadding_Widget(d->footerButtons, hPad, 0, hPad, vPad);
        d->footerButtons->rect.pos.y = height_Rect(bounds) -
                                       footerHeight_DocumentWidget(d) +
                                       (scrollMax > 0 ? scrollMax - scrollPos : 0);
    }
}

void didScrollView_DocumentWidget(iDocumentWidget *d) {
    animateMedia_DocumentWidget_(d);
    /* Remember scroll positions of recently visited pages. */ {
        iAssert(~d->widget.flags & destroyPending_WidgetFlag);
        iRecentUrl *recent = mostRecentUrl_History(d->mod.history);
        if (recent && size_GmDocument(d->view->doc).y > 0 && d->state == ready_RequestState &&
            equal_String(&recent->url, d->mod.url)) {
            recent->normScrollY = normScrollPos_DocumentView(d->view);
        }
    }
    /* After scrolling/resizing stops, begin pre-rendering the visbuf contents. */ {
        removeTicker_App(prerender_DocumentView, d->view);
        remove_Periodic(periodic_App(), d);
        if (~as_Widget(d)->flags & destroyPending_WidgetFlag) {
            add_Periodic(periodic_App(), d, "document.render");
        }
    }
}

static void drawViewOrBlank_DocumentWidget_(const iDocumentWidget *d, const iDocumentView *view,
                                            int offset, iBool isBlank) {
    iRect bounds = bounds_Widget(constAs_Widget(d));
    if (view && !isBlank) {
        draw_DocumentView(view, offset);
    }
    else {
        if (isCoveringTopSafeArea_DocumentView(d->view)) {
            adjustEdges_Rect(&bounds, -top_Rect(bounds), 0, 0, 0);
        }
        iPaint p;
        init_Paint(&p);
        fillRect_Paint(&p,
                       intersect_Rect(moved_Rect(bounds, init_I2(offset, 0)), bounds),
                       uiBackground_ColorId);
        drawLogo_MainWindow(get_MainWindow(), moved_Rect(bounds, init_I2(offset, 0)));
    }
}

static void draw_DocumentWidget_(const iDocumentWidget *d) {
    const iWidget *w          = constAs_Widget(d);
    const iRect    bounds     = bounds_Widget(w);
    const iRect    clipBounds = bounds;
    if (width_Rect(bounds) <= 0) {
        return;
    }
    iPaint p;
    init_Paint(&p);
    /* Views. */
    if (d->swipeView) {
        const int underlayOffset = 0.25f * (value_Anim(&d->swipeOffset) - width_Rect(bounds));
        const int overlayOffset  = value_Anim(&d->swipeOffset);
        const iDocumentView *under, *over;
        if (d->flags & swipeViewOverlay_DocumentWidgetFlag) {
            over  = d->swipeView;
            under = d->view;
            if (over == under) {
                under = NULL;
            }
        }
        else {
            over  = d->view;
            under = d->swipeView;
            if (over == under) {
                over = NULL;
            }
        }
        drawViewOrBlank_DocumentWidget_(d, under, underlayOffset, iFalse);
        if (overlayOffset > 0) {
            /* Dim the occluded view with a soft shadow. */
            iRect safeBounds = bounds;
            if (isCoveringTopSafeArea_DocumentView(d->view)) {
                adjustEdges_Rect(&safeBounds, -top_Rect(safeBounds), 0, 0, 0);
            }
            SDL_SetRenderDrawBlendMode(renderer_Window(get_Window()), SDL_BLENDMODE_BLEND);
            iRect dimRect = initCorners_Rect(
                topLeft_Rect(safeBounds),
                addX_I2(bottomLeft_Rect(safeBounds), overlayOffset)
            );
            setClip_Paint(&p, dimRect);
            const float relativeOffset =
                iClamp(value_Anim(&d->swipeOffset) / (float) width_Rect(safeBounds), 0, 1);
            const float darkness = isDark_ColorTheme(prefs_App()->theme) ? 0.4f : 0.25f;
            drawSoftShadow_Paint(&p, moved_Rect(safeBounds, init_I2(overlayOffset, 0)),
                                 gap_UI * 80,
                                 black_ColorId,
                                 255 * darkness * (1.0f - relativeOffset));
            unsetClip_Paint(&p);
            SDL_SetRenderDrawBlendMode(renderer_Window(get_Window()), SDL_BLENDMODE_BLEND);
        }
        drawViewOrBlank_DocumentWidget_(d, over, overlayOffset, iFalse);
    }
    else {
        int offset = value_Anim(&d->swipeOffset);
        if (offset && isCoveringTopSafeArea_DocumentView(d->view)) {
            /* Blank the safe area as well. */
            fillRect_Paint(&p,
                           initCorners_Rect(zero_I2(), topRight_Rect(bounds)),
                           uiBackground_ColorId);
        }
        drawViewOrBlank_DocumentWidget_(d, d->view, offset,
                                        (d->flags & viewWasSwipedAway_DocumentWidgetFlag) != 0);
    }
    if (colorTheme_App() == pureWhite_ColorTheme &&
        !(prefs_App()->bottomNavBar && prefs_App()->bottomTabBar)) {
        /* A subtle separator between UI and content. */
        drawHLine_Paint(&p, topLeft_Rect(bounds), width_Rect(bounds), uiSeparator_ColorId);
    }
    /* Sidebar swipe indicator. */
    if (deviceType_App() == tablet_AppDeviceType && prefs_App()->edgeSwipe) {
        iWindow * win    = get_Window();
        const int gap    = 4 * win->pixelRatio; /* not dependent on UI scaling; cf. Home indicator */
        const int indGap = 6 * gap;
        const int indMar = 6 * gap / 3;
        const int indHgt = sidebarSwipeAreaHeight_DocumentWidget_(d) - 2 * indGap;
        const int indPos = prefs_App()->bottomNavBar
                                ? (bottom_Rect(bounds) - indHgt - indGap)
                                : (top_Rect(bounds) + indGap);
        const int indThick = 5 * gap / 3;
        iRect indRect = (iRect){ init_I2(left_Rect(bounds) + indMar, indPos),
                                 init_I2(indThick, indHgt) };
        iRoot *leftSideRoot  = win->roots[0];
        iRoot *rightSideRoot = (win->roots[1] ? win->roots[1] : win->roots[0]);
        /* TODO: Could look up and save these pointers ahead of time, they don't change. */
        iWidget *sbar[2] = {
            findChild_Widget(leftSideRoot->widget, "sidebar"),
            findChild_Widget(rightSideRoot->widget, "sidebar2")
        };
        SDL_SetRenderDrawBlendMode(renderer_Window(get_Window()), SDL_BLENDMODE_BLEND);
        p.alpha = isDark_ColorTheme(prefs_App()->theme) ? 0x60 : 0x80;
        if (w->root == leftSideRoot && !isVisible_Widget(sbar[0])) {
            fillRect_Paint(&p, indRect, tmQuoteIcon_ColorId);
        }
        if (w->root == rightSideRoot && !isVisible_Widget(sbar[1])) {
            indRect.pos.x = right_Rect(bounds) - indMar - indThick;
            fillRect_Paint(&p, indRect, tmQuoteIcon_ColorId);
        }
        p.alpha = 0xff;
        SDL_SetRenderDrawBlendMode(renderer_Window(get_Window()), SDL_BLENDMODE_NONE);
    }
    /* Pull action indicator. */
    if (deviceType_App() != desktop_AppDeviceType) {
        float pullPos = pullActionPos_SmoothScroll(&d->view->scrollY);
        /* Account for the part where the indicator isn't yet visible. */
        pullPos = (pullPos - 0.2f) / 0.8f;
        iRect indRect = initCentered_Rect(init_I2(mid_Rect(bounds).x,
                                                  top_Rect(bounds) - 5 * gap_UI -
                                                  pos_SmoothScroll(&d->view->scrollY)),
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
                        d,
                        d->view->hoverLink ? d->view->hoverLink->linkId : 0,
                        width_Rect(bounds) - 2 * pad);
        const iInt2 infoSize = size_LinkInfo(d->linkInfo);
        iInt2 infoPos = add_I2(bottomLeft_Rect(bounds), init_I2(pad, -infoSize.y - pad));
        if (d->view->hoverLink) {
            const iRect runRect = runRect_DocumentView(d->view, d->view->hoverLink);
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
//    drawRect_Paint(&p, docBounds, red_ColorId);
    if (deviceType_App() == phone_AppDeviceType && document_App()) {
        /* The phone toolbar uses the palette of the active tab, but there may be other
           documents drawn before the toolbar, causing the colors to be incorrect. */
        makePaletteGlobal_GmDocument(document_App()->view->doc);
    }
}

/*----------------------------------------------------------------------------------------------*/

void init_DocumentWidget(iDocumentWidget *d) {
    iWidget *w = as_Widget(d);
    init_Widget(w);
    setId_Widget(w, format_CStr("document%03d", ++docEnum_));
    setFlags_Widget(w, hover_WidgetFlag | noBackground_WidgetFlag, iTrue);
    init_PersistentDocumentState(&d->mod);
    d->flags = 0;
    if (isAppleDesktop_Platform() || deviceType_App() != desktop_AppDeviceType) {
        d->flags |= swipeNavigable_DocumentWidgetFlag;
    }
    d->phoneToolbar = findWidget_App("bottombar");
    d->footerButtons = NULL;
    iZap(d->certExpiry);
    d->certFingerprint  = new_Block(0);
    d->certFlags        = 0;
    d->certSubject      = new_String();
    d->state            = blank_RequestState;
    d->titleUser        = new_String();
    d->request          = NULL;
    d->requestLinkId    = 0;
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
    allocView_DocumentWidget_(d);
    d->swipeView   = NULL;
    d->swipeBanner = NULL;
    init_Anim(&d->swipeOffset, 0);
    addChild_Widget(w, iClob(d->scroll = new_ScrollWidget()));
    setThumbColor_ScrollWidget(d->scroll, tmQuote_ColorId);
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
    pauseAllPlayers_Media(media_GmDocument(d->view->doc), iTrue);
    removeTicker_App(animate_DocumentWidget, d);
    removeTicker_App(prerender_DocumentView, d->view);
    removeTicker_App(refreshWhileScrolling_DocumentWidget, d);
    remove_Periodic(periodic_App(), d);
    delete_Translation(d->translation);
    delete_DocumentView(d->view);
    resetSwipeAnimation_DocumentWidget_(d);
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
    setUrl_GmDocument(d->view->doc, d->mod.url);
    const int docWidth = documentWidth_DocumentView(d->view);
    setSource_GmDocument(d->view->doc,
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

iWidget *footerButtons_DocumentWidget(const iDocumentWidget *d) {
    return d->footerButtons;
}

iScrollWidget *scrollBar_DocumentWidget(const iDocumentWidget *d) {
    return d->scroll;
}

const iGmDocument *document_DocumentWidget(const iDocumentWidget *d) {
    return d->view->doc;
}

const iBlock *sourceContent_DocumentWidget(const iDocumentWidget *d) {
    return &d->sourceContent;
}

iTime sourceTime_DocumentWidget(const iDocumentWidget *d) {
    return d->sourceTime;
}

int documentWidth_DocumentWidget(const iDocumentWidget *d) {
    return documentWidth_DocumentView(d->view);
}

iBool isSourceTextView_DocumentWidget(const iDocumentWidget *d) {
    return (d->flags & viewSource_DocumentWidgetFlag) != 0;
}

const iGmIdentity *identity_DocumentWidget(const iDocumentWidget *d) {
    /* The document may override the default identity. */
    const iGmIdentity *ident = findIdentity_GmCerts(certs_App(), d->mod.setIdentity);
    if (ident) {
        return ident;
    }
    return identityForUrl_GmCerts(certs_App(), url_DocumentWidget(d));
}

int generation_DocumentWidget(const iDocumentWidget *d) {
    return d->mod.generation;
}

iBool isIdentityPinned_DocumentWidget(const iDocumentWidget *d) {
    return !isEmpty_Block(d->mod.setIdentity);
}

const iString *feedTitle_DocumentWidget(const iDocumentWidget *d) {
    if (!isEmpty_String(title_GmDocument(d->view->doc))) {
        return title_GmDocument(d->view->doc);
    }
    return bookmarkTitle_DocumentWidget(d);
}

const iString *bookmarkTitle_DocumentWidget(const iDocumentWidget *d) {
    iStringArray *title = iClob(new_StringArray());
    if (!isEmpty_String(title_GmDocument(d->view->doc))) {
        pushBack_StringArray(title, title_GmDocument(d->view->doc));
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

void serializeState_DocumentWidget(const iDocumentWidget *d, iStream *outs, iBool withContent) {
    serializeWithContent_PersistentDocumentState_(&d->mod, outs, withContent);
}

void deserializeState_DocumentWidget(iDocumentWidget *d, iStream *ins) {
    if (d) {
        deserialize_PersistentDocumentState(&d->mod, ins);
        parseUser_DocumentWidget_(d);
        updateFromHistory_DocumentWidget_(d, iTrue);
    }
    else {
        /* Read and throw away the data. */
        iPersistentDocumentState *dummy = new_PersistentDocumentState();
        deserialize_PersistentDocumentState(dummy, ins);
        delete_PersistentDocumentState(dummy);
    }
}

void setUrlFlags_DocumentWidget(iDocumentWidget *d, const iString *url, int setUrlFlags,
                                const iBlock *setIdent) {
    const iBool allowCache     = (setUrlFlags & useCachedContentIfAvailable_DocumentWidgetSetUrlFlag) != 0;
    const iBool allowCachedDoc = (setUrlFlags & disallowCachedDocument_DocumentWidgetSetUrlFlag) == 0;
    iChangeFlags(d->flags, preventInlining_DocumentWidgetFlag,
                 setUrlFlags & preventInlining_DocumentWidgetSetUrlFlag);
    iChangeFlags(d->flags, waitForIdle_DocumentWidgetFlag,
                 setUrlFlags & waitForOtherDocumentsToIdle_DocumentWidgetSetUrlFag);
    d->flags |= goBackOnStop_DocumentWidgetFlag;
    setLinkNumberMode_DocumentWidget_(d, iFalse);
    setUrl_DocumentWidget_(d, urlFragmentStripped_String(url));
    if (setIdent) {
        setIdentity_DocumentWidget(d, setIdent);
    }
    /* See if there a username in the URL. */
    parseUser_DocumentWidget_(d);
    if (!allowCache || !updateFromHistory_DocumentWidget_(d, allowCachedDoc)) {
        fetch_DocumentWidget_(d);
        if (setIdent) {
            setIdentity_History(d->mod.history, setIdent);
        }
    }
}

void setUrlAndSource_DocumentWidget(iDocumentWidget *d, const iString *url, const iString *mime,
                                    const iBlock *source, float normScrollY) {
    setLinkNumberMode_DocumentWidget_(d, iFalse);
    d->flags |= preventInlining_DocumentWidgetFlag;
    setUrl_DocumentWidget_(d, url);
    parseUser_DocumentWidget_(d);
    iGmResponse *resp = new_GmResponse();
    resp->statusCode = success_GmStatusCode;
    initCurrent_Time(&resp->when);
    set_String(&resp->meta, mime);
    set_Block(&resp->body, source);
    updateFromCachedResponse_DocumentWidget_(d, normScrollY, resp, NULL);
    updateBanner_DocumentWidget_(d);
    delete_GmResponse(resp);
}

iDocumentWidget *duplicate_DocumentWidget(const iDocumentWidget *orig) {
    iDocumentWidget *d = new_DocumentWidget();
    delete_History(d->mod.history);
    d->initNormScrollY = normScrollPos_DocumentView(d->view);
    d->mod.history = copy_History(orig->mod.history);
    setUrlFlags_DocumentWidget(
        d, orig->mod.url, useCachedContentIfAvailable_DocumentWidgetSetUrlFlag |
                               /* don't share GmDocument between tabs; width may differ,
                                  and runs may be invalidated by one while others won't notice */
                               disallowCachedDocument_DocumentWidgetSetUrlFlag,
        d->mod.setIdentity);
    return d;
}

void setOrigin_DocumentWidget(iDocumentWidget *d, const iDocumentWidget *other) {
    if (d != other) {
        /* TODO: Could remember the other's ID? */
        d->mod.generation = other->mod.generation + 1;
        set_String(&d->linePrecedingLink, &other->linePrecedingLink);
    }
}

void setIdentity_DocumentWidget(iDocumentWidget *d, const iBlock *setIdent) {
    if (!setIdent || isEmpty_Block(setIdent)) {
        delete_Block(d->mod.setIdentity);
        d->mod.setIdentity = NULL;
        return;
    }
    if (!d->mod.setIdentity) {
        d->mod.setIdentity = copy_Block(setIdent);
    }
    else {
        set_Block(d->mod.setIdentity, setIdent);
    }
}

void setUrl_DocumentWidget(iDocumentWidget *d, const iString *url) {
    setUrlFlags_DocumentWidget(d, url, 0, NULL);
}

void setInitialScroll_DocumentWidget(iDocumentWidget *d, float normScrollY) {
    d->initNormScrollY = normScrollY;
}

void setRedirectCount_DocumentWidget(iDocumentWidget *d, int count) {
    d->redirectCount = count;
}

iBool isRequestOngoing_DocumentWidget(const iDocumentWidget *d) {
    if (d) {
        return d->request != NULL || d->flags & pendingRedirect_DocumentWidgetFlag;
    }
    return iFalse;
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
    iDocumentView *view = d->view;
    updateDocumentWidthRetainingScrollPosition_DocumentView(view, iFalse);
    resetWideRuns_DocumentView(view);
    updateDrawBufs_DocumentView(view, updateSideBuf_DrawBufsFlag);
    updateVisible_DocumentView(view);
    setWidth_Banner(d->banner, documentWidth_DocumentView(view));
    invalidate_DocumentWidget_(d);
    arrange_Widget(d->footerButtons);
}

static void sizeChanged_DocumentWidget_(iDocumentWidget *d) {
    updateSize_DocumentWidget(d);
}

iBeginDefineSubclass(DocumentWidget, Widget)
    .processEvent = (iAny *) processEvent_DocumentWidget_,
    .draw         = (iAny *) draw_DocumentWidget_,
    .sizeChanged  = (iAny *) sizeChanged_DocumentWidget_,
iEndDefineSubclass(DocumentWidget)
