/* Copyright 2021 Jaakko Keränen <jaakko.keranen@iki.fi>

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

#include "translation.h"

#include "app.h"
#include "gmdocument.h"
#include "ui/command.h"
#include "ui/documentwidget.h"
#include "ui/labelwidget.h"
#include "ui/paint.h"
#include "ui/util.h"

#include <the_Foundation/regexp.h>
#include <SDL_timer.h>
#include <math.h>

/*----------------------------------------------------------------------------------------------*/

iDeclareWidgetClass(TranslationProgressWidget)
iDeclareObjectConstruction(TranslationProgressWidget)

iDeclareType(Sprite)

struct Impl_Sprite {
    iInt2 pos;
    iInt2 size;
    int color;
    int xoff;
    iString text;
};

struct Impl_TranslationProgressWidget {
    iWidget widget;
    uint32_t startTime;
    int font;
    iArray sprites;
};

void init_TranslationProgressWidget(iTranslationProgressWidget *d) {
    iWidget *w = &d->widget;
    init_Widget(w);
    setId_Widget(w, "xlt.progress");
    init_Array(&d->sprites, sizeof(iSprite));
    d->startTime = SDL_GetTicks();
    /* Set up some letters to animate. */
    const char *chars = "ARGOS";
    const size_t n = strlen(chars);
    resize_Array(&d->sprites, n);
    d->font = uiContentBold_FontId;
    const int width = lineHeight_Text(d->font);
    const int gap = gap_Text / 2;
    int x = (int) (n * width + (n - 1) * gap) / -2;
    const int y = -lineHeight_Text(d->font) / 2;
    for (size_t i = 0; i < n; i++) {
        iSprite *spr = at_Array(&d->sprites, i);
        spr->pos = init_I2(x, y);
        spr->color = 0;
        init_String(&spr->text);
        appendChar_String(&spr->text, chars[i]);
        spr->xoff = (width - advanceRange_Text(d->font, range_String(&spr->text)).x) / 2;
        spr->size = init_I2(width, lineHeight_Text(d->font));
        x += width + gap;
    }
}

void deinit_TranslationProgressWidget(iTranslationProgressWidget *d) {
    iForEach(Array, i, &d->sprites) {
        iSprite *spr = i.value;
        deinit_String(&spr->text);
    }
    deinit_Array(&d->sprites);
}

iDefineObjectConstruction(TranslationProgressWidget)

static void draw_TranslationProgressWidget_(const iTranslationProgressWidget *d) {
    const iWidget *w = &d->widget;
    const float t = (float) (SDL_GetTicks() - d->startTime) / 1000.0f;
    const iRect bounds = bounds_Widget(w);
    iPaint p;
    init_Paint(&p);
    const iInt2 mid = mid_Rect(bounds);
    SDL_SetRenderDrawBlendMode(renderer_Window(get_Window()), SDL_BLENDMODE_BLEND);
    iConstForEach(Array, i, &d->sprites) {
        const int index = index_ArrayConstIterator(&i);
        const float angle = (float) index;
        const iSprite *spr = i.value;
        const float opacity = iClamp(t - index * 0.5f, 0.0, 1.0f);
        int bg = uiBackgroundSelected_ColorId;
        int fg = uiTextSelected_ColorId;
        iInt2 pos = add_I2(mid, spr->pos);
        pos.y += sin(angle + t) * spr->size.y * iClamp(t * 0.25f - 0.3f, 0.0f, 1.0f);
        if (bg >= 0) {
            p.alpha = opacity * 255;
            fillRect_Paint(&p, (iRect){ pos, spr->size }, bg);
        }
        if (fg >= 0) {
            setOpacity_Text(opacity * 2);
            drawRange_Text(d->font, addX_I2(pos, spr->xoff), fg, range_String(&spr->text));
        }
    }
    setOpacity_Text(1.0f);
    SDL_SetRenderDrawBlendMode(renderer_Window(get_Window()), SDL_BLENDMODE_NONE);
}

static iBool processEvent_TranslationProgressWidget_(iTranslationProgressWidget *d,
                                                     const SDL_Event *ev) {
    iUnused(d, ev);
    return iFalse;
}

iBeginDefineSubclass(TranslationProgressWidget, Widget)
    .draw         = (iAny *) draw_TranslationProgressWidget_,
    .processEvent = (iAny *) processEvent_TranslationProgressWidget_,
 iEndDefineSubclass(TranslationProgressWidget)

