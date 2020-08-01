#include "util.h"

#include "app.h"
#include "color.h"
#include "command.h"
#include "labelwidget.h"
#include "inputwidget.h"
#include "widget.h"
#include "text.h"
#include "window.h"

#include <the_Foundation/math.h>
#include <the_Foundation/path.h>

iBool isCommand_UserEvent(const SDL_Event *d, const char *cmd) {
    return d->type == SDL_USEREVENT && d->user.code == command_UserEventCode &&
           equal_Command(d->user.data1, cmd);
}

const char *command_UserEvent(const SDL_Event *d) {
    if (d->type == SDL_USEREVENT && d->user.code == command_UserEventCode) {
        return d->user.data1;
    }
    return "";
}

int keyMods_Sym(int kmods) {
    kmods &= (KMOD_SHIFT | KMOD_ALT | KMOD_CTRL | KMOD_GUI);
    /* Don't treat left/right modifiers differently. */
    if (kmods & KMOD_SHIFT) kmods |= KMOD_SHIFT;
    if (kmods & KMOD_ALT)   kmods |= KMOD_ALT;
    if (kmods & KMOD_CTRL)  kmods |= KMOD_CTRL;
    if (kmods & KMOD_GUI)   kmods |= KMOD_GUI;
    return kmods;
}

/*-----------------------------------------------------------------------------------------------*/

void init_Click(iClick *d, iAnyObject *widget, int button) {
    d->isActive = iFalse;
    d->button   = button;
    d->bounds   = as_Widget(widget);
    d->startPos = zero_I2();
    d->pos      = zero_I2();
}

enum iClickResult processEvent_Click(iClick *d, const SDL_Event *event) {
    if (event->type == SDL_MOUSEMOTION) {
        const iInt2 pos = init_I2(event->motion.x, event->motion.y);
        if (d->isActive) {
            d->pos = pos;
            return drag_ClickResult;
        }
    }
    if (event->type != SDL_MOUSEBUTTONDOWN && event->type != SDL_MOUSEBUTTONUP) {
        return none_ClickResult;
    }
    const SDL_MouseButtonEvent *mb = &event->button;
    if (mb->button != d->button) {
        return none_ClickResult;
    }
    const iInt2 pos = init_I2(mb->x, mb->y);
    if (event->type == SDL_MOUSEBUTTONDOWN && mb->clicks == 2) {
        if (contains_Widget(d->bounds, pos)) {
            d->pos = pos;
            setMouseGrab_Widget(NULL);
            return double_ClickResult;
        }
    }
    if (!d->isActive) {
        if (mb->state == SDL_PRESSED) {
            if (contains_Widget(d->bounds, pos)) {
                d->isActive = iTrue;
                d->startPos = d->pos = pos;
                //setFlags_Widget(d->bounds, hover_WidgetFlag, iFalse);
                setMouseGrab_Widget(d->bounds);
                return started_ClickResult;
            }
        }
    }
    else { /* Active. */
        if (mb->state == SDL_RELEASED) {
            enum iClickResult result = contains_Widget(d->bounds, pos)
                                           ? finished_ClickResult
                                           : aborted_ClickResult;
            d->isActive = iFalse;
            d->pos = pos;
            setMouseGrab_Widget(NULL);
            return result;
        }
    }
    return none_ClickResult;
}

void cancel_Click(iClick *d) {
    if (d->isActive) {
        d->isActive = iFalse;
        setMouseGrab_Widget(NULL);
    }
}

iBool isMoved_Click(const iClick *d) {
    return dist_I2(d->startPos, d->pos) > 2;
}

iInt2 pos_Click(const iClick *d) {
    return d->pos;
}

iRect rect_Click(const iClick *d) {
    return initCorners_Rect(min_I2(d->startPos, d->pos), max_I2(d->startPos, d->pos));
}

iInt2 delta_Click(const iClick *d) {
    return sub_I2(d->pos, d->startPos);
}

/*-----------------------------------------------------------------------------------------------*/

iWidget *makePadding_Widget(int size) {
    iWidget *pad = new_Widget();
    setSize_Widget(pad, init1_I2(size));
    return pad;
}

