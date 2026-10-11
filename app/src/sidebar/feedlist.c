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
#include "ui/bookmarkdialogs.h"
#include "ui/command.h"
#include "ui/documentwidget.h"
#include "ui/keys.h"
#include "ui/labelwidget.h"
#include "ui/root.h"
#include "ui/util.h"

#include <lagrange/lang.h>

static const iPtrArray *listFeedEntries_SidebarWidget_(const iSidebarWidget *d) {
    iUnused(d);
    /* TODO: Sort order setting? */
    return listEntries_Feeds();
}

static iBool isSubscribed_Bookmark_(void *context, const iBookmark *bm) {
    iUnused(context);
    return (!isFolder_Bookmark(bm) && bm->flags & subscribed_BookmarkFlag) != 0;
}

iBool updateFeedEntries_SidebarWidget_(iSidebarWidget *d, iBool keepActions) {
    const iBool    isMobile = (deviceType_App() != desktop_AppDeviceType);
    iBool          isEmpty  = iTrue;
    const iString *docUrl   = feedEntryUrl_DocumentWidget(document_App());
    iTime          now;
    iDate          on;
    initCurrent_Time(&now);
    init_Date(&on, &now);
    const iDate today = on;
    iZap(on);
    const iPtrArray *feedEntries = listFeedEntries_SidebarWidget_(d);
    iConstForEach(PtrArray, i, listVisibleEntries_Feeds(d->feedsMode == unread_FeedsMode, docUrl)) {
        const iFeedEntry *entry     = i.ptr;
        const iBool       isOpen    = equal_String(docUrl, &entry->url);
        const iBool       isUnread  = isUnread_FeedEntry(entry);
        const iBool       isTinylog = entry->isTinylog;
        isEmpty = iFalse;
        /* Insert date separators. */ {
            iDate entryDate;
            init_Date(&entryDate, &entry->posted);
            if (on.year != entryDate.year || on.month != entryDate.month ||
                on.day != entryDate.day) {
                on                        = entryDate;
                iSidebarItem *sep         = new_SidebarItem();
                sep->listItem.flags.isSeparator = iTrue;
                iString *text             = format_Date(&on,
                                            cstr_Lang(on.year == today.year
                                                          ? "sidebar.date.thisyear"
                                                          : "sidebar.date.otheryear"));
                if (today.year == on.year && today.month == on.month &&
                    today.day == on.day) {
                    appendCStr_String(text, " \u2014 ");
                    appendCStr_String(text, cstr_Lang("feeds.today"));
                }
                set_String(&sep->meta, text);
                delete_String(text);
                addItem_ListWidget(d->list, sep);
                iRelease(sep);
            }
        }
        iSidebarItem *item              = new_SidebarItem();
        item->listItem.flags.isSelected = isOpen; /* currently being viewed */
        item->indent                    = isUnread | (isTinylog << 1); /* `indent` used for flags */
        set_String(&item->url, &entry->url);
        set_String(&item->label, &entry->title);
        const iBookmark *bm = get_Bookmarks(bookmarks_App(), entry->bookmarkId);
        if (bm) {
            item->id   = entry->bookmarkId;
            item->icon = bm->icon ? bm->icon : 0x1f4ac; /* speech balloon */
            if (isTinylog) {
                iTinylogInfo info;
                init_TinylogInfo(&info);
                if (tinylogInfo_Feeds(&entry->url, &info)) {
                    if (!isEmpty_String(&info.avatar)) {
                        item->icon = first_String(&info.avatar);
                    }
                    if (!isEmpty_String(&info.author) && !equal_String(&info.author, &bm->title)) {
                        const char *restoreCStr = "\x1b[0m" restore_ColorEscape;
                        appendFormat_String(&item->meta,
                                            "\x1b[1m%s",
                                            cstr_String(&info.author));
                        /* The domain name of the handle has normal styling. */
                        const size_t secondAtPos = indexOfCStrFrom_String(&item->meta, "@", 5);
                        if (secondAtPos != iInvalidPos) {
                            insertData_Block(&item->meta.chars, secondAtPos, restoreCStr, strlen(restoreCStr));
                        }
                        else {
                            appendCStr_String(&item->meta, restoreCStr);
                        }
                        appendCStr_String(&item->meta, " — ");
                    }
                }
                deinit_TinylogInfo(&info);
            }
            append_String(&item->meta, &bm->title);
        }
        addItem_ListWidget(d->list, item);
        iRelease(item);
    #if 0
        if (isTinylog) {
                /* Tinylog entries are twice as tall. */
                iSidebarItem *cont                  = new_SidebarItem();
                cont->listItem.flags.isContinuation = iTrue;
                addItem_ListWidget(d->list, cont);
                iRelease(cont);
            }
        }
    #endif
    }
    /* Actions. */
    if (!isMobile) {
        if (!keepActions && !isEmpty_PtrArray(feedEntries)) {
            setId_Widget(as_Widget(addActionButton_SidebarWidget_(
                             d,
                             check_Icon " ${sidebar.action.feeds.markallread}",
                             "feeds.markallread",
                             expand_WidgetFlag | tight_WidgetFlag | collapse_WidgetFlag)),
                         "feeds.markallread");
            iLabelWidget *showLabel =
                addChildFlags_Widget(d->actions,
                                     iClob(new_LabelWidget("${sidebar.action.show}", NULL)),
                                     frameless_WidgetFlag | tight_WidgetFlag |
                                         collapse_WidgetFlag);
            setId_Widget(as_Widget(showLabel), "feeds.showlabel");
            updateSize_LabelWidget(showLabel);
            const iMenuItem items[] = {
                { page_Icon " ${sidebar.action.feeds.showall}",
                  SDLK_U,
                  SDL_KMOD_SHIFT,
                  "feeds.mode arg:0" },
                { circle_Icon " ${sidebar.action.feeds.showunread}",
                  SDLK_U,
                  0,
                  "feeds.mode arg:1" },
            };
            iWidget *dropButton = addChild_Widget(
                d->actions,
                iClob(makeMenuButton_LabelWidget(items[d->feedsMode].label, items, 2)));
            setId_Widget(dropButton, "feeds.modebutton");
            checkIcon_LabelWidget((iLabelWidget *) dropButton);
            setFixedSize_Widget(
                dropButton,
                init_I2(
                    iMaxi(20 * gap_UI,
                          measure_Text(default_FontId,
                                       translateCStr_Lang(
                                           items[findWidestLabel_MenuItem(items, 2)].label))
                                  .advance.x +
                              13 * gap_UI),
                    -1));
        }
        else {
            updateDropdownSelection_LabelWidget(
                findChild_Widget(d->actions, "feeds.modebutton"),
                format_CStr(" arg:%d", d->feedsMode));
        }
    }
    else {
        if (!keepActions) {
            iLabelWidget *readAll = addActionButton_SidebarWidget_(
                d, check_Icon, "feeds.markallread confirm:1", 0);
            setTextColor_LabelWidget(readAll, uiTextCaution_ColorId);
            addActionButton_SidebarWidget_(d, page_Icon, "feeds.mode arg:0", 0);
            addActionButton_SidebarWidget_(d, circle_Icon, "feeds.mode arg:1", 0);
        }
        setOutline_LabelWidget(child_Widget(d->actions, 1), d->feedsMode != all_FeedsMode);
        setOutline_LabelWidget(child_Widget(d->actions, 2),
                               d->feedsMode != unread_FeedsMode);
    }
    const iMenuItem menuItems[] = {
        { rightArrowhead_Icon " ${menu.opencurrent}", 0, 0, "feed.entry.open" },
        { openTab_Icon " ${menu.opentab}", 0, 0, "feed.entry.open newtab:1" },
        { openTabBg_Icon " ${menu.opentab.background}", 0, 0, "feed.entry.open newtab:2" },
        { openWindow_Icon " ${menu.openwindow}", 0, KMOD_DESKTOP, "feed.entry.open newwindow:1" },
        { "---", 0, 0, NULL },
        { circle_Icon " ${feeds.entry.markread}", 0, 0, "feed.entry.toggleread" },
        { downArrow_Icon " ${feeds.entry.markbelowread}",
          0,
          0,
          "feed.entry.markread below:1" },
        { bookmark_Icon " ${feeds.entry.bookmark}", 0, 0, "feed.entry.bookmark" },
        { "${menu.copyurl}", 0, 0, "sideitem.copy" },
        { "---", 0, 0, NULL },
        { page_Icon " ${feeds.entry.openfeed}", 0, 0, "feed.entry.openfeed" },
        { edit_Icon " ${menu.feed.edit}", 0, 0, "feed.entry.edit" },
        { whiteStar_Icon " " uiTextCaution_ColorEscape "${feeds.unsubscribe}",
          0,
          0,
          "feed.entry.unsubscribe" },
        { "---", 0, 0, NULL },
        { check_Icon " ${feeds.markallread}", SDLK_A, SDL_KMOD_SHIFT, "feeds.markallread" },
        { reload_Icon " ${feeds.refresh}", refreshFeeds_KeyShortcut, "feeds.refresh" }
    };
    d->menu     = makeMenu_Widget(as_Widget(d), menuItems, iElemCount(menuItems));
    d->modeMenu = makeMenu_Widget(
        as_Widget(d),
        (iMenuItem[]) {
            { check_Icon " ${feeds.markallread}", SDLK_A, SDL_KMOD_SHIFT, "feeds.markallread" },
            { reload_Icon " ${feeds.refresh}",
              refreshFeeds_KeyShortcut,
              "feeds.refresh" } },
        2);
    return !isEmpty;
}

