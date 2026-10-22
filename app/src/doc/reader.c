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

#include "impl.h"

#include "../app.h"
#include "../feeds.h"
#include "../gmutil.h"
#include "documentview.h"
#include "pagedview.h"
#include "render/text.h"
#include "ui/command.h"
#include "ui/keys.h"
#include "ui/labelwidget.h"
#include "ui/root.h"
#include "ui/sidebarwidget.h"
#include "ui/util.h"

static iBool isFeedsSidebarVisible_DocumentWidget_(const iDocumentWidget *d) {
    const iWidget *root = root_Widget(constAs_Widget(d));
    for (int i = 0; i < 2; i++) {
        const iSidebarWidget *sidebar = findChild_Widget(root, i == 0 ? "sidebar" : "sidebar2");
        if (sidebar && isVisible_Widget(sidebar) &&
            mode_SidebarWidget(sidebar) == feedEntries_SidebarMode) {
            return iTrue;
        }
    }
    return iFalse;
}

static iBool setFooterButtonHidden_DocumentWidget_(iDocumentWidget *d, const char *id, iBool hide) {
    iWidget *button = d->footerButtons ? findChild_Widget(d->footerButtons, id) : NULL;
    if (!button || hide == ((flags_Widget(button) & hidden_WidgetFlag) != 0)) {
        return iFalse;
    }
    setFlags_Widget(button, hidden_WidgetFlag, hide);
    return iTrue;
}

iBool updateReaderNavButtons_DocumentWidget(iDocumentWidget *d) {
    if (!d->footerButtons || !d->mod.isReader) {
        return iFalse;
    }
    /* Scrolled pages on mobile navigate via overscroll. */
    const iBool hide =
        deviceType_App() != desktop_AppDeviceType && !wantsPaged_DocumentView(d->view);
    return setFooterButtonHidden_DocumentWidget_(d, "reader.prev", hide) |
           setFooterButtonHidden_DocumentWidget_(d, "reader.next", hide);
}

iBool updateReaderButtons_DocumentWidget(iDocumentWidget *d) {
    if (!d->footerButtons || !d->mod.isReader) {
        return iFalse;
    }
    const iBool changed = updateReaderNavButtons_DocumentWidget(d) |
                          setFooterButtonHidden_DocumentWidget_(
                              d, "reader.showfeeds", isFeedsSidebarVisible_DocumentWidget_(d));
    if (changed) {
        arrange_Widget(d->footerButtons);
    }
    return changed;
}

const iArray *readerFooterItems_DocumentWidget(const iDocumentWidget *d, const iMenuItem *items,
                                               size_t count) {
    /* Entry navigation stays in place when the page-specific items change. */
    iArray *all = collectNew_Array(sizeof(iMenuItem));
    if (count) {
        pushBackN_Array(all, items, count);
    }
    const iBinding *nextKey = findCommand_Keys("scroll.page arg:1");
    const iBinding *prevKey = findCommand_Keys("scroll.page arg:-1");
    pushBackN_Array(
        all,
        (iMenuItem[]){ { upArrow_Icon " ${reader.prev}",
                         prevKey ? prevKey->key : 0, prevKey ? prevKey->mods : 0,
                         "reader.prev" },
                       { downArrow_Icon " ${reader.next}",
                         nextKey ? nextKey->key : 0, nextKey ? nextKey->mods : 0,
                         "reader.next" },
                       { check_Icon " ${feeds.markallread}", SDLK_A, SDL_KMOD_SHIFT, "reader.markallread" },
                       { format_CStr("%s ${reader.showfeeds}",
                                     icon_SidebarMode(feedEntries_SidebarMode)),
                         0, 0, "reader.showfeeds" } },
        4);
    return all;
}