iLabelWidget *makeHeading_Widget(const char *text) {
    iLabelWidget *heading = new_LabelWidget(text, 0, 0, NULL);
    setFlags_Widget(as_Widget(heading), frameless_WidgetFlag | fixedSize_WidgetFlag, iTrue);
    return heading;
}

iWidget *makeVDiv_Widget(void) {
    iWidget *div = new_Widget();
    setFlags_Widget(div, resizeChildren_WidgetFlag | arrangeVertical_WidgetFlag, iTrue);
    return div;
}

iWidget *makeHDiv_Widget(void) {
    iWidget *div = new_Widget();
    setFlags_Widget(div, resizeChildren_WidgetFlag | arrangeHorizontal_WidgetFlag, iTrue);
    return div;
}

iWidget *addAction_Widget(iWidget *parent, int key, int kmods, const char *command) {
    iLabelWidget *action = new_LabelWidget("", key, kmods, command);
    setSize_Widget(as_Widget(action), zero_I2());
    addChildFlags_Widget(parent, iClob(action), hidden_WidgetFlag);
    return as_Widget(action);
}

/*-----------------------------------------------------------------------------------------------*/

static iBool menuHandler_(iWidget *menu, const char *cmd) {
    if (isVisible_Widget(menu)) {
        if (equal_Command(cmd, "menu.open") && pointer_Command(cmd) == menu->parent) {
            /* Don't reopen self; instead, root will close the menu. */
            return iFalse;
        }
        if (!equal_Command(cmd, "window.resized")) {
            closeMenu_Widget(menu);
        }
    }
    return iFalse;
}

iWidget *makeMenu_Widget(iWidget *parent, const iMenuItem *items, size_t n) {
    iWidget *menu = new_Widget();
    setFrameColor_Widget(menu, black_ColorId);
    setBackgroundColor_Widget(menu, gray25_ColorId);
    setFlags_Widget(menu,
                    keepOnTop_WidgetFlag | hidden_WidgetFlag | arrangeVertical_WidgetFlag |
                        arrangeSize_WidgetFlag | resizeChildrenToWidestChild_WidgetFlag,
                    iTrue);
    for (size_t i = 0; i < n; ++i) {
        const iMenuItem *item = &items[i];
        if (equal_CStr(item->label, "---")) {
            iWidget *sep = addChild_Widget(menu, iClob(new_Widget()));
            setBackgroundColor_Widget(sep, black_ColorId);
            sep->rect.size.y = gap_UI / 3;
            setFlags_Widget(sep, hover_WidgetFlag | fixedHeight_WidgetFlag, iTrue);
        }
        else {
            iLabelWidget *label = addChildFlags_Widget(
                menu,
                iClob(new_LabelWidget(item->label, item->key, item->kmods, item->command)),
                frameless_WidgetFlag | alignLeft_WidgetFlag | drawKey_WidgetFlag);
            updateSize_LabelWidget(label); /* drawKey was set */
        }
    }
    addChild_Widget(parent, iClob(menu));
    setCommandHandler_Widget(menu, menuHandler_);
    addAction_Widget(menu, SDLK_ESCAPE, 0, "cancel");
    return menu;
}

void openMenu_Widget(iWidget *d, iInt2 coord) {
    /* Menu closes when commands are emitted, so handle any pending ones beforehand. */
//    processEvents_App(postedEventsOnly_AppEventMode);
    setFlags_Widget(d, hidden_WidgetFlag, iFalse);
    arrange_Widget(d);
    d->rect.pos = coord;
    /* Ensure the full menu is visible. */
    const iInt2 rootSize = rootSize_Window(get_Window());
    const int bottomExcess = bottom_Rect(bounds_Widget(d)) - rootSize.y;
    if (bottomExcess > 0) {
        d->rect.pos.y -= bottomExcess;
    }
    if (top_Rect(d->rect) < 0) {
        d->rect.pos.y += -top_Rect(d->rect);
    }
    if (right_Rect(bounds_Widget(d)) > rootSize.x) {
        d->rect.pos.x = coord.x - d->rect.size.x;
    }
    if (left_Rect(d->rect) < 0) {
        d->rect.pos.x = 0;
    }
    refresh_App();
}

