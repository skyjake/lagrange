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

#include "dialog.h"

#include "app.h"
#include "command.h"
#include "defs.h"
#include "documentwidget.h"
#include "inputwidget.h"
#include "labelwidget.h"
#include "render/text.h"
#include "root.h"
#include "uploadwidget.h"
#include "util.h"
#include "widget.h"
#include "window.h"

#include <SDL3/SDL_clipboard.h>
#include <SDL3/SDL_timer.h>

iWidget *makeSheet_Widget(const char *id) {
    iWidget *sheet = new_Widget();
    setId_Widget(sheet, id);
    useSheetStyle_Widget(sheet);
    return sheet;
}

void useSheetStyle_Widget(iWidget *d) {
    setPadding1_Widget(d, 3 * gap_UI);
    setFrameColor_Widget(d, uiSeparator_ColorId);
    setBackgroundColor_Widget(d, uiBackground_ColorId);
    setFlags_Widget(d,
                    parentCannotResize_WidgetFlag | focusRoot_WidgetFlag | mouseModal_WidgetFlag |
                        keepOnTop_WidgetFlag | arrangeVertical_WidgetFlag | arrangeSize_WidgetFlag |
                        centerHorizontal_WidgetFlag | overflowScrollable_WidgetFlag,
                    iTrue);
}

static iLabelWidget *addDialogTitle_(iWidget *dlg, const char *text, const char *idOrNull) {
    iLabelWidget *label = new_LabelWidget(text, NULL);
    addChildFlags_Widget(dlg,
                         iClob(label),
                         alignLeft_WidgetFlag | frameless_WidgetFlag |
                             resizeToParentWidth_WidgetFlag);
    setAllCaps_LabelWidget(label, iTrue);
    setTextColor_LabelWidget(label, uiHeading_ColorId);
    if (idOrNull) {
        setId_Widget(as_Widget(label), idOrNull);
    }
    return label;
}

iLabelWidget *addWrappedLabel_Widget(iWidget *dlg, const char *text, const char *id) {
    iLabelWidget *label = addChildFlags_Widget(
        dlg,
        iClob(label = new_LabelWidget(text, NULL)),
        frameless_WidgetFlag | resizeToParentWidth_WidgetFlag | fixedHeight_WidgetFlag);
    setWrap_LabelWidget(label, iTrue);
    if (id) {
        setId_Widget(as_Widget(label), id);
    }
    return label;
}

iLabelWidget *addDialogTitle_Widget(iWidget *dlg, const char *text, const char *idOrNull) {
    return addDialogTitle_(dlg, text, idOrNull);
}

iLocalDef iBool isEmbeddedInputPrompt_Widget_(const iWidget *dlg) {
    return !isEmpty_String(&dlg->data);
}

static const iString *valueInputCommand_(const iWidget *dlg) {
    /* Embedded instances store the accept command in `data` instead of `id`. */
    return isEmbeddedInputPrompt_Widget_(dlg) ? &dlg->data : id_Widget(dlg);
}

static void clearValueInputCommand_(iWidget *dlg) {
    if (isEmbeddedInputPrompt_Widget_(dlg)) {
        clear_String(&dlg->data); /* embedded instance */
    }
    else {
        setId_Widget(dlg, "");
    }
}

static void acceptValueInput_(iWidget *dlg) {
    iInputWidget *input = findChild_Widget(dlg, "input");
    const iString *acceptCommand = valueInputCommand_(dlg);
    if (!isEmpty_String(acceptCommand)) {
        const iString *val = text_InputWidget(input);
        postCommandf_App("%s arg:%d value:%s",
                         cstr_String(acceptCommand),
                         toInt_String(val),
                         cstr_String(val));
        setBackupFileName_InputWidget(input, NULL);
    }
}

iLocalDef int metricFromIndex_(int index) {
    const int sizes[3] = { 100, 115, 130 };
    return sizes[iClamp(index, 0, iElemCount(sizes) - 1)];
}

static void updateValueInputSizing_(iWidget *dlg) {
    if (isEmbeddedInputPrompt_Widget_(dlg)) {
        /* Embedded instances get their width from the caller. */
        return;
    }
    const iBool isModal = (flags_Widget(dlg) & mouseModal_WidgetFlag) != 0;
    const iRect safeRoot = safeRect_Root(dlg->root);
    const iInt2 rootSize = safeRoot.size;
    iWidget *   title    = findChild_Widget(dlg, "valueinput.title");
    iWidget *   prompt   = findChild_Widget(dlg, "valueinput.prompt");
    if (~dlg->flags2 & horizontallyResizable_WidgetFlag2) {
        if (deviceType_App() == phone_AppDeviceType) {
            dlg->rect.size.x = rootSize.x;
        }
        else if (deviceType_App() == tablet_AppDeviceType) {
            dlg->rect.size.x = iMin(rootSize.x, rootSize.y);
        }
        else {
            dlg->rect.size.x = iMin(rootSize.x,
                                    metricFromIndex_(prefs_App()->inputZoomLevel) * gap_UI);
                                                /*title ? title->rect.size.x : 0*//*,
                                          prompt->rect.size.x);*/
        }
    }
    if (deviceType_App() != desktop_AppDeviceType) {
        dlg->minSize.y = (isModal && get_MainWindow()->keyboardHeight == 0) ? 60 * gap_UI : 0;
    }
    /* Adjust the maximum number of visible lines. */
    int footer = 6 * gap_UI;
    iWidget *buttons = findChild_Widget(dlg, "dialogbuttons");
    if (buttons && deviceType_App() == desktop_AppDeviceType) {
        footer += height_Widget(buttons);
    }
    iInputWidget *input = findChild_Widget(dlg, "input");
    setLineLimits_InputWidget(
        input,
        1,
        isModal
            ? (height_Rect(visibleRect_Root(dlg->root)) - footer - height_Widget(buttons) -
               height_Widget(prompt)) / lineHeight_Text(font_InputWidget(input))
            : 3); /* non-modal: keep the sheet small */
}

