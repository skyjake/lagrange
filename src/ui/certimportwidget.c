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

#include "certimportwidget.h"

#include "app.h"
#include "color.h"
#include "command.h"
#include "gmcerts.h"
#include "inputwidget.h"
#include "labelwidget.h"
#include "text.h"
#include "ui/util.h"

#if defined (iPlatformAppleMobile)
#   include "ios.h"
#   define pickFile_Mobile pickFile_iOS
#endif

#if defined (iPlatformAndroidMobile)
#   include "android.h"
#   define pickFile_Mobile pickFile_Android
#endif

#include <the_Foundation/file.h>
#include <the_Foundation/tlsrequest.h>
#include <the_Foundation/path.h>
#include <SDL_clipboard.h>

iDefineObjectConstruction(CertImportWidget)

static const int validColor_   = green_ColorId;
static const int validTextColor_ = uiText_ColorId;
static const int invalidColor_ = uiEmbossHover2_ColorId;

struct Impl_CertImportWidget {
    iWidget widget;
    iLabelWidget *info;
    iLabelWidget *crtLabel;
    iLabelWidget *keyLabel;
    iInputWidget *notes;
    iTlsCertificate *cert;
};

static const char *infoText_ = "${dlg.certimport.help}";

static iBool tryImport_CertImportWidget_(iCertImportWidget *d, const iBlock *data) {
    iBool ok = iFalse;
    iString pem;
    initBlock_String(&pem, data);
    iTlsCertificate *newCert = newPemKey_TlsCertificate(&pem, &pem);
    const iBool gotNewCrt = !isEmpty_TlsCertificate(newCert);
    const iBool gotNewKey = hasPrivateKey_TlsCertificate(newCert);
    if (d->cert && (gotNewCrt ^ gotNewKey)) { /* One new part? Merge with existing. */
        const iString *crt = collect_String(pem_TlsCertificate(gotNewCrt ? newCert : d->cert));
        const iString *key = collect_String(privateKeyPem_TlsCertificate(gotNewKey ? newCert : d->cert));
        delete_TlsCertificate(d->cert);
        delete_TlsCertificate(newCert);
        d->cert = newPemKey_TlsCertificate(crt, key);
        ok = iTrue;
    }
    else if (gotNewCrt || gotNewKey) {
        delete_TlsCertificate(d->cert);
        d->cert = newCert;
        ok = iTrue;
    }
    else {
        delete_TlsCertificate(newCert);
    }
    deinit_String(&pem);
    /* Update the labels. */ {
        if (d->cert && !isEmpty_TlsCertificate(d->cert)) {
            updateText_LabelWidget(d->crtLabel, subject_TlsCertificate(d->cert));
            setTextColor_LabelWidget(d->crtLabel, validTextColor_);
            setFrameColor_Widget(as_Widget(d->crtLabel), validColor_);
        }
        else {
            updateTextCStr_LabelWidget(d->crtLabel, "${dlg.certimport.nocert}");
            setTextColor_LabelWidget(d->crtLabel, invalidColor_);
            setFrameColor_Widget(as_Widget(d->crtLabel), invalidColor_);
        }
        if (d->cert && hasPrivateKey_TlsCertificate(d->cert)) {
            iString *fng = collect_String(
                hexEncode_Block(collect_Block(privateKeyFingerprint_TlsCertificate(d->cert))));
            insertData_Block(&fng->chars, size_String(fng) / 2, "\n", 1);
            updateText_LabelWidget(d->keyLabel, fng);
            setTextColor_LabelWidget(d->keyLabel, validTextColor_);
            setFrameColor_Widget(as_Widget(d->keyLabel), validColor_);
        }
        else {
            updateTextCStr_LabelWidget(d->keyLabel, "${dlg.certimport.nokey}");
            setTextColor_LabelWidget(d->keyLabel, invalidColor_);
            setFrameColor_Widget(as_Widget(d->keyLabel), invalidColor_);
        }
    }
    return ok;
}

