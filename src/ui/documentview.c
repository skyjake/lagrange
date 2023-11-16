/* Copyright 2023 Jaakko Keränen <jaakko.keranen@iki.fi>

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

#include "documentview.h"

#include "app.h"
#include "audio/player.h"
#include "banner.h"
#include "bookmarks.h"
#include "defs.h"
#include "documentwidget.h"
#include "gempub.h"
#include "gmrequest.h"
#include "gmutil.h"
#include "media.h"
#include "paint.h"
#include "root.h"
#include "mediaui.h"
#include "touch.h"
#include "util.h"

#if defined (iPlatformAppleDesktop)
#   include "macos.h"
#endif
#if defined (iPlatformAppleMobile)
#   include "ios.h"
#endif
#if defined (iPlatformAndroidMobile)
#   include "android.h"
#endif

#include <the_Foundation/archive.h>
#include <the_Foundation/file.h>
#include <the_Foundation/fileinfo.h>
#include <the_Foundation/objectlist.h>
#include <the_Foundation/path.h>
#include <the_Foundation/ptrarray.h>
#include <the_Foundation/regexp.h>
#include <the_Foundation/stringarray.h>
#include <SDL_clipboard.h>
#include <SDL_timer.h>
#include <SDL_render.h>
#include <ctype.h>
#include <errno.h>

/*----------------------------------------------------------------------------------------------*/

struct Impl_DrawBufs {
    int          flags;
    SDL_Texture *sideIconBuf;
    iTextBuf    *timestampBuf;
    uint32_t     lastRenderTime;
};

static void init_DrawBufs(iDrawBufs *d) {
    d->flags = 0;
    d->sideIconBuf = NULL;
    d->timestampBuf = NULL;
    d->lastRenderTime = 0;
}

static void deinit_DrawBufs(iDrawBufs *d) {
    delete_TextBuf(d->timestampBuf);
    if (d->sideIconBuf) {
        SDL_DestroyTexture(d->sideIconBuf);
    }
}

iDefineTypeConstruction(DrawBufs)

/*----------------------------------------------------------------------------------------------*/

struct Impl_VisBufMeta {
    iGmRunRange runsDrawn;
};

static void visBufInvalidated_(iVisBuf *d, size_t index) {
    iVisBufMeta *meta = d->buffers[index].user;
    iZap(meta->runsDrawn);
}

/*----------------------------------------------------------------------------------------------*/

iDefineTypeConstruction(DocumentView)

void init_DocumentView(iDocumentView *d) {
    d->owner         = NULL;
    d->doc           = new_GmDocument();
    d->invalidRuns   = new_PtrSet();
    d->drawBufs      = new_DrawBufs();
    d->pageMargin    = 5;
    d->hoverPre      = NULL;
    d->hoverAltPre   = NULL;
    d->hoverLink     = NULL;
    d->animWideRunId = 0;
    init_Anim(&d->animWideRunOffset, 0);
    iZap(d->renderRuns);
    iZap(d->visibleRuns);
    d->visBuf = new_VisBuf(); {
        d->visBufMeta = malloc(sizeof(iVisBufMeta) * numBuffers_VisBuf);
        /* Additional metadata for each buffer. */
        d->visBuf->bufferInvalidated = visBufInvalidated_;
        for (size_t i = 0; i < numBuffers_VisBuf; i++) {
            d->visBuf->buffers[i].user = d->visBufMeta + i;
        }
    }
    init_Anim(&d->sideOpacity, 0);
    init_Anim(&d->altTextOpacity, 0);
    init_PtrArray(&d->visibleLinks);
    init_PtrArray(&d->visiblePre);
    init_PtrArray(&d->visibleWideRuns);
    init_Array(&d->wideRunOffsets, sizeof(int));
    init_PtrArray(&d->visibleMedia);
    d->userHasScrolled = iFalse;
}

void deinit_DocumentView(iDocumentView *d) {
    removeTicker_App(prerender_DocumentView, d);
    delete_DrawBufs(d->drawBufs);
    delete_VisBuf(d->visBuf);
    free(d->visBufMeta);
    delete_PtrSet(d->invalidRuns);
    deinit_Array(&d->wideRunOffsets);
    deinit_PtrArray(&d->visibleMedia);
    deinit_PtrArray(&d->visibleWideRuns);
    deinit_PtrArray(&d->visiblePre);
    deinit_PtrArray(&d->visibleLinks);
    iReleasePtr(&d->doc);
}

void setOwner_DocumentView(iDocumentView *d, iDocumentWidget *doc) {
    d->owner = doc;
    d->userHasScrolled = iFalse;
    init_SmoothScroll(&d->scrollY, as_Widget(doc), scrollBegan_DocumentWidget);
    if (deviceType_App() != desktop_AppDeviceType) {
        d->scrollY.flags |= pullDownAction_SmoothScrollFlag; /* pull to refresh */
    }
}

static int runOffset_DocumentView_(const iDocumentView *d, const iGmRun *run) {
    if (preId_GmRun(run) && run->flags & wide_GmRunFlag) {
        if (d->animWideRunId == preId_GmRun(run)) {
            return -value_Anim(&d->animWideRunOffset);
        }
        const size_t numOffsets = size_Array(&d->wideRunOffsets);
        const int *offsets = constData_Array(&d->wideRunOffsets);
        if (preId_GmRun(run) <= numOffsets) {
            return -offsets[preId_GmRun(run) - 1];
        }
    }
    return 0;
}

static int *wideRunOffset_DocumentView_(iDocumentView *d, uint16_t preId) {
    if (size_Array(&d->wideRunOffsets) < preId) {
        resize_Array(&d->wideRunOffsets, preId);
    }
    return at_Array(&d->wideRunOffsets, preId - 1);
}

void resetWideRuns_DocumentView(iDocumentView *d) {
    clear_Array(&d->wideRunOffsets);
    for (size_t i = 0; i < numPre_GmDocument(d->doc); i++) {
        const uint16_t    preId = i + 1;
        const iGmPreMeta *meta  = preMeta_GmDocument(d->doc, preId);
        if (meta->initialOffset) {
            *wideRunOffset_DocumentView_(d, preId) = meta->initialOffset;
        }
    }
    d->animWideRunId = 0;
    init_Anim(&d->animWideRunOffset, 0);
    iZap(d->animWideRunRange);
}

void invalidateAndResetWideRunsWithNonzeroOffset_DocumentView(iDocumentView *d) {
    iConstForEach(PtrArray, i, &d->visibleWideRuns) {
        const iGmRun *run = i.ptr;
        if (runOffset_DocumentView_(d, run)) {
            insert_PtrSet(d->invalidRuns, run);
        }
    }
    resetWideRuns_DocumentView(d);
}

int documentWidth_DocumentView(const iDocumentView *d) {
    const iWidget *w        = constAs_Widget(d->owner);
    const iRect    bounds   = bounds_Widget(w);
    const iPrefs * prefs    = prefs_App();
    const int      minWidth = 50 * gap_UI * aspect_UI; /* lines must fit a word at least */
    const float    adjust   = iClamp((float) bounds.size.x / gap_UI / 11 - 12,
                                -1.0f, 10.0f); /* adapt to width */
    //printf("%f\n", adjust); fflush(stdout);
    int prefsWidth = prefs->lineWidth;
    if (isTerminal_Platform()) {
        prefsWidth /= aspect_UI * 0.8f;
    }
    return iMini(iMax(minWidth, bounds.size.x - gap_UI * (d->pageMargin + adjust) * 2),
                 fontSize_UI * prefsWidth * prefs->zoomPercent / 100);
}

int documentTopPad_DocumentView(const iDocumentView *d) {
    /* Amount of space between banner and top of the document. */
    return isEmpty_Banner(d->banner) ? 0 : lineHeight_Text(paragraph_FontId);
}

static int documentTopMargin_DocumentView_(const iDocumentView *d) {
    return (isEmpty_Banner(d->banner) ? d->pageMargin * gap_UI : height_Banner(d->banner)) +
           documentTopPad_DocumentView(d);
}

int pageHeight_DocumentView(const iDocumentView *d) {
    return height_Banner(d->banner) + documentTopPad_DocumentView(d) + size_GmDocument(d->doc).y;
}

iRect documentBounds_DocumentView(const iDocumentView *d) {
    const iRect bounds = bounds_Widget(constAs_Widget(d->owner));
    const int   margin = gap_UI * d->pageMargin;
    iRect       rect;
    iBool       wasCentered = iFalse;
    rect.size.x = documentWidth_DocumentView(d);
    rect.pos.x  = mid_Rect(bounds).x - rect.size.x / 2;
    rect.pos.y  = top_Rect(bounds) + margin;
    rect.size.y = height_Rect(bounds) - margin;
    const iWidget *footerButtons = footerButtons_DocumentWidget(d->owner);
    /* TODO: Further separation of View and Widget: configure header and footer heights
       without involving the widget here. */
    if (d->flags & centerVertically_DocumentViewFlag) {
        const int docSize = documentTopMargin_DocumentView_(d) + size_GmDocument(d->doc).y;
        if (size_GmDocument(d->doc).y == 0) {
            /* Document is empty; maybe just showing an error banner. */
            rect.pos.y = top_Rect(bounds) + height_Rect(bounds) / 2 -
                         documentTopPad_DocumentView(d) - height_Banner(d->banner) / 2;
            rect.size.y = 0;
            wasCentered = iTrue;
        }
        else if (docSize + height_Widget(footerButtons_DocumentWidget(d->owner)) < rect.size.y) {
            /* Center vertically when the document is short. */
            const int relMidY   = (height_Rect(bounds) -
                                   height_Widget(footerButtons) -
                                   phoneToolbarHeight_DocumentWidget(d->owner)) / 2;
            const int visHeight = size_GmDocument(d->doc).y +
                                  height_Widget(footerButtons);
            const int offset    = -height_Banner(d->banner) -
                                  documentTopPad_DocumentView(d) +
                                  height_Widget(footerButtons);
            rect.pos.y  = top_Rect(bounds) + iMaxi(0, relMidY - visHeight / 2 + offset);
            rect.size.y = size_GmDocument(d->doc).y + documentTopMargin_DocumentView_(d);
            wasCentered = iTrue;
        }
    }
    if (!wasCentered) {
        /* The banner overtakes the top margin. */
        if (!isEmpty_Banner(d->banner)) {
            rect.pos.y -= margin;
        }
        else {
            rect.size.y -= margin;
        }
    }
    return rect;
}

