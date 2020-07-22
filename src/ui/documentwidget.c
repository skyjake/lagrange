#include "documentwidget.h"
#include "paint.h"
#include "util.h"
#include "app.h"
#include "../gemini.h"
#include "../gmdocument.h"

#include <the_Foundation/file.h>
#include <the_Foundation/path.h>
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
        capturedRange_RegExpMatch(&m, 1, &d->protocol);
        capturedRange_RegExpMatch(&m, 2, &d->host);
        capturedRange_RegExpMatch(&m, 3, &d->port);
        if (!isEmpty_Range(&d->port)) {
            /* Don't include the colon. */
            d->port.start++;
        }
        capturedRange_RegExpMatch(&m, 4, &d->path);
        capturedRange_RegExpMatch(&m, 5, &d->query);
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
    setBackgroundColor_Widget(w, gray25_ColorId);
    d->state = blank_DocumentState;
    d->url = new_String();
    d->statusCode = 0;
    d->request = NULL;
    d->newSource = new_String();
    d->doc = new_GmDocument();
    d->pageMargin = 5;
    d->scrollY = 0;
    setUrl_DocumentWidget(
        d, collectNewFormat_String("file://%s/test.gmi", cstr_String(collect_String(home_Path()))));
}

void deinit_DocumentWidget(iDocumentWidget *d) {
    delete_String(d->url);
    delete_String(d->newSource);
    iRelease(d->doc);
}

static int documentWidth_DocumentWidget_(const iDocumentWidget *d) {
    const iWidget *w = constAs_Widget(d);
    const iRect bounds = bounds_Widget(w);
    return iMini(bounds.size.x - gap_UI * d->pageMargin * 2, fontSize_UI * 40);
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
    clear_String(d->url);
    if (indexOfCStr_String(url, "://") == iInvalidPos && !startsWithCase_String(url, "gemini:")) {
        /* Prepend default protocol. */
        setCStr_String(d->url, "gemini://");
    }
    append_String(d->url, url);
    fetch_DocumentWidget_(d);
}

static iBool processEvent_DocumentWidget_(iDocumentWidget *d, const SDL_Event *ev) {
    iWidget *w = as_Widget(d);
    if (isResize_UserEvent(ev)) {
        setWidth_GmDocument(d->doc, documentWidth_DocumentWidget_(d));
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
            size_GmDocument(d->doc).y - height_Rect(bounds_Widget(w)) + d->pageMargin * gap_UI;
        if (scrollMax > 0) {
            d->scrollY = iMin(d->scrollY, scrollMax);
        }
        else {
            d->scrollY = 0;
        }
        postRefresh_App();
        return iTrue;
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
//    drawRect_Paint(&d->paint, moved_Rect(run->bounds, origin), red_ColorId);
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
    }
    if (d->state != ready_DocumentState) return;
    iDrawContext ctx = {.widget = d, .bounds = bounds_Widget(w) };
    const int margin = gap_UI * d->pageMargin;
    shrink_Rect(&ctx.bounds, init1_I2(margin));
    ctx.bounds.size.x = documentWidth_DocumentWidget_(d);
    ctx.bounds.pos.x = bounds_Widget(w).size.x / 2 - ctx.bounds.size.x / 2;
    init_Paint(&ctx.paint);
    drawRect_Paint(&ctx.paint, ctx.bounds, teal_ColorId);
    render_GmDocument(
        d->doc,
        (iRangei){ d->scrollY - margin, d->scrollY + height_Rect(ctx.bounds) + margin },
        drawRun_DrawContext_,
        &ctx);
}

iBeginDefineSubclass(DocumentWidget, Widget)
    .processEvent = (iAny *) processEvent_DocumentWidget_,
    .draw         = (iAny *) draw_DocumentWidget_,
iEndDefineSubclass(DocumentWidget)
