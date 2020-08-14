#include "sidebarwidget.h"
#include "labelwidget.h"
#include "scrollwidget.h"
#include "documentwidget.h"
#include "inputwidget.h"
#include "bookmarks.h"
#include "paint.h"
#include "util.h"
#include "command.h"
#include "../gmdocument.h"
#include "app.h"

#include <the_Foundation/array.h>
#include <SDL_clipboard.h>
#include <SDL_mouse.h>

iDeclareType(SidebarItem)

struct Impl_SidebarItem {
    uint32_t    id;
    int         indent;
    iChar       icon;
    iString     label;
    iString     meta;
    iString     url;
};

void init_SidebarItem(iSidebarItem *d) {
    d->id     = 0;
    d->indent = 0;
    d->icon   = 0;
    init_String(&d->label);
    init_String(&d->meta);
    init_String(&d->url);
}

void deinit_SidebarItem(iSidebarItem *d) {
    deinit_String(&d->url);
    deinit_String(&d->meta);
    deinit_String(&d->label);
}

iDefineTypeConstruction(SidebarItem)

/*----------------------------------------------------------------------------------------------*/

struct Impl_SidebarWidget {
    iWidget widget;
    enum iSidebarMode mode;
    iScrollWidget *scroll;
    int scrollY;
    int width;
    iLabelWidget *modeButtons[max_SidebarMode];
    int itemHeight;
    int maxButtonLabelWidth;
    iArray items;
    size_t hoverItem;
    iClick click;
    iWidget *resizer;
    SDL_Cursor *resizeCursor;
    iWidget *menu;
};

iDefineObjectConstruction(SidebarWidget)

static iBool isResizing_SidebarWidget_(const iSidebarWidget *d) {
    return (flags_Widget(d->resizer) & pressed_WidgetFlag) != 0;
}

static void clearItems_SidebarWidget_(iSidebarWidget *d) {
    iForEach(Array, i, &d->items) {
        deinit_SidebarItem(i.value);
    }
    clear_Array(&d->items);
}

static iRect contentBounds_SidebarWidget_(const iSidebarWidget *d) {
    iRect bounds = bounds_Widget(constAs_Widget(d));
    adjustEdges_Rect(&bounds, as_Widget(d->modeButtons[0])->rect.size.y + gap_UI,
                     -constAs_Widget(d->scroll)->rect.size.x, -gap_UI, 0);
    return bounds;
}

static int scrollMax_SidebarWidget_(const iSidebarWidget *d) {
    return iMax(0,
                (int) size_Array(&d->items) * d->itemHeight -
                    height_Rect(contentBounds_SidebarWidget_(d)));
}

static void updateVisible_SidebarWidget_(iSidebarWidget *d) {
    const int contentSize = size_Array(&d->items) * d->itemHeight;
    const iRect bounds = contentBounds_SidebarWidget_(d);
    setRange_ScrollWidget(d->scroll, (iRangei){ 0, scrollMax_SidebarWidget_(d) });
    setThumb_ScrollWidget(d->scroll,
                          d->scrollY,
                          contentSize > 0 ? height_Rect(bounds_Widget(as_Widget(d->scroll))) *
                                                height_Rect(bounds) / contentSize
                                          : 0);
}

static void updateItems_SidebarWidget_(iSidebarWidget *d) {
    clearItems_SidebarWidget_(d);
    destroy_Widget(d->menu);
    d->menu = NULL;
    d->hoverItem = iInvalidPos;
    switch (d->mode) {
        case documentOutline_SidebarMode: {
            const iGmDocument *doc = document_DocumentWidget(document_App());
            iConstForEach(Array, i, headings_GmDocument(doc)) {
                const iGmHeading *head = i.value;
                iSidebarItem item;
                init_SidebarItem(&item);
                item.id = index_ArrayConstIterator(&i);
                setRange_String(&item.label, head->text);
                item.indent = head->level * 4 * gap_UI;
                pushBack_Array(&d->items, &item);
            }
            break;
        }
        case bookmarks_SidebarMode: {
            iConstForEach(PtrArray, i, list_Bookmarks(bookmarks_App(), NULL, NULL)) {
                const iBookmark *bm = i.ptr;
                iSidebarItem item;
                init_SidebarItem(&item);
                item.id = id_Bookmark(bm);
                item.icon = bm->icon;
                set_String(&item.url, &bm->url);
                set_String(&item.label, &bm->title);
                iDate date;
                init_Date(&date, &bm->when);
                iString *ds = format_Date(&date, "%Y %b %d");
                set_String(&item.meta, ds);
                delete_String(ds);
                pushBack_Array(&d->items, &item);
            }
            d->menu = makeMenu_Widget(
                as_Widget(d),
                (iMenuItem[]){ { "Edit Bookmark...", 0, 0, "bookmark.edit" },
                               { "Copy URL", 0, 0, "bookmark.copy" },
                               { "---", 0, 0, NULL },
                               { orange_ColorEscape "Delete Bookmark", 0, 0, "bookmark.delete" } },
                4);
            break;
        }
        case history_SidebarMode:
            break;
        default:
            break;
    }
    updateVisible_SidebarWidget_(d);
    refresh_Widget(as_Widget(d));
}

