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

#include "inlinemedia.h"

#include "../app.h"
#include "../defs.h"
#include "../gmutil.h"
#include "../media/player.h"
#include "documentview.h"
#include "ui/command.h"
#include "ui/documentwidget.h"
#include "ui/mediaui.h"
#include "ui/util.h"
#include "ui/widget.h"
#include "ui/window.h"

#include <lagrange/gmrequest.h>
#include <SDL_timer.h>

iDefineTypeConstruction(InlineMedia)

void init_InlineMedia(iInlineMedia *d) {
    d->owner              = NULL;
    d->media              = new_ObjectList();
    d->lastMediaInterval  = 0;
    d->grabbedPlayer      = NULL;
    d->grabbedStartVolume = 0.0f;
    d->mediaTimer         = 0;
    d->playerMenu         = NULL;
}

void deinit_InlineMedia(iInlineMedia *d) {
    if (d->mediaTimer) {
        SDL_RemoveTimer(d->mediaTimer);
    }
    iRelease(d->media);
}

void setOwner_InlineMedia(iInlineMedia *d, iDocumentWidget *owner) {
    d->owner = owner;
}

static iDocumentView *view_InlineMedia_(const iInlineMedia *d) {
    return view_DocumentWidget(d->owner);
}

static iGmDocument *doc_InlineMedia_(const iInlineMedia *d) {
    return view_InlineMedia_(d)->doc;
}

static iMedia *media_InlineMedia_(const iInlineMedia *d) {
    return media_GmDocument(doc_InlineMedia_(d));
}

void clearRequests_InlineMedia(iInlineMedia *d) {
    clear_ObjectList(d->media);
}

void cancelRequests_InlineMedia(iInlineMedia *d) {
    iForEach(ObjectList, i, d->media) {
        iMediaRequest *mr = i.object;
        cancel_GmRequest(mr->req);
    }
}

void addRequest_InlineMedia(iInlineMedia *d, iMediaRequest *mr) {
    pushBack_ObjectList(d->media, mr);
}

iBool dragGrabbedPlayer_InlineMedia(iInlineMedia *d, int deltaX) {
#if defined (LAGRANGE_ENABLE_AUDIO)
    if (d->grabbedPlayer) {
        iDocumentView *view = view_InlineMedia_(d);
        iPlayer *       plr =
            audioPlayer_Media(media_GmDocument(view->doc), mediaId_GmRun(d->grabbedPlayer));
        iPlayerUI ui;
        init_PlayerUI(&ui, plr, runRect_DocumentView(view, d->grabbedPlayer));
        const float off = (float) deltaX / (float) width_Rect(ui.volumeSlider);
        setVolume_Player(plr, d->grabbedStartVolume + off);
        refresh_Widget(d->owner);
        return iTrue;
    }
#else
    iUnused(d, deltaX);
#endif
    return iFalse;
}

/*----------------------------------------------------------------------------------------------*/

iMediaRequest *findRequest_InlineMedia(const iInlineMedia *d, iGmLinkId linkId) {
    iConstForEach(ObjectList, i, d->media) {
        const iMediaRequest *req = (const iMediaRequest *) i.object;
        if (req->linkId == linkId) {
            return iConstCast(iMediaRequest *, req);
        }
    }
    return NULL;
}

