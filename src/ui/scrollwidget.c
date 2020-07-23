#include "scrollwidget.h"
#include "paint.h"

iDefineObjectConstruction(ScrollWidget)

struct Impl_ScrollWidget {
    iWidget widget;
    iRangei range;
    int thumb;
    int thumbSize;
};

void init_ScrollWidget(iScrollWidget *d) {
    iWidget *w = as_Widget(d);
    init_Widget(w);
    setId_Widget(w, "scroll");
    setFlags_Widget(w,
                    fixedWidth_WidgetFlag | resizeToParentHeight_WidgetFlag |
                        moveToParentRightEdge_WidgetFlag,
                    iTrue);
    w->rect.size.x = gap_UI * 3;
}

void deinit_ScrollWidget(iScrollWidget *d) {
    iUnused(d);
}

static iRect thumbRect_ScrollWidget_(const iScrollWidget *d) {
    const iRect bounds = bounds_Widget(constAs_Widget(d));
    iRect rect = init_Rect(bounds.pos.x, bounds.pos.y, bounds.size.x, 0);
    const int total = size_Range(&d->range);
//    printf("total: %d  thumb: %d  thsize: %d\n", total, d->thumb, d->thumbSize); fflush(stdout);
    if (total > 0) {
        const int tsize = iMax(gap_UI * 6, d->thumbSize);
        const int tpos =
            iClamp((float) d->thumb / (float) total, 0, 1) * (height_Rect(bounds) - tsize);
        rect.pos.y  = tpos;
        rect.size.y = tsize;
    }
    return rect;
}

static void checkVisible_ScrollWidget_(iScrollWidget *d) {
    setFlags_Widget(as_Widget(d), hidden_WidgetFlag, height_Rect(thumbRect_ScrollWidget_(d)) == 0);
}

void setRange_ScrollWidget(iScrollWidget *d, iRangei range) {
    range.end = iMax(range.start, range.end);
    d->range = range;
    checkVisible_ScrollWidget_(d);
}

void setThumb_ScrollWidget(iScrollWidget *d, int thumb, int thumbSize) {
    d->thumb     = thumb;
    d->thumbSize = thumbSize;
    checkVisible_ScrollWidget_(d);
}

static iBool processEvent_ScrollWidget_(iScrollWidget *d, const SDL_Event *ev) {
    iWidget *w = as_Widget(d);
    return processEvent_Widget(w, ev);
}

static void draw_ScrollWidget_(const iScrollWidget *d) {
    const iWidget *w = constAs_Widget(d);
    const iRect bounds = bounds_Widget(w);
    if (bounds.size.x > 0) {
        iPaint p;
        init_Paint(&p);
        drawRect_Paint(&p, bounds, black_ColorId);
        iRect tr = thumbRect_ScrollWidget_(d);
        fillRect_Paint(&p, thumbRect_ScrollWidget_(d), gray50_ColorId);
    }
}

iBeginDefineSubclass(ScrollWidget, Widget)
    .processEvent = (iAny *) processEvent_ScrollWidget_,
    .draw         = (iAny *) draw_ScrollWidget_,
iEndDefineSubclass(ScrollWidget)
