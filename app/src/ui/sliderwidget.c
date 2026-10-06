/* Copyright 2026 Jaakko Keränen <jaakko.keranen@iki.fi>

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

#include "sliderwidget.h"
#include "app.h"
#include "command.h"
#include "menu.h"
#include "touch.h"
#include "util.h"
#include "render/paint.h"
#include "render/text.h"

#include <lagrange/lang.h>
#include <the_Foundation/array.h>
#include <math.h>

iDeclareType(SliderLabel)

struct Impl_SliderLabel {
    int     index;
    iString source; /* may contain ${} translation IDs */
    iString text;
};

struct Impl_SliderWidget {
    iWidget          widget;
    iString          command;
    float            min;
    float            step;
    int              numSteps; /* valid indices are 0...numSteps */
    int              bigStep;  /* number of steps */
    int              index;
    float            value;
    int              precision;
    enum iSliderUnit unit;
    iBool            showSign;
    int              font;
    int              thumbWidth;
    iArray           labels; /* SliderLabel */
    iClick           click;
    int              startIndex;
    int              grabOffset;
};

iDefineObjectConstructionArgs(SliderWidget, (const char *command), command)

static float valueAt_SliderWidget_(const iSliderWidget *d, int index) {
    const float value = d->min + index * d->step;
    return fabsf(value) < d->step * 0.001f ? 0.0f : value;
}

static int indexOf_SliderWidget_(const iSliderWidget *d, float value) {
    return iClamp((int) lroundf((value - d->min) / d->step), 0, d->numSteps);
}

static const char *findLabel_SliderWidget_(const iSliderWidget *d, int index) {
    iConstForEach(Array, i, &d->labels) {
        const iSliderLabel *label = i.value;
        if (label->index == index) {
            return cstr_String(&label->text);
        }
    }
    return NULL;
}

static const char *unitSuffix_SliderWidget_(const iSliderWidget *d) {
    switch (d->unit) {
        case times_SliderUnit:
            return "x";
        case percent_SliderUnit:
            return " %";
        case kilobytes_SliderUnit:
            return format_CStr(" %s", cstr_Lang("kb"));
        default:
            return "";
    }
}

static const char *number_SliderWidget_(const iSliderWidget *d, int index) {
    const float value = valueAt_SliderWidget_(d, index);
    return format_CStr(d->showSign && value != 0.0f ? "%+.*f" : "%.*f", d->precision, value);
}

static const char *text_SliderWidget_(const iSliderWidget *d, int index) {
    const char *label = findLabel_SliderWidget_(d, index);
    if (label) {
        return label;
    }
    return format_CStr("%s%s", number_SliderWidget_(d, index), unitSuffix_SliderWidget_(d));
}

static int labelFont_SliderWidget_(const iSliderWidget *d) {
    return d->font == uiLabelBig_FontId || d->font == uiLabelMedium_FontId ? uiLabel_FontId
                                                                           : uiLabelSmall_FontId;
}

static void updateMetrics_SliderWidget_(iSliderWidget *d) {
    iWidget  *w    = as_Widget(d);
    const int padX = isTerminal_Platform() ? 1 : isMobile_Platform() ? 4 * gap_UI : 3 * gap_UI;
    const int padY = isTerminal_Platform() ? 0
                     : isMobile_Platform() ? 3 * gap_UI / 2
                                           : (int) (gap_UI * aspect_UI);
    /* Steps next to the ends are measured as well, in case the ends have custom labels. */
    const int ends[] = { 0, iMin(1, d->numSteps), iMax(0, d->numSteps - 1), d->numSteps };
    int       widest = 0;
    iForIndices(i, ends) {
        widest = iMax(widest, measure_Text(d->font, text_SliderWidget_(d, ends[i])).bounds.size.x);
    }
    iConstForEach(Array, j, &d->labels) {
        const iSliderLabel *label = j.value;
        widest = iMax(widest, measure_Text(d->font, cstr_String(&label->text)).bounds.size.x);
    }
    d->thumbWidth = widest + 2 * padX;
    w->rect.size.y = lineHeight_Text(d->font) + 2 * padY;
    w->minSize.x = isTerminal_Platform() ? iMax(3 * d->thumbWidth, 24)
                   : isMobile_Platform() ? 2 * d->thumbWidth /* shares the row with a heading */
                                         : iMax(3 * d->thumbWidth, 60 * gap_UI);
    w->rect.size.x = iMax(w->rect.size.x, w->minSize.x);
}

