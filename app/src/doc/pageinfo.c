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

#include "pageinfo.h"

#include "../app.h"
#include "../gmutil.h"
#include "../history.h"
#include "../sync.h"
#include "fetch.h"
#include "ui/command.h"
#include "ui/documentwidget.h"
#include "ui/keys.h"
#include "ui/labelwidget.h"
#include "ui/miscdialogs.h"
#include "ui/util.h"
#include "ui/widget.h"

#include <lagrange/gmcerts.h>
#include <lagrange/gmrequest.h>
#include <lagrange/lang.h>
#include <lagrange/prefs.h>
#include <SDL3/SDL_clipboard.h>

static void showPageInfo_DocumentWidget_(iDocumentWidget *d) {
    const iDocumentFetch *fetch = fetch_DocumentWidget(d);
    const iString *url          = url_DocumentWidget(d);
    const char *unchecked       = red_ColorEscape "☐";
    const char *checked         = green_ColorEscape "☑";
    const iBool haveFingerprint = (fetch->certFlags & haveFingerprint_GmCertFlag) != 0;
    const int   requiredForTrust =
        (available_GmCertFlag | haveFingerprint_GmCertFlag | timeVerified_GmCertFlag);
    const iBool canTrust = ~fetch->certFlags & trusted_GmCertFlag &&
                           ((fetch->certFlags & requiredForTrust) == requiredForTrust);
    const iRecentUrl *recent = constMostRecentUrl_History(history_DocumentWidget(d));
    const iString    *meta   = &fetch->sourceMime;
    if (recent && recent->cachedResponse) {
        meta = &recent->cachedResponse->meta;
    }
    iString *msg = collectNew_String();
    if (isEmpty_String(&fetch->sourceHeader)) {
        appendFormat_String(msg,
                            "%s\n%s\n",
                            cstr_String(meta),
                            formatCStrs_Lang("num.bytes.n", size_Block(&fetch->sourceContent)));
    }
    else {
        appendFormat_String(msg, "%s\n", cstr_String(&fetch->sourceHeader));
        if (size_Block(&fetch->sourceContent)) {
            appendFormat_String(
                msg, "%s\n", formatCStrs_Lang("num.bytes.n", size_Block(&fetch->sourceContent)));
        }
    }
    if (equalCase_Rangecc(urlScheme_String(url), "gemini") ||
        equalCase_Rangecc(urlScheme_String(url), "gophers")) {
        appendFormat_String(
            msg,
            "\n%s${pageinfo.cert.status}\n"
            "%s%s  %s\n"
            "%s%s  %s%s\n"
            "%s%s  %s (%04d-%02d-%02d %02d:%02d:%02d)\n"
            "%s%s  %s",
            uiHeading_ColorEscape,
            fetch->certFlags & authorityVerified_GmCertFlag ? checked
                                                            : uiText_ColorEscape "☐",
            uiText_ColorEscape,
            fetch->certFlags & authorityVerified_GmCertFlag ? "${pageinfo.cert.ca.verified}"
                                                            : "${pageinfo.cert.ca.unverified}",
            fetch->certFlags & domainVerified_GmCertFlag ? checked : unchecked,
            uiText_ColorEscape,
            fetch->certFlags & domainVerified_GmCertFlag ? "${pageinfo.domain.match}"
                                                         : "${pageinfo.domain.mismatch}",
            ~fetch->certFlags & domainVerified_GmCertFlag
                ? format_CStr(" (%s)", cstr_String(fetch->certSubject))
                : "",
            fetch->certFlags & timeVerified_GmCertFlag ? checked : unchecked,
            uiText_ColorEscape,
            fetch->certFlags & timeVerified_GmCertFlag ? "${pageinfo.cert.notexpired}"
                                                       : "${pageinfo.cert.expired}",
            fetch->certExpiry.year,
            fetch->certExpiry.month,
            fetch->certExpiry.day,
            fetch->certExpiry.hour,
            fetch->certExpiry.minute,
            fetch->certExpiry.second,
            fetch->certFlags & trusted_GmCertFlag ? checked : unchecked,
            uiText_ColorEscape,
            fetch->certFlags & trusted_GmCertFlag ? "${pageinfo.cert.trusted}"
                                                  : "${pageinfo.cert.untrusted}");
    }
    setFocus_Widget(NULL);

    const iMenuItem fingerprintItems[] = {
        { "${dlg.cert.fingerprint.full}", 0, 0, "server.copycert arg:1" },
        { "${dlg.cert.fingerprint.pubkey}", 0, 0, "server.copycert" },
    };

    iArray *items = new_Array(sizeof(iMenuItem));
    if (canTrust) {
        pushBack_Array(items,
                       &(iMenuItem){ uiTextAction_ColorEscape "\x1b[1m${dlg.cert.trust}",
                                     trustServerCertificate_KeyShortcut,
                                     "server.trustcert" });
    }
    const iRangecc root = urlRoot_String(url);
    if (!isEmpty_Range(&root)) {
        pushBack_Array(items, &(iMenuItem){ "${pageinfo.settings}", 0, 0, "document.sitespec" });
    }
    if (haveFingerprint && deviceType_App() != desktop_AppDeviceType) {
        /* The sheet-based popup does not have buttons. */
        pushBackN_Array(items, fingerprintItems, iElemCount(fingerprintItems));
    }
    if (!isEmpty_Array(items)) {
        pushBack_Array(items, &(iMenuItem){ "---", 0, 0, 0 });
    }
    pushBack_Array(items, &(iMenuItem){ "${close}", 0, 0, "message.ok" });
    iWidget *dlg = makeQuestion_Widget(uiHeading_ColorEscape "${heading.pageinfo}",
                                       cstr_String(msg),
                                       data_Array(items),
                                       size_Array(items));
    /* Fingerprint menu. */
    if (deviceType_App() == desktop_AppDeviceType) {
        iWidget *buttons = findChild_Widget(dlg, "dialogbuttons");
        if (haveFingerprint) {
            iLabelWidget *fpMenu = makeMenuButton_LabelWidget(
                "${dlg.cert.fingerprint}", fingerprintItems, iElemCount(fingerprintItems));
            if (!canTrust) {
                addChildPos_Widget(buttons, iClob(fpMenu), front_WidgetAddPos);
            }
            else {
                insertChildAfter_Widget(buttons, iClob(fpMenu), 0);
            }
        }
    }
    delete_Array(items);
    arrange_Widget(dlg);
    addAction_Widget(dlg, SDLK_ESCAPE, 0, "message.ok");
    addAction_Widget(dlg, SDLK_SPACE, 0, "message.ok");
}

