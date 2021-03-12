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

#include "widget.h"

iDeclareWidgetClass(InputWidget)
iDeclareObjectConstructionArgs(InputWidget, size_t maxLen)

enum iInputMode {
    insert_InputMode,
    overwrite_InputMode,
};

iDeclareType(InputWidgetContentPadding)

struct Impl_InputWidgetContentPadding {
    int left;
    int right;
};

void    setHint_InputWidget     (iInputWidget *, const char *hintText);
void    setMode_InputWidget     (iInputWidget *, enum iInputMode mode);
void    setMaxLen_InputWidget   (iInputWidget *, size_t maxLen);
void    setText_InputWidget     (iInputWidget *, const iString *text);
void    setTextCStr_InputWidget (iInputWidget *, const char *cstr);
void    setFont_InputWidget     (iInputWidget *, int fontId);
void    setCursor_InputWidget   (iInputWidget *, size_t pos);
void    setContentPadding_InputWidget   (iInputWidget *, int left, int right); /* only affects the text entry */
void    begin_InputWidget       (iInputWidget *);
void    end_InputWidget         (iInputWidget *, iBool accept);
void    selectAll_InputWidget   (iInputWidget *);

void    setSelectAllOnFocus_InputWidget (iInputWidget *, iBool selectAllOnFocus);
void    setSensitiveContent_InputWidget (iInputWidget *, iBool isSensitive);
void    setUrlContent_InputWidget       (iInputWidget *, iBool isUrl);
void    setNotifyEdits_InputWidget      (iInputWidget *, iBool notifyEdits);
void    setEatEscape_InputWidget        (iInputWidget *, iBool eatEscape);

const iString * text_InputWidget            (const iInputWidget *);
iInputWidgetContentPadding
                contentPadding_InputWidget  (const iInputWidget *);

iLocalDef iInputWidget *newHint_InputWidget(size_t maxLen, const char *hint) {
    iInputWidget *d = new_InputWidget(maxLen);
    setHint_InputWidget(d, hint);
    return d;
}