static uint32_t updateInterval_InlineMedia_(const iInlineMedia *d) {
    if (document_App() != d->owner) {
        return 0;
    }
    if (as_MainWindow(window_Widget(d->owner))->isDrawFrozen) {
        return 0;
    }
    static const uint32_t invalidInterval_ = ~0u;
    uint32_t interval = invalidInterval_;
    iConstForEach(PtrArray, i, &view_InlineMedia_(d)->visibleMedia) {
        const iGmRun *run = i.ptr;
        if (run->mediaType == audio_MediaType) {
#if defined (LAGRANGE_ENABLE_AUDIO)
            iPlayer *plr = audioPlayer_Media(media_InlineMedia_(d), mediaId_GmRun(run));
            if (flags_Player(plr) & adjustingVolume_PlayerFlag ||
                (isStarted_Player(plr) && !isComplete_Player(plr))) {
                interval = iMin(interval, 1000 / 15); /* download status animation */
            }
            else if (isStarted_Player(plr) && !isPaused_Player(plr)) {
                interval = iMin(interval, 1000); /* per-second position */
            }
#endif
        }
        else if (run->mediaType == download_MediaType) {
            interval = iMin(interval, 1000);
        }
    }
    /* Keep the timer running for active off-screen players so end-of-playback
       is detected even when the player widget is scrolled out of view. */
    if (interval == invalidInterval_ &&
        numActivePlayers_Media(media_InlineMedia_(d)) > 0) {
        interval = 1000;
    }
    return interval != invalidInterval_ ? interval : 0;
}

static uint32_t postUpdate_InlineMedia_(uint32_t interval, void *context) {
    /* Called in timer thread; don't access the widget. */
    iUnused(context);
    if (!isSuspended_App()) {
        notify_App("media.player.update");
    }
    return interval;
}

void update_InlineMedia(iInlineMedia *d) {
    if (document_App() == d->owner) {
        refresh_Widget(d->owner);
        iConstForEach(PtrArray, i, &view_InlineMedia_(d)->visibleMedia) {
            const iGmRun *run = i.ptr;
            if (run->mediaType == audio_MediaType) {
#if defined (LAGRANGE_ENABLE_AUDIO)
                iPlayer *plr = audioPlayer_Media(media_InlineMedia_(d), mediaId_GmRun(run));
                if (idleTimeMs_Player(plr) > 3000 && ~flags_Player(plr) & volumeGrabbed_PlayerFlag &&
                    flags_Player(plr) & adjustingVolume_PlayerFlag) {
                    setFlags_Player(plr, adjustingVolume_PlayerFlag, iFalse);
                }
#endif
            }
        }
    }
    if (d->mediaTimer && updateInterval_InlineMedia_(d) == 0) {
        SDL_RemoveTimer(d->mediaTimer);
        d->mediaTimer = 0;
    }
}

void animate_InlineMedia(iInlineMedia *d) {
    if (!current_Root() || document_App() != d->owner) {
        if (d->mediaTimer) {
            SDL_RemoveTimer(d->mediaTimer);
            d->mediaTimer = 0;
        }
        return;
    }
    const uint32_t interval = updateInterval_InlineMedia_(d);
    if (interval != d->lastMediaInterval && d->mediaTimer) {
        /* We need to change the interval. */
        SDL_RemoveTimer(d->mediaTimer);
        d->mediaTimer = 0;
    }
    d->lastMediaInterval = interval;
    if (interval && !d->mediaTimer) {
        d->mediaTimer = SDL_AddTimer(interval, postUpdate_InlineMedia_, d);
    }
}

void remove_InlineMedia(iInlineMedia *d, iGmLinkId linkId) {
    iForEach(ObjectList, i, d->media) {
        iMediaRequest *req = (iMediaRequest *) i.object;
        if (req->linkId == linkId) {
            remove_ObjectListIterator(&i);
            break;
        }
    }
}

iBool request_InlineMedia(iInlineMedia *d, iGmLinkId linkId, iBool enableFilters) {
    if (linkFlags_GmDocument(doc_InlineMedia_(d), linkId) & content_GmLinkFlag) {
        return iFalse; /* We have the content, no need to request anything. */
    }
    if (!findRequest_InlineMedia(d, linkId)) {
        const iString *mediaUrl = absoluteUrl_String(
            url_DocumentWidget(d->owner), linkUrl_GmDocument(doc_InlineMedia_(d), linkId));
        pushBack_ObjectList(
            d->media,
            iClob(new_MediaRequest(d->owner,
                                   media_InlineMedia_(d),
                                   linkId,
                                   mediaUrl,
                                   enableFilters,
                                   overrideIdentity_DocumentWidget(d->owner))));
        invalidate_DocumentWidget(d->owner);
        return iTrue;
    }
    return iFalse;
}

