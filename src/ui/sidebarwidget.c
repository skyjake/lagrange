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

#include "sidebarwidget.h"

#include "app.h"
#include "defs.h"
#include "bookmarks.h"
#include "certlistwidget.h"
#include "command.h"
#include "documentwidget.h"
#include "feeds.h"
#include "gmcerts.h"
#include "gmutil.h"
#include "gmdocument.h"
#include "inputwidget.h"
#include "labelwidget.h"
#include "listwidget.h"
#include "mobile.h"
#include "keys.h"
#include "paint.h"
#include "root.h"
#include "scrollwidget.h"
#include "touch.h"
#include "util.h"
#include "visited.h"

#include <the_Foundation/intset.h>
#include <the_Foundation/regexp.h>
#include <the_Foundation/stringarray.h>
#include <SDL_clipboard.h>
#include <SDL_mouse.h>

iDeclareType(SidebarItem)
typedef iListItemClass iSidebarItemClass;

struct Impl_SidebarItem {
    iListItem listItem;
    uint32_t  id;
    int       indent;
    iChar     icon;
    iBool     isBold;
    iString   label;
    iString   meta;
    iString   url;
};

void init_SidebarItem(iSidebarItem *d) {
    init_ListItem(&d->listItem);
    d->id     = 0;
    d->indent = 0;
    d->icon   = 0;
    d->isBold = iFalse;
    init_String(&d->label);
    init_String(&d->meta);
    init_String(&d->url);
}

void deinit_SidebarItem(iSidebarItem *d) {
    deinit_String(&d->url);
    deinit_String(&d->meta);
    deinit_String(&d->label);
}

static void draw_SidebarItem_(const iSidebarItem *d, iPaint *p, iRect itemRect, const iListWidget *list);

iBeginDefineSubclass(SidebarItem, ListItem)
    .draw = (iAny *) draw_SidebarItem_,
iEndDefineSubclass(SidebarItem)

iDefineObjectConstruction(SidebarItem)

/*----------------------------------------------------------------------------------------------*/

static const char *normalModeLabels_[max_SidebarMode] = {
    book_Icon   " ${sidebar.bookmarks}",
    star_Icon   " ${sidebar.feeds}",
    clock_Icon  " ${sidebar.history}",
    person_Icon " ${sidebar.identities}",
    page_Icon   " ${sidebar.outline}",
};

static const char *tightModeLabels_[max_SidebarMode] = {
    book_Icon,
    star_Icon,
    clock_Icon,
    person_Icon,
    page_Icon,
};

struct Impl_SidebarWidget {
    iWidget           widget;
    enum iSidebarSide side;
    enum iSidebarMode mode;
    enum iFeedsMode   feedsMode;
    iString           cmdPrefix;
    iWidget *         blank;
    iListWidget *     list;
    iCertListWidget * certList;
    iWidget *         actions; /* below the list, area for buttons */
    int               midHeight; /* on portrait phone, the height for the middle state */
    iBool             isEditing; /* mobile edit mode */
    int               modeScroll[max_SidebarMode];
    iLabelWidget *    modeButtons[max_SidebarMode];
    int               maxButtonLabelWidth;
    float             widthAsGaps;
    int               buttonFont;
    int               itemFonts[2];
    size_t            numUnreadEntries;
    iWidget *         resizer;
    iWidget *         menu; /* context menu for an item */
    iWidget *         modeMenu; /* context menu for the sidebar mode (no item) */
    iSidebarItem *    contextItem;  /* list item accessed in the context menu */
    size_t            contextIndex; /* index of list item accessed in the context menu */
    iIntSet *         closedFolders; /* otherwise open */
};

iDefineObjectConstructionArgs(SidebarWidget, (enum iSidebarSide side), side)

iLocalDef iListWidget *list_SidebarWidget_(iSidebarWidget *d) {
    return d->mode == identities_SidebarMode ? (iListWidget *) d->certList : d->list;
}

static iBool isResizing_SidebarWidget_(const iSidebarWidget *d) {
    return (flags_Widget(d->resizer) & pressed_WidgetFlag) != 0;
}

iBookmark *parent_Bookmark(const iBookmark *d) {
    /* TODO: Parent pointers should be prefetched! */
    if (d->parentId) {
        return get_Bookmarks(bookmarks_App(), d->parentId);
    }
    return NULL;
}

iBool hasParent_Bookmark(const iBookmark *d, uint32_t parentId) {
    /* TODO: Parent pointers should be prefetched! */
    while (d->parentId) {
        if (d->parentId == parentId) {
            return iTrue;
        }
        d = get_Bookmarks(bookmarks_App(), d->parentId);
    }
    return iFalse;
}

int depth_Bookmark(const iBookmark *d) {
    /* TODO: Precalculate this! */
    int depth = 0;
    for (; d->parentId; depth++) {
        d = get_Bookmarks(bookmarks_App(), d->parentId);
    }
    return depth;
}

int cmpTree_Bookmark(const iBookmark **a, const iBookmark **b) {
    const iBookmark *bm1 = *a, *bm2 = *b;
    /* Contents of a parent come after it. */
    if (hasParent_Bookmark(bm2, id_Bookmark(bm1))) {
        return -1;
    }
    if (hasParent_Bookmark(bm1, id_Bookmark(bm2))) {
        return 1;
    }
    /* Comparisons are only valid inside the same parent. */
    while (bm1->parentId != bm2->parentId) {
        int depth1 = depth_Bookmark(bm1);
        int depth2 = depth_Bookmark(bm2);
        if (depth1 != depth2) {
            /* Equalize the depth. */
            while (depth1 > depth2) {
                bm1 = parent_Bookmark(bm1);
                depth1--;
            }
            while (depth2 > depth1) {
                bm2 = parent_Bookmark(bm2);
                depth2--;
            }
            continue;
        }
        bm1 = parent_Bookmark(bm1);
        depth1--;
        bm2 = parent_Bookmark(bm2);
        depth2--;
    }
    const int cmp = iCmp(bm1->order, bm2->order);
    if (cmp) return cmp;
    return cmpStringCase_String(&bm1->title, &bm2->title);
}

static enum iFontId actionButtonFont_SidebarWidget_(const iSidebarWidget *d) {
    switch (deviceType_App()) {
        default:
            break;
        case phone_AppDeviceType:
            return isPortrait_App() ? uiLabelBig_FontId : uiLabelMedium_FontId;
        case tablet_AppDeviceType:
            return uiLabelMedium_FontId;
    }
    return d->buttonFont;
}

static iLabelWidget *addActionButton_SidebarWidget_(iSidebarWidget *d, const char *label,
                                                    const char *command, int64_t flags) {
    iLabelWidget *btn = addChildFlags_Widget(d->actions,
                                             iClob(new_LabelWidget(label, command)),
                                             flags);
    setFont_LabelWidget(btn, actionButtonFont_SidebarWidget_(d));
    checkIcon_LabelWidget(btn);
    if (deviceType_App() != desktop_AppDeviceType) {
        setFlags_Widget(as_Widget(btn), frameless_WidgetFlag, iTrue);
        setTextColor_LabelWidget(btn, uiTextAction_ColorId);
        setBackgroundColor_Widget(as_Widget(btn), uiBackground_ColorId);
    }
    return btn;
}

static iBool isBookmarkFolded_SidebarWidget_(const iSidebarWidget *d, const iBookmark *bm) {
    while (bm->parentId) {
        if (contains_IntSet(d->closedFolders, bm->parentId)) {
            return iTrue;
        }
        bm = get_Bookmarks(bookmarks_App(), bm->parentId);
    }
    return iFalse;
}

static iBool isSlidingSheet_SidebarWidget_(const iSidebarWidget *d) {
    return isPortraitPhone_App();
}

static void setMobileEditMode_SidebarWidget_(iSidebarWidget *d, iBool editing) {
    iWidget *w = as_Widget(d);
    d->isEditing = editing;
    if (d->actions) {
        setFlags_Widget(findChild_Widget(w, "sidebar.close"), hidden_WidgetFlag, editing);
        setFlags_Widget(child_Widget(d->actions, 0), hidden_WidgetFlag, !editing);
        setTextCStr_LabelWidget(child_Widget(as_Widget(d->actions), 2),
                                editing ? "${sidebar.close}" : "${sidebar.action.bookmarks.edit}");
        setDragHandleWidth_ListWidget(d->list, editing ? itemHeight_ListWidget(d->list) * 3 / 2 : 0);
        arrange_Widget(d->actions);
    }
}

static const iPtrArray *listFeedEntries_SidebarWidget_(const iSidebarWidget *d) {
    iUnused(d);
    /* TODO: Sort order setting? */
    return listEntries_Feeds();
}

