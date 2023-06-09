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

#include "labelwidget.h"
#include "text.h"
#include "defs.h"
#include "color.h"
#include "paint.h"
#include "app.h"
#include "util.h"
#include "keys.h"
#include "touch.h"

#include <SDL_version.h>

struct Impl_LabelWidget {
    iWidget widget;
    iString srcLabel;
    iString label;
    iInt2   labelOffset;
    int     font;
    int     key;
    int     kmods;
    iChar   icon;
    int     forceFg;
    int     iconColor;
    iString command;
    iClick  click;
    struct {
        uint16_t alignVisual         : 1; /* align according to visible bounds, not font metrics */
        uint16_t noAutoMinHeight     : 1; /* minimum height is not set automatically */
        uint16_t drawAsOutline       : 1; /* draw as outline, filled with background color */
        uint16_t noTopFrame          : 1;
        uint16_t noBottomFrame       : 1;
        uint16_t wrap                : 1;
        uint16_t allCaps             : 1;
        uint16_t removeTrailingColon : 1;
        uint16_t chevron             : 1;        
        uint16_t checkMark           : 1;
        uint16_t truncateToFit       : 1;
    } flags;
};

static iBool isHover_LabelWidget_(const iLabelWidget *d) {
    if (isMobile_Platform() && !isHovering_Touch()) {
        return iFalse;
    }
    return isHover_Widget(d);
}

static iInt2 padding_LabelWidget_(const iLabelWidget *d, int corner) {
    const iWidget *w = constAs_Widget(d);
    const int64_t flags = flags_Widget(w);
    const iInt2 widgetPad = (corner   == 0 ? init_I2(w->padding[0], w->padding[1])
                             : corner == 1 ? init_I2(w->padding[2], w->padding[1])
                             : corner == 2 ? init_I2(w->padding[2], w->padding[3])
                             : init_I2(w->padding[0], w->padding[3]));
    if (isMobile_Platform()) {
        return add_I2(widgetPad,
                      init_I2(flags & tight_WidgetFlag ? 2 * gap_UI : (4 * gap_UI),
                              (flags & extraPadding_WidgetFlag ? 1.5f : 1.0f) * 3 * gap_UI / 2));
    }
    return add_I2(widgetPad,
                  init_I2(flags & tight_WidgetFlag ? 3 * gap_UI / 2 : (3 * gap_UI),
                          gap_UI * aspect_UI));
}

iDefineObjectConstructionArgs(LabelWidget,
                              (const char *label, const char *cmd),
                              label, cmd)

static iBool checkModifiers_(int have, int req) {
    return keyMods_Sym(req) == keyMods_Sym(have);
}

static void trigger_LabelWidget_(const iLabelWidget *d) {
    const iWidget *w = constAs_Widget(d);
    postCommand_Widget(&d->widget, "%s", cstr_String(&d->command));
    if (flags_Widget(w) & radio_WidgetFlag) {
        iForEach(ObjectList, i, children_Widget(w->parent)) {
            setFlags_Widget(i.object, selected_WidgetFlag, d == i.object);
        }
    }
}

static void updateKey_LabelWidget_(iLabelWidget *d) {
    if (!isEmpty_String(&d->command)) {
        const iBinding *bind = findCommand_Keys(cstr_String(&d->command));
        if (bind && bind->id < builtIn_BindingId) {
            d->key   = bind->key;
            d->kmods = bind->mods;
        }
    }
}

static void endSiblingOrderDrag_LabelWidget_(iLabelWidget *d) {
    iWidget *w = as_Widget(d);
    if ((w->flags2 & siblingOrderDraggable_WidgetFlag2) && (flags_Widget(w) & dragged_WidgetFlag)) {
        float dragAmount = (float) w->visualOffset.to / (float) width_Widget(w);
        if (dragAmount > -0.5f && dragAmount < -0.1f) {
            dragAmount = -0.5f;
        }
        else if (dragAmount < 0.5f && dragAmount > 0.1f) {
            dragAmount = 0.5f;
        }
        postCommand_Widget(w, "tabs.move arg:%d dragged:1", iRound(dragAmount));
        setVisualOffset_Widget(w, 0, 0, 0);
        setFlags_Widget(w, dragged_WidgetFlag | keepOnTop_WidgetFlag, iFalse);
    }
}

