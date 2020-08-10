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

enum iDocumentState {
    blank_DocumentState,
    fetching_DocumentState,
    receivedPartialResponse_DocumentState,
    layout_DocumentState,
    ready_DocumentState,
};

iDeclareClass(MediaRequest)

struct Impl_MediaRequest {
    iObject object;
    iDocumentWidget *doc;
    iGmLinkId linkId;
    iGmRequest *req;
    iAtomicInt isUpdated;
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

struct Impl_DocumentWidget {
    iWidget widget;
    iHistory *history;
    enum iDocumentState state;
    iString *url;
    iString *titleUser;
    iGmRequest *request;
    iAtomicInt isRequestUpdated; /* request has new content, need to parse it */
    iObjectList *media;
    int textSizePercent;
    iGmDocument *doc;
    int certFlags;
    iDate certExpiry;
    iString *certSubject;
    iBool selecting;
    iRangecc selectMark;
    iRangecc foundMark;
    int pageMargin;
    int scrollY;
    iPtrArray visibleLinks;
    const iGmRun *hoverLink;
    iBool noHoverWhileScrolling;
    iClick click;
    int initialScrollY;
    iScrollWidget *scroll;
    iWidget *menu;
    SDL_Cursor *arrowCursor; /* TODO: cursors belong in Window */
    SDL_Cursor *beamCursor;
    SDL_Cursor *handCursor;
};

iDefineObjectConstruction(DocumentWidget)

void init_DocumentWidget(iDocumentWidget *d) {
    iWidget *w = as_Widget(d);
    init_Widget(w);
    setId_Widget(w, "document000");
    iZap(d->certExpiry);
    d->history          = new_History();
    d->state            = blank_DocumentState;
    d->url              = new_String();
    d->titleUser        = new_String();
    d->request          = NULL;
    d->isRequestUpdated = iFalse;
    d->media            = new_ObjectList();
    d->textSizePercent  = 100;
    d->doc              = new_GmDocument();
    d->certFlags        = 0;
    d->certSubject      = new_String();
    d->selecting        = iFalse;
    d->selectMark       = iNullRange;
    d->foundMark        = iNullRange;
    d->pageMargin       = 5;
    d->scrollY          = 0;
    d->hoverLink        = NULL;
    d->noHoverWhileScrolling = iFalse;
    d->arrowCursor      = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_ARROW);
    d->beamCursor       = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_IBEAM);
    d->handCursor       = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_HAND);
    init_PtrArray(&d->visibleLinks);
    init_Click(&d->click, d, SDL_BUTTON_LEFT);
    addChild_Widget(w, iClob(d->scroll = new_ScrollWidget()));
    d->menu =
        makeMenu_Widget(w,
                        (iMenuItem[]){ { "Back", SDLK_LEFT, KMOD_PRIMARY, "navigate.back" },
                                       { "Forward", SDLK_RIGHT, KMOD_PRIMARY, "navigate.forward" },
                                       { "Reload", 'r', KMOD_PRIMARY, "navigate.reload" },
                                       { "---", 0, 0, NULL },
                                       { "Copy", 'c', KMOD_PRIMARY, "copy" },
                                       { "Copy Link", 0, 0, "document.copylink" } },
                        6);
#if !defined (iPlatformApple) /* in system menu */
    addAction_Widget(w, SDLK_w, KMOD_PRIMARY, "tabs.close");
#endif
}

void deinit_DocumentWidget(iDocumentWidget *d) {
    iRelease(d->media);
    iRelease(d->request);
    iRelease(d->doc);
    deinit_PtrArray(&d->visibleLinks);
    delete_String(d->url);
    delete_String(d->certSubject);
    delete_String(d->titleUser);
    SDL_FreeCursor(d->arrowCursor);
    SDL_FreeCursor(d->beamCursor);
    SDL_FreeCursor(d->handCursor);
    delete_History(d->history);
}