static void postCommand_SliderWidget_(const iSliderWidget *d) {
    if (!isEmpty_String(&d->command)) {
        postCommand_Widget(d, "%s arg:%s", cstr_String(&d->command),
                           number_SliderWidget_(d, d->index));
    }
}

static void setIndex_SliderWidget_(iSliderWidget *d, int index) {
    index = iClamp(index, 0, d->numSteps);
    if (index != d->index) {
        d->index = index;
        d->value = valueAt_SliderWidget_(d, index);
        refresh_Widget(d);
    }
}

void init_SliderWidget(iSliderWidget *d, const char *command) {
    iWidget *w = as_Widget(d);
    init_Widget(w);
    setFlags_Widget(w, focusable_WidgetFlag | hover_WidgetFlag | fixedHeight_WidgetFlag, iTrue);
    if (isMobile_Platform()) {
        setFlags_Widget(w, touchDrag_WidgetFlag, iTrue);
    }
    initCStr_String(&d->command, command ? command : "");
    d->min        = 0.0f;
    d->step       = 1.0f;
    d->numSteps   = 1;
    d->bigStep    = 1;
    d->index      = 0;
    d->value      = 0.0f;
    d->precision  = 0;
    d->unit       = none_SliderUnit;
    d->showSign   = iFalse;
    d->font       = uiLabel_FontId;
    d->thumbWidth = 0;
    init_Array(&d->labels, sizeof(iSliderLabel));
    init_Click(&d->click, d, SDL_BUTTON_LEFT);
    d->startIndex = 0;
    d->grabOffset = 0;
    updateMetrics_SliderWidget_(d);
}

void deinit_SliderWidget(iSliderWidget *d) {
    iForEach(Array, i, &d->labels) {
        iSliderLabel *label = i.value;
        deinit_String(&label->source);
        deinit_String(&label->text);
    }
    deinit_Array(&d->labels);
    deinit_String(&d->command);
}

static enum iSliderUnit parseUnit_(iRangecc unit) {
    if (equal_Rangecc(unit, "x")) {
        return times_SliderUnit;
    }
    if (equal_Rangecc(unit, "percent")) {
        return percent_SliderUnit;
    }
    if (equal_Rangecc(unit, "kb")) {
        return kilobytes_SliderUnit;
    }
    return none_SliderUnit;
}

iSliderWidget *newMenuItem_SliderWidget(const iMenuItem *item) {
    const char    *spec = item->label;
    iSliderWidget *d    = new_SliderWidget(hasLabel_Command(spec, "cmd") ? cstr_Command(spec, "cmd")
                                                                         : NULL);
    setId_Widget(as_Widget(d), cstr_Command(spec, "id"));
    d->precision = argLabel_Command(spec, "prec");
    d->unit      = parseUnit_(range_Command(spec, "unit"));
    d->showSign  = argLabel_Command(spec, "sign") != 0;
    const float step = argfLabel_Command(spec, "step");
    setRange_SliderWidget(d,
                          argfLabel_Command(spec, "min"),
                          argfLabel_Command(spec, "max"),
                          step > 0.0f ? step : 1.0f);
    if (hasLabel_Command(spec, "bigstep")) {
        setBigStep_SliderWidget(d, argfLabel_Command(spec, "bigstep"));
    }
    if (item->data) {
        for (const iMenuItem *tick = item->data; tick->label; tick++) {
            addLabel_SliderWidget(d,
                                  argfLabel_Command(tick->label, "value"),
                                  hasLabel_Command(tick->label, "label")
                                      ? format_CStr("${%s}", cstr_Command(tick->label, "label"))
                                      : suffixPtr_Command(tick->label, "text"));
        }
    }
    return d;
}

void setRange_SliderWidget(iSliderWidget *d, float min, float max, float step) {
    d->min      = min;
    d->step     = step;
    d->numSteps = iMax(1, (int) lroundf((max - min) / step));
    d->index    = indexOf_SliderWidget_(d, d->value);
    updateMetrics_SliderWidget_(d);
}

