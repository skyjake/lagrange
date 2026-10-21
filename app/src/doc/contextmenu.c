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

#include "contextmenu.h"
#include "impl.h"

#include "../app.h"
#include "../defs.h"
#include "../gmutil.h"
#include "ui/keys.h"
#include "linkinfo.h"
#include "ui/simpledocumentwidget.h"
#include "ui/util.h"
#include "ui/widget.h"

#include <lagrange/gmrequest.h>

static iBool isNativeScheme_(iRangecc scheme) {
    return equalCase_Rangecc(scheme, "gemini") || willUseProxy_App(scheme) ||
           equalCase_Rangecc(scheme, "data") || isLocalFileScheme_Rangecc(scheme) ||
           equalCase_Rangecc(scheme, "finger") || isGopherScheme_Rangecc(scheme) ||
           equalCase_Rangecc(scheme, "spartan") || equalCase_Rangecc(scheme, "nex");
}

static void appendOpenItems_(iArray *items, const iString *linkUrl, const char *openArgs) {
    pushBackN_Array(
        items,
        (iMenuItem[]){
            { openTab_Icon " ${link.newtab}",
              0,
              0,
              format_CStr("!open%s newtab:1 url:%s", openArgs, cstr_String(linkUrl)) },
            { openTabBg_Icon " ${link.newtab.background}",
              0,
              0,
              format_CStr("!open%s newtab:2 url:%s", openArgs, cstr_String(linkUrl)) },
            { openWindow_Icon " ${link.newwindow}",
              0,
              KMOD_DESKTOP,
              format_CStr("!open%s newwindow:1 url:%s", openArgs, cstr_String(linkUrl)) },
            { "${link.side}",
              0,
              KMOD_DESKTOP | KMOD_TABLET,
              format_CStr("!open%s newtab:4 url:%s", openArgs, cstr_String(linkUrl)) },
            { "${link.side.newtab}",
              0,
              KMOD_DESKTOP | KMOD_TABLET,
              format_CStr("!open%s newtab:5 url:%s", openArgs, cstr_String(linkUrl)) },
        },
        5);
}

static const iString *selectedText_DocumentWidget_(const iDocumentWidget *d) {
    iRangecc mark = d->selectMark;
    if (mark.start > mark.end) {
        iSwap(const char *, mark.start, mark.end);
    }
    return collect_String(newRange_String(mark));
}

