#pragma once

#include "widget.h"

iWidget *   createUserInterface_Root            (void);

void        setCurrent_Root                     (iWidget *root);
iWidget *   get_Root                            (void);

void        updateMetrics_Root                  (iWidget *);
void        updatePadding_Root                  (iWidget *); /* TODO: is part of metrics? */
void        dismissPortraitPhoneSidebars_Root   (iWidget *);
void        showToolbars_Root                   (iWidget *, iBool show);
