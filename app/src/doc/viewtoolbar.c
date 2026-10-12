/* Copyright 2026 Jaakko Keränen <jaakko.keranen@iki.fi>

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

#include "viewtoolbar.h"
#include "../app.h"
#include "../color.h"
#include "../gmdocument.h"
#include "banner.h"
#include "documentview.h"
#include "pagedview.h"
#include "persistentstate.h"
#include "render/paint.h"
#include "ui/documentwidget.h"
#include "ui/labelwidget.h"
#include "ui/menu.h"
#include "ui/root.h"
#include "ui/scrollwidget.h"
#include "ui/sidebarwidget.h"
#include "render/text.h"
#include "ui/util.h"
#include "ui/widget.h"
#include "ui/window.h"
#include <lagrange/defs.h>
#include <lagrange/gmcerts.h>

static const int      maxNameLength_ViewToolbar_ = 10;
static const float    dimmedOpacity_ViewToolbar_ = 0.0f; /* fully hidden */
static const uint32_t fadeSpan_ViewToolbar_      = 250;

iDefineTypeConstruction(ViewToolbar)

void init_ViewToolbar(iViewToolbar *d) {
    d->owner          = NULL;
    d->widget         = NULL;
    init_Anim(&d->opacity, 1.0f);
    d->isScrolledAway = iFalse;
    d->wasAvailable   = iFalse;
    d->isCompact      = iFalse;
    d->fullWidth      = 0;
    d->headerArea     = zero_Rect();
    d->shownBarsEdge  = 0;
}

static void animate_ViewToolbar_(void *ptr) {
    iViewToolbar *d = ptr;
    refresh_Widget(d->widget);
    if (!isFinished_Anim(&d->opacity)) {
        addTicker_App(animate_ViewToolbar_, d);
    }
}

void deinit_ViewToolbar(iViewToolbar *d) {
    removeTicker_App(animate_ViewToolbar_, d); /* the widget is owned by the document */
}

static iDocumentWidget *menuOwner_ViewToolbar_(iWidget *menu) {
    for (iWidget *w = menu; w; w = parent_Widget(w)) {
        if (isInstance_Object(w, &Class_DocumentWidget)) {
            return (iDocumentWidget *) w;
        }
    }
    return document_App();
}

static const iArray *identityMenuItems_ViewToolbar_(iWidget *menu) {
    const iDocumentWidget *doc      = menuOwner_ViewToolbar_(menu);
    const iGmIdentity     *ident    = identity_DocumentWidget(doc);
    const iBool            isPinned = isIdentityPinned_DocumentWidget(doc);
    iArray                *items    = collectNew_Array(sizeof(iMenuItem));
    pushBack_Array(items,
                   &(iMenuItem){ format_CStr("```" uiHeading_ColorEscape "\x1b[1m%s",
                                             ident ? cstr_String(name_GmIdentity(ident))
                                                   : "${menu.identity.notactive}") });
    if (isPinned) {
        pushBack_Array(items, &(iMenuItem){ close_Icon " ${ident.unset}", 0, 0, "document.unsetident" });
    }
    else if (ident && isUsedOn_GmIdentity(ident, url_DocumentWidget(doc))) {
        pushBack_Array(items,
                       &(iMenuItem){ close_Icon " ${ident.stopuse}",
                                     0,
                                     0,
                                     format_CStr("ident.signout ident:%s url:%s",
                                                 cstrCollect_String(hexEncode_Block(&ident->fingerprint)),
                                                 cstr_String(url_DocumentWidget(doc))) });
    }
    pushBack_Array(items, &(iMenuItem){ "---" });
    pushBack_Array(items, &(iMenuItem){ "---" pin_Icon " ${viewtools.ident.pin}" });
    const size_t first = size_Array(items);
    appendIdentities_MenuItem(items, "document.setident", NULL);
    if (isPinned && ident) {
        const size_t pos = findCommand_MenuItem(
            constAt_Array(items, first),
            size_Array(items) - first,
            format_CStr("document.setident fp:%s",
                        cstrCollect_String(hexEncode_Block(&ident->fingerprint))));
        if (pos != iInvalidPos) {
            remove_Array(items, first + pos);
        }
    }
    if (size_Array(items) == first) {
        /* Nothing to pin, so no submenu. */
        popBack_Array(items);
        pushBack_Array(items, &(iMenuItem){ add_Icon " ${menu.identity.new}", 0, 0, "ident.new" });
    }
    pushBack_Array(items, &(iMenuItem){ NULL });
    return items;
}

static iLabelWidget *addButton_ViewToolbar_(iViewToolbar *d, iLabelWidget *button, const char *id) {
    setAlignVisually_LabelWidget(button, iTrue);
    setFont_LabelWidget(button, uiContent_FontId); /* same as the footer buttons */
    addChildIdFlags_Widget(
        d->widget, iClob(button), id, frameless_WidgetFlag | noBackground_WidgetFlag);
    setFlags2_Widget(button, alwaysRound_WidgetFlag2, iTrue);
    return button;
}

