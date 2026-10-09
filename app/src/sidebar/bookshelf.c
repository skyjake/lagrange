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

#include "app.h"
#include "books.h"
#include "gempub.h"
#include "gmutil.h"
#include "ui/command.h"
#include "ui/documentwidget.h"
#include "ui/filedialog.h"
#include "ui/labelwidget.h"
#include "ui/root.h"
#include "ui/simpledocumentwidget.h"
#include "ui/util.h"

#include <lagrange/lang.h>
#include <the_Foundation/file.h>
#include <the_Foundation/path.h>
#include <SDL3/SDL_clipboard.h>
#include <errno.h>

#if defined (iPlatformAppleMobile)
#   include "platform/ios.h"
#endif
#if defined (iPlatformAndroidMobile)
#   include "platform/android.h"
#endif

iBool updateGempub_SidebarWidget_(iSidebarWidget *d) {
    /* Shows the cover or the index of the current book. Returns False if there is none. */
    const iGempub *gempub = gempub_SidebarWidget_(d);
    if (!gempub) {
        clear_String(&d->gempubUrl);
        clear_SimpleDocumentWidget(d->gempubDoc);
        return iFalse;
    }
    const iString *url = d->gempubPage == 0 ? coverPageUrl_Gempub(gempub)
                                            : indexPageUrl_Gempub(gempub);
    const iBool wasShown = equal_String(&d->gempubUrl, url) && isVisible_Widget(d->gempubDoc);
    if (!equal_String(&d->gempubUrl, url)) {
        set_String(&d->gempubUrl, url);
        clear_String(&d->gempubCentered);
        iString *src = d->gempubPage == 0 ? coverPageSource_Gempub(gempub)
                                          : indexPageSource_Gempub(gempub);
        setSource_SimpleDocumentWidget(d->gempubDoc, url, src);
        delete_String(src);
        if (d->gempubPage == 0 &&
            preloadCoverImage_Gempub(gempub, document_SimpleDocumentWidget(d->gempubDoc))) {
            redoLayout_SimpleDocumentWidget(d->gempubDoc);
        }
    }
    if (d->gempubPage == 1) {
        /* The index follows the current page. */
        const iString *docUrl = url_DocumentWidget(document_Root(as_Widget(d)->root));
        if (!equal_String(&d->gempubCentered, docUrl)) {
            set_String(&d->gempubCentered, docUrl);
            scrollToLink_SimpleDocumentWidget(d->gempubDoc, docUrl, wasShown);
        }
    }
    return iTrue;
}

static iBool copyFile_SidebarWidget_(const iString *srcPath, const iString *dstPath) {
    iBool  ok  = iFalse;
    iFile *src = iClob(new_File(srcPath));
    if (open_File(src, readOnly_FileMode)) {
        const iBlock *data = collect_Block(readAll_File(src));
        iFile        *dst  = iClob(new_File(dstPath));
        if (open_File(dst, writeOnly_FileMode)) {
            ok = write_File(dst, data) == size_Block(data);
        }
    }
    if (!ok) {
        makeSimpleMessage_Widget(uiTextCaution_ColorEscape "${heading.save.error}",
                                 strerror(errno));
    }
    return ok;
}

iBool isShowingShelf_SidebarWidget_(const iSidebarWidget *d) {
    return d->mode == gempub_SidebarMode && (d->gempubPage == 2 || !gempub_SidebarWidget_(d));
}

