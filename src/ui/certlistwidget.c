/* Copyright 2021 Jaakko Keränen <jaakko.keranen@iki.fi>

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

#include "certlistwidget.h"

#include "documentwidget.h"
#include "command.h"
#include "labelwidget.h"
#include "listwidget.h"
#include "../gmcerts.h"
#include "../app.h"

#include <SDL_clipboard.h>

iDeclareType(CertItem)
typedef iListItemClass iCertItemClass;

struct Impl_CertItem {
    iListItem listItem;
    uint32_t  id;
    int       indent;
    iChar     icon;
    iBool     isBold;
    iString   label;
    iString   meta;
};

void init_CertItem(iCertItem *d) {
    init_ListItem(&d->listItem);
    d->id     = 0;
    d->indent = 0;
    d->icon   = 0;
    d->isBold = iFalse;
    init_String(&d->label);
    init_String(&d->meta);
}

void deinit_CertItem(iCertItem *d) {
    deinit_String(&d->meta);
    deinit_String(&d->label);
}

static void draw_CertItem_(const iCertItem *d, iPaint *p, iRect itemRect, const iListWidget *list);

iBeginDefineSubclass(CertItem, ListItem)
    .draw = (iAny *) draw_CertItem_,
iEndDefineSubclass(CertItem)

iDefineObjectConstruction(CertItem)

/*----------------------------------------------------------------------------------------------*/

struct Impl_CertListWidget {
    iListWidget  list;
    int          itemFonts[2];
    iWidget     *menu;         /* context menu for an item */
    iCertItem   *contextItem;  /* list item accessed in the context menu */
    size_t       contextIndex; /* index of list item accessed in the context menu */
};

iDefineObjectConstruction(CertListWidget)

static iGmIdentity *menuIdentity_CertListWidget_(const iCertListWidget *d) {
    if (d->contextItem) {
        return identity_GmCerts(certs_App(), d->contextItem->id);
    }
    return NULL;
}

static void updateContextMenu_CertListWidget_(iCertListWidget *d) {
    iArray *items = collectNew_Array(sizeof(iMenuItem));
    const iString *docUrl = url_DocumentWidget(document_App());
    size_t firstIndex = 0;
    if (deviceType_App() != desktop_AppDeviceType && !isEmpty_String(docUrl)) {
        pushBack_Array(items, &(iMenuItem){ format_CStr("```%s", cstr_String(docUrl)) });
        firstIndex = 1;
    }
    const iMenuItem ctxItems[] = {
        { person_Icon " ${ident.use}", 0, 0, "ident.use arg:1" },
        { close_Icon " ${ident.stopuse}", 0, 0, "ident.use arg:0" },
        { close_Icon " ${ident.stopuse.all}", 0, 0, "ident.use arg:0 clear:1" },
        { "---", 0, 0, NULL },
        { edit_Icon " ${menu.edit.notes}", 0, 0, "ident.edit" },
        { "${ident.fingerprint}", 0, 0, "ident.fingerprint" },
#if defined (iPlatformAppleDesktop)
        { magnifyingGlass_Icon " ${menu.reveal.macos}", 0, 0, "ident.reveal" },
#endif
#if defined (iPlatformLinux)
        { magnifyingGlass_Icon " ${menu.reveal.filemgr}", 0, 0, "ident.reveal" },
#endif
        { export_Icon " ${ident.export}", 0, 0, "ident.export" },
        { "---", 0, 0, NULL },
        { delete_Icon " " uiTextCaution_ColorEscape "${ident.delete}", 0, 0, "ident.delete confirm:1" },
    };
    pushBackN_Array(items, ctxItems, iElemCount(ctxItems));
    /* Used URLs. */
    const iGmIdentity *ident = menuIdentity_CertListWidget_(d);
    if (ident) {
        size_t insertPos = firstIndex + 3;
        if (!isEmpty_StringSet(ident->useUrls)) {
            insert_Array(items, insertPos++, &(iMenuItem){ "---", 0, 0, NULL });
        }
        iBool usedOnCurrentPage = iFalse;
        iConstForEach(StringSet, i, ident->useUrls) {            
            const iString *url = i.value;
            usedOnCurrentPage |= startsWithCase_String(docUrl, cstr_String(url));
            iRangecc urlStr = range_String(url);
            if (startsWith_Rangecc(urlStr, "gemini://")) {
                urlStr.start += 9; /* omit the default scheme */
            }
            insert_Array(items,
                         insertPos++,
                         &(iMenuItem){ format_CStr(globe_Icon " %s", cstr_Rangecc(urlStr)),
                                       0,
                                       0,
                                       format_CStr("!open url:%s", cstr_String(url)) });
        }
        if (!usedOnCurrentPage) {
            remove_Array(items, firstIndex + 1);
        }
        else {
            remove_Array(items, firstIndex);
        }
    }
    destroy_Widget(d->menu);    
    d->menu = makeMenu_Widget(as_Widget(d), data_Array(items), size_Array(items));    
}