int viewPos_DocumentView(const iDocumentView *d) {
    return height_Banner(d->banner) + documentTopPad_DocumentView(d) -
           pos_SmoothScroll(&d->scrollY);
}

static iInt2 documentPos_DocumentView_(const iDocumentView *d, iInt2 pos) {
    return addY_I2(sub_I2(pos, topLeft_Rect(documentBounds_DocumentView(d))),
                   -viewPos_DocumentView(d));
}

iRangei visibleRange_DocumentView(const iDocumentView *d) {
    int top = pos_SmoothScroll(&d->scrollY) - height_Banner(d->banner) -
              documentTopPad_DocumentView(d);
    if (isEmpty_Banner(d->banner)) {
        /* Top padding is not collapsed. */
        top -= d->pageMargin * gap_UI;
    }
    return (iRangei){ top, top + height_Rect(bounds_Widget(constAs_Widget(d->owner))) };
}

static void addVisible_DocumentView_(void *context, const iGmRun *run) {
    iDocumentView *d = context;
    if (~run->flags & decoration_GmRunFlag && !run->mediaId) {
        if (!d->visibleRuns.start) {
            d->visibleRuns.start = run;
        }
        d->visibleRuns.end = run;
    }
    if (preId_GmRun(run)) {
        pushBack_PtrArray(&d->visiblePre, run);
        if (run->flags & wide_GmRunFlag) {
            pushBack_PtrArray(&d->visibleWideRuns, run);
        }
    }
    /* Image runs are static so they're drawn as part of the content. */
    if (isMedia_GmRun(run) && run->mediaType != image_MediaType) {
        iAssert(run->mediaId);
        pushBack_PtrArray(&d->visibleMedia, run);
    }
    if (run->linkId) {
        pushBack_PtrArray(&d->visibleLinks, run);
    }
}

const iGmRun *lastVisibleLink_DocumentView(const iDocumentView *d) {
    iReverseConstForEach(PtrArray, i, &d->visibleLinks) {
        const iGmRun *run = i.ptr;
        if (run->flags & decoration_GmRunFlag && run->linkId) {
            return run;
        }
    }
    return NULL;
}

static int scrollMax_DocumentView_(const iDocumentView *d) {
    const iWidget *w = constAs_Widget(d->owner);
    int sm = pageHeight_DocumentView(d) +
             (isEmpty_Banner(d->banner) ? 2 : 1) * d->pageMargin * gap_UI + /* top and bottom margins */
             footerHeight_DocumentWidget(d->owner) - height_Rect(bounds_Widget(w));
    return iMax(0, sm);
}

float normScrollPos_DocumentView(const iDocumentView *d) {
    const int height = pageHeight_DocumentView(d);
    if (height > 0) {
        float pos = pos_SmoothScroll(&d->scrollY) / (float) height;
        return iMax(pos, 0.0f);
    }
    return 0;
}

void invalidateLink_DocumentView(iDocumentView *d, iGmLinkId id) {
    /* A link has multiple runs associated with it. */
    iConstForEach(PtrArray, i, &d->visibleLinks) {
        const iGmRun *run = i.ptr;
        if (run->linkId == id) {
            insert_PtrSet(d->invalidRuns, run);
        }
    }
}

void invalidateVisibleLinks_DocumentView(iDocumentView *d) {
    iConstForEach(PtrArray, i, &d->visibleLinks) {
        const iGmRun *run = i.ptr;
        if (run->linkId) {
            insert_PtrSet(d->invalidRuns, run);
        }
    }
}

void updateHoverLinkInfo_DocumentView(iDocumentView *d) {
    updateHoverLinkInfo_DocumentWidget(d->owner, d->hoverLink ? d->hoverLink->linkId : 0);
}

void updateHover_DocumentView(iDocumentView *d, iInt2 mouse) {
    const iWidget *w            = constAs_Widget(d->owner);
    const iRect    docBounds    = documentBounds_DocumentView(d);
    const iGmRun * oldHoverLink = d->hoverLink;
    d->hoverPre          = NULL;
    d->hoverLink         = NULL;
    const iInt2 hoverPos = addY_I2(sub_I2(mouse, topLeft_Rect(docBounds)),
                                   -viewPos_DocumentView(d));
    const iGmRun *selectableRun = NULL;
    if (isHoverAllowed_DocumentWidget(d->owner)) {
        /* Look for any selectable text run. */
        for (const iGmRun *v = d->visibleRuns.start; v && v != d->visibleRuns.end; v++) {
            if (~v->flags & decoration_GmRunFlag && !isEmpty_Range(&v->text) &&
                contains_Rect(v->bounds, hoverPos)) {
                selectableRun = v;
                break;
            }
        }
        iConstForEach(PtrArray, i, &d->visibleLinks) {
            const iGmRun *run = i.ptr;
            /* Click targets are slightly expanded so there are no gaps between links. */
            if (contains_Rect(expanded_Rect(run->bounds, init1_I2(gap_Text / 2)), hoverPos)) {
                d->hoverLink = run;
                break;
            }
        }
    }
    if (d->hoverLink != oldHoverLink) {
        if (oldHoverLink) {
            invalidateLink_DocumentView(d, oldHoverLink->linkId);
        }
        if (d->hoverLink) {
            invalidateLink_DocumentView(d, d->hoverLink->linkId);
        }
        updateHoverLinkInfo_DocumentView(d);
        refresh_Widget(w);
    }
    /* Hovering over preformatted blocks. */
    if (isHoverAllowed_DocumentWidget(d->owner) && contains_Widget(w, mouse)) {
        iConstForEach(PtrArray, j, &d->visiblePre) {
            const iGmRun *run = j.ptr;
            if (contains_Rangei(ySpan_Rect(run->bounds), hoverPos.y) &&
                (run->flags & wide_GmRunFlag || contains_Rangei(xSpan_Rect(docBounds), mouse.x))) {
                d->hoverPre    = run;
                d->hoverAltPre = run;
                break;
            }
        }
    }
    if (!d->hoverPre) {
        setValueSpeed_Anim(&d->altTextOpacity, 0.0f, 1.5f);
        if (!isFinished_Anim(&d->altTextOpacity)) {
            animate_DocumentWidget(d->owner);
        }
    }
    else if (d->hoverPre &&
             preHasAltText_GmDocument(d->doc, preId_GmRun(d->hoverPre)) &&
             !noHoverWhileScrolling_DocumentWidget(d->owner)) {
        setValueSpeed_Anim(&d->altTextOpacity, 1.0f, 1.5f);
        if (!isFinished_Anim(&d->altTextOpacity)) {
            animate_DocumentWidget(d->owner);
        }
    }
    if (isHover_Widget(w) &&
        !contains_Widget(constAs_Widget(scrollBar_DocumentWidget(d->owner)), mouse)) {
        setCursor_Window(get_Window(),
                         d->hoverLink || d->hoverPre ? SDL_SYSTEM_CURSOR_HAND
                         : selectableRun             ? SDL_SYSTEM_CURSOR_IBEAM
                                                     : SDL_SYSTEM_CURSOR_ARROW);
        if (d->hoverLink &&
            linkFlags_GmDocument(d->doc, d->hoverLink->linkId) & permanent_GmLinkFlag) {
            setCursor_Window(get_Window(), SDL_SYSTEM_CURSOR_ARROW); /* not dismissable */
        }
    }
}

void updateSideOpacity_DocumentView(iDocumentView *d, iBool isAnimated) {
    float opacity = 0.0f;
    if (!isEmpty_Banner(d->banner) &&
        height_Banner(d->banner) < pos_SmoothScroll(&d->scrollY)) {
        opacity = 1.0f;
    }
    setValue_Anim(&d->sideOpacity, opacity, isAnimated ? (opacity < 0.5f ? 100 : 200) : 0);
    animate_DocumentWidget(d->owner);
}

static iRangecc currentHeading_DocumentView_(const iDocumentView *d) {
    iRangecc heading = iNullRange;
    if (d->visibleRuns.start) {
        iConstForEach(Array, i, headings_GmDocument(d->doc)) {
            const iGmHeading *head = i.value;
            if (head->level == 0) {
                if (head->text.start <= d->visibleRuns.start->text.start) {
                    heading = head->text;
                }
                if (d->visibleRuns.end && head->text.start > d->visibleRuns.end->text.start) {
                    break;
                }
            }
        }
    }
    return heading;
}

int updateScrollMax_DocumentView(iDocumentView *d) {
    arrange_Widget(footerButtons_DocumentWidget(d->owner)); /* scrollMax depends on footer height */
    const int scrollMax = scrollMax_DocumentView_(d);
    setMax_SmoothScroll(&d->scrollY, scrollMax);
    return scrollMax;
}

void updateVisible_DocumentView(iDocumentView *d) {
    const int scrollMax = updateScrollMax_DocumentView(d);
    aboutToScrollView_DocumentWidget(d->owner, scrollMax); /* TODO: A widget may have many views. */
    clear_PtrArray(&d->visibleLinks);
    clear_PtrArray(&d->visibleWideRuns);
    clear_PtrArray(&d->visiblePre);
    clear_PtrArray(&d->visibleMedia);
    const iRangei  visRange   = visibleRange_DocumentView(d);
    const iRangecc oldHeading = currentHeading_DocumentView_(d);
    /* Scan for visible runs. */ {
        iZap(d->visibleRuns);
        render_GmDocument(d->doc, visRange, addVisible_DocumentView_, d);
    }
    const iRangecc newHeading = currentHeading_DocumentView_(d);
    if (memcmp(&oldHeading, &newHeading, sizeof(oldHeading))) {
        d->drawBufs->flags |= updateSideBuf_DrawBufsFlag;
    }
    updateHover_DocumentView(d, mouseCoord_Window(get_Window(), 0));
    updateSideOpacity_DocumentView(d, iTrue);
    didScrollView_DocumentWidget(d->owner);
}

