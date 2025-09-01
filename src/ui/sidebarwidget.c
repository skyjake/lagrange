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
#include "bookmarks.h"
#include "certlistwidget.h"
#include "command.h"
#include "defs.h"
#include "documentwidget.h"
#include "feeds.h"
#include "gmcerts.h"
#include "gmdocument.h"
#include "gmutil.h"
#include "inputwidget.h"
#include "keys.h"
#include "labelwidget.h"
#include "listwidget.h"
#include "mobile.h"
#include "paint.h"
#include "root.h"
#include "scrollwidget.h"
#include "touch.h"
#include "util.h"
#include "visited.h"

#include <SDL_clipboard.h>
#include <SDL_mouse.h>
#include <the_Foundation/intset.h>
#include <the_Foundation/regexp.h>
#include <the_Foundation/stringarray.h>

iDeclareType(SidebarItem) typedef iListItemClass iSidebarItemClass;

struct Impl_SidebarItem {
    iListItem listItem;
    uint32_t  id;
    int       indent;
    iChar     icon;
    iBool     isBold;
    iString   label;
    iString   meta;
    iString   url;
    int       count;
    iTime     ts;
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
    d->count = 0;
    iZap(d->ts);
}

void deinit_SidebarItem(iSidebarItem *d) {
    deinit_String(&d->url);
    deinit_String(&d->meta);
    deinit_String(&d->label);
}

static void draw_SidebarItem_(const iSidebarItem *d, iPaint *p, iRect itemRect,
                              const iListWidget *list);

iBeginDefineSubclass(SidebarItem, ListItem).draw = (iAny *) draw_SidebarItem_,
                                  iEndDefineSubclass(SidebarItem)

                                      iDefineObjectConstruction(SidebarItem);

/*----------------------------------------------------------------------------------------------*/

static const char *normalModeLabels_[max_SidebarMode] = {
    book_Icon " ${sidebar.bookmarks}",
    star_Icon " ${sidebar.feeds}",
    whiteStar_Icon " ${sidebar.subscriptions}",
    person_Icon " ${sidebar.identities}",
    page_Icon " ${sidebar.outline}",
    hierarchy_Icon " ${sidebar.structure}",
    openTabBg_Icon " ${sidebar.documents}",
    clock_Icon " ${sidebar.history}",
};

static const char *tightModeLabels_[max_SidebarMode] = {
    book_Icon,
    star_Icon,
    whiteStar_Icon,
    person_Icon,
    page_Icon,
    hierarchy_Icon,
    openTabBg_Icon,
    clock_Icon,
};

struct Impl_SidebarWidget {
    iWidget           widget;
    enum iSidebarSide side;
    enum iSidebarMode mode;
    enum iFeedsMode   feedsMode;
    iString           cmdPrefix;
    iWidget          *blank;
    iListWidget      *list;
    iCertListWidget  *certList;
    iWidget          *actions;   /* below the list, area for buttons */
    int               midHeight; /* on portrait phone, the height for the middle state */
    iBool             isEditing; /* mobile edit mode */
    int               modeScroll[max_SidebarMode];
    iLabelWidget     *modeButtons[max_SidebarMode];
    iLabelWidget     *firstVisibleModeButton;
    int               maxButtonLabelWidth;
    float             widthAsGaps;
    int               buttonFont;
    int               itemFonts[2];
    size_t            numUnreadEntries;
    iWidget          *resizer;
    iWidget          *menu;          /* context menu for an item */
    iWidget          *modeMenu;      /* context menu for the sidebar mode (no item) */
    iWidget          *folderMenu;    /* context menu for bookmark folders */
    iSidebarItem     *contextItem;   /* list item accessed in the context menu */
    size_t            contextIndex;  /* index of list item accessed in the context menu */
    iIntSet          *closedFolders; /* otherwise open */
    iString           bookmarkFilter;
    iStringSet       *structureUrls;
    iString           structureHost;
    iStringSet       *structureUnfolds;
};

iDefineObjectConstructionArgs(SidebarWidget, (enum iSidebarSide side), side);

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

