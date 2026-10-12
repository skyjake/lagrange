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

#include "scopedprefs.h"

#include "app.h"
#include "command.h"
#include "defs.h"
#include "dialog.h"
#include "fontpack.h"
#include "inputwidget.h"
#include "labelwidget.h"
#include "mobile.h"
#include "prefsdialog.h"
#include "sliderwidget.h"
#include "tabs.h"
#include "util.h"
#include "widget.h"

#include <lagrange/lang.h>
#include <math.h>

/*----------------------------------------------------------------------------------------------*/

static iBool proportionalFonts_(const iFontSpec *spec) {
    return (spec->flags & monospace_FontSpecFlag) == 0 && ~spec->flags & auxiliary_FontSpecFlag;
}

static iBool monospaceFonts_(const iFontSpec *spec) {
    return (spec->flags & monospace_FontSpecFlag) != 0 && ~spec->flags & auxiliary_FontSpecFlag;
}

/* Copy up to the first `numWords` space-delimited words of `name` into `out`. */
static void extractFamilyPrefix_(const char *name, char *out, size_t outSize, int numWords) {
    const char *p = name;
    int words = 0;
    const char *underline = strchr(name, '_');
    if (underline) {
        size_t len = iMin(underline - name, outSize - 1);
        memcpy(out, name, len);
        out[len] = '\0';
        return;
    }
    while (*p) {
        if (*p == ' '|| *p == '_') {
            words++;
            if (words == numWords) break;
        }
        p++;
    }
    size_t len = iMin((size_t)(p - name), outSize - 1);
    memcpy(out, name, len);
    out[len] = '\0';
}

/* `name` starts with `prefix` at a word boundary? (space or end of string) */
static iBool hasFamilyPrefix_(const char *name, const char *prefix, size_t prefixLen) {
    if (iCmpStrN(name, prefix, prefixLen) != 0) return iFalse;
    const char c = name[prefixLen];
    return c == '\0' || c == ' ' || c == '_';
}

static void appendGroupedFontItems_(iArray *items, const iPtrArray *specs, const char *id) {
    const size_t n = size_PtrArray(specs);
    char prefix[128];
    for (size_t i = 0; i < n;) {
        const iFontSpec *spec = constAt_PtrArray(specs, i);
        extractFamilyPrefix_(cstr_String(&spec->name), prefix, sizeof(prefix), 2);
        const size_t prefixLen = strlen(prefix);
        /* Count how many consecutive fonts share this word prefix. */
        size_t groupEnd = i + 1;
        while (groupEnd < n) {
            const iFontSpec *s = constAt_PtrArray(specs, groupEnd);
            if (!hasFamilyPrefix_(cstr_String(&s->name), prefix, prefixLen)) break;
            groupEnd++;
        }
        const size_t groupSize = groupEnd - i;
        if (groupSize >= 3) {
            /* Emit a submenu header followed by the group's fonts. */
            pushBack_Array(items, &(iMenuItem) { format_CStr("---%s", prefix) });
            for (size_t j = i; j < groupEnd; j++) {
                const iFontSpec *s = constAt_PtrArray(specs, j);
                pushBack_Array(
                    items,
                    &(iMenuItem) { cstr_String(&s->name),
                                   0,
                                   0,
                                   format_CStr("!font.set %s:%s", id, cstr_String(&s->id)) });
            }
            pushBack_Array(items, &(iMenuItem) { "---:" }); /* terminate the group */
        }
        else {
            /* Emit the font(s) flat. */
            for (size_t j = i; j < groupEnd; j++) {
                const iFontSpec *s = constAt_PtrArray(specs, j);
                pushBack_Array(
                    items,
                    &(iMenuItem) { cstr_String(&s->name),
                                   0,
                                   0,
                                   format_CStr("!font.set %s:%s", id, cstr_String(&s->id)) });
            }
        }
        i = groupEnd;
    }
}

const iArray *fontItems_ScopedPrefs(const char *id) {
    iArray *items = collectNew_Array(sizeof(iMenuItem));
    if (!startsWith_CStr(id, "mono")) {
        appendGroupedFontItems_(items, listSpecs_Fonts(proportionalFonts_), id);
        pushBack_Array(items, &(iMenuItem){ "---" });
    }
    appendGroupedFontItems_(items, listSpecs_Fonts(monospaceFonts_), id);
    pushBack_Array(items, &(iMenuItem){ NULL });
    return items;
}

/*----------------------------------------------------------------------------------------------*/