void init_CertImportWidget(iCertImportWidget *d) {
    iWidget *w = as_Widget(d);
    const iMenuItem actions[] = {
#if defined (iPlatformAppleMobile) || defined (iPlatformAndroidMobile)
        { "${dlg.certimport.pickfile}", 0, 0, "certimport.pickfile" },
        { "${dlg.certimport.paste}", 0, 0, "certimport.paste" },
        { "---" },
#elif defined (iPlatformMobile)
        { "${dlg.certimport.paste}", 0, 0, "certimport.paste" },
        { "---" },
#endif
        { "${cancel}" },
        { uiTextAction_ColorEscape "${dlg.certimport.import}",
          SDLK_RETURN, KMOD_ACCEPT,
          "certimport.accept" }
    };
    init_Widget(w);
    setId_Widget(w, "certimport");
    d->cert = NULL;
    if (isUsingPanelLayout_Mobile()) {
        initPanels_Mobile(w, NULL, (iMenuItem[]){
            { "title id:heading.certimport" },
            { format_CStr("label id:certimport.info text:%s", infoText_) },
            //{ "padding" },
            { "label id:certimport.crt nowrap:1 frame:1" },
            { "padding arg:0.25" },
            { "label id:certimport.key nowrap:1 frame:1" },
            { "heading text:${dlg.certimport.notes}" },
            { "input id:certimport.notes hint:hint.certimport.description noheading:1" },
            { NULL }
        }, actions, iElemCount(actions));
        d->info     = findChild_Widget(w, "certimport.info");
        d->crtLabel = findChild_Widget(w, "certimport.crt");
        d->keyLabel = findChild_Widget(w, "certimport.key");
        d->notes    = findChild_Widget(w, "certimport.notes");
        setFont_LabelWidget(d->crtLabel, uiContent_FontId);
        setFont_LabelWidget(d->keyLabel, uiContent_FontId);
        setFixedSize_Widget(as_Widget(d->crtLabel), init_I2(-1, gap_UI * 12));
        setFixedSize_Widget(as_Widget(d->keyLabel), init_I2(-1, gap_UI * 12));
    }
    else {
        /* This should behave similar to sheets. */
        useSheetStyle_Widget(w);
        addDialogTitle_Widget(w, "${heading.certimport}", NULL);
        //d->info = addChildFlags_Widget(w, iClob(new_LabelWidget(infoText_, NULL)), frameless_WidgetFlag);
        d->info = addWrappedLabel_Widget(w, infoText_, NULL);
        addChild_Widget(w, iClob(makePadding_Widget(gap_UI)));
        d->crtLabel = new_LabelWidget("", NULL); {
            setFont_LabelWidget(d->crtLabel, uiContent_FontId);
            addChildFlags_Widget(w, iClob(d->crtLabel), 0);
        }
        d->keyLabel = new_LabelWidget("", NULL); {
            setFont_LabelWidget(d->keyLabel, uiContent_FontId);
            addChild_Widget(w, iClob(makePadding_Widget(gap_UI)));
            addChildFlags_Widget(w, iClob(d->keyLabel), 0);
        }
        addChild_Widget(w, iClob(makePadding_Widget(gap_UI)));
        /* TODO: Use makeTwoColumnWidget_() */
        iWidget *page = new_Widget(); {
            setFlags_Widget(page, arrangeHorizontal_WidgetFlag | arrangeSize_WidgetFlag, iTrue);
            iWidget *headings = addChildFlags_Widget(
                page, iClob(new_Widget()), arrangeVertical_WidgetFlag | arrangeSize_WidgetFlag);
            iWidget *values = addChildFlags_Widget(
                page, iClob(new_Widget()), arrangeVertical_WidgetFlag | arrangeSize_WidgetFlag);
            addTwoColumnDialogInputField_Widget(
                headings,
                values,
                "${dlg.certimport.notes}",
                "",
                iClob(d->notes = newHint_InputWidget(0, "${hint.certimport.description}")));
            as_Widget(d->notes)->rect.size.x = gap_UI * 70;
        }
        addChild_Widget(w, iClob(page));
        arrange_Widget(w);
        setFixedSize_Widget(as_Widget(d->crtLabel), init_I2(width_Widget(w) - 6.5 * gap_UI, gap_UI * 12));
        setFixedSize_Widget(as_Widget(d->keyLabel), init_I2(width_Widget(w) - 6.5 * gap_UI, gap_UI * 12));
        /* Buttons. */
        addChild_Widget(w, iClob(makePadding_Widget(gap_UI)));
        iWidget *buttons = makeDialogButtons_Widget(actions, iElemCount(actions));
        addChild_Widget(w, iClob(buttons));
    }
    setTextColor_LabelWidget(d->crtLabel, invalidColor_);
    setTextColor_LabelWidget(d->keyLabel, invalidColor_);
    setFrameColor_Widget(as_Widget(d->crtLabel), invalidColor_);
    setFrameColor_Widget(as_Widget(d->keyLabel), invalidColor_);
    if (deviceType_App() != desktop_AppDeviceType) {
        /* Try auto-pasting. */
        postCommand_App("certimport.paste");
    }
}

void deinit_CertImportWidget(iCertImportWidget *d) {
    delete_TlsCertificate(d->cert);
}

static iBool isComplete_CertImportWidget_(const iCertImportWidget *d) {
    return d->cert && !isEmpty_TlsCertificate(d->cert) && hasPrivateKey_TlsCertificate(d->cert);
}

