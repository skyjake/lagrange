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
#include "defs.h"
#include "gmdocument.h"
#include "ui/documentwidget.h"
#include "ui/labelwidget.h"
#include "ui/paint.h"
#include "ui/util.h"

#include <the_Foundation/regexp.h>
#include <the_Foundation/stringlist.h>
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
    iString message;
};

void init_TranslationProgressWidget(iTranslationProgressWidget *d) {
    iWidget *w = &d->widget;
    init_Widget(w);
    setId_Widget(w, "xlt.progress");
    init_Array(&d->sprites, sizeof(iSprite));
    d->startTime = SDL_GetTicks();
    init_String(&d->message);
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
        spr->xoff = (width - measureRange_Text(d->font, range_String(&spr->text)).advance.x) / 2;
        spr->size = init_I2(width, lineHeight_Text(d->font));
        x += width + gap;
    }
}

void deinit_TranslationProgressWidget(iTranslationProgressWidget *d) {
    deinit_String(&d->message);
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
    if (!isEmpty_String(&d->message)) {
        drawCentered_Text(
            uiLabel_FontId, bounds, iFalse, uiText_ColorId, "%s", cstr_String(&d->message));
        return;
    }
    iPaint p;
    init_Paint(&p);
    const iInt2 mid = mid_Rect(bounds);
    SDL_SetRenderDrawBlendMode(renderer_Window(get_Window()), SDL_BLENDMODE_BLEND);
    const int palette[] = {
        uiBackgroundSelected_ColorId,
        red_ColorId,
        blue_ColorId,
        green_ColorId,
    };
    iConstForEach(Array, i, &d->sprites) {
        const int      index   = (int) index_ArrayConstIterator(&i);
        const float    angle   = (float) index;
        const iSprite *spr     = i.value;
        const float    opacity = iClamp(t - index * 0.5f, 0.0, 1.0f);
        const float    palPos  = index * 0.025f + t / 10;
        const int      palCur  = (size_t)(palPos) % iElemCount(palette);
        const int      palNext = (palCur + 1) % iElemCount(palette);

        int fg = palCur == 0                            ? uiTextSelected_ColorId
                 : isLight_ColorTheme(colorTheme_App()) ? white_ColorId
                                                        : black_ColorId;
        iInt2 pos = add_I2(mid, spr->pos);
        float t2 = sin(0.2f * t);
        pos.y += sin(angle + t) * spr->size.y * t2 * t2 * iClamp(t * 0.25f - 0.3f, 0.0f, 1.0f);
        p.alpha = opacity * 255;
        const iColor back = mix_Color(
            get_Color(palette[palCur]), get_Color(palette[palNext]), palPos - (int) palPos);
        SDL_SetRenderDrawColor(renderer_Window(get_Window()), back.r, back.g, back.b, p.alpha);
        SDL_RenderFillRect(renderer_Window(get_Window()),
                           &(SDL_Rect){ pos.x + origin_Paint.x, pos.y + origin_Paint.y,
                                        spr->size.x, spr->size.y });
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

/* TODO: Move these quote/unquote methods to the_Foundation. */

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
        else if (ch >= 0x80) {
            if ((ch >= 0xD800 && ch < 0xE000) || ch >= 0x10000) {
                /* TODO: Add a helper function? */
                /* UTF-16 surrogate pair */
                iString *chs = newUnicodeN_String(&ch, 1);
                iBlock *u16 = toUtf16_String(chs);
                delete_String(chs);
                const uint16_t *ch16 = constData_Block(u16);
                appendFormat_String(quot, "\\u%04x\\u%04x", ch16[0], ch16[1]);
            }
            else {
                appendFormat_String(quot, "\\u%04x", ch);
            }
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
                for (size_t j = 0; j < 4; j++) {
                    next_StringConstIterator(&i);
                    digits[j] = *i.pos;
                }
                uint16_t ch16[2] = { strtoul(digits, NULL, 16), 0 };
                if (ch16[0] < 0xD800 || ch16[0] >= 0xE000) {
                    appendChar_String(unquot, ch16[0]);
                }
                else {
                    /* UTF-16 surrogate pair */
                    next_StringConstIterator(&i);
                    next_StringConstIterator(&i);
                    iZap(digits);
                    for (size_t j = 0; j < 4; j++) {
                        next_StringConstIterator(&i);
                        digits[j] = *i.pos;
                    }
                    ch16[1] = strtoul(digits, NULL, 16);
                    iString *u16 = newUtf16N_String(ch16, 2);
                    append_String(unquot, u16);
                    delete_String(u16);
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
    d->includingPreformatted = iFalse;
    d->linePrefixes = new_StringArray();
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
    iRelease(d->linePrefixes);
}

static uint32_t animate_Translation_(uint32_t interval, iAny *ptr) {
    postCommandf_App("translation.update ptr:%p", ((iTranslation *) ptr)->doc);
    return interval;
}

void submit_Translation(iTranslation *d) {
    iAssert(status_TlsRequest(d->request) != submitted_TlsRequestStatus);
    /* Check the selected languages from the dialog. */
    const char *idFrom = languageId_String(text_LabelWidget(findChild_Widget(d->dlg, "xlt.from")));
    const char *idTo   = languageId_String(text_LabelWidget(findChild_Widget(d->dlg, "xlt.to")));
    /* Remember these in Preferences. */
    postCommandf_App("translation.languages from:%d to:%d pre:%d",
                     languageIndex_CStr(idFrom),
                     languageIndex_CStr(idTo),
                     d->includingPreformatted);
    iBlock * json   = collect_Block(new_Block(0));
    iString *docSrc = collectNew_String();
    iRegExp *linkPattern = iClob(new_RegExp("^=>\\s*([^\\s]+)(\\s+(.*))?$", 0));
    clear_StringArray(d->linePrefixes);
    /* The translation engine doesn't preserve Gemtext markup so we'll strip all of it and
       remember each line's type. These are reapplied when reading the response. Newlines seem
       to be preserved pretty well. */ {
        iBool inPreformatted = iFalse;
        iRangecc line = iNullRange;
        size_t xlatIndex = 0;
        while (nextSplit_Rangecc(
            range_String(source_GmDocument(document_DocumentWidget(d->doc))), "\n", &line)) {
            iRangecc cleanLine = trimmed_Rangecc(line);
            iRangecc prefixPart = iNullRange;
            iRangecc translatedPart = iNullRange;
            const int lineType = lineType_Rangecc(cleanLine);
            if (inPreformatted) {
                if (lineType == preformatted_GmLineType) {
                    inPreformatted = iFalse;
                    prefixPart = cleanLine;
                }
                else if (d->includingPreformatted) {
                    translatedPart = cleanLine;
                }
                else {
                    prefixPart = line; /* preserve original whitespace */
                }
            }
            else switch (lineType) {
                case link_GmLineType: {
                    iRegExpMatch m;
                    init_RegExpMatch(&m);
                    matchRange_RegExp(linkPattern, cleanLine, &m);
                    const iRangecc label = capturedRange_RegExpMatch(&m, 3);
                    if (!isEmpty_Range(&label)) {
                        prefixPart = (iRangecc){ cleanLine.start, label.start };
                        translatedPart = label;
                    }
                    else {
                        prefixPart = cleanLine;
                    }
                    break;
                }
                case preformatted_GmLineType:
                    prefixPart = (iRangecc){ cleanLine.start, cleanLine.start + 3 };
                    translatedPart = (iRangecc){ prefixPart.end, cleanLine.end };
                    inPreformatted = iTrue;
                    break;
                case heading1_GmLineType:
                case quote_GmLineType:
                    prefixPart = (iRangecc){ cleanLine.start, cleanLine.start + 1 };
                    translatedPart = (iRangecc){ prefixPart.end, cleanLine.end };
                    break;
                case heading2_GmLineType:
                case bullet_GmLineType:
                    prefixPart = (iRangecc){ cleanLine.start, cleanLine.start + 2 };
                    translatedPart = (iRangecc){ prefixPart.end, cleanLine.end };
                    break;
                case heading3_GmLineType:
                    prefixPart = (iRangecc){ cleanLine.start, cleanLine.start + 3 };
                    translatedPart = (iRangecc){ prefixPart.end, cleanLine.end };
                    break;                    
                default:
                    translatedPart = cleanLine;
                    break;
            }
            if (!isEmpty_Range(&translatedPart)) {
                if (!isEmpty_String(docSrc)) {
                    appendCStr_String(docSrc, "\n");
                    xlatIndex++;
                }
                appendRange_String(docSrc, translatedPart);
            }
            iString templ;
            initRange_String(&templ, prefixPart);
            if (!isEmpty_Range(&translatedPart)) {
                appendFormat_String(&templ, " ${%u:xlatIndex}", xlatIndex);
            }
            pushBack_StringArray(d->linePrefixes, &templ);
            deinit_String(&templ);
        }
    }
//    printf("\n---\n%s\n---\n", cstr_String(docSrc));
    printf_Block(json,
                 "{\"q\":\"%s\",\"source\":\"%s\",\"target\":\"%s\"}",
                 cstrCollect_String(quote_String_(docSrc)),
                 idFrom,
                 idTo);
    iBlock *msg = collect_Block(new_Block(0));
    printf_Block(msg,
                 "POST /translate HTTP/1.1\r\n"
                 "Host: %s\r\n"
                 "Connection: close\r\n"
                 "Content-Type: application/json; charset=utf-8\r\n"
                 "Content-Length: %zu\r\n\r\n",
                 translationServiceHost,
                 size_Block(json));
    append_Block(msg, json);
    setContent_TlsRequest(d->request, msg);
    submit_TlsRequest(d->request);
    d->startTime = SDL_GetTicks();
    d->timer     = SDL_AddTimer(1000 / 30, animate_Translation_, d);
}

static void setFailed_Translation_(iTranslation *d, const char *msg) {
    iTranslationProgressWidget *prog = findChild_Widget(d->dlg, "xlt.progress");
    if (prog && isEmpty_String(&prog->message)) {
        setCStr_String(&prog->message, translateCStr_Lang(msg));
    }
}

static iBool processResult_Translation_(iTranslation *d) {
    SDL_RemoveTimer(d->timer);
    d->timer = 0;
    if (status_TlsRequest(d->request) == error_TlsRequestStatus) {
        setFailed_Translation_(d, explosion_Icon "  ${dlg.translate.fail}");
        return iFalse;
    }
    iBlock *resultData = collect_Block(readAll_TlsRequest(d->request));
//    printf("result(%zu):\n%s\n", size_Block(resultData), cstr_Block(resultData));
//    fflush(stdout);    
    iRegExp *pattern = iClob(new_RegExp(".*translatedText\":\"(.*)\"\\}", caseSensitive_RegExpOption));
    iRegExpMatch m;
    init_RegExpMatch(&m);
    if (matchRange_RegExp(pattern, range_Block(resultData), &m)) {
        iString *translation = unquote_String_(collect_String(captured_RegExpMatch(&m, 1)));
        iString *result = collectNew_String();
        size_t lineIndex = 0;
        iStringList *xlatLines = iClob(split_String(translation, "\n"));
        iConstForEach(StringArray, i, d->linePrefixes) {
            if (endsWith_String(i.value, ":xlatIndex}")) {
                iRangecc idRange;
                idRange.start = idRange.end = constEnd_String(i.value) - 11;
                while (idRange.start > constBegin_String(i.value) &&
                       isNumeric_Char(idRange.start[-1])) {
                    idRange.start--;
                }
                iString idStr;
                initRange_String(&idStr, idRange);
                const size_t xlatIndex = toInt_String(&idStr);
                deinit_String(&idStr);
                appendRange_String(result, (iRangecc){ constBegin_String(i.value),
                                                       idRange.start - 2 });
                if (xlatIndex < size_StringList(xlatLines)) {
                    append_String(result, at_StringList(xlatLines, xlatIndex));
                }
            }
            else {
                append_String(result, i.value);
            }
            appendCStr_String(result, "\n");
        }
#if 0
        while (nextSplit_Rangecc(range_String(translation), "\n", &line)) {
            iRangecc cleanLine = trimmed_Rangecc(line);
            if (!isEmpty_String(marked)) {
                appendCStr_String(marked, "\n");
            }
            if (lineIndex < size_StringArray(d->linePrefixes)) { /* translator messed up the lines? */
                append_String(marked, at_StringArray(d->linePrefixes, lineIndex));
            }
            appendRange_String(marked, cleanLine);
            lineIndex++;
        }
#endif
        setSource_DocumentWidget(d->doc, result);
        postCommand_App("sidebar.update");
        delete_String(translation);
    }
    else {
        setFailed_Translation_(d, unhappy_Icon "  ${dlg.translate.unavail}");
        return iFalse;
    }
    return iTrue;
}

static iLabelWidget *acceptButton_Translation_(const iTranslation *d) {
    return dialogAcceptButton_Widget(d->dlg);
}

iBool handleCommand_Translation(iTranslation *d, const char *cmd) {
    iWidget *w = as_Widget(d->doc);
    if (equalWidget_Command(cmd, w, "translation.submit")) {
        if (status_TlsRequest(d->request) == initialized_TlsRequestStatus) {
            d->includingPreformatted = !isSelected_Widget(findChild_Widget(d->dlg, "xlt.preskip"));
            iWidget *langs = findChild_Widget(d->dlg, "xlt.langs");
            setFlags_Widget(langs, hidden_WidgetFlag, iTrue);
            setFlags_Widget(findChild_Widget(d->dlg, "xlt.from"),  hidden_WidgetFlag, iTrue);
            setFlags_Widget(findChild_Widget(d->dlg, "xlt.to"),    hidden_WidgetFlag, iTrue);
            if (isUsingPanelLayout_Mobile()) {
                setFlags_Widget(findChild_Widget(d->dlg, "panel.top"), hidden_WidgetFlag, iTrue);
                refresh_Widget(findChild_Widget(d->dlg, "panel.top"));
            }
            if (!langs) {
                langs = d->dlg;
            }
            iLabelWidget *acceptButton = acceptButton_Translation_(d);
            updateTextCStr_LabelWidget(acceptButton, "00:00");
            setFlags_Widget(as_Widget(acceptButton), disabled_WidgetFlag, iTrue);
            iTranslationProgressWidget *prog = new_TranslationProgressWidget();
            if (isUsingPanelLayout_Mobile()) {
                setPos_Widget(as_Widget(prog), init_I2(0, 3 * gap_UI)); /* TODO: No fixed offsets... */
            }
            else {
                setPos_Widget(as_Widget(prog), langs->rect.pos);
                
            }
            setFixedSize_Widget(as_Widget(prog), init_I2(width_Rect(innerBounds_Widget(d->dlg)),
                                                         langs->rect.size.y));
            addChildFlags_Widget(d->dlg, iClob(prog), 0);
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
            if (processResult_Translation_(d)) {
                setupSheetTransition_Mobile(d->dlg, dialogTransitionDir_Widget(d->dlg));
                destroy_Widget(d->dlg);
                d->dlg = NULL;
            }
        }
        return iTrue;
    }
    if (equalWidget_Command(cmd, d->dlg, "translation.cancel")) {
        if (status_TlsRequest(d->request) == submitted_TlsRequestStatus) {
            setFailed_Translation_(d, "Cancelled");
            updateTextCStr_LabelWidget(
                findMenuItem_Widget(findChild_Widget(d->dlg, "dialogbuttons"),
                                    "translation.cancel"),
                "${close}");
            cancel_TlsRequest(d->request);
        }
        else {
            setupSheetTransition_Mobile(d->dlg, dialogTransitionDir_Widget(d->dlg));
            destroy_Widget(d->dlg);
            d->dlg = NULL;
        }
        return iTrue;
    }
    return iFalse;
}

iBool isFinished_Translation(const iTranslation *d) {
    return d->dlg == NULL;
}
