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

#pragma once

#include "defs.h"
#include <the_Foundation/regexp.h>
#include <the_Foundation/toml.h>

/* Document preferences that cascade by scope (site, protocol, all protocols). */

iDeclareType(DocPrefs)
iDeclareType(DocPrefsSpec)
iDeclareType(ScopeKey)
iDeclareType(ScopeRule)

typedef struct { iBool value; iBool isDefined; } iOptBool;
typedef struct { int   value; iBool isDefined; } iOptInt;
typedef struct { float value; iBool isDefined; } iOptFloat;

enum iCollapse {
    never_Collapse,
    notByDefault_Collapse,
    byDefault_Collapse,
    always_Collapse,
};

enum iJustify {
    never_Justify,
    pagesOnly_Justify,
    always_Justify,
};

/* Note: Group IDs are part of the file format. */
enum iDocPrefsGroup {
    none_DocPrefsGroup, /* ungrouped fields follow `isDefined` */
    typography_DocPrefsGroup,
    layout_DocPrefsGroup,
    style_DocPrefsGroup,
    theme_DocPrefsGroup,
    content_DocPrefsGroup,
    max_DocPrefsGroup
};

enum iDocPrefsType {
    bool_DocPrefsType,
    int_DocPrefsType,
    float_DocPrefsType,
    string_DocPrefsType,
};

/* Note: These match the arrays/structs in DocPrefs. */
enum iDocPrefsBool {
    /* Typography */
    monospaceDocument_DocPrefsBool,
    /* Layout */
    bigFirstParagraph_DocPrefsBool,
    plainTextWrap_DocPrefsBool,
    expandToLongLines_DocPrefsBool,
    centerShortDocs_DocPrefsBool,
    pageView_DocPrefsBool,
    /* Style */
    italicQuote_DocPrefsBool,
    quoteIcon_DocPrefsBool,
    sideIcon_DocPrefsBool,
    boldLinkVisited_DocPrefsBool,
    boldLinkDark_DocPrefsBool,
    boldLinkLight_DocPrefsBool,
    /* Content */
    geminiStyledGopher_DocPrefsBool,
    markdownAsSource_DocPrefsBool,
    max_DocPrefsBool
};

enum iDocPrefsInt {
    /* Layout */
    lineWidth_DocPrefsInt,
    tabWidth_DocPrefsInt,
    collapsePre_DocPrefsInt, /* enum iCollapse */
    /* Style */
    justify_DocPrefsInt, /* enum iJustify */
    /* Theme */
    docThemeDark_DocPrefsInt,  /* enum iGmDocumentTheme */
    docThemeLight_DocPrefsInt, /* enum iGmDocumentTheme */
    pageThemeDark_DocPrefsInt, /* enum iGmDocumentTheme; none: same as `docThemeDark` */
    pageThemeLight_DocPrefsInt,
    imageStyle_DocPrefsInt,    /* enum iImageStyle */
    /* Content */
    ansiEscapes_DocPrefsInt, /* enum iAnsiFlag */
    /* Ungrouped */
    zoomPercent_DocPrefsInt,
    max_DocPrefsInt
};

enum iDocPrefsFloat {
    lineSpacing_DocPrefsFloat,
    saturation_DocPrefsFloat,
    max_DocPrefsFloat
};

enum iDocPrefsString {
    /* Typography */
    headingFont_DocPrefsString,
    bodyFont_DocPrefsString,
    monospaceFont_DocPrefsString,
    monospaceDocumentFont_DocPrefsString,
    /* Ungrouped */
    paletteSeed_DocPrefsString,
    max_DocPrefsString
};

/* A sparse rule and a resolved snapshot share this layout. Everything is defined in
   a resolved snapshot. */
struct Impl_DocPrefs {
    union {
        iOptBool bools[max_DocPrefsBool];
        struct {
            iOptBool monospaceDocument;
            iOptBool bigFirstParagraph;
            iOptBool plainTextWrap;
            iOptBool expandToLongLines;
            iOptBool centerShortDocs;
            iOptBool pageView; /* paginated instead of scrolling */
            iOptBool italicQuote;
            iOptBool quoteIcon;
            iOptBool sideIcon;
            iOptBool boldLinkVisited;
            iOptBool boldLinkDark;
            iOptBool boldLinkLight;
            iOptBool geminiStyledGopher;
            iOptBool markdownAsSource;
        };
    };
    union {
        iOptInt ints[max_DocPrefsInt];
        struct {
            iOptInt lineWidth;
            iOptInt tabWidth;
            iOptInt collapsePre;
            iOptInt justify;
            iOptInt docThemeDark;
            iOptInt docThemeLight;
            iOptInt pageThemeDark;
            iOptInt pageThemeLight;
            iOptInt imageStyle;
            iOptInt ansiEscapes;
            iOptInt zoomPercent;
        };
    };
    union {
        iOptFloat floats[max_DocPrefsFloat];
        struct {
            iOptFloat lineSpacing;
            iOptFloat saturation;
        };
    };
    union {
        iString *strings[max_DocPrefsString]; /* NULL = undefined */
        struct {
            iString *headingFont;
            iString *bodyFont;
            iString *monospaceFont;
            iString *monospaceDocumentFont;
            iString *paletteSeed; /* empty: derived from the host */
        };
    };
};

