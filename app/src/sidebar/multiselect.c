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
#include "defs.h"
#include "gmutil.h"
#include "ui/command.h"
#include "ui/root.h"
#include "ui/tabs.h"
#include "ui/util.h"


iBool isMultiSelectMode_SidebarWidget_(const iSidebarWidget *d) {
    switch (d->mode) {
        case bookmarks_SidebarMode:
        case feedEntries_SidebarMode:
        case subscriptions_SidebarMode:
        case history_SidebarMode:
        case openDocuments_SidebarMode:
            return iTrue;
        default:
            return iFalse;
    }
}

static const iString *itemKey_SidebarWidget_(const iSidebarWidget *d, const iSidebarItem *item) {
    /* Identifies the item after the list has been rebuilt. */
    switch (d->mode) {
        case openDocuments_SidebarMode:
            return &item->meta;
        case bookmarks_SidebarMode:
        case subscriptions_SidebarMode:
            return collectNewFormat_String("%u", item->id);
        default:
            return collectNewFormat_String("%u %s", item->id, cstr_String(&item->url));
    }
}

iStringSet *saveMultiSelection_SidebarWidget_(const iSidebarWidget *d) {
    if (!d->list || !numMultiSelected_ListWidget(d->list)) {
        return NULL;
    }
    iStringSet *keys = new_StringSet();
    iBeginCollect();
    for (size_t i = 0; i < numItems_ListWidget(d->list); i++) {
        const iSidebarItem *item = constItem_ListWidget(d->list, i);
        if (item->listItem.flags.isMultiSelected) {
            insert_StringSet(keys, itemKey_SidebarWidget_(d, item));
        }
    }
    iEndCollect();
    return keys;
}

void restoreMultiSelection_SidebarWidget_(iSidebarWidget *d, iStringSet *selection) {
    if (!selection) {
        return;
    }
    if (isMultiSelectMode_SidebarWidget_(d)) {
        iBeginCollect();
        for (size_t i = 0; i < numItems_ListWidget(d->list); i++) {
            const iSidebarItem *item = constItem_ListWidget(d->list, i);
            if (!item->listItem.flags.isSeparator &&
                contains_StringSet(selection, itemKey_SidebarWidget_(d, item))) {
                setMultiSelected_ListWidget(d->list, i, iTrue);
            }
        }
        iEndCollect();
    }
    iRelease(selection);
}

iString *saveContextItem_SidebarWidget_(const iSidebarWidget *d) {
    return d->contextItem ? copy_String(itemKey_SidebarWidget_(d, d->contextItem)) : NULL;
}

void restoreContextItem_SidebarWidget_(iSidebarWidget *d, iString *key) {
    /* The old item was released when the list was cleared. */
    const iBool wasIndexed = (d->contextIndex != iInvalidPos);
    d->contextItem  = NULL;
    d->contextIndex = iInvalidPos;
    if (key) {
        iBeginCollect();
        for (size_t i = 0; i < numItems_ListWidget(d->list); i++) {
            iSidebarItem *item = item_ListWidget(d->list, i);
            if (!item->listItem.flags.isSeparator &&
                equal_String(key, itemKey_SidebarWidget_(d, item))) {
                d->contextItem = item;
                if (wasIndexed) {
                    d->contextIndex = i;
                }
                break;
            }
        }
        iEndCollect();
        delete_String(key);
    }
}

iBool isMultiContextItem_SidebarWidget_(const iSidebarWidget *d, const iSidebarItem *item) {
    return item && item->listItem.flags.isMultiSelected && numMultiSelected_ListWidget(d->list) > 1;
}

void contextItems_SidebarWidget_(const iSidebarWidget *d, iPtrArray *items) {
    if (isMultiContextItem_SidebarWidget_(d, d->contextItem)) {
        for (size_t i = 0; i < numItems_ListWidget(d->list); i++) {
            const iSidebarItem *item = constItem_ListWidget(d->list, i);
            if (item->listItem.flags.isMultiSelected) {
                pushBack_PtrArray(items, item);
            }
        }
    }
    else if (d->contextItem) {
        pushBack_PtrArray(items, d->contextItem);
    }
}

int sequenceTabMode_SidebarWidget_(int newTab, size_t index) {
    return index == 0 ? newTab : newBackground_OpenTabFlag;
}

void postOpenCommands_SidebarWidget_(iRoot *root, const iStringArray *cmds) {
    iConstForEach(StringArray, i, cmds) {
        iString *cmd = copy_String(i.value);
        if (index_StringArrayConstIterator(&i) > 0) {
            /* Previous ones should be idle before continuing. */
            insertData_Block(&cmd->chars, 4, " idle:1", 7);
        }
        postCommandString_Root(root, cmd);
        delete_String(cmd);
    }
}

