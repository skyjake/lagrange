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
#include "command.h"
#include "documentwidget.h"
#include "feeds.h"
#include "gmcerts.h"
#include "gmutil.h"
#include "gmdocument.h"
#include "inputwidget.h"
#include "labelwidget.h"
#include "listwidget.h"
#include "paint.h"
#include "scrollwidget.h"
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
    iString   label;
    iString   meta;
    iString   url;
};

void init_SidebarItem(iSidebarItem *d) {
    init_ListItem(&d->listItem);
    d->id     = 0;
    d->indent = 0;
    d->icon   = 0;
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

struct Impl_SidebarWidget {
    iWidget           widget;
    enum iSidebarSide side;
    enum iSidebarMode mode;
    iString           cmdPrefix;
    iWidget *         blank;
    iListWidget *     list;
    int               modeScroll[max_SidebarMode];
    iLabelWidget *    modeButtons[max_SidebarMode];
    int               maxButtonLabelWidth;
    int               width;
    iWidget *         resizer;
    iWidget *         menu;
    iSidebarItem *    contextItem; /* list item accessed in the context menu */
};

iDefineObjectConstructionArgs(SidebarWidget, (enum iSidebarSide side), side)

static iBool isResizing_SidebarWidget_(const iSidebarWidget *d) {
    return (flags_Widget(d->resizer) & pressed_WidgetFlag) != 0;
}

static int cmpTitle_Bookmark_(const iBookmark **a, const iBookmark **b) {
    return cmpStringCase_String(&(*a)->title, &(*b)->title);
}

static void updateItems_SidebarWidget_(iSidebarWidget *d) {
    clear_ListWidget(d->list);
    releaseChildren_Widget(d->blank);
    destroy_Widget(d->menu);
    d->menu = NULL;
    switch (d->mode) {
        case feeds_SidebarMode: {
            const iString *docUrl = url_DocumentWidget(document_App());
            iTime now;
            iDate on;
            initCurrent_Time(&now);
            init_Date(&on, &now);
            const int thisYear = on.year;
            iZap(on);
            iConstForEach(PtrArray, i, listEntries_Feeds()) {
                const iFeedEntry *entry = i.ptr;
                if (isHidden_FeedEntry(entry)) {
                    continue; /* A hidden entry. */
                }
                /* For more items, one can always see "about:feeds". A large number of items
                   is a bit difficult to navigate in the sidebar. */
                if (numItems_ListWidget(d->list) == 100) {
                    break;
                }
                /* Exclude entries that are too old for Visited to keep track of. */
                if (secondsSince_Time(&now, &entry->discovered) > maxAge_Visited) {
                    break; /* the rest are even older */
                }
                /* Insert date separators. */ {
                    iDate entryDate;
                    init_Date(&entryDate, &entry->posted);
                    if (on.year != entryDate.year || on.month != entryDate.month ||
                        on.day != entryDate.day) {
                        on = entryDate;
                        iSidebarItem *sep = new_SidebarItem();
                        sep->listItem.isSeparator = iTrue;
                        iString *text = format_Date(&on, on.year == thisYear ? "%b. %d" : "%b. %d, %Y");
                        set_String(&sep->meta, text);
                        delete_String(text);
                        addItem_ListWidget(d->list, sep);
                        iRelease(sep);
                    }
                }
                iSidebarItem *item = new_SidebarItem();
                if (equal_String(docUrl, &entry->url)) {
                    item->listItem.isSelected = iTrue; /* currently being viewed */
                }
                item->indent = isUnread_FeedEntry(entry);
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
            }
            d->menu = makeMenu_Widget(
                as_Widget(d),
                (iMenuItem[]){ { "Open Entry in New Tab", 0, 0, "feed.entry.opentab" },
                               { "Open Feed Page", 0, 0, "feed.entry.openfeed" },
                               { "Mark as Read", 0, 0, "feed.entry.toggleread" },
                               { "Add Bookmark...", 0, 0, "feed.entry.bookmark" },
                               { "---", 0, 0, NULL },
                               { "Edit Feed...", 0, 0, "feed.entry.edit" },
                               { uiTextCaution_ColorEscape "Unsubscribe...", 0, 0, "feed.entry.unsubscribe" },
                               { "---", 0, 0, NULL },
                               { "Mark All as Read", SDLK_a, KMOD_SHIFT, "feeds.markallread" },
                               { "Refresh Feeds", SDLK_r, KMOD_PRIMARY | KMOD_SHIFT, "feeds.refresh" } },
                10);
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
                addItem_ListWidget(d->list, item);
                iRelease(item);
            }
            break;
        }
        case bookmarks_SidebarMode: {
            iRegExp *homeTag = iClob(new_RegExp("\\bhomepage\\b", caseSensitive_RegExpOption));
            iRegExp *subTag  = iClob(new_RegExp("\\bsubscribed\\b", caseSensitive_RegExpOption));
            iRegExp *remoteSourceTag = iClob(new_RegExp("\\bremotesource\\b", caseSensitive_RegExpOption));
            iConstForEach(PtrArray, i, list_Bookmarks(bookmarks_App(), cmpTitle_Bookmark_, NULL, NULL)) {
                const iBookmark *bm = i.ptr;
                iSidebarItem *item = new_SidebarItem();
                item->id = id_Bookmark(bm);
                item->icon = bm->icon;
                set_String(&item->url, &bm->url);
                set_String(&item->label, &bm->title);
                /* Icons for special tags. */ {
                    iRegExpMatch m;
                    init_RegExpMatch(&m);
                    if (matchString_RegExp(subTag, &bm->tags, &m)) {
                        appendChar_String(&item->meta, 0x2605);
                    }
                    init_RegExpMatch(&m);
                    if (matchString_RegExp(homeTag, &bm->tags, &m)) {
                        appendChar_String(&item->meta, 0x1f3e0);
                    }
                    init_RegExpMatch(&m);
                    if (matchString_RegExp(remoteSourceTag, &bm->tags, &m)) {
                        appendChar_String(&item->meta, 0x2601);
                    }
                }
                addItem_ListWidget(d->list, item);
                iRelease(item);
            }
            d->menu = makeMenu_Widget(
                as_Widget(d),
                (iMenuItem[]){ { "Open in New Tab", 0, 0, "bookmark.open newtab:1" },
                               { "Open in Background Tab", 0, 0, "bookmark.open newtab:2" },
                               { "---", 0, 0, NULL },
                               { "Edit Bookmark...", 0, 0, "bookmark.edit" },
                               { "Copy URL", 0, 0, "bookmark.copy" },
                               { "---", 0, 0, NULL },
                               { "?", 0, 0, "bookmark.tag tag:subscribed" },
                               { "?", 0, 0, "bookmark.tag tag:homepage" },
                               { "?", 0, 0, "bookmark.tag tag:remotesource" },
                               { "---", 0, 0, NULL },
                               { uiTextCaution_ColorEscape "Delete Bookmark", 0, 0, "bookmark.delete" },
                               { "---", 0, 0, NULL },
                               { "Refresh Remote Bookmarks", 0, 0, "bookmarks.reload.remote" } },
               13);
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
                    const iString *text = collect_String(format_Date(
                        &date, date.year != thisYear ? "%b. %d, %Y" : "%b. %d"));
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
            d->menu = makeMenu_Widget(
                as_Widget(d),
                (iMenuItem[]){
                    { "Copy URL", 0, 0, "history.copy" },
                    { "Add Bookmark...", 0, 0, "history.addbookmark" },
                    { "---", 0, 0, NULL },
                    { "Forget URL", 0, 0, "history.delete" },
                    { "---", 0, 0, NULL },
                    { uiTextCaution_ColorEscape "Clear History...", 0, 0, "history.clear confirm:1" },
                }, 6);
            break;
        }
        case identities_SidebarMode: {
            const iString *tabUrl = url_DocumentWidget(document_App());
            iConstForEach(PtrArray, i, identities_GmCerts(certs_App())) {
                const iGmIdentity *ident = i.ptr;
                iSidebarItem *item = new_SidebarItem();
                item->id = index_PtrArrayConstIterator(&i);
                item->icon = ident->icon;
                set_String(&item->label, collect_String(subject_TlsCertificate(ident->cert)));
                iDate until;
                validUntil_TlsCertificate(ident->cert, &until);
                const iBool isActive = isUsedOn_GmIdentity(ident, tabUrl);
                format_String(
                    &item->meta,
                    "%s",
                    isActive ? "Using on this page"
                             : isUsed_GmIdentity(ident)
                                   ? format_CStr("Used on %zu URLs", size_StringSet(ident->useUrls))
                                   : "Not used");
                const char *expiry =
                    ident->flags & temporary_GmIdentityFlag
                        ? "Temporary"
                        : cstrCollect_String(format_Date(&until, "Expires %b %d, %Y"));
                if (isEmpty_String(&ident->notes)) {
                    appendFormat_String(&item->meta, "\n%s", expiry);
                }
                else {
                    appendFormat_String(&item->meta,
                                        " \u2014 %s\n%s%s",
                                        expiry,
                                        escape_Color(uiHeading_ColorId),
                                        cstr_String(&ident->notes));
                }
                item->listItem.isSelected = isActive;
                addItem_ListWidget(d->list, item);
                iRelease(item);
            }
            const iMenuItem menuItems[] = {
                { "Use on This Page", 0, 0, "ident.use arg:1" },
                { "Stop Using on This Page", 0, 0, "ident.use arg:0" },
                { "Stop Using Everywhere", 0, 0, "ident.use arg:0 clear:1" },
                { "Show Usage", 0, 0, "ident.showuse" },
                { "---", 0, 0, NULL },
                { "Edit Notes...", 0, 0, "ident.edit" },
//                { "Pick Icon...", 0, 0, "ident.pickicon" },
                { "---", 0, 0, NULL },
                //{ "Reveal Files", 0, 0, "ident.reveal" },
                { uiTextCaution_ColorEscape "Delete Identity...", 0, 0, "ident.delete confirm:1" },
            };
            d->menu = makeMenu_Widget(as_Widget(d), menuItems, iElemCount(menuItems));
            break;
        }
        default:
            break;
    }
    updateVisible_ListWidget(d->list);
    invalidate_ListWidget(d->list);
    /* Content for a blank tab. */
    if (isEmpty_ListWidget(d->list)) {
        if (d->mode == feeds_SidebarMode) {
            iWidget *div = makeVDiv_Widget();
            setPadding_Widget(div, 3 * gap_UI, 0, 3 * gap_UI, 2 * gap_UI);
            addChildFlags_Widget(div, iClob(new_Widget()), expand_WidgetFlag); /* pad */
            addChild_Widget(div, iClob(new_LabelWidget("Refresh Feeds", "feeds.refresh")));
            addChildFlags_Widget(div, iClob(new_Widget()), expand_WidgetFlag); /* pad */
            addChild_Widget(d->blank, iClob(div));
        }
        else if (d->mode == identities_SidebarMode) {
            iWidget *div = makeVDiv_Widget();
            setPadding_Widget(div, 3 * gap_UI, 0, 3 * gap_UI, 2 * gap_UI);
            addChildFlags_Widget(div, iClob(new_Widget()), expand_WidgetFlag); /* pad */
            iLabelWidget *msg = new_LabelWidget("No Identities", NULL);
            setFont_LabelWidget(msg, uiLabelLarge_FontId);
            addChildFlags_Widget(div, iClob(msg), frameless_WidgetFlag);
            addChild_Widget(div, iClob(makePadding_Widget(3 * gap_UI)));
            addChild_Widget(div, iClob(new_LabelWidget("New Identity...", "ident.new")));
            addChildFlags_Widget(div, iClob(new_Widget()), expand_WidgetFlag); /* pad */
            addChildFlags_Widget(
                div,
                iClob(new_LabelWidget("See " uiTextStrong_ColorEscape "Help" uiText_ColorEscape
                                      " for more information about TLS client certificates.",
                                      "!open newtab:1 gotoheading:1.6 url:about:help")),
                frameless_WidgetFlag | fixedHeight_WidgetFlag | wrapText_WidgetFlag);
            addChild_Widget(d->blank, iClob(div));
        }
        arrange_Widget(d->blank);
    }
}