/* Note: An entry is not necessarily a real URL scheme. */
static const iMenuItem protocolItems_[] = {
    { "${prefs.scope.all}", 0, 0, "prefs.scope.set scope:base" },
    { "Gemini", 0, 0, "prefs.scope.set scope:scheme:gemini" },
    { "Spartan", 0, 0, "prefs.scope.set scope:scheme:spartan" },
    { "Gopher", 0, 0, "prefs.scope.set scope:scheme:gopher" },
    { "Finger", 0, 0, "prefs.scope.set scope:scheme:finger" },
    { "Gempub", 0, 0, "prefs.scope.set scope:scheme:gempub" },
    { NULL }
};

/* Line width slider uses a relative scale where each unit is a factor of 1.12. */
static const float lineWidthFill_ = 3.0f;

static int lineWidthChars_(float rel) {
    return rel >= lineWidthFill_ ? 1000 : (int) lroundf(38 * powf(1.12f, rel));
}

static float lineWidthRel_(int chars) {
    return chars >= 1000 ? lineWidthFill_
                         : iMin(lineWidthFill_ - 0.5f, logf(chars / 38.0f) / logf(1.12f));
}

/* clang-format off */
static const iMenuItem lineWidthTicks_[] = {
    { "tick value:0 label:prefs.linewidth.normal" },
    { "tick value:3 label:prefs.linewidth.fill" },
    { NULL }
};
static const iMenuItem boldLinkItems_[] = {
    { "button id:prefs.boldlink.visited" },
    { "button id:prefs.boldlink.dark" },
    { "button id:prefs.boldlink.light" },
    { NULL }
};
static const iMenuItem ansiItems_[] = {
    { "button id:prefs.gemtext.ansi.fg" },
    { "button id:prefs.gemtext.ansi.bg" },
    { "button id:prefs.gemtext.ansi.fontstyle" },
    { NULL }
};
static const iMenuItem quoteItems_[] = {
    { "button id:prefs.quoteicon.1 label:prefs.quoteicon.icon", 0, 0, "quoteicon.set arg:1" },
    { "button id:prefs.quoteicon.0 label:prefs.quoteicon.line", 0, 0, "quoteicon.set arg:0" },
    { NULL }
};
/* clang-format on */

static const iMenuItem *collapseItems_(void) {
    iArray *items = collectNew_Array(sizeof(iMenuItem));
    pushBackN_Array(items, (iMenuItem[]){
        { "${collapse.never}", 0, 0, format_CStr("collapsepre.set arg:%d", never_Collapse) },
        { "${collapse.notbydefault}", 0, 0, format_CStr("collapsepre.set arg:%d", notByDefault_Collapse) },
        { "${collapse.bydefault}", 0, 0, format_CStr("collapsepre.set arg:%d", byDefault_Collapse) },
        { "${collapse.always}", 0, 0, format_CStr("collapsepre.set arg:%d", always_Collapse) },
        { NULL }
    }, 5);
    return constData_Array(items);
}

static const iMenuItem *justifyItems_(void) {
    iArray *items = collectNew_Array(sizeof(iMenuItem));
    pushBackN_Array(items, (iMenuItem[]){
        { "${justify.never}", 0, 0, format_CStr("justify.set arg:%d", never_Justify) },
        { "${justify.pagesonly}", 0, 0, format_CStr("justify.set arg:%d", pagesOnly_Justify) },
        { "${justify.always}", 0, 0, format_CStr("justify.set arg:%d", always_Justify) },
        { NULL }
    }, 4);
    return constData_Array(items);
}

static const iMenuItem *docThemeItems_(const char *cmd, iBool withNone) {
    static const struct {
        const char *label;
        enum iGmDocumentTheme theme;
    } themes[] = {
        { "${prefs.doctheme.name.colorfuldark}", colorfulDark_GmDocumentTheme },
        { "${prefs.doctheme.name.colorfullight}", colorfulLight_GmDocumentTheme },
        { "${prefs.doctheme.name.vibrantlight}", vibrantLight_GmDocumentTheme },
        { "${prefs.doctheme.name.black}", black_GmDocumentTheme },
        { "${prefs.doctheme.name.gray}", gray_GmDocumentTheme },
        { "${prefs.doctheme.name.white}", white_GmDocumentTheme },
        { "${prefs.doctheme.name.sepia}", sepia_GmDocumentTheme },
        { "${prefs.doctheme.name.oceanic}", oceanic_GmDocumentTheme },
        { "${prefs.doctheme.name.highcontrast}", highContrast_GmDocumentTheme },
    };
    iArray *items = collectNew_Array(sizeof(iMenuItem));
    if (withNone) {
        pushBack_Array(items,
                       &(iMenuItem){ "${prefs.doctheme.name.none}", 0, 0,
                                     format_CStr("%s.set arg:%d", cmd, none_GmDocumentTheme) });
    }
    iForIndices(i, themes) {
        pushBack_Array(items,
                       &(iMenuItem){ themes[i].label, 0, 0,
                                     format_CStr("%s.set arg:%d", cmd, themes[i].theme) });
    }
    pushBack_Array(items, &(iMenuItem){ NULL });
    return constData_Array(items);
}

