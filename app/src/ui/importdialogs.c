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

#include "importdialogs.h"

#include "app.h"
#include "bookmarks.h"
#include "command.h"
#include "defs.h"
#include "dialog.h"
#include "export.h"
#include "labelwidget.h"
#include "root.h"
#include "util.h"
#include "widget.h"

static iBool handleLinkImporterCommands_(iWidget *dlg, const char *cmd) {
    if (equalWidget_Command(cmd, dlg, "cancel")) {
        setupSheetTransition_Mobile(dlg, dialogTransitionDir_Widget(dlg));
        destroy_Widget(dlg);
        return iTrue;
    }
    else if (equalWidget_Command(cmd, dlg, "dlg.import.intofolder")) {
        updateDropdownSelection_LabelWidget(findChild_Widget(dlg, "dlg.import.intofolder"),
                                            format_CStr(" arg:%d", arg_Command(cmd)));
        return iTrue;
    }
    else if (equalWidget_Command(cmd, dlg, "dlg.import.accept")) {
        const char *intoFolder =
            selectedDropdownCommand_LabelWidget(findChild_Widget(dlg, "dlg.import.intofolder"));
        const iBool headings = isSelected_Widget(findChild_Widget(dlg, "dlg.import.headings"));
        postCommandf_App("bookmark.links folder:%d headings:%d", arg_Command(intoFolder), headings);
        setupSheetTransition_Mobile(dlg, dialogTransitionDir_Widget(dlg));
        destroy_Widget(dlg);
        return iTrue;
    }
    return iFalse;
}

iWidget *makeLinkImporter_Widget(size_t count) {
    const iMenuItem actions[] = {
        { "${cancel}" },
        { format_CStr(
              cstrCount_Lang("dlg.import.add.n", (int) count), uiTextAction_ColorEscape, count),
          0,
          0,
          "dlg.import.accept" },
    };
    iWidget *dlg = NULL;
    const char *dlgId = "linkbookmarking";
    const iArray *folders = makeBookmarkFolderActions_MenuItem("dlg.import.intofolder", iTrue, 0);
    if (isUsingPanelLayout_Mobile()) {
        dlg = makePanels_Mobile(dlgId, (iMenuItem[]){
            { "title id:heading.import.bookmarks" },
            { format_CStr("label text:%s", formatCStrs_Lang("dlg.import.found.n", count)) },
            { "dropdown id:dlg.import.intofolder", 0, 0, (const void *) constData_Array(folders) },
            { "toggle id:dlg.import.headings" },
            { "padding" },
            { NULL }
        }, actions, iElemCount(actions));
    }
    else {
        iWidget *headings, *values;
        dlg = makeSheet_Widget(dlgId);
        addDialogTitle_Widget(dlg, "${heading.import.bookmarks}", "heading.import.bookmarks");
        addWrappedLabel_Widget(dlg, formatCStrs_Lang("dlg.import.found.n", count), NULL);
        addChild_Widget(dlg, iClob(makePadding_Widget(gap_UI)));
        addChild_Widget(dlg, iClob(makeTwoColumns_Widget(&headings, &values)));
        iLabelWidget *intoFolder = addDialogDropMenu_Widget(headings,
                           values,
                           "${dlg.import.intofolder}",
                           constData_Array(folders),
                           iInvalidSize,
                           "dlg.import.intofolder");
        addDialogToggle_Widget(headings, values, "${dlg.import.headings}", "dlg.import.headings");
        addChild_Widget(dlg, iClob(makePadding_Widget(gap_UI)));
        addChild_Widget(dlg, iClob(makeDialogButtons_Widget(actions, iElemCount(actions))));
        addChild_Widget(get_Root()->widget, iClob(dlg));
        arrange_Widget(dlg);
        arrange_Widget(dlg);
    }
    updateDropdownSelection_LabelWidget(findChild_Widget(dlg, "dlg.import.intofolder"),
                                        format_CStr(" arg:%zu", recentFolder_Bookmarks(bookmarks_App())));
    setToggle_Widget(findChild_Widget(dlg, "dlg.import.headings"), iTrue);
    setCommandHandler_Widget(dlg, handleLinkImporterCommands_);
    setupSheetTransition_Mobile(dlg, incoming_TransitionFlag | dialogTransitionDir_Widget(dlg));
    return dlg;
}

/*-----------------------------------------------------------------------------------------------*/

static enum iImportMethod checkImportMethod_(const iWidget *dlg, const char *id) {
    return isSelected_Widget(findChild_Widget(dlg, format_CStr("%s.0", id))) ? none_ImportMethod
           : isSelected_Widget(findChild_Widget(dlg, format_CStr("%s.1", id)))
               ? ifMissing_ImportMethod
               : all_ImportMethod;
}

