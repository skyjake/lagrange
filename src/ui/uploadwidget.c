/* Copyright 2021-2024 Jaakko Keränen <jaakko.keranen@iki.fi>

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
#include "root.h"
#include "color.h"
#include "command.h"
#include "gmrequest.h"
#include "sitespec.h"
#include "misfin.h"
#include "window.h"
#include "gmcerts.h"
#include "periodic.h"
#include "app.h"

#if defined (iPlatformAppleMobile)
#   include "ios.h"
#   define pickFile_Mobile pickFile_iOS
#endif

#if defined (iPlatformAndroidMobile)
#   include "android.h"
#   define pickFile_Mobile pickFile_Android
#endif

#include <the_Foundation/file.h>
#include <the_Foundation/fileinfo.h>
#include <the_Foundation/path.h>
#include <the_Foundation/thread.h>
#include <SDL_timer.h>

iDefineObjectConstructionArgs(UploadWidget, (enum iUploadProtocol protocol), protocol)

enum iUploadIdentity {
    none_UploadIdentity,
    defaultForSite_UploadIdentity,
    dropdown_UploadIdentity,
};

enum iMisfinStage {
    none_MisfinStage,
    verifyRecipient_MisfinStage, /* check if the recipient is valid, query fingerprint */
    sendToRecipient_MisfinStage,
    carbonCopyToSelf_MisfinStage,
};

struct Impl_UploadWidget {
    iWidget          widget;
    enum iUploadProtocol protocol;
    iString          originalUrl;
    iString          url;
    iDocumentWidget *viewer;
    iGmRequest *     request;
    iGmRequest *     editRequest; /* when editing, fetch the existing contents first */
    int              editRedirectCount;
    iBool            allowRetryEdit;
    enum iMisfinStage misfinStage;
    iWidget *        tabs;
    iLabelWidget *   info;
    iInputWidget *   path;
    iInputWidget *   mime;
    iInputWidget *   token;
    iLabelWidget *   ident;
    iInputWidget *   input;
    iLabelWidget *   filePathLabel;
    iInputWidget *   filePathInput;
    iLabelWidget *   fileSizeLabel;
    iLabelWidget *   editLabel;
    iLabelWidget *   counter;
    iString          filePath;
    size_t           fileSize;
    enum iUploadIdentity idMode;
    iBlock           idFingerprint;
    iAtomicInt       isRequestUpdated;
};

static void filePathValidator_UploadWidget_(iInputWidget *, void *);

static void releaseFile_UploadWidget_(iUploadWidget *d) {
#if defined (iPlatformAppleMobile) || defined (iPlatformAndroidMobile)
    if (!isEmpty_String(&d->filePath)) {
        /* Delete the temporary file that was copied for uploading. */
        remove(cstr_String(&d->filePath));
    }
#endif
    clear_String(&d->filePath);
}

static void updateProgress_UploadWidget_(iGmRequest *request, size_t current, size_t total) {
    iUploadWidget *d = userData_Object(request);
    postCommand_Widget(d,
                       "upload.request.updated reqid:%u arg:%zu total:%zu",
                       id_GmRequest(request),
                       current,
                       total);
}

static void updateInputMaxHeight_UploadWidget_(iUploadWidget *d) {
    iWidget *w = as_Widget(d);
    /* Calculate how many lines fits vertically in the view. */
    const iInt2 inputPos = topLeft_Rect(boundsWithoutVisualOffset_Widget(as_Widget(d->input)));
    int footerHeight = 0;
    if (!isUsingPanelLayout_Mobile()) {
        footerHeight = (height_Widget(d->token) +
                        height_Widget(findChild_Widget(w, "dialogbuttons")) +
                        12 * gap_UI);
    }
    const int avail = bottom_Rect(visibleRect_Root(w->root)) - footerHeight - inputPos.y;
    /* On desktop, retain the previously set minLines value. */
    int minLines = isUsingPanelLayout_Mobile() ? 1 : minLines_InputWidget(d->input);
    const int lineHeight = lineHeight_Text(font_InputWidget(d->input));
    int maxLines = iMaxi(minLines, ((avail - gap_UI) / lineHeight));
    /* On mobile, the height is fixed to the available space. */
    setLineLimits_InputWidget(d->input, isUsingPanelLayout_Mobile() ? maxLines : minLines, maxLines);
}

static const iGmIdentity *titanIdentityForUrl_(const iString *url) {
    const iGmIdentity *ident = findIdentity_GmCerts(
        certs_App(),
        collect_Block(hexDecode_Rangecc(range_String(valueString_SiteSpec(
            collectNewRange_String(urlRoot_String(url)), titanIdentity_SiteSpecKey)))));
    if (!ident) {
        /* Fall back to the global choice, perhaps switching to equivalent Gemini URL. */
        ident = identityForUrl_GmCerts(certs_App(), url);
    }
    return ident;
}

void appendIdentities_MenuItem(iArray *menuItems, const char *command,
                               iGmCertsIdentityFilterFunc filter) {
    iConstForEach(PtrArray, i, listIdentities_GmCerts(certs_App(), filter, NULL)) {
        const iGmIdentity *id = i.ptr;
        iString *str = collect_String(copy_String(name_GmIdentity(id)));
        prependCStr_String(str, isTerminal_Platform() ? uiTextStrong_ColorEscape : "\x1b[1m");
        if (!isEmpty_String(&id->notes)) {
            appendFormat_String(
                str, "\x1b[0m\n%s%s", escape_Color(uiTextDim_ColorId), cstr_String(&id->notes));
        }
        pushBack_Array(
            menuItems,
            &(iMenuItem){ cstr_String(str),
                          0,
                          0,
                          format_CStr("%s fp:%s",
                                      command,
                                      cstrCollect_String(hexEncode_Block(&id->fingerprint))) });
    }
}

static iBool onlyMisfinIdentities_(void *context, const iGmIdentity *ident) {
    iUnused(context);
    return isMisfin_GmIdentity(ident);
}

static const iArray *makeIdentityItems_UploadWidget_(const iUploadWidget *d) {
    iArray *items = collectNew_Array(sizeof(iMenuItem));
    if (d->protocol == titan_UploadProtocol) {
        const iGmIdentity *urlId = titanIdentityForUrl_(&d->url);
        pushBack_Array(items,
                       &(iMenuItem){ format_CStr("${dlg.upload.id.default} (%s)",
                                                 urlId ? cstr_String(name_GmIdentity(urlId))
                                                       : "${dlg.upload.id.none}"),
                                     0, 0, "upload.setid arg:1" });
        pushBack_Array(items, &(iMenuItem){ "${dlg.upload.id.none}", 0, 0, "upload.setid arg:0" });
        pushBack_Array(items, &(iMenuItem){ "---" });
    }
    appendIdentities_MenuItem(
        items, "upload.setid", d->protocol == misfin_UploadProtocol ? onlyMisfinIdentities_ : NULL);
    pushBack_Array(items, &(iMenuItem){ NULL });
    return items;
}

static void enableUploadPanelButton_UploadWidget_(iUploadWidget *d, iBool enable) {
    if (isUsingPanelLayout_Mobile()) {
        iWidget *actions = findChild_Widget(as_Widget(d), "navi.actions");
        showCollapsed_Widget(lastChild_Widget(actions), enable);
    }
    else {
        /* Not on used in the desktop layout. */
    }
}

iLabelWidget *makeIdentityDropdown_LabelWidget(iWidget *headings, iWidget *values,
                                               const iArray *identItems, const char *label,
                                               const char *id) {
    const iMenuItem *items    = constData_Array(identItems);
    const size_t     numItems = size_Array(identItems);
    iLabelWidget    *ident    = makeMenuButton_LabelWidget(label, items, numItems);
    setFixedSize_Widget(
        as_Widget(ident),
        init_I2(-1, lineHeight_Text(uiLabel_FontId) + (isTerminal_Platform() ? 0 : 2) * gap_UI));
    setTextCStr_LabelWidget(ident, items[findWidestLabel_MenuItem(items, numItems)].label);
    setTruncateToFit_LabelWidget(ident, iTrue);
    iWidget *identHeading = addChild_Widget(headings, iClob(makeHeading_Widget(label)));
    identHeading->sizeRef = as_Widget(ident);
    setId_Widget(addChildFlags_Widget(values, iClob(ident), alignLeft_WidgetFlag), id);
    return ident;
}

