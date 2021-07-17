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

#include "uploadwidget.h"
#include "labelwidget.h"
#include "inputwidget.h"
#include "documentwidget.h"
#include "color.h"
#include "gmrequest.h"
#include "app.h"

iDefineObjectConstruction(UploadWidget)

struct Impl_UploadWidget {
    iWidget          widget;
    iString          url;
    iDocumentWidget *viewer;
    iGmRequest *     request;
    iLabelWidget *   info;
    iInputWidget *   mime;
    iInputWidget *   token;
    iInputWidget *   input;
};

void init_UploadWidget(iUploadWidget *d) {
    iWidget *w = as_Widget(d);
    init_Widget(w);
    setId_Widget(w, "upload");
    useSheetStyle_Widget(w);
    init_String(&d->url);
    d->viewer = NULL;
    d->request = NULL;
    addChildFlags_Widget(w,
                         iClob(new_LabelWidget(uiHeading_ColorEscape "${heading.upload}", NULL)),
                         frameless_WidgetFlag);
    d->info = addChildFlags_Widget(w, iClob(new_LabelWidget("", NULL)), frameless_WidgetFlag);
    /* Tabs for input data. */
    iWidget *tabs = makeTabs_Widget(w);
    iWidget *headings, *values;
    setBackgroundColor_Widget(findChild_Widget(tabs, "tabs.buttons"), uiBackgroundSidebar_ColorId);
    setId_Widget(tabs, "upload.tabs");
//    const int bigGap = lineHeight_Text(uiLabel_FontId) * 3 / 4;
    /* Text input. */ {
        //appendTwoColumnTabPage_Widget(tabs, "${heading.upload.text}", '1', &headings, &values);
        iWidget *page = new_Widget();
        setFlags_Widget(page, arrangeSize_WidgetFlag, iTrue);
        d->input = new_InputWidget(0);
        setEnterInsertsLF_InputWidget(d->input, iTrue);
        setFixedSize_Widget(as_Widget(d->input), init_I2(120 * gap_UI, -1));
        addChild_Widget(page, iClob(d->input));
        appendTabPage_Widget(tabs, iClob(page), "${heading.upload.text}", '1', 0);
    }
    /* File content. */ {
        appendTwoColumnTabPage_Widget(tabs, "${heading.upload.file}", '2', &headings, &values);        
//        iWidget *pad = addChild_Widget(headings, iClob(makePadding_Widget(0)));
//        iWidget *hint = addChild_Widget(values, iClob(new_LabelWidget("${upload.file.drophint}", NULL)));
//        pad->sizeRef = hint;
        addChild_Widget(headings, iClob(new_LabelWidget("${upload.file.name}", NULL)));
        addChild_Widget(values, iClob(new_LabelWidget("filename.ext", NULL)));
        addChild_Widget(headings, iClob(new_LabelWidget("${upload.file.size}", NULL)));        
        addChild_Widget(values, iClob(new_LabelWidget("0 KB", NULL)));
        d->mime = new_InputWidget(0);
        setFixedSize_Widget(as_Widget(d->mime), init_I2(50 * gap_UI, -1));
        addTwoColumnDialogInputField_Widget(headings, values, "${upload.mime}", "upload.mime", iClob(d->mime));
    }
    /* Token. */ {
        addChild_Widget(w, iClob(makePadding_Widget(gap_UI)));
        iWidget *page = makeTwoColumns_Widget(&headings, &values);
        d->token = addTwoColumnDialogInputField_Widget(
            headings, values, "${upload.token}", "upload.token", iClob(new_InputWidget(0)));
        setHint_InputWidget(d->token, "${hint.upload.token}");
        setFixedSize_Widget(as_Widget(d->token), init_I2(50 * gap_UI, -1));
        addChild_Widget(w, iClob(page));
    }
    /* Buttons. */ {
        addChild_Widget(w, iClob(makePadding_Widget(gap_UI)));
        iWidget *buttons =
            makeDialogButtons_Widget((iMenuItem[]){ { "${cancel}", SDLK_ESCAPE, 0, "upload.cancel" },
                                                    { uiTextAction_ColorEscape "${dlg.upload.send}",
                                                      SDLK_RETURN,
                                                      KMOD_PRIMARY,
                                                      "upload.accept" } },
                                     2);
        addChild_Widget(w, iClob(buttons));
    }
    resizeToLargestPage_Widget(tabs);
    setFocus_Widget(as_Widget(d->token));
}

void deinit_UploadWidget(iUploadWidget *d) {
    deinit_String(&d->url);
    iRelease(d->request);
}

void setUrl_UploadWidget(iUploadWidget *d, const iString *url) {
    set_String(&d->url, url);
    setText_LabelWidget(d->info, &d->url);
}

void setResponseViewer_UploadWidget(iUploadWidget *d, iDocumentWidget *doc) {
    d->viewer = doc;
}

static iBool processEvent_UploadWidget_(iUploadWidget *d, const SDL_Event *ev) {
    iWidget *w = as_Widget(d);
    if (isCommand_Widget(w, ev, "upload.cancel")) {
        /* TODO: If text has been entered, ask for confirmation. */
        setupSheetTransition_Mobile(w, iFalse);
        destroy_Widget(w);
        return iTrue;
    }
    if (isCommand_Widget(w, ev, "upload.accept")) {
        /* Make a GmRequest and send the data. */
        /* The dialog will remain open until the request finishes, showing upload progress. */
    }
    if (ev->type == SDL_DROPFILE) {
        /* Switch to File tab. */
    }
    return processEvent_Widget(w, ev);
}

static void draw_UploadWidget_(const iUploadWidget *d) {
    draw_Widget(constAs_Widget(d));
}

iBeginDefineSubclass(UploadWidget, Widget)
    .processEvent = (iAny *) processEvent_UploadWidget_,
    .draw         = (iAny *) draw_UploadWidget_,
iEndDefineSubclass(UploadWidget)