void updateDrawBufs_DocumentView(iDocumentView *d, int drawBufsFlags) {
    d->drawBufs->flags |= drawBufsFlags;
}

void swap_DocumentView(iDocumentView *d, iDocumentView *swapBuffersWith) {
    /* TODO: This must go! Views should not be swapped between widgets! */
    d->scrollY        = swapBuffersWith->scrollY;
    d->scrollY.widget = as_Widget(d->owner);
    iSwap(iVisBuf *,     d->visBuf,     swapBuffersWith->visBuf);
    iSwap(iVisBufMeta *, d->visBufMeta, swapBuffersWith->visBufMeta);
    iSwap(iDrawBufs *,   d->drawBufs,   swapBuffersWith->drawBufs);
    updateVisible_DocumentView(d);
    updateVisible_DocumentView(swapBuffersWith);
}

static void updateTimestampBuf_DocumentView_(const iDocumentView *d) {
    if (!isExposed_Window(get_Window())) {
        return;
    }
    if (d->drawBufs->timestampBuf) {
        delete_TextBuf(d->drawBufs->timestampBuf);
        d->drawBufs->timestampBuf = NULL;
    }
    iTime sourceTime = sourceTime_DocumentWidget(d->owner);
    if (isValid_Time(&sourceTime)) {
        iString *fmt = timeFormatHourPreference_Lang("page.timestamp");
        replace_String(fmt, "\n", " "); /* TODO: update original lang strings */
        d->drawBufs->timestampBuf = newRange_TextBuf(
            uiLabel_FontId,
            white_ColorId,
            range_String(collect_String(format_Time(&sourceTime, cstr_String(fmt)))));
        delete_String(fmt);
    }
    d->drawBufs->flags &= ~updateTimestampBuf_DrawBufsFlag;
}

void invalidate_DocumentView(iDocumentView *d) {
    invalidate_VisBuf(d->visBuf);
    clear_PtrSet(d->invalidRuns);
}

void documentRunsInvalidated_DocumentView(iDocumentView *d) {
    /* Note: Don't call this only, the owner widget keeps pointers, too. */
    d->hoverPre    = NULL;
    d->hoverAltPre = NULL;
    d->hoverLink   = NULL;
    clear_PtrArray(&d->visibleMedia);
    iZap(d->visibleRuns);
    iZap(d->renderRuns);
}

void resetScroll_DocumentView(iDocumentView *d) {
    reset_SmoothScroll(&d->scrollY);
    d->userHasScrolled = iFalse;
    init_Anim(&d->sideOpacity, 0);
    init_Anim(&d->altTextOpacity, 0);
    resetWideRuns_DocumentView(d);
}

iBool updateWidth_DocumentView(iDocumentView *d) {
    if (updateWidth_GmDocument(d->doc, documentWidth_DocumentView(d), width_Widget(d->owner))) {
        documentRunsInvalidated_DocumentView(d); /* GmRuns reallocated */
        return iTrue;
    }
    return iFalse;
}

void clampScroll_DocumentView(iDocumentView *d) {
    move_SmoothScroll(&d->scrollY, 0);
}

void immediateScroll_DocumentView(iDocumentView *d, int offset) {
    move_SmoothScroll(&d->scrollY, offset);
    d->userHasScrolled = iTrue;
}

void smoothScroll_DocumentView(iDocumentView *d, int offset, int duration) {
    moveSpan_SmoothScroll(&d->scrollY, offset, duration);
    d->userHasScrolled = iTrue;
}

void scrollTo_DocumentView(iDocumentView *d, int documentY, iBool centered) {
    if (!isEmpty_Banner(d->banner)) {
        documentY += height_Banner(d->banner) + documentTopPad_DocumentView(d);
    }
    else {
        documentY += documentTopPad_DocumentView(d) + d->pageMargin * gap_UI;
    }
    init_Anim(&d->scrollY.pos,
              documentY - (centered ? documentBounds_DocumentView(d).size.y / 2
                                    : lineHeight_Text(paragraph_FontId)));
    clampScroll_DocumentView(d);
    updateVisible_DocumentView(d);
}

void scrollToHeading_DocumentView(iDocumentView *d, const char *heading) {
    /* Try an exact match first and then try finding a prefix. */
    for (int pass = 0; pass < 2; pass++) {
        iConstForEach(Array, h, headings_GmDocument(d->doc)) {
            const iGmHeading *head = h.value;
            if ((pass == 0 && equalCase_Rangecc     (head->text, heading)) ||
                (pass == 1 && startsWithCase_Rangecc(head->text, heading))) {
                postCommandf_Root(as_Widget(d->owner)->root, "document.goto loc:%p",
                                  head->text.start);
                return;
            }
        }
    }
}

iBool isWideBlockScrollable_DocumentView(const iDocumentView *d, const iRect docBounds,
                                         const iGmRun *run) {
    const iGmPreMeta *meta       = preMeta_GmDocument(d->doc, preId_GmRun(run));
    const iGmRunRange range      = meta->runRange;
    int               maxWidth   = width_Rect(meta->pixelRect);
    const iRect       pageBounds = shrunk_Rect(bounds_Widget(as_Widget(d->owner)),
                                               init1_I2(d->pageMargin * gap_UI));
    return left_Rect(docBounds) + run->bounds.pos.x + meta->initialOffset + maxWidth >
           right_Rect(pageBounds);
}

iBool scrollWideBlock_DocumentView(iDocumentView *d, iInt2 mousePos, int delta, int duration,
                                   iBool *isAtEnd_out) {
    if (delta == 0 || wheelSwipeState_DocumentWidget(d->owner) == direct_WheelSwipeState) {
        return iFalse;
    }
    const int docWidth = documentWidth_DocumentView(d);
    const iRect docBounds = documentBounds_DocumentView(d);
    const iInt2 docPos = documentPos_DocumentView_(d, mousePos);
    if (isAtEnd_out) {
        *isAtEnd_out = iFalse;
    }
    iConstForEach(PtrArray, i, &d->visibleWideRuns) {
        const iGmRun *run = i.ptr;
        if (contains_Rangei(ySpan_Rect(run->bounds), docPos.y)) {
            /* We can scroll this run. First find out how much is allowed. */
            const iGmPreMeta *meta = preMeta_GmDocument(d->doc, preId_GmRun(run));
            const iGmRunRange range = meta->runRange;
            int maxWidth = width_Rect(meta->pixelRect);
            if (!isWideBlockScrollable_DocumentView(d, docBounds, run)) {
                return iFalse;
            }
            const int maxOffset = maxWidth + run->bounds.pos.x - docWidth;
            int *offset = wideRunOffset_DocumentView_(d, preId_GmRun(run));
            const int oldOffset = *offset;
            *offset = iClamp(*offset + delta, 0, maxOffset);
            /* Make sure the whole block gets redraw. */
            if (oldOffset != *offset) {
                for (const iGmRun *r = range.start; r != range.end; r++) {
                    insert_PtrSet(d->invalidRuns, r);
                }
                refresh_Widget(d->owner);
                *d->selectMark = iNullRange;
                *d->foundMark  = iNullRange;
                if (duration) {
                    if (d->animWideRunId != preId_GmRun(run) || isFinished_Anim(&d->animWideRunOffset)) {
                        d->animWideRunId = preId_GmRun(run);
                        init_Anim(&d->animWideRunOffset, oldOffset);
                    }
                    setValueEased_Anim(&d->animWideRunOffset, *offset, duration);
                    d->animWideRunRange = range;
                    addTicker_App(refreshWhileScrolling_DocumentWidget, d->owner);
                }
                else {
                    d->animWideRunId = 0;
                    init_Anim(&d->animWideRunOffset, 0);
                }
            }
            else {
                /* Offset didn't change. We could consider allowing swipe navigation to occur
                   by returning iFalse here, but perhaps only if the original starting
                   offset of the wide block was at the far end already .*/
                if (isAtEnd_out) {
                    *isAtEnd_out = iTrue;
                }
            }
            return iTrue;
        }
    }
    return iFalse;
}

iRangecc sourceLoc_DocumentView(const iDocumentView *d, iInt2 pos) {
    return findLoc_GmDocument(d->doc, documentPos_DocumentView_(d, pos));
}

iDeclareType(MiddleRunParams)

struct Impl_MiddleRunParams {
    int midY;
    const iGmRun *closest;
    int distance;
};

static void find_MiddleRunParams_(void *params, const iGmRun *run) {
    iMiddleRunParams *d = params;
    if (isEmpty_Rect(run->bounds)) {
        return;
    }
    const int distance = iAbs(mid_Rect(run->bounds).y - d->midY);
    if (!d->closest || distance < d->distance) {
        d->closest  = run;
        d->distance = distance;
    }
}

static const iGmRun *middleRun_DocumentView_(const iDocumentView *d) {
    iRangei visRange = visibleRange_DocumentView(d);
    iMiddleRunParams params = { (visRange.start + visRange.end) / 2, NULL, 0 };
    render_GmDocument(d->doc, visRange, find_MiddleRunParams_, &params);
    return params.closest;
}

void allocVisBuffer_DocumentView(const iDocumentView *d) {
    const iWidget *w         = constAs_Widget(d->owner);
    const iBool    isVisible = isVisible_Widget(w);
    const iInt2    size      = bounds_Widget(w).size;
    if (isVisible) {
        alloc_VisBuf(d->visBuf, size, 1);
    }
    else {
        dealloc_VisBuf(d->visBuf);
    }
}