static const iMenuItem *imageStyleItems_(void) {
    iArray *items = collectNew_Array(sizeof(iMenuItem));
    pushBackN_Array(items, (iMenuItem[]){
        { "${prefs.imagestyle.original}",  0, 0, format_CStr("imagestyle.set arg:%d", original_ImageStyle) },
        { "${prefs.imagestyle.grayscale}", 0, 0, format_CStr("imagestyle.set arg:%d", grayscale_ImageStyle) },
        { "${prefs.imagestyle.bgfg}",      0, 0, format_CStr("imagestyle.set arg:%d", bgFg_ImageStyle) },
        { "${prefs.imagestyle.text}",      0, 0, format_CStr("imagestyle.set arg:%d", textColorized_ImageStyle) },
        { "${prefs.imagestyle.preformat}", 0, 0, format_CStr("imagestyle.set arg:%d", preformatColorized_ImageStyle) },
        { NULL }
    }, 6);
    return constData_Array(items);
}

static const char *scopeBarId_(enum iDocPrefsGroup group) {
    return format_CStr("prefs.scope.%s", groupId_DocPrefs(group));
}

/* Dropdown contents are omitted when only the IDs of the controls are needed. */
static const iMenuItem *makeItems_(enum iDocPrefsGroup group, iBool withProtocols, iBool withMenus) {
    iArray *items = collectNew_Array(sizeof(iMenuItem));
#define menu_(expr)     (withMenus ? (const void *) (expr) : NULL)
#define add_(...)       pushBack_Array(items, &(iMenuItem){ __VA_ARGS__ })
    /* Finger is always unstyled plain text, so many of the settings do not apply to it. */
    add_(format_CStr("scopebar id:%s", scopeBarId_(group)), 0, 0,
         (const void *) (withProtocols ? protocolItems_ : NULL));
    switch (group) {
        case typography_DocPrefsGroup:
            if (!isTerminal_Platform()) {
                add_("dropdown id:prefs.font.heading except:finger", 0, 0, menu_(constData_Array(fontItems_ScopedPrefs("heading"))));
                add_("dropdown id:prefs.font.body except:finger", 0, 0, menu_(constData_Array(fontItems_ScopedPrefs("body"))));
                add_("dropdown id:prefs.font.mono except:finger", 0, 0, menu_(constData_Array(fontItems_ScopedPrefs("mono"))));
                add_("heading id:heading.prefs.monodoc");
                add_("dropdown id:prefs.font.monodoc", 0, 0, menu_(constData_Array(fontItems_ScopedPrefs("monodoc"))));
            }
            add_("toggle id:prefs.mono.doc except:finger");
            break;
        case layout_DocPrefsGroup:
            add_("toggle id:prefs.pageview");
            add_("padding");
            add_("slider id:prefs.linewidth cmd:linewidth.slider min:-2 max:3 step:0.5 bigstep:1 "
                 "prec:1 sign:1", 0, 0, (const void *) lineWidthTicks_);
            add_("slider id:prefs.linespacing cmd:linespacing.set min:0.95 max:2 step:0.01 "
                 "bigstep:0.1 prec:2");
            add_("slider id:prefs.tabwidth cmd:tabwidth.set min:1 max:16 step:1 bigstep:2");
            add_("toggle id:prefs.plaintext.wrap");
            add_("padding");
            add_("toggle id:prefs.expandline");
            add_("toggle id:prefs.centershort");
            add_("padding id:prefs.pad.collapsepre except:finger");
            add_("dropdown id:prefs.collapsepre except:finger", 0, 0, menu_(collapseItems_()));
            break;
        case style_DocPrefsGroup:
#if defined (LAGRANGE_ENABLE_HARFBUZZ) || defined (LAGRANGE_ENABLE_CORETEXT)
            add_("dropdown id:prefs.justify except:finger", 0, 0, menu_(justifyItems_()));
#endif
            add_("toggle id:prefs.biglede except:finger");
            add_("padding id:prefs.pad.quoteicon except:finger");
            /* On mobile, the heading also covers the italic toggle. */
            add_(isUsingPanelLayout_Mobile()
                     ? "radio horizontal:1 id:prefs.quoteicon except:finger text:${prefs.quotes}"
                     : "radio horizontal:1 id:prefs.quoteicon except:finger",
                 0, 0, (const void *) quoteItems_);
            add_("toggle id:prefs.quote.italic except:finger");
            add_("padding");
            add_("buttons id:prefs.boldlink except:finger", 0, 0, (const void *) boldLinkItems_);
            add_("padding id:prefs.pad.sideicon except:finger");
            add_("toggle id:prefs.sideicon");
            break;
        case theme_DocPrefsGroup:
            add_("dropdown id:prefs.doctheme.dark", 0, 0, menu_(docThemeItems_("doctheme.dark", iFalse)));
            add_("dropdown id:prefs.doctheme.light", 0, 0, menu_(docThemeItems_("doctheme.light", iFalse)));
            add_("padding");
            add_("dropdown id:prefs.pagetheme.dark", 0, 0, menu_(docThemeItems_("pagetheme.dark", iTrue)));
            add_("dropdown id:prefs.pagetheme.light", 0, 0, menu_(docThemeItems_("pagetheme.light", iTrue)));
            add_("padding");
            add_("slider id:prefs.saturation cmd:saturation.set min:0 max:100 step:10 bigstep:30 "
                 "unit:percent");
            add_("padding id:prefs.pad.imagestyle except:finger");
            add_("dropdown id:prefs.imagestyle except:finger", 0, 0, menu_(imageStyleItems_()));
            break;
        case content_DocPrefsGroup:
            add_("buttons id:prefs.gemtext.ansi except:finger", 0, 0, (const void *) ansiItems_);
            if (isUsingPanelLayout_Mobile()) {
                add_("padding");
            }
            add_("toggle id:prefs.markdown.viewsource except:finger");
            add_("toggle id:prefs.gopher.gemstyle scheme:gopher");
            break;
        default:
            break;
    }
    add_(NULL);
#undef add_
#undef menu_
    return constData_Array(items);
}