static void animateToRootVisibleBottom_(iWidget *widget, uint32_t span) {
    /* Move bottom to visible area's bottom. */
    int curY = bounds_Widget(widget).pos.y;
    int dstY = bottom_Rect(visibleRect_Root(widget->root)) - height_Widget(widget);
    widget->rect.pos.y = windowToLocal_Widget(widget, init_I2(0, dstY)).y;
    setVisualOffset_Widget(widget, curY - dstY, 0, 0);
    setVisualOffset_Widget(widget, 0, span, easeOut_AnimFlag | softer_AnimFlag);
}

void animateToRootVisibleTop_Widget(iWidget *widget, uint32_t span) {
    int curY = bounds_Widget(widget).pos.y;
    int dstY = top_Rect(visibleRect_Root(widget->root));
    widget->rect.pos.y = windowToLocal_Widget(widget, init_I2(0, dstY)).y;
    setVisualOffset_Widget(widget, curY - dstY, 0, 0);
    setVisualOffset_Widget(widget, 0, span, easeOut_AnimFlag | softer_AnimFlag);
}

int dialogTransitionDir_Widget(const iWidget *dlg) {
    if (deviceType_App() == desktop_AppDeviceType) {
        return top_TransitionDir;
    }
    return isFullSizePanel_Mobile(dlg) ? right_TransitionDir : bottom_TransitionDir;
}

