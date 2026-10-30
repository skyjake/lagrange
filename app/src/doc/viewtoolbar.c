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
#include "impl.h"
#include "../app.h"
#include "../color.h"
#include "../gmdocument.h"
#include "banner.h"
#include "documentview.h"
#include "pagedview.h"
#include "persistentstate.h"
#include "render/paint.h"
#include "ui/labelwidget.h"
#include "ui/menu.h"
#include "ui/mobile.h"
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

static const iLabelColors buttonColors_ViewToolbar_ = {
    .bg         = none_ColorId,
    .fg         = tmBannerIcon_ColorId,
    .hoverBg    = tmBannerItemBackground_ColorId,
    .hoverFg    = tmBannerIcon_ColorId,
    .selectedBg = tmBannerIcon_ColorId,
    .selectedFg = tmBannerBackground_ColorId,
    .pressedBg  = tmBannerItemFrame_ColorId,
    .pressedFg  = tmBannerItemTitle_ColorId,
    .disabledFg = tmBannerItemFrame_ColorId,
};

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
    d->followsScroll  = iFalse;
    d->ownerWidth     = 0;
    d->titleY         = 0;
    d->top            = 0;
    d->barsPush       = 0;
    d->nativeMenuRect = zero_Rect();
}

static void animate_ViewToolbar_(void *ptr) {
    iViewToolbar *d = ptr;
    refresh_Widget(d->widget);
    if (!isFinished_Anim(&d->opacity)) {
        addTicker_App(animate_ViewToolbar_, d);
    }
}

static void rootArrangementChanged_ViewToolbar_(iWidget *, iRoot *);

void deinit_ViewToolbar(iViewToolbar *d) {
    removeTicker_App(animate_ViewToolbar_, d); /* the widget is owned by the document */
    if (d->widget && isSupported_SystemMenu()) {
        iDisconnect(Root, d->widget->root, arrangementChanged, d->widget,
                    rootArrangementChanged_ViewToolbar_);
    }
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
    setColors_LabelWidget(button, &buttonColors_ViewToolbar_);
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
    setBackgroundColor_Widget(w, tmBannerBackground_ColorId);
    setFrameColor_Widget(w, tmBannerItemFrame_ColorId);
    const float hPad = 1.66f * gap_UI * (isMobile_Platform() ? 1.5f : 1.0f);
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
    if (isSupported_SystemMenu()) {
        setUserData_Object(w, d);
        iConnect(Root, w->root, arrangementChanged, w, rootArrangementChanged_ViewToolbar_);
    }
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
    const iPersistentDocumentState *state = &doc->mod;
    iLabelWidget                   *identButton  = findChild_Widget(w, "viewtools.ident");
    iLabelWidget                   *reloadButton = findChild_Widget(w, "viewtools.reload");
    iWidget                        *infoButton   = findChild_Widget(w, "viewtools.info");

    const iRangecc scheme   = urlScheme_String(url_DocumentWidget(doc));
    const iBool    isBook   = gempub_DocumentWidget(doc) != NULL;
    const iBool    isReader = isReader_DocumentWidget(doc);
    const iBool    isLocal  = isLocalFileScheme_Rangecc(scheme);
    const iBool    isTls    = isTlsScheme_Rangecc(scheme);
    const iBool    isAbout  = equalCase_Rangecc(scheme, "about");

    setFlags_Widget(identButton, hidden_WidgetFlag | collapse_WidgetFlag, isBook || isLocal || !isTls);
    setFlags_Widget(reloadButton, hidden_WidgetFlag | collapse_WidgetFlag, isBook || isReader || isAbout);
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
    }
    /* Auto-reload. */ {
        setLabel_ViewToolbar_(reloadButton,
                              timer_Icon,
                              collectNewCStr_String(shortLabel_ReloadInterval(state->reloadInterval)));
        setFlags_Widget(reloadButton, selected_WidgetFlag, isAutoReloading_DocumentWidget(doc));
    }
    /* The button switches to the other mode. */ {
        iLabelWidget        *button = findChild_Widget(w, "viewtools.pagemode");
        const iDocumentView *view   = doc->view;
        const iBool isPagedByPrefs  = docPrefs_GmDocument(view->doc)->pageView.value != state->invertPageView;
        updateTextCStr_LabelWidget(button, isPagedByPrefs ? upDownArrow_Icon : book_Icon);
        const iBool isAllowed = isPageViewAllowed_DocumentView(view);
        setFlags_Widget(button, selected_WidgetFlag, state->invertPageView);
        setFlags_Widget(button, disabled_WidgetFlag, !isAllowed);
        /* Error pages and other UI content. */
        setFlags_Widget(button, hidden_WidgetFlag | collapse_WidgetFlag, !isAllowed && !isBook);
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
    if (offset) {
        d->followsScroll = iTrue;
    }
    if (iAbs(offset) > 5) {
        d->isScrolledAway = offset > 0 && !isAtTop;
    }
}

