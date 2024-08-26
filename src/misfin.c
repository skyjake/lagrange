/* Copyright 2024 Jaakko Keränen <jaakko.keranen@iki.fi>

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

#include "misfin.h"
#include "app.h"
#include "ui/uploadwidget.h"
#include "ui/util.h"
#include "ui/window.h"

#include <the_Foundation/file.h>
#include <the_Foundation/path.h>
#include <the_Foundation/regexp.h>
#include <the_Foundation/stringlist.h>

iDeclareType(Misfin);

struct Impl_Misfin {
    iString trustedPath;
    iRegExp *trustPattern;
};

static iMisfin misfin_;

static iBool onlyMisfin_(void *context, const iGmIdentity *d) {
    iUnused(context);
    return isMisfin_GmIdentity(d);
}

void init_Misfin(const char *dir) {
    iMisfin *d = &misfin_;
    iZap(*d);
    initCStr_String(&d->trustedPath, concatPath_CStr(dir, "trusted.misfin.txt"));
    d->trustPattern = new_RegExp("^([0-9a-f]{64}) ([^\\s]+)$", 0);
}

void deinit_Misfin(void) {
    iMisfin *d = &misfin_;
    iRelease(d->trustPattern);
    deinit_String(&d->trustedPath);
}

enum iMisfinResult checkTrust_Misfin(const iString *address, const iString *expectedFingerprint,
                                     iString *fingerprint_out) {
    iMisfin *d = &misfin_;
    enum iMisfinResult result = unknown_MisfinResult;
    iFile *f = new_File(&d->trustedPath);
    if (open_File(f, readOnly_FileMode | text_FileMode)) {
        const iStringList *lines = readLines_File(f);
        iConstForEach(StringList, i, lines) {
            iRegExpMatch m;
            init_RegExpMatch(&m);
            if (matchString_RegExp(d->trustPattern, i.value, &m)) {
                const iRangecc fp   = capturedRange_RegExpMatch(&m, 1);
                const iRangecc addr = capturedRange_RegExpMatch(&m, 2);
                if (!expectedFingerprint) {
                    /* Just checking if we know of this address. */
                    if (equalCase_Rangecc(addr, cstr_String(address))) {
                        result = trusted_MisfinResult;
                        if (fingerprint_out) {
                            setRange_String(fingerprint_out, fp);
                        }
                        break;
                    }
                    continue;
                }
                /* Compare the given fingerprint to the one previously seen. */
                if (equalCase_Rangecc(fp, cstr_String(expectedFingerprint))) {
                    /* A known, trusted fingerprint. */
                    result = trusted_MisfinResult;
                    break;
                }
                else if (equalCase_Rangecc(addr, cstr_String(address))) {
                    result = equalCase_Rangecc(fp, cstr_String(expectedFingerprint))
                                 ? trusted_MisfinResult
                                 : fingerprintMismatch_MisfinResult;
                    break;
                }
            }
        }
        iRelease(lines);
    }
    iRelease(f);
    return result;
}

void trust_Misfin(const iString *address, const iString *fingerprint) {
    iMisfin *d = &misfin_;
    iString *updated = new_String();
    iFile *f = new_File(&d->trustedPath);
    if (open_File(f, readOnly_FileMode | text_FileMode)) {
        const iStringList *lines = readLines_File(f);
        iConstForEach(StringList, i, lines) {
            iRegExpMatch m;
            init_RegExpMatch(&m);
            if (matchString_RegExp(d->trustPattern, i.value, &m)) {
                if (equalCase_Rangecc(capturedRange_RegExpMatch(&m, 2), cstr_String(address))) {
                    /* Skip any existing matching line. */
                    continue;
                }
            }
            /* Include this line without alteration. */
            append_String(updated, i.value);
            appendCStr_String(updated, "\n");
        }
        close_File(f);
        iRelease(lines);
    }
    /* At the end, append the newly trusted fingerprint. */
    append_String(updated, fingerprint);
    appendCStr_String(updated, " ");
    append_String(updated, address);
    appendCStr_String(updated, "\n");
    /* Write the new contents of the file. */
    if (open_File(f, writeOnly_FileMode | text_FileMode)) {
        write_File(f, utf8_String(updated));
    }
    iRelease(f);
    delete_String(updated);
}

const iPtrArray *listIdentities_Misfin(void) {
    return listIdentities_GmCerts(certs_App(), onlyMisfin_, NULL);
}

size_t numIdentities_Misfin(void) {
    return size_PtrArray(listIdentities_Misfin());
}

void openMessageComposer_Misfin(const iString *url, const iGmIdentity *sender) {
    if (!numIdentities_Misfin()) {
        makeSimpleMessage_Widget("${heading.upload.misfin.noident}",
                                 "${dlg.upload.misfin.noident}");
        return;
    }
    iUploadWidget *upload = new_UploadWidget(misfin_UploadProtocol);
    if (url) {
        setUrl_UploadWidget(upload, url);
    }
    if (sender) {
        setIdentity_UploadWidget(upload, sender);
    }
    else {
        /* Use the most recently used Misfin identity. */
        setIdentity_UploadWidget(
            upload,
            findIdentity_GmCerts(certs_App(),
                                 collect_Block(hexDecode_Rangecc(range_String(
                                     &prefs_App()->strings[recentMisfinId_PrefsString])))));
    }
    if (!url) {
        postCommand_Widget(upload, "focus.set id:upload.path");
    }
    addChild_Widget(get_Root()->widget, iClob(upload));
    setupSheetTransition_Mobile(as_Widget(upload), iTrue);
    /* User can resize the upload dialog. */
    setResizeId_Widget(as_Widget(upload), "upload");
    restoreWidth_Widget(as_Widget(upload));
    postRefresh_Window(get_Window());
}
