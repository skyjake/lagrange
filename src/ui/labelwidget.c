#include "labelwidget.h"
#include "text.h"
#include "color.h"
#include "paint.h"
#include "app.h"
#include "util.h"

iLocalDef iInt2 padding_(int flags) {
    return init_I2(flags & tight_WidgetFlag ? 3 * gap_UI / 2 : (3 * gap_UI), gap_UI);
}

struct Impl_LabelWidget {
    iWidget widget;
    iString label;
    int     font;
    int     key;
    int     kmods;
    iString command;
    iBool   alignVisual; /* align according to visible bounds, not typography */
    iClick  click;
};

iDefineObjectConstructionArgs(LabelWidget,
                              (const char *label, int key, int kmods, const char *cmd),
                              label, key, kmods, cmd)

static iBool checkModifiers_(int have, int req) {
    return keyMods_Sym(req) == keyMods_Sym(have);
}

static void trigger_LabelWidget_(const iLabelWidget *d) {
    postCommand_Widget(&d->widget, "%s", cstr_String(&d->command));
}

static iBool processEvent_LabelWidget_(iLabelWidget *d, const SDL_Event *ev) {
    iWidget *w = &d->widget;
    if (isCommand_UserEvent(ev, "metrics.changed")) {
        updateSize_LabelWidget(d);
    }
    if (!isEmpty_String(&d->command)) {
        switch (processEvent_Click(&d->click, ev)) {
            case started_ClickResult:
                setFlags_Widget(w, pressed_WidgetFlag, iTrue);
                refresh_Widget(w);
                return iTrue;
            case aborted_ClickResult:
                setFlags_Widget(w, pressed_WidgetFlag, iFalse);
                refresh_Widget(w);
                return iTrue;
            case finished_ClickResult:
                setFlags_Widget(w, pressed_WidgetFlag, iFalse);
                trigger_LabelWidget_(d);
                refresh_Widget(w);
                return iTrue;
            case double_ClickResult:
                return iTrue;
            default:
                break;
        }
        switch (ev->type) {
            case SDL_KEYDOWN: {
                const int mods = ev->key.keysym.mod;
                if (d->key && ev->key.keysym.sym == d->key && checkModifiers_(mods, d->kmods)) {
                    trigger_LabelWidget_(d);
                    return iTrue;
                }
                break;
            }
        }
    }
    return processEvent_Widget(&d->widget, ev);
}

static void keyStr_LabelWidget_(const iLabelWidget *d, iString *str) {
#if defined (iPlatformApple)
    if (d->kmods & KMOD_CTRL) {
        appendChar_String(str, 0x2303);
    }
    if (d->kmods & KMOD_ALT) {
        appendChar_String(str, 0x2325);
    }
    if (d->kmods & KMOD_SHIFT) {
        appendChar_String(str, 0x21e7);
    }
    if (d->kmods & KMOD_GUI) {
        appendChar_String(str, 0x2318);
    }
#else
    if (d->kmods & KMOD_CTRL) {
        appendCStr_String(str, "Ctrl+");
    }
    if (d->kmods & KMOD_ALT) {
        appendCStr_String(str, "Alt+");
    }
    if (d->kmods & KMOD_SHIFT) {
        appendCStr_String(str, "Shift+");
    }
    if (d->kmods & KMOD_GUI) {
        appendCStr_String(str, "Meta+");
    }
#endif
    if (d->key == 0x20) {
        appendCStr_String(str, "Space");
    }
    else if (d->key == SDLK_LEFT) {
        appendChar_String(str, 0x2190);
    }
    else if (d->key == SDLK_RIGHT) {
        appendChar_String(str, 0x2192);
    }
    else if (d->key < 128 && (isalnum(d->key) || ispunct(d->key))) {
        appendChar_String(str, upper_Char(d->key));
    }
    else if (d->key == SDLK_BACKSPACE) {
        appendChar_String(str, 0x232b); /* Erase to the Left */
    }
    else if (d->key == SDLK_DELETE) {
        appendChar_String(str, 0x2326); /* Erase to the Right */
    }
    else {
        appendCStr_String(str, SDL_GetKeyName(d->key));
    }
}