void setOwner_ViewToolbar(iViewToolbar *d, iDocumentWidget *owner) {
    d->owner = owner;
    if (isTerminal_Platform()) {
        return;
    }
    iWidget *w = d->widget = new_Widget();
    setId_Widget(w, "viewtools");
    setFlags_Widget(w, hidden_WidgetFlag | arrangeHorizontal_WidgetFlag | arrangeSize_WidgetFlag, iTrue);
    setFlags2_Widget(w, roundFreeEdges_WidgetFlag2 | floating_WidgetFlag2 | yieldSwipeToParent_WidgetFlag2, iTrue);
    setBackgroundColor_Widget(w, uiBackgroundSidebar_ColorId);
    setFrameColor_Widget(w, uiSeparator_ColorId);
    const float hPad = 1.75f * gap_UI * (isMobile_Platform() ? 1.5f : 1.0f);
    const float vPad = 1.0f  * gap_UI * (isMobile_Platform() ? 1.5f : 1.0f);
    setPadding_Widget(w, hPad, vPad, hPad, vPad);
    setDrawBufferEnabled_Widget(w, iTrue);
    /* Identity pinned in the tab. */ {
        iLabelWidget *ident = makeMenuButton_LabelWidget(person_Icon, NULL, 0);
        setCommand_LabelWidget(ident, collectNewCStr_String("menu.open under:1"));
        setMenuUpdateItemsFunc_Widget(findChild_Widget(as_Widget(ident), "menu"),
                                      identityMenuItems_ViewToolbar_);
        addButton_ViewToolbar_(d, ident, "viewtools.ident");
    }
    addButton_ViewToolbar_(
        d, newIcon_LabelWidget(timer_Icon, 0, 0, "document.autoreload.menu"), "viewtools.reload");
    addButton_ViewToolbar_(
        d, newIcon_LabelWidget(book_Icon, 0, 0, "document.pagemode.toggle"), "viewtools.pagemode");
    //addChild_Widget(w, iClob(makePadding_Widget(hPad)));
    addButton_ViewToolbar_(
        d, newIcon_LabelWidget(gear_Icon, 0, 0, "document.sitespec"), "viewtools.sitespec");
    addButton_ViewToolbar_(d, newIcon_LabelWidget(info_Icon, 0, 0, "document.info"), "viewtools.info");
    addChild_Widget(as_Widget(owner), iClob(w));
    update_ViewToolbar(d);
}

static void setLabel_ViewToolbar_(iLabelWidget *button, const char *icon, const iString *text) {
    if (isEmpty_String(text)) {
        updateTextCStr_LabelWidget(button, icon);
    }
    else {
        updateText_LabelWidget(button, collectNewFormat_String("%s %s", icon, cstr_String(text)));
    }
    setAlignVisually_LabelWidget(button, isEmpty_String(text));
    updateSize_LabelWidget(button);
}