void setBigStep_SliderWidget(iSliderWidget *d, float bigStep) {
    d->bigStep = iMax(1, (int) lroundf(bigStep / d->step));
}

void setPrecision_SliderWidget(iSliderWidget *d, int precision) {
    d->precision = precision;
    updateMetrics_SliderWidget_(d);
}

void setUnit_SliderWidget(iSliderWidget *d, enum iSliderUnit unit) {
    d->unit = unit;
    updateMetrics_SliderWidget_(d);
}

void setShowSign_SliderWidget(iSliderWidget *d, iBool showSign) {
    d->showSign = showSign;
    updateMetrics_SliderWidget_(d);
}

void setFont_SliderWidget(iSliderWidget *d, int fontId) {
    d->font = fontId;
    updateMetrics_SliderWidget_(d);
}

void addLabel_SliderWidget(iSliderWidget *d, float value, const char *label) {
    iSliderLabel lab = { .index = indexOf_SliderWidget_(d, value) };
    initCStr_String(&lab.source, label);
    initCopy_String(&lab.text, &lab.source);
    translate_Lang(&lab.text);
    pushBack_Array(&d->labels, &lab);
    updateMetrics_SliderWidget_(d);
}

void setValue_SliderWidget(iSliderWidget *d, float value) {
    if (!d) return;
    d->value = value;
    d->index = indexOf_SliderWidget_(d, value);
    refresh_Widget(d);
}

float value_SliderWidget(const iSliderWidget *d) {
    return d->value;
}

static iRect thumbRect_SliderWidget_(const iSliderWidget *d, int index) {
    const iRect bounds = bounds_Widget(constAs_Widget(d));
    const int   span   = width_Rect(bounds) - d->thumbWidth;
    return init_Rect(left_Rect(bounds) + span * index / d->numSteps,
                     top_Rect(bounds),
                     d->thumbWidth,
                     height_Rect(bounds));
}

static int indexAt_SliderWidget_(const iSliderWidget *d, int x) {
    const iRect bounds = bounds_Widget(constAs_Widget(d));
    const int   span   = iMax(1, width_Rect(bounds) - d->thumbWidth);
    const int   pos    = x - left_Rect(bounds) - d->thumbWidth / 2;
    return iClamp((int) lroundf((float) pos * d->numSteps / span), 0, d->numSteps);
}

static iBool processEvent_SliderWidget_(iSliderWidget *d, const SDL_Event *ev) {
    iWidget *w = as_Widget(d);
    if (isMetricsChange_UserEvent(ev)) {
        updateMetrics_SliderWidget_(d);
    }
    else if (isCommand_UserEvent(ev, "lang.changed")) {
        iForEach(Array, i, &d->labels) {
            iSliderLabel *label = i.value;
            set_String(&label->text, &label->source);
            translate_Lang(&label->text);
        }
        updateMetrics_SliderWidget_(d);
        refresh_Widget(d);
        return iFalse;
    }
    else if (isCommand_Widget(w, ev, "focus.gained") || isCommand_Widget(w, ev, "focus.lost")) {
        refresh_Widget(d);
        return iFalse;
    }
    switch (processEvent_Click(&d->click, ev)) {
        case started_ClickResult: {
            const int   x         = pos_Click(&d->click).x;
            const iRect thumb     = thumbRect_SliderWidget_(d, d->index);
            const iBool isOnThumb = contains_Rect(thumb, pos_Click(&d->click));
            /* Grabbing the thumb does not move it; elsewhere it jumps to the pointer. */
            d->startIndex = d->index;
            d->grabOffset = isOnThumb ? x - mid_Rect(thumb).x : 0;
            if (isOnThumb) {
                setFocus_Widget(w);
            }
            setIndex_SliderWidget_(d, indexAt_SliderWidget_(d, x - d->grabOffset));
            setFlags_Widget(w, pressed_WidgetFlag, iTrue);
            refresh_Widget(d);
            return iTrue;
        }
        case drag_ClickResult:
            setIndex_SliderWidget_(d,
                                   indexAt_SliderWidget_(d, pos_Click(&d->click).x - d->grabOffset));
            return iTrue;
        case finished_ClickResult:
        case aborted_ClickResult:
            setFlags_Widget(w, pressed_WidgetFlag, iFalse);
            if (d->index != d->startIndex) {
                postCommand_SliderWidget_(d);
            }
            refresh_Widget(d);
            return iTrue;
        default:
            break;
    }
    if (ev->type == SDL_EVENT_KEY_DOWN && isFocused_Widget(w)) {
        const int mods = keyMods_Sym(ev->key.mod) & ~SDL_KMOD_CAPS;
        if (mods == 0 || mods == SDL_KMOD_SHIFT) {
            const int delta    = (mods ? d->bigStep : 1);
            int       newIndex = -1;
            switch (ev->key.key) {
                case SDLK_LEFT:
                    newIndex = iMax(0, d->index - delta);
                    break;
                case SDLK_RIGHT:
                    newIndex = iMin(d->numSteps, d->index + delta);
                    break;
                case SDLK_HOME:
                    newIndex = 0;
                    break;
                case SDLK_END:
                    newIndex = d->numSteps;
                    break;
            }
            if (newIndex >= 0) {
                if (newIndex != d->index) {
                    setIndex_SliderWidget_(d, newIndex);
                    postCommand_SliderWidget_(d);
                }
                return iTrue;
            }
        }
    }
    return processEvent_Widget(w, ev);
}

