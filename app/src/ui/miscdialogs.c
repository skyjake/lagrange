/* Copyright 2020 Jaakko Keränen <jaakko.keranen@iki.fi>

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

#include "miscdialogs.h"

#include "app.h"
#include "command.h"
#include "defs.h"
#include "dialog.h"
#include "documentwidget.h"
#include "gmutil.h"
#include "inputwidget.h"
#include "labelwidget.h"
#include "render/text.h"
#include "root.h"
#include <lagrange/sitespec.h>
#include <lagrange/snippets.h>
#include "util.h"
#include "widget.h"

static void siteSpecificThemeChanged_(const iWidget *dlg) {
    iDocumentWidget *doc = document_App();
    setThemeSeed_GmDocument((iGmDocument *) document_DocumentWidget(doc),
                            urlPaletteSeed_String(url_DocumentWidget(doc)),
                            urlThemeSeed_String(url_DocumentWidget(doc)));
    postCommand_App("theme.changed");
}

static const iString *siteSpecificRoot_(const iWidget *dlg) {
    return collect_String(suffix_Command(cstr_String(id_Widget(dlg)), "site"));
}

static void updateSiteSpecificTheme_(iInputWidget *palSeed, void *context) {
    iWidget *dlg = context;
    const iString *siteRoot = siteSpecificRoot_(dlg);
    setValueString_SiteSpec(siteRoot, paletteSeed_SiteSpecKey, text_InputWidget(palSeed));
    siteSpecificThemeChanged_(dlg);
    /* Allow seeing the new theme. */
    setFlags_Widget(dlg, noFadeBackground_WidgetFlag, iTrue);
}

static void closeSiteSpecific_(iWidget *dlg) {
    setupSheetTransition_Mobile(dlg, dialogTransitionDir_Widget(dlg));
    delete_String(userData_Object(dlg)); /* saved original palette seed */
    destroy_Widget(dlg);
}

static iBool siteSpecificSettingsHandler_(iWidget *dlg, const char *cmd) {
    if (equal_Command(cmd, "cancel")) {
        const iBool wasNoFade = (flags_Widget(dlg) & noFadeBackground_WidgetFlag) != 0;
        iInputWidget *palSeed = findChild_Widget(dlg, "sitespec.palette");
        setText_InputWidget(palSeed, userData_Object(dlg));
        updateSiteSpecificTheme_(palSeed, dlg);
        setFlags_Widget(dlg, noFadeBackground_WidgetFlag, wasNoFade);
        closeSiteSpecific_(dlg);
        return iTrue;
    }
    if (equalArg_Command(cmd, "input.ended", "id", "sitespec.palette")) {
        setFlags_Widget(dlg, noFadeBackground_WidgetFlag, iFalse);
        refresh_Widget(dlg);
        siteSpecificThemeChanged_(dlg);
        return iTrue;
    }
    if (equal_Command(cmd, "sitespec.accept")) {
        const iInputWidget *palSeed   = findChild_Widget(dlg, "sitespec.palette");
        const iBool         warnAnsi  = isSelected_Widget(findChild_Widget(dlg, "sitespec.ansi"));
        const iString      *siteRoot  = siteSpecificRoot_(dlg);
        int                 dismissed = value_SiteSpec(siteRoot, dismissWarnings_SiteSpecKey);
        iChangeFlags(dismissed, ansiEscapes_GmDocumentWarning, !warnAnsi);
        setValue_SiteSpec(siteRoot, dismissWarnings_SiteSpecKey, dismissed);
        setValue_SiteSpec(siteRoot,
                          tlsSessionCache_SiteSpeckey,
                          isSelected_Widget(findChild_Widget(dlg, "sitespec.tlscache")));
        setValueString_SiteSpec(siteRoot, paletteSeed_SiteSpecKey, text_InputWidget(palSeed));
        siteSpecificThemeChanged_(dlg);
        /* Note: The active DocumentWidget may actually be different than when opening the dialog. */
        closeSiteSpecific_(dlg);
        return iTrue;
    }
    return iFalse;
}

