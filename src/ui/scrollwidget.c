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

#include "scrollwidget.h"
#include "paint.h"
#include "util.h"
#include "periodic.h"
#include "app.h"

#include <SDL_timer.h>

iDefineObjectConstruction(ScrollWidget)

#if defined (iPlatformTerminal)
const int fadeTime_ScrollWidget_   = 10;
const int unfadeTime_ScrollWidget_ = 10;
#else
const int fadeTime_ScrollWidget_   = 200;
const int unfadeTime_ScrollWidget_ = 66;
#endif

static float minOpacity_(void) {
#if !defined (iPlatformApple)
    if (deviceType_App() == desktop_AppDeviceType) {
        /* Don't fade the scrollbars completely. */
        return 0.333f;
    }
#endif
    return 0.0f;
}

struct Impl_ScrollWidget {
    iWidget widget;
    iRangei range;
    int thumb;
    int thumbSize;
    int thumbColor;
    iClick click;
    int startThumb;
    iAnim opacity;
    iBool fadeEnabled;
    uint32_t fadeStart;
    iBool willCheckFade;
};

static void updateMetrics_ScrollWidget_(iScrollWidget *d) {
    iWidget *w = as_Widget(d);
    w->rect.size.x = gap_UI * 3;
}

static void animateOpacity_ScrollWidget_(void *ptr) {
    iScrollWidget *d = ptr;
    if (!isFinished_Anim(&d->opacity)) {
        addTicker_App(animateOpacity_ScrollWidget_, ptr);
    }
    refresh_Widget(ptr);
}

void init_ScrollWidget(iScrollWidget *d) {
    iWidget *w = as_Widget(d);
    init_Widget(w);
    setId_Widget(w, "scroll");
    setFlags_Widget(w,
                    fixedWidth_WidgetFlag | resizeToParentHeight_WidgetFlag |
                        moveToParentRightEdge_WidgetFlag | touchDrag_WidgetFlag,
                    iTrue);
    updateMetrics_ScrollWidget_(d);
    init_Click(&d->click, d, SDL_BUTTON_LEFT);
    init_Anim(&d->opacity, minOpacity_());
    d->willCheckFade = iFalse;
    d->fadeEnabled = iTrue;
    d->thumbColor = uiBackgroundPressed_ColorId;
}

void deinit_ScrollWidget(iScrollWidget *d) {
    remove_Periodic(periodic_App(), d);
    removeTicker_App(animateOpacity_ScrollWidget_, d);
}

static int thumbSize_ScrollWidget_(const iScrollWidget *d) {
    return iMax(gap_UI * 6, d->thumbSize);
}

static iRect bounds_ScrollWidget_(const iScrollWidget *d) {
    const iWidget *w = constAs_Widget(d);
    iRect bounds = bounds_Widget(w);
    if (deviceType_App() == phone_AppDeviceType && isPortrait_App()) {
        /* Account for the hidable toolbar. */
        /* TODO: use height of "bottombar" */
        int toolbarHeight = lineHeight_Text(uiLabelLarge_FontId) + 3 * gap_UI;
        int excess = bottom_Rect(bounds) - (bottom_Rect(safeRect_Root(w->root)) - toolbarHeight);
        if (excess > 0) {
            adjustEdges_Rect(&bounds, 0, 0, -excess, 0);
        }
    }
    return bounds;
}

static iRect thumbRect_ScrollWidget_(const iScrollWidget *d) {
    const iRect bounds = bounds_ScrollWidget_(d);
    iRect rect = init_Rect(bounds.pos.x, bounds.pos.y, bounds.size.x, 0);
    const int total = (int) size_Range(&d->range);
    if (total > 0) {
        const int tsize = thumbSize_ScrollWidget_(d);
        if (tsize > height_Rect(bounds) * 0.95f) {
            return rect;
        }
        const int tpos =
            iClamp((float) d->thumb / (float) total, 0, 1) * (height_Rect(bounds) - tsize);
        rect.pos.y  = bounds.pos.y + tpos;
        rect.size.y = tsize;
    }
    return rect;
}

static void unfade_ScrollWidget_(iScrollWidget *d, float opacity) {
    d->fadeStart = SDL_GetTicks() + 1000;
    if (targetValue_Anim(&d->opacity) < opacity) {
        setValue_Anim(&d->opacity, opacity, unfadeTime_ScrollWidget_);
        addTickerRoot_App(animateOpacity_ScrollWidget_, as_Widget(d)->root, d);
    }
    if (!d->willCheckFade && d->fadeEnabled) {
        d->willCheckFade = iTrue;
        add_Periodic(periodic_App(), d, "scrollbar.fade");
    }
    refresh_Widget(d);
}

static void checkVisible_ScrollWidget_(iScrollWidget *d) {
    const iBool wasHidden = !isVisible_Widget(d);
    const iBool isHidden = d->thumbSize != 0 ? height_Rect(thumbRect_ScrollWidget_(d)) == 0 : iTrue;
    setFlags_Widget(as_Widget(d), hidden_WidgetFlag, isHidden);
    if (wasHidden && !isHidden) {
        unfade_ScrollWidget_(d, 1.0f);
    }
}

static void rootChanged_ScrollWidget_(iScrollWidget *d) {
    removeTicker_App(animateOpacity_ScrollWidget_, d);
    setValue_Anim(&d->opacity, 0.0f, 0);
}

