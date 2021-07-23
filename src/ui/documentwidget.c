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

/* TODO: This file is a little (!) too large. DocumentWidget could be split into
   a couple of smaller objects. One for rendering the document, for instance. */

#include "documentwidget.h"

#include "app.h"
#include "audio/player.h"
#include "bookmarks.h"
#include "command.h"
#include "defs.h"
#include "gempub.h"
#include "gmcerts.h"
#include "gmdocument.h"
#include "gmrequest.h"
#include "gmutil.h"
#include "history.h"
#include "indicatorwidget.h"
#include "inputwidget.h"
#include "keys.h"
#include "labelwidget.h"
#include "media.h"
#include "paint.h"
#include "periodic.h"
#include "root.h"
#include "mediaui.h"
#include "scrollwidget.h"
#include "touch.h"
#include "translation.h"
#include "uploadwidget.h"
#include "util.h"
#include "visbuf.h"
#include "visited.h"

#if defined (iPlatformAppleMobile)
#   include "ios.h"
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
    int            flags;
    SDL_Texture *  sideIconBuf;
    iTextBuf *     timestampBuf;
    uint32_t       lastRenderTime;
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

static void animate_DocumentWidget_             (void *ticker);
static void animateMedia_DocumentWidget_        (iDocumentWidget *d);
static void updateSideIconBuf_DocumentWidget_   (const iDocumentWidget *d);
static void prerender_DocumentWidget_           (iAny *);
static void scrollBegan_DocumentWidget_         (iAnyObject *, int, uint32_t);

static const int smoothDuration_DocumentWidget_(enum iScrollType type) {
    return 600 /* milliseconds */ * scrollSpeedFactor_Prefs(prefs_App(), type);
}

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
    openedFromSidebar_DocumentWidgetFlag     = iBit(14),
};

enum iDocumentLinkOrdinalMode {
    numbersAndAlphabet_DocumentLinkOrdinalMode,
    homeRow_DocumentLinkOrdinalMode,
};

struct Impl_DocumentWidget {
    iWidget        widget;
    int            flags;
    
    /* User interface: */
    enum iDocumentLinkOrdinalMode ordinalMode;
    size_t         ordinalBase;
    iRangecc       selectMark;
    iRangecc       initialSelectMark; /* for word/line selection */
    iRangecc       foundMark;
    const iGmRun * grabbedPlayer; /* currently adjusting volume in a player */
    float          grabbedStartVolume;
    int            mediaTimer;
    const iGmRun * hoverPre;    /* for clicking */
    const iGmRun * hoverAltPre; /* for drawing alt text */
    const iGmRun * hoverLink;
    const iGmRun * contextLink;
    iClick         click;
    iInt2          contextPos; /* coordinates of latest right click */
    int            pinchZoomInitial;
    int            pinchZoomPosted;
    iString        pendingGotoHeading;
    
    /* Network request: */
    enum iRequestState state;
    iGmRequest *   request;
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
    iBlock         sourceContent; /* original content as received, for saving */
    iTime          sourceTime;
    iGempub *      sourceGempub; /* NULL unless the page is Gempub content */
    iGmDocument *  doc;
    
