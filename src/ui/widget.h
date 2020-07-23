#pragma once

/* Base class for UI widgets. */

#include "metrics.h"

#include <the_Foundation/object.h>
#include <the_Foundation/objectlist.h>
#include <the_Foundation/rect.h>
#include <the_Foundation/string.h>
#include <SDL_events.h>

#define iDeclareWidgetClass(className) \
    iDeclareType(className); \
    typedef iWidgetClass i##className##Class; \
    extern i##className##Class Class_##className;

iDeclareType(Widget)
iBeginDeclareClass(Widget)
    iBool (*processEvent)   (iWidget *, const SDL_Event *);
    void  (*draw)           (const iWidget *);
iEndDeclareClass(Widget)

enum iWidgetFlag {
    hidden_WidgetFlag            = iBit(1),
    disabled_WidgetFlag          = iBit(2),
    hover_WidgetFlag             = iBit(3), /* eligible for mouse hover */
    selected_WidgetFlag          = iBit(4),
    pressed_WidgetFlag           = iBit(5),
    alignLeft_WidgetFlag         = iBit(6),
    alignRight_WidgetFlag        = iBit(7),
    frameless_WidgetFlag         = iBit(8),
    drawKey_WidgetFlag           = iBit(10),
    focusable_WidgetFlag         = iBit(11),
    keepOnTop_WidgetFlag         = iBit(12), /* gets events first; drawn last */
    arrangeHorizontal_WidgetFlag = iBit(17), /* arrange children horizontally */
    arrangeVertical_WidgetFlag   = iBit(18), /* arrange children vertically */
    arrangeWidth_WidgetFlag      = iBit(19), /* area of children becomes parent size */
    arrangeHeight_WidgetFlag     = iBit(20), /* area of children becomes parent size */
    arrangeSize_WidgetFlag       = arrangeWidth_WidgetFlag | arrangeHeight_WidgetFlag,
    resizeChildren_WidgetFlag    = iBit(21), /* resize children to fill parent size */
    expand_WidgetFlag            = iBit(22),
    fixedWidth_WidgetFlag        = iBit(23),
    fixedHeight_WidgetFlag       = iBit(24),
    fixedSize_WidgetFlag         = fixedWidth_WidgetFlag | fixedHeight_WidgetFlag,
    resizeChildrenToWidestChild_WidgetFlag = iBit(25),
    resizeToParentWidth_WidgetFlag         = iBit(26),
    resizeToParentHeight_WidgetFlag        = iBit(27),
    moveToParentRightEdge_WidgetFlag       = iBit(28),
};

enum iWidgetAddPos {
    back_WidgetAddPos,
    front_WidgetAddPos,
};

enum iWidgetFocusDir {
    forward_WidgetFocusDir,
    backward_WidgetFocusDir,
};

struct Impl_Widget {
    iObject      object;
    iString      id;
    int          flags;
    iRect        rect;
    int          bgColor;
    iObjectList *children;
    iWidget *    parent;
    iBool (*commandHandler)(iWidget *, const char *);
};

iDeclareObjectConstruction(Widget)

iLocalDef iWidget *as_Widget(iAnyObject *d) {
    if (d) {
        iAssertIsObject(d);
        iAssert(isInstance_Object(d, &Class_Widget));
    }
    return (iWidget *) d;
}

iLocalDef const iWidget *constAs_Widget(const iAnyObject *d) {
    if (d) {
        iAssertIsObject(d);
        iAssert(isInstance_Object(d, &Class_Widget));
    }
    return (const iWidget *) d;
}

void    destroy_Widget      (iWidget *); /* widget removed and deleted later */
void    destroyPending_Widget(void);

const iString *id_Widget    (const iWidget *);
int     flags_Widget        (const iWidget *);
iRect   bounds_Widget       (const iWidget *);
iInt2   localCoord_Widget   (const iWidget *, iInt2 coord);
iBool   contains_Widget     (const iWidget *, iInt2 coord);
iAny *  findChild_Widget    (const iWidget *, const char *id);
iAny *  findFocusable_Widget(const iWidget *startFrom, enum iWidgetFocusDir focusDir);
size_t  childCount_Widget   (const iWidget *);
void    draw_Widget         (const iWidget *);

iBool   isVisible_Widget    (const iWidget *);
iBool   isDisabled_Widget   (const iWidget *);
iBool   isFocused_Widget    (const iWidget *);
iBool   isHover_Widget      (const iWidget *);
iBool   isSelected_Widget   (const iWidget *);
iBool   isCommand_Widget    (const iWidget *d, const SDL_Event *ev, const char *cmd);
iBool   hasParent_Widget    (const iWidget *d, const iWidget *someParent);
void    setId_Widget        (iWidget *, const char *id);
void    setFlags_Widget     (iWidget *, int flags, iBool set);
void    setPos_Widget       (iWidget *, iInt2 pos);
void    setSize_Widget      (iWidget *, iInt2 size);
void    setBackgroundColor_Widget   (iWidget *, int bgColor);
void    setCommandHandler_Widget    (iWidget *, iBool (*handler)(iWidget *, const char *));
iAny *  addChild_Widget     (iWidget *, iAnyObject *child); /* holds a ref */
iAny *  addChildPos_Widget  (iWidget *, iAnyObject *child, enum iWidgetAddPos addPos);
iAny *  addChildFlags_Widget(iWidget *, iAnyObject *child, int childFlags); /* holds a ref */
iAny *  removeChild_Widget  (iWidget *, iAnyObject *child); /* returns a ref */
iAny *  child_Widget        (iWidget *, size_t index); /* O(n) */
void    arrange_Widget      (iWidget *);
iBool   dispatchEvent_Widget(iWidget *, const SDL_Event *);
iBool   processEvent_Widget (iWidget *, const SDL_Event *);
void    postCommand_Widget  (const iWidget *, const char *cmd, ...);
void    refresh_Widget      (const iWidget *);

void    setFocus_Widget     (iWidget *);
iWidget *focus_Widget       (void);
iWidget *hover_Widget       (void);
void    unhover_Widget      (void);
void    setMouseGrab_Widget (iWidget *);
iWidget *mouseGrab_Widget   (void);
