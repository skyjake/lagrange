#pragma once

#include "widget.h"
#include <the_Foundation/range.h>

iDeclareWidgetClass(ScrollWidget)
iDeclareObjectConstruction(ScrollWidget)

void    setRange_ScrollWidget   (iScrollWidget *, iRangei range);
void    setThumb_ScrollWidget   (iScrollWidget *, int thumb, int thumbSize);

