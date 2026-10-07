/* Copyright 2020-2026 Jaakko Keränen <jaakko.keranen@iki.fi>
   Copyright 2026 Sidney Cammeresi <sac@cheesecake.org>

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

/* Private declarations shared by the InputWidget sources. The core is in ui/inputwidget.c,
   and the editing is done either by the custom editor (editor.c) or by a system-provided
   text control (native.c). */

#include "ui/inputwidget.h"
#include "ui/util.h"
#include "render/paint.h"
#include "render/text.h"

#if defined (iPlatformAppleMobile) || defined (iPlatformAndroidMobile)
#   include "ui/mobile.h"
#   define LAGRANGE_USE_SYSTEM_TEXT_INPUT 1 /* System-provided UI control handles almost everything. */
#else
#   include "textbuf.h"
#   define LAGRANGE_USE_SYSTEM_TEXT_INPUT 0
#endif

#if !LAGRANGE_USE_SYSTEM_TEXT_INPUT && defined (LAGRANGE_HAVE_SDL_TEXTEDITING)
#   define LAGRANGE_INPUT_PREEDIT 1 /* IME composition is shown inline */
#else
#   define LAGRANGE_INPUT_PREEDIT 0
#endif

#define minWidth_InputWidget_       (3 * gap_UI)
#define unlimitedWidth_InputWidget_ 1000000 /* TODO: WrapText disables some functionality if maxWidth==0 */
#define sensitiveChar_InputWidget_  0x25cf  /* black circle */

enum iInputWidgetFlag {
    isSensitive_InputWidgetFlag          = iBit(1),
    isUrl_InputWidgetFlag                = iBit(2), /* affected by decoding preference */
    enterPressed_InputWidgetFlag         = iBit(3),
    selectAllOnFocus_InputWidgetFlag     = iBit(4),
    notifyEdits_InputWidgetFlag          = iBit(5),
    eatEscape_InputWidgetFlag            = iBit(6),
    isMarking_InputWidgetFlag            = iBit(7),
    markWords_InputWidgetFlag            = iBit(8),
    needUpdateBuffer_InputWidgetFlag     = iBit(9),
    enterKeyEnabled_InputWidgetFlag      = iBit(10),
    lineBreaksEnabled_InputWidgetFlag    = iBit(11),
    needBackup_InputWidgetFlag           = iBit(12),
    useReturnKeyBehavior_InputWidgetFlag = iBit(13),
    dragCursor_InputWidgetFlag           = iBit(14),
    dragMarkerStart_InputWidgetFlag      = iBit(15),
    dragMarkerEnd_InputWidgetFlag        = iBit(16),
    omitDefaultSchemeIfNarrow_InputWidgetFlag = iBit(17),
    arrowFocusNavigable_InputWidgetFlag  = iBit(18),
};

enum iEventResult {
    ignored_EventResult = 0, /* event was not processed */
    false_EventResult   = 1, /* event was processed but other widgets can still process it, too*/
    true_EventResult    = 2, /* event was processed and should not be passed on */
};

#if LAGRANGE_INPUT_PREEDIT
iDeclareType(InputPreedit)

/* IME composition text, shown inline but not yet part of the document (e.g., a Korean
   syllable being assembled). Committed on Enter or when composition ends. */
struct Impl_InputPreedit {
    iString text;
    int     cursor;  /* byte offset of active segment within text */
    int     length;  /* byte length of active segment within text */
    int     advance; /* cached pixel width of text */
};
#endif

struct Impl_InputWidget {
    iWidget         widget;
    enum iInputMode mode;
    int             font;
    int             inFlags;
    size_t          maxLen;  /* characters */
    iString         srcHint;
    iString         hint;
    int             leftPadding; /* additional padding between frame and content */
    int             rightPadding;
    int             minWrapLines, maxWrapLines; /* min/max number of visible lines allowed */
    iRangei         visWrapLines; /* which wrap lines are current visible */
    iClick          click;
    int             wheelAccum;
    iTextBuf *      buffered; /* pre-rendered static text */
    iInputWidgetValidatorFunc validator;
    void *          validatorContext;
    iInputWidgetHighlighterFunc highlighter;
    void *          highlighterContext;
    iString *       backupPath;
    int             backupTimer;
    iString         oldText; /* for restoring if edits cancelled */
    int             lastUpdateWidth;
    uint32_t        lastOverflowScrollTime; /* scrolling to show focused widget */
#if LAGRANGE_USE_SYSTEM_TEXT_INPUT
    iSystemTextInput *sysCtrl;
    iString         text;
    iRangei         pendingSelectionRange;
#else
    iEditBuf        buf;
    iInt2           prevCursor;   /* previous cursor position */
    iRanges         initialMark;
    uint32_t        tapStartTime;
    uint32_t        lastTapTime;
    iInt2           lastTapPos;
    int             tapCount;
    int             cursorVis;
#   if LAGRANGE_INPUT_PREEDIT
    iInputPreedit   preedit;
#   endif
#endif
};

iLocalDef iInt2 padding_InputWidget_(void) {
    return init_I2(gap_UI / 2, gap_UI / 2);
}

iLocalDef iBool isEditing_InputWidget_(const iInputWidget *d) {
    return (flags_Widget(constAs_Widget(d)) & selected_WidgetFlag) != 0;
}

