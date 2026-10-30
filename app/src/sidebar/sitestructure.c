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

#include "app.h"
#include "bookmarks.h"
#include "gmdocument.h"
#include "gmutil.h"
#include "ui/command.h"
#include "ui/documentwidget.h"
#include "ui/util.h"

#include <lagrange/visited.h>

static iBool hasUrlPrefix_Bookmark_(void *context, const iBookmark *bm) {
    return startsWith_String(&bm->url, cstr_String(context));
}

#if 0
int cmpGopherStructureUrl_(const iString *a, const iString *b) {
    /* All of the URLs in the structure tree have the same scheme, host, and port. */
    const size_t start = indexOfCStrFrom_String(a, "/", 9) + 1; /* skip the "gopher://" */
    iAssert(start < size_String(a));
    iAssert(start < size_String(b));
    const int      kind1 = at_Block(&a->chars, start);
    const int      kind2 = at_Block(&b->chars, start);
    const iRangecc sel1  = rangeFrom_String(a, start + 1);
    const iRangecc sel2  = rangeFrom_String(b, start + 1);
    const int      cmp   = iCmpStr(sel1.start, sel2.start);
    if (!cmp) return kind1 - kind2;
    return cmp;
}
#endif

static iBool isGopherStructure_SidebarWidget_(const iSidebarWidget *d) {
    /* Note: case-sensitive, unlike scheme comparisons elsewhere. */
    const iRangecc scheme = urlScheme_String(&d->structureHost);
    return equal_Rangecc(scheme, "gopher") || equal_Rangecc(scheme, "gophers");
}

static void removeStructureUnfold_SidebarWidget_(iSidebarWidget *d, const iString *url) {
    iForEach(Array, i, &d->structureUnfolds->strings.values) {
        iString *str = i.value;
        if (startsWith_String(str, cstr_String(url))) {
            deinit_String(str);
            remove_ArrayIterator(&i);
        }
    }
}

static iBool isUnfoldedStructurePath_SidebarWidget_(const iSidebarWidget *d, const iString *url,
                                                    const iString *activeDocumentUrl) {
    iUrl parts;
    init_Url(&parts, url);
    iRangecc path = parts.path;
    if (endsWith_Rangecc(path, "/") && isEmpty_Range(&parts.query)) path.end--;
    /* We need to know the parent directory. */
    iString sub;
    init_String(&sub);
    /* We have to check that all the parent directories are also unfolded,
    or otherwise a nested child item might be visible under a folded parent. */
    for (;;) {
        const size_t slash = lastIndexOfCStr_Rangecc(path, "/");
        if (slash == iInvalidPos || slash == 0) {
            /* The full hierarchy has been checked, and nothing seems to be folded. */
            deinit_String(&sub);
            return iTrue;
        }
        path.end = path.start + slash + 1; /* keep the slash */
        setRange_String(&sub, (iRangecc) { constBegin_String(url), path.end });
        if (startsWith_String(activeDocumentUrl, cstr_String(&sub))) {
            /* Everything upwards from current document should be unfolded temporarily. */
            deinit_String(&sub);
            return iTrue;
        }
        if (!contains_StringSet(d->structureUnfolds, &sub)) {
            break;
        }
        path.end--; /* lose the slash */
    }
    deinit_String(&sub);
    return iFalse;
}

static iSidebarItem *addStructureItem_SidebarWidget_(iSidebarWidget *d, const iString *url,
                                                     const iString *label, iArray *stack,
                                                     const iString *activeDocumentUrl,
                                                     size_t        *highlightedItemPos_out) {
    const int itemIndent = (int) size_Array(stack) + 1;
    if (itemIndent == 1 || isUnfoldedStructurePath_SidebarWidget_(d, url, activeDocumentUrl)) {
        iSidebarItem *previous = backItem_ListWidget(d->list);
        if (previous->indent == itemIndent - 1) {
            /* This must be the parent item. */
            previous->id |= 2; /* show as open folder */
        }
        iSidebarItem *item = new_SidebarItem();
        set_String(&item->url, url);
        iString *dec = maybeUrlDecodeExclude_String(label, "");
        if (dec) {
            set_String(&item->label, dec);
            delete_String(dec);
        }
        else {
            set_String(&item->label, label);
        }
        item->indent = itemIndent;
        if (equal_String(&item->url, activeDocumentUrl)) {
            *highlightedItemPos_out = numItems_ListWidget(d->list);
            item->id                = iTrue; /* makes it appear highlighted */
        }
        addItem_ListWidget(d->list, item);
        iRelease(item);
        return item;
    }
    else {
        iSidebarItem *item = backItem_ListWidget(d->list);
        item->count++; /* counts as a folded child */
        return NULL;
    }
}