static void itemClicked_CertListWidget_(iCertListWidget *d, iCertItem *item, size_t itemIndex) {
    iWidget *w = as_Widget(d);
    setFocus_Widget(NULL);
    d->contextItem  = item;
    if (d->contextIndex != iInvalidPos) {
        invalidateItem_ListWidget(&d->list, d->contextIndex);
    }
    d->contextIndex = itemIndex;
    if (itemIndex < numItems_ListWidget(&d->list)) {
        updateContextMenu_CertListWidget_(d);
        arrange_Widget(d->menu);
        openMenuFlags_Widget(d->menu,
                             bounds_Widget(w).pos.x < mid_Rect(rect_Root(w->root)).x
                                 ? topRight_Rect(itemRect_ListWidget(&d->list, itemIndex))
                                 : addX_I2(topLeft_Rect(itemRect_ListWidget(&d->list, itemIndex)),
                                           -width_Widget(d->menu)),
                             postCommands_MenuOpenFlags | setFocus_MenuOpenFlags);
    }
}

static iBool processEvent_CertListWidget_(iCertListWidget *d, const SDL_Event *ev) {
    iWidget *w = as_Widget(d);
    /* Handle commands. */
    if (ev->type == SDL_USEREVENT && ev->user.code == command_UserEventCode) {
        const char *cmd = command_UserEvent(ev);
        if (equal_Command(cmd, "idents.changed")) {
            updateItems_CertListWidget(d);
            invalidate_ListWidget(&d->list);
        }
        else if (isCommand_Widget(w, ev, "list.clicked")) {
            itemClicked_CertListWidget_(
                d, pointerLabel_Command(cmd, "item"), argU32Label_Command(cmd, "arg"));
            return iTrue;
        }
        else if (isCommand_Widget(w, ev, "ident.use")) {
            iGmIdentity *ident = menuIdentity_CertListWidget_(d);            
            const iString *tabUrl = urlQueryStripped_String(url_DocumentWidget(document_App()));
            if (ident) {
                if (argLabel_Command(cmd, "clear")) {
                    clearUse_GmIdentity(ident);
                }
                else if (arg_Command(cmd)) {
                    signIn_GmCerts(certs_App(), ident, tabUrl);
                    postCommand_App("navigate.reload");
                }
                else {
                    signOut_GmCerts(certs_App(), tabUrl);
                    postCommand_App("navigate.reload");
                }
                saveIdentities_GmCerts(certs_App());
                updateItems_CertListWidget(d);
            }
            return iTrue;
        }
        else if (isCommand_Widget(w, ev, "ident.edit")) {
            const iGmIdentity *ident = menuIdentity_CertListWidget_(d);
            if (ident) {
                makeValueInput_Widget(get_Root()->widget,
                                      &ident->notes,
                                      uiHeading_ColorEscape "${heading.ident.notes}",
                                      format_CStr(cstr_Lang("dlg.ident.notes"),
                                                  cstr_String(name_GmIdentity(ident))),
                                      uiTextAction_ColorEscape "${dlg.default}",
                                      format_CStr("!ident.setnotes ident:%p ptr:%p", ident, d));
            }
            return iTrue;
        }
        else if (isCommand_Widget(w, ev, "ident.fingerprint")) {
            const iGmIdentity *ident = menuIdentity_CertListWidget_(d);
            if (ident) {
                const iString *fps = collect_String(
                    hexEncode_Block(collect_Block(fingerprint_TlsCertificate(ident->cert))));
                SDL_SetClipboardText(cstr_String(fps));
            }
            return iTrue;
        }
        else if (isCommand_Widget(w, ev, "ident.export")) {
            const iGmIdentity *ident = menuIdentity_CertListWidget_(d);
            if (ident) {
                iString *pem = collect_String(pem_TlsCertificate(ident->cert));
                append_String(pem, collect_String(privateKeyPem_TlsCertificate(ident->cert)));
                iDocumentWidget *expTab = newTab_App(NULL, switchTo_NewTabFlag);
                setUrlAndSource_DocumentWidget(
                    expTab,
                    collectNewFormat_String("file:%s.pem", cstr_String(name_GmIdentity(ident))),
                    collectNewCStr_String("text/plain"),
                    utf8_String(pem),
                    0);
            }
            return iTrue;
        }
        else if (isCommand_Widget(w, ev, "ident.setnotes")) {
            iGmIdentity *ident = pointerLabel_Command(cmd, "ident");
            if (ident) {
                setCStr_String(&ident->notes, suffixPtr_Command(cmd, "value"));
                updateItems_CertListWidget(d);
            }
            return iTrue;
        }
        else if (isCommand_Widget(w, ev, "ident.pickicon")) {
            return iTrue;
        }
        else if (isCommand_Widget(w, ev, "ident.reveal")) {
            const iGmIdentity *ident = menuIdentity_CertListWidget_(d);
            if (ident) {
                const iString *crtPath = certificatePath_GmCerts(certs_App(), ident);
                if (crtPath) {
                    postCommandf_App("reveal path:%s", cstr_String(crtPath));
                }
            }
            return iTrue;
        }
        else if (isCommand_Widget(w, ev, "ident.delete")) {
            iCertItem *item = d->contextItem;
            if (argLabel_Command(cmd, "confirm")) {
                makeQuestion_Widget(
                    uiTextCaution_ColorEscape "${heading.ident.delete}",
                    format_CStr(cstr_Lang("dlg.confirm.ident.delete"),
                                uiTextAction_ColorEscape,
                                cstr_String(&item->label),
                                uiText_ColorEscape),
                    (iMenuItem[]){ { "${cancel}", 0, 0, NULL },
                                   { uiTextAction_ColorEscape "${dlg.ident.delete}",
                                     0,
                                     0,
                                     format_CStr("!ident.delete confirm:0 ptr:%p", d) } },
                    2);
                return iTrue;
            }
            deleteIdentity_GmCerts(certs_App(), menuIdentity_CertListWidget_(d));
            postCommand_App("idents.changed");
            return iTrue;
        }
    }
    if (ev->type == SDL_MOUSEMOTION && !isVisible_Widget(d->menu)) {
        const iInt2 mouse = init_I2(ev->motion.x, ev->motion.y);
        /* Update cursor. */
        if (contains_Widget(w, mouse)) {
            setCursor_Window(get_Window(), SDL_SYSTEM_CURSOR_ARROW);
        }
        else if (d->contextIndex != iInvalidPos) {
            invalidateItem_ListWidget(&d->list, d->contextIndex);
            d->contextIndex = iInvalidPos;
        }
    }
    /* Update context menu items. */
    if (ev->type == SDL_MOUSEBUTTONDOWN && ev->button.button == SDL_BUTTON_RIGHT) {
        d->contextItem = NULL;
        if (!isVisible_Widget(d->menu)) {
            updateMouseHover_ListWidget(&d->list);
        }
        if (constHoverItem_ListWidget(&d->list) || isVisible_Widget(d->menu)) {
            d->contextItem = hoverItem_ListWidget(&d->list);
            /* Context is drawn in hover state. */
            if (d->contextIndex != iInvalidPos) {
                invalidateItem_ListWidget(&d->list, d->contextIndex);
            }
            d->contextIndex = hoverItemIndex_ListWidget(&d->list);
            updateContextMenu_CertListWidget_(d);                
            /* TODO: Some callback-based mechanism would be nice for updating menus right
               before they open? At least move these to `updateContextMenu_ */
            const iGmIdentity *ident  = constHoverIdentity_CertListWidget(d);
            const iString *    docUrl = url_DocumentWidget(document_App());
            iForEach(ObjectList, i, children_Widget(d->menu)) {
                if (isInstance_Object(i.object, &Class_LabelWidget)) {
                    iLabelWidget *menuItem = i.object;
                    const char *  cmdItem  = cstr_String(command_LabelWidget(menuItem));
                    if (equal_Command(cmdItem, "ident.use")) {
                        const iBool cmdUse   = arg_Command(cmdItem) != 0;
                        const iBool cmdClear = argLabel_Command(cmdItem, "clear") != 0;
                        setFlags_Widget(
                            as_Widget(menuItem),
                            disabled_WidgetFlag,
                            (cmdClear && !isUsed_GmIdentity(ident)) ||
                                (!cmdClear && cmdUse && isUsedOn_GmIdentity(ident, docUrl)) ||
                                (!cmdClear && !cmdUse && !isUsedOn_GmIdentity(ident, docUrl)));
                    }
                }
            }
        }
        if (hoverItem_ListWidget(&d->list) || isVisible_Widget(d->menu)) {
            processContextMenuEvent_Widget(d->menu, ev, {});
        }
    }
    return ((iWidgetClass *) class_Widget(w)->super)->processEvent(w, ev);
}

