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

#include "sidebar/impl.h"

#include "app.h"
#include "bookmarks.h"
#include "certlistwidget.h"
#include "command.h"
#include "defs.h"
#include "documentwidget.h"
#include "doc/book.h"
#include "feeds.h"
#include "gamepad.h"
#include "indicatorwidget.h"
#include <lagrange/gmcerts.h>
#include "gmdocument.h"
#include "gmutil.h"
#include "keys.h"
#include "labelwidget.h"
#include "listwidget.h"
#include "mobile.h"
#include "render/paint.h"
#include "root.h"
#include "scrollwidget.h"
#include "simpledocumentwidget.h"
#include "gempub.h"
#include "touch.h"
#include "util.h"
#include <lagrange/visited.h>

#include <SDL3/SDL_clipboard.h>
#include <SDL3/SDL_mouse.h>
#include <the_Foundation/intset.h>
#include <the_Foundation/stringarray.h>

/*----------------------------------------------------------------------------------------------*/

static const char *normalModeLabels_[max_SidebarMode] = {
    bookmarks_Icon " ${sidebar.bookmarks}",
    star_Icon " ${sidebar.feeds}",
    whiteStar_Icon " ${sidebar.subscriptions}",
    person_Icon " ${sidebar.identities}",
    page_Icon " ${sidebar.outline}",
    hierarchy_Icon " ${sidebar.structure}",
    openTabBg_Icon " ${sidebar.documents}",
    clock_Icon " ${sidebar.history}",
    books_Icon " ${sidebar.gempub}",
};

static const char *tightModeLabels_[max_SidebarMode] = {
    bookmarks_Icon,
    star_Icon,
    whiteStar_Icon,
    person_Icon,
    page_Icon,
    hierarchy_Icon,
    openTabBg_Icon,
    clock_Icon,
    books_Icon,
};

/* Display order of the tabs. */
static int tabOrder_[2][max_SidebarMode] = {
    { 0, 1, 2, 3, 4, 5, 6, 7, 8 },
    { 0, 1, 2, 3, 4, 5, 6, 7, 8 },
};

iDefineObjectConstructionArgs(SidebarWidget, (enum iSidebarSide side), side);

const iGempub *gempub_SidebarWidget_(const iSidebarWidget *d) {
    /* The book that the root's current document belongs to. */
    const iDocumentWidget *doc    = document_Root(constAs_Widget(d)->root);
    const iGempub         *gempub = doc ? gempub_DocumentWidget(doc) : NULL;
    return gempub && !isRemote_Gempub(gempub) ? gempub : NULL;
}

enum iSidebarSide bookSide_SidebarWidget(void) {
    if (deviceType_App() != phone_AppDeviceType &&
        !prefs_App()->sidebarModeEnabled[left_SidebarSide][gempub_SidebarMode] &&
        prefs_App()->sidebarModeEnabled[right_SidebarSide][gempub_SidebarMode]) {
        return right_SidebarSide;
    }
    return left_SidebarSide;
}

static iBool isModeEnabled_SidebarWidget_(const iSidebarWidget *d, int side,
                                          enum iSidebarMode mode) {
    return prefs_App()->sidebarModeEnabled[side][mode];
}

static iBool isTab_SidebarMode_(int side, enum iSidebarMode mode) {
    if (side == right_SidebarSide && deviceType_App() == phone_AppDeviceType) {
        return iFalse;
    }
    if (mode == identities_SidebarMode && deviceType_App() != desktop_AppDeviceType) {
        return iFalse;
    }
    return prefs_App()->sidebarModeEnabled[side][mode];
}

static size_t numTabs_SidebarMode_(int side) {
    size_t count = 0;
    for (int mode = 0; mode < maxSidebarModes_Prefs; mode++) {
        count += isTab_SidebarMode_(side, mode);
    }
    return count;
}

enum iSidebarMode orderedMode_SidebarMode(enum iSidebarSide side, size_t pos) {
    return tabOrder_[side][pos];
}

static int modePos_SidebarWidget_(const iSidebarWidget *d, enum iSidebarMode mode) {
    for (int i = 0; i < max_SidebarMode; i++) {
        if (tabOrder_[d->side][i] == mode) {
            return i;
        }
    }
    return 0;
}

void serializeOrder_SidebarMode(iStream *outs) {
    for (int s = 0; s < 2; s++) {
        writeU8_Stream(outs, max_SidebarMode);
        for (int i = 0; i < max_SidebarMode; i++) {
            writeU8_Stream(outs, tabOrder_[s][i]);
        }
    }
}

void deserializeOrder_SidebarMode(iStream *ins, iBool apply) {
    for (int s = 0; s < 2; s++) {
        int    order[max_SidebarMode];
        iBool  isUsed[max_SidebarMode];
        size_t count = 0;
        iZap(isUsed);
        for (int i = readU8_Stream(ins); i > 0; i--) {
            const int mode = readU8_Stream(ins);
            if (mode < max_SidebarMode && !isUsed[mode]) {
                isUsed[mode]   = iTrue;
                order[count++] = mode;
            }
        }
        /* Modes missing from the saved order go last. */
        for (int mode = 0; mode < max_SidebarMode; mode++) {
            if (!isUsed[mode]) {
                order[count++] = mode;
            }
        }
        if (apply) {
            memcpy(tabOrder_[s], order, sizeof(order));
        }
    }
}

iBool tab_SidebarMode(size_t index, enum iSidebarSide *side, enum iSidebarMode *mode) {
    for (int s = 0; s < 2; s++) {
        for (int i = 0; i < max_SidebarMode; i++) {
            const int m = tabOrder_[s][i];
            if (isTab_SidebarMode_(s, m) && index-- == 0) {
                *side = s;
                *mode = m;
                return iTrue;
            }
        }
    }
    return iFalse;
}

static iBool isResizing_SidebarWidget_(const iSidebarWidget *d) {
    return (flags_Widget(d->resizer) & pressed_WidgetFlag) != 0;
}

enum iFontId actionButtonFont_SidebarWidget_(const iSidebarWidget *d) {
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

iLabelWidget *addActionButton_SidebarWidget_(iSidebarWidget *d, const char *label,
                                             const char *command, int64_t flags) {
    iLabelWidget *btn =
        addChildFlags_Widget(d->actions, iClob(new_LabelWidget(label, command)), flags);
    setFont_LabelWidget(btn, actionButtonFont_SidebarWidget_(d));
    checkIcon_LabelWidget(btn);
    if (deviceType_App() != desktop_AppDeviceType) {
        setFlags_Widget(btn, frameless_WidgetFlag | noBackground_WidgetFlag, iTrue);
        setTextColor_LabelWidget(btn, uiTextAction_ColorId);
    }
    return btn;
}

iBool isSlidingSheet_SidebarWidget_(const iSidebarWidget *d) {
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
        setTextCStr_LabelWidget(findChild_Widget(d->actions, "mobile.bookmarks.edit"),
                                editing ? "${sidebar.close}" : "${sidebar.action.bookmarks.edit}");
        setDragHandleWidth_ListWidget(d->list,
                                      editing ? itemHeight_ListWidget(d->list) * 3 / 2 : 0);
        arrange_Widget(d->actions);
    }
}