iDeclareTypeConstruction(DocPrefs)

void        initCopy_DocPrefs   (iDocPrefs *, const iDocPrefs *other);
void        set_DocPrefs        (iDocPrefs *, const iDocPrefs *other);
iBool       equal_DocPrefs      (const iDocPrefs *, const iDocPrefs *other);
uint32_t    hash_DocPrefs       (const iDocPrefs *);

const iDocPrefs *   defaults_DocPrefs   (void); /* built-in; everything defined */

iLocalDef enum iGmDocumentTheme docTheme_DocPrefs(const iDocPrefs *d, iBool isDark) {
    if (d->pageView.value && !isTerminal_Platform()) {
        /* Paginated documents may have themes of their own. */
        const int pageTheme = isDark ? d->pageThemeDark.value : d->pageThemeLight.value;
        if (pageTheme != none_GmDocumentTheme) {
            return pageTheme;
        }
    }
    return isDark ? d->docThemeDark.value : d->docThemeLight.value;
}

iLocalDef iBool isJustified_DocPrefs(const iDocPrefs *d, iBool isPaged) {
    return d->justify.value == always_Justify ||
           (d->justify.value == pagesOnly_Justify && isPaged);
}

/*----------------------------------------------------------------------------------------------*/

enum iDocPrefsSpecFlags {
    relayout_DocPrefsSpecFlag = iBit(1), /* layout must be redone after a change */
    reparse_DocPrefsSpecFlag  = iBit(2), /* source must be parsed again after a change */
    font_DocPrefsSpecFlag     = iBit(3), /* affects the fonts set up for text rendering */
    palette_DocPrefsSpecFlag  = iBit(4), /* affects document colors */
    manual_DocPrefsSpecFlag   = iBit(5), /* value is set by a dedicated command handler */
};

struct Impl_DocPrefsSpec {
    enum iDocPrefsType  type;
    int                 index; /* in the array of the type */
    enum iDocPrefsGroup group;
    const char *        key;   /* in the TOML files */
    const char *        id;    /* widget ID; the change command is "<id>.changed" */
    const char *        cmd;   /* command that sets the value; NULL means "<id>.changed" */
    uint32_t            flags;
};

size_t                  numSpecs_DocPrefs       (void);
const iDocPrefsSpec *   spec_DocPrefs           (size_t index);
const iDocPrefsSpec *   findSpec_DocPrefs       (enum iDocPrefsType type, int index);
const iDocPrefsSpec *   findCommandSpec_DocPrefs(iRangecc cmdName); /* NULL if not found */
const char *            groupId_DocPrefs        (enum iDocPrefsGroup);
enum iDocPrefsGroup     findGroup_DocPrefs      (iRangecc id); /* none_DocPrefsGroup if not found */

iBool       isDefined_DocPrefs      (const iDocPrefs *, const iDocPrefsSpec *);
void        undefine_DocPrefs       (iDocPrefs *, const iDocPrefsSpec *);
iBool       equalField_DocPrefs     (const iDocPrefs *, const iDocPrefs *other, const iDocPrefsSpec *);
void        copyField_DocPrefs      (iDocPrefs *, const iDocPrefs *other, const iDocPrefsSpec *);
uint32_t    diff_DocPrefs           (const iDocPrefs *, const iDocPrefs *other); /* spec flags of changed fields */

iBool       setBool_DocPrefs        (iDocPrefs *, enum iDocPrefsBool, iBool value); /* true if changed */
iBool       setInt_DocPrefs         (iDocPrefs *, enum iDocPrefsInt, int value);
iBool       setFloat_DocPrefs       (iDocPrefs *, enum iDocPrefsFloat, float value);
iBool       setString_DocPrefs      (iDocPrefs *, enum iDocPrefsString, const iString *value);

/*----------------------------------------------------------------------------------------------*/