iBool setMode_SidebarWidget(iSidebarWidget *d, enum iSidebarMode mode) {
    if (d->mode == mode) {
        return iFalse;
    }
    if (d->mode >= 0 && d->mode < max_SidebarMode) {
        d->modeScroll[d->mode] = scrollPos_ListWidget(d->list); /* saved for later */
    }
    d->mode = mode;
    for (enum iSidebarMode i = 0; i < max_SidebarMode; i++) {
        setFlags_Widget(as_Widget(d->modeButtons[i]), selected_WidgetFlag, i == d->mode);
    }
    const float heights[max_SidebarMode] = { 1.333f, 2.333f, 1.333f, 3.5f, 1.2f };
    setBackgroundColor_Widget(as_Widget(d->list),
                              d->mode == documentOutline_SidebarMode ? tmBannerBackground_ColorId
                                                                     : uiBackground_ColorId);
    setItemHeight_ListWidget(d->list, heights[mode] * lineHeight_Text(uiContent_FontId));
    /* Restore previous scroll position. */
    setScrollPos_ListWidget(d->list, d->modeScroll[mode]);
    return iTrue;
}

enum iSidebarMode mode_SidebarWidget(const iSidebarWidget *d) {
    return d->mode;
}

int width_SidebarWidget(const iSidebarWidget *d) {
    return d->width;
}

