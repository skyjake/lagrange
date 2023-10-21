/* Copyright 2023 Jaakko Keränen <jaakko.keranen@iki.fi>

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

#include "snippetwidget.h"
#include "command.h"
#include "inputwidget.h"
#include "labelwidget.h"
#include "listwidget.h"
#include "../snippets.h"
#include "app.h"

#include <SDL_clipboard.h>

iDeclareType(SnippetItem)
typedef iListItemClass iSnippetItemClass;

struct Impl_SnippetItem {
    iListItem listItem;
    iString   label;
    iString   content;
};

void init_SnippetItem(iSnippetItem *d) {
    init_ListItem(&d->listItem);
    init_String(&d->label);
    init_String(&d->content);
}

void deinit_SnippetItem(iSnippetItem *d) {
    deinit_String(&d->content);
    deinit_String(&d->label);
}

static void draw_SnippetItem_(const iSnippetItem *d, iPaint *p, iRect itemRect,
                              const iListWidget *list);

iBeginDefineSubclass(SnippetItem, ListItem)
    .draw = (iAny *) draw_SnippetItem_,
iEndDefineSubclass(SnippetItem)

iDefineObjectConstruction(SnippetItem)

/*----------------------------------------------------------------------------------------------*/

struct Impl_SnippetWidget {
    iWidget widget;
    iListWidget *list;
    iWidget *menu;
    size_t contextPos;
    int itemFonts[2];
};

iDefineObjectConstruction(SnippetWidget)

static void updateItems_SnippetWidget_(iSnippetWidget *d) {
    clear_ListWidget(d->list);
    const char *lineBreakSymbol =
        format_CStr("%s" return_Icon " " restore_ColorEscape, escape_Color(uiAnnotation_ColorId));
    iConstForEach(StringArray, i, names_Snippets()) {
        const iString *name = i.value;
        iSnippetItem *item = new_SnippetItem();
        set_String(&item->label, name);
        set_String(&item->content, get_Snippets(name));
        replace_String(&item->content, "\n", lineBreakSymbol);
        addItem_ListWidget(d->list, item);
        iRelease(item);
    }
    updateVisible_ListWidget(d->list);
    invalidate_ListWidget(d->list);
}

void init_SnippetWidget(iSnippetWidget *d) {
    iWidget *w = as_Widget(d);
    init_Widget(w);
    setId_Widget(w, "sniped");
    setFlags_Widget(w, resizeChildren_WidgetFlag | arrangeVertical_WidgetFlag, iTrue);
    iLabelWidget *addButton = newKeyMods_LabelWidget("${sniped.new}", SDLK_RETURN, 0, "sniped.new");
    setId_Widget(as_Widget(addButton), "sniped.new");
    addChildFlags_Widget(w, iClob(addButton), drawKey_WidgetFlag | alignLeft_WidgetFlag);
    d->list = new_ListWidget();
    switch (deviceType_App()) {
        case phone_AppDeviceType:
            d->itemFonts[0] = uiLabelBig_FontId;
            d->itemFonts[1] = uiLabelBigBold_FontId;
            break;
        case tablet_AppDeviceType:
            d->itemFonts[0] = uiLabelMedium_FontId;
            d->itemFonts[1] = uiLabelMediumBold_FontId;
            break;
        default:
            d->itemFonts[0] = uiLabel_FontId;
            d->itemFonts[1] = uiLabelBold_FontId;
            break;
    }
    setItemHeight_ListWidget(d->list, lineHeight_Text(d->itemFonts[0]) * 2.5f);
    setPadding_Widget(as_Widget(d->list), 0, gap_UI, 0, gap_UI);
    addChildFlags_Widget(w, iClob(d->list), expand_WidgetFlag);
    updateItems_SnippetWidget_(d);
    d->menu = makeMenu_Widget(
        w,
        (iMenuItem[]){ { edit_Icon " ${menu.snip.edit}", 0, 0, "sniped.edit" },
                       { copy_Icon " ${menu.snip.clipboard}", 0, 0, "sniped.clipboard" },
                       { "---" },
                       { delete_Icon " " uiTextCaution_ColorEscape "${menu.snip.delete}", 0, 0, "sniped.delete" } },
        4);
    d->contextPos = iInvalidPos;
}

void deinit_SnippetWidget(iSnippetWidget *d) {
    iUnused(d);
}

iListWidget *list_SnippetWidget(iSnippetWidget *d) {
    return d->list;
}