size_t visibleLinkOrdinal_DocumentView(const iDocumentView *d, iGmLinkId linkId) {
    size_t ord = 0;
    const iRangei visRange = visibleRange_DocumentView(d);
    iConstForEach(PtrArray, i, &d->visibleLinks) {
        const iGmRun *run = i.ptr;
        if (top_Rect(run->visBounds) >= visRange.start + gap_UI * d->pageMargin * 4 / 5) {
            if (run->flags & decoration_GmRunFlag && run->linkId) {
                if (run->linkId == linkId) return ord;
                ord++;
            }
        }
    }
    return iInvalidPos;
}

iBool updateDocumentWidthRetainingScrollPosition_DocumentView(iDocumentView *d, iBool keepCenter) {
    const int newWidth = documentWidth_DocumentView(d);
    if (newWidth == size_GmDocument(d->doc).x && !keepCenter /* not a font change */) {
        return iFalse;
    }
    /* Font changes (i.e., zooming) will keep the view centered, otherwise keep the top
       of the visible area fixed. */
    const iGmRun *run     = keepCenter ? middleRun_DocumentView_(d) : d->visibleRuns.start;
    const char *  runLoc  = (run ? run->text.start : NULL);
    int           voffset = 0;
    if (!keepCenter && run) {
        /* Keep the first visible run visible at the same position. */
        /* TODO: First *fully* visible run? */
        voffset = visibleRange_DocumentView(d).start - top_Rect(run->visBounds);
    }
    run = NULL;
    setWidth_GmDocument(d->doc, newWidth, width_Widget(d->owner));
    setWidth_Banner(d->banner, newWidth);
    documentRunsInvalidated_DocumentWidget(d->owner);
    if (runLoc && !keepCenter) {
        run = findRunAtLoc_GmDocument(d->doc, runLoc);
        if (run) {
            scrollTo_DocumentView(
                d, top_Rect(run->visBounds) + lineHeight_Text(paragraph_FontId) + voffset, iFalse);
        }
    }
    else if (runLoc && keepCenter) {
        run = findRunAtLoc_GmDocument(d->doc, runLoc);
        if (run) {
            scrollTo_DocumentView(d, mid_Rect(run->bounds).y, iTrue);
        }
    }
    return iTrue;
}

iRect runRect_DocumentView(const iDocumentView *d, const iGmRun *run) {
    const iRect docBounds = documentBounds_DocumentView(d);
    return moved_Rect(run->bounds, addY_I2(topLeft_Rect(docBounds), viewPos_DocumentView(d)));
}

iDeclareType(DrawContext)

struct Impl_DrawContext {
    const iDocumentView *view;
    iRect widgetBounds;
    int widgetFullWidth; /* including area behind scrollbar */
    iRect docBounds;
    iRangei vis;
    iInt2 viewPos; /* document area origin */
    iPaint paint;
    iBool inSelectMark;
    iBool inFoundMark;
    iBool showLinkNumbers;
    iRect firstMarkRect;
    iRect lastMarkRect;
    int drawDir; /* -1 for progressive reverse direction */
    iGmRunRange runsDrawn;
};

static int measureAdvanceToLoc_(const iGmRun *run, const char *end) {
    iWrapText wt = { .text     = run->text,
                     .mode     = anyCharacter_WrapTextMode,
                     .maxWidth = isJustified_GmRun(run) ? iAbsi(drawBoundWidth_GmRun(run)) : 0,
                     .justify  = isJustified_GmRun(run),
                     .hitChar  = end };
    measure_WrapText(&wt, run->font);
    return wt.hitAdvance_out.x;
}

static void fillRange_DrawContext_(iDrawContext *d, const iGmRun *run, enum iColorId color,
                                   iRangecc mark, iBool *isInside) {
    if (mark.start > mark.end) {
        /* Selection may be done in either direction. */
        iSwap(const char *, mark.start, mark.end);
    }
    if (*isInside || (contains_Range(&run->text, mark.start) ||
                      contains_Range(&mark, run->text.start))) {
        int x = 0;
        if (!*isInside) {
            x = measureAdvanceToLoc_(run, iMax(run->text.start, mark.start));
        }
        const int boundWidth = iAbsi(drawBoundWidth_GmRun(run));
        int w = boundWidth - x;
        if (contains_Range(&run->text, mark.end) || mark.end < run->text.start) {
            iRangecc mk =
                !*isInside ? mark : (iRangecc){ run->text.start, iMax(run->text.start, mark.end) };
            mk.start  = iMax(mk.start, run->text.start);
            int x1    = measureAdvanceToLoc_(run, mk.start);
            w         = measureAdvanceToLoc_(run, mk.end) - x1;
            *isInside = iFalse;
        }
        else {
            *isInside = iTrue; /* at least until the next run */
        }
        if (w > boundWidth - x) {
            w = boundWidth - x;
        }
        if (~run->flags & decoration_GmRunFlag) {
            const iInt2 visPos =
                add_I2(run->bounds.pos, addY_I2(d->viewPos, viewPos_DocumentView(d->view)));
            const iRect rangeRect = { addX_I2(visPos, x), init_I2(w, height_Rect(run->bounds)) };
            if (rangeRect.size.x) {
                fillRect_Paint(&d->paint, rangeRect, color);
                /* Keep track of the first and last marked rects. */
                if (d->firstMarkRect.size.x == 0) {
                    d->firstMarkRect = rangeRect;
                }
                d->lastMarkRect = rangeRect;
            }
        }
    }
    /* Link URLs are not part of the visible document, so they are ignored above. Handle
       these ranges as a special case. */
    if (run->linkId && run->flags & decoration_GmRunFlag) {
        const iRangecc url = linkUrlRange_GmDocument(d->view->doc, run->linkId);
        if (contains_Range(&url, mark.start) &&
            (contains_Range(&url, mark.end) || url.end == mark.end)) {
            fillRect_Paint(
                &d->paint,
                moved_Rect(run->visBounds, addY_I2(d->viewPos, viewPos_DocumentView(d->view))),
                color);
        }
    }
}

static void drawMark_DrawContext_(void *context, const iGmRun *run) {
    iDrawContext *d = context;
    if (!isMedia_GmRun(run)) {
        fillRange_DrawContext_(d, run, uiMatching_ColorId, *d->view->foundMark, &d->inFoundMark);
        fillRange_DrawContext_(d, run, uiMarked_ColorId, *d->view->selectMark, &d->inSelectMark);
    }
}