static iBool hasUrlPrefix_Bookmark_(void *context, const iBookmark *bm) {
    return startsWith_String(&bm->url, cstr_String(context));
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
    iLabelWidget *btn =
        addChildFlags_Widget(d->actions, iClob(new_LabelWidget(label, command)), flags);
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

static iBool isEdgeSwipable_SidebarWidget_(const iSidebarWidget *d) {
    return prefs_App()->edgeSwipe &&
           (deviceType_App() == tablet_AppDeviceType || isLandscapePhone_App());
}

static void setMobileEditMode_SidebarWidget_(iSidebarWidget *d, iBool editing) {
    iWidget *w   = as_Widget(d);
    d->isEditing = editing;
    if (d->actions) {
        setFlags_Widget(findChild_Widget(w, "sidebar.close"), hidden_WidgetFlag, editing);
        setFlags_Widget(child_Widget(d->actions, 0), hidden_WidgetFlag, !editing);
        setTextCStr_LabelWidget(child_Widget(as_Widget(d->actions), 2),
                                editing ? "${sidebar.close}" : "${sidebar.action.bookmarks.edit}");
        setDragHandleWidth_ListWidget(d->list,
                                      editing ? itemHeight_ListWidget(d->list) * 3 / 2 : 0);
        arrange_Widget(d->actions);
    }
}

static const iPtrArray *listFeedEntries_SidebarWidget_(const iSidebarWidget *d) {
    iUnused(d);
    /* TODO: Sort order setting? */
    return listEntries_Feeds();
}

static const iMenuItem bookmarkModeMenuItems_[] = {
    { bookmark_Icon " ${menu.page.bookmark}", SDLK_d, KMOD_PRIMARY, "bookmark.add" },
    { "---" },
    { folder_Icon " ${menu.newfolder}", 0, 0, "bookmark.addfolder" },
    { upDownArrow_Icon " ${menu.sort.alpha}", 0, 0, "bookmark.sortfolder" },
    { "---" },
    { rightAngle_Icon " ${menu.foldall}", 0, 0, "bookmark.foldall arg:1" },
    { downAngle_Icon " ${menu.unfoldall}", 0, 0, "bookmark.foldall arg:0" },
    { reload_Icon " ${bookmarks.reload}", 0, 0, "bookmarks.reload.remote" }
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

static void updateBookmarkItems_SidebarWidget_(iSidebarWidget *d) {
    iConstForEach(PtrArray, i, list_Bookmarks(bookmarks_App(), cmpTree_Bookmark, NULL, NULL)) {
        const iBookmark *bm = i.ptr;
        if (isBookmarkFolded_SidebarWidget_(d, bm)) {
            continue; /* inside a closed folder */
        }
        iSidebarItem *item = new_SidebarItem();
        setupFromBookmark_SidebarItem_(item, bm);
        item->listItem.flags.isDraggable = iTrue;
        item->isBold = item->listItem.flags.isDropTarget = isFolder_Bookmark(bm);
        if (isFolder_Bookmark(bm)) {
            item->icon = contains_IntSet(d->closedFolders, item->id) ? 0x27e9 : 0xfe40;
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
}

static iBool filterBookmark_String_(void *context, const iBookmark *bm) {
    const iString *term = context;
    size_t         pos;
    if (isFolder_Bookmark(bm)) return iFalse;
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

static iBool isSubscribed_Bookmark_(void *context, const iBookmark *bm) {
    iUnused(context);
    return (!isFolder_Bookmark(bm) && bm->flags & subscribed_BookmarkFlag) != 0;
}

static void updateFilteredBookmarkItems_SidebarWidget_(iSidebarWidget *d) {
    const iWidget *w    = constAs_Widget(d);
    iString       *term = lower_String(&d->bookmarkFilter);
    iConstForEach(
        PtrArray,
        i,
        list_Bookmarks(bookmarks_App(), cmpTitleAscending_Bookmark, filterBookmark_String_, term)) {
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

int cmpGopherStructureUrl_(const iString *a, const iString *b) {
    /* All of the URLs in the structure tree have the same scheme, host, and port. */
    const size_t start = indexOfCStrFrom_String(a, "/", 9) + 1; /* skip the "gopher://" */
    iAssert(start < size_String(a));
    iAssert(start < size_String(b));
    const int      kind1 = at_Block(&a->chars, start);
    const int      kind2 = at_Block(&b->chars, start);
    const iRangecc sel1  = rangeFrom_String(a, start + 1);
    const iRangecc sel2  = rangeFrom_String(b, start + 1);
    const int      cmp   = iCmpStr(sel1.start, sel2.start);
    if (!cmp) return kind1 - kind2;
    return cmp;
}

static iBool isGopherStructure_SidebarWidget_(const iSidebarWidget *d) {
    return equal_Rangecc(urlScheme_String(&d->structureHost), "gopher");
}

static void removeStructureUnfold_SidebarWidget_(iSidebarWidget *d, const iString *url) {
    iForEach(Array, i, &d->structureUnfolds->strings.values) {
        iString *str = i.value;
        if (startsWith_String(str, cstr_String(url))) {
            deinit_String(str);
            remove_ArrayIterator(&i);
        }
    }
}

static iBool isUnfoldedStructurePath_SidebarWidget_(const iSidebarWidget *d, const iString *url,
                                                    const iString *activeDocumentUrl) {
    iUrl parts;
    init_Url(&parts, url);
    iRangecc path = parts.path;
    if (endsWith_Rangecc(path, "/") && isEmpty_Range(&parts.query)) path.end--;
    /* We need to know the parent directory. */
    iString sub;
    init_String(&sub);
    /* We have to check that all the parent directories are also unfolded,
    or otherwise a nested child item might be visible under a folded parent. */
    for (;;) {
        const size_t slash = lastIndexOfCStr_Rangecc(path, "/");
        if (slash == iInvalidPos || slash == 0) {
            /* The full hierarchy has been checked, and nothing seems to be folded. */
            deinit_String(&sub);
            return iTrue;
        }
        path.end = path.start + slash + 1; /* keep the slash */
        setRange_String(&sub, (iRangecc) { constBegin_String(url), path.end });
        if (startsWith_String(activeDocumentUrl, cstr_String(&sub))) {
            /* Everything upwards from current document should be unfolded temporarily. */
            deinit_String(&sub);
            return iTrue;
        }
        if (!contains_StringSet(d->structureUnfolds, &sub)) {
            break;
        }
        path.end--; /* lose the slash */
    }
    deinit_String(&sub);
    return iFalse;
}

static iSidebarItem *addStructureItem_SidebarWidget_(iSidebarWidget *d, const iString *url,
                                                     const iString *label, iArray *stack,
                                                     const iString *activeDocumentUrl,
                                                     size_t        *highlightedItemPos_out) {
    const int itemIndent = (int) size_Array(stack) + 1;
    if (itemIndent == 1 || isUnfoldedStructurePath_SidebarWidget_(d, url, activeDocumentUrl)) {
        iSidebarItem *previous = backItem_ListWidget(d->list);
        if (previous->indent == itemIndent - 1) {
            /* This must be the parent item. */
            previous->id |= 2; /* show as open folder */
        }
        iSidebarItem *item = new_SidebarItem();
        set_String(&item->url, url);
        iString *dec = maybeUrlDecodeExclude_String(label, "");
        if (dec) {
            set_String(&item->label, dec);
            delete_String(dec);
        }
        else {
            set_String(&item->label, label);
        }
        item->indent = itemIndent;
        if (equal_String(&item->url, activeDocumentUrl)) {
            *highlightedItemPos_out = numItems_ListWidget(d->list);
            item->id                = iTrue; /* makes it appear highlighted */
        }
        addItem_ListWidget(d->list, item);
        iRelease(item);
        return item;
    }
    else {
        iSidebarItem *item = backItem_ListWidget(d->list);
        item->count++; /* counts as a folded child */
        return NULL;
    }
}

static void updateItemsWithFlags_SidebarWidget_(iSidebarWidget *d, iBool keepActions) {
    const iBool isMobile = (deviceType_App() != desktop_AppDeviceType);
    clear_ListWidget(d->list);
    releaseChildren_Widget(d->blank);
    if (!keepActions) {
        if (focus_Widget() && hasParent_Widget(focus_Widget(), as_Widget(d))) {
            /* Something inside this sidebar has input focus, so let it go first. */
            setFocus_Widget(NULL);
        }
        releaseChildren_Widget(d->actions);
    }
    d->actions->rect.size.y = 0;
    destroy_Widget(d->menu);
    destroy_Widget(d->modeMenu);
    destroy_Widget(d->folderMenu);
    d->menu       = NULL;
    d->modeMenu   = NULL;
    d->folderMenu = NULL;
    iBool isEmpty = iFalse; /* show blank? */
    switch (d->mode) {
        case feedEntries_SidebarMode: {
            const iString *docUrl = canonicalUrl_String(url_DocumentWidget(document_App()));
            iTime          now;
            iDate          on;
            initCurrent_Time(&now);
            init_Date(&on, &now);
            const iDate today = on;
            iZap(on);
            size_t numItems              = 0;
            isEmpty                      = iTrue;
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
                const iBool isOpen   = equal_String(docUrl, &entry->url);
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
                iSidebarItem *item        = new_SidebarItem();
                item->listItem.flags.isSelected = isOpen; /* currently being viewed */
                item->indent              = isUnread;
                set_String(&item->url, &entry->url);
                set_String(&item->label, &entry->title);
                const iBookmark *bm = get_Bookmarks(bookmarks_App(), entry->bookmarkId);
                if (bm) {
                    item->id   = entry->bookmarkId;
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
                { check_Icon " ${feeds.markallread}", SDLK_a, KMOD_SHIFT, "feeds.markallread" },
                { reload_Icon " ${feeds.refresh}", refreshFeeds_KeyShortcut, "feeds.refresh" }
            };
            d->menu     = makeMenu_Widget(as_Widget(d), menuItems, iElemCount(menuItems));
            d->modeMenu = makeMenu_Widget(
                as_Widget(d),
                (iMenuItem[]) {
                    { check_Icon " ${feeds.markallread}", SDLK_a, KMOD_SHIFT, "feeds.markallread" },
                    { reload_Icon " ${feeds.refresh}",
                      refreshFeeds_KeyShortcut,
                      "feeds.refresh" } },
                2);
            break;
        }
        case subscriptions_SidebarMode: {
            struct Impl_FeedItem {
                iHashNode     node;
                iSidebarItem *item;
            };
            iDeclareType(FeedItem);
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
                iSidebarItem     *item = ((iFeedItem *) value_Hash(&hash, entry->bookmarkId))->item;
                item->count++;
                if (!isValid_Time(&item->ts) || cmp_Time(&entry->posted, &item->ts) > 0) {
                    item->ts = entry->posted; /* latest timestamp */
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
            break;
        }
        case documentOutline_SidebarMode: {
            const iGmDocument *doc = document_DocumentWidget(document_App());
            iConstForEach(Array, i, headings_GmDocument(doc)) {
                const iGmHeading *head = i.value;
                iSidebarItem     *item = new_SidebarItem();
                item->id               = index_ArrayConstIterator(&i);
                setRange_String(&item->label, head->text);
                item->indent = head->level * 5 * gap_UI;
                item->isBold = head->level == 0;
                addItem_ListWidget(d->list, item);
                iRelease(item);
            }
            break;
        }
        case siteStructure_SidebarMode: {
            if (!isVisible_Widget(as_Widget(d))) {
                /* This is a relatively heavy operation, so don't update unless we are
                   looking at the structure tab. */
                break;
            }
            const iString *docUrl =
                urlDefaultPortStripped_String(url_DocumentWidget(document_App()));
            /* We look for the protocol in addition to the domain. */
            const iRangecc urlHost = urlHostWithPort_String(docUrl);
            if (isEmpty_Range(&urlHost)) {
                break;
            }
            const iRangecc host    = { constBegin_String(docUrl), urlHost.end };
            iString       *hostStr = collect_String(newRange_String(host));
            if (!endsWith_String(hostStr, "/")) {
                appendCStr_String(hostStr, "/");
            }
            if (isEmpty_Range(&host)) break;
            /* We keep the set of known URLs as long as the host remains the same.
               This allows accumulating entries from unvisited links on pages. */
            if (!equal_String(&d->structureHost, hostStr)) {
                set_String(&d->structureHost, hostStr);
                iRelease(d->structureUrls);
                d->structureUrls = new_StringSet();
                clear_StringSet(d->structureUnfolds);
                scrollOffset_ListWidget(d->list, 0);
            }
            const iBool isGopher = isGopherStructure_SidebarWidget_(d);
            iStringSet *urls     = d->structureUrls;
            /* Look through everything we know at the moment: visited URLs, bookmarks,
               feed entries, and links on the page. */
            iConstForEach(PtrArray, i, listMatching_Visited(visited_App(), cstr_String(hostStr))) {
                const iVisitedUrl *vis = i.ptr;
                insert_StringSet(urls, &vis->url);
            }
            const iPtrArray *bookmarks =
                list_Bookmarks(bookmarks_App(), NULL, hasUrlPrefix_Bookmark_, hostStr);
            for (init_PtrArrayConstIterator(&i, bookmarks); i.value;
                 next_PtrArrayConstIterator(&i)) {
                insert_StringSet(urls, &((const iBookmark *) i.ptr)->url);
            }
            const iGmDocument *doc = document_DocumentWidget(document_App());
            for (size_t j = 0; j < numLinks_GmDocument(doc); j++) {
                iBeginCollect();
                const iString *linkUrl = urlDefaultPortStripped_String(linkUrl_GmDocument(doc, j));
                if (startsWith_String(linkUrl, cstr_String(hostStr))) {
                    insert_StringSet(urls, linkUrl);
                }
                iEndCollect();
            }
            /* The root item is always present. */
            iSidebarItem *rootItem = new_SidebarItem();
            setRange_String(&rootItem->url, host);
            setRange_String(&rootItem->label, urlHost_String(docUrl));
            rootItem->isBold = iTrue;
            rootItem->id     = equal_String(&rootItem->url, docUrl);
            rootItem->count  = 1;
            size_t docItem   = rootItem->id ? 0 : iInvalidPos;
            addItem_ListWidget(d->list, rootItem);
            iRelease(rootItem);
            iString label;
            init_String(&label);
            /* Use a stack to keep track of the directory containment structure. */
            struct Impl_Level {
                iRangecc prefix;
            };
            iDeclareType(Level);
            iArray stack;
            init_Array(&stack, sizeof(iLevel));
            iConstForEach(StringSet, u, urls) {
                const iString *url = u.value;
                /* Compare the structure of this URL compared to the current stack. */
                iRangecc itemDir = urlPath_String(url);
                size_t dirEnd = lastIndexOfCStr_Rangecc(itemDir, "/");
                if (dirEnd == iInvalidPos) continue; /* must be root */
                itemDir.end = itemDir.start + dirEnd;
                if (dirEnd > 0 && isEmpty_Range(&itemDir)) {
                    continue; /* must be root */
                }
                size_t   i      = 0;
                iRangecc seg    = iNullRange;
                iRangecc subDir = iNullRange;
                while (nextSplit_Rangecc(itemDir, "/", &seg)) {
                    subDir = (iRangecc) { itemDir.start, seg.end };
                    if (i < size_Array(&stack) &&
                        !equalRange_Rangecc(subDir, constValue_Array(&stack, i, iLevel).prefix)) {
                        /* Only the parts before this are the same. */
                        resize_Array(&stack, i);
                    }
                    if (i >= size_Array(&stack)) {
                        /* The itemDir has more components than the stack. */
                        iSidebarItem *previousItem   = backItem_ListWidget(d->list);
                        iRangecc      parentUrlRange = { constBegin_String(url), subDir.end };
                        if (!equalRange_Rangecc(range_String(&previousItem->url), parentUrlRange)) {
                            iString parentUrl;
                            parentUrlRange.end++; /* include the slash */
                            initRange_String(&parentUrl, parentUrlRange);
                            setRange_String(&label, seg);
                            iSidebarItem *parent = addStructureItem_SidebarWidget_(
                                d, &parentUrl, &label, &stack, docUrl, &docItem);
                            deinit_String(&parentUrl);
                            if (parent) {
                                appendCStr_String(&parent->label, "/");
                                parent->isBold = (parent->indent == 1);
                            }
                        }
                        pushBack_Array(&stack, &(iLevel) { subDir });
                    }
                    i++;
                }
                if (i < size_Array(&stack)) {
                    /* The prefix has become shorter. */
                    resize_Array(&stack, i);
                }
                setRange_String(&label, (iRangecc) { itemDir.end + 1, constEnd_String(url) });
                if (isEmpty_String(&label) || !cmp_String(&label, "/")) {
                    /* The directory item was already created above. */
                    continue;
                }
                addStructureItem_SidebarWidget_(
                    d, url, &label, &stack, docUrl, &docItem);
            }
            if (docItem != iInvalidPos && !keepActions) {
                postCommand_Widget(d->list, "sideitem.show arg:%u", docItem);
            }
            deinit_Array(&stack);
            deinit_String(&label);
            /* Context menu. */
            const iMenuItem menuItems[] = {
                { "${menu.fold}", 0, 0, "structure.fold" },
                { "---" },
                { openTab_Icon " ${menu.opentab}", 0, 0, "sideitem.open newtab:1" },
                { openTabBg_Icon " ${menu.opentab.background}", 0, 0, "sideitem.open newtab:2" },
                { openWindow_Icon " ${menu.openwindow}", 0, KMOD_DESKTOP, "sideitem.open newwindow:1" },
                { "---" },
                { "${menu.copyurl}", 0, 0, "sideitem.copy" },
            };
            d->menu       = makeMenu_Widget(as_Widget(d), menuItems + 2, iElemCount(menuItems) - 2);
            d->folderMenu = makeMenu_Widget(as_Widget(d), menuItems, iElemCount(menuItems));
            break;
        }
        case bookmarks_SidebarMode: {
            iAssert(get_Root() == d->widget.root);
            trim_String(&d->bookmarkFilter);
            if (!isEmpty_String(&d->bookmarkFilter)) {
                updateFilteredBookmarkItems_SidebarWidget_(d);
            }
            else {
                updateBookmarkItems_SidebarWidget_(d);
            }
            d->modeMenu = makeMenu_Widget(
                as_Widget(d), bookmarkModeMenuItems_, iElemCount(bookmarkModeMenuItems_));
            if (keepActions) {
                break;
            }
            if (!isMobile) {
                /* Filter/search field. */
                /* TODO: Where to put this on mobile? */
                iLabelWidget *magnifier =
                    addChildFlags_Widget(d->actions,
                                         iClob(new_LabelWidget(magnifyingGlass_Icon, NULL)),
                                         frameless_WidgetFlag | noBackground_WidgetFlag);
                iInputWidget *filter = new_InputWidget(0);
                setText_InputWidget(filter, &d->bookmarkFilter);
                setId_Widget(as_Widget(filter), "filter.bookmark.input");
                setHint_InputWidget(filter, "${hint.filter.bookmark}");
                setEatEscape_InputWidget(filter, iFalse);
                setSelectAllOnFocus_InputWidget(filter, iTrue);
                setNotifyEdits_InputWidget(filter, iTrue);
                setLineBreaksEnabled_InputWidget(filter, iFalse);
                addChildFlags_Widget(
                    d->actions, iClob(filter), expand_WidgetFlag | frameless_WidgetFlag);
                setId_Widget(
                    addChildFlags_Widget(
                        d->actions,
                        iClob(newIcon_LabelWidget(
                            close_Icon, SDLK_ESCAPE, 0, "filter.bookmark.clear")),
                        noBackground_WidgetFlag | frameless_WidgetFlag |
                            (isEmpty_String(&d->bookmarkFilter) ? disabled_WidgetFlag : 0)),
                    "filter.bookmark.clear");
            }
            else {
                /* On mobile, we need to show buttons for edit actions. */
                addActionButton_SidebarWidget_(d,
                                               "${sidebar.action.bookmarks.newfolder}",
                                               "bookmarks.addfolder",
                                               !d->isEditing ? hidden_WidgetFlag : 0);
                addChildFlags_Widget(d->actions, iClob(new_Widget()), expand_WidgetFlag);
                addActionButton_SidebarWidget_(
                    d,
                    d->isEditing ? "${sidebar.close}" : "${sidebar.action.bookmarks.edit}",
                    format_CStr("%s.bookmarks.edit", cstr_String(&d->widget.id)),
                    0);
            }
            break;
        }
        case history_SidebarMode: {
            iDate on;
            initCurrent_Date(&on);
            const int thisYear = on.year;
            iConstForEach(PtrArray, i, list_Visited(visited_App(), 200)) {
                const iVisitedUrl *visit = i.ptr;
                iSidebarItem      *item  = new_SidebarItem();
                set_String(&item->url, &visit->url);
                set_String(&item->label, &visit->url);
                if (prefs_App()->decodeUserVisibleURLs) {
                    set_String(&item->label,
                               collect_String(urlDecodeExclude_String(&item->label,
                                                                      URL_DECODE_EXCLUDE_CHARS)));
                }
                else {
                    set_String(&item->label,
                               collect_String(urlEncodeExclude_String(&item->label,
                                                                      URL_ENCODE_EXCLUDE_CHARS)));
                }
                iDate date;
                init_Date(&date, &visit->when);
                if (date.day != on.day || date.month != on.month || date.year != on.year) {
                    on = date;
                    /* Date separator. */
                    iSidebarItem *sep         = new_SidebarItem();
                    sep->listItem.flags.isSeparator = iTrue;
                    const iString *text       = collect_String(
                        format_Date(&date,
                                    cstr_Lang(date.year != thisYear ? "sidebar.date.otheryear"
                                                                    : "sidebar.date.thisyear")));
                    set_String(&sep->meta, text);
                    const int yOffset = itemHeight_ListWidget(d->list) * 2 / 3;
                    sep->id           = yOffset;
                    addItem_ListWidget(d->list, sep);
                    iRelease(sep);
                    /* Date separators are two items tall. */
                    sep                       = new_SidebarItem();
                    sep->listItem.flags.isSeparator = iTrue;
                    sep->id                   = -itemHeight_ListWidget(d->list) + yOffset;
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
                { openWindow_Icon " ${menu.openwindow}", 0, KMOD_DESKTOP, "history.open newwindow:1" },
                { "---" },
                { bookmark_Icon " ${sidebar.entry.bookmark}", 0, 0, "history.addbookmark" },
                { "${menu.copyurl}", 0, 0, "sideitem.copy canon:1" },
                { "---", 0, 0, NULL },
                { close_Icon " ${menu.forgeturl}", 0, 0, "history.delete" },
                { "---", 0, 0, NULL },
                { delete_Icon " " uiTextCaution_ColorEscape "${history.clear}",
                  0,
                  0,
                  "history.clear confirm:1" },
            };
            d->menu = makeMenu_Widget(as_Widget(d), menuItems, iElemCount(menuItems));
            d->modeMenu =
                makeMenu_Widget(as_Widget(d),
                                (iMenuItem[]) {
                                    { delete_Icon " " uiTextCaution_ColorEscape "${history.clear}",
                                      0,
                                      0,
                                      "history.clear confirm:1" },
                                },
                                1);
            if (isMobile) {
                addChildFlags_Widget(d->actions, iClob(new_Widget()), expand_WidgetFlag);
                iLabelWidget *btn = addActionButton_SidebarWidget_(
                    d, "${sidebar.action.history.clear}", "history.clear confirm:1", 0);
            }
            break;
        }
        case identities_SidebarMode: {
            isEmpty = !updateItems_CertListWidget(d->certList);
            /* Actions. */
            if (!isEmpty) {
                addActionButton_SidebarWidget_(
                    d, add_Icon " ${sidebar.action.ident.new}", "ident.new", 0);
                addActionButton_SidebarWidget_(
                    d, "${sidebar.action.ident.import}", "ident.import", 0);
            }
            break;
        }
        case openDocuments_SidebarMode: {
            iWidget *w = as_Widget(d);
            iConstForEach(ObjectList, i, listDocuments_App(w->root)) {
                const iDocumentWidget *doc  = i.object;
                iSidebarItem          *item = new_SidebarItem();
                set_String(&item->label, title_GmDocument(document_DocumentWidget(doc)));
                if (isEmpty_String(&item->label)) {
                    set_String(&item->label, url_DocumentWidget(doc));
                }
                if (isEmpty_String(&item->label)) {
                    setCStr_String(&item->label, "\u2014");
                }
                set_String(&item->url, url_DocumentWidget(doc));
                set_String(&item->meta, id_Widget(constAs_Widget(doc)));
                item->icon   = siteIcon_GmDocument(document_DocumentWidget(doc));
                item->isBold = isUnseen_DocumentWidget(doc);
                item->indent = isVisible_Widget(doc) ? 1 : 0;
                item->id     = (isRequestOngoing_DocumentWidget(doc) ? 1 : 0);
                if (numActivePlayers_Media(constMedia_GmDocument(document_DocumentWidget(doc)))) {
                    item->id |= 2;
                }
                addItem_ListWidget(d->list, item);
                item->listItem.flags.isDraggable = iTrue;
                iRelease(item);
            }
            /* We can provide both tab and page related items in the menu. */
            const iMenuItem menuItems[] = {
                { close_Icon " ${menu.closetab}", 0, 0, "opendocs.close" },
                { "---" },
                { barLeftArrow_Icon " ${menu.closetab.above}", 0, 0, "opendocs.close toleft:1" },
                { barRightArrow_Icon " ${menu.closetab.below}", 0, 0, "opendocs.close toright:1" },
                { "${menu.closetab.other}", 0, 0, "opendocs.close toleft:1 toright:1" },
                { "---" , 0, KMOD_DESKTOP | KMOD_TABLET },
                { "${menu.movetab.split}", 0, KMOD_DESKTOP | KMOD_TABLET, "opendocs.swap" },
                { "${menu.movetab.newwindow}", 0, KMOD_DESKTOP, "opendocs.swap newwindow:1" },
                { "---" },
                { copy_Icon " ${menu.duptab}", 0, 0, "opendocs.dup" },
                { "${menu.copyurl}", 0, 0, "sideitem.copy" },
                { bookmark_Icon " ${sidebar.entry.bookmark}", 0, 0, "opendocs.bookmark" },
            };
            d->menu = makeMenu_Widget(as_Widget(d), menuItems, iElemCount(menuItems));
            if (isMobile && !keepActions) {
                addActionButton_SidebarWidget_(d, "${close}", "tabs.close", 0);
                addChildFlags_Widget(d->actions, iClob(new_Widget()), expand_WidgetFlag);
                addActionButton_SidebarWidget_(d, openTab_Icon, "tabs.new", 0);
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
    setDragHandleWidth_ListWidget(d->list, isMobile_Platform() &&
                                  (d->mode == openDocuments_SidebarMode ||
                                   (d->mode == bookmarks_SidebarMode && d->isEditing)) ?
                                  itemHeight_ListWidget(d->list) * 3 / 2 : 0);
    d->list->hideItemOnDrag = (isMobile_Platform() && d->mode == openDocuments_SidebarMode);
    /* Content for a blank tab. */
    if (isEmpty) {
        const int rightPad = (isTerminal_Platform() ? 5 : (3 * gap_UI));
        if (d->mode == feedEntries_SidebarMode) {
            iWidget *div = makeVDiv_Widget();
            setPadding_Widget(div, 3 * gap_UI, 0, rightPad, 2 * gap_UI);
            addChildFlags_Widget(div, iClob(new_Widget()), expand_WidgetFlag); /* pad */
            if (d->feedsMode == all_FeedsMode) {
                addChild_Widget(div,
                                iClob(new_LabelWidget("${menu.feeds.refresh}", "feeds.refresh")));
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
            setPadding_Widget(div, 3 * gap_UI, 0, rightPad, 2 * gap_UI);
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
                addChildFlags_Widget(div,
                                     iClob(linkLabel = new_LabelWidget(
                                               format_CStr(cstr_Lang("ident.gotohelp"),
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

static size_t findItemUrl_SidebarWidget_(const iSidebarWidget *d, const iString *url) {
    /* O(n) performance! */
    for (size_t i = 0; i < numItems_ListWidget(d->list); i++) {
        const iSidebarItem *item = constItem_ListWidget(d->list, i);
        if (equal_String(url, &item->url)) {
            return i;
        }
    }
    return iInvalidPos;
}

static void updateItemHeight_SidebarWidget_(iSidebarWidget *d) {
    /* Note: identity item height is defined by CertListWidget */
#if !defined(iPlatformTerminal)
    const float heights[max_SidebarMode] = { 1.333f, 2.333f, 2.5f, 0, 1.2f, 1.2f, 1.333f, 1.333f };
#else
    const float heights[max_SidebarMode] = { 1, 3, 3, 0, 1, 1, 1, 1 };
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
                              // d->mode == documentOutline_SidebarMode ? tmBannerBackground_ColorId
                              uiBackgroundSidebar_ColorId);
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
    iWidget  *d       = as_Widget(sidebar);
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
    d->side             = side;
    d->mode             = -1;
    d->feedsMode        = all_FeedsMode;
    d->midHeight        = 0;
    d->isEditing        = iFalse;
    d->numUnreadEntries = 0;
    d->buttonFont       = uiLabel_FontId; /* wiil be changed later */
    d->itemFonts[0]     = uiContent_FontId;
    d->itemFonts[1]     = uiContentBold_FontId;
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
    addChildFlags_Widget(
        w, iClob(vdiv), resizeToParentWidth_WidgetFlag | resizeToParentHeight_WidgetFlag);
    iZap(d->modeButtons);
    d->resizer       = NULL;
    d->list          = NULL;
    d->certList      = NULL;
    d->actions       = NULL;
    d->closedFolders = new_IntSet();
    init_String(&d->bookmarkFilter);
    init_String(&d->structureHost);
    d->structureUrls    = new_StringSet();
    d->structureUnfolds = new_StringSet();
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
    d->firstVisibleModeButton = NULL;
    for (int i = 0; i < max_SidebarMode; i++) {
        if (i == identities_SidebarMode && deviceType_App() != desktop_AppDeviceType) {
            /* On mobile, identities are managed via Settings. */
            continue;
        }
        const iBool isEnabled = prefs_App()->sidebarModeEnabled[d->side][i];
        d->modeButtons[i] = addChildFlags_Widget(
            buttons,
            iClob(new_LabelWidget(tightModeLabels_[i],
                                  format_CStr("%s.mode arg:%d", cstr_String(id_Widget(w)), i))),
            frameless_WidgetFlag | noBackground_WidgetFlag | expand_WidgetFlag |
                collapse_WidgetFlag |
                (isEnabled ? 0 : hidden_WidgetFlag));
        if (!d->firstVisibleModeButton && isEnabled) {
            d->firstVisibleModeButton = d->modeButtons[i];
        }
        as_Widget(d->modeButtons[i])->flags2 |= slidingSheetDraggable_WidgetFlag2; /* phone */
    }
    /* Dropdown menu for changing the mode. */
    if (!isSlidingSheet_SidebarWidget_(d) && !isTerminal_Platform()) {
        const char *barId = cstr_String(id_Widget(w));
        // clang-format off
        const iMenuItem modeDropItems[] = {
            { normalModeLabels_[0], 0, 0, format_CStr("%s.mode force:1 arg:0", barId) },
            { normalModeLabels_[1], 0, 0, format_CStr("%s.mode force:1 arg:1", barId) },
            { normalModeLabels_[2], 0, 0, format_CStr("%s.mode force:1 arg:2", barId) },
            { "---" },
            { normalModeLabels_[3], 0, KMOD_DESKTOP, format_CStr("%s.mode force:1 arg:3", barId) },
            { normalModeLabels_[4], 0, 0, format_CStr("%s.mode force:1 arg:4", barId) },
            { normalModeLabels_[5], 0, 0, format_CStr("%s.mode force:1 arg:5", barId) },
            { "---" },
            { normalModeLabels_[6], 0, 0, format_CStr("%s.mode force:1 arg:6", barId) },
            { normalModeLabels_[7], 0, 0, format_CStr("%s.mode force:1 arg:7", barId) },
            { "---" },
            { gear_Icon " ${menu.sidebar.configure}", 0, 0, "preferences sidecfg:1" },
            { close_Icon " ${close}", 0, 0, format_CStr("%s.toggle", barId) },
        };
        // clang-format on
        addChildFlags_Widget(buttons,
                             iClob(makeMenuButton_LabelWidget(
                                 midEllipsis_Icon, modeDropItems, iElemCount(modeDropItems))),
                             0);
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
    addChildFlags_Widget(listAndActions, iClob(listArea), expand_WidgetFlag);
    setId_Widget(addChildPosFlags_Widget(listAndActions,
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
    setMode_SidebarWidget(d, bookmarks_SidebarMode);
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
    d->menu       = NULL;
    d->modeMenu   = NULL;
    d->folderMenu = NULL;
    addAction_Widget(w, refreshFeeds_KeyShortcut, "feeds.refresh");
    updateMetrics_SidebarWidget_(d);
    if (side == left_SidebarSide) {
        postCommand_App("~sidebar.update"); /* unread count */
    }
}

void deinit_SidebarWidget(iSidebarWidget *d) {
    iRelease(d->structureUnfolds);
    iRelease(d->structureUrls);
    deinit_String(&d->structureHost);
    deinit_String(&d->bookmarkFilter);
    delete_IntSet(d->closedFolders);
    deinit_String(&d->cmdPrefix);
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
    iString *setIdentArg = NULL;
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
        case feedEntries_SidebarMode: {
            postCommandString_Root(
                get_Root(),
                feedEntryOpenCommand_String(
                    &item->url, mouseTabMode ? mouseTabMode : openTabMode_Sym(modState_Keys()), 0));
            setFocus_Widget(NULL);
            break;
        }
        case siteStructure_SidebarMode:
            if (item->indent > 0 && item->count > 0) {
                /* This item with folded children will now unfold. */
                iString *itemUrl = collect_String(copy_String(&item->url));
                iString *origUrl = collect_String(copy_String(itemUrl));
                if (!endsWith_String(itemUrl, "/")) {
                    appendCStr_String(itemUrl, "/"); /* always must have a directory separator */
                }
                insert_StringSet(d->structureUnfolds, itemUrl);
                updateItemsWithFlags_SidebarWidget_(d, iTrue); /* `item` becomes invalid */
                return;
            }
            /* Everything the user clicks is automatically unfolded. */
            insert_StringSet(d->structureUnfolds, &item->url);
            /* fall-through */
        case subscriptions_SidebarMode: {
            postCommandf_Root(get_Root(),
                              "open newtab:%d url:%s",
                              mouseTabMode ? mouseTabMode : openTabMode_Sym(modState_Keys()),
                              cstr_String(&item->url));
            setFocus_Widget(NULL);
            break;
        }
        case bookmarks_SidebarMode:
            /* Bookmark folder folding is toggled when clicking. */
            if (isEmpty_String(&item->url) /* is a folder */) {
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
            else {
                /* Check if the bookmark changes the active identity on the URL. */
                const iBookmark *bm = get_Bookmarks(bookmarks_App(), item->id);
                if (bm && !isEmpty_String(&bm->identity)) {
                    setIdentArg = collect_String(copy_String(&bm->identity));
                    prependCStr_String(setIdentArg, " setident:");
                }
            }
            if (d->isEditing) {
                d->contextItem  = item;
                d->contextIndex = itemIndex;
                setFocus_Widget(NULL);
                postCommand_Widget(d, "bookmark.edit");
                break;
            }
            /* fall through */
        case history_SidebarMode: {
            if (!isEmpty_String(&item->url)) {
                postCommandf_Root(get_Root(),
                                  "open fromsidebar:1%s newtab:%d url:%s",
                                  setIdentArg ? cstr_String(setIdentArg) : "",
                                  mouseTabMode ? mouseTabMode : openTabMode_Sym(modState_Keys()),
                                  cstr_String(&item->url));
                setFocus_Widget(NULL);
            }
            break;
        }
        case openDocuments_SidebarMode: {
            iWidget *tabs = findWidget_Root("doctabs");
            dismissPortraitPhoneSidebars_Root(as_Widget(d)->root);
            showTabPage_Widget(tabs, findChild_Widget(tabs, cstr_String(&item->meta)));
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
        const int fonts[2] = { isPortrait_App() ? uiLabelBig_FontId : uiContent_FontId,
                               isPortrait_App() ? uiLabelBigBold_FontId : uiContentBold_FontId };
        if (d->itemFonts[0] != fonts[0]) {
            d->itemFonts[0] = fonts[0];
            d->itemFonts[1] = fonts[1];
            updateItemHeight_SidebarWidget_(d);
        }
        setButtonFont_SidebarWidget(d, isPortrait_App() ? uiLabelMedium_FontId : uiLabel_FontId);
    }
    const iBool isTight = isPortraitPhone_App() ||
        (width_Rect(bounds_Widget(as_Widget(d->firstVisibleModeButton))) < d->maxButtonLabelWidth);
    const size_t tabCount = tabCount_Widget(findWidget_App("doctabs"));
    for (int i = 0; i < max_SidebarMode; i++) {
        iLabelWidget *button = d->modeButtons[i];
        if (!button) continue;
        setFlags_Widget(
            as_Widget(button), hidden_WidgetFlag, !prefs_App()->sidebarModeEnabled[d->side][i]);
        setAlignVisually_LabelWidget(button, isTight);
        setFlags_Widget(as_Widget(button), tight_WidgetFlag, isTight);
        if (i == feedEntries_SidebarMode && d->numUnreadEntries) {
            updateText_LabelWidget(
                button,
                collectNewFormat_String(
                    "%s " uiTextAction_ColorEscape "%zu%s%s",
                    tightModeLabels_[i],
                    d->numUnreadEntries,
                    !isTight ? " " : "",
                    !isTight ? formatCStrs_Lang("sidebar.unread.n", d->numUnreadEntries) : ""));
        }
        else if (i == openDocuments_SidebarMode && tabCount > 1) {
            updateText_LabelWidget(button,
                                   collectNewFormat_String("%s " uiTextAction_ColorEscape "%u",
                                                           tightModeLabels_[i],
                                                           tabCount));
        }
        else {
            updateTextCStr_LabelWidget(button,
                                       isTight ? tightModeLabels_[i] : normalModeLabels_[i]);
        }
    }
}

void setWidth_SidebarWidget(iSidebarWidget *d, float widthAsGaps) {
    if (!d) return;
    iWidget    *w            = as_Widget(d);
    const iBool isFixedWidth = deviceType_App() == phone_AppDeviceType;
    int         width        = widthAsGaps * gap_UI; /* in pixels */
    if (!isFixedWidth) {
        /* Even less space if the other sidebar is visible, too. */
        const iWidget *other = findWidget_App(d->side == left_SidebarSide ? "sidebar2" : "sidebar");
        const int      otherWidth = isVisible_Widget(other) ? width_Widget(other) : 0;
        width                     = iClamp(width,
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
                set_String(&bm->url, url);
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
    iWidget    *w        = as_Widget(d);
    const int   pos      = w->rect.pos.y;
    const iRect safeRect = safeRect_Root(w->root);
    if (slide == top_SlidingSheetPos) {
        w->rect.pos.y  = top_Rect(safeRect);
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
        w->rect.pos.y  = height_Rect(safeRect) - w->rect.size.y;
        setVisualOffset_Widget(w, pos - w->rect.pos.y, 0, 0);
        setVisualOffset_Widget(w, 0, 200, easeOut_AnimFlag | softer_AnimFlag);
        setScrollMode_ListWidget(d->list, disabledAtTopBothDirections_ScrollMode);
    }
    //    animateSlidingSheetHeight_SidebarWidget_(d);
}

static iBool handleSidebarCommand_SidebarWidget_(iSidebarWidget *d, const char *cmd) {
    iWidget *w = as_Widget(d);
    if (equal_Command(cmd, "width")) {
        setWidth_SidebarWidget(
            d, arg_Command(cmd) * (argLabel_Command(cmd, "gaps") ? 1.0f : (1.0f / gap_UI)));
        return iTrue;
    }
    else if (equal_Command(cmd, "mode")) {
        const int mode = arg_Command(cmd);
        if (!isSlidingSheet_SidebarWidget_(d) && !argLabel_Command(cmd, "force") &&
            !prefs_App()->sidebarModeEnabled[d->side][mode] &&
            prefs_App()->sidebarModeEnabled[d->side ^ 1][mode]) {
            /* Make it affect the other side instead. */
            postCommand_Widget(w, "%s.mode arg:%d show:%d toggle:%d",
                               d->side == 0 ? "sidebar2" : "sidebar",
                               mode,
                               argLabel_Command(cmd, "show"),
                               argLabel_Command(cmd, "toggle"));
            return iTrue;
        }
        const iBool wasChanged = setMode_SidebarWidget(d, mode);
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
        if (argLabel_Command(cmd, "hide") && !isVisible_Widget(w)) {
            return iTrue;
        }
        /* The sidebar will appear/disappear and UI elements will change position. Stop any
           ongoing touch interactions based on the old arrangement. */
        clear_Touch();
        const iBool isAnimated =
            prefs_App()->uiAnimations && argLabel_Command(cmd, "noanim") == 0 &&
            (d->side == left_SidebarSide || deviceType_App() != phone_AppDeviceType);
        int visX = 0;
        if (isVisible_Widget(w)) {
            visX = left_Rect(bounds_Widget(w)) - left_Rect(w->root->widget->rect);
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
            setFlags_Widget(w, horizontalOffset_WidgetFlag | keepOnTop_WidgetFlag, iFalse);
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
    postCommandf_App("bookmarks.changed nosidebar:%p",
                     d); /* skip this sidebar since we updated already */
}

static void bookmarkMovedOntoFolder_SidebarWidget_(iSidebarWidget *d, size_t index,
                                                   size_t folderIndex) {
    const iSidebarItem *movingItem = item_ListWidget(d->list, index);
    const iSidebarItem *dstItem    = item_ListWidget(d->list, folderIndex);
    iBookmark          *bm         = get_Bookmarks(bookmarks_App(), movingItem->id);
    bm->parentId                   = dstItem->id;
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
    const iWidget *w          = constAs_Widget(d);
    const iRect    safeRect   = safeRect_Root(w->root);
    const int      midY       = bottom_Rect(safeRect) - d->midHeight;
    const int      topHalf    = (top_Rect(safeRect) + midY) / 2;
    const int      bottomHalf = (bottom_Rect(safeRect) + midY * 2) / 3;
    return (iRangei) { topHalf, bottomHalf };
}

static void gotoNearestSlidingSheetPos_SidebarWidget_(iSidebarWidget *d) {
    const iRangei midRegion = SlidingSheetMiddleRegion_SidebarWidget_(d);
    const int     pos       = top_Rect(d->widget.rect);
    setSlidingSheetPos_SidebarWidget_(d,
                                      pos < midRegion.start ? top_SlidingSheetPos
                                      : pos > midRegion.end ? bottom_SlidingSheetPos
                                                            : middle_SlidingSheetPos);
}

static iBool isFolder_(void *context, const iBookmark *bm) {
    iUnused(context);
    return isFolder_Bookmark(bm);
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

static iBool processEvent_SidebarWidget_(iSidebarWidget *d, const SDL_Event *ev) {
    iWidget *w = as_Widget(d);
    /* Handle commands. */
    if (isResize_UserEvent(ev)) {
        checkModeButtonLayout_SidebarWidget_(d);
        if (deviceType_App() == phone_AppDeviceType) {
            setPadding_Widget(d->actions, 0, 0, 0, 0);
            setFlags_Widget(
                findChild_Widget(w, "sidebar.title"), hidden_WidgetFlag, isLandscape_App());
            setFlags_Widget(
                findChild_Widget(w, "sidebar.close"), hidden_WidgetFlag, isLandscape_App());
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
            setFlags_Widget(w, drawBackgroundToBottom_WidgetFlag, isPortrait_App());
            setBackgroundColor_Widget(
                w, isPortrait_App() ? uiBackgroundSidebar_ColorId : none_ColorId);
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
            setPadding_Widget(
                d->actions, 0, 0, 0, prefs_App()->bottomNavBar ? 0 : bottomSafeInset_Mobile());
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
                setPadding_Widget(d->actions,
                                  0,
                                  0,
                                  0,
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
            if (d->mode != siteStructure_SidebarMode) {
                scrollOffset_ListWidget(d->list, 0);
            }
        }
        else if (equal_Command(cmd, "sidebar.modes.changed")) {
            checkModeButtonLayout_SidebarWidget_(d);
            arrange_Widget(parent_Widget(d->modeButtons[0]));
        }
        else if (equal_Command(cmd, "document.openurls.changed")) {
            if (d->mode == openDocuments_SidebarMode) {
                updateItems_SidebarWidget_(d);
            }
            checkModeButtonLayout_SidebarWidget_(d);
            return iFalse;
        }
        else if (equal_Command(cmd, "sidebar.update")) {
            d->numUnreadEntries = numUnread_Feeds();
            checkModeButtonLayout_SidebarWidget_(d);
            updateItems_SidebarWidget_(d);
        }
        else if (equal_Command(cmd, "visited.changed")) {
            d->numUnreadEntries = numUnread_Feeds();
            checkModeButtonLayout_SidebarWidget_(d);
            if (d->mode == history_SidebarMode || d->mode == feedEntries_SidebarMode) {
                updateItems_SidebarWidget_(d);
            }
        }
        else if (equal_Command(cmd, "bookmarks.changed") &&
                 (d->mode == bookmarks_SidebarMode || d->mode == feedEntries_SidebarMode ||
                  d->mode == subscriptions_SidebarMode)) {
            if (pointerLabel_Command(cmd, "nosidebar") != d) {
                updateItems_SidebarWidget_(d);
                if (hasLabel_Command(cmd, "added")) {
                    const size_t addedId    = argLabel_Command(cmd, "added");
                    const size_t addedIndex = findItem_SidebarWidget_(d, addedId);
                    scrollToItem_ListWidget(d->list, addedIndex, 200);
                }
            }
        }
        else if (d->mode == bookmarks_SidebarMode &&
                 equalArg_Command(cmd, "input.edited", "id", "filter.bookmark.input") &&
                 hasParent_Widget(constAs_Widget(pointer_Command(cmd)), w)) {
            set_String(&d->bookmarkFilter, text_InputWidget(pointer_Command(cmd)));
            setFlags_Widget(findChild_Widget(d->actions, "filter.bookmark.clear"),
                            disabled_WidgetFlag,
                            isEmpty_String(&d->bookmarkFilter));
            updateItemsWithFlags_SidebarWidget_(d, iTrue);
            return iTrue;
        }
        else if (d->mode == bookmarks_SidebarMode &&
                 equalWidget_Command(cmd, w, "filter.bookmark.clear")) {
            iInputWidget *filter = findChild_Widget(w, "filter.bookmark.input");
            setTextCStr_InputWidget(filter, "");
            setFlags_Widget(pointer_Command(cmd), disabled_WidgetFlag, iTrue);
            clear_String(&d->bookmarkFilter);
            updateItemsWithFlags_SidebarWidget_(d, iTrue);
            setFocus_Widget(as_Widget(filter));
            return iTrue;
        }
        else if (d->mode == openDocuments_SidebarMode &&
                 (equal_Command(cmd, "document.request.started") ||
                  equal_Command(cmd, "document.request.finished"))) {
            /* TODO: There are no notifications for audio player starting/stopping.
               These would be useful for updating the active-player status icons. */
            updateItemsWithFlags_SidebarWidget_(d, iTrue);
            return iFalse;
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
                const iInt2 inner  = windowToInner_Widget(w, coord_Command(cmd));
                const int   resMid = d->resizer->rect.size.x / 2;
                setWidth_SidebarWidget(
                    d,
                    ((d->side == left_SidebarSide
                          ? inner.x
                          : (right_Rect(rect_Root(w->root)) - coord_Command(cmd).x)) +
                     resMid) /
                        (float) gap_UI);
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
        else if (isCommand_Widget(w, ev, "list.delete")) {
            if (d->mode == bookmarks_SidebarMode) {
                d->contextItem = item_ListWidget(d->list, arg_Command(cmd));
                postCommand_Widget(w, "bookmark.delete");
                return iTrue;
            }
        }
        else if (isCommand_Widget(w, ev, "list.dragged")) {
            if (d->mode == bookmarks_SidebarMode) {
                if (hasLabel_Command(cmd, "onto")) {
                    /* Dragged onto a folder. */
                    bookmarkMovedOntoFolder_SidebarWidget_(
                        d, argU32Label_Command(cmd, "arg"), argU32Label_Command(cmd, "onto"));
                }
                else {
                    const iBool isBefore = hasLabel_Command(cmd, "before");
                    bookmarkMoved_SidebarWidget_(
                        d,
                        argU32Label_Command(cmd, "arg"),
                        argU32Label_Command(cmd, isBefore ? "before" : "after"),
                        isBefore);
                }
            }
            else if (d->mode == openDocuments_SidebarMode) {
                /* Reorder tabs. */
                const int           srcIndex = argU32Label_Command(cmd, "arg");
                const iSidebarItem *item     = constItem_ListWidget(d->list, srcIndex);
                /* Dragging onto is the same as dragging before. */
                const int dstIndex = hasLabel_Command(cmd, "before")
                                         ? argU32Label_Command(cmd, "before")
                                         : argU32Label_Command(cmd, "after");
                /* Must be the current tab to move. */
                postCommandf_App("tabs.switch id:%s", cstr_String(&item->meta));
                postCommandf_App("tabs.move arg:%d", dstIndex - srcIndex);
            }
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
                postCommand_App("bookmarks.changed");
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
            clear_IntSet(d->closedFolders);
            if (arg_Command(cmd)) {
                iConstForEach(PtrArray, i, list_Bookmarks(bookmarks_App(), NULL, isFolder_, NULL)) {
                    insert_IntSet(d->closedFolders, id_Bookmark(i.ptr));
                }
            }
            if (d->mode == bookmarks_SidebarMode) {
                updateItems_SidebarWidget_(d);
            }
            return iTrue;
        }
        else if (equal_Command(cmd, "feeds.update.finished")) {
            d->numUnreadEntries = argLabel_Command(cmd, "unread");
            checkModeButtonLayout_SidebarWidget_(d);
            if (d->mode == feedEntries_SidebarMode || d->mode == subscriptions_SidebarMode) {
                updateItems_SidebarWidget_(d);
            }
        }
        else if (equalWidget_Command(cmd, w, "feeds.mode")) {
            d->feedsMode = arg_Command(cmd);
            updateItemsWithFlags_SidebarWidget_(d, iTrue);
            return iTrue;
        }
        else if (equal_Command(cmd, "feeds.markallread") && d->mode == feedEntries_SidebarMode) {
            if (argLabel_Command(cmd, "confirm")) {
                /* This is used on mobile. */
                iWidget *menu = makeMenu_Widget(
                    w->root->widget,
                    (iMenuItem[]) { check_Icon " " uiTextCaution_ColorEscape "${feeds.markallread}",
                                    0,
                                    0,
                                    "feeds.markallread" },
                    1);
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
        else if (startsWith_CStr(cmd, "feed.entry.") && d->mode == feedEntries_SidebarMode) {
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
                                                    argLabel_Command(cmd, "newwindow")));
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
                        handleFeedUnsubscribeCommand_SidebarWidget_(d, cmd, feedBookmark);
                        return iTrue;
                    }
                }
            }
        }
        else if (d->mode == siteStructure_SidebarMode &&
                 isCommand_Widget(w, ev, "structure.fold")) {
            const iSidebarItem *item = d->contextItem;
            if (item) {
                removeStructureUnfold_SidebarWidget_(d, &item->url);
                updateItemsWithFlags_SidebarWidget_(d, iTrue);
            }
            return iTrue;
        }
        else if (startsWith_CStr(cmd, "sideitem.")) {
            const iSidebarItem *item = d->contextItem;
            if (item) {
                if (isCommand_Widget(w, ev, "sideitem.open")) {
                    postCommandf_App("open newwindow:%d newtab:%d url:%s",
                                     argLabel_Command(cmd, "newwindow"),
                                     argLabel_Command(cmd, "newtab"),
                                     cstr_String(&item->url));
                    return iTrue;
                }
                else if (isCommand_Widget(w, ev, "sideitem.copy")) {
                    SDL_SetClipboardText(cstr_String(argLabel_Command(cmd, "canon")
                                                         ? canonicalUrl_String(&item->url)
                                                         : &item->url));
                    return iTrue;
                }
            }
            if (isCommand_Widget(w, ev, "sideitem.show")) {
                const size_t pos = arg_Command(cmd);
                /* Sliding sheet and the list both animating at the same time is confusing. */
                scrollToItem_ListWidget(
                    d->list,
                    pos,
                    !prefs_App()->uiAnimations || isSlidingSheet_SidebarWidget_(d) ? 0 : 400);
                return iTrue;
            }
        }
        else if (startsWith_CStr(cmd, "sub.") && d->mode == subscriptions_SidebarMode) {
            const iSidebarItem *item = d->contextItem;
            if (item) {
                if (isCommand_Widget(w, ev, "sub.edit")) {
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
                makeQuestion_Widget(
                    uiTextCaution_ColorEscape "${heading.history.clear}",
                    "${dlg.confirm.history.clear}",
                    (iMenuItem[]) { { "${cancel}", 0, 0, NULL },
                                    { uiTextCaution_ColorEscape "${dlg.history.clear}",
                                      0,
                                      0,
                                      "history.clear confirm:0" } },
                    2);
            }
            else {
                clear_Visited(visited_App());
                if (d->mode == history_SidebarMode) {
                    updateItems_SidebarWidget_(d);
                    scrollOffset_ListWidget(d->list, 0);
                }
                return iFalse; /* all sidebars clear themselves */
            }
            return iTrue;
        }
        else if (isCommand_Widget(w, ev, "opendocs.bookmark")) {
            const iSidebarItem *item = d->contextItem;
            if (item && !isEmpty_String(&item->url)) {
                const iDocumentWidget *doc = findWidget_App(cstr_String(&item->meta));
                if (!doc) return iTrue; /* something's stale? */
                makeBookmarkCreation_Widget(&item->url,
                                            bookmarkTitle_DocumentWidget(doc),
                                            siteIcon_GmDocument(document_DocumentWidget(doc)));
            }
            return iTrue;
        }
        else if (isCommand_Widget(w, ev, "opendocs.swap")) {
            const iSidebarItem *item = d->contextItem;
            if (item) {
                postCommandf_App("tabs.switch id:%s", cstr_String(&item->meta));
                postCommandf_App("tabs.swap newwindow:%d", argLabel_Command(cmd, "newwindow"));
            }
            return iTrue;
        }
        else if (isCommand_Widget(w, ev, "opendocs.dup")) {
            const iSidebarItem *item = d->contextItem;
            if (item) {
                postCommandf_App("tabs.switch id:%s", cstr_String(&item->meta));
                postCommand_App("tabs.new duplicate:1");
            }
            return iTrue;
        }
        else if (isCommand_Widget(w, ev, "opendocs.close")) {
            const iSidebarItem *item = d->contextItem;
            if (item) {
                const int              toLeft  = argLabel_Command(cmd, "toleft");
                const int              toRight = argLabel_Command(cmd, "toright");
                const iDocumentWidget *doc     = findWidget_App(cstr_String(&item->meta));
                if (!doc) return iTrue;
                // postCommandf_App("tabs.switch id:%s", cstr_String(&item->meta));
                postCommandf_App("tabs.close id:%s toleft:%d toright:%d",
                                 cstr_String(&item->meta),
                                 toLeft,
                                 toRight);
            }
            return iTrue;
        }
        else if (isEdgeSwipable_SidebarWidget_(d) && hasAffinity_Touch(w) &&
                 equal_Command(cmd, "edgeswipe.moved") && argLabel_Command(cmd, "edge") &&
                 !isVisible_Widget(w)) {
            const int side  = argLabel_Command(cmd, "side");
            const int delta = arg_Command(cmd);
            if (d->side == left_SidebarSide && side == 1 && delta > 0) {
                postCommand_Widget(w, "sidebar.toggle");
            }
            else if (d->side == right_SidebarSide && side == 2 && delta < 0) {
                postCommand_Widget(w, "sidebar2.toggle");
            }
            return iTrue;
        }
        else if (isEdgeSwipable_SidebarWidget_(d) && equal_Command(cmd, "listswipe.moved") &&
                 isVisible_Widget(w) && contains_Widget(w, coord_Command(cmd))) {
            const int delta = arg_Command(cmd);
            if (d->side == left_SidebarSide && delta < 0) {
                postCommand_Widget(w, "sidebar.toggle");
            }
            else if (d->side == right_SidebarSide && delta > 0) {
                postCommand_Widget(w, "sidebar2.toggle");
            }
            return iTrue;
        }
#if defined(iPlatformTerminal)
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
                             item ? (item->listItem.flags.isSeparator ? SDL_SYSTEM_CURSOR_ARROW
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
        if (isSlidingSheet_SidebarWidget_(d) && ev->button.button == SDL_BUTTON_LEFT &&
            isVisible_Widget(d) && !contains_Widget(w, init_I2(ev->button.x, ev->button.y))) {
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
                if (isMobile_Platform()) {
                    setCursorItem_ListWidget(d->list, hoverItemIndex_ListWidget(d->list));
                }
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
                else if (d->mode == feedEntries_SidebarMode && d->contextItem) {
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
    if (isEdgeSwipable_SidebarWidget_(d)) {
        if (ev->type == SDL_MOUSEWHEEL && isPerPixel_MouseWheelEvent(&ev->wheel)) {
            if (d->side == left_SidebarSide && ev->wheel.x < 0 && isVisible_Widget(w)) {
                postCommand_Widget(w, "sidebar.toggle");
                return iTrue;
            }
        }
    }
    else if (isSlidingSheet_SidebarWidget_(d)) {
        if (ev->type == SDL_MOUSEWHEEL) {
            enum iWidgetTouchMode touchMode = widgetMode_Touch(w);
            if (touchMode == momentum_WidgetTouchMode) {
                /* We don't do momentum. */
                float swipe = stopWidgetMomentum_Touch(w) / gap_UI;
                //                printf("swipe: %f\n", swipe);
                const iRangei midRegion = SlidingSheetMiddleRegion_SidebarWidget_(d);
                const int     pos       = top_Rect(w->rect);
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
                const int   top      = top_Rect(rootRect);
                if (w->rect.pos.y < top) {
                    setScrollMode_ListWidget(d->list, disabledAtTopUpwards_ScrollMode);
                    setScrollPos_ListWidget(d->list, top - w->rect.pos.y);
                    transferAffinity_Touch(w, as_Widget(d->list));
                    w->rect.pos.y  = top;
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
        if (ev->type == SDL_USEREVENT && ev->user.code == widgetTouchEnds_UserEventCode &&
            widgetMode_Touch(w) != momentum_WidgetTouchMode) {
            gotoNearestSlidingSheetPos_SidebarWidget_(d);
            return iTrue;
        }
    }
    if (ev->type == SDL_MOUSEBUTTONDOWN &&
        contains_Widget(as_Widget(d->list), init_I2(ev->button.x, ev->button.y))) {
        if (hoverItem_ListWidget(d->list) || isVisible_Widget(d->menu) ||
            isVisible_Widget(d->folderMenu)) {
            iWidget *contextMenu = d->menu;
            /* Update the menu before opening. */
            /* TODO: This kind of updating is already done above, and in `updateContextMenu_`... */
            const iSidebarItem *hoverItem = hoverItem_ListWidget(d->list);
            if (d->mode == bookmarks_SidebarMode) {
                if (!hoverItem) {
                    return iTrue;
                }
                const iBookmark *bm = get_Bookmarks(bookmarks_App(), hoverItem->id);
                if (!bm) {
                    return iTrue;
                }
                if (isFolder_Bookmark(bm)) {
                    contextMenu = d->folderMenu;
                }
                else if (!isVisible_Widget(d->menu)) {
                    const iBool        isRemote        = (bm->flags & remote_BookmarkFlag) != 0;
                    static const char *localOnlyCmds[] = { "bookmark.edit",
                                                           "bookmark.delete",
                                                           "bookmark.tag tag:subscribed",
                                                           "bookmark.tag tag:homepage",
                                                           "bookmark.tag tag:remotesource" };
                    iForIndices(i, localOnlyCmds) {
                        setFlags_Widget(
                            as_Widget(findMenuItem_Widget(contextMenu, localOnlyCmds[i])),
                            disabled_WidgetFlag, /* Remote bookmarks have limitations. */
                            isRemote);
                    }
                }
            }
            else if (d->mode == siteStructure_SidebarMode) {
                if (hoverItem && hoverItem->id & 2 /* unfolded */) {
                    contextMenu = d->folderMenu;
                }
            }
            processContextMenuEvent_Widget(contextMenu, ev, {});
        }
        else if (!constHoverItem_ListWidget(d->list) || isVisible_Widget(d->modeMenu)) {
            /* Show a more generic context menu that is unrelated to any particular item. */
            processContextMenuEvent_Widget(d->modeMenu, ev, {});
        }
    }
    return processEvent_Widget(w, ev);
}

static void draw_SidebarWidget_(const iSidebarWidget *d) {
    const iWidget *w      = constAs_Widget(d);
    const iRect    bounds = bounds_Widget(w);
    iPaint         p;
    init_Paint(&p);
    if (!isPortraitPhone_App()) { /* this would erase page contents during transition on the phone
                                   */
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

static void drawDragHandle_(iPaint *p, iRect *itemRect, int bg) {
    const int itemHeight = height_Rect(*itemRect);
    iRect dragRect = { addX_I2(topRight_Rect(*itemRect), -itemHeight * 3 / 2),
                       init_I2(itemHeight * 3 / 2, itemHeight) };
    fillRect_Paint(p, dragRect, bg);
    drawVLine_Paint(p, topLeft_Rect(dragRect), height_Rect(dragRect), uiSeparator_ColorId);
    drawCentered_Text(uiContent_FontId, dragRect, iTrue, uiAnnotation_ColorId, menu_Icon);
    /* Adjust the item rectangle to account for the handle. */
    adjustEdges_Rect(itemRect, 0, -width_Rect(dragRect), 0, 0);
}

static void draw_SidebarItem_(const iSidebarItem *d, iPaint *p, iRect itemRect,
                              const iListWidget *list) {
    const iSidebarWidget *sidebar =
        findParentClass_Widget(constAs_Widget(list), &Class_SidebarWidget);
    const iBool isListFocus   = isFocused_Widget(list);
    const iBool isMenuVisible = isVisible_Widget(sidebar->menu);
    const iBool isDragging    = constDragItem_ListWidget(list) == d;
    const iBool isEditing     = sidebar->isEditing; /* only on mobile */
    const iBool isPressing    = isMouseDown_ListWidget(list) && !isDragging;
    const iBool isHover       = (!isMenuVisible && isHover_Widget(constAs_Widget(list)) &&
                           constHoverItem_ListWidget(list) == d) ||
                          (isMenuVisible && sidebar->contextItem == d) ||
                          (isFocused_Widget(list) && constCursorItem_ListWidget(list) == d) ||
                          isDragging;
    const int scrollBarWidth = scrollBarWidth_ListWidget(list);
    const int blankWidth     = isApple_Platform() ? 0 : scrollBarWidth;
    const int itemHeight     = height_Rect(itemRect);
    const int font           = sidebar->itemFonts[d->isBold ? 1 : 0];
    const int iconColor =
        isHover ? (isPressing ? uiTextPressed_ColorId : uiIconHover_ColorId) : uiIcon_ColorId;
    /* Draw item background. */
    int bg = uiBackgroundSidebar_ColorId;
    if (isHover) {
        bg = isPressing ? uiBackgroundPressed_ColorId : uiBackgroundFramelessHover_ColorId;
        fillRect_Paint(p, itemRect, bg);
    }
    else if (d->listItem.flags.isSelected && (sidebar->mode == feedEntries_SidebarMode ||
                                              sidebar->mode == identities_SidebarMode)) {
        bg = uiBackgroundUnfocusedSelection_ColorId;
        fillRect_Paint(p, itemRect, bg);
    }
    else if (sidebar->mode == bookmarks_SidebarMode) {
        if (d->indent) /* remote icon */ {
            bg = uiBackgroundFolder_ColorId;
            fillRect_Paint(p, itemRect, bg);
        }
    }
    else if (sidebar->mode == siteStructure_SidebarMode) {
        if (d->indent >= 2) {
            bg = uiBackgroundFolder_ColorId;
            fillRect_Paint(p, itemRect, bg);
        }
    }
    /* Draw item contents. */
    iInt2 pos = itemRect.pos;
    if (sidebar->mode == documentOutline_SidebarMode) {
        const int level = d->indent / (5 * gap_UI);
        const int fg = isHover ? (isPressing ? uiTextPressed_ColorId : uiTextFramelessHover_ColorId)
                               : (level == 0   ? uiTextStrong_ColorId
                                  : level == 1 ? uiTextStrong_ColorId
                                  : level == 2 ? uiText_ColorId
                                               : uiTextDim_ColorId);
        drawRange_Text(font,
                       init_I2(pos.x + (3 * gap_UI + d->indent) * aspect_UI,
                               mid_Rect(itemRect).y - lineHeight_Text(font) / 2),
                       fg,
                       range_String(&d->label));
    }
    else if (sidebar->mode == feedEntries_SidebarMode) {
        const int fg = isHover ? (isPressing ? uiTextPressed_ColorId : uiTextFramelessHover_ColorId)
                               : uiText_ColorId;
        const int iconPad = 12 * gap_UI;
        if (d->listItem.flags.isSeparator) {
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
            const iBool isUnread  = (d->indent != 0);
            const int   titleFont = sidebar->itemFonts[isUnread ? 1 : 0];
            const int   h1        = lineHeight_Text(uiLabel_FontId);
            const int   h2        = lineHeight_Text(titleFont);
            iRect       iconArea  = { addY_I2(pos, 0), init_I2(iconPad * aspect_UI, itemHeight) };
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
                                  isHover && isPressing          ? iconColor
                                  : isUnread                     ? unreadIconColor
                                  : d->listItem.flags.isSelected ? iconColor
                                                                 : readIconColor,
                                  "%s",
                                  cstr_String(&str));
                deinit_String(&str);
            }
            /* Select the layout based on how the title fits. */
            int   metaFg    = isPressing ? fg : uiSubheading_ColorId;
            iInt2 titleSize = measureRange_Text(titleFont, range_String(&d->label)).bounds.size;
            const iInt2 metaSize =
                measureRange_Text(uiLabel_FontId, range_String(&d->meta)).bounds.size;
            pos.x += iconPad * aspect_UI;
            const int avail = width_Rect(itemRect) - iconPad - 3 * gap_UI;
            const int labelFg =
                isPressing ? fg : (isUnread ? uiTextStrong_ColorId : uiText_ColorId);
            if (titleSize.x > avail && metaSize.x < avail * 0.75f) {
                /* Must wrap the title. */
                pos.y += (itemHeight - h2 - h2) / 2;
                draw_Text(uiLabel_FontId,
                          addY_I2(pos, h2 - h1 - gap_UI / 8),
                          metaFg,
                          "%s \u2014 ",
                          cstr_String(&d->meta));
                int         skip = metaSize.x + measure_Text(uiLabel_FontId, " \u2014 ").advance.x;
                iInt2       cur  = addX_I2(pos, skip);
                const char *endPos;
                tryAdvance_Text(titleFont, range_String(&d->label), avail - skip, &endPos);
                drawRange_Text(
                    titleFont, cur, labelFg, (iRangecc) { constBegin_String(&d->label), endPos });
                if (endPos < constEnd_String(&d->label)) {
                    drawRange_Text(titleFont,
                                   addY_I2(pos, h2),
                                   labelFg,
                                   (iRangecc) { endPos, constEnd_String(&d->label) });
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
                       : d->listItem.flags.isDropTarget ? uiHeading_ColorId
                                                        : uiText_ColorId;
        /* The icon. */
        iString str;
        init_String(&str);
        appendChar_String(&str, d->icon ? d->icon : 0x1f588);
        const int   leftIndent = d->indent * gap_UI * 4 * aspect_UI;
        const iRect iconArea   = { addX_I2(pos, aspect_UI * gap_UI + leftIndent),
                                   init_I2(!isTerminal_Platform() ? 1.75f * lineHeight_Text(font) : 5,
                                           itemHeight) };
        drawCentered_Text(font,
                          iconArea,
                          iTrue,
                          isPressing                       ? iconColor
                          : d->icon == 0x2913 /* remote */ ? uiTextCaution_ColorId
                                                           : iconColor,
                          "%s",
                          cstr_String(&str));
        deinit_String(&str);
        const iInt2 textPos =
            addY_I2(topRight_Rect(iconArea), (itemHeight - lineHeight_Text(font)) / 2);
        drawRange_Text(font, textPos, fg, range_String(&d->label));
        const int metaFont      = uiLabel_FontId;
        const int metaIconWidth = 4.5f * gap_UI * aspect_UI;
        if (isEditing) {
            drawDragHandle_(p, &itemRect, bg);
        }
        const iInt2 metaPos =
            init_I2(right_Rect(itemRect) - length_String(&d->meta) * metaIconWidth - 2 * gap_UI -
                        (blankWidth ? blankWidth - 1.5f * gap_UI : (gap_UI / 2)),
                    textPos.y);
        if (!isDragging) {
            fillRect_Paint(p,
                           init_Rect(metaPos.x,
                                     top_Rect(itemRect),
                                     right_Rect(itemRect) - metaPos.x,
                                     height_Rect(itemRect)),
                           bg);
        }
        iInt2                mpos = metaPos;
        iStringConstIterator iter;
        init_StringConstIterator(&iter, &d->meta);
        iRangecc range = { cstr_String(&d->meta), iter.pos };
        while (iter.value) {
            next_StringConstIterator(&iter);
            range.end       = iter.pos;
            iRect iconArea  = { mpos, init_I2(metaIconWidth, lineHeight_Text(metaFont)) };
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
        if (d->listItem.flags.isSeparator) {
            if (!isEmpty_String(&d->meta)) {
                iInt2 drawPos = addY_I2(topLeft_Rect(itemRect), d->id);
                drawHLine_Paint(p,
                                addY_I2(drawPos, -gap_UI * aspect_UI),
                                width_Rect(itemRect) - blankWidth,
                                uiSeparator_ColorId);
                drawRange_Text(
                    uiLabelLargeBold_FontId,
                    add_I2(drawPos,
                           init_I2(3 * gap_UI * aspect_UI,
                                   1 + (itemHeight - lineHeight_Text(uiLabelLargeBold_FontId)) /
                                           2.0f)),
                    uiIcon_ColorId,
                    range_String(&d->meta));
            }
        }
        else {
            const int fg = isHover
                               ? (isPressing ? uiTextPressed_ColorId : uiTextFramelessHover_ColorId)
                               : uiTextDim_ColorId;
            iUrl      parts;
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
                draw_Text(font,
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
    else if (sidebar->mode == siteStructure_SidebarMode) {
        const iBool isActive   = (d->id & 1) != 0;
        const iBool isUnfolded = (d->id & 2) != 0;
        const int fg = isHover ? (isPressing ? uiTextPressed_ColorId : uiTextFramelessHover_ColorId)
                       : d->indent == 0 || d->isBold || isUnfolded ? uiTextStrong_ColorId
                       : d->count > 0
                           ? (d->indent <= 2 ? uiTextStrong_ColorId : uiTextAction_ColorId)
                           : uiTextDim_ColorId;
        const int fg2 = isPressing ? uiTextPressed_ColorId
                        : isHover  ? uiTextFramelessHover_ColorId
                                   : uiAnnotation_ColorId;
        const int fg3 = isPressing   ? uiTextPressed_ColorId
                        : isHover    ? isUnfolded ? uiText_ColorId : uiAnnotation_ColorId
                        : isUnfolded ? uiText_ColorId
                                     : uiAnnotation_ColorId;
        if (isActive && !isHover && !isPressing) {
            fillRect_Paint(p, itemRect, uiBackgroundUnfocusedSelection_ColorId);
        }
        const iInt2 pos =
            add_I2(topLeft_Rect(itemRect),
                   init_I2(3 * gap_UI * aspect_UI + d->indent * 5 * gap_UI * aspect_UI,
                           (itemHeight - lineHeight_Text(font)) / 2));
        const int span = measureRange_Text(font, range_String(&d->label)).advance.x;
        drawRange_Text(font, pos, fg, range_String(&d->label));
        if (d->indent > 0) {
            if (isUnfolded || d->count > 0) {
                drawCenteredRange_Text(
                    font,
                    initCorners_Rect(
                        addX_I2(pos,
                                gap_UI * (isSlidingSheet_SidebarWidget_(sidebar) ? -7 : -6) *
                                    aspect_UI),
                        init_I2(pos.x, pos.y + lineHeight_Text(font))),
                    iTrue,
                    fg3,
                    range_CStr(isUnfolded ? downAngle_Icon : rightAngle_Icon));
            }
            if (d->count > 0) {
                draw_Text(uiLabel_FontId,
                          add_I2(pos,
                                 init_I2(span + gap_UI * aspect_UI,
                                         ascent_Text(font) - ascent_Text(uiLabel_FontId))),
                          fg2,
                          " (%d)",
                          d->count);
            }
        }
    }
    else if (sidebar->mode == openDocuments_SidebarMode) {
        const int fg = isPressing  ? uiTextPressed_ColorId
                       : d->isBold ? uiTextStrong_ColorId /* unseen */
                       : d->indent ? uiTextStrong_ColorId /* active */
                       : isHover   ? uiTextFramelessHover_ColorId
                                   : uiText_ColorId;
        if (d->indent && !isPressing && !isHover) {
            fillRect_Paint(p, itemRect, bg = uiBackgroundUnfocusedSelection_ColorId);
        }
        const iInt2 textPos = add_I2(topLeft_Rect(itemRect),
                                     init_I2(3 * gap_UI, (itemHeight - lineHeight_Text(font)) / 2));
        iString     label;
        init_String(&label);
        set_String(&label, &d->label);
        const iRangecc host = urlHost_String(&d->url);
        appendFormat_String(&label,
                            " %s%s%s",
                            escape_Color(isPressing  ? uiTextPressed_ColorId
                                         : isHover   ? uiTextFramelessHover_ColorId
                                         : d->isBold ? uiAnnotation_ColorId
                                         : d->indent ? uiAnnotation_ColorId
                                                     : uiTextShortcut_ColorId),
                            !isEmpty_Range(&host) ? "\u2014 " : "",
                            cstr_Rangecc(host));
        drawRange_Text(font, textPos, fg, range_String(&label));
        deinit_String(&label);
        if (isMobile_Platform()) {
            drawDragHandle_(p, &itemRect, bg);
        }
        /* Status icons appear on the right. */
        const int metaIconWidth = 7.0f * gap_UI * aspect_UI + blankWidth;
        iInt2     metaIconPos =
            init_I2(right_Rect(itemRect) - metaIconWidth + 0.5f * gap_UI * aspect_UI, textPos.y);
        iRect metaIconRect = initCorners_Rect(addX_I2(topRight_Rect(itemRect), -metaIconWidth),
                                              bottomRight_Rect(itemRect));
        if (d->id) { /* used for status flags */
            fillRect_Paint(
                p,
                metaIconRect,
                d->indent && !isPressing && !isHover ? uiBackgroundUnfocusedSelection_ColorId : bg);
            draw_Text(font,
                      metaIconPos,
                      uiTextAction_ColorId,
                      d->id & 2 ? "\U0001f50a" /* audio speaker, high volume */ : reload_Icon);
        }
    }
    else if (sidebar->mode == subscriptions_SidebarMode) {
        const int fg1   = isPressing  ? uiTextPressed_ColorId
                          : d->isBold ? uiTextStrong_ColorId
                                      : uiText_ColorId;
        const int fg2   = isPressing ? uiTextPressed_ColorId : uiText_ColorId;
        const int fg3   = isPressing  ? uiTextPressed_ColorId
                          : d->isBold ? uiIcon_ColorId
                                      : uiTextDim_ColorId;
        const int font2 = uiLabel_FontId;
        iString   str;
        init_String(&str);
        appendChar_String(&str, d->icon ? d->icon : 0x1f588);
        iInt2 pos = add_I2(
            topLeft_Rect(itemRect),
            init_I2(10 * gap_UI * aspect_UI,
                    (height_Rect(itemRect) - lineHeight_Text(font) - lineHeight_Text(font2)) / 2));
        const iRect iconRect =
            initCorners_Rect(init_I2(left_Rect(itemRect) + gap_UI * aspect_UI, pos.y),
                             init_I2(pos.x, pos.y + lineHeight_Text(font)));
        drawCenteredRange_Text(font, iconRect, iTrue, fg3, range_String(&str));
        clear_String(&str);
        if (d->count > 0) {
            setCStr_String(&str, formatCStr_Lang("num.entries.n", d->count));
        }
        append_String(&str, &d->meta);
        drawRange_Text(font, pos, fg1, range_String(&d->label));
        pos.y += lineHeight_Text(font);
        drawRange_Text(font2, pos, fg2, range_String(&str));
        deinit_String(&str);
    }
    if (isListFocus && isHover && constCursorItem_ListWidget(list) == d && !isTerminal_Platform()) {
        /* Visualize the keyboard cursor. */
        drawRect_Paint(p, shrunk_Rect(itemRect, one_I2()), uiTextAction_ColorId);
    }
}

iBeginDefineSubclass(SidebarWidget, Widget).processEvent = (iAny *) processEvent_SidebarWidget_,
                                    .draw                = (iAny *) draw_SidebarWidget_,
                                    iEndDefineSubclass(SidebarWidget)