iBool valueInputHandler_(iWidget *dlg, const char *cmd) {
    const iBool isSheet = !isEmbeddedInputPrompt_Widget_(dlg);
    const int transitionDir = isDesktop_Platform() &&
                                      prefs_App()->promptPosition == bottom_InputPromptPosition
                                  ? bottom_TransitionDir
                                  : dialogTransitionDir_Widget(dlg);
    iWidget *ptr = as_Widget(pointer_Command(cmd));
    if (equal_Command(cmd, "window.resized") || equal_Command(cmd, "keyboard.changed")) {
        if (isVisible_Widget(dlg)) {
            updateValueInputSizing_(dlg);
            arrange_Widget(dlg);
            if (deviceType_App() != desktop_AppDeviceType) {
                animateToRootVisibleBottom_(dlg, keyboardShowSpan_Mobile);
            }
        }
        return iFalse;
    }
    if (equalWidget_Command(cmd, dlg, "input.resized")) {
        arrange_Widget(dlg);
        arrange_Widget(dlg);
        refresh_Widget(dlg);
        if (deviceType_App() != desktop_AppDeviceType) {
            animateToRootVisibleBottom_(dlg, 100);
        }
        /* Ensure the container/page layout gets resized appropriately. */
        postCommand_Widget(dlg, "valueinput.resized");
        return iTrue;
    }
    if (equal_Command(cmd, "input.ended")) {
        if (argLabel_Command(cmd, "enter") && hasParent_Widget(ptr, dlg)) {
            const iBool accepted = arg_Command(cmd);
            if (accepted) {
                acceptValueInput_(dlg);
            }
            else {
                postCommandf_App("valueinput.cancelled id:%s", cstr_String(valueInputCommand_(dlg)));
                clearValueInputCommand_(dlg);
            }
            if (isSheet) {
                setupSheetTransition_Mobile(dlg, transitionDir);
            }
            if (isSheet || !accepted) {
                destroy_Widget(dlg);
            }
            return iTrue;
        }
        return iFalse;
    }
    else if (equalWidget_Command(cmd, dlg, "valueinput.set")) {
        iInputWidget *input = findChild_Widget(dlg, "input");
        setTextUndoableCStr_InputWidget(input, suffixPtr_Command(cmd, "text"), iTrue);
        deselect_InputWidget(input);
        validate_InputWidget(input);
        updateValueInputSizing_(dlg);
        arrange_Widget(dlg);
        if (deviceType_App() != desktop_AppDeviceType) {
            animateToRootVisibleBottom_(dlg, 100);
        }
        if (argLabel_Command(cmd, "select")) {
            selectAll_InputWidget(input);
        }
        return iTrue;
    }
    else if (equalWidget_Command(cmd, dlg, "valueinput.upload")) {
        setFocus_Widget(NULL);
        iInputWidget *input = findChild_Widget(dlg, "input");
        /* Contents of the editor are transferred via the backup file. */
        notify_Widget(input, "input.backup");
        processEvents_App(postedEventsOnly_AppEventMode); /* unfocus, save backup */
        const iString *url = collect_String(suffix_Command(cmd, "url"));
        iAssert(equalCase_Rangecc(urlScheme_String(url), "spartan"));
        iUploadWidget *upload = new_UploadWidget(spartan_UploadProtocol);
        setUrl_UploadWidget(upload, url);
        setResponseViewer_UploadWidget(upload, document_Command(cmd));
        addChild_Widget(get_Root()->widget, iClob(upload));
        setupSheetTransition_Mobile(dlg, transitionDir);
        destroy_Widget(dlg);
        return iTrue;
    }
    else if (equal_Command(cmd, "valueinput.cancel") &&
             /* A global cancel has no source widget, so it closes any open prompt. */
             (!pointer_Command(cmd) || equalWidget_Command(cmd, dlg, "valueinput.cancel"))) {
        if (!argLabel_Command(cmd, "navigating")) {
            /* A navigation-triggered cancel skips this "back" reaction to a user cancel. */
            postCommandf_App("valueinput.cancelled id:%s", cstr_String(valueInputCommand_(dlg)));
        }
        clearValueInputCommand_(dlg);
        if (isSheet) {
            setupSheetTransition_Mobile(dlg, transitionDir);
        }
        destroy_Widget(dlg);
        return iTrue;
    }
    else if (equalWidget_Command(cmd, dlg, "valueinput.accept")) {
        acceptValueInput_(dlg);
        if (isSheet) {
            setupSheetTransition_Mobile(dlg, transitionDir);
            destroy_Widget(dlg);
        }
        return iTrue;
    }
    else if (equalWidget_Command(cmd, dlg, "mouse.clicked") &&
             equal_Rangecc(range_Command(cmd, "id"), "valueinput.prompt") &&
             arg_Command(cmd) == 0 &&
             argLabel_Command(cmd, "button") == SDL_BUTTON_RIGHT) {
        /* The menu is not a child of the dialog, so the target must be explicit. */
        const iMenuItem items[] = {
            { "${menu.input.copyprompt}", 0, 0, format_CStr("valueinput.prompt.copy dlg:%p", dlg) },
        };
        openMenu_Widget(makeMenu_Widget(get_Root()->widget, items, iElemCount(items)),
                        mouseCoord_Window(get_Window(), 0));
        return iTrue;
    }
    else if (equal_Command(cmd, "valueinput.prompt.copy") &&
             pointerLabel_Command(cmd, "dlg") == dlg) {
        SDL_SetClipboardText(
            cstr_String(text_LabelWidget(findChild_Widget(dlg, "valueinput.prompt"))));
        return iTrue;
    }
    else if (equalWidget_Command(cmd, dlg, "valueinput.togglebottom")) {
        /* Top/bottom placing only applies to sheets. */
        iAssert(isSheet);
        const iBool wasBottom = prefs_App()->promptPosition == bottom_InputPromptPosition;
        dlg->rect.pos.y = 0;
        setFlags_Widget(dlg, moveToParentBottomEdge_WidgetFlag, !wasBottom);
        postCommand_Widget(dlg, "promptposition.set arg:%d",
                           wasBottom ? top_InputPromptPosition : bottom_InputPromptPosition);
        arrange_Widget(dlg);
        return iTrue;
    }
    else if (isMobile_Platform() && equal_Command(cmd, "mouse.clicked") &&
             contains_Widget(dlg, coord_Command(cmd))) {
        setFocus_Widget(findChild_Widget(dlg, "input"));
        return iTrue;
    }
    else if (isSheet && equal_Command(cmd, "mouse.clicked") && ptr == dlg &&
             flags_Widget(dlg) & mouseModal_WidgetFlag &&
             !contains_Widget(dlg, coord_Command(cmd))) {
        /* User clicked/tapped outside the sheet: clear focus and modal state, keep dialog open. */
        setFocus_Widget(NULL);
        setFlags_Widget(dlg, mouseModal_WidgetFlag, iFalse);
        updateValueInputSizing_(dlg);
        arrange_Widget(dlg);
        return iTrue;
    }
    else if (equal_Command(cmd, "focus.gained") &&
             ptr == as_Widget(findChild_Widget(dlg, "input"))) {
        if (isSheet) {
            /* Regaining focus re-establishes the modal sheet. */
            setFlags_Widget(dlg, mouseModal_WidgetFlag, iTrue);
            updateValueInputSizing_(dlg);
            arrange_Widget(dlg);
        }
        if (isMobile_Platform()) {
            /* The initial contents are selected only when the prompt is first shown. When
               refocusing later, the user usually wants to add to the existing text instead of
               replacing all of it. */
            setSelectAllOnFocus_InputWidget((iInputWidget *) ptr, iFalse);
        }
    }
    else if (isDesktop_Platform() &&
             (equal_Command(cmd, "zoom.set") || equal_Command(cmd, "zoom.delta"))) {
        /* DocumentWidget sets an ID (or `data`, for embedded instances) as the posted "accept"
           command. A configurable flag might be cleaner if this is needed elsewhere. */
        iInputWidget *input = findChild_Widget(dlg, "input");
        if (isFocused_Widget(input) /* if unfocused, zoom probably meant for document */ &&
            startsWith_String(valueInputCommand_(dlg), "!document.input.submit")) {
            int sizeIndex = prefs_App()->inputZoomLevel;
            if (equal_Command(cmd, "zoom.set")) {
                sizeIndex = 0;
            }
            else {
                sizeIndex += iSign(arg_Command(cmd));
                sizeIndex = iClamp(sizeIndex, 0, 2);
            }
            setInputZoomLevel_App(sizeIndex);
            setFont_InputWidget(input,
                                FONT_ID(default_FontId,
                                        regular_FontStyle,
                                        uiMedium_FontSize + sizeIndex));
            updateValueInputSizing_(dlg);
            arrange_Widget(dlg);
            arrange_Widget(dlg);
            refresh_Widget(dlg);
            return iTrue;
        }
        return iFalse;
    }
    return iFalse;
}