static void updateFieldWidths_UploadWidget(iUploadWidget *d) {
    if (d->protocol == titan_UploadProtocol) {
        const int width = width_Widget(d->tabs) - 3 * gap_UI -
                          (d->mime ? left_Rect(parent_Widget(d->mime)->rect) : 0);
        setFixedSize_Widget(as_Widget(d->path),
                            init_I2(width_Widget(d->tabs) - width_Widget(d->info), -1));
        setFixedSize_Widget(as_Widget(d->filePathInput), init_I2(width, -1));
        setFixedSize_Widget(as_Widget(d->mime), init_I2(width, -1));
        setFixedSize_Widget(as_Widget(d->ident), init_I2(width_Widget(d->token), -1));
        if (d->token) {
            setFixedSize_Widget(
                as_Widget(d->token),
                init_I2(width_Widget(d->tabs) - left_Rect(parent_Widget(d->token)->rect), -1));
            setFlags_Widget(as_Widget(d->token), expand_WidgetFlag, iTrue);
        }
    }
    else if (d->protocol == misfin_UploadProtocol) {
        const int width = width_Widget(d->tabs) - 3 * gap_UI -
                          width_Widget(d->info);
        setFixedSize_Widget(as_Widget(d->path), init_I2(width, -1));
        setFixedSize_Widget(as_Widget(d->ident), init_I2(width, -1));
        /* Misfin does not need multiple tabs. */
        iWidget *tabButtons = findChild_Widget(d->tabs, "tabs.buttons");
        setFlags_Widget(tabButtons, hidden_WidgetFlag, iTrue);
        setFixedSize_Widget(tabButtons, init_I2(-1, 0));

    }
    else {
        setFixedSize_Widget(as_Widget(d->info), init_I2(width_Widget(d->tabs), -1));
    }
}

static int font_UploadWidget_(const iUploadWidget *d, enum iFontStyle style) {
    iUnused(d);
    static const int fontSizes_[4] = {
        uiSmall_FontSize, uiNormal_FontSize, uiMedium_FontSize, uiBig_FontSize
    };
    return FONT_ID(monospace_FontId, style, fontSizes_[prefs_App()->editorZoomLevel]);
}

static iWidget *acceptButton_UploadWidget_(iUploadWidget *d) {
    return lastChild_Widget(findChild_Widget(
        as_Widget(d), isUsingPanelLayout_Mobile() ? "navi.actions" : "dialogbuttons"));
}

static iInputWidgetHighlight gemtextHighlighter_UploadWidget_(const iInputWidget *input,
                                                              iRangecc line, void *context) {
    const iBool isFocused = isFocused_Widget(input);
    iUploadWidget *d = context;
    if (startsWith_Rangecc(line, "#")) {
        return (iInputWidgetHighlight){ font_UploadWidget_(d, bold_FontStyle),
                                        uiTextAction_ColorId };
    }
    if (startsWith_Rangecc(line, ">")) {
        return (iInputWidgetHighlight){ font_UploadWidget_(d, italic_FontStyle),
                                        uiTextStrong_ColorId };
    }
    if (startsWith_Rangecc(line, "* ")) {
        return (iInputWidgetHighlight){ font_UploadWidget_(d, regular_FontStyle),
                                        uiTextCaution_ColorId };
    }
    if (startsWith_Rangecc(line, "=>")) {
        return (iInputWidgetHighlight){ font_UploadWidget_(d, regular_FontStyle),
                                        uiTextAction_ColorId };
    }
    return (iInputWidgetHighlight){ font_UploadWidget_(d, regular_FontStyle),
                                    isFocused ? uiInputTextFocused_ColorId : uiInputText_ColorId };
}

static void misfinAddressValidator_UploadWidget_(iInputWidget *input, void *context) {
    iUploadWidget *d = context;
    iString *address = collect_String(copy_String(text_InputWidget(input)));
    trim_String(address);
    setCStr_String(&d->url, "misfin://");
    append_String(&d->url, address);
    /* Update the indicator to show whether this address is trusted. */
    add_Periodic(periodic_App(), d, "upload.trusted.check");
}

static iBool createRequest_UploadWidget_(iUploadWidget *d, iBool isText);

static void handleMisfinRequestFinished_UploadWidget_(iUploadWidget *d) {
    const char *title = cstr_String(meta_GmRequest(d->request));
    const iString *address = collect_String(trimmed_String(text_InputWidget(d->path)));
    /*if (d->misfinStage == verifyRecipient_MisfinStage) {
        if (status_GmRequest(d->request) == 20) {
            const iString *fingerprint = meta_GmRequest(d->request);
            trust_Misfin(address, fingerprint);
            iReleasePtr(&d->request);
            d->misfinStage = carbonCopyToSelf_MisfinStage;
            if (createRequest_UploadWidget_(d, iTrue)) {
                submit_GmRequest(d->request);
                return;
            }
        }
        else {
            title = format_CStr("${misfin.verify}:\n%s", title);
        }
    }
    else */
    if (d->misfinStage == sendToRecipient_MisfinStage) {
        if (status_GmRequest(d->request) == 20) {
            /* Update the trusted fingzerprint after successful delivery of message.
               Since we don't receive any messages in the app, we can automatically
               update to new certificates. (Currently the fingerprints aren't really
               needed?) */
            trust_Misfin(address, meta_GmRequest(d->request)); /* TODO: Does this make sense? */
            /* Continue by sending the actual message. */
            if (prefs_App()->misfinSelfCopy) {
                iReleasePtr(&d->request);
                d->misfinStage = carbonCopyToSelf_MisfinStage;
                if (createRequest_UploadWidget_(d, iTrue)) {
                    submit_GmRequest(d->request);
                    return;
                }
            }
        }
    }
    const char *msg;
    const int status = status_GmRequest(d->request);
    switch (status) {
        case 20:
            title = envelope_Icon " ${heading.misfin.ok}";
            msg = "${misfin.success}";
            break;

        case 30:
        case 31:
            msg = "${misfin.redirect}";
            break;

        case 40:
        case 41:
        case 42:
        case 43:
        case 44:
        case 45:
        case 50:
        case 51:
        case 52:
        case 53:
        case 59:
            msg = "${misfin.failure}";
            break;

        case 60:
            msg = "${misfin.needcert}";
            break;

        case 61:
            msg = "${misfin.unauth}";
            break;

        case 62:
            msg = "${misfin.badcert}";
            break;

        case 63:
            msg = "${misfin.changed}";
            break;

        default:
            msg = "${misfin.unknown}";
            break;
    }
    makeMessage_Widget(
        title,
        msg,
        (iMenuItem[]){ { "${dlg.message.ok}", 0, 0, status == 20 ? "!upload.cancel" : "cancel" } },
        1);
    iReleasePtr(&d->request);
    setFlags_Widget(acceptButton_UploadWidget_(d), disabled_WidgetFlag, iFalse);
    setFlags_Widget(d->tabs, disabled_WidgetFlag, iFalse);
    d->misfinStage = none_MisfinStage;
}

static void updateButtonExcerpts_UploadWidget_(iUploadWidget *d) {
    if (isUsingPanelLayout_Mobile()) {
        /* Update the excerpt in the panel button. */
        iLabelWidget *panelButton = findChild_Widget(as_Widget(d), "dlg.upload.text.button");
        setWrap_LabelWidget(panelButton, iTrue);
        setFlags_Widget(as_Widget(panelButton), fixedHeight_WidgetFlag, iTrue);
        iString *excerpt = collect_String(copy_String(text_InputWidget(d->input)));
        const size_t maxLen = 150;
        if (length_String(excerpt) > maxLen) {
            truncate_String(excerpt, maxLen);
            appendChar_String(excerpt, 0x2026 /* ellipsis */);
        }
        replace_String(excerpt, "\n", uiTextAction_ColorEscape return_Icon restore_ColorEscape " ");
        trim_String(excerpt);
        if (isEmpty_String(excerpt)) {
            setCStr_String(excerpt, "${dlg.upload.text}");
        }
        setText_LabelWidget(panelButton, excerpt);
        /* Also update the file button. */
        panelButton = findChild_Widget(as_Widget(d), "dlg.upload.file.button");
        if (!isEmpty_String(&d->filePath)) {
            const iString *mime = text_InputWidget(d->mime);
            updateTextCStr_LabelWidget(panelButton,
                                       format_CStr("%s%s%s%s",
                                                   formatCStrs_Lang("num.bytes.n", d->fileSize),
                                                   !isEmpty_String(mime) ? " (" : "",
                                                   cstr_String(mime),
                                                   !isEmpty_String(mime) ? ")" : ""));
        }
        else {
            updateTextCStr_LabelWidget(panelButton, "${dlg.upload.file}");
        }
    }
}

