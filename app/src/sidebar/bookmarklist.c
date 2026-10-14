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
#include "bookmarks.h"
#include "feeds.h"
#include "gmutil.h"
#include "ui/bookmarkdialogs.h"
#include "ui/command.h"
#include "ui/inputwidget.h"
#include "ui/labelwidget.h"
#include "ui/mobile.h"
#include "ui/root.h"
#include "ui/util.h"

#include <lagrange/lang.h>

static iBool isBookmarkFolded_SidebarWidget_(const iSidebarWidget *d, const iBookmark *bm) {
    while (bm->parentId) {
        if (!contains_IntSet(d->openFolders, bm->parentId)) {
            return iTrue;
        }
        bm = get_Bookmarks(bookmarks_App(), bm->parentId);
    }
    return iFalse;
}

static const iMenuItem bookmarkModeMenuItems_[] = {
    { bookmark_Icon " ${menu.page.bookmark}", SDLK_D, KMOD_PRIMARY, "bookmark.add" },
    { "---" },
    { folder_Icon " ${menu.newfolder}", 0, 0, "bookmark.addfolder" },
    { upDownArrow_Icon " ${menu.sort.alpha}", 0, 0, "bookmark.sortfolder" },
    { "---" },
    { rightAngle_Icon " ${menu.foldall}", 0, 0, "bookmark.foldall arg:1" },
    { downAngle_Icon " ${menu.unfoldall}", 0, 0, "bookmark.foldall arg:0" },
    { reload_Icon " ${bookmarks.reload}", 0, 0, "bookmarks.reload.remote" },
    { "---" },
    { trash_Icon " " uiTextCaution_ColorEscape "${menu.trash.empty}", 0, 0,
      "bookmarks.trash.empty" },
};

static void setupFromBookmark_SidebarItem_(iSidebarItem *d, const iBookmark *bm) {
    d->id     = id_Bookmark(bm);
    d->indent = depth_Bookmark(bm);
    d->icon   = bm->icon;
    set_String(&d->url, &bm->url);
    set_String(&d->label, &bm->title);
    /* Icons for special behaviors. */
    if (bm->flags & subscribed_BookmarkFlag) {
        appendChar_String(&d->meta, 0x2605);
    }
    if (bm->flags & homepage_BookmarkFlag) {
        appendChar_String(&d->meta, 0x1f3e0);
    }
    if (bm->flags & remoteSource_BookmarkFlag) {
        appendChar_String(&d->meta, 0x2913);
        d->isBold = iTrue;
    }
    if (bm->flags & linkSplit_BookmarkFlag) {
        appendChar_String(&d->meta, 0x25e7);
    }
    if (!isEmpty_String(&bm->identity)) {
        appendCStr_String(&d->meta, person_Icon);
    }
}

static const iMenuItem bookmarkMenuItems_[] = {
    { openTab_Icon " ${menu.opentab}", 0, 0, "bookmark.open newtab:1" },
    { openTabBg_Icon " ${menu.opentab.background}", 0, 0, "bookmark.open newtab:2" },
    { openWindow_Icon " ${menu.openwindow}", 0, KMOD_DESKTOP, "bookmark.open newwindow:1" },
    { "---", 0, 0, NULL },
    { edit_Icon " ${menu.edit}", 0, 0, "bookmark.edit" },
    { copy_Icon " ${menu.dup}", 0, 0, "bookmark.dup" },
    { "${menu.copyurl}", 0, 0, "sideitem.copy canon:1" },
    { "---", 0, 0, NULL },
    { "", 0, 0, "bookmark.tag tag:subscribed" },
    { "", 0, 0, "bookmark.tag tag:homepage" },
    { "", 0, 0, "bookmark.tag tag:remotesource" },
    { "---", 0, 0, NULL },
    { uiTextCaution_ColorEscape "${bookmark.delete}", SDLK_BACKSPACE, KMOD_DESKTOP, "bookmark.delete" },
    { delete_Icon " " uiTextCaution_ColorEscape "${bookmark.delete}", 0, KMOD_MOBILE, "bookmark.delete" },
    { "---", 0, 0, NULL },
    { folder_Icon " ${menu.newfolder}", 0, 0, "bookmark.addfolder" },
    { upDownArrow_Icon " ${menu.sort.alpha}", 0, 0, "bookmark.sortfolder" },
    { "---", 0, 0, NULL },
    { reload_Icon " ${bookmarks.reload}", 0, 0, "bookmarks.reload.remote" }
};

static iBool filterTrash_(void *d, const iBookmark *bm) {
    iUnused(d);
    return isTrash_Bookmark(bm);
}