void setMode_SidebarWidget(iSidebarWidget *d, enum iSidebarMode mode) {
    if (d->mode == mode) return;
    d->mode = mode;
    for (enum iSidebarMode i = 0; i < max_SidebarMode; i++) {
        setFlags_Widget(as_Widget(d->modeButtons[i]), selected_WidgetFlag, i == d->mode);
    }
    const float heights[max_SidebarMode] = { 1.2f, 2.4f, 3, 3 };
    d->itemHeight = heights[mode] * lineHeight_Text(default_FontId);
}

enum iSidebarMode mode_SidebarWidget(const iSidebarWidget *d) {
    return d->mode;
}

int width_SidebarWidget(const iSidebarWidget *d) {
    return d->width;
}

static const char *normalModeLabels_[max_SidebarMode] = {
    "\U0001f5b9 Outline",
    "\U0001f588 Bookmarks",
    "\U0001f553 History",
    "\U0001f464 Identities",
};

static const char *tightModeLabels_[max_SidebarMode] = {
    "\U0001f5b9",
    "\U0001f588",
    "\U0001f553",
    "\U0001f464",
};

void init_SidebarWidget(iSidebarWidget *d) {
    iWidget *w = as_Widget(d);
    init_Widget(w);
    setId_Widget(w, "sidebar");
    setBackgroundColor_Widget(w, none_ColorId);
    setFlags_Widget(w,
                    hidden_WidgetFlag | hover_WidgetFlag | arrangeHorizontal_WidgetFlag |
                        resizeWidthOfChildren_WidgetFlag | collapse_WidgetFlag,
                    iTrue);
    d->scrollY = 0;
    d->mode    = -1;
    d->width   = 75 * gap_UI;
    init_Array(&d->items, sizeof(iSidebarItem));
    d->hoverItem = iInvalidPos;
    init_Click(&d->click, d, SDL_BUTTON_LEFT);
    setFlags_Widget(w, fixedWidth_WidgetFlag, iTrue);
    d->maxButtonLabelWidth = 0;
    for (int i = 0; i < max_SidebarMode; i++) {
        d->modeButtons[i] = addChildFlags_Widget(
            w,
            iClob(
                new_LabelWidget(normalModeLabels_[i], 0, 0, format_CStr("sidebar.mode arg:%d", i))),
            frameless_WidgetFlag | expand_WidgetFlag);
        d->maxButtonLabelWidth =
            iMaxi(d->maxButtonLabelWidth,
                  3 * gap_UI + measure_Text(default_FontId, normalModeLabels_[i]).x);
    }
    addChild_Widget(w, iClob(d->scroll = new_ScrollWidget()));
    setThumb_ScrollWidget(d->scroll, 0, 0);
    setMode_SidebarWidget(d, documentOutline_SidebarMode);
    d->resizer = addChildFlags_Widget(
        w,
        iClob(new_Widget()),
        hover_WidgetFlag | commandOnClick_WidgetFlag | fixedWidth_WidgetFlag |
            resizeToParentHeight_WidgetFlag | moveToParentRightEdge_WidgetFlag);
    setId_Widget(d->resizer, "sidebar.grab");
    d->resizer->rect.size.x = gap_UI;
    setBackgroundColor_Widget(d->resizer, none_ColorId);
    d->resizeCursor = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_SIZEWE);
    d->menu = NULL;
}

void deinit_SidebarWidget(iSidebarWidget *d) {
    SDL_FreeCursor(d->resizeCursor);
    clearItems_SidebarWidget_(d);
    deinit_Array(&d->items);
}

static int visCount_SidebarWidget_(const iSidebarWidget *d) {
    return iMin(height_Rect(bounds_Widget(constAs_Widget(d))) / d->itemHeight,
                (int) size_Array(&d->items));
}

