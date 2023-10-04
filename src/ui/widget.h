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

#pragma once

/* Base class for UI widgets. */

#include "metrics.h"
#include "util.h"

#include <the_Foundation/object.h>
#include <the_Foundation/objectlist.h>
#include <the_Foundation/ptrarray.h>
#include <the_Foundation/rect.h>
#include <the_Foundation/string.h>
#include <SDL_events.h>

iDeclareType(Root)   /* each widget is associated with a Root */
iDeclareType(Window) /* each Root is inside a Window */

#define iDeclareWidgetClass(className) \
    iDeclareType(className); \
    typedef iWidgetClass i##className##Class; \
    extern i##className##Class Class_##className;

iDeclareType(Widget)
iBeginDeclareClass(Widget)
    iBool (*processEvent)   (iWidget *, const SDL_Event *);
    void  (*draw)           (const iWidget *);
    void  (*sizeChanged)    (iWidget *); /* optional, defaults to NULL */
    void  (*rootChanged)    (iWidget *); /* optional, defaults to NULL */
iEndDeclareClass(Widget)

enum iWidgetFlag {
    hidden_WidgetFlag             = iBit(1),
    disabled_WidgetFlag           = iBit(2),
    hover_WidgetFlag              = iBit(3), /* eligible for mouse hover */
    selected_WidgetFlag           = iBit(4),
    pressed_WidgetFlag            = iBit(5),
    alignLeft_WidgetFlag          = iBit(6),
    alignRight_WidgetFlag         = iBit(7),
    frameless_WidgetFlag          = iBit(8),
    commandOnClick_WidgetFlag     = iBit(9),
    commandOnMouseMiss_WidgetFlag = iBit(10),
    drawKey_WidgetFlag            = iBit(11), /* TODO: LabelWidget-specific! */
    focusable_WidgetFlag          = iBit(12),
    tight_WidgetFlag              = iBit(13), /* smaller padding */
    keepOnTop_WidgetFlag          = iBit(14), /* gets events first; drawn last */
    mouseModal_WidgetFlag         = iBit(15), /* eats all unprocessed mouse events */
    radio_WidgetFlag              = iBit(16), /* select on click; deselect siblings */
    /* arrangement */
    fixedPosition_WidgetFlag               = iBit(17),
    arrangeHorizontal_WidgetFlag           = iBit(18), /* arrange children horizontally */
    arrangeVertical_WidgetFlag             = iBit(19), /* arrange children vertically */
    arrangeWidth_WidgetFlag                = iBit(20), /* area of children becomes parent size */
    arrangeHeight_WidgetFlag               = iBit(21), /* area of children becomes parent size */
    resizeWidthOfChildren_WidgetFlag       = iBit(22),
    resizeHeightOfChildren_WidgetFlag      = iBit(23),
    expand_WidgetFlag                      = iBit(24),
    fixedWidth_WidgetFlag                  = iBit(25),
    fixedHeight_WidgetFlag                 = iBit(26),
    resizeChildrenToWidestChild_WidgetFlag = iBit(27),
    resizeToParentWidth_WidgetFlag         = iBit(28),
    resizeToParentHeight_WidgetFlag        = iBit(29),
    collapse_WidgetFlag                    = iBit(30), /* if hidden, arrange size to zero */
    /* combinations */
    arrangeSize_WidgetFlag    = arrangeWidth_WidgetFlag | arrangeHeight_WidgetFlag,
    resizeChildren_WidgetFlag = resizeWidthOfChildren_WidgetFlag | resizeHeightOfChildren_WidgetFlag,
    fixedSize_WidgetFlag      = fixedWidth_WidgetFlag | fixedHeight_WidgetFlag,
};