iWidget *makeSiteSpecificSettings_Widget(const iString *url) {
    iWidget *dlg;
    const char *sheetId = format_CStr("sitespec site:%s", cstr_Rangecc(urlRoot_String(url)));
    const iMenuItem actions[] = {
        { "${cancel}" },
        { uiTextAction_ColorEscape "${sitespec.accept}", SDLK_RETURN, KMOD_ACCEPT, "sitespec.accept" }
    };
    if (isUsingPanelLayout_Mobile()) {
        dlg = makePanels_Mobile(sheetId, (iMenuItem[]){
            { "title id:heading.sitespec" },
            { "input id:sitespec.palette" },
            { "padding" },
            { "toggle id:sitespec.ansi" },
            { "toggle id:sitespec.tlscache" },
            { "padding" },
            { NULL }
        }, actions, iElemCount(actions));
    }
    else {
        iWidget *headings, *values;
        dlg = makeSheet_Widget(sheetId);
        addDialogTitle_Widget(dlg, "${heading.sitespec}", "heading.sitespec");
        addChild_Widget(dlg, iClob(makeTwoColumns_Widget(&headings, &values)));
        iInputWidget *palSeed = new_InputWidget(0);
        setHint_InputWidget(palSeed, cstr_Block(urlThemeSeed_String(url)));
        addPrefsInputWithHeading_Widget(headings, values, "sitespec.palette", iClob(palSeed));
        addDialogToggle_Widget(headings, values, "${sitespec.ansi}", "sitespec.ansi");
        addDialogToggle_Widget(headings, values, "${sitespec.tlscache}", "sitespec.tlscache");
        addChild_Widget(dlg, iClob(makeDialogButtons_Widget(actions, iElemCount(actions))));
        addChild_Widget(get_Root()->widget, iClob(dlg));
        as_Widget(palSeed)->rect.size.x = aspect_UI * 60 * gap_UI;
        arrange_Widget(dlg);
    }
    /* Initialize. */ {
        const iString *site = collectNewRange_String(urlRoot_String(url));
        setToggle_Widget(findChild_Widget(dlg, "sitespec.ansi"),
                         ~value_SiteSpec(site, dismissWarnings_SiteSpecKey) & ansiEscapes_GmDocumentWarning);
        setToggle_Widget(findChild_Widget(dlg, "sitespec.tlscache"),
                         value_SiteSpec(site, tlsSessionCache_SiteSpeckey));
        iInputWidget *palSeed = findChild_Widget(dlg, "sitespec.palette");
        setText_InputWidget(palSeed, valueString_SiteSpec(site, paletteSeed_SiteSpecKey));
        setHint_InputWidget(palSeed, cstr_Block(urlThemeSeed_String(url)));
        /* Keep a copy of the original palette seed for restoring on cancel. */
        setUserData_Object(dlg, copy_String(valueString_SiteSpec(site, paletteSeed_SiteSpecKey)));
        if (!isUsingPanelLayout_Mobile()) {
            setValidator_InputWidget(findChild_Widget(dlg, "sitespec.palette"),
                                     updateSiteSpecificTheme_, dlg);
        }
    }
    setCommandHandler_Widget(dlg, siteSpecificSettingsHandler_);
    setupSheetTransition_Mobile(dlg, incoming_TransitionFlag | dialogTransitionDir_Widget(dlg));
    setFocus_Widget(findChild_Widget(dlg, "sitespec.palette"));
    return dlg;
}

/*-----------------------------------------------------------------------------------------------*/

static iBool handleSnippetCreationCommands_(iWidget *dlg, const char *cmd) {
    if (equal_Command(cmd, "widget.resized")) {
        iWidget  *headings   = findChild_Widget(dlg, "snip.columns.head");
        iWidget  *name       = findChild_Widget(dlg, "snip.name");
        iWidget  *content    = findChild_Widget(dlg, "snip.content");
        const int newWidth   = width_Widget(dlg) - width_Widget(headings) - 6 * gap_UI;
        name->rect.size.x    = newWidth;
        content->rect.size.x = newWidth;
        return iTrue;
    }
    if (equalWidget_Command(cmd, dlg, "cancel")) {
        setupSheetTransition_Mobile(dlg, dialogTransitionDir_Widget(dlg));
        destroy_Widget(dlg);
        return iTrue;
    }
    else if (equalWidget_Command(cmd, dlg, "snip.accept")) {
        iInputWidget *name    = findChild_Widget(dlg, "snip.name");
        iInputWidget *content = findChild_Widget(dlg, "snip.content");
        if (!set_Snippets(text_InputWidget(name), text_InputWidget(content))) {
            return iTrue;
        }
        postCommandf_App("snippets.changed added:%s", cstr_String(text_InputWidget(name)));
        setupSheetTransition_Mobile(dlg, dialogTransitionDir_Widget(dlg));
        destroy_Widget(dlg);
        return iTrue;
    }
    return iFalse;
}