static iBool isDownloadRequest_InlineMedia_(const iInlineMedia *d, const iMediaRequest *req) {
    return findMediaForLink_Media(media_InlineMedia_(d), req->linkId, download_MediaType).type != 0;
}

iBool handleCommand_InlineMedia(iInlineMedia *d, const char *cmd) {
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
            if (isDownloadRequest_InlineMedia_(d, req) ||
                startsWith_String(&resp->meta, "audio/") ||
                startsWith_String(&resp->meta, "image/")) {
                /* TODO: Use a helper? This is same as below except for the partialData flag. */
                if (setData_Media(media_InlineMedia_(d),
                                  req->linkId,
                                  &resp->meta,
                                  &resp->body,
                                  partialData_MediaFlag | allowHide_MediaFlag)) {
                    redoLayout_GmDocument(doc_InlineMedia_(d));
                }
                updateVisible_DocumentView(view_InlineMedia_(d));
                invalidate_DocumentWidget(d->owner);
                refresh_Widget(d->owner);
            }
            unlockResponse_GmRequest(req->req);
        }
        /* Update the link's progress. */
        invalidateLink_DocumentView(view_InlineMedia_(d), req->linkId);
        refresh_Widget(d->owner);
        return iTrue;
    }
    else if (equal_Command(cmd, "media.finished")) {
        const enum iGmStatusCode code = status_GmRequest(req->req);
        /* Give the media to the document for presentation. */
        if (isSuccess_GmStatusCode(code)) {
            if (isDownloadRequest_InlineMedia_(d, req) ||
                startsWith_String(meta_GmRequest(req->req), "image/") ||
                startsWith_String(meta_GmRequest(req->req), "audio/")) {
                setData_Media(media_InlineMedia_(d),
                              req->linkId,
                              meta_GmRequest(req->req),
                              body_GmRequest(req->req),
                              allowHide_MediaFlag);
                redoLayout_GmDocument(doc_InlineMedia_(d));
                documentRunsInvalidated_DocumentWidget(d->owner);
                updateVisible_DocumentView(view_InlineMedia_(d));
                invalidate_DocumentWidget(d->owner);
                refresh_Widget(d->owner);
                setRedirectCount_DocumentWidget(d->owner, 0);
            }
        }
        else if (category_GmStatusCode(code) == categoryRedirect_GmStatusCode) {
            const int redirectCount = redirectCount_DocumentWidget(d->owner);
            setRedirectCount_DocumentWidget(d->owner, redirectCount + 1);
            if (redirectCount < 5) {
                /* Redo the request. */
                iString *url = copy_String(meta_GmRequest(req->req));
                resubmitWithUrl_MediaRequest(req, url);
                delete_String(url);
            }
            else {
                const iGmError *err = get_GmError(tooManyRedirects_GmStatusCode);
                makeSimpleMessage_Widget(format_CStr(uiTextCaution_ColorEscape "%s", err->title), err->info);
                remove_InlineMedia(d, req->linkId);
            }
        }
        else {
            const iGmError *err = get_GmError(code);
            makeSimpleMessage_Widget(format_CStr(uiTextCaution_ColorEscape "%s", err->title), err->info);
            remove_InlineMedia(d, req->linkId);
        }
        return iTrue;
    }
    return iFalse;
}

iBool fetchNextUnfetchedImage_InlineMedia(iInlineMedia *d) {
    iConstForEach(PtrArray, i, &view_InlineMedia_(d)->visibleLinks) {
        const iGmRun *run = i.ptr;
        if (run->linkId && run->mediaType == none_MediaType &&
            ~run->flags & decoration_GmRunFlag) {
            const int linkFlags = linkFlags_GmDocument(doc_InlineMedia_(d), run->linkId);
            if (isMediaLink_GmDocument(doc_InlineMedia_(d), run->linkId) &&
                linkFlags & imageFileExtension_GmLinkFlag &&
                ~linkFlags & content_GmLinkFlag && ~linkFlags & permanent_GmLinkFlag ) {
                if (request_InlineMedia(d, run->linkId, iTrue)) {
                    return iTrue;
                }
            }
        }
    }
    return iFalse;
}