iWidget *makeDialogButtons_Widget(const iMenuItem *actions, size_t numActions) {
    iWidget *div = new_Widget();
    setId_Widget(div, "dialogbuttons");
    setFlags_Widget(div,
                    arrangeHorizontal_WidgetFlag | arrangeHeight_WidgetFlag |
                        resizeToParentWidth_WidgetFlag |
                        resizeWidthOfChildren_WidgetFlag,
                    iTrue);
    /* If there is no separator, align everything to the right. */
    iBool haveSep = iFalse;
    for (size_t i = 0; i < numActions; i++) {
        if (!iCmpStr(actions[i].label, "---")) {
            haveSep = iTrue;
            break;
        }
    }
    if (!haveSep) {
        addChildFlags_Widget(div, iClob(new_Widget()), expand_WidgetFlag);
    }
    int fonts[2] = { uiLabel_FontId, uiLabelBold_FontId };
    if (deviceType_App() != desktop_AppDeviceType) {
        fonts[0] = uiLabelBig_FontId;
        fonts[1] = uiLabelBigBold_FontId;
    }
    for (size_t i = 0; i < numActions; i++) {
        const char *label     = actions[i].label;
        const char *cmd       = actions[i].command;
        int         key       = actions[i].key;
        int         kmods     = actions[i].kmods;
        const iBool isDefault = (i == numActions - 1);
        iBool       isToggle  = iFalse;
        if (*label == '*' || *label == '&') {
            continue; /* Special value selection items for a Question dialog. */
        }
        if (startsWith_CStr(label, "```")) {
            /* Annotation. */
            iLabelWidget *annotation = addChild_Widget(div, iClob(new_LabelWidget(label + 3, NULL)));
            setTextColor_LabelWidget(annotation, uiTextAction_ColorId);
            continue;
        }
        if (!iCmpStr(label, "---")) {
            /* Separator.*/
            addChildFlags_Widget(div, iClob(new_Widget()), expand_WidgetFlag);
            continue;
        }
        if (*label == '!') {
            isToggle = iTrue;
            label++;
        }
        if (!iCmpStr(label, "${cancel}") && !cmd) {
            cmd = "cancel";
            key = SDLK_ESCAPE;
            kmods = 0;
        }
        if (isDefault) {
            if (!key) {
                key = SDLK_RETURN;
                kmods = 0;
            }
            if (label == NULL) {
                label = format_CStr(uiTextAction_ColorEscape "%s", cstr_Lang("dlg.default"));
            }
        }
        iLabelWidget *button =
            addChild_Widget(div,
                            isToggle ? iClob(makeToggle_Widget(label))
                                     : iClob(newKeyMods_LabelWidget(label, key, kmods, cmd)));
        if (isDefault) {
            setId_Widget(as_Widget(button), "default");
        }
        setFlags_Widget(as_Widget(button), alignLeft_WidgetFlag | drawKey_WidgetFlag, isDefault);
        if (key && key != SDLK_ESCAPE && deviceType_App() == desktop_AppDeviceType) {
            setFlags_Widget(as_Widget(button), alignLeft_WidgetFlag | drawKey_WidgetFlag, iTrue);
        }
        if (deviceType_App() != desktop_AppDeviceType) {
            setFlags_Widget(as_Widget(button), frameless_WidgetFlag | noBackground_WidgetFlag, iTrue);
            setTextColor_LabelWidget(button, uiTextAction_ColorId);
        }
        setFont_LabelWidget(button, isDefault ? fonts[1] : fonts[0]);
    }
    return div;
}

iWidget *makeValueInput_Widget(iWidget *parent, const iString *initialValue, const char *title,
                               const char *prompt, const char *acceptLabel,
                               const char *command){
    return makeValueInputWithAdditionalActions_Widget(
        parent, initialValue, title, prompt, acceptLabel, command, NULL, 0);
}

