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
void    setAllCaps_LabelWidget      (iLabelWidget *, iBool allCaps);
void    setCheckMark_LabelWidget    (iLabelWidget *, iBool checkMark);
void    setChevron_LabelWidget      (iLabelWidget *, iBool chevron);
void    setCommand_LabelWidget      (iLabelWidget *, const iString *command);
void    setFont_LabelWidget         (iLabelWidget *, int fontId);
void    setIconColor_LabelWidget    (iLabelWidget *, int color);
void    setIcon_LabelWidget         (iLabelWidget *, iChar icon);
void    setKeyTrigger_LabelWidget   (iLabelWidget *, iBool keyTrigger); /* false: owner calls `triggerKey` */
void    setMenuCanceling_LabelWidget(iLabelWidget *, iBool menuCanceling);
void    setNoAutoMinHeight_LabelWidget(iLabelWidget *, iBool noAutoMinHeight);
void    setParentTextColor_LabelWidget(iLabelWidget *, iBool parentTextColor);
void    setNoBottomFrame_LabelWidget(iLabelWidget *, iBool noBottomFrame);
void    setNoTopFrame_LabelWidget   (iLabelWidget *, iBool noTopFrame);
void    setRemoveTrailingColon_LabelWidget(iLabelWidget *, iBool removeTrailingColon);
void    setTitleCase_LabelWidget    (iLabelWidget *, iBool titleCase); /* English only */
void    setTextCStr_LabelWidget     (iLabelWidget *, const char *text);
void    setTextColor_LabelWidget    (iLabelWidget *, int color);
void    setTextOffset_LabelWidget   (iLabelWidget *, iInt2 offset);
void    setText_LabelWidget         (iLabelWidget *, const iString *text); /* resizes widget */
void    setTranslation_LabelWidget  (iLabelWidget *, iBool translationEnabled);
void    setTruncateToFit_LabelWidget(iLabelWidget *, iBool truncateToFit);
void    setWrap_LabelWidget         (iLabelWidget *, iBool wrap);
void    setCenterWrapped_LabelWidget(iLabelWidget *, iBool centerWrapped); /* each wrapped line is centered */

iBool   checkIcon_LabelWidget       (iLabelWidget *);
iBool   triggerKey_LabelWidget      (iLabelWidget *, int key, int kmods); /* true if the key matches */
void    updateSize_LabelWidget      (iLabelWidget *);
void    updateWrappedHeight_LabelWidget(iLabelWidget *); /* fixedHeight: fit text to current width */
void    updateText_LabelWidget      (iLabelWidget *, const iString *text); /* not resized */
void    updateTextCStr_LabelWidget  (iLabelWidget *, const char *text); /* not resized */

void    updateTextAndResizeWidthCStr_LabelWidget    (iLabelWidget *, const char *text);
iBool   updateFont_LabelWidget      (iLabelWidget *, int fontId);
void    updateFontStyle_LabelWidget (iLabelWidget *, int fontStyle);

iInt2           defaultSize_LabelWidget (const iLabelWidget *);
int             font_LabelWidget        (const iLabelWidget *);
const iString * text_LabelWidget        (const iLabelWidget *);
const iString * sourceText_LabelWidget  (const iLabelWidget *); /* untranslated */
const iString * command_LabelWidget     (const iLabelWidget *);
int             textColor_LabelWidget   (const iLabelWidget *);
iChar           icon_LabelWidget        (const iLabelWidget *);
int             textOffset_LabelWidget  (const iLabelWidget *); /* left edge of the text */
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
