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

#include "mobile.h"

#include <the_Foundation/array.h>
#include <the_Foundation/string.h>
#include <the_Foundation/rect.h>
#include <the_Foundation/vec2.h>
#include <SDL_events.h>
#include <ctype.h>

iDeclareType(Click)
iDeclareType(Widget)
iDeclareType(LabelWidget)
iDeclareType(InputWidget)
iDeclareType(Window)

iBool           isCommand_SDLEvent  (const SDL_Event *d);
iBool           isCommand_UserEvent (const SDL_Event *, const char *cmd);
const char *    command_UserEvent   (const SDL_Event *);

iLocalDef iBool isResize_UserEvent(const SDL_Event *d) {
    return isCommand_UserEvent(d, "window.resized");
}
iLocalDef iBool isMetricsChange_UserEvent(const SDL_Event *d) {
    return isCommand_UserEvent(d, "metrics.changed");
}

iLocalDef iBool isEmulatedMouseDevice_UserEvent (const SDL_Event *d) {
    return (d->type == SDL_MOUSEBUTTONDOWN || d->type == SDL_MOUSEBUTTONUP) &&
           d->button.which & 1024;
}

void    emulateMouseClick_Widget    (const iWidget *, int button);
void    emulateMouseClickPos_Widget (const iWidget *, int button, iInt2 clickPos);

enum iMouseWheelFlag {
    /* Note: A future version of SDL may support per-pixel scrolling, but 2.0.x doesn't. */
    perPixel_MouseWheelFlag       = iBit(9), /* e.g., trackpad or finger scroll; applied to `direction` */
    inertia_MouseWheelFlag        = iBit(10),
    scrollFinished_MouseWheelFlag = iBit(11),
};

iLocalDef void setPerPixel_MouseWheelEvent(SDL_MouseWheelEvent *ev, iBool set) {
    iChangeFlags(ev->direction, perPixel_MouseWheelFlag, set);
}
iLocalDef void setInertia_MouseWheelEvent(SDL_MouseWheelEvent *ev, iBool set) {
    iChangeFlags(ev->direction, inertia_MouseWheelFlag, set);
}
iLocalDef void setScrollFinished_MouseWheelEvent(SDL_MouseWheelEvent *ev, iBool set) {
    iChangeFlags(ev->direction, scrollFinished_MouseWheelFlag, set);
}

iLocalDef iBool isPerPixel_MouseWheelEvent(const SDL_MouseWheelEvent *ev) {
    return (ev->direction & perPixel_MouseWheelFlag) != 0;
}
iLocalDef iBool isInertia_MouseWheelEvent(const SDL_MouseWheelEvent *ev) {
    return (ev->direction & inertia_MouseWheelFlag) != 0;
}
iLocalDef iBool isScrollFinished_MouseWheelEvent(const SDL_MouseWheelEvent *ev) {
    return (ev->direction & scrollFinished_MouseWheelFlag) != 0;
}

iInt2   mouseCoord_SDLEvent     (const SDL_Event *);
iInt2   coord_MouseWheelEvent   (const SDL_MouseWheelEvent *);

#if defined (iPlatformTerminal)
#   define KMOD_PRIMARY     KMOD_CTRL
#   define KMOD_SECONDARY   KMOD_ALT
#   define KMOD_TERTIARY    KMOD_CTRL | KMOD_ALT  /* TODO: does this work? */
#   define KMOD_ACCEPT      KMOD_ALT
#   define KMOD_UNDO        KMOD_ALT
#   define KMOD_ZOOM        0
#elif defined (iPlatformApple)
#   define KMOD_PRIMARY     KMOD_GUI
#   define KMOD_SECONDARY   KMOD_GUI | KMOD_SHIFT
#   define KMOD_TERTIARY    KMOD_GUI | KMOD_SHIFT | KMOD_ALT
#   define KMOD_ACCEPT      KMOD_PRIMARY
#   define KMOD_UNDO        KMOD_PRIMARY
#   define KMOD_ZOOM        KMOD_PRIMARY
#else
#   define KMOD_PRIMARY     KMOD_CTRL
#   define KMOD_SECONDARY   KMOD_CTRL | KMOD_SHIFT
#   define KMOD_TERTIARY    KMOD_CTRL | KMOD_SHIFT | KMOD_ALT
#   define KMOD_ACCEPT      KMOD_PRIMARY
#   define KMOD_UNDO        KMOD_PRIMARY
#   define KMOD_ZOOM        KMOD_PRIMARY
#endif