static iBool processEvent_LabelWidget_(iLabelWidget *d, const SDL_Event *ev) {
    iWidget *w = &d->widget;
    if (isMetricsChange_UserEvent(ev)) {
        updateSize_LabelWidget(d);
    }
    else if (isCommand_UserEvent(ev, "lang.changed")) {
        const iChar oldIcon = d->icon; /* icon will be retained */
        setText_LabelWidget(d, &d->srcLabel);
        checkIcon_LabelWidget(d); /* strip it */
        d->icon = oldIcon;
        return iFalse;
    }
    else if (isCommand_UserEvent(ev, "bindings.changed")) {
        /* Update the key used to trigger this label. */
        updateKey_LabelWidget_(d);
        return iFalse;
    }
    else if (isCommand_UserEvent(ev, "cancel")) {
        if (flags_Widget(w) & pressed_WidgetFlag) {
            setFlags_Widget(w, pressed_WidgetFlag, iFalse);
            endSiblingOrderDrag_LabelWidget_(d);
            refresh_Widget(w);
        }
        return iFalse;
    }
    else if (isCommand_Widget(w, ev, "focus.gained") ||
             isCommand_Widget(w, ev, "focus.lost")) {
        iWidget *scr = findOverflowScrollable_Widget(w);
        if (scr) {
            const iRect root   = visibleRect_Root(w->root);
            const iRect bounds = boundsWithoutVisualOffset_Widget(w);
            int delta = top_Rect(root) - top_Rect(bounds);
            if (delta > 0) {
                scrollOverflow_Widget(scr, delta);
            }
            else {
                delta = bottom_Rect(bounds) - bottom_Rect(root);
                if (delta > 0) {
                    scrollOverflow_Widget(scr, -delta);
                }
            }
        }
        refresh_Widget(d);
        return iFalse;
    }
    else if (isCommand_Widget(w, ev, "trigger")) {
        trigger_LabelWidget_(d);
        return iTrue;
    }
    if (!isEmpty_String(&d->command)) {
#if 0 && defined (iPlatformAppleMobile)
        /* Touch allows activating any button on release. */
        switch (ev->type) {
            case SDL_MOUSEBUTTONUP: {
                const iInt2 mouse = init_I2(ev->button.x, ev->button.y);
                if (contains_Widget(w, mouse)) {
                    trigger_LabelWidget_(d);
                    refresh_Widget(w);
                }
                break;
            }
        }
#endif
        switch (processEvent_Click(&d->click, ev)) {
            case started_ClickResult:
                setFlags_Widget(w, pressed_WidgetFlag, iTrue);
                refresh_Widget(w);
                return iTrue;
            case drag_ClickResult:
                if (w->flags2 & siblingOrderDraggable_WidgetFlag2) {
                    setFlags_Widget(w, dragged_WidgetFlag | keepOnTop_WidgetFlag, iTrue);
                    setVisualOffset_Widget(w, delta_Click(&d->click).x, 0, 0);
                    refresh_Widget(w);
                    return iTrue;
                }
                break;
            case aborted_ClickResult:
                setFlags_Widget(w, pressed_WidgetFlag, iFalse);
                endSiblingOrderDrag_LabelWidget_(d);
                refresh_Widget(w);
                return iTrue;
//            case double_ClickResult:
            case finished_ClickResult:
                setFlags_Widget(w, pressed_WidgetFlag, iFalse);
                endSiblingOrderDrag_LabelWidget_(d);
                trigger_LabelWidget_(d);
                refresh_Widget(w);
                setFocus_Widget(NULL);
                return iTrue;
            default:
                break;
        }
        switch (ev->type) {
            case SDL_KEYDOWN: {
                const int mods = ev->key.keysym.mod;
                const int sym  = ev->key.keysym.sym;
                if (d->key && sym == d->key && checkModifiers_(mods, d->kmods)) {
                    trigger_LabelWidget_(d);
                    return iTrue;
                }
                if (isFocused_Widget(d) && mods == 0 &&
                    (sym == SDLK_RETURN || sym == SDLK_KP_ENTER)) {
                    trigger_LabelWidget_(d);
                    refresh_Widget(d);
                    return iTrue;
                }
                break;
            }
        }
    }
    return processEvent_Widget(&d->widget, ev);
}