/*----------------------------------------------------------------------------------------------*/

iDefineTypeConstructionArgs(Translation, (iDocumentWidget *doc), doc)

static const char *   translationServiceHost = "xlt.skyjake.fi";
static const uint16_t translationServicePort = 443;

static const char *doubleArrowSymbol    = "\u20e2"; /* prevent getting mangled */
static const char *tripleBacktickSymbol = "\u20e3";
static const char *h1Symbol             = "\u20e4";
static const char *h2Symbol             = "\u20e5";
static const char *h3Symbol             = "\u20e6";

static iString *quote_String_(const iString *d) {
    iString *quot = new_String();
    iConstForEach(String, i, d) {
        const iChar ch = i.value;
        if (ch == '"') {
            appendCStr_String(quot, "\\\"");
        }
        else if (ch == '\\') {
            appendCStr_String(quot, "\\\\");
        }
        else if (ch == '\n') {
            appendCStr_String(quot, "\\n");
        }
        else if (ch == '\r') {
            appendCStr_String(quot, "\\r");
        }
        else if (ch == '\t') {
            appendCStr_String(quot, "\\t");
        }
        else if (ch >= 0x100) {
            appendFormat_String(quot, "\\u%04x", ch);
        }
        else {
            appendChar_String(quot, ch);
        }
    }
    return quot;
}

static iString *unquote_String_(const iString *d) {
    iString *unquot = new_String();
    iConstForEach(String, i, d) {
        const iChar ch = i.value;
        if (ch == '\\') {
            next_StringConstIterator(&i);
            const iChar esc = i.value;
            if (esc == '\\') {
                appendChar_String(unquot, esc);
            }
            else if (esc == 'n') {
                appendChar_String(unquot, '\n');
            }
            else if (esc == 'r') {
                appendChar_String(unquot, '\r');
            }
            else if (esc == 't') {
                appendChar_String(unquot, '\t');
            }
            else if (esc == '"') {
                appendChar_String(unquot, '"');
            }
            else if (esc == 'u') {
                char digits[5];
                iZap(digits);
                iForIndices(j, digits) {
                    next_StringConstIterator(&i);
                    digits[j] = *i.pos;
                }
                iChar codepoint = strtoul(digits, NULL, 16);
                if (codepoint) {
                    appendChar_String(unquot, codepoint);
                }
            }
            else {
                iAssert(0);
            }
        }
        else {
            appendChar_String(unquot, ch);
        }
    }
    return unquot;
}

static void finished_Translation_(iTlsRequest *d, iTlsRequest *req) {
    iUnused(req);
    postCommandf_App("translation.finished ptr:%p", userData_Object(d));
}

void init_Translation(iTranslation *d, iDocumentWidget *doc) {
    d->dlg       = makeTranslation_Widget(as_Widget(doc));
    d->startTime = 0;
    d->doc       = doc; /* owner */
    d->request   = new_TlsRequest();
    d->timer     = 0;
    setUserData_Object(d->request, d->doc);
    setHost_TlsRequest(d->request,
                       collectNewCStr_String(translationServiceHost),
                       translationServicePort);
    iConnect(TlsRequest, d->request, finished, d->request, finished_Translation_);
}

void deinit_Translation(iTranslation *d) {
    if (d->timer) {
        SDL_RemoveTimer(d->timer);
    }
    cancel_TlsRequest(d->request);
    iRelease(d->request);
    destroy_Widget(d->dlg);
}

static uint32_t animate_Translation_(uint32_t interval, iAny *ptr) {
    postCommandf_App("translation.update ptr:%p", ((iTranslation *) ptr)->doc);
    return interval;
}

