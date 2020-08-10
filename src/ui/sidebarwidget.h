#pragma once

#include "widget.h"

enum iSidebarMode {
    bookmarks_SidebarMode,
    history_SidebarMode,
    documentOutline_SidebarMode,
    identities_SidebarMode,
};

iDeclareWidgetClass(SidebarWidget)
iDeclareObjectConstruction(SidebarWidget)

void    setMode_SidebarWidget   (iSidebarWidget *, enum iSidebarMode mode);

enum iSidebarMode mode_SidebarWidget(const iSidebarWidget *);