iWidget *makeMultiMenu_SidebarWidget_(iSidebarWidget *d) {
    iArray *items = collectNew_Array(sizeof(iMenuItem));
    switch (d->mode) {
        case bookmarks_SidebarMode:
            if (isShowingTrash_SidebarWidget(d)) {
                pushBackN_Array(
                    items,
                    (iMenuItem[]) {
                        { "${menu.trash.restore}", 0, 0, "bookmarks.trash.restore" },
                        { "---" },
                        { openTab_Icon " ${menu.folder.opentab}", 0, 0, "bookmark.open newtab:1" },
                        { openTabBg_Icon " ${menu.multi.opentab.background}", 0, 0, "bookmark.open newtab:2" } },
                    4);
            }
            else {
                pushBackN_Array(
                    items,
                    (iMenuItem[]) {
                        { openTab_Icon " ${menu.folder.opentab}", 0, 0, "bookmark.open newtab:1" },
                        { openTabBg_Icon " ${menu.multi.opentab.background}", 0, 0, "bookmark.open newtab:2" },
                        { "---" },
                        { "${menu.multi.copyurls}", 0, 0, "sideitem.copy canon:1" },
                        { "---" },
                        { uiTextCaution_ColorEscape "${menu.multi.bookmarks.delete}",
                          SDLK_BACKSPACE,
                          KMOD_DESKTOP,
                          "bookmark.delete" },
                        { delete_Icon " " uiTextCaution_ColorEscape "${menu.multi.bookmarks.delete}",
                          0,
                          KMOD_MOBILE,
                          "bookmark.delete" } },
                    7);
            }
            break;
        case feedEntries_SidebarMode:
            pushBackN_Array(
                items,
                (iMenuItem[]) {
                    { openTab_Icon " ${menu.folder.opentab}", 0, 0, "feed.entry.open newtab:1" },
                    { openTabBg_Icon " ${menu.multi.opentab.background}", 0, 0, "feed.entry.open newtab:2" },
                    { "---" },
                    { circleWhite_Icon " ${feeds.entry.markread}", 0, 0, "feed.entry.markread arg:1" },
                    { circle_Icon " ${feeds.entry.markunread}", 0, 0, "feed.entry.markread arg:0" },
                    { "${menu.multi.copyurls}", 0, 0, "sideitem.copy" } },
                6);
            break;
        case subscriptions_SidebarMode:
            pushBackN_Array(
                items,
                (iMenuItem[]) {
                    { whiteStar_Icon " " uiTextCaution_ColorEscape "${feeds.unsubscribe}", 0, 0, "sub.unsubscribe" },
                    { "---" },
                    { openTab_Icon " ${menu.folder.opentab}", 0, 0, "sideitem.open newtab:1" },
                    { openTabBg_Icon " ${menu.multi.opentab.background}", 0, 0, "sideitem.open newtab:2" },
                    { "${menu.multi.copyurls}", 0, 0, "sideitem.copy" } },
                5);
            break;
        case history_SidebarMode:
            pushBackN_Array(
                items,
                (iMenuItem[]) {
                    { openTab_Icon " ${menu.folder.opentab}", 0, 0, "history.open newtab:1" },
                    { openTabBg_Icon " ${menu.multi.opentab.background}", 0, 0, "history.open newtab:2" },
                    { "---" },
                    { "${menu.multi.copyurls}", 0, 0, "sideitem.copy canon:1" },
                    { "---" },
                    { close_Icon " ${menu.multi.forgeturls}", 0, 0, "history.delete" } },
                6);
            break;
        case openDocuments_SidebarMode:
            pushBackN_Array(items,
                            (iMenuItem[]) {
                                { close_Icon " ${menu.multi.closetabs}", 0, 0, "opendocs.close" },
                                { "---" },
                                { "${menu.multi.copyurls}", 0, 0, "sideitem.copy" } },
                            3);
            break;
        default:
            return NULL;
    }
    return makeMenu_Widget(as_Widget(d), constData_Array(items), size_Array(items));
}

void moveTabs_SidebarWidget_(iSidebarWidget *d, const iPtrArray *moving, size_t dstPos) {
    iWidget *tabs = findWidget_Root("doctabs");
    if (!tabs) {
        return;
    }
    /* Other tabs before the destination, then the moving ones, then the rest. */
    iPtrArray order;
    init_PtrArray(&order);
    for (int pass = 0; pass < 3; pass++) {
        for (size_t i = 0; i < numItems_ListWidget(d->list); i++) {
            const iSidebarItem *item     = constItem_ListWidget(d->list, i);
            const iBool         isMoving = indexOf_PtrArray(moving, item) != iInvalidPos;
            if ((pass == 0 && !isMoving && i < dstPos) || (pass == 1 && isMoving) ||
                (pass == 2 && !isMoving && i >= dstPos)) {
                iWidget *page = findChild_Widget(tabs, cstr_String(&item->meta));
                if (page) {
                    pushBack_PtrArray(&order, page);
                }
            }
        }
    }
    iConstForEach(PtrArray, i, &order) {
        moveTabPage_Widget(
            tabs, tabPageIndex_Widget(tabs, i.ptr), index_PtrArrayConstIterator(&i));
    }
    deinit_PtrArray(&order);
    refresh_Widget(tabs);
    postCommand_App("doctabs.order.changed");
}