static iInputWidget *makeValueInputContents_(iWidget *dlg, const iString *initialValue,
                                             const char *prompt, const char *acceptLabel,
                                             const iMenuItem *additionalActions,
                                             size_t numAdditionalActions) {
    /* Value input instances can be placed either in a sheet or an embedded widget
       (inline prompt). The returned input widget is owned by `dlg`. */
    iLabelWidget *promptLabel = addWrappedLabel_Widget(dlg, prompt, "valueinput.prompt");
    setFlags_Widget(as_Widget(promptLabel), commandOnClick_WidgetFlag, iTrue);
    iInputWidget *input = addChildFlags_Widget(dlg, iClob(new_InputWidget(0)),
                                               resizeToParentWidth_WidgetFlag);
    setContentPadding_InputWidget(input, 0.5f * gap_UI, 0.5f * gap_UI);
    if (deviceType_App() == phone_AppDeviceType) {
        setFont_InputWidget(input, uiLabelBig_FontId);
        setBackgroundColor_Widget(dlg, uiBackgroundSidebar_ColorId);
        setContentPadding_InputWidget(input, gap_UI, gap_UI);
    }
    else if (isDesktop_Platform()) {
        /* The input prompt font is resizable. */
        setFont_InputWidget(input,
                            FONT_ID(default_FontId,
                                    regular_FontStyle,
                                    uiMedium_FontSize + prefs_App()->inputZoomLevel));
    }
    if (initialValue) {
        setText_InputWidget(input, initialValue);
    }
    setId_Widget(as_Widget(input), "input");
    addChild_Widget(dlg, iClob(makePadding_Widget(gap_UI)));
    /* On mobile, the actions are laid out a bit differently: buttons on top, on opposite edges. */
    iArray actions;
    init_Array(&actions, sizeof(iMenuItem));
    for (size_t i = 0; i < numAdditionalActions; i++) {
        pushBack_Array(&actions, &additionalActions[i]);
    }
    if (numAdditionalActions) {
        pushBack_Array(&actions, &(iMenuItem){ "---" });
    }
    pushBack_Array(&actions, &(iMenuItem){ "${cancel}", SDLK_ESCAPE, 0, "valueinput.cancel" });
    if (!isDesktop_Platform()) {
        pushBack_Array(&actions, &(iMenuItem){ "---" });
    }
    pushBack_Array(&actions,
                   &(iMenuItem){ acceptLabel,
                                 SDLK_RETURN,
                                 acceptKeyMod_ReturnKeyBehavior(prefs_App()->returnKey),
                                 "valueinput.accept" });
    addChildPos_Widget(
        dlg,
        iClob(makeDialogButtons_Widget(constData_Array(&actions), size_Array(&actions))),
        deviceType_App() != desktop_AppDeviceType ? front_WidgetAddPos : back_WidgetAddPos);
    deinit_Array(&actions);
    return input;
}

iWidget *makeValueInputWithAdditionalActions_Widget(iWidget *parent, const iString *initialValue,
                                                    const char *title, const char *prompt,
                                                    const char *acceptLabel, const char *command,
                                                    const iMenuItem *additionalActions,
                                                    size_t           numAdditionalActions) {
    if (parent) {
        setFocus_Widget(NULL);
    }
    iWidget *dlg = makeSheet_Widget(command);
    if (isDesktop_Platform()) {
        /* The dialog will resize itself appropriately. */
        setFlags_Widget(dlg, overflowScrollable_WidgetFlag, iFalse);
    }
    setFlags_Widget(dlg, commandOnClick_WidgetFlag, iTrue); /* for toggling modality */
    setCommandHandler_Widget(dlg, valueInputHandler_);
    if (parent) {
        addChildFlags_Widget(parent,
                             iClob(dlg),
                             isDesktop_Platform() &&
                                     prefs_App()->promptPosition == bottom_InputPromptPosition
                                 ? moveToParentBottomEdge_WidgetFlag
                                 : 0);
    }
    if (deviceType_App() == desktop_AppDeviceType) { /* conserve space on mobile */
        addDialogTitle_(dlg, title, "valueinput.title");
    }
    iInputWidget *input = makeValueInputContents_(dlg, initialValue, prompt, acceptLabel,
                                                  additionalActions, numAdditionalActions);
    arrange_Widget(dlg);
    if (parent) {
        setFocus_Widget(as_Widget(input));
    }
    /* Check that the top is in the safe area. */
    if (deviceType_App() != desktop_AppDeviceType) {
        dlg->rect.pos.y =
            windowToLocal_Widget(
                dlg, init_I2(0, bottom_Rect(visibleRect_Root(dlg->root)) - dlg->rect.size.y))
                .y;
        setFlags_Widget(dlg, drawBackgroundToBottom_WidgetFlag, iTrue);
    }
    updateValueInputSizing_(dlg);
    enableResizing_Widget(dlg, width_Widget(dlg), "input");
    setupSheetTransition_Mobile(dlg,
                                incoming_TransitionFlag |
                                    (isDesktop_Platform() &&
                                            prefs_App()->promptPosition == bottom_InputPromptPosition
                                         ? bottom_TransitionDir
                                         : dialogTransitionDir_Widget(dlg)));
    return dlg;
}

