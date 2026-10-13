#pragma once

#include "widget.h"
#include "color.h"
#include <the_Foundation/audience.h>
#include <the_Foundation/ptrset.h>
#include <the_Foundation/vec2.h>

iDeclareType(DocumentWidget)

iDeclareType(Root)
iDeclareNotifyFunc(Root, ArrangementChanged)
iDeclareNotifyFunc(Root, VisualOffsetsChanged)
iDeclareAudienceGetter(Root, arrangementChanged)
iDeclareAudienceGetter(Root, visualOffsetsChanged)

struct Impl_Root {
    iWidget *  widget;
    iWindow *  window;
    iPtrArray *onTop; /* order is important; last one is topmost */
    iPtrSet *  pendingDestruction;
    int        pendingArrange; /* incremented counter */
    int        loadAnimTimer;
    int        loadAnimIndex;
    iBool      didAnimateVisualOffsets;
    iBool      didChangeArrangement;
    iAudience *arrangementChanged;
    iAudience *visualOffsetsChanged; /* called after running tickers */
    iColor     tmPalette[tmMax_ColorId]; /* theme-specific palette */
    iString    tabInsertId; /* place new tab next to this one */
    iBool      isReading;   /* bars float over a paginated document */
    iBool      areReadingBarsHidden;
    int        readingBarsRest[2]; /* visual offsets of the shown navbar and tab bar */
    struct {
        iWidget *navDiv;
        iWidget *navBar;
        iWidget *docTabs;
        iWidget *tabButtons;
        iWidget *searchBar;
    } bars; /* looked up often (e.g., the page viewport), so not searched for by ID */
};

iDeclareTypeConstruction(Root)

iRoot *     newOffscreen_Root                   (void);

void        createUserInterface_Root            (iRoot *);
void        createClipMenu_Root                 (iRoot *);
void        createSplitMenu_Root                (iRoot *, iBool withShortcuts);
void        recreateSnippetMenu_Root            (iRoot *);
const iArray *updateUserDataMenu_Widget          (iWidget *menu);

void        setCurrent_Root                     (iRoot *);
iRoot *     current_Root                        (void);
iRoot *     get_Root                            (void); /* assert != NULL */
iAnyObject *        findWidget_Root             (const char *id); /* under current Root */
iDocumentWidget *   findDocument_Root           (const iRoot *, const iString *url);
iDocumentWidget *   document_Root               (iRoot *);

iPtrArray * onTop_Root                          (iRoot *);
void        destroyPending_Root                 (iRoot *);

void        updateMetrics_Root                  (iRoot *);
void        updatePadding_Root                  (iRoot *); /* TODO: is part of metrics? */
void        dismissPortraitPhoneSidebars_Root   (iRoot *);
void        showToolbar_Root                    (iRoot *, iBool show);
void        enableToolbar_Root                  (iRoot *, iBool enable);
void        updateToolbarColors_Root            (iRoot *);
void        updateNavBarSize_Root               (iRoot *);
void        showOrHideNewTabButton_Root         (iRoot *);

/* Reading mode: the navbar and tab bar float over a paginated document and hide
   automatically, so the document has a fixed area whether or not they are shown. */
void        setReadingMode_Root                 (iRoot *, iBool reading);
iBool       isReadingMode_Root                  (const iRoot *);
void        showReadingBars_Root                (iRoot *, iBool show);
void        toggleReadingBars_Root              (iRoot *);
iRect       readingRect_Root                    (const iRoot *, iRect docBounds); /* area under the bars */
iBool       readingBarsEdge_Root                (const iRoot *, iBool bottom, int *edge_out, float *opacity_out); /* drawn edge facing the document */

void        notifyVisualOffsetChange_Root       (iRoot *);

size_t      windowIndex_Root                    (const iRoot *);
iInt2       size_Root                           (const iRoot *);
iRect       rect_Root                           (const iRoot *);
iRect       safeRect_Root                       (const iRoot *);
iRect       visibleRect_Root                    (const iRoot *); /* may be obstructed by software keyboard */
iBool       isNarrow_Root                       (const iRoot *);
iBool       isSheetOpen_Root                    (const iRoot *);
int         appIconSize_Root                    (void);

iBool       handleRootCommands_Widget           (iWidget *, const char *cmd);