void init_UploadWidget(iUploadWidget *d, enum iUploadProtocol protocol) {
    iWidget *w = as_Widget(d);
    init_Widget(w);
    setId_Widget(w, "upload");
    init_String(&d->originalUrl);
    init_String(&d->url);
    d->protocol = protocol;
    d->viewer = NULL;
    d->path = NULL;
    d->token = NULL;
    d->ident = NULL;
    d->mime = NULL;
    d->request = NULL;
    d->editRequest = NULL;
    d->editRedirectCount = 0;
    d->misfinStage = none_MisfinStage;
    init_String(&d->filePath);
    d->fileSize = 0;
    d->filePathLabel = NULL;
    d->filePathInput = NULL;
    d->editLabel = NULL;
    d->allowRetryEdit = iFalse;
    d->idMode = defaultForSite_UploadIdentity;
    init_Block(&d->idFingerprint, 0);
    /* Dialog actions. */
    const iMenuItem titanActions[] = {
        { "${upload.port}", 0, 0, "upload.setport" },
        { "---" },
        { "${close}", SDLK_ESCAPE, 0, "upload.cancel" },
        { uiTextAction_ColorEscape "${dlg.upload.send}", SDLK_RETURN, KMOD_ACCEPT, "upload.accept" }
    };
    const iMenuItem misfinActions[] = {
        { "${misfin.self.copy}" },
        { "!misfin.self.copy" }, /* toggle */
        { "---" },
        { "${close}", SDLK_ESCAPE, 0, "upload.cancel" },
        { uiTextAction_ColorEscape "${dlg.upload.sendmsg}", SDLK_RETURN, KMOD_ACCEPT, "upload.accept" }
    };
    const iMenuItem otherActions[] = {
        { "${close}", SDLK_ESCAPE, 0, "upload.cancel" },
        { uiTextAction_ColorEscape "${dlg.upload.send}", SDLK_RETURN, KMOD_ACCEPT, "upload.accept" }
    };
    const iMenuItem *actionItems = (d->protocol == titan_UploadProtocol    ? titanActions
                                    : d->protocol == misfin_UploadProtocol ? misfinActions
                                                                           : otherActions);
    const size_t numActionItems =
        (d->protocol == titan_UploadProtocol    ? iElemCount(titanActions)
         : d->protocol == misfin_UploadProtocol ? iElemCount(misfinActions)
                                                : iElemCount(otherActions));
    if (isUsingPanelLayout_Mobile()) {
        const int infoFont = (deviceType_App() == phone_AppDeviceType ? uiLabelBig_FontId
                                                                      : uiLabelMedium_FontId);
        const iMenuItem ellipsisItems[] = {
            { clipboard_Icon " ${menu.paste.snippet}", 0, 0, "submenu id:snippetmenu" },
            { select_Icon " ${menu.selectall}", 0, 0, "upload.text.selectall" },
            { export_Icon " ${menu.upload.export}", 0, 0, "upload.text.export" },
            { "---${menu.upload.delete}" },
            { delete_Icon " " uiTextAction_ColorEscape "${menu.upload.delete.confirm}",
                0, 0, "upload.text.delete confirmed:1" },
            { NULL }
        };
        const iMenuItem textItems[] = {
            { "navi.menubutton text:\u00a0\u00a0\u00a0" midEllipsis_Icon "\u00a0\u00a0\u00a0\u00a0", 0, 0, (const void *) ellipsisItems },
            { "title id:heading.upload.text" },
            { "input id:upload.text noheading:1" },
            { NULL }
        };
        const iMenuItem titanFileItems[] = {
            { "title id:heading.upload.file" },
            { "heading id:upload.file.name" },
            { format_CStr("label id:upload.filepathlabel font:%d text:\u2014", infoFont) },
            { "heading id:upload.file.size" },
            { format_CStr("label id:upload.filesizelabel font:%d text:\u2014", infoFont) },
            { "padding" },
            { "input id:upload.mime" },
            { "label id:upload.counter text:" },
            { "button text:" uiTextAction_ColorEscape "${dlg.upload.pickfile}", 0, 0, "upload.pickfile" },
            { NULL }
        };
        const iMenuItem urlItems[] = {
            { "title id:upload.url" },
            { format_CStr("label id:upload.info font:%d", infoFont) },
            { "input id:upload.path hint:hint.upload.path noheading:1 url:1 text:" },
            { NULL }
        };
        const iMenuItem uploadTypeItems[] = {
            { "button id:upload.type.text text:${heading.upload.text}", 0, 0, "upload.settype arg:0" },
            { "button id:upload.type.file text:${heading.upload.file}", 0, 0, "upload.settype arg:1" },
            { NULL }
        };
        const iMenuItem titanItems[] = {
            { "title id:upload.title text:${heading.upload}" },
            { "panel id:dlg.upload.url buttonid:dlg.upload.urllabel icon:0x1f310 text:", 0, 0, (const void *) urlItems },
            { "label id:upload.progress collapse:1 text:" },
            { "radio horizontal:1 id:upload.type collapse:1", 0, 0, (const void *) uploadTypeItems },
            { "panel id:dlg.upload.text collapse:1 icon:0x1f5b9 noscroll:1", 0, 0, (const void *) textItems },
            { "panel id:dlg.upload.file collapse:1 icon:0x1f4c1", 0, 0, (const void *) titanFileItems },
            { "heading text:${heading.upload.id}" },
            { "dropdown id:upload.id noheading:1 text:", 0, 0, constData_Array(makeIdentityItems_UploadWidget_(d)) },
            { "input id:upload.token hint:hint.upload.token.long noheading:1" },
            { NULL }
        };
        const iMenuItem misfinItems[] = {
            { "title id:heading.upload.misfin" },
            { "input id:upload.path text:${upload.to}" },
            { "dropdown id:upload.id text:${upload.from}", 0, 0, constData_Array(makeIdentityItems_UploadWidget_(d)) },
            { "padding" },
            { "panel id:dlg.upload.text icon:0x1f5b9 noscroll:1", 0, 0, (const void *) textItems },
            { NULL }
        };
        const iMenuItem spartanFileItems[] = {
            { "title id:heading.upload.file" },
            { "heading id:upload.file.name" },
            { format_CStr("label id:upload.filepathlabel font:%d text:\u2014", infoFont) },
            { "heading id:upload.file.size" },
            { format_CStr("label id:upload.filesizelabel font:%d text:\u2014", infoFont) },
            { "label id:upload.counter text:" },
            { "button text:" uiTextAction_ColorEscape "${dlg.upload.pickfile}", 0, 0, "upload.pickfile" },
            { NULL }
        };
        const iMenuItem spartanItems[] = {
            { "title id:heading.upload.spartan" },
            { format_CStr("label id:upload.info font:%d", infoFont) },
            { "radio horizontal:1 id:upload.type collapse:1", 0, 0, (const void *) uploadTypeItems },
            { "panel id:dlg.upload.text collapse:1 icon:0x1f5b9 noscroll:1", 0, 0, (const void *) textItems },
            { "panel id:dlg.upload.file collapse:1 icon:0x1f4c1", 0, 0, (const void *) spartanFileItems },
            { NULL }
        };
        initPanels_Mobile(w,
                          NULL,
                          d->protocol == titan_UploadProtocol  ? titanItems :
                          d->protocol == misfin_UploadProtocol ? misfinItems :
                                                                 spartanItems,
                          actionItems,
                          numActionItems);
        // printTree_Widget(w);
        d->info          = findChild_Widget(w, "upload.info");
        d->path          = findChild_Widget(w, "upload.path");
        d->input         = findChild_Widget(w, "upload.text");
        d->filePathLabel = findChild_Widget(w, "upload.filepathlabel");
        d->fileSizeLabel = findChild_Widget(w, "upload.filesizelabel");
        d->mime          = findChild_Widget(w, "upload.mime");
        d->token         = findChild_Widget(w, "upload.token");
        d->counter       = findChild_Widget(w, "upload.counter");
        d->editLabel     = findChild_Widget(w, "upload.progress");
        showCollapsed_Widget(findChild_Widget(w, "upload.type"), iTrue);
        setPadding_Widget(as_Widget(d->editLabel), 0, 3 * gap_UI, 0, 0);
        /* Style the Identity dropdown. */ {
            setFlags_Widget(findChild_Widget(w, "upload.id"), alignRight_WidgetFlag, iFalse);
            setFlags_Widget(findChild_Widget(w, "upload.id"), alignLeft_WidgetFlag, iTrue);
        }
        setFlags_Widget(findChild_Widget(w, "upload.type.text"), selected_WidgetFlag, iTrue);
        showCollapsed_Widget(findChild_Widget(w, "dlg.upload.file.button"), iFalse);
        // if (isPortraitPhone_App()) {
        // }
        enableUploadPanelButton_UploadWidget_(d, iTrue);
    }
    else {
        const float aspectRatio = isTerminal_Platform() ? 0.6f : 1.0f;
        useSheetStyle_Widget(w);
        setFlags_Widget(w, overflowScrollable_WidgetFlag, iFalse);
        addDialogTitle_Widget(w,
                              d->protocol == titan_UploadProtocol    ? "${heading.upload}"
                              : d->protocol == misfin_UploadProtocol ? "${heading.upload.misfin}"
                                                                     : "${heading.upload.spartan}",
                              "upload.title");
        iWidget *headings, *values;
        /* URL path. */ {
            if (d->protocol == titan_UploadProtocol || d->protocol == misfin_UploadProtocol) {
                iWidget *page = makeTwoColumns_Widget(&headings, &values);
                d->path = new_InputWidget(0);
                addTwoColumnDialogInputField_Widget(
                    headings,
                    values,
                    d->protocol == misfin_UploadProtocol ? "${upload.to}" : "",
                    "upload.path",
                    iClob(d->path));
                d->info = (iLabelWidget *) lastChild_Widget(headings);
                if (d->protocol == misfin_UploadProtocol) {
                    setValidator_InputWidget(d->path, misfinAddressValidator_UploadWidget_, d);
                    /* Sender identity. */
                    const iArray *idItems = makeIdentityItems_UploadWidget_(d);
                    iAssert(!isEmpty_Array(idItems));
                    d->ident = makeIdentityDropdown_LabelWidget(
                        headings, values, idItems, "${upload.from}", "upload.id");
                    iLabelWidget *label = (iLabelWidget *) lastChild_Widget(headings);
                    setFont_LabelWidget(label, uiContent_FontId);
                    setTextColor_LabelWidget(label, uiInputTextFocused_ColorId);
                    /* Add a trust indicator into the path field. */ {
                        iLabelWidget *trustedRecipient = new_LabelWidget(check_Icon, 0);
                        setId_Widget(as_Widget(trustedRecipient), "upload.trusted");
                        setTextColor_LabelWidget(trustedRecipient, green_ColorId);
                        addChildFlags_Widget(as_Widget(d->path),
                                             iClob(trustedRecipient),
                                             hidden_WidgetFlag | frameless_WidgetFlag |
                                                 moveToParentRightEdge_WidgetFlag |
                                                 resizeToParentHeight_WidgetFlag);
                        setContentPadding_InputWidget(d->path, -1, width_Widget(trustedRecipient));
                    }
                    /* Initialize the currently chosen identity. */
                    const iRangecc fp = range_Command(
                        ((const iMenuItem *) constAt_Array(idItems, 0))->command, "fp");
                    set_Block(&d->idFingerprint, collect_Block(hexDecode_Rangecc(fp)));
                }
                addChild_Widget(w, iClob(page));
            }
            else {
                /* Just a plain label for the URL. */
                d->info = addChild_Widget(w, iClob(new_LabelWidget("", NULL)));
                setWrap_LabelWidget(d->info, iTrue);
            }
            setFont_LabelWidget(d->info, uiContent_FontId);
            setTextColor_LabelWidget(d->info, uiInputTextFocused_ColorId);
            addChild_Widget(w, iClob(makePadding_Widget(gap_UI)));
        }
        /* Tabs for input data. */
        d->tabs = makeTabs_Widget(w);
        /* Make the tabs support vertical expansion based on content. */ {
            setFlags_Widget(d->tabs, resizeHeightOfChildren_WidgetFlag, iFalse);
            setFlags_Widget(d->tabs, arrangeHeight_WidgetFlag, iTrue);
            iWidget *tabPages = findChild_Widget(d->tabs, "tabs.pages");
            setFlags_Widget(tabPages, resizeHeightOfChildren_WidgetFlag, iFalse);
            setFlags_Widget(tabPages, arrangeHeight_WidgetFlag, iTrue);
        }
        setBackgroundColor_Widget(findChild_Widget(d->tabs, "tabs.buttons"),
                                  uiBackgroundSidebar_ColorId);
        setId_Widget(d->tabs, "upload.tabs");
        /* Text input. */ {
            iWidget *page = new_Widget();
            setFlags_Widget(page, arrangeSize_WidgetFlag, iTrue);
            d->input = new_InputWidget(0);
            setId_Widget(as_Widget(d->input), "upload.text");
            /* It would be annoying for focus to exit the widget accidentally when typing text.
               One needs to use TAB to move focus. */
            setArrowFocusNavigable_InputWidget(d->input, iFalse);
            setFixedSize_Widget(as_Widget(d->input), init_I2(120 * gap_UI * aspectRatio, -1));
            if (prefs_App()->editorSyntaxHighlighting) {
                setHighlighter_InputWidget(d->input, gemtextHighlighter_UploadWidget_, d);
            }
            addChild_Widget(page, iClob(d->input));
            appendFramelessTabPage_Widget(
                d->tabs, iClob(page), "${heading.upload.text}", none_ColorId, '1', 0);
        }
        /* File content. */
        if (d->protocol != misfin_UploadProtocol) {
            iWidget *page = appendTwoColumnTabPage_Widget(
                d->tabs, "${heading.upload.file}", none_ColorId, '2', &headings, &values);
            setBackgroundColor_Widget(page, uiBackgroundSidebar_ColorId);
            iWidget *heading;
            addChildFlags_Widget(headings,
                                 heading = iClob(new_LabelWidget("${upload.file.path}", NULL)),
                                 frameless_WidgetFlag | alignLeft_WidgetFlag);
            d->filePathInput = addChildFlags_Widget(values, iClob(new_InputWidget(0)), 0);
            heading->sizeRef = as_Widget(d->filePathInput);
            if (!isTerminal_Platform()) {
                setHint_InputWidget(d->filePathInput, "${upload.file.drophere}");
            }
            setValidator_InputWidget(d->filePathInput, filePathValidator_UploadWidget_, d);
            addChildFlags_Widget(headings,
                                 iClob(new_LabelWidget("${upload.file.size}", NULL)),
                                 frameless_WidgetFlag);
            d->fileSizeLabel = addChildFlags_Widget(
                values, iClob(new_LabelWidget("\u2014", NULL)), frameless_WidgetFlag);
            if (d->protocol == titan_UploadProtocol) {
                d->mime = new_InputWidget(0);
                setFixedSize_Widget(as_Widget(d->mime), init_I2(70 * gap_UI * aspectRatio, -1));
                addTwoColumnDialogInputField_Widget(
                    headings, values, "${upload.mime}", "upload.mime", iClob(d->mime));
            }
        }
        /* Progress reporting for the Titan edit sequence. */
        if (d->protocol != misfin_UploadProtocol) {
            d->editLabel = new_LabelWidget("", "");
            setBackgroundColor_Widget((iWidget *) d->editLabel, uiBackgroundSidebar_ColorId);
            setFlags_Widget(as_Widget(d->editLabel), resizeToParentWidth_WidgetFlag, iTrue);
            /* Ensure the height of the progress pane matches the text editor, as the latter
               determines the height of the whole dialog. */
            as_Widget(d->editLabel)->sizeRef = (iWidget *) d->input;
            appendFramelessTabPage_Widget(d->tabs, iClob(d->editLabel), "", none_ColorId, 0, 0);

            iLabelWidget *tabButton = tabPageButton_Widget(d->tabs, d->editLabel);
            setFlags_Widget(as_Widget(tabButton),
                            collapse_WidgetFlag | hidden_WidgetFlag | disabled_WidgetFlag,
                            iTrue);
            setFlags_Widget(as_Widget(tabPageButton_Widget(d->tabs, tabPage_Widget(d->tabs, 0))),
                            collapse_WidgetFlag,
                            iTrue);
            setFlags_Widget(as_Widget(tabPageButton_Widget(d->tabs, tabPage_Widget(d->tabs, 1))),
                            collapse_WidgetFlag,
                            iTrue);
        }
        /* Identity and Token. */
        if (d->protocol == titan_UploadProtocol) {
            addChild_Widget(w, iClob(makePadding_Widget(gap_UI)));
            iWidget *page = makeTwoColumns_Widget(&headings, &values);
            /* Identity. */
            d->ident = makeIdentityDropdown_LabelWidget(
                headings, values, makeIdentityItems_UploadWidget_(d), "${upload.id}", "upload.id");
            /* Token. */
            d->token = addTwoColumnDialogInputField_Widget(
                headings, values, "${upload.token}", "upload.token", iClob(new_InputWidget(0)));
            setHint_InputWidget(d->token, "${hint.upload.token}");
            setFixedSize_Widget(as_Widget(d->token), init_I2(50 * gap_UI * aspectRatio, -1));
            addChild_Widget(w, iClob(page));
        }
        /* Buttons. */ {
            addChild_Widget(w, iClob(makePadding_Widget(gap_UI)));
            iWidget *buttons = makeDialogButtons_Widget(actionItems, numActionItems);
            setId_Widget(insertChildAfterFlags_Widget(buttons,
                                                      iClob(d->counter = new_LabelWidget("", NULL)),
                                                      d->protocol == misfin_UploadProtocol ? 2 : 0,
                                                      frameless_WidgetFlag),
                         "upload.counter");
            addChild_Widget(w, iClob(buttons));
        }
        resizeToLargestPage_Widget(d->tabs);
        arrange_Widget(w);
        updateFieldWidths_UploadWidget(d);
        setFocus_Widget(as_Widget(d->input));
    }
    setFont_InputWidget(d->input, font_UploadWidget_(d, regular_FontStyle));
    setUseReturnKeyBehavior_InputWidget(d->input, iFalse); /* traditional text editor */
    setLineLimits_InputWidget(d->input, 7, 20);
    setHint_InputWidget(d->input, "${hint.upload.text}");
    switch (d->protocol) {
        case titan_UploadProtocol: {
            setBackupFileName_InputWidget(d->input, "uploadbackup");
            setBackupFileName_InputWidget(d->token, "uploadtoken"); /* TODO: site-specific config? */
            break;
        }
        case misfin_UploadProtocol: {
            setBackupFileName_InputWidget(d->input, "misfinbackup");
            setHint_InputWidget(d->input, "${hint.upload.misfin}");
            setFlags_Widget(findChild_Widget(w, "misfin.send.copy"), fixedWidth_WidgetFlag, iTrue);
            if (d->tabs) {
                iLabelWidget *fileTabButton = tabPageButton_Widget(d->tabs, tabPage_Widget(d->tabs, 0));
                setFlags_Widget((iWidget *) fileTabButton, disabled_WidgetFlag, iTrue);
            }
            setToggle_Widget(findChild_Widget(w, "misfin.self.copy"), prefs_App()->misfinSelfCopy);
            break;
        }
        case spartan_UploadProtocol: {
            setBackupFileName_InputWidget(d->input, "spartanbackup");
            break;
        }
        default:
            break;
    }
    updateInputMaxHeight_UploadWidget_(d);
    updateButtonExcerpts_UploadWidget_(d);
    enableResizing_Widget(as_Widget(d), width_Widget(d), NULL);
}