enum iOpenTabFlag {
    new_OpenTabFlag           = iBit(1),
    newBackground_OpenTabFlag = iBit(2),
    newTabMask_OpenTabFlag    = new_OpenTabFlag | newBackground_OpenTabFlag,
    otherRoot_OpenTabFlag     = iBit(3),
};

iBool       isMod_Sym           (int key);
int         normalizedMod_Sym   (int key);
int         keyMods_Sym         (int kmods); /* shift, alt, control, or gui */
void        toString_Sym        (int key, int kmods, iString *str);
int         openTabMode_Sym     (int kmods); /* returns OpenTabFlags */

iRangei     intersect_Rangei    (iRangei a, iRangei b);
iRangei     union_Rangei        (iRangei a, iRangei b);

iLocalDef iBool equal_Rangei(iRangei a, iRangei b) {
    return a.start == b.start && a.end == b.end;
}
iLocalDef iBool isEmpty_Rangei(iRangei d) {
    return size_Range(&d) == 0;
}
iLocalDef iBool contains_Rangei(iRangei a, int b) {
    return b >= a.start && b < a.end;
}
iLocalDef iBool isOverlapping_Rangei(iRangei a, iRangei b) {
    return !isEmpty_Rangei(intersect_Rangei(a, b));
}

enum iRangeExtension {
    word_RangeExtension            = iBit(1),
    line_RangeExtension            = iBit(2),
    moveStart_RangeExtension       = iBit(3),
    moveEnd_RangeExtension         = iBit(4),
    bothStartAndEnd_RangeExtension = moveStart_RangeExtension | moveEnd_RangeExtension,
};

void        extendRange_Rangecc     (iRangecc *, iRangecc bounds, int mode);

iBool       isSelectionBreaking_Char(iChar);

/*-----------------------------------------------------------------------------------------------*/

iDeclareType(Anim)

enum iAnimFlag {
    indefinite_AnimFlag = iBit(1), /* does not end; must be linear */
    easeIn_AnimFlag     = iBit(2),
    easeOut_AnimFlag    = iBit(3),
    easeBoth_AnimFlag   = easeIn_AnimFlag | easeOut_AnimFlag,
    softer_AnimFlag     = iBit(4),
    muchSofter_AnimFlag = iBit(5),
    bounce_AnimFlag     = iBit(6),
};

struct Impl_Anim {
    float    from, to, bounce;
    uint32_t when, due;
    int      flags;
};

void    init_Anim           (iAnim *, float value);
void    setValue_Anim       (iAnim *, float to, uint32_t span);
void    setValueSpeed_Anim  (iAnim *, float to, float unitsPerSecond);
void    setValueEased_Anim  (iAnim *, float to, uint32_t span);
void    setFlags_Anim       (iAnim *, int flags, iBool set);
void    stop_Anim           (iAnim *);

iBool   isFinished_Anim     (const iAnim *);
float   pos_Anim            (const iAnim *);
float   value_Anim          (const iAnim *);

iLocalDef float targetValue_Anim(const iAnim *d) {
    return d->to;
}
iLocalDef iBool isLinear_Anim(const iAnim *d) {
    return (d->flags & (easeIn_AnimFlag | easeOut_AnimFlag)) == 0;
}

/*-----------------------------------------------------------------------------------------------*/

enum iClickResult {
    none_ClickResult,
    started_ClickResult,
    drag_ClickResult,
    finished_ClickResult,
    aborted_ClickResult,
};

struct Impl_Click {
    int      buttons; /* all recognized buttons */
    int      clickButton; /* currently active click */
    iBool    isActive;
    iBool    isDragging;
    int      count;
    iWidget *bounds;
    int      minHeight;
    int      minDrag;
    iInt2    startPos;
    iInt2    pos;
};

void                init_Click          (iClick *, iAnyObject *widget, int button);
void                initButtons_Click   (iClick *, iAnyObject *widget, int buttonMask);
enum iClickResult   processEvent_Click  (iClick *, const SDL_Event *event);
void                cancel_Click        (iClick *);