iBool openAdjacentFeedEntry_DocumentWidget(iDocumentWidget *d, int dir) {
    const iWidget *root = root_Widget(as_Widget(d));
    const iSidebarWidget *sidebar = findChild_Widget(root, "sidebar");
    if (mode_SidebarWidget(sidebar) != feedEntries_SidebarMode) {
        const iSidebarWidget *sidebar2 = findChild_Widget(root, "sidebar2");
        if (sidebar2 && mode_SidebarWidget(sidebar2) == feedEntries_SidebarMode) {
            sidebar = sidebar2;
        }
    }
    const iString   *openUrl    = feedEntryUrl_DocumentWidget(d);
    const iBool      unreadOnly = (feedsMode_SidebarWidget(sidebar) == unread_FeedsMode);
    const iPtrArray *entries    = listVisibleEntries_Feeds(unreadOnly, openUrl);
    const size_t     count      = size_PtrArray(entries);
    size_t pos = iInvalidPos;
    iConstForEach(PtrArray, i, entries) {
        const iFeedEntry *entry = i.ptr;
        if (equal_String(&entry->url, openUrl)) {
            pos = index_PtrArrayConstIterator(&i);
            break;
        }
    }
    if (pos == iInvalidPos) {
        /* Current entry is no longer listed, so start from the top. */
        if (dir < 0 && !unreadOnly) {
            return iFalse;
        }
        pos = 0;
    }
    else {
        if (unreadOnly && (dir < 0 ? pos == 0 : pos + 1 >= count)) {
            /* Turn around at the end so all unread entries are reachable in one direction. */
            dir = -dir;
        }
        if (dir < 0 && pos == 0) {
            return iFalse;
        }
        pos += dir;
    }
    if (pos >= count) {
        return iFalse;
    }
    const iFeedEntry *entry = constAt_PtrArray(entries, pos);
    postCommandString_Root(as_Widget(d)->root,
                           feedEntryOpenCommand_String(&entry->url, 0, 0, iTrue));
    return iTrue;
}

void readerPullAction_DocumentWidget(iDocumentWidget *d, int dir) {
    if (!openAdjacentFeedEntry_DocumentWidget(d, dir)) {
        d->view->scrollY.pullActionTriggered = 0;
        if (!isFeedsSidebarVisible_DocumentWidget_(d)) {
            showSidebar_DocumentWidget(d);
        }
    }
}

iBool handleReaderCommand_DocumentWidget(iDocumentWidget *d, const char *cmd) {
    iWidget *w = as_Widget(d);
    if (equal_Command(cmd, "reader.next") || equal_Command(cmd, "reader.prev")) {
        if (!openAdjacentFeedEntry_DocumentWidget(d, equal_Command(cmd, "reader.next") ? +1 : -1) &&
            !isFeedsSidebarVisible_DocumentWidget_(d)) {
            /* No more entries this way; the list shows where we are. */
            showSidebar_DocumentWidget(d);
        }
        return iTrue;
    }
    else if (equal_Command(cmd, "reader.markallread")) {
        if (!argLabel_Command(cmd, "confirmed")) {
            iWidget *button =
                d->footerButtons ? findChild_Widget(d->footerButtons, "reader.markallread") : NULL;
            openMarkAllReadMenu_Widget(button ? button : w,
                                       format_CStr("reader.markallread confirmed:1 doc:%p", d));
            return iTrue;
        }
        markAllEntriesAsRead_Feeds();
        notify_App("visited.changed");
        return iTrue;
    }
    else if (equal_Command(cmd, "reader.showfeeds")) {
        showSidebar_DocumentWidget(d);
        return iTrue;
    }
    return iFalse;
}

void drawReaderPullActions_DocumentWidget(const iDocumentWidget *d) {
    /* Entry navigation in the overscroll areas. */
    const iRect          bounds  = bounds_Widget(constAs_Widget(d));
    const iSmoothScroll *scrollY = &d->view->scrollY;
    for (int dir = -1; dir <= 1; dir += 2) {
        const float pullPos = pullActionPos_SmoothScroll(scrollY, dir);
        if (pullPos > 0) {
            const int y = dir < 0 ? top_Rect(bounds) - 5 * gap_UI - pos_SmoothScroll(scrollY)
                                  : overscrollBottom_DocumentView(d->view) + 5 * gap_UI;
            drawCentered_Text(uiLabelMediumBold_FontId,
                              initCentered_Rect(init_I2(mid_Rect(bounds).x, y),
                                                init_I2(width_Rect(bounds),
                                                        lineHeight_Text(uiLabelMediumBold_FontId))),
                              iFalse,
                              pullPos < 1.0f ? tmQuoteIcon_ColorId
                                             : tmBannerItemText_ColorId,
                              "%s %s",
                              dir < 0 ? upArrow_Icon : downArrow_Icon,
                              cstr_Lang(dir < 0 ? "reader.prev" : "reader.next"));
        }
    }
}