iBool updateSubscriptions_SidebarWidget_(iSidebarWidget *d) {
    iDeclareType(FeedItem);
    struct Impl_FeedItem {
        iHashNode     node;
        iSidebarItem *item;
    };
    iHash hash;
    init_Hash(&hash);
    /* Make an item for each subscribed page. */
    iPtrArray *items = new_PtrArray();
    iConstForEach(
        PtrArray,
        i,
        list_Bookmarks(
            bookmarks_App(), cmpTitleAscending_Bookmark, isSubscribed_Bookmark_, NULL)) {
        const iBookmark *bm   = i.ptr;
        iSidebarItem    *item = new_SidebarItem(); {
            set_String(&item->label, &bm->title);
            set_String(&item->url, &bm->url);
            item->id   = id_Bookmark(bm);
            item->icon = bm->icon;
        }
        addItem_ListWidget(d->list, item);
        iRelease(item);                 /* list owns it */
        pushBack_PtrArray(items, item); /* we'll use this in the next phase */
        /* Build a lookup hash from feeds to items. */ {
            iFeedItem *mapping = iMalloc(FeedItem);
            mapping->node.key  = item->id;
            mapping->item      = item;
            insert_Hash(&hash, &mapping->node);
        }
    }
    /* Go through the entries and collect statistics.
       TODO: Do this while refreshing feeds... */
    iConstForEach(PtrArray, e, listFeedEntries_SidebarWidget_(d)) {
        const iFeedEntry *entry = e.ptr;
        const iFeedItem *feedItem = (const iFeedItem *) value_Hash(&hash, entry->bookmarkId);
        if (feedItem) { /* if NULL, we don't have the subscription any more */
            iSidebarItem *item = feedItem->item;
            item->count++;
            if (!isValid_Time(&item->ts) || cmp_Time(&entry->posted, &item->ts) > 0) {
                item->ts = entry->posted; /* latest timestamp */
            }
        }
    }
    /* Finalize the items. */
    const iTime now = now_Time();
    iDate       on;
    initCurrent_Date(&on);
    iForEach(PtrArray, it, items) {
        iSidebarItem *item = it.ptr;
        if (isValid_Time(&item->ts)) {
            iDate lastUpdate;
            init_Date(&lastUpdate, &item->ts);
            iString *str = format_Date(&lastUpdate,
                                       on.year == lastUpdate.year ? " \u2014 %b %d"
                                                                  : " \u2014 %b %d, %Y");
            set_String(&item->meta, str);
            delete_String(str);
            iTime age = now;
            sub_Time(&age, &item->ts);
            /* Recently updated feeds are highlighted. */
            item->isBold = seconds_Time(&age) < 6 * 30 * 24 * 3600;
        }
        else {
            setCStr_String(&item->meta, uiTextAction_ColorEscape warning_Icon " ");
            appendCStr_String(&item->meta, cstr_Lang("sidebar.sub.empty"));
        }
    }
    const iBool isEmpty = isEmpty_PtrArray(items);
    delete_PtrArray(items);
    iForEach(Hash, h, &hash) {
        free(remove_HashIterator(&h));
    }
    iAssert(isEmpty_Hash(&hash));
    deinit_Hash(&hash);
    /* The context menus. */
    const iMenuItem menuItems[] = {
        { edit_Icon " ${menu.feed.edit}", 0, 0, "sub.edit" },
        { "${menu.sub.edit}", 0, 0, "bookmark.edit" },
        { whiteStar_Icon " " uiTextCaution_ColorEscape "${feeds.unsubscribe}",
          0,
          0,
          "sub.unsubscribe" },
        { "---", 0, 0, NULL },
        { openTab_Icon " ${menu.opentab}", 0, 0, "sideitem.open newtab:1" },
        { openTabBg_Icon " ${menu.opentab.background}", 0, 0, "sideitem.open newtab:2" },
        { openWindow_Icon " ${menu.openwindow}", 0, KMOD_DESKTOP, "sideitem.open newwindow:1" },
        { "---", 0, 0, NULL },
        { reload_Icon " ${feeds.refresh}", refreshFeeds_KeyShortcut, "feeds.refresh" },
        { "${feeds.reset}", 0, 0, "feeds.reset" },
    };
    d->menu = makeMenu_Widget(as_Widget(d), menuItems, iElemCount(menuItems));
    return !isEmpty;
}