static const char *normalModeLabels_[max_SidebarMode] = {
    "\U0001f588 Bookmarks",
    "\U00002605 Feeds",
    "\U0001f553 History",
    "\U0001f464 Identities",
    "\U0001f5b9 Outline",
};

static const char *tightModeLabels_[max_SidebarMode] = {
    "\U0001f588",
    "\U00002605",
    "\U0001f553",
    "\U0001f464",
    "\U0001f5b9",
};

void init_SidebarWidget(iSidebarWidget *d, enum iSidebarSide side) {
    iWidget *w = as_Widget(d);
    init_Widget(w);
    setId_Widget(w, side == left_SideBarSide ? "sidebar" : "sidebar2");
    initCopy_String(&d->cmdPrefix, id_Widget(w));
    appendChar_String(&d->cmdPrefix, '.');
    setBackgroundColor_Widget(w, none_ColorId);
    setFlags_Widget(w,
                    collapse_WidgetFlag | hidden_WidgetFlag | arrangeHorizontal_WidgetFlag |
                        resizeWidthOfChildren_WidgetFlag,
                    iTrue);
    iZap(d->modeScroll);
    d->side = side;
    d->mode  = -1;
    d->width = 60 * gap_UI;
    setFlags_Widget(w, fixedWidth_WidgetFlag, iTrue);
    d->maxButtonLabelWidth = 0;
    iWidget *vdiv = makeVDiv_Widget();
    addChildFlags_Widget(w, vdiv, resizeToParentWidth_WidgetFlag | resizeToParentHeight_WidgetFlag);
    iWidget *buttons = new_Widget();
    for (int i = 0; i < max_SidebarMode; i++) {
        d->modeButtons[i] = addChildFlags_Widget(
            buttons,
            iClob(new_LabelWidget(
                tightModeLabels_[i],
                format_CStr("%s.mode arg:%d", cstr_String(id_Widget(w)), i))),
            frameless_WidgetFlag);
        d->maxButtonLabelWidth =
            iMaxi(d->maxButtonLabelWidth,
                  3 * gap_UI + measure_Text(uiLabel_FontId, normalModeLabels_[i]).x);
    }
    addChildFlags_Widget(vdiv,
                         iClob(buttons),
                         arrangeHorizontal_WidgetFlag | resizeWidthOfChildren_WidgetFlag |
                             arrangeHeight_WidgetFlag | resizeToParentWidth_WidgetFlag);
    iWidget *content = new_Widget();
    setFlags_Widget(content, resizeChildren_WidgetFlag, iTrue);
    d->list = new_ListWidget();
    setPadding_Widget(as_Widget(d->list), 0, gap_UI, 0, gap_UI);
    addChild_Widget(content, iClob(d->list));
    d->blank = new_Widget();
    addChildFlags_Widget(content, iClob(d->blank), resizeChildren_WidgetFlag);
    addChildFlags_Widget(vdiv, iClob(content), expand_WidgetFlag);
    setMode_SidebarWidget(d, bookmarks_SidebarMode);
    d->resizer =
        addChildFlags_Widget(w,
                             iClob(new_Widget()),
                             hover_WidgetFlag | commandOnClick_WidgetFlag | fixedWidth_WidgetFlag |
                                 resizeToParentHeight_WidgetFlag |
                                 (side == left_SideBarSide ? moveToParentRightEdge_WidgetFlag
                                                           : moveToParentLeftEdge_WidgetFlag));
    setId_Widget(d->resizer, side == left_SideBarSide ? "sidebar.grab" : "sidebar2.grab");
    d->resizer->rect.size.x = gap_UI;
    setBackgroundColor_Widget(d->resizer, none_ColorId);
    d->menu = NULL;
    addAction_Widget(w, SDLK_r, KMOD_PRIMARY | KMOD_SHIFT, "feeds.refresh");
}

void deinit_SidebarWidget(iSidebarWidget *d) {
    deinit_String(&d->cmdPrefix);
}

static const iGmIdentity *constHoverIdentity_SidebarWidget_(const iSidebarWidget *d) {
    if (d->mode == identities_SidebarMode) {
        const iSidebarItem *hoverItem = constHoverItem_ListWidget(d->list);
        if (hoverItem) {
            return identity_GmCerts(certs_App(), hoverItem->id);
        }
    }
    return NULL;
}

static iGmIdentity *menuIdentity_SidebarWidget_(const iSidebarWidget *d) {
    if (d->mode == identities_SidebarMode) {
        if (d->contextItem) {
            return identity_GmCerts(certs_App(), d->contextItem->id);
        }
    }
    return NULL;
}

static iGmIdentity *hoverIdentity_SidebarWidget_(const iSidebarWidget *d) {
    return iConstCast(iGmIdentity *, constHoverIdentity_SidebarWidget_(d));
}

static void itemClicked_SidebarWidget_(iSidebarWidget *d, const iSidebarItem *item) {
    setFocus_Widget(NULL);
    switch (d->mode) {
        case documentOutline_SidebarMode: {
            const iGmDocument *doc = document_DocumentWidget(document_App());
            const iGmHeading *head = constAt_Array(headings_GmDocument(doc), item->id);
            postCommandf_App("document.goto loc:%p", head->text.start);
            break;
        }
        case feeds_SidebarMode: {
            postCommandString_App(feedEntryOpenCommand_String(&item->url));
            break;
        }
        case bookmarks_SidebarMode:
        case history_SidebarMode: {
            if (!isEmpty_String(&item->url)) {
                postCommandf_App("open newtab:%d url:%s",
                                 openTabMode_Sym(SDL_GetModState()),
                                 cstr_String(&item->url));
            }
            break;
        }
        case identities_SidebarMode: {
            iGmIdentity *ident = hoverIdentity_SidebarWidget_(d);
            if (ident) {
                const iString *tabUrl = url_DocumentWidget(document_App());
                if (isUsedOn_GmIdentity(ident, tabUrl)) {
                    signOut_GmCerts(certs_App(), tabUrl);
                }
                else {
                    signIn_GmCerts(certs_App(), ident, tabUrl);
                }
                updateItems_SidebarWidget_(d);
                updateMouseHover_ListWidget(d->list);
            }
            break;
        }
        default:
            break;
    }
}

