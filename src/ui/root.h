#pragma once

#include "widget.h"
#include <the_Foundation/ptrset.h>
#include <the_Foundation/vec2.h>

iDeclareType(Root)
    
struct Impl_Root {
    iWidget *  widget;
    iWidget *  hover;
    iWidget *  mouseGrab;
    iWidget *  focus;
    iPtrArray *onTop; /* order is important; last one is topmost */
    iPtrSet *  pendingDestruction;
    int        loadAnimTimer;
};

iDeclareTypeConstruction(Root)

/*----------------------------------------------------------------------------------------------*/

void        createUserInterface_Root            (iRoot *);

void        setCurrent_Root                     (iRoot *);
iRoot *     get_Root                            (void);

iPtrArray * onTop_Root                          (iRoot *);
void        destroyPending_Root                 (iRoot *);
    
void        updateMetrics_Root                  (iRoot *);
void        updatePadding_Root                  (iRoot *); /* TODO: is part of metrics? */
void        dismissPortraitPhoneSidebars_Root   (iRoot *);
void        showToolbars_Root                   (iRoot *, iBool show);

iInt2       size_Root                           (const iRoot *);
iRect       rect_Root                           (const iRoot *);
iRect       safeRect_Root                       (const iRoot *);
iInt2       visibleSize_Root                    (const iRoot *); /* may be obstructed by software keyboard */
iBool       isNarrow_Root                       (const iRoot *);