static void handleFeedUnsubscribeCommand_SidebarWidget_(iSidebarWidget *d, const char *cmd,
                                                        iBookmark *feedBookmark) {
    if (arg_Command(cmd) /* was confirmed? */) {
        feedBookmark->flags &= ~subscribed_BookmarkFlag;
        removeEntries_Feeds(id_Bookmark(feedBookmark));
        updateItems_SidebarWidget_(d);
    }
    else {
        /* Ask for confirmation first. */
        makeQuestion_Widget(
            uiTextCaution_ColorEscape "${heading.unsub}",
            format_CStr(cstr_Lang("dlg.confirm.unsub"), cstr_String(&feedBookmark->title)),
            (iMenuItem[]) {
                { "${cancel}", 0, 0, NULL },
                { uiTextCaution_ColorEscape "${dlg.unsub}",
                  0,
                  0,
                  format_CStr("!%s arg:1 ptr:%p", cstr_Rangecc(name_Command(cmd)), d) } },
            2);
    }
}

void updateFeedContextMenu_SidebarWidget_(iSidebarWidget *d) {
    const iBool isRead = (d->contextItem->indent & 1) == 0;
    setMenuItemLabel_Widget(d->menu,
                            "feed.entry.toggleread",
                            isRead ? circle_Icon " ${feeds.entry.markunread}"
                                   : circleWhite_Icon " ${feeds.entry.markread}",
                            0);
}