iBool updateStructure_SidebarWidget_(iSidebarWidget *d, iBool keepActions) {
    if (!isVisible_Widget(as_Widget(d))) {
        /* This is a relatively heavy operation, so don't update unless we are
           looking at the structure tab. */
        return iTrue;
    }
    const iString *docUrl =
        urlDefaultPortStripped_String(url_DocumentWidget(document_App()));
    /* We look for the protocol in addition to the domain. */
    const iRangecc urlHost = urlHostWithPort_String(docUrl);
    if (isEmpty_Range(&urlHost)) {
        return iFalse;
    }
    const iRangecc host    = { constBegin_String(docUrl), urlHost.end };
    iString       *hostStr = collect_String(newRange_String(host));
    if (!endsWith_String(hostStr, "/")) {
        appendCStr_String(hostStr, "/");
    }
    if (isEmpty_Range(&host)) return iFalse;
    /* We keep the set of known URLs as long as the host remains the same.
       This allows accumulating entries from unvisited links on pages. */
    if (!equal_String(&d->structureHost, hostStr)) {
        set_String(&d->structureHost, hostStr);
        iRelease(d->structureUrls);
        d->structureUrls = new_StringSet();
        clear_StringSet(d->structureUnfolds);
        scrollOffset_ListWidget(d->list, 0);
    }
    const iBool isGopher = isGopherStructure_SidebarWidget_(d);
    iStringSet *urls     = d->structureUrls;
    /* Look through everything we know at the moment: visited URLs, bookmarks,
       feed entries, and links on the page. */
    iConstForEach(PtrArray, i, listMatching_Visited(visited_App(), cstr_String(hostStr))) {
        const iVisitedUrl *vis = i.ptr;
        insert_StringSet(urls, &vis->url);
    }
    const iPtrArray *bookmarks =
        list_Bookmarks(bookmarks_App(), NULL, hasUrlPrefix_Bookmark_, hostStr);
    for (init_PtrArrayConstIterator(&i, bookmarks); i.value;
         next_PtrArrayConstIterator(&i)) {
        insert_StringSet(urls, &((const iBookmark *) i.ptr)->url);
    }
    const iGmDocument *doc = document_DocumentWidget(document_App());
    for (size_t j = 0; j < numLinks_GmDocument(doc); j++) {
        iBeginCollect();
        const iString *linkUrl = urlDefaultPortStripped_String(linkUrl_GmDocument(doc, j));
        if (startsWith_String(linkUrl, cstr_String(hostStr))) {
            insert_StringSet(urls, linkUrl);
        }
        iEndCollect();
    }
    /* The root item is always present. */
    iSidebarItem *rootItem = new_SidebarItem();
    setRange_String(&rootItem->url, host);
    setRange_String(&rootItem->label, urlHost_String(docUrl));
    rootItem->isBold = iTrue;
    rootItem->id     = equal_String(&rootItem->url, docUrl);
    rootItem->count  = 1;
    size_t docItem   = rootItem->id ? 0 : iInvalidPos;
    addItem_ListWidget(d->list, rootItem);
    iRelease(rootItem);
    iString label;
    init_String(&label);
    /* Use a stack to keep track of the directory containment structure. */
    struct Impl_Level {
        iRangecc prefix;
    };
    iDeclareType(Level);
    iArray stack;
    init_Array(&stack, sizeof(iLevel));
    iConstForEach(StringSet, u, urls) {
        const iString *url = u.value;
        /* Compare the structure of this URL compared to the current stack. */
        iRangecc itemDir = urlPath_String(url);
        size_t dirEnd = lastIndexOfCStr_Rangecc(itemDir, "/");
        if (dirEnd == iInvalidPos) continue; /* must be root */
        itemDir.end = itemDir.start + dirEnd;
        if (dirEnd > 0 && isEmpty_Range(&itemDir)) {
            continue; /* must be root */
        }
        size_t   i      = 0;
        iRangecc seg    = iNullRange;
        iRangecc subDir = iNullRange;
        while (nextSplit_Rangecc(itemDir, "/", &seg)) {
            subDir = (iRangecc) { itemDir.start, seg.end };
            if (i < size_Array(&stack) &&
                !equalRange_Rangecc(subDir, constValue_Array(&stack, i, iLevel).prefix)) {
                /* Only the parts before this are the same. */
                resize_Array(&stack, i);
            }
            if (i >= size_Array(&stack)) {
                /* The itemDir has more components than the stack. */
                iSidebarItem *previousItem   = backItem_ListWidget(d->list);
                iRangecc      parentUrlRange = { constBegin_String(url), subDir.end };
                if (!equalRange_Rangecc(range_String(&previousItem->url), parentUrlRange)) {
                    iString parentUrl;
                    parentUrlRange.end++; /* include the slash */
                    initRange_String(&parentUrl, parentUrlRange);
                    setRange_String(&label, seg);
                    iSidebarItem *parent = addStructureItem_SidebarWidget_(
                        d, &parentUrl, &label, &stack, docUrl, &docItem);
                    deinit_String(&parentUrl);
                    if (parent) {
                        appendCStr_String(&parent->label, "/");
                        parent->isBold = (parent->indent == 1);
                    }
                }
                pushBack_Array(&stack, &(iLevel) { subDir });
            }
            i++;
        }
        if (i < size_Array(&stack)) {
            /* The prefix has become shorter. */
            resize_Array(&stack, i);
        }
        setRange_String(&label, (iRangecc) { itemDir.end + 1, constEnd_String(url) });
        if (isEmpty_String(&label) || !cmp_String(&label, "/")) {
            /* The directory item was already created above. */
            continue;
        }
        addStructureItem_SidebarWidget_(
            d, url, &label, &stack, docUrl, &docItem);
    }
    if (docItem != iInvalidPos && !keepActions) {
        postCommand_Widget(d->list, "sideitem.show arg:%u", docItem);
    }
    deinit_Array(&stack);
    deinit_String(&label);
    const iBool isEmpty = numItems_ListWidget(d->list) <= 1; /* just the root */
    /* Context menu. */
    const iMenuItem menuItems[] = {
        { "${menu.fold}", 0, 0, "structure.fold" },
        { "---" },
        { openTab_Icon " ${menu.opentab}", 0, 0, "sideitem.open newtab:1" },
        { openTabBg_Icon " ${menu.opentab.background}", 0, 0, "sideitem.open newtab:2" },
        { openWindow_Icon " ${menu.openwindow}", 0, KMOD_NEWWINDOW, "sideitem.open newwindow:1" },
        { "---" },
        { "${menu.copyurl}", 0, 0, "sideitem.copy" },
    };
    d->menu       = makeMenu_Widget(as_Widget(d), menuItems + 2, iElemCount(menuItems) - 2);
    d->folderMenu = makeMenu_Widget(as_Widget(d), menuItems, iElemCount(menuItems));
    return !isEmpty;
}

iBool handleStructureCommand_SidebarWidget_(iSidebarWidget *d, const SDL_Event *ev, const char *cmd) {
    iWidget *w = as_Widget(d);
    if (d->mode == siteStructure_SidebarMode && isCommand_Widget(w, ev, "structure.fold")) {
        const iSidebarItem *item = d->contextItem;
        if (item) {
            removeStructureUnfold_SidebarWidget_(d, &item->url);
            updateItemsWithFlags_SidebarWidget_(d, iTrue);
        }
        return iTrue;
    }
    return iFalse;
}