const iMenuItem *items_ScopedPrefs(enum iDocPrefsGroup group, iBool withProtocols) {
    return makeItems_(group, withProtocols, iTrue);
}

static iBool isContainer_MenuItem_(const iMenuItem *item) {
    return equal_Command(item->label, "radio") || equal_Command(item->label, "buttons");
}

static const struct {
    enum iDocPrefsGroup group;
    const char *icon;
    int         mobileIcon;
    const char *title;
    const char *mobileTitle; /* NULL if same as `title` */
    const char *pageId;
} pages_[] = {
    { typography_DocPrefsGroup, fonts_Icon, 0x1f5da, "heading.prefs.typography", NULL, "prefs.page.fonts" },
    { theme_DocPrefsGroup, palette_Icon, 0x1f3a8, "heading.prefs.theme", "heading.prefs.colors", "prefs.page.theme" },
    { style_DocPrefsGroup, pageStyle_Icon, 0x1f660, "heading.prefs.style", NULL, "prefs.page.style" },
    { layout_DocPrefsGroup, pageLayout_Icon, 0x1f4c4, "heading.prefs.layout", NULL, "prefs.page.layout" },
    /* Global in Preferences; can only be overridden for a site. */
    { content_DocPrefsGroup, photo_Icon, 0x1f5bc, "heading.prefs.content", NULL, "sitespec.page.content" },
};

static iBool isPageUsed_(size_t index, iBool withProtocols) {
    return !(withProtocols && pages_[index].group == content_DocPrefsGroup);
}

/*----------------------------------------------------------------------------------------------*/

