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

#include "ui/util.h"

iDeclareType(MenuItem)
iDeclareType(Window)
iDeclareType(Widget)

/* Platform-specific functionality for macOS */

iBool   shouldDefaultToMetalRenderer_MacOS  (void);

void    enableMomentumScroll_MacOS  (void);
void    registerURLHandler_MacOS    (void);
void    setupApplication_MacOS      (void);
void    hideTitleBar_MacOS          (iWindow *window);
void    insertMenuItems_MacOS       (const char *menuLabel, int atIndex, int firstItemIndex, const iMenuItem *items, size_t count);
void    updateMenuItems_MacOS       (int atIndex, const iMenuItem *items, size_t count);
void    removeMenu_MacOS            (int atIndex);
void    removeMenuItems_MacOS       (int atIndex, int firstItem, int numItems);
void    enableMenu_MacOS            (const char *menuLabel, iBool enable);
void    enableMenuIndex_MacOS       (int index, iBool enable);
void    enableMenuItem_MacOS        (const char *menuItemCommand, iBool enable);
void    enableMenuItemsByKey_MacOS  (int key, int kmods, iBool enable);
void    enableMenuItemsOnHomeRow_MacOS(iBool enable);
void    handleCommand_MacOS         (const char *cmd);

void    showPopupMenu_MacOS         (iWidget *source, iInt2 windowCoord, const iMenuItem *items, size_t n);