static void keyStr_LabelWidget_(const iLabelWidget *d, iString *str) {
    toString_Sym(d->key, d->kmods, str);
}

static iBool areTabButtonsThemeColored_(void) {
    if (type_Window(get_Window()) != main_WindowType) {
        /* Only the main window has themes available, since it has DocumentWidgets. */
        return iFalse;
    }
    const enum iGmDocumentTheme docTheme = docTheme_Prefs(prefs_App());
    const iBool isDarkUI = isDark_ColorTheme(colorTheme_App());
    return (docTheme == colorfulLight_GmDocumentTheme ||
            docTheme == sepia_GmDocumentTheme ||
            (docTheme == oceanic_GmDocumentTheme && !isDarkUI));
}

static void getColors_LabelWidget_(const iLabelWidget *d, int *bg, int *fg, int *frame1, int *frame2,
                                   int *icon, int *meta) {
    const iWidget *w           = constAs_Widget(d);
    const int64_t  flags       = flags_Widget(w);
    const iBool    isHover     = isHover_LabelWidget_(d);
    const iBool    isFocus     = (flags & focusable_WidgetFlag && isFocused_Widget(d));
    const iBool    isPress     = (flags & pressed_WidgetFlag) != 0;
    const iBool    isSel       = (flags & selected_WidgetFlag) != 0;
    const iBool    isFrameless = (flags & frameless_WidgetFlag) != 0;
    const iBool    isButton    = d->click.buttons != 0;
    const iBool    isMenuItem  = !cmp_String(id_Widget(parent_Widget(d)), "menu");
    const iBool    isKeyRoot   = (w->root == get_Window()->keyRoot);
    const iBool    isDarkTheme = isDark_ColorTheme(colorTheme_App());
    /* Default color state. */
    *bg     = isButton && ~flags & noBackground_WidgetFlag ? (d->widget.bgColor != none_ColorId ?
                                                              d->widget.bgColor : uiBackground_ColorId)
                                                           : none_ColorId;
    if (d->flags.checkMark) {
        *bg = none_ColorId;
    }
    *fg     = uiText_ColorId;
    *frame1 = isButton ? uiEmboss1_ColorId : d->widget.frameColor;
    *frame2 = isButton ? uiEmboss2_ColorId : *frame1;
    *icon   = uiIcon_ColorId;
    *meta   = uiTextShortcut_ColorId;
    if (flags & disabled_WidgetFlag && isButton) {
        *icon = uiTextDisabled_ColorId;
        *fg   = uiTextDisabled_ColorId;
        *meta = uiTextDisabled_ColorId;
    }
    iBool isThemeBackground = iFalse;
    if (isSel) {
        if (!d->flags.checkMark) {
            if (isMenuItem) {
                *bg = uiBackgroundUnfocusedSelection_ColorId;
            }
            else {
                const enum iGmDocumentTheme docTheme = docTheme_Prefs(prefs_App());
                if (areTabButtonsThemeColored_() &&
                    !cmp_String(&d->widget.parent->id, "tabs.buttons")) {
                    *bg = (docTheme == oceanic_GmDocumentTheme ||
                                   (docTheme == sepia_GmDocumentTheme &&
                                    colorTheme_App() == pureWhite_ColorTheme)
                               ? tmBackground_ColorId
                               : tmBannerBackground_ColorId);
                    isThemeBackground = iTrue;
                    /* Ensure visibility in case the background matches UI background. */
                    if (delta_Color(get_Color(*bg), get_Color(uiBackground_ColorId)) < 30) {
                        *bg = uiBackgroundSelected_ColorId;
                        isThemeBackground = iFalse;
                    }
                }
                else {
                    *bg = uiBackgroundSelected_ColorId;
                }
            }
            if (!isKeyRoot) {
                *bg = isDark_ColorTheme(colorTheme_App()) ? uiBackgroundUnfocusedSelection_ColorId
                                                          : uiMarked_ColorId;
            }
        }
        *fg = uiTextSelected_ColorId;
        if (isThemeBackground) {
            *fg = tmParagraph_ColorId;
        }
        if (isButton) {
            *frame1 = d->flags.noTopFrame    ? uiEmboss1_ColorId : uiEmbossSelected1_ColorId;
            *frame2 = d->flags.noBottomFrame ? uiEmboss2_ColorId : uiEmbossSelected2_ColorId;
            if (!isKeyRoot) {
                *frame1 = *bg;
            }
        }
    }    
    if (isFocus) {
        *frame1 = *frame2 = (isSel ? uiText_ColorId : uiInputFrameFocused_ColorId);
    }
    int colorEscape = none_ColorId;
    if (startsWith_String(&d->label, "\v")) {
        colorEscape = parseEscape_Color(cstr_String(&d->label), NULL);
    }
    if (colorEscape == uiTextCaution_ColorId) {
        *icon = *meta = colorEscape;
    }
    if (d->iconColor != none_ColorId) {
        *icon = d->iconColor;
        if ((*icon >= brown_ColorId && *icon <= blue_ColorId) && !isDarkTheme) {
            /* Auto-adjust absolute color IDs to suit the UI theme. */
            (*icon)--; /* make it darker */
        }
    }
    if (isHover) {
        if (isFrameless) {
            if (prefs_App()->accent == gray_ColorAccent && prefs_App()->theme >= light_ColorTheme) {
                *bg = gray75_ColorId;
            }
            else if (!isSel) {
                *bg = uiBackgroundFramelessHover_ColorId;
            }
            *fg = uiTextFramelessHover_ColorId;
        }
        else {
            /* Frames matching color escaped text. */
            if (colorEscape == uiTextCaution_ColorId) {
                *frame1 = colorEscape;
                *frame2 = isDarkTheme ? black_ColorId : white_ColorId;
            }
            else if (isSel) {
                *frame1 = uiEmbossSelectedHover1_ColorId;
                *frame2 = uiEmbossSelectedHover2_ColorId;
            }
            else {
                *frame1 = uiEmbossHover1_ColorId;
                *frame2 = uiEmbossHover2_ColorId;
            }
        }
        if (colorEscape > 0) {
            *frame1 = accent_Color(isDarkTheme);
        }
        if (colorEscape == uiTextCaution_ColorId) {
            *icon = *meta = *fg = permanent_ColorId | (isDarkTheme ? black_ColorId : white_ColorId);
            *bg = accent_Color(isDarkTheme);
        }
    }
    if (d->forceFg >= 0) {
        *fg = *meta = d->forceFg;
    }
    if (isPress) {
        if (colorEscape == uiTextAction_ColorId || colorEscape == uiTextCaution_ColorId) {
            *bg = colorEscape;
            *frame1 = *bg;
            *frame2 = *bg;
            *fg = *icon = *meta = (isDarkTheme ? black_ColorId : white_ColorId) | permanent_ColorId;
        }
        else {
            *bg = uiBackgroundPressed_ColorId | permanent_ColorId;
            if (isButton) {
                *frame1 = uiEmbossPressed1_ColorId;
                *frame2 = colorEscape != none_ColorId ? colorEscape : uiEmbossPressed2_ColorId;
            }
            *fg = *icon = *meta = uiTextPressed_ColorId | permanent_ColorId;
        }
    }
    if (((isSel || isHover) && isFrameless) || isPress) {
        /* Ensure that the full label text remains readable. */
        *fg |= permanent_ColorId;
    }
}

