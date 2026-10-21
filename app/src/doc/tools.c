/* Copyright 2026 Jaakko Keränen <jaakko.keranen@iki.fi>

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

#include "impl.h"

#include "../app.h"
#include "../tooljob.h"
#include "fetch.h"
#include "ui/command.h"
#include "ui/dialog.h"
#include "ui/indicatorwidget.h"
#include "ui/menu.h"
#include "ui/mobile.h"
#include "ui/root.h"
#include "ui/translation.h"
#include "ui/uploadwidget.h"
#include "ui/util.h"

#include <lagrange/tools.h>

static iBool canRunTool_DocumentWidget_(const iDocumentWidget *d, const iTool *tool) {
    return tool && isUsable_Tool(tool) && !d->toolJob && !isRequestOngoing_DocumentFetch(d->fetch) &&
           !isEmpty_Block(&d->fetch->sourceContent) && matchMime_Tool(tool, &d->fetch->sourceMime);
}

static void runTool_DocumentWidget_(iDocumentWidget *d, const iTool *tool) {
    if (!canRunTool_DocumentWidget_(d, tool)) {
        return;
    }
    d->toolJob = new_ToolJob(tool, &d->fetch->sourceMime, &d->fetch->sourceContent, d->mod.url);
    start_ToolJob(d->toolJob);
    start_IndicatorWidget(d->progress);
}

static void finishTool_DocumentWidget_(iDocumentWidget *d) {
    iToolJob *job = d->toolJob;
    d->toolJob = NULL;
    finish_IndicatorWidget(d->progress);
    /* The page may have changed while the tool was running. */
    if (equal_String(url_ToolJob(job), d->mod.url) &&
        !cmp_Block(body_ToolJob(job), &d->fetch->sourceContent)) {
        iString mime;
        iBlock  body;
        init_String(&mime);
        init_Block(&body, 0);
        if (output_ToolJob(job) && parseOutput_Tool(output_ToolJob(job), &mime, &body)) {
            setUrlAndSource_DocumentWidget(d, d->mod.url, &mime, &body, 0.0f);
        }
        else {
            makeSimpleMessage_Widget(uiTextCaution_ColorEscape "${heading.tool.failed}",
                                     format_CStr(cstr_Lang("dlg.tool.failed"),
                                                 cstr_String(&tool_ToolJob(job)->name)));
        }
        deinit_Block(&body);
        deinit_String(&mime);
    }
    delete_ToolJob(job);
}

void updateToolsMenu_DocumentWidget(iDocumentWidget *d) {
    iWidget *toolsMenu = findChild_Widget(as_Widget(d)->root->widget, "toolsmenu");
    if (!toolsMenu) {
        return;
    }
    iConstForEach(PtrArray, i, list_Tools(tools_App())) {
        const iTool *tool = i.ptr;
        if (tool->inMenu) {
            setMenuItemDisabled_Widget(toolsMenu,
                                       format_CStr("tool.run id:%s", cstr_String(&tool->id)),
                                       !canRunTool_DocumentWidget_(d, tool));
        }
    }
}

iBool handleToolCommand_DocumentWidget(iDocumentWidget *d, const char *cmd) {
    iWidget *w = as_Widget(d);
    if (equal_Command(cmd, "tool.run") && d == document_App()) {
        runTool_DocumentWidget_(d, find_Tools(tools_App(), string_Command(cmd, "id")));
        return iTrue;
    }
    else if (equal_Command(cmd, "tool.finished") && d->toolJob &&
             pointerLabel_Command(cmd, "job") == d->toolJob) {
        finishTool_DocumentWidget_(d);
        return iTrue;
    }
    else if (equal_Command(cmd, "document.translate") && d == document_App()) {
        if (!d->translation) {
            d->translation = new_Translation(d);
        }
        return iTrue;
    }
    else if (startsWith_Command(cmd, "translation.") && d->translation) {
        const iBool wasHandled = handleCommand_Translation(d->translation, cmd);
        if (isFinished_Translation(d->translation)) {
            delete_Translation(d->translation);
            d->translation = NULL;
        }
        return wasHandled;
    }
    else if (equal_Command(cmd, "document.upload") && d == document_App()) {
        if (findChild_Widget(root_Widget(w), "upload")) {
            return iTrue; /* already open */
        }
        const iString *url = d->mod.url;
        if (hasLabel_Command(cmd, "url")) {
            url = collect_String(suffix_Command(cmd, "url"));
        }
        const iRangecc scheme = urlScheme_String(url);
        if (equalCase_Rangecc(scheme, "gemini") || equalCase_Rangecc(scheme, "titan") ||
            equalCase_Rangecc(scheme, "spartan")) {
            iUploadWidget *upload =
                new_UploadWidget(equalCase_Rangecc(scheme, "spartan") ? spartan_UploadProtocol
                                                                      : titan_UploadProtocol);
            setUrl_UploadWidget(upload, url);
            setResponseViewer_UploadWidget(upload, d);
            addChild_Widget(get_Root()->widget, iClob(upload));
            setupSheetTransition_Mobile(as_Widget(upload),
                                        incoming_TransitionFlag |
                                            dialogTransitionDir_Widget(as_Widget(upload)));
            if (argLabel_Command(cmd, "copy") && isUtf8_Rangecc(range_Block(&d->fetch->sourceContent))) {
                iString text;
                initBlock_String(&text, &d->fetch->sourceContent);
                setText_UploadWidget(upload, &text);
                deinit_String(&text);
            }
            /* User can resize upload dialogs. */
            setResizeId_Widget(as_Widget(upload), "upload");
            restoreWidth_Widget(as_Widget(upload));
            refresh_Widget(d);
        }
        return iTrue;
    }
    return iFalse;
}
