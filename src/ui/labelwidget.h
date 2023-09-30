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

/* Text label/button. */

#include "widget.h"

iDeclareWidgetClass(LabelWidget)
iDeclareObjectConstructionArgs(LabelWidget, const char *label, const char *command)

void    setAlignVisually_LabelWidget(iLabelWidget *, iBool alignVisual);
void    setNoAutoMinHeight_LabelWidget  (iLabelWidget *, iBool noAutoMinHeight);
void    setNoTopFrame_LabelWidget   (iLabelWidget *, iBool noTopFrame);
void    setNoBottomFrame_LabelWidget(iLabelWidget *, iBool noBottomFrame);
void    setChevron_LabelWidget      (iLabelWidget *, iBool chevron);
void    setCheckMark_LabelWidget    (iLabelWidget *, iBool checkMark);
void    setWrap_LabelWidget         (iLabelWidget *, iBool wrap);
void    setTruncateToFit_LabelWidget(iLabelWidget *, iBool truncateToFit);
void    setOutline_LabelWidget      (iLabelWidget *, iBool drawAsOutline);
void    setAllCaps_LabelWidget      (iLabelWidget *, iBool allCaps);
void    setRemoveTrailingColon_LabelWidget  (iLabelWidget *, iBool removeTrailingColon);
void    setMenuCanceling_LabelWidget(iLabelWidget *, iBool menuCanceling);
void    setTextOffset_LabelWidget   (iLabelWidget *, iInt2 offset);
void    setFont_LabelWidget         (iLabelWidget *, int fontId);
void    setTextColor_LabelWidget    (iLabelWidget *, int color);
void    setText_LabelWidget         (iLabelWidget *, const iString *text); /* resizes widget */
void    setTextCStr_LabelWidget     (iLabelWidget *, const char *text);
void    setCommand_LabelWidget      (iLabelWidget *, const iString *command);
void    setIcon_LabelWidget         (iLabelWidget *, iChar icon);
void    setIconColor_LabelWidget    (iLabelWidget *, int color);

iBool   checkIcon_LabelWidget       (iLabelWidget *);
void    updateSize_LabelWidget      (iLabelWidget *);
void    updateText_LabelWidget      (iLabelWidget *, const iString *text); /* not resized */
void    updateTextCStr_LabelWidget  (iLabelWidget *, const char *text); /* not resized */

void    updateTextAndResizeWidthCStr_LabelWidget    (iLabelWidget *, const char *text);

iInt2           defaultSize_LabelWidget (const iLabelWidget *);
int             font_LabelWidget        (const iLabelWidget *);
const iString * text_LabelWidget        (const iLabelWidget *);
const iString * sourceText_LabelWidget  (const iLabelWidget *); /* untranslated */
const iString * command_LabelWidget     (const iLabelWidget *);
int             textColor_LabelWidget   (const iLabelWidget *);
iChar           icon_LabelWidget        (const iLabelWidget *);
iBool           isWrapped_LabelWidget   (const iLabelWidget *);

iLabelWidget *newKeyMods_LabelWidget(const char *label, int key, int kmods, const char *command);
iLabelWidget *newColor_LabelWidget  (const char *text, int color);

iLocalDef iLabelWidget *newFont_LabelWidget(const char *label, const char *command, int fontId) {
    iLabelWidget *d = new_LabelWidget(label, command);
    checkIcon_LabelWidget(d);
    setFont_LabelWidget(d, fontId);
    return d;
}

iLocalDef iLabelWidget *newEmpty_LabelWidget(void) {
    return new_LabelWidget("", NULL);
}
iLocalDef iLabelWidget *newIcon_LabelWidget(const char *label, int key, int kmods, const char *command) {
    iLabelWidget *d = newKeyMods_LabelWidget(label, key, kmods, command);
    setAlignVisually_LabelWidget(d, iTrue);
    return d;
}