iLocalDef int iconPadding_LabelWidget_(const iLabelWidget *d) {
    const float amount = flags_Widget(constAs_Widget(d)) & extraPadding_WidgetFlag ? 1.5f : 1.15f;
    return d->icon ? iRound(lineHeight_Text(d->font) * amount) : 0;
}

static iRect contentBounds_LabelWidget_(const iLabelWidget *d) {
    iRect content = adjusted_Rect(bounds_Widget(constAs_Widget(d)),
                                  padding_LabelWidget_(d, 0),
                                  neg_I2(padding_LabelWidget_(d, 2)));
    adjustEdges_Rect(&content, 0, 0, 0, iconPadding_LabelWidget_(d));
    return content;
}

static void draw_LabelWidget_(const iLabelWidget *d) {
    const iWidget *w = constAs_Widget(d);
    drawBackground_Widget(w);
    const iBool   isButton = d->click.buttons != 0;
    const int64_t flags    = flags_Widget(w);
    const iRect   bounds   = bounds_Widget(w);
    iRect         rect     = bounds;
    const iBool   isHover  = isHover_LabelWidget_(d);
    if (isButton) {
        shrink_Rect(&rect, divi_I2(gap2_UI, 4));
        adjustEdges_Rect(&rect, gap_UI / 8, 0, -gap_UI / 8, 0);
    }
    iPaint p;
    init_Paint(&p);
    int bg, fg, frame, frame2, iconColor, metaColor;
    getColors_LabelWidget_(d, &bg, &fg, &frame, &frame2, &iconColor, &metaColor);
    /* Indicate focused label with an underline attribute. */
    if (isTerminal_Platform() && isFocused_Widget(w)) {
        fg |= underline_ColorId;
    }
    setBaseAttributes_Text(d->font, fg);
    const enum iColorId colorEscape = parseEscape_Color(cstr_String(&d->label), NULL);
    const iBool isCaution = (colorEscape == uiTextCaution_ColorId);
    if (bg >= 0) {
        if (flags & dragged_WidgetFlag) {
            p.alpha = 0x70;
            SDL_SetRenderDrawBlendMode(renderer_Window(get_Window()), SDL_BLENDMODE_BLEND);
        }
        fillRect_Paint(&p, rect, bg);
        if (flags & dragged_WidgetFlag) {
            SDL_SetRenderDrawBlendMode(renderer_Window(get_Window()), SDL_BLENDMODE_NONE);
        }
    }
    if (isFocused_Widget(w)) {
        iRect frameRect = adjusted_Rect(rect, zero_I2(), init1_I2(-1));
        drawRectThickness_Paint(&p, frameRect, gap_UI / 4, uiTextAction_ColorId /*frame*/);        
    }
    else if (~flags & frameless_WidgetFlag) {
        iRect frameRect = adjusted_Rect(rect, zero_I2(), init1_I2(-1));
        if (isButton) {
            iInt2 points[] = {
                bottomLeft_Rect(frameRect),
                topLeft_Rect(frameRect),
                topRight_Rect(frameRect),
                bottomRight_Rect(frameRect),
                bottomLeft_Rect(frameRect)
            };
#if SDL_COMPILEDVERSION == SDL_VERSIONNUM(2, 0, 16)
            if (isOpenGLRenderer_Window()) {
                /* A very curious regression in SDL 2.0.16. */
                points[3].x--;    
            }
#endif
            if (d->flags.noBottomFrame && !isFocused_Widget(w) && !isHover) {
                drawLines_Paint(&p, points + 2, 2, frame2);
                drawLines_Paint(&p, points, 3, frame);
            }
            else {
                drawLines_Paint(&p, points + 2, 3, frame2);
                drawLines_Paint(&p,
                                points,
                                (d->flags.noTopFrame && !isFocused_Widget(w) && !isHover ? 2 : 3),
                                frame);
            }
        }
    }
    setClip_Paint(&p, rect);
    const int iconPad = iconPadding_LabelWidget_(d);
    if (d->icon && d->icon != 0x20) { /* no need to draw an empty icon */
        iString str;
        initUnicodeN_String(&str, &d->icon, 1);
        drawCentered_Text(
            d->font,
            (iRect){
                /* The icon position is fine-tuned; c.f. high baseline of Source Sans Pro. */
                add_I2(add_I2(bounds.pos, padding_LabelWidget_(d, 0)),
                       init_I2((flags & extraPadding_WidgetFlag ? -2 : -1.20f) * gap_UI / aspect_UI +
                               (deviceType_App() == tablet_AppDeviceType ? -gap_UI : 0),
                               -gap_UI / 8)),
                init_I2(iconPad, lineHeight_Text(d->font)) },
            iTrue,
            iconColor,
            "%s",
            cstr_String(&str));
        deinit_String(&str);
    }
    if (d->flags.wrap) {
        const iRect cont = contentBounds_LabelWidget_(d);
        iWrapText wt = {
            .text = range_String(&d->label),
            .maxWidth = width_Rect(cont),
            .mode = word_WrapTextMode,
        };
        draw_WrapText(&wt, d->font, topLeft_Rect(cont), fg);
    }
    else if (flags & alignLeft_WidgetFlag) {
        const iInt2 topLeft = add_I2(bounds.pos, addX_I2(padding_LabelWidget_(d, 0), iconPad));
        if (d->flags.truncateToFit) {
            const char *endPos;
            tryAdvanceNoWrap_Text(d->font,
                                  range_String(&d->label),
                                  width_Rect(rect) - padding_LabelWidget_(d, 0).x -
                                      padding_LabelWidget_(d, 1).x - iconPad,
                                  &endPos);
            drawRange_Text(d->font, topLeft, fg, (iRangecc){ constBegin_String(&d->label), endPos });
        }
        else {
            draw_Text(d->font, topLeft, fg, "%s", cstr_String(&d->label));
        }
        if ((flags & drawKey_WidgetFlag) && d->key) {
            iString str;
            init_String(&str);
            keyStr_LabelWidget_(d, &str);
            drawAlign_Text(uiShortcuts_FontId,
                           add_I2(topRight_Rect(bounds),
                                  addX_I2(negX_I2(padding_LabelWidget_(d, 1)),
                                          deviceType_App() == tablet_AppDeviceType ? gap_UI : 0)),
                           metaColor,
                           right_Alignment,
                           "%s",
                           cstr_String(&str));
            deinit_String(&str);
        }
    }
    else if (flags & alignRight_WidgetFlag) {
        drawAlign_Text(
            d->font,
            add_I2(topRight_Rect(bounds), negX_I2(padding_LabelWidget_(d, 1))),
            fg,
            right_Alignment,
            "%s",
            cstr_String(&d->label));
    }
    else {
        drawCenteredOutline_Text(
            d->font,
            moved_Rect(
                adjusted_Rect(bounds,
                              init_I2(iconPad * (flags & tight_WidgetFlag ? 1.0f : 1.5f), 0),
                          init_I2(-iconPad * (flags & tight_WidgetFlag ? 0.5f : 1.0f), 0)),
                d->labelOffset),
            d->flags.alignVisual,
            d->flags.drawAsOutline ? fg : none_ColorId,
            d->flags.drawAsOutline ? d->widget.bgColor : fg,
            "%s",
            cstr_String(&d->label));
    }
    if (d->flags.chevron || (flags & selected_WidgetFlag && d->flags.checkMark)) {
        const iRect chRect = rect;
        const int chSize = lineHeight_Text(d->font);
        int offset = 0;
        if (d->flags.chevron) {
            offset = -iconPad;
        }
        else {
            offset = -10 * gap_UI;
        }
        drawCentered_Text(d->font,
                          (iRect){ addX_I2(topRight_Rect(chRect), offset),
                                   init_I2(chSize, height_Rect(chRect)) },
                          iTrue,
                          iconColor,
                          d->flags.chevron ? rightAngle_Icon : check_Icon);
    }
    setBaseAttributes_Text(-1, -1);
    unsetClip_Paint(&p);
    drawChildren_Widget(w);
}