static void getColors_LabelWidget_(const iLabelWidget *d, int *bg, int *fg, int *frame1, int *frame2) {
    const iWidget *w           = constAs_Widget(d);
    const iBool    isPress     = (flags_Widget(w) & pressed_WidgetFlag) != 0;
    const iBool    isSel       = (flags_Widget(w) & selected_WidgetFlag) != 0;
    const iBool    isFrameless = (flags_Widget(w) & frameless_WidgetFlag) != 0;
    const iBool    isButton    = d->click.button != 0;
    /* Default color state. */
    *bg     = isButton ? uiBackground_ColorId : none_ColorId;
    *fg     = uiText_ColorId;
    *frame1 = isButton ? uiEmboss1_ColorId : uiFrame_ColorId;
    *frame2 = isButton ? uiEmboss2_ColorId : *frame1;
    if (isSel) {
        *bg = uiBackgroundSelected_ColorId;
        *fg = uiTextSelected_ColorId;
        if (isButton) {
            *frame1 = uiEmbossSelected1_ColorId;
            *frame2 = uiEmbossSelected2_ColorId;
        }
    }
    if (isHover_Widget(w)) {
        if (isFrameless) {
            *bg = uiBackgroundFramelessHover_ColorId;
            *fg = uiTextFramelessHover_ColorId;
        }
        else {
            /* Frames matching color escaped text. */
            if (startsWith_String(&d->label, "\r")) {
                if (isDark_ColorTheme(colorTheme_App())) {
                    *frame1 = cstr_String(&d->label)[1] - '0';
                    *frame2 = darker_Color(*frame1);
                }
                else {
                    *bg = *frame1 = *frame2 = cstr_String(&d->label)[1] - '0';
                    *fg = uiBackground_ColorId | permanent_ColorId;
                }
            }
            else if (isSel) {
                *frame1 = uiEmbossSelectedHover1_ColorId;
                *frame2 = uiEmbossSelectedHover2_ColorId;
            }
            else {
                if (isButton) *bg = uiBackgroundHover_ColorId;
                *frame1 = uiEmbossHover1_ColorId;
                *frame2 = uiEmbossHover2_ColorId;
            }
        }
    }
    if (isPress) {
        *bg = uiBackgroundPressed_ColorId | permanent_ColorId;
        if (isButton) {
            *frame1 = uiEmbossPressed1_ColorId;
            *frame2 = uiEmbossPressed2_ColorId;
        }
        *fg = uiTextPressed_ColorId | permanent_ColorId;
    }
}

static void draw_LabelWidget_(const iLabelWidget *d) {
    const iWidget *w = constAs_Widget(d);
    draw_Widget(w);
    const iBool isButton = d->click.button != 0;
    const int   flags    = flags_Widget(w);
    const iRect bounds   = bounds_Widget(w);
    iRect       rect     = bounds;
    if (isButton) {
        shrink_Rect(&rect, divi_I2(gap2_UI, 4));
        adjustEdges_Rect(&rect, gap_UI / 8, 0, -gap_UI / 8, 0);
    }
    iPaint p;
    init_Paint(&p);
    int bg, fg, frame, frame2;
    getColors_LabelWidget_(d, &bg, &fg, &frame, &frame2);
    if (bg >= 0) {
        fillRect_Paint(&p, rect, bg);
    }
    if (~flags & frameless_WidgetFlag) {
        iRect frameRect = adjusted_Rect(rect, zero_I2(), init1_I2(-1));
        if (isButton) {
            iInt2 points[] = {
                bottomLeft_Rect(frameRect), topLeft_Rect(frameRect), topRight_Rect(frameRect),
                bottomRight_Rect(frameRect), bottomLeft_Rect(frameRect)
            };
            drawLines_Paint(&p, points + 2, 3, frame2);
            drawLines_Paint(&p, points, 3, frame);
        }
        else {
            drawRect_Paint(&p, frameRect, frame);
        }
    }
    setClip_Paint(&p, rect);
    if (flags & alignLeft_WidgetFlag) {
        draw_Text(d->font, add_I2(bounds.pos, padding_(flags)), fg, cstr_String(&d->label));
        if ((flags & drawKey_WidgetFlag) && d->key) {
            iString str;
            init_String(&str);
            keyStr_LabelWidget_(d, &str);
            drawAlign_Text(uiShortcuts_FontId,
                           add_I2(topRight_Rect(bounds), negX_I2(padding_(flags))),
                           flags & pressed_WidgetFlag ? fg : uiTextShortcut_ColorId,
                           right_Alignment,
                           cstr_String(&str));
            deinit_String(&str);
        }
    }
    else if (flags & alignRight_WidgetFlag) {
        drawAlign_Text(
            d->font,
            add_I2(topRight_Rect(bounds), negX_I2(padding_(flags))),
            fg,
            right_Alignment,
            cstr_String(&d->label));
    }
    else {
        drawCentered_Text(d->font, bounds, d->alignVisual, fg, cstr_String(&d->label));
    }
    unsetClip_Paint(&p);
}