static iBool handleUserDataImporterCommands_(iWidget *dlg, const char *cmd) {
    if (equalWidget_Command(cmd, dlg, "importer.cancel") ||
        equalWidget_Command(cmd, dlg, "importer.accept")) {
        if (equal_Command(cmd, "importer.accept")) {
            /* Compose the final import command. */
            enum iImportMethod bookmarkMethod = checkImportMethod_(dlg, "importer.bookmark");
            enum iImportMethod identMethod =
                isSelected_Widget(findChild_Widget(dlg, "importer.idents")) ? ifMissing_ImportMethod
                                                                            : none_ImportMethod;
            enum iImportMethod trustedMethod  = checkImportMethod_(dlg, "importer.trusted");
            enum iImportMethod snippetsMethod = checkImportMethod_(dlg, "importer.snippets");
            enum iImportMethod sitespecMethod = checkImportMethod_(dlg, "importer.sitespec");
            enum iImportMethod visitedMethod =
                isSelected_Widget(findChild_Widget(dlg, "importer.history")) ? all_ImportMethod
                                                                             : none_ImportMethod;
            postCommandf_App(
                "import arg:1 "
                "bookmarks:%d idents:%d trusted:%d visited:%d sitespec:%d snippets:%d path:%s",
                bookmarkMethod,
                identMethod,
                trustedMethod,
                visitedMethod,
                sitespecMethod,
                snippetsMethod,
                suffixPtr_Command(cmd, "path"));
        }
        setupSheetTransition_Mobile(dlg, dialogTransitionDir_Widget(dlg));
        destroy_Widget(dlg);
        return iTrue;
    }
    else if (equalWidget_Command(cmd, dlg, "importer.selectall")) {
        postCommand_Widget(findChild_Widget(dlg, "importer.bookmark.1"), "trigger");
        postCommand_Widget(findChild_Widget(dlg, "importer.trusted.1"), "trigger");
        postCommand_Widget(findChild_Widget(dlg, "importer.sitespec.1"), "trigger");
        postCommand_Widget(findChild_Widget(dlg, "importer.snippet.1"), "trigger");
        setToggle_Widget(findChild_Widget(dlg, "importer.history"), iTrue);
        setToggle_Widget(findChild_Widget(dlg, "importer.idents"), iTrue);
        return iTrue;
    }
    return iFalse;
}