void closeMenu_Widget(iWidget *d) {
    setFlags_Widget(d, hidden_WidgetFlag, iTrue);
    refresh_App();
}

int checkContextMenu_Widget(iWidget *menu, const SDL_Event *ev) {
    if (ev->type == SDL_MOUSEBUTTONDOWN && ev->button.button == SDL_BUTTON_RIGHT) {
        if (isVisible_Widget(menu)) {
            closeMenu_Widget(menu);
            return 0x1;
        }
        const iInt2 mousePos = init_I2(ev->button.x, ev->button.y);
        if (contains_Widget(menu->parent, mousePos)) {
            openMenu_Widget(menu, localCoord_Widget(menu->parent, mousePos));
        }
        return 0x2;
    }
    return 0;
}

iLabelWidget *makeMenuButton_LabelWidget(const char *label, const iMenuItem *items, size_t n) {
    iLabelWidget *button = new_LabelWidget(label, 0, 0, "menu.open");
    iWidget *menu = makeMenu_Widget(as_Widget(button), items, n);
    setId_Widget(menu, "menu");
    return button;
}

/*-----------------------------------------------------------------------------------------------*/

static iBool isTabPage_Widget_(const iWidget *tabs, const iWidget *page) {
    return page->parent == findChild_Widget(tabs, "tabs.pages");
}

static iBool tabSwitcher_(iWidget *tabs, const char *cmd) {
    if (equal_Command(cmd, "tabs.switch")) {
        iWidget *target = pointerLabel_Command(cmd, "page");
        if (!target) {
            const iString *id = string_Command(cmd, "id");
            target = findChild_Widget(tabs, cstr_String(id));
        }
        if (!target) return iFalse;
        if (flags_Widget(target) & focusable_WidgetFlag) {
            setFocus_Widget(target);
        }
        if (isTabPage_Widget_(tabs, target)) {
            showTabPage_Widget(tabs, target);
            return iTrue;
        }
        else if (hasParent_Widget(target, tabs)) {
            /* Some widget on a page. */
            while (!isTabPage_Widget_(tabs, target)) {
                target = target->parent;
            }
            showTabPage_Widget(tabs, target);
            return iTrue;
        }
    }
    else if (equal_Command(cmd, "tabs.next") || equal_Command(cmd, "tabs.prev")) {
        iWidget *pages = findChild_Widget(tabs, "tabs.pages");
        int tabIndex = 0;
        iConstForEach(ObjectList, i, pages->children) {
            const iWidget *child = constAs_Widget(i.object);
            if (isVisible_Widget(child)) break;
            tabIndex++;
        }
        tabIndex += (equal_Command(cmd, "tabs.next") ? +1 : -1);
        showTabPage_Widget(tabs, child_Widget(pages, iWrap(tabIndex, 0, childCount_Widget(pages))));
        return iTrue;
    }
    return iFalse;
}

iWidget *makeTabs_Widget(iWidget *parent) {
    iWidget *tabs = makeVDiv_Widget();
    iWidget *buttons = addChild_Widget(tabs, iClob(new_Widget()));
    setFlags_Widget(buttons, arrangeHorizontal_WidgetFlag | arrangeHeight_WidgetFlag, iTrue);
    setId_Widget(buttons, "tabs.buttons");
    iWidget *pages = addChildFlags_Widget(
        tabs, iClob(new_Widget()), expand_WidgetFlag | resizeChildren_WidgetFlag);
    setId_Widget(pages, "tabs.pages");
    addChild_Widget(parent, iClob(tabs));
    setCommandHandler_Widget(tabs, tabSwitcher_);
    return tabs;
}

