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

#include "documentwidget.h"

#include "app.h"
#include "audio/player.h"
#include "command.h"
#include "defs.h"
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
#include "playerui.h"
#include "scrollwidget.h"
#include "util.h"
#include "visbuf.h"

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

iDeclareClass(MediaRequest)

struct Impl_MediaRequest {
    iObject          object;
    iDocumentWidget *doc;
    iGmLinkId        linkId;
    iGmRequest *     req;
};

static void updated_MediaRequest_(iAnyObject *obj) {
    iMediaRequest *d = obj;
    postCommandf_App("media.updated link:%u request:%p", d->linkId, d);
}

static void finished_MediaRequest_(iAnyObject *obj) {
    iMediaRequest *d = obj;
    postCommandf_App("media.finished link:%u request:%p", d->linkId, d);
}

void init_MediaRequest(iMediaRequest *d, iDocumentWidget *doc, iGmLinkId linkId, const iString *url) {
    d->doc    = doc;
    d->linkId = linkId;
    d->req    = new_GmRequest(certs_App());
    setUrl_GmRequest(d->req, url);
    iConnect(GmRequest, d->req, updated, d, updated_MediaRequest_);
    iConnect(GmRequest, d->req, finished, d, finished_MediaRequest_);
    submit_GmRequest(d->req);
}

void deinit_MediaRequest(iMediaRequest *d) {
    iDisconnect(GmRequest, d->req, updated, d, updated_MediaRequest_);
    iDisconnect(GmRequest, d->req, finished, d, finished_MediaRequest_);
    iRelease(d->req);
}

iDefineObjectConstructionArgs(MediaRequest,
                              (iDocumentWidget *doc, iGmLinkId linkId, const iString *url),
                              doc, linkId, url)
iDefineClass(MediaRequest)

/*----------------------------------------------------------------------------------------------*/

iDeclareType(Model)
iDeclareTypeConstruction(Model)
iDeclareTypeSerialization(Model)

struct Impl_Model {
    /* state that persists across sessions */
    iHistory *history;
    iString * url;
};

void init_Model(iModel *d) {
    d->history = new_History();
    d->url     = new_String();
}

void deinit_Model(iModel *d) {
    delete_String(d->url);
    delete_History(d->history);
}

void serialize_Model(const iModel *d, iStream *outs) {
    serialize_String(d->url, outs);
    write16_Stream(outs, 0 /*d->zoomPercent*/);
    serialize_History(d->history, outs);
}

void deserialize_Model(iModel *d, iStream *ins) {
    deserialize_String(d->url, ins);
    /*d->zoomPercent =*/ read16_Stream(ins);
    deserialize_History(d->history, ins);
}

iDefineTypeConstruction(Model)

/*----------------------------------------------------------------------------------------------*/

iDeclareType(OutlineItem)

struct Impl_OutlineItem {
    iRangecc text;
    int      font;
    iRect    rect;
};

/*----------------------------------------------------------------------------------------------*/

static void animatePlayers_DocumentWidget_      (iDocumentWidget *d);
static void updateSideIconBuf_DocumentWidget_   (iDocumentWidget *d);

static const int smoothDuration_DocumentWidget_  = 600; /* milliseconds */
static const int outlineMinWidth_DocumentWdiget_ = 45;  /* times gap_UI */
static const int outlineMaxWidth_DocumentWidget_ = 65;  /* times gap_UI */
static const int outlinePadding_DocumentWidget_  = 3;   /* times gap_UI */

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
};

struct Impl_DocumentWidget {
    iWidget        widget;
    enum iRequestState state;
    iModel         mod;
    int            flags;
    iString *      titleUser;
    iGmRequest *   request;
    iAtomicInt     isRequestUpdated; /* request has new content, need to parse it */
    iObjectList *  media;
    iString        sourceMime;
    iBlock         sourceContent; /* original content as received, for saving */
    iTime          sourceTime;
    iGmDocument *  doc;
    int            certFlags;
    iDate          certExpiry;
    iString *      certSubject;
    int            redirectCount;
    iRangecc       selectMark;
    iRangecc       foundMark;
    int            pageMargin;
    iPtrArray      visibleLinks;
    iPtrArray      visiblePlayers; /* currently playing audio */
    const iGmRun * grabbedPlayer; /* currently adjusting volume in a player */
    float          grabbedStartVolume;
    int            playerTimer;
    const iGmRun * hoverLink;
    const iGmRun * contextLink;
    const iGmRun * firstVisibleRun;
    const iGmRun * lastVisibleRun;
    iClick         click;
    float          initNormScrollY;
    iAnim          scrollY;
    iAnim          sideOpacity;
    iAnim          outlineOpacity;
    iArray         outline;
    iScrollWidget *scroll;
    iWidget *      menu;
    iWidget *      playerMenu;
    iVisBuf *      visBuf;
    iPtrSet *      invalidRuns;
    SDL_Texture *  sideIconBuf;
    iTextBuf *     timestampBuf;
};

iDefineObjectConstruction(DocumentWidget)

void init_DocumentWidget(iDocumentWidget *d) {
    iWidget *w = as_Widget(d);
    init_Widget(w);
    setId_Widget(w, "document000");
    setFlags_Widget(w, hover_WidgetFlag, iTrue);
    init_Model(&d->mod);
    d->flags = 0;
    iZap(d->certExpiry);
    d->certFlags        = 0;
    d->certSubject      = new_String();
    d->state            = blank_RequestState;
    d->titleUser        = new_String();
    d->request          = NULL;
    d->isRequestUpdated = iFalse;
    d->media            = new_ObjectList();
    d->doc              = new_GmDocument();
    d->redirectCount    = 0;
    d->initNormScrollY  = 0;
    init_Anim(&d->scrollY, 0);
    d->selectMark       = iNullRange;
    d->foundMark        = iNullRange;
    d->pageMargin       = 5;
    d->hoverLink        = NULL;
    d->contextLink      = NULL;
    d->firstVisibleRun  = NULL;
    d->lastVisibleRun   = NULL;
    d->visBuf           = new_VisBuf();
    d->invalidRuns      = new_PtrSet();
    init_Array(&d->outline, sizeof(iOutlineItem));
    init_Anim(&d->sideOpacity, 0);
    init_Anim(&d->outlineOpacity, 0);
    init_String(&d->sourceMime);
    init_Block(&d->sourceContent, 0);
    iZap(d->sourceTime);
    init_PtrArray(&d->visibleLinks);
    init_PtrArray(&d->visiblePlayers);
    d->grabbedPlayer = NULL;
    d->playerTimer   = 0;
    init_Click(&d->click, d, SDL_BUTTON_LEFT);
    addChild_Widget(w, iClob(d->scroll = new_ScrollWidget()));
    d->menu         = NULL; /* created when clicking */
    d->playerMenu   = NULL;
    d->sideIconBuf  = NULL;
    d->timestampBuf = NULL;
    addChildFlags_Widget(w,
                         iClob(new_IndicatorWidget()),
                         resizeToParentWidth_WidgetFlag | resizeToParentHeight_WidgetFlag);
#if !defined (iPlatformApple) /* in system menu */
    addAction_Widget(w, reload_KeyShortcut, "navigate.reload");
    addAction_Widget(w, SDLK_w, KMOD_PRIMARY, "tabs.close");
#endif
    addAction_Widget(w, navigateBack_KeyShortcut, "navigate.back");
    addAction_Widget(w, navigateForward_KeyShortcut, "navigate.forward");
}

void deinit_DocumentWidget(iDocumentWidget *d) {
    if (d->sideIconBuf) {
        SDL_DestroyTexture(d->sideIconBuf);
    }
    delete_TextBuf(d->timestampBuf);
    delete_VisBuf(d->visBuf);
    delete_PtrSet(d->invalidRuns);
    deinit_Array(&d->outline);
    iRelease(d->media);
    iRelease(d->request);
    deinit_Block(&d->sourceContent);
    deinit_String(&d->sourceMime);
    iRelease(d->doc);
    if (d->playerTimer) {
        SDL_RemoveTimer(d->playerTimer);
    }
    deinit_PtrArray(&d->visiblePlayers);
    deinit_PtrArray(&d->visibleLinks);
    delete_String(d->certSubject);
    delete_String(d->titleUser);
    deinit_Model(&d->mod);
}

static void requestUpdated_DocumentWidget_(iAnyObject *obj) {
    iDocumentWidget *d = obj;
    const int wasUpdated = exchange_Atomic(&d->isRequestUpdated, iTrue);
    if (!wasUpdated) {
        postCommand_Widget(obj, "document.request.updated doc:%p request:%p", d, d->request);
    }
}

static void requestTimedOut_DocumentWidget_(iAnyObject *obj) {
    iDocumentWidget *d = obj;
    postCommandf_App("document.request.timeout doc:%p request:%p", d, d->request);
}

static void requestFinished_DocumentWidget_(iAnyObject *obj) {
    iDocumentWidget *d = obj;
    postCommand_Widget(obj, "document.request.finished doc:%p request:%p", d, d->request);
}

