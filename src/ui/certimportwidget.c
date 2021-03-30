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

#include <the_Foundation/file.h>
#include <the_Foundation/tlsrequest.h>
#include <the_Foundation/path.h>
#include <SDL_clipboard.h>

iDefineObjectConstruction(CertImportWidget)

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
            setTextCStr_LabelWidget(
                d->crtLabel,
                format_CStr("%s%s",
                            uiTextAction_ColorEscape,
                            cstrCollect_String(subject_TlsCertificate(d->cert))));
            setFrameColor_Widget(as_Widget(d->crtLabel), uiTextAction_ColorId);
        }
        else {
            setTextCStr_LabelWidget(d->crtLabel, uiTextCaution_ColorEscape "${dlg.certimport.nocert}");
            setFrameColor_Widget(as_Widget(d->crtLabel), uiTextCaution_ColorId);
        }
        if (d->cert && hasPrivateKey_TlsCertificate(d->cert)) {
            iString *fng = collect_String(
                hexEncode_Block(collect_Block(privateKeyFingerprint_TlsCertificate(d->cert))));
            insertData_Block(&fng->chars, size_String(fng) / 2, "\n", 1);
            setTextCStr_LabelWidget(
                d->keyLabel, format_CStr("%s%s", uiTextAction_ColorEscape, cstr_String(fng)));
            setFrameColor_Widget(as_Widget(d->keyLabel), uiTextAction_ColorId);
        }
        else {
            setTextCStr_LabelWidget(d->keyLabel, uiTextCaution_ColorEscape "${dlg.certimport.nokey}");
            setFrameColor_Widget(as_Widget(d->keyLabel), uiTextCaution_ColorId);
        }
    }
    return ok;
}

void init_CertImportWidget(iCertImportWidget *d) {
    iWidget *w = as_Widget(d);
    init_Widget(w);
    setId_Widget(w, "certimport");
    d->cert = NULL;
    /* This should behave similar to sheets. */  {
        setPadding1_Widget(w, 3 * gap_UI);
        setFrameColor_Widget(w, uiSeparator_ColorId);
        setBackgroundColor_Widget(w, uiBackground_ColorId);
        setFlags_Widget(w,
                        mouseModal_WidgetFlag | keepOnTop_WidgetFlag | arrangeVertical_WidgetFlag |
                            arrangeSize_WidgetFlag | centerHorizontal_WidgetFlag |
                            overflowScrollable_WidgetFlag,
                        iTrue);
    }
    addChildFlags_Widget(
        w,
        iClob(new_LabelWidget(uiHeading_ColorEscape "${heading.certimport}", NULL)),
        frameless_WidgetFlag);
    d->info = addChildFlags_Widget(w, iClob(new_LabelWidget(infoText_, NULL)), frameless_WidgetFlag);
    addChild_Widget(w, iClob(makePadding_Widget(gap_UI)));
    d->crtLabel = new_LabelWidget("", NULL); {
        setFont_LabelWidget(d->crtLabel, uiContent_FontId);
        addChildFlags_Widget(w, iClob(d->crtLabel), 0);
        setFrameColor_Widget(as_Widget(d->crtLabel), uiTextCaution_ColorId);
    }
    d->keyLabel = new_LabelWidget("", NULL); {
        setFont_LabelWidget(d->keyLabel, uiContent_FontId);
        addChild_Widget(w, iClob(makePadding_Widget(gap_UI)));
        addChildFlags_Widget(w, iClob(d->keyLabel), 0);
        setFrameColor_Widget(as_Widget(d->keyLabel), uiTextCaution_ColorId);
    }
    addChild_Widget(w, iClob(makePadding_Widget(gap_UI)));
    iWidget *page = new_Widget(); {
        setFlags_Widget(page, arrangeHorizontal_WidgetFlag | arrangeSize_WidgetFlag, iTrue);
        iWidget *headings = addChildFlags_Widget(
            page, iClob(new_Widget()), arrangeVertical_WidgetFlag | arrangeSize_WidgetFlag);
        iWidget *values = addChildFlags_Widget(
            page, iClob(new_Widget()), arrangeVertical_WidgetFlag | arrangeSize_WidgetFlag);
        addChild_Widget(headings, iClob(makeHeading_Widget("${dlg.certimport.notes}")));
        addChild_Widget(values, iClob(d->notes = new_InputWidget(0)));
        setHint_InputWidget(d->notes, "${hint.certimport.description}");
        as_Widget(d->notes)->rect.size.x = gap_UI * 70;
    }
    addChild_Widget(w, iClob(page));
    arrange_Widget(w);
    setFixedSize_Widget(as_Widget(d->crtLabel), init_I2(width_Widget(w) - 6.5 * gap_UI, gap_UI * 12));
    setFixedSize_Widget(as_Widget(d->keyLabel), init_I2(width_Widget(w) - 6.5 * gap_UI, gap_UI * 12));
    /* Buttons. */
    addChild_Widget(w, iClob(makePadding_Widget(gap_UI)));
    iWidget *buttons = makeDialogButtons_Widget(
        (iMenuItem[]){ { "${cancel}", 0, 0, NULL },
                       { uiTextAction_ColorEscape "${dlg.certimport.import}",
                         SDLK_RETURN,
                         KMOD_PRIMARY,
                         "certimport.accept" } },
        2);
    addChild_Widget(w, iClob(buttons));
    arrange_Widget(w);
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
            d->info, format_CStr("${dlg.certimport.notfound.page}\n%s", infoText_));
    }
    arrange_Widget(as_Widget(d));
}