static iRanges visRange_SidebarWidget_(const iSidebarWidget *d) {
    iRanges vis = { d->scrollY / d->itemHeight, 0 };
    vis.end = iMin(size_Array(&d->items), vis.start + visCount_SidebarWidget_(d));
    return vis;
}

static size_t itemIndex_SidebarWidget_(const iSidebarWidget *d, iInt2 pos) {
    const iRect bounds = contentBounds_SidebarWidget_(d);
    pos.y -= top_Rect(bounds) - d->scrollY;
    if (pos.y < 0) return iInvalidPos;
    size_t index = pos.y / d->itemHeight;
    if (index >= size_Array(&d->items)) return iInvalidPos;
    return index;
}

static void itemClicked_SidebarWidget_(iSidebarWidget *d, size_t index) {
    const iSidebarItem *item = constAt_Array(&d->items, index);
    switch (d->mode) {
        case documentOutline_SidebarMode: {
            const iGmDocument *doc = document_DocumentWidget(document_App());
            const iGmHeading *head = constAt_Array(headings_GmDocument(doc), item->id);
            postCommandf_App("document.goto loc:%p", head->text.start);
            break;
        }
        case bookmarks_SidebarMode: {
            postCommandf_App("open url:%s", cstr_String(&item->url));
            break;
        }
    }
}

static void scroll_SidebarWidget_(iSidebarWidget *d, int offset) {
    d->scrollY += offset;
    if (d->scrollY < 0) {
        d->scrollY = 0;
    }
    const int scrollMax = scrollMax_SidebarWidget_(d);
    d->scrollY = iMin(d->scrollY, scrollMax);
    updateVisible_SidebarWidget_(d);
    refresh_Widget(as_Widget(d));
}

static void checkModeButtonLayout_SidebarWidget_(iSidebarWidget *d) {
    const iBool isTight =
        (width_Rect(bounds_Widget(as_Widget(d->modeButtons[0]))) < d->maxButtonLabelWidth);
    for (int i = 0; i < max_SidebarMode; i++) {
        if (isTight && ~flags_Widget(as_Widget(d->modeButtons[i])) & tight_WidgetFlag) {
            setFlags_Widget(as_Widget(d->modeButtons[i]), tight_WidgetFlag, iTrue);
            updateTextCStr_LabelWidget(d->modeButtons[i], tightModeLabels_[i]);
        }
        else if (!isTight && flags_Widget(as_Widget(d->modeButtons[i])) & tight_WidgetFlag) {
            setFlags_Widget(as_Widget(d->modeButtons[i]), tight_WidgetFlag, iFalse);
            updateTextCStr_LabelWidget(d->modeButtons[i], normalModeLabels_[i]);
        }
    }
}

static iSidebarItem *hoverItem_SidebarWidget_(iSidebarWidget *d) {
    if (d->hoverItem < size_Array(&d->items)) {
        return at_Array(&d->items, d->hoverItem);
    }
    return NULL;
}

void setWidth_SidebarWidget(iSidebarWidget *d, int width) {
    iWidget *w = as_Widget(d);
    width = iMax(30 * gap_UI, width);
    d->width = width;
    if (isVisible_Widget(w)) {
        w->rect.size.x = width;
    }
    arrange_Widget(findWidget_App("doctabs"));
    checkModeButtonLayout_SidebarWidget_(d);
    if (!isRefreshPending_App()) {
        updateSize_DocumentWidget(document_App());
        refresh_Widget(w);
    }
}

iBool handleBookmarkEditorCommands_SidebarWidget_(iWidget *editor, const char *cmd) {
    iSidebarWidget *d = findWidget_App("sidebar");
    if (equal_Command(cmd, "bmed.accept") || equal_Command(cmd, "cancel")) {
        setFlags_Widget(as_Widget(d), disabled_WidgetFlag, iFalse);
        destroy_Widget(editor);
        return iTrue;
    }
    return iFalse;
}