static void checkModeButtonLayout_SidebarWidget_(iSidebarWidget *d) {
    const iBool isTight =
        (width_Rect(bounds_Widget(as_Widget(d->modeButtons[0]))) < d->maxButtonLabelWidth);
    for (int i = 0; i < max_SidebarMode; i++) {
        if (isTight && ~flags_Widget(as_Widget(d->modeButtons[i])) & tight_WidgetFlag) {
            setFlags_Widget(as_Widget(d->modeButtons[i]), tight_WidgetFlag, iTrue);
            updateTextCStr_LabelWidget(d->modeButtons[i], tightModeLabels_[i]);
        }
        else if (!isTight && flags_Widget(as_Widget(d->modeButtons[i])) & tight_WidgetFlag) {
            setFlags_Widget(as_Widget(d->modeButtons[i]), tight_WidgetFlag, iFalse);
            updateTextCStr_LabelWidget(d->modeButtons[i], normalModeLabels_[i]);
        }
    }
}

void setWidth_SidebarWidget(iSidebarWidget *d, int width) {
    iWidget * w = as_Widget(d);
    /* Even less space if the other sidebar is visible, too. */
    const int otherWidth =
        width_Widget(findWidget_App(d->side == left_SideBarSide ? "sidebar2" : "sidebar"));
    width = iClamp(width, 30 * gap_UI, rootSize_Window(get_Window()).x - 50 * gap_UI - otherWidth);
    d->width = width;
    if (isVisible_Widget(w)) {
        w->rect.size.x = width;
    }
    arrange_Widget(findWidget_App("doctabs"));
    checkModeButtonLayout_SidebarWidget_(d);
    if (!isRefreshPending_App()) {
        updateSize_DocumentWidget(document_App());
        invalidate_ListWidget(d->list);
    }
}

iBool handleBookmarkEditorCommands_SidebarWidget_(iWidget *editor, const char *cmd) {
    if (equal_Command(cmd, "bmed.accept") || equal_Command(cmd, "cancel")) {
        iAssert(startsWith_String(id_Widget(editor), "bmed."));
        iSidebarWidget *d = findWidget_App(cstr_String(id_Widget(editor)) + 5); /* bmed.sidebar */
        if (equal_Command(cmd, "bmed.accept")) {
            const iString *title = text_InputWidget(findChild_Widget(editor, "bmed.title"));
            const iString *url   = text_InputWidget(findChild_Widget(editor, "bmed.url"));
            const iString *tags  = text_InputWidget(findChild_Widget(editor, "bmed.tags"));
            const iSidebarItem *item = hoverItem_ListWidget(d->list);
            iAssert(item); /* hover item cannot have been changed */
            iBookmark *bm = get_Bookmarks(bookmarks_App(), item->id);
            set_String(&bm->title, title);
            set_String(&bm->url, url);
            set_String(&bm->tags, tags);
            postCommand_App("bookmarks.changed");
        }
        setFlags_Widget(as_Widget(d), disabled_WidgetFlag, iFalse);
        destroy_Widget(editor);
        return iTrue;
    }
    return iFalse;
}

static iBool handleSidebarCommand_SidebarWidget_(iSidebarWidget *d, const char *cmd) {
    iWidget *w = as_Widget(d);
    if (equal_Command(cmd, "width")) {
        setWidth_SidebarWidget(d, arg_Command(cmd));
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
        return iTrue;
    }
    else if (equal_Command(cmd, "toggle")) {
        if (arg_Command(cmd) && isVisible_Widget(w)) {
            return iTrue;
        }
        setFlags_Widget(w, hidden_WidgetFlag, isVisible_Widget(w));
        if (isVisible_Widget(w)) {
            w->rect.size.x = d->width;
            invalidate_ListWidget(d->list);
        }
        arrange_Widget(w->parent);
        /* BUG: Rearranging because the arrange above didn't fully resolve the height. */
        arrange_Widget(w);
        updateSize_DocumentWidget(document_App());
        if (isVisible_Widget(w)) {
            updateItems_SidebarWidget_(d);
            scrollOffset_ListWidget(d->list, 0);
        }
        refresh_Widget(w->parent);
        return iTrue;
    }
    return iFalse;
}

