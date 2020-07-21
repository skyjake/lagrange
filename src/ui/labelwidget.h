#pragma once

/* Text label/button. */

#include "widget.h"

iDeclareWidgetClass(LabelWidget)
iDeclareObjectConstructionArgs(LabelWidget, const char *label, int key, int kmods, const char *command)

iLocalDef iLabelWidget *newEmpty_LabelWidget(void) {
    return new_LabelWidget("", 0, 0, NULL);
}

void    setFont_LabelWidget         (iLabelWidget *, int fontId);
void    setText_LabelWidget         (iLabelWidget *, const iString *text); /* resizes widget */
void    setTextCStr_LabelWidget     (iLabelWidget *, const char *text);

void    updateSize_LabelWidget      (iLabelWidget *);
void    updateText_LabelWidget      (iLabelWidget *, const iString *text); /* not resized */
void    updateTextCStr_LabelWidget  (iLabelWidget *, const char *text); /* not resized */

const iString *command_LabelWidget  (const iLabelWidget *);