void deinit_UploadWidget(iUploadWidget *d) {
    remove_Periodic(periodic_App(), d);
    if (d->editRequest) {
        cancel_GmRequest(d->editRequest);
        iReleasePtr(&d->editRequest);
    }
    releaseFile_UploadWidget_(d);
    deinit_Block(&d->idFingerprint);
    deinit_String(&d->filePath);
    deinit_String(&d->url);
    deinit_String(&d->originalUrl);
    iRelease(d->request);
}

static void remakeIdentityItems_UploadWidget_(iUploadWidget *d) {
    if (d->protocol == titan_UploadProtocol || d->protocol == misfin_UploadProtocol) {
        iWidget *dropMenu = findChild_Widget(findChild_Widget(as_Widget(d), "upload.id"), "menu");
        const iArray *items = makeIdentityItems_UploadWidget_(d);
        /* TODO: Make the following a utility method. */
        if (flags_Widget(dropMenu) & nativeMenu_WidgetFlag) {
            setNativeMenuItems_Widget(dropMenu, constData_Array(items), size_Array(items));
        }
        else {
            releaseChildren_Widget(dropMenu);
            makeMenuItems_Widget(dropMenu, constData_Array(items), size_Array(items));
        }
    }
}

static void updateIdentityDropdown_UploadWidget_(iUploadWidget *d) {
    if (d->protocol == titan_UploadProtocol || d->protocol == misfin_UploadProtocol) {
        updateDropdownSelection_LabelWidget(
            findChild_Widget(as_Widget(d), "upload.id"),
            d->idMode == none_UploadIdentity ? " arg:0"
            : d->idMode == defaultForSite_UploadIdentity
                ? " arg:1"
                : format_CStr(" fp:%s", cstrCollect_String(hexEncode_Block(&d->idFingerprint))));
    }
}

