#include "sidebarwidget.h"
#include "labelwidget.h"
#include "scrollwidget.h"
#include "paint.h"
#include "util.h"

struct Impl_SidebarWidget {
    iWidget widget;
    enum iSidebarMode mode;
    iScrollWidget *scroll;
};

iDefineObjectConstruction(SidebarWidget)

void init_SidebarWidget(iSidebarWidget *d) {
    iWidget *w = as_Widget(d);
    init_Widget(w);
    setFlags_Widget(w, resizeChildren_WidgetFlag, iTrue);
    d->mode = documentOutline_SidebarMode;
    addChild_Widget(w, iClob(d->scroll = new_ScrollWidget()));
    w->rect.size.x = 80 * gap_UI;
    setFlags_Widget(w, fixedWidth_WidgetFlag, iTrue);
    iWidget *modeButtons = makeHDiv_Widget();
    setFlags_Widget(modeButtons, arrangeWidth_WidgetFlag | arrangeHeight_WidgetFlag, iTrue);
    addChild_Widget(w, iClob(modeButtons));
    addChildFlags_Widget(
        modeButtons,
        iClob(new_LabelWidget(
            "\U0001f5b9 Outline", 0, 0, format_CStr("sidebar.mode arg:%d", documentOutline_SidebarMode))), frameless_WidgetFlag);
    addChildFlags_Widget(
        modeButtons,
        iClob(new_LabelWidget(
            "\U0001f588 Bookmarks", 0, 0, format_CStr("sidebar.mode arg:%d", bookmarks_SidebarMode))), frameless_WidgetFlag);
    addChildFlags_Widget(
        modeButtons,
        iClob(new_LabelWidget(
            "\U0001f553 History", 0, 0, format_CStr("sidebar.mode arg:%d", history_SidebarMode))), frameless_WidgetFlag);
    addChildFlags_Widget(
        modeButtons,
        iClob(new_LabelWidget(
            "\U0001f464 Identities", 0, 0, format_CStr("sidebar.mode arg:%d", identities_SidebarMode))), frameless_WidgetFlag);
}

void deinit_SidebarWidget(iSidebarWidget *d) {
    iUnused(d);
}

static iBool processEvent_SidebarWidget_(iSidebarWidget *d, const SDL_Event *ev) {
    iWidget *w = as_Widget(d);
    return processEvent_Widget(w, ev);
}

static void draw_SidebarWidget_(const iSidebarWidget *d) {
    const iWidget *w = constAs_Widget(d);
    draw_Widget(w);
}

iBeginDefineSubclass(SidebarWidget, Widget)
    .processEvent = (iAny *) processEvent_SidebarWidget_,
    .draw         = (iAny *) draw_SidebarWidget_,
iEndDefineSubclass(SidebarWidget)
