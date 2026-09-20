/* Copyright 2020 Jaakko Keränen <jaakko.keranen@iki.fi>

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

#include "bookmarkdialogs.h"

#include "app.h"
#include "bookmarks.h"
#include "command.h"
#include "defs.h"
#include "dialog.h"
#include "documentwidget.h"
#include "feeds.h"
#include "gmutil.h"
#include "inputwidget.h"
#include "labelwidget.h"
#include "render/text.h"
#include "root.h"
#include "util.h"
#include "widget.h"

static iBool isBookmarkFolder_(void *context, const iBookmark *bm) {
    iUnused(context);
    return isFolder_Bookmark(bm);
}

const iArray *makeBookmarkFolderActions_MenuItem(const char *command, iBool withNullTerminator,
                                                 uint32_t omitFolderId) {
    iArray *folders = new_Array(sizeof(iMenuItem));
    pushBack_Array(folders, &(iMenuItem){ "\u2014", 0, 0, format_CStr("%s arg:0", command) });
    iConstForEach(PtrArray, i, list_Bookmarks(bookmarks_App(), cmpTree_Bookmark,
                                              isBookmarkFolder_, NULL)) {
        const iBookmark *bm = i.ptr;
        if (id_Bookmark(bm) == omitFolderId || hasParent_Bookmark(bm, omitFolderId)) {
            continue;
        }
        iString *title = collect_String(copy_String(&bm->title));
        for (const iBookmark *j = bm; j && j->parentId; ) {
            j = get_Bookmarks(bookmarks_App(), j->parentId);
            prependCStr_String(title, " > ");
            prepend_String(title, &j->title);
        }
        pushBack_Array(
            folders,
            &(iMenuItem){ cstr_String(title),
                          0,
                          0,
                          format_CStr("%s arg:%u", command, id_Bookmark(bm)) });
    }
    if (withNullTerminator) {
        pushBack_Array(folders, &(iMenuItem){ NULL });
    }
    return collect_Array(folders);
}

void updateBookmarkEditorFieldWidths_Widget(iWidget *d) {
    static const char *ids[] = {
        "bmed.title", "bmed.url", "bmed.setident", "bmed.tags", "bmed.notes"
    };
    iWidget *headings = findChild_Widget(d, "bmed.columns.head");
    const int newWidth = width_Widget(d) - width_Widget(headings) - 6 * gap_UI;
    iForIndices(i, ids) {
        iWidget *widget = findChild_Widget(d, ids[i]);
        if (widget) {
            widget->rect.size.x = newWidth;
        }
    }
}

iWidget *makeBookmarkEditor_Widget(uint32_t folderId, iBool withDup) {
    const iBool isFolder = (folderId != 0);
    if (isFolder) {
        withDup = iFalse;
    }
    const iMenuItem dupActions[] = {
        { "${menu.dup}", 0, 0, "bmed.dup" },
        { "---" },
        { "${cancel}", SDLK_ESCAPE, 0, "bmed.cancel" },
        { uiTextAction_ColorEscape "${dlg.bookmark.save}", SDLK_RETURN, KMOD_ACCEPT, "bmed.accept" }
    };
    const iMenuItem actions[] = { dupActions[2], dupActions[3] };
    /* List of identities, if changing identity is necessary for opening the bookmark. */
    iArray *identItems = collectNew_Array(sizeof(iMenuItem)); {
        pushBack_Array(identItems, &(iMenuItem){ "\u2014", 0, 0, "bmed.setident fp:" });
        pushBack_Array(identItems, &(iMenuItem){ "---" });
        appendIdentities_MenuItem(identItems, "bmed.setident", NULL);
        pushBack_Array(identItems, &(iMenuItem){ NULL });
    }
    iWidget *dlg = NULL;
    if (isUsingPanelLayout_Mobile()) {
        const iArray *parentFolderItems =
            makeBookmarkFolderActions_MenuItem("dlg.bookmark.setfolder", iTrue, folderId);
        const iMenuItem folderItems[] = {
            { "title id:bmed.heading text:${heading.bookmark.editfolder}" },
            { "input id:bmed.title text:${dlg.bookmark.title}" },
            { "dropdown id:bmed.folder text:${dlg.bookmark.parentfolder}", 0, 0,
                  (const void *) constData_Array(parentFolderItems) },
            { "padding" },
            { NULL }
        };
        const iMenuItem items[] = {
            { "title id:bmed.heading text:${heading.bookmark.edit}" },
            { "input id:bmed.title noheading:1 dlg.bookmark.title" },
            { "input id:bmed.url url:1 noheading:1 hint:dlg.bookmark.url" },
            { "padding" },
            { "dropdown id:bmed.folder text:"
              uiTextAction_ColorEscape folder_Icon uiTextStrong_ColorEscape
              " ${dlg.bookmark.folder}", 0, 0,
              (const void *) constData_Array(parentFolderItems) },
            { "padding" },
            { "dropdown id:bmed.setident text:${LC:dlg.bookmark.identity}", 0, 0,
              (const void *) constData_Array(identItems) },
            { "padding" },
            { "input id:bmed.tags hint:hint.dlg.bookmark.tags text:${dlg.bookmark.tags}" },
            { "input id:bmed.notes hint:hint.dlg.bookmark.notes text:${dlg.bookmark.notes}" },
            { "input id:bmed.icon maxlen:1 text:${dlg.bookmark.icon}" },
            { "heading text:${heading.bookmark.tags}" },
            { "toggle id:bmed.tag.home text:${LC:bookmark.tag.home}" },
            { "toggle id:bmed.tag.remote text:${LC:bookmark.tag.remote}" },
            { "toggle device:0 id:bmed.tag.linksplit text:${LC:bookmark.tag.linksplit}" },
            { "toggle device:1 id:bmed.tag.linksplit text:${LC:bookmark.tag.linksplit}" },
            { "padding" },
            { NULL }
        };
        dlg = makePanels_Mobile("bmed",
                                isFolder ? folderItems : items,
                                withDup ? dupActions : actions,
                                withDup ? iElemCount(dupActions) : iElemCount(actions));
        setupSheetTransition_Mobile(dlg, incoming_TransitionFlag | dialogTransitionDir_Widget(dlg));
    }
    else {
        dlg = makeSheet_Widget("bmed");
        addDialogTitle_Widget(dlg,
                        isFolder ? "${heading.bookmark.editfolder}" : "${heading.bookmark.edit}",
                        "bmed.heading");
        iWidget *headings, *values;
        addChild_Widget(dlg, iClob(makeTwoColumns_Widget(&headings, &values)));
        setId_Widget(headings, "bmed.columns.head");
        iInputWidget *inputs[5];
        iZap(inputs);
        /* Folder to add to. */ {
            addChild_Widget(headings,
                            iClob(makeHeading_Widget(isFolder ? "${dlg.bookmark.parentfolder}"
                                                              : "${dlg.bookmark.folder}")));
            const iArray *folderItems =
                makeBookmarkFolderActions_MenuItem("!dlg.bookmark.setfolder", iFalse, folderId);
            iLabelWidget *folderButton;
            setId_Widget(addChildFlags_Widget(values,
                                         iClob(folderButton = makeMenuButton_LabelWidget(
                                                   widestLabel_MenuItemArray(folderItems),
                                                   constData_Array(folderItems),
                                                   size_Array(folderItems))), alignLeft_WidgetFlag),
                         "bmed.folder");
        }
        addDialogInputWithHeading_Widget(headings, values, "${dlg.bookmark.title}", "bmed.title", iClob(inputs[0] = new_InputWidget(0)));
        if (!isFolder) {
            addDialogInputWithHeading_Widget(headings, values, "${dlg.bookmark.url}",   "bmed.url",   iClob(inputs[1] = new_InputWidget(0)));
            setUrlContent_InputWidget(inputs[1], iTrue);
            addDialogPadding_Widget(headings, values);
            makeIdentityDropdown_LabelWidget(
                headings, values, identItems, "${dlg.bookmark.identity}", "bmed.setident");
            addDialogInputWithHeading_Widget(headings, values, "${dlg.bookmark.tags}",  "bmed.tags",  iClob(inputs[2] = new_InputWidget(0)));
            addDialogInputWithHeading_Widget(headings, values, "${dlg.bookmark.notes}",  "bmed.notes",  iClob(inputs[3] = new_InputWidget(0)));
            setHint_InputWidget(inputs[2], "${hint.dlg.bookmark.tags}");
            setHint_InputWidget(inputs[3], "${hint.dlg.bookmark.notes}");
            addDialogInputWithHeading_Widget(headings, values, "${dlg.bookmark.icon}",  "bmed.icon",  iClob(inputs[4] = new_InputWidget(1)));
            /* Buttons for special tags. */
//            addChild_Widget(dlg, iClob(makePadding_Widget(gap_UI)));
            iWidget *special = addChild_Widget(dlg, iClob(makeTwoColumns_Widget(&headings, &values)));
            setFlags_Widget(special, collapse_WidgetFlag, iTrue);
            setId_Widget(special, "bmed.special");
            makeTwoColumnHeading_Widget("${heading.bookmark.tags}", headings, values);
            addDialogToggle_Widget(headings, values, "${LC:bookmark.tag.home}:", "bmed.tag.home");
            addDialogToggle_Widget(headings, values, "${LC:bookmark.tag.remote}:", "bmed.tag.remote");
            addDialogToggle_Widget(headings, values, "${bookmark.tag.linksplit}:", "bmed.tag.linksplit");
        }
        arrange_Widget(dlg);
        const int inputWidth = iMin(100 * gap_UI, width_Rect(rect_Root(dlg->root))) - headings->rect.size.x;
        for (int i = 0; i < 4; ++i) {
            if (inputs[i]) {
                as_Widget(inputs[i])->rect.size.x = inputWidth;
            }
        }
        iWidget *setIdent = findChild_Widget(dlg, "bmed.setident");
        if (setIdent) {
            setIdent->rect.size.x = inputWidth;
        }
        addChild_Widget(dlg, iClob(makePadding_Widget(gap_UI)));
        addChild_Widget(dlg,
                        iClob(makeDialogButtons_Widget(withDup ? dupActions : actions,
                                                       withDup ? iElemCount(dupActions)
                                                               : iElemCount(actions))));
        addChild_Widget(get_Root()->widget, iClob(dlg));
        setupSheetTransition_Mobile(dlg, incoming_TransitionFlag | top_TransitionDir);
        if (isDesktop_Platform()) {
            arrange_Widget(dlg);
            enableResizing_Widget(dlg, width_Widget(dlg), NULL);
        }
    }
    /* Use a recently accessed folder as the default. */
    const uint32_t recentFolderId = recentFolder_Bookmarks(bookmarks_App());
    iLabelWidget *folderDrop = findChild_Widget(dlg, "bmed.folder");
    updateDropdownSelection_LabelWidget(folderDrop, format_CStr(" arg:%u", recentFolderId));
    setUserData_Object(folderDrop, get_Bookmarks(bookmarks_App(), recentFolderId));
    updateDropdownSelection_LabelWidget(findChild_Widget(dlg, "bmed.setident"), "bmed.setident fp:");
    return dlg;
}