/* 64-bit extended flags */
#define rightEdgeDraggable_WidgetFlag       iBit64(31)
#define disabledWhenHidden_WidgetFlag       iBit64(32)
#define centerHorizontal_WidgetFlag         iBit64(33)
#define moveToParentLeftEdge_WidgetFlag     iBit64(34)
#define moveToParentRightEdge_WidgetFlag    iBit64(35)
#define noShadowBorder_WidgetFlag           iBit64(36)
#define borderTop_WidgetFlag                iBit64(37)
#define overflowScrollable_WidgetFlag       iBit64(38)
#define focusRoot_WidgetFlag                iBit64(39)
#define unhittable_WidgetFlag               iBit64(40)
#define touchDrag_WidgetFlag                iBit64(41) /* touch event behavior: immediate drag */
#define noBackground_WidgetFlag             iBit64(42)
#define drawBackgroundToHorizontalSafeArea_WidgetFlag   iBit64(43)
#define drawBackgroundToVerticalSafeArea_WidgetFlag     iBit64(44)
#define visualOffset_WidgetFlag             iBit64(45)
#define parentCannotResize_WidgetFlag       iBit64(46)
#define ignoreForParentHeight_WidgetFlag    iBit64(47)
#define unpadded_WidgetFlag                 iBit64(48) /* ignore parent's padding */
#define extraPadding_WidgetFlag             iBit64(49)
#define borderBottom_WidgetFlag             iBit64(50)
#define horizontalOffset_WidgetFlag         iBit64(51) /* default is vertical offset */
#define visibleOnParentHover_WidgetFlag     iBit64(52)
#define drawBackgroundToBottom_WidgetFlag   iBit64(53)
#define dragged_WidgetFlag                  iBit64(54)
#define hittable_WidgetFlag                 iBit64(55)
#define safePadding_WidgetFlag              iBit64(56) /* padded using safe area insets */
#define moveToParentBottomEdge_WidgetFlag   iBit64(57)
#define parentCannotResizeHeight_WidgetFlag iBit64(58)
#define ignoreForParentWidth_WidgetFlag     iBit64(59)
#define noFadeBackground_WidgetFlag         iBit64(60)
#define destroyPending_WidgetFlag           iBit64(61)
#define leftEdgeDraggable_WidgetFlag        iBit64(62)
#define refChildrenOffset_WidgetFlag        iBit64(63) /* visual offset determined by the offset of referenced children */
#define nativeMenu_WidgetFlag               iBit64(64)

enum iWidgetFlag2 {
    slidingSheetDraggable_WidgetFlag2       = iBit(1),
    fadeBackground_WidgetFlag2              = iBit(2),
    visibleOnParentSelected_WidgetFlag2     = iBit(3),
    permanentVisualOffset_WidgetFlag2       = iBit(4), /* usually visual offset overrides hiding */
    commandOnHover_WidgetFlag2              = iBit(5), /* only dispatched to the hovered widget */
    centerChildrenVertical_WidgetFlag2      = iBit(6), /* pad top and bottom to center children in the middle */
    usedAsPeriodicContext_WidgetFlag2       = iBit(7), /* add_Periodic() called on the widget */
    siblingOrderDraggable_WidgetFlag2       = iBit(8),
    horizontallyResizable_WidgetFlag2       = iBit(9), /* may drag left/right edges to resize */
    leftEdgeResizing_WidgetFlag2            = iBit(10),
    rightEdgeResizing_WidgetFlag2           = iBit(11),
    childMenuOpenedAsPopup_WidgetFlag2      = iBit(12),
};

enum iWidgetAddPos {
    back_WidgetAddPos,
    front_WidgetAddPos,
};

enum iWidgetFocusDir {
    forward_WidgetFocusDir,
    backward_WidgetFocusDir,
    /* flags: */
    dirMask_WidgetFocusFlag = 0x1,
    notInput_WidgetFocusFlag = 0x100,
};

iDeclareType(WidgetDrawBuffer)

struct Impl_Widget {
    iObject      object;
    iString      id;
    iString      resizeId; /* if user-resized width will be remembered; TODO: apply to sidebars? */
    int64_t      flags;
    int          flags2;
    iRect        rect;
    iInt2        oldSize; /* in previous arrangement; for notification */
    iInt2        minSize;
    iWidget *    sizeRef;
    iWidget *    offsetRef;
    int          padding[4]; /* left, top, right, bottom */
    int          overflowTopMargin; /* keep clear of this much space at the top */
    iAnim        visualOffset;
    int          bgColor;
    int          frameColor;
    iObjectList *children;
    iWidget *    parent;
    iRoot *      root;
    iWidgetDrawBuffer *drawBuf;
    iAnim        overflowScrollOpacity; /* scrollbar fading */
    iString      data; /* custom user data */
    /* Callbacks. */
    iBool      (*commandHandler)(iWidget *, const char *);
    const iArray *(*updateMenuItems)(iWidget *); /* returns the updated items for the menu */
    void       (*menuClosed)(iWidget *);
};

iDeclareObjectConstruction(Widget)

iLocalDef iWidget *as_Widget(iAnyObject *d) {
#if !defined (NDEBUG)
    if (d) {
        iAssertIsObject(d);
        iAssert(isInstance_Object(d, &Class_Widget));
    }
#endif
    return (iWidget *) d;
}

iLocalDef const iWidget *constAs_Widget(const iAnyObject *d) {
#if !defined (NDEBUG)
    if (d) {
        iAssertIsObject(d);
        iAssert(isInstance_Object(d, &Class_Widget));
    }
#endif
    return (const iWidget *) d;
}

void    destroy_Widget          (iWidget *); /* widget removed and deleted later */
void    destroyPending_Widget   (void);
void    releaseChildren_Widget  (iWidget *);

/* Coordinate spaces:
    - window: 0,0 is at the top left corner of the window
    - local:  0,0 is at the top left corner of the parent widget
    - inner:  0,0 is at the top left corner of the widget */