    /* Rendering: */
    int            pageMargin;
    float          initNormScrollY;
    iSmoothScroll  scrollY;
    iAnim          sideOpacity;
    iAnim          altTextOpacity;
    iGmRunRange    visibleRuns;
    iPtrArray      visibleLinks;
    iPtrArray      visiblePre;
    iPtrArray      visibleMedia; /* currently playing audio / ongoing downloads */   
    iPtrArray      visibleWideRuns; /* scrollable blocks; TODO: merge into `visiblePre` */
    iArray         wideRunOffsets;
    iAnim          animWideRunOffset;
    uint16_t       animWideRunId;
    iGmRunRange    animWideRunRange;
    iDrawBufs *    drawBufs; /* dynamic state for drawing */
    iVisBuf *      visBuf;    
    iVisBufMeta *  visBufMeta;
    iGmRunRange    renderRuns;
    iPtrSet *      invalidRuns;
    
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

static int docEnum_ = 0;

void init_DocumentWidget(iDocumentWidget *d) {
    iWidget *w = as_Widget(d);
    init_Widget(w);
    setId_Widget(w, format_CStr("document%03d", ++docEnum_));
    setFlags_Widget(w, hover_WidgetFlag | noBackground_WidgetFlag, iTrue);
    if (deviceType_App() != desktop_AppDeviceType) {
        setFlags_Widget(w, leftEdgeDraggable_WidgetFlag | rightEdgeDraggable_WidgetFlag |
                        horizontalOffset_WidgetFlag, iTrue);
    }
    init_PersistentDocumentState(&d->mod);
    d->flags           = 0;
    d->phoneToolbar    = NULL;
    d->footerButtons   = NULL;
    iZap(d->certExpiry);
    d->certFingerprint  = new_Block(0);
    d->certFlags        = 0;
    d->certSubject      = new_String();
    d->state            = blank_RequestState;
    d->titleUser        = new_String();
    d->request          = NULL;
    d->isRequestUpdated = iFalse;
    d->media            = new_ObjectList();
    d->doc              = new_GmDocument();
    d->redirectCount    = 0;
    d->ordinalBase      = 0;
    d->initNormScrollY  = 0;
    init_SmoothScroll(&d->scrollY, w, scrollBegan_DocumentWidget_);
    d->animWideRunId = 0;
    init_Anim(&d->animWideRunOffset, 0);
    d->selectMark       = iNullRange;
    d->foundMark        = iNullRange;
    d->pageMargin       = 5;
    d->hoverPre         = NULL;
    d->hoverAltPre      = NULL;
    d->hoverLink        = NULL;
    d->contextLink      = NULL;
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
    d->invalidRuns = new_PtrSet();
    init_Anim(&d->sideOpacity, 0);
    init_Anim(&d->altTextOpacity, 0);
    d->sourceStatus = none_GmStatusCode;
    init_String(&d->sourceHeader);
    init_String(&d->sourceMime);
    init_Block(&d->sourceContent, 0);
    iZap(d->sourceTime);
    d->sourceGempub = NULL;
    init_PtrArray(&d->visibleLinks);
    init_PtrArray(&d->visiblePre);
    init_PtrArray(&d->visibleWideRuns);
    init_Array(&d->wideRunOffsets, sizeof(int));
    init_PtrArray(&d->visibleMedia);
    d->grabbedPlayer = NULL;
    d->mediaTimer    = 0;
    init_String(&d->pendingGotoHeading);
    init_Click(&d->click, d, SDL_BUTTON_LEFT);
    addChild_Widget(w, iClob(d->scroll = new_ScrollWidget()));
    d->menu         = NULL; /* created when clicking */
    d->playerMenu   = NULL;
    d->copyMenu     = NULL;
    d->drawBufs     = new_DrawBufs();
    d->translation  = NULL;
    addChildFlags_Widget(w,
                         iClob(new_IndicatorWidget()),
                         resizeToParentWidth_WidgetFlag | resizeToParentHeight_WidgetFlag);
#if !defined (iPlatformAppleDesktop) /* in system menu */
    addAction_Widget(w, reload_KeyShortcut, "navigate.reload");
    addAction_Widget(w, closeTab_KeyShortcut, "tabs.close");
    addAction_Widget(w, SDLK_d, KMOD_PRIMARY, "bookmark.add");
    addAction_Widget(w, subscribeToPage_KeyModifier, "feeds.subscribe");
#endif
    addAction_Widget(w, navigateBack_KeyShortcut, "navigate.back");
    addAction_Widget(w, navigateForward_KeyShortcut, "navigate.forward");
    addAction_Widget(w, navigateParent_KeyShortcut, "navigate.parent");
    addAction_Widget(w, navigateRoot_KeyShortcut, "navigate.root");
}

void deinit_DocumentWidget(iDocumentWidget *d) {
    pauseAllPlayers_Media(media_GmDocument(d->doc), iTrue);
    removeTicker_App(animate_DocumentWidget_, d);
    removeTicker_App(prerender_DocumentWidget_, d);
    remove_Periodic(periodic_App(), d);
    delete_Translation(d->translation);
    delete_DrawBufs(d->drawBufs);
    delete_VisBuf(d->visBuf);
    free(d->visBufMeta);
    delete_PtrSet(d->invalidRuns);
    iRelease(d->media);
    iRelease(d->request);
    delete_Gempub(d->sourceGempub);
    deinit_String(&d->pendingGotoHeading);
    deinit_Block(&d->sourceContent);
    deinit_String(&d->sourceMime);
    deinit_String(&d->sourceHeader);
    iRelease(d->doc);
    if (d->mediaTimer) {
        SDL_RemoveTimer(d->mediaTimer);
    }
    deinit_Array(&d->wideRunOffsets);
    deinit_PtrArray(&d->visibleMedia);
    deinit_PtrArray(&d->visibleWideRuns);
    deinit_PtrArray(&d->visiblePre);
    deinit_PtrArray(&d->visibleLinks);
    delete_Block(d->certFingerprint);
    delete_String(d->certSubject);
    delete_String(d->titleUser);
    deinit_PersistentDocumentState(&d->mod);
}

static iRangecc selectMark_DocumentWidget_(const iDocumentWidget *d) {
    /* Normalize so start < end. */
    iRangecc norm = d->selectMark;
    if (norm.start > norm.end) {
        iSwap(const char *, norm.start, norm.end);
    }
    return norm;
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
    iChangeFlags(d->flags, showLinkNumbers_DocumentWidgetFlag, set);
    /* Children have priority when handling events. */
    enableActions_DocumentWidget_(d, !set);
    if (d->menu) {
        setFlags_Widget(d->menu, disabled_WidgetFlag, set);
    }
}

static void resetWideRuns_DocumentWidget_(iDocumentWidget *d) {
    clear_Array(&d->wideRunOffsets);
    d->animWideRunId = 0;
    init_Anim(&d->animWideRunOffset, 0);
    iZap(d->animWideRunRange);
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

static int documentWidth_DocumentWidget_(const iDocumentWidget *d) {
    const iWidget *w        = constAs_Widget(d);
    const iRect    bounds   = bounds_Widget(w);
    const iPrefs * prefs    = prefs_App();
    const int      minWidth = 50 * gap_UI; /* lines must fit a word at least */
    const float    adjust   = iClamp((float) bounds.size.x / gap_UI / 11 - 12,
                                     -2.0f, 10.0f); /* adapt to width */
    //printf("%f\n", adjust); fflush(stdout);
    return iMini(iMax(minWidth, bounds.size.x - gap_UI * (d->pageMargin + adjust) * 2),
                 fontSize_UI * prefs->lineWidth * prefs->zoomPercent / 100);
}

static iRect documentBounds_DocumentWidget_(const iDocumentWidget *d) {
    const iRect bounds = bounds_Widget(constAs_Widget(d));
    const int   margin = gap_UI * d->pageMargin;
    iRect       rect;
    rect.size.x = documentWidth_DocumentWidget_(d);
    rect.pos.x  = mid_Rect(bounds).x - rect.size.x / 2;
    rect.pos.y  = top_Rect(bounds);
    rect.size.y = height_Rect(bounds) - margin;
    const iGmRun *banner = siteBanner_GmDocument(d->doc);
    if (!banner) {
        rect.pos.y += margin;
        rect.size.y -= margin;
    }
    if (d->flags & centerVertically_DocumentWidgetFlag) {
        const iInt2 docSize = size_GmDocument(d->doc);
        if (docSize.y < rect.size.y) {
            /* Center vertically if short. There is one empty paragraph line's worth of margin
               between the banner and the page contents. */
            const int bannerHeight = banner ? height_Rect(banner->visBounds) : 0;
            int offset = iMax(0, (rect.size.y + margin - docSize.y - bannerHeight -
                                  lineHeight_Text(paragraph_FontId)) / 2);
            rect.pos.y += offset;
            rect.size.y = docSize.y;
        }
    }
    return rect;
}

static iRect siteBannerRect_DocumentWidget_(const iDocumentWidget *d) {
    const iGmRun *banner = siteBanner_GmDocument(d->doc);
    if (!banner) {
        return zero_Rect();
    }
    const iRect docBounds = documentBounds_DocumentWidget_(d);
    const iInt2 origin = addY_I2(topLeft_Rect(docBounds), -pos_SmoothScroll(&d->scrollY));
    return moved_Rect(banner->visBounds, origin);
}

static iInt2 documentPos_DocumentWidget_(const iDocumentWidget *d, iInt2 pos) {
    return addY_I2(sub_I2(pos, topLeft_Rect(documentBounds_DocumentWidget_(d))),
                   pos_SmoothScroll(&d->scrollY));
}

static iRangei visibleRange_DocumentWidget_(const iDocumentWidget *d) {
    const int margin = !hasSiteBanner_GmDocument(d->doc) ? gap_UI * d->pageMargin : 0;
    return (iRangei){ pos_SmoothScroll(&d->scrollY) - margin,
                      pos_SmoothScroll(&d->scrollY) + height_Rect(bounds_Widget(constAs_Widget(d))) -
                          margin };
}

static void addVisible_DocumentWidget_(void *context, const iGmRun *run) {
    iDocumentWidget *d = context;
    if (~run->flags & decoration_GmRunFlag && !run->mediaId) {
        if (!d->visibleRuns.start) {
            d->visibleRuns.start = run;
        }
        d->visibleRuns.end = run;
    }
    if (run->preId) {
        pushBack_PtrArray(&d->visiblePre, run);
        if (run->flags & wide_GmRunFlag) {
            pushBack_PtrArray(&d->visibleWideRuns, run);
        }
    }
    if (run->mediaType == audio_GmRunMediaType || run->mediaType == download_GmRunMediaType) {
        iAssert(run->mediaId);
        pushBack_PtrArray(&d->visibleMedia, run);
    }
    if (run->linkId) {
        pushBack_PtrArray(&d->visibleLinks, run);
    }
}

static const iGmRun *lastVisibleLink_DocumentWidget_(const iDocumentWidget *d) {
    iReverseConstForEach(PtrArray, i, &d->visibleLinks) {
        const iGmRun *run = i.ptr;
        if (run->flags & decoration_GmRunFlag && run->linkId) {
            return run;
        }
    }
    return NULL;
}

static float normScrollPos_DocumentWidget_(const iDocumentWidget *d) {
    const int docSize = size_GmDocument(d->doc).y;
    if (docSize) {
        return pos_SmoothScroll(&d->scrollY) / (float) docSize;
    }
    return 0;
}

static int scrollMax_DocumentWidget_(const iDocumentWidget *d) {
    const iWidget *w = constAs_Widget(d);
    int sm = size_GmDocument(d->doc).y - height_Rect(bounds_Widget(w)) +
             (hasSiteBanner_GmDocument(d->doc) ? 1 : 2) * d->pageMargin * gap_UI +
             height_Widget(d->footerButtons);
    if (d->phoneToolbar) {
        sm += size_Root(w->root).y -
              top_Rect(boundsWithoutVisualOffset_Widget(d->phoneToolbar));
    }
    return sm;
}

static void invalidateLink_DocumentWidget_(iDocumentWidget *d, iGmLinkId id) {
    /* A link has multiple runs associated with it. */
    iConstForEach(PtrArray, i, &d->visibleLinks) {
        const iGmRun *run = i.ptr;
        if (run->linkId == id) {
            insert_PtrSet(d->invalidRuns, run);
        }
    }
}

static void invalidateVisibleLinks_DocumentWidget_(iDocumentWidget *d) {
    iConstForEach(PtrArray, i, &d->visibleLinks) {
        const iGmRun *run = i.ptr;
        if (run->linkId) {
            insert_PtrSet(d->invalidRuns, run);
        }
    }
}

static int runOffset_DocumentWidget_(const iDocumentWidget *d, const iGmRun *run) {
    if (run->preId && run->flags & wide_GmRunFlag) {
        if (d->animWideRunId == run->preId) {
            return -value_Anim(&d->animWideRunOffset);
        }
        const size_t numOffsets = size_Array(&d->wideRunOffsets);
        const int *offsets = constData_Array(&d->wideRunOffsets);
        if (run->preId <= numOffsets) {
            return -offsets[run->preId - 1];
        }
    }
    return 0;
}

static void invalidateWideRunsWithNonzeroOffset_DocumentWidget_(iDocumentWidget *d) {
    iConstForEach(PtrArray, i, &d->visibleWideRuns) {
        const iGmRun *run = i.ptr;
        if (runOffset_DocumentWidget_(d, run)) {
            insert_PtrSet(d->invalidRuns, run);
        }
    }
}

static void animate_DocumentWidget_(void *ticker) {
    iDocumentWidget *d = ticker;
    refresh_Widget(d);
    if (!isFinished_Anim(&d->sideOpacity) || !isFinished_Anim(&d->altTextOpacity)) {
        addTicker_App(animate_DocumentWidget_, d);
    }
}

static iBool isHoverAllowed_DocumentWidget_(const iDocumentWidget *d) {
    if (!isHover_Widget(d)) {
        return iFalse;
    }
    if (!(d->state == ready_RequestState || d->state == receivedPartialResponse_RequestState)) {
        return iFalse;
    }
    if (d->flags & noHoverWhileScrolling_DocumentWidgetFlag) {
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

static void updateHover_DocumentWidget_(iDocumentWidget *d, iInt2 mouse) {
    const iWidget *w            = constAs_Widget(d);
    const iRect    docBounds    = documentBounds_DocumentWidget_(d);
    const iGmRun * oldHoverLink = d->hoverLink;
    d->hoverPre                 = NULL;
    d->hoverLink                = NULL;
    const iInt2 hoverPos = addY_I2(sub_I2(mouse, topLeft_Rect(docBounds)), pos_SmoothScroll(&d->scrollY));
    if (isHoverAllowed_DocumentWidget_(d)) {
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
            invalidateLink_DocumentWidget_(d, oldHoverLink->linkId);
        }
        if (d->hoverLink) {
            invalidateLink_DocumentWidget_(d, d->hoverLink->linkId);
        }
        refresh_Widget(w);
    }
    /* Hovering over preformatted blocks. */
    if (isHoverAllowed_DocumentWidget_(d)) {
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
            animate_DocumentWidget_(d);
        }
    }
    else if (d->hoverPre &&
             preHasAltText_GmDocument(d->doc, d->hoverPre->preId) &&
             ~d->flags & noHoverWhileScrolling_DocumentWidgetFlag) {
        setValueSpeed_Anim(&d->altTextOpacity, 1.0f, 1.5f);
        if (!isFinished_Anim(&d->altTextOpacity)) {
            animate_DocumentWidget_(d);
        }
    }
    if (isHover_Widget(w) && !contains_Widget(constAs_Widget(d->scroll), mouse)) {
        setCursor_Window(get_Window(),
                         d->hoverLink || d->hoverPre ? SDL_SYSTEM_CURSOR_HAND
                                                     : SDL_SYSTEM_CURSOR_IBEAM);
        if (d->hoverLink &&
            linkFlags_GmDocument(d->doc, d->hoverLink->linkId) & permanent_GmLinkFlag) {
            setCursor_Window(get_Window(), SDL_SYSTEM_CURSOR_ARROW); /* not dismissable */
        }
    }
}

static void updateSideOpacity_DocumentWidget_(iDocumentWidget *d, iBool isAnimated) {
    float opacity = 0.0f;
    const iGmRun *banner = siteBanner_GmDocument(d->doc);
    if (banner && bottom_Rect(banner->visBounds) < pos_SmoothScroll(&d->scrollY)) {
        opacity = 1.0f;
    }
    setValue_Anim(&d->sideOpacity, opacity, isAnimated ? (opacity < 0.5f ? 100 : 200) : 0);
    animate_DocumentWidget_(d);
}

static uint32_t mediaUpdateInterval_DocumentWidget_(const iDocumentWidget *d) {
    if (document_App() != d) {
        return 0;
    }
    if (get_Window()->isDrawFrozen) {
        return 0;
    }
    static const uint32_t invalidInterval_ = ~0u;
    uint32_t interval = invalidInterval_;
    iConstForEach(PtrArray, i, &d->visibleMedia) {
        const iGmRun *run = i.ptr;
        if (run->mediaType == audio_GmRunMediaType) {
            iPlayer *plr = audioPlayer_Media(media_GmDocument(d->doc), run->mediaId);
            if (flags_Player(plr) & adjustingVolume_PlayerFlag ||
                (isStarted_Player(plr) && !isPaused_Player(plr))) {
                interval = iMin(interval, 1000 / 15);
            }
        }
        else if (run->mediaType == download_GmRunMediaType) {
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
        iConstForEach(PtrArray, i, &d->visibleMedia) {
            const iGmRun *run = i.ptr;
            if (run->mediaType == audio_GmRunMediaType) {
                iPlayer *plr = audioPlayer_Media(media_GmDocument(d->doc), run->mediaId);
                if (idleTimeMs_Player(plr) > 3000 && ~flags_Player(plr) & volumeGrabbed_PlayerFlag &&
                    flags_Player(plr) & adjustingVolume_PlayerFlag) {
                    setFlags_Player(plr, adjustingVolume_PlayerFlag, iFalse);
                }
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

static iRangecc currentHeading_DocumentWidget_(const iDocumentWidget *d) {
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

static int updateScrollMax_DocumentWidget_(iDocumentWidget *d) {
    arrange_Widget(d->footerButtons); /* scrollMax depends on footer height */
    const int scrollMax = scrollMax_DocumentWidget_(d);
    setMax_SmoothScroll(&d->scrollY, scrollMax);
    return scrollMax;
}

static void updateVisible_DocumentWidget_(iDocumentWidget *d) {
    iChangeFlags(d->flags,
                 centerVertically_DocumentWidgetFlag,
                 prefs_App()->centerShortDocs || startsWithCase_String(d->mod.url, "about:") ||
                     !isSuccess_GmStatusCode(d->sourceStatus));
    const iRangei visRange  = visibleRange_DocumentWidget_(d);
    const iRect   bounds    = bounds_Widget(as_Widget(d));
    const int     scrollMax = updateScrollMax_DocumentWidget_(d);
    /* Reposition the footer buttons as appropriate. */
    /* TODO: You can just position `footerButtons` here completely without having to get
       `Widget` involved with the offset in any way. */
    if (d->footerButtons) {
        const iRect bounds    = bounds_Widget(as_Widget(d));
        const iRect docBounds = documentBounds_DocumentWidget_(d);
        const int   hPad      = (width_Rect(bounds) - iMin(120 * gap_UI, width_Rect(docBounds))) / 2;
        const int   vPad      = 3 * gap_UI;
        setPadding_Widget(d->footerButtons, hPad, vPad, hPad, vPad);
        d->footerButtons->animOffsetRef = (scrollMax > 0 ? &d->scrollY.pos : NULL);
        if (scrollMax <= 0) {
            d->footerButtons->animOffsetRef = NULL;
            d->footerButtons->rect.pos.y = height_Rect(bounds) - height_Widget(d->footerButtons);
        }
        else {
            d->footerButtons->animOffsetRef = &d->scrollY.pos;
            d->footerButtons->rect.pos.y = size_GmDocument(d->doc).y + 2 * gap_UI * d->pageMargin;
        }
    }
    setRange_ScrollWidget(d->scroll, (iRangei){ 0, scrollMax });
    const int docSize = size_GmDocument(d->doc).y;
    setThumb_ScrollWidget(d->scroll,
                          pos_SmoothScroll(&d->scrollY),
                          docSize > 0 ? height_Rect(bounds) * size_Range(&visRange) / docSize : 0);
    clear_PtrArray(&d->visibleLinks);
    clear_PtrArray(&d->visibleWideRuns);
    clear_PtrArray(&d->visiblePre);
    clear_PtrArray(&d->visibleMedia);
    const iRangecc oldHeading = currentHeading_DocumentWidget_(d);
    /* Scan for visible runs. */ {
        iZap(d->visibleRuns);
        render_GmDocument(d->doc, visRange, addVisible_DocumentWidget_, d);
    }
    const iRangecc newHeading = currentHeading_DocumentWidget_(d);
    if (memcmp(&oldHeading, &newHeading, sizeof(oldHeading))) {
        d->drawBufs->flags |= updateSideBuf_DrawBufsFlag;
    }
    updateHover_DocumentWidget_(d, mouseCoord_Window(get_Window(), 0));
    updateSideOpacity_DocumentWidget_(d, iTrue);
    animateMedia_DocumentWidget_(d);
    /* Remember scroll positions of recently visited pages. */ {
        iRecentUrl *recent = mostRecentUrl_History(d->mod.history);
        if (recent && docSize && d->state == ready_RequestState) {
            recent->normScrollY = normScrollPos_DocumentWidget_(d);
        }
    }
    /* After scrolling/resizing stops, begin pre-rendering the visbuf contents. */ {
        removeTicker_App(prerender_DocumentWidget_, d);
        remove_Periodic(periodic_App(), d);
        add_Periodic(periodic_App(), d, "document.render");
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
    if (!isEmpty_String(title_GmDocument(d->doc))) {
        pushBack_StringArray(title, title_GmDocument(d->doc));
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
    }
    if (isEmpty_StringArray(title)) {
        pushBackCStr_StringArray(title, "Lagrange");
    }
    /* Take away parts if it doesn't fit. */
    const int avail = bounds_Widget(as_Widget(tabButton)).size.x - 3 * gap_UI;
    iBool setWindow = (document_App() == d && isUnderKeyRoot_Widget(d));
    for (;;) {
        iString *text = collect_String(joinCStr_StringArray(title, " \u2014 "));
        if (setWindow) {
            /* Longest version for the window title, and omit the icon. */
            setTitle_Window(get_Window(), text);
            setWindow = iFalse;
        }
        const iChar siteIcon = siteIcon_GmDocument(d->doc);
        if (siteIcon) {
            if (!isEmpty_String(text)) {
                prependCStr_String(text, "  " restore_ColorEscape);
            }
            prependChar_String(text, siteIcon);
            prependCStr_String(text, escape_Color(uiIcon_ColorId));
        }
        const int width = measureRange_Text(default_FontId, range_String(text)).advance.x;
        if (width <= avail ||
            isEmpty_StringArray(title)) {
            updateText_LabelWidget(tabButton, text);
            break;
        }
        if (size_StringArray(title) == 1) {
            /* Just truncate to fit. */
            const char *endPos;
            tryAdvanceNoWrap_Text(default_FontId,
                                  range_String(text),
                                  avail - measure_Text(default_FontId, "...").advance.x,
                                  &endPos);
            updateText_LabelWidget(
                tabButton,
                collectNewFormat_String(
                    "%s...", cstr_Rangecc((iRangecc){ constBegin_String(text), endPos })));
            break;
        }
        remove_StringArray(title, size_StringArray(title) - 1);
    }
}

static void updateTimestampBuf_DocumentWidget_(const iDocumentWidget *d) {
    if (!isExposed_Window(get_Window())) {
        return;
    }
    if (d->drawBufs->timestampBuf) {
        delete_TextBuf(d->drawBufs->timestampBuf);
        d->drawBufs->timestampBuf = NULL;
    }
    if (isValid_Time(&d->sourceTime)) {
        d->drawBufs->timestampBuf = newRange_TextBuf(
            uiLabel_FontId,
            white_ColorId,
            range_String(collect_String(format_Time(&d->sourceTime, cstr_Lang("page.timestamp")))));
    }
    d->drawBufs->flags &= ~updateTimestampBuf_DrawBufsFlag;
}

static void invalidate_DocumentWidget_(iDocumentWidget *d) {
    if (flags_Widget(as_Widget(d)) & destroyPending_WidgetFlag) {
        return;
    }
    invalidate_VisBuf(d->visBuf);
    clear_PtrSet(d->invalidRuns);
}

static iRangecc bannerText_DocumentWidget_(const iDocumentWidget *d) {
    return isEmpty_String(d->titleUser) ? range_String(bannerText_GmDocument(d->doc))
                                        : range_String(d->titleUser);
}

static void documentRunsInvalidated_DocumentWidget_(iDocumentWidget *d) {
    d->foundMark       = iNullRange;
    d->selectMark      = iNullRange;
    d->hoverPre        = NULL;
    d->hoverAltPre     = NULL;
    d->hoverLink       = NULL;
    d->contextLink     = NULL;
    iZap(d->visibleRuns);
    iZap(d->renderRuns);
}

iBool isPinned_DocumentWidget_(const iDocumentWidget *d) {
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

static void showOrHidePinningIndicator_DocumentWidget_(iDocumentWidget *d) {
    iWidget *w = as_Widget(d);
    showCollapsed_Widget(findChild_Widget(root_Widget(w), "document.pinned"),
                         isPinned_DocumentWidget_(d));
}

static void documentWasChanged_DocumentWidget_(iDocumentWidget *d) {
    updateVisitedLinks_GmDocument(d->doc);
    documentRunsInvalidated_DocumentWidget_(d);
    updateWindowTitle_DocumentWidget_(d);
    updateVisible_DocumentWidget_(d);
    d->drawBufs->flags |= updateSideBuf_DrawBufsFlag;
    invalidate_DocumentWidget_(d);
    refresh_Widget(as_Widget(d));
    /* Check for special bookmark tags. */
    d->flags &= ~otherRootByDefault_DocumentWidgetFlag;
    const uint16_t bmid = findUrl_Bookmarks(bookmarks_App(), d->mod.url);
    if (bmid) {
        const iBookmark *bm = get_Bookmarks(bookmarks_App(), bmid);
        if (hasTag_Bookmark(bm, linkSplit_BookmarkTag)) {
            d->flags |= otherRootByDefault_DocumentWidgetFlag;
        }
    }
    showOrHidePinningIndicator_DocumentWidget_(d);
    setCachedDocument_History(d->mod.history,
                              d->doc, /* keeps a ref */
                              (d->flags & openedFromSidebar_DocumentWidgetFlag) != 0);
}

void setSource_DocumentWidget(iDocumentWidget *d, const iString *source) {
    setUrl_GmDocument(d->doc, d->mod.url);
    setSource_GmDocument(d->doc,
                         source,
                         documentWidth_DocumentWidget_(d),
                         isFinished_GmRequest(d->request) ? final_GmDocumentUpdate
                                                          : partial_GmDocumentUpdate);
    documentWasChanged_DocumentWidget_(d);
}

static void replaceDocument_DocumentWidget_(iDocumentWidget *d, iGmDocument *newDoc) {
    pauseAllPlayers_Media(media_GmDocument(d->doc), iTrue);
    iRelease(d->doc);
    d->doc = ref_Object(newDoc);
    documentWasChanged_DocumentWidget_(d);
}

static void updateTheme_DocumentWidget_(iDocumentWidget *d) {
    if (isEmpty_String(d->titleUser)) {
        setThemeSeed_GmDocument(d->doc,
                                collect_Block(newRange_Block(urlHost_String(d->mod.url))));
    }
    else {
        setThemeSeed_GmDocument(d->doc, &d->titleUser->chars);
    }
    d->drawBufs->flags |= updateTimestampBuf_DrawBufsFlag;
}

static enum iGmDocumentBanner bannerType_DocumentWidget_(const iDocumentWidget *d) {
    if (d->certFlags & available_GmCertFlag) {
        const int req = domainVerified_GmCertFlag | timeVerified_GmCertFlag | trusted_GmCertFlag;
        if ((d->certFlags & req) != req) {
            return certificateWarning_GmDocumentBanner;
        }
    }
    return siteDomain_GmDocumentBanner;
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
    //setBackgroundColor_Widget(d->footerButtons, tmBackground_ColorId);
    for (size_t i = 0; i < count; ++i) {
        iLabelWidget *button = addChildFlags_Widget(
            d->footerButtons,
            iClob(newKeyMods_LabelWidget(
                items[i].label, items[i].key, items[i].kmods, items[i].command)),
            alignLeft_WidgetFlag | drawKey_WidgetFlag);
        checkIcon_LabelWidget(button);
        setFont_LabelWidget(button, uiContent_FontId);
    }
    addChild_Widget(as_Widget(d), iClob(d->footerButtons));
    arrange_Widget(d->footerButtons);
    arrange_Widget(w);
    updateVisible_DocumentWidget_(d); /* final placement for the buttons */
}

static void showErrorPage_DocumentWidget_(iDocumentWidget *d, enum iGmStatusCode code,
                                          const iString *meta) {
    iString *src = collectNewCStr_String("# ");
    const iGmError *msg = get_GmError(code);
    appendChar_String(src, msg->icon ? msg->icon : 0x2327); /* X in a box */
    appendFormat_String(src, " %s\n%s", msg->title, msg->info);
    iBool useBanner = iTrue;
    if (meta) {
        switch (code) {
            case schemeChangeRedirect_GmStatusCode:
            case tooManyRedirects_GmStatusCode:
                appendFormat_String(src, "\n=> %s\n", cstr_String(meta));
                break;
            case tlsFailure_GmStatusCode:
                useBanner = iFalse; /* valid data wasn't received from host */
                appendFormat_String(src, "\n\n>%s\n", cstr_String(meta));
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
                appendFormat_String(src, "\n\n%s", cstr_String(meta));
                break;
            case unsupportedMimeType_GmStatusCode: {
                iString *key = collectNew_String();
                toString_Sym(SDLK_s, KMOD_PRIMARY, key);
                appendFormat_String(src, "\n```\n%s\n```\n", cstr_String(meta));
                makeFooterButtons_DocumentWidget_(
                    d,
                    (iMenuItem[]){ { translateCStr_Lang(download_Icon " " saveToDownloads_Label),
                                     0,
                                     0,
                                     "document.save" } },
                    1);
                break;
            }
            case slowDown_GmStatusCode:
                appendFormat_String(src, "\n\nWait %s seconds before your next request.",
                                    cstr_String(meta));
                break;
            default:
                if (!isEmpty_String(meta)) {
                    appendFormat_String(src, "\n\n${error.server.msg}\n> %s", cstr_String(meta));
                }
                break;
        }
    }
    if (category_GmStatusCode(code) == categoryClientCertificate_GmStatus) {
        makeFooterButtons_DocumentWidget_(
            d,
            (iMenuItem[]){ { leftHalf_Icon " ${menu.show.identities}", '4', KMOD_PRIMARY, "sidebar.mode arg:3 show:1" },
                           { person_Icon " ${menu.identity.new}", newIdentity_KeyShortcut, "ident.new" } },
            2);
    }
    /* Make a new document for the error page.*/ {
        iGmDocument *errorDoc = new_GmDocument();
        setUrl_GmDocument(errorDoc, d->mod.url);
        setBanner_GmDocument(errorDoc, useBanner ? bannerType_DocumentWidget_(d) : none_GmDocumentBanner);
        setFormat_GmDocument(errorDoc, gemini_SourceFormat);
        replaceDocument_DocumentWidget_(d, errorDoc);
        iRelease(errorDoc);
    }
    translate_Lang(src);
    d->state = ready_RequestState;
    setSource_DocumentWidget(d, src);
    updateTheme_DocumentWidget_(d);
    reset_SmoothScroll(&d->scrollY);
    init_Anim(&d->sideOpacity, 0);
    init_Anim(&d->altTextOpacity, 0);
    resetWideRuns_DocumentWidget_(d);
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
        if (localPath && equal_CStr(mediaType_Path(localPath), "application/gpub+zip")) {
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
            if (preloadCoverImage_Gempub(d->sourceGempub, d->doc)) {
                redoLayout_GmDocument(d->doc);
                updateVisible_DocumentWidget_(d);
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
                        "open newtab:%d url:%s", otherRoot_OpenTabFlag, cstr_String(navStart));
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
        d->drawBufs->flags |= updateTimestampBuf_DrawBufsFlag;
        initBlock_String(&str, &response->body);
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
                if (equal_Rangecc(param, "text/gemini")) {
                    docFormat = gemini_SourceFormat;
                    setRange_String(&d->sourceMime, param);
                }
                else if (startsWith_Rangecc(param, "text/") ||
                         equal_Rangecc(param, "application/json") ||
                         equal_Rangecc(param, "application/x-pem-file") ||
                         equal_Rangecc(param, "application/pem-certificate-chain")) {
                    docFormat = plainText_SourceFormat;
                    setRange_String(&d->sourceMime, param);
                }
                else if (equal_Rangecc(param, "application/zip") ||
                         (startsWith_Rangecc(param, "application/") &&
                          endsWithCase_Rangecc(param, "+zip"))) {
                    docFormat = gemini_SourceFormat;
                    setRange_String(&d->sourceMime, param);
                    iString *key = collectNew_String();
                    toString_Sym(SDLK_s, KMOD_PRIMARY, key);
                    format_String(&str, "# %s\n", zipPageHeading_(param));
                    appendFormat_String(&str,
                                        cstr_Lang("doc.archive"),
                                        cstr_Rangecc(baseName_Path(d->mod.url)));
                    appendCStr_String(&str, "\n\n");
                    iString *localPath = localFilePathFromUrl_String(d->mod.url);
                    if (!localPath) {
                        appendFormat_String(&str, "%s\n\n",
                                            format_CStr(cstr_Lang("error.unsupported.suggestsave"),
                                                        cstr_String(key),
                                                        saveToDownloads_Label));
                    }
                    delete_String(localPath);
                    if (equalCase_Rangecc(urlScheme_String(d->mod.url), "file")) {
                        appendFormat_String(&str, "=> %s/ ${doc.archive.view}\n",
                                            cstr_String(withSpacesEncoded_String(d->mod.url)));
                    }
                    translate_Lang(&str);
                }
                else if (startsWith_Rangecc(param, "image/") ||
                         startsWith_Rangecc(param, "audio/")) {
                    const iBool isAudio = startsWith_Rangecc(param, "audio/");
                    /* Make a simple document with an image or audio player. */
                    docFormat = gemini_SourceFormat;
                    setRange_String(&d->sourceMime, param);
                    const iGmLinkId imgLinkId = 1; /* there's only the one link */
                    /* TODO: Do the image loading in `postProcessRequestContent_DocumentWidget_()` */
                    if ((isAudio && isInitialUpdate) || (!isAudio && isRequestFinished)) {
                        const char *linkTitle =
                            startsWith_String(mimeStr, "image/") ? "Image" : "Audio";
                        iUrl parts;
                        init_Url(&parts, d->mod.url);
                        if (!isEmpty_Range(&parts.path)) {
                            linkTitle =
                                baseName_Path(collect_String(newRange_String(parts.path))).start;
                        }
                        format_String(&str, "=> %s %s\n",
                                      cstr_String(canonicalUrl_String(d->mod.url)),
                                      linkTitle);
                        setData_Media(media_GmDocument(d->doc),
                                      imgLinkId,
                                      mimeStr,
                                      &response->body,
                                      !isRequestFinished ? partialData_MediaFlag : 0);
                        redoLayout_GmDocument(d->doc);
                    }
                    else if (isAudio && !isInitialUpdate) {
                        /* Update the audio content. */
                        setData_Media(media_GmDocument(d->doc),
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
                showErrorPage_DocumentWidget_(d, unsupportedMimeType_GmStatusCode, &response->meta);
                deinit_String(&str);
                return;
            }
            setFormat_GmDocument(d->doc, docFormat);
            /* Convert the source to UTF-8 if needed. */
            if (!equalCase_Rangecc(charset, "utf-8")) {
                set_String(&str,
                           collect_String(decode_Block(&str.chars, cstr_Rangecc(charset))));
            }
        }
        if (cachedDoc) {
            replaceDocument_DocumentWidget_(d, cachedDoc);
        }
        else if (setSource) {
            setSource_DocumentWidget(d, &str);
        }
        deinit_String(&str);
    }
}

static void fetch_DocumentWidget_(iDocumentWidget *d) {
    /* Forget the previous request. */
    if (d->request) {
        iRelease(d->request);
        d->request = NULL;
    }
    postCommandf_Root(as_Widget(d)->root,
                      "document.request.started doc:%p url:%s",
                      d,
                      cstr_String(d->mod.url));
    clear_ObjectList(d->media);
    d->certFlags = 0;
    setLinkNumberMode_DocumentWidget_(d, iFalse);
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
        updateTextCStr_LabelWidget(lock, gray50_ColorEscape openLock_Icon);
        return;
    }
    setFlags_Widget(as_Widget(lock), disabled_WidgetFlag, iFalse);
    const iBool isDarkMode = isDark_ColorTheme(colorTheme_App());
    if (~d->certFlags & domainVerified_GmCertFlag ||
        ~d->certFlags & trusted_GmCertFlag) {
        updateTextCStr_LabelWidget(lock, red_ColorEscape warning_Icon);
    }
    else if (~d->certFlags & timeVerified_GmCertFlag) {
        updateTextCStr_LabelWidget(lock, isDarkMode ? orange_ColorEscape warning_Icon
                                                    : black_ColorEscape warning_Icon);        
    }
    else {
        updateTextCStr_LabelWidget(lock, green_ColorEscape closedLock_Icon);
    }
    setBanner_GmDocument(d->doc, bannerType_DocumentWidget_(d));
}

static void parseUser_DocumentWidget_(iDocumentWidget *d) {
    setRange_String(d->titleUser, urlUser_String(d->mod.url));
}

static void cacheRunGlyphs_(void *data, const iGmRun *run) {
    iUnused(data);
    if (!isEmpty_Range(&run->text)) {
        cache_Text(run->textParams.font, run->text);
    }
}

static void cacheDocumentGlyphs_DocumentWidget_(const iDocumentWidget *d) {
    if (isFinishedLaunching_App() && isExposed_Window(get_Window())) {
        /* Just cache the top of the document, since this is what we usually need. */
        int maxY = height_Widget(&d->widget) * 2;
        if (maxY == 0) {
            maxY = size_GmDocument(d->doc).y;
        }
        render_GmDocument(d->doc, (iRangei){ 0, maxY }, cacheRunGlyphs_, NULL);
    }
}

static void updateFromCachedResponse_DocumentWidget_(iDocumentWidget *d, float normScrollY,
                                                     const iGmResponse *resp, iGmDocument *cachedDoc) {
    setLinkNumberMode_DocumentWidget_(d, iFalse);
    clear_ObjectList(d->media);
    delete_Gempub(d->sourceGempub);
    d->sourceGempub = NULL;
    pauseAllPlayers_Media(media_GmDocument(d->doc), iTrue);
    iRelease(d->doc);
    destroy_Widget(d->footerButtons);
    d->footerButtons = NULL;
    d->doc = new_GmDocument();
    resetWideRuns_DocumentWidget_(d);
    d->state = fetching_RequestState;
    /* Do the fetch. */ {
        d->initNormScrollY = normScrollY;
        /* Use the cached response data. */
        updateTrust_DocumentWidget_(d, resp);
        d->sourceTime   = resp->when;
        d->sourceStatus = success_GmStatusCode;
        format_String(&d->sourceHeader, cstr_Lang("pageinfo.header.cached"));
        set_Block(&d->sourceContent, &resp->body);
        updateDocument_DocumentWidget_(d, resp, cachedDoc, iTrue);
//        setCachedDocument_History(d->mod.history, d->doc,
//                                  (d->flags & openedFromSidebar_DocumentWidgetFlag) != 0);
    }
    d->state = ready_RequestState;
    postProcessRequestContent_DocumentWidget_(d, iTrue);
    init_Anim(&d->altTextOpacity, 0);
    reset_SmoothScroll(&d->scrollY);
    init_Anim(&d->scrollY.pos, d->initNormScrollY * size_GmDocument(d->doc).y);
    updateSideOpacity_DocumentWidget_(d, iFalse);
    updateVisible_DocumentWidget_(d);
    moveSpan_SmoothScroll(&d->scrollY, 0, 0); /* clamp position to new max */
    cacheDocumentGlyphs_DocumentWidget_(d);
    d->drawBufs->flags |= updateTimestampBuf_DrawBufsFlag | updateSideBuf_DrawBufsFlag;
    d->flags &= ~urlChanged_DocumentWidgetFlag;
    postCommandf_Root(
        as_Widget(d)->root, "document.changed doc:%p url:%s", d, cstr_String(d->mod.url));
}

static iBool updateFromHistory_DocumentWidget_(iDocumentWidget *d) {
    const iRecentUrl *recent = findUrl_History(d->mod.history, d->mod.url);
    if (recent && recent->cachedResponse) {
        iChangeFlags(d->flags,
                     openedFromSidebar_DocumentWidgetFlag,
                     recent->flags.openedFromSidebar);
        updateFromCachedResponse_DocumentWidget_(
            d, recent->normScrollY, recent->cachedResponse, recent->cachedDoc);
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
    iDocumentWidget *d = ptr;
    updateVisible_DocumentWidget_(d);
    refresh_Widget(d);
    if (d->animWideRunId) {
        for (const iGmRun *r = d->animWideRunRange.start; r != d->animWideRunRange.end; r++) {
            insert_PtrSet(d->invalidRuns, r);
        }
    }
    if (isFinished_Anim(&d->animWideRunOffset)) {
        d->animWideRunId = 0;
    }
    if (!isFinished_SmoothScroll(&d->scrollY) || !isFinished_Anim(&d->animWideRunOffset)) {
        addTicker_App(refreshWhileScrolling_DocumentWidget_, d);
    }
}

static void scrollBegan_DocumentWidget_(iAnyObject *any, int offset, uint32_t duration) {
    iDocumentWidget *d = any;
    /* Get rid of link numbers when scrolling. */
    if (offset && d->flags & showLinkNumbers_DocumentWidgetFlag) {
        setLinkNumberMode_DocumentWidget_(d, iFalse);
        invalidateVisibleLinks_DocumentWidget_(d);
    }
    /* Show and hide toolbar on scroll. */
    if (deviceType_App() == phone_AppDeviceType) {
        const float normPos = normScrollPos_DocumentWidget_(d);
        if (prefs_App()->hideToolbarOnScroll && iAbs(offset) > 5 && normPos >= 0) {
            showToolbar_Root(as_Widget(d)->root, offset < 0);
        }
    }
    updateVisible_DocumentWidget_(d);
    refresh_Widget(as_Widget(d));
    if (duration > 0) {
        iChangeFlags(d->flags, noHoverWhileScrolling_DocumentWidgetFlag, iTrue);
        addTicker_App(refreshWhileScrolling_DocumentWidget_, d);
    }
}

static void clampScroll_DocumentWidget_(iDocumentWidget *d) {
    move_SmoothScroll(&d->scrollY, 0);
}

static void immediateScroll_DocumentWidget_(iDocumentWidget *d, int offset) {
    move_SmoothScroll(&d->scrollY, offset);
}

static void smoothScroll_DocumentWidget_(iDocumentWidget *d, int offset, int duration) {
    moveSpan_SmoothScroll(&d->scrollY, offset, duration);
}

static void scrollTo_DocumentWidget_(iDocumentWidget *d, int documentY, iBool centered) {
    if (!hasSiteBanner_GmDocument(d->doc)) {
        documentY += d->pageMargin * gap_UI;
    }
    init_Anim(&d->scrollY.pos,
              documentY - (centered ? documentBounds_DocumentWidget_(d).size.y / 2
                                    : lineHeight_Text(paragraph_FontId)));
    clampScroll_DocumentWidget_(d);
}

static void scrollToHeading_DocumentWidget_(iDocumentWidget *d, const char *heading) {
    iConstForEach(Array, h, headings_GmDocument(d->doc)) {
        const iGmHeading *head = h.value;
        if (startsWithCase_Rangecc(head->text, heading)) {
            postCommandf_Root(as_Widget(d)->root, "document.goto loc:%p", head->text.start);
            break;
        }
    }
}

static void scrollWideBlock_DocumentWidget_(iDocumentWidget *d, iInt2 mousePos, int delta,
                                            int duration) {
    if (delta == 0) {
        return;
    }
    const iInt2 docPos = documentPos_DocumentWidget_(d, mousePos);
    iConstForEach(PtrArray, i, &d->visibleWideRuns) {
        const iGmRun *run = i.ptr;
        if (docPos.y >= top_Rect(run->bounds) && docPos.y <= bottom_Rect(run->bounds)) {
            /* We can scroll this run. First find out how much is allowed. */
            const iGmRunRange range = findPreformattedRange_GmDocument(d->doc, run);
            int maxWidth = 0;
            for (const iGmRun *r = range.start; r != range.end; r++) {
                maxWidth = iMax(maxWidth, width_Rect(r->visBounds));
            }
            const int maxOffset = maxWidth - documentWidth_DocumentWidget_(d) + d->pageMargin * gap_UI;
            if (size_Array(&d->wideRunOffsets) <= run->preId) {
                resize_Array(&d->wideRunOffsets, run->preId + 1);
            }
            int *offset = at_Array(&d->wideRunOffsets, run->preId - 1);
            const int oldOffset = *offset;
            *offset = iClamp(*offset + delta, 0, maxOffset);
            /* Make sure the whole block gets redraw. */
            if (oldOffset != *offset) {
                for (const iGmRun *r = range.start; r != range.end; r++) {
                    insert_PtrSet(d->invalidRuns, r);
                }
                refresh_Widget(d);
                d->selectMark = iNullRange;
                d->foundMark  = iNullRange;
            }
            if (duration) {
                if (d->animWideRunId != run->preId || isFinished_Anim(&d->animWideRunOffset)) {
                    d->animWideRunId = run->preId;
                    init_Anim(&d->animWideRunOffset, oldOffset);
                }
                setValueEased_Anim(&d->animWideRunOffset, *offset, duration);
                d->animWideRunRange = range;
                addTicker_App(refreshWhileScrolling_DocumentWidget_, d);
            }
            else {
                d->animWideRunId = 0;
                init_Anim(&d->animWideRunOffset, 0);
            }
            break;
        }
    }
}

static void togglePreFold_DocumentWidget_(iDocumentWidget *d, uint16_t preId) {
    d->hoverPre    = NULL;
    d->hoverAltPre = NULL;
    d->selectMark  = iNullRange;
    foldPre_GmDocument(d->doc, preId);
    redoLayout_GmDocument(d->doc);
    clampScroll_DocumentWidget_(d);
    updateHover_DocumentWidget_(d, mouseCoord_Window(get_Window(), 0));
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
    append_String(url, collect_String(urlEncode_String(userEnteredText)));
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
        d->state = receivedPartialResponse_RequestState;
        updateTrust_DocumentWidget_(d, resp);
        init_Anim(&d->sideOpacity, 0);
        init_Anim(&d->altTextOpacity, 0);
        format_String(&d->sourceHeader, "%d %s", statusCode, get_GmError(statusCode)->title);
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
                    uiTextCaution_ColorEscape "${dlg.input.send}",
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
                    else {
                        lineBreak = new_LabelWidget("${dlg.input.linebreak}", "text.insert arg:10");
                    }
                    setFlags_Widget(as_Widget(lineBreak), frameless_WidgetFlag, iTrue);
                    setTextColor_LabelWidget(lineBreak, uiTextDim_ColorId);
                }
                setId_Widget(addChildPosFlags_Widget(buttons,
                                                     iClob(new_LabelWidget("", NULL)),
                                                     front_WidgetAddPos, frameless_WidgetFlag),
                             "valueinput.counter");
                if (lineBreak && deviceType_App() != desktop_AppDeviceType) {
                    addChildPos_Widget(buttons, iClob(lineBreak), front_WidgetAddPos);
                }
                setValidator_InputWidget(findChild_Widget(dlg, "input"), inputQueryValidator_, d);
                setSensitiveContent_InputWidget(findChild_Widget(dlg, "input"),
                                                statusCode == sensitiveInput_GmStatusCode);
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
                    reset_SmoothScroll(&d->scrollY);
                }
                pauseAllPlayers_Media(media_GmDocument(d->doc), iTrue);
                iRelease(d->doc); /* new content incoming */
                d->doc = new_GmDocument();
                delete_Gempub(d->sourceGempub);
                d->sourceGempub = NULL;
                destroy_Widget(d->footerButtons);
                d->footerButtons = NULL;
                resetWideRuns_DocumentWidget_(d);
                updateDocument_DocumentWidget_(d, resp, NULL, iTrue);
                break;
            case categoryRedirect_GmStatusCode:
                if (isEmpty_String(&resp->meta)) {
                    showErrorPage_DocumentWidget_(d, invalidRedirect_GmStatusCode, NULL);
                }
                else {
                    /* Only accept redirects that use gemini scheme. */
                    const iString *dstUrl = absoluteUrl_String(d->mod.url, &resp->meta);
                    if (d->redirectCount >= 5) {
                        showErrorPage_DocumentWidget_(d, tooManyRedirects_GmStatusCode, dstUrl);
                    }
                    else if (equalCase_Rangecc(urlScheme_String(dstUrl),
                                               cstr_Rangecc(urlScheme_String(d->mod.url)))) {
                        /* Redirects with the same scheme are automatic. */
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

static iRangecc sourceLoc_DocumentWidget_(const iDocumentWidget *d, iInt2 pos) {
    return findLoc_GmDocument(d->doc, documentPos_DocumentWidget_(d, pos));
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

static const iGmRun *middleRun_DocumentWidget_(const iDocumentWidget *d) {
    iRangei visRange = visibleRange_DocumentWidget_(d);
    iMiddleRunParams params = { (visRange.start + visRange.end) / 2, NULL, 0 };
    render_GmDocument(d->doc, visRange, find_MiddleRunParams_, &params);
    return params.closest;
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

static iMediaRequest *findMediaRequest_DocumentWidget_(const iDocumentWidget *d, iGmLinkId linkId) {
    iConstForEach(ObjectList, i, d->media) {
        const iMediaRequest *req = (const iMediaRequest *) i.object;
        if (req->linkId == linkId) {
            return iConstCast(iMediaRequest *, req);
        }
    }
    return NULL;
}

static iBool requestMedia_DocumentWidget_(iDocumentWidget *d, iGmLinkId linkId, iBool enableFilters) {
    if (!findMediaRequest_DocumentWidget_(d, linkId)) {
        const iString *mediaUrl = absoluteUrl_String(d->mod.url, linkUrl_GmDocument(d->doc, linkId));
        pushBack_ObjectList(d->media, iClob(new_MediaRequest(d, linkId, mediaUrl, enableFilters)));
        invalidate_DocumentWidget_(d);
        return iTrue;
    }
    return iFalse;
}

static iBool isDownloadRequest_DocumentWidget(const iDocumentWidget *d, const iMediaRequest *req) {
    return findLinkDownload_Media(constMedia_GmDocument(d->doc), req->linkId) != 0;
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
                if (setData_Media(media_GmDocument(d->doc),
                                  req->linkId,
                                  &resp->meta,
                                  &resp->body,
                                  partialData_MediaFlag | allowHide_MediaFlag)) {
                    redoLayout_GmDocument(d->doc);
                }
                updateVisible_DocumentWidget_(d);
                invalidate_DocumentWidget_(d);
                refresh_Widget(as_Widget(d));
            }
            unlockResponse_GmRequest(req->req);
        }
        /* Update the link's progress. */
        invalidateLink_DocumentWidget_(d, req->linkId);
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
                setData_Media(media_GmDocument(d->doc),
                              req->linkId,
                              meta_GmRequest(req->req),
                              body_GmRequest(req->req),
                              allowHide_MediaFlag);
                redoLayout_GmDocument(d->doc);
                updateVisible_DocumentWidget_(d);
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

static void allocVisBuffer_DocumentWidget_(const iDocumentWidget *d) {
    const iWidget *w         = constAs_Widget(d);
    const iBool    isVisible = isVisible_Widget(w);
    const iInt2    size      = bounds_Widget(w).size;
    if (isVisible) {
        alloc_VisBuf(d->visBuf, size, 1);
    }
    else {
        dealloc_VisBuf(d->visBuf);
    }
}

static iBool fetchNextUnfetchedImage_DocumentWidget_(iDocumentWidget *d) {
    iConstForEach(PtrArray, i, &d->visibleLinks) {
        const iGmRun *run = i.ptr;
        if (run->linkId && run->mediaType == none_GmRunMediaType &&
            ~run->flags & decoration_GmRunFlag) {
            const int linkFlags = linkFlags_GmDocument(d->doc, run->linkId);
            if (isMediaLink_GmDocument(d->doc, run->linkId) &&
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

static const iString *saveToDownloads_(const iString *url, const iString *mime, const iBlock *content,
                                       iBool showDialog) {
    const iString *savePath = downloadPathForUrl_App(url, mime);
    /* Write the file. */ {
        iFile *f = new_File(savePath);
        if (open_File(f, writeOnly_FileMode)) {
            write_File(f, content);
            close_File(f);
            const size_t size   = size_Block(content);
            const iBool  isMega = size >= 1000000;
#if defined (iPlatformAppleMobile)
            exportDownloadedFile_iOS(savePath);
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
            return savePath;
        }
        else {
            makeSimpleMessage_Widget(uiTextCaution_ColorEscape "${heading.save.error}",
                                     strerror(errno));
        }
        iRelease(f);
    }
    return collectNew_String();
}

static void addAllLinks_(void *context, const iGmRun *run) {
    iPtrArray *links = context;
    if (~run->flags & decoration_GmRunFlag && run->linkId) {
        pushBack_PtrArray(links, run);
    }
}

static size_t visibleLinkOrdinal_DocumentWidget_(const iDocumentWidget *d, iGmLinkId linkId) {
    size_t ord = 0;
    const iRangei visRange = visibleRange_DocumentWidget_(d);
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

static iBool updateDocumentWidthRetainingScrollPosition_DocumentWidget_(iDocumentWidget *d,
                                                                        iBool keepCenter) {
    const int newWidth = documentWidth_DocumentWidget_(d);
    if (newWidth == size_GmDocument(d->doc).x && !keepCenter /* not a font change */) {
        return iFalse;
    }
    /* Font changes (i.e., zooming) will keep the view centered, otherwise keep the top
       of the visible area fixed. */
    const iGmRun *run     = keepCenter ? middleRun_DocumentWidget_(d) : d->visibleRuns.start;
    const char *  runLoc  = (run ? run->text.start : NULL);
    int           voffset = 0;
    if (!keepCenter && run) {
        /* Keep the first visible run visible at the same position. */
        /* TODO: First *fully* visible run? */
        voffset = visibleRange_DocumentWidget_(d).start - top_Rect(run->visBounds);
    }
    setWidth_GmDocument(d->doc, newWidth);
    documentRunsInvalidated_DocumentWidget_(d);
    if (runLoc && !keepCenter) {
        run = findRunAtLoc_GmDocument(d->doc, runLoc);
        if (run) {
            scrollTo_DocumentWidget_(d,
                                     top_Rect(run->visBounds) +
                                         lineHeight_Text(paragraph_FontId) + voffset,
                                     iFalse);
        }
    }
    else if (runLoc && keepCenter) {
        run = findRunAtLoc_GmDocument(d->doc, runLoc);
        if (run) {
            scrollTo_DocumentWidget_(d, mid_Rect(run->bounds).y, iTrue);
        }
    }
    return iTrue;
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
        iGmDocument *copy = ref_Object(doc);
        iRelease(d->doc);
        d->doc = copy;
        d->scrollY = swapBuffersWith->scrollY;
        updateVisible_DocumentWidget_(d);
        iSwap(iVisBuf *,     d->visBuf,     swapBuffersWith->visBuf);
        iSwap(iVisBufMeta *, d->visBufMeta, swapBuffersWith->visBufMeta);
        iSwap(iDrawBufs *,   d->drawBufs,   swapBuffersWith->drawBufs);
        invalidate_DocumentWidget_(swapBuffersWith);
    }
}

static iWidget *swipeParent_DocumentWidget_(iDocumentWidget *d) {
    return findChild_Widget(as_Widget(d)->root->widget, "doctabs");
}

static iBool handleSwipe_DocumentWidget_(iDocumentWidget *d, const char *cmd) {
    iWidget *w = as_Widget(d);
    /* Swipe animations are rather complex and utilize both cached GmDocument content
       and temporary DocumentWidgets. Depending on the swipe direction, this DocumentWidget
       may wait until the finger is released to actually perform the navigation action. */
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
            /* The temporary "swipeIn" will display the previous page until the finger is lifted. */
            iDocumentWidget *swipeIn = findChild_Widget(swipeParent, "swipein");
            if (!swipeIn) {
                const iBool sidebarSwipe = (isPortraitPhone_App() &&
                                            d->flags & openedFromSidebar_DocumentWidgetFlag &&
                                            !isVisible_Widget(findWidget_App("sidebar")));
                swipeIn = new_DocumentWidget();
                setId_Widget(as_Widget(swipeIn), "swipein");
                setFlags_Widget(as_Widget(swipeIn),
                                disabled_WidgetFlag | refChildrenOffset_WidgetFlag |
                                fixedPosition_WidgetFlag | fixedSize_WidgetFlag, iTrue);
                swipeIn->widget.rect.pos = windowToInner_Widget(swipeParent, localToWindow_Widget(w, w->rect.pos));
                swipeIn->widget.rect.size = d->widget.rect.size;
                swipeIn->widget.offsetRef = parent_Widget(w);
                if (!sidebarSwipe) {
                    iRecentUrl *recent = new_RecentUrl();
                    preceding_History(d->mod.history, recent);
                    if (recent->cachedDoc) {
                        iChangeRef(swipeIn->doc, recent->cachedDoc);
                        updateScrollMax_DocumentWidget_(d);
                        setValue_Anim(&swipeIn->scrollY.pos, size_GmDocument(swipeIn->doc).y * recent->normScrollY, 0);
                        updateVisible_DocumentWidget_(swipeIn);
                        swipeIn->drawBufs->flags |= updateTimestampBuf_DrawBufsFlag | updateSideBuf_DrawBufsFlag;
                    }
                    delete_RecentUrl(recent);
                }
                addChildPos_Widget(swipeParent, iClob(swipeIn), front_WidgetAddPos);
            }
        }
        if (side == 2) { /* right edge */
            if (offset < -get_Window()->pixelRatio * 10) {
                int animSpan = 10;
                if (!atLatest_History(d->mod.history) &&
                    ~flags_Widget(w) & dragged_WidgetFlag) {
                    animSpan = 0;
                    postCommand_Widget(d, "navigate.forward");
                    setFlags_Widget(w, dragged_WidgetFlag, iTrue);
                    /* Set up the swipe dummy. */
                    iWidget *swipeParent = swipeParent_DocumentWidget_(d);
                    iDocumentWidget *target = new_DocumentWidget();
                    setId_Widget(as_Widget(target), "swipeout");
                    /* The target takes the old document and jumps on top. */
                    target->widget.rect.pos = windowToInner_Widget(swipeParent, localToWindow_Widget(w, w->rect.pos));
                    target->widget.rect.size = d->widget.rect.size;
                    setFlags_Widget(as_Widget(target), fixedPosition_WidgetFlag | fixedSize_WidgetFlag, iTrue);
                    swap_DocumentWidget_(target, d->doc, d);
                    addChildPos_Widget(swipeParent, iClob(target), front_WidgetAddPos);
                    setFlags_Widget(as_Widget(target), refChildrenOffset_WidgetFlag, iTrue);
                    as_Widget(target)->offsetRef = parent_Widget(w);
                    destroy_Widget(as_Widget(target)); /* will be actually deleted after animation finishes */
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
            postCommand_Widget(d, "navigate.back");
        }
        setFlags_Widget(w, dragged_WidgetFlag, iFalse);
        setVisualOffset_Widget(w, 0, 100, 0);
        return iTrue;
    }
    if (equal_Command(cmd, "edgeswipe.ended") && argLabel_Command(cmd, "side") == 1) {
        iWidget *swipeParent = swipeParent_DocumentWidget_(d);
        iWidget *swipeIn = findChild_Widget(swipeParent, "swipein");
        if (swipeIn) {
            swipeIn->offsetRef = NULL;
            destroy_Widget(swipeIn);
        }
    }
    if (equal_Command(cmd, "swipe.back")) {
        if (atOldest_History(d->mod.history)) {
            setVisualOffset_Widget(w, 0, 100, 0);
            return iTrue;
        }
        iWidget *swipeParent = swipeParent_DocumentWidget_(d);
        iDocumentWidget *target = new_DocumentWidget();
        setId_Widget(as_Widget(target), "swipeout");
        /* The target takes the old document and jumps on top. */
        target->widget.rect.pos = windowToInner_Widget(swipeParent, innerToWindow_Widget(w, zero_I2()));
        /* Note: `innerToWindow_Widget` does not apply visual offset. */
        target->widget.rect.size = w->rect.size;
        setFlags_Widget(as_Widget(target), fixedPosition_WidgetFlag | fixedSize_WidgetFlag, iTrue);
        swap_DocumentWidget_(target, d->doc, d);
        addChildPos_Widget(swipeParent, iClob(target), back_WidgetAddPos);
        setFlags_Widget(as_Widget(d), refChildrenOffset_WidgetFlag, iTrue);
        as_Widget(d)->offsetRef = swipeParent;
        setVisualOffset_Widget(as_Widget(target), value_Anim(&w->visualOffset), 0, 0);
        setVisualOffset_Widget(as_Widget(target), width_Widget(target), 150, 0);
        destroy_Widget(as_Widget(target)); /* will be actually deleted after animation finishes */
        setVisualOffset_Widget(w, 0, 0, 0);
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

static iBool handleCommand_DocumentWidget_(iDocumentWidget *d, const char *cmd) {
    iWidget *w = as_Widget(d);
    if (equal_Command(cmd, "document.openurls.changed")) {
        /* When any tab changes its document URL, update the open link indicators. */
        if (updateOpenURLs_GmDocument(d->doc)) {
            invalidate_DocumentWidget_(d);
            refresh_Widget(d);
        }
        return iFalse;
    }
    if (equal_Command(cmd, "visited.changed")) {
        updateVisitedLinks_GmDocument(d->doc);
        invalidateVisibleLinks_DocumentWidget_(d);
        return iFalse;
    }
    if (equal_Command(cmd, "document.render")) /* `Periodic` makes direct dispatch to here */ {
//        printf("%u: document.render\n", SDL_GetTicks());
        if (SDL_GetTicks() - d->drawBufs->lastRenderTime > 150) {
            remove_Periodic(periodic_App(), d);
            /* Scrolling has stopped, begin filling up the buffer. */
            if (d->visBuf->buffers[0].texture) {
                addTicker_App(prerender_DocumentWidget_, d);
            }
        }
        return iTrue;
    }
    else if (equal_Command(cmd, "window.resized") || equal_Command(cmd, "font.changed") ||
             equal_Command(cmd, "keyroot.changed")) {
        /* Alt/Option key may be involved in window size changes. */
        setLinkNumberMode_DocumentWidget_(d, iFalse);
        d->phoneToolbar = findWidget_App("toolbar");
        const iBool keepCenter = equal_Command(cmd, "font.changed");
        updateDocumentWidthRetainingScrollPosition_DocumentWidget_(d, keepCenter);
        d->drawBufs->flags |= updateSideBuf_DrawBufsFlag;
        updateVisible_DocumentWidget_(d);
        invalidate_DocumentWidget_(d);
        dealloc_VisBuf(d->visBuf);
        updateWindowTitle_DocumentWidget_(d);
        showOrHidePinningIndicator_DocumentWidget_(d);
        refresh_Widget(w);
    }
    else if (equal_Command(cmd, "window.focus.lost")) {
        if (d->flags & showLinkNumbers_DocumentWidgetFlag) {
            setLinkNumberMode_DocumentWidget_(d, iFalse);
            invalidateVisibleLinks_DocumentWidget_(d);
            refresh_Widget(w);
        }
        return iFalse;
    }
    else if (equal_Command(cmd, "window.mouse.exited")) {
        return iFalse;
    }
    else if (equal_Command(cmd, "theme.changed") && document_App() == d) {
//        invalidateTheme_History(d->mod.history); /* cached colors */
        updateTheme_DocumentWidget_(d);
        updateVisible_DocumentWidget_(d);
        updateTrust_DocumentWidget_(d, NULL);
        d->drawBufs->flags |= updateSideBuf_DrawBufsFlag;
        invalidate_DocumentWidget_(d);
        refresh_Widget(w);
    }
    else if (equal_Command(cmd, "document.layout.changed") && document_Root(get_Root()) == d) {
        if (argLabel_Command(cmd, "redo")) {
            redoLayout_GmDocument(d->doc);
        }
        updateSize_DocumentWidget(d);
    }
    else if (equal_Command(cmd, "pinsplit.set")) {
        postCommand_App("document.update.pin"); /* prefs value not set yet */
        return iFalse;
    }
    else if (equal_Command(cmd, "document.update.pin")) {
        showOrHidePinningIndicator_DocumentWidget_(d);
        return iFalse;
    }
    else if (equal_Command(cmd, "tabs.changed")) {
        setLinkNumberMode_DocumentWidget_(d, iFalse);
        if (cmp_String(id_Widget(w), suffixPtr_Command(cmd, "id")) == 0) {
            /* Set palette for our document. */
            updateTheme_DocumentWidget_(d);
            updateTrust_DocumentWidget_(d, NULL);
            updateSize_DocumentWidget(d);
            showOrHidePinningIndicator_DocumentWidget_(d);
            updateFetchProgress_DocumentWidget_(d);
        }
        init_Anim(&d->sideOpacity, 0);
        init_Anim(&d->altTextOpacity, 0);
        updateSideOpacity_DocumentWidget_(d, iFalse);
        updateWindowTitle_DocumentWidget_(d);
        allocVisBuffer_DocumentWidget_(d);
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
            d->selectMark = sourceLoc_DocumentWidget_(d, d->contextPos);
            extendRange_Rangecc(&d->selectMark, range_String(source_GmDocument(d->doc)),
                                word_RangeExtension | bothStartAndEnd_RangeExtension);
            d->initialSelectMark = d->selectMark;
        }
        return iTrue;
    }
    else if (equal_Command(cmd, "document.info") && d == document_App()) {
        const char *unchecked       = red_ColorEscape "\u2610";
        const char *checked         = green_ColorEscape "\u2611";
        const iBool haveFingerprint = (d->certFlags & haveFingerprint_GmCertFlag) != 0;
        const iBool canTrust =
            (d->certFlags == (available_GmCertFlag | haveFingerprint_GmCertFlag |
                              timeVerified_GmCertFlag | domainVerified_GmCertFlag));
        const iRecentUrl *recent = findUrl_History(d->mod.history, d->mod.url);
        const iString *meta = &d->sourceMime;
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
        /* TODO: On mobile, omit the CA status. */
        appendFormat_String(
            msg,
            "\n%s${pageinfo.cert.status}\n"
            "%s%s  %s\n"
            "%s%s  %s%s\n"
            "%s%s  %s (%04d-%02d-%02d %02d:%02d:%02d)\n"
            "%s%s  %s",
            uiHeading_ColorEscape,
            d->certFlags & authorityVerified_GmCertFlag ? checked
                                                        : uiTextAction_ColorEscape "\u2610",
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
        setFocus_Widget(NULL);
        iArray *items = new_Array(sizeof(iMenuItem));
        if (canTrust) {
            pushBack_Array(items,
                           &(iMenuItem){ uiTextCaution_ColorEscape "${dlg.cert.trust}",
                                         SDLK_u,
                                         KMOD_PRIMARY | KMOD_SHIFT,
                                         "server.trustcert" });
        }
        if (haveFingerprint) {
            pushBack_Array(items, &(iMenuItem){ "${dlg.cert.fingerprint}", 0, 0, "server.copycert" });
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
        /* Enforce a minimum size. */
        iWidget *sizer = new_Widget();
        setFixedSize_Widget(sizer, init_I2(gap_UI * 65, 1));
        addChildFlags_Widget(dlg, iClob(sizer), frameless_WidgetFlag);
        setFlags_Widget(dlg, centerHorizontal_WidgetFlag, iFalse);
        if (deviceType_App() != phone_AppDeviceType) {
            const iWidget *lockButton = findWidget_Root("navbar.lock");
            setPos_Widget(dlg, windowToLocal_Widget(dlg, bottomLeft_Rect(bounds_Widget(lockButton))));
        }
        arrange_Widget(dlg);
        addAction_Widget(dlg, SDLK_ESCAPE, 0, "message.ok");
        addAction_Widget(dlg, SDLK_SPACE, 0, "message.ok");
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
            copied = copy_String(source_GmDocument(d->doc));
        }
        SDL_SetClipboardText(cstr_String(copied));
        delete_String(copied);
        if (flags_Widget(w) & touchDrag_WidgetFlag) {
            postCommand_Widget(w, "document.select arg:0");
        }
        return iTrue;
    }
    else if (equal_Command(cmd, "document.copylink") && document_App() == d) {
        if (d->contextLink) {
            SDL_SetClipboardText(cstr_String(canonicalUrl_String(absoluteUrl_String(
                d->mod.url, linkUrl_GmDocument(d->doc, d->contextLink->linkId)))));
        }
        else {
            SDL_SetClipboardText(cstr_String(canonicalUrl_String(d->mod.url)));
        }
        return iTrue;
    }
    else if (equalWidget_Command(cmd, w, "document.downloadlink")) {
        if (d->contextLink) {
            const iGmLinkId linkId = d->contextLink->linkId;
            setDownloadUrl_Media(
                media_GmDocument(d->doc), linkId, linkUrl_GmDocument(d->doc, linkId));
            requestMedia_DocumentWidget_(d, linkId, iFalse /* no filters */);
            redoLayout_GmDocument(d->doc); /* inline downloader becomes visible */
            updateVisible_DocumentWidget_(d);
            invalidate_DocumentWidget_(d);
            refresh_Widget(w);
        }
        return iTrue;
    }
    else if (equal_Command(cmd, "document.input.submit") && document_Command(cmd) == d) {
        postCommandf_Root(w->root,
                          "open url:%s",
                          cstrCollect_String(makeQueryUrl_DocumentWidget_
                                             (d, collect_String(suffix_Command(cmd, "value")))));
        return iTrue;
    }
    else if (equal_Command(cmd, "valueinput.cancelled") &&
             equal_Rangecc(range_Command(cmd, "id"), "document.input.submit") && document_App() == d) {
        postCommand_Root(get_Root(), "navigate.back");
        return iTrue;
    }
    else if (equalWidget_Command(cmd, w, "document.request.updated") &&
             id_GmRequest(d->request) == argU32Label_Command(cmd, "reqid")) {
        set_Block(&d->sourceContent, &lockResponse_GmRequest(d->request)->body);
        unlockResponse_GmRequest(d->request);
        if (document_App() == d) {
            updateFetchProgress_DocumentWidget_(d);
        }
        checkResponse_DocumentWidget_(d);
        set_Atomic(&d->isRequestUpdated, iFalse); /* ready to be notified again */
        return iFalse;
    }
    else if (equalWidget_Command(cmd, w, "document.request.finished") &&
             id_GmRequest(d->request) == argU32Label_Command(cmd, "reqid")) {
        set_Block(&d->sourceContent, body_GmRequest(d->request));
        if (!isSuccess_GmStatusCode(status_GmRequest(d->request))) {
            format_String(&d->sourceHeader,
                          "%d %s",
                          status_GmRequest(d->request),
                          cstr_String(meta_GmRequest(d->request)));
        }
        else {
            clear_String(&d->sourceHeader);
        }
        updateFetchProgress_DocumentWidget_(d);
        checkResponse_DocumentWidget_(d);
        if (category_GmStatusCode(status_GmRequest(d->request)) == categorySuccess_GmStatusCode) {
            init_Anim(&d->scrollY.pos, d->initNormScrollY * size_GmDocument(d->doc).y); /* TODO: unless user already scrolled! */
        }
        d->flags &= ~urlChanged_DocumentWidgetFlag;
        d->state = ready_RequestState;
        postProcessRequestContent_DocumentWidget_(d, iFalse);
        /* The response may be cached. */
        if (d->request) {
            if (!equal_Rangecc(urlScheme_String(d->mod.url), "about") &&
                (startsWithCase_String(meta_GmRequest(d->request), "text/") ||
                 !cmp_String(&d->sourceMime, mimeType_Gempub))) {
                setCachedResponse_History(d->mod.history, lockResponse_GmRequest(d->request));
                unlockResponse_GmRequest(d->request);
            }
        }
        iReleasePtr(&d->request);
        updateVisible_DocumentWidget_(d);
        d->drawBufs->flags |= updateSideBuf_DrawBufsFlag;
        postCommandf_Root(w->root, "document.changed doc:%p url:%s", d, cstr_String(d->mod.url));
        /* Check for a pending goto. */
        if (!isEmpty_String(&d->pendingGotoHeading)) {
            scrollToHeading_DocumentWidget_(d, cstr_String(&d->pendingGotoHeading));
            clear_String(&d->pendingGotoHeading);
        }
        cacheDocumentGlyphs_DocumentWidget_(d);
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
        if (equalCase_Rangecc(urlScheme_String(d->mod.url), "gemini") ||
            equalCase_Rangecc(urlScheme_String(d->mod.url), "titan")) {
            iUploadWidget *upload = new_UploadWidget();
            setUrl_UploadWidget(upload, d->mod.url);
            setResponseViewer_UploadWidget(upload, d);
            addChild_Widget(get_Root()->widget, iClob(upload));
            finalizeSheet_Mobile(as_Widget(upload));
            postRefresh_App();
        }
        return iTrue;
    }
    else if (equal_Command(cmd, "media.updated") || equal_Command(cmd, "media.finished")) {
        return handleMediaCommand_DocumentWidget_(d, cmd);
    }
    else if (equal_Command(cmd, "media.player.started")) {
        /* When one media player starts, pause the others that may be playing. */
        const iPlayer *startedPlr = pointerLabel_Command(cmd, "player");
        const iMedia * media  = media_GmDocument(d->doc);
        const size_t   num    = numAudio_Media(media);
        for (size_t id = 1; id <= num; id++) {
            iPlayer *plr = audioPlayer_Media(media, id);
            if (plr != startedPlr) {
                setPaused_Player(plr, iTrue);
            }
        }
    }
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
            const iBool    doOpen   = argLabel_Command(cmd, "open");
            const iString *savePath = saveToDownloads_(d->mod.url, &d->sourceMime,
                                                       &d->sourceContent, !doOpen);
            if (!isEmpty_String(savePath) && doOpen) {
                postCommandf_Root(
                    w->root, "!open url:%s", cstrCollect_String(makeFileUrl_String(savePath)));
            }
        }
        return iTrue;
    }
    else if (equal_Command(cmd, "document.reload") && document_Command(cmd) == d) {
        d->initNormScrollY = normScrollPos_DocumentWidget_(d);
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
                const iGmRun *last = lastVisibleLink_DocumentWidget_(d);
                if (!last) {
                    d->ordinalBase = 0;
                }
                else {
                    d->ordinalBase += numKeys;
                    if (visibleLinkOrdinal_DocumentWidget_(d, last->linkId) < d->ordinalBase) {
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
        invalidateVisibleLinks_DocumentWidget_(d);
        refresh_Widget(d);
        return iTrue;
    }
    else if (equal_Command(cmd, "navigate.back") && document_App() == d) {
        if (isPortraitPhone_App()) {
            if (d->flags & openedFromSidebar_DocumentWidgetFlag &&
                !isVisible_Widget(findWidget_App("sidebar"))) {
                postCommand_App("sidebar.toggle");
                showToolbar_Root(get_Root(), iTrue);
#if defined (iPlatformAppleMobile)
                playHapticEffect_iOS(gentleTap_HapticEffect);
#endif
                return iTrue;
            }
            d->flags &= ~openedFromSidebar_DocumentWidgetFlag;
        }
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
        /* Remove the last path segment. */
        if (size_Range(&parts.path) > 1) {
            if (parts.path.end[-1] == '/') {
                parts.path.end--;
            }
            while (parts.path.end > parts.path.start) {
                if (parts.path.end[-1] == '/') break;
                parts.path.end--;
            }
            postCommandf_Root(w->root,
                "open url:%s",
                cstr_Rangecc((iRangecc){ constBegin_String(d->mod.url), parts.path.end }));
        }
        return iTrue;
    }
    else if (equal_Command(cmd, "navigate.root") && document_App() == d) {
        postCommandf_Root(w->root, "open url:%s/", cstr_Rangecc(urlRoot_String(d->mod.url)));
        return iTrue;
    }
    else if (equalWidget_Command(cmd, w, "scroll.moved")) {
        init_Anim(&d->scrollY.pos, arg_Command(cmd));
        updateVisible_DocumentWidget_(d);
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
        smoothScroll_DocumentWidget_(d,
                                     dir * amount * height_Rect(documentBounds_DocumentWidget_(d)),
                                     smoothDuration_DocumentWidget_(keyboard_ScrollType));
        return iTrue;
    }
    else if (equal_Command(cmd, "scroll.top") && document_App() == d) {
        init_Anim(&d->scrollY.pos, 0);
        invalidate_VisBuf(d->visBuf);
        clampScroll_DocumentWidget_(d);
        updateVisible_DocumentWidget_(d);
        refresh_Widget(w);
        return iTrue;
    }
    else if (equal_Command(cmd, "scroll.bottom") && document_App() == d) {
        updateScrollMax_DocumentWidget_(d); /* scrollY.max might not be fully updated */
        init_Anim(&d->scrollY.pos, d->scrollY.max);
        invalidate_VisBuf(d->visBuf);
        clampScroll_DocumentWidget_(d);
        updateVisible_DocumentWidget_(d);
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
        smoothScroll_DocumentWidget_(d,
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
            scrollToHeading_DocumentWidget_(d, heading);
            return iTrue;
        }
        const char *loc = pointerLabel_Command(cmd, "loc");
        const iGmRun *run = findRunAtLoc_GmDocument(d->doc, loc);
        if (run) {
            scrollTo_DocumentWidget_(d, run->visBounds.pos.y, iFalse);
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
            d->foundMark     = finder(d->doc, text_InputWidget(find), dir > 0 ? d->foundMark.end
                                                                          : d->foundMark.start);
            if (!d->foundMark.start && wrap) {
                /* Wrap around. */
                d->foundMark = finder(d->doc, text_InputWidget(find), NULL);
            }
            if (d->foundMark.start) {
                const iGmRun *found;
                if ((found = findRunAtLoc_GmDocument(d->doc, d->foundMark.start)) != NULL) {
                    scrollTo_DocumentWidget_(d, mid_Rect(found->bounds).y, iTrue);
                }
            }
        }
        if (flags_Widget(w) & touchDrag_WidgetFlag) {
            postCommand_Root(w->root, "document.select arg:0"); /* we can't handle both at the same time */
        }
        invalidateWideRunsWithNonzeroOffset_DocumentWidget_(d); /* markers don't support offsets */
        resetWideRuns_DocumentWidget_(d);
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
        render_GmDocument(d->doc, (iRangei){ 0, size_GmDocument(d->doc).y }, addAllLinks_, links);
        /* Find links that aren't already bookmarked. */
        iForEach(PtrArray, i, links) {
            const iGmRun *run = i.ptr;
            uint32_t      bmid;
            if ((bmid = findUrl_Bookmarks(bookmarks_App(),
                                          linkUrl_GmDocument(d->doc, run->linkId))) != 0) {
                const iBookmark *bm = get_Bookmarks(bookmarks_App(), bmid);
                /* We can import local copies of remote bookmarks. */
                if (!hasTag_Bookmark(bm, remote_BookmarkTag)) {
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
                    (iMenuItem[]){ { "${cancel}", 0, 0, NULL },
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
                                  linkUrl_GmDocument(d->doc, run->linkId),
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
        updateHover_DocumentWidget_(d, mouseCoord_Window(get_Window(), 0));
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
    else if (startsWith_CStr(cmd, "pinch.") && document_Command(cmd) == d) {
        return handlePinch_DocumentWidget_(d, cmd);
    }
    else if ((startsWith_CStr(cmd, "edgeswipe.") || startsWith_CStr(cmd, "swipe.")) &&
             document_App() == d) {
        return handleSwipe_DocumentWidget_(d, cmd);
    }
    return iFalse;
}

static iRect runRect_DocumentWidget_(const iDocumentWidget *d, const iGmRun *run) {
    const iRect docBounds = documentBounds_DocumentWidget_(d);
    return moved_Rect(run->bounds, addY_I2(topLeft_Rect(docBounds), -pos_SmoothScroll(&d->scrollY)));
}

static void setGrabbedPlayer_DocumentWidget_(iDocumentWidget *d, const iGmRun *run) {
    if (run && run->mediaType == audio_GmRunMediaType) {
        iPlayer *plr = audioPlayer_Media(media_GmDocument(d->doc), run->mediaId);
        setFlags_Player(plr, volumeGrabbed_PlayerFlag, iTrue);
        d->grabbedStartVolume = volume_Player(plr);
        d->grabbedPlayer      = run;
        refresh_Widget(d);
    }
    else if (d->grabbedPlayer) {
        setFlags_Player(
            audioPlayer_Media(media_GmDocument(d->doc), d->grabbedPlayer->mediaId),
            volumeGrabbed_PlayerFlag,
            iFalse);
        d->grabbedPlayer = NULL;
        refresh_Widget(d);
    }
    else {
        iAssert(iFalse);
    }
}

static iBool processMediaEvents_DocumentWidget_(iDocumentWidget *d, const SDL_Event *ev) {
    if (ev->type != SDL_MOUSEBUTTONDOWN && ev->type != SDL_MOUSEBUTTONUP &&
        ev->type != SDL_MOUSEMOTION) {
        return iFalse;
    }
    if (ev->type == SDL_MOUSEBUTTONDOWN || ev->type == SDL_MOUSEBUTTONUP) {
        if (ev->button.button != SDL_BUTTON_LEFT) {
            return iFalse;
        }
    }
    if (d->grabbedPlayer) {
        /* Updated in the drag. */
        return iFalse;
    }
    const iInt2 mouse = init_I2(ev->button.x, ev->button.y);
    iConstForEach(PtrArray, i, &d->visibleMedia) {
        const iGmRun *run  = i.ptr;
        if (run->mediaType != audio_GmRunMediaType) {
            continue;
        }
        const iRect rect = runRect_DocumentWidget_(d, run);
        iPlayer *   plr  = audioPlayer_Media(media_GmDocument(d->doc), run->mediaId);
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
                        { cstrCollect_String(metadataLabel_Player(plr)), 0, 0, NULL },
                    },
                    1);
                openMenu_Widget(d->playerMenu, bottomLeft_Rect(ui.menuRect));
                return iTrue;
            }
        }
    }
    return iFalse;
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

static void beginMarkingSelection_DocumentWidget_(iDocumentWidget *d, iInt2 pos) {
    setFocus_Widget(NULL); /* TODO: Focus this document? */
    invalidateWideRunsWithNonzeroOffset_DocumentWidget_(d);
    resetWideRuns_DocumentWidget_(d); /* Selections don't support horizontal scrolling. */
    iChangeFlags(d->flags, selecting_DocumentWidgetFlag, iTrue);
    d->selectMark = sourceLoc_DocumentWidget_(d, pos);
    refresh_Widget(as_Widget(d));
}

static iBool processEvent_DocumentWidget_(iDocumentWidget *d, const SDL_Event *ev) {
    iWidget *w = as_Widget(d);
    if (isMetricsChange_UserEvent(ev)) {
        updateSize_DocumentWidget(d);
    }
    else if (processEvent_SmoothScroll(&d->scrollY, ev)) {
        return iTrue;
    }
    else if (ev->type == SDL_USEREVENT && ev->user.code == command_UserEventCode) {
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
            iConstForEach(PtrArray, i, &d->visibleLinks) {
                if (ord == iInvalidPos) break;
                const iGmRun *run = i.ptr;
                if (run->flags & decoration_GmRunFlag &&
                    visibleLinkOrdinal_DocumentWidget_(d, run->linkId) == ord) {
                    if (d->flags & setHoverViaKeys_DocumentWidgetFlag) {
                        d->hoverLink = run;
                    }
                    else {
                        postCommandf_Root(w->root,
                                          "open newtab:%d url:%s",
                                          (isPinned_DocumentWidget_(d) ? otherRoot_OpenTabFlag : 0) ^
                                          (d->ordinalMode ==
                                                 numbersAndAlphabet_DocumentLinkOrdinalMode
                                             ? openTabMode_Sym(modState_Keys())
                                             : (d->flags & newTabViaHomeKeys_DocumentWidgetFlag ? 1 : 0)),
                                          cstr_String(absoluteUrl_String(
                                             d->mod.url, linkUrl_GmDocument(d->doc, run->linkId))));
                    }
                    setLinkNumberMode_DocumentWidget_(d, iFalse);
                    invalidateVisibleLinks_DocumentWidget_(d);
                    refresh_Widget(d);
                    return iTrue;
                }
            }
        }
        switch (key) {
            case SDLK_ESCAPE:
                if (d->flags & showLinkNumbers_DocumentWidgetFlag && document_App() == d) {
                    setLinkNumberMode_DocumentWidget_(d, iFalse);
                    invalidateVisibleLinks_DocumentWidget_(d);
                    refresh_Widget(d);
                    return iTrue;
                }
                break;
#if 1
            case SDLK_KP_1:
            case '`': {
                iBlock *seed = new_Block(64);
                for (size_t i = 0; i < 64; ++i) {
                    setByte_Block(seed, i, iRandom(0, 256));
                }
                setThemeSeed_GmDocument(d->doc, seed);
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
    else if (ev->type == SDL_MOUSEWHEEL && isHover_Widget(w)) {
        const iInt2 mouseCoord = coord_MouseWheelEvent(&ev->wheel);
        if (isPerPixel_MouseWheelEvent(&ev->wheel)) {
            const iInt2 wheel = init_I2(ev->wheel.x, ev->wheel.y);
            stop_Anim(&d->scrollY.pos);
            immediateScroll_DocumentWidget_(d, -wheel.y);
            scrollWideBlock_DocumentWidget_(d, mouseCoord, -wheel.x, 0);
        }
        else {
            /* Traditional mouse wheel. */
            const int amount = ev->wheel.y;
            if (keyMods_Sym(modState_Keys()) == KMOD_PRIMARY) {
                postCommandf_App("zoom.delta arg:%d", amount > 0 ? 10 : -10);
                return iTrue;
            }
            smoothScroll_DocumentWidget_(
                d,
                -3 * amount * lineHeight_Text(paragraph_FontId),
                smoothDuration_DocumentWidget_(mouse_ScrollType) *
                    /* accelerated speed for repeated wheelings */
                    (!isFinished_SmoothScroll(&d->scrollY) && pos_Anim(&d->scrollY.pos) < 0.25f
                         ? 0.5f
                         : 1.0f));
            scrollWideBlock_DocumentWidget_(
                d, mouseCoord, -3 * ev->wheel.x * lineHeight_Text(paragraph_FontId), 167);
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
        else if (contains_Rect(siteBannerRect_DocumentWidget_(d), mpos)) {
            setCursor_Window(get_Window(), SDL_SYSTEM_CURSOR_HAND);
        }
        else {
            if (value_Anim(&d->altTextOpacity) < 0.833f) {
                setValue_Anim(&d->altTextOpacity, 0, 0); /* keep it hidden while moving */
            }
            updateHover_DocumentWidget_(d, mpos);
        }
    }
    if (ev->type == SDL_USEREVENT && ev->user.code == widgetTapBegins_UserEventCode) {
        iChangeFlags(d->flags, noHoverWhileScrolling_DocumentWidgetFlag, iFalse);
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
        if (ev->button.button == SDL_BUTTON_MIDDLE && d->hoverLink) {
            postCommandf_Root(w->root, "open newtab:%d url:%s",
                              (isPinned_DocumentWidget_(d) ? otherRoot_OpenTabFlag : 0) |
                              (modState_Keys() & KMOD_SHIFT ? new_OpenTabFlag : newBackground_OpenTabFlag),
                              cstr_String(linkUrl_GmDocument(d->doc, d->hoverLink->linkId)));
            return iTrue;
        }
        if (ev->button.button == SDL_BUTTON_RIGHT &&
            contains_Widget(w, init_I2(ev->button.x, ev->button.y))) {
            if (!isVisible_Widget(d->menu)) {
                d->contextLink = d->hoverLink;
                d->contextPos = init_I2(ev->button.x, ev->button.y);
                if (d->menu) {
                    destroy_Widget(d->menu);
                    d->menu = NULL;
                }
                setFocus_Widget(NULL);
                iArray items;
                init_Array(&items, sizeof(iMenuItem));
                if (d->contextLink) {
                    /* Context menu for a link. */
                    const iString *linkUrl  = linkUrl_GmDocument(d->doc, d->contextLink->linkId);
//                    const int      linkFlags = linkFlags_GmDocument(d->doc, d->contextLink->linkId);
                    const iRangecc scheme   = urlScheme_String(linkUrl);
                    const iBool    isGemini = equalCase_Rangecc(scheme, "gemini");
                    iBool          isNative = iFalse;
                    if (deviceType_App() != desktop_AppDeviceType) {
                        /* Show the link as the first, non-interactive item. */
                        pushBack_Array(&items, &(iMenuItem){
                            format_CStr("```%s", cstr_String(linkUrl)),
                            0, 0, NULL });
                    }
                    if (willUseProxy_App(scheme) || isGemini ||
                        equalCase_Rangecc(scheme, "file") ||
                        equalCase_Rangecc(scheme, "finger") ||
                        equalCase_Rangecc(scheme, "gopher")) {
                        isNative = iTrue;
                        /* Regular links that we can open. */
                        pushBackN_Array(
                            &items,
                            (iMenuItem[]){
                                { openTab_Icon " ${link.newtab}",
                                  0,
                                  0,
                                  format_CStr("!open newtab:1 url:%s", cstr_String(linkUrl)) },
                                { openTabBg_Icon " ${link.newtab.background}",
                                  0,
                                  0,
                                  format_CStr("!open newtab:2 url:%s", cstr_String(linkUrl)) },
                                { "${link.side}",
                                  0,
                                  0,
                                  format_CStr("!open newtab:4 url:%s", cstr_String(linkUrl)) },
                                { "${link.side.newtab}",
                                  0,
                                  0,
                                  format_CStr("!open newtab:5 url:%s", cstr_String(linkUrl)) } },
                            4);
                        if (deviceType_App() == phone_AppDeviceType) {
                            removeN_Array(&items, size_Array(&items) - 2, iInvalidSize);
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
                                { "---", 0, 0, NULL },
                                { isGemini ? "${link.noproxy}" : openExt_Icon " ${link.browser}",
                                  0,
                                  0,
                                  format_CStr("!open noproxy:1 url:%s", cstr_String(linkUrl)) } },
                            2);
                    }
                    iString *linkLabel = collectNewRange_String(
                        linkLabel_GmDocument(d->doc, d->contextLink->linkId));
                    urlEncodeSpaces_String(linkLabel);
                    pushBackN_Array(&items,
                                    (iMenuItem[]){ { "---", 0, 0, NULL },
                                                   { "${link.copy}", 0, 0, "document.copylink" },
                                                   { bookmark_Icon " ${link.bookmark}",
                                                     0,
                                                     0,
                                                     format_CStr("!bookmark.add title:%s url:%s",
                                                                 cstr_String(linkLabel),
                                                                 cstr_String(linkUrl)) },
                                                   },
                                    3);
                    if (isNative && d->contextLink->mediaType != download_GmRunMediaType) {
                        pushBackN_Array(&items, (iMenuItem[]){
                            { "---", 0, 0, NULL },
                            { download_Icon " ${link.download}", 0, 0, "document.downloadlink" },
                        }, 2);
                    }
                    iMediaRequest *mediaReq;
                    if ((mediaReq = findMediaRequest_DocumentWidget_(d, d->contextLink->linkId)) != NULL &&
                        d->contextLink->mediaType != download_GmRunMediaType) {
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
                            { "${menu.back}", navigateBack_KeyShortcut, "navigate.back" },
                            { "${menu.forward}", navigateForward_KeyShortcut, "navigate.forward" },
                            { upArrow_Icon " ${menu.parent}", navigateParent_KeyShortcut, "navigate.parent" },
                            { upArrowBar_Icon " ${menu.root}", navigateRoot_KeyShortcut, "navigate.root" },
                            { "---", 0, 0, NULL },
                            { reload_Icon " ${menu.reload}", reload_KeyShortcut, "navigate.reload" },
                            { timer_Icon " ${menu.autoreload}", 0, 0, "document.autoreload.menu" },
                            { "---", 0, 0, NULL },
                            { bookmark_Icon " ${menu.page.bookmark}", SDLK_d, KMOD_PRIMARY, "bookmark.add" },
                            { star_Icon " ${menu.page.subscribe}", subscribeToPage_KeyModifier, "feeds.subscribe" },
                            { "---", 0, 0, NULL },
                            { book_Icon " ${menu.page.import}", 0, 0, "bookmark.links confirm:1" },
                            { globe_Icon " ${menu.page.translate}", 0, 0, "document.translate" },
                            { upload_Icon " ${menu.page.upload}", 0, 0, "document.upload" },
                            { "---", 0, 0, NULL },
                            { "${menu.page.copyurl}", 0, 0, "document.copylink" } },
                        15);
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
            }
            processContextMenuEvent_Widget(d->menu, ev, {});
        }
    }
    if (processMediaEvents_DocumentWidget_(d, ev)) {
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
                updateHover_DocumentWidget_(d, mouseCoord_Window(get_Window(), ev->button.which));
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
                        range_String(source_GmDocument(d->doc)),
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
            if (d->grabbedPlayer) {
                iPlayer *plr =
                    audioPlayer_Media(media_GmDocument(d->doc), d->grabbedPlayer->mediaId);
                iPlayerUI ui;
                init_PlayerUI(&ui, plr, runRect_DocumentWidget_(d, d->grabbedPlayer));
                float off = (float) delta_Click(&d->click).x / (float) width_Rect(ui.volumeSlider);
                setVolume_Player(plr, d->grabbedStartVolume + off);
                refresh_Widget(w);
                return iTrue;
            }
            /* Fold/unfold a preformatted block. */
            if (~d->flags & selecting_DocumentWidgetFlag && d->hoverPre &&
                preIsFolded_GmDocument(d->doc, d->hoverPre->preId)) {
                return iTrue;
            }
            /* Begin selecting a range of text. */
            if (~d->flags & selecting_DocumentWidgetFlag) {
                beginMarkingSelection_DocumentWidget_(d, d->click.startPos);
            }
            iRangecc loc = sourceLoc_DocumentWidget_(d, pos_Click(&d->click));
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
                        const iRangecc loc     = sourceLoc_DocumentWidget_(d, pos_Click(&d->click));
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
                    d->selectMark.end = (d->selectMark.end > d->selectMark.start ? loc.end : loc.start);
                }
            }
            iAssert((!d->selectMark.start && !d->selectMark.end) ||
                    ( d->selectMark.start &&  d->selectMark.end));
            /* Extend to full words/paragraphs. */
            if (d->flags & (selectWords_DocumentWidgetFlag | selectLines_DocumentWidgetFlag)) {
                extendRange_Rangecc(
                    &d->selectMark,
                    range_String(source_GmDocument(d->doc)),
                    (d->flags & movingSelectMarkStart_DocumentWidgetFlag ? moveStart_RangeExtension
                                                                         : moveEnd_RangeExtension) |
                        (d->flags & selectWords_DocumentWidgetFlag ? word_RangeExtension
                                                                   : line_RangeExtension));
                if (d->flags & movingSelectMarkStart_DocumentWidgetFlag) {
                    d->initialSelectMark.start =
                        d->initialSelectMark.end = d->selectMark.start;
                }
                if (!isEmpty_Range(&d->initialSelectMark)) {
                    if (d->selectMark.end > d->selectMark.start) {
                        d->selectMark.start = d->initialSelectMark.start;
                    }
                    else if (d->selectMark.end < d->selectMark.start) {
                        d->selectMark.start = d->initialSelectMark.end;
                    }
                }
            }
//            printf("mark %zu ... %zu\n", d->selectMark.start - cstr_String(source_GmDocument(d->doc)),
//                   d->selectMark.end - cstr_String(source_GmDocument(d->doc)));
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
                    const iRangecc tapLoc = sourceLoc_DocumentWidget_(d, pos_Click(&d->click));
                    /* Tapping on the selection will show a menu. */
                    const iRangecc mark = selectMark_DocumentWidget_(d);
                    if (tapLoc.start >= mark.start && tapLoc.end <= mark.end) {
                        if (d->copyMenu) {
                            closeMenu_Widget(d->copyMenu);
                            destroy_Widget(d->copyMenu);
                            d->copyMenu = NULL;
                        }
                        d->copyMenu = makeMenu_Widget(w, (iMenuItem[]){
                            { clipCopy_Icon " ${menu.copy}", 0, 0, "copy" },
                            { "---", 0, 0, NULL },
                            { close_Icon " ${menu.select.clear}", 0, 0, "document.select arg:0" },
                        }, 3);
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
                if (d->hoverPre) {
                    togglePreFold_DocumentWidget_(d, d->hoverPre->preId);
                    return iTrue;
                }
                if (d->hoverLink) {
                    /* TODO: Move this to a method. */
                    const iGmLinkId linkId = d->hoverLink->linkId;
                    const int linkFlags = linkFlags_GmDocument(d->doc, linkId);
                    iAssert(linkId);
                    /* Media links are opened inline by default. */
                    if (isMediaLink_GmDocument(d->doc, linkId)) {
                        if (linkFlags & content_GmLinkFlag && linkFlags & permanent_GmLinkFlag) {
                            /* We have the content and it cannot be dismissed, so nothing
                               further to do. */
                            return iTrue;
                        }
                        if (!requestMedia_DocumentWidget_(d, linkId, iTrue)) {
                            if (linkFlags & content_GmLinkFlag) {
                                /* Dismiss shown content on click. */
                                setData_Media(media_GmDocument(d->doc),
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
                                redoLayout_GmDocument(d->doc);
                                d->hoverLink = NULL;
                                clampScroll_DocumentWidget_(d);
                                updateVisible_DocumentWidget_(d);
                                invalidate_DocumentWidget_(d);
                                refresh_Widget(w);
                                return iTrue;
                            }
                            else {
                                /* Show the existing content again if we have it. */
                                iMediaRequest *req = findMediaRequest_DocumentWidget_(d, linkId);
                                if (req) {
                                    setData_Media(media_GmDocument(d->doc),
                                                  linkId,
                                                  meta_GmRequest(req->req),
                                                  body_GmRequest(req->req),
                                                  allowHide_MediaFlag);
                                    redoLayout_GmDocument(d->doc);
                                    updateVisible_DocumentWidget_(d);
                                    invalidate_DocumentWidget_(d);
                                    refresh_Widget(w);
                                    return iTrue;
                                }
                            }
                        }
                        refresh_Widget(w);
                    }
                    else if (linkFlags & supportedScheme_GmLinkFlag) {
                        int tabMode = openTabMode_Sym(modState_Keys());
                        if (isPinned_DocumentWidget_(d)) {
                            tabMode ^= otherRoot_OpenTabFlag;
                        }
                        postCommandf_Root(w->root, "open newtab:%d url:%s",
                                         tabMode,
                                         cstr_String(absoluteUrl_String(
                                             d->mod.url, linkUrl_GmDocument(d->doc, linkId))));
                    }
                    else {
                        const iString *url = absoluteUrl_String(
                            d->mod.url, linkUrl_GmDocument(d->doc, linkId));
                        makeQuestion_Widget(
                            uiTextCaution_ColorEscape "${heading.openlink}",
                            format_CStr(
                                cstr_Lang("dlg.openlink.confirm"),
                                uiTextAction_ColorEscape,
                                cstr_String(url)),
                            (iMenuItem[]){
                                { "${cancel}", 0, 0, NULL },
                                { uiTextCaution_ColorEscape "${dlg.openlink}",
                                  0, 0, format_CStr("!open default:1 url:%s", cstr_String(url)) } },
                            2);
                    }
                }
                if (d->selectMark.start && !(d->flags & (selectLines_DocumentWidgetFlag |
                                                         selectWords_DocumentWidgetFlag))) {
                    d->selectMark = iNullRange;
                    refresh_Widget(w);
                }
                /* Clicking on the top/side banner navigates to site root. */
                const iRect banRect = siteBannerRect_DocumentWidget_(d);
                if (contains_Rect(banRect, pos_Click(&d->click))) {
                    /* Clicking on a warning? */
                    if (bannerType_DocumentWidget_(d) == certificateWarning_GmDocumentBanner &&
                        pos_Click(&d->click).y - top_Rect(banRect) >
                            lineHeight_Text(banner_FontId) * 2) {
                        postCommand_Widget(d, "document.info");
                    }
                    else {
                        postCommand_Widget(d, "navigate.root");
                    }
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

iDeclareType(DrawContext)

struct Impl_DrawContext {
    const iDocumentWidget *widget;
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
            x = measureRange_Text(run->textParams.font,
                                  (iRangecc){ run->text.start, iMax(run->text.start, mark.start) })
                    .advance.x;
        }
        int w = width_Rect(run->visBounds) - x;
        if (contains_Range(&run->text, mark.end) || mark.end < run->text.start) {
            w = measureRange_Text(
                    run->textParams.font,
                    !*isInside ? mark
                               : (iRangecc){ run->text.start, iMax(run->text.start, mark.end) })
                    .advance.x;
            *isInside = iFalse;
        }
        else {
            *isInside = iTrue; /* at least until the next run */
        }
        if (w > width_Rect(run->visBounds) - x) {
            w = width_Rect(run->visBounds) - x;
        }
        if (~run->flags & decoration_GmRunFlag) {
            const iInt2 visPos =
                add_I2(run->bounds.pos, addY_I2(d->viewPos, -pos_SmoothScroll(&d->widget->scrollY)));
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
        const iRangecc url = linkUrlRange_GmDocument(d->widget->doc, run->linkId);
        if (contains_Range(&url, mark.start) &&
            (contains_Range(&url, mark.end) || url.end == mark.end)) {
            fillRect_Paint(
                &d->paint,
                moved_Rect(run->visBounds, addY_I2(d->viewPos, -pos_SmoothScroll(&d->widget->scrollY))),
                color);
        }
    }
}

static void drawMark_DrawContext_(void *context, const iGmRun *run) {
    iDrawContext *d = context;
    if (run->mediaType == none_GmRunMediaType) {
        fillRange_DrawContext_(d, run, uiMatching_ColorId, d->widget->foundMark, &d->inFoundMark);
        fillRange_DrawContext_(d, run, uiMarked_ColorId, d->widget->selectMark, &d->inSelectMark);
    }
}

static void drawBannerRun_DrawContext_(iDrawContext *d, const iGmRun *run, iInt2 visPos) {
    const iGmDocument *doc  = d->widget->doc;
    const iChar        icon = siteIcon_GmDocument(doc);
    iString            str;
    init_String(&str);
    iInt2 bpos = add_I2(visPos, init_I2(0, lineHeight_Text(banner_FontId) / 2));
    if (icon) {
        appendChar_String(&str, icon);
        const iRect iconRect = visualBounds_Text(run->textParams.font, range_String(&str));
        drawRange_Text(
            run->textParams.font,
            addY_I2(bpos, -mid_Rect(iconRect).y + lineHeight_Text(run->textParams.font) / 2),
            tmBannerIcon_ColorId,
            range_String(&str));
        bpos.x += right_Rect(iconRect) + 3 * gap_Text;
    }
    drawRange_Text(run->textParams.font,
                   bpos,
                   tmBannerTitle_ColorId,
                   bannerText_DocumentWidget_(d->widget));
    if (bannerType_GmDocument(doc) == certificateWarning_GmDocumentBanner) {
        const int domainHeight = lineHeight_Text(banner_FontId) * 2;
        iRect rect = { add_I2(visPos, init_I2(0, domainHeight)),
                       addY_I2(run->visBounds.size, -domainHeight - lineHeight_Text(uiContent_FontId)) };
        format_String(&str, "${heading.certwarn}");
        const int certFlags = d->widget->certFlags;
        if (certFlags & timeVerified_GmCertFlag && certFlags & domainVerified_GmCertFlag) {
            iUrl parts;
            init_Url(&parts, d->widget->mod.url);
            const iTime oldUntil =
                domainValidUntil_GmCerts(certs_App(), parts.host, port_Url(&parts));
            iDate exp;
            init_Date(&exp, &oldUntil);
            iTime now;
            initCurrent_Time(&now);
            const int days = secondsSince_Time(&oldUntil, &now) / 3600 / 24;
            appendCStr_String(&str, "\n");
            if (days <= 30) {
                appendCStr_String(&str,
                                  format_CStr(cstrCount_Lang("dlg.certwarn.mayberenewed.n", days),
                                              cstrCollect_String(format_Date(&exp, "%Y-%m-%d")),
                                              days));
            }
            else {
                appendCStr_String(&str, cstr_Lang("dlg.certwarn.different"));
            }
        }
        else if (certFlags & domainVerified_GmCertFlag) {
            appendCStr_String(&str, "\n");
            appendFormat_String(&str, cstr_Lang("dlg.certwarn.expired"),
                                cstrCollect_String(format_Date(&d->widget->certExpiry, "%Y-%m-%d")));
        }
        else if (certFlags & timeVerified_GmCertFlag) {
            appendCStr_String(&str, "\n");
            appendFormat_String(&str, cstr_Lang("dlg.certwarn.domain"),
                                cstr_String(d->widget->certSubject));
        }
        else {
            appendCStr_String(&str, "\n");
            appendCStr_String(&str, cstr_Lang("dlg.certwarn.domain.expired"));
        }
        const iInt2 dims = measureWrapRange_Text(
            uiContent_FontId, width_Rect(rect) - 16 * gap_UI, range_String(&str)).bounds.size;
        const int warnHeight = run->visBounds.size.y - domainHeight;
        const int yOff = (lineHeight_Text(uiLabelLarge_FontId) -
                          lineHeight_Text(uiContent_FontId)) / 2;
        const iRect bgRect =
            init_Rect(0, visPos.y + domainHeight, d->widgetBounds.size.x, warnHeight);
        fillRect_Paint(&d->paint, bgRect, orange_ColorId);
        if (!isDark_ColorTheme(colorTheme_App())) {
            drawHLine_Paint(&d->paint,
                            topLeft_Rect(bgRect), width_Rect(bgRect), tmBannerTitle_ColorId);
            drawHLine_Paint(&d->paint,
                            bottomLeft_Rect(bgRect), width_Rect(bgRect), tmBannerTitle_ColorId);
        }
        const int fg = black_ColorId;
        adjustEdges_Rect(&rect, warnHeight / 2 - dims.y / 2 - yOff, 0, 0, 0);
        bpos = topLeft_Rect(rect);
        draw_Text(uiLabelLarge_FontId, bpos, fg, "\u26a0");
        adjustEdges_Rect(&rect, 0, -8 * gap_UI, 0, 8 * gap_UI);
        translate_Lang(&str);
        drawWrapRange_Text(uiContent_FontId,
                           addY_I2(topLeft_Rect(rect), yOff),
                           width_Rect(rect),
                           fg,
                           range_String(&str));
    }
    deinit_String(&str);
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
    if (run->mediaType == image_GmRunMediaType) {
        SDL_Texture *tex = imageTexture_Media(media_GmDocument(d->widget->doc), run->mediaId);
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
    else if (run->mediaType) {
        /* Media UIs are drawn afterwards as a dynamic overlay. */
        return;
    }
    enum iColorId      fg        = run->textParams.color;
    const iGmDocument *doc       = d->widget->doc;
    const int          linkFlags = linkFlags_GmDocument(doc, run->linkId);
    /* Hover state of a link. */
    iBool isHover =
        (run->linkId && d->widget->hoverLink && run->linkId == d->widget->hoverLink->linkId &&
         ~run->flags & decoration_GmRunFlag);
    /* Visible (scrolled) position of the run. */
    const iInt2 visPos = addX_I2(add_I2(run->visBounds.pos, origin),
                                 /* Preformatted runs can be scrolled. */
                                 runOffset_DocumentWidget_(d->widget, run));
    const iRect visRect = { visPos, run->visBounds.size };
#if 0
    if (run->flags & footer_GmRunFlag) {
        iRect footerBack =
            (iRect){ visPos, init_I2(width_Rect(d->widgetBounds), run->visBounds.size.y) };
        footerBack.pos.x = left_Rect(d->widgetBounds);
        fillRect_Paint(&d->paint, footerBack, tmBackground_ColorId);
        return;
    }
#endif
    /* Fill the background. */ {
        if (run->linkId && linkFlags & isOpen_GmLinkFlag && ~linkFlags & content_GmLinkFlag) {
            /* Open links get a highlighted background. */
            int bg = tmBackgroundOpenLink_ColorId;
            const int frame = tmFrameOpenLink_ColorId;
            iRect     wideRect = { init_I2(left_Rect(d->widgetBounds), visPos.y),
                               init_I2(width_Rect(d->widgetBounds) +
                                           width_Widget(d->widget->scroll),
                                       height_Rect(run->visBounds)) };
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
            if (run->flags & (startOfLine_GmRunFlag | decoration_GmRunFlag)) {
                drawHLine_Paint(&d->paint, topLeft_Rect(wideRect), width_Rect(wideRect), frame);
            }
            /* TODO: The decoration is not marked as endOfLine, so it lacks the bottom line. */
//            if (run->flags & endOfLine_GmRunFlag) {
//                drawHLine_Paint(
//                    &d->paint, addY_I2(bottomLeft_Rect(wideRect), -1), width_Rect(wideRect), frame);
//            }
        }
        else if (run->linkId) {
            /* Normal background for runs that may change appearance. */
            fillRect_Paint(&d->paint, (iRect){ visPos, run->visBounds.size }, tmBackground_ColorId);
        }
    }
    if (run->linkId && ~run->flags & decoration_GmRunFlag) {
        fg = linkColor_GmDocument(doc, run->linkId, isHover ? textHover_GmLinkPart : text_GmLinkPart);
        if (linkFlags & content_GmLinkFlag) {
            fg = linkColor_GmDocument(doc, run->linkId, textHover_GmLinkPart); /* link is inactive */
        }
    }
    if (run->flags & altText_GmRunFlag) {
        const iInt2 margin = preRunMargin_GmDocument(doc, run->preId);
        fillRect_Paint(&d->paint, (iRect){ visPos, run->visBounds.size }, tmBackgroundAltText_ColorId);
        drawRect_Paint(&d->paint, (iRect){ visPos, run->visBounds.size }, tmQuoteIcon_ColorId);
        drawWrapRange_Text(run->textParams.font,
                           add_I2(visPos, margin),
                           run->visBounds.size.x - 2 * margin.x,
                           run->textParams.color,
                           run->text);
    }
    else if (run->flags & siteBanner_GmRunFlag) {
        /* Banner background. */
        iRect bannerBack = initCorners_Rect(topLeft_Rect(d->widgetBounds),
                                            init_I2(right_Rect(bounds_Widget(constAs_Widget(d->widget))),
                                                    visPos.y + height_Rect(run->visBounds)));
        fillRect_Paint(&d->paint, bannerBack, tmBannerBackground_ColorId);
        drawBannerRun_DrawContext_(d, run, visPos);
    }
    else {
        if (d->showLinkNumbers && run->linkId && run->flags & decoration_GmRunFlag) {
            const size_t ord = visibleLinkOrdinal_DocumentWidget_(d->widget, run->linkId);
            if (ord >= d->widget->ordinalBase) {
                const iChar ordChar =
                    linkOrdinalChar_DocumentWidget_(d->widget, ord - d->widget->ordinalBase);
                if (ordChar) {
                    const char *circle = "\u25ef"; /* Large Circle */
                    const int   circleFont = defaultContentRegular_FontId;
                    iRect nbArea = { init_I2(d->viewPos.x - gap_UI / 3, visPos.y),
                                     init_I2(3.95f * gap_Text, 1.0f * lineHeight_Text(circleFont)) };
                    drawRange_Text(
                        circleFont, topLeft_Rect(nbArea), tmQuote_ColorId, range_CStr(circle));
                    iRect circleArea = visualBounds_Text(circleFont, range_CStr(circle));
                    addv_I2(&circleArea.pos, topLeft_Rect(nbArea));
                    drawCentered_Text(defaultContentSmall_FontId,
                                      circleArea,
                                      iTrue,
                                    tmQuote_ColorId,
                                      "%lc",
                                      (int) ordChar);
                    goto runDrawn;
                }
            }
        }
        if (run->flags & quoteBorder_GmRunFlag) {
            drawVLine_Paint(&d->paint,
                            addX_I2(visPos,
                                    !run->textParams.isRTL
                                        ? -gap_Text * 5 / 2
                                        : (width_Rect(run->visBounds) + gap_Text * 5 / 2)),
                            height_Rect(run->visBounds),
                            tmQuoteIcon_ColorId);
        }
        drawBoundRange_Text(run->textParams.font,
                            visPos,
                            (run->textParams.isRTL ? -1 : 1) * width_Rect(run->visBounds),
                            fg,
                            run->text);
    runDrawn:;
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
            iMediaId imageId = linkImage_GmDocument(doc, run->linkId);
            iMediaId audioId = !imageId ? linkAudio_GmDocument(doc, run->linkId) : 0;
            iMediaId downloadId = !imageId && !audioId ?
                findLinkDownload_Media(constMedia_GmDocument(doc), run->linkId) : 0;
            iAssert(imageId || audioId || downloadId);
            if (imageId) {
                iAssert(!isEmpty_Rect(run->bounds));
                iGmMediaInfo info;
                imageInfo_Media(constMedia_GmDocument(doc), imageId, &info);
                const iInt2 imgSize = imageSize_Media(constMedia_GmDocument(doc), imageId);
                format_String(&text, "%s \u2014 %d x %d \u2014 %.1f%s",
                              info.type, imgSize.x, imgSize.y, info.numBytes / 1.0e6f,
                              cstr_Lang("mb"));
            }
            else if (audioId) {
                iGmMediaInfo info;
                audioInfo_Media(constMedia_GmDocument(doc), audioId, &info);
                format_String(&text, "%s", info.type);
            }
            else if (downloadId) {
                iGmMediaInfo info;
                downloadInfo_Media(constMedia_GmDocument(doc), downloadId, &info);
                format_String(&text, "%s", info.type);
            }
            if (findMediaRequest_DocumentWidget_(d->widget, run->linkId)) {
                appendFormat_String(
                    &text, "  %s" close_Icon, isHover ? escape_Color(tmLinkText_ColorId) : "");
            }
            const iInt2 size = measureRange_Text(metaFont, range_String(&text)).bounds.size;
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
            deinit_String(&text);
        }
        else if (run->flags & endOfLine_GmRunFlag &&
                 (mr = findMediaRequest_DocumentWidget_(d->widget, run->linkId)) != NULL) {
            if (!isFinished_GmRequest(mr->req)) {
                draw_Text(metaFont,
                          topRight_Rect(linkRect),
                          tmInlineContentMetadata_ColorId,
                          translateCStr_Lang(" \u2014 ${doc.fetching}\u2026 (%.1f ${mb})"),
                          (float) bodySize_GmRequest(mr->req) / 1.0e6f);
            }
        }
        else if (isHover) {
            const iGmLinkId linkId = d->widget->hoverLink->linkId;
            const iString * url    = linkUrl_GmDocument(doc, linkId);
            const int       flags  = linkFlags;
            iUrl parts;
            init_Url(&parts, url);
            fg                    = linkColor_GmDocument(doc, linkId, textHover_GmLinkPart);
            const enum iGmLinkScheme scheme = scheme_GmLinkFlag(flags);
            const iBool showHost  = (flags & humanReadable_GmLinkFlag &&
                                    (!isEmpty_Range(&parts.host) ||
                                     scheme == mailto_GmLinkScheme));
            const iBool showImage = (flags & imageFileExtension_GmLinkFlag) != 0;
            const iBool showAudio = (flags & audioFileExtension_GmLinkFlag) != 0;
            iString str;
            init_String(&str);
            /* Show scheme and host. */
            if (run->flags & endOfLine_GmRunFlag &&
                (flags & (imageFileExtension_GmLinkFlag | audioFileExtension_GmLinkFlag) ||
                 showHost)) {
                format_String(
                    &str,
                    "%s%s%s%s%s",
                    showHost ? "" : "",
                    showHost
                        ? (scheme == mailto_GmLinkScheme   ? cstr_String(url)
                           : scheme != gemini_GmLinkScheme ? format_CStr("%s://%s",
                                                                         cstr_Rangecc(parts.scheme),
                                                                         cstr_Rangecc(parts.host))
                                                           : cstr_Rangecc(parts.host))
                        : "",
                    showHost && (showImage || showAudio) ? " \u2014" : "",
                    showImage || showAudio
                        ? escape_Color(fg)
                        : escape_Color(linkColor_GmDocument(doc, run->linkId, domain_GmLinkPart)),
                    showImage || showAudio
                        ? format_CStr(showImage ? " %s \U0001f5bb" : " %s \U0001f3b5",
                                      cstr_Lang(showImage ? "link.hint.image" : "link.hint.audio"))
                        : "");
            }
            if (run->flags & endOfLine_GmRunFlag && flags & visited_GmLinkFlag) {
                iDate date;
                init_Date(&date, linkTime_GmDocument(doc, run->linkId));
                appendCStr_String(&str, " \u2014 ");
                appendCStr_String(
                    &str, escape_Color(linkColor_GmDocument(doc, run->linkId, visited_GmLinkPart)));
                append_String(&str, collect_String(format_Date(&date, "%b %d")));
            }
            if (!isEmpty_String(&str)) {
                if (run->textParams.isRTL) {
                    appendCStr_String(&str, " \u2014 ");
                }
                else {
                    prependCStr_String(&str, " \u2014 ");
                }
                const iInt2 textSize = measure_Text(metaFont, cstr_String(&str)).bounds.size;
                int tx = topRight_Rect(linkRect).x;
                const char *msg = cstr_String(&str);
                if (run->textParams.isRTL) {
                    tx = topLeft_Rect(linkRect).x - textSize.x;                    
                }
                if (tx + textSize.x > right_Rect(d->widgetBounds)) {
                    tx = right_Rect(d->widgetBounds) - textSize.x;
                    fillRect_Paint(&d->paint, (iRect){ init_I2(tx, top_Rect(linkRect)), textSize },
                                   uiBackground_ColorId);
                    msg += 4; /* skip the space and dash */
                    tx += measure_Text(metaFont, " \u2014").advance.x / 2;
                }
                drawAlign_Text(metaFont,
                               init_I2(tx, top_Rect(linkRect)),
                               linkColor_GmDocument(doc, run->linkId, domain_GmLinkPart),
                               left_Alignment,
                               "%s",
                               msg);
                deinit_String(&str);
            }
        }
    }
//    drawRect_Paint(&d->paint, (iRect){ visPos, run->bounds.size }, green_ColorId);
//    drawRect_Paint(&d->paint, (iRect){ visPos, run->visBounds.size }, red_ColorId);
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

static int sideElementAvailWidth_DocumentWidget_(const iDocumentWidget *d) {
    return left_Rect(documentBounds_DocumentWidget_(d)) -
           left_Rect(bounds_Widget(constAs_Widget(d))) - 2 * d->pageMargin * gap_UI;
}

static iBool isSideHeadingVisible_DocumentWidget_(const iDocumentWidget *d) {
    return sideElementAvailWidth_DocumentWidget_(d) >= lineHeight_Text(banner_FontId) * 4.5f;
}

static void updateSideIconBuf_DocumentWidget_(const iDocumentWidget *d) {
    if (!isExposed_Window(get_Window())) {
        return;
    }
    iDrawBufs *dbuf = d->drawBufs;
    dbuf->flags &= ~updateSideBuf_DrawBufsFlag;
    if (dbuf->sideIconBuf) {
        SDL_DestroyTexture(dbuf->sideIconBuf);
        dbuf->sideIconBuf = NULL;
    }
    const iGmRun *banner = siteBanner_GmDocument(d->doc);
    if (!banner) {
        return;
    }
    const int   margin           = gap_UI * d->pageMargin;
    const int   minBannerSize    = lineHeight_Text(banner_FontId) * 2;
    const iChar icon             = siteIcon_GmDocument(d->doc);
    const int   avail            = sideElementAvailWidth_DocumentWidget_(d) - margin;
    iBool       isHeadingVisible = isSideHeadingVisible_DocumentWidget_(d);
    /* Determine the required size. */
    iInt2 bufSize = init1_I2(minBannerSize);
    if (isHeadingVisible) {
        const iInt2 headingSize = measureWrapRange_Text(heading3_FontId, avail,
                                                        currentHeading_DocumentWidget_(d)).bounds.size;
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
    const iRect iconRect = { zero_I2(), init1_I2(minBannerSize) };
    int fg = drawSideRect_(&p, iconRect);
    iString str;
    initUnicodeN_String(&str, &icon, 1);
    drawCentered_Text(banner_FontId, iconRect, iTrue, fg, "%s", cstr_String(&str));
    deinit_String(&str);
    if (isHeadingVisible) {
        iRangecc    text = currentHeading_DocumentWidget_(d);
        iInt2       pos  = addY_I2(bottomLeft_Rect(iconRect), gap_Text);
        const int   font = heading3_FontId;
        drawWrapRange_Text(font, pos, avail, tmBannerSideTitle_ColorId, text);
    }
    endTarget_Paint(&p);
    SDL_SetTextureBlendMode(dbuf->sideIconBuf, SDL_BLENDMODE_BLEND);
}

static void drawSideElements_DocumentWidget_(const iDocumentWidget *d) {
    const iWidget *w         = constAs_Widget(d);
    const iRect    bounds    = bounds_Widget(w);
    const iRect    docBounds = documentBounds_DocumentWidget_(d);
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

static void drawMedia_DocumentWidget_(const iDocumentWidget *d, iPaint *p) {
    iConstForEach(PtrArray, i, &d->visibleMedia) {
        const iGmRun * run = i.ptr;
        if (run->mediaType == audio_GmRunMediaType) {
            iPlayerUI ui;
            init_PlayerUI(&ui,
                          audioPlayer_Media(media_GmDocument(d->doc), run->mediaId),
                          runRect_DocumentWidget_(d, run));
            draw_PlayerUI(&ui, p);
        }
        else if (run->mediaType == download_GmRunMediaType) {
            iDownloadUI ui;
            init_DownloadUI(&ui, d, run->mediaId, runRect_DocumentWidget_(d, run));
            draw_DownloadUI(&ui, p);
        }
    }
}

static void drawPin_(iPaint *p, iRect rangeRect, int dir) {
    const int pinColor = tmQuote_ColorId;
    const int height   = height_Rect(rangeRect);
    iRect pin;
    if (dir == 0) {
        pin = (iRect){ add_I2(topLeft_Rect(rangeRect), init_I2(-gap_UI / 4, -gap_UI)),
                       init_I2(gap_UI / 2, height + gap_UI) };
    }
    else {
        pin = (iRect){ addX_I2(topRight_Rect(rangeRect), -gap_UI / 4),
                       init_I2(gap_UI / 2, height + gap_UI) };
    }
    fillRect_Paint(p, pin, pinColor);
    fillRect_Paint(p, initCentered_Rect(dir == 0 ? topMid_Rect(pin) : bottomMid_Rect(pin),
                                        init1_I2(gap_UI * 2)), pinColor);
}

static void extend_GmRunRange_(iGmRunRange *runs) {
    if (runs->start) {
        runs->start--;
        runs->end++;
    }
}

static iBool render_DocumentWidget_(const iDocumentWidget *d, iDrawContext *ctx, iBool prerenderExtra) {
    iBool didDraw = iFalse;
    const iRect bounds = bounds_Widget(constAs_Widget(d));
    const iRect ctxWidgetBounds = init_Rect(
        0, 0, width_Rect(bounds) - constAs_Widget(d->scroll)->rect.size.x, height_Rect(bounds));
    const iRangei full = { 0, size_GmDocument(d->doc).y };
    const iRangei vis = ctx->vis;
    iVisBuf *visBuf = d->visBuf; /* will be updated now */
    d->drawBufs->lastRenderTime = SDL_GetTicks();
    /* Swap buffers around to have room available both before and after the visible region. */
    allocVisBuffer_DocumentWidget_(d);
    reposition_VisBuf(visBuf, vis);
    /* Redraw the invalid ranges. */
    if (~flags_Widget(constAs_Widget(d)) & destroyPending_WidgetFlag) {
        iPaint *p = &ctx->paint;
        init_Paint(p);
        iForIndices(i, visBuf->buffers) {
            iVisBufTexture *buf  = &visBuf->buffers[i];
            iVisBufMeta *   meta = buf->user;
            const iRangei   bufRange    = intersect_Rangei(bufferRange_VisBuf(visBuf, i), full);
            const iRangei   bufVisRange = intersect_Rangei(bufRange, vis);
            ctx->widgetBounds = moved_Rect(ctxWidgetBounds, init_I2(0, -buf->origin));
            ctx->viewPos      = init_I2(left_Rect(ctx->docBounds) - left_Rect(bounds), -buf->origin);
//            printf("  buffer %zu: buf vis range %d...%d\n", i, bufVisRange.start, bufVisRange.end);
            if (!prerenderExtra && !isEmpty_Range(&bufVisRange)) {
                didDraw = iTrue;
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
                    }
                }
                else {
                    /* Progressively fill the required runs. */
                    if (meta->runsDrawn.start) {
                        beginTarget_Paint(p, buf->texture);
                        meta->runsDrawn.start = renderProgressive_GmDocument(d->doc, meta->runsDrawn.start,
                                                                             -1, iInvalidSize,
                                                                             bufVisRange,
                                                                             drawRun_DrawContext_,
                                                                             ctx);
                        buf->validRange.start = bufVisRange.start;
                    }
                    if (meta->runsDrawn.end) {
                        beginTarget_Paint(p, buf->texture);
                        meta->runsDrawn.end = renderProgressive_GmDocument(d->doc, meta->runsDrawn.end,
                                                                           +1, iInvalidSize,
                                                                           bufVisRange,
                                                                           drawRun_DrawContext_,
                                                                           ctx);
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
                iConstForEach(PtrSet, r, d->invalidRuns) {
                    const iGmRun *run = *r.value;
                    if (isOverlapping_Rangei(bufRange, ySpan_Rect(run->visBounds))) {
                        beginTarget_Paint(p, buf->texture);
                        drawRun_DrawContext_(ctx, run);
                    }
                }
            }
            endTarget_Paint(p);
            if (prerenderExtra && didDraw) {
                return iTrue;
            }
        }
        clear_PtrSet(d->invalidRuns);
    }
    return didDraw;
}

static void prerender_DocumentWidget_(iAny *context) {
    if (current_Root() == NULL) {
        /* The widget has probably been removed from the widget tree, pending destruction.
           Tickers are not cancelled until the widget is actually destroyed. */
        return;
    }
    const iDocumentWidget *d = context;
    iDrawContext ctx = {
        .widget          = d,
        .docBounds       = documentBounds_DocumentWidget_(d),
        .vis             = visibleRange_DocumentWidget_(d),
        .showLinkNumbers = (d->flags & showLinkNumbers_DocumentWidgetFlag) != 0
    };
//    printf("%u prerendering\n", SDL_GetTicks());
    if (d->visBuf->buffers[0].texture) {
        if (render_DocumentWidget_(d, &ctx, iTrue /* just fill up progressively */)) {
            /* Something was drawn, should check if there is still more to do. */
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
    /* TODO: Come up with a better palette caching system.
       It should be able to recompute cached colors in `History` when the theme has changed.
       Cache the theme seed in `GmDocument`? */
//    makePaletteGlobal_GmDocument(d->doc);
    if (d->drawBufs->flags & updateTimestampBuf_DrawBufsFlag) {
        updateTimestampBuf_DocumentWidget_(d);
    }
    if (d->drawBufs->flags & updateSideBuf_DrawBufsFlag) {
        updateSideIconBuf_DocumentWidget_(d);
    }
    const iRect   docBounds = documentBounds_DocumentWidget_(d);
    const iRangei vis       = visibleRange_DocumentWidget_(d);
    iDrawContext  ctx = {
        .widget          = d,
        .docBounds       = docBounds,
        .vis             = vis,
        .showLinkNumbers = (d->flags & showLinkNumbers_DocumentWidgetFlag) != 0,
    };
    init_Paint(&ctx.paint);
    render_DocumentWidget_(d, &ctx, iFalse /* just the mandatory parts */);
    setClip_Paint(&ctx.paint, clipBounds);
    int yTop = docBounds.pos.y - pos_SmoothScroll(&d->scrollY);
    draw_VisBuf(d->visBuf, init_I2(bounds.pos.x, yTop), ySpan_Rect(bounds));
    /* Text markers. */
    const iBool isTouchSelecting = (flags_Widget(w) & touchDrag_WidgetFlag) != 0;
    if (!isEmpty_Range(&d->foundMark) || !isEmpty_Range(&d->selectMark)) {
        SDL_Renderer *render = renderer_Window(get_Window());
        ctx.firstMarkRect = zero_Rect();
        ctx.lastMarkRect = zero_Rect();
        SDL_SetRenderDrawBlendMode(render,
                                   isDark_ColorTheme(colorTheme_App()) ? SDL_BLENDMODE_ADD
                                                                       : SDL_BLENDMODE_BLEND);
        ctx.viewPos = topLeft_Rect(docBounds);
        /* Marker starting outside the visible range? */
        if (d->visibleRuns.start) {
            if (!isEmpty_Range(&d->selectMark) &&
                d->selectMark.start < d->visibleRuns.start->text.start &&
                d->selectMark.end > d->visibleRuns.start->text.start) {
                ctx.inSelectMark = iTrue;
            }
            if (isEmpty_Range(&d->foundMark) &&
                d->foundMark.start < d->visibleRuns.start->text.start &&
                d->foundMark.end > d->visibleRuns.start->text.start) {
                ctx.inFoundMark = iTrue;
            }
        }
        render_GmDocument(d->doc, vis, drawMark_DrawContext_, &ctx);
        SDL_SetRenderDrawBlendMode(render, SDL_BLENDMODE_NONE);
        /* Selection range pins. */
        if (isTouchSelecting) {
            drawPin_(&ctx.paint, ctx.firstMarkRect, 0);
            drawPin_(&ctx.paint, ctx.lastMarkRect, 1);
        }
    }
    drawMedia_DocumentWidget_(d, &ctx.paint);
    /* Fill the top and bottom, in case the document is short. */
    if (yTop > top_Rect(bounds)) {
        fillRect_Paint(&ctx.paint,
                       (iRect){ bounds.pos, init_I2(bounds.size.x, yTop - top_Rect(bounds)) },
                       hasSiteBanner_GmDocument(d->doc) ? tmBannerBackground_ColorId
                                                        : tmBackground_ColorId);
    }
    const int yBottom = yTop + size_GmDocument(d->doc).y + 1;
    if (yBottom < bottom_Rect(bounds)) {
        fillRect_Paint(&ctx.paint,
                       init_Rect(bounds.pos.x, yBottom, bounds.size.x, bottom_Rect(bounds) - yBottom),
                       tmBackground_ColorId);
    }
    unsetClip_Paint(&ctx.paint);
    drawSideElements_DocumentWidget_(d);
    if (prefs_App()->hoverLink && d->hoverLink) {
        const int      font     = uiLabel_FontId;
        const iRangecc linkUrl  = range_String(linkUrl_GmDocument(d->doc, d->hoverLink->linkId));
        const iInt2    size     = measureRange_Text(font, linkUrl).bounds.size;
        const iRect    linkRect = { addY_I2(bottomLeft_Rect(bounds), -size.y),
                                    addX_I2(size, 2 * gap_UI) };
        fillRect_Paint(&ctx.paint, linkRect, tmBackground_ColorId);
        drawRange_Text(font, addX_I2(topLeft_Rect(linkRect), gap_UI), tmParagraph_ColorId, linkUrl);
    }
    if (colorTheme_App() == pureWhite_ColorTheme) {
        drawHLine_Paint(&ctx.paint, topLeft_Rect(bounds), width_Rect(bounds), uiSeparator_ColorId);
    }
    drawChildren_Widget(w);
    /* Alt text. */
    const float altTextOpacity = value_Anim(&d->altTextOpacity) * 6 - 5;
    if (d->hoverAltPre && altTextOpacity > 0) {
        const iGmPreMeta *meta = preMeta_GmDocument(d->doc, d->hoverAltPre->preId);
        if (meta->flags & topLeft_GmPreMetaFlag && ~meta->flags & decoration_GmRunFlag &&
            !isEmpty_Range(&meta->altText)) {
            const int   margin   = 3 * gap_UI / 2;
            const int   altFont  = uiLabel_FontId;
            const int   wrap     = docBounds.size.x - 2 * margin;
            iInt2 pos            = addY_I2(add_I2(docBounds.pos, meta->pixelRect.pos),
                                           -pos_SmoothScroll(&d->scrollY));
            const iInt2 textSize = measureWrapRange_Text(altFont, wrap, meta->altText).bounds.size;
            pos.y -= textSize.y + gap_UI;
            pos.y               = iMax(pos.y, top_Rect(bounds));
            const iRect altRect = { pos, init_I2(docBounds.size.x, textSize.y) };
            ctx.paint.alpha     = altTextOpacity * 255;
            if (altTextOpacity < 1) {
                SDL_SetRenderDrawBlendMode(renderer_Window(get_Window()), SDL_BLENDMODE_BLEND);
            }
            fillRect_Paint(&ctx.paint, altRect, tmBackgroundAltText_ColorId);
            drawRect_Paint(&ctx.paint, altRect, tmQuoteIcon_ColorId);
            setOpacity_Text(altTextOpacity);
            drawWrapRange_Text(altFont, addX_I2(pos, margin), wrap,
                               tmQuote_ColorId, meta->altText);
            SDL_SetRenderDrawBlendMode(renderer_Window(get_Window()), SDL_BLENDMODE_NONE);
            setOpacity_Text(1.0f);
        }
    }
    /* Pinch zoom indicator. */
    if (d->flags & pinchZoom_DocumentWidgetFlag) {
        const int   font   = defaultLargeBold_FontId;
        const int   height = lineHeight_Text(font) * 2;
        const iInt2 size   = init_I2(height * 2, height);
        const iRect rect   = { sub_I2(mid_Rect(bounds), divi_I2(size, 2)), size };
        fillRect_Paint(&ctx.paint, rect, d->pinchZoomPosted == 100 ? uiTextCaution_ColorId : uiTextAction_ColorId);
        drawCentered_Text(font, bounds, iFalse, uiBackground_ColorId, "%d %%",
                          d->pinchZoomPosted);
    }
    /* Touch selection indicator. */
    if (isTouchSelecting) {
        iRect rect = { topLeft_Rect(bounds),
                       init_I2(width_Rect(bounds), lineHeight_Text(uiLabelBold_FontId)) };
        fillRect_Paint(&ctx.paint, rect, uiTextAction_ColorId);
        const iRangecc mark = selectMark_DocumentWidget_(d);
        drawCentered_Text(uiLabelBold_FontId, rect, iFalse, uiBackground_ColorId, "%zu bytes selected",
                          size_Range(&mark));
    }
    if (w->offsetRef) {
        const int offX = visualOffsetByReference_Widget(w);
        if (offX) {
            setClip_Paint(&ctx.paint, clipBounds);
            SDL_SetRenderDrawBlendMode(renderer_Window(get_Window()), SDL_BLENDMODE_BLEND);
            ctx.paint.alpha = iAbs(offX) / (float) get_Window()->size.x * 300;
            fillRect_Paint(&ctx.paint, bounds, backgroundFadeColor_Widget());
            SDL_SetRenderDrawBlendMode(renderer_Window(get_Window()), SDL_BLENDMODE_NONE);
            unsetClip_Paint(&ctx.paint);
        }
        else {
            /* TODO: Should have a better place to do this; drawing is supposed to be immutable. */
            iWidget *mut = iConstCast(iWidget *, w);
            mut->offsetRef = NULL;
            mut->flags &= ~refChildrenOffset_WidgetFlag;
        }
    }
}

/*----------------------------------------------------------------------------------------------*/

iHistory *history_DocumentWidget(iDocumentWidget *d) {
    return d->mod.history;
}

const iString *url_DocumentWidget(const iDocumentWidget *d) {
    return d->mod.url;
}

const iGmDocument *document_DocumentWidget(const iDocumentWidget *d) {
    return d->doc;
}

const iBlock *sourceContent_DocumentWidget(const iDocumentWidget *d) {
    return &d->sourceContent;
}

int documentWidth_DocumentWidget(const iDocumentWidget *d) {
    return documentWidth_DocumentWidget_(d);
}

const iString *feedTitle_DocumentWidget(const iDocumentWidget *d) {
    if (!isEmpty_String(title_GmDocument(d->doc))) {
        return title_GmDocument(d->doc);
    }
    return bookmarkTitle_DocumentWidget(d);
}

const iString *bookmarkTitle_DocumentWidget(const iDocumentWidget *d) {
    iStringArray *title = iClob(new_StringArray());
    if (!isEmpty_String(title_GmDocument(d->doc))) {
        pushBack_StringArray(title, title_GmDocument(d->doc));
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
    deserialize_PersistentDocumentState(&d->mod, ins);
    parseUser_DocumentWidget_(d);
    updateFromHistory_DocumentWidget_(d);
}

static void setUrl_DocumentWidget_(iDocumentWidget *d, const iString *url) {
    url = canonicalUrl_String(url);
    if (!equal_String(d->mod.url, url)) {
        d->flags |= urlChanged_DocumentWidgetFlag;
        set_String(d->mod.url, url);
    }
}

void setUrlFlags_DocumentWidget(iDocumentWidget *d, const iString *url, int setUrlFlags) {
    iChangeFlags(d->flags, openedFromSidebar_DocumentWidgetFlag,
                 (setUrlFlags & openedFromSidebar_DocumentWidgetSetUrlFlag) != 0);
    const iBool isFromCache = (setUrlFlags & useCachedContentIfAvailable_DocumentWidgetSetUrlFlag) != 0;
    setLinkNumberMode_DocumentWidget_(d, iFalse);
    setUrl_DocumentWidget_(d, urlFragmentStripped_String(url));
    /* See if there a username in the URL. */
    parseUser_DocumentWidget_(d);
    if (!isFromCache || !updateFromHistory_DocumentWidget_(d)) {
        fetch_DocumentWidget_(d);
    }
}

void setUrlAndSource_DocumentWidget(iDocumentWidget *d, const iString *url, const iString *mime,
                                    const iBlock *source) {
    d->flags &= ~openedFromSidebar_DocumentWidgetFlag;
    setLinkNumberMode_DocumentWidget_(d, iFalse);
    setUrl_DocumentWidget_(d, url);
    parseUser_DocumentWidget_(d);
    iGmResponse *resp = new_GmResponse();
    resp->statusCode = success_GmStatusCode;
    initCurrent_Time(&resp->when);
    set_String(&resp->meta, mime);
    set_Block(&resp->body, source);
    updateFromCachedResponse_DocumentWidget_(d, 0, resp, NULL);
    delete_GmResponse(resp);
}

iDocumentWidget *duplicate_DocumentWidget(const iDocumentWidget *orig) {
    iDocumentWidget *d = new_DocumentWidget();
    delete_History(d->mod.history);
    d->initNormScrollY = normScrollPos_DocumentWidget_(d);
    d->mod.history = copy_History(orig->mod.history);
    setUrlFlags_DocumentWidget(d, orig->mod.url, useCachedContentIfAvailable_DocumentWidgetSetUrlFlag);
    return d;
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

void setOpenedFromSidebar_DocumentWidget(iDocumentWidget *d, iBool fromSidebar) {
    iChangeFlags(d->flags, openedFromSidebar_DocumentWidgetFlag, fromSidebar);
//    setCachedDocument_History(d->mod.history, d->doc, fromSidebar);
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
    updateDocumentWidthRetainingScrollPosition_DocumentWidget_(d, iFalse);
    resetWideRuns_DocumentWidget_(d);
    d->drawBufs->flags |= updateSideBuf_DrawBufsFlag;
    updateVisible_DocumentWidget_(d);
    invalidate_DocumentWidget_(d);
}

iBeginDefineSubclass(DocumentWidget, Widget)
    .processEvent = (iAny *) processEvent_DocumentWidget_,
    .draw         = (iAny *) draw_DocumentWidget_,
iEndDefineSubclass(DocumentWidget)