static uint16_t titanPortForUrl_(const iString *url) {
    uint16_t port = 0;
    const iString *root = collectNewRange_String(urlRoot_String(url));
    iUrl parts;
    init_Url(&parts, url);
    /* If the port is not specified, use the site-specific configuration. */
    if (isEmpty_Range(&parts.port) || equalCase_Rangecc(parts.scheme, "gemini")) {
        port = value_SiteSpec(root, titanPort_SiteSpecKey);
    }
    else {
        port = atoi(cstr_Rangecc(parts.port));
    }
    return port ? port : GEMINI_DEFAULT_PORT;
}

static const iString *requestUrl_UploadWidget_(const iUploadWidget *d) {
    if (d->protocol == spartan_UploadProtocol) {
        iAssert(!isEmpty_String(&d->url));
        return &d->url;
    }
    if (d->protocol == misfin_UploadProtocol) {
        iString *reqUrl = collectNewCStr_String("misfin://");
        if (d->misfinStage == carbonCopyToSelf_MisfinStage) {
            iGmIdentity *ident = findIdentity_GmCerts(certs_App(), &d->idFingerprint);
            if (ident) {
                append_String(reqUrl, collect_String(misfinIdentity_GmIdentity(ident, NULL)));
            }
        }
        else {
            append_String(reqUrl, text_InputWidget(d->path)); /* recipient address */
        }
        return reqUrl;
    }
    /* Compose Titan URL with the configured path. */
    iAssert(d->protocol == titan_UploadProtocol);
    iAssert(!isEmpty_String(&d->url));
    const iRangecc siteRoot = urlRoot_String(&d->url);
    iString *reqUrl = collectNew_String();
    setRange_String(reqUrl, (iRangecc){ constBegin_String(&d->url), siteRoot.end });
    const iString *path = text_InputWidget(d->path);
    if (!isEmpty_String(path)) {
        if (!startsWith_String(path, "/")) {
            appendCStr_String(reqUrl, "/");
        }
        append_String(reqUrl, path);
    }
    iUrl parts;
    init_Url(&parts, &d->originalUrl);
    if (!isEmpty_Range(&parts.query)) {
        appendRange_String(reqUrl, parts.query);
    }
    return reqUrl;
}

static void updateUrlPanelButton_UploadWidget_(iUploadWidget *d) {
    if (isUsingPanelLayout_Mobile() && d->protocol == titan_UploadProtocol) {
        iLabelWidget *urlPanelButton = findChild_Widget(as_Widget(d), "dlg.upload.urllabel");
        setFlags_Widget(as_Widget(urlPanelButton), fixedHeight_WidgetFlag, iTrue);
        setWrap_LabelWidget(urlPanelButton, iTrue);
        setText_LabelWidget(urlPanelButton, requestUrl_UploadWidget_(d));
        arrange_Widget(as_Widget(d));
    }
}

static void showOrHideProgressTab_UploadWidget_(iUploadWidget *d, iBool show) {
    iWidget *w = as_Widget(d);
    if (isUsingPanelLayout_Mobile()) {
        showCollapsed_Widget(as_Widget(d->editLabel), show);
        showCollapsed_Widget(findChild_Widget(w, "dlg.upload.text.button"), !show);
        //enableUploadPanelButton_UploadWidget_(d, !show);
        return;
    }
    iWidget *buttons[3];
    for (size_t i = 0; i < 3; i++) {
        buttons[i] = as_Widget(tabPageButton_Widget(d->tabs, tabPage_Widget(d->tabs, i)));
        showCollapsed_Widget(buttons[i], show ^ (i != 2));
    }
    if (show) {
        showTabPage_Widget(d->tabs, d->editLabel);
        updateText_LabelWidget((iLabelWidget *) buttons[2], &d->originalUrl);
        setFlags_Widget(buttons[2], selected_WidgetFlag, iFalse);
        setWrap_LabelWidget(d->editLabel, iFalse);
        updateTextCStr_LabelWidget(d->editLabel, "");
    }
    else {
        showTabPage_Widget(d->tabs, tabPage_Widget(d->tabs, 0));
    }
}

static void editContentProgress_UploadWidget_(void *obj, iGmRequest *req) {
    static uint32_t    lastTime_ = 0;
    const uint32_t     now       = SDL_GetTicks();
    const iGmResponse *resp      = lockResponse_GmRequest(req);
    if (now - lastTime_ > 100) {
        postCommand_Widget(obj, "upload.fetch.progressed arg:%u", size_Block(&resp->body));
        lastTime_ = now;
    }
    unlockResponse_GmRequest(req);
}

static void editContentFetched_UploadWidget_(void *obj, iGmRequest *req) {
    postCommand_Widget(obj, "upload.fetch.progressed arg:%u", bodySize_GmRequest(req));
    sleep_Thread(0.100); /* short delay to see the final update */
    postCommand_Widget(obj, "upload.fetched reqid:%u", id_GmRequest(req));
}

static void setupRequest_UploadWidget_(const iUploadWidget *d, const iString *url, iGmRequest *req) {
    if (url) {
        setUrl_GmRequest(req, url);
    }
    else {
        url = url_GmRequest(req);
    }
    const iString *site = collectNewRange_String(urlRoot_String(url));
    switch (d->idMode) {
        case none_UploadIdentity:
            /* Ensure no identity will be used for this specific URL. */
            signOut_GmCerts(certs_App(), url);
            setValueString_SiteSpec(site, titanIdentity_SiteSpecKey, collectNew_String());
            break;
        case dropdown_UploadIdentity: {
            /* Update the site-specific preference to the chosen identity. */
            iGmIdentity *ident = findIdentity_GmCerts(certs_App(), &d->idFingerprint);
            if (ident) {
                setValueString_SiteSpec(site,
                                        titanIdentity_SiteSpecKey,
                                        collect_String(hexEncode_Block(&ident->fingerprint)));
            }
            break;
        }
        default:
            break;
    }
    if (d->idMode != none_UploadIdentity) {
        const iGmIdentity *ident = titanIdentityForUrl_(url); /* site-specific preference */
        setIdentity_GmRequest(req, ident);
    }
}