iWidget *makeUserDataImporter_Widget(const iString *archivePath) {
    iWidget *dlg;
    const iMenuItem actions[] = {
        { "${menu.selectall}", 0, 0, "importer.selectall" },
        { "---" },
        { "${cancel}", SDLK_ESCAPE, 0, "importer.cancel" },
        { uiTextAction_ColorEscape "${import.userdata}",
          SDLK_RETURN, KMOD_ACCEPT,
          format_CStr("importer.accept path:%s", cstr_String(archivePath)) },
    };
    if (isUsingPanelLayout_Mobile()) {
        const iMenuItem bookmarkItems[] = {
            { "button id:importer.bookmark.0 label:dlg.userdata.no", 0, 0, "." },
            { "button id:importer.bookmark.1 label:dlg.userdata.missing", 0, 0, "." },
            { "button id:importer.bookmark.2 label:dlg.userdata.alldup", 0, 0, "." },
            { NULL }
        };
        const iMenuItem snippetItems[] = {
            { "button id:importer.snippet.0 label:dlg.userdata.no", 0, 0, "." },
            { "button id:importer.snippet.1 label:dlg.userdata.missing", 0, 0, "." },
            { "button id:importer.snippet.2 label:dlg.userdata.all", 0, 0, "." },
            { NULL }
        };
        const iMenuItem sitespecItems[] = {
            { "button id:importer.sitespec.0 label:dlg.userdata.no", 0, 0, "." },
            { "button id:importer.sitespec.1 label:dlg.userdata.missing", 0, 0, "." },
            { "button id:importer.sitespec.2 label:dlg.userdata.all", 0, 0, "." },
            { NULL }
        };
        const iMenuItem trustedItems[] = {
            { "button id:importer.trusted.0 label:dlg.userdata.no", 0, 0, "." },
            { "button id:importer.trusted.1 label:dlg.userdata.missing", 0, 0, "." },
            { "button id:importer.trusted.2 label:dlg.userdata.all", 0, 0, "." },
            { NULL }
        };
        dlg = makePanels_Mobile(
            "importer",
            (iMenuItem[]){ { "title id:heading.import.userdata" },
                           { "toggle id:importer.history text:${import.userdata.history}" },
                           { "toggle id:importer.idents text:${import.userdata.idents}" },
                           { "radio id:import.userdata.bookmarks", 0, 0, (const void *) bookmarkItems },
                           { "radio id:import.userdata.snippets", 0, 0, (const void *) snippetItems },
                           { "radio id:import.userdata.sitespec", 0, 0, (const void *) sitespecItems },
                           { "radio id:import.userdata.trusted", 0, 0, (const void *) trustedItems },
                           { NULL } },
            actions,
            iElemCount(actions));
    }
    else {
        dlg = makeSheet_Widget("importer");
        addDialogTitle_Widget(dlg, "${heading.import.userdata}", NULL);
        iWidget *headings, *values;
        addChild_Widget(dlg, iClob(makeTwoColumns_Widget(&headings, &values)));
        addDialogToggle_Widget(headings, values, "${import.userdata.history}", "importer.history");
        addDialogToggle_Widget(headings, values, "${import.userdata.idents}", "importer.idents");
        /* Bookmarks. */
        addChild_Widget(headings, iClob(makeHeading_Widget("${import.userdata.bookmarks}")));
        iWidget *radio = new_Widget(); {
            addRadioButton_Widget(radio, "importer.bookmark.0", "${dlg.userdata.no}", ".");
            addRadioButton_Widget(radio, "importer.bookmark.1", "${dlg.userdata.missing}", ".");
            addRadioButton_Widget(radio, "importer.bookmark.2", "${dlg.userdata.alldup}", ".");
        }
        addChildFlags_Widget(values, iClob(radio), arrangeHorizontal_WidgetFlag | arrangeSize_WidgetFlag);
        /* Snippets. */
        addChild_Widget(headings, iClob(makeHeading_Widget("${import.userdata.snippets}")));
        radio = new_Widget(); {
            addRadioButton_Widget(radio, "importer.snippet.0", "${dlg.userdata.no}", ".");
            addRadioButton_Widget(radio, "importer.snippet.1", "${dlg.userdata.missing}", ".");
            addRadioButton_Widget(radio, "importer.snippet.2", "${dlg.userdata.all}", ".");
        }
        addChildFlags_Widget(values, iClob(radio), arrangeHorizontal_WidgetFlag | arrangeSize_WidgetFlag);
        /* Site-specific. */
        addChild_Widget(headings, iClob(makeHeading_Widget("${import.userdata.sitespec}")));
        radio = new_Widget(); {
            addRadioButton_Widget(radio, "importer.sitespec.0", "${dlg.userdata.no}", ".");
            addRadioButton_Widget(radio, "importer.sitespec.1", "${dlg.userdata.missing}", ".");
            addRadioButton_Widget(radio, "importer.sitespec.2", "${dlg.userdata.all}", ".");
        }
        addChildFlags_Widget(values, iClob(radio), arrangeHorizontal_WidgetFlag | arrangeSize_WidgetFlag);
        /* Trusted certs. */
        addChild_Widget(headings, iClob(makeHeading_Widget("${import.userdata.trusted}")));
        radio = new_Widget(); {
            addRadioButton_Widget(radio, "importer.trusted.0", "${dlg.userdata.no}", ".");
            addRadioButton_Widget(radio, "importer.trusted.1", "${dlg.userdata.missing}", ".");
            addRadioButton_Widget(radio, "importer.trusted.2", "${dlg.userdata.all}", ".");
        }
        addChildFlags_Widget(values, iClob(radio), arrangeHorizontal_WidgetFlag | arrangeSize_WidgetFlag);
        addDialogPadding_Widget(headings, values);
        addChild_Widget(dlg, iClob(makeDialogButtons_Widget(actions, iElemCount(actions))));
        addChild_Widget(dlg->root->widget, iClob(dlg));
        arrange_Widget(dlg);
        arrange_Widget(dlg);
    }
    /* Initialize. */
    setToggle_Widget(findChild_Widget(dlg, "importer.bookmark.0"), iTrue);
    setToggle_Widget(findChild_Widget(dlg, "importer.snippet.0"), iTrue);
    setToggle_Widget(findChild_Widget(dlg, "importer.idents.0"), iTrue);
    setToggle_Widget(findChild_Widget(dlg, "importer.sitespec.0"), iTrue);
    setToggle_Widget(findChild_Widget(dlg, "importer.trusted.0"), iTrue);
    setCommandHandler_Widget(dlg, handleUserDataImporterCommands_);
    setupSheetTransition_Mobile(dlg, incoming_TransitionFlag | dialogTransitionDir_Widget(dlg));
    return dlg;
}