/* Track stands out from the background behind it. */
static int trackColor_SliderWidget_(const iSliderWidget *d) {
    for (const iWidget *w = constAs_Widget(d); w; w = w->parent) {
        if (w->bgColor >= 0) {
            return w->bgColor == uiBackgroundSidebar_ColorId ? uiBackground_ColorId
                                                             : uiBackgroundSidebar_ColorId;
        }
    }
    return uiBackgroundSidebar_ColorId;
}

static iRect drawnThumbRect_SliderWidget_(const iSliderWidget *d) {
    iRect thumb = thumbRect_SliderWidget_(d, d->index);
    if (!isTerminal_Platform()) {
        shrink_Rect(&thumb, divi_I2(gap2_UI, 4));
        adjustEdges_Rect(&thumb, gap_UI / 8, 0, -gap_UI / 8, 0);
    }
    return thumb;
}

/* Min and max labels are aligned to the line ends; custom labels are centered on the thumb
   position of their value. Labels touching the thumb are omitted. */
static void drawLabels_SliderWidget_(const iSliderWidget *d, iPaint *p, int x0, int x1, int midY,
                                     int bg) {
    const iRect bounds = bounds_Widget(constAs_Widget(d));
    const iRect thumb  = drawnThumbRect_SliderWidget_(d);
    const int   font   = labelFont_SliderWidget_(d);
    const int   height = lineHeight_Text(font);
    const int   fg     = isDisabled_Widget(d) ? uiTextDisabled_ColorId : uiTextDim_ColorId;
    iArray indices, drawn;
    init_Array(&indices, sizeof(int));
    init_Array(&drawn, sizeof(iRect));
    pushBackN_Array(&indices, (int[]){ 0, d->numSteps }, 2);
    iConstForEach(Array, lab, &d->labels) {
        pushBack_Array(&indices, &((const iSliderLabel *) lab.value)->index);
    }
    iConstForEach(Array, i, &indices) {
        const size_t pos   = index_ArrayConstIterator(&i);
        const int    index = *(const int *) i.value;
        const char  *text  = text_SliderWidget_(d, index);
        const int    width = measure_Text(font, text).bounds.size.x + 2 * gap_UI;
        iRect rect = init_Rect(0, midY - height / 2, width, height);
        if (pos == 0) {
            rect.pos.x = x0 - gap_UI;
        }
        else if (pos == 1) {
            rect.pos.x = x1 + gap_UI - width;
        }
        else {
            rect.pos.x = mid_Rect(thumbRect_SliderWidget_(d, index)).x - width / 2;
            rect.pos.x = iClamp(rect.pos.x, left_Rect(bounds), right_Rect(bounds) - width);
        }
        /* Overlapping the label's padding or the thumb's rounded corner is tolerated. */
        iBool isOverlapping = isOverlapping_Rect(shrunk_Rect(rect, init_I2(3 * gap_UI / 2, 0)),
                                                 thumb);
        iConstForEach(Array, j, &drawn) {
            if (isOverlapping) break;
            isOverlapping = isOverlapping_Rect(expanded_Rect(rect, init_I2(gap_UI, 0)),
                                               *(const iRect *) j.value);
        }
        if (!isOverlapping) {
            fillRect_Paint(p, rect, bg);
            drawCentered_Text(font, rect, iTrue, fg, "%s", text);
        }
        /* Labels hidden by the thumb still reserve their place, so others don't pop in. */
        pushBack_Array(&drawn, &rect);
    }
    deinit_Array(&drawn);
    deinit_Array(&indices);
}