iWidget *makeSnippetCreation_Widget(void) {
    const iMenuItem actions[] = {
        { "${cancel}" },
        { uiTextAction_ColorEscape "${snip.accept}", SDLK_RETURN, KMOD_ACCEPT, "snip.accept" }
    };
    iWidget *dlg = NULL;
    if (isUsingPanelLayout_Mobile()) {
        dlg = makePanels_Mobile("snip", (iMenuItem[]){
            { "title id:heading.snip text:${heading.snip.new}" },
            { "input id:snip.name nolinebreaks:1" },
            { "label text:${sniped.help}" },
            { "heading text:${snip.content}"},
            { "input id:snip.content noheading:1" },
            { NULL }
        }, actions, iElemCount(actions));
        setCommandHandler_Widget(dlg, handleSnippetCreationCommands_);
    }
    else {
        iWidget *headings, *values;
        dlg = makeSheet_Widget("snip");
        addDialogTitle_Widget(dlg, "${heading.snip.new}", "heading.snip");
        addChild_Widget(dlg, iClob(makeTwoColumns_Widget(&headings, &values)));
        setId_Widget(headings, "snip.columns.head");
        iInputWidget *name    = new_InputWidget(0);
        iInputWidget *content = new_InputWidget(0);
        setLineBreaksEnabled_InputWidget(name, iFalse);
        addPrefsInputWithHeading_Widget(headings, values, "snip.name", iClob(name));
        addPrefsInputWithHeading_Widget(headings, values, "snip.content", iClob(content));
        addChild_Widget(dlg, iClob(makePadding_Widget(gap_UI)));
        addWrappedLabel_Widget(dlg, "${sniped.help}", NULL);
        addChild_Widget(dlg, iClob(makePadding_Widget(gap_UI)));
        addChild_Widget(dlg, iClob(makeDialogButtons_Widget(actions, iElemCount(actions))));
        addChild_Widget(get_Root()->widget, iClob(dlg));
        as_Widget(name)->rect.size.x = 60 * gap_UI;
        as_Widget(content)->rect.size.x = 60 * gap_UI;
        arrange_Widget(dlg);
        setCommandHandler_Widget(dlg, handleSnippetCreationCommands_);
        enableResizing_Widget(dlg, width_Widget(dlg), "snip");
    }
    setFocus_Widget(findChild_Widget(dlg, "snip.name"));
    setupSheetTransition_Mobile(dlg, incoming_TransitionFlag | dialogTransitionDir_Widget(dlg));
    return dlg;
}

/*-----------------------------------------------------------------------------------------------*/