void setGrabbedPlayer_InlineMedia(iInlineMedia *d, const iGmRun *run) {
#if defined (LAGRANGE_ENABLE_AUDIO)
    if (run && run->mediaType == audio_MediaType) {
        iPlayer *plr = audioPlayer_Media(media_InlineMedia_(d), mediaId_GmRun(run));
        setFlags_Player(plr, volumeGrabbed_PlayerFlag, iTrue);
        d->grabbedStartVolume = volume_Player(plr);
        d->grabbedPlayer      = run;
        refresh_Widget(d->owner);
    }
    else if (d->grabbedPlayer) {
        setFlags_Player(
            audioPlayer_Media(media_InlineMedia_(d), mediaId_GmRun(d->grabbedPlayer)),
            volumeGrabbed_PlayerFlag,
            iFalse);
        d->grabbedPlayer = NULL;
        refresh_Widget(d->owner);
    }
    else {
        iAssert(iFalse);
    }
#endif
}

iBool processEvent_InlineMedia(iInlineMedia *d, const SDL_Event *ev) {
    if (ev->type != SDL_MOUSEBUTTONDOWN && ev->type != SDL_MOUSEBUTTONUP &&
        ev->type != SDL_MOUSEMOTION) {
        return iFalse;
    }
    if (d->grabbedPlayer) {
        /* Updated in the drag. */
        return iFalse;
    }
    const iInt2 mouse = init_I2(ev->button.x, ev->button.y);
    iConstForEach(PtrArray, i, &view_InlineMedia_(d)->visibleMedia) {
        const iGmRun *run  = i.ptr;
        if (run->mediaType == download_MediaType) {
            iDownloadUI ui;
            init_DownloadUI(&ui, media_InlineMedia_(d), mediaId_GmRun(run).id,
                            runRect_DocumentView(view_InlineMedia_(d), run));
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
        const iRect rect = runRect_DocumentView(view_InlineMedia_(d), run);
        iPlayer *   plr  = audioPlayer_Media(media_InlineMedia_(d), mediaId_GmRun(run));
        if (contains_Rect(rect, mouse)) {
            iPlayerUI ui;
            init_PlayerUI(&ui, plr, rect);
            if (ev->type == SDL_MOUSEBUTTONDOWN && flags_Player(plr) & adjustingVolume_PlayerFlag &&
                contains_Rect(adjusted_Rect(ui.volumeAdjustRect,
                                            zero_I2(),
                                            init_I2(-height_Rect(ui.volumeAdjustRect), 0)),
                              mouse)) {
                setGrabbedPlayer_InlineMedia(d, run);
                processEvent_Click(click_DocumentWidget(d->owner), ev);
                /* The rest is done in the DocumentWidget click responder. */
                refresh_Widget(d->owner);
                return iTrue;
            }
            else if (ev->type == SDL_MOUSEBUTTONDOWN || ev->type == SDL_MOUSEMOTION) {
                refresh_Widget(d->owner);
                return iTrue;
            }
            if (contains_Rect(ui.playPauseRect, mouse)) {
                if (isStarted_Player(plr)) {
                    setPaused_Player(plr, !isPaused_Player(plr));
                }
                else {
                    start_Player(plr);
                }
                animate_InlineMedia(d);
                return iTrue;
            }
            else if (contains_Rect(ui.rewindRect, mouse)) {
                if (isStarted_Player(plr) && time_Player(plr) > 0.5f) {
                    stop_Player(plr);
                    start_Player(plr);
                    setPaused_Player(plr, iTrue);
                }
                refresh_Widget(d->owner);
                return iTrue;
            }
            else if (contains_Rect(ui.volumeRect, mouse)) {
                setFlags_Player(plr,
                                adjustingVolume_PlayerFlag,
                                !(flags_Player(plr) & adjustingVolume_PlayerFlag));
                animate_InlineMedia(d);
                refresh_Widget(d->owner);
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
                    as_Widget(d->owner),
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