static void drawRun_DrawContext_(void *context, const iGmRun *run) {
    iDrawContext *d      = context;
    const iInt2   origin = d->viewPos;
    /* Keep track of the drawn visible runs. */ {
        if (!d->runsDrawn.start || run < d->runsDrawn.start) {
            d->runsDrawn.start = run;
        }
        if (!d->runsDrawn.end || run > d->runsDrawn.end) {
            d->runsDrawn.end = run;
        }
    }
    if (run->mediaType == image_MediaType) {
        SDL_Texture *tex = imageTexture_Media(media_GmDocument(d->view->doc), mediaId_GmRun(run));
        const iRect dst = moved_Rect(run->visBounds, origin);
        if (tex) {
            fillRect_Paint(&d->paint, dst, tmBackground_ColorId); /* in case the image has alpha */
            SDL_RenderCopy(d->paint.dst->render, tex, NULL,
                           &(SDL_Rect){ dst.pos.x, dst.pos.y, dst.size.x, dst.size.y });
        }
        else {
            drawRect_Paint(&d->paint, dst, tmQuoteIcon_ColorId);
            drawCentered_Text(uiLabel_FontId,
                              dst,
                              iFalse,
                              tmQuote_ColorId,
                              explosion_Icon "  Error Loading Image");
        }
        return;
    }
    else if (isMedia_GmRun(run)) {
        /* Media UIs are drawn afterwards as a dynamic overlay. */
        return;
    }
    enum iColorId      fg        = run->color;
    const iGmDocument *doc       = d->view->doc;
    const int          linkFlags = linkFlags_GmDocument(doc, run->linkId);
    /* Hover state of a link. */
    const iBool isPartOfHover = (run->linkId && d->view->hoverLink &&
                                 run->linkId == d->view->hoverLink->linkId);
    iBool isHover = (isPartOfHover && ~run->flags & decoration_GmRunFlag);
    /* Visible (scrolled) position of the run. */
    const iInt2 visPos = addX_I2(add_I2(run->visBounds.pos, origin),
                                 /* Preformatted runs can be scrolled. */
                                 runOffset_DocumentView_(d->view, run));
    const iRect visRect = { visPos, run->visBounds.size };
    /* Fill the background. */ {
#if 0
        iBool isInlineImageCaption = run->linkId && linkFlags & content_GmLinkFlag &&
                                     ~linkFlags & permanent_GmLinkFlag;
        if (run->flags & decoration_GmRunFlag && ~run->flags & startOfLine_GmRunFlag) {
            /* This is the metadata. */
            isInlineImageCaption = iFalse;
        }
#endif
        iBool isMobileHover = deviceType_App() != desktop_AppDeviceType &&
                              (isPartOfHover || contains_PtrSet(d->view->invalidRuns, run)) &&
                              (~run->flags & decoration_GmRunFlag || run->flags & startOfLine_GmRunFlag
                               /* highlight link icon but not image captions */);
        /* While this is consistent, it's a bit excessive to indicate that an inlined image
           is open: the image itself is the indication. */
        const iBool isInlineImageCaption = iFalse;
        if (run->linkId && (linkFlags & isOpen_GmLinkFlag || isInlineImageCaption || isMobileHover)) {
            const int pad = gap_Text;
            int       bg  = tmBackgroundOpenLink_ColorId;
            iRect     wideRect;
            /* Open links get a highlighted background. */
            if (isMobileHover && !isPartOfHover) {
                bg = tmBackground_ColorId; /* hover ended and was invalidated */
            }
            if (linkFlags & inline_GmLinkFlag) {
                wideRect = visRect;
            }
            else {
                wideRect =
                    (iRect){ init_I2(origin.x - pad, visPos.y),
                             init_I2(d->docBounds.size.x + 2 * pad, height_Rect(run->visBounds)) };
                adjustEdges_Rect(&wideRect,
                                 run->flags & startOfLine_GmRunFlag ? -pad * 3 / 4 : 0, 0,
                                 run->flags & endOfLine_GmRunFlag ? pad * 3 / 4 : 0, 0);
                /* The first line is composed of two runs that may be drawn in either order, so
                   only draw half of the background. */
                if (run->flags & decoration_GmRunFlag) {
                    wideRect.size.x = right_Rect(visRect) - left_Rect(wideRect);
                }
                else if (run->flags & startOfLine_GmRunFlag) {
                    wideRect.size.x = right_Rect(wideRect) - left_Rect(visRect);
                    wideRect.pos.x  = left_Rect(visRect);
                }
            }
            fillRect_Paint(&d->paint, wideRect, bg);
        }
        else if (run->flags & wide_GmRunFlag) {
            /* Wide runs may move any amount horizontally. */
            iRect wideRect  = visRect;
            wideRect.pos.x  = 0;
            wideRect.size.x = d->widgetFullWidth;
            /* Due to adaptive scaling of monospace fonts to fit a non-fractional pixel grid,
               there may be a slight overdraw on the edges if glyphs extend to their maximum
               bounds (e.g., box drawing). Ensure that the edges of the preformatted block
               remain clean. (GmDocument leaves empty padding around blocks.) */
            adjustEdges_Rect(&wideRect,
                             run->flags & startOfLine_GmRunFlag ? -gap_UI / 2 : 0, 0,
                             run->flags & endOfLine_GmRunFlag ? gap_UI / 2 : 0, 0);
            fillRect_Paint(&d->paint, wideRect, tmBackground_ColorId);
        }
        else {
            /* Normal background for other runs. There are cases when runs get drawn multiple times,
               e.g., at the buffer boundary, and there are slightly overlapping characters in
               monospace blocks. Clearing the background here ensures a cleaner visual appearance
               since only one glyph is visible at any given point. */
            fillRect_Paint(&d->paint, visRect, tmBackground_ColorId);
        }
    }
    if (run->linkId) {
        if (run->flags & decoration_GmRunFlag && run->flags & startOfLine_GmRunFlag) {
            /* Link icon. */
            if (linkFlags & content_GmLinkFlag) {
                fg = linkColor_GmDocument(doc, run->linkId, textHover_GmLinkPart);
            }
        }
        else if (~run->flags & decoration_GmRunFlag) {
            fg = linkColor_GmDocument(doc, run->linkId, isHover ? textHover_GmLinkPart : text_GmLinkPart);
            if (linkFlags & content_GmLinkFlag) {
                fg = linkColor_GmDocument(doc, run->linkId, textHover_GmLinkPart); /* link is inactive */
            }
        }
    }
    if (run->flags & altText_GmRunFlag) {
        const iInt2 margin = preRunMargin_GmDocument(doc, preId_GmRun(run));
        fillRect_Paint(&d->paint, (iRect){ visPos, run->visBounds.size }, tmBackgroundAltText_ColorId);
        drawRect_Paint(&d->paint, (iRect){ visPos, run->visBounds.size }, tmFrameAltText_ColorId);
        drawWrapRange_Text(run->font,
                           add_I2(visPos, margin),
                           run->visBounds.size.x - 2 * margin.x,
                           run->color,
                           run->text);
    }
    else {
        if (d->showLinkNumbers && run->linkId && run->flags & decoration_GmRunFlag &&
            ~run->flags & caption_GmRunFlag) {
            const size_t ord = visibleLinkOrdinal_DocumentView(d->view, run->linkId);
            if (ord >= ordinalBase_DocumentWidget(d->view->owner)) {
                const iChar ordChar =
                    linkOrdinalChar_DocumentWidget(d->view->owner,
                                                   ord - ordinalBase_DocumentWidget(d->view->owner));
                if (ordChar) {
                    const char *circle = "\u25ef"; /* Large Circle */
                    const int   circleFont = FONT_ID(default_FontId, regular_FontStyle, contentRegular_FontSize);
                    iRect nbArea = { init_I2(d->viewPos.x - gap_UI / 3, visPos.y),
                                    init_I2(3.95f * gap_Text, 1.0f * lineHeight_Text(circleFont)) };
                    if (isTerminal_Platform()) {
                        nbArea.pos.x += 1;
                    }
                    drawRange_Text(
                        circleFont, topLeft_Rect(nbArea), tmQuote_ColorId, range_CStr(circle));
                    iRect circleArea = visualBounds_Text(circleFont, range_CStr(circle));
                    addv_I2(&circleArea.pos, topLeft_Rect(nbArea));
                    drawCentered_Text(FONT_ID(default_FontId, regular_FontStyle, contentSmall_FontSize),
                                      circleArea,
                                      iTrue,
                                      tmQuote_ColorId,
                                      "%lc",
                                      (int) ordChar);
                    goto runDrawn;
                }
            }
        }
        if (run->flags & ruler_GmRunFlag) {
            if (height_Rect(run->visBounds) > 0 &&
                height_Rect(run->visBounds) <= width_Rect(run->visBounds)) {
                /* This is used for block quotes. */
                drawVLine_Paint(&d->paint,
                                addX_I2(visPos,
                                        !run->isRTL
                                            ? -gap_Text * 5 / 2
                                            : (width_Rect(run->visBounds) + gap_Text * 5 / 2)),
                                height_Rect(run->visBounds),
                                tmQuoteIcon_ColorId);
            }
            else {
                drawHLine_Paint(&d->paint, visPos, width_Rect(run->visBounds), tmQuoteIcon_ColorId);
            }
        }
        /* Base attributes. */ {
            int f, c;
            runBaseAttributes_GmDocument(doc, run, &f, &c);
            setBaseAttributes_Text(f, c);
        }
        /* Fancy date in Gemini feed links. */ {
            if (run->linkId && run->flags & startOfLine_GmRunFlag && ~run->flags & decoration_GmRunFlag) {
                static iRegExp *datePattern_;
                if (!datePattern_) {
                    datePattern_ = new_RegExp("^[12][0-9][0-9][0-9]-[01][0-9]-[0-3][0-9]\\s", 0);
                }
                iRegExpMatch m;
                init_RegExpMatch(&m);
                if (matchRange_RegExp(datePattern_, run->text, &m)) {
                    /* The date uses regular weight and a dimmed color. */
                    iString styled;
                    initRange_String(&styled, run->text);
                    insertData_Block(&styled.chars, 10, "\x1b[0m", 4); /* restore */
                    iBlock buf;
                    init_Block(&buf, 0);
                    appendCStr_Block(&buf, "\x1b[10m"); /* regular font weight */
                    appendCStr_Block(&buf, escape_Color(isHover ? fg : tmLinkFeedEntryDate_ColorId));
                    insertData_Block(&styled.chars, 0, constData_Block(&buf), size_Block(&buf));
                    deinit_Block(&buf);
                    const int oldAnsi = ansiFlags_Text();
                    setAnsiFlags_Text(oldAnsi | allowFontStyle_AnsiFlag);
                    setBaseAttributes_Text(run->font, fg);
                    drawBoundRange_Text(run->font,
                                        visPos,
                                        drawBoundWidth_GmRun(run),
                                        isJustified_GmRun(run),
                                        fg,
                                        range_String(&styled));
                    setAnsiFlags_Text(oldAnsi);
                    deinit_String(&styled);
                    goto runDrawn;
                }
            }
        }
        drawBoundRange_Text(run->font,
                            visPos,
                            drawBoundWidth_GmRun(run),
                            isJustified_GmRun(run),
                            fg,
                            run->text);
    runDrawn:;
        setBaseAttributes_Text(-1, -1);
    }
    /* Presentation of links. */
    if (run->linkId && ~run->flags & decoration_GmRunFlag) {
        const int metaFont = paragraph_FontId;
        /* TODO: Show status of an ongoing media request. */
        const int flags = linkFlags;
        const iRect linkRect = moved_Rect(run->visBounds, origin);
        iMediaRequest *mr = NULL;
        /* Show metadata about inline content. */
        if (flags & content_GmLinkFlag && run->flags & endOfLine_GmRunFlag) {
            fg = linkColor_GmDocument(doc, run->linkId, textHover_GmLinkPart);
            iString text;
            init_String(&text);
            const iMediaId linkMedia = findMediaForLink_Media(constMedia_GmDocument(doc),
                                                              run->linkId, none_MediaType);
            iAssert(linkMedia.type != none_MediaType);
            iGmMediaInfo info;
            info_Media(constMedia_GmDocument(doc), linkMedia, &info);
            switch (linkMedia.type) {
            case image_MediaType: {
                /* There's a separate decorative GmRun for the metadata. */
                break;
            }
            case audio_MediaType:
                format_String(&text, "%s", info.type);
                break;
            case download_MediaType:
                format_String(&text, "%s", info.type);
                break;
            default:
                break;
            }
            if (linkMedia.type != download_MediaType && /* can't cancel downloads currently */
                linkMedia.type != image_MediaType &&
                findMediaRequest_DocumentWidget(d->view->owner, run->linkId)) {
                appendFormat_String(
                    &text, "  %s" close_Icon, isHover ? escape_Color(tmLinkText_ColorId) : "");
            }
            const iInt2 size = measureRange_Text(metaFont, range_String(&text)).bounds.size;
            if (size.x) {
                fillRect_Paint(
                    &d->paint,
                    (iRect){ add_I2(origin, addX_I2(topRight_Rect(run->bounds), -size.x - gap_UI)),
                            addX_I2(size, 2 * gap_UI) },
                    tmBackground_ColorId);
                drawAlign_Text(metaFont,
                               add_I2(topRight_Rect(run->bounds), origin),
                               fg,
                               right_Alignment,
                               "%s", cstr_String(&text));
            }
            deinit_String(&text);
        }
        else if (run->flags & endOfLine_GmRunFlag &&
                 (mr = findMediaRequest_DocumentWidget(d->view->owner, run->linkId)) != NULL) {
            if (!isFinished_GmRequest(mr->req)) {
                fillRect_Paint(&d->paint,
                               (iRect){ topRight_Rect(linkRect),
                                        init_I2(d->widgetFullWidth - right_Rect(linkRect),
                                                lineHeight_Text(metaFont)) },
                               tmBackground_ColorId);
                draw_Text(metaFont,
                          topRight_Rect(linkRect),
                          tmInlineContentMetadata_ColorId,
                          translateCStr_Lang(" \u2014 ${doc.fetching}\u2026 (%.1f ${mb})"),
                          (float) bodySize_GmRequest(mr->req) / 1.0e6f);
            }
        }
    }
    /* Debug. */
    if (0) {
        drawRect_Paint(&d->paint, (iRect){ visPos, run->bounds.size }, green_ColorId);
        drawRect_Paint(&d->paint, (iRect){ visPos, run->visBounds.size },
                       run->linkId ? orange_ColorId : red_ColorId);
    }
}

