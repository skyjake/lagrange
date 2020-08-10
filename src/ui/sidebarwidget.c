#include "sidebarwidget.h"
#include "scrollwidget.h"
#include "paint.h"

struct Impl_SidebarWidget {
    iWidget widget;
    enum iSidebarMode mode;
    iScrollWidget *scroll;
};

iDefineObjectConstruction(SidebarWidget)

void init_SidebarWidget(iSidebarWidget *d) {
    iWidget *w = as_Widget(d);
    init_Widget(w);
    d->mode = documentOutline_SidebarMode;
    addChild_Widget(w, iClob(d->scroll = new_ScrollWidget()));
    w->rect.size.x = 60 * gap_UI;
    setFlags_Widget(w, fixedWidth_WidgetFlag, iTrue);
    setBackgroundColor_Widget(w, red_ColorId);
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