static void updateItemsWithFlags_SidebarWidget_(iSidebarWidget *d, iBool keepActions) {
    const iBool isMobile = (deviceType_App() != desktop_AppDeviceType);
    clear_ListWidget(d->list);
    releaseChildren_Widget(d->blank);
    if (!keepActions) {
        releaseChildren_Widget(d->actions);
    }
    d->actions->rect.size.y = 0;
    destroy_Widget(d->menu);
    destroy_Widget(d->modeMenu);
    d->menu       = NULL;
    d->modeMenu   = NULL;
    iBool isEmpty = iFalse; /* show blank? */
    switch (d->mode) {
        case feeds_SidebarMode: {
            const iString *docUrl = canonicalUrl_String(url_DocumentWidget(document_App()));
                                    /* TODO: internal URI normalization */
            iTime now;
            iDate on;
            initCurrent_Time(&now);
            init_Date(&on, &now);
            const iDate today = on;
            iZap(on);
            size_t numItems = 0;
            isEmpty = iTrue;
            const iPtrArray *feedEntries = listFeedEntries_SidebarWidget_(d);
            iConstForEach(PtrArray, i, feedEntries) {
                const iFeedEntry *entry = i.ptr;
                if (isHidden_FeedEntry(entry)) {
                    continue; /* A hidden entry. */
                }
                /* Don't show entries in the far future. */
                if (secondsSince_Time(&now, &entry->posted) < -24 * 60 * 60) {
                    continue;
                }
                /* Exclude entries that are too old for Visited to keep track of. */
                if (secondsSince_Time(&now, &entry->discovered) > maxAge_Visited) {
                    break; /* the rest are even older */
                }
                const iBool isOpen = equal_String(docUrl, &entry->url);
                const iBool isUnread = isUnread_FeedEntry(entry);
                if (d->feedsMode == unread_FeedsMode && !isUnread && !isOpen) {
                    continue;
                }
                isEmpty = iFalse;
                /* Insert date separators. */ {
                    iDate entryDate;
                    init_Date(&entryDate, &entry->posted);
                    if (on.year != entryDate.year || on.month != entryDate.month ||
                        on.day != entryDate.day) {
                        on = entryDate;
                        iSidebarItem *sep = new_SidebarItem();
                        sep->listItem.isSeparator = iTrue;
                        iString *text = format_Date(&on,
                                                    cstr_Lang(on.year == today.year
                                                                  ? "sidebar.date.thisyear"
                                                                  : "sidebar.date.otheryear"));
                        if (today.year == on.year &&
                            today.month == on.month &&
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
                iSidebarItem *item = new_SidebarItem();
                item->listItem.isSelected = isOpen; /* currently being viewed */
                item->indent = isUnread;
                set_String(&item->url, &entry->url);
                set_String(&item->label, &entry->title);
                const iBookmark *bm = get_Bookmarks(bookmarks_App(), entry->bookmarkId);
                if (bm) {
                    item->id = entry->bookmarkId;
                    item->icon = bm->icon;
                    append_String(&item->meta, &bm->title);
                }
                addItem_ListWidget(d->list, item);
                iRelease(item);
                if (++numItems == 100) {
                    /* For more items, one can always see "about:feeds". A large number of items
                       is a bit difficult to navigate in the sidebar. */
                    break;
                }
            }
            /* Actions. */
            if (!isMobile) {
                if (!keepActions && !isEmpty_PtrArray(feedEntries)) {
                    addActionButton_SidebarWidget_(d,
                                                   check_Icon
                                                   " ${sidebar.action.feeds.markallread}",
                                                   "feeds.markallread",
                                                   expand_WidgetFlag | tight_WidgetFlag);
                    updateSize_LabelWidget(
                        addChildFlags_Widget(d->actions,
                                             iClob(new_LabelWidget("${sidebar.action.show}", NULL)),
                                             frameless_WidgetFlag | tight_WidgetFlag));
                    const iMenuItem items[] = {
                        { page_Icon " ${sidebar.action.feeds.showall}",
                          SDLK_u,
                          KMOD_SHIFT,
                          "feeds.mode arg:0" },
                        { circle_Icon " ${sidebar.action.feeds.showunread}",
                          SDLK_u,
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
                    iLabelWidget *readAll = addActionButton_SidebarWidget_(d,
                                                   check_Icon,
                                                   "feeds.markallread confirm:1",
                                                   0);
                    setTextColor_LabelWidget(readAll, uiTextCaution_ColorId);
                    addActionButton_SidebarWidget_(d,
                                                   page_Icon,
                                                   "feeds.mode arg:0",
                                                   0);
                    addActionButton_SidebarWidget_(d,
                                                   circle_Icon,
                                                   "feeds.mode arg:1",
                                                   0);
                }
                setOutline_LabelWidget(child_Widget(d->actions, 1), d->feedsMode != all_FeedsMode);
                setOutline_LabelWidget(child_Widget(d->actions, 2), d->feedsMode != unread_FeedsMode);
            }
            const iMenuItem menuItems[] = {
                { openTab_Icon " ${menu.opentab}", 0, 0, "feed.entry.open newtab:1" },
                { openTabBg_Icon " ${menu.opentab.background}", 0, 0, "feed.entry.open newtab:2" },
#if defined (iPlatformDesktop)
                { openWindow_Icon " ${menu.openwindow}", 0, 0, "feed.entry.open newwindow:1" },
#endif
                { "---", 0, 0, NULL },
                { circle_Icon " ${feeds.entry.markread}", 0, 0, "feed.entry.toggleread" },
                { downArrow_Icon " ${feeds.entry.markbelowread}", 0, 0, "feed.entry.markread below:1" },
                { bookmark_Icon " ${feeds.entry.bookmark}", 0, 0, "feed.entry.bookmark" },
                { "${menu.copyurl}", 0, 0, "feed.entry.copy" },
                { "---", 0, 0, NULL },
                { page_Icon " ${feeds.entry.openfeed}", 0, 0, "feed.entry.openfeed" },
                { edit_Icon " ${feeds.edit}", 0, 0, "feed.entry.edit" },
                { whiteStar_Icon " " uiTextCaution_ColorEscape "${feeds.unsubscribe}", 0, 0, "feed.entry.unsubscribe" },
                { "---", 0, 0, NULL },
                { check_Icon " ${feeds.markallread}", SDLK_a, KMOD_SHIFT, "feeds.markallread" },
                { reload_Icon " ${feeds.refresh}", refreshFeeds_KeyShortcut, "feeds.refresh" }
            };
            d->menu = makeMenu_Widget(as_Widget(d), menuItems, iElemCount(menuItems));
            d->modeMenu = makeMenu_Widget(
                as_Widget(d),
                (iMenuItem[]){
                    { check_Icon " ${feeds.markallread}", SDLK_a, KMOD_SHIFT, "feeds.markallread" },
                    { reload_Icon " ${feeds.refresh}", refreshFeeds_KeyShortcut, "feeds.refresh" } },
                2);
            break;
        }
        case documentOutline_SidebarMode: {
            const iGmDocument *doc = document_DocumentWidget(document_App());
            iConstForEach(Array, i, headings_GmDocument(doc)) {
                const iGmHeading *head = i.value;
                iSidebarItem *item = new_SidebarItem();
                item->id = index_ArrayConstIterator(&i);
                setRange_String(&item->label, head->text);
                item->indent = head->level * 5 * gap_UI;
                item->isBold = head->level == 0;
                addItem_ListWidget(d->list, item);
                iRelease(item);
            }
            break;
        }
        case bookmarks_SidebarMode: {
            iAssert(get_Root() == d->widget.root);
            iConstForEach(PtrArray, i, list_Bookmarks(bookmarks_App(), cmpTree_Bookmark, NULL, NULL)) {
                const iBookmark *bm = i.ptr;
                if (isBookmarkFolded_SidebarWidget_(d, bm)) {
                    continue; /* inside a closed folder */
                }
                iSidebarItem *item = new_SidebarItem();
                item->listItem.isDraggable = iTrue;
                item->isBold = item->listItem.isDropTarget = isFolder_Bookmark(bm);
                item->id = id_Bookmark(bm);
                item->indent = depth_Bookmark(bm);
                if (isFolder_Bookmark(bm)) {
                    item->icon = contains_IntSet(d->closedFolders, item->id) ? 0x27e9 : 0xfe40;
                }
                else {
                    item->icon = bm->icon;
                }
                set_String(&item->url, &bm->url);
                set_String(&item->label, &bm->title);
                /* Icons for special behaviors. */ {
                    if (bm->flags & subscribed_BookmarkFlag) {
                        appendChar_String(&item->meta, 0x2605);
                    }
                    if (bm->flags & homepage_BookmarkFlag) {
                        appendChar_String(&item->meta, 0x1f3e0);
                    }
                    if (bm->flags & remote_BookmarkFlag) { 
                        item->listItem.isDraggable = iFalse;
                    }
                    if (bm->flags & remoteSource_BookmarkFlag) {
                        appendChar_String(&item->meta, 0x2913);
                        item->isBold = iTrue;
                    }
                    if (bm->flags & linkSplit_BookmarkFlag) {
                        appendChar_String(&item->meta, 0x25e7);
                    }
                }
                addItem_ListWidget(d->list, item);
                iRelease(item);
            }
            const iMenuItem menuItems[] = {
                { openTab_Icon " ${menu.opentab}", 0, 0, "bookmark.open newtab:1" },
                { openTabBg_Icon " ${menu.opentab.background}", 0, 0, "bookmark.open newtab:2" },
#if defined (iPlatformDesktop)
                { openWindow_Icon " ${menu.openwindow}", 0, 0, "bookmark.open newwindow:1" },
#endif
                { "---", 0, 0, NULL },
                { edit_Icon " ${menu.edit}", 0, 0, "bookmark.edit" },
                { copy_Icon " ${menu.dup}", 0, 0, "bookmark.dup" },
                { "${menu.copyurl}", 0, 0, "bookmark.copy" },
                { "---", 0, 0, NULL },
                { "", 0, 0, "bookmark.tag tag:subscribed" },
                { "", 0, 0, "bookmark.tag tag:homepage" },
                { "", 0, 0, "bookmark.tag tag:remotesource" },
                { "---", 0, 0, NULL },
                { delete_Icon " " uiTextCaution_ColorEscape "${bookmark.delete}", 0, 0, "bookmark.delete" },
                { "---", 0, 0, NULL },
                { folder_Icon " ${menu.newfolder}", 0, 0, "bookmark.addfolder" },
                { upDownArrow_Icon " ${menu.sort.alpha}", 0, 0, "bookmark.sortfolder" },
                { "---", 0, 0, NULL },
                { reload_Icon " ${bookmarks.reload}", 0, 0, "bookmarks.reload.remote" }
            };
            d->menu = makeMenu_Widget(as_Widget(d), menuItems, iElemCount(menuItems));
            d->modeMenu = makeMenu_Widget(
                as_Widget(d),
                (iMenuItem[]){ { bookmark_Icon " ${menu.page.bookmark}", SDLK_d, KMOD_PRIMARY, "bookmark.add" },
                               { add_Icon " ${menu.newfolder}", 0, 0, "bookmark.addfolder" },
                               { "---", 0, 0, NULL },                               
                               { upDownArrow_Icon " ${menu.sort.alpha}", 0, 0, "bookmark.sortfolder" },
                               { "---", 0, 0, NULL },
                               { reload_Icon " ${bookmarks.reload}", 0, 0, "bookmarks.reload.remote" } },               
                6);
            if (isMobile) {
                addActionButton_SidebarWidget_(d, "${sidebar.action.bookmarks.newfolder}",
                                               "bookmarks.addfolder", !d->isEditing ? hidden_WidgetFlag : 0);
                addChildFlags_Widget(d->actions, iClob(new_Widget()), expand_WidgetFlag);
                addActionButton_SidebarWidget_(d,
                    d->isEditing ? "${sidebar.close}" : "${sidebar.action.bookmarks.edit}",
                    "sidebar.bookmarks.edit", 0);
            }
            break;
        }
        case history_SidebarMode: {
            iDate on;
            initCurrent_Date(&on);
            const int thisYear = on.year;
            iConstForEach(PtrArray, i, list_Visited(visited_App(), 200)) {
                const iVisitedUrl *visit = i.ptr;
                iSidebarItem *item = new_SidebarItem();
                set_String(&item->url, &visit->url);
                set_String(&item->label, &visit->url);
                if (prefs_App()->decodeUserVisibleURLs) {
                    urlDecodePath_String(&item->label);
                }
                else {
                    urlEncodePath_String(&item->label);
                }
                iDate date;
                init_Date(&date, &visit->when);
                if (date.day != on.day || date.month != on.month || date.year != on.year) {
                    on = date;
                    /* Date separator. */
                    iSidebarItem *sep = new_SidebarItem();
                    sep->listItem.isSeparator = iTrue;
                    const iString *text = collect_String(
                        format_Date(&date,
                                    cstr_Lang(date.year != thisYear ? "sidebar.date.otheryear"
                                                                    : "sidebar.date.thisyear")));
                    set_String(&sep->meta, text);
                    const int yOffset = itemHeight_ListWidget(d->list) * 2 / 3;
                    sep->id = yOffset;
                    addItem_ListWidget(d->list, sep);
                    iRelease(sep);
                    /* Date separators are two items tall. */
                    sep = new_SidebarItem();
                    sep->listItem.isSeparator = iTrue;
                    sep->id = -itemHeight_ListWidget(d->list) + yOffset;
                    set_String(&sep->meta, text);
                    addItem_ListWidget(d->list, sep);
                    iRelease(sep);
                }
                addItem_ListWidget(d->list, item);
                iRelease(item);
            }
            const iMenuItem menuItems[] = {
                { openTab_Icon " ${menu.opentab}", 0, 0, "history.open newtab:1" },
                { openTabBg_Icon " ${menu.opentab.background}", 0, 0, "history.open newtab:2" },
#if defined (iPlatformDesktop)
                { openWindow_Icon " ${menu.openwindow}", 0, 0, "history.open newwindow:1" },
#endif
                { "---" },
                { bookmark_Icon " ${sidebar.entry.bookmark}", 0, 0, "history.addbookmark" },
                { "${menu.copyurl}", 0, 0, "history.copy" },
                { "---", 0, 0, NULL },
                { close_Icon " ${menu.forgeturl}", 0, 0, "history.delete" },
                { "---", 0, 0, NULL },
                { delete_Icon " " uiTextCaution_ColorEscape "${history.clear}", 0, 0, "history.clear confirm:1" },                
            };
            d->menu = makeMenu_Widget(as_Widget(d), menuItems, iElemCount(menuItems));
            d->modeMenu = makeMenu_Widget(
                as_Widget(d),
                (iMenuItem[]){
                    { delete_Icon " " uiTextCaution_ColorEscape "${history.clear}", 0, 0, "history.clear confirm:1" },
                }, 1);
            if (isMobile) {
                addChildFlags_Widget(d->actions, iClob(new_Widget()), expand_WidgetFlag);
                iLabelWidget *btn = addActionButton_SidebarWidget_(d, "${sidebar.action.history.clear}",
                                                                   "history.clear confirm:1", 0);
            }
            break;
        }
        case identities_SidebarMode: {
            isEmpty = !updateItems_CertListWidget(d->certList);
            /* Actions. */
            if (!isEmpty) {
                addActionButton_SidebarWidget_(d, add_Icon " ${sidebar.action.ident.new}", "ident.new", 0);
                addActionButton_SidebarWidget_(d, "${sidebar.action.ident.import}", "ident.import", 0);
            }
            break;
        }
        default:
            break;
    }
    setFlags_Widget(as_Widget(d->list), hidden_WidgetFlag, d->mode == identities_SidebarMode);
    setFlags_Widget(as_Widget(d->certList), hidden_WidgetFlag, d->mode != identities_SidebarMode);    
    scrollOffset_ListWidget(list_SidebarWidget_(d), 0);
    updateVisible_ListWidget(list_SidebarWidget_(d));
    invalidate_ListWidget(list_SidebarWidget_(d));
    /* Content for a blank tab. */
    if (isEmpty) {
        if (d->mode == feeds_SidebarMode) {
            iWidget *div = makeVDiv_Widget();
            setPadding_Widget(div, 3 * gap_UI, 0, 3 * gap_UI, 2 * gap_UI);
            addChildFlags_Widget(div, iClob(new_Widget()), expand_WidgetFlag); /* pad */
            if (d->feedsMode == all_FeedsMode) {
                addChild_Widget(div, iClob(new_LabelWidget("${menu.feeds.refresh}", "feeds.refresh")));
            }
            else {
                iLabelWidget *msg =
                    addChildFlags_Widget(div,
                                         iClob(new_LabelWidget("${sidebar.empty.unread}", NULL)),
                                         frameless_WidgetFlag);
                setFont_LabelWidget(msg, uiLabelLarge_FontId);
                arrange_Widget(d->actions);
                div->padding[3] = height_Widget(d->actions);
            }
            addChildFlags_Widget(div, iClob(new_Widget()), expand_WidgetFlag); /* pad */
            addChild_Widget(d->blank, iClob(div));
        }
        else if (d->mode == identities_SidebarMode) {
            iWidget *div = makeVDiv_Widget();
            setPadding_Widget(div, 3 * gap_UI, 0, 3 * gap_UI, 2 * gap_UI);
            addChildFlags_Widget(div, iClob(new_Widget()), expand_WidgetFlag); /* pad */
            iLabelWidget *msg = new_LabelWidget("${sidebar.empty.idents}", NULL);
            setFont_LabelWidget(msg, uiLabelLarge_FontId);
            addChildFlags_Widget(div, iClob(msg), frameless_WidgetFlag);
            addChild_Widget(div, iClob(makePadding_Widget(3 * gap_UI)));
            addChild_Widget(div, iClob(new_LabelWidget("${menu.identity.new}", "ident.new")));
            addChild_Widget(div, iClob(makePadding_Widget(gap_UI)));
            addChild_Widget(div, iClob(new_LabelWidget("${menu.identity.import}", "ident.import")));
            addChildFlags_Widget(div, iClob(new_Widget()), expand_WidgetFlag); /* pad */
            iLabelWidget *linkLabel;
            setBackgroundColor_Widget(
                addChildFlags_Widget(
                    div,
                    iClob(linkLabel = new_LabelWidget(format_CStr(cstr_Lang("ident.gotohelp"),
                                                      uiTextStrong_ColorEscape,
                                                      restore_ColorEscape),
                                          "!open newtab:1 gotoheading:1.6 url:about:help")),
                    frameless_WidgetFlag | fixedHeight_WidgetFlag),
                uiBackgroundSidebar_ColorId);
            setWrap_LabelWidget(linkLabel, iTrue);
            addChild_Widget(d->blank, iClob(div));
        }
        arrange_Widget(d->blank);
    }
    arrange_Widget(d->actions);
    arrange_Widget(as_Widget(d));
    updateMouseHover_ListWidget(list_SidebarWidget_(d));
}

static void updateItems_SidebarWidget_(iSidebarWidget *d) {
    updateItemsWithFlags_SidebarWidget_(d, iFalse);
}

static size_t findItem_SidebarWidget_(const iSidebarWidget *d, int id) {
    /* Note that this is O(n), so only meant for infrequent use. */
    for (size_t i = 0; i < numItems_ListWidget(d->list); i++) {
        const iSidebarItem *item = constItem_ListWidget(d->list, i);
        if (item->id == id) {
            return i;
        }
    }
    return iInvalidPos;
}

static void updateItemHeight_SidebarWidget_(iSidebarWidget *d) {
    /* Note: identity item height is defined by CertListWidget */
#if !defined (iPlatformTerminal)
    const float heights[max_SidebarMode] = { 1.333f, 2.333f, 1.333f, 0, 1.2f };
#else
    const float heights[max_SidebarMode] = { 1, 3, 1, 0, 1 };
#endif
    if (d->list) {
        setItemHeight_ListWidget(d->list, heights[d->mode] * lineHeight_Text(d->itemFonts[0]));
    }
    if (d->certList) {
        updateItemHeight_CertListWidget(d->certList);
    }
}

iBool setMode_SidebarWidget(iSidebarWidget *d, enum iSidebarMode mode) {
    if (d->mode == mode) {
        return iFalse;
    }
    if (mode == identities_SidebarMode && deviceType_App() != desktop_AppDeviceType) {
        return iFalse; /* Identities are in Settings. */
    }
    if (d->mode >= 0 && d->mode < max_SidebarMode) {
        d->modeScroll[d->mode] = scrollPos_ListWidget(list_SidebarWidget_(d)); /* saved for later */
    }
    d->mode = mode;
    for (enum iSidebarMode i = 0; i < max_SidebarMode; i++) {
        setFlags_Widget(as_Widget(d->modeButtons[i]), selected_WidgetFlag, i == d->mode);
    }
    setBackgroundColor_Widget(as_Widget(list_SidebarWidget_(d)),
                              d->mode == documentOutline_SidebarMode ? tmBannerBackground_ColorId
                                                                     : uiBackgroundSidebar_ColorId);
    updateItemHeight_SidebarWidget_(d);
    if (deviceType_App() != desktop_AppDeviceType && mode != bookmarks_SidebarMode) {
        setMobileEditMode_SidebarWidget_(d, iFalse);
    }
    /* Restore previous scroll position. */
    setScrollPos_ListWidget(list_SidebarWidget_(d), d->modeScroll[mode]);
    /* Title of the mobile sliding sheet. */
    iLabelWidget *sheetTitle = findChild_Widget(&d->widget, "sidebar.title");
    if (sheetTitle) {
        iString title;
        initCStr_String(&title, normalModeLabels_[d->mode]);
        removeIconPrefix_String(&title);
        setText_LabelWidget(sheetTitle, &title);
        deinit_String(&title);
    }
    return iTrue;
}

void setClosedFolders_SidebarWidget(iSidebarWidget *d, const iIntSet *closedFolders) {
    if (d) {
        delete_IntSet(d->closedFolders);
        d->closedFolders = copy_IntSet(closedFolders);
    }
}

void setMidHeight_SidebarWidget(iSidebarWidget *d, int midHeight) {
    d->midHeight = midHeight;
}

enum iSidebarMode mode_SidebarWidget(const iSidebarWidget *d) {
    return d ? d->mode : 0;
}

enum iFeedsMode feedsMode_SidebarWidget(const iSidebarWidget *d) {
    return d ? d->feedsMode : 0;
}

float width_SidebarWidget(const iSidebarWidget *d) {
    return d ? d->widthAsGaps : 0;
}

const iIntSet *closedFolders_SidebarWidget(const iSidebarWidget *d) {
    return d ? d->closedFolders : collect_IntSet(new_IntSet());
}

iListWidget *list_SidebarWidget(iSidebarWidget *d) {
    return list_SidebarWidget_(d);
}

const char *icon_SidebarMode(enum iSidebarMode mode) {
    return tightModeLabels_[mode];
}

static void updateMetrics_SidebarWidget_(iSidebarWidget *d) {
    if (d->resizer) {
        d->resizer->rect.size.x = gap_UI;
    }
    d->maxButtonLabelWidth = 0;
    for (int i = 0; i < max_SidebarMode; i++) {
        if (d->modeButtons[i]) {
            d->maxButtonLabelWidth =
                iMaxi(d->maxButtonLabelWidth,
                      3 * gap_UI + measure_Text(font_LabelWidget(d->modeButtons[i]),
                                                translateCStr_Lang(normalModeLabels_[i]))
                                       .bounds.size.x);
        }
    }
    updateItemHeight_SidebarWidget_(d);
}

static void updateSlidingSheetHeight_SidebarWidget_(iSidebarWidget *sidebar, iRoot *root) {
    if (!isPortraitPhone_App() || !isVisible_Widget(sidebar)) return;
    iWidget *d = as_Widget(sidebar);
    const int oldSize = d->rect.size.y;
    const int newSize = bottom_Rect(safeRect_Root(d->root)) - top_Rect(bounds_Widget(d));
    if (oldSize != newSize) {
        d->rect.size.y = newSize;
        arrange_Widget(d);
    }
//    printf("[%p] %u: %d  animating %d\n", d, window_Widget(d)->frameTime,
//           (flags_Widget(d) & visualOffset_WidgetFlag) != 0,
//           newSize);
}

void init_SidebarWidget(iSidebarWidget *d, enum iSidebarSide side) {
    iWidget *w = as_Widget(d);
    init_Widget(w);
    setId_Widget(w, side == left_SidebarSide ? "sidebar" : "sidebar2");
    initCopy_String(&d->cmdPrefix, id_Widget(w));
    appendChar_String(&d->cmdPrefix, '.');
    setBackgroundColor_Widget(w, none_ColorId);
    setFlags_Widget(w,
                    collapse_WidgetFlag | hidden_WidgetFlag | arrangeHorizontal_WidgetFlag |
                        resizeWidthOfChildren_WidgetFlag | noFadeBackground_WidgetFlag |
                    noShadowBorder_WidgetFlag,
                    iTrue);
    iZap(d->modeScroll);
    d->side = side;
    d->mode = -1;
    d->feedsMode = all_FeedsMode;
    d->midHeight = 0;
    d->isEditing = iFalse;
    d->numUnreadEntries = 0;
    d->buttonFont = uiLabel_FontId; /* wiil be changed later */
    d->itemFonts[0] = uiContent_FontId;
    d->itemFonts[1] = uiContentBold_FontId;
    if (isMobile_Platform()) {
        if (deviceType_App() == phone_AppDeviceType) {
            d->itemFonts[0] = uiLabelBig_FontId;
            d->itemFonts[1] = uiLabelBigBold_FontId;
        }
        d->widthAsGaps = 73.0f;
    }
    else {
        d->widthAsGaps = isTerminal_Platform() ? 35.0f : 60.0f;
    }
    setFlags_Widget(w, fixedWidth_WidgetFlag, iTrue);
    iWidget *vdiv = makeVDiv_Widget();
    addChildFlags_Widget(w, iClob(vdiv), resizeToParentWidth_WidgetFlag | resizeToParentHeight_WidgetFlag);
    iZap(d->modeButtons);
    d->resizer       = NULL;
    d->list          = NULL;
    d->certList      = NULL;
    d->actions       = NULL;
    d->closedFolders = new_IntSet();
    /* On a phone, the right sidebar is not used. */
    const iBool isPhone = (deviceType_App() == phone_AppDeviceType);
    if (isPhone) {
        iLabelWidget *sheetTitle = addChildFlags_Widget(
            vdiv,
            iClob(new_LabelWidget("", NULL)),
            collapse_WidgetFlag | extraPadding_WidgetFlag | frameless_WidgetFlag);
        setBackgroundColor_Widget(as_Widget(sheetTitle), uiBackground_ColorId);
        iLabelWidget *closeButton = addChildFlags_Widget(
            as_Widget(sheetTitle),
            iClob(new_LabelWidget(uiTextAction_ColorEscape "${sidebar.close}", "sidebar.toggle")),
            extraPadding_WidgetFlag | frameless_WidgetFlag | alignRight_WidgetFlag |
                moveToParentRightEdge_WidgetFlag);
        as_Widget(sheetTitle)->flags2 |= slidingSheetDraggable_WidgetFlag2;  /* phone */
        as_Widget(closeButton)->flags2 |= slidingSheetDraggable_WidgetFlag2; /* phone */
        setId_Widget(as_Widget(sheetTitle), "sidebar.title");
        setId_Widget(as_Widget(closeButton), "sidebar.close");
        setFont_LabelWidget(sheetTitle, uiLabelBig_FontId);
        setFont_LabelWidget(closeButton, uiLabelBigBold_FontId);
        iConnect(
            Root, get_Root(), visualOffsetsChanged, d, updateSlidingSheetHeight_SidebarWidget_);
    }
    iWidget *buttons = new_Widget();
    setId_Widget(buttons, "buttons");
    setDrawBufferEnabled_Widget(buttons, iTrue);
    for (int i = 0; i < max_SidebarMode; i++) {
        if (i == identities_SidebarMode && deviceType_App() != desktop_AppDeviceType) {
            /* On mobile, identities are managed via Settings. */
            continue;
        }
        d->modeButtons[i] = addChildFlags_Widget(
            buttons,
            iClob(new_LabelWidget(tightModeLabels_[i],
                                  format_CStr("%s.mode arg:%d", cstr_String(id_Widget(w)), i))),
            frameless_WidgetFlag | noBackground_WidgetFlag);
        as_Widget(d->modeButtons[i])->flags2 |= slidingSheetDraggable_WidgetFlag2; /* phone */
    }
    setButtonFont_SidebarWidget(d, isPhone ? uiLabelBig_FontId : uiLabel_FontId);
    addChildFlags_Widget(vdiv,
                         iClob(buttons),
                         arrangeHorizontal_WidgetFlag | resizeWidthOfChildren_WidgetFlag |
                             arrangeHeight_WidgetFlag | resizeToParentWidth_WidgetFlag);
    setBackgroundColor_Widget(buttons, uiBackgroundSidebar_ColorId);
    iWidget *content = new_Widget();
    setFlags_Widget(content, resizeChildren_WidgetFlag, iTrue);
    iWidget *listAndActions = makeVDiv_Widget();
    addChild_Widget(content, iClob(listAndActions));
    iWidget *listArea = new_Widget();
    setFlags_Widget(listArea, resizeChildren_WidgetFlag, iTrue);
    d->list = new_ListWidget();
    setPadding_Widget(as_Widget(d->list), 0, gap_UI, 0, gap_UI);
    addChildFlags_Widget(listArea, iClob(d->list), focusable_WidgetFlag);
    if (!isPhone) {
        d->certList = new_CertListWidget();
        setPadding_Widget(as_Widget(d->certList), 0, gap_UI, 0, gap_UI);
        addChild_Widget(listArea, iClob(d->certList));
    }
    addChildFlags_Widget(listAndActions,
                         iClob(listArea),
                         expand_WidgetFlag); // | drawBackgroundToHorizontalSafeArea_WidgetFlag);
    setId_Widget(
        addChildPosFlags_Widget(listAndActions,
                                iClob(d->actions = new_Widget()),
                                back_WidgetAddPos,
                                arrangeHorizontal_WidgetFlag | arrangeHeight_WidgetFlag |
                                    resizeWidthOfChildren_WidgetFlag),
        "actions");
    if (deviceType_App() != desktop_AppDeviceType) {
        setFlags_Widget(findChild_Widget(w, "sidebar.title"), borderTop_WidgetFlag, iTrue);
        setFlags_Widget(d->actions, drawBackgroundToBottom_WidgetFlag, iTrue);
        setBackgroundColor_Widget(d->actions, uiBackground_ColorId);
    }
    else {
        setBackgroundColor_Widget(d->actions, uiBackgroundSidebar_ColorId);
    }
    d->contextItem  = NULL;
    d->contextIndex = iInvalidPos;
    d->blank        = new_Widget();
    addChildFlags_Widget(content, iClob(d->blank), resizeChildren_WidgetFlag);
    addChildFlags_Widget(vdiv, iClob(content), expand_WidgetFlag);
    setMode_SidebarWidget(d,
                          /*deviceType_App() == phone_AppDeviceType && d->side == right_SidebarSide
                          ? identities_SidebarMode :*/
                          bookmarks_SidebarMode);
    d->resizer =
        addChildFlags_Widget(w,
                             iClob(new_Widget()),
                             hover_WidgetFlag | commandOnClick_WidgetFlag | fixedWidth_WidgetFlag |
                                 resizeToParentHeight_WidgetFlag |
                                 (side == left_SidebarSide ? moveToParentRightEdge_WidgetFlag
                                                           : moveToParentLeftEdge_WidgetFlag));
    if (deviceType_App() != desktop_AppDeviceType) {
        setFlags_Widget(d->resizer, hidden_WidgetFlag | disabled_WidgetFlag, iTrue);
    }
    setId_Widget(d->resizer, side == left_SidebarSide ? "sidebar.grab" : "sidebar2.grab");
    setBackgroundColor_Widget(d->resizer, none_ColorId);
    d->menu     = NULL;
    d->modeMenu = NULL;
    addAction_Widget(w, refreshFeeds_KeyShortcut, "feeds.refresh");
    updateMetrics_SidebarWidget_(d);
    if (side == left_SidebarSide) {
        postCommand_App("~sidebar.update"); /* unread count */
    }
}

void deinit_SidebarWidget(iSidebarWidget *d) {
    deinit_String(&d->cmdPrefix);
    delete_IntSet(d->closedFolders);
}

iBool setButtonFont_SidebarWidget(iSidebarWidget *d, int font) {
    if (d->buttonFont != font) {
        d->buttonFont = font;
        for (int i = 0; i < max_SidebarMode; i++) {
            if (d->modeButtons[i]) {
                setFont_LabelWidget(d->modeButtons[i], font);
            }
        }
        updateMetrics_SidebarWidget_(d);
        return iTrue;
    }
    return iFalse;
}

static const iGmIdentity *constHoverIdentity_SidebarWidget_(const iSidebarWidget *d) {
    if (d->mode == identities_SidebarMode) {
        return constHoverIdentity_CertListWidget(d->certList);
    }
    return NULL;
}

static iGmIdentity *hoverIdentity_SidebarWidget_(const iSidebarWidget *d) {
    if (d->mode == identities_SidebarMode) {
        return hoverIdentity_CertListWidget(d->certList);
    }
    return NULL;
}

static void itemClicked_SidebarWidget_(iSidebarWidget *d, iSidebarItem *item, size_t itemIndex,
                                       int mouseButton) {
    const int mouseTabMode =
        mouseButton == SDL_BUTTON_MIDDLE
            ? (keyMods_Sym(modState_Keys()) & KMOD_SHIFT ? new_OpenTabFlag
                                                         : newBackground_OpenTabFlag)
            : 0;
    switch (d->mode) {
        case documentOutline_SidebarMode: {
            const iGmDocument *doc = document_DocumentWidget(document_App());
            if (item->id < size_Array(headings_GmDocument(doc))) {
                const iGmHeading *head = constAt_Array(headings_GmDocument(doc), item->id);
                postCommandf_App("document.goto loc:%p", head->text.start);
                dismissPortraitPhoneSidebars_Root(as_Widget(d)->root);
                setFocus_Widget(NULL);
            }
            break;
        }
        case feeds_SidebarMode: {
            postCommandString_Root(
                get_Root(),
                feedEntryOpenCommand_String(
                    &item->url, mouseTabMode ? mouseTabMode : openTabMode_Sym(modState_Keys()), 0));
            setFocus_Widget(NULL);
            break;
        }
        case bookmarks_SidebarMode:
            if (isEmpty_String(&item->url)) /* a folder */ {
                if (contains_IntSet(d->closedFolders, item->id)) {
                    remove_IntSet(d->closedFolders, item->id);
                    setRecentFolder_Bookmarks(bookmarks_App(), item->id);
                }
                else {
                    insert_IntSet(d->closedFolders, item->id);
                    setRecentFolder_Bookmarks(bookmarks_App(), 0);
                }
                updateItems_SidebarWidget_(d);
                break;
            }
            if (d->isEditing) {
                d->contextItem = item;
                d->contextIndex = itemIndex;
                setFocus_Widget(NULL);
                postCommand_Widget(d, "bookmark.edit");
                break;
            }
            /* fall through */
        case history_SidebarMode: {
            if (!isEmpty_String(&item->url)) {
                postCommandf_Root(get_Root(),
                                  "open fromsidebar:1 newtab:%d url:%s",
                                  mouseTabMode ? mouseTabMode : openTabMode_Sym(modState_Keys()),
                                  cstr_String(&item->url));
                setFocus_Widget(NULL);
            }
            break;
        }
        default:
            break;
    }
}

static void checkModeButtonLayout_SidebarWidget_(iSidebarWidget *d) {
    if (!d->modeButtons[0]) return;
    if (deviceType_App() == phone_AppDeviceType) {
        /* Change font size depending on orientation. */
        const int fonts[2] = {
            isPortrait_App() ? uiLabelBig_FontId : uiContent_FontId,
            isPortrait_App() ? uiLabelBigBold_FontId : uiContentBold_FontId
        };
        if (d->itemFonts[0] != fonts[0]) {
            d->itemFonts[0] = fonts[0];
            d->itemFonts[1] = fonts[1];
//            updateMetrics_SidebarWidget_(d);
            updateItemHeight_SidebarWidget_(d);
        }
        setButtonFont_SidebarWidget(d, isPortrait_App() ? uiLabelMedium_FontId : uiLabel_FontId);
    }
    const iBool isTight =
        (width_Rect(bounds_Widget(as_Widget(d->modeButtons[0]))) < d->maxButtonLabelWidth);
    for (int i = 0; i < max_SidebarMode; i++) {
        iLabelWidget *button = d->modeButtons[i];
        if (!button) continue;
        setAlignVisually_LabelWidget(button, isTight);
        setFlags_Widget(as_Widget(button), tight_WidgetFlag, isTight);
        if (i == feeds_SidebarMode && d->numUnreadEntries) {
            updateText_LabelWidget(
                button,
                collectNewFormat_String("%s " uiTextAction_ColorEscape "%zu%s%s",
                                        tightModeLabels_[i],
                                        d->numUnreadEntries,
                                        !isTight ? " " : "",
                                        !isTight
                                            ? formatCStrs_Lang("sidebar.unread.n", d->numUnreadEntries)
                                            : ""));
        }
        else {
            updateTextCStr_LabelWidget(button,
                                       isTight ? tightModeLabels_[i] : normalModeLabels_[i]);
        }
    }
}

void setWidth_SidebarWidget(iSidebarWidget *d, float widthAsGaps) {
    if (!d) return;
    iWidget *w = as_Widget(d);
    const iBool isFixedWidth = deviceType_App() == phone_AppDeviceType;
    int width = widthAsGaps * gap_UI; /* in pixels */
    if (!isFixedWidth) {
        /* Even less space if the other sidebar is visible, too. */
        const iWidget *other = findWidget_App(d->side == left_SidebarSide ? "sidebar2" : "sidebar");
        const int otherWidth = isVisible_Widget(other) ? width_Widget(other) : 0;
        width = iClamp(width,
                       30 * gap_UI * aspect_UI,
                       size_Root(w->root).x - 50 * gap_UI * aspect_UI - otherWidth);
    }
    d->widthAsGaps = (float) width / (float) gap_UI;
    w->rect.size.x = width;
    arrange_Widget(findWidget_Root("stack"));
    checkModeButtonLayout_SidebarWidget_(d);
    updateItemHeight_SidebarWidget_(d);
}

static uint32_t bookmarkEditorId_(const iWidget *editor) {
    iAssert(startsWith_String(id_Widget(editor), "bmed."));
    uint32_t bmId = strtoul(cstr_String(id_Widget(editor)) + 5, NULL, 10);
    iAssert(bmId != 0);
    return bmId;    
}

iBool handleBookmarkEditorCommands_SidebarWidget_(iWidget *editor, const char *cmd) {
    if (equal_Command(cmd, "dlg.bookmark.setfolder")) {
        setBookmarkEditorParentFolder_Widget(editor, arg_Command(cmd));
        return iTrue;
    }
    else if (equal_Command(cmd, "bmed.dup")) {
        const iString *title = text_InputWidget(findChild_Widget(editor, "bmed.title"));
        const iString *url   = text_InputWidget(findChild_Widget(editor, "bmed.url"));
        const iString *icon  = collect_String(trimmed_String(
            text_InputWidget(findChild_Widget(editor, "bmed.icon"))));
        makeBookmarkCreation_Widget(url, title, isEmpty_String(icon) ? 0 : first_String(icon));
        setupSheetTransition_Mobile(editor, dialogTransitionDir_Widget(editor));
        destroy_Widget(editor);
        return iTrue;
    }
    else if (equal_Command(cmd, "bmed.accept") || equal_Command(cmd, "bmed.cancel")) {
        const uint32_t bmId = bookmarkEditorId_(editor);
        if (equal_Command(cmd, "bmed.accept")) {
            const iString *title = text_InputWidget(findChild_Widget(editor, "bmed.title"));
            const iString *url   = text_InputWidget(findChild_Widget(editor, "bmed.url"));
            const iString *tags  = text_InputWidget(findChild_Widget(editor, "bmed.tags"));
            const iString *icon  = collect_String(trimmed_String(
                                        text_InputWidget(findChild_Widget(editor, "bmed.icon"))));
            iBookmark *bm = get_Bookmarks(bookmarks_App(), bmId);
            set_String(&bm->title, title);
            if (!isFolder_Bookmark(bm)) {
                set_String(&bm->url, url);
                set_String(&bm->tags, tags);
                if (isEmpty_String(icon)) {
                    bm->flags &= ~userIcon_BookmarkFlag;
                    bm->icon = 0;
                }
                else {
                    bm->flags |= userIcon_BookmarkFlag;
                    bm->icon = first_String(icon);
                }
                iChangeFlags(bm->flags, homepage_BookmarkFlag, isSelected_Widget(findChild_Widget(editor, "bmed.tag.home")));
                iChangeFlags(bm->flags, remoteSource_BookmarkFlag, isSelected_Widget(findChild_Widget(editor, "bmed.tag.remote")));
                iChangeFlags(bm->flags, linkSplit_BookmarkFlag, isSelected_Widget(findChild_Widget(editor, "bmed.tag.linksplit")));
            }
            const iBookmark *folder = userData_Object(findChild_Widget(editor, "bmed.folder"));
            if (!folder || !hasParent_Bookmark(folder, id_Bookmark(bm))) {
                bm->parentId = folder ? id_Bookmark(folder) : 0;
            }
            postCommand_App("bookmarks.changed");
        }
        setupSheetTransition_Mobile(editor, dialogTransitionDir_Widget(editor));
        destroy_Widget(editor);
        return iTrue;
    }
    return iFalse;
}

enum iSlidingSheetPos {
    top_SlidingSheetPos,
    middle_SlidingSheetPos,
    bottom_SlidingSheetPos,
};

static void setSlidingSheetPos_SidebarWidget_(iSidebarWidget *d, enum iSlidingSheetPos slide) {
    iWidget *w = as_Widget(d);
    const int pos = w->rect.pos.y;
    const iRect safeRect = safeRect_Root(w->root);
    if (slide == top_SlidingSheetPos) {
        w->rect.pos.y = top_Rect(safeRect);
        w->rect.size.y = height_Rect(safeRect);
        setVisualOffset_Widget(w, pos - w->rect.pos.y, 0, 0);
        setVisualOffset_Widget(w, 0, 200, easeOut_AnimFlag | softer_AnimFlag);
        setScrollMode_ListWidget(d->list, disabledAtTopUpwards_ScrollMode);
    }
    else if (slide == bottom_SlidingSheetPos) {
        postCommand_Widget(w, "sidebar.toggle");
    }
    else {
        w->rect.size.y = d->midHeight;
        w->rect.pos.y = height_Rect(safeRect) - w->rect.size.y;
        setVisualOffset_Widget(w, pos - w->rect.pos.y, 0, 0);
        setVisualOffset_Widget(w, 0, 200, easeOut_AnimFlag | softer_AnimFlag);
        setScrollMode_ListWidget(d->list, disabledAtTopBothDirections_ScrollMode);
    }
//    animateSlidingSheetHeight_SidebarWidget_(d);
}

static iBool handleSidebarCommand_SidebarWidget_(iSidebarWidget *d, const char *cmd) {
    iWidget *w = as_Widget(d);
    if (equal_Command(cmd, "width")) {
        setWidth_SidebarWidget(d, arg_Command(cmd) *
                               (argLabel_Command(cmd, "gaps") ? 1.0f : (1.0f / gap_UI)));
        return iTrue;
    }
    else if (equal_Command(cmd, "mode")) {
        const iBool wasChanged = setMode_SidebarWidget(d, arg_Command(cmd));
        updateItems_SidebarWidget_(d);
        if ((argLabel_Command(cmd, "show") && !isVisible_Widget(w)) ||
            (argLabel_Command(cmd, "toggle") && (!isVisible_Widget(w) || !wasChanged))) {
            postCommandf_App("%s.toggle", cstr_String(id_Widget(w)));
        }
        scrollOffset_ListWidget(d->list, 0);
        if (wasChanged) {
            postCommandf_App("%s.mode.changed arg:%d", cstr_String(id_Widget(w)), d->mode);
            if (isTerminal_Platform()) {
                setFocus_Widget(as_Widget(list_SidebarWidget(d)));
                if (wasChanged) {
                    setCursorItem_ListWidget(list_SidebarWidget(d), 0);
                }
            }
        }
        refresh_Widget(findChild_Widget(w, "buttons"));
        return iTrue;
    }
    else if (equal_Command(cmd, "toggle")) {
        if (arg_Command(cmd) && isVisible_Widget(w)) {
            return iTrue;
        }        
        const iBool isAnimated = prefs_App()->uiAnimations &&
                                 argLabel_Command(cmd, "noanim") == 0 &&
                                 (d->side == left_SidebarSide || deviceType_App() != phone_AppDeviceType);
        int visX = 0;
//        int visY = 0;
        if (isVisible_Widget(w)) {
            visX = left_Rect(bounds_Widget(w)) - left_Rect(w->root->widget->rect);
//            visY = top_Rect(bounds_Widget(w)) - top_Rect(w->root->widget->rect);
        }
        const iBool isHiding = isVisible_Widget(w);
        if (!isHiding && !isMobile_Platform()) {
            setFocus_Widget(as_Widget(list_SidebarWidget(d)));
        }
        else {
            setFocus_Widget(NULL);
        }
        setFlags_Widget(w, hidden_WidgetFlag, isHiding);
        /* Safe area inset for mobile. */
        const int safePad =
            deviceType_App() == desktop_AppDeviceType
                ? 0
                : (d->side == left_SidebarSide ? left_Rect(safeRect_Root(w->root)) : 0);
        const int animFlags = easeOut_AnimFlag | softer_AnimFlag;
        if (!isPortraitPhone_App()) {
            if (!isHiding) {
                setFlags_Widget(w, keepOnTop_WidgetFlag, iFalse);
                w->rect.size.x = d->widthAsGaps * gap_UI;
                invalidate_ListWidget(d->list);
                if (isAnimated) {
                    setFlags_Widget(w, horizontalOffset_WidgetFlag, iTrue);
                    setVisualOffset_Widget(w,
                                           (d->side == left_SidebarSide ? -1 : 1) *
                                               (w->rect.size.x + safePad),
                                           0,
                                           0);
                    setVisualOffset_Widget(w, 0, 300, animFlags);
                }
            }
            else if (isAnimated) {
                setFlags_Widget(w, horizontalOffset_WidgetFlag, iTrue);
                if (d->side == right_SidebarSide) {
                    setVisualOffset_Widget(w, visX, 0, 0);
                    setVisualOffset_Widget(w, visX + w->rect.size.x + safePad, 300, animFlags);
                }
                else {
                    setFlags_Widget(w, keepOnTop_WidgetFlag, iTrue);
                    setVisualOffset_Widget(w, -w->rect.size.x - safePad, 300, animFlags);
                }
            }
            setScrollMode_ListWidget(d->list, normal_ScrollMode);
        }
        else {
            /* Portrait phone sidebar works differently: it slides up from the bottom. */
            setFlags_Widget(w, horizontalOffset_WidgetFlag, iFalse);
            if (!isHiding) {
                invalidate_ListWidget(d->list);
                w->rect.pos.y = height_Rect(safeRect_Root(w->root)) - d->midHeight;
                setVisualOffset_Widget(w, bottom_Rect(rect_Root(w->root)) - w->rect.pos.y, 0, 0);
                setVisualOffset_Widget(w, 0, 300, animFlags);
                // animateSlidingSheetHeight_SidebarWidget_(d);
                setScrollMode_ListWidget(d->list, disabledAtTopBothDirections_ScrollMode);
            }
            else {
                setVisualOffset_Widget(
                    w, bottom_Rect(rect_Root(w->root)) - w->rect.pos.y, 300, animFlags);
                if (d->isEditing) {
                    setMobileEditMode_SidebarWidget_(d, iFalse);
                }
            }
            showToolbar_Root(w->root, isHiding);
        }
        updateToolbarColors_Root(w->root);
        arrange_Widget(w->parent);
        /* BUG: Rearranging because the arrange above didn't fully resolve the height. */
        arrange_Widget(w);
        updateSize_DocumentWidget(document_App());
        if (isVisible_Widget(w)) {
            updateItems_SidebarWidget_(d);
            scrollOffset_ListWidget(d->list, 0);
        }
        if (isDesktop_Platform() && prefs_App()->evenSplit) {
            resizeSplits_MainWindow(as_MainWindow(window_Widget(d)), iTrue);
        }
        refresh_Widget(w->parent);
        return iTrue;
    }
    else if (equal_Command(cmd, "bookmarks.edit")) {
        setMobileEditMode_SidebarWidget_(d, !d->isEditing);
        invalidate_ListWidget(d->list);
    }
    return iFalse;
}

static void bookmarkMoved_SidebarWidget_(iSidebarWidget *d, size_t index, size_t dstIndex,
                                         iBool isBefore) {
    const iSidebarItem *movingItem = item_ListWidget(d->list, index);
    const iBool         isLast     = (dstIndex == numItems_ListWidget(d->list));
    const iSidebarItem *dstItem    = item_ListWidget(d->list,
                                                     isLast ? numItems_ListWidget(d->list) - 1
                                                            : dstIndex);
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
    postCommandf_App("bookmarks.changed nosidebar:%p", d); /* skip this sidebar since we updated already */
}

static void bookmarkMovedOntoFolder_SidebarWidget_(iSidebarWidget *d, size_t index,
                                                   size_t folderIndex) {
    const iSidebarItem *movingItem = item_ListWidget(d->list, index);
    const iSidebarItem *dstItem    = item_ListWidget(d->list, folderIndex);
    iBookmark *bm = get_Bookmarks(bookmarks_App(), movingItem->id);
    bm->parentId = dstItem->id;
    postCommand_App("bookmarks.changed");
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

static iRangei SlidingSheetMiddleRegion_SidebarWidget_(const iSidebarWidget *d) {
    const iWidget *w = constAs_Widget(d);
    const iRect safeRect = safeRect_Root(w->root);
    const int midY = bottom_Rect(safeRect) - d->midHeight;
    const int topHalf = (top_Rect(safeRect) + midY) / 2;
    const int bottomHalf = (bottom_Rect(safeRect) + midY * 2) / 3;
    return (iRangei){ topHalf, bottomHalf };
}

static void gotoNearestSlidingSheetPos_SidebarWidget_(iSidebarWidget *d) {
    const iRangei midRegion = SlidingSheetMiddleRegion_SidebarWidget_(d);
    const int pos = top_Rect(d->widget.rect);
    setSlidingSheetPos_SidebarWidget_(d, pos < midRegion.start
                                      ? top_SlidingSheetPos
                                      : pos > midRegion.end ? bottom_SlidingSheetPos
                                                            : middle_SlidingSheetPos);
}

static iBool processEvent_SidebarWidget_(iSidebarWidget *d, const SDL_Event *ev) {
    iWidget *w = as_Widget(d);
    /* Handle commands. */
    if (isResize_UserEvent(ev)) {
        checkModeButtonLayout_SidebarWidget_(d);
        if (deviceType_App() == phone_AppDeviceType) {
            setPadding_Widget(d->actions, 0, 0, 0, 0);
            setFlags_Widget(findChild_Widget(w, "sidebar.title"), hidden_WidgetFlag, isLandscape_App());
            setFlags_Widget(findChild_Widget(w, "sidebar.close"), hidden_WidgetFlag, isLandscape_App());
            /* In landscape, visibility of the toolbar is controlled separately. */
            if (isVisible_Widget(w)) {
                postCommand_Widget(w, "sidebar.toggle");
            }
            setFlags_Widget(findChild_Widget(w, "buttons"),
                            drawBackgroundToHorizontalSafeArea_WidgetFlag,
                            isLandscape_App());
            setFlags_Widget(findChild_Widget(w, "actions"),
                            drawBackgroundToHorizontalSafeArea_WidgetFlag,
                            isLandscape_App());
            setFlags_Widget(as_Widget(d->list),
                            drawBackgroundToHorizontalSafeArea_WidgetFlag,
                            isLandscape_App());
            setFlags_Widget(w,
                            drawBackgroundToBottom_WidgetFlag,
                            isPortrait_App());
            setBackgroundColor_Widget(w, isPortrait_App() ? uiBackgroundSidebar_ColorId : none_ColorId);
        }
        /* Padding under the action bar depends on whether there are other UI elements next
           to the bottom of the window. This is surprisingly convoluted; perhaps there is a
           better way to handle this? (Some sort of intelligent padding widget at the bottom
           of the sidebar? Root should just use safe insets as the padding? In that case,
           individual widgets still need to be able to extend into the safe area.) */
        if (deviceType_App() == desktop_AppDeviceType) {
            setPadding_Widget(d->actions, 0, 0, 0, 0);
        }
        else if (deviceType_App() == tablet_AppDeviceType) {
            setPadding_Widget(d->actions, 0, 0, 0,
                              prefs_App()->bottomNavBar ? 0 : bottomSafeInset_Mobile());
        }
        else if (deviceType_App() == phone_AppDeviceType) {
            if (isPortrait_App()) {
                /* In sliding sheet mode, sidebar is resized to fit in the safe area. */
                setPadding_Widget(d->actions, 0, 0, 0, 0);
            }
            else if (!prefs_App()->bottomNavBar) {
                setPadding_Widget(d->actions, 0, 0, 0, bottomSafeInset_Mobile());
            }
            else {
                setPadding_Widget(d->actions, 0, 0, 0,
                                  (prefs_App()->bottomNavBar && !prefs_App()->hideToolbarOnScroll
                                       ? height_Widget(findChild_Widget(root_Widget(w), "navbar"))
                                       : 0) +
                                      bottomSafeInset_Mobile());
            }
        }
        return iFalse;
    }
    else if (isMetricsChange_UserEvent(ev)) {
        w->rect.size.x = d->widthAsGaps * gap_UI;
        updateMetrics_SidebarWidget_(d);
        arrange_Widget(w);
        checkModeButtonLayout_SidebarWidget_(d);
    }
    else if (isCommand_SDLEvent(ev)) {
        const char *cmd = command_UserEvent(ev);
        if ((equal_Command(cmd, "tabs.changed") &&
             startsWith_Rangecc(range_Command(cmd, "id"), "doc")) ||
            equal_Command(cmd, "document.changed")) {
            updateItems_SidebarWidget_(d);
            scrollOffset_ListWidget(d->list, 0);
        }
        else if (equal_Command(cmd, "sidebar.update")) {
            d->numUnreadEntries = numUnread_Feeds();
            checkModeButtonLayout_SidebarWidget_(d);
            updateItems_SidebarWidget_(d);
        }
        else if (equal_Command(cmd, "visited.changed")) {
            d->numUnreadEntries = numUnread_Feeds();
            checkModeButtonLayout_SidebarWidget_(d);
            if (d->mode == history_SidebarMode || d->mode == feeds_SidebarMode) {
                updateItems_SidebarWidget_(d);
            }
        }
        else if (equal_Command(cmd, "bookmarks.changed") && (d->mode == bookmarks_SidebarMode ||
                                                             d->mode == feeds_SidebarMode)) {
            if (pointerLabel_Command(cmd, "nosidebar") != d) {
                updateItems_SidebarWidget_(d);
                if (hasLabel_Command(cmd, "added")) {
                    const size_t addedId    = argLabel_Command(cmd, "added");
                    const size_t addedIndex = findItem_SidebarWidget_(d, addedId);
                    scrollToItem_ListWidget(d->list, addedIndex, 200);
                }
            }
        }
        else if (equal_Command(cmd, "idents.changed") && d->mode == identities_SidebarMode) {
            updateItems_SidebarWidget_(d);
            return iTrue;
        }
        else if (isPortraitPhone_App() && isVisible_Widget(w) && d->side == left_SidebarSide &&
                 equal_Command(cmd, "swipe.forward")) {
            postCommand_App("sidebar.toggle");
            return iTrue;
        }
        else if (startsWith_CStr(cmd, cstr_String(&d->cmdPrefix))) {
            if (handleSidebarCommand_SidebarWidget_(d, cmd + size_String(&d->cmdPrefix))) {
                return iTrue;
            }
        }
        else if (equal_Command(cmd, "menu.closed") && d->menu == pointer_Command(cmd)) {
            setFocus_Widget(as_Widget(d->list));
            return iFalse;
        }
        else if (isCommand_Widget(w, ev, "mouse.clicked")) {
            if (argLabel_Command(cmd, "button") == SDL_BUTTON_LEFT) {
                if (arg_Command(cmd)) {
                    setFlags_Widget(d->resizer, pressed_WidgetFlag, iTrue);
                    setBackgroundColor_Widget(d->resizer, uiBackgroundFramelessHover_ColorId);
                    setMouseGrab_Widget(d->resizer);
                    refresh_Widget(d->resizer);
                }
                else {
                    setFlags_Widget(d->resizer, pressed_WidgetFlag, iFalse);
                    setBackgroundColor_Widget(d->resizer, none_ColorId);
                    setMouseGrab_Widget(NULL);
                    /* Final size update in case it was resized. */
                    updateSize_DocumentWidget(document_App());
                    resizeSplits_MainWindow(as_MainWindow(window_Widget(d)), iTrue);
                    refresh_Widget(d->resizer);
                }
            }
            return iTrue;
        }
        else if (isCommand_Widget(w, ev, "mouse.moved")) {
            if (isResizing_SidebarWidget_(d)) {
                const iInt2 inner = windowToInner_Widget(w, coord_Command(cmd));
                const int resMid = d->resizer->rect.size.x / 2;
                setWidth_SidebarWidget(
                    d,
                    ((d->side == left_SidebarSide
                         ? inner.x
                          : (right_Rect(rect_Root(w->root)) - coord_Command(cmd).x)) +
                     resMid) / (float) gap_UI);
                resizeSplits_MainWindow(as_MainWindow(window_Widget(d)), iFalse);
            }
            return iTrue;
        }
        else if (isCommand_Widget(w, ev, "list.clicked")) {
            itemClicked_SidebarWidget_(d,
                                       pointerLabel_Command(cmd, "item"),
                                       argU32Label_Command(cmd, "arg"),
                                       argLabel_Command(cmd, "button"));
            return iTrue;
        }
        else if (isCommand_Widget(w, ev, "list.dragged")) {
            iAssert(d->mode == bookmarks_SidebarMode);
            if (hasLabel_Command(cmd, "onto")) {
                /* Dragged onto a folder. */
                bookmarkMovedOntoFolder_SidebarWidget_(d,
                                                       argU32Label_Command(cmd, "arg"),
                                                       argU32Label_Command(cmd, "onto"));
            }
            else {
                const iBool isBefore = hasLabel_Command(cmd, "before");
                bookmarkMoved_SidebarWidget_(d,
                                             argU32Label_Command(cmd, "arg"),
                                             argU32Label_Command(cmd, isBefore ? "before" : "after"),
                                             isBefore);
            }
            return iTrue;
        }
        else if (isCommand_Widget(w, ev, "bookmark.open")) {
            const iSidebarItem *item = d->contextItem;
            if (d->mode == bookmarks_SidebarMode && item) {
                postCommandf_App("open newtab:%d newwindow:%d url:%s",
                                 argLabel_Command(cmd, "newtab"),
                                 argLabel_Command(cmd, "newwindow"),
                                 cstr_String(&item->url));
            }
            return iTrue;
        }
        else if (isCommand_Widget(w, ev, "bookmark.copy")) {
            const iSidebarItem *item = d->contextItem;
            if (d->mode == bookmarks_SidebarMode && item) {
                SDL_SetClipboardText(cstr_String(canonicalUrl_String(&item->url)));
            }
            return iTrue;
        }
        else if (isCommand_Widget(w, ev, "bookmark.edit")) {
            const iSidebarItem *item = d->contextItem;
            const int argId = argLabel_Command(cmd, "id");
            if ((d->mode == bookmarks_SidebarMode && item) || argId) {
                iBookmark *bm  = get_Bookmarks(bookmarks_App(), argId ? argId : item->id);
                const char *dlgId = format_CStr("bmed.%u", id_Bookmark(bm));
                if (findWidget_Root(dlgId)) {
                    return iTrue;
                }
                iWidget *dlg = makeBookmarkEditor_Widget(isFolder_Bookmark(bm), iTrue);
                setId_Widget(dlg, dlgId);
                setText_InputWidget(findChild_Widget(dlg, "bmed.title"), &bm->title);
                if (!isFolder_Bookmark(bm)) {
                    iInputWidget *urlInput        = findChild_Widget(dlg, "bmed.url");
                    iInputWidget *tagsInput       = findChild_Widget(dlg, "bmed.tags");
                    iInputWidget *iconInput       = findChild_Widget(dlg, "bmed.icon");
                    iWidget *     homeTag         = findChild_Widget(dlg, "bmed.tag.home");
                    iWidget *     remoteSourceTag = findChild_Widget(dlg, "bmed.tag.remote");
                    iWidget *     linkSplitTag    = findChild_Widget(dlg, "bmed.tag.linksplit");
                    setText_InputWidget(urlInput, &bm->url);
                    setText_InputWidget(tagsInput, &bm->tags);
                    if (bm->flags & userIcon_BookmarkFlag) {
                        setText_InputWidget(iconInput,
                                            collect_String(newUnicodeN_String(&bm->icon, 1)));
                    }
                    setToggle_Widget(homeTag, bm->flags & homepage_BookmarkFlag);
                    setToggle_Widget(remoteSourceTag, bm->flags & remoteSource_BookmarkFlag);
                    setToggle_Widget(linkSplitTag, bm->flags & linkSplit_BookmarkFlag);
                }
                setBookmarkEditorParentFolder_Widget(dlg, bm ? bm->parentId : 0);
                setCommandHandler_Widget(dlg, handleBookmarkEditorCommands_SidebarWidget_);
                postCommand_Root(dlg->root, "focus.set id:bmed.title");
            }
            return iTrue;
        }
        else if (isCommand_Widget(w, ev, "bookmark.dup")) {
            const iSidebarItem *item = d->contextItem;
            if (d->mode == bookmarks_SidebarMode && item) {
                iBookmark *bm = get_Bookmarks(bookmarks_App(), item->id);
                const iBool isRemote = (bm->flags & remote_BookmarkFlag) != 0;
                iChar icon = isRemote ? 0x1f588 : bm->icon;
                iWidget *dlg = makeBookmarkCreation_Widget(&bm->url, &bm->title, icon);
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
                postCommand_App("bookmarks.changed");
            }
            return iTrue;
        }
        else if (isCommand_Widget(w, ev, "bookmark.delete")) {
            const iSidebarItem *item = d->contextItem;
            if (d->mode == bookmarks_SidebarMode && item) {
                iBookmark *bm = get_Bookmarks(bookmarks_App(), item->id);
                if (isFolder_Bookmark(bm)) {
                    const iPtrArray *list = list_Bookmarks(bookmarks_App(), NULL,
                                                           filterInsideFolder_Bookmark, bm);
                    /* Folder deletion requires confirmation because folders can contain
                       any number of bookmarks and other folders. */
                    if (argLabel_Command(cmd, "confirmed") || isEmpty_PtrArray(list)) {
                        iConstForEach(PtrArray, i, list) {
                            removeEntries_Feeds(id_Bookmark(i.ptr));
                        }
                        remove_Bookmarks(bookmarks_App(), item->id);
                        postCommand_App("bookmarks.changed");
                    }
                    else {
                        const size_t numBookmarks = numBookmarks_(list);
                        makeQuestion_Widget(uiHeading_ColorEscape "${heading.confirm.bookmarks.delete}",
                                            formatCStrs_Lang("dlg.confirm.bookmarks.delete.n", numBookmarks),
                                            (iMenuItem[]){
                            { "${cancel}" },
                            { format_CStr(uiTextCaution_ColorEscape "%s",
                                          formatCStrs_Lang("dlg.bookmarks.delete.n", numBookmarks)),
                                          0, 0, format_CStr("!bookmark.delete confirmed:1 ptr:%p", d) },
                        }, 2);
                    }
                }
                else {
                    /* TODO: Move it to a Trash folder? */
                    if (remove_Bookmarks(bookmarks_App(), item->id)) {
                        removeEntries_Feeds(item->id);
                        postCommand_App("bookmarks.changed");
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
                                 : item->listItem.isDropTarget
                                     ? item->id
                                     : get_Bookmarks(bookmarks_App(), item->id)->parentId);
            }
            return iTrue;
        }
        else if (isCommand_Widget(w, ev, "bookmark.sortfolder")) {
            const iSidebarItem *item = d->contextItem;
            if (d->mode == bookmarks_SidebarMode && item) {
                postCommandf_App("bookmarks.sort arg:%zu",
                                 item->listItem.isDropTarget
                                     ? item->id
                                     : get_Bookmarks(bookmarks_App(), item->id)->parentId);
            }
            return iTrue;
        }
        else if (equal_Command(cmd, "feeds.update.finished")) {
            d->numUnreadEntries = argLabel_Command(cmd, "unread");
            checkModeButtonLayout_SidebarWidget_(d);
            if (d->mode == feeds_SidebarMode) {
                updateItems_SidebarWidget_(d);
            }
        }
        else if (equalWidget_Command(cmd, w, "feeds.mode")) {
            d->feedsMode = arg_Command(cmd);
            updateItemsWithFlags_SidebarWidget_(d, iTrue);
            return iTrue;
        }
        else if (equal_Command(cmd, "feeds.markallread") && d->mode == feeds_SidebarMode) {
            if (argLabel_Command(cmd, "confirm")) {
                /* This is used on mobile. */
                iWidget *menu = makeMenu_Widget(w->root->widget, (iMenuItem[]){
                    check_Icon " " uiTextCaution_ColorEscape "${feeds.markallread}", 0, 0,
                    "feeds.markallread"
                }, 1);
                openMenu_Widget(menu, topLeft_Rect(bounds_Widget(d->actions)));
                return iTrue;
            }
            iConstForEach(PtrArray, i, listFeedEntries_SidebarWidget_(d)) {
                const iFeedEntry *entry = i.ptr;
                markEntryAsRead_Feeds(entry->bookmarkId, &entry->url, iTrue);
            }
            postCommand_App("visited.changed");
            return iTrue;
        }
        else if (startsWith_CStr(cmd, "feed.entry.") && d->mode == feeds_SidebarMode) {
            const iSidebarItem *item = d->contextItem;
            if (item) {
                if (isCommand_Widget(w, ev, "feed.entry.open")) {
                    const char *cmd = command_UserEvent(ev);
                    postCommandString_Root(
                        get_Root(),
                        feedEntryOpenCommand_String(&item->url,
                                                    argLabel_Command(cmd, "newtab"),
                                                    argLabel_Command(cmd, "newwindow")));
                    return iTrue;
                }
                else if (isCommand_Widget(w, ev, "feed.entry.copy")) {
                    SDL_SetClipboardText(cstr_String(&item->url));
                    return iTrue;
                }
                else if (isCommand_Widget(w, ev, "feed.entry.toggleread")) {
                    const iString *url = urlFragmentStripped_String(&item->url);
                    markEntryAsRead_Feeds(
                        item->id, &item->url, isUnreadEntry_Feeds(item->id, &item->url));
                    postCommand_App("visited.changed");
                    return iTrue;
                }
                else if (isCommand_Widget(w, ev, "feed.entry.markread")) {
                    iBool isBelow = iFalse;
                    const iBool markingBelow = argLabel_Command(command_UserEvent(ev), "below") != 0;
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
                    postCommand_App("visited.changed");                    
                    return iTrue;
                }
                else if (isCommand_Widget(w, ev, "feed.entry.bookmark")) {
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
                        iWidget *dlg = makeFeedSettings_Widget(id_Bookmark(feedBookmark));
                        return iTrue;
                    }
                    if (isCommand_Widget(w, ev, "feed.entry.unsubscribe")) {
                        if (arg_Command(cmd)) {
                            feedBookmark->flags &= ~subscribed_BookmarkFlag;
                            removeEntries_Feeds(id_Bookmark(feedBookmark));
                            updateItems_SidebarWidget_(d);
                        }
                        else {
                            makeQuestion_Widget(
                                uiTextCaution_ColorEscape "${heading.unsub}",
                                format_CStr(cstr_Lang("dlg.confirm.unsub"),
                                            cstr_String(&feedBookmark->title)),
                                (iMenuItem[]){
                                    { "${cancel}", 0, 0, NULL },
                                    { uiTextCaution_ColorEscape "${dlg.unsub}",
                                      0,
                                      0,
                                      format_CStr("!feed.entry.unsubscribe arg:1 ptr:%p", d) } },
                                2);
                        }
                        return iTrue;
                    }
                }
            }
        }
        else if (isCommand_Widget(w, ev, "history.delete")) {
            if (d->contextItem && !isEmpty_String(&d->contextItem->url)) {
                removeUrl_Visited(visited_App(), &d->contextItem->url);
                updateItems_SidebarWidget_(d);
                scrollOffset_ListWidget(d->list, 0);
            }
            return iTrue;
        }
        else if (isCommand_Widget(w, ev, "history.open")) {
            const iSidebarItem *item = d->contextItem;
            if (item && !isEmpty_String(&item->url)) {
                const char *cmd = command_UserEvent(ev);
                postCommand_Widget(d,
                                   "!open newtab:%d newwindow:%d url:%s",
                                   argLabel_Command(cmd, "newtab"),
                                   argLabel_Command(cmd, "newwindow"),
                                   cstr_String(&item->url));
            }
            return iTrue;
        }
        else if (isCommand_Widget(w, ev, "history.copy")) {
            const iSidebarItem *item = d->contextItem;
            if (item && !isEmpty_String(&item->url)) {
                SDL_SetClipboardText(cstr_String(canonicalUrl_String(&item->url)));
            }
            return iTrue;
        }
        else if (isCommand_Widget(w, ev, "history.addbookmark")) {
            const iSidebarItem *item = d->contextItem;
            if (!isEmpty_String(&item->url)) {
                makeBookmarkCreation_Widget(
                    &item->url,
                    collect_String(newRange_String(urlHost_String(&item->url))),
                    0x1f310 /* globe */);
                if (deviceType_App() == desktop_AppDeviceType) {
                    postCommand_App("focus.set id:bmed.title");
                }
            }
        }
        else if (equal_Command(cmd, "history.clear")) {
            if (argLabel_Command(cmd, "confirm")) {
                makeQuestion_Widget(uiTextCaution_ColorEscape "${heading.history.clear}",
                                    "${dlg.confirm.history.clear}",
                                    (iMenuItem[]){ { "${cancel}", 0, 0, NULL },
                                                   { uiTextCaution_ColorEscape "${dlg.history.clear}",
                                                     0, 0, "history.clear confirm:0" } },
                                    2);
            }
            else {
                clear_Visited(visited_App());
                updateItems_SidebarWidget_(d);
                scrollOffset_ListWidget(d->list, 0);
            }
            return iTrue;
        }
#if defined (iPlatformTerminal)
        else if (equal_Command(cmd, "zoom.set") && isVisible_Widget(w)) {
            setWidth_SidebarWidget(d, 35.0f * arg_Command(cmd) / 100.0f);
            invalidate_ListWidget(list_SidebarWidget_(d));
            return iTrue;
        }
        else if (equal_Command(cmd, "zoom.delta") && isVisible_Widget(w)) {
            setWidth_SidebarWidget(d, d->widthAsGaps + arg_Command(cmd) * 2 / 10);
//            invalidate_ListWidget(list_SidebarWidget_(d));
            refresh_Widget(d);
            return iTrue;
        }
        else if (equal_Command(cmd, "zoom.delta") && !isVisible_Widget(w) &&
                 d->side == left_SidebarSide) {
            postCommand_Widget(w, "sidebar.toggle");
            return iTrue;
        }
#endif        
    }
    if (ev->type == SDL_MOUSEMOTION &&
        (!isVisible_Widget(d->menu) && !isVisible_Widget(d->modeMenu))) {
        const iInt2 mouse = init_I2(ev->motion.x, ev->motion.y);
        if (contains_Widget(d->resizer, mouse)) {
            setCursor_Window(get_Window(), SDL_SYSTEM_CURSOR_SIZEWE);
        }
        /* Update cursor. */
        else if (contains_Widget(w, mouse)) {
            const iSidebarItem *item = constHoverItem_ListWidget(d->list);
                setCursor_Window(get_Window(),
                             item ? (item->listItem.isSeparator ? SDL_SYSTEM_CURSOR_ARROW
                                                                : SDL_SYSTEM_CURSOR_HAND)
                                  : SDL_SYSTEM_CURSOR_ARROW);
        }
        if (d->contextIndex != iInvalidPos) {
            invalidateItem_ListWidget(d->list, d->contextIndex);
            d->contextIndex = iInvalidPos;
        }
    }
    /* Update context menu items. */
    if (d->menu && ev->type == SDL_MOUSEBUTTONDOWN) {
        if (isSlidingSheet_SidebarWidget_(d) &&
            ev->button.button == SDL_BUTTON_LEFT &&
            isVisible_Widget(d) &&
            !contains_Widget(w, init_I2(ev->button.x, ev->button.y))) {
            setSlidingSheetPos_SidebarWidget_(d, bottom_SlidingSheetPos);
            return iTrue;
        }
        if (ev->button.button == SDL_BUTTON_RIGHT) {
            d->contextItem = NULL;
            if (!isVisible_Widget(d->menu) && !isEmulatedMouseDevice_UserEvent(ev)) {
                updateMouseHover_ListWidget(d->list);
            }
            if (constHoverItem_ListWidget(d->list) || isVisible_Widget(d->menu)) {
                d->contextItem = hoverItem_ListWidget(d->list);
                /* Context is drawn in hover state. */
                if (d->contextIndex != iInvalidPos) {
                    invalidateItem_ListWidget(d->list, d->contextIndex);
                }
                d->contextIndex = hoverItemIndex_ListWidget(d->list);
                /* TODO: Some callback-based mechanism would be nice for updating menus right
                   before they open? At least move these to `updateContextMenu_ */
                if (d->mode == bookmarks_SidebarMode && d->contextItem) {
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
                else if (d->mode == feeds_SidebarMode && d->contextItem) {
                    const iBool isRead = d->contextItem->indent == 0;
                    setMenuItemLabel_Widget(d->menu,
                                            "feed.entry.toggleread",
                                            isRead ? circle_Icon " ${feeds.entry.markunread}"
                                                   : circleWhite_Icon " ${feeds.entry.markread}",
                                            0);
                }
            }
        }
    }
    if (ev->type == SDL_KEYDOWN) {
        const int key   = ev->key.keysym.sym;
        const int kmods = keyMods_Sym(ev->key.keysym.mod);
        /* Hide the sidebar when Escape is pressed. */
        if (kmods == 0 && key == SDLK_ESCAPE && isVisible_Widget(d)) {
            postCommand_Widget(d, "%s.toggle", cstr_String(id_Widget(w)));
            return iTrue;
        }
    }
    if (isSlidingSheet_SidebarWidget_(d)) {
        if (ev->type == SDL_MOUSEWHEEL) {
            enum iWidgetTouchMode touchMode = widgetMode_Touch(w);
            if (touchMode == momentum_WidgetTouchMode) {
                /* We don't do momentum. */
                float swipe = stopWidgetMomentum_Touch(w) / gap_UI;
//                printf("swipe: %f\n", swipe);
                const iRangei midRegion = SlidingSheetMiddleRegion_SidebarWidget_(d);
                const int pos = top_Rect(w->rect);
                if (swipe < 170) {
                    gotoNearestSlidingSheetPos_SidebarWidget_(d);
                }
                else if (swipe > 500 && ev->wheel.y > 0) {
                    /* Fast swipe down will dismiss. */
                    setSlidingSheetPos_SidebarWidget_(d, bottom_SlidingSheetPos);
                }
                else if (ev->wheel.y < 0) {
                    setSlidingSheetPos_SidebarWidget_(d, top_SlidingSheetPos);
                }
                else if (pos < (midRegion.start + midRegion.end) / 2) {
                    setSlidingSheetPos_SidebarWidget_(d, middle_SlidingSheetPos);
                }
                else {
                    setSlidingSheetPos_SidebarWidget_(d, bottom_SlidingSheetPos);
                }
            }
            else if (touchMode == touch_WidgetTouchMode) {
                /* Move with the finger. */
                adjustEdges_Rect(&w->rect, ev->wheel.y, 0, 0, 0);
                /* Upon reaching the top, scrolling is switched back to the list. */
                const iRect rootRect = safeRect_Root(w->root);
                const int top = top_Rect(rootRect);
                if (w->rect.pos.y < top) {
                    setScrollMode_ListWidget(d->list, disabledAtTopUpwards_ScrollMode);
                    setScrollPos_ListWidget(d->list, top - w->rect.pos.y);
                    transferAffinity_Touch(w, as_Widget(d->list));
                    w->rect.pos.y = top;
                    w->rect.size.y = height_Rect(rootRect);
                }
                else {
                    setScrollMode_ListWidget(d->list, disabled_ScrollMode);
                }
                arrange_Widget(w);
                refresh_Widget(w);
            }
            else {
                return iFalse;
            }
            return iTrue;
        }
        if (ev->type == SDL_USEREVENT && ev->user.code == widgetTouchEnds_UserEventCode) {
            gotoNearestSlidingSheetPos_SidebarWidget_(d);
            return iTrue;
        }
    }
    if (ev->type == SDL_MOUSEBUTTONDOWN &&
        contains_Widget(as_Widget(d->list), init_I2(ev->button.x, ev->button.y))) {
        if (hoverItem_ListWidget(d->list) || isVisible_Widget(d->menu)) {
            /* Update the menu before opening. */
            /* TODO: This kind of updating is already done above, and in `updateContextMenu_`... */
            if (d->mode == bookmarks_SidebarMode && !isVisible_Widget(d->menu)) {
                /* Remote bookmarks have limitations. */
                const iSidebarItem *hoverItem = hoverItem_ListWidget(d->list);
                iAssert(hoverItem);
                const iBookmark *  bm              = get_Bookmarks(bookmarks_App(), hoverItem->id);
                const iBool        isRemote        = (bm->flags & remote_BookmarkFlag) != 0;
                static const char *localOnlyCmds[] = { "bookmark.edit",
                                                       "bookmark.delete",
                                                       "bookmark.tag tag:subscribed",
                                                       "bookmark.tag tag:homepage",
                                                       "bookmark.tag tag:remotesource" };
                iForIndices(i, localOnlyCmds) {
                    setFlags_Widget(as_Widget(findMenuItem_Widget(d->menu, localOnlyCmds[i])),
                                    disabled_WidgetFlag,
                                    isRemote);
                }
            }
            processContextMenuEvent_Widget(d->menu, ev, {});
        }
        else if (!constHoverItem_ListWidget(d->list) || isVisible_Widget(d->modeMenu)) {
            processContextMenuEvent_Widget(d->modeMenu, ev, {});
        }
    }
    return processEvent_Widget(w, ev);
}

static void draw_SidebarWidget_(const iSidebarWidget *d) {
    const iWidget *w      = constAs_Widget(d);
    const iRect    bounds = bounds_Widget(w);
    iPaint p;
    init_Paint(&p);
    if (d->mode == documentOutline_SidebarMode) {
        makePaletteGlobal_GmDocument(document_DocumentWidget(document_App()));
    }
    if (!isPortraitPhone_App()) { /* this would erase page contents during transition on the phone */
        if (flags_Widget(w) & visualOffset_WidgetFlag &&
            flags_Widget(w) & horizontalOffset_WidgetFlag && isVisible_Widget(w)) {
            fillRect_Paint(&p, boundsWithoutVisualOffset_Widget(w), tmBackground_ColorId);
        }
    }
    draw_Widget(w);
    if (isVisible_Widget(w)) {
        drawVLine_Paint(
            &p,
            addX_I2(d->side == left_SidebarSide ? topRight_Rect(bounds) : topLeft_Rect(bounds), -1),
            height_Rect(bounds),
            uiSeparator_ColorId);
    }
}

static void draw_SidebarItem_(const iSidebarItem *d, iPaint *p, iRect itemRect,
                              const iListWidget *list) {
    const iSidebarWidget *sidebar = findParentClass_Widget(constAs_Widget(list),
                                                           &Class_SidebarWidget);
    const iBool isMenuVisible = isVisible_Widget(sidebar->menu);
    const iBool isDragging   = constDragItem_ListWidget(list) == d;
    const iBool isEditing    = sidebar->isEditing; /* only on mobile */
    const iBool isPressing   = isMouseDown_ListWidget(list) && !isDragging;
    const iBool isHover      =
            (!isMenuVisible &&
            isHover_Widget(constAs_Widget(list)) &&
            constHoverItem_ListWidget(list) == d) ||
            (isMenuVisible && sidebar->contextItem == d) ||
            (isFocused_Widget(list) && constCursorItem_ListWidget(list) == d) ||
            isDragging;
    const int scrollBarWidth = scrollBarWidth_ListWidget(list);
#if defined (iPlatformApple)
    const int blankWidth     = 0;
#else
    const int blankWidth     = scrollBarWidth;
#endif
    const int itemHeight     = height_Rect(itemRect);
    const int iconColor      = isHover ? (isPressing ? uiTextPressed_ColorId : uiIconHover_ColorId)
                                       : uiIcon_ColorId;
//    const int altIconColor   = isPressing ? uiTextPressed_ColorId : uiTextAction_ColorId;
    const int font = sidebar->itemFonts[d->isBold ? 1 : 0];
    int bg         = uiBackgroundSidebar_ColorId;
    if (isHover) {
        bg = isPressing ? uiBackgroundPressed_ColorId
                        : uiBackgroundFramelessHover_ColorId;
        fillRect_Paint(p, itemRect, bg);
    }
    else if (d->listItem.isSelected &&
             (sidebar->mode == feeds_SidebarMode || sidebar->mode == identities_SidebarMode)) {
        bg = uiBackgroundUnfocusedSelection_ColorId;
        fillRect_Paint(p, itemRect, bg);
    }
    else if (sidebar->mode == bookmarks_SidebarMode) {
        if (d->indent) /* remote icon */  {
            bg = uiBackgroundFolder_ColorId;
            fillRect_Paint(p, itemRect, bg);
        }
    }
    iInt2 pos = itemRect.pos;
    if (sidebar->mode == documentOutline_SidebarMode) {
        const int fg = isHover ? (isPressing ? uiTextPressed_ColorId : uiTextFramelessHover_ColorId)
                               : (tmHeading1_ColorId + d->indent / (4 * gap_UI));
        drawRange_Text(font,
                       init_I2(pos.x + (3 * gap_UI + d->indent) * aspect_UI,
                               mid_Rect(itemRect).y - lineHeight_Text(font) / 2),
                       fg,
                       range_String(&d->label));
    }
    else if (sidebar->mode == feeds_SidebarMode) {
        const int fg = isHover ? (isPressing ? uiTextPressed_ColorId : uiTextFramelessHover_ColorId)
                               : uiText_ColorId;
        const int iconPad = 12 * gap_UI;
        if (d->listItem.isSeparator) {
            if (d != constItem_ListWidget(list, 0)) {
                drawHLine_Paint(p,
                                addY_I2(pos, 2 * gap_UI),
                                width_Rect(itemRect) - blankWidth,
                                uiSeparator_ColorId);
            }
            drawRange_Text(
                uiLabelLargeBold_FontId,
                add_I2(pos,
                       init_I2(3 * gap_UI * aspect_UI,
                               itemHeight - lineHeight_Text(uiLabelLargeBold_FontId) - 1 * gap_UI)),
                uiIcon_ColorId,
                range_String(&d->meta));
        }
        else {
            const iBool isUnread = (d->indent != 0);
            const int titleFont = sidebar->itemFonts[isUnread ? 1 : 0];
            const int h1 = lineHeight_Text(uiLabel_FontId);
            const int h2 = lineHeight_Text(titleFont);
            iRect iconArea = { addY_I2(pos, 0), init_I2(iconPad * aspect_UI, itemHeight) };
            /* Icon. */ {
                /* TODO: Use the primary hue from the theme of this site. */
                iString str;
                initUnicodeN_String(&str, &d->icon, 1);
                /* TODO: Add to palette. */
                const int unreadIconColor = uiTextCaution_ColorId;
                const int readIconColor =
                    isDark_ColorTheme(colorTheme_App()) ? uiText_ColorId : uiAnnotation_ColorId;
                drawCentered_Text(uiLabelLarge_FontId,
                                  adjusted_Rect(iconArea, init_I2(gap_UI, 0), zero_I2()),
                                  iTrue,
                                  isHover && isPressing
                                      ? iconColor
                                      : isUnread ? unreadIconColor
                                      : d->listItem.isSelected ? iconColor
                                      : readIconColor,
                                  "%s",
                                  cstr_String(&str));
                deinit_String(&str);
            }
            /* Select the layout based on how the title fits. */
            int         metaFg    = isPressing ? fg : uiSubheading_ColorId;
            iInt2       titleSize = measureRange_Text(titleFont, range_String(&d->label)).bounds.size;
            const iInt2 metaSize  = measureRange_Text(uiLabel_FontId, range_String(&d->meta)).bounds.size;
            pos.x += iconPad * aspect_UI;
            const int avail = width_Rect(itemRect) - iconPad - 3 * gap_UI;
            const int labelFg = isPressing ? fg : (isUnread ? uiTextStrong_ColorId : uiText_ColorId);
            if (titleSize.x > avail && metaSize.x < avail * 0.75f) {
                /* Must wrap the title. */
                pos.y += (itemHeight - h2 - h2) / 2;
                draw_Text(
                    uiLabel_FontId, addY_I2(pos, h2 - h1 - gap_UI / 8), metaFg, "%s \u2014 ", cstr_String(&d->meta));
                int skip  = metaSize.x + measure_Text(uiLabel_FontId, " \u2014 ").advance.x;
                iInt2 cur = addX_I2(pos, skip);
                const char *endPos;
                tryAdvance_Text(titleFont, range_String(&d->label), avail - skip, &endPos);
                drawRange_Text(titleFont,
                               cur,
                               labelFg,
                               (iRangecc){ constBegin_String(&d->label), endPos });
                if (endPos < constEnd_String(&d->label)) {
                    drawRange_Text(titleFont,
                                   addY_I2(pos, h2), labelFg,
                                   (iRangecc){ endPos, constEnd_String(&d->label) });
                }
            }
            else {
                pos.y += (itemHeight - h1 - h2) / 2;
                drawRange_Text(uiLabel_FontId, pos, metaFg, range_String(&d->meta));
                drawRange_Text(titleFont, addY_I2(pos, h1), labelFg, range_String(&d->label));
            }
        }
    }
    else if (sidebar->mode == bookmarks_SidebarMode) {
        const int fg = isHover ? (isPressing ? uiTextPressed_ColorId : uiTextFramelessHover_ColorId)
                       : d->listItem.isDropTarget ? uiHeading_ColorId
                                                  : uiText_ColorId;
        /* The icon. */
        iString str;
        init_String(&str);
        appendChar_String(&str, d->icon ? d->icon : 0x1f588);
        const int leftIndent = d->indent * gap_UI * 4;
        const iRect iconArea = { addX_I2(pos, gap_UI + leftIndent),
                                 init_I2(!isTerminal_Platform() ? 1.75f * lineHeight_Text(font) : 5, itemHeight) };
        drawCentered_Text(font,
                          iconArea,
                          iTrue,
                          isPressing                       ? iconColor
                          : d->icon == 0x2913 /* remote */ ? uiTextCaution_ColorId
                                                           : iconColor,
                          "%s",
                          cstr_String(&str));
        deinit_String(&str);
        const iInt2 textPos = addY_I2(topRight_Rect(iconArea), (itemHeight - lineHeight_Text(font)) / 2);
        drawRange_Text(font, textPos, fg, range_String(&d->label));
        const int metaFont = uiLabel_FontId;
        const int metaIconWidth = 4.5f * gap_UI;
        if (isEditing) {
            iRect dragRect = {
                addX_I2(topRight_Rect(itemRect), -itemHeight * 3 / 2),
                init_I2(itemHeight * 3 / 2, itemHeight)
            };
            fillRect_Paint(p, dragRect, bg);
            drawVLine_Paint(p, topLeft_Rect(dragRect), height_Rect(dragRect), uiSeparator_ColorId);
            drawCentered_Text(uiContent_FontId, dragRect, iTrue, uiAnnotation_ColorId, menu_Icon);
            adjustEdges_Rect(&itemRect, 0, -width_Rect(dragRect), 0, 0);
        }
        const iInt2 metaPos =
            init_I2(right_Rect(itemRect) -
                        length_String(&d->meta) *
                            metaIconWidth
                        - 2 * gap_UI - (blankWidth ? blankWidth - 1.5f * gap_UI : (gap_UI / 2)),
                    textPos.y);
        if (!isDragging) {
            fillRect_Paint(p,
                           init_Rect(metaPos.x,
                                     top_Rect(itemRect),
                                     right_Rect(itemRect) - metaPos.x,
                                     height_Rect(itemRect)),
                           bg);
        }
        iInt2 mpos = metaPos;
        iStringConstIterator iter;
        init_StringConstIterator(&iter, &d->meta);
        iRangecc range = { cstr_String(&d->meta), iter.pos };
        while (iter.value) {
            next_StringConstIterator(&iter);
            range.end = iter.pos;
            iRect iconArea = { mpos, init_I2(metaIconWidth, lineHeight_Text(metaFont)) };
            iRect visBounds = visualBounds_Text(metaFont, range);
            drawRange_Text(metaFont,
                           sub_I2(mid_Rect(iconArea), mid_Rect(visBounds)),
                           isHover && isPressing ? fg : uiTextShortcut_ColorId,
                           range);
            mpos.x += metaIconWidth;
            range.start = range.end;            
        }        
    }
    else if (sidebar->mode == history_SidebarMode) {
        iBeginCollect();
        if (d->listItem.isSeparator) {
            if (!isEmpty_String(&d->meta)) {
                iInt2 drawPos = addY_I2(topLeft_Rect(itemRect), d->id);
                drawHLine_Paint(p,
                                addY_I2(drawPos, -gap_UI),
                                width_Rect(itemRect) - blankWidth,
                                uiSeparator_ColorId);
                drawRange_Text(
                    uiLabelLargeBold_FontId,
                    add_I2(drawPos,
                           init_I2(3 * gap_UI * aspect_UI,
                                   1 + (itemHeight - lineHeight_Text(uiLabelLargeBold_FontId)) / 2.0f)),
                    uiIcon_ColorId,
                    range_String(&d->meta));
            }
        }
        else {
            const int fg = isHover ? (isPressing ? uiTextPressed_ColorId : uiTextFramelessHover_ColorId)
                                   : uiTextDim_ColorId;
            iUrl parts;
            init_Url(&parts, &d->label);
            const iBool isAbout    = equalCase_Rangecc(parts.scheme, "about");
            const iBool isGemini   = equalCase_Rangecc(parts.scheme, "gemini");
            const iBool isData     = equalCase_Rangecc(parts.scheme, "data");
            const int   queryColor = isPressing ? uiTextPressed_ColorId
                                     : isHover  ? uiText_ColorId
                                                : uiAnnotation_ColorId;
            const iInt2 textPos =
                add_I2(topLeft_Rect(itemRect),
                       init_I2(3 * gap_UI, (itemHeight - lineHeight_Text(font)) / 2));
            if (isData) {
                drawRange_Text(
                    font, textPos, fg, range_String(prettyDataUrl_String(&d->label, queryColor)));
            }
            else {
                draw_Text(
                    font,
                    textPos,
                    fg,
                    "%s%s%s%s%s%s%s%s",
                    isGemini ? "" : cstr_Rangecc(parts.scheme),
                    isGemini  ? ""
                    : isAbout ? ":"
                              : "://",
                    escape_Color(isHover ? (isPressing ? uiTextPressed_ColorId
                                                       : uiTextFramelessHover_ColorId)
                                         : uiTextStrong_ColorId),
                    cstr_Rangecc(parts.host),
                    escape_Color(fg),
                    cstr_Rangecc(parts.path),
                    !isEmpty_Range(&parts.query) ? escape_Color(queryColor) : "",
                    !isEmpty_Range(&parts.query) ? cstr_Rangecc(parts.query) : "");
            }
        }
        iEndCollect();
    }
}

iBeginDefineSubclass(SidebarWidget, Widget)
    .processEvent = (iAny *) processEvent_SidebarWidget_,
    .draw         = (iAny *) draw_SidebarWidget_,
iEndDefineSubclass(SidebarWidget)