static iBool processEvent_SidebarWidget_(iSidebarWidget *d, const SDL_Event *ev) {
    iWidget *w = as_Widget(d);
    /* Handle commands. */
    if (isResize_UserEvent(ev)) {
        updateVisible_SidebarWidget_(d);
        checkModeButtonLayout_SidebarWidget_(d);
    }
    else if (ev->type == SDL_USEREVENT && ev->user.code == command_UserEventCode) {
        const char *cmd = command_UserEvent(ev);
        if (isCommand_Widget(w, ev, "mouse.clicked")) {
            if (argLabel_Command(cmd, "button") == SDL_BUTTON_LEFT) {
                if (arg_Command(cmd)) {
                    setFlags_Widget(d->resizer, pressed_WidgetFlag, iTrue);
                    setBackgroundColor_Widget(d->resizer, gray75_ColorId);
                    setMouseGrab_Widget(d->resizer);
                    refresh_Widget(d->resizer);
                }
                else {
                    setFlags_Widget(d->resizer, pressed_WidgetFlag, iFalse);
                    setBackgroundColor_Widget(d->resizer, none_ColorId);
                    setMouseGrab_Widget(NULL);
                    refresh_Widget(d->resizer);
                }
            }
            return iTrue;
        }
        else if (isCommand_Widget(w, ev, "mouse.moved")) {
            if (isResizing_SidebarWidget_(d)) {
                const iInt2 local = localCoord_Widget(w, coord_Command(cmd));
                setWidth_SidebarWidget(d, local.x + d->resizer->rect.size.x / 2);
            }
            return iTrue;
        }
        else if (equal_Command(cmd, "sidebar.width")) {
            setWidth_SidebarWidget(d, arg_Command(cmd));
            return iTrue;
        }
        else if (equal_Command(cmd, "sidebar.mode")) {
            setMode_SidebarWidget(d, arg_Command(cmd));
            updateItems_SidebarWidget_(d);
            if (argLabel_Command(cmd, "show") && !isVisible_Widget(w)) {
                postCommand_App("sidebar.toggle");
            }
            return iTrue;
        }
        else if (equal_Command(cmd, "sidebar.toggle")) {
            setFlags_Widget(w, hidden_WidgetFlag, isVisible_Widget(w));
            if (isVisible_Widget(w)) {
                w->rect.size.x = d->width;
            }
            arrange_Widget(w->parent);
            updateSize_DocumentWidget(document_App());
            refresh_Widget(w->parent);
            return iTrue;
        }
        else if (equal_Command(cmd, "scroll.moved")) {
            d->scrollY = arg_Command(command_UserEvent(ev));
            d->hoverItem = iInvalidPos;
            refresh_Widget(w);
            return iTrue;
        }
        else if (equal_Command(cmd, "tabs.changed") || equal_Command(cmd, "document.changed")) {
            d->scrollY = 0;
            updateItems_SidebarWidget_(d);
        }
        else if (equal_Command(cmd, "bookmark.copy")) {
            const iSidebarItem *item = hoverItem_SidebarWidget_(d);
            if (d->mode == bookmarks_SidebarMode && item) {
                SDL_SetClipboardText(cstr_String(&item->url));
            }
            return iTrue;
        }
        else if (equal_Command(cmd, "bookmark.edit")) {
            const iSidebarItem *item = hoverItem_SidebarWidget_(d);
            if (d->mode == bookmarks_SidebarMode && item) {
                setFlags_Widget(w, disabled_WidgetFlag, iTrue);
                iWidget *dlg = makeBookmarkEditor_Widget();
                setText_InputWidget(findChild_Widget(dlg, "bmed.title"), &item->label);
                setText_InputWidget(findChild_Widget(dlg, "bmed.url"), &item->url);
                //setText_InputWidget(findChild_Widget(dlg, "bmed.tags"), &item->)
                setCommandHandler_Widget(dlg, handleBookmarkEditorCommands_SidebarWidget_);
            }
            return iTrue;
        }
        else if (equal_Command(cmd, "bookmark.delete")) {
            const iSidebarItem *item = hoverItem_SidebarWidget_(d);
            if (d->mode == bookmarks_SidebarMode && item && remove_Bookmarks(bookmarks_App(), item->id)) {
                postCommand_App("bookmarks.changed");
            }
            return iTrue;
        }
        else if (equal_Command(cmd, "bookmarks.changed")) {
            updateItems_SidebarWidget_(d);
        }
    }
    if (ev->type == SDL_MOUSEMOTION && !isVisible_Widget(d->menu)) {
        const iInt2 mouse = init_I2(ev->motion.x, ev->motion.y);
        size_t hover = iInvalidPos;
        if (contains_Widget(d->resizer, mouse)) {
            SDL_SetCursor(d->resizeCursor);
        }
        else {
            SDL_SetCursor(NULL);
            if (contains_Widget(w, mouse)) {
                hover = itemIndex_SidebarWidget_(d, mouse);
            }
        }
        if (hover != d->hoverItem) {
            d->hoverItem = hover;
            refresh_Widget(w);
        }
    }
    if (ev->type == SDL_MOUSEWHEEL && isHover_Widget(w)) {
#if defined (iPlatformApple)
        /* Momentum scrolling. */
        scroll_SidebarWidget_(d, -ev->wheel.y * get_Window()->pixelRatio);
#else
        scroll_SidebarWidget_(d, -ev->wheel.y * 3 * d->itemHeight);
#endif
        d->hoverItem = iInvalidPos;
        refresh_Widget(w);
        return iTrue;
    }
    if (d->menu && ev->type == SDL_MOUSEBUTTONDOWN) {
        if (d->hoverItem != iInvalidPos || isVisible_Widget(d->menu)) {
            processContextMenuEvent_Widget(d->menu, ev, {});
        }
    }
    switch (processEvent_Click(&d->click, ev)) {
        case started_ClickResult:
            refresh_Widget(w);
            break;
        case finished_ClickResult:
            if (contains_Rect(contentBounds_SidebarWidget_(d), pos_Click(&d->click)) &&
                d->hoverItem != iInvalidSize) {
                itemClicked_SidebarWidget_(d, d->hoverItem);
            }
            refresh_Widget(w);
            break;
        default:
            break;
    }
    return processEvent_Widget(w, ev);
}

