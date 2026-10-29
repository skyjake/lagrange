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
#include <math.h>

static const iPtrArray *listFeedEntries_SidebarWidget_(const iSidebarWidget *d) {
    iUnused(d);
    /* TODO: Sort order setting? */
    return listEntries_Feeds();
}

static iBool isSubscribed_Bookmark_(void *context, const iBookmark *bm) {
    iUnused(context);
    return (!isFolder_Bookmark(bm) && bm->flags & subscribed_BookmarkFlag) != 0;
}

iDeclareType(FeedItemMeta)

struct Impl_FeedItemMeta {
    uint32_t bookmarkId;
    iBool    isTinylog;
    iChar    icon; /* zero if the bookmark is missing */
    iString  meta; /* shared by all items of the feed */
};

static int cmp_FeedItemMeta_(const void *a, const void *b) {
    const iFeedItemMeta *x = a, *y = b;
    const int cmp = iCmp(x->bookmarkId, y->bookmarkId);
    return cmp ? cmp : iCmp(x->isTinylog, y->isTinylog);
}

static void initEntry_FeedItemMeta_(iFeedItemMeta *d, const iFeedEntry *entry) {
    d->bookmarkId = entry->bookmarkId;
    d->isTinylog  = entry->isTinylog;
    d->icon       = 0;
    init_String(&d->meta);
    const iBookmark *bm = get_Bookmarks(bookmarks_App(), entry->bookmarkId);
    if (!bm) {
        return;
    }
    d->icon = bm->icon ? bm->icon : 0x1f4ac; /* speech balloon */
    if (entry->isTinylog) {
        iTinylogInfo info;
        init_TinylogInfo(&info);
        tinylogInfo_Feeds(&entry->url, &info);
        if (!isEmpty_String(&info.avatar)) {
            d->icon = first_String(&info.avatar);
        }
        if (!isEmpty_String(&info.author) && !equal_String(&info.author, &bm->title)) {
            const char *restoreCStr = "\x1b[0m" restore_ColorEscape;
            appendFormat_String(&d->meta, "\x1b[1m%s", cstr_String(&info.author));
            /* The domain name of the handle has normal styling. */
            const size_t secondAtPos = indexOfCStrFrom_String(&d->meta, "@", 5);
            if (secondAtPos != iInvalidPos) {
                insertData_Block(&d->meta.chars, secondAtPos, restoreCStr, strlen(restoreCStr));
            }
            else {
                appendCStr_String(&d->meta, restoreCStr);
            }
            appendCStr_String(&d->meta, " — ");
        }
        deinit_TinylogInfo(&info);
    }
    append_String(&d->meta, &bm->title);
}

static const iFeedItemMeta *cachedMeta_(iSortedArray *cache, const iFeedEntry *entry) {
    /* Icon and meta are per-feed, so look them up once per bookmark. */
    const iFeedItemMeta key = { .bookmarkId = entry->bookmarkId, .isTinylog = entry->isTinylog };
    size_t pos;
    if (!locate_SortedArray(cache, &key, &pos)) {
        iFeedItemMeta meta;
        initEntry_FeedItemMeta_(&meta, entry);
        insert_SortedArray(cache, &meta);
    }
    return constAt_SortedArray(cache, pos);
}

const char *feedsRefreshAgeCStr_SidebarWidget_(void) {
    const iTime refreshed = lastRefreshedAt_Feeds();
    if (!isValid_Time(&refreshed)) {
        return NULL;
    }
    const int elapsed = iMax(0, (int) (elapsedSeconds_Time(&refreshed) / 60));
    if (elapsed == 0) {
        return cstr_Lang("sidebar.feeds.updated.now");
    }
    if (elapsed < 60) {
        return format_CStr(cstr_Lang("sidebar.feeds.updated.min"), elapsed);
    }
    if (elapsed < 1440) {
        return format_CStr(cstr_Lang("sidebar.feeds.updated.hours"), elapsed / 60, elapsed % 60);
    }
    return formatCStr_Lang("sidebar.feeds.updated.days.n", elapsed / 1440);
}

