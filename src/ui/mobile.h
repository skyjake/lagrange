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

#pragma once

#include "defs.h"
#include <the_Foundation/rect.h>

#if defined (iPlatformAppleMobile)
#   define keyboardShowSpan_Mobile  450
#else
#   define keyboardShowSpan_Mobile  200
#endif

iDeclareType(ToolbarActionSpec)
    
struct Impl_ToolbarActionSpec {
    const char *icon;
    const char *label;
    const char *command;
};

extern const iToolbarActionSpec toolbarActions_Mobile[max_ToolbarAction];

iDeclareType(Widget)
iDeclareType(MenuItem)
    
iBool       isUsingPanelLayout_Mobile   (void);
iWidget *   makePanels_Mobile           (const char *id,
                                         const iMenuItem *itemsNullTerminated,
                                         const iMenuItem *actions, size_t numActions);
iWidget *   makePanelsParent_Mobile     (iWidget *parent,
                                         const char *id,                                         
                                         const iMenuItem *itemsNullTerminated,
                                         const iMenuItem *actions, size_t numActions);
void        initPanels_Mobile           (iWidget *panels, iWidget *parentWidget,
                                         const iMenuItem *itemsNullTerminated,
                                         const iMenuItem *actions, size_t numActions);

iWidget *   panel_Mobile                (const iWidget *panels, size_t index);
size_t      currentPanelIndex_Mobile    (const iWidget *panels);
iBool       isFullSizePanel_Mobile      (const iWidget *panels);

enum iTransitionFlags {
    incoming_TransitionFlag = iBit(1),
    dirMask_TransitionFlag  = iBit(2) | iBit(3),
};

enum iTransitionDir {
    right_TransitionDir  = 0,
    bottom_TransitionDir = 2,
    left_TransitionDir   = 4,
    top_TransitionDir    = 6,
};

void        setupMenuTransition_Mobile  (iWidget *menu,  iBool isIncoming);
void        setupSheetTransition_Mobile (iWidget *sheet, int flags);

int         leftSafeInset_Mobile        (void);
int         rightSafeInset_Mobile       (void);
int         topSafeInset_Mobile         (void);
int         bottomSafeInset_Mobile      (void);

/*----------------------------------------------------------------------------------------------*/

enum iSystemTextInputFlags {
    selectAll_SystemTextInputFlags         = iBit(1),
    multiLine_SystemTextInputFlags         = iBit(2),
    returnGo_SystemTextInputFlags          = iBit(3),
    returnSend_SystemTextInputFlags        = iBit(4),
    disableAutocorrect_SystemTextInputFlag = iBit(5),
    disableAutocapitalize_SystemTextInputFlag = iBit(6),
    alignRight_SystemTextInputFlag         = iBit(7),
    insertNewlines_SystemTextInputFlag     = iBit(8),
    extraPadding_SystemTextInputFlag       = iBit(9),
};

iDeclareType(SystemTextInput)
iDeclareTypeConstructionArgs(SystemTextInput, iRect rect, int flags)

void    setRect_SystemTextInput  (iSystemTextInput *, iRect rect);
void    setText_SystemTextInput  (iSystemTextInput *, const iString *text, iBool allowUndo);
void    setFont_SystemTextInput  (iSystemTextInput *, int fontId);
void    setTextChangedFunc_SystemTextInput
        (iSystemTextInput *, void (*textChangedFunc)(iSystemTextInput *, void *), void *);
void    selectAll_SystemTextInput(iSystemTextInput *);

const iString * text_SystemTextInput    (const iSystemTextInput *);
int     preferredHeight_SystemTextInput (const iSystemTextInput *);

/*----------------------------------------------------------------------------------------------*/

iBool   isSupported_SystemMenu  (void);
iBool   makePopup_SystemMenu    (iWidget *owner);
void    setRect_SystemMenu      (iWidget *owner, iRect anchorRect);
void    setHidden_SystemMenu    (iWidget *owner, iBool hide);
void    updateAfterBoundsChange_SystemMenu (iWidget *owner);
void    updateItems_SystemMenu  (iWidget *owner, const iMenuItem *items, size_t n);
void    releasePopup_SystemMenu (iWidget *owner);