void setBookmarkEditorParentFolder_Widget(iWidget *editor, uint32_t folderId) {
    iLabelWidget *button = findChild_Widget(editor, "bmed.folder");
    updateDropdownSelection_LabelWidget(button, format_CStr(" arg:%u", folderId));
    setUserData_Object(button, get_Bookmarks(bookmarks_App(), folderId));
}

static iBool handleBookmarkCreationCommands_SidebarWidget_(iWidget *editor, const char *cmd) {
    if (equal_Command(cmd, "dlg.bookmark.setfolder")) {
        setBookmarkEditorParentFolder_Widget(editor, arg_Command(cmd));
        return iTrue;
    }
    else if (equal_Command(cmd, "widget.resized")) {
        updateBookmarkEditorFieldWidths_Widget(editor);
        return iTrue;
    }
    else if (equalWidget_Command(cmd, editor, "bmed.setident")) {
        const iString *fp = string_Command(cmd, "fp");
        iLabelWidget *setident = findChild_Widget(editor, "bmed.setident");
        set_String(&as_Widget(setident)->data, fp);
        updateDropdownSelection_LabelWidget(setident, format_CStr(" fp:%s", cstr_String(fp)));
        return iTrue;
    }
    if (equal_Command(cmd, "bmed.accept") || equal_Command(cmd, "bmed.cancel")) {
        if (equal_Command(cmd, "bmed.accept")) {
            const iString *title = text_InputWidget(findChild_Widget(editor, "bmed.title"));
            const iString *url   = text_InputWidget(findChild_Widget(editor, "bmed.url"));
            const iString *tags  = text_InputWidget(findChild_Widget(editor, "bmed.tags"));
            const iString *notes = text_InputWidget(findChild_Widget(editor, "bmed.notes"));
            const iString *ident = &as_Widget(findChild_Widget(editor, "bmed.setident"))->data;
            const iBookmark *folder = userData_Object(findChild_Widget(editor, "bmed.folder"));
            const iString *icon  = collect_String(trimmed_String(text_InputWidget(findChild_Widget(editor, "bmed.icon"))));
            const uint32_t id    = add_Bookmarks(bookmarks_App(), url, title, tags, first_String(icon));
            iBookmark *    bm    = get_Bookmarks(bookmarks_App(), id);
            set_String(&bm->notes, notes);
            set_String(&bm->identity, ident);
            if (!isEmpty_String(icon)) {
                bm->flags |= userIcon_BookmarkFlag;
            }
            if (isSelected_Widget(findChild_Widget(editor, "bmed.tag.home"))) {
                bm->flags |= homepage_BookmarkFlag;
            }
            if (isSelected_Widget(findChild_Widget(editor, "bmed.tag.remote"))) {
                bm->flags |= remoteSource_BookmarkFlag;
            }
            if (isSelected_Widget(findChild_Widget(editor, "bmed.tag.linksplit"))) {
                bm->flags |= linkSplit_BookmarkFlag;
            }
            bm->parentId = folder ? id_Bookmark(folder) : 0;
            setRecentFolder_Bookmarks(bookmarks_App(), bm->parentId);
            notifyf_App("bookmarks.changed added:%zu", id);
        }
        setupSheetTransition_Mobile(editor, dialogTransitionDir_Widget(editor));
        destroy_Widget(editor);
        return iTrue;
    }
    return iFalse;
}

