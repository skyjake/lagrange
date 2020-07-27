#include "documentwidget.h"
#include "scrollwidget.h"
#include "inputwidget.h"
#include "paint.h"
#include "command.h"
#include "util.h"
#include "app.h"
#include "../gemini.h"
#include "../gmdocument.h"
#include "../gmrequest.h"
#include "../gmutil.h"

#include <the_Foundation/file.h>
#include <the_Foundation/path.h>
#include <the_Foundation/ptrarray.h>
#include <the_Foundation/regexp.h>

#include <SDL_clipboard.h>
#include <SDL_timer.h>

enum iDocumentState {
    blank_DocumentState,
    fetching_DocumentState,
    receivedPartialResponse_DocumentState,
    layout_DocumentState,
    ready_DocumentState,
};

struct Impl_DocumentWidget {
    iWidget widget;
    enum iDocumentState state;
    iString *url;
    iString *titleUser;
    iGmRequest *request;
    iAtomicInt isSourcePending; /* request has new content, need to parse it */
    iGmDocument *doc;
    iBool selecting;
    iRangecc selectMark;
    iRangecc foundMark;
    int pageMargin;
    int scrollY;
    iPtrArray visibleLinks;
    const iGmRun *hoverLink;
    iClick click;
    iScrollWidget *scroll;
    iWidget *menu;
    SDL_Cursor *arrowCursor;
    SDL_Cursor *beamCursor;
    SDL_Cursor *handCursor;
};

iDefineObjectConstruction(DocumentWidget)

void init_DocumentWidget(iDocumentWidget *d) {
    iWidget *w = as_Widget(d);
    init_Widget(w);
    setId_Widget(w, "document");
    d->state           = blank_DocumentState;
    d->url             = new_String();
    d->titleUser       = new_String();
    d->request         = NULL;
    d->isSourcePending = iFalse;
    d->doc             = new_GmDocument();
    d->selecting       = iFalse;
    d->selectMark      = iNullRange;
    d->foundMark       = iNullRange;
    d->pageMargin      = 5;
    d->scrollY         = 0;
    d->hoverLink       = NULL;
    d->arrowCursor     = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_ARROW);
    d->beamCursor      = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_IBEAM);
    d->handCursor      = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_HAND);
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
}

void deinit_DocumentWidget(iDocumentWidget *d) {
    deinit_PtrArray(&d->visibleLinks);
    delete_String(d->url);
    delete_String(d->titleUser);
    iRelease(d->request);
    iRelease(d->doc);
    SDL_FreeCursor(d->arrowCursor);
    SDL_FreeCursor(d->beamCursor);
    SDL_FreeCursor(d->handCursor);
}

static int documentWidth_DocumentWidget_(const iDocumentWidget *d) {
    const iWidget *w = constAs_Widget(d);
    const iRect bounds = bounds_Widget(w);
    return iMini(bounds.size.x - gap_UI * d->pageMargin * 2, fontSize_UI * 40);
}

static iRect documentBounds_DocumentWidget_(const iDocumentWidget *d) {
    const iRect bounds = bounds_Widget(constAs_Widget(d));
    const int   margin = gap_UI * d->pageMargin;
    iRect       rect;
    rect.size.x = documentWidth_DocumentWidget_(d);
    rect.pos.x  = bounds.size.x / 2 - rect.size.x / 2;
    rect.pos.y  = top_Rect(bounds) + margin;
    rect.size.y = height_Rect(bounds) - 2 * margin;
    if (size_GmDocument(d->doc).y < rect.size.y) {
        /* Center vertically if short. */
        int offset = (rect.size.y - size_GmDocument(d->doc).y) / 2;
        rect.pos.y += offset;
        rect.size.y = size_GmDocument(d->doc).y;
    }
    return rect;
}

static iInt2 documentPos_DocumentWidget_(const iDocumentWidget *d, iInt2 pos) {
    return addY_I2(sub_I2(pos, topLeft_Rect(documentBounds_DocumentWidget_(d))), d->scrollY);
}

static void requestUpdated_DocumentWidget_(iAnyObject *obj) {
    iDocumentWidget *d = obj;
    const int wasPending = exchange_Atomic(&d->isSourcePending, iTrue);
    if (!wasPending) {
        postCommand_Widget(obj, "document.request.updated request:%p", d->request);
    }
}

static void requestFinished_DocumentWidget_(iAnyObject *obj) {
    iDocumentWidget *d = obj;
    postCommand_Widget(obj, "document.request.finished request:%p", d->request);
}