static void draw_SidebarWidget_(const iSidebarWidget *d) {
    const iWidget *w      = constAs_Widget(d);
    const iRect    bounds = contentBounds_SidebarWidget_(d);
    const iBool    isPressing = d->click.isActive && contains_Rect(bounds, pos_Click(&d->click));
    iPaint p;
    init_Paint(&p);
    /* Draw the items. */ {
        const int font = default_FontId;
        const iRanges visRange = visRange_SidebarWidget_(d);
        iInt2 pos = addY_I2(topLeft_Rect(bounds), -(d->scrollY % d->itemHeight));
        for (size_t i = visRange.start; i < visRange.end; i++) {
            const iSidebarItem *item        = constAt_Array(&d->items, i);
            const iRect         itemRect    = { pos, init_I2(width_Rect(bounds), d->itemHeight) };
            const iBool         isHover     = (d->hoverItem == i);
            setClip_Paint(&p, intersect_Rect(itemRect, bounds));
            if (isHover) {
                fillRect_Paint(&p, itemRect, isPressing ? orange_ColorId : teal_ColorId);
            }
            if (d->mode == documentOutline_SidebarMode) {
                const int fg = isHover ? (isPressing ? black_ColorId : white_ColorId) :
                                       (item->indent == 0 ? white_ColorId : gray75_ColorId);
                drawRange_Text(font, init_I2(pos.x + 3 * gap_UI + item->indent,
                                        mid_Rect(itemRect).y - lineHeight_Text(font) / 2),
                               fg, range_String(&item->label));
            }
            else if (d->mode == bookmarks_SidebarMode) {
                const int fg =
                    isHover ? (isPressing ? black_ColorId : white_ColorId) : gray75_ColorId;
                iString str;
                init_String(&str);
                appendChar_String(&str, item->icon ? item->icon : 0x1f588);
                const iRect iconArea = { pos, init_I2(10 * gap_UI, d->itemHeight) };
                drawCentered_Text(uiBookmarkIcon_FontId,
                                  iconArea,
                                  iTrue,
                                  isHover && isPressing ? black_ColorId : cyan_ColorId,
                                  "%s",
                                  cstr_String(&str));
                deinit_String(&str);
                iInt2 textPos = addY_I2(topRight_Rect(iconArea), 0.2f * lineHeight_Text(font));
                drawRange_Text(font, textPos, fg, range_String(&item->label));
                drawRange_Text(font,
                               addY_I2(textPos, lineHeight_Text(font)),
                               isHover ? (isPressing ? black_ColorId : cyan_ColorId) : teal_ColorId,
                               range_String(&item->meta));
            }
            unsetClip_Paint(&p);
            pos.y += d->itemHeight;
        }
    }
    draw_Widget(w);
    drawVLine_Paint(&p,
                    addX_I2(topRight_Rect(bounds_Widget(w)), -1),
                    height_Rect(bounds_Widget(w)),
                    black_ColorId);
}

iBeginDefineSubclass(SidebarWidget, Widget)
    .processEvent = (iAny *) processEvent_SidebarWidget_,
    .draw         = (iAny *) draw_SidebarWidget_,
iEndDefineSubclass(SidebarWidget)