iBool isShowingTrash_SidebarWidget(const iSidebarWidget *d) {
    return isSelected_Widget(findChild_Widget(constAs_Widget(d), "filter.bookmarks.trash"));
}

static void updateBookmarkItems_SidebarWidget_(iSidebarWidget *d) {
    iConstForEach(PtrArray,
                  i,
                  list_Bookmarks(bookmarks_App(),
                                 cmpTree_Bookmark,
                                 isShowingTrash_SidebarWidget(d) ? filterTrash_ : NULL,
                                 NULL)) {
        const iBookmark *bm = i.ptr;
        if (isBookmarkFolded_SidebarWidget_(d, bm)) {
            continue; /* inside a closed folder */
        }
        iSidebarItem *item = new_SidebarItem();
        setupFromBookmark_SidebarItem_(item, bm);
        item->listItem.flags.isDraggable = iTrue;
        item->isBold = item->listItem.flags.isDropTarget = isFolder_Bookmark(bm);
        if (isFolder_Bookmark(bm)) {
            item->icon = contains_IntSet(d->openFolders, item->id) ? 0xfe40 : 0x27e9;
        }
        if (bm->flags & remote_BookmarkFlag) {
            item->listItem.flags.isDraggable = iFalse;
        }
        addItem_ListWidget(d->list, item);
        iRelease(item);
    }
    d->menu = makeMenu_Widget(as_Widget(d), bookmarkMenuItems_, iElemCount(bookmarkMenuItems_));
    /* Menu for a bookmark folder. */ {
        iArray *items = new_Array(sizeof(iMenuItem));
        pushBackN_Array(
            items,
            (iMenuItem[]) {
                { openTab_Icon " ${menu.folder.opentab}", 0, 0, "bookmark.open newtab:1" },
#if !defined (iPlatformTerminal)
                { openWindow_Icon " ${menu.openwindow}", 0, KMOD_DESKTOP, "bookmark.open newwindow:1" },
#endif
                { "---" },
                { edit_Icon " ${menu.edit}", 0, 0, "bookmark.edit" },
                { "---" },
                { uiTextCaution_ColorEscape "${bookmark.folder.delete}",
                  SDLK_BACKSPACE, KMOD_DESKTOP,
                  "bookmark.delete" },
                { delete_Icon " " uiTextCaution_ColorEscape "${bookmark.delete}",
                  0, KMOD_MOBILE,
                  "bookmark.delete" },
                { "---" } },
            7);
        pushBackN_Array(items, bookmarkModeMenuItems_, iElemCount(bookmarkModeMenuItems_));
        d->folderMenu = makeMenu_Widget(as_Widget(d), constData_Array(items), size_Array(items));
        delete_Array(items);
    }
    /* Menu for trashed bookmarks. */ {
        static const iMenuItem trashMenuItems[] = {
            { "${menu.trash.restore}", 0, 0, "bookmarks.trash.restore" },
            { "---" },
            { openTab_Icon " ${menu.folder.opentab}", 0, 0, "bookmark.open newtab:1" },
#if !defined (iPlatformTerminal)
            { openWindow_Icon " ${menu.openwindow}", 0, KMOD_DESKTOP, "bookmark.open newwindow:1" },
#endif
            { "---" },
            { edit_Icon " ${menu.edit}", 0, 0, "bookmark.edit" },
            { "---" },
            { trash_Icon " " uiTextCaution_ColorEscape "${menu.trash.empty}", 0, 0, "bookmarks.trash.empty" },
        };
        d->trashMenu = makeMenu_Widget(as_Widget(d), trashMenuItems, iElemCount(trashMenuItems));
    }
}

iDeclareType(BookmarkFilterContext)

struct Impl_BookmarkFilterContext {
    const iString *term;
    iBool          showTrash;
};

static iBool filterBookmark_String_(void *context, const iBookmark *bm) {
    const iBookmarkFilterContext *d    = context;
    const iString                *term = d->term;
    size_t                        pos;
    if (isFolder_Bookmark(bm)) return iFalse;
    if (d->showTrash ^ isTrash_Bookmark(bm)) {
        return iFalse;
    }
    if (startsWith_String(term, ".")) {
        /* Starting with period means a special tag. */
        const uint32_t flag = fromSpecialTag_BookmarkFlag(cstr_String(term));
        if (flag) {
            return (bm->flags & flag) != 0;
        }
    }
    if (indexOfCStrSc_String(&bm->title, cstr_String(term), &iCaseInsensitive) != iInvalidPos) {
        return iTrue;
    }
    if (indexOfCStrSc_String(&bm->url, cstr_String(term), &iCaseInsensitive) != iInvalidPos) {
        return iTrue;
    }
    /* Check each tag separately. */
    iRangecc tag = iNullRange;
    while (nextSplit_Rangecc(range_String(&bm->tags), " ", &tag)) {
        if (lastIndexOfCStr_Rangecc(tag, cstr_String(term)) != iInvalidPos) {
            return iTrue;
        }
    }
    return iFalse;
}