iLocalDef iBool hasNativeControl_InputWidget_(const iInputWidget *d) {
#if LAGRANGE_USE_SYSTEM_TEXT_INPUT
    return d->sysCtrl != NULL;
#else
    iUnused(d);
    return iFalse;
#endif
}

/* Core (ui/inputwidget.c). */
iRect       contentBounds_InputWidget_          (const iInputWidget *);
iWrapText   wrapParams_InputWidget_             (const iInputWidget *); /* `text` not set */
void        updateMetrics_InputWidget_          (iInputWidget *);
void        contentsWereChanged_InputWidget_    (iInputWidget *);
void        restartBackupTimer_InputWidget_     (iInputWidget *);
iBool       isAllowedToInsertNewline_InputWidget_(const iInputWidget *);
iBool       checkLineBreakMods_InputWidget_     (const iInputWidget *, int mods);
iBool       drawStatic_InputWidget_             (const iInputWidget *, iRect contentBounds,
                                                 int visLineOffsetY);

/* Editing variant (input/editor.c or input/native.c). */
void        initVariant_InputWidget_            (iInputWidget *);
void        deinitVariant_InputWidget_          (iInputWidget *);
iString *   newText_InputWidget_                (const iInputWidget *);
iString *   newVisibleText_InputWidget_         (const iInputWidget *);
iBool       isEmpty_InputWidget_                (const iInputWidget *);
iBool       isAllSelected_InputWidget_          (const iInputWidget *);
void        replaceText_InputWidget_            (iInputWidget *, const iString *text, iBool isUndoable);
int         visLineOffsetY_InputWidget_         (const iInputWidget *);
void        updateVisible_InputWidget_          (iInputWidget *);
void        updateAllLinesAndResizeHeight_InputWidget_(iInputWidget *);
int         contentHeight_InputWidget_          (const iInputWidget *);
iBool       isHeightKnown_InputWidget_          (const iInputWidget *);
void        updateTextInputRect_InputWidget_    (const iInputWidget *);
void        beginEditing_InputWidget_           (iInputWidget *, iBool allowSelectAll);
void        endEditing_InputWidget_             (iInputWidget *, iBool accept);
enum iEventResult processCommand_InputWidget_       (iInputWidget *, const SDL_Event *);
enum iEventResult processInputEvent_InputWidget_    (iInputWidget *, const SDL_Event *);
enum iEventResult processPointerEvents_InputWidget_ (iInputWidget *, const SDL_Event *);
enum iEventResult processTouchEvents_InputWidget_   (iInputWidget *, const SDL_Event *);
enum iEventResult processKeyDown_InputWidget_       (iInputWidget *, const SDL_Event *);
void        drawContent_InputWidget_            (const iInputWidget *, iPaint *, iRect bounds,
                                                 iRect contentBounds, int fg);

#if !LAGRANGE_USE_SYSTEM_TEXT_INPUT
/* Custom editor (input/editor.c). */
iInputLayout layout_InputWidget_                (const iInputWidget *);
void        insertRange_InputWidget_            (iInputWidget *, iRangecc);
iBool       deleteMarked_InputWidget_           (iInputWidget *);
void        showCursor_InputWidget_             (iInputWidget *);
#endif

/* IME composition (input/preedit.c). */
#if LAGRANGE_INPUT_PREEDIT
void        initPreedit_InputWidget_            (iInputWidget *);
void        deinitPreedit_InputWidget_          (iInputWidget *);
void        clearPreedit_InputWidget_           (iInputWidget *);
void        commitPreedit_InputWidget_          (iInputWidget *);
iBool       processTextEditing_InputWidget_     (iInputWidget *, const SDL_Event *);
void        drawPreedit_InputWidget_            (const iInputWidget *, iPaint *, iInt2 origin,
                                                 const iString *preeditLine, iInt2 endCoord);

iLocalDef iRangecc preeditRange_InputWidget_(const iInputWidget *d) {
    return range_String(&d->preedit.text);
}
iLocalDef iBool hasPreedit_InputWidget_(const iInputWidget *d) {
    return !isEmpty_String(&d->preedit.text);
}
iLocalDef int preeditAdvance_InputWidget_(const iInputWidget *d) {
    return d->preedit.advance;
}
#else
iLocalDef void initPreedit_InputWidget_(iInputWidget *d) { iUnused(d); }
iLocalDef void deinitPreedit_InputWidget_(iInputWidget *d) { iUnused(d); }
iLocalDef void clearPreedit_InputWidget_(iInputWidget *d) { iUnused(d); }
iLocalDef void commitPreedit_InputWidget_(iInputWidget *d) { iUnused(d); }
iLocalDef iBool processTextEditing_InputWidget_(iInputWidget *d, const SDL_Event *ev) {
    iUnused(d, ev);
    return iFalse;
}
iLocalDef void drawPreedit_InputWidget_(const iInputWidget *d, iPaint *p, iInt2 origin,
                                        const iString *preeditLine, iInt2 endCoord) {
    iUnused(d, p, origin, preeditLine, endCoord);
}
iLocalDef iRangecc preeditRange_InputWidget_(const iInputWidget *d) {
    iUnused(d);
    return iNullRange;
}
iLocalDef iBool hasPreedit_InputWidget_(const iInputWidget *d) {
    iUnused(d);
    return iFalse;
}
iLocalDef int preeditAdvance_InputWidget_(const iInputWidget *d) {
    iUnused(d);
    return 0;
}
#endif