static iBool tryImportFromClipboard_CertImportWidget_(iCertImportWidget *d) {
    return tryImport_CertImportWidget_(d, collect_Block(newCStr_Block(SDL_GetClipboardText())));
}

static iBool processEvent_CertImportWidget_(iCertImportWidget *d, const SDL_Event *ev) {
    iWidget *w = as_Widget(d);
    if (ev->type == SDL_KEYDOWN) {
        const int key  = ev->key.keysym.sym;
        const int mods = keyMods_Sym(ev->key.keysym.mod);
        if (key == SDLK_v && mods == KMOD_PRIMARY) {
            if (!tryImportFromClipboard_CertImportWidget_(d)) {
                makeMessage_Widget(uiTextCaution_ColorEscape "${heading.certimport.pasted}",
                                   "${dlg.certimport.notfound}");
            }
            postRefresh_App();
            return iTrue;
        }
    }
    if (isCommand_UserEvent(ev, "certimport.paste")) {
        tryImportFromClipboard_CertImportWidget_(d);
        return iTrue;
    }
    if (isCommand_Widget(w, ev, "cancel")) {
        destroy_Widget(w);
        return iTrue;
    }
    if (isCommand_Widget(w, ev, "certimport.accept")) {
        if (d->cert && !isEmpty_TlsCertificate(d->cert) && hasPrivateKey_TlsCertificate(d->cert)) {
            importIdentity_GmCerts(certs_App(), d->cert, text_InputWidget(d->notes));
            d->cert = NULL; /* taken */
            destroy_Widget(w);
            postCommand_App("idents.changed");
        }
        return iTrue;
    }
    if (ev->type == SDL_DROPFILE) {
        const iString *name = collectNewCStr_String(ev->drop.file);
        iFile *f = new_File(name);
        if (open_File(f, readOnly_FileMode | text_FileMode)) {
            if (tryImport_CertImportWidget_(d, collect_Block(readAll_File(f)))) {
                if (isComplete_CertImportWidget_(d)) {
                    setFocus_Widget(as_Widget(d->notes));
                }
            }
            else {
                makeMessage_Widget(uiTextCaution_ColorEscape "${heading.certimport.dropped}",
                                   "${dlg.certimport.notfound}");
            }
        }
        iRelease(f);
        return iTrue;
    }
    return processEvent_Widget(w, ev);
}

static void draw_CertImportWidget_(const iCertImportWidget *d) {
    const iWidget *w = constAs_Widget(d);
    draw_Widget(w);
}

iBeginDefineSubclass(CertImportWidget, Widget)
    .processEvent = (iAny *) processEvent_CertImportWidget_,
    .draw         = (iAny *) draw_CertImportWidget_,
iEndDefineSubclass(CertImportWidget)
