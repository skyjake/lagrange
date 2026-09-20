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

#include "tabs.h"

#include "app.h"
#include "command.h"
#include "defs.h"
#include "dialog.h"
#include "labelwidget.h"
#include "root.h"
#include "util.h"
#include "widget.h"
#include "window.h"

static iBool isTabPage_Widget_(const iWidget *tabs, const iWidget *page) {
    return page && page->parent == findChild_Widget(tabs, "tabs.pages");
}

static void unfocusFocusInsideTabPage_(const iWidget *page) {
    iWidget *focus = focus_Widget();
    if (page && focus && hasParent_Widget(focus, page)) {
//        printf("unfocus inside page: %p\n", focus);
        setFocus_Widget(NULL);
    }
}

static void setFocusInsideTabPage_(iWidget *page) {
    iWidget *focus =
        flags_Widget(page) & focusable_WidgetFlag
            ? page
            : findFocusable_Widget(child_Widget(page, 0),
                                   forward_WidgetFocusDir | notInput_WidgetFocusFlag);
    if (focus == page || hasParent_Widget(focus, page)) {
        setFocus_Widget(focus);
    }
}

static iBool tabSwitcher_(iWidget *tabs, const char *cmd) {
    if (equal_Command(cmd, "tabs.switch")) {
        iWidget *target = pointerLabel_Command(cmd, "page");
        if (!target && hasLabel_Command(cmd, "id")) {
            /* Note that an empty ID would match the first child without an ID. */
            target = findChild_Widget(tabs, cstr_Command(cmd, "id"));
        }
        if (!target) return iFalse;
        unfocusFocusInsideTabPage_(currentTabPage_Widget(tabs));
        if (flags_Widget(target) & focusable_WidgetFlag) {
            setFocus_Widget(target);
        }
        if (isTabPage_Widget_(tabs, target)) {
            showTabPage_Widget(tabs, target);
            return iTrue;
        }
        else if (hasParent_Widget(target, tabs)) {
            /* Some widget on a page. */
            while (target && !isTabPage_Widget_(tabs, target)) {
                target = target->parent;
            }
            showTabPage_Widget(tabs, target);
            return iTrue;
        }
    }
    else if (equal_Command(cmd, "tabs.next") || equal_Command(cmd, "tabs.prev")) {
        unfocusFocusInsideTabPage_(currentTabPage_Widget(tabs));
        iWidget *pages    = findChild_Widget(tabs, "tabs.pages");
        iWidget *buttons  = findChild_Widget(tabs, "tabs.buttons");
        int      tabIndex = 0;
        iConstForEach(ObjectList, i, pages->children) {
            const iWidget *child = constAs_Widget(i.object);
            if (isVisible_Widget(child)) break;
            tabIndex++;
        }
        const int dir = (equal_Command(cmd, "tabs.next") ? +1 : -1);
        /* If out of tabs, rotate to the next set of tabs if one is available.
           However, don't do this if the tabs are inside a sheet or dialog. */
        if ((tabIndex == 0 && dir < 0) || (tabIndex == childCount_Widget(pages) - 1 && dir > 0)) {
            if (focusRoot_Widget(tabs) == root_Widget(tabs)) {
                iWidget *nextTabs = findChild_Widget(otherRoot_Window(get_Window(), tabs->root)->widget,
                                                     "doctabs");
                iWidget *nextPages = findChild_Widget(nextTabs, "tabs.pages");
                if (nextPages) {
                    tabIndex = (int) (dir < 0 ? childCount_Widget(nextPages) - 1 : 0);
                    showTabPage_Widget(nextTabs, child_Widget(nextPages, tabIndex));
                    postCommand_App("keyroot.next");
                }
            }
        }
        else if (isVisible_Widget(child_Widget(buttons, tabIndex + dir))) {
            showTabPage_Widget(tabs, child_Widget(pages, tabIndex + dir));
        }
        if (argLabel_Command(cmd, "keydown")) {
            setFocusInsideTabPage_((iWidget *) currentTabPage_Widget(tabs));
        }
        refresh_Widget(tabs);
        return iTrue;
    }
    return iFalse;
}

iWidget *makeTabs_Widget(iWidget *parent) {
    iWidget *tabs = makeVDiv_Widget();
    iWidget *buttons = addChild_Widget(tabs, iClob(new_Widget()));
    setFlags_Widget(buttons,
                    resizeWidthOfChildren_WidgetFlag | arrangeHorizontal_WidgetFlag |
                        arrangeHeight_WidgetFlag,
                    iTrue);
    setId_Widget(buttons, "tabs.buttons");
//    setBackgroundColor_Widget(buttons, red_ColorId);
    iWidget *content = addChildFlags_Widget(tabs, iClob(makeHDiv_Widget()), expand_WidgetFlag);
    setId_Widget(content, "tabs.content");
    iWidget *pages = addChildFlags_Widget(
        content, iClob(new_Widget()), expand_WidgetFlag | resizeChildren_WidgetFlag);
    setId_Widget(pages, "tabs.pages");
    addChild_Widget(parent, iClob(tabs));
    setCommandHandler_Widget(tabs, tabSwitcher_);
    return tabs;
}