static void addTabPage_Widget_(iWidget *tabs, enum iWidgetAddPos addPos, iWidget *page,
                               const char *label, int key, int kmods) {
    iWidget *   pages  = findChild_Widget(tabs, "tabs.pages");
    const iBool isSel  = childCount_Widget(pages) == 0;
    iWidget *   button = addChildPos_Widget(
        findChild_Widget(tabs, "tabs.buttons"),
        iClob(new_LabelWidget(label, key, kmods, format_CStr("tabs.switch page:%p", page))),
        addPos);
    setFlags_Widget(button, selected_WidgetFlag, isSel);
    addChildPos_Widget(pages, page, addPos);
    setFlags_Widget(page, hidden_WidgetFlag | disabled_WidgetFlag, !isSel);
}

void appendTabPage_Widget(iWidget *tabs, iWidget *page, const char *label, int key, int kmods) {
    addTabPage_Widget_(tabs, back_WidgetAddPos, page, label, key, kmods);
}

void prependTabPage_Widget(iWidget *tabs, iWidget *page, const char *label, int key, int kmods) {
    addTabPage_Widget_(tabs, front_WidgetAddPos, page, label, key, kmods);
}

iWidget *tabPage_Widget(iWidget *tabs, size_t index) {
    iWidget *pages = findChild_Widget(tabs, "tabs.pages");
    return child_Widget(pages, index);
}

iWidget *removeTabPage_Widget(iWidget *tabs, size_t index) {
    iWidget *buttons = findChild_Widget(tabs, "tabs.buttons");
    iWidget *pages = findChild_Widget(tabs, "tabs.pages");
    iWidget *button = removeChild_Widget(buttons, child_Widget(buttons, index));
    iRelease(button);
    iWidget *page = child_Widget(pages, index);
    ref_Object(page);
    setFlags_Widget(page, hidden_WidgetFlag | disabled_WidgetFlag, iFalse);
    removeChild_Widget(pages, page);
    return page;
}

void showTabPage_Widget(iWidget *tabs, const iWidget *page) {
    /* Select the corresponding button. */ {
        iWidget *buttons = findChild_Widget(tabs, "tabs.buttons");
        iForEach(ObjectList, i, buttons->children) {
            iAssert(isInstance_Object(i.object, &Class_LabelWidget));
            iAny *label = i.object;
            const iBool isSel =
                (pointerLabel_Command(cstr_String(command_LabelWidget(label)), "page") == page);
            setFlags_Widget(label, selected_WidgetFlag, isSel);
        }
    }
    /* Show/hide pages. */ {
        iWidget *pages = findChild_Widget(tabs, "tabs.pages");
        iForEach(ObjectList, i, pages->children) {
            iWidget *child = as_Widget(i.object);
            setFlags_Widget(child, hidden_WidgetFlag | disabled_WidgetFlag, child != page);
        }
    }
    /* Notify. */
    if (!isEmpty_String(id_Widget(page))) {
        postCommandf_App("tabs.changed id:%s", cstr_String(id_Widget(page)));
    }
}

const iWidget *currentTabPage_Widget(const iWidget *tabs) {
    iWidget *pages = findChild_Widget(tabs, "tabs.pages");
    iConstForEach(ObjectList, i, pages->children) {
        if (isVisible_Widget(constAs_Widget(i.object))) {
            return constAs_Widget(i.object);
        }
    }
    return NULL;
}

size_t tabCount_Widget(const iWidget *tabs) {
    return childCount_Widget(findChild_Widget(tabs, "tabs.buttons"));
}

/*-----------------------------------------------------------------------------------------------*/

static void acceptFilePath_(iWidget *dlg) {
    iInputWidget *input = findChild_Widget(dlg, "input");
    iString *path = makeAbsolute_Path(text_InputWidget(input));
    postCommandf_App("%s path:%s", cstr_String(id_Widget(dlg)), cstr_String(path));
    destroy_Widget(dlg);
    delete_String(path);
}