iWidget *makeIdentityCreation_Widget(void) {
    const iMenuItem actions[] = { { "${dlg.newident.more}", 0, 0, "ident.showmore" },
                                  { "---" },
                                  { "${cancel}", SDLK_ESCAPE, 0, "ident.cancel" },
                                  { uiTextAction_ColorEscape "${dlg.newident.create}",
                                    SDLK_RETURN,
                                    KMOD_ACCEPT,
                                    "ident.accept" } };
    const iString *docUrl = url_DocumentWidget(document_App());
    iUrl url;
    init_Url(&url, docUrl);
    const iMenuItem scopeItems[] = {
        { "${dlg.newident.scope.none}", 0, 0, "ident.scope arg:0" },
        { format_CStr("${dlg.newident.scope.domain}:\n%s", cstr_Rangecc(url.host)), 0, 0, "ident.scope arg:1" },
        { format_CStr("${dlg.newident.scope.dir}:\n%s", cstr_Rangecc(urlDirectory_String(docUrl))), 0, 0, "ident.scope arg:2" },
        { format_CStr("${dlg.newident.scope.page}:\n%s", cstr_Rangecc(url.path)), 0, 0, "ident.scope arg:3" },
        { NULL }
    };
    iWidget *dlg;
    if (isUsingPanelLayout_Mobile()) {
        dlg = makePanels_Mobile("ident", (iMenuItem[]){
            { "title id:ident.heading text:${heading.newident}" },
            { "label text:${dlg.newident.rsa.selfsign}" },
            { "dropdown id:ident.scope text:${dlg.newident.scope}", 0, 0,
              (const void *) scopeItems },
            { "input id:ident.until hint:hint.newident.date maxlen:10 text:${dlg.newident.until}" },
            //{ "padding" },
            //{ "toggle id:ident.temp text:${dlg.newident.temp}" },
            //{ "label text:${help.ident.temp}" },
            { "heading id:dlg.newident.commonname" },
            { "input id:ident.common noheading:1" },
            { "padding collapse:1" },
            { "input collapse:1 id:ident.email hint:hint.optional text:${dlg.newident.email}" },
            { "input collapse:1 id:ident.userid hint:hint.optional text:${dlg.newident.userid}" },
            { "input collapse:1 id:ident.domain hint:hint.optional text:${dlg.newident.domain}" },
            { "input collapse:1 id:ident.org hint:hint.optional text:${dlg.newident.org}" },
            { "input collapse:1 id:ident.country hint:hint.optional text:${dlg.newident.country}" },
            { NULL }
        }, actions, iElemCount(actions));
    }
    else {
        dlg = makeSheet_Widget("ident");
        addDialogTitle_Widget(dlg, "${heading.newident}", "ident.heading");
        iWidget *page = new_Widget();
        addChildFlags_Widget(
            dlg, iClob(new_LabelWidget("${dlg.newident.rsa.selfsign}", NULL)), frameless_WidgetFlag);
        /* TODO: Use makeTwoColumnWidget_? */
        addChild_Widget(dlg, iClob(page));
        setFlags_Widget(page, arrangeHorizontal_WidgetFlag | arrangeSize_WidgetFlag, iTrue);
        iWidget *headings = addChildFlags_Widget(
            page, iClob(new_Widget()), arrangeVertical_WidgetFlag | arrangeSize_WidgetFlag);
        iWidget *values = addChildFlags_Widget(
            page, iClob(new_Widget()), arrangeVertical_WidgetFlag | arrangeSize_WidgetFlag);
        setId_Widget(headings, "headings");
        setId_Widget(values, "values");
        iInputWidget *inputs[6];
        /* Where will the new identity be active on? */ {
            iWidget *head = addChild_Widget(headings, iClob(makeHeading_Widget("${dlg.newident.scope}")));
            iWidget *val;
            setId_Widget(
                addChild_Widget(values,
                                val = iClob(makeMenuButton_LabelWidget(
                                    scopeItems[0].label, scopeItems, iElemCount(scopeItems)))),
                "ident.scope");
            head->sizeRef = val;
        }
        addDialogInputWithHeading_Widget(headings,
                                   values,
                                   "${dlg.newident.until}",
                                   "ident.until",
                                   iClob(newHint_InputWidget(19, "${hint.newident.date}")));
        addDialogInputWithHeading_Widget(headings,
                                   values,
                                   "${dlg.newident.commonname}",
                                   "ident.common",
                                   iClob(inputs[0] = new_InputWidget(0)));
        /* Temporary? */ {
            addChild_Widget(headings, iClob(makeHeading_Widget("${dlg.newident.temp}")));
            iWidget *tmpGroup = new_Widget();
            setFlags_Widget(tmpGroup, arrangeSize_WidgetFlag | arrangeHorizontal_WidgetFlag, iTrue);
            addChild_Widget(tmpGroup, iClob(makeToggle_Widget("ident.temp")));
            setId_Widget(
                addChildFlags_Widget(tmpGroup,
                                     iClob(new_LabelWidget(uiTextCaution_ColorEscape warning_Icon
                                                           "  ${dlg.newident.notsaved}",
                                                           NULL)),
                                     hidden_WidgetFlag | frameless_WidgetFlag),
                "ident.temp.note");
            addChild_Widget(values, iClob(tmpGroup));
        }
        addChildFlags_Widget(headings, iClob(makePadding_Widget(gap_UI)), collapse_WidgetFlag | hidden_WidgetFlag);
        addChildFlags_Widget(values, iClob(makePadding_Widget(gap_UI)), collapse_WidgetFlag | hidden_WidgetFlag);
        addDialogInputWithHeadingAndFlags_Widget(headings, values, "${dlg.newident.email}",   "ident.email",   iClob(inputs[1] = newHint_InputWidget(0, "${hint.optional}")), collapse_WidgetFlag | hidden_WidgetFlag);
        addDialogInputWithHeadingAndFlags_Widget(headings, values, "${dlg.newident.userid}",  "ident.userid",  iClob(inputs[2] = newHint_InputWidget(0, "${hint.optional}")), collapse_WidgetFlag | hidden_WidgetFlag);
        addDialogInputWithHeadingAndFlags_Widget(headings, values, "${dlg.newident.domain}",  "ident.domain",  iClob(inputs[3] = newHint_InputWidget(0, "${hint.optional}")), collapse_WidgetFlag | hidden_WidgetFlag);
        addDialogInputWithHeadingAndFlags_Widget(headings, values, "${dlg.newident.org}",     "ident.org",     iClob(inputs[4] = newHint_InputWidget(0, "${hint.optional}")), collapse_WidgetFlag | hidden_WidgetFlag);
        addDialogInputWithHeadingAndFlags_Widget(headings, values, "${dlg.newident.country}", "ident.country", iClob(inputs[5] = newHint_InputWidget(0, "${hint.optional}")), collapse_WidgetFlag | hidden_WidgetFlag);
        arrange_Widget(dlg);
        for (size_t i = 0; i < iElemCount(inputs); ++i) {
            as_Widget(inputs[i])->rect.size.x = 100 * gap_UI * aspect_UI - headings->rect.size.x;
        }
        addChild_Widget(dlg, iClob(makeDialogButtons_Widget(actions, iElemCount(actions))));
        addChild_Widget(get_Root()->widget, iClob(dlg));
    }
    setupSheetTransition_Mobile(dlg, incoming_TransitionFlag | dialogTransitionDir_Widget(dlg));
    return dlg;
}

