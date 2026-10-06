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

#pragma once

/* Horizontal slider for picking a numeric value within a range. */

#include "widget.h"

iDeclareType(MenuItem)

enum iSliderUnit {
    none_SliderUnit,
    times_SliderUnit,
    percent_SliderUnit,
    kilobytes_SliderUnit,
};

iDeclareWidgetClass(SliderWidget)
iDeclareObjectConstructionArgs(SliderWidget, const char *command)

/* Spec: "slider id:X cmd:C min:A max:B step:S bigstep:L prec:N unit:x|percent|kb sign:1".
   Data is an optional array of "tick value:V label:L" or "tick value:V text:T" items. */
iSliderWidget *newMenuItem_SliderWidget(const iMenuItem *item);

void    setRange_SliderWidget       (iSliderWidget *, float min, float max, float step);
void    setBigStep_SliderWidget     (iSliderWidget *, float bigStep);
void    setPrecision_SliderWidget   (iSliderWidget *, int precision);
void    setUnit_SliderWidget        (iSliderWidget *, enum iSliderUnit unit);
void    setShowSign_SliderWidget    (iSliderWidget *, iBool showSign);
void    setFont_SliderWidget        (iSliderWidget *, int fontId);
void    addLabel_SliderWidget       (iSliderWidget *, float value, const char *label);
void    setValue_SliderWidget       (iSliderWidget *, float value); /* no command is posted */

float   value_SliderWidget          (const iSliderWidget *); /* unsnapped if not changed by user */