void setTabBarPosition_Widget(iWidget *tabs, iBool atBottom) {
    if (tabs) {
        iWidget *buttons = findChild_Widget(tabs, "tabs.buttons");
        removeChild_Widget(tabs, buttons);
        addChildPos_Widget(tabs, buttons, atBottom ? back_WidgetAddPos : front_WidgetAddPos);
        iRelease(buttons);
    }
}

void setVerticalTabBar_Widget(iWidget *tabs) {
    iWidget *buttons = findChild_Widget(tabs, "tabs.buttons");
    iWidget *content = findChild_Widget(tabs, "tabs.content");
    setFlags_Widget(tabs, arrangeVertical_WidgetFlag, iFalse);
    setFlags_Widget(tabs, arrangeHorizontal_WidgetFlag, iTrue);
    setFlags_Widget(buttons, arrangeHorizontal_WidgetFlag | arrangeHeight_WidgetFlag |
                             resizeWidthOfChildren_WidgetFlag, iFalse);
    setFlags_Widget(buttons, arrangeVertical_WidgetFlag | arrangeWidth_WidgetFlag |
                    resizeChildrenToWidestChild_WidgetFlag, iTrue);
    buttons->flags2 |= centerChildrenVertical_WidgetFlag2;
    setFlags_Widget(content, arrangeHorizontal_WidgetFlag, iFalse);
    setFlags_Widget(content, arrangeVertical_WidgetFlag, iTrue);
}

iBool isVerticalTabBar_Widget(const iWidget *tabs) {
    return (tabs->flags & arrangeVertical_WidgetFlag) == 0;
}

static void addTabPage_Widget_(iWidget *tabs, enum iWidgetAddPos addPos, iWidget *page,
                               const char *label, int key, int kmods) {
    const iBool isVerticalTabs = isVerticalTabBar_Widget(tabs);
    iWidget *   pages   = findChild_Widget(tabs, "tabs.pages");
    const iBool isSel   = childCount_Widget(pages) == 0;
    iWidget *   buttons = findChild_Widget(tabs, "tabs.buttons");
    iWidget *   button  = addChildPos_Widget(
        buttons,
        iClob(newKeyMods_LabelWidget(label, key, kmods, format_CStr("tabs.switch page:%p", page))),
        addPos);
    checkIcon_LabelWidget((iLabelWidget *) button);
    if (isTerminal_Platform()) {
        setIcon_LabelWidget((iLabelWidget *) button, 0); /* no icon in the terminal (lack of room) */
    }
    setFlags_Widget(button, selected_WidgetFlag, isSel);
    setFlags_Widget(button,
                    commandOnClick_WidgetFlag |
                        (!isVerticalTabs ? horizontalOffset_WidgetFlag : 0) |
                        (isVerticalTabs ? alignLeft_WidgetFlag : expand_WidgetFlag),
                    iTrue);
    if (!cmp_String(id_Widget(tabs), "doctabs")) {
        /* Document tabs can be reordered.
           TODO: Maybe not hardcode the parent ID here? Could check a flag on `tabs`. */
        button->flags2 |= siblingOrderDraggable_WidgetFlag2;
        if (isMobile_Platform()) {
            button->flags |= touchDrag_WidgetFlag;
        }
    }
    if (prefs_App()->bottomTabBar) {
        setNoBottomFrame_LabelWidget((iLabelWidget *) button, iTrue);
    }
    else {
        setNoTopFrame_LabelWidget((iLabelWidget *) button, iTrue);
    }
    addChildPos_Widget(pages, page, addPos);
    if (tabCount_Widget(tabs) > 1 && (cmp_String(id_Widget(tabs), "doctabs") ||
                                      !prefs_App()->hideTabBar)) {
        setFlags_Widget(buttons, hidden_WidgetFlag, iFalse);
    }
    setFlags_Widget(page, hidden_WidgetFlag | disabled_WidgetFlag, !isSel);
}

void appendTabPage_Widget(iWidget *tabs, iWidget *page, const char *label, int key, int kmods) {
    addTabPage_Widget_(tabs, back_WidgetAddPos, page, label, key, kmods);
}

void prependTabPage_Widget(iWidget *tabs, iWidget *page, const char *label, int key, int kmods) {
    addTabPage_Widget_(tabs, front_WidgetAddPos, page, label, key, kmods);
}