/* A row at the top of a page. */
static void makeScopeBar_(iWidget *page, const iMenuItem *item) {
    const char *     id        = cstr_Command(item->label, "id");
    const iMenuItem *protocols = item->data;
    iWidget *        toggle    = makeToggle_Widget(format_CStr("%s.toggle", id));
    iLabelWidget *   label     = makeHeading_Widget("");
    iWidget *        bar       = new_Widget();
    const size_t     count     = count_MenuItem(protocols);
    setId_Widget(as_Widget(label), format_CStr("%s.label", id));
    iLabelWidget *drop = makeMenuButton_LabelWidget(
        protocols[findWidestLabel_MenuItem(protocols, count)].label, protocols, count);
    setBackgroundColor_Widget(findChild_Widget(as_Widget(drop), "menu"), uiBackgroundMenu_ColorId);
    setId_Widget(as_Widget(drop), id);
    setFixedSize_Widget(as_Widget(drop), init_I2(width_Widget(drop), -1));
    addChild_Widget(bar, iClob(makeHeading_Widget("${prefs.scope}")));
    addChildFlags_Widget(bar, iClob(drop), alignLeft_WidgetFlag);
    /* The toggle stays at the right edge regardless of the label. */
    addChildFlags_Widget(bar, iClob(new_Widget()), expand_WidgetFlag);
    addChild_Widget(bar, iClob(label));
    addChild_Widget(bar, iClob(toggle));
    addChildFlags_Widget(page,
                         iClob(bar),
                         arrangeHorizontal_WidgetFlag | arrangeHeight_WidgetFlag |
                             resizeToParentWidth_WidgetFlag | resizeWidthOfChildren_WidgetFlag);
}

/* Without protocols to choose from, the toggle is the first row of the page. */
static void makeScopeToggleRow_(iWidget *headings, iWidget *values, const iMenuItem *item) {
    const char *  id    = cstr_Command(item->label, "id");
    iLabelWidget *label = makeHeading_Widget("");
    setId_Widget(as_Widget(label), format_CStr("%s.label", id));
    addChild_Widget(headings, iClob(label));
    addChild_Widget(values, iClob(makeToggle_Widget(format_CStr("%s.toggle", id))));
    addDialogPadding_Widget(headings, values);
}

void appendTabPages_ScopedPrefs(iWidget *tabs, iBool withProtocols, int iconColor,
                                int firstShortcut) {
    iForIndices(i, pages_) {
        if (!isPageUsed_(i, withProtocols)) {
            continue;
        }
        const iMenuItem *items    = items_ScopedPrefs(pages_[i].group, withProtocols);
        const int        shortcut = firstShortcut ? firstShortcut + (int) i : 0;
        /* The scope bar is at the top and the rest is centered vertically. */
        iWidget *page = new_Widget();
        setId_Widget(page, pages_[i].pageId);
        setFlags_Widget(page, arrangeVertical_WidgetFlag | arrangeSize_WidgetFlag, iTrue);
        setPadding_Widget(page, 0, gap_UI, 0, gap_UI);
        if (withProtocols) {
            makeScopeBar_(page, &items[0]);
        }
        addChildFlags_Widget(page, iClob(new_Widget()), expand_WidgetFlag);
        iWidget *headings, *values;
        addChild_Widget(page, iClob(makeTwoColumns_Widget(&headings, &values)));
        addChildFlags_Widget(page, iClob(new_Widget()), expand_WidgetFlag);
        if (!withProtocols) {
            makeScopeToggleRow_(headings, values, &items[0]);
        }
        makeTwoColumnItems_Widget(headings, values, items);
        appendFramelessTabPage_Widget(tabs,
                                      iClob(page),
                                      format_CStr("%s ${%s}", pages_[i].icon, pages_[i].title),
                                      iconColor,
                                      shortcut,
                                      shortcut ? KMOD_PRIMARY : 0);
    }
}

void appendPanels_ScopedPrefs(iArray *panelItems, iBool withProtocols, int iconColor) {
    iForIndices(i, pages_) {
        if (!isPageUsed_(i, withProtocols)) {
            continue;
        }
        const char *title = pages_[i].mobileTitle ? pages_[i].mobileTitle : pages_[i].title;
        iArray     *items = collectNew_Array(sizeof(iMenuItem));
        pushBack_Array(items,
                       &(iMenuItem){ format_CStr("title id:%s text:${%s}", pages_[i].title, title) });
        pushBack_Array(items, &(iMenuItem){ "padding arg:0.667" });
        const iMenuItem *groupItems = items_ScopedPrefs(pages_[i].group, withProtocols);
        /* The scope bar is separate from the settings. */
        pushBack_Array(items, &groupItems[0]);
        pushBack_Array(items, &(iMenuItem){ "padding" });
        pushBackN_Array(items, groupItems + 1, count_MenuItem(groupItems + 1));
        pushBack_Array(items, &(iMenuItem){ "padding" });
        pushBack_Array(items, &(iMenuItem){ NULL });
        pushBack_Array(panelItems,
                       &(iMenuItem){ format_CStr("panel icon:0x%x iconcolor:%d id:%s text:${%s}",
                                                 pages_[i].mobileIcon,
                                                 iconColor,
                                                 pages_[i].title,
                                                 title),
                                     0, 0, (const void *) constData_Array(items) });
    }
}