static int drawSideRect_(iPaint *p, iRect rect) {
    int bg = tmBannerBackground_ColorId;
    int fg = tmBannerIcon_ColorId;
    if (equal_Color(get_Color(bg), get_Color(tmBackground_ColorId))) {
        bg = tmBannerIcon_ColorId;
        fg = tmBannerBackground_ColorId;
    }
    fillRect_Paint(p, rect, bg);
    return fg;
}

static int sideElementAvailWidth_DocumentView_(const iDocumentView *d) {
    return left_Rect(documentBounds_DocumentView(d)) -
           left_Rect(bounds_Widget(constAs_Widget(d->owner))) - 2 * d->pageMargin * gap_UI;
}

iLocalDef int minBannerSize_(void) {
    return iMaxi(lineHeight_Text(banner_FontId) * 2, 5);
}

static iBool isSideHeadingVisible_DocumentView_(const iDocumentView *d) {
    return sideElementAvailWidth_DocumentView_(d) >= minBannerSize_() * 2.25f / aspect_UI;
}

static void updateSideIconBuf_DocumentView_(const iDocumentView *d) {
    if (!isExposed_Window(get_Window())) {
        return;
    }
    iDrawBufs *dbuf = d->drawBufs;
    dbuf->flags &= ~updateSideBuf_DrawBufsFlag;
    if (dbuf->sideIconBuf) {
        SDL_DestroyTexture(dbuf->sideIconBuf);
        dbuf->sideIconBuf = NULL;
    }
    //    const iGmRun *banner = siteBanner_GmDocument(d->doc);
    if (isEmpty_Banner(d->banner)) {
        return;
    }
    const int   margin           = gap_UI * d->pageMargin;
    const int   minBannerSize    = minBannerSize_();
    const iChar icon             = siteIcon_GmDocument(d->doc);
    const int   avail            = sideElementAvailWidth_DocumentView_(d) - margin;
    iBool       isHeadingVisible = isSideHeadingVisible_DocumentView_(d);
    /* Determine the required size. */
    iInt2 bufSize = init_I2(minBannerSize / aspect_UI, minBannerSize);
    const int sideHeadingFont = FONT_ID(documentHeading_FontId, regular_FontStyle, contentBig_FontSize);
    if (isHeadingVisible) {
        const iInt2 headingSize = measureWrapRange_Text(sideHeadingFont, avail,
                                                        currentHeading_DocumentView_(d)).bounds.size;
        if (headingSize.x > 0) {
            bufSize.y += gap_Text + headingSize.y;
            bufSize.x = iMax(bufSize.x, headingSize.x);
        }
        else {
            isHeadingVisible = iFalse;
        }
    }
    SDL_Renderer *render = renderer_Window(get_Window());
    dbuf->sideIconBuf = SDL_CreateTexture(render,
                                          SDL_PIXELFORMAT_RGBA4444,
                                          SDL_TEXTUREACCESS_STATIC | SDL_TEXTUREACCESS_TARGET,
                                          bufSize.x, bufSize.y);
    iPaint p;
    init_Paint(&p);
    beginTarget_Paint(&p, dbuf->sideIconBuf);
    const iColor back = get_Color(tmBannerSideTitle_ColorId);
    SDL_SetRenderDrawColor(render, back.r, back.g, back.b, 0); /* better blending of the edge */
    SDL_RenderClear(render);
    const iRect iconRect = { zero_I2(), init_I2(minBannerSize / aspect_UI, minBannerSize) };
    int fg = drawSideRect_(&p, iconRect);
    iString str;
    initUnicodeN_String(&str, &icon, 1);
    drawCentered_Text(banner_FontId, iconRect, iTrue, fg, "%s", cstr_String(&str));
    if (isHeadingVisible) {
        iRangecc    text = currentHeading_DocumentView_(d);
        iInt2       pos  = addY_I2(bottomLeft_Rect(iconRect), gap_Text);
        const int   font = sideHeadingFont;
        /* If the heading starts with the same symbol as we have in the icon, there's no
           point in repeating. The icon is always a non-alphabetic symbol like Emoji so
           we aren't cutting any words off here. */
        if (startsWith_Rangecc(text, cstr_String(&str)) &&
            size_Range(&text) > size_String(&str)) {
            text.start += size_String(&str);
            trimStart_Rangecc(&text);
        }
        iTextMetrics metrics = measureWrapRange_Text(font, avail, text);
        int xOff = 0;
        if (width_Rect(metrics.bounds) < width_Rect(iconRect)) {
            /* Very short captions should be centered under the icon. */
            xOff = (width_Rect(iconRect) - width_Rect(metrics.bounds)) / 2;
        }
        drawWrapRange_Text(font, addX_I2(pos, xOff), avail, tmBannerSideTitle_ColorId, text);
    }
    deinit_String(&str);
    endTarget_Paint(&p);
    SDL_SetTextureBlendMode(dbuf->sideIconBuf, SDL_BLENDMODE_BLEND);
}

static void drawSideElements_DocumentView_(const iDocumentView *d, int horizOffset) {
    if (size_GmDocument(d->doc).y == 0) {
        return;
    }
    const iWidget *w         = constAs_Widget(d->owner);
    const iRect    bounds    = bounds_Widget(w);
    const iRect    docBounds = documentBounds_DocumentView(d);
    const int      margin    = gap_UI * d->pageMargin;
    float          opacity   = value_Anim(&d->sideOpacity);
    const int      avail     = left_Rect(docBounds) - left_Rect(bounds) - 2 * margin;
    iDrawBufs *    dbuf      = d->drawBufs;
    iPaint         p;
    init_Paint(&p);
    setClip_Paint(&p, boundsWithoutVisualOffset_Widget(w));
    /* Side icon and current heading. */
    if (prefs_App()->sideIcon && opacity > 0 && dbuf->sideIconBuf) {
        const iInt2 texSize = size_SDLTexture(dbuf->sideIconBuf);
        if (avail > texSize.x) {
            const int minBannerSize = lineHeight_Text(banner_FontId) * 2;
            iInt2 pos = addY_I2(add_I2(topLeft_Rect(bounds), init_I2(margin, 0)),
                                height_Rect(bounds) / 2 - minBannerSize / 2 -
                                    (texSize.y > minBannerSize
                                         ? (gap_Text + lineHeight_Text(heading3_FontId)) / 2
                                         : 0));
            SDL_SetTextureAlphaMod(dbuf->sideIconBuf, 255 * opacity);
            SDL_RenderCopy(renderer_Window(get_Window()),
                           dbuf->sideIconBuf, NULL,
                           &(SDL_Rect){ pos.x + horizOffset, pos.y, texSize.x, texSize.y });
        }
    }
    /* Reception timestamp. On mobile, it's below the footer in the overscroll area. */
    if (dbuf->timestampBuf) {
        draw_TextBuf(
            dbuf->timestampBuf,
            add_I2(
                init_I2(horizOffset + mid_Rect(docBounds).x - dbuf->timestampBuf->size.x / 2,
                        bottom_Rect(bounds)),
                init_I2(0,
                        (deviceType_App() != phone_AppDeviceType
                             ? -margin + -dbuf->timestampBuf->size.y : 0) +
                            -(!prefs_App()->hideToolbarOnScroll
                                  ? phoneToolbarHeight_DocumentWidget(d->owner) +
                                        phoneBottomNavbarHeight_DocumentWidget(d->owner) : 0) +
                            d->scrollY.max - pos_SmoothScroll(&d->scrollY))),
            tmQuoteIcon_ColorId);
    }
    unsetClip_Paint(&p);
}

static void drawMedia_DocumentView_(const iDocumentView *d, iPaint *p) {
    iConstForEach(PtrArray, i, &d->visibleMedia) {
        const iGmRun * run = i.ptr;
        if (run->mediaType == audio_MediaType) {
            iPlayerUI ui;
            init_PlayerUI(&ui,
                          audioPlayer_Media(media_GmDocument(d->doc), mediaId_GmRun(run)),
                          runRect_DocumentView(d, run));
            draw_PlayerUI(&ui, p);
        }
        else if (run->mediaType == download_MediaType) {
            iDownloadUI ui;
            init_DownloadUI(&ui, constMedia_GmDocument(d->doc), run->mediaId,
                            runRect_DocumentView(d, run));
            draw_DownloadUI(&ui, p);
        }
    }
}

static void extend_GmRunRange_(iGmRunRange *runs) {
    if (runs->start) {
        runs->start--;
        runs->end++;
    }
}