void moveTabButtonToEnd_Widget(iWidget *tabButton) {
    iWidget *buttons = tabButton->parent;
    iWidget *tabs    = buttons->parent;
    removeChild_Widget(buttons, tabButton);
    addChild_Widget(buttons, iClob(tabButton));
    arrange_Widget(tabs);
}

iWidget *tabPage_Widget(iWidget *tabs, size_t index) {
    iWidget *pages = findChild_Widget(tabs, "tabs.pages");
    return child_Widget(pages, index);
}

iWidget *removeTabPage_Widget(iWidget *tabs, size_t index) {
    iWidget *buttons = findChild_Widget(tabs, "tabs.buttons");
    iWidget *pages   = findChild_Widget(tabs, "tabs.pages");
    iWidget *button  = removeChild_Widget(buttons, child_Widget(buttons, index));
    iRelease(button);
    iWidget *page = child_Widget(pages, index);
    setFlags_Widget(page, hidden_WidgetFlag | disabled_WidgetFlag, iFalse);
    removeChild_Widget(pages, page); /* `page` is now ours */
    if (tabCount_Widget(tabs) <= 1 && flags_Widget(buttons) & collapse_WidgetFlag) {
        setFlags_Widget(buttons, hidden_WidgetFlag, iTrue);
    }
    return page;
}

void moveTabPage_Widget(iWidget *tabs, size_t index, size_t newIndex) {
    const size_t count = tabCount_Widget(tabs);
    newIndex = iMin(newIndex, count - 1);
    if (index == newIndex) {
        return;
    }
    iWidget *buttons = findChild_Widget(tabs, "tabs.buttons");
    iWidget *pages   = findChild_Widget(tabs, "tabs.pages");
    iWidget *button  = child_Widget(buttons, index);
    iWidget *page    = child_Widget(pages, index);
    changeChildIndex_Widget(buttons, button, newIndex);
    changeChildIndex_Widget(pages, page, newIndex);
    arrange_Widget(tabs);
}

void resizeToLargestPage_Widget(iWidget *tabs) {
    if (!tabs) return;
//    puts("RESIZE TO LARGEST PAGE ...");
    iWidget *pages = findChild_Widget(tabs, "tabs.pages");
    iForEach(ObjectList, i, children_Widget(pages)) {
        setMinSize_Widget(i.object, zero_I2());
        iWidget *w = i.object;
        w->rect.size = zero_I2();
    }
    arrange_Widget(tabs);
    iInt2 largest = zero_I2();
    iConstForEach(ObjectList, j, children_Widget(pages)) {
        const iWidget *page = constAs_Widget(j.object);
        largest = max_I2(largest, page->rect.size);
    }
    iForEach(ObjectList, k, children_Widget(pages)) {
        setMinSize_Widget(k.object, largest);
    }
    const iWidget *buttons = findChild_Widget(tabs, "tabs.buttons");
    setFixedSize_Widget(tabs,
                        isVerticalTabBar_Widget(tabs) ? addX_I2(largest, width_Widget(buttons))
                                                      : addY_I2(largest, height_Widget(buttons)));
//    puts("... DONE WITH RESIZE TO LARGEST PAGE");
}

static iLabelWidget *tabButtonForPage_Widget_(iWidget *tabs, const iWidget *page) {
    iWidget *buttons = findChild_Widget(tabs, "tabs.buttons");
    iForEach(ObjectList, i, buttons->children) {
        iAssert(isInstance_Object(i.object, &Class_LabelWidget));
        iAny *label = i.object;
        if (pointerLabel_Command(cstr_String(command_LabelWidget(label)), "page") == page) {
            return label;
        }
    }
    return NULL;
}

void addTabCloseButton_Widget(iWidget *tabs, const iWidget *page, const char *command) {
    if (deviceType_App() == phone_AppDeviceType) {
        return; /* Close buttons not used on a phone due to lack of space. */
    }
    iLabelWidget *tabButton = tabButtonForPage_Widget_(tabs, page);
    setPadding_Widget(as_Widget(tabButton), 0, 0, 0, gap_UI / 4);
    setFlags_Widget(as_Widget(tabButton), arrangeVertical_WidgetFlag | resizeHeightOfChildren_WidgetFlag, iTrue);
#if defined (iPlatformApple)
    const int64_t edge = moveToParentLeftEdge_WidgetFlag;
#else
    const int64_t edge = moveToParentRightEdge_WidgetFlag;
#endif
    iLabelWidget *close = addChildFlags_Widget(
        as_Widget(tabButton),
        iClob(new_LabelWidget(close_Icon,
                              format_CStr("%s id:%s", command, cstr_String(id_Widget(page))))),
        edge | tight_WidgetFlag | frameless_WidgetFlag | noBackground_WidgetFlag |
            hidden_WidgetFlag | visibleOnParentHover_WidgetFlag);
    if (deviceType_App() != desktop_AppDeviceType) {
        setFlags_Widget(as_Widget(close),
                        hidden_WidgetFlag | visibleOnParentHover_WidgetFlag, iFalse);
    }
    if (deviceType_App() == tablet_AppDeviceType) {
        setFlags_Widget(as_Widget(close), hidden_WidgetFlag | disabledWhenHidden_WidgetFlag, iTrue);
        as_Widget(close)->flags2 |= visibleOnParentSelected_WidgetFlag2;
    }
    setNoAutoMinHeight_LabelWidget(close, iTrue);
    updateSize_LabelWidget(close);
}

