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

#include "fetch.h"

#include "../app.h"
#include "../defs.h"
#include "../export.h"
#include "../bookmarks.h"
#include "../feeds.h"
#include "../gempub.h"
#include "../gmutil.h"
#include "../history.h"
#include "banner.h"
#include "documentview.h"
#include "inlinemedia.h"
#include "query.h"
#include "persistentstate.h"
#include "ui/command.h"
#include "ui/documentwidget.h"
#include "ui/indicatorwidget.h"
#include "ui/keys.h"
#include "ui/labelwidget.h"
#include "ui/root.h"
#include "ui/util.h"
#include "ui/widget.h"
#include "ui/window.h"

#include <lagrange/gmrequest.h>
#include <lagrange/gopher.h>
#include <lagrange/sitespec.h>
#include <lagrange/visited.h>
#include <the_Foundation/file.h>
#include <the_Foundation/fileinfo.h>
#include <the_Foundation/path.h>
#include <the_Foundation/regexp.h>
#include <SDL3/SDL_timer.h>

iDefineTypeConstruction(DocumentFetch)

void init_DocumentFetch(iDocumentFetch *d) {
    d->owner               = NULL;
    d->flags               = 0;
    d->state               = blank_RequestState;
    d->request             = NULL;
    d->requestLinkId       = 0;
    d->lastRequestUpdateAt = 0;
    d->redirectCount       = 0;
    d->sourceStatus        = none_GmStatusCode;
    d->certFlags           = 0;
    d->certFingerprint     = new_Block(0);
    d->certFullFingerprint = new_Block(0);
    d->certSubject         = new_String();
    iZap(d->certExpiry);
    init_String(&d->sourceHeader);
    init_String(&d->sourceMime);
    init_Block(&d->sourceContent, 0);
    iZap(d->sourceTime);
    d->sourceGempub        = NULL;
}

void deinit_DocumentFetch(iDocumentFetch *d) {
    iRelease(d->request);
    delete_Gempub(d->sourceGempub);
    deinit_Block(&d->sourceContent);
    deinit_String(&d->sourceMime);
    deinit_String(&d->sourceHeader);
    delete_Block(d->certFullFingerprint);
    delete_Block(d->certFingerprint);
    delete_String(d->certSubject);
}

void setOwner_DocumentFetch(iDocumentFetch *d, iDocumentWidget *owner) {
    d->owner = owner;
}

static iDocumentView *view_DocumentFetch_(const iDocumentFetch *d) {
    return view_DocumentWidget(d->owner);
}

static iGmDocument *doc_DocumentFetch_(const iDocumentFetch *d) {
    return view_DocumentWidget(d->owner)->doc;
}

static iBanner *banner_DocumentFetch_(const iDocumentFetch *d) {
    return banner_DocumentWidget(d->owner);
}

static const iString *url_DocumentFetch_(const iDocumentFetch *d) {
    return state_DocumentWidget(d->owner)->url;
}

/*----------------------------------------------------------------------------------------------*/

void requestUpdated_DocumentFetch(iAnyObject *obj) {
    iDocumentFetch *d = fetch_DocumentWidget(obj);
    uint32_t now = SDL_GetTicks();
    iBool didLockUnlock = iFalse;
#if defined (iPlatformAndroidMobile)
    /* On Android, the SDL main thread may be suspended when the app is backgrounded,
       causing the posting of "document.request.updated" to stall. Streaming audio data
       must be forwarded directly on the network thread to keep the audio buffer filled. */
    if (d->state != ready_RequestState) {
        iGmResponse *resp = lockResponse_GmRequest(d->request);
        if (startsWith_String(&resp->meta, "audio/")) {
            const iGmLinkId imgLinkId = 1; /* navigation audio always uses link ID 1 */
            updateStreamData_Media(media_GmDocument(doc_DocumentFetch_(d)), imgLinkId, &resp->body);
        }
        unlockResponse_GmRequest(d->request);
        didLockUnlock = iTrue;
    }
#endif
    if (now - d->lastRequestUpdateAt > 100) {
        d->lastRequestUpdateAt = now;
        notify_Widget(obj,
                      "document.request.updated doc:%p reqid:%u request:%p",
                      obj,
                      id_GmRequest(d->request),
                      d->request);
    }
    else if (!didLockUnlock) {
        /* This will tell GmRequest to notify us again when new data comes in. */
        lockResponse_GmRequest(d->request);
        unlockResponse_GmRequest(d->request);
    }
}

void requestFinished_DocumentFetch(iAnyObject *obj) {
    iDocumentFetch *d = fetch_DocumentWidget(obj);
    notify_Widget(obj,
                  "document.request.finished doc:%p reqid:%u request:%p",
                  obj,
                  id_GmRequest(d->request),
                  d->request);
}