void setPageContent_CertImportWidget(iCertImportWidget *d, const iBlock *content) {
    if (tryImport_CertImportWidget_(d, content)) {
        setTextCStr_LabelWidget(d->info, infoText_);
        if (isComplete_CertImportWidget_(d)) {
            setFocus_Widget(as_Widget(d->notes));
        }
    }
    else {
        setTextCStr_LabelWidget(
            d->info, format_CStr("${dlg.certimport.notfound.page} %s", infoText_));
    }
    arrange_Widget(as_Widget(d));
}

static iBool tryImportFromClipboard_CertImportWidget_(iCertImportWidget *d) {
    return tryImport_CertImportWidget_(d, collect_Block(newCStr_Block(SDL_GetClipboardText())));
}

static iBool tryImportFromFile_CertImportWidget_(iCertImportWidget *d, const iString *path) {
    iBool success = iFalse;
    iFile *f = new_File(path);
    if (open_File(f, readOnly_FileMode | text_FileMode)) {
        if (tryImport_CertImportWidget_(d, collect_Block(readAll_File(f)))) {
            success = iTrue;
            if (isComplete_CertImportWidget_(d)) {
                setFocus_Widget(as_Widget(d->notes));
            }
        }
        else {
            makeSimpleMessage_Widget(uiTextCaution_ColorEscape "${heading.certimport.dropped}",
                                     "${dlg.certimport.notfound}");
        }
    }
    iRelease(f);
    return success;
}

static iBool processEvent_CertImportWidget_(iCertImportWidget *d, const SDL_Event *ev) {
    iWidget *w = as_Widget(d);
    if (ev->type == SDL_KEYDOWN) {
        const int key  = ev->key.keysym.sym;
        const int mods = keyMods_Sym(ev->key.keysym.mod);
        if (key == SDLK_v && mods == KMOD_PRIMARY) {
            if (!tryImportFromClipboard_CertImportWidget_(d)) {
                makeSimpleMessage_Widget(uiTextCaution_ColorEscape "${heading.certimport.pasted}",
                                         "${dlg.certimport.notfound}");
            }
            refresh_Widget(d);
            return iTrue;
        }
    }
    if (isCommand_UserEvent(ev, "input.paste")) {
        if (!tryImportFromClipboard_CertImportWidget_(d)) {
            makeSimpleMessage_Widget(uiTextCaution_ColorEscape "${heading.certimport.pasted}",
                                     "${dlg.certimport.notfound}");
        }
        refresh_Widget(d);
        return iTrue;
    }
    if (isCommand_UserEvent(ev, "certimport.paste")) {
        tryImportFromClipboard_CertImportWidget_(d);
        return iTrue;
    }
    if (isCommand_Widget(w, ev, "cancel")) {
        setupSheetTransition_Mobile(w, dialogTransitionDir_Widget(w));
        destroy_Widget(w);
        return iTrue;
    }
    if (isCommand_Widget(w, ev, "certimport.accept")) {
        if (d->cert && !isEmpty_TlsCertificate(d->cert) && hasPrivateKey_TlsCertificate(d->cert)) {
            importIdentity_GmCerts(certs_App(), d->cert, text_InputWidget(d->notes));
            d->cert = NULL; /* taken */
            setupSheetTransition_Mobile(w, dialogTransitionDir_Widget(w));
            destroy_Widget(w);
            postCommand_App("idents.changed");
        }
        return iTrue;
    }
#if defined (iPlatformAppleMobile) || defined (iPlatformAndroidMobile)
    if (isCommand_UserEvent(ev, "certimport.pickfile")) {
        const char *cmd = command_UserEvent(ev);
        if (hasLabel_Command(cmd, "path")) {
            const iString *path = collect_String(suffix_Command(cmd, "path"));
            tryImportFromFile_CertImportWidget_(d, path);
            remove(cstr_String(path)); /* it is a temporary copy */
        }
        else {
            pickFile_Mobile("certimport.pickfile");
        }
        return iTrue;
    }
#endif
    if (ev->type == SDL_DROPFILE) {
        tryImportFromFile_CertImportWidget_(d, collectNewCStr_String(ev->drop.file));
        return iTrue;
    }
    return processEvent_Widget(w, ev);
}

static void draw_CertImportWidget_(const iCertImportWidget *d) {
    draw_Widget(constAs_Widget(d));
}

iBeginDefineSubclass(CertImportWidget, Widget)
    .processEvent = (iAny *) processEvent_CertImportWidget_,
    .draw         = (iAny *) draw_CertImportWidget_,
iEndDefineSubclass(CertImportWidget)