static void sizeChanged_LabelWidget_(iLabelWidget *d) {
    iWidget *w = as_Widget(d);
    if (d->flags.wrap) {
        if (flags_Widget(w) & fixedHeight_WidgetFlag) {
            /* Calculate a new height based on the wrapping. */
            const iRect cont = contentBounds_LabelWidget_(d);
            w->rect.size.y =
                measureWrapRange_Text(d->font, width_Rect(cont), range_String(&d->label))
                    .bounds.size.y +
                padding_LabelWidget_(d, 0).y + padding_LabelWidget_(d, 2).y;
        }
    }
}

iInt2 defaultSize_LabelWidget(const iLabelWidget *d) {
    const iWidget *w = constAs_Widget(d);
    const int64_t flags = flags_Widget(w);
    iInt2 size = add_I2(measure_Text(d->font, cstr_String(&d->label)).bounds.size,
                        add_I2(padding_LabelWidget_(d, 0), padding_LabelWidget_(d, 2)));
    if ((flags & drawKey_WidgetFlag) && d->key) {
        iString str;
        init_String(&str);
        keyStr_LabelWidget_(d, &str);
        size.x += 2 * gap_UI + measure_Text(uiShortcuts_FontId, cstr_String(&str)).bounds.size.x;
        deinit_String(&str);
    }
    size.x += iconPadding_LabelWidget_(d);
    return size;
}