static iBool processEvent_SnippetWidget_(iSnippetWidget *d, const SDL_Event *ev) {
    iWidget *w = as_Widget(d);
    if (isCommand_UserEvent(ev, "sniped.new")) {
        makeSnippetCreation_Widget();
        return iTrue;
    }
    else if (isCommand_UserEvent(ev, "snippets.changed")) {
        const char *cmd = command_UserEvent(ev);
        updateItems_SnippetWidget_(d);
        if (hasLabel_Command(cmd, "added")) {
            /* Scroll to the added new item. */
            const char *added = suffixPtr_Command(cmd, "added");
            for (size_t i = 0; i < numItems_ListWidget(d->list); i++) {
                const iSnippetItem *item = constItem_ListWidget(d->list, i);
                if (!cmp_String(&item->label, added)) {
                    scrollToItem_ListWidget(d->list, i, 350);
                    break;
                }
            }
        }
        return iFalse;
    }
    else if (isCommand_Widget(w, ev, "list.clicked")) {
        const char *cmd = command_UserEvent(ev);
        d->contextPos = arg_Command(cmd);
        openMenu_Widget(d->menu, mouseCoord_Window(get_Window(),
                                                   argU32Label_Command(cmd, "device")));
        return iTrue;
    }
    else if (isCommand_Widget(w, ev, "sniped.edit")) {
        const iSnippetItem *item = constItem_ListWidget(d->list, d->contextPos);
        if (item) {
            iWidget *dlg = makeSnippetCreation_Widget();
            setTextCStr_LabelWidget(findChild_Widget(dlg, "heading.snip"), "${heading.snip.edit}");
            setText_InputWidget(findChild_Widget(dlg, "snip.name"), &item->label);
            iInputWidget *content = findChild_Widget(dlg, "snip.content");
            setText_InputWidget(content, get_Snippets(&item->label));
            setFocus_Widget(as_Widget(content));
        }
        return iTrue;
    }
    else if (isCommand_Widget(w, ev, "sniped.clipboard")) {
        const iSnippetItem *item = constItem_ListWidget(d->list, d->contextPos);
        if (item) {
            SDL_SetClipboardText(cstr_String(get_Snippets(&item->label)));
        }
        return iTrue;
    }
    else if (isCommand_Widget(w, ev, "sniped.delete")) {
        const iSnippetItem *item = constItem_ListWidget(d->list, d->contextPos);
        if (item) {
            set_Snippets(&item->label, NULL);
            updateItems_SnippetWidget_(d);
        }
        return iTrue;
    }
    if (ev->type == SDL_MOUSEBUTTONDOWN && ev->button.button == SDL_BUTTON_RIGHT) {
        if (!isVisible_Widget(d->menu)) {
            d->contextPos = hoverItemIndex_ListWidget(d->list);
        }
    }
    if (d->contextPos != iInvalidPos) {
        processContextMenuEvent_Widget(d->menu, ev, {});
    }
    return processEvent_Widget(w, ev);
}

static void draw_SnippetWidget_(const iSnippetWidget *d) {
    const iWidget *w = constAs_Widget(d);
    drawBackground_Widget(w);
    drawChildren_Widget(w);
}

static void draw_SnippetItem_(const iSnippetItem *d, iPaint *p, iRect itemRect,
                              const iListWidget *list) {
    const iSnippetWidget *parent = (const iSnippetWidget *) parent_Widget(list);
    const int   font       = parent->itemFonts[0];
    const int   itemHeight = height_Rect(itemRect);
    const int   line       = lineHeight_Text(font);
    const iBool isMenuOpen = isVisible_Widget(parent->menu);
    const iBool isHover    = (!isMenuOpen &&
                              isHover_Widget(constAs_Widget(list)) &&
                              constHoverItem_ListWidget(list) == d) ||
                             (isMenuOpen &&
                              d == constItem_ListWidget(list, parent->contextPos));
    int         fg         = uiTextStrong_ColorId;
    int         fg2        = uiTextDim_ColorId;
    int         bg         = uiBackground_ColorId;
    if (isHover) {
        bg = uiBackgroundFramelessHover_ColorId;
        fillRect_Paint(p, itemRect, bg);
    }
    if (isMobile_Platform()) {
        adjustEdges_Rect(&itemRect, 0, -3 * gap_UI, 0, 3 * gap_UI);
    }
    iInt2 pos = init_I2(left_Rect(itemRect) + 3 * gap_UI,
                        top_Rect(itemRect) + itemHeight / 2 - line);
    drawRange_Text(parent->itemFonts[1], pos, fg, range_String(&d->label));
    pos.y += line;
    drawRange_Text(font, pos, fg2, range_String(&d->content));
}

iBeginDefineSubclass(SnippetWidget, Widget)
    .processEvent = (iAny *) processEvent_SnippetWidget_,
    .draw         = (iAny *) draw_SnippetWidget_,
iEndDefineSubclass(SnippetWidget)