iWidget *makeEmbeddedValueInput_Widget(iWidget *container, const iString *initialValue,
                                       const char *prompt, const char *acceptLabel,
                                       const char *command, const iMenuItem *additionalActions,
                                       size_t numAdditionalActions) {
    iWidget *dlg = new_Widget();
    setBackgroundColor_Widget(dlg, uiBackground_ColorId);
    setFrameColor_Widget(dlg, uiSeparator_ColorId);
    if (isTerminal_Platform()) {
        const int vPad = gap_UI / aspect_UI;
        const int hPad = 2 * gap_UI / aspect_UI;
        setPadding_Widget(dlg, hPad, vPad, hPad, vPad); /* FIXME: possible aspect_UI bug in here */
    }
    else {
        setPadding1_Widget(dlg, 2 * gap_UI);
    }
    setFlags_Widget(dlg,
                    arrangeVertical_WidgetFlag | arrangeHeight_WidgetFlag |
                        fixedPosition_WidgetFlag | resizeWidthOfChildren_WidgetFlag,
                    iTrue);
    /* There may be multiple inline prompts open on a page so we need to find a specific one.
       Therefore, the accept command goes into `data`, leaving `id` for lookups. */
    setCStr_String(&dlg->data, command);
    setCommandHandler_Widget(dlg, valueInputHandler_);
    makeValueInputContents_(
        dlg, initialValue, prompt, acceptLabel, additionalActions, numAdditionalActions);
    addChild_Widget(container, iClob(dlg));
    arrange_Widget(dlg);
    return dlg;
}

void updateValueInput_Widget(iWidget *d, const char *title, const char *prompt) {
    setTextCStr_LabelWidget(findChild_Widget(d, "valueinput.title"), title);
    setTextCStr_LabelWidget(findChild_Widget(d, "valueinput.prompt"), prompt);
    updateValueInputSizing_(d);
}

static void updateQuestionWidth_(iWidget *dlg) {
    iWidget *title = findChild_Widget(dlg, "question.title");
    iWidget *msg   = findChild_Widget(dlg, "question.msg");
    if (title && msg) {
        const iRect safeRoot = safeRect_Root(dlg->root);
        const iInt2 rootSize = safeRoot.size;
        const int padding = 6 * gap_UI;
        dlg->rect.size.x =
            iMin(iMin(150 * gap_UI, rootSize.x),
                 iMaxi(iMaxi(100 * gap_UI, padding + title->rect.size.x),
                       padding + msg->rect.size.x));
    }
}

static iBool messageHandler_(iWidget *msg, const char *cmd) {
    /* Almost any command dismisses the sheet. */
    if (!(isNotification_Command(cmd) ||
          equal_Command(cmd, "document.reload") || /* may also be a user action */
          equal_Command(cmd, "document.linkkeys") ||
          equal_Command(cmd, "theme.changed") ||
          equal_Command(cmd, "focus.default") ||
          equal_Command(cmd, "menu.open") ||
          equal_Command(cmd, "menu.opened") ||
          equal_Command(cmd, "menu.cancel") ||
          equal_Command(cmd, "mouse.missed") ||
          equal_Command(cmd, "server.copycert") ||
          startsWith_Command(cmd, "cancel menu:") ||
          startsWith_Command(cmd, "window."))) {
#ifndef NDEBUG
        printf("message dismissed by: %s\n", cmd); fflush(stdout);
#endif
        // SDL_Delay(5000);
        setupSheetTransition_Mobile(msg, dialogTransitionDir_Widget(msg));
        destroy_Widget(msg);
    }
    else if (equal_Command(cmd, "window.resized")) {
        updateQuestionWidth_(msg);
    }
    return iFalse;
}

iWidget *makeSimpleMessage_Widget(const char *title, const char *msg) {
    return makeMessage_Widget(title,
                              msg,
                              (iMenuItem[]){ { "${dlg.message.ok}", 0, 0, "message.ok" } },
                              1);
}

iWidget *makeMessage_Widget(const char *title, const char *msg, const iMenuItem *items,
                            size_t numItems) {
    iWidget *dlg = makeQuestion_Widget(title, msg, items, numItems);
    addAction_Widget(dlg, SDLK_ESCAPE, 0, "message.ok");
    addAction_Widget(dlg, SDLK_SPACE, 0, "message.ok");
    return dlg;
}

iWidget *makeQuestion_Widget(const char *title, const char *msg,
                             const iMenuItem *items, size_t numItems) {
    processEvents_App(postedEventsOnly_AppEventMode);
    if (isUsingPanelLayout_Mobile()) {
        iArray *panelItems = collectNew_Array(sizeof(iMenuItem));
        pushBackN_Array(panelItems, (iMenuItem[]){
            { format_CStr("title text:%s", title) },
            { format_CStr("label text:%s", msg) },
            { NULL }
        }, 3);
        for (size_t i = 0; i < numItems; i++) {
            const iMenuItem *item = &items[i];
            const char first = item->label[0];
            if (first == '*' || first == '&') {
                insert_Array(panelItems, size_Array(panelItems) - 1,
                             &(iMenuItem){ format_CStr("button selected:%d text:%s",
                                                       first == '&' ? 1 : 0, item->label + 1),
                                           0, 0, item->command });
            }
        }
        iWidget *dlg = makePanels_Mobile("", data_Array(panelItems), items, numItems);
        setCommandHandler_Widget(dlg, messageHandler_);
        setupSheetTransition_Mobile(dlg, incoming_TransitionFlag | dialogTransitionDir_Widget(dlg));
        return dlg;
    }
    iWidget *dlg = makeSheet_Widget("");
    setCommandHandler_Widget(dlg, messageHandler_);
    addDialogTitle_(dlg, title, "question.title");
    iLabelWidget *msgLabel = addWrappedLabel_Widget(dlg, msg, "question.msg");
    /* Check for value selections. */
    for (size_t i = 0; i < numItems; i++) {
        const iMenuItem *item = &items[i];
        const char first = item->label[0];
        if (first == '*' || first == '&') {
            iLabelWidget *option =
                addChildFlags_Widget(dlg,
                                 iClob(newKeyMods_LabelWidget(item->label + 1,
                                                              item->key,
                                                              item->kmods,
                                                              item->command)),
                                 resizeToParentWidth_WidgetFlag |
                                 (first == '&' ? selected_WidgetFlag : 0));
            if (deviceType_App() != desktop_AppDeviceType) {
                setFont_LabelWidget(option, uiLabelBig_FontId);
            }
        }
    }
    addChild_Widget(dlg, iClob(makePadding_Widget(gap_UI)));
    addChild_Widget(dlg, iClob(makeDialogButtons_Widget(items, numItems)));
    addChild_Widget(dlg->root->widget, iClob(dlg));
    updateQuestionWidth_(dlg);
    class_Widget(as_Widget(msgLabel))->sizeChanged(as_Widget(msgLabel));
    arrange_Widget(dlg); /* BUG: This extra arrange shouldn't be needed but the dialog won't
                            be arranged correctly unless it's here. */
    setupSheetTransition_Mobile(dlg, incoming_TransitionFlag | top_TransitionDir);
    /* If this prompt is opened as a result of a context menu action, the menu
       will switch keyboard focus back to its owner when it closes. This would
       leave keyboard focus outside the dialog's focus root. */
    postCommand_Root(dlg->root, "focus.default");
    return dlg;
}