iWidget *makeBookmarkCreation_Widget(const iString *url, const iString *title, iChar icon) {
    iWidget *dlg = makeBookmarkEditor_Widget(0, iFalse);
    setId_Widget(dlg, "bmed.create");
    setTextCStr_LabelWidget(findChild_Widget(dlg, "bmed.heading"),
                            uiHeading_ColorEscape "${heading.bookmark.add}");
    iUrl parts;
    init_Url(&parts, url);
    setTextCStr_InputWidget(findChild_Widget(dlg, "bmed.title"),
                            title ? cstr_String(title) : cstr_Rangecc(parts.host));
    setText_InputWidget(findChild_Widget(dlg, "bmed.url"), url);
    if (icon) {
        setText_InputWidget(findChild_Widget(dlg, "bmed.icon"),
                            collect_String(newUnicodeN_String(&icon, 1)));
    }
    setCommandHandler_Widget(dlg, handleBookmarkCreationCommands_SidebarWidget_);
    setResizeId_Widget(dlg, "bmed");
    restoreWidth_Widget(dlg);
    return dlg;
}

static iBool handleFeedSettingCommands_(iWidget *dlg, const char *cmd) {
    if (equal_Command(cmd, "cancel")) {
        setupSheetTransition_Mobile(dlg, dialogTransitionDir_Widget(dlg));
        destroy_Widget(dlg);
        return iTrue;
    }
    if (equal_Command(cmd, "feedcfg.accept")) {
        iString *feedTitle =
            collect_String(copy_String(text_InputWidget(findChild_Widget(dlg, "feedcfg.title"))));
        trim_String(feedTitle);
        if (isEmpty_String(feedTitle)) {
            return iTrue;
        }
        int id = argLabel_Command(cmd, "bmid");
        const iBool headings = isSelected_Widget(findChild_Widget(dlg, "feedcfg.type.headings"));
        const iBool ignoreWeb = isSelected_Widget(findChild_Widget(dlg, "feedcfg.ignoreweb"));
        if (!id) {
            const size_t numSubs = numSubscribed_Feeds();
            const iString *url   = url_DocumentWidget(document_App());
            id = add_Bookmarks(bookmarks_App(),
                               url,
                               feedTitle,
                               NULL,
                               siteIcon_GmDocument(document_DocumentWidget(document_App())));
            if (numSubs == 0) {
                /* Auto-refresh after first addition. */
                /* TODO: Also when settings changed? */
                postCommand_App("feeds.refresh");
            }
        }
        iBookmark *bm = get_Bookmarks(bookmarks_App(), id);
        iAssert(bm);
        set_String(&bm->title, feedTitle);
        bm->flags |= subscribed_BookmarkFlag;
        iChangeFlags(bm->flags, headings_BookmarkFlag, headings);
        iChangeFlags(bm->flags, ignoreWeb_BookmarkFlag, ignoreWeb);
        notify_App("bookmarks.changed");
        setupSheetTransition_Mobile(dlg, dialogTransitionDir_Widget(dlg));
        destroy_Widget(dlg);
        return iTrue;
    }
    return iFalse;
}