iWidget *makeLinkContextMenuWithParameters_DocumentWidget(iDocumentWidget *d,
                                                          const iString   *linkUrl,
                                                          const iString   *linkLabel,
                                                          iGmLinkId        linkId,
                                                          enum iMediaType  linkMediaType) {
    iWidget       *w      = as_Widget(d);
    iArray        *items  = collectNew_Array(sizeof(iMenuItem));
    const iRangecc scheme = urlScheme_String(linkUrl);
    const iBool    isFile = isLocalFileScheme_Rangecc(scheme) &&
                            !startsWithCase_String(linkUrl, "local:/tinylog/"); /* virtual */
    if (deviceType_App() != desktop_AppDeviceType && linkId) {
        /* Show the link as the first, non-interactive item. */
        iString *infoText = collectNew_String();
        infoText_LinkInfo(d, linkId, infoText);
        pushBack_Array(items,
                       &(iMenuItem){ format_CStr("```%s", cstr_String(infoText)), 0, 0, NULL });
    }
    const char *originArg = format_CStr(" origin:%s", cstr_String(id_Widget(w)));
    const iBool isGemini  = equalCase_Rangecc(scheme, "gemini");
    if (isNativeScheme_(scheme)) {
        /* Regular links that we can open. */
        appendOpenItems_(items,
                         linkUrl,
                         format_CStr(" query:%d%s%s",
                                     isSpartanQueryLink_DocumentWidget(d, linkId),
                                     originArg,
                                     setIdentArg_DocumentWidget(d, linkUrl)));
        if (isFile) {
            pushBack_Array(items, &(iMenuItem){ "---" });
            pushBack_Array(
                items,
                &(iMenuItem){ export_Icon " ${menu.open.external}",
                              0,
                              0,
                              format_CStr("!open default:1 url:%s", cstr_String(linkUrl)) });
#if defined (LAGRANGE_HAVE_FILE_REVEAL)
            pushBack_Array(items,
                           &(iMenuItem){ revealFile_Label,
                                         0,
                                         0,
                                         format_CStr("!reveal url:%s", cstr_String(linkUrl)) });
#endif
        }
    }
    else if (!willUseProxy_App(scheme)) {
        pushBack_Array(items,
                       &(iMenuItem){ openExt_Icon " ${link.browser}",
                                     0,
                                     0,
                                     format_CStr("!open default:1 url:%s", cstr_String(linkUrl)) });
    }
    if (willUseProxy_App(scheme)) {
        pushBackN_Array(
            items,
            (iMenuItem[]){ { "---" },
                           { isGemini ? "${link.noproxy}" : openExt_Icon " ${link.browser}",
                             0,
                             0,
                             format_CStr("!open%s noproxy:1 url:%s",
                                         originArg,
                                         cstr_String(linkUrl)) } },
            2);
    }
    iString *encLabel = copy_String(linkLabel);
    urlEncodeSpaces_String(encLabel);
    pushBackN_Array(
        items,
        (iMenuItem[]){
            { "---" },
            { copy_Icon " ${link.copy}", 0, 0, "document.copylink" },
            { "${link.copy.label}", 0, 0, "document.copylink label:1" },
            { "${link.copy.gemtext}", 0, 0, "document.copylink gemtext:1" },
            { "---" },
            { bookmark_Icon " ${link.bookmark}", 0, 0,
              format_CStr("!bookmark.add title:%s url:%s", cstr_String(encLabel), cstr_String(linkUrl)) },
            { clipboard_Icon " ${link.snippet}", 0, 0,
              format_CStr("!snippet.add content:%s", cstr_String(linkUrl)) },
            { "---" },
            { magnifyingGlass_Icon " ${link.searchurl}", 0, 0,
              format_CStr("!searchurl address:%s", cstr_String(linkUrl)) },
        },
        9);
    delete_String(encLabel);
    if (isNativeScheme_(scheme) && linkId && linkMediaType != download_MediaType &&
        !isLocalFileScheme_Rangecc(scheme)) {
        pushBackN_Array(items,
                        (iMenuItem[]){
                            { "---" },
                            { download_Icon " ${link.download}", 0, 0, "document.downloadlink" },
                        },
                        2);
    }
    iMediaRequest *mediaReq;
    if ((mediaReq = findMediaRequest_DocumentWidget(d, linkId)) != NULL &&
        linkMediaType != download_MediaType) {
        if (isFinished_GmRequest(mediaReq->req)) {
            pushBack_Array(
                items,
                &(iMenuItem){ download_Icon " " saveToDownloads_Label,
                              0,
                              0,
                              format_CStr("document.media.save link:%u", linkId) });
#if defined (LAGRANGE_FILE_DIALOGS)
            pushBack_Array(
                items,
                &(iMenuItem){ "${menu.save.as}",
                              0,
                              0,
                              format_CStr("document.media.save link:%u saveas:1", linkId) });
#endif
        }
    }
    if (isFile) {
        /* Local files may be deleted. */
        pushBack_Array(items, &(iMenuItem){ "---" });
        pushBack_Array(
            items,
            &(iMenuItem){ delete_Icon " " uiTextCaution_ColorEscape "${link.file.delete}",
                          0,
                          0,
                          format_CStr("!file.delete confirm:1 path:%s",
                                      cstrCollect_String(localFilePathFromUrl_String(linkUrl))) });
    }
    return makeMenu_Widget(w, data_Array(items), size_Array(items));
}

iWidget *makeLinkContextMenu_DocumentWidget(iDocumentWidget *d, const iGmRun *link) {
    /* Construct the link context menu, depending on what kind of link was clicked. */
    interactingWithLink_DocumentWidget(d, link->linkId); /* perhaps will be triggered */
    return makeLinkContextMenuWithParameters_DocumentWidget(
        d,
        linkUrl_GmDocument(document_DocumentWidget(d), link->linkId),
        collectNewRange_String(linkLabel_GmDocument(document_DocumentWidget(d), link->linkId)),
        link->linkId,
        link->mediaType);
}