static int documentWidth_DocumentWidget_(const iDocumentWidget *d) {
    const iWidget *w = constAs_Widget(d);
    const iRect bounds = bounds_Widget(w);
    return iMini(bounds.size.x - gap_UI * d->pageMargin * 2,
                 fontSize_UI * 38 * d->textSizePercent / 100); /* TODO: Add user preference .*/
}

static iRect documentBounds_DocumentWidget_(const iDocumentWidget *d) {
    const iRect bounds = bounds_Widget(constAs_Widget(d));
    const int   margin = gap_UI * d->pageMargin;
    iRect       rect;
    rect.size.x = documentWidth_DocumentWidget_(d);
    rect.pos.x  = bounds.size.x / 2 - rect.size.x / 2;
    rect.pos.y  = top_Rect(bounds);
    rect.size.y = height_Rect(bounds) - margin;
    if (!hasSiteBanner_GmDocument(d->doc)) {
        rect.pos.y += margin;
        rect.size.y -= margin;
    }
    iInt2 docSize = addY_I2(size_GmDocument(d->doc), 0 /*-lineHeight_Text(banner_FontId) * 2*/);
    if (docSize.y < rect.size.y) {
        /* Center vertically if short. */
        int offset = (rect.size.y - docSize.y) / 2;
        rect.pos.y += offset;
        rect.size.y = docSize.y;
    }
    return rect;
}

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
    const int margin = gap_UI * d->pageMargin;
    return (iRangei){ d->scrollY - margin,
                      d->scrollY + height_Rect(bounds_Widget(constAs_Widget(d))) };
}

static void addVisibleLink_DocumentWidget_(void *context, const iGmRun *run) {
    iDocumentWidget *d = context;
    if (run->linkId && linkFlags_GmDocument(d->doc, run->linkId) & supportedProtocol_GmLinkFlag) {
        pushBack_PtrArray(&d->visibleLinks, run);
    }
}

static int scrollMax_DocumentWidget_(const iDocumentWidget *d) {
    return size_GmDocument(d->doc).y - height_Rect(bounds_Widget(constAs_Widget(d))) +
           2 * d->pageMargin * gap_UI;
}