iBool filePathHandler_(iWidget *dlg, const char *cmd) {
    iWidget *ptr = as_Widget(pointer_Command(cmd));
    if (equal_Command(cmd, "input.ended")) {
        if (hasParent_Widget(ptr, dlg)) {
            if (arg_Command(cmd)) {
                acceptFilePath_(dlg);
            }
            else {
                destroy_Widget(dlg);
            }
            return iTrue;
        }
        return iFalse;
    }
    else if (ptr && !hasParent_Widget(ptr, dlg)) {
        /* Command from outside the dialog, so dismiss the dialog. */
        if (!equal_Command(cmd, "focus.lost")) {
            destroy_Widget(dlg);
        }
        return iFalse;
    }
    else if (equal_Command(cmd, "filepath.cancel")) {
        end_InputWidget(findChild_Widget(dlg, "input"), iFalse);
        destroy_Widget(dlg);
        return iTrue;
    }
    else if (equal_Command(cmd, "filepath.accept")) {
        acceptFilePath_(dlg);
        return iTrue;
    }
    return iFalse;
}

iWidget *makeSheet_Widget(const char *id) {
    iWidget *sheet = new_Widget();
    setId_Widget(sheet, id);
    setFrameColor_Widget(sheet, black_ColorId);
    setBackgroundColor_Widget(sheet, gray25_ColorId);
    setFlags_Widget(
        sheet, keepOnTop_WidgetFlag | arrangeVertical_WidgetFlag | arrangeHeight_WidgetFlag, iTrue);
    const iInt2 rootSize = rootSize_Window(get_Window());
    setSize_Widget(sheet, init_I2(rootSize.x / 2, 0));
    setFlags_Widget(sheet, fixedHeight_WidgetFlag, iFalse);
    return sheet;
}

void centerSheet_Widget(iWidget *sheet) {
    arrange_Widget(sheet);
    const iInt2 rootSize = rootSize_Window(get_Window());
    sheet->rect.pos.x = rootSize.x / 2 - sheet->rect.size.x / 2;
    postRefresh_App();
}

void makeFilePath_Widget(iWidget *      parent,
                         const iString *initialPath,
                         const char *   title,
                         const char *   acceptLabel,
                         const char *   command) {
    setFocus_Widget(NULL);
//    processEvents_App(postedEventsOnly_AppEventMode);
    iWidget *dlg = makeSheet_Widget(command);
    setCommandHandler_Widget(dlg, filePathHandler_);
    addChild_Widget(parent, iClob(dlg));
    addChildFlags_Widget(dlg, iClob(new_LabelWidget(title, 0, 0, NULL)), frameless_WidgetFlag);
    iInputWidget *input = addChild_Widget(dlg, iClob(new_InputWidget(0)));
    if (initialPath) {
        setText_InputWidget(input, collect_String(makeRelative_Path(initialPath)));
    }
    setId_Widget(as_Widget(input), "input");
    as_Widget(input)->rect.size.x = dlg->rect.size.x;
    addChild_Widget(dlg, iClob(makePadding_Widget(gap_UI)));
    iWidget *div = new_Widget(); {
        setFlags_Widget(div, arrangeHorizontal_WidgetFlag | arrangeSize_WidgetFlag, iTrue);
        addChild_Widget(div, iClob(new_LabelWidget("Cancel", SDLK_ESCAPE, 0, "filepath.cancel")));
        addChild_Widget(div, iClob(new_LabelWidget(acceptLabel, SDLK_RETURN, 0, "filepath.accept")));
    }
    addChild_Widget(dlg, iClob(div));
    centerSheet_Widget(dlg);
    setFocus_Widget(as_Widget(input));
}

static void acceptValueInput_(iWidget *dlg) {
    const iInputWidget *input = findChild_Widget(dlg, "input");
    const iString *val = text_InputWidget(input);
    postCommandf_App("%s arg:%d value:%s",
                     cstr_String(id_Widget(dlg)),
                     toInt_String(val),
                     cstr_String(val));
}

static void updateValueInputWidth_(iWidget *dlg) {
    const iInt2 rootSize = rootSize_Window(get_Window());
    iWidget *   title    = findChild_Widget(dlg, "valueinput.title");
    iWidget *   prompt   = findChild_Widget(dlg, "valueinput.prompt");
    dlg->rect.size.x     = iMaxi(iMaxi(rootSize.x / 2, title->rect.size.x), prompt->rect.size.x);
    as_Widget(findChild_Widget(dlg, "input"))->rect.size.x = dlg->rect.size.x;
    centerSheet_Widget(dlg);
}