static void fetchEditableResource_UploadWidget_(iUploadWidget *d, const iString *url) {
    showOrHideProgressTab_UploadWidget_(d, iTrue);
    enableUploadPanelButton_UploadWidget_(d, iFalse);
    iAssert(d->editRequest == NULL);
    d->editRequest = new_GmRequest(certs_App());
    iAssert(endsWith_Rangecc(urlPath_String(&d->originalUrl), ";edit")); /* was checked earlier */
    iConnect(GmRequest, d->editRequest, updated,  d, editContentProgress_UploadWidget_);
    iConnect(GmRequest, d->editRequest, finished, d, editContentFetched_UploadWidget_);
    iString *editUrl = copy_String(url);
    if (isTitanUrl_String(url) && !endsWithCase_Rangecc(urlPath_String(editUrl), ";edit")) {
        set_String(editUrl, collect_String(withUrlParameters_String(editUrl, "edit", NULL, NULL)));
    }
    setupRequest_UploadWidget_(d, editUrl, d->editRequest);
    delete_String(editUrl);
    if (d->tabs) {
        updateText_LabelWidget(tabPageButton_Widget(d->tabs, tabPage_Widget(d->tabs, 2)),
                               url_GmRequest(d->editRequest));
    }
    else {
        updateTextCStr_LabelWidget(d->editLabel, "${doc.fetching}");
    }
    submit_GmRequest(d->editRequest);
}

static iBool handleEditContentResponse_UploadWidget_(iUploadWidget *d, uint32_t reqId) {
    if (id_GmRequest(d->editRequest) != reqId) {
        return iFalse;
    }
    iGmRequest *req = d->editRequest;
    const enum iGmStatusCode status = status_GmRequest(req);
    const char *errorFormat = uiTextCaution_ColorEscape "%lc  \x1b[1m%s\x1b[0m \u2014 %s";
    d->allowRetryEdit = iFalse;
    if (category_GmStatusCode(status) == categoryRedirect_GmStatusCode) {
        const iString *newUrl = collect_String(copy_String(meta_GmRequest(d->editRequest)));
        if (++d->editRedirectCount == 5) {
            const iGmError *error = get_GmError(tooManyRedirects_GmStatusCode);
            setWrap_LabelWidget(d->editLabel, iTrue);
            updateText_LabelWidget(
                d->editLabel,
                collectNewFormat_String(errorFormat,
                                        error->icon,
                                        error->title,
                                        format_CStr("%s\n\n%s", error->info, cstr_String(newUrl))));
            arrange_Widget(as_Widget(d));
            iReleasePtr(&d->editRequest);
            d->editRedirectCount = 0;
            return iTrue;
        }
        /* Resubmit with the new URL. */
        iReleasePtr(&d->editRequest);
        fetchEditableResource_UploadWidget_(d, newUrl);
        return iTrue;
    }
    enableUploadPanelButton_UploadWidget_(d, iTrue);
    if (!isSuccess_GmStatusCode(status_GmRequest(req))) {
        iChar           icon  = 0x26a0;
        const char     *title = "${heading.upload.edit.error}";
        const char     *msg   = "${dlg.upload.edit.error}";
        const iGmError *error = get_GmError(status);
        if (isDefined_GmError(status)) {
            icon  = error->icon;
            title = error->title;
            msg   = error->info;
            if (category_GmStatusCode(status_GmRequest(req)) >= categoryTemporaryFailure_GmStatusCode) {
                title = cstr_String(meta_GmRequest(req));
            }
        }
        setWrap_LabelWidget(d->editLabel, iTrue);
        if (isUsingPanelLayout_Mobile()) {
            setText_LabelWidget(d->editLabel,
                                collectNewFormat_String(errorFormat, icon, title, msg));
            arrange_Widget(as_Widget(d));
            refresh_Widget(d->editLabel);
            refresh_Widget(d);
        }
        else {
            updateText_LabelWidget(d->editLabel,
                                   collectNewFormat_String(errorFormat, icon, title, msg));
        }
        iReleasePtr(&d->editRequest);
        d->allowRetryEdit = iTrue; /* with different credentials, for example */
        return iTrue;
    }
    /* We have successfully fetched the resource for editing. */
    iGmResponse *resp = lockResponse_GmRequest(req);
    setText_InputWidget(d->mime, &resp->meta);
    if (startsWithCase_String(&resp->meta, "text/")) {
        setText_UploadWidget(d, collect_String(newBlock_String(&resp->body)));
        showOrHideProgressTab_UploadWidget_(d, iFalse);
        if (isUsingPanelLayout_Mobile()) {
            /* Automatically switch to the text editor. */
            postCommand_Widget(findChild_Widget(as_Widget(d), "dlg.upload.text.button"),
                               "panel.open");
        }
    }
    else {
        /* Report that non-text content cannot be edited in the app. */
        setWrap_LabelWidget(d->editLabel, iTrue);
        updateText_LabelWidget(d->editLabel,
                               collectNewFormat_String(errorFormat,
                                                       0x26a0,
                                                       "${heading.upload.edit.error}",
                                                       "${dlg.upload.edit.incompatible}"));
        iReleasePtr(&d->editRequest);
        return iTrue;
    }
    unlockResponse_GmRequest(req);
    iReleasePtr(&d->editRequest);
    setFlags_Widget(as_Widget(d->path), disabled_WidgetFlag, iTrue); /* don't change path while editing */
    return iTrue;
}

static void setUrlPort_UploadWidget_(iUploadWidget *d, const iString *url, uint16_t overridePort) {
    iWidget *w = as_Widget(d);
    /* Any ongoing edit request must be first cancelled. */
    if (d->editRequest) {
        cancel_GmRequest(d->editRequest);
        iReleasePtr(&d->editRequest);
    }
    showOrHideProgressTab_UploadWidget_(d, iFalse);
    set_String(&d->originalUrl, url);
    iUrl parts;
    init_Url(&parts, url);
    if (d->protocol == spartan_UploadProtocol) {
        set_String(&d->url, &d->originalUrl);
        setText_LabelWidget(d->info, &d->url);
    }
    else if (d->protocol == titan_UploadProtocol) {
        setCStr_String(&d->url, "titan");
        appendRange_String(&d->url, (iRangecc){ parts.scheme.end, parts.host.end });
        appendFormat_String(&d->url, ":%u", overridePort ? overridePort : titanPortForUrl_(url));
        const char *paramStart = strchr(parts.path.start, ';');
        const iBool isEdit = paramStart && !iCmpStr(paramStart, ";edit");
        appendRange_String(&d->url,
                           (iRangecc){ parts.path.start,
                                       /* strip any pre-existing params */
                                       paramStart ? paramStart
                                       : size_Range(&parts.query)
                                           ? parts.query.start /* query is excluded here */
                                           : constEnd_String(url) });
        const iRangecc siteRoot = urlRoot_String(&d->url);
        iUrl parts;
        init_Url(&parts, &d->url);
        setTextCStr_LabelWidget(d->info, cstr_Rangecc((iRangecc){ parts.host.start, siteRoot.end }));
        /* From root onwards, the URL is editable. */
        setTextCStr_InputWidget(d->path,
                                cstr_Rangecc((iRangecc){ siteRoot.end, constEnd_String(&d->url) }));
        if (!cmp_String(text_InputWidget(d->path), "/") &&
            siteRoot.end == parts.path.start /* not a user root */) {
            setTextCStr_InputWidget(d->path, ""); /* might as well show the hint */
        }
        if (isEdit) {
            /* Modify the UI to be appropriate for editing an existing resource. */
            setTextCStr_LabelWidget(findChild_Widget(w, "upload.title"),
                                    "${heading.upload.edit}");
            setTextCStr_LabelWidget((iLabelWidget *) acceptButton_UploadWidget_(d),
                                    uiTextAction_ColorEscape "${dlg.upload.edit}");
            if (isUsingPanelLayout_Mobile()) {
                showCollapsed_Widget(findChild_Widget(w, "upload.type"), iFalse); /* just text */
                setFlags_Widget(findChild_Widget(w, "dlg.upload.urllabel"), disabled_WidgetFlag, iTrue);
                setChevron_LabelWidget(findChild_Widget(w, "dlg.upload.urllabel"), iFalse);
            }
            fetchEditableResource_UploadWidget_(d, requestUrl_UploadWidget_(d));
        }
    }
    else if (d->protocol == misfin_UploadProtocol) {
        set_String(&d->url, &d->originalUrl);
        setText_InputWidget(d->path, collect_String(mid_String(&d->url, 9, iInvalidSize)));
        misfinAddressValidator_UploadWidget_(d->path, d);
    }
    /* Layout updatae. */
    if (isUsingPanelLayout_Mobile()) {
        updateUrlPanelButton_UploadWidget_(d);
    }
    else {
        setFixedSize_Widget(as_Widget(d->path),
                            init_I2(width_Widget(d->tabs) - width_Widget(d->info), -1));
    }
}

void setUrl_UploadWidget(iUploadWidget *d, const iString *url) {
    setUrlPort_UploadWidget_(d, url, 0);
    remakeIdentityItems_UploadWidget_(d);
    updateIdentityDropdown_UploadWidget_(d);
}