int font_LabelWidget(const iLabelWidget *d) {
    return d->font;
}

void updateSize_LabelWidget(iLabelWidget *d) {
    if (!d) return;
    iWidget      *w     = as_Widget(d);
    const int64_t flags = flags_Widget(w);
    const iInt2   size  = defaultSize_LabelWidget(d);
    if (!d->flags.noAutoMinHeight) {
        w->minSize.y = size.y; /* vertically text must remain visible */
    }
    /* Wrapped text implies that width must be defined by arrangement. */
    if (~flags & fixedWidth_WidgetFlag && !d->flags.wrap) {
        w->rect.size.x = size.x;
    }
    if (~flags & fixedHeight_WidgetFlag) {
        w->rect.size.y = size.y;
    }
}

static void replaceVariables_LabelWidget_(iLabelWidget *d) {
    translate_Lang(&d->label);
    if (d->flags.allCaps) {
        set_String(&d->label, collect_String(upperLang_String(&d->label, code_Lang())));
    }
    if (d->flags.removeTrailingColon && endsWith_String(&d->label, ":")) {
        removeEnd_String(&d->label, 1);
    }
}

void init_LabelWidget(iLabelWidget *d, const char *label, const char *cmd) {
    iWidget *w = &d->widget;
    init_Widget(w);
    iZap(d->flags);
    d->font = uiLabel_FontId;
    d->forceFg = none_ColorId;
    d->iconColor = none_ColorId;
    d->icon = 0;
    d->labelOffset = zero_I2();
    initCStr_String(&d->srcLabel, label);
    initCopy_String(&d->label, &d->srcLabel);
    replaceVariables_LabelWidget_(d);
    if (cmd) {
        initCStr_String(&d->command, cmd);
    }
    else {
        setFrameColor_Widget(w, uiFrame_ColorId);
        init_String(&d->command);
    }
    d->key   = 0;
    d->kmods = 0;
    init_Click(&d->click, d, !isEmpty_String(&d->command) ? SDL_BUTTON_LEFT : 0);
    setFlags_Widget(w, focusable_WidgetFlag | hover_WidgetFlag, d->click.buttons != 0);
    updateSize_LabelWidget(d);
    updateKey_LabelWidget_(d); /* could be bound to another key */
}