iWidget *makeFeedSettings_Widget(uint32_t bookmarkId) {
    iWidget        *dlg;
    const char     *headingText = bookmarkId ? "${heading.feedcfg}" : "${heading.subscribe}";
    const iMenuItem actions[]   = { { "${cancel}" },
                                    { bookmarkId ? uiTextAction_ColorEscape "${dlg.feed.save}"
                                                 : uiTextAction_ColorEscape "${dlg.feed.sub}",
                                      SDLK_RETURN,
                                      KMOD_ACCEPT,
                                      format_CStr("feedcfg.accept bmid:%d", bookmarkId) } };
    if (isUsingPanelLayout_Mobile()) {
        const iMenuItem typeItems[] = {
            { "button id:feedcfg.type.gemini label:dlg.feed.type.gemini", 0, 0, "feedcfg.type arg:0" },
            { "button id:feedcfg.type.headings label:dlg.feed.type.headings", 0, 0, "feedcfg.type arg:1" },
            { NULL }
        };
        dlg = makePanels_Mobile("feedcfg", (iMenuItem[]){
            { format_CStr("title id:feedcfg.heading text:%s", headingText) },
            { "input id:feedcfg.title text:${dlg.feed.title}" },
            { "radio id:dlg.feed.entrytype", 0, 0, (const void *) typeItems },
            { "padding" },
            { "toggle id:feedcfg.ignoreweb text:${dlg.feed.ignoreweb}" },
            { "padding" },
            { NULL }
        }, actions, iElemCount(actions));
    }
    else {
        dlg = makeSheet_Widget("feedcfg");
        addDialogTitle_Widget(dlg, headingText, "feedcfg.heading");
        iWidget *headings, *values;
        addChild_Widget(dlg, iClob(makeTwoColumns_Widget(&headings, &values)));
        iInputWidget *input = new_InputWidget(0);
        addDialogInputWithHeading_Widget(headings, values, "${dlg.feed.title}", "feedcfg.title", iClob(input));
        addChild_Widget(headings, iClob(makeHeading_Widget("${dlg.feed.entrytype}")));
        iWidget *types = new_Widget(); {
            addRadioButton_Widget(types, "feedcfg.type.gemini", "${dlg.feed.type.gemini}", "feedcfg.type arg:0");
            addRadioButton_Widget(types, "feedcfg.type.headings", "${dlg.feed.type.headings}", "feedcfg.type arg:1");
        }
        addChildFlags_Widget(values, iClob(types), arrangeHorizontal_WidgetFlag | arrangeSize_WidgetFlag);
        addChild_Widget(headings, iClob(makeHeading_Widget("${dlg.feed.ignoreweb}")));
        addChild_Widget(values, iClob(makeToggle_Widget("feedcfg.ignoreweb")));
        iWidget *buttons =
            addChild_Widget(dlg, iClob(makeDialogButtons_Widget(actions, iElemCount(actions))));
        setId_Widget(child_Widget(buttons, childCount_Widget(buttons) - 1), "feedcfg.save");
        arrange_Widget(dlg);
        as_Widget(input)->rect.size.x = 100 * gap_UI - headings->rect.size.x;
        addChild_Widget(get_Root()->widget, iClob(dlg));
    }
    /* Initialize. */ {
        const iBookmark *bm  = bookmarkId ? get_Bookmarks(bookmarks_App(), bookmarkId) : NULL;
        setText_InputWidget(findChild_Widget(dlg, "feedcfg.title"),
                            bm ? &bm->title : feedTitle_DocumentWidget(document_App()));
        setFlags_Widget(findChild_Widget(dlg,
                                         bm && bm->flags & headings_BookmarkFlag
                                             ? "feedcfg.type.headings"
                                             : "feedcfg.type.gemini"),
                        selected_WidgetFlag,
                        iTrue);
        setToggle_Widget(findChild_Widget(dlg, "feedcfg.ignoreweb"),
                         bm && bm->flags & ignoreWeb_BookmarkFlag);
        setCommandHandler_Widget(dlg, handleFeedSettingCommands_);
    }
    setupSheetTransition_Mobile(dlg, incoming_TransitionFlag | dialogTransitionDir_Widget(dlg));
    return dlg;
}