iBool updateShelf_SidebarWidget_(iSidebarWidget *d, iBool keepActions) {
    /* Lists the books on the shelf. Returns False if there are none. */
    postCommand_App("sync.recent"); /* reading positions may have changed elsewhere */
    const iGempub *current   = gempub_SidebarWidget_(d);
    const iString *currentId = current ? id_Books(current) : NULL;
    const iPtrArray *books = list_Books(d->shelfSort);
    if (d->isShelfOrderValid && size_StringArray(d->shelfOrder) == size_PtrArray(books)) {
        /* Items keep their places so a clicked book doesn't jump elsewhere. */
        iPtrArray *ordered = collectNew_PtrArray();
        iConstForEach(StringArray, s, d->shelfOrder) {
            iConstForEach(PtrArray, b, books) {
                if (equal_String(&((const iBook *) b.ptr)->id, s.value)) {
                    pushBack_PtrArray(ordered, b.ptr);
                    break;
                }
            }
        }
        if (size_PtrArray(ordered) == size_PtrArray(books)) {
            books = ordered;
        }
    }
    clear_StringArray(d->shelfOrder);
    iConstForEach(PtrArray, b, books) {
        pushBack_StringArray(d->shelfOrder, &((const iBook *) b.ptr)->id);
    }
    /* Sorted again when the shelf is shown the next time. */
    d->isShelfOrderValid = isVisible_Widget(as_Widget(d));
    iConstForEach(PtrArray, i, books) {
        const iBook   *book = i.ptr;
        const iString *path = path_Book(book);
        iSidebarItem  *item = new_SidebarItem();
        set_String(&item->label, &book->title);
        if (isEmpty_String(&item->label)) {
            set_String(&item->label, &book->fileName);
        }
        set_String(&item->meta, &book->author);
        set_String(&item->url,
                   path ? canonicalUrl_String(collect_String(makeFileUrl_String(path)))
                        : &book->url);
        set_String(&item->bookId, &book->id);
        item->progress = book->chapter >= 0 ? progress_Book(book) : -1.0f;
        item->size     = path ? book->size : 0;
        item->icon     = path ? 0x1f56e /* book */ : 0x2ba7 /* download */;
        item->isBold   = currentId && equal_String(currentId, &book->id);
        item->listItem.flags.isSelected = item->isBold;
        addItem_ListWidget(d->list, item);
        iRelease(item);
    }
    const iMenuItem menuItems[] = {
        { openTab_Icon " ${menu.opentab}", 0, 0, "book.open newtab:1" },
        { openTabBg_Icon " ${menu.opentab.background}", 0, 0, "book.open newtab:2" },
        { openWindow_Icon " ${menu.openwindow}", 0, KMOD_DESKTOP, "book.open newwindow:1" },
        { "---" },
        { "${book.copysource}", 0, 0, "book.copysource" },
#if defined (iPlatformMobile)
        { download_Icon " ${menu.save.files}", 0, 0, "book.saveas" },
#elif defined (LAGRANGE_FILE_DIALOGS)
        { download_Icon " ${menu.save.as}", 0, 0, "book.saveas" },
#endif
#if defined (LAGRANGE_HAVE_FILE_REVEAL)
        { magnifyingGlass_Icon " " revealFile_Label, 0, 0, "book.reveal" },
#endif
        { "---" },
        { "${book.redownload}", 0, 0, "book.redownload" },
        { "${book.removedownload}", 0, 0, "book.removedownload" },
        { delete_Icon " " uiTextCaution_ColorEscape "${book.delete}", 0, 0, "book.delete" },
    };
    d->menu = makeMenu_Widget(as_Widget(d), menuItems, iElemCount(menuItems));
    /* Actions. */
    if (deviceType_App() == desktop_AppDeviceType) {
        if (!keepActions && !isEmpty_PtrArray(books)) {
            addChildFlags_Widget(d->actions, iClob(new_Widget()), expand_WidgetFlag);
            updateSize_LabelWidget(
                addChildFlags_Widget(d->actions,
                                     iClob(new_LabelWidget("${sidebar.action.sort}", NULL)),
                                     frameless_WidgetFlag | tight_WidgetFlag));
            const iMenuItem items[] = {
                { "${sidebar.action.books.recent}", 0, 0, "books.sort arg:0" },
                { "${sidebar.action.books.title}", 0, 0, "books.sort arg:1" },
                { "${sidebar.action.books.author}", 0, 0, "books.sort arg:2" },
            };
            iWidget *dropButton = addChild_Widget(
                d->actions,
                iClob(makeMenuButton_LabelWidget(items[d->shelfSort].label, items, 3)));
            setId_Widget(dropButton, "books.sortbutton");
            checkIcon_LabelWidget((iLabelWidget *) dropButton);
            setFixedSize_Widget(
                dropButton,
                init_I2(iMaxi(20 * gap_UI,
                              measure_Text(default_FontId,
                                           translateCStr_Lang(
                                               items[findWidestLabel_MenuItem(items, 3)].label))
                                      .advance.x +
                                  13 * gap_UI),
                        -1));
            addChildFlags_Widget(d->actions, iClob(new_Widget()), expand_WidgetFlag);
        }
        else if (keepActions) {
            updateDropdownSelection_LabelWidget(findChild_Widget(d->actions, "books.sortbutton"),
                                                format_CStr(" arg:%d", d->shelfSort));
        }
    }
    else {
        if (!keepActions && !isEmpty_PtrArray(books)) {
            const iMenuItem items[] = {
                { upDownArrow_Icon " ${sidebar.action.books.recent}", 0, 0, "books.sort arg:0" },
                { upDownArrow_Icon " ${sidebar.action.books.title}", 0, 0, "books.sort arg:1" },
                { upDownArrow_Icon " ${sidebar.action.books.author}", 0, 0, "books.sort arg:2" },
            };
            iLabelWidget *dropButton =
                makeMenuButton_LabelWidget(items[d->shelfSort].label, items, 3);
            addChildFlags_Widget(d->actions, iClob(dropButton), frameless_WidgetFlag);
            setId_Widget(as_Widget(dropButton), "books.sortbutton");
            setFont_LabelWidget(dropButton, actionButtonFont_SidebarWidget_(d));
            setTextColor_LabelWidget(dropButton, uiTextAction_ColorId);
            setBackgroundColor_Widget(as_Widget(dropButton), uiBackground_ColorId);
            checkIcon_LabelWidget(dropButton);
        }
        else if (keepActions) {
            updateDropdownSelection_LabelWidget(findChild_Widget(d->actions, "books.sortbutton"),
                                                format_CStr(" arg:%d", d->shelfSort));
        }
    }
    return !isEmpty_PtrArray(books);
}