static void updateFilteredBookmarkItems_SidebarWidget_(iSidebarWidget *d) {
    const iWidget *w    = constAs_Widget(d);
    iString       *term = lower_String(&d->bookmarkFilter);
    iBookmarkFilterContext ctx  = {
        .term      = term,
        .showTrash = isShowingTrash_SidebarWidget(d),
    };
    iConstForEach(
        PtrArray,
        i,
        list_Bookmarks(bookmarks_App(), cmpTitleAscending_Bookmark, filterBookmark_String_, &ctx)) {
        const iBookmark *bm   = i.ptr;
        iSidebarItem    *item = new_SidebarItem();
        setupFromBookmark_SidebarItem_(item, bm);
        item->indent = 0;
        addItem_ListWidget(d->list, item);
        iRelease(item);
    }
    delete_String(term);
    d->menu = makeMenu_Widget(as_Widget(d),
                              bookmarkMenuItems_,
                              iElemCount(bookmarkMenuItems_) -
                                  5 /* only items related to the individual bookmark */);
}

iBool updateBookmarks_SidebarWidget_(iSidebarWidget *d, iBool keepActions) {
    const iBool isMobile = (deviceType_App() != desktop_AppDeviceType);
    iAssert(get_Root() == d->widget.root);
    trim_String(&d->bookmarkFilter);
    if (!isEmpty_String(&d->bookmarkFilter)) {
        updateFilteredBookmarkItems_SidebarWidget_(d);
    }
    else {
        updateBookmarkItems_SidebarWidget_(d);
    }
    const iBool isEmpty = isEmpty_String(&d->bookmarkFilter) && isEmpty_ListWidget(d->list);
    d->modeMenu = makeMenu_Widget(
        as_Widget(d), bookmarkModeMenuItems_, iElemCount(bookmarkModeMenuItems_));
    if (keepActions) {
        return !isEmpty;
    }
    if (!isMobile) {
        /* Filter/search field. */
        /* TODO: Where to put this on mobile? */
        iInputWidget *filter = makeFilterBar_Widget("filter.bookmark.input",
                                                    "${hint.filter.bookmark}",
                                                    SDLK_ESCAPE,
                                                    "filter.bookmark.clear");
        setBackgroundColor_Widget(as_Widget(filter), uiBackgroundSidebar_ColorId);
        setText_InputWidget(filter, &d->bookmarkFilter);
        updateFilterBar_Widget(filter);
        iWidget *div = new_Widget();
        addChildFlags_Widget(div, iClob(filter), 0);
        /* Button for showing everything in the trash. */
        iLabelWidget *trashBtn = new_LabelWidget(
            trash_Icon " ${sidebar.action.bookmarks.showtrash}", "bookmarks.trash.toggle");
        checkIcon_LabelWidget(trashBtn);
        setIconColor_LabelWidget(trashBtn, uiText_ColorId);
        setFont_LabelWidget(trashBtn, uiContent_FontId);
        setId_Widget(addChildFlags_Widget(div, iClob(trashBtn), alignLeft_WidgetFlag),
                     "filter.bookmarks.trash");
        addChildFlags_Widget(d->actions,
                             iClob(div),
                             expand_WidgetFlag | arrangeVertical_WidgetFlag |
                                 arrangeHeight_WidgetFlag | resizeWidthOfChildren_WidgetFlag |
                                 unhittable_WidgetFlag);
    }
    else {
        /* On mobile, we need to show buttons for edit actions. */
        addActionButton_SidebarWidget_(d,
                                       "${sidebar.action.bookmarks.newfolder}",
                                       "bookmarks.addfolder",
                                       collapse_WidgetFlag |
                                           (!d->isEditing ? hidden_WidgetFlag : 0));
        iWidget *trashBtn = (iWidget *) addActionButton_SidebarWidget_(
            d, trash_Icon " ${sidebar.action.bookmarks.showtrash}", "bookmarks.trash.toggle", 0);
        setFixedSize_Widget(
            as_Widget(trashBtn),
            init_I2(measure_Text(fontWithStyle_Text(font_LabelWidget((iLabelWidget *) trashBtn),
                                                    bold_FontStyle),
                                 cstr_Lang("sidebar.action.bookmarks.showtrash"))
                            .advance.x +
                        textOffset_LabelWidget((iLabelWidget *) trashBtn) + 6 * gap_UI,
                    -1));
        setFlags2_Widget(trashBtn, alwaysRound_WidgetFlag2, iTrue);
        setId_Widget(trashBtn, "filter.bookmarks.trash");
        addChildFlags_Widget(d->actions, iClob(new_Widget()), expand_WidgetFlag);
        setId_Widget((iWidget *) addActionButton_SidebarWidget_(
            d,
            d->isEditing ? "${sidebar.close}" : "${sidebar.action.bookmarks.edit}",
            format_CStr("%s.bookmarks.edit", cstr_String(&d->widget.id)),
            0), "mobile.bookmarks.edit");
    }
    return !isEmpty;
}