void reveal_ViewToolbar(iViewToolbar *d) {
    d->isScrolledAway = iFalse;
}

static iBool isAllowedUrl_ViewToolbar_(const iString *url) {
    iUrl parts;
    init_Url(&parts, url);
    if (!equalCase_Rangecc(parts.scheme, "about")) {
        return iTrue;
    }
    static const char *pages_[] = { "help", "protocols", "reference" };
    iForIndices(i, pages_) {
        if (equalCase_Rangecc(parts.path, pages_[i])) {
            return iTrue;
        }
    }
    return iFalse;
}

static iBool isAvailable_ViewToolbar_(const iViewToolbar *d) {
    iDocumentWidget     *doc  = d->owner;
    const iDocumentView *view = doc->view;
    if (prefs_App()->hideViewTools || hasError_Banner(doc->banner) ||
        !isAllowedUrl_ViewToolbar_(url_DocumentWidget(doc))) {
        return iFalse;
    }
    return size_GmDocument(view->doc).y > 0 || !isEmpty_Banner(doc->banner);
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

static iBool isCovered_ViewToolbar_(const iViewToolbar *d) {
    if (isPortraitPhone_App() &&
        isVisible_Widget(findChild_Widget(d->widget->root->widget, "sidebar"))) {
        return iTrue;
    }
    const iWidget *footer = footerButtons_DocumentWidget(d->owner);
    if (footer && isVisible_Widget(footer)) {
        const iRect bounds = bounds_Widget(d->widget);
        iConstForEach(ObjectList, i, footer->children) {
            if (isVisible_Widget(i.object) && isOverlapping_Rect(bounds, bounds_Widget(i.object))) {
                return iTrue;
            }
        }
    }
    return iFalse;
}

static void updateOpacity_ViewToolbar_(iViewToolbar *d) {
    const iRoot *root   = d->widget->root;
    float        target = 1.0f;
    if (isCovered_ViewToolbar_(d)) {
        target = 0.0f;
    }
    else if (isReadingMode_Root(root)) {
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
    setFlags_Widget(d->widget, disabled_WidgetFlag, target <= 0.0f); /* also while fading out */
}

static void updateNativeMenu_ViewToolbar_(iViewToolbar *d, iBool force) {
    /* The native menu button is on top of everything, so it must follow the toolbar. */
    if (!isSupported_SystemMenu()) {
        return;
    }
    iWidget *ident = findChild_Widget(d->widget, "viewtools.ident");
    iWidget *menu  = findChild_Widget(ident, "menu");
    if (!menu || ~flags_Widget(menu) & nativeMenu_WidgetFlag) {
        return;
    }
    const iRect rect =
        isVisible_Widget(ident) && !isDisabled_Widget(ident) ? bounds_Widget(ident) : zero_Rect();
    if (force || !equal_Rect(rect, d->nativeMenuRect)) {
        d->nativeMenuRect = rect;
        updateAfterBoundsChange_SystemMenu(menu);
    }
}

static void rootArrangementChanged_ViewToolbar_(iWidget *w, iRoot *root) {
    /* A fully covered document isn't drawn, so arrange_ViewToolbar may not run. Forced since
       the native menu's own observer may have just shown it. */
    iViewToolbar *d = userData_Object(w);
    iUnused(root);
    if (d->wasAvailable) {
        updateOpacity_ViewToolbar_(d);
    }
    updateNativeMenu_ViewToolbar_(d, iTrue);
}

void updateHover_ViewToolbar(iViewToolbar *d) {
    if (d->widget && d->wasAvailable) {
        updateOpacity_ViewToolbar_(d);
        updateNativeMenu_ViewToolbar_(d, iFalse);
    }
}

static int bottomBarsEdge_ViewToolbar_(iViewToolbar *d, const iRoot *root) {
    /* The phone's toolbar and the tab bar above it. */
    const iWidget *bottomBar  = root->bars.bottomBar;
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

int bottomSpace_ViewToolbar(const iViewToolbar *d) {
    if (!d->widget || !isPortraitPhone_App() || !isAvailable_ViewToolbar_(d)) {
        return 0;
    }
    return height_Widget(d->widget) + 3 * gap_UI;
}

void arrange_ViewToolbar(iViewToolbar *d) {
    iWidget *w = d->widget;
    if (!w) {
        return;
    }
    if (!isAvailable_ViewToolbar_(d)) {
        setFlags_Widget(w, hidden_WidgetFlag, iTrue);
        d->wasAvailable = iFalse;
        updateNativeMenu_ViewToolbar_(d, iFalse);
        return;
    }
    const iWidget       *owner    = constAs_Widget(d->owner);
    iRoot               *root     = owner->root;
    const iDocumentView *view     = d->owner->view;
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
    int       titleY = 0; /* unscrolled, in scrolling mode */
    d->headerArea = zero_Rect();
    if (atBottom) {
        top = bottomBarsEdge_ViewToolbar_(d, root) - 3 * gap_UI - height;
    }
    else if (paged) {
        /* Centered on the page header, below the bars and the safe area inset. The bars' resting
           position is used, so the toolbar doesn't slide along with them. While hidden, the bars
           are assumed to have the same height as when last shown. */
        const int bandTop = top_Rect(area) + metrics_GmDocument(view->doc)->topInset;
        int       barsEdge;
        if (readingBarsRestEdge_Root(root, iFalse, &barsEdge)) {
            d->barsPush = iMax(0, barsEdge - bandTop);
        }
        else if (!isReadingMode_Root(root) || !hasReadingBarAt_Root(root, iFalse)) {
            d->barsPush = 0;
        }
        top = bandTop + d->barsPush;
        if (!isLandscapePhone_App()) {
            /* Both the header and the toolbar's position below the bars can be hovered. */
            const int band = pageBandHeight_GmDocument(view->doc);
            d->headerArea  =
                init_Rect(left_Rect(bounds), bandTop, width_Rect(bounds), d->barsPush + band);
            top = top_Rect(area) + pageHeaderTextTop_DocumentView(view) + d->barsPush +
                  (lineHeight_Text(pageBandFont_GmDocument(view->doc)) - height) / 2;
        }
    }
    else {
        /* Equally far from the top and right edges, but not above the site title. */
        const int      areaTop  = iMax(top_Rect(bounds), top_Rect(safe));
        const iBanner *banner   = d->owner->banner;
        const int      titleMid = siteTitleMid_Banner(banner);
        titleY = top_Rect(documentBounds_DocumentView(view)) + titleMid;
        top    = areaTop + iMin(right_Rect(bounds), right_Rect(safe)) - right;
        if (titleMid > 0) {
            /* The edge distance may exceed the title's resting position (e.g., on mobile). */
            top = iMin(top, iMax(areaTop, titleY - height / 2));
            const int mid = titleY - (int) floorf(pos_SmoothScroll(&view->scrollY));
            top = iMax(top, mid - height / 2);
        }
        d->headerArea =
            init_Rect(left_Rect(bounds), areaTop, width_Rect(bounds), 2 * (top - areaTop) + height);
    }
    /* Only moves while unhiding, scrolling, or after a resize or relayout, so it doesn't shift
       around while being used. Switching modes keeps the position, too. */
    const iBool isShown    = d->wasAvailable && ~flags_Widget(w) & hidden_WidgetFlag;
    const iBool isUnhiding = !isShown || value_Anim(&d->opacity) < targetValue_Anim(&d->opacity);
    const iBool isMoved    = width_Rect(bounds) != d->ownerWidth ||
                             (titleY && d->titleY && titleY != d->titleY);
    if (atBottom || isUnhiding || d->followsScroll || isMoved) {
        d->top = top;
    }
    if (isFinished_Anim(&view->scrollY.pos)) {
        d->followsScroll = iFalse;
    }
    d->ownerWidth = width_Rect(bounds);
    d->titleY     = titleY;
    w->rect.pos = sub_I2(init_I2(atBottom ? (left + right - width_Widget(w)) / 2
                                          : right - width_Widget(w),
                                 d->top),
                         topLeft_Rect(bounds));
    updateOpacity_ViewToolbar_(d);
    setFlags_Widget(w, hidden_WidgetFlag, value_Anim(&d->opacity) <= 0.0f);
    setOpacity_Widget(w, value_Anim(&d->opacity));
    d->wasAvailable = iTrue;
    updateNativeMenu_ViewToolbar_(d, iFalse);
}