void openMarkAllReadMenu_Widget(iWidget *anchor, const char *command) {
    /* Phone menus already have a Cancel item. */
    const iMenuItem items[] = {
        { "```${feeds.markallread.confirm}" },
        { "---" },
        { check_Icon " " uiTextCaution_ColorEscape "${feeds.markallread}", 0, 0, command },
        { "---" },
        { "${cancel}", 0, 0, "cancel" },
    };
    const size_t numItems = iElemCount(items) - (deviceType_App() == phone_AppDeviceType ? 2 : 0);
    iWidget *menu = makeMenu_Widget(anchor->root->widget, items, numItems);
    openMenu_Widget(menu, topLeft_Rect(bounds_Widget(anchor)));
}

iBool handleFeedCommand_SidebarWidget_(iSidebarWidget *d, const SDL_Event *ev, const char *cmd) {
    iWidget *w = as_Widget(d);
    if (equalWidget_Command(cmd, w, "feeds.mode")) {
        d->feedsMode = arg_Command(cmd);
        updateItemsWithFlags_SidebarWidget_(d, iTrue);
        return iTrue;
    }
    else if (equal_Command(cmd, "feeds.markallread") && d->mode == feedEntries_SidebarMode) {
        if (argLabel_Command(cmd, "confirm")) {
            /* This is used on mobile. */
            openMarkAllReadMenu_Widget(d->actions, "feeds.markallread");
            return iTrue;
        }
        markAllEntriesAsRead_Feeds();
        notify_App("visited.changed");
        return iTrue;
    }
    else if (startsWith_Command(cmd, "feed.entry.") && d->mode == feedEntries_SidebarMode) {
        const iSidebarItem *item = d->contextItem;
        if (item) {
            if (isCommand_Widget(w, ev, "feed.entry.open")) {
                const char *cmd = command_UserEvent(ev);
                /* Opening an entry will always mark it as read. */
                markEntryAsRead_Feeds(item->id, &item->url, iTrue);
                postCommandString_Root(
                    get_Root(),
                    feedEntryOpenCommand_String(&item->url,
                                                argLabel_Command(cmd, "newtab"),
                                                argLabel_Command(cmd, "newwindow"),
                                                isReader_DocumentWidget(document_App())));
                return iTrue;
            }
            else if (isCommand_Widget(w, ev, "feed.entry.toggleread")) {
                const iString *url = urlFragmentStripped_String(&item->url);
                markEntryAsRead_Feeds(
                    item->id, &item->url, isUnreadEntry_Feeds(item->id, &item->url));
                notify_App("visited.changed");
                return iTrue;
            }
            else if (isCommand_Widget(w, ev, "feed.entry.markread")) {
                iBool       isBelow = iFalse;
                const iBool markingBelow =
                    argLabel_Command(command_UserEvent(ev), "below") != 0;
                iConstForEach(PtrArray, i, listFeedEntries_SidebarWidget_(d)) {
                    const iFeedEntry *entry = i.ptr;
                    if (isBelow) {
                        markEntryAsRead_Feeds(entry->bookmarkId, &entry->url, iTrue);
                    }
                    else {
                        if (equal_String(&entry->url, &item->url) &&
                            entry->bookmarkId == item->id) {
                            isBelow = iTrue;
                        }
                    }
                }
                notify_App("visited.changed");
                return iTrue;
            }
            else if (isCommand_Widget(w, ev, "feed.entry.bookmark")) {
                if (isBlockedBySync_BookmarkDialogs()) {
                    return iTrue;
                }
                makeBookmarkCreation_Widget(&item->url, &item->label, item->icon);
                if (deviceType_App() == desktop_AppDeviceType) {
                    postCommand_App("focus.set id:bmed.title");
                }
                return iTrue;
            }
            iBookmark *feedBookmark = get_Bookmarks(bookmarks_App(), item->id);
            if (feedBookmark) {
                if (isCommand_Widget(w, ev, "feed.entry.openfeed")) {
                    postCommandf_App("open url:%s", cstr_String(&feedBookmark->url));
                    return iTrue;
                }
                if (isCommand_Widget(w, ev, "feed.entry.edit")) {
                    if (isBlockedBySync_BookmarkDialogs()) {
                        return iTrue;
                    }
                    iWidget *dlg = makeFeedSettings_Widget(id_Bookmark(feedBookmark));
                    return iTrue;
                }
                if (isCommand_Widget(w, ev, "feed.entry.unsubscribe")) {
                    handleFeedUnsubscribeCommand_SidebarWidget_(d, cmd, feedBookmark);
                    return iTrue;
                }
            }
        }
    }
    else if (startsWith_Command(cmd, "sub.") && d->mode == subscriptions_SidebarMode) {
        const iSidebarItem *item = d->contextItem;
        if (item) {
            if (isCommand_Widget(w, ev, "sub.edit")) {
                if (isBlockedBySync_BookmarkDialogs()) {
                    return iTrue;
                }
                makeFeedSettings_Widget(item->id);
                return iTrue;
            }
            else if (isCommand_Widget(w, ev, "sub.unsubscribe")) {
                handleFeedUnsubscribeCommand_SidebarWidget_(
                    d, cmd, get_Bookmarks(bookmarks_App(), item->id));
                return iTrue;
            }
        }
    }
    return iFalse;
}
