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
#include "command.h"
#include "gmrequest.h"
#include "app.h"

#include <the_Foundation/file.h>
#include <the_Foundation/fileinfo.h>

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
    iLabelWidget *   filePathLabel;
    iLabelWidget *   fileSizeLabel;
    iLabelWidget *   counter;
    iString          filePath;
    size_t           fileSize;
    iAtomicInt       isRequestUpdated;
};

static void updateProgress_UploadWidget_(iGmRequest *request, size_t current, size_t total) {
    iUploadWidget *d = userData_Object(request);
    postCommand_Widget(d,
                       "upload.request.updated reqid:%u arg:%zu total:%zu",
                       id_GmRequest(request),
                       current,
                       total);
}

void init_UploadWidget(iUploadWidget *d) {
    iWidget *w = as_Widget(d);
    init_Widget(w);
    setId_Widget(w, "upload");
    useSheetStyle_Widget(w);
    init_String(&d->url);
    d->viewer = NULL;
    d->request = NULL;
    init_String(&d->filePath);
    d->fileSize = 0;
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
        setFont_InputWidget(d->input, monospace_FontId);
        setLineLimits_InputWidget(d->input, 7, 20);
        setHint_InputWidget(d->input, "${hint.upload.text}");
        setEnterInsertsLF_InputWidget(d->input, iTrue);
        setFixedSize_Widget(as_Widget(d->input), init_I2(120 * gap_UI, -1));
        addChild_Widget(page, iClob(d->input));
        appendFramelessTabPage_Widget(tabs, iClob(page), "${heading.upload.text}", '1', 0);
    }
    /* File content. */ {
        appendTwoColumnTabPage_Widget(tabs, "${heading.upload.file}", '2', &headings, &values);        
//        iWidget *pad = addChild_Widget(headings, iClob(makePadding_Widget(0)));
//        iWidget *hint = addChild_Widget(values, iClob(new_LabelWidget("${upload.file.drophint}", NULL)));
//        pad->sizeRef = hint;
        addChild_Widget(headings, iClob(new_LabelWidget("${upload.file.name}", NULL)));
        d->filePathLabel = addChild_Widget(values, iClob(new_LabelWidget(uiTextAction_ColorEscape "${upload.file.drophere}", NULL)));
        addChild_Widget(headings, iClob(new_LabelWidget("${upload.file.size}", NULL)));        
        d->fileSizeLabel = addChild_Widget(values, iClob(new_LabelWidget("\u2014", NULL)));
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
        setId_Widget(addChildPosFlags_Widget(buttons,
                                             iClob(d->counter = new_LabelWidget("", NULL)),
                                             front_WidgetAddPos, frameless_WidgetFlag),
                     "upload.counter");
        addChild_Widget(w, iClob(buttons));
    }
    resizeToLargestPage_Widget(tabs);
    setFocus_Widget(as_Widget(d->input));
}

void deinit_UploadWidget(iUploadWidget *d) {
    deinit_String(&d->filePath);
    deinit_String(&d->url);
    iRelease(d->request);
}

void setUrl_UploadWidget(iUploadWidget *d, const iString *url) {
    iUrl parts;
    init_Url(&parts, url);
    setCStr_String(&d->url, "titan");
    appendRange_String(&d->url, (iRangecc){ parts.scheme.end, constEnd_String(url) });
    setText_LabelWidget(d->info, &d->url);
}

void setResponseViewer_UploadWidget(iUploadWidget *d, iDocumentWidget *doc) {
    d->viewer = doc;
}

static iWidget *acceptButton_UploadWidget_(iUploadWidget *d) {
    return lastChild_Widget(findChild_Widget(as_Widget(d), "dialogbuttons"));
}

#if 0
static void requestUpdated_UploadWidget_(iUploadWidget *d, iGmRequest *req) {
    if (!exchange_Atomic(&d->isRequestUpdated, iTrue)) {
        postCommand_Widget(d, "upload.request.updated reqid:%u", id_GmRequest(req));
    }
}
#endif

static void requestFinished_UploadWidget_(iUploadWidget *d, iGmRequest *req) {
    postCommand_Widget(d, "upload.request.finished reqid:%u", id_GmRequest(req));
}

