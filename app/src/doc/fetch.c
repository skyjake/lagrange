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
#include "inputprompts.h"
#include "persistentstate.h"
#include "ui/command.h"
#include "ui/documentwidget.h"
#include "ui/indicatorwidget.h"
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
#include <SDL_timer.h>

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

static iGmDocument *doc_DocumentFetch_(const iDocumentFetch *d) {
    return view_DocumentWidget(d->owner)->doc;
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
    showForPromptUrls_InputPrompts(inputPrompts_DocumentWidget(d->owner));
    /* Gempub page behavior and footer actions. */ {
        delete_Gempub(d->sourceGempub);
        d->sourceGempub = NULL;
        iBool isInsideArchive = iFalse;
        d->sourceGempub = openForContent_Gempub(&d->sourceContent, &d->sourceMime, mod_DocumentWidget(d->owner)->url);
        if (!d->sourceGempub) {
            d->sourceGempub = openForLocalUrl_Gempub(mod_DocumentWidget(d->owner)->url, &isInsideArchive);
        }
        if (d->sourceGempub && !isInsideArchive) {
            setSource_DocumentWidget(d->owner, collect_String(coverPageSource_Gempub(d->sourceGempub)));
            setCStr_String(&d->sourceMime, mimeType_Gempub);
        }
        if (d->sourceGempub) {
            if (equal_String(mod_DocumentWidget(d->owner)->url, coverPageUrl_Gempub(d->sourceGempub))) {
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
                                         SDLK_s,
                                         KMOD_PRIMARY | KMOD_SHIFT,
                                         "document.save open:1" },
                                       { download_Icon " " saveToDownloads_Label,
                                         SDLK_s,
                                         KMOD_PRIMARY,
                                         "document.save" } },
                        2);
                }
                if (preloadCoverImage_Gempub(d->sourceGempub, doc_DocumentFetch_(d))) {
                    redoLayout_GmDocument(doc_DocumentFetch_(d));
                    updateVisible_DocumentView(view_DocumentWidget(d->owner));
                    invalidate_DocumentWidget(d->owner);
                }
            }
            else if (equal_String(mod_DocumentWidget(d->owner)->url, indexPageUrl_Gempub(d->sourceGempub))) {
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
                const size_t navIndex = navIndex_Gempub(d->sourceGempub, mod_DocumentWidget(d->owner)->url);
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
                    else if (!equalCase_String(mod_DocumentWidget(d->owner)->url, indexPageUrl_Gempub(d->sourceGempub))) {
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
                equal_String(mod_DocumentWidget(d->owner)->url, indexPageUrl_Gempub(d->sourceGempub))) {
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
    if (isTitanUrl_String(mod_DocumentWidget(d->owner)->url)) {
        return iFalse; /* don't fetch Titan URLs from here, only through UploadWidget */
    }
    releasePlayers_Media(media_GmDocument(doc_DocumentFetch_(d)));
    notifyf_Root(as_Widget(d->owner)->root,
                 "document.request.started doc:%p url:%s",
                 d->owner,
                 cstr_String(mod_DocumentWidget(d->owner)->url));
    setLinkNumberMode_DocumentWidget(d->owner, iFalse);
    setDrawDownloadCounter_DocumentWidget(d->owner, iFalse);
    d->flags &= ~pendingRedirect_DocumentFetchFlag;
    d->state = fetching_RequestState;
    d->lastRequestUpdateAt = 0;
    d->request = new_GmRequest(certs_App());
    setUrl_GmRequest(d->request, mod_DocumentWidget(d->owner)->url);
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
       just-finished request was for, which may not equal mod_DocumentWidget(d->owner)->url yet at the call site. */
    if (equal_String(&mostRecentUrl_History(mod_DocumentWidget(d->owner)->history)->url, fetchedUrl)) {
        undo_History(mod_DocumentWidget(d->owner)->history);
    }
    if (setDocumentUrl_DocumentWidget(d->owner, url_GmDocument(doc_DocumentFetch_(d)))) {
        notify_Widget(d->owner, "!document.changed doc:%p url:%s", d->owner, cstr_String(mod_DocumentWidget(d->owner)->url));
    }
}

void cleanupRedirected_DocumentFetch(iDocumentFetch *d) {
    iAssert(!isOriginToNewTab_DocumentWidget(d->owner));
    /* This is called in the special case where an input prompt becomes inlined
       (response does not contain a document body) so the previous document of
       the tab is retained. */
    restoreAddressBarAndHistory_DocumentFetch(d, mod_DocumentWidget(d->owner)->url);
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
            (equalCase_Rangecc(urlScheme_String(mod_DocumentWidget(d->owner)->url), "gemini") ||
             equalCase_Rangecc(urlScheme_String(mod_DocumentWidget(d->owner)->url), "gophers")) &&
            prefs_App()->warnTlsSecurity) {
            statusCode = tlsServerCertificateNotVerified_GmStatusCode;
        }
        init_Anim(&view_DocumentWidget(d->owner)->sideOpacity, 0);
        init_Anim(&view_DocumentWidget(d->owner)->altTextOpacity, 0);
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
                visitUrl_Visited(visited_App(), mod_DocumentWidget(d->owner)->url, transient_VisitedUrlFlag);
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
                    makeInputPrompt_DocumentWidget(
                        d->owner,
                        mod_DocumentWidget(d->owner)->url,
                        statusCode == sensitiveInput_GmStatusCode,
                        isEmpty_String(&resp->meta) ? NULL : cstr_String(&resp->meta),
                        format_CStr("!document.input.submit doc:%p", d->owner));
                    if (document_App() != d->owner) {
                        /* The modal must be visible to be interacted with at all. */
                        postCommandf_App("tabs.switch page:%p", d->owner);
                    }
                }
                else if (target == d->owner) {
                    show_InputPrompts(inputPrompts_DocumentWidget(d->owner), d->requestLinkId, mod_DocumentWidget(d->owner)->url, resp,
                                                          statusCode);
                    /* Same as the inline-image handling above. */
                    restoreAddressBarAndHistory_DocumentFetch(d, mod_DocumentWidget(d->owner)->url);
                }
                else {
                    /* Widget construction targets whatever root is "current", which must
                       match target's root here (possibly a different split). */
                    iRoot *oldRoot = current_Root();
                    setCurrent_Root(as_Widget(target)->root);
                    show_InputPrompts(inputPrompts_DocumentWidget(target),
                                      requestLinkId_DocumentWidget(target),
                                      mod_DocumentWidget(d->owner)->url,
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
                visitUrl_Visited(visited_App(), mod_DocumentWidget(d->owner)->url, 0);
                iGmDocument *newDoc = new_GmDocument();
                replaceDocument_DocumentWidget(d->owner, newDoc /* keeps ref */);
                iRelease(newDoc);
                clear_Banner(banner_DocumentWidget(d->owner));
                delete_Gempub(d->sourceGempub);
                d->sourceGempub = NULL;
                destroy_Widget(footerButtons_DocumentWidget(d->owner));
                setFooterButtons_DocumentWidget(d->owner, NULL);
                if (isUrlChanged_DocumentWidget(d->owner)) {
                    /* Keep scroll position when reloading the same page. */
                    resetScroll_DocumentView(view_DocumentWidget(d->owner));
                }
                view_DocumentWidget(d->owner)->scrollY.pullActionTriggered = 0;
                updateTheme_DocumentWidget(d->owner);
                updateDocument_DocumentWidget(d->owner, resp, NULL, iTrue);
                resetWideRuns_DocumentView(view_DocumentWidget(d->owner));
                break;
            }
            case categoryRedirect_GmStatusCode:
                if (isEmpty_String(&resp->meta)) {
                    showErrorPage_DocumentWidget(d->owner, invalidRedirect_GmStatusCode, NULL);
                }
                else {
                    /* Only accept redirects that use gemini scheme. */
                    const iString *dstUrl    = absoluteUrl_String(mod_DocumentWidget(d->owner)->url, &resp->meta);
                    const iRangecc srcScheme = urlScheme_String(mod_DocumentWidget(d->owner)->url);
                    const iRangecc dstScheme = urlScheme_String(dstUrl);
                    /* Update bookmarks automatically to reflect the permanent redirection. */
                    if (statusCode == redirectPermanent_GmStatusCode) {
                        if (updateUrls_Bookmark(bookmarks_App(), mod_DocumentWidget(d->owner)->url, dstUrl)) {
                            notify_App("bookmarks.changed");
                        }
                    }
                    /* We only follow a fixed number of redirects at once, per Gemini spec.
                       Titan uploads are discrete, user-initiated actions rather than an
                       automatic redirect chain, so the limit does not apply to them. */
                    if (equalCase_Rangecc(srcScheme, "gemini") && d->redirectCount >= 5) {
                        showErrorPage_DocumentWidget(d->owner, tooManyRedirects_GmStatusCode, dstUrl);
                    }
                    /* Redirects with the same scheme are automatic, and switching automatically
                       between "gemini" and "titan" is allowed. */
                    else if (prefs_App()->allowSchemeChangingRedirect ||
                             equalRangeCase_Rangecc(dstScheme, srcScheme) ||
                             (equalCase_Rangecc(srcScheme, "titan") &&
                              equalCase_Rangecc(dstScheme, "gemini")) ||
                             (equalCase_Rangecc(srcScheme, "gemini") &&
                              equalCase_Rangecc(dstScheme, "titan"))) {
                        visitUrl_Visited(visited_App(), mod_DocumentWidget(d->owner)->url, transient_VisitedUrlFlag);
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
                        showErrorPage_DocumentWidget(d->owner, schemeChangeRedirect_GmStatusCode, dstUrl);
                    }
                    unlockResponse_GmRequest(d->request);
                    iReleasePtr(&d->request);
                }
                break;
            default:
                if (isDefined_GmError(statusCode)) {
                    showErrorPage_DocumentWidget(d->owner, statusCode, &resp->meta);
                }
                else if (category_GmStatusCode(statusCode) ==
                         categoryTemporaryFailure_GmStatusCode) {
                    showErrorPage_DocumentWidget(d->owner, temporaryFailure_GmStatusCode, &resp->meta);
                }
                else if (category_GmStatusCode(statusCode) ==
                         categoryPermanentFailure_GmStatusCode) {
                    showErrorPage_DocumentWidget(d->owner, permanentFailure_GmStatusCode, &resp->meta);
                }
                else {
                    showErrorPage_DocumentWidget(d->owner, unknownStatusCode_GmStatusCode, &resp->meta);
                }
                break;
        }
    }
    else if (d->state == receivedPartialResponse_RequestState) {
        d->flags &= ~fromCache_DocumentFetchFlag;
        switch (category_GmStatusCode(statusCode)) {
            case categorySuccess_GmStatusCode:
                /* More content available. */
                updateDocument_DocumentWidget(d->owner, resp, NULL, iFalse);
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
                      cstr_String(mod_DocumentWidget(d->owner)->url));
        iReleasePtr(&d->request);
        if (d->state != ready_RequestState) {
            d->state = ready_RequestState;
            if (postBack) {
                postCommand_Root(w->root, "navigate.back");
            }
        }
        reenableAll_InputPrompts(inputPrompts_DocumentWidget(d->owner));
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

    add_History(mod_DocumentWidget(d->owner)->history, url);
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