iBool               isMoved_Click       (const iClick *);
iInt2               pos_Click           (const iClick *);
iRect               rect_Click          (const iClick *);
iInt2               delta_Click         (const iClick *);
iBool               contains_Click      (const iClick *, iInt2 coord);

/*-----------------------------------------------------------------------------------------------*/

iDeclareType(SmoothScroll)

typedef void (*iSmoothScrollNotifyFunc)(iAnyObject *, int offset, uint32_t span);

enum iSmoothScrollFlags {
    pullDownAction_SmoothScrollFlag = iBit(1),
    pullUpAction_SmoothScrollFlag = iBit(2),
};

struct Impl_SmoothScroll {
    iAnim    pos;
    int      max;
    int      overscroll;
    iWidget *widget;
    int      flags;
    int      pullActionTriggered;
    iSmoothScrollNotifyFunc notify;
};

void    init_SmoothScroll           (iSmoothScroll *, iWidget *owner, iSmoothScrollNotifyFunc notify);

void    reset_SmoothScroll          (iSmoothScroll *);
void    setMax_SmoothScroll         (iSmoothScroll *, int max);
void    move_SmoothScroll           (iSmoothScroll *, int offset);
void    moveSpan_SmoothScroll       (iSmoothScroll *, int offset, uint32_t span);
iBool   processEvent_SmoothScroll   (iSmoothScroll *, const SDL_Event *ev);

float   pos_SmoothScroll            (const iSmoothScroll *);
iBool   isFinished_SmoothScroll     (const iSmoothScroll *);
float   pullActionPos_SmoothScroll  (const iSmoothScroll *); /* 0...1 */

/*-----------------------------------------------------------------------------------------------*/

iWidget *       makePadding_Widget  (int size);
iLabelWidget *  makeHeading_Widget  (const char *text);
iWidget *       makeHDiv_Widget     (void);
iWidget *       makeVDiv_Widget     (void);
iWidget *       addAction_Widget    (iWidget *parent, int key, int kmods, const char *command);
iBool           isAction_Widget     (const iWidget *);
iBool           isButton_Widget     (const iAnyObject *);

/*-----------------------------------------------------------------------------------------------*/

iWidget *       makeToggle_Widget       (const char *id);
void            setToggle_Widget        (iWidget *toggle, iBool active);

/*-----------------------------------------------------------------------------------------------*/

iDeclareType(MenuItem)

struct Impl_MenuItem {
    const char *label;
    int key;
    int kmods;
    union {
        const char *command;
        const void *data;
    };
};

enum iMenuOpenFlags {
    postCommands_MenuOpenFlags = iBit(1),
    center_MenuOpenFlags       = iBit(2),
    setFocus_MenuOpenFlags     = iBit(3),
    submenu_MenuOpenFlags      = iBit(4),
    forcePopup_MenuOpenFlags   = iBit(5),
    fromMenuBar_MenuOpenFlags  = iBit(6), /* must not overlap the menubar */
};

iWidget *       makeMenu_Widget                 (iWidget *parent, const iMenuItem *items, size_t n); /* returns no ref */
iWidget *       makeMenuFlags_Widget            (iWidget *parent, const iMenuItem *items, size_t n, iBool allowNative);
void            makeMenuItems_Widget            (iWidget *menu, const iMenuItem *items, size_t n);
void            openMenu_Widget                 (iWidget *, iInt2 windowCoord);
void            openMenuFlags_Widget            (iWidget *, iInt2 windowCoord, int flags);
void            openMenuAnchorFlags_Widget      (iWidget *, iRect windowAnchorRect, int menuOpenFlags);
void            closeMenu_Widget                (iWidget *);
iBool           handleMenuCommand_Widget        (iWidget *menu, const char *cmd); /* used as the command handler */
void            releaseNativeMenu_Widget        (iWidget *);
void            setMenuUpdateItemsFunc_Widget   (iWidget *menu, const iArray *(*func)(iWidget *));

