#pragma once

#include "widget.h"
#include "color.h"
#include <the_Foundation/audience.h>
#include <the_Foundation/ptrset.h>
#include <the_Foundation/vec2.h>

iDeclareType(Root)

iDeclareNotifyFunc(Root, VisualOffsetsChanged)
iDeclareAudienceGetter(Root, visualOffsetsChanged)

struct Impl_Root {
    iWidget *  widget;
    iWindow *  window;
    iPtrArray *onTop; /* order is important; last one is topmost */
    iPtrSet *  pendingDestruction;
    iBool      pendingArrange;
    int        loadAnimTimer;
    iBool      didAnimateVisualOffsets;
    iBool      didOverflowScroll;
    iAudience *visualOffsetsChanged; /* called after running tickers */
    iColor     tmPalette[tmMax_ColorId]; /* theme-specific palette */
};

iDeclareTypeConstruction(Root)

/*----------------------------------------------------------------------------------------------*/

void        createUserInterface_Root            (iRoot *);

void        setCurrent_Root                     (iRoot *);
iRoot *     current_Root                        (void);
iRoot *     get_Root                            (void); /* assert != NULL */
iAnyObject *findWidget_Root                     (const char *id); /* under current Root */

iPtrArray * onTop_Root                          (iRoot *);
void        destroyPending_Root                 (iRoot *);

void        updateMetrics_Root                  (iRoot *);
void        updatePadding_Root                  (iRoot *); /* TODO: is part of metrics? */
void        dismissPortraitPhoneSidebars_Root   (iRoot *);
void        showToolbar_Root                    (iRoot *, iBool show);
void        updateToolbarColors_Root            (iRoot *);
void        notifyVisualOffsetChange_Root       (iRoot *);

iInt2       size_Root                           (const iRoot *);
iRect       rect_Root                           (const iRoot *);
iRect       safeRect_Root                       (const iRoot *);
iRect       visibleRect_Root                    (const iRoot *); /* may be obstructed by software keyboard */
iBool       isNarrow_Root                       (const iRoot *);
int         appIconSize_Root                    (void);