iWidget *makeTwoColumns_Widget(iWidget **headings, iWidget **values) {
    iWidget *page = new_Widget();
    setFlags_Widget(page, arrangeHorizontal_WidgetFlag | arrangeSize_WidgetFlag, iTrue);
    *headings = addChildFlags_Widget(
        page, iClob(new_Widget()), arrangeVertical_WidgetFlag | arrangeSize_WidgetFlag);
    *values = addChildFlags_Widget(
        page, iClob(new_Widget()), arrangeVertical_WidgetFlag | arrangeSize_WidgetFlag);
    return page;
}

iLabelWidget *dialogAcceptButton_Widget(const iWidget *d) {
    iWidget *buttonParent = findChild_Widget(d, "dialogbuttons");
    if (!buttonParent) {
        iAssert(isUsingPanelLayout_Mobile());
        buttonParent = findChild_Widget(d, "navi.actions");
    }
    return (iLabelWidget *) lastChild_Widget(buttonParent);
}

static void addDialogPaddingSize_(iWidget *headings, iWidget *values, int size) {
    addChild_Widget(headings, iClob(makePadding_Widget(size)));
    addChild_Widget(values,   iClob(makePadding_Widget(size)));
}

void addDialogPadding_Widget(iWidget *headings, iWidget *values) {
    addDialogPaddingSize_(headings, values, iMaxi(1, lineHeight_Text(uiLabel_FontId) * 3 / 4));
}

void makeTwoColumnHeading_Widget(const char *title, iWidget *headings, iWidget *values) {
    if (childCount_Widget(headings)) {
        if (isTerminal_Platform()) {
            addDialogPadding_Widget(headings, values);
        }
        else {
            addDialogPaddingSize_(headings, values, 2 * gap_UI);
        }
    }
    setFont_LabelWidget(addChildFlags_Widget(headings,
                                             iClob(makeHeading_Widget(
                                                 format_CStr(uiHeading_ColorEscape "%s", title))),
                                             ignoreForParentWidth_WidgetFlag),
                        uiLabelBold_FontId);
    addChild_Widget(values, iClob(makeHeading_Widget("")));
}

void addRadioButton_Widget(iWidget *parent, const char *id, const char *label, const char *cmd) {
    setId_Widget(
        addChildFlags_Widget(parent, iClob(new_LabelWidget(label, cmd)), radio_WidgetFlag),
        id);
}

void addDialogInputWithHeadingAndFlags_Widget(iWidget *headings, iWidget *values,
                                               const char *labelText, const char *inputId,
                                               iInputWidget *input, int64_t flags) {
    iLabelWidget *head = addChild_Widget(headings, iClob(makeHeading_Widget(labelText)));
    if (isMobile_Platform()) {
        /* On mobile, inputs have 2 gaps of extra padding. */
        setFixedSize_Widget(as_Widget(head), init_I2(-1, height_Widget(input)));
        setPadding_Widget(as_Widget(head), 0, gap_UI, 0, 0);
    }
    setId_Widget(addChild_Widget(values, input), inputId);
    if (deviceType_App() != phone_AppDeviceType) {
        /* Ensure that the label has the same height as the input widget. */
        as_Widget(head)->sizeRef = as_Widget(input);
    }
    setFlags_Widget(as_Widget(head), flags, iTrue);
    setFlags_Widget(as_Widget(input), flags, iTrue);
}

void addDialogInputWithHeading_Widget(iWidget *headings, iWidget *values, const char *labelText,
                                       const char *inputId, iInputWidget *input) {
    addDialogInputWithHeadingAndFlags_Widget(headings, values, labelText, inputId, input, 0);
}