static iBool processEvent_SidebarWidget_(iSidebarWidget *d, const SDL_Event *ev) {
    iWidget *w = as_Widget(d);
    /* Handle commands. */
    if (isResize_UserEvent(ev)) {
        checkModeButtonLayout_SidebarWidget_(d);
    }
    else if (ev->type == SDL_USEREVENT && ev->user.code == command_UserEventCode) {
        const char *cmd = command_UserEvent(ev);
        if (equal_Command(cmd, "tabs.changed") || equal_Command(cmd, "document.changed")) {
            updateItems_SidebarWidget_(d);
            scrollOffset_ListWidget(d->list, 0);
        }
        else if (equal_Command(cmd, "visited.changed") &&
                 (d->mode == history_SidebarMode || d->mode == feeds_SidebarMode)) {
            updateItems_SidebarWidget_(d);
        }
        else if (equal_Command(cmd, "bookmarks.changed") && (d->mode == bookmarks_SidebarMode ||
                                                             d->mode == feeds_SidebarMode)) {
            updateItems_SidebarWidget_(d);
        }
        else if (equal_Command(cmd, "idents.changed") && d->mode == identities_SidebarMode) {
            updateItems_SidebarWidget_(d);
        }
        else if (startsWith_CStr(cmd, cstr_String(&d->cmdPrefix))) {
            if (handleSidebarCommand_SidebarWidget_(d, cmd + size_String(&d->cmdPrefix))) {
                return iTrue;
            }
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
                    refresh_Widget(d->resizer);
                }
            }
            return iTrue;
        }
        else if (isCommand_Widget(w, ev, "mouse.moved")) {
            if (isResizing_SidebarWidget_(d)) {
                const iInt2 local = localCoord_Widget(w, coord_Command(cmd));
                const int resMid = d->resizer->rect.size.x / 2;
                setWidth_SidebarWidget(
                    d,
                    (d->side == left_SideBarSide
                         ? local.x
                         : (rootSize_Window(get_Window()).x - coord_Command(cmd).x)) +
                        resMid);
            }
            return iTrue;
        }
        else if (isCommand_Widget(w, ev, "list.clicked")) {
            itemClicked_SidebarWidget_(d, pointerLabel_Command(cmd, "item"));
            return iTrue;
        }
        else if (isCommand_Widget(w, ev, "menu.opened")) {
            setFlags_Widget(as_Widget(d->list), disabled_WidgetFlag, iTrue);
        }
        else if (isCommand_Widget(w, ev, "menu.closed")) {
            setFlags_Widget(as_Widget(d->list), disabled_WidgetFlag, iFalse);
        }
        else if (isCommand_Widget(w, ev, "bookmark.open")) {
            const iSidebarItem *item = d->contextItem;
            if (d->mode == bookmarks_SidebarMode && item) {
                postCommandf_App("open newtab:%d url:%s",
                                 argLabel_Command(cmd, "newtab"),
                                 cstr_String(&item->url));
            }
            return iTrue;
        }
        else if (isCommand_Widget(w, ev, "bookmark.copy")) {
            const iSidebarItem *item = d->contextItem;
            if (d->mode == bookmarks_SidebarMode && item) {
                SDL_SetClipboardText(cstr_String(&item->url));
            }
            return iTrue;
        }
        else if (isCommand_Widget(w, ev, "bookmark.edit")) {
            const iSidebarItem *item = d->contextItem;
            if (d->mode == bookmarks_SidebarMode && item) {
                setFlags_Widget(w, disabled_WidgetFlag, iTrue);
                iWidget *dlg = makeBookmarkEditor_Widget();
                setId_Widget(dlg, format_CStr("bmed.%s", cstr_String(id_Widget(w))));
                iBookmark *bm = get_Bookmarks(bookmarks_App(), item->id);
                setText_InputWidget(findChild_Widget(dlg, "bmed.title"), &bm->title);
                setText_InputWidget(findChild_Widget(dlg, "bmed.url"), &bm->url);
                setText_InputWidget(findChild_Widget(dlg, "bmed.tags"), &bm->tags);
                setCommandHandler_Widget(dlg, handleBookmarkEditorCommands_SidebarWidget_);
                setFocus_Widget(findChild_Widget(dlg, "bmed.title"));
            }
            return iTrue;
        }
        else if (isCommand_Widget(w, ev, "bookmark.tag")) {
            const iSidebarItem *item = d->contextItem;
            if (d->mode == bookmarks_SidebarMode && item) {
                const char *tag = cstr_String(string_Command(cmd, "tag"));
                iBookmark *bm = get_Bookmarks(bookmarks_App(), item->id);
                if (hasTag_Bookmark(bm, tag)) {
                    removeTag_Bookmark(bm, tag);
                    if (!iCmpStr(tag, "subscribed")) {
                        removeEntries_Feeds(item->id);
                    }
                }
                else {
                    addTag_Bookmark(bm, tag);
                }
                postCommand_App("bookmarks.changed");
            }
            return iTrue;
        }
        else if (isCommand_Widget(w, ev, "bookmark.delete")) {
            const iSidebarItem *item = d->contextItem;
            if (d->mode == bookmarks_SidebarMode && item && remove_Bookmarks(bookmarks_App(), item->id)) {
                removeEntries_Feeds(item->id);
                postCommand_App("bookmarks.changed");
            }
            return iTrue;
        }
        else if (equal_Command(cmd, "feeds.update.finished") && d->mode == feeds_SidebarMode) {
            updateItems_SidebarWidget_(d);
        }
        else if (equal_Command(cmd, "feeds.markallread") && d->mode == feeds_SidebarMode) {
            iConstForEach(PtrArray, i, listEntries_Feeds()) {
                const iFeedEntry *entry = i.ptr;
                const iString *url = &entry->url;
                if (!containsUrl_Visited(visited_App(), url)) {
                    visitUrl_Visited(visited_App(), url, transient_VisitedUrlFlag);
                }
            }
            postCommand_App("visited.changed");
            return iTrue;
        }
        else if (startsWith_CStr(cmd, "feed.entry.") && d->mode == feeds_SidebarMode) {
            const iSidebarItem *item = d->contextItem;
            if (item) {
                if (isCommand_Widget(w, ev, "feed.entry.opentab")) {
                    postCommandf_App("open newtab:1 url:%s", cstr_String(&item->url));
                    return iTrue;
                }
                if (isCommand_Widget(w, ev, "feed.entry.toggleread")) {
                    iVisited *vis = visited_App();
                    if (containsUrl_Visited(vis, &item->url)) {
                        removeUrl_Visited(vis, &item->url);
                    }
                    else {
                        visitUrl_Visited(vis, &item->url, transient_VisitedUrlFlag);
                    }
                    postCommand_App("visited.changed");
                    return iTrue;
                }
                if (isCommand_Widget(w, ev, "feed.entry.bookmark")) {
                    makeBookmarkCreation_Widget(&item->url, &item->label, item->icon);
                    postCommand_App("focus.set id:bmed.title");
                    return iTrue;
                }
                iBookmark *feedBookmark = get_Bookmarks(bookmarks_App(), item->id);
                if (feedBookmark) {
                    if (isCommand_Widget(w, ev, "feed.entry.openfeed")) {
                        postCommandf_App("open url:%s", cstr_String(&feedBookmark->url));
                        return iTrue;
                    }
                    if (isCommand_Widget(w, ev, "feed.entry.edit")) {
                        setFlags_Widget(w, disabled_WidgetFlag, iTrue);
                        makeFeedSettings_Widget(id_Bookmark(feedBookmark));
                        return iTrue;
                    }
                    if (isCommand_Widget(w, ev, "feed.entry.unsubscribe")) {
                        if (arg_Command(cmd)) {
                            removeTag_Bookmark(feedBookmark, "subscribed");
                            removeEntries_Feeds(id_Bookmark(feedBookmark));
                            updateItems_SidebarWidget_(d);
                        }
                        else {
                            makeQuestion_Widget(
                                uiTextCaution_ColorEscape "UNSUBSCRIBE",
                                format_CStr("Really unsubscribe from feed\n\"%s\"?",
                                            cstr_String(&feedBookmark->title)),
                                (const char *[]){ "Cancel",
                                                  uiTextCaution_ColorEscape "Unsubscribe" },
                                (const char *[]){
                                    "cancel",
                                    format_CStr("!feed.entry.unsubscribe arg:1 ptr:%p", d) },
                                2);
                        }
                        return iTrue;
                    }
                }
            }
        }
        else if (isCommand_Widget(w, ev, "ident.use")) {
            iGmIdentity *  ident  = menuIdentity_SidebarWidget_(d);
            const iString *tabUrl = url_DocumentWidget(document_App());
            if (ident) {
                if (argLabel_Command(cmd, "clear")) {
                    clearUse_GmIdentity(ident);
                }
                else if (arg_Command(cmd)) {
                    signIn_GmCerts(certs_App(), ident, tabUrl);
                }
                else {
                    signOut_GmCerts(certs_App(), tabUrl);
                }
                updateItems_SidebarWidget_(d);
            }
            return iTrue;
        }
        else if (isCommand_Widget(w, ev, "ident.showuse")) {
            const iGmIdentity *ident = menuIdentity_SidebarWidget_(d);
            if (ident) {
                makeMessage_Widget(uiHeading_ColorEscape "IDENTITY USAGE",
                                   cstrCollect_String(joinCStr_StringSet(ident->useUrls, "\n")));
            }
            return iTrue;
        }
        else if (isCommand_Widget(w, ev, "ident.edit")) {
            const iGmIdentity *ident = menuIdentity_SidebarWidget_(d);
            if (ident) {
                makeValueInput_Widget(get_Window()->root,
                                      &ident->notes,
                                      uiHeading_ColorEscape "IDENTITY NOTES",
                                      format_CStr("Notes about %s:", cstr_String(name_GmIdentity(ident))),
                                      uiTextAction_ColorEscape "OK",
                                      format_CStr("ident.setnotes ident:%p", ident));
            }
            return iTrue;
        }
        else if (isCommand_Widget(w, ev, "ident.setnotes")) {
            iGmIdentity *ident = pointerLabel_Command(cmd, "ident");
            if (ident) {
                setCStr_String(&ident->notes, suffixPtr_Command(cmd, "value"));
                updateItems_SidebarWidget_(d);
            }
            return iTrue;
        }
        else if (isCommand_Widget(w, ev, "ident.pickicon")) {
            return iTrue;
        }
        else if (isCommand_Widget(w, ev, "ident.reveal")) {
            const iGmIdentity *ident = menuIdentity_SidebarWidget_(d);
            if (ident) {
                const iString *crtPath = certificatePath_GmCerts(certs_App(), ident);
                if (crtPath) {
                    revealPath_App(crtPath);
                }
            }
            return iTrue;
        }
        else if (isCommand_Widget(w, ev, "ident.delete")) {
            iSidebarItem *item = d->contextItem;
            if (argLabel_Command(cmd, "confirm")) {
                makeQuestion_Widget(
                    uiTextCaution_ColorEscape "DELETE IDENTITY",
                    format_CStr(
                        "Do you really want to delete the identity\n" uiTextAction_ColorEscape
                        "%s\n" uiText_ColorEscape
                        "including its certificate and private key files?",
                        cstr_String(&item->label)),
                    (const char *[]){ "Cancel",
                                      uiTextCaution_ColorEscape "Delete Identity and Files" },
                    (const char *[]){ "cancel", format_CStr("!ident.delete confirm:0 ptr:%p", d) },
                    2);
                return iTrue;
            }
            deleteIdentity_GmCerts(certs_App(), hoverIdentity_SidebarWidget_(d));
            postCommand_App("idents.changed");
            return iTrue;
        }
        else if (isCommand_Widget(w, ev, "history.delete")) {
            if (d->contextItem && !isEmpty_String(&d->contextItem->url)) {
                removeUrl_Visited(visited_App(), &d->contextItem->url);
                updateItems_SidebarWidget_(d);
                scrollOffset_ListWidget(d->list, 0);
            }
            return iTrue;
        }
        else if (isCommand_Widget(w, ev, "history.copy")) {
            const iSidebarItem *item = d->contextItem;
            if (item && !isEmpty_String(&item->url)) {
                SDL_SetClipboardText(cstr_String(&item->url));
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
                postCommand_App("focus.set id:bmed.title");
            }
        }
        else if (equal_Command(cmd, "history.clear")) {
            if (argLabel_Command(cmd, "confirm")) {
                makeQuestion_Widget(
                    uiTextCaution_ColorEscape "CLEAR HISTORY",
                    "Do you really want to erase the history of all visited pages?",
                    (const char *[]){ "Cancel", uiTextCaution_ColorEscape "Clear History" },
                    (const char *[]){ "cancel", "history.clear confirm:0" },
                    2);
            }
            else {
                clear_Visited(visited_App());
                updateItems_SidebarWidget_(d);
                scrollOffset_ListWidget(d->list, 0);
            }
            return iTrue;
        }
    }
    if (ev->type == SDL_MOUSEMOTION && !isVisible_Widget(d->menu)) {
        const iInt2 mouse = init_I2(ev->motion.x, ev->motion.y);
        if (contains_Widget(d->resizer, mouse)) {
            setCursor_Window(get_Window(), SDL_SYSTEM_CURSOR_SIZEWE);
        }
        /* Update cursor. */
        else if (contains_Widget(w, mouse)) {
            const iSidebarItem *item = constHoverItem_ListWidget(d->list);
            if (item && d->mode != identities_SidebarMode) {
                setCursor_Window(get_Window(),
                                 item->listItem.isSeparator ? SDL_SYSTEM_CURSOR_ARROW
                                                            : SDL_SYSTEM_CURSOR_HAND);
            }
            else {
                setCursor_Window(get_Window(), SDL_SYSTEM_CURSOR_ARROW);
            }
        }
    }
    if (d->menu && ev->type == SDL_MOUSEBUTTONDOWN) {
        if (ev->button.button == SDL_BUTTON_RIGHT) {
            if (!isVisible_Widget(d->menu)) {
                updateMouseHover_ListWidget(d->list);
            }
            if (constHoverItem_ListWidget(d->list) || isVisible_Widget(d->menu)) {
                d->contextItem = hoverItem_ListWidget(d->list);
                /* Update menu items. */
                /* TODO: Some callback-based mechanism would be nice for updating menus right
                   before they open? */
                if (d->mode == bookmarks_SidebarMode && d->contextItem) {
                    const iBookmark *bm = get_Bookmarks(bookmarks_App(), d->contextItem->id);
                    if (bm) {
                        iLabelWidget *menuItem = findMenuItem_Widget(d->menu,
                                                                     "bookmark.tag tag:homepage");
                        if (menuItem) {
                            setTextCStr_LabelWidget(menuItem,
                                                    hasTag_Bookmark(bm, "homepage")
                                                        ? "Remove Homepage"
                                                        : "Use as Homepage");
                        }
                        menuItem = findMenuItem_Widget(d->menu, "bookmark.tag tag:subscribed");
                        if (menuItem) {
                            setTextCStr_LabelWidget(menuItem,
                                                    hasTag_Bookmark(bm, "subscribed")
                                                        ? "Unsubscribe from Feed"
                                                        : "Subscribe to Feed");
                        }
                        menuItem = findMenuItem_Widget(d->menu, "bookmark.tag tag:remotesource");
                        if (menuItem) {
                            setTextCStr_LabelWidget(menuItem,
                                                    hasTag_Bookmark(bm, "remotesource")
                                                        ? "Remove Bookmark Source"
                                                        : "Use as Bookmark Source");
                        }
                    }
                }
                else if (d->mode == feeds_SidebarMode && d->contextItem) {
                    iLabelWidget *menuItem = findMenuItem_Widget(d->menu, "feed.entry.toggleread");
                    const iBool isRead = containsUrl_Visited(visited_App(), &d->contextItem->url);
                    setTextCStr_LabelWidget(menuItem, isRead ? "Mark as Unread" : "Mark as Read");
                }
                else if (d->mode == identities_SidebarMode) {
                    const iGmIdentity *ident  = constHoverIdentity_SidebarWidget_(d);
                    const iString *    docUrl = url_DocumentWidget(document_App());
                    iForEach(ObjectList, i, children_Widget(d->menu)) {
                        if (isInstance_Object(i.object, &Class_LabelWidget)) {
                            iLabelWidget *menuItem = i.object;
                            const char *  cmdItem  = cstr_String(command_LabelWidget(menuItem));
                            if (equal_Command(cmdItem, "ident.use")) {
                                const iBool cmdUse   = arg_Command(cmdItem) != 0;
                                const iBool cmdClear = argLabel_Command(cmdItem, "clear") != 0;
                                setFlags_Widget(
                                    as_Widget(menuItem),
                                    disabled_WidgetFlag,
                                    (cmdClear && !isUsed_GmIdentity(ident)) ||
                                        (!cmdClear && cmdUse && isUsedOn_GmIdentity(ident, docUrl)) ||
                                        (!cmdClear && !cmdUse && !isUsedOn_GmIdentity(ident, docUrl)));
                            }
                            else if (equal_Command(cmdItem, "ident.showuse")) {
                                setFlags_Widget(as_Widget(menuItem),
                                                disabled_WidgetFlag,
                                                !isUsed_GmIdentity(ident));
                            }
                        }
                    }
                }
            }
        }
    }
    if (ev->type == SDL_KEYDOWN) {
        const int key   = ev->key.keysym.sym;
        const int kmods = keyMods_Sym(ev->key.keysym.mod);
        /* Hide the sidebar when Escape is pressed. */
        if (kmods == 0 && key == SDLK_ESCAPE && isVisible_Widget(d)) {
            setFlags_Widget(w, hidden_WidgetFlag, iTrue);
            arrange_Widget(w->parent);
            updateSize_DocumentWidget(document_App());
            refresh_Widget(w->parent);
            return iTrue;
        }
    }
    if (hoverItem_ListWidget(d->list) || isVisible_Widget(d->menu)) {
        /* Update the menu before opening. */
        if (d->mode == bookmarks_SidebarMode && !isVisible_Widget(d->menu)) {
            /* Remote bookmarks have limitations. */
            const iSidebarItem *hoverItem = hoverItem_ListWidget(d->list);
            iAssert(hoverItem);
            const iBookmark *  bm              = get_Bookmarks(bookmarks_App(), hoverItem->id);
            const iBool        isRemote        = hasTag_Bookmark(bm, "remote");
            static const char *localOnlyCmds[] = { "bookmark.edit",
                                                   "bookmark.delete",
                                                   "bookmark.tag tag:subscribed",
                                                   "bookmark.tag tag:homepage",
                                                   "bookmark.tag tag:remotesource",
                                                   "bookmark.tag tag:subscribed" };
            iForIndices(i, localOnlyCmds) {
                setFlags_Widget(as_Widget(findMenuItem_Widget(d->menu, localOnlyCmds[i])),
                                disabled_WidgetFlag,
                                isRemote);
            }
        }
        processContextMenuEvent_Widget(d->menu, ev, {});
    }
    return processEvent_Widget(w, ev);
}

