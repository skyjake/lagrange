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

#include <the_Foundation/string.h>
#include <the_Foundation/rect.h>
#include <the_Foundation/vec2.h>
#include <SDL_events.h>
#include <ctype.h>

iDeclareType(Click)
iDeclareType(Widget)
iDeclareType(LabelWidget)

iBool           isCommand_UserEvent (const SDL_Event *, const char *cmd);
const char *    command_UserEvent   (const SDL_Event *);

iLocalDef iBool isResize_UserEvent(const SDL_Event *d) {
    return isCommand_UserEvent(d, "window.resized");
}

#if defined (iPlatformApple)
#   define KMOD_PRIMARY     KMOD_GUI
#   define KMOD_SECONDARY   KMOD_CTRL
#else
#   define KMOD_PRIMARY     KMOD_CTRL
#   define KMOD_SECONDARY   KMOD_GUI
#endif

int     keyMods_Sym         (int kmods); /* shift, alt, control, or gui */

/*-----------------------------------------------------------------------------------------------*/

enum iClickResult {
    none_ClickResult,
    started_ClickResult,
    drag_ClickResult,
    finished_ClickResult,
    aborted_ClickResult,
    double_ClickResult,
};

struct Impl_Click {
    iBool    isActive;
    int      button;
    iWidget *bounds;
    iInt2    startPos;
    iInt2    pos;
};

void                init_Click          (iClick *, iAnyObject *widget, int button);
enum iClickResult   processEvent_Click  (iClick *, const SDL_Event *event);
void                cancel_Click        (iClick *);

iBool               isMoved_Click       (const iClick *);
iInt2               pos_Click           (const iClick *);
iRect               rect_Click          (const iClick *);
iInt2               delta_Click         (const iClick *);

/*-----------------------------------------------------------------------------------------------*/

iWidget *       makePadding_Widget  (int size);
iLabelWidget *  makeHeading_Widget  (const char *text);
iWidget *       makeHDiv_Widget     (void);
iWidget *       makeVDiv_Widget     (void);
iWidget *       addAction_Widget    (iWidget *parent, int key, int kmods, const char *command);

/*-----------------------------------------------------------------------------------------------*/

iWidget *       makeToggle_Widget       (const char *id);
void            setToggle_Widget        (iWidget *toggle, iBool active);

/*-----------------------------------------------------------------------------------------------*/

iDeclareType(MenuItem)

struct Impl_MenuItem {
    const char *label;
    int key;
    int kmods;
    const char *command;
};

iWidget *   makeMenu_Widget     (iWidget *parent, const iMenuItem *items, size_t n); /* returns no ref */
void        openMenu_Widget     (iWidget *, iInt2 coord);
void        closeMenu_Widget    (iWidget *);

int         checkContextMenu_Widget   (iWidget *, const SDL_Event *ev); /* see macro below */

#define processContextMenuEvent_Widget(menu, sdlEvent, stmtEaten) \
    for (const int result = checkContextMenu_Widget((menu), (sdlEvent));;) { \
        if (result) { {stmtEaten;} return result >> 1; } \
        break; \
    }

iLabelWidget *  makeMenuButton_LabelWidget  (const char *label, const iMenuItem *items, size_t n);

/*-----------------------------------------------------------------------------------------------*/

iWidget *       makeTabs_Widget         (iWidget *parent);
void            appendTabPage_Widget    (iWidget *tabs, iWidget *page, const char *label, int key, int kmods);
void            prependTabPage_Widget   (iWidget *tabs, iWidget *page, const char *label, int key, int kmods);
iWidget *       removeTabPage_Widget    (iWidget *tabs, size_t index); /* returns the page */
void            showTabPage_Widget      (iWidget *tabs, const iWidget *page);
void            setTabPageLabel_Widget  (iWidget *tabs, const iAnyObject *page, const iString *label);
iWidget *       tabPage_Widget          (iWidget *tabs, size_t index);
iLabelWidget *  tabPageButton_Widget    (iWidget *tabs, const iAnyObject *page);
iBool           isTabButton_Widget      (const iWidget *);
size_t          tabPageIndex_Widget     (const iWidget *tabs, const iAnyObject *page);
const iWidget * currentTabPage_Widget   (const iWidget *tabs);
size_t          tabCount_Widget         (const iWidget *tabs);

/*-----------------------------------------------------------------------------------------------*/

iWidget *   makeSheet_Widget        (const char *id);
void        centerSheet_Widget      (iWidget *sheet);

void        makeFilePath_Widget     (iWidget *parent, const iString *initialPath, const char *title,
                                     const char *acceptLabel, const char *command);
iWidget *   makeValueInput_Widget   (iWidget *parent, const iString *initialValue, const char *title,
                                     const char *prompt, const char *acceptLabel, const char *command);
void        updateValueInput_Widget (iWidget *, const char *title, const char *prompt);
void        makeMessage_Widget      (const char *title, const char *msg);
iWidget *   makeQuestion_Widget     (const char *title, const char *msg,
                                     const char *labels[], const char *commands[], size_t count);

iWidget *   makePreferences_Widget      (void);
iWidget *   makeBookmarkEditor_Widget   (void);
iWidget *   makeBookmarkCreation_Widget (const iString *url, const iString *title, iChar icon);
