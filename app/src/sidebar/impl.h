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

#pragma once

/* Private declarations shared by the SidebarWidget sources. The core is in ui/sidebarwidget.c,
   and the contents of the more involved modes are in the other sources under sidebar. */

#include "books.h"
#include "render/font.h"
#include "ui/certlistwidget.h"
#include "ui/listwidget.h"
#include "ui/sidebarwidget.h"

#include <the_Foundation/intset.h>
#include <the_Foundation/stringarray.h>
#include <the_Foundation/stringset.h>
#include <the_Foundation/time.h>
#include <SDL3/SDL_events.h>
#include <SDL3/SDL_timer.h>

iDeclareType(Gempub)
iDeclareType(IndicatorWidget)
iDeclareType(LabelWidget)
iDeclareType(MenuItem)
iDeclareType(Root)
iDeclareType(SimpleDocumentWidget)

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
    int       count;
    iTime     ts;
    float     progress; /* of a book; negative if not started */
    uint64_t  size;
    iString   bookId;
};

iDeclareObjectConstruction(SidebarItem)

struct Impl_SidebarWidget {
    iWidget           widget;

    /* Widget structure. */
    iLabelWidget     *modeButtons[max_SidebarMode];
    iWidget          *blank;
    iListWidget      *list;
    iWidget          *actions;      /* below the list, area for buttons */
    iWidget          *resizer;
    iIndicatorWidget *feedsProgress; /* phone only: the sheet covers the navbar indicator */
    iWidget          *menu;          /* context menu for an item */
    iWidget          *modeMenu;      /* context menu for the sidebar mode (no item) */
    iWidget          *folderMenu;    /* context menu for bookmark folders */
    iWidget          *trashMenu;     /* context menu for trashed bookmarks */
    iWidget          *multiMenu;     /* context menu for multiple selected items */

    /* State. */
    enum iSidebarSide side;
    enum iSidebarMode mode;
    iString           cmdPrefix;
    iBool             hasSpecialTabs; /* Reader or Gempub tabs are open */
    int               buttonFont;
    int               itemFonts[2];
    iLabelWidget     *firstVisibleModeButton;
    int               modeScroll[max_SidebarMode];
    size_t            modeCursor[max_SidebarMode];
    iSidebarItem     *contextItem;   /* list item accessed in the context menu */
    size_t            contextIndex;  /* index of list item accessed in the context menu */

    /* Metrics. */
    int               midHeight;         /* on portrait phone, the height for the middle state */
    iBool             wasLandscape;      /* orientation at the time of the previous resize */
    float             widthAsGaps;
    float             pendingWidth;      /* applied once per frame while resizing; <0 if none */
    int               itemCharWidths[2]; /* average for `itemFonts`, for estimating wrapping */
    int               maxButtonLabelWidth;

    /* Bookmarks. */
    iIntSet          *openFolders;   /* otherwise folded */
    iBool             isEditing;     /* mobile edit mode */
    iBool             isShowingUrls; /* Alt held down in Bookmarks */
    iString           bookmarkFilter;
    iBool             showTrash;

    /* Feeds. */
    enum iFeedsMode   feedsMode;
    size_t            numUnreadEntries;
    SDL_TimerID       feedsAgeTimer; /* redraws the time since the latest refresh */

    /* Subscriptions. */
    enum iSubscriptionsSortOrder subSort;

    /* Identitites. */
    iCertListWidget  *certList;
    enum iCertListSortOrder identSort; /* also used by the identity list in mobile Settings */

    /* Books. */
    enum iBooksSortOrder shelfSort;
    iStringArray     *shelfOrder;     /* book IDs as shown; kept while the shelf stays visible */
    iBool             isShelfOrderValid;
    iWidget          *gempubTabArea;  /* shown when the current document is a book */
    iLabelWidget     *gempubTabs[3];
    int               gempubPage;     /* tab page: 0=cover, 1=index, 2=shelf */
    iWidget          *gempubArea;     /* a document for the current book */
    iSimpleDocumentWidget *gempubDoc;
    iString           gempubUrl;      /* page shown in `gempubDoc` */
    iString           gempubCentered; /* document (URL) whose link was centered in the index */

    /* Outline. */
    iString           outlineChapter;       /* book chapter scrolled to the top of the outline */
    int               outlineChapterPos;    /* scroll position where the chapter was left */
    int               outlineChapterOffset; /* chapter's first item from the top of the list */

    /* Path structure. */
    iStringSet       *structureUrls;
    iString           structureHost;
    iStringSet       *structureUnfolds;

};

iLocalDef iListWidget *list_SidebarWidget_(iSidebarWidget *d) {
    return d->mode == identities_SidebarMode ? (iListWidget *) d->certList : d->list;
}

