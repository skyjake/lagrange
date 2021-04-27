#pragma once

#include "widget.h"
#include <the_Foundation/ptrset.h>

iDeclareType(RootData)
    
/* TODO: Rename to Root, include `iWidget *root` as well. */
struct Impl_RootData {
    iWidget *  hover;
    iWidget *  mouseGrab;
    iWidget *  focus;
    iPtrArray *onTop; /* order is important; last one is topmost */
    iPtrSet *  pendingDestruction;
};

/*----------------------------------------------------------------------------------------------*/

iWidget *   createUserInterface_Root            (void);

void        setCurrent_Root                     (iWidget *root, iRootData *rootData);
iWidget *   get_Root                            (void);
iRootData * data_Root                           (void);

iPtrArray * onTop_RootData                      (void);
void        destroyPending_RootData             (iRootData *);
    
void        updateMetrics_Root                  (iWidget *);
void        updatePadding_Root                  (iWidget *); /* TODO: is part of metrics? */
void        dismissPortraitPhoneSidebars_Root   (iWidget *);
void        showToolbars_Root                   (iWidget *, iBool show);