void deinit_LabelWidget(iLabelWidget *d) {
    deinit_String(&d->label);
    deinit_String(&d->srcLabel);
    deinit_String(&d->command);
}

void setFont_LabelWidget(iLabelWidget *d, int fontId) {
    d->font = fontId;
    updateSize_LabelWidget(d);
}

void setTextColor_LabelWidget(iLabelWidget *d, int color) {
    if (d && d->forceFg != color) {
        d->forceFg = color;
        refresh_Widget(d);
    }
}

void setText_LabelWidget(iLabelWidget *d, const iString *text) {
    if (d) {
        updateText_LabelWidget(d, text);
        updateSize_LabelWidget(d);
        if (isWrapped_LabelWidget(d)) {
            sizeChanged_LabelWidget_(d);
        }
    }
}

void setTextCStr_LabelWidget(iLabelWidget *d, const char *text) {
    if (d) {
        updateTextCStr_LabelWidget(d, text);
        updateSize_LabelWidget(d);
        if (isWrapped_LabelWidget(d)) {
            sizeChanged_LabelWidget_(d);
        }
    }
}

void setAlignVisually_LabelWidget(iLabelWidget *d, iBool alignVisual) {
    d->flags.alignVisual = alignVisual;
}

void setNoAutoMinHeight_LabelWidget(iLabelWidget *d, iBool noAutoMinHeight) {
    /* By default all labels use a minimum height determined by the text dimensions. */
    d->flags.noAutoMinHeight = noAutoMinHeight;
    if (noAutoMinHeight) {
        d->widget.minSize.y = 0;
    }
}

void setNoTopFrame_LabelWidget(iLabelWidget *d, iBool noTopFrame) {
    d->flags.noTopFrame = noTopFrame;
}

void setNoBottomFrame_LabelWidget(iLabelWidget *d, iBool noBottomFrame) {
    d->flags.noBottomFrame = noBottomFrame;
}

void setChevron_LabelWidget(iLabelWidget *d, iBool chevron) {
    d->flags.chevron = chevron;
}

void setCheckMark_LabelWidget(iLabelWidget *d, iBool checkMark) {
    d->flags.checkMark = checkMark;
}

