#include "documentwidget.h"
#include "scrollwidget.h"
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
    iGmRequest *request;
    iAtomicInt isSourcePending; /* request has new content, need to parse it */
    iGmDocument *doc;
    int pageMargin;
    int scrollY;
    iPtrArray visibleLinks;
    const iGmRun *hoverLink;
    iClick click;
    iScrollWidget *scroll;
};

iDefineObjectConstruction(DocumentWidget)

void init_DocumentWidget(iDocumentWidget *d) {
    iWidget *w = as_Widget(d);
    init_Widget(w);
    setId_Widget(w, "document");
    d->state      = blank_DocumentState;
    d->url        = new_String();
//    d->statusCode = 0;
    d->request    = NULL;
    d->isSourcePending = iFalse;
//    d->requestTimeout = 0;
//    d->readPending = iFalse;
//    d->newSource  = new_String();
//    d->needSourceUpdate = iFalse;
    d->doc        = new_GmDocument();
    d->pageMargin = 5;
    d->scrollY    = 0;
    d->hoverLink  = NULL;
    init_PtrArray(&d->visibleLinks);
    init_Click(&d->click, d, SDL_BUTTON_LEFT);
    addChild_Widget(w, iClob(d->scroll = new_ScrollWidget()));
}

void deinit_DocumentWidget(iDocumentWidget *d) {
    deinit_PtrArray(&d->visibleLinks);
    delete_String(d->url);
//    delete_String(d->newSource);
    iRelease(d->request);
    iRelease(d->doc);
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
    return rect;
}

static iRangecc getLine_(iRangecc text) {
    iRangecc line = { text.start, text.start };
    for (; *line.end != '\n' && line.end != text.end; line.end++) {}
    return line;
}

static void requestUpdated_DocumentWidget_(iAnyObject *obj) {
#if 0
    iDocumentWidget *d = obj;
    iBlock *response = readAll_TlsRequest(d->request);
    if (d->state == fetching_DocumentState) {
        iRangecc responseRange = { constBegin_Block(response), constEnd_Block(response) };
        iRangecc respLine = getLine_(responseRange);
        responseRange.start = respLine.end + 1;
        /* First line is the status code. */ {
            iString *line = newRange_String(respLine);
            trim_String(line);
            d->statusCode = toInt_String(line);
            printf("response (%02d): %s\n", d->statusCode, cstr_String(line));
            /* TODO: post a command with the status code */
            switch (d->statusCode) {
                case redirectPermanent_GmStatusCode:
                case redirectTemporary_GmStatusCode:
                    postCommandf_App("open url:%s", cstr_String(line) + 3);
                    break;
            }
            delete_String(line);
        }
        setCStrN_String(d->newSource, responseRange.start, size_Range(&responseRange));
        d->requestTimeout = SDL_AddTimer(2000, requestTimedOut_DocumentWidget_, d);
        d->state = receivedPartialResponse_DocumentState;
    }
    else if (d->state == receivedPartialResponse_DocumentState) {
        appendCStr_String(d->newSource, cstr_Block(response));
        d->needSourceUpdate = iTrue;
    }
    delete_Block(response);
    refresh_Widget(as_Widget(d));
#endif
    iDocumentWidget *d = obj;
    const int wasPending = exchange_Atomic(&d->isSourcePending, iTrue);
    if (!wasPending) {
        postCommand_Widget(obj, "document.request.updated request:%p", d->request);
    }
}