static void draw_SidebarWidget_(const iSidebarWidget *d) {
    const iWidget *w      = constAs_Widget(d);
    const iRect    bounds = bounds_Widget(w);
    iPaint p;
    init_Paint(&p);
    draw_Widget(w);
    drawVLine_Paint(
        &p, addX_I2(topRight_Rect(bounds), -1), height_Rect(bounds), uiSeparator_ColorId);
}

static void draw_SidebarItem_(const iSidebarItem *d, iPaint *p, iRect itemRect,
                              const iListWidget *list) {
    const iSidebarWidget *sidebar = findParentClass_Widget(constAs_Widget(list),
                                                           &Class_SidebarWidget);
    const iBool isPressing   = isMouseDown_ListWidget(list);
    const iBool isHover      = isHover_Widget(constAs_Widget(list)) &&
                               constHoverItem_ListWidget(list) == d;
    const int scrollBarWidth = scrollBarWidth_ListWidget(list);
    const int itemHeight     = height_Rect(itemRect);
    const int iconColor      = isHover ? (isPressing ? uiTextPressed_ColorId : uiIconHover_ColorId)
                                       : uiIcon_ColorId;
    const int font = uiContent_FontId;
    int       bg   = uiBackground_ColorId;
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
    iInt2 pos = itemRect.pos;
    if (sidebar->mode == documentOutline_SidebarMode) {
        const int fg = isHover ? (isPressing ? uiTextPressed_ColorId : uiTextFramelessHover_ColorId)
                               : (tmHeading1_ColorId + d->indent / (4 * gap_UI));
        drawRange_Text(font,
                       init_I2(pos.x + 3 * gap_UI + d->indent,
                               mid_Rect(itemRect).y - lineHeight_Text(font) / 2),
                       fg,
                       range_String(&d->label));
    }
    else if (sidebar->mode == feeds_SidebarMode) {
        const int fg = isHover ? (isPressing ? uiTextPressed_ColorId : uiTextFramelessHover_ColorId)
                               : uiText_ColorId;
        if (d->listItem.isSeparator) {
            if (d != constItem_ListWidget(list, 0)) {
                drawHLine_Paint(p,
                                addY_I2(pos, 2 * gap_UI),
                                width_Rect(itemRect) - scrollBarWidth,
                                uiSeparator_ColorId);
            }
            drawRange_Text(
                uiLabelLarge_FontId,
                add_I2(pos,
                       init_I2(3 * gap_UI,
                               itemHeight - lineHeight_Text(uiLabelLarge_FontId) - 1 * gap_UI)),
                uiIcon_ColorId,
                range_String(&d->meta));
        }
        else {
            const iBool isUnread = (d->indent != 0);
            const int h1 = lineHeight_Text(uiLabel_FontId);
            const int h2 = lineHeight_Text(uiContent_FontId);
            const int iconPad = 9 * gap_UI;
            iRect iconArea = { addY_I2(pos, 0), init_I2(iconPad, itemHeight) };
            if (isUnread) {
                fillRect_Paint(
                    p,
                    (iRect){ topLeft_Rect(iconArea), init_I2(gap_UI / 2, height_Rect(iconArea)) },
                    iconColor);
            }
            /* Icon. */ {
                /* TODO: Use the primary hue from the theme of this site. */
                iString str;
                initUnicodeN_String(&str, &d->icon, 1);
                drawCentered_Text(uiContent_FontId,
                                  adjusted_Rect(iconArea, init_I2(gap_UI, 0), zero_I2()),
                                  iTrue,
                                  isHover && isPressing
                                      ? iconColor
                                      : (isUnread ? uiTextCaution_ColorId : iconColor),
                                  "%s",
                                  cstr_String(&str));
                deinit_String(&str);
            }
            /* Select the layout based on how the title fits. */
            iInt2       titleSize = advanceRange_Text(uiContent_FontId, range_String(&d->label));
            const iInt2 metaSize  = advanceRange_Text(uiLabel_FontId, range_String(&d->meta));
            pos.x += iconPad;
            const int avail = width_Rect(itemRect) - iconPad - 3 * gap_UI;
            const int labelFg = isPressing ? fg : (isUnread ? uiTextStrong_ColorId : uiText_ColorId);
            if (titleSize.x > avail && metaSize.x < avail * 0.75f) {
                /* Must wrap the title. */
                pos.y += (itemHeight - h2 - h2) / 2;
                draw_Text(
                    uiLabel_FontId, addY_I2(pos, h2 - h1 - gap_UI / 8), fg, "%s \u2014 ", cstr_String(&d->meta));
                int skip  = metaSize.x + advance_Text(uiLabel_FontId, " \u2014 ").x;
                iInt2 cur = addX_I2(pos, skip);
                const char *endPos;
                tryAdvance_Text(
                    uiContent_FontId, range_String(&d->label), avail - skip, &endPos);
                drawRange_Text(uiContent_FontId,
                               cur,
                               labelFg,
                               (iRangecc){ constBegin_String(&d->label), endPos });
                if (endPos < constEnd_String(&d->label)) {
                    drawRange_Text(uiContent_FontId,
                                   addY_I2(pos, h2), labelFg,
                                   (iRangecc){ endPos, constEnd_String(&d->label) });
                }
            }
            else {
                pos.y += (itemHeight - h1 - h2) / 2;
                drawRange_Text(uiLabel_FontId, pos, fg, range_String(&d->meta));
                drawRange_Text(uiContent_FontId, addY_I2(pos, h1), labelFg, range_String(&d->label));
            }
        }
    }
    else if (sidebar->mode == bookmarks_SidebarMode) {
        const int fg = isHover ? (isPressing ? uiTextPressed_ColorId : uiTextFramelessHover_ColorId)
                               : uiText_ColorId;
        iString str;
        init_String(&str);
        appendChar_String(&str, d->icon ? d->icon : 0x1f588);
        const iRect iconArea = { addX_I2(pos, gap_UI), init_I2(7 * gap_UI, itemHeight) };
        drawCentered_Text(font, iconArea, iTrue, iconColor, "%s", cstr_String(&str));
        deinit_String(&str);
        const iInt2 textPos = addY_I2(topRight_Rect(iconArea), (itemHeight - lineHeight_Text(font)) / 2);
        drawRange_Text(font, textPos, fg, range_String(&d->label));
        const iInt2 metaPos =
            init_I2(right_Rect(itemRect) - advanceRange_Text(font, range_String(&d->meta)).x -
                        2 * gap_UI - (scrollBarWidth ? scrollBarWidth - gap_UI : 0),
                    textPos.y);
        fillRect_Paint(p,
                       init_Rect(metaPos.x,
                                 top_Rect(itemRect),
                                 right_Rect(itemRect) - metaPos.x,
                                 height_Rect(itemRect)),
                       bg);
        drawRange_Text(font,
                       metaPos,
                       isHover && isPressing ? fg : uiTextCaution_ColorId,
                       range_String(&d->meta));
    }
    else if (sidebar->mode == history_SidebarMode) {
        iBeginCollect();
        const int fg = isHover ? (isPressing ? uiTextPressed_ColorId : uiTextFramelessHover_ColorId)
                               : uiText_ColorId;
        if (d->listItem.isSeparator) {
            if (!isEmpty_String(&d->meta)) {
                iInt2 drawPos = addY_I2(topLeft_Rect(itemRect), d->id);
                drawHLine_Paint(p,
                                addY_I2(drawPos, -gap_UI),
                                width_Rect(itemRect) - scrollBarWidth,
                                uiSeparator_ColorId);
                drawRange_Text(
                    uiLabelLarge_FontId,
                    add_I2(drawPos,
                           init_I2(3 * gap_UI, (itemHeight - lineHeight_Text(uiLabelLarge_FontId)) / 2)),
                    uiIcon_ColorId,
                    range_String(&d->meta));
            }
        }
        else {
            iUrl parts;
            init_Url(&parts, &d->label);
            const iBool isAbout  = equalCase_Rangecc(parts.scheme, "about");
            const iBool isGemini = equalCase_Rangecc(parts.scheme, "gemini");
            draw_Text(font,
                      add_I2(topLeft_Rect(itemRect),
                             init_I2(3 * gap_UI, (itemHeight - lineHeight_Text(font)) / 2)),
                      fg,
                      "%s%s%s%s%s%s",
                      isGemini ? "" : cstr_Rangecc(parts.scheme),
                      isGemini ? "" : isAbout ? ":" : "://",
                      escape_Color(isHover ? (isPressing ? uiTextPressed_ColorId
                                                         : uiTextFramelessHover_ColorId)
                                           : uiTextStrong_ColorId),
                      cstr_Rangecc(parts.host),
                      escape_Color(fg),
                      cstr_Rangecc(parts.path));
        }
        iEndCollect();
    }
    else if (sidebar->mode == identities_SidebarMode) {
        const int fg = isHover ? (isPressing ? uiTextPressed_ColorId : uiTextFramelessHover_ColorId)
                               : uiTextStrong_ColorId;
        iString icon;
        initUnicodeN_String(&icon, &d->icon, 1);
        iInt2 cPos = topLeft_Rect(itemRect);
        addv_I2(&cPos,
                init_I2(3 * gap_UI,
                        (itemHeight - lineHeight_Text(default_FontId) * 2 - lineHeight_Text(font)) /
                            2));
        const int metaFg = isHover ? permanent_ColorId | (isPressing ? uiTextPressed_ColorId
                                                                     : uiTextFramelessHover_ColorId)
                                   : uiText_ColorId;
        drawRange_Text(
            font, cPos, d->listItem.isSelected ? iconColor : metaFg, range_String(&icon));
        deinit_String(&icon);
        drawRange_Text(font, add_I2(cPos, init_I2(6 * gap_UI, 0)), fg, range_String(&d->label));
        drawRange_Text(default_FontId,
                       add_I2(cPos, init_I2(6 * gap_UI, lineHeight_Text(font))),
                       metaFg,
                       range_String(&d->meta));
    }
}

iBeginDefineSubclass(SidebarWidget, Widget)
    .processEvent = (iAny *) processEvent_SidebarWidget_,
    .draw         = (iAny *) draw_SidebarWidget_,
iEndDefineSubclass(SidebarWidget)
