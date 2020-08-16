#pragma once

#include "widget.h"

enum iSidebarMode {
    bookmarks_SidebarMode,
    history_SidebarMode,
    identities_SidebarMode,
    documentOutline_SidebarMode,
    max_SidebarMode
};

iDeclareWidgetClass(SidebarWidget)
iDeclareObjectConstruction(SidebarWidget)

void                setMode_SidebarWidget   (iSidebarWidget *, enum iSidebarMode mode);

enum iSidebarMode   mode_SidebarWidget      (const iSidebarWidget *);
int                 width_SidebarWidget     (const iSidebarWidget *);
void                setWidth_SidebarWidget  (iSidebarWidget *, int width);