static iBool render_DocumentView_(const iDocumentView *d, iDrawContext *ctx, iBool prerenderExtra) {
    iBool       didDraw = iFalse;
    const iRect bounds  = bounds_Widget(constAs_Widget(d->owner));
    const iRect ctxWidgetBounds =
        init_Rect(0,
                  0,
                  width_Rect(bounds) - constAs_Widget(scrollBar_DocumentWidget(d->owner))->rect.size.x,
                  height_Rect(bounds));
    const iRangei full          = { 0, size_GmDocument(d->doc).y };
    const iRangei vis           = ctx->vis;
    iVisBuf      *visBuf        = d->visBuf; /* will be updated now */
    if (isEmpty_Range(&full)) {
        return didDraw;
    }
    d->drawBufs->lastRenderTime = SDL_GetTicks();
    /* Swap buffers around to have room available both before and after the visible region. */
    allocVisBuffer_DocumentView(d);
    reposition_VisBuf(visBuf, vis);
    /* Redraw the invalid ranges. */
    if (~flags_Widget(constAs_Widget(d->owner)) & destroyPending_WidgetFlag) {
        iPaint *p = &ctx->paint;
        init_Paint(p);
        iForIndices(i, visBuf->buffers) {
            iVisBufTexture *buf         = &visBuf->buffers[i];
            iVisBufMeta    *meta        = buf->user;
            const iRangei   bufRange    = intersect_Rangei(bufferRange_VisBuf(visBuf, i), full);
            const iRangei   bufVisRange = intersect_Rangei(bufRange, vis);
            ctx->widgetBounds = moved_Rect(ctxWidgetBounds, init_I2(0, -buf->origin));
            ctx->viewPos      = init_I2(left_Rect(ctx->docBounds) - left_Rect(bounds), -buf->origin);
            //            printf("  buffer %zu: buf vis range %d...%d\n", i, bufVisRange.start, bufVisRange.end);
            if (!prerenderExtra && !isEmpty_Range(&bufVisRange)) {
                if (isEmpty_Rangei(buf->validRange)) {
                    /* Fill the required currently visible range (vis). */
                    const iRangei bufVisRange = intersect_Rangei(bufRange, vis);
                    if (!isEmpty_Range(&bufVisRange)) {
                        beginTarget_Paint(p, buf->texture);
                        fillRect_Paint(p, (iRect){ zero_I2(), visBuf->texSize }, tmBackground_ColorId);
                        iZap(ctx->runsDrawn);
                        render_GmDocument(d->doc, bufVisRange, drawRun_DrawContext_, ctx);
                        meta->runsDrawn = ctx->runsDrawn;
                        extend_GmRunRange_(&meta->runsDrawn);
                        buf->validRange = bufVisRange;
                        //                printf("  buffer %zu valid %d...%d\n", i, bufRange.start, bufRange.end);
                        didDraw = iTrue;
                    }
                }
                else {
                    /* Progressively fill the required runs. */
                    if (meta->runsDrawn.start && buf->validRange.start > bufRange.start) {
                        beginTarget_Paint(p, buf->texture);
                        iZap(ctx->runsDrawn);
                        ctx->drawDir = -1;
                        const iGmRun *newStart = renderProgressive_GmDocument(d->doc,
                                                                              meta->runsDrawn.start,
                                                                              ctx->drawDir,
                                                                              iInvalidSize,
                                                                              bufVisRange,
                                                                              drawRun_DrawContext_,
                                                                              ctx);
                        if (ctx->runsDrawn.start) {
                            /* Something was actually drawn, so update the valid range. */
                            const int newTop = top_Rect(ctx->runsDrawn.start->visBounds);
                            if (newTop != buf->validRange.start) {
                                didDraw = iTrue;
                                //                                printf("render: valid:%d->%d run:%p->%p\n",
                                //                                       buf->validRange.start, newTop,
                                //                                       meta->runsDrawn.start,
                                //                                       ctx->runsDrawn.start); fflush(stdout);
                                buf->validRange.start = newTop;
                            }
                            meta->runsDrawn.start = newStart;
                        }
                    }
                    if (meta->runsDrawn.end) {
                        beginTarget_Paint(p, buf->texture);
                        iZap(ctx->runsDrawn);
                        ctx->drawDir = +1;
                        meta->runsDrawn.end = renderProgressive_GmDocument(d->doc, meta->runsDrawn.end,
                                                                           ctx->drawDir, iInvalidSize,
                                                                           bufVisRange,
                                                                           drawRun_DrawContext_,
                                                                           ctx);
                        if (ctx->runsDrawn.start) {
                            didDraw = iTrue;
                        }
                        buf->validRange.end = bufVisRange.end;
                    }
                }
            }
            /* Progressively draw the rest of the buffer if it isn't fully valid. */
            if (prerenderExtra && !equal_Rangei(bufRange, buf->validRange)) {
                const iGmRun *next;
                //                printf("%zu: prerenderExtra (start:%p end:%p)\n", i, meta->runsDrawn.start, meta->runsDrawn.end);
                if (meta->runsDrawn.start == NULL) {
                    /* Haven't drawn anything yet in this buffer, so let's try seeding it. */
                    const int rh = lineHeight_Text(paragraph_FontId);
                    const int y = i >= iElemCount(visBuf->buffers) / 2 ? bufRange.start : (bufRange.end - rh);
                    beginTarget_Paint(p, buf->texture);
                    fillRect_Paint(p, (iRect){ zero_I2(), visBuf->texSize }, tmBackground_ColorId);
                    buf->validRange = (iRangei){ y, y + rh };
                    iZap(ctx->runsDrawn);
                    render_GmDocument(d->doc, buf->validRange, drawRun_DrawContext_, ctx);
                    meta->runsDrawn = ctx->runsDrawn;
                    extend_GmRunRange_(&meta->runsDrawn);
                    //                    printf("%zu: seeded, next %p:%p\n", i, meta->runsDrawn.start, meta->runsDrawn.end);
                    didDraw = iTrue;
                }
                else {
                    if (meta->runsDrawn.start) {
                        const iRangei upper = intersect_Rangei(bufRange, (iRangei){ full.start, buf->validRange.start });
                        if (upper.end > upper.start) {
                            beginTarget_Paint(p, buf->texture);
                            ctx->drawDir = -1;
                            next = renderProgressive_GmDocument(d->doc, meta->runsDrawn.start,
                                                                ctx->drawDir, 1, upper,
                                                                drawRun_DrawContext_,
                                                                ctx);
                            if (next && meta->runsDrawn.start != next) {
                                meta->runsDrawn.start = next;
                                buf->validRange.start = bottom_Rect(next->visBounds);
                                didDraw = iTrue;
                            }
                            else {
                                buf->validRange.start = bufRange.start;
                            }
                        }
                    }
                    if (!didDraw && meta->runsDrawn.end) {
                        const iRangei lower = intersect_Rangei(bufRange, (iRangei){ buf->validRange.end, full.end });
                        if (lower.end > lower.start) {
                            beginTarget_Paint(p, buf->texture);
                            ctx->drawDir = +1;
                            next = renderProgressive_GmDocument(d->doc, meta->runsDrawn.end,
                                                                ctx->drawDir, 1, lower,
                                                                drawRun_DrawContext_,
                                                                ctx);
                            if (next && meta->runsDrawn.end != next) {
                                meta->runsDrawn.end = next;
                                buf->validRange.end = top_Rect(next->visBounds);
                                didDraw = iTrue;
                            }
                            else {
                                buf->validRange.end = bufRange.end;
                            }
                        }
                    }
                }
            }
            /* Draw any invalidated runs that fall within this buffer. */
            if (!prerenderExtra) {
                const iRangei bufRange = { buf->origin, buf->origin + visBuf->texSize.y };
                /* Clear full-width backgrounds first in case there are any dynamic elements. */ {
                    iConstForEach(PtrSet, r, d->invalidRuns) {
                        const iGmRun *run = *r.value;
                        if (isOverlapping_Rangei(bufRange, ySpan_Rect(run->visBounds))) {
                            beginTarget_Paint(p, buf->texture);
                            fillRect_Paint(p,
                                           moved_Rect(run->visBounds, init_I2(0, -buf->origin)),/*
                                           init_Rect(0,
                                                     run->visBounds.pos.y - buf->origin,
                                                     visBuf->texSize.x,
                                                     run->visBounds.size.y),*/
                                           tmBackground_ColorId);
                        }
                    }
                }
                setAnsiFlags_Text(ansiEscapes_GmDocument(d->doc));
                iConstForEach(PtrSet, r, d->invalidRuns) {
                    const iGmRun *run = *r.value;
                    if (isOverlapping_Rangei(bufRange, ySpan_Rect(run->visBounds))) {
                        beginTarget_Paint(p, buf->texture);
                        drawRun_DrawContext_(ctx, run);
                    }
                }
                setAnsiFlags_Text(allowAll_AnsiFlag);
            }
            endTarget_Paint(p);
            if (prerenderExtra && didDraw) {
                /* Just a run at a time. */
                break;
            }
        }
        if (!prerenderExtra) {
            clear_PtrSet(d->invalidRuns);
        }
    }
    return didDraw;
}

uint32_t lastRenderTime_DocumentView(const iDocumentView *d) {
    return d->drawBufs->lastRenderTime;
}

void prerender_DocumentView(iAny *context) {
    //iAssert(isInstance_Object(context, &Class_DocumentWidget));
    iDocumentView *d = context;
    if (current_Root() == NULL) {
        /* The widget has probably been removed from the widget tree, pending destruction.
           Tickers are not cancelled until the widget is actually destroyed. */
        return;
    }
    //const iDocumentWidget *d = context;
    iDrawContext ctx = {
        .view            = d,
        .docBounds       = documentBounds_DocumentView(d),
        .vis             = visibleRange_DocumentView(d),
        .showLinkNumbers = isShowingLinkNumbers_DocumentWidget(d->owner),
        .drawDir         = +1,
    };
    //    printf("%u prerendering\n", SDL_GetTicks());
    if (isPrerenderingAllowed_DocumentWidget(d->owner)) {
        makePaletteGlobal_GmDocument(d->doc);
        if (render_DocumentView_(d, &ctx, iTrue /* just fill up progressively */)) {
            /* Something was drawn, should check later if there is still more to do. */
            addTicker_App(prerender_DocumentView, d);
        }
    }
}

