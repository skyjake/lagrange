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

#include "metrics.h"
#include "app.h"

#include <the_Foundation/math.h>

#define defaultFontSize_Metrics     18
#define defaultGap_Metrics          4

int   gap_UI      = defaultGap_Metrics;
iInt2 gap2_UI     = { defaultGap_Metrics, defaultGap_Metrics };
int   fontSize_UI = defaultFontSize_Metrics;

void setPixelRatio_Metrics(float pixelRatio) {
#if defined (iPlatformAppleMobile)
    /* iPad needs a bit larger UI elements as the viewing distance is generally longer.*/
    if (deviceType_App() == tablet_AppDeviceType) {
        pixelRatio *= 1.1f;
    }
#endif
    gap_UI      = iRound(defaultGap_Metrics * pixelRatio);
    gap2_UI     = init1_I2(gap_UI);
    fontSize_UI = iRound(defaultFontSize_Metrics * pixelRatio);
}