static iBool isFolder_(void *context, const iBookmark *bm) {
    iUnused(context);
    return isFolder_Bookmark(bm);
}

static uint32_t bookmarkEditorId_(const iWidget *editor) {
    iAssert(startsWith_String(id_Widget(editor), "bmed."));
    uint32_t bmId = strtoul(cstr_String(id_Widget(editor)) + 5, NULL, 10);
    iAssert(bmId != 0);
    return bmId;
}

static iBool handleBookmarkEditorCommands_SidebarWidget_(iWidget *editor, const char *cmd) {
    if (equal_Command(cmd, "dlg.bookmark.setfolder")) {
        setBookmarkEditorParentFolder_Widget(editor, arg_Command(cmd));
        return iTrue;
    }
    else if (equal_Command(cmd, "widget.resized")) {
        updateBookmarkEditorFieldWidths_Widget(editor);
        return iTrue;
    }
    else if (equal_Command(cmd, "bmed.dup")) {
        const iString *title = text_InputWidget(findChild_Widget(editor, "bmed.title"));
        const iString *url   = text_InputWidget(findChild_Widget(editor, "bmed.url"));
        const iString *icon =
            collect_String(trimmed_String(text_InputWidget(findChild_Widget(editor, "bmed.icon"))));
        makeBookmarkCreation_Widget(url, title, isEmpty_String(icon) ? 0 : first_String(icon));
        setupSheetTransition_Mobile(editor, dialogTransitionDir_Widget(editor));
        destroy_Widget(editor);
        return iTrue;
    }
    else if (equal_Command(cmd, "bmed.setident")) {
        const uint32_t bmId = bookmarkEditorId_(editor);
        iBookmark     *bm   = get_Bookmarks(bookmarks_App(), bmId);
        if (bm) {
            set_String(&bm->identity, string_Command(cmd, "fp"));
            updateDropdownSelection_LabelWidget(findChild_Widget(editor, "bmed.setident"),
                                                format_CStr(" fp:%s", cstr_String(&bm->identity)));
        }
        return iTrue;
    }
    else if (equal_Command(cmd, "bmed.accept") || equal_Command(cmd, "bmed.cancel")) {
        const uint32_t bmId = bookmarkEditorId_(editor);
        if (equal_Command(cmd, "bmed.accept")) {
            const iString *title = text_InputWidget(findChild_Widget(editor, "bmed.title"));
            const iString *url   = text_InputWidget(findChild_Widget(editor, "bmed.url"));
            const iString *tags  = text_InputWidget(findChild_Widget(editor, "bmed.tags"));
            const iString *notes = text_InputWidget(findChild_Widget(editor, "bmed.notes"));
            const iString *icon  = collect_String(
                trimmed_String(text_InputWidget(findChild_Widget(editor, "bmed.icon"))));
            iBookmark *bm = get_Bookmarks(bookmarks_App(), bmId);
            set_String(&bm->title, title);
            if (!isFolder_Bookmark(bm)) {
                set_String(&bm->url, canonicalUrl_String(url));
                set_String(&bm->tags, tags);
                set_String(&bm->notes, notes);
                if (isEmpty_String(icon)) {
                    bm->flags &= ~userIcon_BookmarkFlag;
                    bm->icon = 0;
                }
                else {
                    bm->flags |= userIcon_BookmarkFlag;
                    bm->icon = first_String(icon);
                }
                iChangeFlags(bm->flags,
                             homepage_BookmarkFlag,
                             isSelected_Widget(findChild_Widget(editor, "bmed.tag.home")));
                iChangeFlags(bm->flags,
                             remoteSource_BookmarkFlag,
                             isSelected_Widget(findChild_Widget(editor, "bmed.tag.remote")));
                iChangeFlags(bm->flags,
                             linkSplit_BookmarkFlag,
                             isSelected_Widget(findChild_Widget(editor, "bmed.tag.linksplit")));
            }
            const iBookmark *folder = userData_Object(findChild_Widget(editor, "bmed.folder"));
            if (!folder || !hasParent_Bookmark(folder, id_Bookmark(bm))) {
                bm->parentId = folder ? id_Bookmark(folder) : 0;
            }
            notify_App("bookmarks.changed");
        }
        setupSheetTransition_Mobile(editor, dialogTransitionDir_Widget(editor));
        destroy_Widget(editor);
        return iTrue;
    }
    return iFalse;
}