void setIdentity_UploadWidget(iUploadWidget *d, const iGmIdentity *ident) {
    if (ident) {
        postCommand_Widget(as_Widget(d),
                           "upload.setid fp:%s",
                           cstrCollect_String(hexEncode_Block(&ident->fingerprint)));
    }
}

void setResponseViewer_UploadWidget(iUploadWidget *d, iDocumentWidget *doc) {
    d->viewer = doc;
}

void setText_UploadWidget(iUploadWidget *d, const iString *text) {
    setText_InputWidget(d->input, text);
    deselect_InputWidget(d->input);
    moveCursorHome_InputWidget(d->input);
    updateButtonExcerpts_UploadWidget_(d);
}

static void requestFinished_UploadWidget_(iUploadWidget *d, iGmRequest *req) {
    postCommand_Widget(d, "upload.request.finished reqid:%u", id_GmRequest(req));
}

static void updateFileInfo_UploadWidget_(iUploadWidget *d) {
    iFileInfo *info = iClob(new_FileInfo(&d->filePath));
    if (isDirectory_FileInfo(info)) {
        makeMessage_Widget("${heading.upload.error.file}",
                           "${upload.error.directory}",
                           (iMenuItem[]){ "${dlg.message.ok}", 0, 0, "message.ok" }, 1);
        clear_String(&d->filePath);
        d->fileSize = 0;
        return;
    }
    if (!exists_FileInfo(info)) {
        setTextCStr_LabelWidget(d->filePathLabel, "");
        return;
    }
    d->fileSize = size_FileInfo(info);
    if (isMobile_Platform()) {
        setTextCStr_LabelWidget(d->filePathLabel, cstr_Rangecc(baseName_Path(&d->filePath)));
    }
    else {
        setText_LabelWidget(d->filePathLabel, &d->filePath);
    }
    setTextCStr_LabelWidget(d->fileSizeLabel, formatCStrs_Lang("num.bytes.n", d->fileSize));
    setTextCStr_InputWidget(d->mime, mediaType_Path(&d->filePath));
    updateButtonExcerpts_UploadWidget_(d);
}

static void filePathValidator_UploadWidget_(iInputWidget *input, void *context) {
    iUploadWidget *d = context;
    iString *path = collect_String(copy_String(text_InputWidget(input)));
    clean_Path(path);
    iFileInfo *info = new_FileInfo(path);
    if (exists_FileInfo(info) && !isDirectory_FileInfo(info)) {
        set_String(&d->filePath, path);
        updateFileInfo_UploadWidget_(d);
    }
    else {
        clear_String(&d->filePath);
        setTextCStr_LabelWidget(d->fileSizeLabel, "");
    }
    iRelease(info);
}

static iBool createRequest_UploadWidget_(iUploadWidget *d, iBool isText) {
    iAssert(d->request == NULL);
    d->request = new_GmRequest(certs_App());
    setSendProgressFunc_GmRequest(d->request, updateProgress_UploadWidget_);
    setUserData_Object(d->request, d);
    setUrl_GmRequest(d->request, requestUrl_UploadWidget_(d));
    if (d->protocol == titan_UploadProtocol) {
        setupRequest_UploadWidget_(d, NULL, d->request);
    }
    else if (d->protocol == misfin_UploadProtocol) {
        iGmIdentity *ident = findIdentity_GmCerts(certs_App(), &d->idFingerprint);
        if (ident) {
            setIdentity_GmRequest(d->request, ident);
        }
    }
    /* Attach the data to upload. */
    if (isText) {
        /* Uploading text. */
        const iString *text = text_InputWidget(d->input);
        if (d->misfinStage == verifyRecipient_MisfinStage) {
            text = collectNew_String(); /* blank message */
        }
        else if (d->misfinStage == carbonCopyToSelf_MisfinStage) {
            /* Include metadata line showing the actual recipient. */
            text = collectNewFormat_String(": %s\n\n%s", cstr_String(text_InputWidget(d->path)),
                                           cstr_String(text));
        }
        setUploadData_GmRequest(d->request,
                                collectNewCStr_String("text/plain"),
                                utf8_String(text),
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
            return iFalse;
        }
        setUploadData_GmRequest(d->request,
                                text_InputWidget(d->mime),
                                collect_Block(readAll_File(f)),
                                text_InputWidget(d->token));
        close_File(f);
    }
    iConnect(GmRequest, d->request, finished, d, requestFinished_UploadWidget_);
    return iTrue;
}

