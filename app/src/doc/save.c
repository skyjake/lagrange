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
#include "../defs.h"
#include "../gmutil.h"
#include "book.h"
#include "fetch.h"
#include "inlinemedia.h"
#include "ui/command.h"
#include "ui/filedialog.h"
#include "ui/root.h"
#include "ui/util.h"
#if defined (iPlatformAppleMobile)
#   include "platform/ios.h"
#endif
#if defined (iPlatformAndroidMobile)
#   include "platform/android.h"
#endif
#if defined (iPlatformEmscripten)
#   include "platform/web.h"
#endif

#include <lagrange/gmrequest.h>
#include <the_Foundation/file.h>
#include <the_Foundation/fileinfo.h>
#include <the_Foundation/path.h>
#include <errno.h>
#include <string.h>

static iBool saveToFile_(const iString *savePath, const iBlock *content, const iString *mime,
                         iBool showDialog) {
    iBool ok = iFalse;
    /* Write the file. */ {
        iFile *f = new_File(savePath);
        if (open_File(f, writeOnly_FileMode)) {
            write_File(f, content);
            close_File(f);
            const size_t size   = size_Block(content);
            const iBool  isMega = size >= 1000000;
#if defined (iPlatformAppleMobile)
            if (showDialog) {
                exportDownloadedFile_iOS(savePath);
            }
#elif defined (iPlatformAndroidMobile)
            if (showDialog) {
                exportDownloadedFile_Android(savePath, mime);
            }
#elif defined (iPlatformEmscripten)
            if (showDialog) {
                exportDownloadedFile_Web(savePath, mime);
            }
#else
            if (showDialog) {
                const iMenuItem items[2] = {
#if defined (LAGRANGE_HAVE_FILE_REVEAL)
                    { revealFile_Label, 0, 0,
                        format_CStr("!reveal path:%s", cstr_String(savePath)) },
#else
                    { "${dlg.save.opendownload}", 0, 0,
                        format_CStr("!open url:%s", cstrCollect_String(makeFileUrl_String(savePath))) },
#endif
                    { "${dlg.message.ok}", 0, 0, "message.ok" },
                };
                makeMessage_Widget(uiHeading_ColorEscape "${heading.save}",
                                       format_CStr("%s\n${dlg.save.size} %.3f %s",
                                                   cstr_String(path_File(f)),
                                               isMega ? size / 1.0e6f : (size / 1.0e3f),
                                               isMega ? "${mb}" : "${kb}"),
                                       items,
                                       iElemCount(items));
            }
#endif
            ok = iTrue;
        }
        else {
            makeSimpleMessage_Widget(uiTextCaution_ColorEscape "${heading.save.error}",
                                     strerror(errno));
        }
        iRelease(f);
    }
    return ok;
}

static const iString *saveToDownloads_(const iString *url, const iString *mime, const iBlock *content,
                                       iBool showDialog) {
    const iString *savePath = downloadPathForUrl_App(url, mime);
    if (!saveToFile_(savePath, content, mime, showDialog)) {
        return collectNew_String();
    }
    return savePath;
}

static const iString *saveAsPath_(const iString *url, const iString *mime) {
    return collect_String(concat_Path(downloadDir_App(), fileNameForUrl_App(url, mime)));
}

iBool handleSaveCommand_DocumentWidget(iDocumentWidget *d, const char *cmd) {
    iWidget *w = as_Widget(d);
    if (equalWidget_Command(cmd, w, "document.media.save")) {
        const iGmLinkId      linkId = argLabel_Command(cmd, "link");
        const iMediaRequest *media  = findRequest_InlineMedia(d->media, linkId);
        if (media && hasLabel_Command(cmd, "path")) {
            saveToFile_(collect_String(suffix_Command(cmd, "path")),
                        body_GmRequest(media->req),
                        meta_GmRequest(media->req),
                        iFalse);
        }
        else if (media && argLabel_Command(cmd, "saveas")) {
            save_FileDialog(format_CStr("document.media.save link:%u ptr:%p", linkId, d),
                            "${menu.save.as}",
                            saveAsPath_(url_GmRequest(media->req), meta_GmRequest(media->req)));
        }
        else if (media) {
            saveToDownloads_(url_GmRequest(media->req),
                             meta_GmRequest(media->req),
                             body_GmRequest(media->req),
                             iTrue); /* shows a message */
        }
        return iTrue;
    }
    else if (equal_Command(cmd, "document.save") &&
             (hasLabel_Command(cmd, "path") ? pointer_Command(cmd) == d : document_App() == d)) {
        if (d->fetch->request) {
            makeSimpleMessage_Widget(uiTextCaution_ColorEscape "${heading.save.incomplete}",
                                     "${dlg.save.incomplete}");
        }
        else if (!isEmpty_Block(&d->fetch->sourceContent)) {
            if (hasLabel_Command(cmd, "path")) {
                saveToFile_(collect_String(suffix_Command(cmd, "path")),
                            &d->fetch->sourceContent,
                            &d->fetch->sourceMime,
                            iFalse);
            }
            else if (argLabel_Command(cmd, "saveas")) {
                save_FileDialog(format_CStr("document.save ptr:%p", d),
                                "${menu.save.as}",
                                saveAsPath_(d->mod.url, &d->fetch->sourceMime));
            }
            else if (argLabel_Command(cmd, "extview")) {
                if (isLocalFileScheme_Rangecc(urlScheme_String(d->mod.url)) &&
                    fileExists_FileInfo(collect_String(localFilePathFromUrl_String(d->mod.url)))) {
                    /* Already a file so just open it directly. */
                    postCommandf_Root(w->root, "!open default:1 url:%s", cstr_String(d->mod.url));
                }
                else {
                    const iString *tmpPath = temporaryPathForUrl_App(d->mod.url, &d->fetch->sourceMime);
                    if (saveToFile_(tmpPath, &d->fetch->sourceContent, &d->fetch->sourceMime, iFalse)) {
                        postCommandf_Root(w->root, "!open default:1 mime:%s url:%s",
                                          cstr_String(&d->fetch->sourceMime),
                                          cstrCollect_String(makeFileUrl_String(tmpPath)));
                    }
                }
            }
            else {
                const iBool    doOpen   = argLabel_Command(cmd, "open");
                const iString *savePath = saveToDownloads_(d->mod.url, &d->fetch->sourceMime,
                                                           &d->fetch->sourceContent, !doOpen);
                if (!isEmpty_String(savePath) && doOpen) {
                    postCommandf_Root(
                        w->root, "!open url:%s", cstrCollect_String(makeFileUrl_String(savePath)));
                }
            }
        }
        return iTrue;
    }
    else if (equal_Command(cmd, "document.savebook") &&
             (hasLabel_Command(cmd, "doc") ? pointerLabel_Command(cmd, "doc") == d
                                           : document_App() == d)) {
        if (d->fetch->request) {
            makeSimpleMessage_Widget(uiTextCaution_ColorEscape "${heading.save.incomplete}",
                                     "${dlg.save.incomplete}");
        }
        else if (!isEmpty_Block(&d->fetch->sourceContent)) {
            saveToShelf_DocumentBook(d->book, argLabel_Command(cmd, "overwrite"));
        }
        return iTrue;
    }
    return iFalse;
}