iBool handlePageInfoCommand_DocumentWidget(iDocumentWidget *d, const char *cmd) {
    if (document_App() != d) {
        return iFalse;
    }
    iWidget              *w     = as_Widget(d);
    const iDocumentFetch *fetch = fetch_DocumentWidget(d);
    const iString        *url   = url_DocumentWidget(d);
    if (equal_Command(cmd, "document.info")) {
        showPageInfo_DocumentWidget_(d);
        return iTrue;
    }
    else if (equal_Command(cmd, "document.sitespec")) {
        if (isRunning_Sync()) {
            /* The settings may change when the synced data is applied. */
            makeSimpleMessage_Widget(uiHeading_ColorEscape "${heading.sync}", "${sync.busy.sitespec}");
        }
        else if (!findWidget_App("sitespec.palette")) {
            makeSiteSpecificSettings_Widget(url);
        }
        return iTrue;
    }
    else if (equal_Command(cmd, "server.unexpire")) {
        const iRangecc host = urlHost_String(url);
        const uint16_t port = urlPort_String(url);
        if (!isEmpty_Block(fetch->certFingerprint) && !isEmpty_Range(&host)) {
            iTime expiry;
            initCurrent_Time(&expiry);
            iTime oneHour; /* One hour is long enough for a single visit (?). */
            initSeconds_Time(&oneHour, 3600);
            add_Time(&expiry, &oneHour);
            iDate expDate;
            init_Date(&expDate, &expiry);
            setTrusted_GmCerts(certs_App(), host, port, fetch->certFingerprint, &expDate);
            postCommand_Widget(w, "navigate.reload");
        }
        return iTrue;
    }
    else if (equal_Command(cmd, "server.trustcert")) {
        const iRangecc scheme = urlScheme_String(url);
        iRangecc host = urlHost_String(url);
        uint16_t port = urlPort_String(url);
        if (isProxyRequest_DocumentWidget(d) && schemeProxy_Prefs(get_Prefs(), scheme)) {
            const iString *proxyHost;
            schemeProxyHostAndPort_Prefs(get_Prefs(), scheme, &proxyHost, &port);
            host = range_String(proxyHost);
        }
        if (!isEmpty_Block(fetch->certFingerprint) && !isEmpty_Range(&host)) {
            setTrusted_GmCerts(certs_App(), host, port, fetch->certFingerprint, &fetch->certExpiry);
            postCommand_Widget(w, "navigate.reload");
        }
        return iTrue;
    }
    else if (equal_Command(cmd, "server.copycert")) {
        const iString *fp = collect_String(
            hexEncode_Block(arg_Command(cmd) ? fetch->certFullFingerprint : fetch->certFingerprint));
        if (isTerminal_Platform()) {
            makeMessage_Widget(
                arg_Command(cmd) ? "${dlg.cert.fingerprint.full}"
                                 : "${dlg.cert.fingerprint.pubkey}",
                cstr_String(fp),
                (iMenuItem[]){ { "${dlg.message.ok}", SDLK_RETURN, 0, "message.ok" } },
                1);
        }
        else {
            SDL_SetClipboardText(cstr_String(fp));
        }
        return iTrue;
    }
    return iFalse;
}