void updateShelfContextMenu_SidebarWidget_(iSidebarWidget *d) {
    const iBook *book = find_Books(&d->contextItem->bookId);
    setMenuItemDisabled_Widget(d->menu,
                               "book.copysource",
                               !book || isEmpty_String(&book->url));
    setMenuItemDisabled_Widget(d->menu,
                               "book.redownload",
                               !book || !path_Book(book) ||
                                   !isDownloadable_Book(book));
    /* Without a source URL, the book would disappear from the shelf. */
    setMenuItemDisabled_Widget(d->menu,
                               "book.removedownload",
                               !book || !path_Book(book) ||
                                   !isDownloadable_Book(book));
    setMenuItemDisabled_Widget(d->menu,
                               "book.reveal",
                               !book || !path_Book(book));
    setMenuItemDisabled_Widget(d->menu,
                               "book.saveas",
                               !book || !path_Book(book));
}

iBool handleShelfCommand_SidebarWidget_(iSidebarWidget *d, const SDL_Event *ev, const char *cmd) {
    iWidget *w = as_Widget(d);
    if (equal_Command(cmd, "books.removedownload")) {
        if (d->side == bookSide_SidebarWidget_()) {
            removeDownload_Books(collect_String(suffix_Command(cmd, "id")));
            postCommand_App("books.changed");
        }
    }
    else if (equal_Command(cmd, "books.saveas") && pointer_Command(cmd) == d) {
        const iBook   *book = find_Books(string_Command(cmd, "id"));
        const iString *path = book ? path_Book(book) : NULL;
        if (path) {
            copyFile_SidebarWidget_(path, collect_String(suffix_Command(cmd, "path")));
        }
    }
    else if (equal_Command(cmd, "books.delete")) {
        if (d->side == bookSide_SidebarWidget_()) {
            remove_Books(collect_String(suffix_Command(cmd, "id")));
            postCommand_App("books.changed");
        }
    }
    else if (equal_Command(cmd, "books.changed")) {
        if (isShowingShelf_SidebarWidget_(d)) {
            updateItems_SidebarWidget_(d);
        }
    }
    else if (equalWidget_Command(cmd, w, "books.sort")) {
        d->shelfSort = iClamp(arg_Command(cmd), 0, author_BooksSortOrder);
        d->isShelfOrderValid = iFalse;
        if (isShowingShelf_SidebarWidget_(d)) {
            updateItemsWithFlags_SidebarWidget_(d, iTrue);
        }
        return iTrue;
    }
    else if (isCommand_Widget(w, ev, "book.open")) {
        const iSidebarItem *item = d->contextItem;
        if (item && !isEmpty_String(&item->url)) {
            postCommand_Widget(d,
                               "!open newtab:%d newwindow:%d url:%s",
                               argLabel_Command(cmd, "newtab"),
                               argLabel_Command(cmd, "newwindow"),
                               cstr_String(&item->url));
        }
        return iTrue;
    }
    else if (isCommand_Widget(w, ev, "book.copysource")) {
        const iBook *book = d->contextItem ? find_Books(&d->contextItem->bookId) : NULL;
        if (book && !isEmpty_String(&book->url)) {
            SDL_SetClipboardText(cstr_String(&book->url));
        }
        return iTrue;
    }
    else if (isCommand_Widget(w, ev, "book.redownload")) {
        const iBook *book = d->contextItem ? find_Books(&d->contextItem->bookId) : NULL;
        if (book && isDownloadable_Book(book)) {
            d->gempubPage = 2; /* stays on the shelf */
            postCommandf_Root(get_Root(), "open overwritebook:1 url:%s", cstr_String(&book->url));
            dismissPortraitPhoneSidebars_Root(as_Widget(d)->root);
        }
        return iTrue;
    }
    else if (isCommand_Widget(w, ev, "book.reveal")) {
        const iSidebarItem *item = d->contextItem;
        const iString *path = item ? collect_String(localFilePathFromUrl_String(&item->url)) : NULL;
        if (path) {
            postCommandf_App("reveal path:%s", cstr_String(path));
        }
        return iTrue;
    }
    else if (isCommand_Widget(w, ev, "book.saveas")) {
        const iBook   *book = d->contextItem ? find_Books(&d->contextItem->bookId) : NULL;
        const iString *path = book ? path_Book(book) : NULL;
        if (path) {
#if defined (iPlatformAppleMobile)
            /* The exported file is removed afterwards. */
            const iString *tmpPath = temporaryPathForUrl_App(
                collect_String(makeFileUrl_String(path)), collectNewCStr_String(mimeType_Gempub));
            if (copyFile_SidebarWidget_(path, tmpPath)) {
                exportDownloadedFile_iOS(tmpPath);
            }
#elif defined (iPlatformAndroidMobile)
            exportDownloadedFile_Android(path, collectNewCStr_String(mimeType_Gempub));
#else
            save_FileDialog(format_CStr("books.saveas ptr:%p id:%s", d, cstr_String(&book->id)),
                            "${menu.save.as}",
                            collect_String(concat_Path(downloadDir_App(), &book->fileName)));
#endif
        }
        return iTrue;
    }
    else if (isCommand_Widget(w, ev, "book.removedownload")) {
        const iSidebarItem *item = d->contextItem;
        if (item && !isEmpty_String(&item->bookId)) {
            makeQuestion_Widget(
                uiTextCaution_ColorEscape "${heading.book.removedownload}",
                format_Lang("${dlg.confirm.book.removedownload}", cstr_String(&item->label)),
                (iMenuItem[]){ { "${cancel}", 0, 0, NULL },
                               { uiTextCaution_ColorEscape "${dlg.book.removedownload}",
                                 0,
                                 0,
                                 format_CStr("!books.removedownload id:%s",
                                             cstr_String(&item->bookId)) } },
                2);
        }
        return iTrue;
    }
    else if (isCommand_Widget(w, ev, "book.delete")) {
        const iSidebarItem *item = d->contextItem;
        if (item && !isEmpty_String(&item->url)) {
            makeQuestion_Widget(
                uiTextCaution_ColorEscape "${heading.book.delete}",
                format_Lang("${dlg.confirm.book.delete}", cstr_String(&item->label)),
                (iMenuItem[]){ { "${cancel}", 0, 0, NULL },
                               { uiTextCaution_ColorEscape "${dlg.book.delete}",
                                 0,
                                 0,
                                 format_CStr("!books.delete id:%s",
                                             cstr_String(&item->bookId)) } },
                2);
        }
        return iTrue;
    }
    return iFalse;
}