size_t          count_MenuItem                  (const iMenuItem *itemsNullTerminated);
size_t          findWidestLabel_MenuItem        (const iMenuItem *items, size_t num);
size_t          findCommand_MenuItem            (const iMenuItem *items, size_t num, const char *command);
void            setSelected_NativeMenuItem      (iMenuItem *item, iBool isSelected);
void            appendIdentities_MenuItem       (iArray *menuItems, const char *command);
const iArray *  makeBookmarkFolderActions_MenuItem(const char *command, iBool withNullTerminator, uint32_t omitFolderId);

iChar           removeIconPrefix_String         (iString *);
enum iColorId   removeColorEscapes_String       (iString *);

iLabelWidget *  findMenuItem_Widget             (iWidget *menu, const char *command);
iMenuItem *     findNativeMenuItem_Widget       (iWidget *menu, const char *commandSuffix);
void            setMenuItemDisabled_Widget      (iWidget *menu, const char *command, iBool disable);
void            setMenuItemDisabledByIndex_Widget(iWidget *menu, size_t index, iBool disable);
void            setMenuItemLabel_Widget         (iWidget *menu, const char *command, const char *newLabel, iChar icon);
void            setMenuItemLabelByIndex_Widget  (iWidget *menu, size_t index, const char *newLabel);
void            setNativeMenuItems_Widget       (iWidget *menu, const iMenuItem *items, size_t n);
iWidget *       findUserData_Widget             (iWidget *, void *userData);
iWidget *       parentMenu_Widget               (const iWidget *menuItem);

int             checkContextMenu_Widget         (iWidget *, const SDL_Event *ev); /* see macro below */
void            animateToRootVisibleTop_Widget  (iWidget *, uint32_t span);

#define processContextMenuEvent_Widget(menu, sdlEvent, stmtEaten) \
    for (const int result = checkContextMenu_Widget((menu), (sdlEvent));;) { \
        if (result) { {stmtEaten;} return result >> 1; } \
        break; \
    }

iLabelWidget *  makeMenuButton_LabelWidget          (const char *label, const iMenuItem *items, size_t n);
void            updateDropdownSelection_LabelWidget (iLabelWidget *dropButton, const char *selectedCommand);
const char *    selectedDropdownCommand_LabelWidget (const iLabelWidget *dropButton);
iLabelWidget *  makeIdentityDropdown_LabelWidget    (iWidget *headings, iWidget *values,
                                                     const iArray *identItems, const char *label,
                                                     const char *id);
/*-----------------------------------------------------------------------------------------------*/

iWidget *       makeMenuBar_Widget                  (const iMenuItem *topLevelMenus, size_t num);
iBool           handleTopLevelMenuBarCommand_Widget (iWidget *menuButton, const char *cmd);

/*-----------------------------------------------------------------------------------------------*/

iWidget *       makeTabs_Widget         (iWidget *parent);
void            setTabBarPosition_Widget(iWidget *tabs, iBool atBottom);
void            setVerticalTabBar_Widget(iWidget *tabs);
void            appendTabPage_Widget    (iWidget *tabs, iWidget *page, const char *label, int key, int kmods);
void            appendFramelessTabPage_Widget(iWidget *tabs, iWidget *page, const char *title, int iconColor, int shortcut, int kmods);
iWidget *       appendTwoColumnTabPage_Widget(iWidget *tabs, const char *title, int iconColor, int shortcut,
                                              iWidget **headings, iWidget **values);
void            prependTabPage_Widget   (iWidget *tabs, iWidget *page, const char *label, int key, int kmods);
iWidget *       removeTabPage_Widget    (iWidget *tabs, size_t index); /* returns the page */
void            moveTabPage_Widget      (iWidget *tabs, size_t index, size_t newIndex);
void            resizeToLargestPage_Widget  (iWidget *tabs);
void            showTabPage_Widget      (iWidget *tabs, const iAnyObject *page);
void            addTabCloseButton_Widget(iWidget *tabs, const iWidget *page, const char *command);
void            setTabPageLabel_Widget  (iWidget *tabs, const iAnyObject *page, const iString *label);
iWidget *       tabPage_Widget          (iWidget *tabs, size_t index);
iLabelWidget *  tabPageButton_Widget    (iWidget *tabs, const iAnyObject *page);
iBool           isVerticalTabBar_Widget (const iWidget *tabs);
iBool           isTabButton_Widget      (const iWidget *);
void            moveTabButtonToEnd_Widget(iWidget *tabButton);
size_t          tabPageIndex_Widget     (const iWidget *tabs, const iAnyObject *page);
const iWidget * currentTabPage_Widget   (const iWidget *tabs);
size_t          tabCount_Widget         (const iWidget *tabs);