static int documentWidth_DocumentWidget_(const iDocumentWidget *d) {
    const iWidget *w      = constAs_Widget(d);
    const iRect    bounds = bounds_Widget(w);
    const iPrefs * prefs  = prefs_App();
    return iMini(bounds.size.x - gap_UI * d->pageMargin * 2,
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
    if (!hasSiteBanner_GmDocument(d->doc)) {
        rect.pos.y += margin;
        rect.size.y -= margin;
    }
    const iInt2 docSize = size_GmDocument(d->doc);
    if (docSize.y < rect.size.y) {
        /* Center vertically if short. */
        int offset = (rect.size.y - docSize.y) / 2;
        rect.pos.y += offset;
        rect.size.y = docSize.y;
    }
    return rect;
}

static int forceBreakWidth_DocumentWidget_(const iDocumentWidget *d) {
    if (forceLineWrap_App()) {
        const iRect bounds    = bounds_Widget(constAs_Widget(d));
        const iRect docBounds = documentBounds_DocumentWidget_(d);
        return right_Rect(bounds) - left_Rect(docBounds) - gap_UI * d->pageMargin;
    }
    return 0;
}

static iInt2 documentPos_DocumentWidget_(const iDocumentWidget *d, iInt2 pos) {
    return addY_I2(sub_I2(pos, topLeft_Rect(documentBounds_DocumentWidget_(d))),
                   value_Anim(&d->scrollY));
}

static iRangei visibleRange_DocumentWidget_(const iDocumentWidget *d) {
    const int margin = !hasSiteBanner_GmDocument(d->doc) ? gap_UI * d->pageMargin : 0;
    return (iRangei){ value_Anim(&d->scrollY) - margin,
                      value_Anim(&d->scrollY) + height_Rect(bounds_Widget(constAs_Widget(d))) -
                          margin };
}

static void addVisible_DocumentWidget_(void *context, const iGmRun *run) {
    iDocumentWidget *d = context;
    if (~run->flags & decoration_GmRunFlag && !run->imageId) {
        if (!d->firstVisibleRun) {
            d->firstVisibleRun = run;
        }
        d->lastVisibleRun = run;
    }
    if (run->audioId) {
        pushBack_PtrArray(&d->visiblePlayers, run);
    }
    if (run->linkId && linkFlags_GmDocument(d->doc, run->linkId) & supportedProtocol_GmLinkFlag) {
        pushBack_PtrArray(&d->visibleLinks, run);
    }
}

static float normScrollPos_DocumentWidget_(const iDocumentWidget *d) {
    const int docSize = size_GmDocument(d->doc).y;
    if (docSize) {
        return value_Anim(&d->scrollY) / (float) docSize;
    }
    return 0;
}

static int scrollMax_DocumentWidget_(const iDocumentWidget *d) {
    return size_GmDocument(d->doc).y - height_Rect(bounds_Widget(constAs_Widget(d))) +
           (hasSiteBanner_GmDocument(d->doc) ? 1 : 2) * d->pageMargin * gap_UI;
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

static void updateHover_DocumentWidget_(iDocumentWidget *d, iInt2 mouse) {
    const iWidget *w            = constAs_Widget(d);
    const iRect    docBounds    = documentBounds_DocumentWidget_(d);
    const iGmRun * oldHoverLink = d->hoverLink;
    d->hoverLink                = NULL;
    const iInt2 hoverPos = addY_I2(sub_I2(mouse, topLeft_Rect(docBounds)), value_Anim(&d->scrollY));
    if (isHover_Widget(w) && (~d->flags & noHoverWhileScrolling_DocumentWidgetFlag) &&
        (d->state == ready_RequestState || d->state == receivedPartialResponse_RequestState)) {
        iConstForEach(PtrArray, i, &d->visibleLinks) {
            const iGmRun *run = i.ptr;
            if (contains_Rect(run->bounds, hoverPos)) {
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
        refresh_Widget(as_Widget(d));
    }
    if (isHover_Widget(w) && !contains_Widget(constAs_Widget(d->scroll), mouse)) {
        setCursor_Window(get_Window(),
                         d->hoverLink ? SDL_SYSTEM_CURSOR_HAND : SDL_SYSTEM_CURSOR_IBEAM);
        if (d->hoverLink &&
            linkFlags_GmDocument(d->doc, d->hoverLink->linkId) & permanent_GmLinkFlag) {
            setCursor_Window(get_Window(), SDL_SYSTEM_CURSOR_ARROW); /* not dismissable */
        }
    }
}

static void animate_DocumentWidget_(void *ticker) {
    iDocumentWidget *d = ticker;
    if (!isFinished_Anim(&d->sideOpacity) || !isFinished_Anim(&d->outlineOpacity)) {
        addTicker_App(animate_DocumentWidget_, d);
    }
}

static void updateSideOpacity_DocumentWidget_(iDocumentWidget *d, iBool isAnimated) {
    float opacity = 0.0f;
    const iGmRun *banner = siteBanner_GmDocument(d->doc);
    if (banner && bottom_Rect(banner->visBounds) < value_Anim(&d->scrollY)) {
        opacity = 1.0f;
    }
    setValue_Anim(&d->sideOpacity, opacity, isAnimated ? (opacity < 0.5f ? 100 : 200) : 0);
    animate_DocumentWidget_(d);
}

static void updateOutlineOpacity_DocumentWidget_(iDocumentWidget *d) {
    float opacity = 0.0f;
    if (isEmpty_Array(&d->outline)) {
        setValue_Anim(&d->outlineOpacity, 0.0f, 0);
        return;
    }
    if (contains_Widget(constAs_Widget(d->scroll), mouseCoord_Window(get_Window()))) {
        opacity = 1.0f;
    }
    setValue_Anim(&d->outlineOpacity, opacity, opacity > 0.5f? 100 : 166);
    animate_DocumentWidget_(d);
}

static uint32_t playerUpdateInterval_DocumentWidget_(const iDocumentWidget *d) {
    if (document_App() != d) {
        return 0;
    }
    uint32_t interval = 0;
    iConstForEach(PtrArray, i, &d->visiblePlayers) {
        const iGmRun *run = i.ptr;
        iPlayer *     plr = audioPlayer_Media(media_GmDocument(d->doc), run->audioId);
        if (flags_Player(plr) & adjustingVolume_PlayerFlag ||
            (isStarted_Player(plr) && !isPaused_Player(plr))) {
            interval = 1000 / 15;
        }
    }
    return interval;
}

static uint32_t postPlayerUpdate_DocumentWidget_(uint32_t interval, void *context) {
    /* Called in timer thread; don't access the widget. */
    iUnused(context);
    postCommand_App("media.player.update");
    return interval;
}

static void updatePlayers_DocumentWidget_(iDocumentWidget *d) {
    if (document_App() == d) {
        refresh_Widget(d);
        iConstForEach(PtrArray, i, &d->visiblePlayers) {
            const iGmRun *run = i.ptr;
            iPlayer *     plr = audioPlayer_Media(media_GmDocument(d->doc), run->audioId);
            if (idleTimeMs_Player(plr) > 3000 && ~flags_Player(plr) & volumeGrabbed_PlayerFlag &&
                flags_Player(plr) & adjustingVolume_PlayerFlag) {
                setFlags_Player(plr, adjustingVolume_PlayerFlag, iFalse);
            }
        }
    }
    if (d->playerTimer && playerUpdateInterval_DocumentWidget_(d) == 0) {
        SDL_RemoveTimer(d->playerTimer);
        d->playerTimer = 0;
    }
}

static void animatePlayers_DocumentWidget_(iDocumentWidget *d) {
    if (document_App() != d) {
        if (d->playerTimer) {
            SDL_RemoveTimer(d->playerTimer);
            d->playerTimer = 0;
        }
        return;
    }
    uint32_t interval = playerUpdateInterval_DocumentWidget_(d);
    if (interval && !d->playerTimer) {
        d->playerTimer = SDL_AddTimer(interval, postPlayerUpdate_DocumentWidget_, d);
    }
}

static iRangecc currentHeading_DocumentWidget_(const iDocumentWidget *d) {
    iRangecc heading = iNullRange;
    if (d->firstVisibleRun) {
        iConstForEach(Array, i, headings_GmDocument(d->doc)) {
            const iGmHeading *head = i.value;
            if (head->level == 0) {
                if (head->text.start <= d->firstVisibleRun->text.start) {
                    heading = head->text;
                }
                if (d->lastVisibleRun && head->text.start > d->lastVisibleRun->text.start) {
                    break;
                }
            }
        }
    }
    return heading;
}

static void updateVisible_DocumentWidget_(iDocumentWidget *d) {
    const iRangei visRange = visibleRange_DocumentWidget_(d);
    const iRect   bounds   = bounds_Widget(as_Widget(d));
    setRange_ScrollWidget(d->scroll, (iRangei){ 0, scrollMax_DocumentWidget_(d) });
    const int docSize = size_GmDocument(d->doc).y;
    setThumb_ScrollWidget(d->scroll,
                          value_Anim(&d->scrollY),
                          docSize > 0 ? height_Rect(bounds) * size_Range(&visRange) / docSize : 0);
    clear_PtrArray(&d->visibleLinks);
    clear_PtrArray(&d->visiblePlayers);
    const iRangecc oldHeading = currentHeading_DocumentWidget_(d);
    /* Scan for visible runs. */ {
        d->firstVisibleRun = NULL;
        render_GmDocument(d->doc, visRange, addVisible_DocumentWidget_, d);
    }
    const iRangecc newHeading = currentHeading_DocumentWidget_(d);
    if (memcmp(&oldHeading, &newHeading, sizeof(oldHeading))) {
        updateSideIconBuf_DocumentWidget_(d);
    }
    updateHover_DocumentWidget_(d, mouseCoord_Window(get_Window()));
    updateSideOpacity_DocumentWidget_(d, iTrue);
    animatePlayers_DocumentWidget_(d);
    /* Remember scroll positions of recently visited pages. */ {
        iRecentUrl *recent = mostRecentUrl_History(d->mod.history);
        if (recent && docSize && d->state == ready_RequestState) {
            recent->normScrollY = normScrollPos_DocumentWidget_(d);
        }
    }
}

static void updateWindowTitle_DocumentWidget_(const iDocumentWidget *d) {
    iLabelWidget *tabButton = tabPageButton_Widget(findWidget_App("doctabs"), d);
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
            pushBackCStr_StringArray(title, "Lagrange");
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
    iBool setWindow = (document_App() == d);
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
                prependCStr_String(text, " ");
            }
            prependChar_String(text, siteIcon);
        }
        const int width = advanceRange_Text(default_FontId, range_String(text)).x;
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
                                  avail - advance_Text(default_FontId, "...").x,
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

static void updateTimestampBuf_DocumentWidget_(iDocumentWidget *d) {
    if (d->timestampBuf) {
        delete_TextBuf(d->timestampBuf);
        d->timestampBuf = NULL;
    }
    if (isValid_Time(&d->sourceTime)) {
        d->timestampBuf = new_TextBuf(
            uiLabel_FontId,
            cstrCollect_String(format_Time(&d->sourceTime, "Received at %I:%M %p\non %b %d, %Y")));
    }
}

static void invalidate_DocumentWidget_(iDocumentWidget *d) {
    invalidate_VisBuf(d->visBuf);
    clear_PtrSet(d->invalidRuns);
}

static int outlineWidth_DocumentWidget_(const iDocumentWidget *d) {
    const iWidget *w         = constAs_Widget(d);
    const iRect    bounds    = bounds_Widget(w);
    const int      docWidth  = documentWidth_DocumentWidget_(d);
    int            width =
        (width_Rect(bounds) - docWidth) / 2 - gap_Text * d->pageMargin - gap_UI * d->pageMargin
        - 2 * outlinePadding_DocumentWidget_ * gap_UI;
    if (width < outlineMinWidth_DocumentWdiget_ * gap_UI) {
        return outlineMinWidth_DocumentWdiget_ * gap_UI;
    }
    return iMin(width, outlineMaxWidth_DocumentWidget_ * gap_UI);
}

static iRangecc bannerText_DocumentWidget_(const iDocumentWidget *d) {
    return isEmpty_String(d->titleUser) ? range_String(bannerText_GmDocument(d->doc))
                                        : range_String(d->titleUser);
}

static void updateOutline_DocumentWidget_(iDocumentWidget *d) {
    iWidget *w = as_Widget(d);
    int outWidth = outlineWidth_DocumentWidget_(d);
    clear_Array(&d->outline);
    if (outWidth == 0 || d->state != ready_RequestState) {
        return;
    }
    if (size_GmDocument(d->doc).y < height_Rect(bounds_Widget(w)) * 2) {
        return; /* Too short */
    }
    iInt2 pos  = zero_I2();
//    const iRangecc topText = urlHost_String(d->mod.url);
//    iInt2 size = advanceWrapRange_Text(uiContent_FontId, outWidth, topText);
//    pushBack_Array(&d->outline, &(iOutlineItem){ topText, uiContent_FontId, (iRect){ pos, size },
//                                                 tmBannerTitle_ColorId, none_ColorId });
//    pos.y += size.y;
    iInt2 size;
    iConstForEach(Array, i, headings_GmDocument(d->doc)) {
        const iGmHeading *head = i.value;
        const int indent = head->level * 5 * gap_UI;
        size = advanceWrapRange_Text(uiLabel_FontId, outWidth - indent, head->text);
        if (head->level == 0) {
            pos.y += gap_UI * 1.5f;
        }
        pushBack_Array(
            &d->outline,
            &(iOutlineItem){ head->text, uiLabel_FontId, (iRect){ addX_I2(pos, indent), size } });
        pos.y += size.y;
    }
}

static void setSource_DocumentWidget_(iDocumentWidget *d, const iString *source) {
    setUrl_GmDocument(d->doc, d->mod.url);
    setSource_GmDocument(
        d->doc, source, documentWidth_DocumentWidget_(d), forceBreakWidth_DocumentWidget_(d));
    d->foundMark      = iNullRange;
    d->selectMark     = iNullRange;
    d->hoverLink      = NULL;
    d->contextLink    = NULL;
    d->lastVisibleRun = NULL;
    setValue_Anim(&d->outlineOpacity, 0.0f, 0);
    updateWindowTitle_DocumentWidget_(d);
    updateVisible_DocumentWidget_(d);
    updateSideIconBuf_DocumentWidget_(d);
    updateOutline_DocumentWidget_(d);
    invalidate_DocumentWidget_(d);
    refresh_Widget(as_Widget(d));
}

static void updateTheme_DocumentWidget_(iDocumentWidget *d) {
    if (isEmpty_String(d->titleUser)) {
        setThemeSeed_GmDocument(d->doc,
                                collect_Block(newRange_Block(urlHost_String(d->mod.url))));
    }
    else {
        setThemeSeed_GmDocument(d->doc, &d->titleUser->chars);
    }
    updateTimestampBuf_DocumentWidget_(d);
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
            case nonGeminiRedirect_GmStatusCode:
            case tooManyRedirects_GmStatusCode:
                appendFormat_String(src, "\n=> %s\n", cstr_String(meta));
                break;
            case tlsFailure_GmStatusCode:
                useBanner = iFalse; /* valid data wasn't received from host */
                appendFormat_String(src, "\n\n>%s\n", cstr_String(meta));
                break;
            case failedToOpenFile_GmStatusCode:
            case certificateNotValid_GmStatusCode:
                appendFormat_String(src, "\n\n%s", cstr_String(meta));
                break;
            case unsupportedMimeType_GmStatusCode: {
                iString *key = collectNew_String();
                toString_Sym(SDLK_s, KMOD_PRIMARY, key);
                appendFormat_String(src,
                                    "\n```\n%s\n```\n"
                                    "You can save it as a file to your Downloads folder, though. "
                                    "Press %s or select \"Save to Downloads\" from the menu.",
                                    cstr_String(meta),
                                    cstr_String(key));
                break;
            }
            case slowDown_GmStatusCode:
                appendFormat_String(src, "\n\nWait %s seconds before your next request.",
                                    cstr_String(meta));
                break;
            default:
                break;
        }
    }
    setSiteBannerEnabled_GmDocument(d->doc, useBanner);
    setSource_DocumentWidget_(d, src);
    updateTheme_DocumentWidget_(d);
    init_Anim(&d->scrollY, 0);
    init_Anim(&d->sideOpacity, 0);
    d->state = ready_RequestState;
}

static void updateFetchProgress_DocumentWidget_(iDocumentWidget *d) {
    iLabelWidget *prog   = findWidget_App("document.progress");
    const size_t  dlSize = d->request ? size_Block(body_GmRequest(d->request)) : 0;
    setFlags_Widget(as_Widget(prog), hidden_WidgetFlag, dlSize < 250000);
    if (isVisible_Widget(prog)) {
        updateText_LabelWidget(prog,
                               collectNewFormat_String("%s%.3f MB",
                                                       isFinished_GmRequest(d->request)
                                                           ? uiHeading_ColorEscape
                                                           : uiTextCaution_ColorEscape,
                                                       dlSize / 1.0e6f));
    }
}

static void updateDocument_DocumentWidget_(iDocumentWidget *d, const iGmResponse *response,
                                           const iBool isInitialUpdate) {
    if (d->state == ready_RequestState) {
        return;
    }
    const iBool isRequestFinished = !d->request || isFinished_GmRequest(d->request);
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
        updateTimestampBuf_DocumentWidget_(d);
        initBlock_String(&str, &response->body);
        if (isSuccess_GmStatusCode(statusCode)) {
            /* Check the MIME type. */
            iRangecc charset = range_CStr("utf-8");
            enum iGmDocumentFormat docFormat = undefined_GmDocumentFormat;
            const iString *mimeStr = collect_String(lower_String(&response->meta)); /* for convenience */
            set_String(&d->sourceMime, mimeStr);
            iRangecc mime = range_String(mimeStr);
            iRangecc seg = iNullRange;
            while (nextSplit_Rangecc(mime, ";", &seg)) {
                iRangecc param = seg;
                trim_Rangecc(&param);
                if (equal_Rangecc(param, "text/plain")) {
                    docFormat = plainText_GmDocumentFormat;
                    setRange_String(&d->sourceMime, param);
                }
                else if (equal_Rangecc(param, "text/gemini")) {
                    docFormat = gemini_GmDocumentFormat;
                    setRange_String(&d->sourceMime, param);
                }
                else if (startsWith_Rangecc(param, "image/") ||
                         startsWith_Rangecc(param, "audio/")) {
                    const iBool isAudio = startsWith_Rangecc(param, "audio/");
                    /* Make a simple document with an image or audio player. */
                    docFormat = gemini_GmDocumentFormat;
                    setRange_String(&d->sourceMime, param);
                    if ((isAudio && isInitialUpdate) || (!isAudio && isRequestFinished)) {
                        const char *linkTitle =
                            startsWith_String(mimeStr, "image/") ? "Image" : "Audio";
                        iUrl parts;
                        init_Url(&parts, d->mod.url);
                        if (!isEmpty_Range(&parts.path)) {
                            linkTitle =
                                baseName_Path(collect_String(newRange_String(parts.path))).start;
                        }
                        format_String(&str, "=> %s %s\n", cstr_String(d->mod.url), linkTitle);
                        setData_Media(media_GmDocument(d->doc),
                                      1,
                                      mimeStr,
                                      &response->body,
                                      !isRequestFinished ? partialData_MediaFlag : 0);
                        redoLayout_GmDocument(d->doc);
                    }
                    else if (isAudio && !isInitialUpdate) {
                        /* Update the audio content. */
                        setData_Media(media_GmDocument(d->doc),
                                      1,
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
            if (docFormat == undefined_GmDocumentFormat) {
                showErrorPage_DocumentWidget_(d, unsupportedMimeType_GmStatusCode, &response->meta);
                deinit_String(&str);
                return;
            }
            /* Convert the source to UTF-8 if needed. */
            if (!equalCase_Rangecc(charset, "utf-8")) {
                set_String(&str,
                           collect_String(decode_Block(&str.chars, cstr_Rangecc(charset))));
            }
        }
        if (setSource) {
            setSource_DocumentWidget_(d, &str);
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
    postCommandf_App("document.request.started doc:%p url:%s", d, cstr_String(d->mod.url));
    clear_ObjectList(d->media);
    d->certFlags = 0;
    d->state = fetching_RequestState;
    set_Atomic(&d->isRequestUpdated, iFalse);
    d->request = new_GmRequest(certs_App());
    setUrl_GmRequest(d->request, d->mod.url);
    iConnect(GmRequest, d->request, updated, d, requestUpdated_DocumentWidget_);
    iConnect(GmRequest, d->request, timeout, d, requestTimedOut_DocumentWidget_);
    iConnect(GmRequest, d->request, finished, d, requestFinished_DocumentWidget_);
    submit_GmRequest(d->request);
}

static void updateTrust_DocumentWidget_(iDocumentWidget *d, const iGmResponse *response) {
    if (response) {
        d->certFlags  = response->certFlags;
        d->certExpiry = response->certValidUntil;
        set_String(d->certSubject, &response->certSubject);
    }
    iLabelWidget *lock = findWidget_App("navbar.lock");
    if (~d->certFlags & available_GmCertFlag) {
        setFlags_Widget(as_Widget(lock), disabled_WidgetFlag, iTrue);
        updateTextCStr_LabelWidget(lock, gray50_ColorEscape openLock_CStr);
        return;
    }
    setFlags_Widget(as_Widget(lock), disabled_WidgetFlag, iFalse);
    if (~d->certFlags & domainVerified_GmCertFlag) {
        updateTextCStr_LabelWidget(lock, red_ColorEscape closedLock_CStr);
    }
    else if (d->certFlags & trusted_GmCertFlag) {
        updateTextCStr_LabelWidget(lock, green_ColorEscape closedLock_CStr);
    }
    else {
        updateTextCStr_LabelWidget(lock, orange_ColorEscape closedLock_CStr);
    }
}

static void parseUser_DocumentWidget_(iDocumentWidget *d) {
    clear_String(d->titleUser);
    iRegExp *userPats[2] = { new_RegExp("~([^/?]+)", 0),
                             new_RegExp("/users/([^/?]+)", caseInsensitive_RegExpOption) };
    iRegExpMatch m;
    init_RegExpMatch(&m);
    iForIndices(i, userPats) {
        if (matchString_RegExp(userPats[i], d->mod.url, &m)) {
            setRange_String(d->titleUser, capturedRange_RegExpMatch(&m, 1));
        }
        iRelease(userPats[i]);
    }
}

static iBool updateFromHistory_DocumentWidget_(iDocumentWidget *d) {
    const iRecentUrl *recent = findUrl_History(d->mod.history, d->mod.url);
    if (recent && recent->cachedResponse) {
        const iGmResponse *resp = recent->cachedResponse;
        clear_ObjectList(d->media);
        reset_GmDocument(d->doc);
        d->state = fetching_RequestState;
        d->initNormScrollY = recent->normScrollY;
        /* Use the cached response data. */
        updateTrust_DocumentWidget_(d, resp);
        d->sourceTime = resp->when;
        updateTimestampBuf_DocumentWidget_(d);
        set_Block(&d->sourceContent, &resp->body);
        updateDocument_DocumentWidget_(d, resp, iTrue);
        init_Anim(&d->scrollY, d->initNormScrollY * size_GmDocument(d->doc).y);
        d->state = ready_RequestState;
        updateSideOpacity_DocumentWidget_(d, iFalse);
        updateSideIconBuf_DocumentWidget_(d);
        updateOutline_DocumentWidget_(d);
        updateVisible_DocumentWidget_(d);
        postCommandf_App("document.changed doc:%p url:%s", d, cstr_String(d->mod.url));
        return iTrue;
    }
    else if (!isEmpty_String(d->mod.url)) {
        fetch_DocumentWidget_(d);
    }
    return iFalse;
}

static void refreshWhileScrolling_DocumentWidget_(iAny *ptr) {
    iDocumentWidget *d = ptr;
    updateVisible_DocumentWidget_(d);
    refresh_Widget(d);
    if (!isFinished_Anim(&d->scrollY)) {
        addTicker_App(refreshWhileScrolling_DocumentWidget_, d);
    }
}

static void smoothScroll_DocumentWidget_(iDocumentWidget *d, int offset, int duration) {
    int destY = targetValue_Anim(&d->scrollY) + offset;
    if (destY < 0) {
        destY = 0;
    }
    const int scrollMax = scrollMax_DocumentWidget_(d);
    if (scrollMax > 0) {
        destY = iMin(destY, scrollMax);
    }
    else {
        destY = 0;
    }
    setValueEased_Anim(&d->scrollY, destY, duration);
    updateVisible_DocumentWidget_(d);
    refresh_Widget(as_Widget(d));
    if (duration > 0) {
        iChangeFlags(d->flags, noHoverWhileScrolling_DocumentWidgetFlag, iTrue);
        addTicker_App(refreshWhileScrolling_DocumentWidget_, d);
    }
}

static void scroll_DocumentWidget_(iDocumentWidget *d, int offset) {
    smoothScroll_DocumentWidget_(d, offset, 0 /* instantly */);
}

static void scrollTo_DocumentWidget_(iDocumentWidget *d, int documentY, iBool centered) {
    init_Anim(&d->scrollY,
              documentY - (centered ? documentBounds_DocumentWidget_(d).size.y / 2
                                    : lineHeight_Text(paragraph_FontId)));
    scroll_DocumentWidget_(d, 0); /* clamp it */
}

static void checkResponse_DocumentWidget_(iDocumentWidget *d) {
    if (!d->request) {
        return;
    }
    enum iGmStatusCode statusCode = status_GmRequest(d->request);
    if (statusCode == none_GmStatusCode) {
        return;
    }
    if (d->state == fetching_RequestState) {
        d->state = receivedPartialResponse_RequestState;
        updateTrust_DocumentWidget_(d, response_GmRequest(d->request));
        init_Anim(&d->sideOpacity, 0);
        switch (category_GmStatusCode(statusCode)) {
            case categoryInput_GmStatusCode: {
                iUrl parts;
                init_Url(&parts, d->mod.url);
//                printf("%s\n", cstr_String(meta_GmRequest(d->request)));
                iWidget *dlg = makeValueInput_Widget(
                    as_Widget(d),
                    NULL,
                    format_CStr(uiHeading_ColorEscape "%s", cstr_Rangecc(parts.host)),
                    isEmpty_String(meta_GmRequest(d->request))
                        ? format_CStr("Please enter input for %s:", cstr_Rangecc(parts.path))
                        : cstr_String(meta_GmRequest(d->request)),
                    uiTextCaution_ColorEscape "Send \u21d2",
                    "document.input.submit");
                setSensitive_InputWidget(findChild_Widget(dlg, "input"),
                                         statusCode == sensitiveInput_GmStatusCode);
                break;
            }
            case categorySuccess_GmStatusCode:
                init_Anim(&d->scrollY, 0);
                reset_GmDocument(d->doc); /* new content incoming */
                updateDocument_DocumentWidget_(d, response_GmRequest(d->request), iTrue);
                break;
            case categoryRedirect_GmStatusCode:
                if (isEmpty_String(meta_GmRequest(d->request))) {
                    showErrorPage_DocumentWidget_(d, invalidRedirect_GmStatusCode, NULL);
                }
                else {
                    /* Only accept redirects that use gemini scheme. */
                    const iString *dstUrl = absoluteUrl_String(d->mod.url, meta_GmRequest(d->request));
                    if (d->redirectCount >= 5) {
                        showErrorPage_DocumentWidget_(d, tooManyRedirects_GmStatusCode, dstUrl);
                    }
                    else if (equalCase_Rangecc(urlScheme_String(dstUrl), "gemini")) {
                        postCommandf_App(
                            "open redirect:%d url:%s", d->redirectCount + 1, cstr_String(dstUrl));
                    }
                    else {
                        showErrorPage_DocumentWidget_(d, nonGeminiRedirect_GmStatusCode, dstUrl);
                    }
                    iReleasePtr(&d->request);
                }
                break;
            default:
                if (isDefined_GmError(statusCode)) {
                    showErrorPage_DocumentWidget_(d, statusCode, meta_GmRequest(d->request));
                }
                else if (category_GmStatusCode(statusCode) ==
                         categoryTemporaryFailure_GmStatusCode) {
                    showErrorPage_DocumentWidget_(
                        d, temporaryFailure_GmStatusCode, meta_GmRequest(d->request));
                }
                else if (category_GmStatusCode(statusCode) ==
                         categoryPermanentFailure_GmStatusCode) {
                    showErrorPage_DocumentWidget_(
                        d, permanentFailure_GmStatusCode, meta_GmRequest(d->request));
                }
                break;
        }
    }
    else if (d->state == receivedPartialResponse_RequestState) {
        switch (category_GmStatusCode(statusCode)) {
            case categorySuccess_GmStatusCode:
                /* More content available. */
                updateDocument_DocumentWidget_(d, response_GmRequest(d->request), iFalse);
                break;
            default:
                break;
        }
    }
}

static const char *sourceLoc_DocumentWidget_(const iDocumentWidget *d, iInt2 pos) {
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

static iBool requestMedia_DocumentWidget_(iDocumentWidget *d, iGmLinkId linkId) {
    if (!findMediaRequest_DocumentWidget_(d, linkId)) {
        pushBack_ObjectList(
            d->media,
            iClob(new_MediaRequest(
                d, linkId, absoluteUrl_String(d->mod.url, linkUrl_GmDocument(d->doc, linkId)))));
        invalidate_DocumentWidget_(d);
        return iTrue;
    }
    return iFalse;
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
            if (startsWith_String(meta_GmRequest(req->req), "audio/")) {
                /* TODO: Use a helper? This is same as below except for the partialData flag. */
                setData_Media(media_GmDocument(d->doc),
                              req->linkId,
                              meta_GmRequest(req->req),
                              body_GmRequest(req->req),
                              partialData_MediaFlag | allowHide_MediaFlag);
                redoLayout_GmDocument(d->doc);
                updateVisible_DocumentWidget_(d);
                invalidate_DocumentWidget_(d);
                refresh_Widget(as_Widget(d));
            }
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
            if (startsWith_String(meta_GmRequest(req->req), "image/") ||
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
            makeMessage_Widget(format_CStr(uiTextCaution_ColorEscape "%s", err->title), err->info);
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

static iBool handleCommand_DocumentWidget_(iDocumentWidget *d, const char *cmd) {
    iWidget *w = as_Widget(d);
    if (equal_Command(cmd, "window.resized") || equal_Command(cmd, "font.changed")) {
        const iGmRun *mid = middleRun_DocumentWidget_(d);
        const char *midLoc = (mid ? mid->text.start : NULL);
        /* Alt/Option key may be involved in window size changes. */
        iChangeFlags(d->flags, showLinkNumbers_DocumentWidgetFlag, iFalse);
        setWidth_GmDocument(
            d->doc, documentWidth_DocumentWidget_(d), forceBreakWidth_DocumentWidget_(d));
        scroll_DocumentWidget_(d, 0);
        if (midLoc) {
            mid = findRunAtLoc_GmDocument(d->doc, midLoc);
            if (mid) {
                scrollTo_DocumentWidget_(d, mid_Rect(mid->bounds).y, iTrue);
            }
        }
        updateSideIconBuf_DocumentWidget_(d);
        updateOutline_DocumentWidget_(d);
        invalidate_DocumentWidget_(d);
        dealloc_VisBuf(d->visBuf);
        updateWindowTitle_DocumentWidget_(d);
        refresh_Widget(w);
    }
    else if (equal_Command(cmd, "window.mouse.exited")) {
        updateOutlineOpacity_DocumentWidget_(d);
        return iFalse;
    }
    else if (equal_Command(cmd, "theme.changed") && document_App() == d) {
        updateTheme_DocumentWidget_(d);
        updateSideIconBuf_DocumentWidget_(d);
        invalidate_DocumentWidget_(d);
        refresh_Widget(w);
    }
    else if (equal_Command(cmd, "document.layout.changed") && document_App() == d) {
        updateSize_DocumentWidget(d);
    }
    else if (equal_Command(cmd, "tabs.changed")) {
        iChangeFlags(d->flags, showLinkNumbers_DocumentWidgetFlag, iFalse);
        if (cmp_String(id_Widget(w), suffixPtr_Command(cmd, "id")) == 0) {
            /* Set palette for our document. */
            updateTheme_DocumentWidget_(d);
            updateTrust_DocumentWidget_(d, NULL);
            updateSize_DocumentWidget(d);
            updateFetchProgress_DocumentWidget_(d);
        }
        init_Anim(&d->sideOpacity, 0);
        updateSideOpacity_DocumentWidget_(d, iFalse);
        updateOutlineOpacity_DocumentWidget_(d);
        updateWindowTitle_DocumentWidget_(d);
        allocVisBuffer_DocumentWidget_(d);
        animatePlayers_DocumentWidget_(d);
        return iFalse;
    }
    else if (equal_Command(cmd, "server.showcert") && d == document_App()) {
        const char *unchecked = red_ColorEscape   "\u2610";
        const char *checked   = green_ColorEscape "\u2611";
        makeMessage_Widget(
            uiHeading_ColorEscape "CERTIFICATE STATUS",
            format_CStr("%s%s  Domain name %s%s\n"
                        "%s%s  %s (%04d-%02d-%02d %02d:%02d:%02d)\n"
                        "%s%s  %s",
                        d->certFlags & domainVerified_GmCertFlag ? checked : unchecked,
                        uiText_ColorEscape,
                        d->certFlags & domainVerified_GmCertFlag ? "matches" : "mismatch",
                        ~d->certFlags & domainVerified_GmCertFlag
                            ? format_CStr(" (%s)", cstr_String(d->certSubject))
                            : "",
                        d->certFlags & timeVerified_GmCertFlag ? checked : unchecked,
                        uiText_ColorEscape,
                        d->certFlags & timeVerified_GmCertFlag ? "Not expired" : "Expired",
                        d->certExpiry.year,
                        d->certExpiry.month,
                        d->certExpiry.day,
                        d->certExpiry.hour,
                        d->certExpiry.minute,
                        d->certExpiry.second,
                        d->certFlags & trusted_GmCertFlag ? checked : unchecked,
                        uiText_ColorEscape,
                        d->certFlags & trusted_GmCertFlag ? "Trusted on first use"
                                                          : "Not trusted"));
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
        return iTrue;
    }
    else if (equal_Command(cmd, "document.copylink") && document_App() == d) {
        if (d->contextLink) {
            SDL_SetClipboardText(cstr_String(
                absoluteUrl_String(d->mod.url, linkUrl_GmDocument(d->doc, d->contextLink->linkId))));
        }
        else {
            SDL_SetClipboardText(cstr_String(d->mod.url));
        }
        return iTrue;
    }
    else if (equal_Command(cmd, "document.input.submit") && document_App() == d) {
        iString *value = collect_String(suffix_Command(cmd, "value"));
        urlEncode_String(value);
        iString *url = collect_String(copy_String(d->mod.url));
        const size_t qPos = indexOfCStr_String(url, "?");
        if (qPos != iInvalidPos) {
            remove_Block(&url->chars, qPos, iInvalidSize);
        }
        appendCStr_String(url, "?");
        append_String(url, value);
        postCommandf_App("open url:%s", cstr_String(url));
        return iTrue;
    }
    else if (equal_Command(cmd, "valueinput.cancelled") &&
             equal_Rangecc(range_Command(cmd, "id"), "document.input.submit") && document_App() == d) {
        postCommand_App("navigate.back");
        return iTrue;
    }
    else if (equalWidget_Command(cmd, w, "document.request.updated") &&
             pointerLabel_Command(cmd, "request") == d->request) {
        set_Block(&d->sourceContent, body_GmRequest(d->request));
        if (document_App() == d) {
            updateFetchProgress_DocumentWidget_(d);
        }
        checkResponse_DocumentWidget_(d);
        set_Atomic(&d->isRequestUpdated, iFalse); /* ready to be notified again */
        return iFalse;
    }
    else if (equalWidget_Command(cmd, w, "document.request.finished") &&
             pointerLabel_Command(cmd, "request") == d->request) {
        set_Block(&d->sourceContent, body_GmRequest(d->request));
        updateFetchProgress_DocumentWidget_(d);
        checkResponse_DocumentWidget_(d);
        init_Anim(&d->scrollY, d->initNormScrollY * size_GmDocument(d->doc).y);
        d->state = ready_RequestState;
        /* The response may be cached. */ {
            if (!equal_Rangecc(urlScheme_String(d->mod.url), "about") &&
                startsWithCase_String(meta_GmRequest(d->request), "text/")) {
                setCachedResponse_History(d->mod.history, response_GmRequest(d->request));
            }
        }
        iReleasePtr(&d->request);
        updateVisible_DocumentWidget_(d);
        updateSideIconBuf_DocumentWidget_(d);
        updateOutline_DocumentWidget_(d);
        postCommandf_App("document.changed url:%s", cstr_String(d->mod.url));
        return iFalse;
    }
    else if (equal_Command(cmd, "document.request.timeout") &&
             pointerLabel_Command(cmd, "request") == d->request) {
        cancel_GmRequest(d->request);
        return iFalse;
    }
    /*
    else if (equal_Command(cmd, "document.request.cancelled") && document_Command(cmd) == d) {
        postCommand_App("navigate.back");
        return iFalse;
    }
    */
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
        updatePlayers_DocumentWidget_(d);
        return iFalse;
    }
    else if (equal_Command(cmd, "document.stop") && document_App() == d) {
        if (d->request) {
            postCommandf_App(
                "document.request.cancelled doc:%p url:%s", d, cstr_String(d->mod.url));
            iReleasePtr(&d->request);
            if (d->state != ready_RequestState) {
                d->state = ready_RequestState;
                postCommand_App("navigate.back");
            }
            updateFetchProgress_DocumentWidget_(d);
            return iTrue;
        }
    }
    else if (equal_Command(cmd, "document.save") && document_App() == d) {
        if (d->request) {
            makeMessage_Widget(uiTextCaution_ColorEscape "PAGE INCOMPLETE",
                               "The page contents are still being downloaded.");
        }
        else if (!isEmpty_Block(&d->sourceContent)) {
            /* Figure out a file name from the URL. */
            /* TODO: Make this a utility function. */
            iUrl parts;
            init_Url(&parts, d->mod.url);
            while (startsWith_Rangecc(parts.path, "/")) {
                parts.path.start++;
            }
            while (endsWith_Rangecc(parts.path, "/")) {
                parts.path.end--;
            }
            iString *name = collectNewCStr_String("pagecontent");
            if (isEmpty_Range(&parts.path)) {
                if (!isEmpty_Range(&parts.host)) {
                    setRange_String(name, parts.host);
                    replace_Block(&name->chars, '.', '_');
                }
            }
            else {
                iRangecc fn = { parts.path.start + lastIndexOfCStr_Rangecc(parts.path, "/") + 1,
                                parts.path.end };
                if (!isEmpty_Range(&fn)) {
                    setRange_String(name, fn);
                }
            }
            if (startsWith_String(name, "~")) {
                /* This would be interpreted as a reference to a home directory. */
                remove_Block(&name->chars, 0, 1);
            }
            iString *savePath = concat_Path(downloadDir_App(), name);
            if (lastIndexOfCStr_String(savePath, ".") == iInvalidPos) {
                /* No extension specified in URL. */
                if (startsWith_String(&d->sourceMime, "text/gemini")) {
                    appendCStr_String(savePath, ".gmi");
                }
                else if (startsWith_String(&d->sourceMime, "text/")) {
                    appendCStr_String(savePath, ".txt");
                }
                else if (startsWith_String(&d->sourceMime, "image/")) {
                    appendCStr_String(savePath, cstr_String(&d->sourceMime) + 6);
                }
            }
            if (fileExists_FileInfo(savePath)) {
                /* Make it unique. */
                iDate now;
                initCurrent_Date(&now);
                size_t insPos = lastIndexOfCStr_String(savePath, ".");
                if (insPos == iInvalidPos) {
                    insPos = size_String(savePath);
                }
                const iString *date = collect_String(format_Date(&now, "_%Y-%m-%d_%H%M%S"));
                insertData_Block(&savePath->chars, insPos, cstr_String(date), size_String(date));
            }
            /* Write the file. */ {
                iFile *f = new_File(savePath);
                if (open_File(f, writeOnly_FileMode)) {
                    write_File(f, &d->sourceContent);
                    const size_t size   = size_Block(&d->sourceContent);
                    const iBool  isMega = size >= 1000000;
                    makeMessage_Widget(uiHeading_ColorEscape "PAGE SAVED",
                                       format_CStr("%s\nSize: %.3f %s", cstr_String(path_File(f)),
                                                   isMega ? size / 1.0e6f : (size / 1.0e3f),
                                                   isMega ? "MB" : "KB"));
                }
                else {
                    makeMessage_Widget(uiTextCaution_ColorEscape "ERROR SAVING PAGE",
                                       strerror(errno));
                }
                iRelease(f);
            }
            delete_String(savePath);
        }
        return iTrue;
    }
    else if (equal_Command(cmd, "document.reload") && document_App() == d) {
        d->initNormScrollY = normScrollPos_DocumentWidget_(d);
        fetch_DocumentWidget_(d);
        return iTrue;
    }
    else if (equal_Command(cmd, "navigate.back") && document_App() == d) {
        goBack_History(d->mod.history);
        return iTrue;
    }
    else if (equal_Command(cmd, "navigate.forward") && document_App() == d) {
        goForward_History(d->mod.history);
        return iTrue;
    }
    else if (equalWidget_Command(cmd, w, "scroll.moved")) {
        init_Anim(&d->scrollY, arg_Command(cmd));
        updateVisible_DocumentWidget_(d);
        return iTrue;
    }
    else if (equalWidget_Command(cmd, w, "scroll.page")) {
        if (argLabel_Command(cmd, "repeat")) {
            /* TODO: Adjust scroll animation to be linear during repeated scroll? */
        }
        smoothScroll_DocumentWidget_(d,
                                     arg_Command(cmd) *
                                         (0.5f * height_Rect(documentBounds_DocumentWidget_(d)) -
                                          0 * lineHeight_Text(paragraph_FontId)),
                                     smoothDuration_DocumentWidget_);
        return iTrue;
    }
    else if (equal_Command(cmd, "document.goto") && document_App() == d) {
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
    return iFalse;
}

static int outlineHeight_DocumentWidget_(const iDocumentWidget *d) {
    if (isEmpty_Array(&d->outline)) return 0;
    return bottom_Rect(((const iOutlineItem *) constBack_Array(&d->outline))->rect);
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

static iRect playerRect_DocumentWidget_(const iDocumentWidget *d, const iGmRun *run) {
    const iRect docBounds = documentBounds_DocumentWidget_(d);
    return moved_Rect(run->bounds, addY_I2(topLeft_Rect(docBounds), -value_Anim(&d->scrollY)));
}

static void setGrabbedPlayer_DocumentWidget_(iDocumentWidget *d, const iGmRun *run) {
    if (run) {
        iPlayer *plr = audioPlayer_Media(media_GmDocument(d->doc), run->audioId);
        setFlags_Player(plr, volumeGrabbed_PlayerFlag, iTrue);
        d->grabbedStartVolume = volume_Player(plr);
        d->grabbedPlayer      = run;
        refresh_Widget(d);
    }
    else if (d->grabbedPlayer) {
        setFlags_Player(
            audioPlayer_Media(media_GmDocument(d->doc), d->grabbedPlayer->audioId),
            volumeGrabbed_PlayerFlag,
            iFalse);
        d->grabbedPlayer = NULL;
        refresh_Widget(d);
    }
    else {
        iAssert(iFalse);
    }
}

static iBool processPlayerEvents_DocumentWidget_(iDocumentWidget *d, const SDL_Event *ev) {
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
    iConstForEach(PtrArray, i, &d->visiblePlayers) {
        const iGmRun *run  = i.ptr;
        const iRect   rect = playerRect_DocumentWidget_(d, run);
        iPlayer *     plr  = audioPlayer_Media(media_GmDocument(d->doc), run->audioId);
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
                animatePlayers_DocumentWidget_(d);
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
                animatePlayers_DocumentWidget_(d);
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
                openMenu_Widget(d->playerMenu,
                                localCoord_Widget(constAs_Widget(d), bottomLeft_Rect(ui.menuRect)));
                return iTrue;
            }
        }
    }
    return iFalse;
}

static iBool processEvent_DocumentWidget_(iDocumentWidget *d, const SDL_Event *ev) {
    iWidget *w = as_Widget(d);
    if (ev->type == SDL_USEREVENT && ev->user.code == command_UserEventCode) {
        if (!handleCommand_DocumentWidget_(d, command_UserEvent(ev))) {
            /* Base class commands. */
            return processEvent_Widget(w, ev);
        }
        return iTrue;
    }
    if (ev->type == SDL_KEYUP) {
        const int key = ev->key.keysym.sym;
        switch (key) {
            case SDLK_LALT:
            case SDLK_RALT:
                if (document_App() == d) {
                    iChangeFlags(d->flags, showLinkNumbers_DocumentWidgetFlag, iFalse);
                    invalidate_DocumentWidget_(d);
                    refresh_Widget(w);
                }
                break;
            case SDLK_PAGEUP:
            case SDLK_PAGEDOWN:
            case SDLK_SPACE:
            case SDLK_UP:
            case SDLK_DOWN:
//                d->smoothContinue = iFalse;
                break;
        }
    }
    if (ev->type == SDL_KEYDOWN) {
        const int mods = keyMods_Sym(ev->key.keysym.mod);
        const int key  = ev->key.keysym.sym;
        if ((d->flags & showLinkNumbers_DocumentWidgetFlag) &&
            ((key >= '1' && key <= '9') || (key >= 'a' && key <= 'z'))) {
            const size_t ord = isdigit(key) ? key - SDLK_1 : (key - 'a' + 9);
            iConstForEach(PtrArray, i, &d->visibleLinks) {
                const iGmRun *run = i.ptr;
                if (run->flags & decoration_GmRunFlag &&
                    visibleLinkOrdinal_DocumentWidget_(d, run->linkId) == ord) {
                    postCommandf_App("open newtab:%d url:%s",
                                     (SDL_GetModState() & KMOD_PRIMARY) != 0,
                                     cstr_String(linkUrl_GmDocument(d->doc, run->linkId)));
                    return iTrue;
                }
            }
        }
        switch (key) {
            case SDLK_LALT:
            case SDLK_RALT:
                if (document_App() == d) {
                    iChangeFlags(d->flags, showLinkNumbers_DocumentWidgetFlag, iTrue);
                    invalidate_DocumentWidget_(d);
                    refresh_Widget(w);
                }
                break;
            case SDLK_HOME:
                init_Anim(&d->scrollY, 0);
                invalidate_VisBuf(d->visBuf);
                scroll_DocumentWidget_(d, 0);
                updateVisible_DocumentWidget_(d);
                refresh_Widget(w);
                return iTrue;
            case SDLK_END:
                init_Anim(&d->scrollY, scrollMax_DocumentWidget_(d));
                invalidate_VisBuf(d->visBuf);
                scroll_DocumentWidget_(d, 0);
                updateVisible_DocumentWidget_(d);
                refresh_Widget(w);
                return iTrue;
            case SDLK_UP:
            case SDLK_DOWN:
                if (mods == 0) {
                    if (ev->key.repeat) {
//                        if (!d->smoothContinue) {
//                            d->smoothContinue = iTrue;
//                        }
//                        else return iTrue;
                    }
                    smoothScroll_DocumentWidget_(d,
                                                 3 * lineHeight_Text(paragraph_FontId) *
                                                     (key == SDLK_UP ? -1 : 1),
                                                 /*gap_Text * */smoothDuration_DocumentWidget_);
                    return iTrue;
                }
                break;
            case SDLK_PAGEUP:
            case SDLK_PAGEDOWN:
            case SDLK_SPACE:
                postCommand_Widget(
                    w,
                    "scroll.page arg:%d repeat:%d",
                    (key == SDLK_SPACE && mods & KMOD_SHIFT) || key == SDLK_PAGEUP ? -1 : +1,
                    ev->key.repeat != 0);
                return iTrue;
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
        }
    }
    else if (ev->type == SDL_MOUSEWHEEL && isHover_Widget(w)) {
        float acceleration = 1.0f;
        if (prefs_App()->hoverOutline &&
            contains_Widget(constAs_Widget(d->scroll), mouseCoord_Window(get_Window()))) {
            const int outHeight = outlineHeight_DocumentWidget_(d);
            if (outHeight > height_Rect(bounds_Widget(w))) {
                acceleration = (float) size_GmDocument(d->doc).y / (float) outHeight;
            }
        }
#if defined (iPlatformApple)
        /* Momentum scrolling. */
        stop_Anim(&d->scrollY);
        scroll_DocumentWidget_(d, -ev->wheel.y * get_Window()->pixelRatio * acceleration);
#else
        if (keyMods_Sym(SDL_GetModState()) == KMOD_PRIMARY) {
            postCommandf_App("zoom.delta arg:%d", ev->wheel.y > 0 ? 10 : -10);
            return iTrue;
        }
        smoothScroll_DocumentWidget_(
            d,
            -3 * ev->wheel.y * lineHeight_Text(paragraph_FontId) * acceleration,
            smoothDuration_DocumentWidget_ *
                (!isFinished_Anim(&d->scrollY) && pos_Anim(&d->scrollY) < 0.25f ? 0.5f : 1.0f));
            /* accelerated speed for repeated wheelings */
#endif
        iChangeFlags(d->flags, noHoverWhileScrolling_DocumentWidgetFlag, iTrue);
        return iTrue;
    }
    else if (ev->type == SDL_MOUSEMOTION) {
        iChangeFlags(d->flags, noHoverWhileScrolling_DocumentWidgetFlag, iFalse);
        if (isVisible_Widget(d->menu)) {
            setCursor_Window(get_Window(), SDL_SYSTEM_CURSOR_ARROW);
        }
        else {
            updateHover_DocumentWidget_(d, init_I2(ev->motion.x, ev->motion.y));
        }
        updateOutlineOpacity_DocumentWidget_(d);
    }
    if (ev->type == SDL_MOUSEBUTTONDOWN) {
        if (ev->button.button == SDL_BUTTON_X1) {
            postCommand_App("navigate.back");
            return iTrue;
        }
        if (ev->button.button == SDL_BUTTON_X2) {
            postCommand_App("navigate.forward");
            return iTrue;
        }
        if (ev->button.button == SDL_BUTTON_MIDDLE && d->hoverLink) {
            postCommandf_App("open newtab:1 url:%s",
                             cstr_String(linkUrl_GmDocument(d->doc, d->hoverLink->linkId)));
            return iTrue;
        }
        if (ev->button.button == SDL_BUTTON_RIGHT) {
            if (!d->menu || !isVisible_Widget(d->menu)) {
                d->contextLink = d->hoverLink;
                if (d->menu) {
                    destroy_Widget(d->menu);
                }
                iArray items;
                init_Array(&items, sizeof(iMenuItem));
                if (d->contextLink) {
                    const iString *linkUrl = linkUrl_GmDocument(d->doc, d->contextLink->linkId);
                    pushBackN_Array(
                        &items,
                        (iMenuItem[]){
                            { "Open Link in New Tab",
                              0,
                              0,
                              format_CStr("!open newtab:1 url:%s", cstr_String(linkUrl)) },
                            { "Open Link in Background Tab",
                              0,
                              0,
                              format_CStr("!open newtab:2 url:%s", cstr_String(linkUrl)) },
                            { "---", 0, 0, NULL },
                            { "Copy Link", 0, 0, "document.copylink" } },
                        4);
                }
                else {
                    if (!isEmpty_Range(&d->selectMark)) {
                        pushBackN_Array(
                            &items,
                            (iMenuItem[]){ { "Copy", 0, 0, "copy" }, { "---", 0, 0, NULL } },
                            2);
                    }
                    pushBackN_Array(
                        &items,
                        (iMenuItem[]){
                            { "Go Back", navigateBack_KeyShortcut, "navigate.back" },
                            { "Go Forward", navigateForward_KeyShortcut, "navigate.forward" },
                            { "Reload Page", reload_KeyShortcut, "navigate.reload" },
                            { "---", 0, 0, NULL },
                            { "Copy Page URL", 0, 0, "document.copylink" },
                            { "---", 0, 0, NULL } },
                        6);
                    if (isEmpty_Range(&d->selectMark)) {
                        pushBackN_Array(
                            &items,
                            (iMenuItem[]){
                                { "Copy Page Source", 'c', KMOD_PRIMARY, "copy" },
                                { "Save to Downloads", SDLK_s, KMOD_PRIMARY, "document.save" } },
                            2);
                    }
                }
                d->menu = makeMenu_Widget(w, data_Array(&items), size_Array(&items));
                deinit_Array(&items);
            }
            processContextMenuEvent_Widget(d->menu, ev, d->hoverLink = NULL);
        }
    }
    if (processPlayerEvents_DocumentWidget_(d, ev)) {
        return iTrue;
    }
    switch (processEvent_Click(&d->click, ev)) {
        case started_ClickResult:
            iChangeFlags(d->flags, selecting_DocumentWidgetFlag, iFalse);
            return iTrue;
        case drag_ClickResult: {
            if (d->grabbedPlayer) {
                iPlayer *plr =
                    audioPlayer_Media(media_GmDocument(d->doc), d->grabbedPlayer->audioId);
                iPlayerUI ui;
                init_PlayerUI(&ui, plr, playerRect_DocumentWidget_(d, d->grabbedPlayer));
                float off = (float) delta_Click(&d->click).x / (float) width_Rect(ui.volumeSlider);
                setVolume_Player(plr, d->grabbedStartVolume + off);
                refresh_Widget(w);
                return iTrue;
            }
            /* Begin selecting a range of text. */
            if (~d->flags & selecting_DocumentWidgetFlag) {
                setFocus_Widget(NULL); /* TODO: Focus this document? */
                iChangeFlags(d->flags, selecting_DocumentWidgetFlag, iTrue);
                d->selectMark.start = d->selectMark.end =
                    sourceLoc_DocumentWidget_(d, d->click.startPos);
                refresh_Widget(w);
            }
            const char *loc = sourceLoc_DocumentWidget_(d, pos_Click(&d->click));
            if (!d->selectMark.start) {
                d->selectMark.start = d->selectMark.end = loc;
            }
            else if (loc) {
                d->selectMark.end = loc;
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
            if (!isMoved_Click(&d->click)) {
                if (d->hoverLink) {
                    const iGmLinkId linkId = d->hoverLink->linkId;
                    iAssert(linkId);
                    /* Media links are opened inline by default. */
                    if (isMediaLink_GmDocument(d->doc, linkId)) {
                        const int linkFlags = linkFlags_GmDocument(d->doc, linkId);
                        if (linkFlags & content_GmLinkFlag && linkFlags & permanent_GmLinkFlag) {
                            /* We have the content and it cannot be dismissed, so nothing
                               further to do. */
                            return iTrue;
                        }
                        if (!requestMedia_DocumentWidget_(d, linkId)) {                            
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
                                scroll_DocumentWidget_(d, 0);
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
                    else {
                        postCommandf_App("open newtab:%d url:%s",
                                         (SDL_GetModState() & KMOD_PRIMARY) != 0,
                                         cstr_String(absoluteUrl_String(
                                             d->mod.url, linkUrl_GmDocument(d->doc, linkId))));
                    }
                }
                if (d->selectMark.start) {
                    d->selectMark = iNullRange;
                    refresh_Widget(w);
                }
            }
            return iTrue;
        case double_ClickResult:
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
    iInt2 viewPos; /* document area origin */
    iPaint paint;
    iBool inSelectMark;
    iBool inFoundMark;
    iBool showLinkNumbers;
};

static void fillRange_DrawContext_(iDrawContext *d, const iGmRun *run, enum iColorId color,
                                   iRangecc mark, iBool *isInside) {
    if (mark.start > mark.end) {
        /* Selection may be done in either direction. */
        iSwap(const char *, mark.start, mark.end);
    }
    if ((!*isInside && (contains_Range(&run->text, mark.start) || mark.start == run->text.end)) ||
        *isInside) {
        int x = 0;
        if (!*isInside) {
            x = advanceRange_Text(run->font, (iRangecc){ run->text.start, mark.start }).x;
        }
        int w = width_Rect(run->bounds) - x;
        if (contains_Range(&run->text, mark.end) || run->text.end == mark.end) {
            w = advanceRange_Text(run->font,
                                  !*isInside ? mark : (iRangecc){ run->text.start, mark.end }).x;
            *isInside = iFalse;
        }
        else {
            *isInside = iTrue; /* at least until the next run */
        }
        if (w > width_Rect(run->visBounds) - x) {
            w = width_Rect(run->visBounds) - x;
        }
        const iInt2 visPos =
            add_I2(run->bounds.pos, addY_I2(d->viewPos, -value_Anim(&d->widget->scrollY)));
        fillRect_Paint(&d->paint, (iRect){ addX_I2(visPos, x),
                                           init_I2(w, height_Rect(run->bounds)) }, color);
    }
}

static void drawMark_DrawContext_(void *context, const iGmRun *run) {
    iDrawContext *d = context;
    if (!run->imageId) {
        fillRange_DrawContext_(d, run, uiMatching_ColorId, d->widget->foundMark, &d->inFoundMark);
        fillRange_DrawContext_(d, run, uiMarked_ColorId, d->widget->selectMark, &d->inSelectMark);
    }
}

static void drawRun_DrawContext_(void *context, const iGmRun *run) {
    iDrawContext *d      = context;
    const iInt2   origin = d->viewPos;
    if (run->imageId) {
        SDL_Texture *tex = imageTexture_Media(media_GmDocument(d->widget->doc), run->imageId);
        if (tex) {
            const iRect dst = moved_Rect(run->visBounds, origin);
            fillRect_Paint(&d->paint, dst, tmBackground_ColorId); /* in case the image has alpha */
            SDL_RenderCopy(d->paint.dst->render, tex, NULL,
                           &(SDL_Rect){ dst.pos.x, dst.pos.y, dst.size.x, dst.size.y });
        }
        return;
    }
    else if (run->audioId) {
        /* Audio player UI is drawn afterwards as a dynamic overlay. */
        return;
    }
    enum iColorId      fg  = run->color;
    const iGmDocument *doc = d->widget->doc;
    const iBool        isHover =
        (run->linkId && d->widget->hoverLink && run->linkId == d->widget->hoverLink->linkId &&
         ~run->flags & decoration_GmRunFlag);
    const iInt2 visPos = add_I2(run->visBounds.pos, origin);
    fillRect_Paint(&d->paint, (iRect){ visPos, run->visBounds.size }, tmBackground_ColorId);
    if (run->linkId && ~run->flags & decoration_GmRunFlag) {
        fg = linkColor_GmDocument(doc, run->linkId, isHover ? textHover_GmLinkPart : text_GmLinkPart);
        if (linkFlags_GmDocument(doc, run->linkId) & content_GmLinkFlag) {
            fg = linkColor_GmDocument(doc, run->linkId, textHover_GmLinkPart); /* link is inactive */
        }
    }
    if (run->flags & siteBanner_GmRunFlag) {
        /* Draw the site banner. */
        fillRect_Paint(
            &d->paint,
            initCorners_Rect(topLeft_Rect(d->widgetBounds),
                             init_I2(right_Rect(bounds_Widget(constAs_Widget(d->widget))),
                                     visPos.y + height_Rect(run->visBounds))),
            tmBannerBackground_ColorId);
        const iChar icon = siteIcon_GmDocument(doc);
        iString bannerText;
        init_String(&bannerText);
        iInt2 bpos = add_I2(visPos, init_I2(0, lineHeight_Text(banner_FontId) / 2));
        if (icon) {
//            appendChar_String(&bannerText, 0x2b24); // icon);
//            const iRect iconRect = visualBounds_Text(hugeBold_FontId, range_String(&bannerText));
//            drawRange_Text(hugeBold_FontId, /*run->font,*/
//                           addY_I2(bpos, -mid_Rect(iconRect).y + lineHeight_Text(run->font) / 2),
//                           tmBannerIcon_ColorId,
//                           range_String(&bannerText));
//            clear_String(&bannerText);
            appendChar_String(&bannerText, icon);
            const iRect iconRect = visualBounds_Text(run->font, range_String(&bannerText));
            drawRange_Text(
                run->font,
                addY_I2(bpos, -mid_Rect(iconRect).y + lineHeight_Text(run->font) / 2),
                tmBannerIcon_ColorId,
                range_String(&bannerText));
            bpos.x += right_Rect(iconRect) + 3 * gap_Text;
        }
        drawRange_Text(run->font,
                       bpos,
                       tmBannerTitle_ColorId,
                       bannerText_DocumentWidget_(d->widget));
//                       isEmpty_String(d->widget->titleUser) ? run->text
//                                                            : range_String(d->widget->titleUser));
        deinit_String(&bannerText);
    }
    else {
        if (d->showLinkNumbers && run->linkId && run->flags & decoration_GmRunFlag) {
            const size_t ord = visibleLinkOrdinal_DocumentWidget_(d->widget, run->linkId);
            if (ord < 9 + 26) {
                const iChar ordChar = ord < 9 ? 0x278a + ord : (0x24b6 + ord - 9);
                drawString_Text(run->font,
                                init_I2(d->viewPos.x - gap_UI / 3, visPos.y),
                                fg,
                                collect_String(newUnicodeN_String(&ordChar, 1)));
                goto runDrawn;
            }
        }
        if (run->flags & quoteBorder_GmRunFlag) {
            drawVLine_Paint(&d->paint,
                            addX_I2(visPos, -gap_Text * 5 / 2),
                            height_Rect(run->visBounds),
                            tmQuoteIcon_ColorId);
        }
        drawRange_Text(run->font, visPos, fg, run->text);
//        printf("{%s}\n", cstr_Rangecc(run->text));
    runDrawn:;
    }
    /* Presentation of links. */
    if (run->linkId && ~run->flags & decoration_GmRunFlag) {
        const int metaFont = paragraph_FontId;
        /* TODO: Show status of an ongoing media request. */
        const int flags = linkFlags_GmDocument(doc, run->linkId);
        const iRect linkRect = moved_Rect(run->visBounds, origin);
        iMediaRequest *mr = NULL;
        /* Show metadata about inline content. */
        if (flags & content_GmLinkFlag && run->flags & endOfLine_GmRunFlag) {
            fg = linkColor_GmDocument(doc, run->linkId, textHover_GmLinkPart);
            iString text;
            init_String(&text);
            iMediaId imageId = linkImage_GmDocument(doc, run->linkId);
            iMediaId audioId = !imageId ? linkAudio_GmDocument(doc, run->linkId) : 0;
            iAssert(imageId || audioId);
            if (imageId) {
                iAssert(!isEmpty_Rect(run->bounds));
                iGmImageInfo info;
                imageInfo_Media(constMedia_GmDocument(doc), imageId, &info);
                format_String(&text, "%s \u2014 %d x %d \u2014 %.1fMB",
                              info.mime, info.size.x, info.size.y, info.numBytes / 1.0e6f);
            }
            else if (audioId) {
                iGmAudioInfo info;
                audioInfo_Media(constMedia_GmDocument(doc), audioId, &info);
                format_String(&text, "%s", info.mime);
            }
            if (findMediaRequest_DocumentWidget_(d->widget, run->linkId)) {
                appendFormat_String(
                    &text, "  %s\u2a2f", isHover ? escape_Color(tmLinkText_ColorId) : "");
            }
            const iInt2 size = measureRange_Text(metaFont, range_String(&text));
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
                          " \u2014 Fetching\u2026 (%.1f MB)",
                          (float) size_Block(body_GmRequest(mr->req)) / 1.0e6f);
            }
        }
        else if (isHover) {
            const iGmLinkId linkId = d->widget->hoverLink->linkId;
            const iString * url    = linkUrl_GmDocument(doc, linkId);
            const int       flags  = linkFlags_GmDocument(doc, linkId);
            iUrl parts;
            init_Url(&parts, url);
            fg                    = linkColor_GmDocument(doc, linkId, textHover_GmLinkPart);
            const iBool showHost  = (flags & humanReadable_GmLinkFlag &&
                                    (!isEmpty_Range(&parts.host) || flags & mailto_GmLinkFlag));
            const iBool showImage = (flags & imageFileExtension_GmLinkFlag) != 0;
            const iBool showAudio = (flags & audioFileExtension_GmLinkFlag) != 0;
            iString str;
            init_String(&str);
            /* Show scheme and host. */
            if (run->flags & endOfLine_GmRunFlag &&
                (flags & (imageFileExtension_GmLinkFlag | audioFileExtension_GmLinkFlag) ||
                 showHost)) {
                format_String(&str,
                              " \u2014%s%s%s\r%c%s",
                              showHost ? " " : "",
                              showHost ? (flags & mailto_GmLinkFlag
                                              ? cstr_String(url)
                                              : ~flags & gemini_GmLinkFlag
                                                    ? format_CStr("%s://%s",
                                                                  cstr_Rangecc(parts.scheme),
                                                                  cstr_Rangecc(parts.host))
                                                    : cstr_Rangecc(parts.host))
                                       : "",
                              showHost && (showImage || showAudio) ? " \u2014" : "",
                              showImage || showAudio
                                  ? asciiBase_ColorEscape + fg
                                  : (asciiBase_ColorEscape +
                                     linkColor_GmDocument(doc, run->linkId, domain_GmLinkPart)),
                              showImage ? " View Image \U0001f5bc"
                                        : showAudio ? " Play Audio \U0001f3b5" : "");
            }
            if (run->flags & endOfLine_GmRunFlag && flags & visited_GmLinkFlag) {
                iDate date;
                init_Date(&date, linkTime_GmDocument(doc, run->linkId));
                appendFormat_String(&str,
                                    " \u2014 %s%s",
                                    escape_Color(linkColor_GmDocument(doc, run->linkId,
                                                                      visited_GmLinkPart)),
                                    cstr_String(collect_String(format_Date(&date, "%b %d"))));
            }
            if (!isEmpty_String(&str)) {
                const iInt2 textSize = measure_Text(metaFont, cstr_String(&str));
                int tx = topRight_Rect(linkRect).x;
                const char *msg = cstr_String(&str);
                if (tx + textSize.x > right_Rect(d->widgetBounds)) {
                    tx = right_Rect(d->widgetBounds) - textSize.x;
                    fillRect_Paint(&d->paint, (iRect){ init_I2(tx, top_Rect(linkRect)), textSize },
                                   uiBackground_ColorId);
                    msg += 4; /* skip the space and dash */
                    tx += measure_Text(metaFont, " \u2014").x / 2;
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

static void updateSideIconBuf_DocumentWidget_(iDocumentWidget *d) {
    if (d->sideIconBuf) {
        SDL_DestroyTexture(d->sideIconBuf);
        d->sideIconBuf = NULL;
    }
    const iGmRun *banner = siteBanner_GmDocument(d->doc);
    if (!banner) {
        return;
    }
    const int      margin           = gap_UI * d->pageMargin;
    const int      minBannerSize    = lineHeight_Text(banner_FontId) * 2;
    const iChar    icon             = siteIcon_GmDocument(d->doc);
    const int      avail            = sideElementAvailWidth_DocumentWidget_(d) - margin;
    iBool          isHeadingVisible = isSideHeadingVisible_DocumentWidget_(d);
    /* Determine the required size. */
    iInt2 bufSize = init1_I2(minBannerSize);
    if (isHeadingVisible) {
        const iInt2 headingSize = advanceWrapRange_Text(heading3_FontId, avail,
                                                        currentHeading_DocumentWidget_(d));
        if (headingSize.x > 0) {
            bufSize.y += gap_Text + headingSize.y;
            bufSize.x = iMax(bufSize.x, headingSize.x);
        }
        else {
            isHeadingVisible = iFalse;
        }
    }
    d->sideIconBuf = SDL_CreateTexture(renderer_Window(get_Window()),
                                       SDL_PIXELFORMAT_RGBA4444,
                                       SDL_TEXTUREACCESS_STATIC | SDL_TEXTUREACCESS_TARGET,
                                       bufSize.x, bufSize.y);
    iPaint p;
    init_Paint(&p);
    beginTarget_Paint(&p, d->sideIconBuf);
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
    SDL_SetTextureBlendMode(d->sideIconBuf, SDL_BLENDMODE_BLEND);
}

static void drawSideElements_DocumentWidget_(const iDocumentWidget *d) {
    const iWidget *w         = constAs_Widget(d);
    const iRect    bounds    = bounds_Widget(w);
    const iRect    docBounds = documentBounds_DocumentWidget_(d);
    const int      margin    = gap_UI * d->pageMargin;
    float          opacity   = value_Anim(&d->sideOpacity);
    const int      avail     = left_Rect(docBounds) - left_Rect(bounds) - 2 * margin;
    iPaint      p;
    init_Paint(&p);
    setClip_Paint(&p, bounds);
    /* Side icon and current heading. */
    if (prefs_App()->sideIcon && opacity > 0 && d->sideIconBuf) {
        const iInt2 texSize = size_SDLTexture(d->sideIconBuf);
        if (avail > texSize.x) {
            const int minBannerSize = lineHeight_Text(banner_FontId) * 2;
            iInt2 pos = addY_I2(add_I2(topLeft_Rect(bounds), init_I2(margin, 0)),
                                height_Rect(bounds) / 2 - minBannerSize / 2 -
                                    (texSize.y > minBannerSize
                                         ? (gap_Text + lineHeight_Text(heading3_FontId)) / 2
                                         : 0));
            SDL_SetTextureAlphaMod(d->sideIconBuf, 255 * opacity);
            SDL_RenderCopy(renderer_Window(get_Window()),
                           d->sideIconBuf, NULL,
                           &(SDL_Rect){ pos.x, pos.y, texSize.x, texSize.y });
        }
    }
    /* Reception timestamp. */
    if (d->timestampBuf && d->timestampBuf->size.x <= avail) {
        draw_TextBuf(
            d->timestampBuf,
            add_I2(
                bottomLeft_Rect(bounds),
                init_I2(margin,
                        -margin + -d->timestampBuf->size.y +
                            iMax(0, scrollMax_DocumentWidget_(d) - value_Anim(&d->scrollY)))),
            tmQuoteIcon_ColorId);
    }
    /* Outline on the right side. */
    const float outlineOpacity = value_Anim(&d->outlineOpacity);
    if (prefs_App()->hoverOutline && !isEmpty_Array(&d->outline) && outlineOpacity > 0.0f) {
        /* TODO: This is very slow to draw; should be buffered appropriately. */
        const int innerWidth   = outlineWidth_DocumentWidget_(d);
        const int outWidth     = innerWidth + 2 * outlinePadding_DocumentWidget_ * gap_UI;
        const int topMargin    = 0;
        const int bottomMargin = 3 * gap_UI;
        const int scrollMax    = scrollMax_DocumentWidget_(d);
        const int outHeight    = outlineHeight_DocumentWidget_(d);
        const int oversize     = outHeight - height_Rect(bounds) + topMargin + bottomMargin;
        const int scroll       = (oversize > 0 && scrollMax > 0
                                ? oversize * value_Anim(&d->scrollY) / scrollMax_DocumentWidget_(d)
                                : 0);
        iInt2 pos =
            add_I2(topRight_Rect(bounds), init_I2(-outWidth - width_Widget(d->scroll), topMargin));
        /* Center short outlines vertically. */
        if (oversize < 0) {
            pos.y -= oversize / 2;
        }
        pos.y -= scroll;
        setOpacity_Text(outlineOpacity);
        SDL_SetRenderDrawBlendMode(renderer_Window(get_Window()), SDL_BLENDMODE_BLEND);
        p.alpha = outlineOpacity * 255;
        iRect outlineFrame = {
            addY_I2(pos, -outlinePadding_DocumentWidget_ * gap_UI / 2),
            init_I2(outWidth, outHeight + outlinePadding_DocumentWidget_ * gap_UI * 1.5f)
        };
        fillRect_Paint(&p, outlineFrame, tmBannerBackground_ColorId);
        drawSideRect_(&p, outlineFrame);
        iBool wasAbove = iTrue;
        iConstForEach(Array, i, &d->outline) {
            const iOutlineItem *item = i.value;
            iInt2 visPos = addX_I2(add_I2(pos, item->rect.pos), outlinePadding_DocumentWidget_ * gap_UI);
            const iBool isVisible = d->lastVisibleRun && d->lastVisibleRun->text.start >= item->text.start;
            const int fg = index_ArrayConstIterator(&i) == 0 || isVisible ? tmOutlineHeadingAbove_ColorId
                                                                          : tmOutlineHeadingBelow_ColorId;
            if (fg == tmOutlineHeadingBelow_ColorId) {
                if (wasAbove) {
                    drawHLine_Paint(&p,
                                    init_I2(left_Rect(outlineFrame), visPos.y - 1),
                                    width_Rect(outlineFrame),
                                    tmOutlineHeadingBelow_ColorId);
                    wasAbove = iFalse;
                }
            }
            drawWrapRange_Text(
                item->font, visPos, innerWidth - left_Rect(item->rect), fg, item->text);
            if (left_Rect(item->rect) > 0) {
                drawRange_Text(item->font, addX_I2(visPos, -2.75f * gap_UI), fg, range_CStr("\u2022"));
            }
        }
        setOpacity_Text(1.0f);
        SDL_SetRenderDrawBlendMode(renderer_Window(get_Window()), SDL_BLENDMODE_NONE);
    }
    unsetClip_Paint(&p);
    }

static void drawPlayers_DocumentWidget_(const iDocumentWidget *d, iPaint *p) {
    iConstForEach(PtrArray, i, &d->visiblePlayers) {
        const iGmRun * run = i.ptr;
        const iPlayer *plr = audioPlayer_Media(media_GmDocument(d->doc), run->audioId);
        const iRect rect   = playerRect_DocumentWidget_(d, run);
        iPlayerUI   ui;
        init_PlayerUI(&ui, plr, rect);
        draw_PlayerUI(&ui, p);
    }
}

static void draw_DocumentWidget_(const iDocumentWidget *d) {
    const iWidget *w        = constAs_Widget(d);
    const iRect    bounds   = bounds_Widget(w);
    iVisBuf *      visBuf   = d->visBuf; /* will be updated now */
    draw_Widget(w);
    allocVisBuffer_DocumentWidget_(d);
    const iRect ctxWidgetBounds = init_Rect(
        0, 0, width_Rect(bounds) - constAs_Widget(d->scroll)->rect.size.x, height_Rect(bounds));
    const iRect  docBounds = documentBounds_DocumentWidget_(d);
    iDrawContext ctx       = {
        .widget          = d,
        .showLinkNumbers = (d->flags & showLinkNumbers_DocumentWidgetFlag) != 0,
    };
    /* Currently visible region. */
    const iRangei vis  = visibleRange_DocumentWidget_(d);
    const iRangei full = { 0, size_GmDocument(d->doc).y };
    reposition_VisBuf(visBuf, vis);
    iRangei invalidRange[3];
    invalidRanges_VisBuf(visBuf, full, invalidRange);
    /* Redraw the invalid ranges. */ {
        iPaint *p = &ctx.paint;
        init_Paint(p);
        iForIndices(i, visBuf->buffers) {
            iVisBufTexture *buf = &visBuf->buffers[i];
            ctx.widgetBounds = moved_Rect(ctxWidgetBounds, init_I2(0, -buf->origin));
            ctx.viewPos      = init_I2(left_Rect(docBounds) - left_Rect(bounds), -buf->origin);
            if (!isEmpty_Rangei(invalidRange[i])) {
                beginTarget_Paint(p, buf->texture);
                if (isEmpty_Rangei(buf->validRange)) {
                    fillRect_Paint(p, (iRect){ zero_I2(), visBuf->texSize }, tmBackground_ColorId);
                }
                render_GmDocument(d->doc, invalidRange[i], drawRun_DrawContext_, &ctx);
            }
            /* Draw any invalidated runs that fall within this buffer. */ {
                const iRangei bufRange = { buf->origin, buf->origin + visBuf->texSize.y };
                /* Clear full-width backgrounds first in case there are any dynamic elements. */ {
                    iConstForEach(PtrSet, r, d->invalidRuns) {
                        const iGmRun *run = *r.value;
                        if (isOverlapping_Rangei(bufRange, ySpan_Rect(run->visBounds))) {
                            beginTarget_Paint(p, buf->texture);
                            fillRect_Paint(&ctx.paint,
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
                        drawRun_DrawContext_(&ctx, run);
                    }
                }
            }
            endTarget_Paint(&ctx.paint);
//            fflush(stdout);
        }
        validate_VisBuf(visBuf);
        clear_PtrSet(d->invalidRuns);
    }
    setClip_Paint(&ctx.paint, bounds);
    const int yTop = docBounds.pos.y - value_Anim(&d->scrollY);
    draw_VisBuf(visBuf, init_I2(bounds.pos.x, yTop));
    /* Text markers. */
    if (!isEmpty_Range(&d->foundMark) || !isEmpty_Range(&d->selectMark)) {
        SDL_SetRenderDrawBlendMode(renderer_Window(get_Window()),
                                   isDark_ColorTheme(colorTheme_App()) ? SDL_BLENDMODE_ADD
                                                                       : SDL_BLENDMODE_BLEND);
        ctx.viewPos = topLeft_Rect(docBounds);
        render_GmDocument(d->doc, vis, drawMark_DrawContext_, &ctx);
        SDL_SetRenderDrawBlendMode(renderer_Window(get_Window()), SDL_BLENDMODE_NONE);
    }
    drawPlayers_DocumentWidget_(d, &ctx.paint);
    unsetClip_Paint(&ctx.paint);
    /* Fill the top and bottom, in case the document is short. */
    if (yTop > top_Rect(bounds)) {
        fillRect_Paint(&ctx.paint,
                       (iRect){ bounds.pos, init_I2(bounds.size.x, yTop - top_Rect(bounds)) },
                       hasSiteBanner_GmDocument(d->doc) ? tmBannerBackground_ColorId
                                                        : tmBackground_ColorId);
    }
    const int yBottom = yTop + size_GmDocument(d->doc).y;
    if (yBottom < bottom_Rect(bounds)) {
        fillRect_Paint(&ctx.paint,
                       init_Rect(bounds.pos.x, yBottom, bounds.size.x, bottom_Rect(bounds) - yBottom),
                       tmBackground_ColorId);
    }
    drawSideElements_DocumentWidget_(d);
    draw_Widget(w);
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
        pushBackCStr_StringArray(title, "Blank Page");
    }
    return collect_String(joinCStr_StringArray(title, " \u2014 "));
}

void serializeState_DocumentWidget(const iDocumentWidget *d, iStream *outs) {
    serialize_Model(&d->mod, outs);
}

void deserializeState_DocumentWidget(iDocumentWidget *d, iStream *ins) {
    deserialize_Model(&d->mod, ins);
    parseUser_DocumentWidget_(d);
    updateFromHistory_DocumentWidget_(d);
}

void setUrlFromCache_DocumentWidget(iDocumentWidget *d, const iString *url, iBool isFromCache) {
    if (cmpStringSc_String(d->mod.url, url, &iCaseInsensitive)) {
        set_String(d->mod.url, url);
        /* See if there a username in the URL. */
        parseUser_DocumentWidget_(d);
        if (!isFromCache || !updateFromHistory_DocumentWidget_(d)) {
            fetch_DocumentWidget_(d);
        }
    }
    else {
        postCommandf_App("document.changed url:%s", cstr_String(d->mod.url));
    }
}

iDocumentWidget *duplicate_DocumentWidget(const iDocumentWidget *orig) {
    iDocumentWidget *d = new_DocumentWidget();
    delete_History(d->mod.history);
    d->initNormScrollY = normScrollPos_DocumentWidget_(d);
    d->mod.history = copy_History(orig->mod.history);
    setUrlFromCache_DocumentWidget(d, orig->mod.url, iTrue);
    return d;
}

void setUrl_DocumentWidget(iDocumentWidget *d, const iString *url) {
    setUrlFromCache_DocumentWidget(d, url, iFalse);
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

void updateSize_DocumentWidget(iDocumentWidget *d) {
    setWidth_GmDocument(
        d->doc, documentWidth_DocumentWidget_(d), forceBreakWidth_DocumentWidget_(d));
    updateSideIconBuf_DocumentWidget_(d);
    updateOutline_DocumentWidget_(d);
    updateVisible_DocumentWidget_(d);
    invalidate_DocumentWidget_(d);
}

iBeginDefineSubclass(DocumentWidget, Widget)
    .processEvent = (iAny *) processEvent_DocumentWidget_,
    .draw         = (iAny *) draw_DocumentWidget_,
iEndDefineSubclass(DocumentWidget)