/* Managed rules have fixed orders; lower wins. */
enum iScopeRuleOrder {
    site_ScopeRuleOrder          = 100,
    scheme_ScopeRuleOrder        = 500,
    builtInScheme_ScopeRuleOrder = 510,
    base_ScopeRuleOrder          = 900,
};

enum iScopeRuleKind {
    base_ScopeRuleKind,   /* "All protocols"; prefs/document.ini */
    scheme_ScopeRuleKind, /* keyed by scheme; prefs/protocols.ini */
    site_ScopeRuleKind,   /* keyed by urlRoot_String(); prefs/sitespec.ini */
    custom_ScopeRuleKind, /* regex; prefs/custom.ini, never written by the app */
};

enum iScopeMatchMode {
    both_ScopeMatchMode,   /* URI and MIME patterns must both match */
    either_ScopeMatchMode,
};

struct Impl_ScopeRule {
    enum iScopeRuleKind  kind;
    iString              key;           /* scheme or site root */
    iString              uriPattern;    /* custom only; empty = match all */
    iString              mimePattern;   /* custom only; empty = match all */
    enum iScopeMatchMode matchMode;     /* custom only */
    int                  order;         /* lower wins */
    uint32_t             enabledGroups; /* scheme and site only; bits indexed by iDocPrefsGroup */
    iDocPrefs            prefs;         /* sparse */
    iRegExp *            uriRegExp;
    iRegExp *            mimeRegExp;
};

iDeclareTypeConstructionArgs(ScopeRule, enum iScopeRuleKind kind)

iBool   parseKey_ScopeRule      (iScopeRule *, const iString *key, const iTomlValue *value);
void    serialize_ScopeRule     (const iScopeRule *, iString *out);
iBool   isGroupEnabled_ScopeRule(const iScopeRule *, enum iDocPrefsGroup);
iBool   hasEffect_ScopeRule     (const iScopeRule *);
void    clear_ScopeRule         (iScopeRule *); /* forgets all groups and values */

/*----------------------------------------------------------------------------------------------*/

/* Selects the scheme and site rules. Normally comes from the URL, but can be overridden. */
struct Impl_ScopeKey {
    iString scheme; /* normalized */
    iString site;
};

iDeclareTypeConstruction(ScopeKey)

void    initUrl_ScopeKey    (iScopeKey *, const iString *url); /* scheme and urlRoot_String() */
void    initScope_ScopeKey  (iScopeKey *, const char *scope); /* for a scope string */
void    initCopy_ScopeKey   (iScopeKey *, const iScopeKey *other);
void    set_ScopeKey        (iScopeKey *, const iScopeKey *other);
void    setScheme_ScopeKey  (iScopeKey *, iRangecc scheme); /* normalizes */

/*----------------------------------------------------------------------------------------------*/

/* The rule set. Site rules are stored in SiteSpec. */

void    init_ScopeRules     (const char *saveDir);
void    deinit_ScopeRules   (void);

const iString *prefsDir_ScopeRules      (void);

/* Scope strings: "base", "scheme:<name>", "root:<urlRoot>". */
const iScopeRule *  constRule_ScopeRules(const char *scope); /* NULL if the rule does not exist */

/* Rules are created when needed. Changes are saved later. */
iBool   setBool_ScopeRules          (const char *scope, enum iDocPrefsBool, iBool value);
iBool   setInt_ScopeRules           (const char *scope, enum iDocPrefsInt, int value);
iBool   setFloat_ScopeRules         (const char *scope, enum iDocPrefsFloat, float value);
iBool   setString_ScopeRules        (const char *scope, enum iDocPrefsString, const iString *value);
iBool   setGroupEnabled_ScopeRules  (const char *scope, enum iDocPrefsGroup, iBool enable);
void    reset_ScopeRules            (const char *scope);
void    fillMissing_ScopeRules      (iScopeRule *); /* undefined fields of enabled groups */

void    serialize_ScopeRules        (enum iScopeRuleKind, iString *out); /* base or scheme */
void    deserialize_ScopeRules      (enum iScopeRuleKind, const iString *src, enum iImportMethod);
void    reloadCustom_ScopeRules     (void);
const iString *customPath_ScopeRules(void);

void    save_ScopeRules             (enum iScopeRuleKind);
void    saveDirty_ScopeRules        (void);

/* `resolved` must be initialized. */
void    resolve_DocPrefs            (iDocPrefs *resolved, const iScopeKey *key, const iString *url,
                                     const iString *mimeType);
void    resolveUrl_DocPrefs         (iDocPrefs *resolved, const iString *url, const iString *mimeType);
void    resolveInherited_DocPrefs   (iDocPrefs *resolved, const iScopeKey *key, const char *scope,
                                     enum iDocPrefsGroup group);