static void draw_CertListWidget_(const iCertListWidget *d) {
    const iWidget *w = constAs_Widget(d);
    ((iWidgetClass *) class_Widget(w)->super)->draw(w);
}

static void draw_CertItem_(const iCertItem *d, iPaint *p, iRect itemRect,
                              const iListWidget *list) {
    const iCertListWidget *certList = (const iCertListWidget *) list;
    const iBool isMenuVisible = isVisible_Widget(certList->menu);
    const iBool isDragging   = constDragItem_ListWidget(list) == d;
    const iBool isPressing   = isMouseDown_ListWidget(list) && !isDragging;
    const iBool isHover      =
            (!isMenuVisible &&
            isHover_Widget(constAs_Widget(list)) &&
            constHoverItem_ListWidget(list) == d) ||
            (isMenuVisible && certList->contextItem == d) ||
            (isFocused_Widget(list) && constCursorItem_ListWidget(list) == d) ||
            isDragging;
    const int itemHeight     = height_Rect(itemRect);
    const int iconColor      = isHover ? (isPressing ? uiTextPressed_ColorId : uiIconHover_ColorId)
                                       : uiIcon_ColorId;
    const int altIconColor   = isPressing ? uiTextPressed_ColorId : uiTextCaution_ColorId;
    const int font = certList->itemFonts[d->isBold ? 1 : 0];
    int bg         = uiBackgroundSidebar_ColorId;
    if (isHover) {
        bg = isPressing ? uiBackgroundPressed_ColorId
                        : uiBackgroundFramelessHover_ColorId;
        fillRect_Paint(p, itemRect, bg);
    }
    else if (d->listItem.isSelected) {
        bg = uiBackgroundUnfocusedSelection_ColorId;
        fillRect_Paint(p, itemRect, bg);
    }
//    iInt2 pos = itemRect.pos; 
    const int fg = isHover ? (isPressing ? uiTextPressed_ColorId : uiTextFramelessHover_ColorId)
                           : uiTextStrong_ColorId;
    const iBool isUsedOnDomain = (d->indent != 0);
    iString icon;
    initUnicodeN_String(&icon, &d->icon, 1);
    iInt2 cPos = topLeft_Rect(itemRect);
    int indent = 1.4f * lineHeight_Text(font) + (isTerminal_Platform() ? 2 * gap_UI : 0);
    addv_I2(&cPos,
            init_I2(3 * gap_UI * aspect_UI,
                    (itemHeight - lineHeight_Text(uiLabel_FontId) * 2 - lineHeight_Text(font)) /
                        2));
    const int metaFg = isHover ? permanent_ColorId | (isPressing ? uiTextPressed_ColorId
                                                                 : uiTextFramelessHover_ColorId)
                               : uiTextDim_ColorId;
    if (!d->listItem.isSelected && !isUsedOnDomain) {
        drawOutline_Text(font, cPos, metaFg, none_ColorId, range_String(&icon));
    }
    drawRange_Text(font,
                   cPos,
                   d->listItem.isSelected ? iconColor
                   : isUsedOnDomain       ? altIconColor
                                          : uiBackgroundSidebar_ColorId,
                   range_String(&icon));
    deinit_String(&icon);
    drawRange_Text(d->listItem.isSelected ? certList->itemFonts[1] : font,
                   add_I2(cPos, init_I2(indent, 0)),
                   fg,
                   range_String(&d->label));
    drawRange_Text(uiLabel_FontId,
                   add_I2(cPos, init_I2(indent, lineHeight_Text(font))),
                   metaFg,
                   range_String(&d->meta));
}

