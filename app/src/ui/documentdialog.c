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

#include "documentdialog.h"

#include "app.h"
#include "command.h"
#include "dialog.h"
#include "gmdocument.h"
#include "labelwidget.h"
#include "mobile.h"
#include "render/text.h"
#include "root.h"
#include "simpledocumentwidget.h"
#include "util.h"
#include "window.h"

#include <lagrange/gmrequest.h>
#include <lagrange/gmutil.h>
#include <lagrange/lang.h>

struct Impl_DocumentDialog {
    iWidget                widget;
    iString                url;
    int                    flags;
    iBool                  isTitleFixed;
    iGmRequest            *request;
    iWidget               *header;
    iLabelWidget          *title;
    iLabelWidget          *back;
    iLabelWidget          *newTab;
    iSimpleDocumentWidget *doc;
};

iDefineObjectConstructionArgs(DocumentDialog, (const iString *url, int flags), url, flags)

/* Matches the navigation bar of the mobile panels. */
static int headerFont_DocumentDialog_(iBool isBold) {
    switch (deviceType_App()) {
        case phone_AppDeviceType:
            return isBold ? uiLabelBigBold_FontId : uiLabelBig_FontId;
        case tablet_AppDeviceType:
            return isBold ? uiLabelMediumBold_FontId : uiLabelMedium_FontId;
        default:
            return isBold ? uiLabelBold_FontId : uiLabel_FontId;
    }
}

static iBool isFullScreen_DocumentDialog_(const iDocumentDialog *d) {
    return deviceType_App() != desktop_AppDeviceType && ~d->flags & popup_DocumentDialogFlag;
}

/* Full-screen slides in from the right like the Settings panels. */
static int transitionDir_DocumentDialog_(const iDocumentDialog *d) {
    if (deviceType_App() == desktop_AppDeviceType) {
        return top_TransitionDir;
    }
    return isFullScreen_DocumentDialog_(d) ? right_TransitionDir : bottom_TransitionDir;
}

static void requestFinished_DocumentDialog_(iAnyObject *obj, iGmRequest *req) {
    iUnused(req);
    postCommand_Widget(obj, "docdlg.loaded");
}

void init_DocumentDialog(iDocumentDialog *d, const iString *url, int flags) {
    iWidget *w = as_Widget(d);
    init_Widget(w);
    setId_Widget(w, "docdlg");
    initCopy_String(&d->url, url);
    d->flags        = flags;
    d->isTitleFixed = iFalse;
    useSheetStyle_Widget(w);
    /* The size is determined by the window, not the contents. */
    setFlags_Widget(w, arrangeSize_WidgetFlag | overflowScrollable_WidgetFlag, iFalse);
    setFlags_Widget(w, moveToParentBottomEdge_WidgetFlag, deviceType_App() != desktop_AppDeviceType);
    /* Swiping from the left edge goes back, like in the Settings panels. */
    setFlags_Widget(w,
                    frameless_WidgetFlag | leftEdgeDraggable_WidgetFlag,
                    isFullScreen_DocumentDialog_(d));
    setPadding1_Widget(w, 0);
    /* Navigation bar: back, title, and opening the page in a tab. */ {
        d->header = addChildFlags_Widget(w, iClob(new_Widget()), 0);
        d->title  = addChildFlags_Widget(d->header,
                                        iClob(new_LabelWidget(cstr_String(url), NULL)),
                                        frameless_WidgetFlag | noBackground_WidgetFlag |
                                            resizeToParentWidth_WidgetFlag |
                                            resizeToParentHeight_WidgetFlag);
        setFont_LabelWidget(d->title, headerFont_DocumentDialog_(iTrue));
        setTruncateToFit_LabelWidget(d->title, iTrue);
        d->back = addChildFlags_Widget(
            d->header,
            iClob(newKeyMods_LabelWidget(flags & popup_DocumentDialogFlag
                                             ? "${close}"
                                             : leftAngle_Icon " ${panel.back}",
                                         SDLK_ESCAPE,
                                         0,
                                         "docdlg.close")),
            frameless_WidgetFlag | noBackground_WidgetFlag | extraPadding_WidgetFlag |
                alignLeft_WidgetFlag | moveToParentLeftEdge_WidgetFlag |
                resizeToParentHeight_WidgetFlag);
        checkIcon_LabelWidget(d->back);
        setFont_LabelWidget(d->back, headerFont_DocumentDialog_(iFalse));
        setTextColor_LabelWidget(d->back, uiTextAction_ColorId);
        d->newTab = addChildFlags_Widget(
            d->header,
            iClob(new_LabelWidget(openInTab_Icon, "docdlg.newtab")),
            frameless_WidgetFlag | noBackground_WidgetFlag | extraPadding_WidgetFlag |
                moveToParentRightEdge_WidgetFlag | resizeToParentHeight_WidgetFlag);
        setFont_LabelWidget(d->newTab, headerFont_DocumentDialog_(iTrue));
        setTextColor_LabelWidget(d->newTab, uiTextAction_ColorId);
    }
    d->doc = addChildFlags_Widget(w, iClob(new_SimpleDocumentWidget()), 0);
    setFlags_SimpleDocumentWidget(d->doc,
                                  reading_SimpleDocumentWidgetFlag |
                                      newTabLinks_SimpleDocumentWidgetFlag |
                                      keyScrolling_SimpleDocumentWidgetFlag,
                                  iTrue);
    d->request = new_GmRequest(certs_App());
    setUrl_GmRequest(d->request, url);
    iConnect(GmRequest, d->request, finished, d, requestFinished_DocumentDialog_);
}

void deinit_DocumentDialog(iDocumentDialog *d) {
    if (d->request) {
        iDisconnectObject(GmRequest, d->request, finished, d);
        cancel_GmRequest(d->request);
        iReleasePtr(&d->request);
    }
    deinit_String(&d->url);
}

