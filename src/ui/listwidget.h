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

#include "scrollwidget.h"
#include "paint.h"

#include <the_Foundation/ptrarray.h>

iDeclareType(ListWidget)

iBeginDeclareClass(ListItem)
    void    (*draw) (const iAnyObject *, iPaint *p, iRect rect, const iListWidget *list);
iEndDeclareClass(ListItem)

iDeclareType(ListItem)

struct Impl_ListItem {
    iObject object;
    iBool   isSeparator;
    iBool   isSelected;
};

iDeclareObjectConstruction(ListItem)

iDeclareWidgetClass(ListWidget)
iDeclareObjectConstruction(ListWidget)

void    setItemHeight_ListWidget    (iListWidget *, int itemHeight);

void    invalidate_ListWidget       (iListWidget *);
void    invalidateItem_ListWidget   (iListWidget *, size_t index);
void    clear_ListWidget            (iListWidget *);
void    addItem_ListWidget          (iListWidget *, iAnyObject *item);

iScrollWidget * scroll_ListWidget   (iListWidget *);

int     itemHeight_ListWidget       (const iListWidget *);
int     scrollPos_ListWidget        (const iListWidget *);

void    setScrollPos_ListWidget     (iListWidget *, int pos);
void    scrollToItem_ListWidget     (iListWidget *, size_t index);
void    scrollOffset_ListWidget     (iListWidget *, int offset);
void    updateVisible_ListWidget    (iListWidget *);
void    updateMouseHover_ListWidget (iListWidget *);

void                sort_ListWidget             (iListWidget *, int (*cmp)(const iListItem **item1, const iListItem **item2));

iAnyObject *        item_ListWidget             (iListWidget *, size_t index);
iAnyObject *        hoverItem_ListWidget        (iListWidget *);

size_t              numItems_ListWidget         (const iListWidget *);
int                 visCount_ListWidget         (const iListWidget *);
size_t              itemIndex_ListWidget        (const iListWidget *, iInt2 pos);
const iAnyObject *  constItem_ListWidget        (const iListWidget *, size_t index);
const iAnyObject *  constHoverItem_ListWidget   (const iListWidget *);

iLocalDef iBool isEmpty_ListWidget(const iListWidget *d) { return numItems_ListWidget(d) == 0; }

iBool   isMouseDown_ListWidget      (const iListWidget *);