void showTabPage_Widget(iWidget *tabs, const iAnyObject *page) {
    if (!page) {
        return;
    }
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
    iBool wasChanged = iFalse;
    /* Show/hide pages. */ {
        iWidget *pages = findChild_Widget(tabs, "tabs.pages");
        iForEach(ObjectList, i, pages->children) {
            iWidget    *child    = as_Widget(i.object);
            const iBool willHide = (child != page);
            if (flags_Widget(child) & hidden_WidgetFlag && !willHide) wasChanged |= iTrue;
            setFlags_Widget(child, hidden_WidgetFlag | disabled_WidgetFlag, willHide);
        }
        refresh_Widget(tabs);
    }
    /* Notify. */
    if (wasChanged && !isEmpty_String(id_Widget(page))) {
        notifyf_Root(constAs_Widget(page)->root,
                     "tabs.changed id:%s",
                     cstr_String(id_Widget(constAs_Widget(page))));
    }
}

iLabelWidget *tabPageButton_Widget(iWidget *tabs, const iAnyObject *page) {
    return tabButtonForPage_Widget_(tabs, page);
}

iBool isTabButton_Widget(const iWidget *d) {
    return d->parent && cmp_String(id_Widget(d->parent), "tabs.buttons") == 0;
}

void setTabPageLabel_Widget(iWidget *tabs, const iAnyObject *page, const iString *label) {
    iLabelWidget *button = tabButtonForPage_Widget_(tabs, page);
    setText_LabelWidget(button, label);
    arrange_Widget(tabs);
}

size_t tabPageIndex_Widget(const iWidget *tabs, const iAnyObject *page) {
    iWidget *pages = findChild_Widget(tabs, "tabs.pages");
    return indexOfChild_Widget(pages, page);
}

const iWidget *currentTabPage_Widget(const iWidget *tabs) {
    if (tabs) {
        iWidget *pages = findChild_Widget(tabs, "tabs.pages");
        iConstForEach(ObjectList, i, pages->children) {
            if (isVisible_Widget(i.object)) {
                return constAs_Widget(i.object);
            }
        }
    }
    return NULL;
}

size_t tabCount_Widget(const iWidget *tabs) {
    return tabs ? childCount_Widget(findChild_Widget(tabs, "tabs.pages")) : 0;
}

void appendFramelessTabPage_Widget(iWidget *tabs, iWidget *page, const char *title, int iconColor,
                                   int shortcut, int kmods) {
    appendTabPage_Widget(tabs, page, title, shortcut, kmods);
    setFlags_Widget(
        (iWidget *) back_ObjectList(children_Widget(findChild_Widget(tabs, "tabs.buttons"))),
        frameless_WidgetFlag | noBackground_WidgetFlag,
        iTrue);
    if (iconColor != none_ColorId) {
        setIconColor_LabelWidget(tabPageButton_Widget(tabs, page), iconColor);
    }
}

iWidget *appendTwoColumnTabPage_Widget(iWidget *tabs, const char *title, int iconColor,
                                       int shortcut, iWidget **headings, iWidget **values) {
    /* TODO: Use `makeTwoColumnWidget_()`, see above. */
    iWidget *page = new_Widget();
    setFlags_Widget(page, arrangeVertical_WidgetFlag | arrangeSize_WidgetFlag, iTrue);
    addChildFlags_Widget(page, iClob(new_Widget()), expand_WidgetFlag);
    setPadding_Widget(page, 0, gap_UI, 0, gap_UI);
    iWidget *columns = new_Widget();
    addChildFlags_Widget(page, iClob(columns), arrangeHorizontal_WidgetFlag | arrangeSize_WidgetFlag);
    *headings = addChildFlags_Widget(
        columns, iClob(new_Widget()), arrangeVertical_WidgetFlag | arrangeSize_WidgetFlag);
    *values = addChildFlags_Widget(
        columns, iClob(new_Widget()), arrangeVertical_WidgetFlag | arrangeSize_WidgetFlag);
    addChildFlags_Widget(page, iClob(new_Widget()), expand_WidgetFlag);
    appendFramelessTabPage_Widget(tabs, iClob(page), title, iconColor, shortcut, shortcut ? KMOD_PRIMARY : 0);
    return page;
}