static void requestFinished_DocumentWidget_(iAnyObject *obj) {
    iDocumentWidget *d = obj;
    /*
    iReleaseLater(d->request);
    d->request = NULL;
    if (d->requestTimeout) {
        SDL_RemoveTimer(d->requestTimeout);
        d->requestTimeout = 0;
    }
    refresh_Widget(constAs_Widget(d));*/
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

static void updateSource_DocumentWidget_(iDocumentWidget *d) {
    /* Update the document? */
    //    if (d->needSourceUpdate) {
    /* TODO: Do this in the background. However, that requires a text metrics calculator
           that does not try to cache the glyph bitmaps. */
    iString str;
    initBlock_String(&str, body_GmRequest(d->request));
    setSource_GmDocument(d->doc, &str, documentWidth_DocumentWidget_(d));
    deinit_String(&str);
    updateVisible_DocumentWidget_(d);
    refresh_Widget(as_Widget(d));
    //        d->state = ready_DocumentState;
    //        if (!d->request) {
    //            d->needSourceUpdate = iFalse;
    //            postCommandf_App("document.changed url:%s", cstr_String(d->url));
    //        }
    //    }
}

static void fetch_DocumentWidget_(iDocumentWidget *d) {
    /* Forget the previous request. */
    if (d->request) {
        iRelease(d->request);
        d->request = NULL;
    }
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
    delete_String(newUrl);
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

static void readResponse_DocumentWidget_(iDocumentWidget *d) {
#if 0
    d->readPending = iFalse;
    iBlock *response = collect_Block(readAll_TlsRequest(d->request));
    if (isEmpty_Block(response)) {
        return;
    }
    if (d->state == fetching_DocumentState) {
        /* TODO: Bug here is that the first read may occur before the first line is
           available, so nothing gets done. Should ensure that the status code is
           read successully. */
        iRangecc responseRange = { constBegin_Block(response), constEnd_Block(response) };
        iRangecc respLine = getLine_(responseRange);
        responseRange.start = respLine.end + 1;
        /* First line is the status code. */ {
            iString *line = collect_String(newRange_String(respLine));
            trim_String(line);
            d->statusCode = toInt_String(line);
            printf("response (%02d): %s\n", d->statusCode, cstr_String(line));
            /* TODO: post a command with the status code */
            switch (d->statusCode) {
                case redirectPermanent_GmStatusCode:
                case redirectTemporary_GmStatusCode:
                    postCommandf_App("open url:%s", cstr_String(line) + 3);
                    return;
            }
        }
        setCStrN_String(d->newSource, responseRange.start, size_Range(&responseRange));
        d->requestTimeout = SDL_AddTimer(2000, requestTimedOut_DocumentWidget_, d);
        d->state = receivedPartialResponse_DocumentState;
        d->scrollY = 0;
    }
    else if (d->state == receivedPartialResponse_DocumentState) {
        appendCStr_String(d->newSource, cstr_Block(response));
    }
#endif
    updateSource_DocumentWidget_(d);
}

static void checkResponseCode_DocumentWidget_(iDocumentWidget *d) {
    if (d->state == fetching_DocumentState) {
        d->state = receivedPartialResponse_DocumentState;
        d->scrollY = 0;
        switch (status_GmRequest(d->request)) {
            case redirectTemporary_GmStatusCode:
            case redirectPermanent_GmStatusCode:
                postCommandf_App("open url:%s", cstr_String(meta_GmRequest(d->request)));
                iReleasePtr(&d->request);
                break;
            default:
                break;
        }
    }
}

static iBool processEvent_DocumentWidget_(iDocumentWidget *d, const SDL_Event *ev) {
    iWidget *w = as_Widget(d);
    if (isResize_UserEvent(ev)) {
        setWidth_GmDocument(d->doc, documentWidth_DocumentWidget_(d));
        updateVisible_DocumentWidget_(d);
        refresh_Widget(w);
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
            case SDLK_PAGEUP:
            case SDLK_PAGEDOWN:
            case ' ':
                postCommand_Widget(w, "scroll.page arg:%d", key == SDLK_PAGEUP ? -1 : +1);
                return iTrue;
            case 'r':
                if (mods == KMOD_PRIMARY) {
                    fetch_DocumentWidget_(d);
                    return iTrue;
                }
                break;
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
        const iGmRun *oldHoverLink = d->hoverLink;
        d->hoverLink          = NULL;
        const iRect docBounds = documentBounds_DocumentWidget_(d);
        const iInt2 hoverPos  = addY_I2(
            sub_I2(init_I2(ev->motion.x, ev->motion.y), topLeft_Rect(docBounds)), d->scrollY);
        iConstForEach(PtrArray, i, &d->visibleLinks) {
            const iGmRun *run = i.ptr;
            if (contains_Rect(run->bounds, hoverPos)) {
                d->hoverLink = run;
                break;
            }
        }
        if (d->hoverLink != oldHoverLink) {
            refresh_Widget(w);
        }
    }
    switch (processEvent_Click(&d->click, ev)) {
        case finished_ClickResult:
            if (d->hoverLink) {
                iAssert(d->hoverLink->linkId);
                postCommandf_App("open url:%s",
                                 cstr_String(absoluteUrl_DocumentWidget_(
                                     d, linkUrl_GmDocument(d->doc, d->hoverLink->linkId))));
            }
            return iTrue;
        case started_ClickResult:
        case double_ClickResult:
        case drag_ClickResult:
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
};

static void drawRun_DrawContext_(void *context, const iGmRun *run) {
    iDrawContext *d = context;
    iString text;
    /* TODO: making a copy is unnecessary; the text routines should accept Rangecc */
    initRange_String(&text, run->text);
    iInt2 origin = addY_I2(d->bounds.pos, -d->widget->scrollY);
    drawString_Text(run->font, add_I2(run->bounds.pos, origin), run->color, &text);
    if (run == d->widget->hoverLink) {
        drawRect_Paint(&d->paint, moved_Rect(run->bounds, origin), orange_ColorId);
    }
    deinit_String(&text);
}

static void draw_DocumentWidget_(const iDocumentWidget *d) {
    const iWidget *w      = constAs_Widget(d);
    const iRect    bounds = bounds_Widget(w);
    draw_Widget(w);
    iDrawContext ctx = { .widget = d, .bounds = documentBounds_DocumentWidget_(d) };
    init_Paint(&ctx.paint);
    fillRect_Paint(&ctx.paint, bounds, gray25_ColorId);
//    if (d->state != ready_DocumentState) return;
    setClip_Paint(&ctx.paint, bounds);
    render_GmDocument(d->doc, visibleRange_DocumentWidget_(d), drawRun_DrawContext_, &ctx);
    clearClip_Paint(&ctx.paint);
    draw_Widget(w);
}

iBeginDefineSubclass(DocumentWidget, Widget)
    .processEvent = (iAny *) processEvent_DocumentWidget_,
    .draw         = (iAny *) draw_DocumentWidget_,
iEndDefineSubclass(DocumentWidget)
