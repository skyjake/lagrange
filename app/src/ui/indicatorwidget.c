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

#include "indicatorwidget.h"
#include "render/paint.h"
#include "util.h"
#include "app.h"
#include "command.h"

#include <lagrange/gmutil.h>
#include <SDL3/SDL_timer.h>

struct Impl_IndicatorWidget {
    iWidget widget;
    iAnim   pos;
    iBool   isManual;
};

iDefineObjectConstruction(IndicatorWidget)

iLocalDef iBool isActive_IndicatorWidget_(const iIndicatorWidget *d) {
    return isSelected_Widget(d);
}

static void animate_IndicatorWidget_(void *ptr) {
    iIndicatorWidget *d = ptr;
    if (!isFinished_Anim(&d->pos)) {
        addTickerRoot_App(animate_IndicatorWidget_, d->widget.root, ptr);
    }
    refresh_Widget(d);
}

static void setActive_IndicatorWidget_(iIndicatorWidget *d, iBool set) {
    setFlags_Widget(d, selected_WidgetFlag, set);
    setValue_Anim(&d->pos, 0.0f, 0);
}

static void rootChanged_IndicatorWidget_(iIndicatorWidget *d) {
    removeTicker_App(animate_IndicatorWidget_, d);
    setActive_IndicatorWidget_(d, iFalse);
}

void init_IndicatorWidget(iIndicatorWidget *d) {
    iWidget *w = &d->widget;
    init_Widget(w);
    init_Anim(&d->pos, 0);
    d->isManual = iFalse;
    setFlags_Widget(w, unhittable_WidgetFlag, iTrue);
}

void deinit_IndicatorWidget(iIndicatorWidget *d) {
    removeTicker_App(animate_IndicatorWidget_, d);
}

static iBool isCompleted_IndicatorWidget_(const iIndicatorWidget *d) {
    return targetValue_Anim(&d->pos) == 1.0f;
}

void setManual_IndicatorWidget(iIndicatorWidget *d, iBool manual) {
    d->isManual = manual;
}

void setProgress_IndicatorWidget(iIndicatorWidget *d, float progress) {
    if (progress <= 0.0f) {
        setValue_Anim(&d->pos, 0, 0);
    }
    else if (progress >= 1.0f) {
        if (isCompleted_IndicatorWidget_(d)) {
            return;
        }
        /* Completion is only shown if there was progress before. */
        setValue_Anim(&d->pos, targetValue_Anim(&d->pos) > 0.0f ? 1.0f : 0.0f, 250);
        setFlags_Anim(&d->pos, easeOut_AnimFlag, iFalse);
    }
    else {
        if (isCompleted_IndicatorWidget_(d)) {
            setValue_Anim(&d->pos, 0, 0);
        }
        setValue_Anim(&d->pos, iMax(progress, targetValue_Anim(&d->pos)), 250);
        setFlags_Anim(&d->pos, easeOut_AnimFlag, iTrue);
    }
    animate_IndicatorWidget_(d);
    refresh_Widget(d);
}

void start_IndicatorWidget(iIndicatorWidget *d) {
    setValue_Anim(&d->pos, 0, 0);
    setValue_Anim(&d->pos, 0.75f, 4000);
    setFlags_Anim(&d->pos, easeOut_AnimFlag, iTrue);
    animate_IndicatorWidget_(d);
}

void finish_IndicatorWidget(iIndicatorWidget *d) {
    if (targetValue_Anim(&d->pos) > 0.0f && !isCompleted_IndicatorWidget_(d)) {
        setValue_Anim(&d->pos, 1.0f, 250);
        setFlags_Anim(&d->pos, easeOut_AnimFlag, iFalse);
        animate_IndicatorWidget_(d);
    }
    else {
        setValue_Anim(&d->pos, 0, 0);
        animate_IndicatorWidget_(d);
        refresh_Widget(d);
    }
}

static iBool isLocalUrl_(const char *url) {
    const iRangecc scheme = urlScheme_String(collectNewCStr_String(url));
    return equalCase_Rangecc(scheme, "about") || equalCase_Rangecc(scheme, "data") ||
           isLocalFileScheme_Rangecc(scheme);
}

void draw_IndicatorWidget_(const iIndicatorWidget *d) {
    const float pos = value_Anim(&d->pos);
    if (pos > 0.0f && pos < 1.0f) {
        const iWidget *w = &d->widget;
        const iRect rect = innerBounds_Widget(w);
        iPaint p;
        init_Paint(&p);
        int colors[2] = { uiTextCaution_ColorId, uiTextAction_ColorId };
        if (isLight_ColorTheme(colorTheme_App())) {
            colors[0] = black_ColorId;
        }
        fillRect_Paint(
            &p,
            (iRect){ addY_I2(topLeft_Rect(rect),
                             /* Active root indicator is also a line at the top,
                                so need a little offset if in split view. */
                             numRoots_Window(window_Widget(d)) > 1 ? gap_UI / 2 : (gap_UI / 4)),
                     init_I2(pos * width_Rect(rect), gap_UI / 3) },
            colors[isCompleted_IndicatorWidget_(d) ? 1 : 0]);
    }
}

iBool processEvent_IndicatorWidget_(iIndicatorWidget *d, const SDL_Event *ev) {
    iWidget *w = &d->widget;
    if (!d->isManual && isCommand_SDLEvent(ev)) {
        const char *cmd = command_UserEvent(ev);
        if (startsWith_Command(cmd, "document.request.")) {
            if (pointerLabel_Command(cmd, "doc") == parent_Widget(w)) {
                cmd = skipPrefix_Command(cmd) + 17;
                if (equal_Command(cmd, "started")) {
                    setValue_Anim(&d->pos, 0, 0);
                    if (isLocalUrl_(suffixPtr_Command(cmd, "url"))) {
                        refresh_Widget(d);
                        return iFalse; /* no progress to show */
                    }
                    start_IndicatorWidget(d);
                }
                else if (equal_Command(cmd, "finished")) {
                    finish_IndicatorWidget(d);
                }
                else if (equal_Command(cmd, "cancelled")) {
                    setValue_Anim(&d->pos, 0, 0);
                    animate_IndicatorWidget_(d);
                    refresh_Widget(d);
                }
            }
        }
    }
    return iFalse;
}

iBeginDefineSubclass(IndicatorWidget, Widget)
    .draw         = (iAny *) draw_IndicatorWidget_,
    .processEvent = (iAny *) processEvent_IndicatorWidget_,
    .rootChanged  = (iAny *) rootChanged_IndicatorWidget_,
iEndDefineSubclass(IndicatorWidget)