iString *formatFeedsDate_SidebarWidget_(const iTime *posted) {
    iDate today, on;
    initCurrent_Date(&today);
    init_Date(&on, posted);
    iString *text = format_Date(
        &on, cstr_Lang(on.year == today.year ? "sidebar.date.thisyear" : "sidebar.date.otheryear"));
    if (today.year == on.year && today.month == on.month && today.day == on.day) {
        appendCStr_String(text, " \u2014 ");
        appendCStr_String(text, cstr_Lang("feeds.today"));
    }
    else if (elapsedSeconds_Time(posted) < 6 * 24 * 3600) {
        iString *weekday = format_Date(&on, " \u2014 %A");
        append_String(text, weekday);
        delete_String(weekday);
    }
    return text;
}

static uint32_t postFeedsAge_SidebarWidget_(void *context, SDL_TimerID timerId,
                                            uint32_t interval) {
    iUnused(timerId, interval);
    notify_Widget(context, "feeds.age.changed");
    return 0;
}

void stopFeedsAgeTimer_SidebarWidget_(iSidebarWidget *d) {
    if (d->feedsAgeTimer) {
        SDL_RemoveTimer(d->feedsAgeTimer);
        d->feedsAgeTimer = 0;
    }
}

static void startFeedsAgeTimer_SidebarWidget_(iSidebarWidget *d) {
    stopFeedsAgeTimer_SidebarWidget_(d);
    const iTime refreshed = lastRefreshedAt_Feeds();
    if (isValid_Time(&refreshed)) {
        /* Fires just after the displayed minute count changes. */
        const double elapsed = iMax(0.0, elapsedSeconds_Time(&refreshed));
        const double wait    = 60.0 - fmod(elapsed, 60.0) + 0.1;
        d->feedsAgeTimer     = SDL_AddTimer((uint32_t) (wait * 1000), postFeedsAge_SidebarWidget_, d);
    }
}