const iMenuItem *globalContentItems_ScopedPrefs(void) {
    return items_ScopedPrefs(content_DocPrefsGroup, iFalse) + 1; /* without the scope bar */
}

/*----------------------------------------------------------------------------------------------*/

const char *scope_ScopedPrefs(const iWidget *dlg) {
    const iString *scope = userData_Object(dlg);
    return scope ? cstr_String(scope) : "base";
}

/* Content settings are scoped only in Site Settings. */
static const char *groupScope_(const iWidget *dlg, enum iDocPrefsGroup group) {
    const char *scope = scope_ScopedPrefs(dlg);
    return group == content_DocPrefsGroup && !startsWith_CStr(scope, "root:") ? "base" : scope;
}

void init_ScopedPrefs(iWidget *dlg, const char *scope) {
    setUserData_Object(dlg, newCStr_String(scope));
    update_ScopedPrefs(dlg);
}

void deinit_ScopedPrefs(iWidget *dlg) {
    delete_String(userData_Object(dlg));
    setUserData_Object(dlg, NULL);
}

static void showOrCollapse_(iWidget *d, iBool show) {
    if (d) {
        setFlags_Widget(d, collapse_WidgetFlag, iTrue); /* no space taken when hidden */
        showCollapsed_Widget(d, show);
    }
}

/* A row has the control and its heading. */
static void showRow_(iWidget *control, iBool show) {
    if (!control) {
        return;
    }
    iWidget *parent = parent_Widget(control);
    if (isUsingPanelLayout_Mobile()) {
        if (!cmp_String(id_Widget(parent), "panel")) {
            /* A group of buttons is directly in the panel, preceded by its heading. */
            const size_t index = indexOfChild_Widget(parent, control);
            if (index > 0) {
                showOrCollapse_(child_Widget(parent, index - 1), show);
            }
            showOrCollapse_(control, show);
            return;
        }
        showOrCollapse_(parent, show);
        return;
    }
    iWidget *headings = child_Widget(parent_Widget(parent), 0);
    showOrCollapse_(child_Widget(headings, indexOfChild_Widget(parent, control)), show);
    showOrCollapse_(control, show);
}

static void setDisabled_(iWidget *dlg, const iMenuItem *items, iBool disable) {
    for (const iMenuItem *item = items; item->label; item++) {
        if (equal_Command(item->label, "scopebar") || !hasLabel_Command(item->label, "id")) {
            continue;
        }
        setFlags_Widget(findChild_Widget(dlg, cstr_Command(item->label, "id")), disabled_WidgetFlag,
                        disable);
        if (isContainer_MenuItem_(item)) {
            setDisabled_(dlg, item->data, disable);
        }
    }
}