static iBool processEvent_UploadWidget_(iUploadWidget *d, const SDL_Event *ev) {
    iWidget *w = as_Widget(d);
    if (isCommand_Widget(w, ev, "upload.cancel")) {
        /* TODO: If text has been entered, ask for confirmation. */
        setupSheetTransition_Mobile(w, iFalse);
        destroy_Widget(w);
        return iTrue;
    }
    const char *cmd = command_UserEvent(ev);
    if (isCommand_Widget(w, ev, "upload.accept")) {
        iWidget * tabs     = findChild_Widget(w, "upload.tabs");
        const int tabIndex = tabPageIndex_Widget(tabs, currentTabPage_Widget(tabs));
        /* Make a GmRequest and send the data. */
        iAssert(d->request == NULL);
        iAssert(!isEmpty_String(&d->url));
        d->request = new_GmRequest(certs_App());
        setSendProgressFunc_GmRequest(d->request, updateProgress_UploadWidget_);
        setUserData_Object(d->request, d);
        setUrl_GmRequest(d->request, &d->url);
        if (tabIndex == 0) {
            /* Uploading text. */
            setTitanData_GmRequest(d->request,
                                   collectNewCStr_String("text/plain"),
                                   utf8_String(text_InputWidget(d->input)),
                                   text_InputWidget(d->token));
        }
        else {
            /* Uploading a file. */
            iFile *f = iClob(new_File(&d->filePath));
            if (!open_File(f, readOnly_FileMode)) {
                makeMessage_Widget("${heading.upload.error.file}",
                                   "${upload.error.msg}",
                                   (iMenuItem[]){ "${dlg.message.ok}", 0, 0, "message.ok" }, 1);
                iReleasePtr(&d->request);
                return iTrue;
            }
            setTitanData_GmRequest(d->request,
                                   text_InputWidget(d->mime),
                                   collect_Block(readAll_File(f)),
                                   text_InputWidget(d->token));
            close_File(f);
        }
//        iConnect(GmRequest, d->request, updated,  d, requestUpdated_UploadWidget_);
        iConnect(GmRequest, d->request, finished, d, requestFinished_UploadWidget_);
        submit_GmRequest(d->request);
        /* The dialog will remain open until the request finishes, showing upload progress. */
        setFocus_Widget(NULL);
        setFlags_Widget(tabs, disabled_WidgetFlag, iTrue);
        setFlags_Widget(as_Widget(d->token), disabled_WidgetFlag, iTrue);
        setFlags_Widget(acceptButton_UploadWidget_(d), disabled_WidgetFlag, iTrue);
        return iTrue;
    }
    else if (isCommand_Widget(w, ev, "upload.request.updated") &&
             id_GmRequest(d->request) == argU32Label_Command(cmd, "reqid")) {
        setText_LabelWidget(d->counter,
                            collectNewFormat_String("%u", argU32Label_Command(cmd, "arg")));
    }
    else if (isCommand_Widget(w, ev, "upload.request.finished") &&
             id_GmRequest(d->request) == argU32Label_Command(cmd, "reqid")) {
        if (d->viewer) {
            takeRequest_DocumentWidget(d->viewer, d->request);
            d->request = NULL; /* DocumentWidget has it now. */
        }
        setupSheetTransition_Mobile(w, iFalse);
        destroy_Widget(w);
        return iTrue;        
    }
    if (ev->type == SDL_DROPFILE) {
        /* Switch to File tab. */
        iWidget *tabs = findChild_Widget(w, "upload.tabs");
        showTabPage_Widget(tabs, tabPage_Widget(tabs, 1));
        setCStr_String(&d->filePath, ev->drop.file);
        iFileInfo *info = iClob(new_FileInfo(&d->filePath));
        if (isDirectory_FileInfo(info)) {
            makeMessage_Widget("${heading.upload.error.file}",
                               "${upload.error.directory}",
                               (iMenuItem[]){ "${dlg.message.ok}", 0, 0, "message.ok" }, 1);
            clear_String(&d->filePath);
            d->fileSize = 0;
            return iTrue;
        }
        d->fileSize = size_FileInfo(info);
        setText_LabelWidget(d->filePathLabel, &d->filePath);
        setTextCStr_LabelWidget(d->fileSizeLabel, formatCStrs_Lang("num.bytes.n", d->fileSize));
        setTextCStr_InputWidget(d->mime, mediaType_Path(&d->filePath));
        return iTrue;
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
