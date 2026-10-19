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

#pragma once

#include "widget.h"

#include <the_Foundation/intset.h>

iDeclareType(ListWidget)

const char *    icon_SidebarMode    (enum iSidebarMode mode);

enum iSidebarSide {
    left_SidebarSide,
    right_SidebarSide,
};

/* Configured tabs in visible order, left sidebar first. */
iBool           tab_SidebarMode     (size_t index, enum iSidebarSide *side, enum iSidebarMode *mode);

/* Tab order is shared by all sidebars on the same side. */
enum iSidebarMode orderedMode_SidebarMode       (enum iSidebarSide side, size_t pos);
void            serializeOrder_SidebarMode      (iStream *outs);
void            deserializeOrder_SidebarMode    (iStream *ins, iBool apply);

enum iFeedsMode {
    all_FeedsMode,
    unread_FeedsMode
};

enum iSubscriptionsSortOrder {
    name_SubscriptionsSortOrder,
    mostRecent_SubscriptionsSortOrder,
    leastRecent_SubscriptionsSortOrder,
    numEntries_SubscriptionsSortOrder,
};

iDeclareWidgetClass(SidebarWidget)
iDeclareObjectConstructionArgs(SidebarWidget, enum iSidebarSide side)

iBool               setMode_SidebarWidget       (iSidebarWidget *, enum iSidebarMode mode);
void                setWidth_SidebarWidget      (iSidebarWidget *, float widthAsGaps);
iBool               setButtonFont_SidebarWidget (iSidebarWidget *, int font);
void                setOpenFolders_SidebarWidget    (iSidebarWidget *, const iIntSet *openFolders);
void                setMidHeight_SidebarWidget  (iSidebarWidget *, int midHeight); /* phone layout */

enum iSidebarMode   mode_SidebarWidget          (const iSidebarWidget *);
enum iFeedsMode     feedsMode_SidebarWidget     (const iSidebarWidget *);
int                 shelfSort_SidebarWidget     (const iSidebarWidget *); /* enum iBooksSortOrder */
int                 identSort_SidebarWidget     (const iSidebarWidget *); /* enum iCertListSortOrder */
int                 subSort_SidebarWidget       (const iSidebarWidget *); /* enum iSubscriptionsSortOrder */
float               width_SidebarWidget         (const iSidebarWidget *);
const iIntSet *     openFolders_SidebarWidget   (const iSidebarWidget *);
iBool               isShowingTrash_SidebarWidget(const iSidebarWidget *);

iListWidget *       list_SidebarWidget          (iSidebarWidget *);

enum iSidebarSide   bookSide_SidebarWidget      (void); /* first side with the Book tab */

void    openMarkAllReadMenu_Widget  (iWidget *anchor, const char *command); /* asks for confirmation */