static void addBlankIcon_SidebarWidget_(const iSidebarWidget *d, iWidget *div) {
    if (isMobile_Platform() || isTerminal_Platform()) {
        return;
    }
    iLabelWidget *icon =
        new_LabelWidget(d->mode == bookmarks_SidebarMode && isShowingTrash_SidebarWidget(d)
                            ? trash_Icon
                            : tightModeLabels_[d->mode],
                        NULL);
    setFont_LabelWidget(icon, uiLabelHuge_FontId);
    setTextColor_LabelWidget(icon, uiTextDim_ColorId);
    addChildFlags_Widget(div, iClob(icon), frameless_WidgetFlag);
}

static int actionsBottomInset_SidebarWidget_(const iSidebarWidget *d) {
    /* Padding under the action bar depends on whether there are other UI elements next
       to the bottom of the window. This is surprisingly convoluted; perhaps there is a
       better way to handle this? (Some sort of intelligent padding widget at the bottom
       of the sidebar? Root should just use safe insets as the padding? In that case,
       individual widgets still need to be able to extend into the safe area.) */
    if (deviceType_App() == tablet_AppDeviceType) {
        /* Hidden reading bars leave the sidebar extending to the bottom. */
        const iWidget *navBar = findChild_Widget(root_Widget(constAs_Widget(d)), "navbar");
        return prefs_App()->bottomNavBar && isVisible_Widget(navBar) ? 0
                                                                     : bottomSafeInset_Mobile();
    }
    if (deviceType_App() == phone_AppDeviceType && !isPortrait_App()) {
        /* In sliding sheet mode (portrait), sidebar is resized to fit in the safe area. */
        if (!prefs_App()->bottomNavBar) {
            return bottomSafeInset_Mobile();
        }
        return (!prefs_App()->hideToolbarOnScroll
                    ? height_Widget(findChild_Widget(root_Widget(constAs_Widget(d)), "navbar"))
                    : 0) +
               bottomSafeInset_Mobile();
    }
    return 0;
}

static void updateActionsPadding_SidebarWidget_(iSidebarWidget *d) {
    const int bottom = actionsBottomInset_SidebarWidget_(d);
    if (childCount_Widget(d->actions) && deviceType_App() == desktop_AppDeviceType) {
        /* Mirrors the book tab area padding. Mobile action buttons have enough padding. */
        setPadding_Widget(d->actions, 3 * gap_UI, gap_UI, 3 * gap_UI, 2 * gap_UI + bottom);
    }
    else if (deviceType_App() == phone_AppDeviceType) {
        /* Keep buttons from going under rounded screen corners. */
        const int edgePad = 3 * gap_UI;
        setPadding_Widget(d->actions, edgePad, 0, edgePad, bottom);
    }
    else {
        setPadding_Widget(d->actions, 0, 0, 0, bottom);
    }
}

/* Feeds actions that don't fit are hidden: first the label, then the button (also in the menu). */
static void checkFeedsActionsLayout_SidebarWidget_(iSidebarWidget *d) {
    iWidget *readAll = findChild_Widget(d->actions, "feeds.markallread");
    iWidget *show    = findChild_Widget(d->actions, "feeds.showlabel");
    iWidget *mode    = findChild_Widget(d->actions, "feeds.modebutton");
    if (!readAll || !show || !mode) {
        return;
    }
    const int avail = width_Widget(d->actions) - d->actions->padding[0] - d->actions->padding[2];
    if (avail <= 0) {
        return;
    }
    const int readWidth = defaultSize_LabelWidget((const iLabelWidget *) readAll).x;
    const int showWidth = defaultSize_LabelWidget((const iLabelWidget *) show).x;
    const int modeWidth = width_Widget(mode);
    const iBool fitsReadAll = readWidth + modeWidth <= avail;
    const iBool fitsShow    = fitsReadAll && readWidth + showWidth + modeWidth <= avail;
    if (!(readAll->flags & hidden_WidgetFlag) != fitsReadAll ||
        !(show->flags & hidden_WidgetFlag) != fitsShow) {
        setFlags_Widget(readAll, hidden_WidgetFlag, !fitsReadAll);
        setFlags_Widget(show, hidden_WidgetFlag, !fitsShow);
        arrange_Widget(d->actions);
        refresh_Widget(d->actions);
    }
}

static int outlineWidth_SidebarWidget_(const iSidebarWidget *d) {
    return width_Rect(innerBounds_Widget(constAs_Widget(d->list)));
}

static void addOutlineItems_SidebarWidget_(iSidebarWidget *d) {
    const iGmDocument *doc = document_DocumentWidget(document_App());
    d->outlineWidth        = outlineWidth_SidebarWidget_(d);
    iConstForEach(Array, i, headings_GmDocument(doc)) {
        const iGmHeading *head = i.value;
        iSidebarItem     *item = new_SidebarItem();
        item->id               = index_ArrayConstIterator(&i);
        setRange_String(&item->label, head->text);
        item->indent = head->level * 5 * gap_UI;
        item->isBold = head->level == 0;
        addItem_ListWidget(d->list, item);
        for (size_t span = outlineSpan_SidebarItem_(item, d, d->outlineWidth); span > 1; span--) {
            iSidebarItem *cont                  = new_SidebarItem();
            cont->id                            = item->id;
            cont->listItem.flags.isContinuation = iTrue;
            addItem_ListWidget(d->list, cont);
            iRelease(cont);
        }
        iRelease(item);
    }
}