iBool valueInputHandler_(iWidget *dlg, const char *cmd) {
    iWidget *ptr = as_Widget(pointer_Command(cmd));
    if (equal_Command(cmd, "window.resized")) {
        if (isVisible_Widget(dlg)) {
            updateValueInputWidth_(dlg);
        }
        return iFalse;
    }
    if (equal_Command(cmd, "input.ended")) {
        if (hasParent_Widget(ptr, dlg)) {
            if (arg_Command(cmd)) {
                acceptValueInput_(dlg);
            }
            else {
                postCommandf_App("valueinput.cancelled id:%s", cstr_String(id_Widget(dlg)));
            }
            destroy_Widget(dlg);
            return iTrue;
        }
        return iFalse;
    }
    else if (equal_Command(cmd, "cancel")) {
        postCommandf_App("valueinput.cancelled id:%s", cstr_String(id_Widget(dlg)));
        destroy_Widget(dlg);
        return iTrue;
    }
    else if (equal_Command(cmd, "valueinput.accept")) {
        acceptValueInput_(dlg);
        destroy_Widget(dlg);
        return iTrue;
    }
    return iFalse;
}

iWidget *makeValueInput_Widget(iWidget *parent, const iString *initialValue, const char *title,
                               const char *prompt, const char *acceptLabel, const char *command) {
    if (parent) {
        setFocus_Widget(NULL);
//        processEvents_App(postedEventsOnly_AppEventMode);
    }
    iWidget *dlg = makeSheet_Widget(command);
    setCommandHandler_Widget(dlg, valueInputHandler_);
    if (parent) {
        addChild_Widget(parent, iClob(dlg));
    }
    setId_Widget(addChild_Widget(dlg, iClob(new_LabelWidget(title, 0, 0, NULL))), "valueinput.title");
    setId_Widget(addChild_Widget(dlg, iClob(new_LabelWidget(prompt, 0, 0, NULL))), "valueinput.prompt");
    iInputWidget *input = addChild_Widget(dlg, iClob(new_InputWidget(0)));
    if (initialValue) {
        setText_InputWidget(input, initialValue);
    }
    setId_Widget(as_Widget(input), "input");
    updateValueInputWidth_(dlg);
    addChild_Widget(dlg, iClob(makePadding_Widget(gap_UI)));
    iWidget *div = new_Widget(); {
        setFlags_Widget(div, arrangeHorizontal_WidgetFlag | arrangeSize_WidgetFlag, iTrue);
        addChild_Widget(div, iClob(new_LabelWidget("Cancel", SDLK_ESCAPE, 0, "cancel")));
        addChild_Widget(div,
                        iClob(new_LabelWidget(acceptLabel ? acceptLabel : cyan_ColorEscape "OK",
                                              SDLK_RETURN,
                                              0,
                                              "valueinput.accept")));
    }
    addChild_Widget(dlg, iClob(div));
    centerSheet_Widget(dlg);
    if (parent) {
        setFocus_Widget(as_Widget(input));
    }
    return dlg;
}

void updateValueInput_Widget(iWidget *d, const char *title, const char *prompt) {
    setTextCStr_LabelWidget(findChild_Widget(d, "valueinput.title"), title);
    setTextCStr_LabelWidget(findChild_Widget(d, "valueinput.prompt"), prompt);
    updateValueInputWidth_(d);
}

static iBool messageHandler_(iWidget *msg, const char *cmd) {
    /* Any command dismisses the sheet. */
    iUnused(cmd);
    destroy_Widget(msg);
    return iFalse;
}

void makeMessage_Widget(const char *title, const char *msg) {
    iWidget *dlg = makeQuestion_Widget(
        title, msg, (const char *[]){ "Continue" }, (const char *[]){ "message.ok" }, 1);
    addAction_Widget(dlg, SDLK_ESCAPE, 0, "message.ok");
    addAction_Widget(dlg, SDLK_SPACE, 0, "message.ok");
}