static void draw_SliderWidget_(const iSliderWidget *d) {
    const iWidget *w = constAs_Widget(d);
    drawBackground_Widget(w);
    const iRect bounds     = bounds_Widget(w);
    const iBool isTerminal = isTerminal_Platform();
    const iBool isDisabled = isDisabled_Widget(w);
    const iBool isFocus    = isFocused_Widget(w);
    const iBool isPress    = (flags_Widget(w) & pressed_WidgetFlag) != 0;
    const iBool isHover    = isHover_Widget(w) && (!isMobile_Platform() || isHovering_Touch());
    const int   midY       = mid_Rect(bounds).y;
    const int   inset      = isTerminal ? 1 : 3 * gap_UI;
    const int   x0         = left_Rect(bounds) + inset;
    const int   x1         = right_Rect(bounds) - inset;
    iPaint p;
    init_Paint(&p);
    if (isTerminal) {
        drawHLine_Paint(&p, init_I2(x0, midY), x1 - x0, uiSeparator_ColorId);
    }
    else {
        const int track     = trackColor_SliderWidget_(d);
        const int thickness = iMax(1, gap_UI / 3);
        drawRoundRect_Paint(&p, bounds, all_RoundCorner, 0, track, none_ColorId, none_ColorId);
        fillRect_Paint(&p,
                       init_Rect(x0, midY - thickness / 2, x1 - x0, thickness),
                       isDisabled ? uiSeparator_ColorId : uiTextDim_ColorId);
        drawLabels_SliderWidget_(d, &p, x0, x1, midY, track);
    }
    /* The thumb. */
    const iRect thumb = drawnThumbRect_SliderWidget_(d);
    int         bg     = uiBackground_ColorId;
    int         fg     = isDisabled ? uiTextDisabled_ColorId : uiText_ColorId;
    int         frame1 = isHover ? uiEmbossHover1_ColorId : uiEmboss1_ColorId;
    int         frame2 = isHover ? uiEmbossHover2_ColorId : uiEmboss2_ColorId;
    if (isPress) {
        bg     = uiBackgroundPressed_ColorId | permanent_ColorId;
        fg     = uiTextPressed_ColorId | permanent_ColorId;
        frame1 = uiEmbossPressed1_ColorId;
        frame2 = uiEmbossPressed2_ColorId;
    }
    if (isTerminal) {
        if (isFocus && !isPress) {
            bg = uiBackgroundSelected_ColorId;
            fg = uiTextSelected_ColorId | underline_ColorId;
        }
        else if (!isPress) {
            bg = uiBackgroundFramelessHover_ColorId;
        }
        fillRect_Paint(&p, thumb, bg);
    }
    else {
        drawRoundRect_Paint(&p,
                            thumb,
                            all_RoundCorner,
                            !isFocus ? 1 : isHover ? gap_UI / 2 : gap_UI / 4,
                            bg,
                            isFocus ? uiTextAction_ColorId : frame1,
                            isFocus ? uiTextAction_ColorId : frame2);
    }
    drawCentered_Text(d->font, thumb, iFalse, fg, "%s", text_SliderWidget_(d, d->index));
}

iBeginDefineSubclass(SliderWidget, Widget)
    .processEvent = (iAny *) processEvent_SliderWidget_,
    .draw         = (iAny *) draw_SliderWidget_,
iEndDefineSubclass(SliderWidget)