iInputWidget *addTwoColumnDialogInputField_Widget(iWidget *headings, iWidget *values,
                                                  const char *labelText, const char *inputId,
                                                  iInputWidget *input) {
    addDialogInputWithHeading_Widget(headings, values, labelText, inputId, input);
    return input;
}

void addPrefsInputWithHeading_Widget(iWidget *headings, iWidget *values,
                                      const char *id, iInputWidget *input) {
    addDialogInputWithHeading_Widget(headings, values, format_CStr("${%s}", id), id, input);
}

iWidget *addDialogToggle_Widget(iWidget *headings, iWidget *values,
                                const char *heading, const char *toggleId) {
    iWidget *toggle;
    addChild_Widget(headings, iClob(makeHeading_Widget(heading)));
    addChild_Widget(values, toggle = iClob(makeToggle_Widget(toggleId)));
    return toggle;
}

iLabelWidget *addDialogDropMenu_Widget(iWidget *headings, iWidget *values, const char *title,
                                        const iMenuItem *items, size_t numItems, const char *id) {
    addChild_Widget(headings, iClob(makeHeading_Widget(title)));
    iLabelWidget *button = makeMenuButton_LabelWidget(
        items[findWidestLabel_MenuItem(items, numItems)].label, items, numItems);
    setBackgroundColor_Widget(findChild_Widget(as_Widget(button), "menu"),
                              uiBackgroundMenu_ColorId);
    setId_Widget(addChildFlags_Widget(values, iClob(button), alignLeft_WidgetFlag), id);
    return button;
}

void enableResizing_Widget(iWidget *d, int minWidth, const char *resizeId) {
    if (isTerminal_Platform()) {
        return; /* cannot grab edges */
    }
    if (deviceType_App() == desktop_AppDeviceType) {
        iChangeFlags(d->flags, arrangeWidth_WidgetFlag, iFalse);
        d->flags2 |= horizontallyResizable_WidgetFlag2;
        d->minSize.x = minWidth;
        if (resizeId) {
            setCStr_String(&d->resizeId, resizeId);
            restoreWidth_Widget(d);
        }
    }
}

void restoreWidth_Widget(iWidget *d) {
    if (!isDesktop_Platform() || isEmpty_String(&d->resizeId)) {
        return;
    }
    float saved;
    if (checkSavedWidth_App(&d->resizeId, &saved)) {
        iAssert(parent_Widget(d));
        applyInteractiveResize_Widget(d, iMini(width_Widget(parent_Widget(d)), saved * gap_UI));
    }
    else {
        arrange_Widget(d);
    }
}

iWidget *makeDialog_Widget(const char *id,
                           const iMenuItem *itemsNullTerminated,
                           const iMenuItem *actions, size_t numActions) {
    iWidget *dlg = makeSheet_Widget(id);
    /* TODO: Construct desktop dialogs using NULL-terminated item arrays, like mobile panels. */
    addChild_Widget(dlg, iClob(makePadding_Widget(gap_UI)));
    addChild_Widget(dlg, iClob(makeDialogButtons_Widget(actions, numActions)));
    addChild_Widget(dlg->root->widget, iClob(dlg));
    arrange_Widget(dlg);
    setupSheetTransition_Mobile(dlg, incoming_TransitionFlag | top_TransitionDir);
    return dlg;
}

iWindow *promoteDialogToWindow_Widget(iWidget *dlg) {
    arrange_Widget(dlg);
    removeChild_Widget(parent_Widget(dlg), dlg);
    setVisualOffset_Widget(dlg, 0, 0, 0);
    setFlags_Widget(dlg, horizontalOffset_WidgetFlag | visualOffset_WidgetFlag |
                    centerHorizontal_WidgetFlag | overflowScrollable_WidgetFlag, iFalse);
    /* Check for a dialog heading. */
    iWidget *child = child_Widget(dlg, 0);
    iWindow *x = newExtra_Window(dlg);
    if (isInstance_Object(child, &Class_LabelWidget)) {
        iLabelWidget *heading = (iLabelWidget *) child;
        iString *title = copy_String(sourceText_LabelWidget(heading));
        translate_Lang(title);
        setTitle_Window(x, title);
        delete_String(title);
        iRelease(removeChild_Widget(dlg, heading));
        arrange_Widget(dlg);
    }
    // setFlags_Widget(dlg, resizeChildren_WidgetFlag, iTrue);
    const float pixelRatio = x->pixelRatio;
    SDL_SetWindowMinimumSize(
        x->win, width_Widget(dlg) / pixelRatio, height_Widget(dlg) / pixelRatio);
    addExtraWindow_App(x);
    SDL_ShowWindow(x->win);
    SDL_RaiseWindow(x->win);
    setActiveWindow_App(x);
    return x;
}

iBool isPromoted_Widget(iWidget *dlg) {
    return type_Window(window_Widget(dlg)) == extra_WindowType;
}

void destroyDialog_Widget(iWidget *dlg) {
    if (isPromoted_Widget(dlg)) {
        if (~dlg->flags & destroyPending_WidgetFlag) {
            /* Let the dialog know that the end is nigh. Otherwise, its dismissal command
               will be automatically posted. */
            setFlags_Widget(dlg, destroyPending_WidgetFlag, iTrue);
            closeWindow_App(window_Widget(dlg));
        }
    }
    else {
        destroy_Widget(dlg);
    }
}
