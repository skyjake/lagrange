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

/* Dialog construction: sheets, layouts, and the generic input/message dialogs. */

#include "menu.h"

iDeclareType(InputWidget)
iDeclareType(LabelWidget)
iDeclareType(Widget)
iDeclareType(Window)

iWidget *   makeSheet_Widget            (const char *id);
void        useSheetStyle_Widget        (iWidget *);
void        enableResizing_Widget       (iWidget *, int minWidth, const char *resizeId);
void        restoreWidth_Widget         (iWidget *);
iWidget *   makeDialog_Widget           (const char *id, const iMenuItem *itemsNullTerminated,
                                         const iMenuItem *actions, size_t numActions);
iWidget *   makeDialogButtons_Widget    (const iMenuItem *actions, size_t numActions);
iWidget *   makeTwoColumns_Widget       (iWidget **headings, iWidget **values);

iLabelWidget *dialogAcceptButton_Widget (const iWidget *);
int           dialogTransitionDir_Widget(const iWidget *);
iLabelWidget *addDialogTitle_Widget     (iWidget *, const char *text, const char *idOrNull);
iWidget      *addDialogToggle_Widget    (iWidget *headings, iWidget *values,
                                         const char *heading, const char *toggleId);
void          addDialogPadding_Widget   (iWidget *headings, iWidget *values);
void          makeTwoColumnHeading_Widget(const char *title, iWidget *headings, iWidget *values);
void          addRadioButton_Widget     (iWidget *parent, const char *id, const char *label,
                                         const char *cmd);
void          addDialogInputWithHeading_Widget(iWidget *headings, iWidget *values,
                                         const char *labelText, const char *inputId,
                                         iInputWidget *input);
void          addDialogInputWithHeadingAndFlags_Widget(iWidget *headings, iWidget *values,
                                         const char *labelText, const char *inputId,
                                         iInputWidget *input, int64_t flags);
void          addPrefsInputWithHeading_Widget(iWidget *headings, iWidget *values, const char *id,
                                         iInputWidget *input);
iLabelWidget *addDialogDropMenu_Widget  (iWidget *headings, iWidget *values, const char *title,
                                         const iMenuItem *items, size_t numItems, const char *id);
iLabelWidget *addWrappedLabel_Widget    (iWidget *, const char *text, const char *idOrNull);
iInputWidget *addTwoColumnDialogInputField_Widget(iWidget *headings, iWidget *values,
                                                  const char *labelText, const char *inputId,
                                                  iInputWidget *input);

iWidget *   makeValueInput_Widget   (iWidget *parent, const iString *initialValue, const char *title,
                                     const char *prompt, const char *acceptLabel, const char *command);
iWidget *   makeValueInputWithAdditionalActions_Widget
                                    (iWidget *parent, const iString *initialValue, const char *title,
                                     const char *prompt, const char *acceptLabel, const char *command,
                                     const iMenuItem *additionalActions, size_t numAdditionalActions);
iWidget *   makeEmbeddedValueInput_Widget
                                    (iWidget *container, const iString *initialValue,
                                     const char *prompt, const char *acceptLabel, const char *command,
                                     const iMenuItem *additionalActions, size_t numAdditionalActions);
void        updateValueInput_Widget (iWidget *, const char *title, const char *prompt);
iWidget *   makeSimpleMessage_Widget(const char *title, const char *msg);
iWidget *   makeMessage_Widget      (const char *title, const char *msg,
                                     const iMenuItem *items, size_t numItems);
iWidget *   makeQuestion_Widget     (const char *title, const char *msg,
                                     const iMenuItem *items, size_t numItems);

void        animateToRootVisibleTop_Widget  (iWidget *, uint32_t span);
iWindow *   promoteDialogToWindow_Widget    (iWidget *);
iBool       isPromoted_Widget               (iWidget *);
void        destroyDialog_Widget            (iWidget *);