static void updateLayout_DocumentDialog_(iDocumentDialog *d) {
    iWidget    *w         = as_Widget(d);
    const iRect safe      = safeRect_Root(w->root);
    const int   headerHgt = lineHeight_Text(headerFont_DocumentDialog_(iTrue)) + 4 * gap_UI;
    int         width, docHeight;
    if (deviceType_App() == desktop_AppDeviceType) {
        width     = iMin(width_Rect(safe), 140 * gap_UI);
        const int height = height_Rect(safe) * 4 / 5;
        /* Rounded bottom corners are not covered by the page. */
        setPadding_Widget(w, 0, 0, 0, radius_UI);
        setFixedSize_Widget(w, init_I2(width, height));
        docHeight = height - headerHgt - radius_UI;
    }
    else if (!isFullScreen_DocumentDialog_(d)) {
        width = deviceType_App() == phone_AppDeviceType ? width_Rect(safe)
                                                        : iMin(width_Rect(safe), 140 * gap_UI);
        /* Extends under the bottom safe area. */
        const int height = bottom_Rect(rect_Root(w->root)) - top_Rect(safe) - 2 * gap_UI;
        setFixedSize_Widget(w, init_I2(width, height));
        docHeight = height - headerHgt;
    }
    else {
        /* Covers the entire root so nothing underneath needs to be drawn. */
        const iRect root = rect_Root(w->root);
        width            = width_Rect(safe);
        setPadding_Widget(w,
                          left_Rect(safe) - left_Rect(root),
                          top_Rect(safe) - top_Rect(root),
                          right_Rect(root) - right_Rect(safe),
                          0);
        setFixedSize_Widget(w, root.size);
        docHeight = bottom_Rect(root) - top_Rect(safe) - headerHgt; /* under bottom safe area */
    }
    setFixedSize_Widget(d->header, init_I2(width, headerHgt));
    setFixedSize_Widget(as_Widget(d->doc), init_I2(width, docHeight));
    setFlags_Widget(w, arrangeVertical_WidgetFlag, iTrue);
    arrange_Widget(w);
    /* The title stays centered and clear of the buttons. */
    const int inset = iMax(width_Widget(d->back), width_Widget(d->newTab));
    setPadding_Widget(as_Widget(d->title), inset, 0, inset, 0);
    arrange_Widget(d->header);
}

static void dismiss_DocumentDialog_(iDocumentDialog *d) {
    iWidget *w = as_Widget(d);
    if (flags_Widget(w) & destroyPending_WidgetFlag) {
        return;
    }
    setupSheetTransition_Mobile(w, transitionDir_DocumentDialog_(d));
    destroy_Widget(w);
}

static void showContent_DocumentDialog_(iDocumentDialog *d) {
    const iGmRequest *req    = d->request;
    const int         status = status_GmRequest(req);
    iString          *source;
    if (isSuccess_GmStatusCode(status) &&
        startsWithCase_String(meta_GmRequest(req), "text/gemini")) {
        source = newBlock_String(body_GmRequest(req));
    }
    else {
        const iGmError *error = get_GmError(status);
        source = newFormat_String("# %s\n%s\n", error->title, cstr_String(meta_GmRequest(req)));
        translate_Lang(source);
    }
    setSource_SimpleDocumentWidget(d->doc, &d->url, source);
    delete_String(source);
    const iString *title = title_GmDocument(document_SimpleDocumentWidget(d->doc));
    if (!d->isTitleFixed && !isEmpty_String(title)) {
        updateText_LabelWidget(d->title, title); /* keeps the size */
    }
}

static iBool processEvent_DocumentDialog_(iDocumentDialog *d, const SDL_Event *ev) {
    iWidget *w = as_Widget(d);
    if (isResize_UserEvent(ev)) {
        updateLayout_DocumentDialog_(d);
    }
    else if (isCommand_Widget(w, ev, "docdlg.loaded")) {
        showContent_DocumentDialog_(d);
        return iTrue;
    }
    else if (isCommand_Widget(w, ev, "docdlg.close") || isCommand_Widget(w, ev, "swipe.back")) {
        dismiss_DocumentDialog_(d);
        return iTrue;
    }
    else if (isCommand_Widget(w, ev, "docdlg.newtab")) {
        postCommandf_Root(w->root, "open newtab:1 url:%s", cstr_String(&d->url));
        return iTrue;
    }
    else if (isCommand_UserEvent(ev, "open")) {
        /* Links open in the main window, so everything over it is closed. */
        dismiss_DocumentDialog_(d);
        if (findWidget_App("prefs")) {
            postCommand_App("prefs.dismiss");
        }
        return iFalse;
    }
    return processEvent_Widget(w, ev);
}

void setTitle_DocumentDialog(iDocumentDialog *d, const char *title) {
    updateTextCStr_LabelWidget(d->title, title);
    d->isTitleFixed = iTrue;
}

iWidget *makeDocumentDialog_Widget(const iString *url, int flags) {
    iDocumentDialog *d = new_DocumentDialog(url, flags);
    iWidget         *w = as_Widget(d);
    addChild_Widget(get_Root()->widget, iClob(d));
    updateLayout_DocumentDialog_(d);
    setupSheetTransition_Mobile(w, incoming_TransitionFlag | transitionDir_DocumentDialog_(d));
    submit_GmRequest(d->request);
    postRefresh_Window(get_Window());
    return w;
}

iBeginDefineSubclass(DocumentDialog, Widget)
    .processEvent = (iAny *) processEvent_DocumentDialog_,
    .draw         = draw_Widget,
iEndDefineSubclass(DocumentDialog)
