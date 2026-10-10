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
static const float    dimmedOpacity_ViewToolbar_ = 0.25f;
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
    addChildIdFlags_Widget(d->widget, iClob(button), id, 0);
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
    w->flags2 |= roundFreeEdges_WidgetFlag2 | floating_WidgetFlag2;
    setBackgroundColor_Widget(w, uiBackground_ColorId);
    setFrameColor_Widget(w, uiSeparator_ColorId);
    const float hPad = 1.75f * gap_UI;
    setPadding_Widget(w, hPad, 1 * gap_UI, hPad, 1 * gap_UI);
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
    addChild_Widget(w, iClob(makePadding_Widget(1 * gap_UI)));
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
    const iBool isBook  = gempub_DocumentWidget(doc) != NULL;
    const iBool isLocal = isLocalFileScheme_Rangecc(urlScheme_String(url_DocumentWidget(doc)));
    setFlags_Widget(as_Widget(identButton), hidden_WidgetFlag | collapse_WidgetFlag, isBook || isLocal);
    setFlags_Widget(as_Widget(reloadButton), hidden_WidgetFlag | collapse_WidgetFlag, isBook);
    setFlags_Widget(findChild_Widget(w, "viewtools.sitespec"), hidden_WidgetFlag | collapse_WidgetFlag, !isBook);
    /* A book's information is in the sidebar. */
    setCommand_LabelWidget(findChild_Widget(w, "viewtools.info"),
                           isBook ? collectNewFormat_String("sidebar.mode arg:%d show:1 current:1 shelf:1",
                                                            gempub_SidebarMode)
                                  : collectNewCStr_String("document.info"));
    /* Identity in use, pinned or not. */ {
        const iGmIdentity *ident  = identity_DocumentWidget(doc);
        iString           *name   = collectNew_String();
        if (ident) {
            set_String(name, name_GmIdentity(ident));
            if (length_String(name) > maxNameLength_ViewToolbar_) {
                truncate_String(name, maxNameLength_ViewToolbar_ - 1);
                appendCStr_String(name, "…");
            }
        }
        setLabel_ViewToolbar_(identButton, person_Icon, name);
        setFlags_Widget(as_Widget(identButton), selected_WidgetFlag, isIdentityPinned_DocumentWidget(doc));
    }
    /* Auto-reload. */ {
        setLabel_ViewToolbar_(reloadButton,
                              timer_Icon,
                              collectNewCStr_String(shortLabel_ReloadInterval(state->reloadInterval)));
        setFlags_Widget(as_Widget(reloadButton), selected_WidgetFlag, isAutoReloading_DocumentWidget(doc));
    }
    /* The button switches to the other mode. */ {
        iLabelWidget        *button = findChild_Widget(w, "viewtools.pagemode");
        const iDocumentView *view   = view_DocumentWidget(doc);
        const iBool isPagedByPrefs  = docPrefs_GmDocument(view->doc)->pageView.value != state->invertPageView;
        updateTextCStr_LabelWidget(button, isPagedByPrefs ? upDownArrow_Icon : book_Icon);
        setFlags_Widget(as_Widget(button), selected_WidgetFlag, state->invertPageView);
        setFlags_Widget(as_Widget(button), disabled_WidgetFlag, !isPageViewAllowed_DocumentView(view));
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
    if (equalCase_Rangecc(urlScheme_String(url_DocumentWidget(doc)), "about")) {
        return iFalse;
    }
    return size_GmDocument(view->doc).y > 0 || !isEmpty_Banner(banner_DocumentWidget(doc));
}

static iBool isActive_ViewToolbar_(const iViewToolbar *d) {
    const iWidget *w     = d->widget;
    const iWindow *win   = window_Widget(w);
    const iWidget *focus = focus_Widget();
    const iWidget *menu  = findChild_Widget(w, "menu");
    if ((win->isMouseInside && contains_Widget(w, mouseCoord_Window(win, 0))) ||
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
        if (root->areReadingBarsHidden) {
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

static int bottomBarsEdge_ViewToolbar_(const iRoot *root) {
    /* The phone's toolbar and the tab bar above it, also while they slide. */
    const iWidget *bars[2] = { findChild_Widget(root->widget, "bottombar"),
                               prefs_App()->bottomTabBar ? root->bars.tabButtons : NULL };
    int edge = bottom_Rect(rect_Root(root));
    for (size_t i = 0; i < iElemCount(bars); i++) {
        if (bars[i] && isVisible_Widget(bars[i])) {
            edge = iMin(edge, top_Rect(bounds_Widget(bars[i])));
        }
    }
    return edge;
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
    const iWidget       *owner  = constAs_Widget(d->owner);
    iRoot               *root   = owner->root;
    const iDocumentView *view   = view_DocumentWidget(d->owner);
    const iScrollWidget *scroll = scrollBar_DocumentWidget(d->owner);
    const iBool          paged  = isPaged_DocumentView(view);
    const iRect          bounds = bounds_Widget(owner);
    const iRect          safe   = safeRect_Root(root);
    const iRect          area   = paged ? pageViewport_DocumentView(view) : bounds;
    const int            pad    = 2 * gap_UI;
    const int            edge   = iMin(right_Rect(area), right_Rect(safe));
    int right = edge - pad;
    if (!isProgressMode_ScrollWidget(scroll) && isVisible_Widget(scroll)) {
        right = iMin(right, left_Rect(bounds_Widget(constAs_Widget(scroll))) - pad);
    }
    /* Labels are omitted if there isn't enough room. */
    const iBool compact = d->fullWidth > right - iMax(left_Rect(area), left_Rect(safe)) - pad;
    if (compact != d->isCompact) {
        d->isCompact = compact;
        update_ViewToolbar(d);
    }
    /* Equal distance from the corner's edges, unless aligned with the header or footer. */
    const int   vPad      = paged ? pad : edge - right;
    const int   height    = height_Widget(w);
    const int   bandAlign = (lineHeight_Text(pageBandFont_GmDocument(view->doc)) - height) / 2;
    const iBool atBottom  = isPortraitPhone_App(); /* within reach */
    int         top;
    int         barsEdge;
    float       barsOpacity;
    if (atBottom) {
        top = paged ? top_Rect(area) + pageFooterTextTop_DocumentView(view) + bandAlign
                    : iMin(bottom_Rect(area), bottom_Rect(safe)) - vPad - height;
        top = iMin(top, bottomBarsEdge_ViewToolbar_(root) - vPad - height);
    }
    else {
        top = paged ? top_Rect(area) + pageHeaderTextTop_DocumentView(view) + bandAlign
                    : iMax(top_Rect(area), top_Rect(safe)) + vPad;
        if (readingBarsEdge_Root(root, iFalse, &barsEdge, &barsOpacity)) {
            top = iMax(top, barsEdge + vPad);
        }
    }
    w->rect.pos = sub_I2(init_I2(right - width_Widget(w), top), topLeft_Rect(bounds));
    updateOpacity_ViewToolbar_(d);
    setFlags_Widget(w, hidden_WidgetFlag, value_Anim(&d->opacity) <= 0.0f);
    setOpacity_Widget(w, value_Anim(&d->opacity));
    d->wasAvailable = iTrue;
}