iBool updateFeedEntries_SidebarWidget_(iSidebarWidget *d, iBool keepActions) {
    const iBool    isMobile   = (deviceType_App() != desktop_AppDeviceType);
    const iBool    unreadOnly = (d->feedsMode == unread_FeedsMode);
    iBool          isEmpty    = iTrue;
    const iString *docUrl     = feedEntryUrl_DocumentWidget(document_App());
    iDate          on;
    iZap(on);
    iSortedArray *metaCache = new_SortedArray(sizeof(iFeedItemMeta), cmp_FeedItemMeta_);
    iConstForEach(PtrArray, i, listVisibleEntries_Feeds(unreadOnly, docUrl)) {
        const iFeedEntry *entry     = i.ptr;
        const iBool       isOpen    = equal_String(docUrl, &entry->url);
        const iBool       isUnread  = (unreadOnly && !isOpen) || isUnread_FeedEntry(entry);
        const iBool       isTinylog = entry->isTinylog;
        iDate             posted;
        init_Date(&posted, &entry->posted);
        isEmpty = iFalse;
        if (on.year != posted.year || on.month != posted.month || on.day != posted.day) {
            /* Date separator. The label is formatted when drawn. */
            on                              = posted;
            iSidebarItem *sep               = new_SidebarItem();
            sep->listItem.flags.isSeparator = iTrue;
            sep->ts                         = entry->posted;
            addItem_ListWidget(d->list, sep);
            iRelease(sep);
        }
        iSidebarItem *item              = new_SidebarItem();
        item->listItem.flags.isSelected = isOpen; /* currently being viewed */
        item->indent                    = isUnread | (isTinylog << 1); /* `indent` used for flags */
        set_String(&item->url, &entry->url);
        if (isTinylog) {
            format_String(&item->label,
                          uiTextAction_ColorEscape "\x1b[1m%02d:%02d\x1b[0m  ",
                          posted.hour,
                          posted.minute);
            append_String(&item->label, &entry->title);
        }
        else {
            set_String(&item->label, &entry->title);
        }
        const iFeedItemMeta *meta = cachedMeta_(metaCache, entry);
        if (meta->icon) {
            item->id   = entry->bookmarkId;
            item->icon = meta->icon;
            set_String(&item->meta, &meta->meta);
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
    iForEach(Array, mi, &metaCache->values) {
        deinit_String(&((iFeedItemMeta *) mi.value)->meta);
    }
    delete_SortedArray(metaCache);
    /* Actions. */
    if (!isMobile) {
        if (!keepActions && !isEmpty_PtrArray(listFeedEntries_SidebarWidget_(d))) {
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
            setFlags2_Widget(addActionButton_SidebarWidget_(d, page_Icon, "feeds.mode arg:0", 0),
                             alwaysRound_WidgetFlag2, iTrue);
            setFlags2_Widget(addActionButton_SidebarWidget_(d, circle_Icon, "feeds.mode arg:1", 0),
                             alwaysRound_WidgetFlag2, iTrue);
        }
        for (int i = 0; i < 2; i++) {
            iLabelWidget *btn   = child_Widget(d->actions, i + 1);
            const iBool   isSel = (d->feedsMode == (i == 0 ? all_FeedsMode : unread_FeedsMode));
            setFlags_Widget(as_Widget(btn), selected_WidgetFlag, isSel);
            setTextColor_LabelWidget(btn, isSel ? none_ColorId : uiTextAction_ColorId);
        }
    }
    const iMenuItem menuItems[] = {
        { rightArrowhead_Icon " ${menu.opencurrent}", 0, 0, "feed.entry.open" },
        { openTab_Icon " ${menu.opentab}", 0, 0, "feed.entry.open newtab:1" },
        { openTabBg_Icon " ${menu.opentab.background}", 0, 0, "feed.entry.open newtab:2" },
        { openWindow_Icon " ${menu.openwindow}", 0, KMOD_NEWWINDOW, "feed.entry.open newwindow:1" },
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
    if (!isEmpty) {
        startFeedsAgeTimer_SidebarWidget_(d);
    }
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

iBool refreshFeedEntries_SidebarWidget_(iSidebarWidget *d) {
    /* Updates the read and open states if the visible entries still match the items. */
    if (flags_Widget(as_Widget(d)) & hidden_WidgetFlag) {
        return iFalse;
    }
    const iBool    unreadOnly = (d->feedsMode == unread_FeedsMode);
    const iString *docUrl     = feedEntryUrl_DocumentWidget(document_App());
    const size_t   numItems   = numItems_ListWidget(d->list);
    size_t         pos        = 0;
    iBool          isResized  = iFalse;
    iConstForEach(PtrArray, i, listVisibleEntries_Feeds(unreadOnly, docUrl)) {
        const iFeedEntry *entry = i.ptr;
        iSidebarItem     *item  = NULL;
        while (!item && pos < numItems) {
            item = item_ListWidget(d->list, pos++);
            if (item->listItem.flags.isSeparator) {
                item = NULL;
            }
        }
        if (!item || !equal_String(&item->url, &entry->url)) {
            return iFalse;
        }
        const iBool isOpen   = equal_String(docUrl, &entry->url);
        const iBool isUnread = (unreadOnly && !isOpen) || isUnread_FeedEntry(entry);
        const int   flags    = isUnread | (entry->isTinylog << 1);
        if (item->indent != flags) {
            item->indent = flags;
            isResized    = iTrue; /* title font changes */
        }
        item->listItem.flags.isSelected = isOpen;
    }
    if (pos < numItems) {
        return iFalse; /* a separator is always followed by an entry */
    }
    if (isResized) {
        invalidateLayout_ListWidget(d->list);
    }
    updateVisible_ListWidget(d->list);
    invalidate_ListWidget(d->list);
    return iTrue;
}

static int cmpName_SidebarItem_(const void *a, const void *b) {
    const iSidebarItem *x = *(const iSidebarItem **) a;
    const iSidebarItem *y = *(const iSidebarItem **) b;
    return cmpStringCase_String(&x->label, &y->label);
}

static int cmpLatest_SidebarItem_(const void *a, const void *b) {
    /* Feeds without entries are the least recent. */
    const iSidebarItem *x  = *(const iSidebarItem **) a;
    const iSidebarItem *y  = *(const iSidebarItem **) b;
    const iBool         vx = isValid_Time(&x->ts);
    const iBool         vy = isValid_Time(&y->ts);
    if (vx != vy) {
        return vx ? 1 : -1;
    }
    return vx ? cmp_Time(&x->ts, &y->ts) : 0;
}

static int cmpMostRecent_SidebarItem_(const void *a, const void *b) {
    const int cmp = cmpLatest_SidebarItem_(b, a);
    return cmp ? cmp : cmpName_SidebarItem_(a, b);
}

static int cmpLeastRecent_SidebarItem_(const void *a, const void *b) {
    const int cmp = cmpLatest_SidebarItem_(a, b);
    return cmp ? cmp : cmpName_SidebarItem_(a, b);
}

static int cmpNumEntries_SidebarItem_(const void *a, const void *b) {
    const iSidebarItem *x = *(const iSidebarItem **) a;
    const iSidebarItem *y = *(const iSidebarItem **) b;
    if (x->count != y->count) {
        return x->count > y->count ? -1 : 1;
    }
    return cmpName_SidebarItem_(a, b);
}

iBool updateSubscriptions_SidebarWidget_(iSidebarWidget *d, iBool keepActions) {
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
        pushBack_PtrArray(items, item); /* added to the list after sorting */
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
    if (d->subSort != name_SubscriptionsSortOrder) {
        sort_Array(items,
                   d->subSort == mostRecent_SubscriptionsSortOrder    ? cmpMostRecent_SidebarItem_
                   : d->subSort == leastRecent_SubscriptionsSortOrder ? cmpLeastRecent_SidebarItem_
                                                                      : cmpNumEntries_SidebarItem_);
    }
    iForEach(PtrArray, ia, items) {
        addItem_ListWidget(d->list, ia.ptr);
        iRelease(ia.ptr); /* list owns it */
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
        { openWindow_Icon " ${menu.openwindow}", 0, KMOD_NEWWINDOW, "sideitem.open newwindow:1" },
        { "---", 0, 0, NULL },
        { reload_Icon " ${feeds.refresh}", refreshFeeds_KeyShortcut, "feeds.refresh" },
        { "${feeds.reset}", 0, 0, "feeds.reset" },
    };
    d->menu = makeMenu_Widget(as_Widget(d), menuItems, iElemCount(menuItems));
    /* Actions. */
    if (!keepActions && !isEmpty) {
        const iBool     isDesktop   = (deviceType_App() == desktop_AppDeviceType);
        const iMenuItem sortItems[] = {
            { "${sidebar.action.subs.name}", 0, 0, "subs.sort arg:0" },
            { "${sidebar.action.subs.recent}", 0, 0, "subs.sort arg:1" },
            { "${sidebar.action.subs.leastrecent}", 0, 0, "subs.sort arg:2" },
            { "${sidebar.action.subs.entries}", 0, 0, "subs.sort arg:3" },
        };
        if (isDesktop) {
            addChildFlags_Widget(d->actions, iClob(new_Widget()), expand_WidgetFlag);
        }
        addSortDropdown_SidebarWidget_(
            d, d->actions, "subs.sortbutton", sortItems, iElemCount(sortItems), d->subSort, 0);
        if (isDesktop) {
            addChildFlags_Widget(d->actions, iClob(new_Widget()), expand_WidgetFlag);
        }
    }
    else if (keepActions) {
        updateSortDropdown_SidebarWidget_(d, "subs.sortbutton", d->subSort);
    }
    return !isEmpty;
}

static void handleFeedUnsubscribeCommand_SidebarWidget_(iSidebarWidget *d, const char *cmd) {
    /* Each context item belongs to a subscribed feed. */
    iPtrArray items;
    iIntSet   feeds;
    init_PtrArray(&items);
    init_IntSet(&feeds);
    contextItems_SidebarWidget_(d, &items);
    iConstForEach(PtrArray, i, &items) {
        const uint32_t id = ((const iSidebarItem *) i.ptr)->id;
        if (get_Bookmarks(bookmarks_App(), id)) {
            insert_IntSet(&feeds, id);
        }
    }
    if (arg_Command(cmd) /* was confirmed? */) {
        iConstForEach(IntSet, i, &feeds) {
            iBookmark *feedBookmark = get_Bookmarks(bookmarks_App(), *i.value);
            feedBookmark->flags &= ~subscribed_BookmarkFlag;
            removeEntries_Feeds(id_Bookmark(feedBookmark));
        }
        updateItems_SidebarWidget_(d);
    }
    else if (!isEmpty_IntSet(&feeds)) {
        /* Ask for confirmation first. */
        makeQuestion_Widget(
            uiTextCaution_ColorEscape "${heading.unsub}",
            size_IntSet(&feeds) == 1
                ? format_CStr(cstr_Lang("dlg.confirm.unsub"),
                              cstr_String(&get_Bookmarks(bookmarks_App(), front_IntSet(&feeds))->title))
                : formatCStrs_Lang("dlg.confirm.unsub.multi.n", size_IntSet(&feeds)),
            (iMenuItem[]) {
                { "${cancel}", 0, 0, NULL },
                { uiTextCaution_ColorEscape "${dlg.unsub}",
                  0,
                  0,
                  format_CStr("!%s arg:1 ptr:%p", cstr_Rangecc(name_Command(cmd)), d) } },
            2);
    }
    deinit_IntSet(&feeds);
    deinit_PtrArray(&items);
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
    if (equalWidget_Command(cmd, w, "feeds.age.changed")) {
        d->feedsAgeTimer = 0;
        if (d->mode == feedEntries_SidebarMode && numItems_ListWidget(d->list)) {
            invalidateItem_ListWidget(d->list, 0);
            startFeedsAgeTimer_SidebarWidget_(d);
        }
        return iTrue;
    }
    if (equal_Command(cmd, "clock.jumped")) {
        if (d->mode == feedEntries_SidebarMode && numItems_ListWidget(d->list)) {
            invalidateItem_ListWidget(d->list, 0);
            startFeedsAgeTimer_SidebarWidget_(d);
        }
        return iFalse;
    }
    if (equalWidget_Command(cmd, w, "feeds.mode")) {
        d->feedsMode = arg_Command(cmd);
        updateItemsWithFlags_SidebarWidget_(d, iTrue);
        return iTrue;
    }
    else if (equalWidget_Command(cmd, w, "subs.sort")) {
        d->subSort = iClamp(arg_Command(cmd), 0, numEntries_SubscriptionsSortOrder);
        if (d->mode == subscriptions_SidebarMode) {
            updateItemsWithFlags_SidebarWidget_(d, iTrue);
        }
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
                iPtrArray items;
                init_PtrArray(&items);
                contextItems_SidebarWidget_(d, &items);
                iStringArray *cmds = new_StringArray();
                iConstForEach(PtrArray, i, &items) {
                    const iSidebarItem *entry = i.ptr;
                    const size_t        index = index_PtrArrayConstIterator(&i);
                    /* Opening an entry will always mark it as read. */
                    markEntryAsRead_Feeds(entry->id, &entry->url, iTrue);
                    pushBack_StringArray(
                        cmds,
                        feedEntryOpenCommand_String(
                            &entry->url,
                            sequenceTabMode_SidebarWidget_(argLabel_Command(cmd, "newtab"), index),
                            index == 0 ? argLabel_Command(cmd, "newwindow") : 0,
                            isReader_DocumentWidget(document_App())));
                }
                postOpenCommands_SidebarWidget_(get_Root(), cmds);
                iRelease(cmds);
                deinit_PtrArray(&items);
                return iTrue;
            }
            else if (isCommand_Widget(w, ev, "feed.entry.toggleread")) {
                const iString *url = urlFragmentStripped_String(&item->url);
                markEntryAsRead_Feeds(
                    item->id, &item->url, isUnreadEntry_Feeds(item->id, &item->url));
                notify_App("visited.changed");
                return iTrue;
            }
            else if (isCommand_Widget(w, ev, "feed.entry.markread") &&
                     !hasLabel_Command(cmd, "below")) {
                iPtrArray items;
                init_PtrArray(&items);
                contextItems_SidebarWidget_(d, &items);
                iConstForEach(PtrArray, i, &items) {
                    const iSidebarItem *entry = i.ptr;
                    markEntryAsRead_Feeds(entry->id, &entry->url, arg_Command(cmd) != 0);
                }
                deinit_PtrArray(&items);
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
                    handleFeedUnsubscribeCommand_SidebarWidget_(d, cmd);
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
                handleFeedUnsubscribeCommand_SidebarWidget_(d, cmd);
                return iTrue;
            }
        }
    }
    return iFalse;
}
