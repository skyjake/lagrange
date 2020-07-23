#include "documentwidget.h"
#include "paint.h"
#include "util.h"
#include "app.h"
#include "../gemini.h"
#include "../gmdocument.h"

#include <the_Foundation/file.h>
#include <the_Foundation/path.h>
#include <the_Foundation/ptrarray.h>
#include <the_Foundation/regexp.h>
#include <the_Foundation/tlsrequest.h>

enum iDocumentState {
    blank_DocumentState,
    fetching_DocumentState,
    layout_DocumentState,
    ready_DocumentState,
};



struct Impl_DocumentWidget {
    iWidget widget;
    enum iDocumentState state;
    iString *url;
    iTlsRequest *request;
    int statusCode;
    iString *newSource;
    iGmDocument *doc;
    int pageMargin;
    int scrollY;
    iPtrArray visibleLinks;
    const iGmRun *hoverLink;
    iClick click;
};

iDeclareType(Url)

struct Impl_Url {
    iRangecc protocol;
    iRangecc host;
    iRangecc port;
    iRangecc path;
    iRangecc query;
};

void init_Url(iUrl *d, const iString *text) {
    iRegExp *pattern =
        new_RegExp("(.+)://([^/:?]*)(:[0-9]+)?([^?]*)(\\?.*)?", caseInsensitive_RegExpOption);
    iRegExpMatch m;
    if (matchString_RegExp(pattern, text, &m)) {
        d->protocol = capturedRange_RegExpMatch(&m, 1);
        d->host     = capturedRange_RegExpMatch(&m, 2);
        d->port     = capturedRange_RegExpMatch(&m, 3);
        if (!isEmpty_Range(&d->port)) {
            /* Don't include the colon. */
            d->port.start++;
        }
        d->path  = capturedRange_RegExpMatch(&m, 4);
        d->query = capturedRange_RegExpMatch(&m, 5);
    }
    else {
        iZap(*d);
    }
    iRelease(pattern);
}

iDefineObjectConstruction(DocumentWidget)

void init_DocumentWidget(iDocumentWidget *d) {
    iWidget *w = as_Widget(d);
    init_Widget(w);
    setId_Widget(w, "document");
    setBackgroundColor_Widget(w, gray25_ColorId);
    d->state = blank_DocumentState;
    d->url = new_String();
    d->statusCode = 0;
    d->request = NULL;
    d->newSource = new_String();
    d->doc = new_GmDocument();
    d->pageMargin = 5;
    d->scrollY = 0;
    init_PtrArray(&d->visibleLinks);
    d->hoverLink = NULL;
    init_Click(&d->click, d, SDL_BUTTON_LEFT);
}