static void applyValues_(iWidget *dlg, const iDocPrefs *prefs) {
    for (size_t i = 0; i < numSpecs_DocPrefs(); i++) {
        const iDocPrefsSpec *spec = spec_DocPrefs(i);
        if (spec->type == bool_DocPrefsType && spec->id && !spec->cmd) {
            setToggle_Widget(findChild_Widget(dlg, spec->id), prefs->bools[spec->index].value);
        }
    }
    updateDropdownSelection_LabelWidget(findChild_Widget(dlg, "prefs.font.heading"),
                                        format_CStr(":%s", cstr_String(prefs->headingFont)));
    updateDropdownSelection_LabelWidget(findChild_Widget(dlg, "prefs.font.body"),
                                        format_CStr(":%s", cstr_String(prefs->bodyFont)));
    updateDropdownSelection_LabelWidget(findChild_Widget(dlg, "prefs.font.mono"),
                                        format_CStr(":%s", cstr_String(prefs->monospaceFont)));
    updateDropdownSelection_LabelWidget(findChild_Widget(dlg, "prefs.font.monodoc"),
                                        format_CStr(":%s", cstr_String(prefs->monospaceDocumentFont)));
    setValue_SliderWidget(findChild_Widget(dlg, "prefs.linewidth"),
                          lineWidthRel_(prefs->lineWidth.value));
    setValue_SliderWidget(findChild_Widget(dlg, "prefs.linespacing"), prefs->lineSpacing.value);
    setValue_SliderWidget(findChild_Widget(dlg, "prefs.tabwidth"), prefs->tabWidth.value);
    updateDropdownSelection_LabelWidget(findChild_Widget(dlg, "prefs.collapsepre"),
                                        format_CStr(" arg:%d", prefs->collapsePre.value));
    updateDropdownSelection_LabelWidget(findChild_Widget(dlg, "prefs.justify"),
                                        format_CStr(" arg:%d", prefs->justify.value));
    for (int i = 0; i < 2; i++) {
        setFlags_Widget(findChild_Widget(dlg, format_CStr("prefs.quoteicon.%d", i)),
                        selected_WidgetFlag,
                        i == prefs->quoteIcon.value);
    }
    updateDropdownSelection_LabelWidget(findChild_Widget(dlg, "prefs.doctheme.dark"),
                                        format_CStr(".set arg:%d", prefs->docThemeDark.value));
    updateDropdownSelection_LabelWidget(findChild_Widget(dlg, "prefs.doctheme.light"),
                                        format_CStr(".set arg:%d", prefs->docThemeLight.value));
    updateDropdownSelection_LabelWidget(findChild_Widget(dlg, "prefs.pagetheme.dark"),
                                        format_CStr(".set arg:%d", prefs->pageThemeDark.value));
    updateDropdownSelection_LabelWidget(findChild_Widget(dlg, "prefs.pagetheme.light"),
                                        format_CStr(".set arg:%d", prefs->pageThemeLight.value));
    setValue_SliderWidget(findChild_Widget(dlg, "prefs.saturation"),
                          prefs->saturation.value * 100.0f);
    updateDropdownSelection_LabelWidget(findChild_Widget(dlg, "prefs.imagestyle"),
                                        format_CStr(".set arg:%d", prefs->imageStyle.value));
    setToggle_Widget(findChild_Widget(dlg, "prefs.gemtext.ansi.fg"),
                     prefs->ansiEscapes.value & allowFg_AnsiFlag);
    setToggle_Widget(findChild_Widget(dlg, "prefs.gemtext.ansi.bg"),
                     prefs->ansiEscapes.value & allowBg_AnsiFlag);
    setToggle_Widget(findChild_Widget(dlg, "prefs.gemtext.ansi.fontstyle"),
                     prefs->ansiEscapes.value & allowFontStyle_AnsiFlag);
}

static const char *toggleLabel_(enum iDocPrefsGroup group, const char *scope) {
    if (startsWith_CStr(scope, "root:")) {
        return format_CStr("${prefs.scope.site.%s}", groupId_DocPrefs(group));
    }
    const char *name = "";
    for (const iMenuItem *item = protocolItems_; item->label; item++) {
        if (endsWith_String(collectNewCStr_String(item->command), scope)) {
            name = item->label;
        }
    }
    return format_CStr(cstr_Lang(format_CStr("prefs.scope.custom.%s", groupId_DocPrefs(group))),
                       name);
}