void updateProgress_DocumentFetch(const iDocumentFetch *d) {
    iLabelWidget *prog   = findChild_Widget(root_Widget(as_Widget(d->owner)), "document.progress");
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

void postProcessContent_DocumentFetch(iDocumentFetch *d, iBool isCached) {
    iWidget *w = as_Widget(d->owner);
    /* Embedded images in data links can be shown immediately as they are already fetched
       data that is part of the document. */
    if (prefs_App()->openDataUrlImagesOnLoad) {
        iGmDocument *doc = doc_DocumentFetch_(d);
        for (size_t linkId = 1; ; linkId++) {
            const int      linkFlags = linkFlags_GmDocument(doc, linkId);
            const iString *linkUrl   = linkUrl_GmDocument(doc, linkId);
            if (!linkUrl) break;
            if (scheme_GmLinkFlag(linkFlags) == data_GmLinkScheme &&
                (linkFlags & imageFileExtension_GmLinkFlag)) {
                request_InlineMedia(media_DocumentWidget(d->owner), linkId, 0);
            }
        }
    }
    showForPromptUrls_Query(query_DocumentWidget(d->owner));
    /* Gempub page behavior and footer actions. */ {
        delete_Gempub(d->sourceGempub);
        d->sourceGempub = NULL;
        iBool isInsideArchive = iFalse;
        d->sourceGempub = openForContent_Gempub(&d->sourceContent, &d->sourceMime, url_DocumentFetch_(d));
        if (!d->sourceGempub) {
            d->sourceGempub = openForLocalUrl_Gempub(url_DocumentFetch_(d), &isInsideArchive);
        }
        if (d->sourceGempub && !isInsideArchive) {
            setSource_DocumentWidget(d->owner, collect_String(coverPageSource_Gempub(d->sourceGempub)));
            setCStr_String(&d->sourceMime, mimeType_Gempub);
        }
        if (d->sourceGempub) {
            if (equal_String(url_DocumentFetch_(d), coverPageUrl_Gempub(d->sourceGempub))) {
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
                    makeFooterButtons_DocumentWidget(d->owner, constData_Array(items), size_Array(items));
                }
                else {
                    makeFooterButtons_DocumentWidget(
                        d->owner,
                        (iMenuItem[]){ { book_Icon " ${menu.save.downloads.open}",
                                         SDLK_S,
                                         KMOD_PRIMARY | SDL_KMOD_SHIFT,
                                         "document.save open:1" },
                                       { download_Icon " " saveToDownloads_Label,
                                         SDLK_S,
                                         KMOD_PRIMARY,
                                         "document.save" } },
                        2);
                }
                if (preloadCoverImage_Gempub(d->sourceGempub, doc_DocumentFetch_(d))) {
                    redoLayout_GmDocument(doc_DocumentFetch_(d));
                    updateVisible_DocumentView(view_DocumentFetch_(d));
                    invalidate_DocumentWidget(d->owner);
                }
            }
            else if (equal_String(url_DocumentFetch_(d), indexPageUrl_Gempub(d->sourceGempub))) {
                makeFooterButtons_DocumentWidget(
                    d->owner,
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
                const size_t navIndex = navIndex_Gempub(d->sourceGempub, url_DocumentFetch_(d));
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
                    else if (!equalCase_String(url_DocumentFetch_(d), indexPageUrl_Gempub(d->sourceGempub))) {
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
                    makeFooterButtons_DocumentWidget(d->owner, constData_Array(items), size_Array(items));
                }
            }
            if (!isCached && prefs_App()->pinSplit &&
                equal_String(url_DocumentFetch_(d), indexPageUrl_Gempub(d->sourceGempub))) {
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

iBool fetch_DocumentFetch(iDocumentFetch *d) {
    /* We may be instructed to wait before fetching to avoid congestion. */
    if (d->flags & waitForIdle_DocumentFetchFlag) {
        /* Check all documents in the window. */
        if (isAnyDocumentRequestOngoing_MainWindow(get_MainWindow())) {
            return iFalse; /* have to try again later */
        }
        d->flags &= ~waitForIdle_DocumentFetchFlag;
    }
    /* Forget the previous request. */
    if (d->request) {
        iRelease(d->request);
        d->request = NULL;
    }
    if (isTitanUrl_String(url_DocumentFetch_(d))) {
        return iFalse; /* don't fetch Titan URLs from here, only through UploadWidget */
    }
    releasePlayers_Media(media_GmDocument(doc_DocumentFetch_(d)));
    notifyf_Root(as_Widget(d->owner)->root,
                 "document.request.started doc:%p url:%s",
                 d->owner,
                 cstr_String(url_DocumentFetch_(d)));
    setLinkNumberMode_DocumentWidget(d->owner, iFalse);
    setDrawDownloadCounter_DocumentWidget(d->owner, iFalse);
    d->flags &= ~pendingRedirect_DocumentFetchFlag;
    d->state = fetching_RequestState;
    d->lastRequestUpdateAt = 0;
    d->request = new_GmRequest(certs_App());
    setUrl_GmRequest(d->request, url_DocumentFetch_(d));
    /* Overriding identity. */
    if (isIdentityPinned_DocumentWidget(d->owner)) {
        const iGmIdentity *ident = identity_DocumentWidget(d->owner);
        if (ident) {
            setIdentity_GmRequest(d->request, ident);
        }
    }
    iConnect(GmRequest, d->request, updated, d->owner, requestUpdated_DocumentFetch);
    iConnect(GmRequest, d->request, finished, d->owner, requestFinished_DocumentFetch);
    submit_GmRequest(d->request);
    return iTrue;
}

void updateTrust_DocumentFetch(iDocumentFetch *d, const iGmResponse *response) {
    if (response) {
        d->certFlags  = response->certFlags;
        d->certExpiry = response->certValidUntil;
        set_Block(d->certFingerprint, &response->certFingerprint);
        set_Block(d->certFullFingerprint, &response->certFullFingerprint);
        set_String(d->certSubject, &response->certSubject);
    }
    iLabelWidget *lock = findChild_Widget(root_Widget(as_Widget(d->owner)), "navbar.lock");
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

const char *humanReadableStatusCode_DocumentFetch(enum iGmStatusCode code) {
    if (code <= 0) {
        return "";
    }
    return format_CStr("%d ", code);
}

void restoreAddressBarAndHistory_DocumentFetch(iDocumentFetch *d, const iString *fetchedUrl) {
    /* The displayed page hasn't changed (e.g., a media/prompt response was inlined into the
       existing document instead of replacing it), so restore the address bar and history to
       match rather than leaving them pointed at the new request's URL. `fetchedUrl` is what the
       just-finished request was for, which may not equal the document's URL yet. */
    if (equal_String(&mostRecentUrl_History(state_DocumentWidget(d->owner)->history)->url, fetchedUrl)) {
        undo_History(state_DocumentWidget(d->owner)->history);
    }
    if (setDocumentUrl_DocumentWidget(d->owner, url_GmDocument(doc_DocumentFetch_(d)))) {
        notify_Widget(d->owner, "!document.changed doc:%p url:%s", d->owner, cstr_String(url_DocumentFetch_(d)));
    }
}

void cleanupRedirected_DocumentFetch(iDocumentFetch *d) {
    iAssert(!isOriginToNewTab_DocumentWidget(d->owner));
    /* This is called in the special case where an input prompt becomes inlined
       (response does not contain a document body) so the previous document of
       the tab is retained. */
    restoreAddressBarAndHistory_DocumentFetch(d, url_DocumentFetch_(d));
}

void checkResponse_DocumentFetch(iDocumentFetch *d) {
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
                ~d->flags & preventInlining_DocumentFetchFlag &&
                d->requestLinkId &&
                isSuccess_GmStatusCode(d->sourceStatus) &&
                startsWithCase_String(&d->sourceMime, "text/gemini") &&
                isSuccess_GmStatusCode(statusCode) &&
                startsWithCase_String(&resp->meta, "image/")) {
            /* This request is turned into a new media request in the current document. */
            iDisconnect(GmRequest, d->request, updated, d->owner, requestUpdated_DocumentFetch);
            iDisconnect(GmRequest, d->request, finished, d->owner, requestFinished_DocumentFetch);
            iMediaRequest *mr = newReused_MediaRequest(d->owner, d->requestLinkId, d->request);
            unlockResponse_GmRequest(d->request);
            d->request = NULL; /* ownership moved */
            if (!isFinished_GmRequest(mr->req)) {
                postCommand_Widget(d->owner, "document.request.cancelled doc:%p", d->owner);
            }
            addRequest_InlineMedia(media_DocumentWidget(d->owner), mr);
            iRelease(mr);
            /* Reset the fetch state, returning to the originating page. */
            d->state = ready_RequestState;
            restoreAddressBarAndHistory_DocumentFetch(d, url_GmRequest(mr->req));
            updateProgress_DocumentFetch(d);
            notify_Widget(d->owner, "media.updated link:%u request:%p", d->requestLinkId, mr);
            if (isFinished_GmRequest(mr->req)) {
                postCommand_Widget(d->owner, "media.finished link:%u request:%p", d->requestLinkId, mr);
            }
            return;
        }
        /* Get ready for the incoming new document. */
        d->state = receivedPartialResponse_RequestState;
        d->flags &= ~(fromCache_DocumentFetchFlag | goBackOnStop_DocumentFetchFlag);
        clearRequests_InlineMedia(media_DocumentWidget(d->owner));
        updateTrust_DocumentFetch(d, resp);
        if (~d->certFlags & trusted_GmCertFlag &&
            isSuccess_GmStatusCode(statusCode) &&
            (equalCase_Rangecc(urlScheme_String(url_DocumentFetch_(d)), "gemini") ||
             equalCase_Rangecc(urlScheme_String(url_DocumentFetch_(d)), "gophers")) &&
            prefs_App()->warnTlsSecurity) {
            statusCode = tlsServerCertificateNotVerified_GmStatusCode;
        }
        init_Anim(&view_DocumentFetch_(d)->sideOpacity, 0);
        init_Anim(&view_DocumentFetch_(d)->altTextOpacity, 0);
        format_String(&d->sourceHeader,
                      "%s%s",
                      humanReadableStatusCode_DocumentFetch(statusCode),
                      isEmpty_String(&resp->meta) && !isSuccess_GmStatusCode(statusCode)
                          ? get_GmError(statusCode)->title
                          : cstr_String(&resp->meta));
        d->sourceStatus = statusCode;
        switch (category_GmStatusCode(statusCode)) {
            case categoryInput_GmStatusCode: {
                /* Let the navigation history know that we have been to this URL even though
                   it is only displayed as an input dialog. */
                visitUrl_Visited(visited_App(), url_DocumentFetch_(d), transient_VisitedUrlFlag);
                /* Split-pinning may route this fetch to a different document than the link
                   lives on. If this one has no anchor but knows its origin, show the prompt
                   there instead of a modal. A deliberately opened new tab is left alone
                   (the user did ask for a tab). */
                iDocumentWidget *target = d->owner;
                if (prefs_App()->promptPosition == inline_InputPromptPosition &&
                    !d->requestLinkId && !isOriginToNewTab_DocumentWidget(d->owner) && !isEmpty_String(originId_DocumentWidget(d->owner))) {
                    iDocumentWidget *origin = findWidget_App(cstr_String(originId_DocumentWidget(d->owner)));
                    if (origin && requestLinkId_DocumentWidget(origin)) {
                        target = origin;
                    }
                }
                /* Falls back to the modal if inline isn't possible. */
                const iBool useModal = deviceType_App() != desktop_AppDeviceType ||
                                       prefs_App()->promptPosition != inline_InputPromptPosition ||
                                       !requestLinkId_DocumentWidget(target);
                if (useModal) {
                    makeModal_Query(
                        query_DocumentWidget(d->owner),
                        url_DocumentFetch_(d),
                        statusCode == sensitiveInput_GmStatusCode,
                        isEmpty_String(&resp->meta) ? NULL : cstr_String(&resp->meta),
                        format_CStr("!document.input.submit doc:%p", d->owner));
                    if (document_App() != d->owner) {
                        /* The modal must be visible to be interacted with at all. */
                        postCommandf_App("tabs.switch page:%p", d->owner);
                    }
                }
                else if (target == d->owner) {
                    show_Query(query_DocumentWidget(d->owner), d->requestLinkId, url_DocumentFetch_(d), resp,
                                                          statusCode);
                    /* Same as the inline-image handling above. */
                    restoreAddressBarAndHistory_DocumentFetch(d, url_DocumentFetch_(d));
                }
                else {
                    /* Widget construction targets whatever root is "current", which must
                       match target's root here (possibly a different split). */
                    iRoot *oldRoot = current_Root();
                    setCurrent_Root(as_Widget(target)->root);
                    show_Query(query_DocumentWidget(target),
                                      requestLinkId_DocumentWidget(target),
                                      url_DocumentFetch_(d),
                                                          resp, statusCode);
                    setCurrent_Root(oldRoot);
                    cleanupRedirected_DocumentFetch(d);
                }
                if (document_App() == d->owner) {
                    updateTheme_DocumentWidget(d->owner);
                }
                break;
            }
            case categorySuccess_GmStatusCode: {
                visitUrl_Visited(visited_App(), url_DocumentFetch_(d), 0);
                iGmDocument *newDoc = new_GmDocument();
                replaceDocument_DocumentWidget(d->owner, newDoc /* keeps ref */);
                iRelease(newDoc);
                clear_Banner(banner_DocumentFetch_(d));
                delete_Gempub(d->sourceGempub);
                d->sourceGempub = NULL;
                destroy_Widget(footerButtons_DocumentWidget(d->owner));
                setFooterButtons_DocumentWidget(d->owner, NULL);
                if (isUrlChanged_DocumentWidget(d->owner)) {
                    /* Keep scroll position when reloading the same page. */
                    resetScroll_DocumentView(view_DocumentFetch_(d));
                }
                view_DocumentFetch_(d)->scrollY.pullActionTriggered = 0;
                updateTheme_DocumentWidget(d->owner);
                updateDocument_DocumentFetch(d, resp, NULL, iTrue);
                resetWideRuns_DocumentView(view_DocumentFetch_(d));
                break;
            }
            case categoryRedirect_GmStatusCode:
                if (isEmpty_String(&resp->meta)) {
                    showErrorPage_DocumentFetch(d, invalidRedirect_GmStatusCode, NULL);
                }
                else {
                    /* Only accept redirects that use gemini scheme. */
                    const iString *dstUrl    = absoluteUrl_String(url_DocumentFetch_(d), &resp->meta);
                    const iRangecc srcScheme = urlScheme_String(url_DocumentFetch_(d));
                    const iRangecc dstScheme = urlScheme_String(dstUrl);
                    /* Update bookmarks automatically to reflect the permanent redirection. */
                    if (statusCode == redirectPermanent_GmStatusCode) {
                        if (updateUrls_Bookmark(bookmarks_App(), url_DocumentFetch_(d), dstUrl)) {
                            notify_App("bookmarks.changed");
                        }
                    }
                    /* We only follow a fixed number of redirects at once, per Gemini spec.
                       Titan uploads are discrete, user-initiated actions rather than an
                       automatic redirect chain, so the limit does not apply to them. */
                    if (equalCase_Rangecc(srcScheme, "gemini") && d->redirectCount >= 5) {
                        showErrorPage_DocumentFetch(d, tooManyRedirects_GmStatusCode, dstUrl);
                    }
                    /* Redirects with the same scheme are automatic, and switching automatically
                       between "gemini" and "titan" is allowed. */
                    else if (prefs_App()->allowSchemeChangingRedirect ||
                             equalRangeCase_Rangecc(dstScheme, srcScheme) ||
                             (equalCase_Rangecc(srcScheme, "titan") &&
                              equalCase_Rangecc(dstScheme, "gemini")) ||
                             (equalCase_Rangecc(srcScheme, "gemini") &&
                              equalCase_Rangecc(dstScheme, "titan"))) {
                        visitUrl_Visited(visited_App(), url_DocumentFetch_(d), transient_VisitedUrlFlag);
                        postCommandf_Root(as_Widget(d->owner)->root,
                                          "open doc:%p redirect:%d url:%s",
                                          d->owner,
                                          d->redirectCount + 1,
                                          cstr_String(dstUrl));
                        /* Opening a Titan URL first prompts the user to provide the content,
                           so nothing is actually being done while we wait on the user.
                           Otherwise, the request is still essentially ongoing even though we
                           will now release the current GmRequest; we will soon continue
                           fetching the destination URL. */
                        if (!equalCase_Rangecc(dstScheme, "titan")) {
                            d->flags |= pendingRedirect_DocumentFetchFlag;
                        }
                    }
                    else {
                        /* Scheme changes must be manually approved. */
                        showErrorPage_DocumentFetch(d, schemeChangeRedirect_GmStatusCode, dstUrl);
                    }
                    unlockResponse_GmRequest(d->request);
                    iReleasePtr(&d->request);
                }
                break;
            default:
                if (isDefined_GmError(statusCode)) {
                    showErrorPage_DocumentFetch(d, statusCode, &resp->meta);
                }
                else if (category_GmStatusCode(statusCode) ==
                         categoryTemporaryFailure_GmStatusCode) {
                    showErrorPage_DocumentFetch(d, temporaryFailure_GmStatusCode, &resp->meta);
                }
                else if (category_GmStatusCode(statusCode) ==
                         categoryPermanentFailure_GmStatusCode) {
                    showErrorPage_DocumentFetch(d, permanentFailure_GmStatusCode, &resp->meta);
                }
                else {
                    showErrorPage_DocumentFetch(d, unknownStatusCode_GmStatusCode, &resp->meta);
                }
                break;
        }
    }
    else if (d->state == receivedPartialResponse_RequestState) {
        d->flags &= ~fromCache_DocumentFetchFlag;
        switch (category_GmStatusCode(statusCode)) {
            case categorySuccess_GmStatusCode:
                /* More content available. */
                updateDocument_DocumentFetch(d, resp, NULL, iFalse);
                break;
            default:
                break;
        }
    }
    unlockResponse_GmRequest(d->request);
}

iBool cancel_DocumentFetch(iDocumentFetch *d, iBool postBack) {
    d->flags &= ~pendingRedirect_DocumentFetchFlag;
    if (d->request) {
        iWidget *w = as_Widget(d->owner);
        postCommandf_Root(w->root,
                          "document.request.cancelled doc:%p url:%s", d->owner,
                      cstr_String(url_DocumentFetch_(d)));
        iReleasePtr(&d->request);
        if (d->state != ready_RequestState) {
            d->state = ready_RequestState;
            if (postBack) {
                postCommand_Root(w->root, "navigate.back");
            }
        }
        reenableAll_Query(query_DocumentWidget(d->owner));
        updateProgress_DocumentFetch(d);
        return iTrue;
    }
    return iFalse;
}

iBool tryWaiting_DocumentFetch(iDocumentFetch *d) {
    if (d->flags & waitForIdle_DocumentFetchFlag) {
        if (fetch_DocumentFetch(d)) {
            return iTrue;
        }
    }
    return iFalse;
}

const iBlock *sourceContent_DocumentFetch(const iDocumentFetch *d) {
    return &d->sourceContent;
}

iTime sourceTime_DocumentFetch(const iDocumentFetch *d) {
    return d->sourceTime;
}

iBool isRequestOngoing_DocumentFetch(const iDocumentFetch *d) {
    if (d) {
        return d->request != NULL || d->flags & pendingRedirect_DocumentFetchFlag;
    }
    return iFalse;
}

iBool isFetchingOwnLink_DocumentFetch(const iDocumentFetch *d) {
    /* Fetching one of the links in the current document. */
    return isRequestOngoing_DocumentFetch(d) && d->requestLinkId;
}

void take_DocumentFetch(iDocumentFetch *d, iGmRequest *finishedRequest) {
    cancel_DocumentFetch(d, iFalse /* don't post anything */);
    const iString *url = url_GmRequest(finishedRequest);

    add_History(state_DocumentWidget(d->owner)->history, url);
    setDocumentUrl_DocumentWidget(d->owner, url);
    d->state = fetching_RequestState;
    iAssert(d->request == NULL);
    d->request = finishedRequest;
    notify_Widget(d->owner,
                  "document.request.finished doc:%p reqid:%u request:%p",
                  d->owner,
                  id_GmRequest(d->request),
                  d->request);
}

/*----------------------------------------------------------------------------------------------*/

void showErrorPage_DocumentFetch(iDocumentFetch *d, enum iGmStatusCode code,
                                 const iString *meta) {
    iString        *src = collectNew_String();
    const iGmError *msg = get_GmError(code);
    makeFooterButtons_DocumentWidget(d->owner, NULL, 0);
    const iString *serverErrorMsg = NULL;
    if (meta) {
        switch (code) {
            case schemeChangeRedirect_GmStatusCode:
            case tooManyRedirects_GmStatusCode:
                appendFormat_String(src, "=> %s\n", cstr_String(meta));
                break;
            case tlsServerCertificateExpired_GmStatusCode:
                makeFooterButtons_DocumentWidget(
                    d->owner,
                    (iMenuItem[]){ { rightArrowhead_Icon " ${menu.unexpire}",
                                       SDLK_RETURN, 0, "server.unexpire"
                                   },
                                   { info_Icon " ${menu.pageinfo}",
                                     SDLK_I,
                                     KMOD_PRIMARY,
                                     "document.info" } },
                    2);
                break;
            case tlsServerCertificateNotVerified_GmStatusCode:
            case proxyCertificateNotVerified_GmStatusCode:
                makeFooterButtons_DocumentWidget(
                    d->owner,
                    (iMenuItem[]){ { info_Icon " ${menu.pageinfo}",
                                     SDLK_I,
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
                toString_Sym(SDLK_S, KMOD_PRIMARY, key);
//                appendFormat_String(src, "\n```\n%s\n```\n", cstr_String(meta));
                const char *mtype = mediaTypeFromFileExtension_String(url_DocumentFetch_(d));
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
                makeFooterButtons_DocumentWidget(d->owner, data_Array(&items), size_Array(&items));
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
        makeFooterButtons_DocumentWidget(
            d->owner,
            (iMenuItem[]){
                { person_Icon " ${menu.identity.newdomain}", SDLK_N, 0, "ident.new scope:1" },
                { person_Icon " ${menu.identity.new}", newIdentity_KeyShortcut, "ident.new" },
                { leftHalf_Icon " ${menu.show.identities}", showIdentities_KeyShortcut,
                  deviceType_App() == desktop_AppDeviceType ? "sidebar.mode arg:3 show:1"
                                                            : "preferences idents:1" } },
            3);
    }
    /* Make a new document for the error page.*/
    iGmDocument *errorDoc = new_GmDocument();
    setMode_GmDocument(errorDoc, coverPage_GmDocumentMode); /* UI content */
    setWidth_GmDocument(errorDoc,
                        documentWidth_DocumentView(view_DocumentFetch_(d)),
                        width_Widget(d->owner),
                        maxDocumentWidth_DocumentView(view_DocumentFetch_(d)));
    setUrl_GmDocument(errorDoc, url_DocumentFetch_(d));
    setFormat_GmDocument(errorDoc, gemini_SourceFormat);
    replaceDocument_DocumentWidget(d->owner, errorDoc);
    iRelease(errorDoc);
    clear_Banner(banner_DocumentFetch_(d));
    add_Banner(banner_DocumentFetch_(d), error_BannerType, code, serverErrorMsg, NULL);
    d->state = ready_RequestState;
    setSource_DocumentWidget(d->owner, src);
    updateTheme_DocumentWidget(d->owner);
    resetScroll_DocumentView(view_DocumentFetch_(d));
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

void updateDocument_DocumentFetch(iDocumentFetch *d,
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
        invalidate_DocumentWidget(d->owner);
        if (document_App() == d->owner) {
            updateTheme_DocumentWidget(d->owner);
        }
        clear_String(&d->sourceMime);
        d->sourceTime = response->when;
        updateDrawBufs_DocumentView(view_DocumentFetch_(d), updateTimestampBuf_DrawBufsFlag);
        initBlock_String(&str, &response->body); /* Note: Body may be megabytes in size. */
        if (isSuccess_GmStatusCode(statusCode)) {
            /* Check the MIME type. */
            iRangecc           charset     = range_CStr("utf-8");
            enum iSourceFormat docFormat   = undefined_SourceFormat;
            iBool              isCoverPage = iFalse;
            const iString     *mimeStr =
                collect_String(lower_String(&response->meta)); /* for convenience */
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
                            isCoverPage = iTrue;
                        }
                        else if (isUtf8_Rangecc(range_Block(&response->body))) {
                            param = range_CStr("text/plain");
                        }
                    }
                    if (equal_Rangecc(param, "text/plain")) {
                        iUrl parts;
                        init_Url(&parts, url_DocumentFetch_(d));
                        const iRangecc fileName = baseNameSep_Path(collectNewRange_String(parts.path), "/");
                        if (endsWithCase_Rangecc(fileName, ".md") ||
                            endsWithCase_Rangecc(fileName, ".mdown") ||
                            endsWithCase_Rangecc(fileName, ".markdown")) {
                            param = range_CStr("text/markdown");
                        }
#if 0
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
#endif
                    }
                }
                if (equal_Rangecc(param, "text/gemini") ||
                    equal_Rangecc(param, "text/gophermenu")) {
                    docFormat = gemini_SourceFormat;
                    setRange_String(&d->sourceMime, param);
                }
                else if (equal_Rangecc(param, "text/markdown")) {
                    docFormat = markdown_SourceFormat;
                    setRange_String(&d->sourceMime, param);
                    postCommand_Widget(
                        d->owner, "document.viewformat arg:%d", !prefs_App()->markdownAsSource);
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
                    isCoverPage = iTrue;
                    docFormat = gemini_SourceFormat;
                    setRange_String(&d->sourceMime, param);
                    format_String(&str, "# TrueType Font\n");
                    iString *decUrl      = collect_String(urlDecode_String(url_DocumentFetch_(d)));
                    iRangecc name        = baseNameSep_Path(decUrl, "/");
                    iBool    isInstalled = iFalse;
                    if (startsWith_String(
                            collect_String(localFilePathFromUrl_String(url_DocumentFetch_(d))),
                            cstr_String(fontsDir_App()))) {
                        isInstalled = iTrue;
                    }
                    appendCStr_String(&str, "## ");
                    appendRange_String(&str, name);
                    appendCStr_String(&str, "\n\n");
                    appendCStr_String(
                        &str, cstr_Lang(isInstalled ? "truetype.help.installed" : "truetype.help"));
                    appendCStr_String(&str, "\n");
                    if (!isInstalled) {
                        makeFooterButtons_DocumentWidget(
                            d->owner,
                            (iMenuItem[]){
                                { add_Icon " ${fontpack.install.ttf}",
                                  SDLK_RETURN,
                                  0,
                                  format_CStr("!fontpack.install ttf:1 name:%s",
                                              cstr_Rangecc(name)) },
                                { folder_Icon " ${fontpack.open.fontsdir}",
                                  SDLK_D,
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
                    isCoverPage = iTrue;
                    docFormat = gemini_SourceFormat;
                    setRange_String(&d->sourceMime, param);
                    iArchive *zip = new_Archive();
                    openData_Archive(zip, &response->body);
                    if (equal_Rangecc(param, mimeType_FontPack)) {
                        /* Show some information about fontpacks, and set up footer actions. */
                        if (isOpen_Archive(zip)) {
                            iFontPack *fp = new_FontPack();
                            setUrl_FontPack(fp, url_DocumentFetch_(d));
                            setStandalone_FontPack(fp, iTrue);
                            if (loadArchive_FontPack(fp, zip)) {
                                appendFormat_String(&str, "# " fontpack_Icon "%s\n%s",
                                                    cstr_String(id_FontPack(fp).id),
                                                    cstrCollect_String(infoText_FontPack(fp, iTrue)));
                            }
                            appendCStr_String(&str, "\n");
                            appendCStr_String(&str, cstr_Lang("fontpack.help"));
                            appendCStr_String(&str, "\n");
                            iConstForEach(Array, a, actions_FontPack(fp, iTrue)) {
                                pushBack_Array(footerItems, a.value);
                            }
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
                            cstr_Rangecc(baseNameSep_Path(
                                collect_String(urlDecode_String(
                                    urlQueryStripped_String(url_DocumentFetch_(d)))),
                                "/")));
                        appendCStr_String(&str, "\n");
                    }
                    iRelease(zip);
                    appendCStr_String(&str, "\n");
                    iString *localPath = localFilePathFromUrl_String(url_DocumentFetch_(d));
                    if (!localPath || !fileExists_FileInfo(localPath)) {
                        iString *key = collectNew_String();
                        toString_Sym(SDLK_S, KMOD_PRIMARY, key);
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
                        appendFormat_String(
                            &str,
                            "=> %s/ " folder_Icon " ${doc.archive.view}\n",
                            cstr_String(withSpacesEncoded_String(url_DocumentFetch_(d))));
                    }
                    delete_String(localPath);
                    translate_Lang(&str);
                    makeFooterButtons_DocumentWidget(
                        d->owner, constData_Array(footerItems), size_Array(footerItems));
                }
                else if (!isTerminal_Platform() && (startsWith_Rangecc(param, "image/") ||
                                                    startsWith_Rangecc(param, "audio/"))) {
                    const iBool isAudio = startsWith_Rangecc(param, "audio/");
                    /* Make a simple document with an image or audio player. */
                    clear_String(&str);
                    isCoverPage = iTrue;
                    docFormat = gemini_SourceFormat;
                    setRange_String(&d->sourceMime, param);
                    const iGmLinkId imgLinkId = 1; /* there's only the one link */
                    /* TODO: Do the image loading in `postProcessRequestContent_DocumentWidget_()` */
                    if ((isAudio && isInitialUpdate) || (!isAudio && isRequestFinished)) {
                        const char *linkTitle = cstr_Lang(
                            startsWith_String(mimeStr, "image/") ? "media.untitled.image"
                                                                 : "media.untitled.audio");
                        iUrl parts;
                        init_Url(&parts, url_DocumentFetch_(d));
                        if (!isEmpty_Range(&parts.path) && !equalCase_Rangecc(parts.scheme, "data")) {
                            linkTitle =
                                baseName_Path(collect_String(newRange_String(parts.path))).start;
                        }
                        format_String(&str, "=> %s %s\n",
                                      cstr_String(canonicalUrl_String(url_DocumentFetch_(d))),
                                      linkTitle);
                        setData_Media(media_GmDocument(doc_DocumentFetch_(d)),
                                      imgLinkId,
                                      mimeStr,
                                      &response->body,
                                      !isRequestFinished ? partialData_MediaFlag : 0);
                        redoLayout_GmDocument(doc_DocumentFetch_(d));
                    }
                    else if (isAudio && !isInitialUpdate) {
                        /* Update the audio content. */
                        setData_Media(media_GmDocument(doc_DocumentFetch_(d)),
                                      imgLinkId,
                                      mimeStr,
                                      &response->body,
                                      !isRequestFinished ? partialData_MediaFlag : 0);
                        refresh_Widget(d->owner);
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
                    setDrawDownloadCounter_DocumentWidget(d->owner, iFalse);
                    if (isUtf8_Rangecc(range_Block(&response->body))) {
                        docFormat = plainText_SourceFormat;
                        charset = range_CStr("utf-8");
                        setWarning_GmDocument(
                            doc_DocumentFetch_(d), unsupportedMediaTypeShownAsUtf8_GmDocumentWarning, iTrue);
                    }
                    else {
                        showErrorPage_DocumentFetch(d, unsupportedMimeType_GmStatusCode, &response->meta);
                        deinit_String(&str);
                        return;
                    }
                }
                else {
                    setDrawDownloadCounter_DocumentWidget(d->owner, iTrue);
                    clear_PtrSet(view_DocumentFetch_(d)->invalidRuns);
                    documentRunsInvalidated_DocumentWidget(d->owner);
                    deinit_String(&str);
                    return;
                }
            }
            if (isCoverPage) {
                /* Cover pages are not considered normal content (e.g., error message, overview). */
                setMode_GmDocument(doc_DocumentFetch_(d), coverPage_GmDocumentMode);
            }
            setFormat_GmDocument(doc_DocumentFetch_(d), docFormat);
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
            replaceDocument_DocumentWidget(d->owner, cachedDoc);
            if (updateWidth_DocumentView(view_DocumentFetch_(d))) {
                documentRunsInvalidated_DocumentWidget(d->owner); /* GmRuns reallocated */
            }
        }
        else if (setSource) {
            setSource_DocumentWidget(d->owner, &str);
        }
        deinit_String(&str);
    }
}

void addBannerWarnings_DocumentFetch(iDocumentFetch *d) {
    updateBanner_DocumentWidget(d->owner);
    /* Warnings are not shown on internal pages. */
    if (equalCase_Rangecc(urlScheme_String(url_DocumentFetch_(d)), "about")) {
        clear_Banner(banner_DocumentFetch_(d));
        return;
    }
    /* Warnings related to certificates and trust. */
    const int req = timeVerified_GmCertFlag | domainVerified_GmCertFlag | trusted_GmCertFlag;
    int certFlags = d->certFlags;
    if (prefs_App()->warnTlsSecurity && certFlags & available_GmCertFlag &&
        (certFlags & req) != req && numItems_Banner(banner_DocumentFetch_(d)) == 0) {
        iString *title = collectNewCStr_String(cstr_Lang("dlg.certwarn.title"));
        iString *str   = collectNew_String();
        if (certFlags & timeVerified_GmCertFlag && certFlags & domainVerified_GmCertFlag) {
            iUrl parts;
            init_Url(&parts, url_DocumentFetch_(d));
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
            const iString *proxy =
                schemeProxy_Prefs(get_Prefs(), urlScheme_String(url_DocumentFetch_(d)));
            appendFormat_String(str, cstr_Lang("dlg.certwarn.domain"),
                                cstr_Rangecc(urlHost_String(proxy
                                    ? collectNewFormat_String("gemini://%s", cstr_String(proxy))
                                    : url_DocumentFetch_(d))),
                                cstr_String(d->certSubject));
        }
        else {
            appendCStr_String(str, cstr_Lang("dlg.certwarn.domain.expired"));
        }
        add_Banner(banner_DocumentFetch_(d), warning_BannerType, none_GmStatusCode, title, str);
    }
    /* Warnings related to page contents. */
    int dismissed =
        value_SiteSpec(collectNewRange_String(urlRoot_String(url_DocumentFetch_(d))),
                       dismissWarnings_SiteSpecKey) |
        (!prefs_App()->warnAboutMissingGlyphs ? missingGlyphs_GmDocumentWarning : 0);
    /* File pages don't allow dismissing warnings, so skip it. */
    if (equalCase_Rangecc(urlScheme_String(url_DocumentFetch_(d)), "file")) {
        dismissed |= ansiEscapes_GmDocumentWarning;
    }
    const int warnings = warnings_GmDocument(doc_DocumentFetch_(d)) & ~dismissed;
    if (warnings & missingGlyphs_GmDocumentWarning) {
        add_Banner(banner_DocumentFetch_(d), warning_BannerType, missingGlyphs_GmStatusCode, NULL, NULL);
        /* TODO: List one or more of the missing characters and/or their Unicode blocks? */
    }
    if (warnings & ansiEscapes_GmDocumentWarning) {
        add_Banner(banner_DocumentFetch_(d), warning_BannerType, ansiEscapes_GmStatusCode, NULL, NULL);
    }
    if (warnings & unsupportedMediaTypeShownAsUtf8_GmDocumentWarning) {
        add_Banner(banner_DocumentFetch_(d), warning_BannerType, unsupportedMimeTypeShownAsUtf8_GmStatusCode,
                   NULL, NULL);
    }
}