iWidget *       root_Widget             (const iWidget *);
iWindow *       window_Widget           (const iAnyObject *);
const iString * id_Widget               (const iWidget *);
int64_t flags_Widget                    (const iWidget *);
iRect   bounds_Widget                   (const iWidget *); /* outer bounds */
iRect   innerBounds_Widget              (const iWidget *);
iRect   boundsWithoutVisualOffset_Widget(const iWidget *);
iInt2   localToWindow_Widget            (const iWidget *, iInt2 localCoord);
iInt2   windowToLocal_Widget            (const iWidget *, iInt2 windowCoord);
iInt2   innerToWindow_Widget            (const iWidget *, iInt2 innerCoord);
iInt2   windowToInner_Widget            (const iWidget *, iInt2 windowCoord);
iBool   contains_Widget                 (const iWidget *, iInt2 windowCoord);
iBool   containsExpanded_Widget         (const iWidget *, iInt2 windowCoord, int expand);
iAny *  hitChild_Widget                 (const iWidget *, iInt2 windowCoord);
iAny *  findChild_Widget                (const iWidget *, const char *id);
const iPtrArray *findChildren_Widget    (const iWidget *, const char *id);
iAny *  findParent_Widget               (const iWidget *, const char *id);
iAny *  findParentClass_Widget          (const iWidget *, const iAnyClass *class);
iAny *  findFocusable_Widget            (const iWidget *startFrom, enum iWidgetFocusDir focusDir);
iAny *  findOverflowScrollable_Widget   (iWidget *);
size_t  childCount_Widget               (const iWidget *);
void    draw_Widget                     (const iWidget *);
void    drawLayerEffects_Widget         (const iWidget *);
void    drawBackground_Widget           (const iWidget *);
void    drawBorders_Widget              (const iWidget *); /* called by `drawBackground` */
void    drawChildren_Widget             (const iWidget *);
void    drawRoot_Widget                 (const iWidget *); /* root only */
void    setDrawBufferEnabled_Widget     (iWidget *, iBool enable);

iLocalDef iBool isDrawBufferEnabled_Widget(const iWidget *d) {
    return d && d->drawBuf;
}

iLocalDef int width_Widget(const iAnyObject *d) {
    if (d) {
        iAssert(isInstance_Object(d, &Class_Widget));
        return ((const iWidget *) d)->rect.size.x;
    }
    return 0;
}
iLocalDef int height_Widget(const iAnyObject *d) {
    if (d) {
        iAssert(isInstance_Object(d, &Class_Widget));
        return ((const iWidget *) d)->rect.size.y;
    }
    return 0;
}
iLocalDef int leftPad_Widget(const iWidget *d) {
    return d->padding[0];
}
iLocalDef int topPad_Widget(const iWidget *d) {
    return d->padding[1];
}
iLocalDef int rightPad_Widget(const iWidget *d) {
    return d->padding[2];
}
iLocalDef int bottomPad_Widget(const iWidget *d) {
    return d->padding[3];
}
iLocalDef iInt2 tlPad_Widget(const iWidget *d) {
    return init_I2(leftPad_Widget(d), topPad_Widget(d));
}
iLocalDef iInt2 brPad_Widget(const iWidget *d) {
    return init_I2(rightPad_Widget(d), bottomPad_Widget(d));
}
iLocalDef iObjectList *children_Widget(iAnyObject *d) {
    if (d == NULL) return NULL;
    iAssert(isInstance_Object(d, &Class_Widget));
    return ((iWidget *) d)->children;
}
iLocalDef iWidget *parent_Widget(const iAnyObject *d) {
    if (d) {
        iAssert(isInstance_Object(d, &Class_Widget));
        return ((iWidget *) d)->parent;
    }
    return NULL;
}
iLocalDef iWidget *lastChild_Widget(iAnyObject *d) {
    return (iWidget *) back_ObjectList(children_Widget(d));
}