/* The update functions fill the list for a mode and return False if there is nothing to show.
   The command handlers return True if the event was eaten. */

/* Core (ui/sidebarwidget.c). */
const iGempub * gempub_SidebarWidget_           (const iSidebarWidget *); /* book of the current tab */
iBool           isSlidingSheet_SidebarWidget_   (const iSidebarWidget *);
enum iFontId    actionButtonFont_SidebarWidget_ (const iSidebarWidget *);
iLabelWidget *  addActionButton_SidebarWidget_  (iSidebarWidget *, const char *label,
                                                 const char *command, int64_t flags);
iLabelWidget *  addSortDropdown_SidebarWidget_  (iSidebarWidget *, iWidget *parent, const char *id,
                                                 const iMenuItem *items, size_t numItems,
                                                 int sortOrder, int64_t dropFlags);
void            updateSortDropdown_SidebarWidget_(iSidebarWidget *, const char *id, int sortOrder);
void            updateItems_SidebarWidget_      (iSidebarWidget *);
void            updateItemsWithFlags_SidebarWidget_(iSidebarWidget *, iBool keepActions);

/* Bookmarks (sidebar/bookmarklist.c). */
iBool           updateBookmarks_SidebarWidget_  (iSidebarWidget *, iBool keepActions);
iBool           handleBookmarkCommand_SidebarWidget_(iSidebarWidget *, const SDL_Event *,
                                                     const char *cmd);
void            bookmarksDragged_SidebarWidget_ (iSidebarWidget *, const char *cmd);
void            updateBookmarkContextMenu_SidebarWidget_(iSidebarWidget *);
iWidget *       bookmarkContextMenu_SidebarWidget_(iSidebarWidget *, const iSidebarItem *hoverItem);

/* Feeds (sidebar/feedlist.c). */
iBool           updateFeedEntries_SidebarWidget_(iSidebarWidget *, iBool keepActions);
iBool           refreshFeedEntries_SidebarWidget_(iSidebarWidget *); /* False if rebuild needed */
iBool           updateSubscriptions_SidebarWidget_(iSidebarWidget *, iBool keepActions);
iBool           handleFeedCommand_SidebarWidget_(iSidebarWidget *, const SDL_Event *,
                                                 const char *cmd);
void            updateFeedContextMenu_SidebarWidget_(iSidebarWidget *);
void            stopFeedsAgeTimer_SidebarWidget_(iSidebarWidget *);
const char *    feedsRefreshAgeCStr_SidebarWidget_(void); /* NULL if never refreshed */
iString *       formatFeedsDate_SidebarWidget_  (const iTime *posted); /* date separator label */

/* Site structure (sidebar/sitestructure.c). */
iBool           updateStructure_SidebarWidget_  (iSidebarWidget *, iBool keepActions);
iBool           handleStructureCommand_SidebarWidget_(iSidebarWidget *, const SDL_Event *,
                                                      const char *cmd);

/* Books (sidebar/bookshelf.c). */
iBool           isShowingShelf_SidebarWidget_   (const iSidebarWidget *);
iBool           updateShelf_SidebarWidget_      (iSidebarWidget *, iBool keepActions);
iBool           updateGempub_SidebarWidget_     (iSidebarWidget *); /* cover or index page */
iBool           handleShelfCommand_SidebarWidget_(iSidebarWidget *, const SDL_Event *,
                                                  const char *cmd);
void            updateShelfContextMenu_SidebarWidget_(iSidebarWidget *);

/* Multiple selection and context items (sidebar/multiselect.c). Commands apply to the
   context items: the selection if the context item is part of it, otherwise just the one item. */
iBool           isMultiSelectMode_SidebarWidget_(const iSidebarWidget *);
iStringSet *    saveMultiSelection_SidebarWidget_(const iSidebarWidget *); /* NULL if none */
void            restoreMultiSelection_SidebarWidget_(iSidebarWidget *, iStringSet *selection);
iString *       saveContextItem_SidebarWidget_  (const iSidebarWidget *); /* NULL if none */
void            restoreContextItem_SidebarWidget_(iSidebarWidget *, iString *key);
iBool           isMultiContextItem_SidebarWidget_(const iSidebarWidget *, const iSidebarItem *);
void            contextItems_SidebarWidget_     (const iSidebarWidget *, iPtrArray *items);
int             sequenceTabMode_SidebarWidget_  (int newTab, size_t index);
void            postOpenCommands_SidebarWidget_ (iRoot *, const iStringArray *cmds);
iWidget *       makeMultiMenu_SidebarWidget_    (iSidebarWidget *);
void            moveTabs_SidebarWidget_         (iSidebarWidget *, const iPtrArray *moving,
                                                 size_t dstPos);