void update_ViewToolbar(iViewToolbar *d) {
    iWidget *w = d->widget;
    if (!w) {
        return;
    }
    iDocumentWidget                *doc   = d->owner;
    const iPersistentDocumentState *state = state_DocumentWidget(doc);
    iLabelWidget                   *identButton  = findChild_Widget(w, "viewtools.ident");
    iLabelWidget                   *reloadButton = findChild_Widget(w, "viewtools.reload");
    iWidget                        *infoButton   = findChild_Widget(w, "viewtools.info");

    const iRangecc scheme   = urlScheme_String(url_DocumentWidget(doc));
    const iBool    isBook   = gempub_DocumentWidget(doc) != NULL;
    const iBool    isReader = isReader_DocumentWidget(doc);
    const iBool    isLocal  = isLocalFileScheme_Rangecc(scheme);
    const iBool    isTls    = isTlsScheme_Rangecc(scheme);

    setFlags_Widget(identButton, hidden_WidgetFlag | collapse_WidgetFlag, isBook || isLocal || !isTls);
    setFlags_Widget(reloadButton, hidden_WidgetFlag | collapse_WidgetFlag, isBook ||isReader);
    setFlags_Widget(findChild_Widget(w, "viewtools.sitespec"), hidden_WidgetFlag | collapse_WidgetFlag, !isBook);
    setFlags_Widget(infoButton, hidden_WidgetFlag | collapse_WidgetFlag, !isBook);
    /* A book's information is in the sidebar. */
    setCommand_LabelWidget(findChild_Widget(w, "viewtools.info"),
                           isBook ? collectNewFormat_String("sidebar.mode arg:%d show:1 contents:1",
                                                            gempub_SidebarMode)
                                  : collectNewCStr_String("document.info"));
    /* Identity in use (pinned or global). */ {
        const iGmIdentity *ident    = identity_DocumentWidget(doc);
        iString           *name     = collectNew_String();
        const iBool        isPinned = isIdentityPinned_DocumentWidget(doc);
        if (ident) {
            set_String(name, name_GmIdentity(ident));
            if (length_String(name) > maxNameLength_ViewToolbar_) {
                truncate_String(name, maxNameLength_ViewToolbar_ - 1);
                appendCStr_String(name, "…");
            }
        }
        setLabel_ViewToolbar_(identButton, person_Icon, name);
        setFlags_Widget(identButton, selected_WidgetFlag, isPinned);
        setTextColor_LabelWidget(identButton, !isPinned && ident ? uiTextAction_ColorId
                                 : none_ColorId /* default */);
    }
    /* Auto-reload. */ {
        setLabel_ViewToolbar_(reloadButton,
                              timer_Icon,
                              collectNewCStr_String(shortLabel_ReloadInterval(state->reloadInterval)));
        setFlags_Widget(reloadButton, selected_WidgetFlag, isAutoReloading_DocumentWidget(doc));
    }
    /* The button switches to the other mode. */ {
        iLabelWidget        *button = findChild_Widget(w, "viewtools.pagemode");
        const iDocumentView *view   = view_DocumentWidget(doc);
        const iBool isPagedByPrefs  = docPrefs_GmDocument(view->doc)->pageView.value != state->invertPageView;
        updateTextCStr_LabelWidget(button, isPagedByPrefs ? upDownArrow_Icon : book_Icon);
        setFlags_Widget(button, selected_WidgetFlag, state->invertPageView);
        setFlags_Widget(button, disabled_WidgetFlag, !isPageViewAllowed_DocumentView(view));
    }
    arrange_Widget(w);
    d->fullWidth = width_Widget(w);
    if (d->isCompact) {
        setLabel_ViewToolbar_(identButton, person_Icon, collectNew_String());
        setLabel_ViewToolbar_(reloadButton, timer_Icon, collectNew_String());
        arrange_Widget(w);
    }
    refresh_Widget(w);
}

void scrollBegan_ViewToolbar(iViewToolbar *d, int offset, iBool isAtTop) {
    if (iAbs(offset) > 5) {
        d->isScrolledAway = offset > 0 && !isAtTop;
    }
}

void reveal_ViewToolbar(iViewToolbar *d) {
    d->isScrolledAway = iFalse;
}

static iBool isAvailable_ViewToolbar_(const iViewToolbar *d) {
    iDocumentWidget     *doc  = d->owner;
    const iDocumentView *view = view_DocumentWidget(doc);
    if (prefs_App()->hideViewTools ||
        equalCase_Rangecc(urlScheme_String(url_DocumentWidget(doc)), "about")) {
        return iFalse;
    }
    return size_GmDocument(view->doc).y > 0 || !isEmpty_Banner(banner_DocumentWidget(doc));
}

static iBool isHovered_ViewToolbar_(const iViewToolbar *d) {
    const iWindow *win = window_Widget(d->widget);
    if (!isDesktop_Platform() || !win->isMouseInside) {
        return iFalse; /* touches leave the mouse coordinates behind */
    }
    const iInt2 mouse = mouseCoord_Window(win, 0);
    return contains_Widget(d->widget, mouse) || contains_Rect(d->headerArea, mouse);
}

static iBool isActive_ViewToolbar_(const iViewToolbar *d) {
    const iWidget *w     = d->widget;
    const iWidget *focus = focus_Widget();
    const iWidget *menu  = findChild_Widget(w, "menu");
    if (isHovered_ViewToolbar_(d) ||
        (focus && hasParent_Widget(focus, w)) || (menu && isVisible_Widget(menu))) {
        return iTrue;
    }
    iConstForEach(ObjectList, i, w->children) {
        if (flags_Widget(i.object) & selected_WidgetFlag) {
            return iTrue;
        }
    }
    return iFalse;
}

static void updateOpacity_ViewToolbar_(iViewToolbar *d) {
    const iRoot *root   = d->widget->root;
    float        target = 1.0f;
    if (isReadingMode_Root(root)) {
        if (root->areReadingBarsHidden && !isHovered_ViewToolbar_(d)) {
            target = 0.0f; /* hidden along with the bars while reading pages */
        }
    }
    else if (d->isScrolledAway && !isActive_ViewToolbar_(d)) {
        target = dimmedOpacity_ViewToolbar_;
    }
    if (targetValue_Anim(&d->opacity) != target) {
        setValue_Anim(&d->opacity,
                      target,
                      d->wasAvailable && prefs_App()->uiAnimations ? fadeSpan_ViewToolbar_ : 0);
        animate_ViewToolbar_(d);
    }
}