void update_ScopedPrefs(iWidget *dlg) {
    iBeginCollect();
    const iBool isSite = startsWith_CStr(scope_ScopedPrefs(dlg), "root:");
    iDocPrefs shown; /* values of this scope, or the inherited ones */
    initCopy_DocPrefs(&shown, &constRule_ScopeRules("base")->prefs);
    for (int group = 1; group < max_DocPrefsGroup; group++) {
        const char *      scope  = groupScope_(dlg, group);
        const iBool       isBase = !iCmpStr(scope, "base");
        const iScopeRule *rule   = constRule_ScopeRules(scope);
        iScopeKey key;
        initScope_ScopeKey(&key, scope);
        const iBool      isEnabled = isBase || (rule && isGroupEnabled_ScopeRule(rule, group));
        const iMenuItem *items     = makeItems_(group, iFalse, iFalse);
        if (!isBase) {
            iDocPrefs inherited;
            init_DocPrefs(&inherited);
            resolveInherited_DocPrefs(&inherited, &key, scope, group);
            for (size_t i = 0; i < numSpecs_DocPrefs(); i++) {
                const iDocPrefsSpec *spec = spec_DocPrefs(i);
                if (spec->group == group) {
                    copyField_DocPrefs(&shown,
                                       isEnabled && isDefined_DocPrefs(&rule->prefs, spec)
                                           ? &rule->prefs
                                           : &inherited,
                                       spec);
                }
            }
            deinit_DocPrefs(&inherited);
        }
        /* Some fields apply only to certain schemes. */
        int numShown = 0;
        for (const iMenuItem *item = items; item->label; item++) {
            iWidget *control = hasLabel_Command(item->label, "id")
                                   ? findChild_Widget(dlg, cstr_Command(item->label, "id"))
                                   : NULL;
            if (!control || equal_Command(item->label, "scopebar")) {
                continue;
            }
            /* Global settings are always shown. */
            const iBool show = (group == content_DocPrefsGroup && !isSite) ||
                               checkScheme_MenuItem(item, cstr_String(&key.scheme));
            if (hasLabel_Command(item->label, "scheme") || hasLabel_Command(item->label, "except")) {
                showRow_(control, show);
            }
            numShown += show ? 1 : 0;
        }
        /* The scope bar. All instances of the dropdown show the same scope. */ {
            const char *barId  = scopeBarId_(group);
            iWidget *   toggle = findChild_Widget(dlg, format_CStr("%s.toggle", barId));
            iWidget *   label  = findChild_Widget(dlg, format_CStr("%s.label", barId));
            updateDropdownSelection_LabelWidget(findChild_Widget(dlg, barId),
                                                format_CStr(" scope:%s", scope));
            setToggle_Widget(toggle, isEnabled);
            if (label) {
                setTextCStr_LabelWidget((iLabelWidget *) label,
                                        isBase ? "" : toggleLabel_(group, scope));
            }
            /* Nothing to enable if no fields apply to the scheme. */
            const iBool showToggle = !isBase && numShown > 0;
            if (isUsingPanelLayout_Mobile()) {
                showOrCollapse_(toggle ? parent_Widget(toggle) : NULL, showToggle);
            }
            else {
                showOrCollapse_(toggle, showToggle);
                showOrCollapse_(label, showToggle);
            }
        }
        setDisabled_(dlg, items, !isEnabled);
        deinit_ScopeKey(&key);
    }
    applyValues_(dlg, &shown);
    deinit_DocPrefs(&shown);
    /* Rows may have been shown or hidden. */
    if (!cmp_String(id_Widget(dlg), "prefs")) {
        updatePreferencesLayout_Widget(dlg);
    }
    else {
        iWidget *tabs = findChild_Widget(dlg, "sitespec.tabs");
        if (tabs) {
            tabs->rect.size = zero_I2();
            resizeToLargestPage_Widget(tabs);
        }
    }
    arrange_Widget(dlg);
    refresh_Widget(dlg);
    iEndCollect();
}

static iBool isDocPrefsCommand_(const char *cmd) {
    if (equal_Command(cmd, "font.set")) {
        return hasLabel_Command(cmd, "heading") || hasLabel_Command(cmd, "body") ||
               hasLabel_Command(cmd, "mono") || hasLabel_Command(cmd, "monodoc");
    }
    return findCommandSpec_DocPrefs(name_Command(cmd)) != NULL ||
           equal_Command(cmd, "saturation.set") || startsWith_Command(cmd, "prefs.gemtext.ansi.");
}

static enum iDocPrefsGroup commandGroup_(const char *cmd) {
    const iDocPrefsSpec *spec = findCommandSpec_DocPrefs(name_Command(cmd));
    if (spec) {
        return spec->group;
    }
    return startsWith_Command(cmd, "prefs.gemtext.ansi.") ? content_DocPrefsGroup
                                                          : none_DocPrefsGroup;
}

iBool handleCommand_ScopedPrefs(iWidget *dlg, const char *cmd) {
    if (equal_Command(cmd, "prefs.scope.set")) {
        set_String(userData_Object(dlg), string_Command(cmd, "scope"));
        update_ScopedPrefs(dlg);
        return iTrue;
    }
    if (equal_Command(cmd, "prefs.scope.update")) {
        update_ScopedPrefs(dlg);
        return iTrue;
    }
    if (startsWith_Command(cmd, "prefs.scope.")) {
        for (int group = 1; group < max_DocPrefsGroup; group++) {
            if (equal_Command(cmd, format_CStr("%s.toggle.changed", scopeBarId_(group)))) {
                postCommandf_App("docprefs.group arg:%d group:%s scope:%s",
                                 arg_Command(cmd), groupId_DocPrefs(group),
                                 groupScope_(dlg, group));
                postCommand_Widget(dlg, "prefs.scope.update");
                return iTrue;
            }
        }
        return iFalse;
    }
    if (equal_Command(cmd, "linewidth.slider")) {
        postCommand_Widget(dlg, "linewidth.set arg:%d", lineWidthChars_(argf_Command(cmd)));
        return iTrue;
    }
    /* The scope is added to the commands of the controls. */
    if (isDocPrefsCommand_(cmd) && !hasLabel_Command(cmd, "scope")) {
        postCommandf_App("%s scope:%s", cmd, groupScope_(dlg, commandGroup_(cmd)));
        postCommand_Widget(dlg, "prefs.scope.update");
        return iTrue;
    }
    return iFalse;
}