void init_CertListWidget(iCertListWidget *d) {
    iWidget *w = as_Widget(d);
    init_ListWidget(&d->list);
    setId_Widget(w, "certlist");
    setFlags_Widget(w, focusable_WidgetFlag, iTrue);
    setBackgroundColor_Widget(w, none_ColorId);
    d->itemFonts[0] = uiContent_FontId;
    d->itemFonts[1] = uiContentBold_FontId;
    if (deviceType_App() == phone_AppDeviceType) {
        d->itemFonts[0] = uiLabelBig_FontId;
        d->itemFonts[1] = uiLabelBigBold_FontId;
    }
    updateItemHeight_CertListWidget(d);
    d->menu = NULL;
    d->contextItem = NULL;
    d->contextIndex = iInvalidPos;
}

void updateItemHeight_CertListWidget(iCertListWidget *d) {
    const float height = isTerminal_Platform() ? 4.0f : 3.5f;
    setItemHeight_ListWidget(&d->list, height * lineHeight_Text(d->itemFonts[0]));
}

iBool updateItems_CertListWidget(iCertListWidget *d) {
    clear_ListWidget(&d->list);
    destroy_Widget(d->menu);
    d->menu       = NULL;
    const iString *tabUrl = url_DocumentWidget(document_App());
    const iRangecc tabHost = urlHost_String(tabUrl);
    iBool haveItems = iFalse;
    iConstForEach(PtrArray, i, identities_GmCerts(certs_App())) {
        const iGmIdentity *ident = i.ptr;
        iCertItem *item = new_CertItem();
        item->id = (uint32_t) index_PtrArrayConstIterator(&i);
        item->icon = 0x1f464; /* person */
        set_String(&item->label, name_GmIdentity(ident));
        iDate until;
        validUntil_TlsCertificate(ident->cert, &until);
        const iBool isActive = isUsedOn_GmIdentity(ident, tabUrl);
        format_String(&item->meta,
                      "%s",
                      isActive ? cstr_Lang("ident.using")
                      : isUsed_GmIdentity(ident)
                          ? formatCStrs_Lang("ident.usedonurls.n", size_StringSet(ident->useUrls))
                          : cstr_Lang("ident.notused"));
        const char *expiry =
            ident->flags & temporary_GmIdentityFlag
                ? cstr_Lang("ident.temporary")
                : cstrCollect_String(format_Date(&until, cstr_Lang("ident.expiry")));
        if (isEmpty_String(&ident->notes)) {
            appendFormat_String(&item->meta, "\n%s", expiry);
        }
        else {
            appendFormat_String(&item->meta,
                                " \u2014 %s\n%s%s",
                                expiry,
                                escape_Color(uiHeading_ColorId),
                                cstr_String(&ident->notes));
        }
        item->listItem.isSelected = isActive;
        if (!isActive && isUsedOnDomain_GmIdentity(ident, tabHost)) {
            item->indent = 1; /* will be highlighted */
        }
        addItem_ListWidget(&d->list, item);
        haveItems = iTrue;
        iRelease(item);
    }
    return haveItems;
}

void deinit_CertListWidget(iCertListWidget *d) {
    iUnused(d);
}

const iGmIdentity *constHoverIdentity_CertListWidget(const iCertListWidget *d) {
    const iCertItem *hoverItem = constHoverItem_ListWidget(&d->list);
    if (hoverItem) {
        return identity_GmCerts(certs_App(), hoverItem->id);
    }
    return NULL;
}

iGmIdentity *hoverIdentity_CertListWidget(const iCertListWidget *d) {
    return iConstCast(iGmIdentity *, constHoverIdentity_CertListWidget(d));
}

iBeginDefineSubclass(CertListWidget, ListWidget)
    .processEvent = (iAny *) processEvent_CertListWidget_,
    .draw         = (iAny *) draw_CertListWidget_,
iEndDefineSubclass(CertListWidget)