void setWrap_LabelWidget(iLabelWidget *d, iBool wrap) {
    d->flags.wrap = wrap;
}

void setTruncateToFit_LabelWidget (iLabelWidget *d, iBool truncateToFit) {
    d->flags.truncateToFit = truncateToFit;
}

void setOutline_LabelWidget(iLabelWidget *d, iBool drawAsOutline) {
    if (d) {
        d->flags.drawAsOutline = drawAsOutline;
    }
}

void setAllCaps_LabelWidget(iLabelWidget *d, iBool allCaps) {
    if (d) {
        d->flags.allCaps = allCaps;
        replaceVariables_LabelWidget_(d);
    }
}

void setRemoveTrailingColon_LabelWidget(iLabelWidget *d, iBool removeTrailingColon) {
    if (d) {
        d->flags.removeTrailingColon = removeTrailingColon;
        replaceVariables_LabelWidget_(d);
    }
}

void setTextOffset_LabelWidget(iLabelWidget *d, iInt2 offset) {
    d->labelOffset = offset;
}

void updateText_LabelWidget(iLabelWidget *d, const iString *text) {
    set_String(&d->label, text);
    set_String(&d->srcLabel, text);
    replaceVariables_LabelWidget_(d);
    refresh_Widget(&d->widget);
}

void updateTextCStr_LabelWidget(iLabelWidget *d, const char *text) {
    if (d) {
        setCStr_String(&d->label, text);
        set_String(&d->srcLabel, &d->label);
        replaceVariables_LabelWidget_(d);
        refresh_Widget(&d->widget);
    }
}

void updateTextAndResizeWidthCStr_LabelWidget(iLabelWidget *d, const char *text) {
    updateTextCStr_LabelWidget(d, text);
    d->widget.rect.size.x = defaultSize_LabelWidget(d).x;
}

void setCommand_LabelWidget(iLabelWidget *d, const iString *command) {
    set_String(&d->command, command);
}

void setIcon_LabelWidget(iLabelWidget *d, iChar icon) {
    if (d->icon != icon) {
        d->icon = icon;
        updateSize_LabelWidget(d);
    }
}

void setIconColor_LabelWidget(iLabelWidget *d, int color) {
    d->iconColor = color;
}

iBool checkIcon_LabelWidget(iLabelWidget *d) {
    if (isEmpty_String(&d->label)) {
        d->icon = 0;
        return iFalse;
    }
    d->icon = removeIconPrefix_String(&d->label);
    if (isTerminal_Platform() && prefs_App()->simpleChars) {
        d->icon = 0; /* ASCII icons may get confused with key shortcuts */
    }
    return d->icon != 0;
}

int textColor_LabelWidget(const iLabelWidget *d) {
    return d->forceFg;
}

iChar icon_LabelWidget(const iLabelWidget *d) {
    return d->icon;
}

iBool isWrapped_LabelWidget(const iLabelWidget *d) {
    return d->flags.wrap;
}

const iString *text_LabelWidget(const iLabelWidget *d) {
    if (!d) return collectNew_String();
    return &d->label;
}

const iString *sourceText_LabelWidget(const iLabelWidget *d) {
    if (!d) return collectNew_String();
    return &d->srcLabel;
}

const iString *command_LabelWidget(const iLabelWidget *d) {
    return &d->command;
}

iLabelWidget *newKeyMods_LabelWidget(const char *label, int key, int kmods, const char *command) {
    iLabelWidget *d = new_LabelWidget(label, command);
    d->key = key;
    d->kmods = kmods;
    updateKey_LabelWidget_(d); /* could be bound to a different key */
    return d;
}

iLabelWidget *newColor_LabelWidget(const char *text, int color) {
    iLabelWidget *d = new_LabelWidget(format_CStr("%s%s", escape_Color(color), text), NULL);
    setFlags_Widget(as_Widget(d), frameless_WidgetFlag, iTrue);
    return d;
}

iBeginDefineSubclass(LabelWidget, Widget)
    .processEvent = (iAny *) processEvent_LabelWidget_,
    .draw         = (iAny *) draw_LabelWidget_,
    .sizeChanged  = (iAny *) sizeChanged_LabelWidget_,
iEndDefineSubclass(LabelWidget)
