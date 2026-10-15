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

iDeclareType(Gempub)
iDeclareType(IndicatorWidget)
iDeclareType(LabelWidget)
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
    enum iSidebarSide side;
    enum iSidebarMode mode;
    enum iFeedsMode   feedsMode;
    enum iBooksSortOrder shelfSort;
    enum iCertListSortOrder identSort; /* also used by the identity list in mobile Settings */
    iStringArray     *shelfOrder;    /* book IDs as shown; kept while the shelf stays visible */
    iBool             isShelfOrderValid;
    iString           cmdPrefix;
    iWidget          *blank;
    iListWidget      *list;
    iCertListWidget  *certList;
    iWidget          *gempubTabArea; /* shown when the current document is a book */
    iLabelWidget     *gempubTabs[3];
    iWidget          *gempubArea;   /* a document for the current book */
    iSimpleDocumentWidget *gempubDoc;
    int               gempubPage;   /* 0: cover, 1: index, 2: shelf */
    iString           gempubUrl;    /* page shown in `gempubDoc` */
    iString           gempubCentered; /* document whose link was centered in the index */
    iWidget          *actions;      /* below the list, area for buttons */
    iIndicatorWidget *feedsProgress; /* phone only: the sheet covers the navbar indicator */
    int               midHeight;    /* on portrait phone, the height for the middle state */
    iBool             isEditing;    /* mobile edit mode */
    iBool             isShowingUrls; /* Alt held down in Bookmarks */
    iBool             wasLandscape; /* orientation at the time of the previous resize */
    int               modeScroll[max_SidebarMode];
    iLabelWidget     *modeButtons[max_SidebarMode];
    iLabelWidget     *firstVisibleModeButton;
    int               maxButtonLabelWidth;
    float             widthAsGaps;
    float             pendingWidth;  /* applied once per frame while resizing; negative if none */
    int               buttonFont;
    int               itemFonts[2];
    size_t            numUnreadEntries;
    iWidget          *resizer;
    iWidget          *menu;          /* context menu for an item */
    iWidget          *modeMenu;      /* context menu for the sidebar mode (no item) */
    iWidget          *folderMenu;    /* context menu for bookmark folders */
    iWidget          *trashMenu;     /* context menu for trashed bookmarks */
    iSidebarItem     *contextItem;   /* list item accessed in the context menu */
    size_t            contextIndex;  /* index of list item accessed in the context menu */
    iIntSet          *openFolders;   /* otherwise folded */
    iString           bookmarkFilter;
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
void            updateItems_SidebarWidget_      (iSidebarWidget *);
void            updateItemsWithFlags_SidebarWidget_(iSidebarWidget *, iBool keepActions);

/* Bookmarks (sidebar/bookmarklist.c). */
iBool           updateBookmarks_SidebarWidget_  (iSidebarWidget *, iBool keepActions);
iBool           handleBookmarkCommand_SidebarWidget_(iSidebarWidget *, const SDL_Event *,
                                                     const char *cmd);
void            bookmarkMoved_SidebarWidget_    (iSidebarWidget *, size_t index, size_t dstIndex,
                                                 iBool isBefore);
void            bookmarkMovedOntoFolder_SidebarWidget_(iSidebarWidget *, size_t index,
                                                       size_t folderIndex);
void            updateBookmarkContextMenu_SidebarWidget_(iSidebarWidget *);
iWidget *       bookmarkContextMenu_SidebarWidget_(iSidebarWidget *, const iSidebarItem *hoverItem);

/* Feeds (sidebar/feedlist.c). */
iBool           updateFeedEntries_SidebarWidget_(iSidebarWidget *, iBool keepActions);
iBool           updateSubscriptions_SidebarWidget_(iSidebarWidget *);
iBool           handleFeedCommand_SidebarWidget_(iSidebarWidget *, const SDL_Event *,
                                                 const char *cmd);
void            updateFeedContextMenu_SidebarWidget_(iSidebarWidget *);

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