/*-----------------------------------------------------------------------------------------------*/

iWidget *   makeSheet_Widget            (const char *id);
void        useSheetStyle_Widget        (iWidget *);
void        enableResizing_Widget       (iWidget *, int minWidth, const char *resizeId);
void        restoreWidth_Widget         (iWidget *);
void        updateBookmarkEditorFieldWidths_Widget(iWidget *);
iWidget *   makeDialogButtons_Widget    (const iMenuItem *actions, size_t numActions);
iWidget *   makeTwoColumns_Widget       (iWidget **headings, iWidget **values);

iLabelWidget *dialogAcceptButton_Widget (const iWidget *);
int           dialogTransitionDir_Widget(const iWidget *);
iLabelWidget *addDialogTitle_Widget     (iWidget *, const char *text, const char *idOrNull);
iLabelWidget *addWrappedLabel_Widget    (iWidget *, const char *text, const char *idOrNull);
iInputWidget *addTwoColumnDialogInputField_Widget(iWidget *headings, iWidget *values,
                                                  const char *labelText, const char *inputId,
                                                  iInputWidget *input);

void        makeFilePath_Widget     (iWidget *parent, const iString *initialPath, const char *title,
                                     const char *acceptLabel, const char *command);
iWidget *   makeValueInput_Widget   (iWidget *parent, const iString *initialValue, const char *title,
                                     const char *prompt, const char *acceptLabel, const char *command);
iWidget *   makeValueInputWithAdditionalActions_Widget
                                    (iWidget *parent, const iString *initialValue, const char *title,
                                     const char *prompt, const char *acceptLabel, const char *command,
                                     const iMenuItem *additionalActions, size_t numAdditionalActions);
void        updateValueInput_Widget (iWidget *, const char *title, const char *prompt);
iWidget *   makeSimpleMessage_Widget(const char *title, const char *msg);
iWidget *   makeMessage_Widget      (const char *title, const char *msg,
                                     const iMenuItem *items, size_t numItems);
iWidget *   makeQuestion_Widget     (const char *title, const char *msg,
                                     const iMenuItem *items, size_t numItems);

iWidget *   makePreferences_Widget          (void);
void        updatePreferencesLayout_Widget  (iWidget *prefs);

iWidget *   makeBookmarkEditor_Widget       (uint32_t folderId, iBool withDup);
void        setBookmarkEditorParentFolder_Widget(iWidget *editor, uint32_t folderId);
iWidget *   makeBookmarkCreation_Widget     (const iString *url, const iString *title, iChar icon);
iWidget *   makeIdentityCreation_Widget     (void);
iWidget *   makeFeedSettings_Widget         (uint32_t bookmarkId);
iWidget *   makeSiteSpecificSettings_Widget (const iString *url);
iWidget *   makeSnippetCreation_Widget      (void);
iWidget *   makeTranslation_Widget          (iWidget *parent);
iWidget *   makeGlyphFinder_Widget          (void);
iWidget *   makeUserDataImporter_Widget     (const iString *archivePath);
iWidget *   makeLinkImporter_Widget         (size_t count);

const char *    languageId_String   (const iString *menuItemLabel);
int             languageIndex_CStr  (const char *langId);

iWindow *   promoteDialogToWindow_Widget    (iWidget *);
iBool       isPromoted_Widget               (iWidget *);
void        destroyDialog_Widget            (iWidget *);

/*-----------------------------------------------------------------------------------------------*/

iDeclareType(PerfTimer)

struct Impl_PerfTimer {
    uint64_t ticks;
};

void        init_PerfTimer                  (iPerfTimer *);
uint64_t    elapsedMicroseconds_PerfTimer   (const iPerfTimer *);
void        print_PerfTimer                 (const iPerfTimer *, const char *msg);

#define start_PerfTimer(name) iPerfTimer _##name##_PerfTimer; init_PerfTimer(&_##name##_PerfTimer)
#define stop_PerfTimer(name)  print_PerfTimer(&_##name##_PerfTimer, #name)