void bookmarkMoved_SidebarWidget_(iSidebarWidget *d, size_t index, size_t dstIndex,
                                         iBool isBefore) {
    const iSidebarItem *movingItem = item_ListWidget(d->list, index);
    const iBool         isLast     = (dstIndex == numItems_ListWidget(d->list));
    const iSidebarItem *dstItem =
        item_ListWidget(d->list, isLast ? numItems_ListWidget(d->list) - 1 : dstIndex);
    if (isLast && isBefore) isBefore = iFalse;
    const iBookmark *dst = get_Bookmarks(bookmarks_App(), dstItem->id);
    if (hasParent_Bookmark(dst, movingItem->id) || dst->flags & remote_BookmarkFlag) {
        /* Can't move a folder inside itself, and remote bookmarks cannot be reordered. */
        return;
    }
    reorder_Bookmarks(bookmarks_App(), movingItem->id, dst->order + (isBefore ? 0 : 1));
    get_Bookmarks(bookmarks_App(), movingItem->id)->parentId = dst->parentId;
    updateItems_SidebarWidget_(d);
    /* Don't confuse the user: keep the dragged item in hover state. */
    setHoverItem_ListWidget(d->list, dstIndex + (isBefore ? 0 : 1) + (index < dstIndex ? -1 : 0));
    notifyf_App("bookmarks.changed nosidebar:%p",
                     d); /* skip this sidebar since we updated already */
}

void bookmarkMovedOntoFolder_SidebarWidget_(iSidebarWidget *d, size_t index,
                                                   size_t folderIndex) {
    const iSidebarItem *movingItem = item_ListWidget(d->list, index);
    const iSidebarItem *dstItem    = item_ListWidget(d->list, folderIndex);
    iBookmark          *bm         = get_Bookmarks(bookmarks_App(), movingItem->id);
    bm->parentId                   = dstItem->id;
    notify_App("bookmarks.changed");
}

static size_t numBookmarks_(const iPtrArray *bmList) {
    size_t num = 0;
    iConstForEach(PtrArray, i, bmList) {
        const iBookmark *bm = i.ptr;
        if (!isFolder_Bookmark(bm) && ~bm->flags & remote_BookmarkFlag) {
            num++;
        }
    }
    return num;
}

void updateBookmarkContextMenu_SidebarWidget_(iSidebarWidget *d) {
    const iBookmark *bm = get_Bookmarks(bookmarks_App(), d->contextItem->id);
    if (bm) {
        setMenuItemLabel_Widget(d->menu,
                                "bookmark.tag tag:homepage",
                                bm->flags & homepage_BookmarkFlag
                                    ? home_Icon " ${bookmark.untag.home}"
                                    : home_Icon " ${bookmark.tag.home}",
                                0);
        setMenuItemLabel_Widget(d->menu,
                                "bookmark.tag tag:subscribed",
                                bm->flags & subscribed_BookmarkFlag
                                    ? star_Icon " ${bookmark.untag.sub}"
                                    : star_Icon " ${bookmark.tag.sub}",
                                0);
        setMenuItemLabel_Widget(d->menu,
                                "bookmark.tag tag:remotesource",
                                bm->flags & remoteSource_BookmarkFlag
                                    ? downArrowBar_Icon " ${bookmark.untag.remote}"
                                    : downArrowBar_Icon " ${bookmark.tag.remote}",
                                0);
    }
}

iWidget *bookmarkContextMenu_SidebarWidget_(iSidebarWidget *d, const iSidebarItem *hoverItem) {
    if (!hoverItem) {
        return NULL;
    }
    const iBookmark *bm = get_Bookmarks(bookmarks_App(), hoverItem->id);
    if (!bm) {
        return NULL;
    }
    if (isTrash_Bookmark(bm)) {
        return d->trashMenu;
    }
    if (isFolder_Bookmark(bm)) {
        return d->folderMenu;
    }
    if (!isVisible_Widget(d->menu)) {
        const iBool        isRemote        = (bm->flags & remote_BookmarkFlag) != 0;
        static const char *localOnlyCmds[] = { "bookmark.edit",
                                               "bookmark.delete",
                                               "bookmark.tag tag:subscribed",
                                               "bookmark.tag tag:homepage",
                                               "bookmark.tag tag:remotesource" };
        iForIndices(i, localOnlyCmds) {
            setFlags_Widget(
                findMenuItem_Widget(d->menu, localOnlyCmds[i]),
                disabled_WidgetFlag, /* Remote bookmarks have limitations. */
                isRemote);
        }
    }
    return d->menu;
}