void updateHover_ViewToolbar(iViewToolbar *d) {
    if (d->widget && d->wasAvailable) {
        updateOpacity_ViewToolbar_(d);
    }
}

static int bottomBarsEdge_ViewToolbar_(iViewToolbar *d, const iRoot *root) {
    /* The phone's toolbar and the tab bar above it. */
    const iWidget *bottomBar  = findChild_Widget(root->widget, "bottombar");
    const iWidget *tabBar     = prefs_App()->bottomTabBar ? root->bars.tabButtons : NULL;
    const int      safeBottom = bottom_Rect(safeRect_Root(root));
    if (!bottomBar) {
        return safeBottom;
    }
    if (flags_Widget(bottomBar) & visualOffset_WidgetFlag &&
        !isFinished_Anim(&bottomBar->visualOffset)) {
        /* The bars slide separately, so follow the toolbar between the resting positions. */
        const int   shown = d->shownBarsEdge ? d->shownBarsEdge
                                             : top_Rect(boundsWithoutVisualOffset_Widget(bottomBar));
        const int   height = height_Widget(bottomBar);
        const float pos    = height > 0 ? iClamp(value_Anim(&bottomBar->visualOffset) / height,
                                                 0.0f, 1.0f)
                                        : 1.0f;
        return iMin(safeBottom, shown + (int) (pos * (safeBottom - shown)));
    }
    if (!isVisible_Widget(bottomBar)) {
        return safeBottom;
    }
    int edge = top_Rect(bounds_Widget(bottomBar));
    if (tabBar && isVisible_Widget(tabBar)) {
        edge = iMin(edge, top_Rect(bounds_Widget(tabBar)));
    }
    d->shownBarsEdge = edge;
    return iMin(safeBottom, edge);
}

void arrange_ViewToolbar(iViewToolbar *d) {
    iWidget *w = d->widget;
    if (!w) {
        return;
    }
    if (!isAvailable_ViewToolbar_(d)) {
        setFlags_Widget(w, hidden_WidgetFlag, iTrue);
        d->wasAvailable = iFalse;
        return;
    }
    const iWidget       *owner    = constAs_Widget(d->owner);
    iRoot               *root     = owner->root;
    const iDocumentView *view     = view_DocumentWidget(d->owner);
    const iBool          paged    = isPaged_DocumentView(view);
    const iRect          bounds   = bounds_Widget(owner);
    const iRect          safe     = safeRect_Root(root);
    const iRect          area     = paged ? pageViewport_DocumentView(view) : bounds;
    const int            pad      = 2 * gap_UI;
    const iBool          atBottom = isPortraitPhone_App(); /* within reach */
    /* The scroll bar's space is reserved in both modes, so toggling doesn't move the toolbar. */
    const int left  = iMax(left_Rect(bounds), left_Rect(safe)) + pad;
    const int right = iMin(right_Rect(bounds), right_Rect(safe)) - pad -
                      (atBottom ? 0 : thickness_ScrollWidget());
    /* Labels are omitted if there isn't enough room. */
    const iBool compact = d->fullWidth > right - left;
    if (compact != d->isCompact) {
        d->isCompact = compact;
        update_ViewToolbar(d);
    }
    const int height = height_Widget(w);
    int       top;
    int       barsEdge;
    float     barsOpacity;
    d->headerArea = zero_Rect();
    if (atBottom) {
        top = bottomBarsEdge_ViewToolbar_(d, root) - 3 * gap_UI - height;
    }
    else {
        /* Below the bars and the safe area inset. */
        top = paged ? top_Rect(area) + metrics_GmDocument(view->doc)->topInset
                    : iMax(top_Rect(bounds), top_Rect(safe));
        if (readingBarsEdge_Root(root, iFalse, &barsEdge, &barsOpacity)) {
            top = iMax(top, barsEdge);
        }
        if (!isLandscapePhone_App()) {
            /* Centered in the page header area, also when not paginated. */
            const int band = pageBandHeightFor_GmDocument(
                view->doc, height_Rect(readingRect_Root(root, bounds)));
            d->headerArea = init_Rect(left_Rect(bounds), top, width_Rect(bounds), band);
            top += (band - height) / 2;
        }
    }
    w->rect.pos = sub_I2(init_I2(atBottom ? (left + right - width_Widget(w)) / 2
                                          : right - width_Widget(w),
                                 top),
                         topLeft_Rect(bounds));
    updateOpacity_ViewToolbar_(d);
    setFlags_Widget(w, hidden_WidgetFlag, value_Anim(&d->opacity) <= 0.0f);
    setOpacity_Widget(w, value_Anim(&d->opacity));
    d->wasAvailable = iTrue;
}