static iBool processEvent_UploadWidget_(iUploadWidget *d, const SDL_Event *ev) {
    iWidget *w = as_Widget(d);
    const char *cmd = command_UserEvent(ev);
    if (isResize_UserEvent(ev) || equal_Command(cmd, "keyboard.changed")) {
        updateInputMaxHeight_UploadWidget_(d);
    }
    else if (equal_Command(cmd, "panel.changed")) {
        const size_t panelIndex = currentPanelIndex_Mobile(w);
        if (panelIndex == 0) {
            setFocus_Widget(as_Widget(d->input));
        }
        else {
            setFocus_Widget(NULL);
        }
        if (isPortraitPhone_App() && (d->protocol == misfin_UploadProtocol ||
                                      isVisible_Widget(findChild_Widget(w, "upload.type")))) {
            /* Don't upload from subpages in non-edit mode. */
            enableUploadPanelButton_UploadWidget_(d, panelIndex == iInvalidPos);
        }
        refresh_Widget(d->input);
        return iFalse;
    }
#if defined (iPlatformAppleMobile) || defined (iPlatformAndroidMobile)
    else if (deviceType_App() != desktop_AppDeviceType && equal_Command(cmd, "menu.opened")) {
        setFocus_Widget(NULL); /* overlaid text fields! */
        refresh_Widget(d->input);
        return iFalse;
    }
#endif
    else if (equal_Command(cmd, "upload.cancel")) {
        setupSheetTransition_Mobile(w, iFalse);
        destroy_Widget(w);
        return iTrue;
    }
    else if (d->protocol == titan_UploadProtocol && isCommand_Widget(w, ev, "upload.setport")) {
        if (hasLabel_Command(cmd, "value")) {
            setValue_SiteSpec(collectNewRange_String(urlRoot_String(&d->originalUrl)),
                              titanPort_SiteSpecKey, arg_Command(cmd));
            setUrlPort_UploadWidget_(d, &d->originalUrl, arg_Command(cmd));
        }
        else {
            makeValueInput_Widget(root_Widget(w),
                                  collectNewFormat_String("%u", titanPortForUrl_(&d->originalUrl)),
                                  uiHeading_ColorEscape "${heading.uploadport}",
                                  "${dlg.uploadport.msg}",
                                  "${dlg.uploadport.set}",
                                  format_CStr("upload.setport ptr:%p", d));
        }
        return iTrue;
    }
    if ((d->protocol == titan_UploadProtocol || d->protocol == misfin_UploadProtocol) &&
        isCommand_Widget(w, ev, "upload.setid")) {
        if (hasLabel_Command(cmd, "fp")) {
            set_Block(&d->idFingerprint,
                      collect_Block(hexDecode_Rangecc(range_Command(cmd, "fp"))));
            d->idMode = dropdown_UploadIdentity;
            /* Remember the most recently selected Misfin identity. */
            if (d->protocol == misfin_UploadProtocol) {
                setRecentMisfinId_App(findIdentity_GmCerts(certs_App(), &d->idFingerprint));
            }
        }
        else if (arg_Command(cmd)) {
            clear_Block(&d->idFingerprint);
            d->idMode = defaultForSite_UploadIdentity;
        }
        else {
            clear_Block(&d->idFingerprint);
            d->idMode = none_UploadIdentity;
        }
        updateIdentityDropdown_UploadWidget_(d);
        return iTrue;
    }
    if (isCommand_Widget(w, ev, "upload.settype")) {
        const int type = arg_Command(cmd);
        iWidget *buttons[2] = {
            findChild_Widget(w, "dlg.upload.text.button"),
            findChild_Widget(w, "dlg.upload.file.button")
        };
        iWidget *radio[2] = {
            findChild_Widget(w, "upload.type.text"),
            findChild_Widget(w, "upload.type.file")
        };
        iForIndices(i, buttons) {
            setFlags_Widget(radio[i], selected_WidgetFlag, type == i);
            showCollapsed_Widget(buttons[i], type == i);
        }
        /* When showing detail on the side, immediately change to the right panel. */
        if (isSideBySideLayout_Mobile()) {
            postCommand_Widget(buttons[type], "panel.open");
        }
        return iTrue;
    }
    if (equal_Command(cmd, "upload.trusted.check")) {
        if (d->protocol == misfin_UploadProtocol) {
            setFlags_Widget(findChild_Widget(w, "upload.trusted"),
                            hidden_WidgetFlag,
                            checkTrust_Misfin(text_InputWidget(d->path), NULL, NULL) !=
                                trusted_MisfinResult);
            remove_Periodic(periodic_App(), d);
        }
        return iTrue;
    }
    if (isCommand_UserEvent(ev, "upload.text.export")) {
#if defined (iPlatformAppleMobile)
        openTextActivityView_iOS(text_InputWidget(d->input));
#endif
        return iTrue;
    }
    if (isCommand_UserEvent(ev, "upload.text.delete")) {
        if (argLabel_Command(command_UserEvent(ev), "confirmed")) {
            setTextCStr_InputWidget(d->input, "");
            setFocus_Widget(as_Widget(d->input));
        }
        else {
            setFocus_Widget(NULL);
            openMenu_Widget(makeMenu_Widget(root_Widget(w), (iMenuItem[]){
                { delete_Icon " " uiTextCaution_ColorEscape "${menu.upload.delete.confirm}", 0, 0,
                    "upload.text.delete confirmed:1" }
            }, 1), zero_I2());
        }
        return iTrue;
    }
    if (isCommand_UserEvent(ev, "upload.text.selectall")) {
        setFocus_Widget(as_Widget(d->input));
        refresh_Widget(as_Widget(d->input));
        postCommand_Widget(d->input, "input.selectall");
        return iTrue;
    }
    if (isCommand_Widget(as_Widget(d->path), ev, "input.ended")) {
        updateUrlPanelButton_UploadWidget_(d);
        return iFalse;
    }
    if (isUsingPanelLayout_Mobile() && isCommand_Widget(as_Widget(d->input), ev, "input.ended")) {
        updateButtonExcerpts_UploadWidget_(d);
        return iFalse;
    }
    if (isCommand_Widget(w, ev, "upload.accept")) {
        if (d->editRequest) {
            return iTrue; /* ongoing edit request */
        }
        if (d->allowRetryEdit) {
            /* Edit request failed, but we can retry. */
            iAssert(endsWithCase_Rangecc(urlPath_String(&d->originalUrl), ";edit"));
            fetchEditableResource_UploadWidget_(d, requestUrl_UploadWidget_(d));
            return iTrue;
        }
        iBool isText;
        if (d->tabs) {
            const size_t tabIndex = tabPageIndex_Widget(d->tabs, currentTabPage_Widget(d->tabs));
            isText = (tabIndex == 0);
        }
        else {
            // const size_t panelIndex = currentPanelIndex_Mobile(w);
            // if (panelIndex != iInvalidPos) {
            //     return iTrue;
            // }
            isText = isVisible_Widget(findChild_Widget(w, "dlg.upload.text.button"));
        }
        if (!isText && !fileExists_FileInfo(&d->filePath)) {
            return iTrue;
        }
        if (d->protocol == misfin_UploadProtocol) {
            // if (!checkTrust_Misfin(text_InputWidget(d->path), NULL, NULL)) {
                /* First check if the recipient actually exists. */
                // d->misfinStage = verifyRecipient_MisfinStage;
            // }
            // else {
            d->misfinStage = sendToRecipient_MisfinStage;
            // }
        }
        if (!createRequest_UploadWidget_(d, isText)) {
            return iTrue;
        }
        submit_GmRequest(d->request);
        /* The dialog will remain open until the request finishes, showing upload progress. */
        setFocus_Widget(NULL);
        setFlags_Widget(d->tabs, disabled_WidgetFlag, iTrue);
        setFlags_Widget(as_Widget(d->token), disabled_WidgetFlag, iTrue);
        setFlags_Widget(acceptButton_UploadWidget_(d), disabled_WidgetFlag, iTrue);
        return iTrue;
    }
    else if (isCommand_Widget(w, ev, "upload.request.updated") &&
             id_GmRequest(d->request) == argU32Label_Command(cmd, "reqid")) {
        setTextCStr_LabelWidget(d->counter,
                                formatCStrs_Lang("num.bytes.n", argU32Label_Command(cmd, "arg")));
        arrange_Widget(parent_Widget(d->counter));
    }
    else if (isCommand_Widget(w, ev, "upload.request.finished") &&
             id_GmRequest(d->request) == argU32Label_Command(cmd, "reqid")) {
        if (isSuccess_GmStatusCode(status_GmRequest(d->request))) {
            setBackupFileName_InputWidget(d->input, NULL); /* erased */
        }
        if (d->protocol == misfin_UploadProtocol) {
            handleMisfinRequestFinished_UploadWidget_(d);
            return iTrue;
        }
        if (d->viewer) {
            takeRequest_DocumentWidget(d->viewer, d->request);
            d->request = NULL; /* DocumentWidget has it now. */
        }
        setupSheetTransition_Mobile(w, iFalse);
        releaseFile_UploadWidget_(d);
        destroy_Widget(w);
        return iTrue;
    }
    else if (isCommand_Widget(w, ev, "upload.fetch.progressed")) {
        updateTextCStr_LabelWidget(
            d->editLabel, formatCStrs_Lang("num.bytes.n", argU32Label_Command(cmd, "arg")));
        return iTrue;
    }
    else if (isCommand_Widget(w, ev, "upload.fetched")) {
        return handleEditContentResponse_UploadWidget_(d, argU32Label_Command(cmd, "reqid"));
    }
    else if (isCommand_Widget(w, ev, "input.resized")) {
        updateInputMaxHeight_UploadWidget_(d);
        if (!isUsingPanelLayout_Mobile()/* && !(w->flags2 & (leftEdgeResizing_WidgetFlag2 |
                                                           rightEdgeResizing_WidgetFlag2))*/) {
            resizeToLargestPage_Widget(d->tabs);
            arrange_Widget(w);
            refresh_Widget(w);
            return iTrue;
        }
        else {
            refresh_Widget(as_Widget(d->input));
        }
    }
    else if (isDesktop_Platform() &&
             (equal_Command(cmd, "zoom.set") || equal_Command(cmd, "zoom.delta"))) {
        int sizeIndex = prefs_App()->editorZoomLevel;
        if (equal_Command(cmd, "zoom.set")) {
            sizeIndex = 0;
        }
        else {
            sizeIndex += iSign(arg_Command(cmd));
            sizeIndex = iClamp(sizeIndex, 0, 3);
        }
        setEditorZoomLevel_App(sizeIndex);
        setFont_InputWidget(d->input, font_UploadWidget_(d, regular_FontStyle));
        refresh_Widget(d->input);
        return iTrue;
    }
    else if (isCommand_UserEvent(ev, "prefs.editor.highlight.changed")) {
        if (arg_Command(command_UserEvent(ev))) {
            setHighlighter_InputWidget(d->input, gemtextHighlighter_UploadWidget_, d);
        }
        else {
            setHighlighter_InputWidget(d->input, NULL, NULL);
        }
        refresh_Widget(d->input);
        return iFalse;
    }
    else if (isCommand_Widget(w, ev, "upload.pickfile")) {
#if defined (iPlatformAppleMobile) || defined (iPlatformAndroidMobile)
        if (hasLabel_Command(cmd, "path")) {
            releaseFile_UploadWidget_(d);
            set_String(&d->filePath, collect_String(suffix_Command(cmd, "path")));
            updateFileInfo_UploadWidget_(d);
        }
        else {
            pickFile_Mobile(format_CStr("upload.pickfile ptr:%p", d));
        }
#endif
        return iTrue;
    }
    if (ev->type == SDL_DROPFILE) {
        if (d->protocol == misfin_UploadProtocol) {
            return iFalse;
        }
        /* Switch to File tab. */
        if (d->tabs) {
            showTabPage_Widget(d->tabs, tabPage_Widget(d->tabs, 1));
        }
        else {
            postCommand_Widget(w, "upload.settype arg:1");
        }
        releaseFile_UploadWidget_(d);
        setCStr_String(&d->filePath, ev->drop.file);
        if (d->filePathInput) {
            setTextCStr_InputWidget(d->filePathInput, ev->drop.file);
            filePathValidator_UploadWidget_(d->filePathInput, d);
        }
        else {
            updateFileInfo_UploadWidget_(d);
        }
        return iTrue;
    }
    return processEvent_Widget(w, ev);
}

void sizeChanged_UploadWidget_(iUploadWidget *d) {
    iWidget *w = as_Widget(d);
    if (w->flags2 & horizontallyResizable_WidgetFlag2) {
        const int newWidth = width_Widget(d) - 6 * gap_UI;
        setFixedSize_Widget(d->tabs, init_I2(newWidth, -1));
        setFixedSize_Widget(as_Widget(d->input), init_I2(newWidth, -1));
        updateFieldWidths_UploadWidget(d);
        updateInputMaxHeight_UploadWidget_(d);
        resizeToLargestPage_Widget(d->tabs);
        arrange_Widget(d->tabs);
        refresh_Widget(d);
    }
}

iBeginDefineSubclass(UploadWidget, Widget)
    .processEvent = (iAny *) processEvent_UploadWidget_,
    .draw         = draw_Widget,
    .sizeChanged  = (iAny *) sizeChanged_UploadWidget_,
iEndDefineSubclass(UploadWidget)