void updateSize_LabelWidget(iLabelWidget *d) {
    iWidget *w = as_Widget(d);
    const int flags = flags_Widget(w);
    iInt2 size = add_I2(measure_Text(d->font, cstr_String(&d->label)), muli_I2(padding_(flags), 2));
    if ((flags & drawKey_WidgetFlag) && d->key) {
        iString str;
        init_String(&str);
        keyStr_LabelWidget_(d, &str);
        size.x += 2 * gap_UI + measure_Text(uiShortcuts_FontId, cstr_String(&str)).x;
        deinit_String(&str);
    }
    if (~flags & fixedWidth_WidgetFlag) {
        w->rect.size.x = size.x;
    }
    if (~flags & fixedHeight_WidgetFlag) {
        w->rect.size.y = size.y;
    }
}

void init_LabelWidget(iLabelWidget *d, const char *label, int key, int kmods, const char *cmd) {
    init_Widget(&d->widget);
    d->font = uiLabel_FontId;
    initCStr_String(&d->label, label);
    if (cmd) {
        initCStr_String(&d->command, cmd);
    }
    else {
        init_String(&d->command);
    }
    d->key = key;
    d->kmods = kmods;
    init_Click(&d->click, d, !isEmpty_String(&d->command) ? SDL_BUTTON_LEFT : 0);
    setFlags_Widget(&d->widget, hover_WidgetFlag, d->click.button != 0);
    d->alignVisual = iFalse;
    updateSize_LabelWidget(d);
}

void deinit_LabelWidget(iLabelWidget *d) {
    deinit_String(&d->label);
    deinit_String(&d->command);
}

void setFont_LabelWidget(iLabelWidget *d, int fontId) {
    d->font = fontId;
    updateSize_LabelWidget(d);
}

void setText_LabelWidget(iLabelWidget *d, const iString *text) {
    updateText_LabelWidget(d, text);
    updateSize_LabelWidget(d);
}

void setAlignVisually_LabelWidget(iLabelWidget *d, iBool alignVisual) {
    d->alignVisual = alignVisual;
}

void updateText_LabelWidget(iLabelWidget *d, const iString *text) {
    set_String(&d->label, text);
    refresh_Widget(&d->widget);
}

void updateTextCStr_LabelWidget(iLabelWidget *d, const char *text) {
    setCStr_String(&d->label, text);
    refresh_Widget(&d->widget);
}

void setTextCStr_LabelWidget(iLabelWidget *d, const char *text) {
    setCStr_String(&d->label, text);
    updateSize_LabelWidget(d);
}

const iString *command_LabelWidget(const iLabelWidget *d) {
    return &d->command;
}

iBeginDefineSubclass(LabelWidget, Widget)
    .processEvent = (iAny *) processEvent_LabelWidget_,
    .draw         = (iAny *) draw_LabelWidget_,
iEndDefineSubclass(LabelWidget)