void updateItemsWithFlags_SidebarWidget_(iSidebarWidget *d, iBool keepActions) {
    const iBool isMobile = (deviceType_App() != desktop_AppDeviceType);
    clear_ListWidget(d->list);
    releaseChildren_Widget(d->blank);
    if (!keepActions) {
        if (focus_Widget() && focus_Widget() != as_Widget(d->list) &&
            hasParent_Widget(focus_Widget(), as_Widget(d))) {
            /* Something inside this sidebar has input focus, so let it go first. */
            setFocus_Widget(NULL);
        }
        releaseChildren_Widget(d->actions);
    }
    d->actions->rect.size.y = 0;
    iWidget **menus[] = { &d->menu, &d->modeMenu, &d->folderMenu, &d->trashMenu };
    iForIndices(i, menus) {
        destroy_Widget(*menus[i]), *menus[i] = NULL;
    }
    iBool isEmpty = iFalse; /* show blank? */
    switch (d->mode) {
        case feedEntries_SidebarMode:
            isEmpty = !updateFeedEntries_SidebarWidget_(d, keepActions);
            break;
        case subscriptions_SidebarMode:
            isEmpty = !updateSubscriptions_SidebarWidget_(d);
            break;
        case documentOutline_SidebarMode:
            addOutlineItems_SidebarWidget_(d);
            break;
        case siteStructure_SidebarMode:
            isEmpty = !updateStructure_SidebarWidget_(d, keepActions);
            break;
        case bookmarks_SidebarMode:
            isEmpty = !updateBookmarks_SidebarWidget_(d, keepActions);
            break;
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
            /* Actions: sort order above the buttons. */
            const iMenuItem sortItems[] = {
                { "${sidebar.action.ident.name}", 0, 0, "idents.sort arg:1" },
                { "${sidebar.action.ident.manual}", 0, 0, "idents.sort arg:0" },
            };
            const char *selectedSort = format_CStr(" arg:%d", d->identSort);
            if (!keepActions && !isEmpty) {
                iWidget *sortRow = new_Widget();
                updateSize_LabelWidget(
                    addChildFlags_Widget(sortRow,
                                         iClob(new_LabelWidget("${sidebar.action.sort}", NULL)),
                                         frameless_WidgetFlag | tight_WidgetFlag));
                iLabelWidget *dropButton = makeMenuButton_LabelWidget(sortItems[0].label, sortItems, 2);
                setId_Widget(as_Widget(dropButton), "idents.sortbutton");
                addChildFlags_Widget(sortRow, iClob(dropButton), expand_WidgetFlag);
                updateDropdownSelection_LabelWidget(dropButton, selectedSort);
                iWidget *buttonRow = new_Widget();
                const iMenuItem buttons[] = {
                    { add_Icon " ${sidebar.action.ident.new}", 0, 0, "ident.new" },
                    { "${sidebar.action.ident.import}", 0, 0, "ident.import" },
                };
                iForIndices(i, buttons) {
                    iLabelWidget *btn = addChild_Widget(
                        buttonRow, iClob(new_LabelWidget(buttons[i].label, buttons[i].command)));
                    setFont_LabelWidget(btn, actionButtonFont_SidebarWidget_(d));
                    checkIcon_LabelWidget(btn);
                }
                iWidget *rows = new_Widget();
                const int64_t rowFlags = arrangeHorizontal_WidgetFlag | arrangeHeight_WidgetFlag |
                                         resizeWidthOfChildren_WidgetFlag;
                addChildFlags_Widget(rows, iClob(sortRow), rowFlags);
                addChild_Widget(rows, iClob(makePadding_Widget(gap_UI)));
                addChildFlags_Widget(rows, iClob(buttonRow), rowFlags);
                addChildFlags_Widget(d->actions,
                                     iClob(rows),
                                     expand_WidgetFlag | arrangeVertical_WidgetFlag |
                                         arrangeHeight_WidgetFlag |
                                         resizeWidthOfChildren_WidgetFlag | unhittable_WidgetFlag);
            }
            else if (keepActions) {
                updateDropdownSelection_LabelWidget(findChild_Widget(d->actions, "idents.sortbutton"),
                                                    selectedSort);
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
                item->id     = (isRequestOngoing_DocumentWidget(doc) ? 1 : 0) |
                               (isAutoReloading_DocumentWidget(doc) ? 4 : 0) |
                               (isReader_DocumentWidget(doc) ? 8 : 0);
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
        case gempub_SidebarMode:
            isEmpty = isShowingShelf_SidebarWidget_(d) ? !updateShelf_SidebarWidget_(d, keepActions)
                                                       : !updateGempub_SidebarWidget_(d);
            break;
        default:
            break;
    }
    updateActionsPadding_SidebarWidget_(d);
    const iBool isBookPage = d->mode == gempub_SidebarMode && !isShowingShelf_SidebarWidget_(d);
    setFlags_Widget(d->list,
                    hidden_WidgetFlag,
                    d->mode == identities_SidebarMode || isBookPage);
    setFlags_Widget(d->certList, hidden_WidgetFlag, d->mode != identities_SidebarMode);
    setFlags_Widget(d->gempubArea, hidden_WidgetFlag, !isBookPage || isEmpty);
    /* Book tabs. */ {
        const iBool showTabs = d->mode == gempub_SidebarMode && gempub_SidebarWidget_(d) != NULL;
        if (showTabs != isVisible_Widget(d->gempubTabArea)) {
            setFlags_Widget(d->gempubTabArea, hidden_WidgetFlag, !showTabs);
            arrange_Widget(as_Widget(d));
        }
        for (int i = 0; i < 3; i++) {
            setFlags_Widget(d->gempubTabs[i], selected_WidgetFlag, i == d->gempubPage);
        }
    }
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
            addBlankIcon_SidebarWidget_(d, div);
            if (d->feedsMode == all_FeedsMode) {
                if (numSubscribed_Feeds() == 0) {
                    iLabelWidget *msg =
                        addChildFlags_Widget(div,
                                             iClob(new_LabelWidget("${sidebar.empty.feeds}", NULL)),
                                             frameless_WidgetFlag);
                    setFont_LabelWidget(msg, uiLabelLarge_FontId);
                    addChild_Widget(div, iClob(makePadding_Widget(3 * gap_UI)));
                }
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
        else if (d->mode == subscriptions_SidebarMode || d->mode == siteStructure_SidebarMode ||
                 d->mode == bookmarks_SidebarMode) {
            iWidget *div = makeVDiv_Widget();
            setPadding_Widget(div, 3 * gap_UI, 0, rightPad, 2 * gap_UI);
            addChildFlags_Widget(div, iClob(new_Widget()), expand_WidgetFlag); /* pad */
            addBlankIcon_SidebarWidget_(d, div);
            iLabelWidget *msg = new_LabelWidget(
                d->mode == subscriptions_SidebarMode ? "${sidebar.empty.subs}"
                : d->mode == bookmarks_SidebarMode
                    ? (isShowingTrash_SidebarWidget(d) ? "${sidebar.empty.trash}"
                                                       : "${sidebar.empty.bookmarks}")
                    : "${sidebar.empty.links}",
                NULL);
            setFont_LabelWidget(msg, uiLabelLarge_FontId);
            addChildFlags_Widget(div, iClob(msg), frameless_WidgetFlag);
            addChildFlags_Widget(div, iClob(new_Widget()), expand_WidgetFlag); /* pad */
            if (d->mode == bookmarks_SidebarMode) {
                arrange_Widget(d->actions);
                div->padding[3] = height_Widget(d->actions);
            }
            addChild_Widget(d->blank, iClob(div));
        }
        else if (d->mode == gempub_SidebarMode) {
            iWidget *div = makeVDiv_Widget();
            setPadding_Widget(div, 3 * gap_UI, 0, rightPad, 2 * gap_UI);
            setBackgroundColor_Widget(div, uiBackgroundSidebar_ColorId);
            addChildFlags_Widget(div, iClob(new_Widget()), expand_WidgetFlag); /* pad */
            addBlankIcon_SidebarWidget_(d, div);
            iLabelWidget *msg = new_LabelWidget("${sidebar.empty.books}", NULL);
            setFont_LabelWidget(msg, uiLabelLarge_FontId);
            setWrap_LabelWidget(msg, iTrue);
            setCenterWrapped_LabelWidget(msg, iTrue);
            addChildFlags_Widget(div, iClob(msg), frameless_WidgetFlag | fixedHeight_WidgetFlag);
            addChildFlags_Widget(div, iClob(new_Widget()), expand_WidgetFlag); /* pad */
            addChild_Widget(d->blank, iClob(div));
        }
        else if (d->mode == identities_SidebarMode) {
            iWidget *div = makeVDiv_Widget();
            setPadding_Widget(div, 3 * gap_UI, 0, rightPad, 2 * gap_UI);
            addChildFlags_Widget(div, iClob(new_Widget()), expand_WidgetFlag); /* pad */
            addBlankIcon_SidebarWidget_(d, div);
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
    checkFeedsActionsLayout_SidebarWidget_(d);
    updateMouseHover_ListWidget(list_SidebarWidget_(d));
}

void updateItems_SidebarWidget_(iSidebarWidget *d) {
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

static size_t outlineItemSpan_SidebarWidget_(const iSidebarWidget *d, size_t index) {
    size_t span = 1;
    for (;;) {
        const iListItem *item = constItem_ListWidget(d->list, index + span);
        if (!item || !item->flags.isContinuation) {
            return span;
        }
        span++;
    }
}

static void reflowOutline_SidebarWidget_(iSidebarWidget *d) {
    if (d->mode != documentOutline_SidebarMode || !isVisible_Widget(d) ||
        outlineWidth_SidebarWidget_(d) == d->outlineWidth) {
        return;
    }
    iListWidget *list       = d->list;
    const int    itemHeight = itemHeight_ListWidget(list);
    if (!itemHeight) {
        return;
    }
    /* The first visible heading keeps its position. */
    const int           scrollPos = scrollPos_ListWidget(list);
    const iSidebarItem *first     = constItem_ListWidget(list, iMax(0, scrollPos) / itemHeight);
    const iSidebarItem *cursor    = constItem_ListWidget(list, list->cursorItem);
    const iBool         hasFirst  = first != NULL;
    const iBool         hasCursor = cursor != NULL;
    const uint32_t      firstId   = first ? first->id : 0;
    const uint32_t      cursorId  = cursor ? cursor->id : 0;
    int                 offset    = 0;
    size_t              oldSpan   = 1;
    if (hasFirst) {
        const size_t index = findItem_SidebarWidget_(d, firstId);
        offset             = scrollPos - (int) index * itemHeight;
        oldSpan            = outlineItemSpan_SidebarWidget_(d, index);
    }
    clear_ListWidget(list);
    addOutlineItems_SidebarWidget_(d);
    if (hasCursor) {
        list->cursorItem = findItem_SidebarWidget_(d, cursorId);
    }
    updateVisible_ListWidget(list);
    if (hasFirst) {
        const size_t index = findItem_SidebarWidget_(d, firstId);
        if (index != iInvalidPos) {
            const int pos = (int) index * itemHeight +
                            offset * (int) outlineItemSpan_SidebarWidget_(d, index) / (int) oldSpan;
            setScrollPos_ListWidget(list, iMin(pos, list->scrollY.max));
            updateVisible_ListWidget(list);
        }
    }
    invalidate_ListWidget(list);
    updateMouseHover_ListWidget(list);
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
    const float heights[max_SidebarMode] = {
        [bookmarks_SidebarMode] = 1.333f,
        [feedEntries_SidebarMode] = 3.333f,
        [subscriptions_SidebarMode] = 2.5f,
        [documentOutline_SidebarMode] = 0.5f, /* a line takes two slots */
        [siteStructure_SidebarMode] = 1.2f,
        [openDocuments_SidebarMode] = 1.333f,
        [history_SidebarMode] = 1.333f,
        [gempub_SidebarMode] = 2.333f,
    };
#else
    const float heights[max_SidebarMode] = {
        [bookmarks_SidebarMode] = 1,     [feedEntries_SidebarMode] = 3,
        [subscriptions_SidebarMode] = 3, [documentOutline_SidebarMode] = 1,
        [siteStructure_SidebarMode] = 1, [openDocuments_SidebarMode] = 1,
        [history_SidebarMode] = 1,       [gempub_SidebarMode] = 2,
    };
#endif
    if (d->list) {
        int padding = d->mode == gempub_SidebarMode && !isTerminal_Platform() ? 2 * gap_UI : 0;
        if (d->mode == documentOutline_SidebarMode && deviceType_App() != desktop_AppDeviceType) {
            padding -= 3 * gap_UI / 2; /* cancels the mobile padding, which would apply per slot */
        }
        setItemHeight_ListWidget(d->list,
                                 heights[d->mode] * lineHeight_Text(d->itemFonts[0]) + padding);
    }
    if (d->certList) {
        updateItemHeight_CertListWidget(d->certList);
    }
}

static void updateFeedsProgress_SidebarWidget_(iSidebarWidget *d) {
    if (d->feedsProgress) {
        setFlags_Widget(as_Widget(d->feedsProgress),
                        hidden_WidgetFlag,
                        !isSlidingSheet_SidebarWidget_(d) ||
                            (d->mode != feedEntries_SidebarMode &&
                             d->mode != subscriptions_SidebarMode));
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
    d->isShelfOrderValid = iFalse;
    if (mode == feedEntries_SidebarMode) {
        notify_App("sync.recent"); /* read status may have changed elsewhere */
    }
    for (enum iSidebarMode i = 0; i < max_SidebarMode; i++) {
        setFlags_Widget(d->modeButtons[i], selected_WidgetFlag, i == d->mode);
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
    updateFeedsProgress_SidebarWidget_(d);
    return iTrue;
}

void setOpenFolders_SidebarWidget(iSidebarWidget *d, const iIntSet *openFolders) {
    if (d) {
        delete_IntSet(d->openFolders);
        d->openFolders = copy_IntSet(openFolders);
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

int shelfSort_SidebarWidget(const iSidebarWidget *d) {
    return d ? d->shelfSort : 0;
}

int identSort_SidebarWidget(const iSidebarWidget *d) {
    return d ? d->identSort : manual_CertListSortOrder;
}

float width_SidebarWidget(const iSidebarWidget *d) {
    return d ? d->widthAsGaps : 0;
}

const iIntSet *openFolders_SidebarWidget(const iSidebarWidget *d) {
    return d ? d->openFolders : collect_IntSet(new_IntSet());
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

static void updateModeButtonOrder_SidebarWidget_(iSidebarWidget *d) {
    iWidget *buttons = parent_Widget(d->modeButtons[0]);
    size_t   index   = 0;
    for (int i = 0; i < max_SidebarMode; i++) {
        iLabelWidget *button = d->modeButtons[orderedMode_SidebarMode(d->side, i)];
        if (button) {
            changeChildIndex_Widget(buttons, button, index++);
        }
    }
}

static iBool moveTab_SidebarWidget_(iSidebarWidget *d, const iWidget *button, int steps) {
    /* Steps are counted in visible buttons. */
    int    visiblePos[max_SidebarMode];
    size_t numVisible = 0;
    size_t from       = iInvalidPos;
    for (int i = 0; i < max_SidebarMode; i++) {
        const iWidget *b = constAs_Widget(d->modeButtons[tabOrder_[d->side][i]]);
        if (b && ~flags_Widget(b) & hidden_WidgetFlag) {
            if (b == button) {
                from = numVisible;
            }
            visiblePos[numVisible++] = i;
        }
    }
    if (from == iInvalidPos) {
        return iFalse;
    }
    const size_t to = iClamp((int) from + steps, 0, (int) numVisible - 1);
    if (to == from) {
        return iTrue;
    }
    int      *order = tabOrder_[d->side];
    const int src   = visiblePos[from];
    const int dst   = visiblePos[to];
    const int mode  = order[src];
    if (src < dst) {
        memmove(order + src, order + src + 1, sizeof(int) * (dst - src));
    }
    else {
        memmove(order + dst + 1, order + dst, sizeof(int) * (src - dst));
    }
    order[dst] = mode;
    postCommand_App("~sidebar.modes.changed");
    return iTrue;
}

static void updateSlidingSheetHeight_SidebarWidget_(iSidebarWidget *sidebar, iRoot *root) {
    if (!isPortraitPhone_App() || !isVisible_Widget(sidebar)) return;
    /* The list's VisBuf is sized to fit the largest possible sheet height so its
       backing textures aren't reallocated on every frame during animations. */
    setMinVisBufHeight_ListWidget(list_SidebarWidget_(sidebar), height_Rect(safeRect_Root(root)));
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
    d->pendingWidth     = -1.0f;
    d->outlineWidth     = 0;
    d->feedsMode        = all_FeedsMode;
    d->shelfSort        = recent_BooksSortOrder;
    d->identSort        = manual_CertListSortOrder;
    d->shelfOrder       = new_StringArray();
    d->isShelfOrderValid = iFalse;
    d->midHeight        = 0;
    d->isEditing        = iFalse;
    d->isShowingUrls    = iFalse;
    d->wasLandscape     = isLandscape_App();
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
    d->openFolders   = new_IntSet();
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
        setFlags2_Widget(sheetTitle, slidingSheetDraggable_WidgetFlag2, iTrue);  /* phone */
        setFlags2_Widget(closeButton, slidingSheetDraggable_WidgetFlag2, iTrue); /* phone */
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
        const iBool isEnabled = isModeEnabled_SidebarWidget_(d, d->side, i);
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
        setFlags2_Widget(d->modeButtons[i], slidingSheetDraggable_WidgetFlag2, iTrue); /* phone */
        setFlags2_Widget(d->modeButtons[i], siblingOrderDraggable_WidgetFlag2, iTrue);
        setFlags_Widget(d->modeButtons[i],
                        horizontalOffset_WidgetFlag |
                            (isMobile_Platform() ? touchDrag_WidgetFlag : 0),
                        iTrue);
    }
    updateModeButtonOrder_SidebarWidget_(d);
    /* Dropdown menu for changing the mode. */
    if (!isSlidingSheet_SidebarWidget_(d) && !isTerminal_Platform()) {
        const char *barId = cstr_String(id_Widget(w));
        // clang-format off
        const iMenuItem modeDropItems[] = {
            { close_Icon " ${close}", 0, 0, format_CStr("%s.toggle", barId) },
            { gear_Icon " ${menu.sidebar.configure}", 0, 0, "preferences sidecfg:1" },
            { "---" },
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
            { normalModeLabels_[8], 0, 0, format_CStr("%s.mode force:1 arg:8", barId) },
        };
        // clang-format on
        addChildFlags_Widget(buttons,
                             iClob(makeMenuButton_LabelWidget(
                                 midEllipsis_Icon, modeDropItems, iElemCount(modeDropItems))),
                             noBackground_WidgetFlag | frameless_WidgetFlag);
    }
    setButtonFont_SidebarWidget(d, isPhone ? uiLabelBig_FontId : uiLabel_FontId);
    addChildFlags_Widget(vdiv,
                         iClob(buttons),
                         arrangeHorizontal_WidgetFlag | resizeWidthOfChildren_WidgetFlag |
                             arrangeHeight_WidgetFlag | resizeToParentWidth_WidgetFlag);
    setBackgroundColor_Widget(buttons, uiBackgroundSidebar_ColorId);
    /* Tabs for the current book and the shelf. */ {
        d->gempubTabArea = new_Widget();
        setPadding_Widget(d->gempubTabArea, 3 * gap_UI, 2 * gap_UI, 3 * gap_UI, gap_UI);
        static const char *tabLabels[3] = {
            "${sidebar.gempub.cover}", "${sidebar.gempub.index}", "${sidebar.gempub.shelf}"
        };
        for (int i = 0; i < 3; i++) {
            d->gempubTabs[i] = addChildFlags_Widget(
                d->gempubTabArea,
                iClob(new_LabelWidget(tabLabels[i],
                                      format_CStr("%s.gempub.page arg:%d", cstr_String(id_Widget(w)), i))),
                expand_WidgetFlag);
        }
        addChildFlags_Widget(vdiv,
                             iClob(d->gempubTabArea),
                             arrangeHorizontal_WidgetFlag | resizeWidthOfChildren_WidgetFlag |
                                 arrangeHeight_WidgetFlag | resizeToParentWidth_WidgetFlag |
                                 hidden_WidgetFlag | collapse_WidgetFlag);
        setBackgroundColor_Widget(d->gempubTabArea, uiBackgroundSidebar_ColorId);
    }
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
    /* The current book: cover and index pages. */ {
        d->gempubArea = makeVDiv_Widget();
        d->gempubDoc = new_SimpleDocumentWidget();
        addChildFlags_Widget(d->gempubArea, iClob(d->gempubDoc), expand_WidgetFlag);
        setBackgroundColor_Widget(d->gempubArea, uiBackgroundSidebar_ColorId);
        addChildFlags_Widget(listArea, iClob(d->gempubArea), hidden_WidgetFlag);
        d->gempubPage = 1;
        init_String(&d->gempubUrl);
        init_String(&d->gempubCentered);
    }
    d->feedsProgress = NULL;
    if (isPhone) {
        d->feedsProgress = new_IndicatorWidget();
        setManual_IndicatorWidget(d->feedsProgress, iTrue);
        addChildFlags_Widget(listArea, iClob(d->feedsProgress), hidden_WidgetFlag);
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
    }
    setBackgroundColor_Widget(d->actions, uiBackgroundSidebar_ColorId);
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
    d->trashMenu  = NULL;
    addAction_Widget(w, refreshFeeds_KeyShortcut, "feeds.refresh");
    updateMetrics_SidebarWidget_(d);
    if (side == left_SidebarSide) {
        postCommand_App("~sidebar.update"); /* unread count */
    }
}

static void applyPendingWidth_SidebarWidget_(iAny *context) {
    iSidebarWidget *d = context;
    if (d->pendingWidth >= 0.0f) {
        setWidth_SidebarWidget(d, d->pendingWidth);
        resizeSplits_MainWindow(as_MainWindow(window_Widget(d)), iFalse);
        d->pendingWidth = -1.0f;
    }
}

void deinit_SidebarWidget(iSidebarWidget *d) {
    removeTicker_App(applyPendingWidth_SidebarWidget_, d);
    iRelease(d->shelfOrder);
    deinit_String(&d->gempubCentered);
    deinit_String(&d->gempubUrl);
    iRelease(d->structureUnfolds);
    iRelease(d->structureUrls);
    deinit_String(&d->structureHost);
    deinit_String(&d->bookmarkFilter);
    delete_IntSet(d->openFolders);
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
            ? (keyMods_Sym(modState_Keys()) & SDL_KMOD_SHIFT ? new_OpenTabFlag
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
            const int tabMode = mouseTabMode ? mouseTabMode : openTabMode_Sym(modState_Keys());
            postCommandString_Root(
                get_Root(), feedEntryOpenCommand_String(&item->url, tabMode, 0, !tabMode));
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
                if (contains_IntSet(d->openFolders, item->id)) {
                    remove_IntSet(d->openFolders, item->id);
                    setRecentFolder_Bookmarks(bookmarks_App(), 0);
                }
                else {
                    insert_IntSet(d->openFolders, item->id);
                    setRecentFolder_Bookmarks(bookmarks_App(), item->id);
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
        case gempub_SidebarMode:
            if (!isEmpty_String(&item->url)) {
                const iBook *book    = find_Books(&item->bookId);
                int          tabMode = mouseTabMode ? mouseTabMode : openTabMode_Sym(modState_Keys());
                d->gempubPage = 2; /* stays on the shelf */
                if (!tabMode) {
                    iDocumentWidget *bookDoc = findTab_DocumentBook(&item->bookId, NULL);
                    if (bookDoc) {
                        postCommandf_Root(as_Widget(bookDoc)->root, "tabs.switch page:%p", bookDoc);
                        dismissPortraitPhoneSidebars_Root(as_Widget(d)->root);
                        setFocus_Widget(NULL);
                        break;
                    }
                    tabMode = new_OpenTabFlag;
                }
                /* Downloaded pages are saved as books, too. */
                postCommandf_Root(get_Root(),
                                  "open newtab:%d%s url:%s",
                                  tabMode,
                                  book && !path_Book(book) ? " overwritebook:1" : "",
                                  cstr_String(&item->url));
                dismissPortraitPhoneSidebars_Root(as_Widget(d)->root);
                setFocus_Widget(NULL);
            }
            break;
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
        setFlags_Widget(button, hidden_WidgetFlag, !isModeEnabled_SidebarWidget_(d, d->side, i));
        setAlignVisually_LabelWidget(button, isTight);
        setFlags_Widget(button, tight_WidgetFlag, isTight);
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
    checkFeedsActionsLayout_SidebarWidget_(d);
    updateItemHeight_SidebarWidget_(d);
    updateNavBarSize_Root(w->root);
    if (!isResizing_SidebarWidget_(d)) {
        reflowOutline_SidebarWidget_(d);
    }
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
    if (isHandheld_Platform()) {
        setScrollMode_ListWidget(d->list, normal_ScrollMode);
    }
}

static iBool handleSidebarCommand_SidebarWidget_(iSidebarWidget *d, const char *cmd) {
    iWidget *w = as_Widget(d);
    if (equal_Command(cmd, "width")) {
        setWidth_SidebarWidget(
            d, arg_Command(cmd) * (argLabel_Command(cmd, "gaps") ? 1.0f : (1.0f / gap_UI)));
        return iTrue;
    }
    else if (equal_Command(cmd, "gempub.page")) {
        if (argLabel_Command(cmd, "keepshelf") && d->gempubPage == 2 && isVisible_Widget(w) &&
            d->mode == gempub_SidebarMode) {
            return iTrue;
        }
        d->gempubPage = iClamp(arg_Command(cmd), 0, 2);
        d->isShelfOrderValid = iFalse;
        if (d->mode == gempub_SidebarMode) {
            updateItems_SidebarWidget_(d);
        }
        return iTrue;
    }
    else if (equal_Command(cmd, "cycle")) {
        const int dir = arg_Command(cmd);
        int       pos = modePos_SidebarWidget_(d, d->mode);
        for (int i = 0; i < max_SidebarMode; i++) {
            pos            = (pos + dir + max_SidebarMode) % max_SidebarMode;
            const int mode = orderedMode_SidebarMode(d->side, pos);
            if (isModeEnabled_SidebarWidget_(d, d->side, mode)) {
                postCommand_Widget(w, "%s.mode arg:%d", d->side == 0 ? "sidebar" : "sidebar2", mode);
                break;
            }
        }
        return iTrue;
    }
    else if (equal_Command(cmd, "tab")) {
        /* Left sidebar indices span both sidebars; right sidebar indices are its own. */
        const size_t      numLeft = numTabs_SidebarMode_(left_SidebarSide);
        const size_t      index   = arg_Command(cmd) + (d->side == right_SidebarSide ? numLeft : 0);
        enum iSidebarSide side;
        enum iSidebarMode mode;
        if (!tab_SidebarMode(index, &side, &mode)) {
            return iTrue;
        }
        if (d->side == right_SidebarSide && side != right_SidebarSide) {
            return iTrue;
        }
        postCommand_Widget(w,
                           "%s.mode force:1 arg:%d toggle:%d",
                           side == left_SidebarSide ? "sidebar" : "sidebar2",
                           mode,
                           argLabel_Command(cmd, "toggle"));
        return iTrue;
    }
    else if (equal_Command(cmd, "mode")) {
        const int mode = arg_Command(cmd);
        if (!isSlidingSheet_SidebarWidget_(d) && !argLabel_Command(cmd, "force") &&
            !isModeEnabled_SidebarWidget_(d, d->side, mode) &&
            isModeEnabled_SidebarWidget_(d, d->side ^ 1, mode)) {
            /* Make it affect the other side instead. */
            postCommand_Widget(w, "%s.mode arg:%d show:%d toggle:%d shelf:%d current:%d contents:%d",
                               d->side == 0 ? "sidebar2" : "sidebar",
                               mode,
                               argLabel_Command(cmd, "show"),
                               argLabel_Command(cmd, "toggle"),
                               argLabel_Command(cmd, "shelf"),
                               argLabel_Command(cmd, "current"),
                               argLabel_Command(cmd, "contents"));
            return iTrue;
        }
        if (mode == gempub_SidebarMode && argLabel_Command(cmd, "contents")) {
            if (isVisible_Widget(w) && d->mode == gempub_SidebarMode && d->gempubPage == 1) {
                postCommandf_App("%s.toggle", cstr_String(id_Widget(w)));
                return iTrue;
            }
            d->gempubPage = 1;
            clear_String(&d->gempubCentered);
        }
        iBool isShelfChanged = iFalse;
        if (mode == gempub_SidebarMode && argLabel_Command(cmd, "shelf")) {
            isShelfChanged = !isShowingShelf_SidebarWidget_(d);
            d->gempubPage = 2;
            d->isShelfOrderValid = iFalse;
        }
        const iBool wasChanged = setMode_SidebarWidget(d, mode);
        updateItems_SidebarWidget_(d);
        if ((argLabel_Command(cmd, "show") && !isVisible_Widget(w)) ||
            (argLabel_Command(cmd, "toggle") &&
             (!isVisible_Widget(w) || (!wasChanged && !isShelfChanged)))) {
            postCommandf_App("%s.toggle", cstr_String(id_Widget(w)));
        }
        scrollOffset_ListWidget(d->list, 0);
        if (argLabel_Command(cmd, "current")) {
            /* Scrolled after the sidebar has been shown. */
            for (size_t i = 0; i < numItems_ListWidget(d->list); i++) {
                const iListItem *item = constItem_ListWidget(d->list, i);
                if (item->flags.isSelected) {
                    postCommand_Widget(d->list, "sideitem.show arg:%zu", i);
                    break;
                }
            }
        }
        if (wasChanged) {
            postCommandf_App("%s.mode.changed arg:%d", cstr_String(id_Widget(w)), d->mode);
            if (isTerminal_Platform()) {
                // if (wasShown) {
                    // setFocus_Widget(as_Widget(list_SidebarWidget(d)));
                // }
                setCursorItem_ListWidget(list_SidebarWidget(d), 0);
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
        if (isHiding) {
            cancelDrag_ListWidget(d->list);
            d->isShelfOrderValid = iFalse;
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
            updateNavBarSize_Root(w->root);
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
                setScrollMode_ListWidget(d->list,
                                         isHandheld_Platform()
                                             ? normal_ScrollMode
                                             : disabledAtTopBothDirections_ScrollMode);
                if (isPointerHidden_Gamepad(gamepad_App())){
                    movePointerOntoWidget_Gamepad(gamepad_App(), as_Widget(d->list), 100);
                }
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
        if (!isHiding && d->mode != gempub_SidebarMode /* keys are for turning pages */ &&
            (!isMobile_Platform() || isPointerHidden_Gamepad(gamepad_App()))) {
            setFocus_Widget(as_Widget(list_SidebarWidget(d)));
        }
        else {
            setFocus_Widget(NULL);
        }
        //if (isConnected_Gamepad(gamepad_App())) {
        //    movePointerOntoWidget_Gamepad(gamepad_App(), NULL, 0); /* update hover */
        //}
        refresh_Widget(w->parent);
        return iTrue;
    }
    else if (equal_Command(cmd, "bookmarks.edit")) {
        setMobileEditMode_SidebarWidget_(d, !d->isEditing);
        invalidate_ListWidget(d->list);
    }
    return iFalse;
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

static void updateShowingUrls_SidebarWidget_(iSidebarWidget *d) {
    if (deviceType_App() != desktop_AppDeviceType || isTerminal_Platform()) {
        return;
    }
    const iBool showUrls = (modState_Keys() & SDL_KMOD_ALT) && isFocused_Widget(d->list);
    if (showUrls != d->isShowingUrls) {
        d->isShowingUrls = showUrls;
        if (d->mode == bookmarks_SidebarMode && isVisible_Widget(d)) {
            invalidate_ListWidget(d->list);
        }
    }
}

static iBool processEvent_SidebarWidget_(iSidebarWidget *d, const SDL_Event *ev) {
    iWidget *w = as_Widget(d);
    /* Handle commands. */
    if (isResize_UserEvent(ev)) {
        checkModeButtonLayout_SidebarWidget_(d);
        if (deviceType_App() == phone_AppDeviceType) {
            setFlags_Widget(
                findChild_Widget(w, "sidebar.title"), hidden_WidgetFlag, isLandscape_App());
            setFlags_Widget(
                findChild_Widget(w, "sidebar.close"), hidden_WidgetFlag, isLandscape_App());
            /* The sidebar is only dismissed when the display orientation changes.
               Note that keyboard visibility may trigger resize events. */
            const iBool isLandscape          = isLandscape_App();
            const iBool didChangeOrientation = (isLandscape != d->wasLandscape);
            d->wasLandscape                  = isLandscape;
            /* Keep sidebar open while editing. */
            if (didChangeOrientation && isVisible_Widget(w) && !d->isEditing) {
                postCommand_Widget(w, "sidebar.toggle");
            }
            setFlags_Widget(findChild_Widget(w, "buttons"),
                            drawBackgroundToHorizontalSafeArea_WidgetFlag,
                            isLandscape_App());
            setFlags_Widget(findChild_Widget(w, "actions"),
                            drawBackgroundToHorizontalSafeArea_WidgetFlag,
                            isLandscape_App());
            setFlags_Widget(d->list,
                            drawBackgroundToHorizontalSafeArea_WidgetFlag,
                            isLandscape_App());
            setFlags_Widget(w, drawBackgroundToBottom_WidgetFlag, isPortrait_App());
            setBackgroundColor_Widget(
                w, isPortrait_App() ? uiBackgroundSidebar_ColorId : none_ColorId);
            updateFeedsProgress_SidebarWidget_(d);
        }
        updateActionsPadding_SidebarWidget_(d);
        checkFeedsActionsLayout_SidebarWidget_(d);
        return iFalse;
    }
    else if (isMetricsChange_UserEvent(ev)) {
        w->rect.size.x = d->widthAsGaps * gap_UI;
        updateMetrics_SidebarWidget_(d);
        arrange_Widget(w);
        checkModeButtonLayout_SidebarWidget_(d);
        checkFeedsActionsLayout_SidebarWidget_(d);
    }
    else if (isCommand_SDLEvent(ev)) {
        const char *cmd = command_UserEvent(ev);
        if (equal_Command(cmd, "focus.gained") || equal_Command(cmd, "focus.lost")) {
            updateShowingUrls_SidebarWidget_(d);
        }
        if ((equal_Command(cmd, "tabs.changed") &&
             startsWith_Rangecc(range_Command(cmd, "id"), "doc")) ||
            equal_Command(cmd, "document.changed")) {
            /* The Book mode is only available for books. */
            checkModeButtonLayout_SidebarWidget_(d);
            arrange_Widget(parent_Widget(d->modeButtons[0]));
            if (d->mode != bookmarks_SidebarMode) {
                updateItems_SidebarWidget_(d);
            }
            if (d->mode != siteStructure_SidebarMode) {
                scrollOffset_ListWidget(d->list, 0);
            }
        }
        else if (equal_Command(cmd, "sidebar.modes.changed")) {
            updateModeButtonOrder_SidebarWidget_(d);
            checkModeButtonLayout_SidebarWidget_(d);
            arrange_Widget(parent_Widget(d->modeButtons[0]));
            refresh_Widget(parent_Widget(d->modeButtons[0]));
        }
        else if (equal_Command(cmd, "tabs.move") && argLabel_Command(cmd, "dragged") &&
                 moveTab_SidebarWidget_(d, pointer_Command(cmd), arg_Command(cmd))) {
            return iTrue;
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
        else if (d->mode == openDocuments_SidebarMode &&
                 (equal_Command(cmd, "document.request.started") ||
                  equal_Command(cmd, "document.request.finished") ||
                  equal_Command(cmd, "doctabs.order.changed"))) {
            /* TODO: There are no notifications for audio player starting/stopping.
               These would be useful for updating the active-player status icons. */
            updateItemsWithFlags_SidebarWidget_(d, iTrue);
            return iFalse;
        }
        else if (equal_Command(cmd, "idents.changed") && d->mode == identities_SidebarMode) {
            updateItems_SidebarWidget_(d);
            return iTrue;
        }
        else if (equalWidget_Command(cmd, w, "idents.sort")) {
            d->identSort = iClamp(arg_Command(cmd), 0, name_CertListSortOrder);
            if (d->certList) {
                setSortOrder_CertListWidget(d->certList, d->identSort);
                if (d->mode == identities_SidebarMode) {
                    updateItemsWithFlags_SidebarWidget_(d, iTrue);
                }
            }
            return iTrue;
        }
        else if (equal_Command(cmd, "sync.prefs.changed") && d->mode == identities_SidebarMode) {
            updateItems_SidebarWidget_(d);
            return iFalse;
        }
        else if (isPortraitPhone_App() && isVisible_Widget(w) && d->side == left_SidebarSide &&
                 equal_Command(cmd, "swipe.forward")) {
            postCommand_App("sidebar.toggle");
            return iTrue;
        }
        else if (startsWith_Command(cmd, cstr_String(&d->cmdPrefix))) {
            if (handleSidebarCommand_SidebarWidget_(
                    d, skipPrefix_Command(cmd) + size_String(&d->cmdPrefix))) {
                return iTrue;
            }
        }
        else if (equal_Command(cmd, "readingbars.changed")) {
            updateActionsPadding_SidebarWidget_(d);
            arrange_Widget(w);
            return iFalse;
        }
        else if (equal_Command(cmd, "menu.closed") && d->menu == pointer_Command(cmd)) {
            if (!isMobile_Platform() || isPointerHidden_Gamepad(gamepad_App())) {
                setFocus_Widget(as_Widget(d->list));
            }
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
                    applyPendingWidth_SidebarWidget_(d);
                    reflowOutline_SidebarWidget_(d);
                    /* Final size update in case it was resized. */
                    updateSize_DocumentWidget(document_App());
                    resizeSplits_MainWindow(as_MainWindow(window_Widget(d)), iTrue);
                    refresh_Widget(d->resizer);
                    updateNavBarSize_Root(w->root);
                }
            }
            return iTrue;
        }
        else if (isCommand_Widget(w, ev, "mouse.moved")) {
            if (isResizing_SidebarWidget_(d)) {
                const iInt2 inner  = windowToInner_Widget(w, coord_Command(cmd));
                const int   resMid = d->resizer->rect.size.x / 2;
                /* Mouse motion may arrive faster than the layout can be updated. */
                d->pendingWidth =
                    ((d->side == left_SidebarSide
                          ? inner.x
                          : (right_Rect(rect_Root(w->root)) - coord_Command(cmd).x)) +
                     resMid) /
                    (float) gap_UI;
                addTicker_App(applyPendingWidth_SidebarWidget_, d);
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
        else if (equal_Command(cmd, "feeds.update.started") ||
                 equal_Command(cmd, "feeds.update.progress")) {
            if (d->feedsProgress) {
                /* Visible from the start. */
                const int total = argLabel_Command(cmd, "total");
                setProgress_IndicatorWidget(
                    d->feedsProgress,
                    0.05f + 0.95f * (total ? (float) arg_Command(cmd) / total : 0.0f));
            }
        }
        else if (equal_Command(cmd, "feeds.update.finished")) {
            if (d->feedsProgress) {
                setProgress_IndicatorWidget(d->feedsProgress, 1.0f);
            }
            d->numUnreadEntries = argLabel_Command(cmd, "unread");
            checkModeButtonLayout_SidebarWidget_(d);
            if (d->mode == feedEntries_SidebarMode || d->mode == subscriptions_SidebarMode) {
                updateItems_SidebarWidget_(d);
            }
        }
        else if (handleBookmarkCommand_SidebarWidget_(d, ev, cmd) ||
                 handleFeedCommand_SidebarWidget_(d, ev, cmd) ||
                 handleStructureCommand_SidebarWidget_(d, ev, cmd) ||
                 handleShelfCommand_SidebarWidget_(d, ev, cmd)) {
            return iTrue;
        }
        else if (startsWith_Command(cmd, "sideitem.")) {
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
                    iString *link = responseLink_Feeds(&item->url);
                    if (link) {
                        SDL_SetClipboardText(cstr_String(link));
                        delete_String(link);
                    }
                    else {
                        SDL_SetClipboardText(cstr_String(argLabel_Command(cmd, "canon")
                                                             ? canonicalUrl_String(&item->url)
                                                             : &item->url));
                    }
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
                if (isBlockedBySync_BookmarkDialogs()) {
                    return iTrue;
                }
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
                if (isBlockedBySync_BookmarkDialogs()) {
                    return iTrue;
                }
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
    if (ev->type == SDL_EVENT_MOUSE_MOTION &&
        (!isVisible_Widget(d->menu) && !isVisible_Widget(d->modeMenu))) {
        const iInt2 mouse = init_I2(ev->motion.x, ev->motion.y);
        if (contains_Widget(d->resizer, mouse)) {
            setCursor_Window(get_Window(), SDL_SYSTEM_CURSOR_EW_RESIZE);
        }
        /* Update cursor. */
        else if (contains_Widget(w, mouse)) {
            const iSidebarItem *item = constHoverItem_ListWidget(d->list);
            setCursor_Window(get_Window(),
                             item ? (item->listItem.flags.isSeparator ? SDL_SYSTEM_CURSOR_DEFAULT
                                                                      : SDL_SYSTEM_CURSOR_POINTER)
                                  : SDL_SYSTEM_CURSOR_DEFAULT);
        }
        if (d->contextIndex != iInvalidPos) {
            invalidateItem_ListWidget(d->list, d->contextIndex);
            d->contextIndex = iInvalidPos;
        }
    }
    /* Update context menu items. */
    if (d->menu && ev->type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
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
                    updateBookmarkContextMenu_SidebarWidget_(d);
                }
                else if (d->mode == gempub_SidebarMode && d->contextItem) {
                    updateShelfContextMenu_SidebarWidget_(d);
                }
                else if (d->mode == feedEntries_SidebarMode && d->contextItem) {
                    updateFeedContextMenu_SidebarWidget_(d);
                }
            }
        }
    }
    if ((ev->type == SDL_EVENT_KEY_DOWN || ev->type == SDL_EVENT_KEY_UP) && isMod_Sym(ev->key.key)) {
        updateShowingUrls_SidebarWidget_(d);
    }
    if (ev->type == SDL_EVENT_KEY_DOWN) {
        const int key   = ev->key.key;
        const int kmods = keyMods_Sym(ev->key.mod);
        /* Hide the sidebar when Escape is pressed. */
        if (kmods == 0 && key == SDLK_ESCAPE && isVisible_Widget(d)) {
            postCommand_Widget(d, "%s.toggle", cstr_String(id_Widget(w)));
            return iTrue;
        }
    }
    if (isEdgeSwipable_SidebarWidget_(d)) {
        if (ev->type == SDL_EVENT_MOUSE_WHEEL && isPerPixel_MouseWheelEvent(&ev->wheel)) {
            if (d->side == left_SidebarSide && ev->wheel.x < 0 && isVisible_Widget(w)) {
                postCommand_Widget(w, "sidebar.toggle");
                return iTrue;
            }
        }
    }
    else if (isSlidingSheet_SidebarWidget_(d)) {
        if (ev->type == SDL_EVENT_MOUSE_WHEEL) {
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
            #if 0
                if (ev->wheel.which == mouseId_Gamepad) {
                    /* With the gamepad, you can't slide the sheet, only scroll the list. */
                    setScrollMode_ListWidget(d->list, normal_ScrollMode);
                }
            #endif
                return iFalse;
            }
            return iTrue;
        }
        if (ev->type == SDL_EVENT_USER && ev->user.code == widgetTouchEnds_UserEventCode &&
            widgetMode_Touch(w) != momentum_WidgetTouchMode) {
            gotoNearestSlidingSheetPos_SidebarWidget_(d);
            return iTrue;
        }
    }
    if (ev->type == SDL_EVENT_MOUSE_BUTTON_DOWN &&
        contains_Widget(as_Widget(d->list), init_I2(ev->button.x, ev->button.y))) {
        if (hoverItem_ListWidget(d->list) || isVisible_Widget(d->menu) ||
            isVisible_Widget(d->folderMenu)) {
            iWidget *contextMenu = d->menu;
            /* Update the menu before opening. */
            /* TODO: This kind of updating is already done above, and in `updateContextMenu_`... */
            const iSidebarItem *hoverItem = hoverItem_ListWidget(d->list);
            if (d->mode == bookmarks_SidebarMode) {
                contextMenu = bookmarkContextMenu_SidebarWidget_(d, hoverItem);
                if (!contextMenu) {
                    return iTrue;
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

iBeginDefineSubclass(SidebarWidget, Widget).processEvent = (iAny *) processEvent_SidebarWidget_,
                                    .draw                = (iAny *) draw_SidebarWidget_,
                                    iEndDefineSubclass(SidebarWidget)