static void updateHover_DocumentWidget_(iDocumentWidget *d, iInt2 mouse) {
    const iRect docBounds      = documentBounds_DocumentWidget_(d);
    const iGmRun *oldHoverLink = d->hoverLink;
    d->hoverLink               = NULL;
    const iInt2 hoverPos = addY_I2(sub_I2(mouse, topLeft_Rect(docBounds)), d->scrollY);
    if (!d->noHoverWhileScrolling &&
        (d->state == ready_DocumentState || d->state == receivedPartialResponse_DocumentState)) {
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
    if (!contains_Widget(constAs_Widget(d), mouse) ||
        contains_Widget(constAs_Widget(d->scroll), mouse)) {
        SDL_SetCursor(d->arrowCursor);
    }
    else {
        SDL_SetCursor(d->hoverLink ? d->handCursor : d->beamCursor);
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
        iRecentUrl *recent = mostRecentUrl_History(d->history);
        if (recent) {
            recent->scrollY = d->scrollY / gap_UI;
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
        init_Url(&parts, d->url);
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

static void setSource_DocumentWidget_(iDocumentWidget *d, const iString *source) {
    setUrl_GmDocument(d->doc, d->url);
    setSource_GmDocument(d->doc, source, documentWidth_DocumentWidget_(d));
    d->foundMark  = iNullRange;
    d->selectMark = iNullRange;
    d->hoverLink  = NULL;
    updateWindowTitle_DocumentWidget_(d);
    updateVisible_DocumentWidget_(d);
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
    d->scrollY = 0;
    d->state = ready_DocumentState;
}

static void updateTheme_DocumentWidget_(iDocumentWidget *d) {
    if (isEmpty_String(d->titleUser)) {
        setThemeSeed_GmDocument(d->doc,
                                collect_Block(newRange_Block(urlHost_String(d->url))));
    }
    else {
        setThemeSeed_GmDocument(d->doc, &d->titleUser->chars);
    }
}

static void updateDocument_DocumentWidget_(iDocumentWidget *d, const iGmResponse *response) {
    if (d->state == ready_DocumentState) {
        return;
    }
    /* TODO: Do this in the background. However, that requires a text metrics calculator
       that does not try to cache the glyph bitmaps. */
    const enum iGmStatusCode statusCode = response->statusCode;
    if (category_GmStatusCode(statusCode) != categoryInput_GmStatusCode) {
        iString str;
        updateTheme_DocumentWidget_(d);
        initBlock_String(&str, &response->body);
        if (category_GmStatusCode(statusCode) == categorySuccess_GmStatusCode) {
            /* Check the MIME type. */
            iRangecc charset = range_CStr("utf-8");
            enum iGmDocumentFormat docFormat = undefined_GmDocumentFormat;
            const iString *mimeStr = collect_String(lower_String(&response->meta)); /* for convenience */
            iRangecc mime = range_String(mimeStr);
            iRangecc seg = iNullRange;
            while (nextSplit_Rangecc(&mime, ";", &seg)) {
                iRangecc param = seg;
                trim_Rangecc(&param);
                if (equal_Rangecc(&param, "text/plain")) {
                    docFormat = plainText_GmDocumentFormat;
                }
                else if (equal_Rangecc(&param, "text/gemini")) {
                    docFormat = gemini_GmDocumentFormat;
                }
                else if (startsWith_Rangecc(&param, "image/")) {
                    docFormat = gemini_GmDocumentFormat;
                    if (!d->request || isFinished_GmRequest(d->request)) {
                        /* Make a simple document with an image. */
                        const char *imageTitle = "Image";
                        iUrl parts;
                        init_Url(&parts, d->url);
                        if (!isEmpty_Range(&parts.path)) {
                            imageTitle =
                                baseName_Path(collect_String(newRange_String(parts.path))).start;
                        }
                        format_String(
                            &str, "=> %s %s\n", cstr_String(d->url), imageTitle);
                        setImage_GmDocument(d->doc, 1, mimeStr, &response->body);
                    }
                    else {
                        clear_String(&str);
                    }
                }
                else if (startsWith_Rangecc(&param, "charset=")) {
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
            if (!equal_Rangecc(&charset, "utf-8")) {
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
    postCommandf_App("document.request.started doc:%p url:%s", d, cstr_String(d->url));
    clear_ObjectList(d->media);
    d->certFlags = 0;
    d->state = fetching_DocumentState;
    set_Atomic(&d->isRequestUpdated, iFalse);
    d->request = new_GmRequest(certs_App());
    setUrl_GmRequest(d->request, d->url);
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
    return d->history;
}

const iString *url_DocumentWidget(const iDocumentWidget *d) {
    return d->url;
}

void setUrlFromCache_DocumentWidget(iDocumentWidget *d, const iString *url, iBool isFromCache) {
    if (cmpStringSc_String(d->url, url, &iCaseInsensitive)) {
        set_String(d->url, url);
        /* See if there a username in the URL. */ {
            clear_String(d->titleUser);
            iRegExp *userPats[2] = { new_RegExp("~([^/?]+)", 0),
                                     new_RegExp("/users/([^/?]+)", caseInsensitive_RegExpOption) };
            iRegExpMatch m;
            iForIndices(i, userPats) {
                if (matchString_RegExp(userPats[i], d->url, &m)) {
                    setRange_String(d->titleUser, capturedRange_RegExpMatch(&m, 1));
                }
                iRelease(userPats[i]);
            }
        }
        const iRecentUrl *recent = mostRecentUrl_History(d->history);
        if (isFromCache && recent && recent->cachedResponse) {
            const iGmResponse *resp = recent->cachedResponse;
            d->state = fetching_DocumentState;
            /* Use the cached response data. */
            d->scrollY = d->initialScrollY;
            updateTrust_DocumentWidget_(d, resp);
            updateDocument_DocumentWidget_(d, resp);
            d->state = ready_DocumentState;
            postCommandf_App("document.changed url:%s", cstr_String(d->url));
        }
        else {
            fetch_DocumentWidget_(d);
        }
    }
}

iDocumentWidget *duplicate_DocumentWidget(const iDocumentWidget *orig) {
    iDocumentWidget *d = new_DocumentWidget();
    delete_History(d->history);
    d->textSizePercent = orig->textSizePercent;
    d->initialScrollY  = orig->scrollY;
    d->history         = copy_History(orig->history);
    setUrlFromCache_DocumentWidget(d, orig->url, iTrue);
    return d;
}

void setUrl_DocumentWidget(iDocumentWidget *d, const iString *url) {
    setUrlFromCache_DocumentWidget(d, url, iFalse);
}

void setInitialScroll_DocumentWidget (iDocumentWidget *d, int scrollY) {
    d->initialScrollY = scrollY;
}

iBool isRequestOngoing_DocumentWidget(const iDocumentWidget *d) {
    return d->state == fetching_DocumentState || d->state == receivedPartialResponse_DocumentState;
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

static void scrollTo_DocumentWidget_(iDocumentWidget *d, int documentY) {
    d->scrollY = documentY - documentBounds_DocumentWidget_(d).size.y / 2;
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
    if (d->state == fetching_DocumentState) {
        d->state = receivedPartialResponse_DocumentState;
        updateTrust_DocumentWidget_(d, response_GmRequest(d->request));
        switch (category_GmStatusCode(statusCode)) {
            case categoryInput_GmStatusCode: {
                iUrl parts;
                init_Url(&parts, d->url);
                printf("%s\n", cstr_String(meta_GmRequest(d->request)));
                iWidget *dlg = makeValueInput_Widget(
                    as_Widget(d),
                    NULL,
                    format_CStr(cyan_ColorEscape "%s", cstr_Rangecc(parts.host)),
                    isEmpty_String(meta_GmRequest(d->request))
                        ? format_CStr("Please enter input for %s:", cstr_Rangecc(parts.path))
                        : cstr_String(meta_GmRequest(d->request)),
                    orange_ColorEscape "Send \u21d2",
                    "document.input.submit");
                setSensitive_InputWidget(findChild_Widget(dlg, "input"),
                                         statusCode == sensitiveInput_GmStatusCode);
                break;
            }
            case categorySuccess_GmStatusCode:
                d->scrollY = d->initialScrollY;
                reset_GmDocument(d->doc); /* new content incoming */
                updateDocument_DocumentWidget_(d, response_GmRequest(d->request));
                break;
            case categoryRedirect_GmStatusCode:
                if (isEmpty_String(meta_GmRequest(d->request))) {
                    showErrorPage_DocumentWidget_(d, invalidRedirect_GmStatusCode);
                }
                else {
                    /* TODO: only accept redirects that use gemini protocol */
                    postCommandf_App(
                        "open redirect:1 url:%s",
                        cstr_String(absoluteUrl_String(d->url, meta_GmRequest(d->request))));
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
    else if (d->state == receivedPartialResponse_DocumentState) {
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
                d, linkId, absoluteUrl_String(d->url, linkUrl_GmDocument(d->doc, linkId)))));
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
            printf("media finished: %s\n  size: %zu\n  type: %s\n",
                   cstr_String(url_GmRequest(req->req)),
                   size_Block(body_GmRequest(req->req)),
                   cstr_String(meta_GmRequest(req->req)));
            if (startsWith_String(meta_GmRequest(req->req), "image/")) {
                setImage_GmDocument(d->doc, req->linkId, meta_GmRequest(req->req),
                                    body_GmRequest(req->req));
                updateVisible_DocumentWidget_(d);
                refresh_Widget(as_Widget(d));
            }
        }
        else {
            const iGmError *err = get_GmError(code);
            makeMessage_Widget(format_CStr(orange_ColorEscape "%s", err->title), err->info);
            removeMediaRequest_DocumentWidget_(d, req->linkId);
        }
        return iTrue;
    }
    return iFalse;
}

static void changeTextSize_DocumentWidget_(iDocumentWidget *d, int delta) {
    if (delta == 0) {
        d->textSizePercent = 100;
    }
    else {
        if (d->textSizePercent < 100 || (delta < 0 && d->textSizePercent == 100)) {
            delta /= 2;
        }
        d->textSizePercent += delta;
        d->textSizePercent = iClamp(d->textSizePercent, 50, 200);
    }
    postCommandf_App("font.setfactor arg:%d", d->textSizePercent);
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
                scrollTo_DocumentWidget_(d, mid_Rect(mid->bounds).y);
            }
        }
        refresh_Widget(w);
        updateWindowTitle_DocumentWidget_(d);
    }
    else if (equal_Command(cmd, "tabs.changed")) {
        if (cmp_String(id_Widget(w), suffixPtr_Command(cmd, "id")) == 0) {
            /* Set palette for our document. */
            updateTheme_DocumentWidget_(d);
            updateTrust_DocumentWidget_(d, NULL);
            setWidth_GmDocument(d->doc, documentWidth_DocumentWidget_(d));
            updateVisible_DocumentWidget_(d);
        }
        updateWindowTitle_DocumentWidget_(d);
        return iFalse;
    }
    else if (equal_Command(cmd, "server.showcert") && d == document_App()) {
        const char *unchecked = red_ColorEscape   "\u2610";
        const char *checked   = green_ColorEscape "\u2611";
        makeMessage_Widget(
            cyan_ColorEscape "CERTIFICATE STATUS",
            format_CStr("%s%s  Domain name %s%s\n"
                        "%s%s  %s (%04d-%02d-%02d %02d:%02d:%02d)\n"
                        "%s%s  %s",
                        d->certFlags & domainVerified_GmCertFlag ? checked : unchecked,
                        gray75_ColorEscape,
                        d->certFlags & domainVerified_GmCertFlag ? "matches" : "mismatch",
                        ~d->certFlags & domainVerified_GmCertFlag
                            ? format_CStr(" (%s)", cstr_String(d->certSubject))
                            : "",
                        d->certFlags & timeVerified_GmCertFlag ? checked : unchecked,
                        gray75_ColorEscape,
                        d->certFlags & timeVerified_GmCertFlag ? "Not expired" : "Expired",
                        d->certExpiry.year,
                        d->certExpiry.month,
                        d->certExpiry.day,
                        d->certExpiry.hour,
                        d->certExpiry.minute,
                        d->certExpiry.second,
                        d->certFlags & trusted_GmCertFlag ? checked : unchecked,
                        gray75_ColorEscape,
                        d->certFlags & trusted_GmCertFlag ? "Trusted on first use"
                                                          : "Not trusted"));
        return iTrue;
    }
    else if (equal_Command(cmd, "copy")) {
        if (d->selectMark.start) {
            iRangecc mark = d->selectMark;
            if (mark.start > mark.end) {
                iSwap(const char *, mark.start, mark.end);
            }
            iString *copied = newRange_String(mark);
            SDL_SetClipboardText(cstr_String(copied));
            delete_String(copied);
            return iTrue;
        }
    }
    else if (equalWidget_Command(cmd, w, "document.copylink")) {
        if (d->hoverLink) {
            SDL_SetClipboardText(cstr_String(
                absoluteUrl_String(d->url, linkUrl_GmDocument(d->doc, d->hoverLink->linkId))));
        }
        else {
            SDL_SetClipboardText(cstr_String(d->url));
        }
        return iTrue;
    }
    else if (equal_Command(cmd, "document.input.submit")) {
        iString *value = collect_String(suffix_Command(cmd, "value"));
        urlEncode_String(value);
        iString *url = collect_String(copy_String(d->url));
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
        d->state = ready_DocumentState;
        setCachedResponse_History(d->history, response_GmRequest(d->request));
        iReleasePtr(&d->request);
        postCommandf_App("document.changed url:%s", cstr_String(d->url));
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
            postCommandf_App("document.request.cancelled doc:%p url:%s", d, cstr_String(d->url));
            iReleasePtr(&d->request);
            d->state = ready_DocumentState;
            return iTrue;
        }
    }
    else if (equal_Command(cmd, "media.updated") || equal_Command(cmd, "media.finished")) {
        return handleMediaCommand_DocumentWidget_(d, cmd);
    }
    else if (equal_Command(cmd, "document.reload") && document_App() == d) {
        fetch_DocumentWidget_(d);
        return iTrue;
    }
    else if (equal_Command(cmd, "navigate.back") && document_App() == d) {
        goBack_History(d->history);
        return iTrue;
    }
    else if (equal_Command(cmd, "navigate.forward") && document_App() == d) {
        goForward_History(d->history);
        return iTrue;
    }
    else if (equalWidget_Command(cmd, w, "scroll.moved")) {
        d->scrollY = arg_Command(cmd);
        updateVisible_DocumentWidget_(d);
        return iTrue;
    }
    else if (equalWidget_Command(cmd, w, "scroll.page")) {
        scroll_DocumentWidget_(d,
                               arg_Command(cmd) * height_Rect(documentBounds_DocumentWidget_(d)));
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
                    scrollTo_DocumentWidget_(d, mid_Rect(found->bounds).y);
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

static iBool processEvent_DocumentWidget_(iDocumentWidget *d, const SDL_Event *ev) {
    iWidget *w = as_Widget(d);
    if (ev->type == SDL_USEREVENT && ev->user.code == command_UserEventCode) {
        if (!handleCommand_DocumentWidget_(d, command_UserEvent(ev))) {
            /* Base class commands. */
            return processEvent_Widget(w, ev);
        }
        return iTrue;
    }
    if (ev->type == SDL_KEYDOWN) {
        const int mods = keyMods_Sym(ev->key.keysym.mod);
        const int key = ev->key.keysym.sym;
        switch (key) {
            case SDLK_HOME:
                d->scrollY = 0;
                updateVisible_DocumentWidget_(d);
                refresh_Widget(w);
                return iTrue;
            case SDLK_END:
                d->scrollY = scrollMax_DocumentWidget_(d);
                updateVisible_DocumentWidget_(d);
                refresh_Widget(w);
                return iTrue;
            case SDLK_UP:
            case SDLK_DOWN:
                if (mods == 0) {
                    scroll_DocumentWidget_(d, 2 * lineHeight_Text(default_FontId) *
                                                  (key == SDLK_UP ? -1 : 1));
                    return iTrue;
                }
                break;
            case SDLK_PAGEUP:
            case SDLK_PAGEDOWN:
            case ' ':
                postCommand_Widget(w, "scroll.page arg:%d", key == SDLK_PAGEUP ? -1 : +1);
                return iTrue;
            case SDLK_MINUS:
            case SDLK_EQUALS:
            case SDLK_0:
                if (mods == KMOD_PRIMARY) {
                    changeTextSize_DocumentWidget_(
                        d, key == SDLK_EQUALS ? 10 : key == SDLK_MINUS ? -10 : 0);
                    return iTrue;
                }
                break;
            case SDLK_9: {
                iBlock *seed = new_Block(64);
                for (size_t i = 0; i < 64; ++i) {
                    setByte_Block(seed, i, iRandom(0, 255));
                }
                setThemeSeed_GmDocument(d->doc, seed);
                delete_Block(seed);
                refresh_Widget(w);
                break;
            }
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
    else if (ev->type == SDL_MOUSEWHEEL) {
#if defined (iPlatformApple)
        /* Momentum scrolling. */
        scroll_DocumentWidget_(d, -ev->wheel.y * get_Window()->pixelRatio);
#else
        if (keyMods_Sym(SDL_GetModState()) == KMOD_PRIMARY) {
            changeTextSize_DocumentWidget_(d, ev->wheel.y > 0 ? 10 : -10);
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
            SDL_SetCursor(d->arrowCursor);
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
                                             d->url, linkUrl_GmDocument(d->doc, linkId))));
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

struct Impl_DrawContext {
    const iDocumentWidget *widget;
    iRect widgetBounds;
    iRect bounds; /* document area */
    iPaint paint;
    iBool inSelectMark;
    iBool inFoundMark;
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
        SDL_Texture *tex = imageTexture_GmDocument(d->widget->doc, run->imageId);
        if (tex) {
            const iRect dst = moved_Rect(run->visBounds, origin);
            SDL_RenderCopy(d->paint.dst->render, tex, NULL,
                           &(SDL_Rect){ dst.pos.x, dst.pos.y, dst.size.x, dst.size.y });
        }
        return;
    }
    enum iColorId      fg  = run->color;
    const iGmDocument *doc = d->widget->doc;
    const iBool isHover =
        (run->linkId != 0 && d->widget->hoverLink && run->linkId == d->widget->hoverLink->linkId &&
         !isEmpty_Rect(run->bounds));
    const iInt2 visPos = add_I2(run->visBounds.pos, origin);
    /* Text markers. */
    /* TODO: Add themed palette entries */
    fillRange_DrawContext_(d, run, teal_ColorId, d->widget->foundMark, &d->inFoundMark);
    fillRange_DrawContext_(d, run, brown_ColorId, d->widget->selectMark, &d->inSelectMark);
    if (run->linkId && !isEmpty_Rect(run->bounds)) {
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
    else {
        drawRange_Text(run->font, visPos, fg, run->text);
    }
    /* Presentation of links. */
    if (run->linkId) {
        const int metaFont = paragraph_FontId;
        /* TODO: Show status of an ongoing media request. */
        const int flags = linkFlags_GmDocument(doc, run->linkId);
        const iRect linkRect = moved_Rect(run->visBounds, origin);
        iMediaRequest *mr = NULL;
        /* Show inline content. */
        if (flags & content_GmLinkFlag) {
            fg = linkColor_GmDocument(doc, run->linkId, textHover_GmLinkPart);
            if (!isEmpty_Rect(run->bounds)) {
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
                drawAlign_Text(metaFont,
                               add_I2(topRight_Rect(run->bounds), origin),
                               fg,
                               right_Alignment,
                               "%s", cstr_String(&text));
                deinit_String(&text);
            }
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
            const iString *host = collect_String(newRange_String(parts.host));
            fg = linkColor_GmDocument(doc, linkId, textHover_GmLinkPart);
            const iBool showHost  = (!isEmpty_String(host) && flags & userFriendly_GmLinkFlag);
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
                    showHost ? cstr_String(host) : "",
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
                                   black_ColorId);
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

static void draw_DocumentWidget_(const iDocumentWidget *d) {
    const iWidget *w      = constAs_Widget(d);
    const iRect    bounds = bounds_Widget(w);
    draw_Widget(w);
    iDrawContext ctx = {
        .widget = d,
        .widgetBounds = /* omit scrollbar width */
            adjusted_Rect(bounds, zero_I2(), init_I2(-constAs_Widget(d->scroll)->rect.size.x, 0)),
        .bounds = documentBounds_DocumentWidget_(d)
    };
    init_Paint(&ctx.paint);
    fillRect_Paint(&ctx.paint, bounds, tmBackground_ColorId);
    setClip_Paint(&ctx.paint, bounds);
    render_GmDocument(d->doc, visibleRange_DocumentWidget_(d), drawRun_DrawContext_, &ctx);
    clearClip_Paint(&ctx.paint);
    draw_Widget(w);
}

iBeginDefineSubclass(DocumentWidget, Widget)
    .processEvent = (iAny *) processEvent_DocumentWidget_,
    .draw         = (iAny *) draw_DocumentWidget_,
iEndDefineSubclass(DocumentWidget)