iWidget *makeQuestion_Widget(const char *title,
                             const char *msg,
                             const char *labels[],
                             const char *commands[],
                             size_t      count) {
//    processEvents_App(postedEventsOnly_AppEventMode);
    iWidget *dlg = makeSheet_Widget("");
    setCommandHandler_Widget(dlg, messageHandler_);
    addChild_Widget(dlg, iClob(new_LabelWidget(title, 0, 0, NULL)));
    addChild_Widget(dlg, iClob(new_LabelWidget(msg, 0, 0, NULL)));
    addChild_Widget(dlg, iClob(makePadding_Widget(gap_UI)));
    iWidget *div = new_Widget(); {
        setFlags_Widget(div, arrangeHorizontal_WidgetFlag | arrangeSize_WidgetFlag, iTrue);
        for (size_t i = 0; i < count; ++i) {
            /* The last one is the default option. */
            const int key = (i == count - 1 ? SDLK_RETURN : 0);
            addChild_Widget(div, iClob(new_LabelWidget(labels[i], key, 0, commands[i])));
        }
    }
    addChild_Widget(dlg, iClob(div));
    addChild_Widget(get_Window()->root, iClob(dlg));
    centerSheet_Widget(dlg);
    return dlg;
}

void setToggle_Widget(iWidget *d, iBool active) {
    setFlags_Widget(d, selected_WidgetFlag, active);
    updateText_LabelWidget(
        (iLabelWidget *) d,
        collectNewFormat_String(
            "%s", isSelected_Widget(d) ? "YES" : "NO"));
}

static iBool toggleHandler_(iWidget *d, const char *cmd) {
    if (equal_Command(cmd, "toggle") && pointer_Command(cmd) == d) {
        setToggle_Widget(d, (flags_Widget(d) & selected_WidgetFlag) == 0);
        postCommand_Widget(d,
                           format_CStr("%s.changed arg:%d",
                                             cstr_String(id_Widget(d)),
                                             isSelected_Widget(d) ? 1 : 0));
        return iTrue;
    }
    return iFalse;
}

iWidget *makeToggle_Widget(const char *id) {
    iWidget *toggle = as_Widget(new_LabelWidget("YES", 0, 0, "toggle"));
    setId_Widget(toggle, id);
    setCommandHandler_Widget(toggle, toggleHandler_);
    return toggle;
}

iWidget *makePreferences_Widget(void) {
    iWidget *dlg = makeSheet_Widget("prefs");
    addChild_Widget(dlg, iClob(new_LabelWidget(cyan_ColorEscape "PREFERENCES", 0, 0, NULL)));
    iWidget *page = new_Widget();
    addChild_Widget(dlg, iClob(page));
    setFlags_Widget(page, arrangeHorizontal_WidgetFlag | arrangeSize_WidgetFlag, iTrue);
    iWidget *headings = addChildFlags_Widget(
        page, iClob(new_Widget()), arrangeVertical_WidgetFlag | arrangeSize_WidgetFlag);
    iWidget *values = addChildFlags_Widget(
        page, iClob(new_Widget()), arrangeVertical_WidgetFlag | arrangeSize_WidgetFlag);
    addChild_Widget(headings, iClob(makeHeading_Widget("Retain window size:")));
    addChild_Widget(values, iClob(makeToggle_Widget("prefs.retainwindow")));
    addChild_Widget(headings, iClob(makeHeading_Widget("UI scale factor:")));
    setId_Widget(addChild_Widget(values, iClob(new_InputWidget(8))), "prefs.uiscale");
    arrange_Widget(dlg);
//    as_Widget(songDir)->rect.size.x = dlg->rect.size.x - headings->rect.size.x;
    iWidget *div = new_Widget(); {
        setFlags_Widget(div, arrangeHorizontal_WidgetFlag | arrangeSize_WidgetFlag, iTrue);
        addChild_Widget(div, iClob(new_LabelWidget("Dismiss", SDLK_ESCAPE, 0, "prefs.dismiss")));
    }
    addChild_Widget(dlg, iClob(div));
    addChild_Widget(get_Window()->root, iClob(dlg));
    centerSheet_Widget(dlg);
    return dlg;
}