iWidget *makePageContextMenu_DocumentWidget(iDocumentWidget *d) {
    iArray *items = collectNew_Array(sizeof(iMenuItem));
    if (!isEmpty_Range(&d->selectMark)) {
        pushBackN_Array(
            items,
            (iMenuItem[]){
                { "${menu.copy}", 0, 0, "copy" },
                { "${menu.search}", 0, 0,
                  format_CStr("search newtab:1 query:%s",
                              cstr_String(selectedText_DocumentWidget_(d))) },
                { "${menu.snippet.add}", 0, 0,
                  format_CStr("!snippet.add content:%s",
                              cstr_String(selectedText_DocumentWidget_(d))) },
                { "---", 0, 0, NULL } },
            4);
    }
#if (defined (iPlatformApple) && defined (LAGRANGE_ENABLE_MAC_MENUS)) || defined (iPlatformTerminal)
    pushBackN_Array(
        items,
        (iMenuItem[]){
            { backArrow_Icon " ${menu.back}", navigateBack_KeyShortcut, "navigate.back" },
            { forwardArrow_Icon " ${menu.forward}", navigateForward_KeyShortcut, "navigate.forward" },
            { upArrow_Icon " ${menu.parent}", navigateParent_KeyShortcut, "navigate.parent" },
            { upArrowBar_Icon " ${menu.root}", navigateRoot_KeyShortcut, "navigate.root" }
        }, 4);
#else
    /* Compact navigation actions. */
    pushBackN_Array(
        items,
        (iMenuItem[]){
            { ">>>" backArrow_Icon, navigateBack_KeyShortcut, "navigate.back" },
            { ">>>" forwardArrow_Icon, navigateForward_KeyShortcut, "navigate.forward" },
            { ">>>" upArrow_Icon, navigateParent_KeyShortcut, "navigate.parent" },
            { ">>>" upArrowBar_Icon, navigateRoot_KeyShortcut, "navigate.root" },
        }, 4);
#endif
    pushBackN_Array(
        items,
        (iMenuItem[]){
            { "---" },
            { reload_Icon " ${menu.reload}", reload_KeyShortcut, "navigate.reload" },
            { "---" },
            { bookmark_Icon " ${menu.page.bookmark}", bookmarkPage_KeyShortcut, "bookmark.add" },
            { star_Icon " ${menu.page.subscribe}", subscribeToPage_KeyShortcut, "feeds.subscribe" },
            { "---" },
            { d->flags & viewSource_DocumentWidgetFlag ? "${menu.viewformat.gemini}"
                                                       : "${menu.viewformat.plain}",
              0, 0, "document.viewformat" },
            { "${menu.sitespec}", 0, 0, "document.sitespec" },
            { hammer_Icon " ${menu.tools}", 0, 0, "submenu id:toolsmenu" },
            { "---" },
            { "${menu.page.copyurl}", 0, 0, "document.copylink" }, },
        11);
    if (isEmpty_Range(&d->selectMark)) {
        pushBackN_Array(
            items,
            (iMenuItem[]){
                { "${menu.page.copysource}", 'c', KMOD_PRIMARY, "copy" },
                { "---" },
                { download_Icon " " saveToDownloads_Label, SDLK_S, KMOD_PRIMARY, "document.save" } },
            3);
#if defined (LAGRANGE_FILE_DIALOGS)
        pushBack_Array(
            items, &(iMenuItem){ "${menu.save.as}", 0, 0, "document.save saveas:1" });
#endif
        pushBack_Array(
            items,
            &(iMenuItem){ books_Icon " ${menu.save.books}", SDLK_S, KMOD_PRIMARY | SDL_KMOD_SHIFT, "document.savebook" });
    }
    iWidget       *menu     = makeMenu_Widget(as_Widget(d), data_Array(items), size_Array(items));
    const iRangecc scheme   = urlScheme_String(d->mod.url);
    const iBool    noUpload = !equalCase_Rangecc(scheme, "gemini") && !equalCase_Rangecc(scheme, "titan");
    setMenuItemDisabled_Widget(menu, "document.upload", noUpload);
    setMenuItemDisabled_Widget(menu, "document.upload copy:1", noUpload);
    updateToolsMenu_DocumentWidget(d);
    return menu;
}

iWidget *makeSelectionMenu_DocumentWidget(iDocumentWidget *d) {
    const iMenuItem items[] = {
        { clipCopy_Icon " ${menu.copy}", 0, 0, "copy" },
        { "---" },
        { magnifyingGlass_Icon " ${menu.search}", 0, 0,
          format_CStr("search newtab:1 query:%s", cstr_String(selectedText_DocumentWidget_(d))) },
        { add_Icon " ${menu.snippet.add}", 0, 0,
          format_CStr("!snippet.add content:%s", cstr_String(selectedText_DocumentWidget_(d))) },
#if defined (iPlatformAppleMobile)
        { export_Icon " ${menu.share}", 0, 0, "copy share:1" },
#endif
        { "---" },
        { close_Icon " ${menu.select.clear}", 0, 0, "document.select arg:0" },
    };
    iWidget *menu = makeMenu_Widget(as_Widget(d), items, iElemCount(items));
    setFlags_Widget(menu, noFadeBackground_WidgetFlag, iTrue);
    return menu;
}

iWidget *makeLinkContextMenu_SimpleDocumentWidget(iSimpleDocumentWidget *d, const iGmRun *link) {
    /* Only the actions that make sense for the pages of a book. */
    const iGmDocument *doc   = document_SimpleDocumentWidget(d);
    const iString     *url   = linkUrl_GmDocument(doc, link->linkId);
    iArray            *items = collectNew_Array(sizeof(iMenuItem));
    if (isNativeScheme_(urlScheme_String(url))) {
        appendOpenItems_(items, url, "");
        pushBack_Array(items, &(iMenuItem){ "---" });
    }
    iString *encLabel = newRange_String(linkLabel_GmDocument(doc, link->linkId));
    urlEncodeSpaces_String(encLabel);
    pushBackN_Array(
        items,
        (iMenuItem[]){
            { copy_Icon " ${link.copy}", 0, 0, "simpledoc.copylink" },
            { "${link.copy.label}", 0, 0, "simpledoc.copylink label:1" },
            { "---" },
            { bookmark_Icon " ${link.bookmark}", 0, 0,
              format_CStr("!bookmark.add title:%s url:%s", cstr_String(encLabel), cstr_String(url)) },
        },
        4);
    delete_String(encLabel);
    return makeMenu_Widget(as_Widget(d), data_Array(items), size_Array(items));
}
