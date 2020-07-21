#include "documentwidget.h"
#include "paint.h"

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
    iString *source;
    int statusCode;
    iTlsRequest *request;
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
    iRegExp *pattern = new_RegExp("(.+)://([^/:?]+)(:[0-9]+)?([^?]*)(\\?.*)?", caseInsensitive_RegExpOption);
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
    d->source = new_String();
    d->request = NULL;

    setUrl_DocumentWidget(d, collectNewCStr_String("gemini.circumlunar.space/"));
}

void deinit_DocumentWidget(iDocumentWidget *d) {
    delete_String(d->source);
    delete_String(d->url);
}

void setSource_DocumentWidget(iDocumentWidget *d, const iString *source) {
    /* TODO: lock source during update */
    set_String(d->source, source);
    printf("%s\n", cstr_String(d->source));
    d->state = layout_DocumentState;
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
        printf("response: %s\n", cstr_String(line));
        delete_String(line);
    }
    setSource_DocumentWidget(d, collect_String(newRange_String(responseRange)));
    delete_Block(response);
    iReleaseLater(d->request);
    d->request = NULL;
    fflush(stdout);
}

static void fetch_DocumentWidget_(iDocumentWidget *d) {
    iAssert(!d->request);
    d->state = fetching_DocumentState;
    d->statusCode = 0;
    iUrl url;
    init_Url(&url, d->url);
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
    return processEvent_Widget(w, ev);
}

static void draw_DocumentWidget_(const iDocumentWidget *d) {
    const iWidget *w = constAs_Widget(d);
    draw_Widget(w);
    if (d->state != ready_DocumentState) return;
    /* TODO: lock source during draw */
}

iBeginDefineSubclass(DocumentWidget, Widget)
    .processEvent = (iAny *) processEvent_DocumentWidget_,
    .draw         = (iAny *) draw_DocumentWidget_,
iEndDefineSubclass(DocumentWidget)