iBool isCoveringTopSafeArea_DocumentView(const iDocumentView *d) {
    return isMobile_Platform() &&
           prefs_App()->bottomNavBar &&
           (isPortraitPhone_App() || (deviceType_App() == tablet_AppDeviceType &&
                                      prefs_App()->bottomTabBar));
}

void draw_DocumentView(const iDocumentView *d, int horizOffset) {
    const iWidget *w          = constAs_Widget(d->owner);
    const iRect    bounds     = bounds_Widget(w);
    const iRect    clipBounds = bounds;
    /* Each document has its own palette, but the drawing routines rely on a global one.
       As we're now drawing a document, ensure that the right palette is in effect.
       Document theme colors can be used elsewhere, too, but first a document's palette
       must be made global. */
    makePaletteGlobal_GmDocument(d->doc);
    if (d->drawBufs->flags & updateTimestampBuf_DrawBufsFlag) {
        updateTimestampBuf_DocumentView_(d);
    }
    if (d->drawBufs->flags & updateSideBuf_DrawBufsFlag) {
        updateSideIconBuf_DocumentView_(d);
    }
    const iRect   docBounds = documentBounds_DocumentView(d);
    const iRangei vis       = visibleRange_DocumentView(d);
    iDrawContext  ctx       = { .widgetFullWidth = width_Rect(bounds),
                                .view            = d,
                                .docBounds       = docBounds,
                                .vis             = vis,
                                .showLinkNumbers = isShowingLinkNumbers_DocumentWidget(d->owner),
                                .drawDir         = +1,
                              };
    init_Paint(&ctx.paint);
    render_DocumentView_(d, &ctx, iFalse /* just the mandatory parts */);
    iBanner    *banner           = d->banner;
    int         yTop             = docBounds.pos.y + viewPos_DocumentView(d);
    const iBool isDocEmpty       = size_GmDocument(d->doc).y == 0;
    const iBool isTouchSelecting = (flags_Widget(w) & touchDrag_WidgetFlag) != 0;
    iBool       didDraw          = iFalse;
    if (!isDocEmpty || !isEmpty_Banner(banner)) {
        didDraw = iTrue;
        const int docBgColor = isDocEmpty ? tmBannerBackground_ColorId : tmBackground_ColorId;
        setClip_Paint(&ctx.paint, clipBounds);
        iAssert(isEqual_I2(origin_Paint, zero_I2()));
        origin_Paint = init_I2(horizOffset, 0);
        if (!isDocEmpty) {
            draw_VisBuf(d->visBuf, init_I2(bounds.pos.x, yTop), ySpan_Rect(bounds));
        }
        /* Text markers. */
        if ((!isEmpty_Range(d->foundMark) || !isEmpty_Range(d->selectMark))) {
            SDL_Renderer *render = renderer_Window(get_Window());
            ctx.firstMarkRect = zero_Rect();
            ctx.lastMarkRect = zero_Rect();
            SDL_SetRenderDrawBlendMode(render,
                                       isDark_ColorTheme(colorTheme_App()) ? SDL_BLENDMODE_ADD
                                                                           : SDL_BLENDMODE_BLEND);
            ctx.viewPos = topLeft_Rect(docBounds);
            /* Marker starting outside the visible range? */
            if (d->visibleRuns.start) {
                if (!isEmpty_Range(d->selectMark) &&
                    d->selectMark->start < d->visibleRuns.start->text.start &&
                    d->selectMark->end > d->visibleRuns.start->text.start) {
                    ctx.inSelectMark = iTrue;
                }
                if (isEmpty_Range(d->foundMark) &&
                    d->foundMark->start < d->visibleRuns.start->text.start &&
                    d->foundMark->end > d->visibleRuns.start->text.start) {
                    ctx.inFoundMark = iTrue;
                }
            }
            render_GmDocument(d->doc, vis, drawMark_DrawContext_, &ctx);
            SDL_SetRenderDrawBlendMode(render, SDL_BLENDMODE_NONE);
            /* Selection range pins. */
            if (isTouchSelecting) {
                drawPin_Paint(&ctx.paint, ctx.firstMarkRect, 0, tmQuote_ColorId);
                drawPin_Paint(&ctx.paint, ctx.lastMarkRect,  1, tmQuote_ColorId);
            }
        }
        drawMedia_DocumentView_(d, &ctx.paint);
        /* Fill the top and bottom, in case the document is short. */
        if (yTop > top_Rect(bounds)) {
            fillRect_Paint(&ctx.paint,
                           (iRect){ bounds.pos, init_I2(bounds.size.x, yTop - top_Rect(bounds)) },
                           !isEmpty_Banner(banner) ? tmBannerBackground_ColorId
                                                   : docBgColor);
        }
        /* Banner. */
        if (!isDocEmpty || numItems_Banner(banner) > 0) {
            /* Fill the part between the banner and the top of the document. */
            if (documentTopPad_DocumentView(d) > 0) {
                fillRect_Paint(&ctx.paint,
                               (iRect){ init_I2(left_Rect(bounds),
                                               top_Rect(docBounds) + viewPos_DocumentView(d) -
                                                   documentTopPad_DocumentView(d)),
                                       init_I2(bounds.size.x, documentTopPad_DocumentView(d)) },
                               docBgColor);
            }
            setPos_Banner(banner, addY_I2(topLeft_Rect(docBounds),
                                          floorf(-pos_SmoothScroll(&d->scrollY))));
            draw_Banner(banner);
        }
        const int yBottom = yTop + size_GmDocument(d->doc).y;
        if (yBottom < bottom_Rect(bounds)) {
            fillRect_Paint(&ctx.paint,
                           init_Rect(bounds.pos.x, yBottom, bounds.size.x, bottom_Rect(bounds) - yBottom),
                           !isDocEmpty ? docBgColor : tmBannerBackground_ColorId);
        }
        origin_Paint = zero_I2();
        unsetClip_Paint(&ctx.paint);
        drawSideElements_DocumentView_(d, horizOffset);
        /* Alt text. */
        const float altTextOpacity = value_Anim(&d->altTextOpacity) * 6 - 5;
        if (d->hoverAltPre && altTextOpacity > 0) {
            const iGmPreMeta *meta = preMeta_GmDocument(d->doc, preId_GmRun(d->hoverAltPre));
            if (meta->flags & topLeft_GmPreMetaFlag && ~meta->flags & decoration_GmRunFlag &&
                !isEmpty_Range(&meta->altText)) {
                const int   margin   = 3 * gap_UI / 2;
                const int   altFont  = uiLabel_FontId;
                const int   wrap     = docBounds.size.x - 2 * margin;
                iInt2 pos            = add_I2(add_I2(docBounds.pos, meta->pixelRect.pos),
                                       init_I2(horizOffset, viewPos_DocumentView(d)));
                const iInt2 textSize = measureWrapRange_Text(altFont, wrap, meta->altText).bounds.size;
                pos.y -= textSize.y + gap_UI;
                pos.y               = iMax(pos.y, top_Rect(bounds));
                const iRect altRect = { pos, init_I2(docBounds.size.x, textSize.y) };
                ctx.paint.alpha     = altTextOpacity * 255;
                if (altTextOpacity < 1) {
                    SDL_SetRenderDrawBlendMode(renderer_Window(get_Window()), SDL_BLENDMODE_BLEND);
                }
                fillRect_Paint(&ctx.paint, altRect, tmBackgroundAltText_ColorId);
                drawRect_Paint(&ctx.paint, altRect, tmFrameAltText_ColorId);
                setOpacity_Text(altTextOpacity);
                drawWrapRange_Text(altFont, addX_I2(pos, margin), wrap,
                                   tmQuote_ColorId, meta->altText);
                SDL_SetRenderDrawBlendMode(renderer_Window(get_Window()), SDL_BLENDMODE_NONE);
                setOpacity_Text(1.0f);
            }
        }
        /* Touch selection indicator. */
        if (isTouchSelecting) {
            iRect rect = { topLeft_Rect(bounds),
                          init_I2(width_Rect(bounds), lineHeight_Text(uiLabelBold_FontId)) };
            fillRect_Paint(&ctx.paint, rect, uiTextAction_ColorId);
            const iRangecc mark = selectionMark_DocumentWidget(d->owner);
            drawCentered_Text(uiLabelBold_FontId,
                              rect,
                              iFalse,
                              uiBackground_ColorId,
                              "%zu bytes selected", /* TODO: i18n */
                              size_Range(&mark));
        }
    }
    else {
        iRect boundsWithOffset = moved_Rect(bounds, init_I2(horizOffset, 0));
        fillRect_Paint(&ctx.paint, intersect_Rect(boundsWithOffset, clipBounds), uiBackground_ColorId);
        if (isBlank_DocumentWidget(d->owner)) {
            drawLogo_MainWindow(get_MainWindow(), boundsWithOffset);
        }
    }
    /* Fill the top safe area above the view, if there is one. */
    if (isCoveringTopSafeArea_DocumentView(d)) {
        if (topSafeInset_Mobile() > 0) {
            const iRect topSafeArea = initCorners_Rect(zero_I2(),
                                                       topRight_Rect(safeRect_Root(w->root)));
            fillRect_Paint(&ctx.paint,
                           moved_Rect(topSafeArea, init_I2(horizOffset, 0)),
                           !didDraw ? uiBackground_ColorId :
                           !isEmpty_Banner(d->banner) && docBounds.pos.y + viewPos_DocumentView(d) -
                                documentTopPad_DocumentView(d) > bounds.pos.y ?
                           tmBannerBackground_ColorId : tmBackground_ColorId);
        }
    }
}

void resetScrollPosition_DocumentView(iDocumentView *d, float normScrollY) {
    resetScroll_DocumentView(d);
    init_Anim(&d->scrollY.pos, normScrollY * pageHeight_DocumentView(d));
    updateVisible_DocumentView(d);
    clampScroll_DocumentView(d);
    updateSideOpacity_DocumentView(d, iFalse);
    updateDrawBufs_DocumentView(d, updateTimestampBuf_DrawBufsFlag | updateSideBuf_DrawBufsFlag);
}