static iRangei visibleRange_DocumentWidget_(const iDocumentWidget *d) {
    const int margin = gap_UI * d->pageMargin;
    return (iRangei){ d->scrollY - margin,
                      d->scrollY + height_Rect(bounds_Widget(constAs_Widget(d))) - margin };
}

static void addVisibleLink_DocumentWidget_(void *context, const iGmRun *run) {
    iDocumentWidget *d = context;
    if (run->linkId) {
        pushBack_PtrArray(&d->visibleLinks, run);
    }
}

static int scrollMax_DocumentWidget_(const iDocumentWidget *d) {
    return size_GmDocument(d->doc).y - height_Rect(bounds_Widget(constAs_Widget(d))) +
           2 * d->pageMargin * gap_UI;
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
}

static void updateWindowTitle_DocumentWidget_(const iDocumentWidget *d) {
    const char *titleSep = " \u2013 ";
    iString *title = collect_String(copy_String(title_GmDocument(d->doc)));
    if (!isEmpty_String(d->titleUser)) {
        if (!isEmpty_String(title)) appendCStr_String(title, titleSep);
        append_String(title, d->titleUser);
    }
    if (isEmpty_String(title)) {
        setCStr_String(title, "Lagrange");
    }
    setTitle_Window(get_Window(), title);
}

static void setSource_DocumentWidget_(iDocumentWidget *d, const iString *source) {
    iUrl parts;
    init_Url(&parts, d->url);
    setHost_GmDocument(d->doc, collect_String(newRange_String(parts.host)));
    setSource_GmDocument(d->doc, source, documentWidth_DocumentWidget_(d));
    d->foundMark = iNullRange;
    d->selectMark = iNullRange;
    d->hoverLink = NULL;
    updateWindowTitle_DocumentWidget_(d);
    updateVisible_DocumentWidget_(d);
    refresh_Widget(as_Widget(d));
}