static void updateAfterTrashAction_SidebarWidget_(iSidebarWidget *d) {
    updateItemsWithFlags_SidebarWidget_(d, iTrue); /* must keep trash open */
    notifyf_App("bookmarks.changed nosidebar:%p", d);
}

iBool handleBookmarkCommand_SidebarWidget_(iSidebarWidget *d, const SDL_Event *ev, const char *cmd) {
    /* TODO: Most of these are conditional on `bookmarks_SidebarMode`. Could check for it
       once up here... */
    iWidget *w = as_Widget(d);
    if (d->mode == bookmarks_SidebarMode &&
        equalArg_Command(cmd, "input.edited", "id", "filter.bookmark.input") &&
        hasParent_Widget(constAs_Widget(pointer_Command(cmd)), w)) {
        set_String(&d->bookmarkFilter, text_InputWidget(pointer_Command(cmd)));
        updateFilterBar_Widget(pointer_Command(cmd));
        updateItemsWithFlags_SidebarWidget_(d, iTrue);
        return iTrue;
    }
    else if (d->mode == bookmarks_SidebarMode &&
             equalWidget_Command(cmd, w, "filter.bookmark.clear")) {
        iInputWidget *filter = findChild_Widget(w, "filter.bookmark.input");
        setTextCStr_InputWidget(filter, "");
        updateFilterBar_Widget(filter);
        clear_String(&d->bookmarkFilter);
        updateItemsWithFlags_SidebarWidget_(d, iTrue);
        setFocus_Widget(as_Widget(filter));
        return iTrue;
    }
    else if (d->mode == bookmarks_SidebarMode && isCommand_Widget(w, ev, "list.delete")) {
        d->contextItem = item_ListWidget(d->list, arg_Command(cmd));
        postCommand_Widget(w, "bookmark.delete");
        return iTrue;
    }
    else if (d->mode == bookmarks_SidebarMode &&
             isCommand_Widget(w, ev, "bookmarks.trash.restore")) {
        const iSidebarItem *item = d->contextItem;
        if (item) {
            iBookmark *bm = get_Bookmarks(bookmarks_App(), item->id);
            if (bm && isTrash_Bookmark(bm)) {
                trash_Bookmarks(bookmarks_App(), id_Bookmark(bm), iFalse);
                updateAfterTrashAction_SidebarWidget_(d);
            }
        }
        return iTrue;
    }
    else if (equal_Command(cmd, "bookmarks.trash.empty")) {
        iBool wasRemoved = iFalse;
        iConstForEach(PtrArray, i, list_Bookmarks(bookmarks_App(), NULL, filterTrash_, NULL)) {
            const iBookmark *bm = i.ptr;
            wasRemoved |= remove_Bookmarks(bookmarks_App(), id_Bookmark(bm));
        }
        if (wasRemoved) {
            updateAfterTrashAction_SidebarWidget_(d);
        }
        return iTrue;
    }
    else if (d->mode == bookmarks_SidebarMode &&
             isCommand_Widget(w, ev, "bookmarks.trash.toggle")) {
        iLabelWidget *toggle  = findChild_Widget(w, "filter.bookmarks.trash");
        const iBool showTrash = !isSelected_Widget(toggle);
        setFlags_Widget(toggle, selected_WidgetFlag, showTrash);
        updateFontStyle_LabelWidget(toggle, showTrash ? bold_FontStyle : regular_FontStyle);
        updateItemsWithFlags_SidebarWidget_(d, iTrue);
        return iTrue;
    }
    else if (isCommand_Widget(w, ev, "bookmark.open")) {
        const iSidebarItem *item = d->contextItem;
        if (d->mode == bookmarks_SidebarMode && item) {
            iBookmark *bm = get_Bookmarks(bookmarks_App(), item->id);
            if (bm) {
                if (isFolder_Bookmark(bm)) {
                    iRoot *openingInRoot = get_Root();
                    iBool  isNewWindow   = iFalse;
                    if (argLabel_Command(cmd, "newwindow")) {
                        /* First open a new window. */
                        iMainWindow *newWin = newMainWindow_App();
                        openingInRoot       = newWin->base.roots[0];
                        isNewWindow         = iTrue;
                    }
                    iBool isFirst = iTrue;
                    iConstForEach(PtrArray,
                                  i,
                                  list_Bookmarks(bookmarks_App(),
                                                 cmpTree_Bookmark,
                                                 filterInsideFolder_Bookmark,
                                                 bm)) {
                        const iBookmark *contained = i.ptr;
                        if (!isFolder_Bookmark(contained)) {
                            /* When opening multiple bookmarks at once, ensure that previous
                               ones are idle before continuing. */
                            postCommandf_Root(
                                openingInRoot,
                                "open idle:%d newtab:%d%s url:%s",
                                !isFirst,
                                (isNewWindow && isFirst ? 0
                                 : isFirst              ? new_OpenTabFlag
                                                        : newBackground_OpenTabFlag),
                                !isEmpty_String(&contained->identity)
                                    ? format_CStr(" setident:%s",
                                                  cstr_String(&contained->identity))
                                    : "",
                                cstr_String(&contained->url));
                            isFirst = iFalse;
                        }
                    }
                    postCommandf_Root(openingInRoot, "window.unfreeze");
                }
                else {
                    postCommandf_App(
                        "open newtab:%d newwindow:%d%s url:%s",
                        argLabel_Command(cmd, "newtab"),
                        argLabel_Command(cmd, "newwindow"),
                        isEmpty_String(&bm->identity)
                            ? format_CStr(" setident:%s", cstr_String(&bm->identity))
                            : "",
                        cstr_String(&item->url));
                }
            }
        }
        return iTrue;
    }
    else if (isCommand_Widget(w, ev, "bookmark.edit")) {
        const iSidebarItem *item  = d->contextItem;
        const int           argId = argLabel_Command(cmd, "id");
        if (((d->mode == bookmarks_SidebarMode || d->mode == subscriptions_SidebarMode) &&
             item) ||
            argId) {
            iBookmark  *bm    = get_Bookmarks(bookmarks_App(), argId ? argId : item->id);
            const char *dlgId = format_CStr("bmed.%u", id_Bookmark(bm));
            if (findWidget_Root(dlgId)) {
                return iTrue;
            }
            if (isBlockedBySync_BookmarkDialogs()) {
                return iTrue;
            }
            iWidget *dlg =
                makeBookmarkEditor_Widget(isFolder_Bookmark(bm) ? id_Bookmark(bm) : 0, iTrue);
            setId_Widget(dlg, dlgId);
            setText_InputWidget(findChild_Widget(dlg, "bmed.title"), &bm->title);
            if (!isFolder_Bookmark(bm)) {
                iInputWidget *urlInput        = findChild_Widget(dlg, "bmed.url");
                iInputWidget *tagsInput       = findChild_Widget(dlg, "bmed.tags");
                iInputWidget *notesInput      = findChild_Widget(dlg, "bmed.notes");
                iInputWidget *iconInput       = findChild_Widget(dlg, "bmed.icon");
                iWidget      *homeTag         = findChild_Widget(dlg, "bmed.tag.home");
                iWidget      *remoteSourceTag = findChild_Widget(dlg, "bmed.tag.remote");
                iWidget      *linkSplitTag    = findChild_Widget(dlg, "bmed.tag.linksplit");
                setText_InputWidget(urlInput, &bm->url);
                setText_InputWidget(tagsInput, &bm->tags);
                setText_InputWidget(notesInput, &bm->notes);
                if (bm->flags & userIcon_BookmarkFlag) {
                    setText_InputWidget(iconInput,
                                        collect_String(newUnicodeN_String(&bm->icon, 1)));
                }
                setToggle_Widget(homeTag, bm->flags & homepage_BookmarkFlag);
                setToggle_Widget(remoteSourceTag, bm->flags & remoteSource_BookmarkFlag);
                setToggle_Widget(linkSplitTag, bm->flags & linkSplit_BookmarkFlag);
                updateDropdownSelection_LabelWidget(
                    findChild_Widget(dlg, "bmed.setident"),
                    format_CStr(" fp:%s", cstr_String(&bm->identity)));
            }
            setBookmarkEditorParentFolder_Widget(dlg, bm ? bm->parentId : 0);
            setCommandHandler_Widget(dlg, handleBookmarkEditorCommands_SidebarWidget_);
            setResizeId_Widget(dlg, "bmed");
            restoreWidth_Widget(dlg);
            postCommand_Root(dlg->root, "focus.set id:bmed.title");
        }
        return iTrue;
    }
    else if (isCommand_Widget(w, ev, "bookmark.dup")) {
        const iSidebarItem *item = d->contextItem;
        if (d->mode == bookmarks_SidebarMode && item) {
            iBookmark  *bm       = get_Bookmarks(bookmarks_App(), item->id);
            const iBool isRemote = (bm->flags & remote_BookmarkFlag) != 0;
            iChar       icon     = isRemote ? 0x1f588 : bm->icon;
            if (isBlockedBySync_BookmarkDialogs()) {
                return iTrue;
            }
            iWidget    *dlg      = makeBookmarkCreation_Widget(&bm->url, &bm->title, icon);
            setId_Widget(dlg, format_CStr("bmed.%s", cstr_String(id_Widget(w))));
            if (!isRemote) {
                setText_InputWidget(findChild_Widget(dlg, "bmed.tags"), &bm->tags);
            }
            setFocus_Widget(findChild_Widget(dlg, "bmed.title"));
        }
        return iTrue;
    }
    else if (isCommand_Widget(w, ev, "bookmark.tag")) {
        const iSidebarItem *item = d->contextItem;
        if (d->mode == bookmarks_SidebarMode && item) {
            const iRangecc tag = range_Command(cmd, "tag");
            const int      flag =
                (equal_Rangecc(tag, "homepage") ? homepage_BookmarkFlag : 0) |
                (equal_Rangecc(tag, "subscribed") ? subscribed_BookmarkFlag : 0) |
                (equal_Rangecc(tag, "remotesource") ? remoteSource_BookmarkFlag : 0);
            iBookmark *bm = get_Bookmarks(bookmarks_App(), item->id);
            if (flag == subscribed_BookmarkFlag && (bm->flags & flag)) {
                removeEntries_Feeds(item->id); /* get rid of unsubscribed entries */
            }
            bm->flags ^= flag;
            notify_App("bookmarks.changed");
        }
        return iTrue;
    }
    else if (isCommand_Widget(w, ev, "bookmark.delete")) {
        const iSidebarItem *item = d->contextItem;
        if (d->mode == bookmarks_SidebarMode && item) {
            iBookmark *bm = get_Bookmarks(bookmarks_App(), item->id);
            if (bm && isFolder_Bookmark(bm)) {
                const iPtrArray *list =
                    list_Bookmarks(bookmarks_App(), NULL, filterInsideFolder_Bookmark, bm);
                /* Folder trashing requires confirmation because folders can contain
                   any number of bookmarks and other folders. Trashing removes contents
                   from folders. */
                if (argLabel_Command(cmd, "confirmed") || isEmpty_PtrArray(list)) {
                    iConstForEach(PtrArray, i, list) {
                        removeEntries_Feeds(id_Bookmark(i.ptr));
                    }
                    trash_Bookmarks(bookmarks_App(), item->id, iTrue);
                    notify_App("bookmarks.changed");
                }
                else {
                    setFocus_Widget(NULL);
                    const size_t numBookmarks = numBookmarks_(list);
                    makeQuestion_Widget(
                        uiHeading_ColorEscape "${heading.confirm.bookmarks.delete}",
                        formatCStrs_Lang("dlg.confirm.bookmarks.delete.n", numBookmarks),
                        (iMenuItem[]) {
                            { "${cancel}" },
                            { format_CStr(
                                  uiTextCaution_ColorEscape "%s",
                                  formatCStrs_Lang("dlg.bookmarks.delete.n", numBookmarks)),
                              0,
                              0,
                              format_CStr("!bookmark.delete confirmed:1 ptr:%p", d) },
                        },
                        2);
                }
            }
            else {
                if (trash_Bookmarks(bookmarks_App(), item->id, iTrue)) {
                    removeEntries_Feeds(item->id); /* unsubscribe from trashed feed */
                    notify_App("bookmarks.changed");
                }
            }
        }
        return iTrue;
    }
    else if (isCommand_Widget(w, ev, "bookmark.addfolder")) {
        const iSidebarItem *item = d->contextItem;
        if (d->mode == bookmarks_SidebarMode) {
            postCommandf_App("bookmarks.addfolder parent:%zu",
                             !item ? 0
                             : item->listItem.flags.isDropTarget
                                 ? item->id
                                 : get_Bookmarks(bookmarks_App(), item->id)->parentId);
        }
        return iTrue;
    }
    else if (isCommand_Widget(w, ev, "bookmark.sortfolder")) {
        const iSidebarItem *item = d->contextItem;
        if (d->mode == bookmarks_SidebarMode && item) {
            postCommandf_App("bookmarks.sort arg:%zu",
                             item->listItem.flags.isDropTarget
                                 ? item->id
                                 : get_Bookmarks(bookmarks_App(), item->id)->parentId);
        }
        return iTrue;
    }
    else if (isCommand_Widget(w, ev, "bookmark.foldall")) {
        clear_IntSet(d->openFolders);
        if (!arg_Command(cmd)) {
            iConstForEach(PtrArray, i, list_Bookmarks(bookmarks_App(), NULL, isFolder_, NULL)) {
                insert_IntSet(d->openFolders, id_Bookmark(i.ptr));
            }
        }
        if (d->mode == bookmarks_SidebarMode) {
            updateItems_SidebarWidget_(d);
        }
        return iTrue;
    }
    return iFalse;
}