void setRange_ScrollWidget(iScrollWidget *d, iRangei range) {
    range.end = iMax(range.start, range.end);
    d->range  = range;
    checkVisible_ScrollWidget_(d);
}

void setThumbColor_ScrollWidget(iScrollWidget *d, int thumbColor) {
    d->thumbColor = thumbColor;
}

void setThumb_ScrollWidget(iScrollWidget *d, int thumb, int thumbSize) {
    const int oldThumb = d->thumb;
    d->thumb     = thumb;
    d->thumbSize = thumbSize;
    checkVisible_ScrollWidget_(d);
    if (oldThumb != d->thumb && thumbSize) {
        unfade_ScrollWidget_(d, 1.0f);
    }
}

void setFadeEnabled_ScrollWidget(iScrollWidget *d, iBool fadeEnabled) {
    d->fadeEnabled = fadeEnabled;
    unfade_ScrollWidget_(d, 1.0f);
}

static iBool processEvent_ScrollWidget_(iScrollWidget *d, const SDL_Event *ev) {
    iWidget *w = as_Widget(d);
    if (isMetricsChange_UserEvent(ev)) {
        updateMetrics_ScrollWidget_(d);
    }
    if (ev->type == SDL_MOUSEMOTION) {
        const iInt2 mouse = init_I2(ev->motion.x, ev->motion.y);
        const iBool isNearby = containsExpanded_Widget(&d->widget, mouse, 4 * gap_UI);
        const iBool isOver = isNearby && contains_Rect(thumbRect_ScrollWidget_(d), mouse);
        if (isNearby) {
            unfade_ScrollWidget_(d, isOver ? 1.0f : 0.4f);
        }
    }
    if (isCommand_UserEvent(ev, "scrollbar.fade")) {
        if (d->fadeEnabled && d->willCheckFade && SDL_GetTicks() > d->fadeStart) {
            setValue_Anim(&d->opacity, minOpacity_(), fadeTime_ScrollWidget_);
            remove_Periodic(periodic_App(), d);
            d->willCheckFade = iFalse;
            if (!isFinished_Anim(&d->opacity)) {
                addTicker_App(animateOpacity_ScrollWidget_, d);
            }
        }
        return iFalse;
    }
    switch (processEvent_Click(&d->click, ev)) {
        case started_ClickResult:
            setFlags_Widget(w, pressed_WidgetFlag, iTrue);
            d->startThumb = d->thumb;
            refresh_Widget(w);
            return iTrue;
        case drag_ClickResult: {
            const iRect bounds = bounds_ScrollWidget_(d);
            const int offset = delta_Click(&d->click).y;
            const int total = (int) size_Range(&d->range);
            int dpos = (float) offset / (float) (height_Rect(bounds) - thumbSize_ScrollWidget_(d)) * total;
            d->thumb = iClamp(d->startThumb + dpos, d->range.start, d->range.end);
            postCommand_Widget(w, "scroll.moved arg:%d", d->thumb);
            refresh_Widget(w);
            return iTrue;
        }
        case finished_ClickResult:
        case aborted_ClickResult:
            if (!isMoved_Click(&d->click)) {
                /* Page up/down. */
                const iRect tr = thumbRect_ScrollWidget_(d);
                const int y = pos_Click(&d->click).y;
                int pgDir = 0;
                if (y < top_Rect(tr)) {
                    pgDir = -1;
                }
                else if (y > bottom_Rect(tr)) {
                    pgDir = +1;
                }
                if (pgDir) {
                    postCommand_Widget(w, "scroll.page arg:%d", pgDir);
                }
            }
            setFlags_Widget(w, pressed_WidgetFlag, iFalse);
            refresh_Widget(w);
            return iTrue;
        default:
            break;
    }
    return processEvent_Widget(w, ev);
}

static void draw_ScrollWidget_(const iScrollWidget *d) {
    const iWidget *w         = constAs_Widget(d);
    const iRect    bounds    = bounds_ScrollWidget_(d);
    const iBool    isPressed = (flags_Widget(w) & pressed_WidgetFlag) != 0;
    if (bounds.size.x > 0) {
        iPaint p;
        init_Paint(&p);
        /* Blend if opacity is not at maximum. */
        p.alpha = 255 * value_Anim(&d->opacity);
#if defined (iPlatformMobile)
        p.alpha /= 2;
#endif
        if (p.alpha > 0) {
            SDL_Renderer *render = renderer_Window(get_Window());
            if (p.alpha < 255) {
                SDL_SetRenderDrawBlendMode(render, SDL_BLENDMODE_BLEND);
            }
            const iRect thumbRect = shrunk_Rect(
                thumbRect_ScrollWidget_(d), init_I2(isPressed ? gap_UI : (gap_UI * 4 / 3), gap_UI / 2));
            fillRect_Paint(&p, thumbRect, isPressed ? uiBackgroundPressed_ColorId : d->thumbColor);
            if (p.alpha < 255) {
                SDL_SetRenderDrawBlendMode(render, SDL_BLENDMODE_NONE);
            }
        }
    }
}

iBeginDefineSubclass(ScrollWidget, Widget)
    .processEvent = (iAny *) processEvent_ScrollWidget_,
    .draw         = (iAny *) draw_ScrollWidget_,
    .rootChanged  = (iAny *) rootChanged_ScrollWidget_,
iEndDefineSubclass(ScrollWidget)