iBool   isVisible_Widget            (const iAnyObject *);
iBool   isDisabled_Widget           (const iAnyObject *);
iBool   isFocused_Widget            (const iAnyObject *);
iBool   isHover_Widget              (const iAnyObject *);
iBool   isSelected_Widget           (const iAnyObject *);
iBool   isUnderKeyRoot_Widget       (const iAnyObject *);
iBool   isCommand_Widget            (const iWidget *, const SDL_Event *ev, const char *cmd);
iBool   isExtraWindowSizeInfluencer_Widget(const iWidget *);
iBool   hasParent_Widget            (const iWidget *, const iWidget *someParent);
iBool   isAffectedByVisualOffset_Widget         (const iWidget *);
iBool   isBeingVisuallyOffsetByReference_Widget (const iWidget *);
int     visualOffsetByReference_Widget          (const iWidget *);
void    setId_Widget                (iWidget *, const char *id);
void    setResizeId_Widget          (iWidget *, const char *resizeId); /* no need to be unique */
void    setFlags_Widget             (iWidget *, int64_t flags, iBool set);
void    setTreeFlags_Widget         (iWidget *, int64_t flags, iBool set);
void    setPos_Widget               (iWidget *, iInt2 pos);
void    setFixedSize_Widget         (iWidget *, iInt2 fixedSize);
void    setMinSize_Widget           (iWidget *, iInt2 minSize);
void    setPadding_Widget           (iWidget *, int left, int top, int right, int bottom);
iLocalDef void setPadding1_Widget   (iWidget *d, int padding) { setPadding_Widget(d, padding, padding, padding, padding); }
void    setVisualOffset_Widget      (iWidget *, int value, uint32_t span, int animFlags);
void    showCollapsed_Widget        (iWidget *, iBool show); /* takes care of rearranging, refresh */
void    setBackgroundColor_Widget   (iWidget *, int bgColor);
void    setFrameColor_Widget        (iWidget *, int frameColor);
void    setCommandHandler_Widget    (iWidget *, iBool (*handler)(iWidget *, const char *));
void    setRoot_Widget              (iWidget *, iRoot *root); /* updates the entire tree */
iAny *  addChild_Widget             (iWidget *, iAnyObject *child); /* holds a ref */
iAny *  addChildPos_Widget          (iWidget *, iAnyObject *child, enum iWidgetAddPos addPos);
iAny *  addChildPosFlags_Widget     (iWidget *, iAnyObject *child, enum iWidgetAddPos addPos, int64_t childFlags);
iAny *  addChildFlags_Widget        (iWidget *, iAnyObject *child, int64_t childFlags); /* holds a ref */
iAny *  insertChildAfter_Widget     (iWidget *, iAnyObject *child, size_t afterIndex);
iAny *  insertChildAfterFlags_Widget(iWidget *, iAnyObject *child, size_t afterIndex, int64_t childFlags);
iAny *  removeChild_Widget          (iWidget *, iAnyObject *child); /* returns a ref */
iAny *  child_Widget                (iWidget *, size_t index); /* O(n) */
size_t  indexOfChild_Widget         (const iWidget *, const iAnyObject *child); /* O(n) */
void    changeChildIndex_Widget     (iWidget *, iAnyObject *child, size_t newIndex); /* O(n) */
void    arrange_Widget              (iWidget *);
iBool   scrollOverflow_Widget       (iWidget *, int delta); /* moves the widget */
void    applyInteractiveResize_Widget(iWidget *, int width);
iBool   dispatchEvent_Widget        (iWidget *, const SDL_Event *);
iBool   processEvent_Widget         (iWidget *, const SDL_Event *);
void    postCommand_Widget          (const iAnyObject *, const char *cmd, ...);
void    refresh_Widget              (const iAnyObject *);

iBool   equalWidget_Command (const char *cmd, const iWidget *widget, const char *checkCommand);

iDeclareType(WidgetScrollInfo)

struct Impl_WidgetScrollInfo {
    int   totalHeight; /* widget's height */
    int   visibleHeight;  /* available height */
    float normScroll;
    int   thumbY; /* window coords */
    int   thumbHeight;
};

void        contentScrollInfo_Widget    (const iWidget *, iWidgetScrollInfo *info, int contentPos, int contentMax);
void        overflowScrollInfo_Widget   (const iWidget *, iWidgetScrollInfo *info);
void        drawScrollIndicator_Widget  (const iWidget *, const iWidgetScrollInfo *info, int color, float opacity);

int         backgroundFadeColor_Widget  (void);

const iWidget *focusRoot_Widget     (const iWidget *);
void        setFocus_Widget         (iWidget *); /* widget must be flagged `focusable` */
void        setKeyboardGrab_Widget  (iWidget *); /* sets focus on any widget */
iWidget *   focus_Widget            (void);
iBool       setHover_Widget         (iWidget *);
iWidget *   hover_Widget            (void);
void        unhover_Widget          (void);
void        setMouseGrab_Widget     (iWidget *);
iWidget *   mouseGrab_Widget        (void);
void        raise_Widget            (iWidget *);
//void        setDelayedCallback_Widget(iWidget *, int delay, void (*)(iWidget *));
iBool       hasVisibleChildOnTop_Widget
                                    (const iWidget *parent);
void        printTree_Widget        (const iWidget *);
void        identify_Widget         (const iWidget *); /* prints to stdout */

void        addRecentlyDeleted_Widget   (iAnyObject *obj);
iBool       isRecentlyDeleted_Widget    (const iAnyObject *obj);
void        clearRecentlyDeleted_Widget (void);