void deinit_DocumentWidget(iDocumentWidget *d) {
    deinit_PtrArray(&d->visibleLinks);
    delete_String(d->url);
    delete_String(d->newSource);
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

void setSource_DocumentWidget(iDocumentWidget *d, const iString *source) {
    /* TODO: lock source during update */
    setSource_GmDocument(d->doc, source, documentWidth_DocumentWidget_(d));
    d->state = ready_DocumentState;
}

static iRangecc getLine_(iRangecc text) {
    iRangecc line = { text.start, text.start };
    for (; *line.end != '\n' && line.end != text.end; line.end++) {}
    return line;
}

static void requestFinished_DocumentWidget_(iAnyObject *obj) {
    iDocumentWidget *d = obj;
    iBlock *response = readAll_TlsRequest(d->request);
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
    delete_Block(response);
    iReleaseLater(d->request);
    d->request = NULL;
    fflush(stdout);
    postRefresh_App();
}

static void fetch_DocumentWidget_(iDocumentWidget *d) {
    iAssert(!d->request);
    d->state = fetching_DocumentState;
    d->statusCode = 0;
    iUrl url;
    init_Url(&url, d->url);
    if (!cmpCStrSc_Rangecc(&url.protocol, "file", &iCaseInsensitive)) {
        iFile *f = new_File(collect_String(newRange_String(url.path)));
        if (open_File(f, readOnly_FileMode)) {
            setBlock_String(d->newSource, collect_Block(readAll_File(f)));
            postRefresh_App();
        }
        iRelease(f);
        return;
    }
    d->request = new_TlsRequest();
    uint16_t port = toInt_String(collect_String(newRange_String(url.port)));
    if (port == 0) {
        port = 1965; /* default Gemini port */
    }
    setUrl_TlsRequest(d->request, collect_String(newRange_String(url.host)), port);
    /* The request string is an UTF-8 encoded absolute URL. */
    iString *content = collectNew_String();
    append_String(content, d->url);
    appendCStr_String(content, "\r\n");
    setContent_TlsRequest(d->request, utf8_String(content));
    iConnect(TlsRequest, d->request, finished, d, requestFinished_DocumentWidget_);
    submit_TlsRequest(d->request);
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

static void addVisibleLink_DocumentWidget_(void *context, const iGmRun *run) {
    iDocumentWidget *d = context;
    if (run->linkId) {
        pushBack_PtrArray(&d->visibleLinks, run);
    }
}

static iRangei visibleRange_DocumentWidget_(const iDocumentWidget *d) {
    const int margin = gap_UI * d->pageMargin;
    return (iRangei){ d->scrollY - margin,
                      d->scrollY + height_Rect(bounds_Widget(constAs_Widget(d))) - margin };
}

static void updateVisible_DocumentWidget_(iDocumentWidget *d) {
    clear_PtrArray(&d->visibleLinks);
    render_GmDocument(
        d->doc,
        visibleRange_DocumentWidget_(d),
        addVisibleLink_DocumentWidget_,
        d);
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

static iBool processEvent_DocumentWidget_(iDocumentWidget *d, const SDL_Event *ev) {
    iWidget *w = as_Widget(d);
    if (isResize_UserEvent(ev)) {
        setWidth_GmDocument(d->doc, documentWidth_DocumentWidget_(d));
        updateVisible_DocumentWidget_(d);
    }
    if (ev->type == SDL_KEYDOWN) {
        const int mods = keyMods_Sym(ev->key.keysym.mod);
        const int key = ev->key.keysym.sym;
        switch (key) {
            case 'r':
                if (mods == KMOD_PRIMARY) {
                    fetch_DocumentWidget_(d);
                    return iTrue;
                }
                break;
            case '0': {
                extern int enableHalfPixelGlyphs_Text;
                enableHalfPixelGlyphs_Text = !enableHalfPixelGlyphs_Text;
                postRefresh_App();
                printf("halfpixel: %d\n", enableHalfPixelGlyphs_Text);
                fflush(stdout);
                break;
            }
        }
    }
    else if (ev->type == SDL_MOUSEWHEEL) {
        d->scrollY -= 3 * ev->wheel.y * lineHeight_Text(default_FontId);
        if (d->scrollY < 0) {
            d->scrollY = 0;
        }
        const int scrollMax =
            size_GmDocument(d->doc).y - height_Rect(bounds_Widget(w)) + 2 * d->pageMargin * gap_UI;
        if (scrollMax > 0) {
            d->scrollY = iMin(d->scrollY, scrollMax);
        }
        else {
            d->scrollY = 0;
        }
        updateVisible_DocumentWidget_(d);
        postRefresh_App();
        return iTrue;
    }
    else if (ev->type == SDL_MOUSEMOTION) {
        const iGmRun *oldHoverLink = d->hoverLink;
        d->hoverLink = NULL;
        const iRect docBounds = documentBounds_DocumentWidget_(d);
        const iInt2 hoverPos =
            addY_I2(sub_I2(localCoord_Widget(w, init_I2(ev->motion.x, ev->motion.y)),
                           topLeft_Rect(docBounds)),
                    d->scrollY);
        iConstForEach(PtrArray, i, &d->visibleLinks) {
            const iGmRun *run = i.ptr;
            if (contains_Rect(run->bounds, hoverPos)) {
                d->hoverLink = run;
                break;
            }
        }
        if (d->hoverLink != oldHoverLink) {
            postRefresh_App();
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
    const iWidget *w = constAs_Widget(d);
    draw_Widget(w);
    /* Update the document? */
    if (!isEmpty_String(d->newSource)) {
        /* TODO: Do this in the background. However, that requires a text metrics calculator
           that does not try to cache the glyph bitmaps. */
        setSource_GmDocument(d->doc, d->newSource, documentWidth_DocumentWidget_(d));
        clear_String(d->newSource);
        iConstCast(iDocumentWidget *, d)->state = ready_DocumentState;
        updateVisible_DocumentWidget_(iConstCast(iDocumentWidget *, d));
    }
    if (d->state != ready_DocumentState) return;
    iDrawContext ctx = { .widget = d, .bounds = documentBounds_DocumentWidget_(d) };
    init_Paint(&ctx.paint);
    render_GmDocument(
        d->doc,
        visibleRange_DocumentWidget_(d),
        drawRun_DrawContext_,
        &ctx);
}

iBeginDefineSubclass(DocumentWidget, Widget)
    .processEvent = (iAny *) processEvent_DocumentWidget_,
    .draw         = (iAny *) draw_DocumentWidget_,
iEndDefineSubclass(DocumentWidget)
