#include "metrics.h"

#include <the_Foundation/math.h>

#define defaultFontSize_Metrics     20
#define defaultGap_Metrics          4

int   gap_UI      = defaultGap_Metrics;
iInt2 gap2_UI     = { defaultGap_Metrics, defaultGap_Metrics };
int   fontSize_UI = defaultFontSize_Metrics;

void setPixelRatio_Metrics(float pixelRatio) {
    gap_UI      = iRound(defaultGap_Metrics * pixelRatio);
    gap2_UI     = init1_I2(gap_UI);
    fontSize_UI = iRound(defaultFontSize_Metrics * pixelRatio);
}
