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

enum iSidebarMode {
    bookmarks_SidebarMode,
    feeds_SidebarMode,
    history_SidebarMode,
    identities_SidebarMode,
    documentOutline_SidebarMode,
    max_SidebarMode
};

const char *    icon_SidebarMode    (enum iSidebarMode mode);

enum iSidebarSide {
    left_SidebarSide,
    right_SidebarSide,
};

enum iFeedsMode {
    all_FeedsMode,
    unread_FeedsMode
};

iDeclareWidgetClass(SidebarWidget)
iDeclareObjectConstructionArgs(SidebarWidget, enum iSidebarSide side)

iBool               setMode_SidebarWidget       (iSidebarWidget *, enum iSidebarMode mode);
void                setWidth_SidebarWidget      (iSidebarWidget *, float widthAsGaps);
iBool               setButtonFont_SidebarWidget (iSidebarWidget *, int font);
void                setClosedFolders_SidebarWidget  (iSidebarWidget *, const iIntSet *closedFolders);
void                setMidHeight_SidebarWidget  (iSidebarWidget *, int midHeight); /* phone layout */

enum iSidebarMode   mode_SidebarWidget          (const iSidebarWidget *);
enum iFeedsMode     feedsMode_SidebarWidget     (const iSidebarWidget *);
float               width_SidebarWidget         (const iSidebarWidget *);
const iIntSet *     closedFolders_SidebarWidget (const iSidebarWidget *);

iListWidget *       list_SidebarWidget          (iSidebarWidget *);