void submit_Translation(iTranslation *d) {
    /* Check the selected languages from the dialog. */
    const char *idFrom = languageId_String(text_LabelWidget(findChild_Widget(d->dlg, "xlt.from")));
    const char *idTo   = languageId_String(text_LabelWidget(findChild_Widget(d->dlg, "xlt.to")));
    iAssert(status_TlsRequest(d->request) != submitted_TlsRequestStatus);
    iBlock *json = collect_Block(new_Block(0));
    iString *docSrc = collect_String(copy_String(source_GmDocument(document_DocumentWidget(d->doc))));
    replace_String(docSrc, "=>", doubleArrowSymbol);
    replace_String(docSrc, "```", tripleBacktickSymbol);
    replace_String(docSrc, "###", h3Symbol);
    replace_String(docSrc, "##", h2Symbol);
    replace_String(docSrc, "#", h1Symbol);
    printf_Block(json,
                 "{\"q\":\"%s\",\"source\":\"%s\",\"target\":\"%s\"}",
                 cstrCollect_String(quote_String_(docSrc)),
                 idFrom,
                 idTo);
    iBlock *msg = collect_Block(new_Block(0));
    printf_Block(msg, "POST /translate HTTP/1.1\r\n"
                      "Host: xlt.skyjake.fi\r\n"
                      "Connection: close\r\n"
                      "Content-Type: application/json\r\n"
                      "Content-Length: %zu\r\n\r\n", size_Block(json));
    append_Block(msg, json);
    setContent_TlsRequest(d->request, msg);
    submit_TlsRequest(d->request);
    d->startTime = SDL_GetTicks();
    d->timer = SDL_AddTimer(1000 / 30, animate_Translation_, d);
}

static void processResult_Translation_(iTranslation *d) {
    SDL_RemoveTimer(d->timer);
    d->timer = 0;
    if (status_TlsRequest(d->request) == error_TlsRequestStatus) {
        return;
    }
    iBlock *resultData = collect_Block(readAll_TlsRequest(d->request));
//    printf("result(%zu):\n%s\n", size_Block(resultData), cstr_Block(resultData));
//    fflush(stdout);
    iRegExp *pattern = iClob(new_RegExp(".*translatedText\":\"(.*)\"\\}", caseSensitive_RegExpOption));
    iRegExpMatch m;
    init_RegExpMatch(&m);
    if (matchRange_RegExp(pattern, range_Block(resultData), &m)) {
        iString *translation = unquote_String_(collect_String(captured_RegExpMatch(&m, 1)));
        replace_String(translation, tripleBacktickSymbol, "```");
        replace_String(translation, doubleArrowSymbol, "=>");
        replace_String(translation, h3Symbol, "###");
        replace_String(translation, h2Symbol, "##");
        replace_String(translation, h1Symbol, "#");
        setSource_DocumentWidget(d->doc, translation);
        postCommand_App("sidebar.update");
        delete_String(translation);
    }
    else {
        /* TODO: Report failure! */
    }
}

static iLabelWidget *acceptButton_Translation_(const iTranslation *d) {
    return (iLabelWidget *) lastChild_Widget(findChild_Widget(d->dlg, "dialogbuttons"));
}

iBool handleCommand_Translation(iTranslation *d, const char *cmd) {
    iWidget *w = as_Widget(d->doc);
    if (equalWidget_Command(cmd, w, "translation.submit")) {
        if (status_TlsRequest(d->request) == initialized_TlsRequestStatus) {
            iWidget *langs = findChild_Widget(d->dlg, "xlt.langs");
            setFlags_Widget(langs, hidden_WidgetFlag, iTrue);
            iLabelWidget *acceptButton = acceptButton_Translation_(d);
            updateTextCStr_LabelWidget(acceptButton, "00:00");
            setFlags_Widget(as_Widget(acceptButton), disabled_WidgetFlag, iTrue);
            iTranslationProgressWidget *prog = new_TranslationProgressWidget();
            setPos_Widget(as_Widget(prog), langs->rect.pos);
            setSize_Widget(as_Widget(prog), langs->rect.size);
            addChild_Widget(d->dlg, iClob(prog));
            submit_Translation(d);
        }
        return iTrue;
    }
    if (equalWidget_Command(cmd, w, "translation.update")) {
        const uint32_t elapsed = SDL_GetTicks() - d->startTime;
        const unsigned seconds = (elapsed / 1000) % 60;
        const unsigned minutes = (elapsed / 60000);
        updateText_LabelWidget(acceptButton_Translation_(d),
                               collectNewFormat_String("%02u:%02u", minutes, seconds));
        return iTrue;
    }
    if (equalWidget_Command(cmd, w, "translation.finished")) {
        if (!isFinished_Translation(d)) {
            processResult_Translation_(d);
            destroy_Widget(d->dlg);
            d->dlg = NULL;
        }
        return iTrue;
    }
    if (equalWidget_Command(cmd, d->dlg, "translation.cancel")) {
        cancel_TlsRequest(d->request);
        return iTrue;
    }
    return iFalse;
}

iBool isFinished_Translation(const iTranslation *d) {
    return d->dlg == NULL;
}