/*-----------------------------------------------------------------------------------------------*/

iWidget *makeGlyphFinder_Widget(void) {
    iString msg;
    iString command;
    init_String(&msg);
    initCStr_String(&command, "!font.find chars:");
    for (size_t i = 0; ; i++) {
        iChar ch = missing_Text(i);
        if (!ch) break;
        appendFormat_String(&msg, " U+%04X", ch);
        appendChar_String(&command, ch);
    }
    iArray items;
    init_Array(&items, sizeof(iMenuItem));
    if (!isEmpty_String(&msg)) {
        prependCStr_String(&msg, "${dlg.glyphfinder.missing} ");
        appendCStr_String(&msg, "\n\n${dlg.glyphfinder.help}");
        pushBackN_Array(
            &items,
            (iMenuItem[]){
                { "${menu.fonts}", 0, 0, "!open newtab:1 url:about:fonts" },
                { "${dlg.glyphfinder.disable}", 0, 0, "prefs.font.warnmissing.changed arg:0" },
                { "---" },
                { uiTextAction_ColorEscape magnifyingGlass_Icon " ${dlg.glyphfinder.search}",
                  0,
                  0,
                  cstr_String(&command) },
                { "${close}", 0, 0, "cancel" } },
            5);
    }
    else {
        setCStr_String(&msg, "${dlg.glyphfinder.help.empty}");
        pushBackN_Array(&items,
                        (iMenuItem[]){ { "${menu.reload}", 0, 0, "navigate.reload" },
                                       { "${close}", 0, 0, "cancel" } },
                        2);
    }
    iWidget *dlg = makeQuestion_Widget("${heading.glyphfinder}", cstr_String(&msg),
                                       constData_Array(&items),
                                       size_Array(&items));
    arrange_Widget(dlg);
    deinit_Array(&items);
    deinit_String(&command);
    deinit_String(&msg);
    return dlg;
}