static void updateSource_DocumentWidget_(iDocumentWidget *d) {
    /* TODO: Do this in the background. However, that requires a text metrics calculator
       that does not try to cache the glyph bitmaps. */
    if (status_GmRequest(d->request) != input_GmStatusCode &&
        status_GmRequest(d->request) != sensitiveInput_GmStatusCode) {        
        iString str;
        initBlock_String(&str, body_GmRequest(d->request));
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
    postCommandf_App("document.request.started url:%s", cstr_String(d->url));
    d->state = fetching_DocumentState;
    set_Atomic(&d->isSourcePending, iFalse);
    d->request = new_GmRequest();
    setUrl_GmRequest(d->request, d->url);
    iConnect(GmRequest, d->request, updated, d, requestUpdated_DocumentWidget_);
    iConnect(GmRequest, d->request, finished, d, requestFinished_DocumentWidget_);
    submit_GmRequest(d->request);
}

void setUrl_DocumentWidget(iDocumentWidget *d, const iString *url) {
    iString *newUrl = new_String();
    if (indexOfCStr_String(url, "://") == iInvalidPos && !startsWithCase_String(url, "gemini:")) {
        /* Prepend default protocol. */
        setCStr_String(newUrl, "gemini://");
    }
    append_String(newUrl, url);
    if (cmpStringSc_String(d->url, newUrl, &iCaseInsensitive)) {
        set_String(d->url, newUrl);
        fetch_DocumentWidget_(d);
    }
    iRegExp *userPat = new_RegExp("~([^/?]+)", 0);
    iRegExpMatch m;
    if (matchString_RegExp(userPat, d->url, &m)) {
        setRange_String(d->titleUser, capturedRange_RegExpMatch(&m, 1));
    }
    iRelease(userPat);
    delete_String(newUrl);
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

static iRangecc dirPath_(iRangecc path) {
    const size_t pos = lastIndexOfCStr_Rangecc(&path, "/");
    if (pos == iInvalidPos) return path;
    return (iRangecc){ path.start, path.start + pos };
}

static const iString *absoluteUrl_DocumentWidget_(const iDocumentWidget *d, const iString *url) {
    if (indexOfCStr_String(url, "://") != iInvalidPos) {
        /* Already absolute. */
        return url;
    }
    iUrl parts;
    init_Url(&parts, d->url);
    iString *absolute = new_String();
    appendRange_String(absolute, parts.protocol);
    appendCStr_String(absolute, "://");
    appendRange_String(absolute, parts.host);
    if (!isEmpty_Range(&parts.port)) {
        appendCStr_String(absolute, ":");
        appendRange_String(absolute, parts.port);
    }
    if (startsWith_String(url, "/")) {
        append_String(absolute, url);
    }
    else {
        iRangecc relPath = range_String(url);
        iRangecc dir = dirPath_(parts.path);
        for (;;) {
            if (equal_Rangecc(&relPath, ".")) {
                relPath.start++;
            }
            else if (startsWith_Rangecc(&relPath, "./")) {
                relPath.start += 2;
            }
            else if (equal_Rangecc(&relPath, "..")) {
                relPath.start += 2;
                dir = dirPath_(dir);
            }
            else if (startsWith_Rangecc(&relPath, "../")) {
                relPath.start += 3;
                dir = dirPath_(dir);
            }
            else break;
        }
        appendRange_String(absolute, dir);
        if (!endsWith_String(absolute, "/")) {
            appendCStr_String(absolute, "/");
        }
        appendRange_String(absolute, relPath);
    }
    return collect_String(absolute);
}

static void showErrorPage_DocumentWidget_(iDocumentWidget *d, enum iGmStatusCode code) {
    iString *src = collectNew_String();
    const iGmError *msg = get_GmError(code);
    format_String(src,
                  "# %lc %s\n%s",
                  msg->icon ? msg->icon : 0x2327, /* X in a box */
                  msg->title,
                  msg->info);
    switch (code) {
        case failedToOpenFile_GmStatusCode:
        case certificateNotValid_GmStatusCode:
            appendFormat_String(src, "\n\n%s", cstr_String(meta_GmRequest(d->request)));
            break;
        case slowDown_GmStatusCode:
            appendFormat_String(src, "\n\nWait %s seconds before your next request.",
                                cstr_String(meta_GmRequest(d->request)));
            break;
        default:
            break;
    }
    setSource_DocumentWidget_(d, src);
}

static void checkResponseCode_DocumentWidget_(iDocumentWidget *d) {
    if (!d->request) {
        return;
    }
    if (d->state == fetching_DocumentState) {
        d->state = receivedPartialResponse_DocumentState;
        d->scrollY = 0;
        enum iGmStatusCode statusCode = status_GmRequest(d->request);
        switch (statusCode) {
            case none_GmStatusCode:
            case success_GmStatusCode:
                break;
            case input_GmStatusCode:
            case sensitiveInput_GmStatusCode: {
                iUrl parts;
                init_Url(&parts, d->url);
                iWidget *dlg = makeValueInput_Widget(
                    as_Widget(d),
                    NULL,
                    cstrFormat_String(cyan_ColorEscape "%s",
                                      cstr_String(collect_String(newRange_String(parts.host)))),
                    isEmpty_String(meta_GmRequest(d->request))
                        ? cstrFormat_String(
                              "Please enter input for %s:",
                              cstr_String(collect_String(newRange_String(parts.path))))
                        : cstr_String(meta_GmRequest(d->request)),
                    orange_ColorEscape "Send \u21d2",
                    "document.input.submit");
                setSensitive_InputWidget(findChild_Widget(dlg, "input"),
                                         statusCode == sensitiveInput_GmStatusCode);
                break;
            }
            case redirectTemporary_GmStatusCode:
            case redirectPermanent_GmStatusCode:
                if (isEmpty_String(meta_GmRequest(d->request))) {
                    showErrorPage_DocumentWidget_(d, invalidRedirect_GmStatusCode);
                }
                else {
                    postCommandf_App("open redirect:1 url:%s",
                                     cstr_String(meta_GmRequest(d->request)));
                    iReleasePtr(&d->request);
                }
                break;
            default:
                showErrorPage_DocumentWidget_(d, statusCode);
                break;
        }
    }
}

const iString *valueString_Command(const char *cmd, const char *label) {
    return collect_String(newCStr_String(suffixPtr_Command(cmd, label)));
}

static const char *sourceLoc_DocumentWidget_(const iDocumentWidget *d, iInt2 pos) {
    return findLoc_GmDocument(d->doc, documentPos_DocumentWidget_(d, pos));
}

static iBool processEvent_DocumentWidget_(iDocumentWidget *d, const SDL_Event *ev) {
    iWidget *w = as_Widget(d);
    if (isResize_UserEvent(ev)) {
        setWidth_GmDocument(d->doc, documentWidth_DocumentWidget_(d));
        scroll_DocumentWidget_(d, 0);
        updateVisible_DocumentWidget_(d);
        refresh_Widget(w);
    }
    else if (isCommand_UserEvent(ev, "copy")) {
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
    else if (isCommand_Widget(w, ev, "document.copylink")) {
        if (d->hoverLink) {
            SDL_SetClipboardText(cstr_String(
                absoluteUrl_DocumentWidget_(d, linkUrl_GmDocument(d->doc, d->hoverLink->linkId))));
        }
        else {
            SDL_SetClipboardText(cstr_String(d->url));
        }
        return iTrue;
    }
    else if (isCommand_UserEvent(ev, "document.input.submit")) {
        iString *value = collect_String(suffix_Command(command_UserEvent(ev), "value"));
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
    else if (isCommand_UserEvent(ev, "valueinput.cancelled") &&
             cmp_String(string_Command(command_UserEvent(ev), "id"), "document.input.submit") ==
                 0) {
        postCommand_App("navigate.back");
        return iTrue;
    }
    else if (isCommand_Widget(w, ev, "document.request.updated") &&
             pointerLabel_Command(command_UserEvent(ev), "request") == d->request) {
        updateSource_DocumentWidget_(d);
        checkResponseCode_DocumentWidget_(d);
        return iTrue;
    }
    else if (isCommand_Widget(w, ev, "document.request.finished") &&
             pointerLabel_Command(command_UserEvent(ev), "request") == d->request) {
        updateSource_DocumentWidget_(d);
        checkResponseCode_DocumentWidget_(d);
        d->state = ready_DocumentState;
        iReleasePtr(&d->request);
        postCommandf_App("document.changed url:%s", cstr_String(d->url));
        return iTrue;
    }
    else if (isCommand_UserEvent(ev, "document.stop")) {
        if (d->request) {
            postCommandf_App("document.request.cancelled url:%s", cstr_String(d->url));
            iReleasePtr(&d->request);
            d->state = ready_DocumentState;
        }
        return iTrue;
    }
    else if (isCommand_UserEvent(ev, "document.reload")) {
        fetch_DocumentWidget_(d);
        return iTrue;
    }
    else if (isCommand_Widget(w, ev, "scroll.moved")) {
        d->scrollY = arg_Command(command_UserEvent(ev));
        updateVisible_DocumentWidget_(d);
        return iTrue;
    }
    else if (isCommand_Widget(w, ev, "scroll.page")) {
        scroll_DocumentWidget_(
            d, arg_Command(command_UserEvent(ev)) * height_Rect(documentBounds_DocumentWidget_(d)));
        return iTrue;
    }
    else if (isCommand_UserEvent(ev, "find.next") || isCommand_UserEvent(ev, "find.prev")) {
        const int dir = isCommand_UserEvent(ev, "find.next") ? +1 : -1;
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
    else if (isCommand_UserEvent(ev, "find.clearmark")) {
        if (d->foundMark.start) {
            d->foundMark = iNullRange;
            refresh_Widget(w);
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
                    scroll_DocumentWidget_(d, 2 * lineHeight_Text(paragraph_FontId) *
                                                  (key == SDLK_UP ? -1 : 1));
                    return iTrue;
                }
                break;
            case SDLK_PAGEUP:
            case SDLK_PAGEDOWN:
            case ' ':
                postCommand_Widget(w, "scroll.page arg:%d", key == SDLK_PAGEUP ? -1 : +1);
                return iTrue;
            case '0': {
                extern int enableHalfPixelGlyphs_Text;
                enableHalfPixelGlyphs_Text = !enableHalfPixelGlyphs_Text;
                refresh_Widget(w);
                printf("halfpixel: %d\n", enableHalfPixelGlyphs_Text);
                fflush(stdout);
                break;
            }
        }
    }
    else if (ev->type == SDL_MOUSEWHEEL) {
        scroll_DocumentWidget_(d, -3 * ev->wheel.y * lineHeight_Text(default_FontId));
        return iTrue;
    }
    else if (ev->type == SDL_MOUSEMOTION) {
        if (isVisible_Widget(d->menu)) {
            SDL_SetCursor(d->arrowCursor);
        }
        else {
            const iRect docBounds      = documentBounds_DocumentWidget_(d);
            const iInt2 mouse          = init_I2(ev->motion.x, ev->motion.y);
            const iGmRun *oldHoverLink = d->hoverLink;
            d->hoverLink               = NULL;
            const iInt2 hoverPos = addY_I2(sub_I2(mouse, topLeft_Rect(docBounds)), d->scrollY);
            if (d->state == ready_DocumentState) {
                iConstForEach(PtrArray, i, &d->visibleLinks) {
                    const iGmRun *run = i.ptr;
                    if (contains_Rect(run->bounds, hoverPos)) {
                        d->hoverLink = run;
                        break;
                    }
                }
            }
            if (d->hoverLink != oldHoverLink) {
                refresh_Widget(w);
            }
            if (!contains_Widget(w, mouse) || contains_Widget(constAs_Widget(d->scroll), mouse)) {
                SDL_SetCursor(d->arrowCursor);
            }
            else {
                SDL_SetCursor(d->hoverLink ? d->handCursor : d->beamCursor);
            }
        }
    }
    processContextMenuEvent_Widget(d->menu, ev);
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
            if (!isMoved_Click(&d->click)) {
                if (d->hoverLink) {
                    iAssert(d->hoverLink->linkId);
                    postCommandf_App("open url:%s",
                                     cstr_String(absoluteUrl_DocumentWidget_(
                                         d, linkUrl_GmDocument(d->doc, d->hoverLink->linkId))));
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
    iRect bounds;
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
    iDrawContext *d = context;
    iString text;
    /* TODO: making a copy is unnecessary; the text routines should accept Rangecc */
    initRange_String(&text, run->text);
    iInt2 origin = addY_I2(d->bounds.pos, -d->widget->scrollY);
    enum iColorId fg = run->color;
    if (run == d->widget->hoverLink) {
        //const char *desc = "";
        const iGmDocument *doc = d->widget->doc;
        const iGmLinkId linkId = d->widget->hoverLink->linkId;
        const iString *url = linkUrl_GmDocument(doc, linkId);
//        const int flags = linkFlags_GmDocument(doc, linkId);
        iUrl parts;
        init_Url(&parts, url);
//        desc = cstrFormat_String("\u2192 %s", cstr_String(collect_String(newRange_String(parts.protocol))));
        const iString *host = collect_String(newRange_String(parts.host));
        fg = linkColor_GmDocument(doc, linkId);
        if (!isEmpty_String(host)) {
//        int descWidth = measure_Text(default_FontId, cstr_String(host)).x + gap_UI;
            iRect linkRect = moved_Rect(run->visBounds, origin);
//        linkRect.size.x += descWidth;
//        fillRect_Paint(&d->paint, linkRect, teal_ColorId);
            drawAlign_Text(default_FontId,
//                           init_I2(right_Rect(d->bounds), top_Rect(linkRect)),
                           topRight_Rect(linkRect),
                           fg - 1,
                           left_Alignment,
                           " \u2014 %s",
                           cstr_String(host));
        }
    }
    const iInt2 visPos = add_I2(run->visBounds.pos, origin);
    /* Text markers. */
    fillRange_DrawContext_(d, run, teal_ColorId, d->widget->foundMark, &d->inFoundMark);
    fillRange_DrawContext_(d, run, brown_ColorId, d->widget->selectMark, &d->inSelectMark);
    drawString_Text(run->font, visPos, fg, &text);
    deinit_String(&text);

//    drawRect_Paint(&d->paint, (iRect){ visPos, run->bounds.size }, green_ColorId);
//    drawRect_Paint(&d->paint, (iRect){ visPos, run->visBounds.size }, red_ColorId);
}

static void draw_DocumentWidget_(const iDocumentWidget *d) {
    const iWidget *w      = constAs_Widget(d);
    const iRect    bounds = bounds_Widget(w);
    draw_Widget(w);
    iDrawContext ctx = { .widget = d, .bounds = documentBounds_DocumentWidget_(d) };
    init_Paint(&ctx.paint);
    fillRect_Paint(&ctx.paint, bounds, gray15_ColorId);
    setClip_Paint(&ctx.paint, bounds);
    render_GmDocument(d->doc, visibleRange_DocumentWidget_(d), drawRun_DrawContext_, &ctx);
    clearClip_Paint(&ctx.paint);
    draw_Widget(w);
}

iBeginDefineSubclass(DocumentWidget, Widget)
    .processEvent = (iAny *) processEvent_DocumentWidget_,
    .draw         = (iAny *) draw_DocumentWidget_,
iEndDefineSubclass(DocumentWidget)