/*-----------------------------------------------------------------------------------------------*/

iDeclareType(FolderItems);

struct Impl_FolderItems {
    iHashNode node;
    iArray *items;
};

void cleanupBookmarksMenu_Widget(iWidget *menu) {
    /* Destroy the previously created folder submenus. */
    iConstForEach(PtrArray, c,
                  findChildren_Widget(menu ? root_Widget(menu)
                                           : get_Root()->widget, "bfmenu.*")) {
        destroy_Widget(c.ptr);
    }
}

extern iMenuItem bookmarksMenuItems_Window[];

const iArray *updateBookmarksMenu_Widget(iWidget *menu) {
    /* TODO: Updating the items is only needed if 1) there hasn't been an update yet, or 2)
       bookmarks have changed. */
    cleanupBookmarksMenu_Widget(menu);
    iWidget *rootWidget = (menu ? root_Widget(menu) : get_Root()->widget);
    iBool    isFirst    = iTrue;
    iString *title      = new_String();
    iHash   *hash       = new_Hash();
    iArray  *items      = collectNew_Array(sizeof(iMenuItem));
    pushBackN_Array(items, bookmarksMenuItems_Window, count_MenuItem(bookmarksMenuItems_Window));
    /* Append top-level bookmarks and create new submenus. */
    iConstForEach(PtrArray, i, list_Bookmarks(bookmarks_App(), cmpTree_Bookmark, NULL, NULL)) {
        const iBookmark *bm = i.ptr;
        iArray *dest = items;
        if (bm->parentId) {
            iFolderItems *f = (iFolderItems *) value_Hash(hash, bm->parentId);
            if (!f) {
                f = iZapMalloc(FolderItems);
                f->node.key = bm->parentId;
                f->items = new_Array(sizeof(iMenuItem));
                insert_Hash(hash, &f->node);
            }
            dest = f->items;
        }
        else if (isFirst) {
            isFirst = iFalse;
            pushBack_Array(dest, &(iMenuItem){ "---" });
        }
        iString iconStr;
        if (isFolder_Bookmark(bm)) {
            initCStr_String(&iconStr, folder_Icon);
        }
        else if (bm->icon) {
            initUnicodeN_String(&iconStr, &bm->icon, 1);
        }
        else {
            initCStr_String(&iconStr, pin_Icon);
        }
        /* Truncate titles to a reasonable width. */ {
            set_String(title, &bm->title);
#if !defined (LAGRANGE_NATIVE_MENU)
            const int maxTitleWidth = 60 * gap_UI;
            const char *end;
            tryAdvanceNoWrap_Text(uiLabel_FontId, range_String(title), maxTitleWidth, &end);
            if (end < constEnd_String(title)) {
                truncate_Block(&title->chars, end - constBegin_String(title));
                appendCStr_String(title, "\u2026" /* ellipsis */);
            }
#endif
        }
        iString *setIdentArg = NULL;
        if (!isEmpty_String(&bm->identity)) {
            setIdentArg = copy_String(&bm->identity);
            prependCStr_String(setIdentArg, " setident:");
        }
        pushBack_Array(
            dest,
            &(iMenuItem){ format_CStr("%s %s", cstr_String(&iconStr), cstr_String(title)),
                          0,
                          0,
                          isFolder_Bookmark(bm)
                              ? format_CStr("submenu id:bfmenu.%d", id_Bookmark(bm))
                              : format_CStr("!open%s url:%s",
                                            setIdentArg ? cstr_String(setIdentArg) : "",
                                            cstr_String(&bm->url)) });
        delete_String(setIdentArg);
        deinit_String(&iconStr);
    }
    /* Create folder menus. */
    iForEach(Hash, h, hash) {
        iFolderItems *f = (iFolderItems *) h.value;
        iWidget *bfmenu = makeMenu_Widget(rootWidget, data_Array(f->items), size_Array(f->items));
        setId_Widget(bfmenu, format_CStr("bfmenu.%d", f->node.key));
        delete_Array(f->items);
        free(remove_HashIterator(&h));
    }
    delete_Hash(hash);
    delete_String(title);
    return items;
}
