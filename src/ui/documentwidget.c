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
#include "scrollwidget.h"
#include "inputwidget.h"
#include "labelwidget.h"
#include "paint.h"
#include "command.h"
#include "util.h"
#include "history.h"
#include "app.h"
#include "../gmdocument.h"
#include "../gmrequest.h"
#include "../gmutil.h"

#include <the_Foundation/file.h>
#include <the_Foundation/objectlist.h>
#include <the_Foundation/path.h>
#include <the_Foundation/ptrarray.h>
#include <the_Foundation/regexp.h>
#include <the_Foundation/stringarray.h>
#include <SDL_clipboard.h>
#include <SDL_timer.h>
#include <SDL_render.h>
#include <ctype.h>

iDeclareClass(MediaRequest)

struct Impl_MediaRequest {
    iObject          object;
    iDocumentWidget *doc;
    iGmLinkId        linkId;
    iGmRequest *     req;
    iAtomicInt       isUpdated;
};

static void updated_MediaRequest_(iAnyObject *obj) {
    iMediaRequest *d = obj;
    int wasUpdated = exchange_Atomic(&d->isUpdated, iTrue);
    if (!wasUpdated) {
        postCommandf_App("media.updated link:%u request:%p", d->linkId, d);
    }
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
    set_Atomic(&d->isUpdated, iFalse);
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
    d->history         = new_History();
    d->url             = new_String();
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

iDeclareType(VisBuffer)
iDeclareTypeConstruction(VisBuffer)

struct Impl_VisBuffer {
    SDL_Texture *  texture[2];
    int            index;
    iInt2          size;
    iRangei        validRange;
};

void init_VisBuffer(iVisBuffer *d) {
    iZap(*d);
}

void deinit_VisBuffer(iVisBuffer *d) {
    iForIndices(i, d->texture) {
        if (d->texture[i]) {
            SDL_DestroyTexture(d->texture[i]);
        }
    }
}

void dealloc_VisBuffer(iVisBuffer *d) {
    d->size = zero_I2();
    iZap(d->validRange);
    iForIndices(i, d->texture) {
        SDL_DestroyTexture(d->texture[i]);
        d->texture[i] = NULL;
    }
}

iDefineTypeConstruction(VisBuffer)

/*----------------------------------------------------------------------------------------------*/

static const int smoothSpeed_DocumentWidget_ = 120; /* unit: gap_Text per second */

enum iRequestState {
    blank_RequestState,
    fetching_RequestState,
    receivedPartialResponse_RequestState,
    ready_RequestState,
};

struct Impl_DocumentWidget {
    iWidget        widget;
    enum iRequestState state;
    iModel         mod;
    iString *      titleUser;
    iGmRequest *   request;
    iAtomicInt     isRequestUpdated; /* request has new content, need to parse it */
    iObjectList *  media;
    iGmDocument *  doc;
    int            certFlags;
    iDate          certExpiry;
    iString *      certSubject;
    iBool          selecting;
    iRangecc       selectMark;
    iRangecc       foundMark;
    int            pageMargin;
    iPtrArray      visibleLinks;
    const iGmRun * hoverLink;
    const iGmRun * contextLink;
    iBool          noHoverWhileScrolling;
    iBool          showLinkNumbers;
    iClick         click;
    float          initNormScrollY;
    int            scrollY;
    iScrollWidget *scroll;
    int            smoothScroll;
    int            smoothSpeed;
    int            smoothLastOffset;
    iBool          smoothContinue;
    iWidget *      menu;
    iVisBuffer *   visBuffer;
};

iDefineObjectConstruction(DocumentWidget)

void init_DocumentWidget(iDocumentWidget *d) {
    iWidget *w = as_Widget(d);
    init_Widget(w);
    setId_Widget(w, "document000");
    setFlags_Widget(w, hover_WidgetFlag, iTrue);
    init_Model(&d->mod);
    iZap(d->certExpiry);
    d->certFlags        = 0;
    d->certSubject      = new_String();
    d->state            = blank_RequestState;
    d->titleUser        = new_String();
    d->request          = NULL;
    d->isRequestUpdated = iFalse;
    d->media            = new_ObjectList();
    d->doc              = new_GmDocument();
    d->initNormScrollY  = 0;
    d->scrollY          = 0;
    d->smoothScroll     = 0;
    d->smoothSpeed      = 0;
    d->smoothLastOffset = 0;
    d->smoothContinue   = iFalse;
    d->selecting        = iFalse;
    d->selectMark       = iNullRange;
    d->foundMark        = iNullRange;
    d->pageMargin       = 5;
    d->hoverLink        = NULL;
    d->contextLink      = NULL;
    d->noHoverWhileScrolling = iFalse;
    d->showLinkNumbers  = iFalse;
    d->visBuffer        = new_VisBuffer();
    init_PtrArray(&d->visibleLinks);
    init_Click(&d->click, d, SDL_BUTTON_LEFT);
    addChild_Widget(w, iClob(d->scroll = new_ScrollWidget()));
    d->menu =
        makeMenu_Widget(w,
                        (iMenuItem[]){ { "Go Back", SDLK_LEFT, KMOD_PRIMARY, "navigate.back" },
                                       { "Go Forward", SDLK_RIGHT, KMOD_PRIMARY, "navigate.forward" },
                                       { "Reload Page", 0, 0, "navigate.reload" },
                                       { "---", 0, 0, NULL },
                                       { "Copy", 'c', KMOD_PRIMARY, "copy" },
                                       { "Copy Link", 0, 0, "document.copylink" } },
                        6);
#if !defined (iPlatformApple) /* in system menu */
    addAction_Widget(w, SDLK_w, KMOD_PRIMARY, "tabs.close");
#endif
}

void deinit_DocumentWidget(iDocumentWidget *d) {
    delete_VisBuffer(d->visBuffer);
    iRelease(d->media);
    iRelease(d->request);
    iRelease(d->doc);
    deinit_PtrArray(&d->visibleLinks);
    delete_String(d->certSubject);
    delete_String(d->titleUser);
    deinit_Model(&d->mod);
}

static void resetSmoothScroll_DocumentWidget_(iDocumentWidget *d) {
    d->smoothSpeed      = 0;
    d->smoothScroll     = 0;
    d->smoothLastOffset = 0;
    d->smoothContinue   = iFalse;
}

static int documentWidth_DocumentWidget_(const iDocumentWidget *d) {
    const iWidget *w = constAs_Widget(d);
    const iRect bounds = bounds_Widget(w);
    return iMini(bounds.size.x - gap_UI * d->pageMargin * 2,
                 fontSize_UI * 38 * zoom_App() / 100); /* TODO: Add user preference .*/
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

iLocalDef int documentToWindowY_DocumentWidget_(const iDocumentWidget *d, int docY) {
    return docY - d->scrollY + documentBounds_DocumentWidget_(d).pos.y;
}

#if 0
iLocalDef int windowToDocumentY_DocumentWidget_(const iDocumentWidget *d, int localY) {
    return localY + d->scrollY - documentBounds_DocumentWidget_(d).pos.y;
}
#endif

static iInt2 documentPos_DocumentWidget_(const iDocumentWidget *d, iInt2 pos) {
    return addY_I2(sub_I2(pos, topLeft_Rect(documentBounds_DocumentWidget_(d))), d->scrollY);
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

static iRangei visibleRange_DocumentWidget_(const iDocumentWidget *d) {
    const int margin = !hasSiteBanner_GmDocument(d->doc) ? gap_UI * d->pageMargin : 0;
    return (iRangei){ d->scrollY - margin,
                      d->scrollY + height_Rect(bounds_Widget(constAs_Widget(d))) - margin };
}

static void addVisibleLink_DocumentWidget_(void *context, const iGmRun *run) {
    iDocumentWidget *d = context;
    if (run->linkId && linkFlags_GmDocument(d->doc, run->linkId) & supportedProtocol_GmLinkFlag) {
        pushBack_PtrArray(&d->visibleLinks, run);
    }
}

static float normScrollPos_DocumentWidget_(const iDocumentWidget *d) {
    const int docSize = size_GmDocument(d->doc).y;
    if (docSize) {
        return (float) d->scrollY / (float) docSize;
    }
    return 0;
}

static int scrollMax_DocumentWidget_(const iDocumentWidget *d) {
    return size_GmDocument(d->doc).y - height_Rect(bounds_Widget(constAs_Widget(d))) +
           (hasSiteBanner_GmDocument(d->doc) ? 1 : 2) * d->pageMargin * gap_UI;
}

static void updateHover_DocumentWidget_(iDocumentWidget *d, iInt2 mouse) {
    const iWidget *w            = constAs_Widget(d);
    const iRect    docBounds    = documentBounds_DocumentWidget_(d);
    const iGmRun * oldHoverLink = d->hoverLink;
    d->hoverLink                = NULL;
    const iInt2 hoverPos        = addY_I2(sub_I2(mouse, topLeft_Rect(docBounds)), d->scrollY);
    if (isHover_Widget(w) && !d->noHoverWhileScrolling &&
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
        refresh_Widget(as_Widget(d));
    }
    if (isHover_Widget(w) && !contains_Widget(constAs_Widget(d->scroll), mouse)) {
        setCursor_Window(get_Window(),
                         d->hoverLink ? SDL_SYSTEM_CURSOR_HAND : SDL_SYSTEM_CURSOR_IBEAM);
    }
}

static void updateVisible_DocumentWidget_(iDocumentWidget *d) {
    const iRangei visRange = visibleRange_DocumentWidget_(d);
    const iRect   bounds   = bounds_Widget(as_Widget(d));
    setRange_ScrollWidget(d->scroll, (iRangei){ 0, scrollMax_DocumentWidget_(d) });
    const int docSize = size_GmDocument(d->doc).y;
    setThumb_ScrollWidget(d->scroll,
                          d->scrollY,
                          docSize > 0 ? height_Rect(bounds) * size_Range(&visRange) / docSize : 0);
    clear_PtrArray(&d->visibleLinks);
    render_GmDocument(d->doc, visRange, addVisibleLink_DocumentWidget_, d);
    updateHover_DocumentWidget_(d, mouseCoord_Window(get_Window()));
    /* Remember scroll positions of recently visited pages. */ {
        iRecentUrl *recent = mostRecentUrl_History(d->mod.history);
        if (recent && docSize && d->state == ready_RequestState) {
            recent->normScrollY = normScrollPos_DocumentWidget_(d);
        }
    }
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
        if (!isEmpty_Range(&parts.host)) {
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

static void invalidate_DocumentWidget_(iDocumentWidget *d) {
    iZap(d->visBuffer->validRange);
}

static void setSource_DocumentWidget_(iDocumentWidget *d, const iString *source) {
    setUrl_GmDocument(d->doc, d->mod.url);
    setSource_GmDocument(d->doc, source, documentWidth_DocumentWidget_(d));
    d->foundMark   = iNullRange;
    d->selectMark  = iNullRange;
    d->hoverLink   = NULL;
    d->contextLink = NULL;
    updateWindowTitle_DocumentWidget_(d);
    updateVisible_DocumentWidget_(d);
    invalidate_DocumentWidget_(d);
    refresh_Widget(as_Widget(d));
}

static void showErrorPage_DocumentWidget_(iDocumentWidget *d, enum iGmStatusCode code) {
    iString *src = collectNewCStr_String("# ");
    const iGmError *msg = get_GmError(code);
    appendChar_String(src, msg->icon ? msg->icon : 0x2327); /* X in a box */
    appendFormat_String(src, " %s\n%s", msg->title, msg->info);
    switch (code) {
        case failedToOpenFile_GmStatusCode:
        case certificateNotValid_GmStatusCode:
            appendFormat_String(src, "\n\n%s", cstr_String(meta_GmRequest(d->request)));
            break;
        case unsupportedMimeType_GmStatusCode:
            appendFormat_String(src, "\n```\n%s\n```\n", cstr_String(meta_GmRequest(d->request)));
            break;
        case slowDown_GmStatusCode:
            appendFormat_String(src, "\n\nWait %s seconds before your next request.",
                                cstr_String(meta_GmRequest(d->request)));
            break;
        default:
            break;
    }
    setSource_DocumentWidget_(d, src);
    resetSmoothScroll_DocumentWidget_(d);
    d->scrollY = 0;
    d->state = ready_RequestState;
}

static void updateTheme_DocumentWidget_(iDocumentWidget *d) {
    if (isEmpty_String(d->titleUser)) {
        setThemeSeed_GmDocument(d->doc,
                                collect_Block(newRange_Block(urlHost_String(d->mod.url))));
    }
    else {
        setThemeSeed_GmDocument(d->doc, &d->titleUser->chars);
    }
}

static void updateDocument_DocumentWidget_(iDocumentWidget *d, const iGmResponse *response) {
    if (d->state == ready_RequestState) {
        return;
    }
    /* TODO: Do this in the background. However, that requires a text metrics calculator
       that does not try to cache the glyph bitmaps. */
    const enum iGmStatusCode statusCode = response->statusCode;
    if (category_GmStatusCode(statusCode) != categoryInput_GmStatusCode) {
        iString str;
        invalidate_DocumentWidget_(d);
        updateTheme_DocumentWidget_(d);
        initBlock_String(&str, &response->body);
        if (category_GmStatusCode(statusCode) == categorySuccess_GmStatusCode) {
            /* Check the MIME type. */
            iRangecc charset = range_CStr("utf-8");
            enum iGmDocumentFormat docFormat = undefined_GmDocumentFormat;
            const iString *mimeStr = collect_String(lower_String(&response->meta)); /* for convenience */
            iRangecc mime = range_String(mimeStr);
            iRangecc seg = iNullRange;
            while (nextSplit_Rangecc(mime, ";", &seg)) {
                iRangecc param = seg;
                trim_Rangecc(&param);
                if (equal_Rangecc(param, "text/plain")) {
                    docFormat = plainText_GmDocumentFormat;
                }
                else if (equal_Rangecc(param, "text/gemini")) {
                    docFormat = gemini_GmDocumentFormat;
                }
                else if (startsWith_Rangecc(param, "image/")) {
                    docFormat = gemini_GmDocumentFormat;
                    if (!d->request || isFinished_GmRequest(d->request)) {
                        /* Make a simple document with an image. */
                        const char *imageTitle = "Image";
                        iUrl parts;
                        init_Url(&parts, d->mod.url);
                        if (!isEmpty_Range(&parts.path)) {
                            imageTitle =
                                baseName_Path(collect_String(newRange_String(parts.path))).start;
                        }
                        format_String(
                            &str, "=> %s %s\n", cstr_String(d->mod.url), imageTitle);
                        setImage_GmDocument(d->doc, 1, mimeStr, &response->body);
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
                showErrorPage_DocumentWidget_(d, unsupportedMimeType_GmStatusCode);
                deinit_String(&str);
                return;
            }
            /* Convert the source to UTF-8 if needed. */
            if (!equalCase_Rangecc(charset, "utf-8")) {
                set_String(&str,
                           collect_String(decode_Block(&str.chars, cstr_Rangecc(charset))));
            }
        }
        setSource_DocumentWidget_(d, &str);
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
#define openLock_CStr   "\U0001f513"
#define closedLock_CStr "\U0001f512"
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

iHistory *history_DocumentWidget(iDocumentWidget *d) {
    return d->mod.history;
}

const iString *url_DocumentWidget(const iDocumentWidget *d) {
    return d->mod.url;
}

const iGmDocument *document_DocumentWidget(const iDocumentWidget *d) {
    return d->doc;
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
        updateDocument_DocumentWidget_(d, resp);
        d->scrollY = d->initNormScrollY * size_GmDocument(d->doc).y;
        d->state = ready_RequestState;
        updateVisible_DocumentWidget_(d);
        postCommandf_App("document.changed doc:%p url:%s", d, cstr_String(d->mod.url));
        return iTrue;
    }
    else if (!isEmpty_String(d->mod.url)) {
        fetch_DocumentWidget_(d);
    }
    return iFalse;
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

iBool isRequestOngoing_DocumentWidget(const iDocumentWidget *d) {
    return d->state == fetching_RequestState || d->state == receivedPartialResponse_RequestState;
}

static void scroll_DocumentWidget_(iDocumentWidget *d, int offset) {
    d->scrollY += offset;
    if (d->scrollY < 0) {
        d->scrollY = 0;
    }
    const int scrollMax = scrollMax_DocumentWidget_(d);
    if (scrollMax > 0) {
        d->scrollY = iMin(d->scrollY, scrollMax);
    }
    else {
        d->scrollY = 0;
    }
    updateVisible_DocumentWidget_(d);
    refresh_Widget(as_Widget(d));
}

static void doScroll_DocumentWidget_(iAny *ptr) {
    iDocumentWidget *d = ptr;
    if (!d->smoothScroll) return; /* was cancelled */
    const double elapsed = (double) elapsedSinceLastTicker_App() / 1000.0;
    int delta = d->smoothSpeed * elapsed * iSign(d->smoothScroll);
    if (iAbs(d->smoothScroll) <= iAbs(delta)) {
        if (d->smoothContinue) {
            d->smoothScroll += d->smoothLastOffset;
        }
        else {
            delta = d->smoothScroll;
        }
    }
    scroll_DocumentWidget_(d, delta);
    d->smoothScroll -= delta;
    if (d->smoothScroll != 0) {
        addTicker_App(doScroll_DocumentWidget_, d);
    }
}

static void smoothScroll_DocumentWidget_(iDocumentWidget *d, int offset, int speed) {
    if (speed == 0) {
        scroll_DocumentWidget_(d, offset);
        return;
    }
    d->smoothSpeed = speed;
    d->smoothScroll += offset;
    d->smoothLastOffset = offset;
    addTicker_App(doScroll_DocumentWidget_, d);
}

static void scrollTo_DocumentWidget_(iDocumentWidget *d, int documentY, iBool centered) {
    d->scrollY = documentY - (centered ? documentBounds_DocumentWidget_(d).size.y / 2 :
                                       lineHeight_Text(paragraph_FontId));
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
                d->scrollY = 0;
                resetSmoothScroll_DocumentWidget_(d);
                reset_GmDocument(d->doc); /* new content incoming */
                updateDocument_DocumentWidget_(d, response_GmRequest(d->request));
                break;
            case categoryRedirect_GmStatusCode:
                if (isEmpty_String(meta_GmRequest(d->request))) {
                    showErrorPage_DocumentWidget_(d, invalidRedirect_GmStatusCode);
                }
                else {
                    /* TODO: only accept redirects that use gemini scheme */
                    postCommandf_App(
                        "open redirect:1 url:%s",
                        cstr_String(absoluteUrl_String(d->mod.url, meta_GmRequest(d->request))));
                    iReleasePtr(&d->request);
                }
                break;
            default:
                if (isDefined_GmError(statusCode)) {
                    showErrorPage_DocumentWidget_(d, statusCode);
                }
                else if (category_GmStatusCode(statusCode) ==
                         categoryTemporaryFailure_GmStatusCode) {
                    showErrorPage_DocumentWidget_(d, temporaryFailure_GmStatusCode);
                }
                else if (category_GmStatusCode(statusCode) ==
                         categoryPermanentFailure_GmStatusCode) {
                    showErrorPage_DocumentWidget_(d, permanentFailure_GmStatusCode);
                }
                break;
        }
    }
    else if (d->state == receivedPartialResponse_RequestState) {
        switch (category_GmStatusCode(statusCode)) {
            case categorySuccess_GmStatusCode:
                /* More content available. */
                updateDocument_DocumentWidget_(d, response_GmRequest(d->request));
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
    if (!req || req->doc != d) {
        return iFalse; /* not our request */
    }
    if (equal_Command(cmd, "media.updated")) {
        /* TODO: Show a progress indicator */
        return iTrue;
    }
    else if (equal_Command(cmd, "media.finished")) {
        const enum iGmStatusCode code = status_GmRequest(req->req);
        /* Give the media to the document for presentation. */
        if (code == success_GmStatusCode) {
//            printf("media finished: %s\n  size: %zu\n  type: %s\n",
//                   cstr_String(url_GmRequest(req->req)),
//                   size_Block(body_GmRequest(req->req)),
//                   cstr_String(meta_GmRequest(req->req)));
            if (startsWith_String(meta_GmRequest(req->req), "image/")) {
                setImage_GmDocument(d->doc, req->linkId, meta_GmRequest(req->req),
                                    body_GmRequest(req->req));
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

static void deallocVisBuffer_DocumentWidget_(const iDocumentWidget *d) {
    d->visBuffer->size = zero_I2();
    iZap(d->visBuffer->validRange);
    iForIndices(i, d->visBuffer->texture) {
        SDL_DestroyTexture(d->visBuffer->texture[i]);
        d->visBuffer->texture[i] = NULL;
    }
}

static void allocVisBuffer_DocumentWidget_(const iDocumentWidget *d) {
    const iWidget *w         = constAs_Widget(d);
    const iBool    isVisible = isVisible_Widget(w);
    const iInt2    size      = bounds_Widget(w).size;
    if (!isEqual_I2(size, d->visBuffer->size) || !isVisible) {
        dealloc_VisBuffer(d->visBuffer);
    }
    if (isVisible && !d->visBuffer->texture[0]) {
        iZap(d->visBuffer->validRange);
        d->visBuffer->size = size;
        iForIndices(i, d->visBuffer->texture) {
            d->visBuffer->texture[i] =
                SDL_CreateTexture(renderer_Window(get_Window()),
                                  SDL_PIXELFORMAT_RGBA8888,
                                  SDL_TEXTUREACCESS_STATIC | SDL_TEXTUREACCESS_TARGET,
                                  size.x,
                                  size.y);
            SDL_SetTextureBlendMode(d->visBuffer->texture[i], SDL_BLENDMODE_NONE);
        }
    }
}

void updateSize_DocumentWidget(iDocumentWidget *d) {
    setWidth_GmDocument(d->doc, documentWidth_DocumentWidget_(d));
    updateVisible_DocumentWidget_(d);
}

static iBool handleCommand_DocumentWidget_(iDocumentWidget *d, const char *cmd) {
    iWidget *w = as_Widget(d);
    if (equal_Command(cmd, "window.resized") || equal_Command(cmd, "font.changed")) {
        const iGmRun *mid = middleRun_DocumentWidget_(d);
        const char *midLoc = (mid ? mid->text.start : NULL);
        setWidth_GmDocument(d->doc, documentWidth_DocumentWidget_(d));
        scroll_DocumentWidget_(d, 0);
        updateVisible_DocumentWidget_(d);
        if (midLoc) {
            mid = findRunAtLoc_GmDocument(d->doc, midLoc);
            if (mid) {
                scrollTo_DocumentWidget_(d, mid_Rect(mid->bounds).y, iTrue);
            }
        }
        invalidate_DocumentWidget_(d);
        allocVisBuffer_DocumentWidget_(d);
        refresh_Widget(w);
        updateWindowTitle_DocumentWidget_(d);
    }
    else if (equal_Command(cmd, "theme.changed") && document_App() == d) {
        updateTheme_DocumentWidget_(d);
        invalidate_DocumentWidget_(d);
        refresh_Widget(w);
    }
    else if (equal_Command(cmd, "tabs.changed")) {
        d->showLinkNumbers = iFalse;
        if (cmp_String(id_Widget(w), suffixPtr_Command(cmd, "id")) == 0) {
            /* Set palette for our document. */
            updateTheme_DocumentWidget_(d);
            updateTrust_DocumentWidget_(d, NULL);
            setWidth_GmDocument(d->doc, documentWidth_DocumentWidget_(d));
            updateVisible_DocumentWidget_(d);
        }
        updateWindowTitle_DocumentWidget_(d);
        allocVisBuffer_DocumentWidget_(d);
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
    else if (equal_Command(cmd, "copy") && document_App() == d) {
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
    else if (equal_Command(cmd, "document.input.submit")) {
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
             cmp_String(string_Command(cmd, "id"), "document.input.submit") == 0) {
        postCommand_App("navigate.back");
        return iTrue;
    }
    else if (equalWidget_Command(cmd, w, "document.request.updated") &&
             pointerLabel_Command(cmd, "request") == d->request) {
        checkResponse_DocumentWidget_(d);
        return iFalse;
    }
    else if (equalWidget_Command(cmd, w, "document.request.finished") &&
             pointerLabel_Command(cmd, "request") == d->request) {
        checkResponse_DocumentWidget_(d);
        resetSmoothScroll_DocumentWidget_(d);
        d->scrollY = d->initNormScrollY * size_GmDocument(d->doc).y;
        d->state = ready_RequestState;
        /* The response may be cached. */ {
            if (!equal_Rangecc(urlScheme_String(d->mod.url), "about")) {
                setCachedResponse_History(d->mod.history, response_GmRequest(d->request));
            }
        }
        iReleasePtr(&d->request);
        updateVisible_DocumentWidget_(d);
        postCommandf_App("document.changed url:%s", cstr_String(d->mod.url));
        return iFalse;
    }
    else if (equal_Command(cmd, "document.request.timeout") &&
             pointerLabel_Command(cmd, "request") == d->request) {
        cancel_GmRequest(d->request);
        return iFalse;
    }
    else if (equal_Command(cmd, "document.request.cancelled") && document_Command(cmd) == d) {
        postCommand_App("navigate.back");
        return iFalse;
    }
    else if (equal_Command(cmd, "document.stop")) {
        if (d->request) {
            postCommandf_App("document.request.cancelled doc:%p url:%s", d, cstr_String(d->mod.url));
            iReleasePtr(&d->request);
            d->state = ready_RequestState;
            return iTrue;
        }
    }
    else if (equal_Command(cmd, "media.updated") || equal_Command(cmd, "media.finished")) {
        return handleMediaCommand_DocumentWidget_(d, cmd);
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
        d->scrollY = arg_Command(cmd);
        resetSmoothScroll_DocumentWidget_(d);
        updateVisible_DocumentWidget_(d);
        return iTrue;
    }
    else if (equalWidget_Command(cmd, w, "scroll.page")) {
        if (argLabel_Command(cmd, "repeat")) {
            if (!d->smoothContinue) {
                d->smoothContinue = iTrue;
            }
            else {
                return iTrue;
            }
        }
        smoothScroll_DocumentWidget_(d,
                                     arg_Command(cmd) *
                                         (0.5f * height_Rect(documentBounds_DocumentWidget_(d)) -
                                          0 * lineHeight_Text(paragraph_FontId)),
                                     25 * smoothSpeed_DocumentWidget_);
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
                    d->showLinkNumbers = iFalse;
                    invalidate_DocumentWidget_(d);
                    refresh_Widget(w);
                }
                break;
            case SDLK_PAGEUP:
            case SDLK_PAGEDOWN:
            case SDLK_SPACE:
            case SDLK_UP:
            case SDLK_DOWN:
                d->smoothContinue = iFalse;
                break;
        }
    }
    if (ev->type == SDL_KEYDOWN) {
        const int mods = keyMods_Sym(ev->key.keysym.mod);
        const int key = ev->key.keysym.sym;
        if (d->showLinkNumbers && ((key >= '1' && key <= '9') ||
                                   (key >= 'a' && key <= 'z'))) {
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
                    d->showLinkNumbers = iTrue;
                    invalidate_DocumentWidget_(d);
                    refresh_Widget(w);
                }
                break;
            case SDLK_HOME:
                d->scrollY = 0;
                resetSmoothScroll_DocumentWidget_(d);
                scroll_DocumentWidget_(d, 0);
                updateVisible_DocumentWidget_(d);
                refresh_Widget(w);
                return iTrue;
            case SDLK_END:
                d->scrollY = scrollMax_DocumentWidget_(d);
                resetSmoothScroll_DocumentWidget_(d);
                scroll_DocumentWidget_(d, 0);
                updateVisible_DocumentWidget_(d);
                refresh_Widget(w);
                return iTrue;
            case SDLK_UP:
            case SDLK_DOWN:
                if (mods == 0) {
                    if (ev->key.repeat) {
                        if (!d->smoothContinue) {
                            d->smoothContinue = iTrue;
                        }
                        else return iTrue;
                    }
                    smoothScroll_DocumentWidget_(d,
                                                 3 * lineHeight_Text(paragraph_FontId) *
                                                     (key == SDLK_UP ? -1 : 1),
                                                 gap_Text * smoothSpeed_DocumentWidget_);
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
            case SDLK_KP_1: {
                iBlock *seed = new_Block(64);
                for (size_t i = 0; i < 64; ++i) {
                    setByte_Block(seed, i, iRandom(0, 255));
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
#if defined (iPlatformApple)
        /* Momentum scrolling. */
        scroll_DocumentWidget_(d, -ev->wheel.y * get_Window()->pixelRatio);
#else
        if (keyMods_Sym(SDL_GetModState()) == KMOD_PRIMARY) {
            postCommandf_App("zoom.delta arg:%d", ev->wheel.y > 0 ? 10 : -10);
            return iTrue;
        }
        scroll_DocumentWidget_(d, -3 * ev->wheel.y * lineHeight_Text(default_FontId));
#endif
        d->noHoverWhileScrolling = iTrue;
        return iTrue;
    }
    else if (ev->type == SDL_MOUSEMOTION) {
        d->noHoverWhileScrolling = iFalse;
        if (isVisible_Widget(d->menu)) {
            setCursor_Window(get_Window(), SDL_SYSTEM_CURSOR_ARROW);
        }
        else {
            updateHover_DocumentWidget_(d, init_I2(ev->motion.x, ev->motion.y));
        }
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
    }
    if (!isVisible_Widget(d->menu)) {
        d->contextLink = d->hoverLink;
    }
    processContextMenuEvent_Widget(d->menu, ev, d->hoverLink = NULL);
    switch (processEvent_Click(&d->click, ev)) {
        case started_ClickResult:
            d->selecting = iFalse;
            return iTrue;
        case drag_ClickResult: {
            /* Begin selecting a range of text. */
            if (!d->selecting) {
                d->selecting = iTrue;
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
            refresh_Widget(w);
            return iTrue;
        }
        case finished_ClickResult:
            if (isVisible_Widget(d->menu)) {
                closeMenu_Widget(d->menu);
            }
            if (!isMoved_Click(&d->click)) {
                if (d->hoverLink) {
                    const iGmLinkId linkId = d->hoverLink->linkId;
                    iAssert(linkId);
                    /* Media links are opened inline by default. */
                    if (isMediaLink_GmDocument(d->doc, linkId)) {
                        if (!requestMedia_DocumentWidget_(d, linkId)) {
                            if (linkFlags_GmDocument(d->doc, linkId) & content_GmLinkFlag) {
                                /* Dismiss shown content on click. */
                                setImage_GmDocument(d->doc, linkId, NULL, NULL);
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
                                    setImage_GmDocument(d->doc, linkId, meta_GmRequest(req->req),
                                                        body_GmRequest(req->req));
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
            return iTrue;
        default:
            break;
    }
    return processEvent_Widget(w, ev);
}

iDeclareType(DrawContext)

enum iDrawRunPass {
    static_DrawRunPass,
    dynamic_DrawRunPass,
};

struct Impl_DrawContext {
    enum iDrawRunPass pass;
    const iDocumentWidget *widget;
    iRect widgetBounds;
    iRect bounds; /* document area */
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
    if ((!*isInside && contains_Range(&run->text, mark.start)) || *isInside) {
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
        const iInt2 visPos = add_I2(run->bounds.pos, addY_I2(d->bounds.pos, -d->widget->scrollY));
        fillRect_Paint(&d->paint, (iRect){ addX_I2(visPos, x),
                                           init_I2(w, height_Rect(run->bounds)) }, color);
    }
}

static void drawRun_DrawContext_(void *context, const iGmRun *run) {
    iDrawContext *d      = context;
    const iInt2   origin = addY_I2(d->bounds.pos, -d->widget->scrollY);
    if (run->imageId) {
        if (d->pass == static_DrawRunPass) {
            SDL_Texture *tex = imageTexture_GmDocument(d->widget->doc, run->imageId);
            if (tex) {
                const iRect dst = moved_Rect(run->visBounds, origin);
                SDL_RenderCopy(d->paint.dst->render, tex, NULL,
                               &(SDL_Rect){ dst.pos.x, dst.pos.y, dst.size.x, dst.size.y });
            }
        }
        return;
    }
    /* Text markers. */
    if (d->pass == dynamic_DrawRunPass) {
        SDL_SetRenderDrawBlendMode(renderer_Window(get_Window()),
                                   isDark_ColorTheme(colorTheme_App()) ? SDL_BLENDMODE_ADD
                                                                       : SDL_BLENDMODE_BLEND);
        fillRange_DrawContext_(d, run, uiMatching_ColorId, d->widget->foundMark, &d->inFoundMark);
        fillRange_DrawContext_(d, run, uiMarked_ColorId, d->widget->selectMark, &d->inSelectMark);
        SDL_SetRenderDrawBlendMode(renderer_Window(get_Window()), SDL_BLENDMODE_NONE);
    }
    enum iColorId      fg  = run->color;
    const iGmDocument *doc = d->widget->doc;
    /* Matches the current drawing pass? */
    const iBool isDynamic = (run->linkId && ~run->flags & decoration_GmRunFlag);
    if (isDynamic ^ (d->pass == dynamic_DrawRunPass)) {
        return;
    }
    const iBool isHover =
        (run->linkId && d->widget->hoverLink && run->linkId == d->widget->hoverLink->linkId &&
         ~run->flags & decoration_GmRunFlag);
    const iInt2 visPos = add_I2(run->visBounds.pos, origin);
    if (run->linkId && ~run->flags & decoration_GmRunFlag) {
        fg = linkColor_GmDocument(doc, run->linkId, isHover ? textHover_GmLinkPart : text_GmLinkPart);
        if (linkFlags_GmDocument(doc, run->linkId) & content_GmLinkFlag) {
            fg = linkColor_GmDocument(doc, run->linkId, textHover_GmLinkPart); /* link is inactive */
        }
    }
    if (run->flags & siteBanner_GmRunFlag) {
        if (d->pass == static_DrawRunPass) {
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
                appendChar_String(&bannerText, icon);
                const iRect iconRect = visualBounds_Text(banner_FontId, range_String(&bannerText));
                drawRange_Text(run->font,
                               addY_I2(bpos, -mid_Rect(iconRect).y + lineHeight_Text(run->font) / 2),
                               tmBannerIcon_ColorId,
                               range_String(&bannerText));
                bpos.x += right_Rect(iconRect) + 3 * gap_Text;
            }
            drawRange_Text(run->font,
                           bpos,
                           tmBannerTitle_ColorId,
                           isEmpty_String(d->widget->titleUser) ? run->text
                                                                : range_String(d->widget->titleUser));
            deinit_String(&bannerText);
        }
    }
    else {
        if (d->showLinkNumbers && run->linkId && run->flags & decoration_GmRunFlag) {
            const size_t ord = visibleLinkOrdinal_DocumentWidget_(d->widget, run->linkId);
            if (ord < 9 + 26) {
                const iChar ordChar = ord < 9 ? 0x278a + ord : (0x24b6 + ord - 9);
                drawString_Text(run->font,
                                init_I2(left_Rect(d->bounds) - gap_UI / 2, visPos.y),
                                fg,
                                collect_String(newUnicodeN_String(&ordChar, 1)));
                goto runDrawn;
            }
        }
        drawRange_Text(run->font, visPos, fg, run->text);
//        printf("{%s}\n", cstr_Rangecc(run->text));
    runDrawn:;
    }
    /* Presentation of links. */
    if (run->linkId && ~run->flags & decoration_GmRunFlag && d->pass == dynamic_DrawRunPass) {
        const int metaFont = paragraph_FontId;
        /* TODO: Show status of an ongoing media request. */
        const int flags = linkFlags_GmDocument(doc, run->linkId);
        const iRect linkRect = moved_Rect(run->visBounds, origin);
        iMediaRequest *mr = NULL;
        /* Show inline content. */
        if (flags & content_GmLinkFlag) {
            fg = linkColor_GmDocument(doc, run->linkId, textHover_GmLinkPart);
            iAssert(!isEmpty_Rect(run->bounds));
            iGmImageInfo info;
            imageInfo_GmDocument(doc, linkImage_GmDocument(doc, run->linkId), &info);
            iString text;
            init_String(&text);
            format_String(&text, "%s \u2014 %d x %d \u2014 %.1fMB",
                          info.mime, info.size.x, info.size.y, info.numBytes / 1.0e6f);
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
                          " \u2014 Fetching\u2026");
            }
        }
        else if (isHover) {
            const iGmLinkId linkId = d->widget->hoverLink->linkId;
            const iString * url    = linkUrl_GmDocument(doc, linkId);
            const int       flags  = linkFlags_GmDocument(doc, linkId);
            iUrl parts;
            init_Url(&parts, url);
            fg = linkColor_GmDocument(doc, linkId, textHover_GmLinkPart);
            const iBool showHost  = (!isEmpty_Range(&parts.host) && flags & userFriendly_GmLinkFlag);
            const iBool showImage = (flags & imageFileExtension_GmLinkFlag) != 0;
            const iBool showAudio = (flags & audioFileExtension_GmLinkFlag) != 0;
            iString str;
            init_String(&str);
            if (run->flags & endOfLine_GmRunFlag &&
                (flags & (imageFileExtension_GmLinkFlag | audioFileExtension_GmLinkFlag) ||
                 showHost)) {
                format_String(
                    &str,
                    " \u2014%s%s%s\r%c%s",
                    showHost ? " " : "",
                    showHost ? (!equalCase_Rangecc(parts.scheme, "gemini")
                                    ? format_CStr("%s://%s",
                                                  cstr_Rangecc(parts.scheme),
                                                  cstr_Rangecc(parts.host))
                                    : cstr_Rangecc(parts.host))
                             : "",
                    showHost && (showImage || showAudio) ? " \u2014" : "",
                    showImage || showAudio
                        ? '0' + fg
                        : ('0' + linkColor_GmDocument(doc, run->linkId, domain_GmLinkPart)),
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

static iRangei intersect_Rangei_(iRangei a, iRangei b) {
    if (a.end < b.start || a.start > b.end) {
        return (iRangei){ 0, 0 };
    }
    return (iRangei){ iMax(a.start, b.start), iMin(a.end, b.end) };
}

iLocalDef iBool isEmpty_Rangei_(iRangei d) {
    return size_Range(&d) == 0;
}

static void draw_DocumentWidget_(const iDocumentWidget *d) {
    const iWidget *w        = constAs_Widget(d);
    const iRect    bounds   = bounds_Widget(w);
    const iInt2    origin   = topLeft_Rect(bounds);
    const iRangei  visRange = visibleRange_DocumentWidget_(d);
    iVisBuffer *   visBuf   = d->visBuffer; /* this may be updated/modified here */
    draw_Widget(w);
    allocVisBuffer_DocumentWidget_(d);
    iDrawContext ctxDynamic = {
        .pass         = dynamic_DrawRunPass,
        .widget       = d,
        .widgetBounds = adjusted_Rect(bounds,
            zero_I2(),
            init_I2(-constAs_Widget(d->scroll)->rect.size.x, 0)), /* omit scrollbar width */
        .bounds          = documentBounds_DocumentWidget_(d),
        .showLinkNumbers = d->showLinkNumbers,
    };
    iDrawContext ctxStatic = ctxDynamic;
    ctxStatic.pass = static_DrawRunPass;
    subv_I2(&ctxStatic.widgetBounds.pos, origin);
    subv_I2(&ctxStatic.bounds.pos, origin);
    SDL_Renderer *render = get_Window()->render;
    /* Static content. */ {
        iPaint *p = &ctxStatic.paint;
        init_Paint(p);
        const int vbSrc = visBuf->index;
        const int vbDst = visBuf->index ^ 1;
        iRangei drawRange = visRange;
        iAssert(visBuf->texture[vbDst]);
        beginTarget_Paint(p, visBuf->texture[vbDst]);
        const iRect visBufferRect = { zero_I2(), visBuf->size };
        iRect drawRect = visBufferRect;
        if (!isEmpty_Rangei_(intersect_Rangei_(visRange, visBuf->validRange))) {
            if (visRange.start < visBuf->validRange.start) {
                drawRange = (iRangei){ visRange.start, visBuf->validRange.start };
            }
            else {
                drawRange = (iRangei){ visBuf->validRange.end, visRange.end };
            }
            if (isEmpty_Range(&drawRange)) {
                SDL_RenderCopy(render, visBuf->texture[vbSrc], NULL, NULL);
            }
            else {
                SDL_RenderCopy(
                    render,
                    visBuf->texture[vbSrc],
                    NULL,
                    &(SDL_Rect){ 0,
                                 documentToWindowY_DocumentWidget_(d, visBuf->validRange.start) -
                                     origin.y,
                                 visBuf->size.x,
                                 visBuf->size.y });
                drawRect =
                    init_Rect(0,
                              documentToWindowY_DocumentWidget_(d, drawRange.start) - origin.y,
                              visBuf->size.x,
                              size_Range(&drawRange));
            }
        }
        if (!isEmpty_Range(&drawRange)) {
            setClip_Paint(p, drawRect);
            fillRect_Paint(p, drawRect, tmBackground_ColorId); // vbDst == 1 ? blue_ColorId : red_ColorId
            render_GmDocument(d->doc, drawRange, drawRun_DrawContext_, &ctxStatic);
            unsetClip_Paint(p);
        }
        endTarget_Paint(p);
        SDL_RenderCopy(render, visBuf->texture[vbDst], NULL,
                       &(SDL_Rect){ origin.x, origin.y, bounds.size.x, bounds.size.y } );
        visBuf->validRange = visRange;
        visBuf->index = vbDst;
    }
    /* Dynamic content. */ {
        extern int enableKerning_Text;
        enableKerning_Text = iFalse; /* need to be fast, these is redone on every redraw */
        iPaint *p = &ctxDynamic.paint;
        init_Paint(p);
        setClip_Paint(p, bounds);
        render_GmDocument(d->doc, visRange, drawRun_DrawContext_, &ctxDynamic);
        unsetClip_Paint(p);
        enableKerning_Text = iTrue;
    }

//    drawRect_Paint(&ctx.paint,
//                   moved_Rect((iRect){ zero_I2(), size_GmDocument(d->doc) },
//                              add_I2(topLeft_Rect(ctx.bounds), init_I2(0, -d->scrollY))),
//                   green_ColorId);
    draw_Widget(w);
}

iBeginDefineSubclass(DocumentWidget, Widget)
    .processEvent = (iAny *) processEvent_DocumentWidget_,
    .draw         = (iAny *) draw_DocumentWidget_,
iEndDefineSubclass(DocumentWidget)
