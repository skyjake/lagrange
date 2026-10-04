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

#include "lagrange/docprefs.h"
#include "lagrange/core.h"
#include "lagrange/gmutil.h"
#include "lagrange/sitespec.h"

#include <assert.h>
#include <the_Foundation/file.h>
#include <the_Foundation/mutex.h>
#include <the_Foundation/path.h>
#include <the_Foundation/ptrarray.h>

#define assertBoolLayout_(member) \
    _Static_assert(offsetof(iDocPrefs, member) == \
                       offsetof(iDocPrefs, bools) + sizeof(iOptBool) * member##_DocPrefsBool, \
                   "memory layout mismatch (needs struct packing?)")
#define assertIntLayout_(member) \
    _Static_assert(offsetof(iDocPrefs, member) == \
                       offsetof(iDocPrefs, ints) + sizeof(iOptInt) * member##_DocPrefsInt, \
                   "memory layout mismatch (needs struct packing?)")
#define assertFloatLayout_(member) \
    _Static_assert(offsetof(iDocPrefs, member) == \
                       offsetof(iDocPrefs, floats) + sizeof(iOptFloat) * member##_DocPrefsFloat, \
                   "memory layout mismatch (needs struct packing?)")
#define assertStringLayout_(member) \
    _Static_assert(offsetof(iDocPrefs, member) == \
                       offsetof(iDocPrefs, strings) + sizeof(iString *) * member##_DocPrefsString, \
                   "memory layout mismatch (needs struct packing?)")

assertBoolLayout_(monospaceDocument);
assertBoolLayout_(pageView);
assertBoolLayout_(italicQuote);
assertBoolLayout_(markdownAsSource);
assertIntLayout_(lineWidth);
assertIntLayout_(zoomPercent);
assertFloatLayout_(lineSpacing);
assertFloatLayout_(saturation);
assertStringLayout_(headingFont);
assertStringLayout_(paletteSeed);

#define B_(name) bool_DocPrefsType,   name##_DocPrefsBool
#define I_(name) int_DocPrefsType,    name##_DocPrefsInt
#define F_(name) float_DocPrefsType,  name##_DocPrefsFloat
#define S_(name) string_DocPrefsType, name##_DocPrefsString

static const iDocPrefsSpec specs_[] = {
    /* Typography */
    { S_(headingFont), typography_DocPrefsGroup, "headingFont", "prefs.font.heading", "font.set",
      font_DocPrefsSpecFlag | manual_DocPrefsSpecFlag },
    { S_(bodyFont), typography_DocPrefsGroup, "bodyFont", "prefs.font.body", "font.set",
      font_DocPrefsSpecFlag | manual_DocPrefsSpecFlag },
    { S_(monospaceFont), typography_DocPrefsGroup, "monospaceFont", "prefs.font.mono", "font.set",
      font_DocPrefsSpecFlag | manual_DocPrefsSpecFlag },
    { S_(monospaceDocumentFont), typography_DocPrefsGroup, "monospaceDocumentFont",
      "prefs.font.monodoc", "font.set", font_DocPrefsSpecFlag | manual_DocPrefsSpecFlag },
    { B_(monospaceDocument), typography_DocPrefsGroup, "monospaceDocument", "prefs.mono.doc", NULL,
      reparse_DocPrefsSpecFlag },
    /* Layout */
    { I_(lineWidth), layout_DocPrefsGroup, "lineWidth", "prefs.linewidth", "linewidth.set",
      reparse_DocPrefsSpecFlag },
    { F_(lineSpacing), layout_DocPrefsGroup, "lineSpacing", "prefs.linespacing", "linespacing.set",
      reparse_DocPrefsSpecFlag },
    { I_(tabWidth), layout_DocPrefsGroup, "tabWidth", "prefs.tabwidth", "tabwidth.set",
      reparse_DocPrefsSpecFlag }, /* spaces need renormalizing */
    { B_(justifyParagraph), style_DocPrefsGroup, "justifyParagraph", "prefs.justify", NULL,
      relayout_DocPrefsSpecFlag },
    { B_(bigFirstParagraph), style_DocPrefsGroup, "bigFirstParagraph", "prefs.biglede", NULL,
      relayout_DocPrefsSpecFlag },
    { B_(plainTextWrap), layout_DocPrefsGroup, "plainTextWrap", "prefs.plaintext.wrap", NULL,
      relayout_DocPrefsSpecFlag },
    { B_(expandToLongLines), layout_DocPrefsGroup, "expandToLongLines", "prefs.expandline", NULL,
      relayout_DocPrefsSpecFlag },
    { B_(centerShortDocs), layout_DocPrefsGroup, "centerShortDocs", "prefs.centershort" },
    { B_(pageView), layout_DocPrefsGroup, "pageView", "prefs.pageview", NULL,
      relayout_DocPrefsSpecFlag },
    { I_(collapsePre), layout_DocPrefsGroup, "collapsePre", "prefs.collapsepre", "collapsepre.set" },
    /* Style */
    { B_(italicQuote), style_DocPrefsGroup, "italicQuote", "prefs.quote.italic", NULL,
      relayout_DocPrefsSpecFlag },
    { B_(quoteIcon), style_DocPrefsGroup, "quoteIcon", "prefs.quoteicon", "quoteicon.set",
      reparse_DocPrefsSpecFlag },
    { B_(sideIcon), style_DocPrefsGroup, "sideIcon", "prefs.sideicon" },
    { B_(boldLinkVisited), style_DocPrefsGroup, "boldLinkVisited", "prefs.boldlink.visited", NULL,
      relayout_DocPrefsSpecFlag },
    { B_(boldLinkDark), style_DocPrefsGroup, "boldLinkDark", "prefs.boldlink.dark", NULL,
      relayout_DocPrefsSpecFlag },
    { B_(boldLinkLight), style_DocPrefsGroup, "boldLinkLight", "prefs.boldlink.light", NULL,
      relayout_DocPrefsSpecFlag },
    /* Theme */
    { I_(docThemeDark), theme_DocPrefsGroup, "docThemeDark", "prefs.doctheme.dark",
      "doctheme.dark.set", palette_DocPrefsSpecFlag },
    { I_(docThemeLight), theme_DocPrefsGroup, "docThemeLight", "prefs.doctheme.light",
      "doctheme.light.set", palette_DocPrefsSpecFlag },
    { F_(saturation), theme_DocPrefsGroup, "saturation", "prefs.saturation", "saturation.set",
      palette_DocPrefsSpecFlag | manual_DocPrefsSpecFlag }, /* argument is a percentage */
    { I_(imageStyle), theme_DocPrefsGroup, "imageStyle", "prefs.imagestyle", "imagestyle.set" },
    /* Content */
    { I_(ansiEscapes), content_DocPrefsGroup, "ansiEscapes", NULL, "ansiescape",
      reparse_DocPrefsSpecFlag | manual_DocPrefsSpecFlag }, /* separate toggle for each flag */
    { B_(geminiStyledGopher), content_DocPrefsGroup, "geminiStyledGopher", "prefs.gopher.gemstyle" },
    { B_(markdownAsSource), content_DocPrefsGroup, "markdownAsSource", "prefs.markdown.viewsource" },
    /* Ungrouped */
    { S_(paletteSeed), none_DocPrefsGroup, "paletteSeed", NULL, NULL,
      palette_DocPrefsSpecFlag | manual_DocPrefsSpecFlag },
    { I_(zoomPercent), none_DocPrefsGroup, "zoomPercent", NULL, "zoom.set",
      font_DocPrefsSpecFlag | manual_DocPrefsSpecFlag },
};

static const char *groupIds_[max_DocPrefsGroup] = {
    "", "typography", "layout", "style", "theme", "content",
};

size_t numSpecs_DocPrefs(void) {
    return iElemCount(specs_);
}

const iDocPrefsSpec *spec_DocPrefs(size_t index) {
    iAssert(index < iElemCount(specs_));
    return &specs_[index];
}

const iDocPrefsSpec *findSpec_DocPrefs(enum iDocPrefsType type, int index) {
    iForIndices(i, specs_) {
        if (specs_[i].type == type && specs_[i].index == index) {
            return &specs_[i];
        }
    }
    iAssert(iFalse);
    return NULL;
}

static iBool isChangeCommand_(const iDocPrefsSpec *d, iRangecc cmdName) {
    static const char *suffix = ".changed";
    const size_t       idLen  = d->id ? strlen(d->id) : 0;
    return idLen && size_Range(&cmdName) == idLen + strlen(suffix) &&
           !iCmpStrN(cmdName.start, d->id, idLen) &&
           !iCmpStrN(cmdName.start + idLen, suffix, strlen(suffix));
}

const iDocPrefsSpec *findCommandSpec_DocPrefs(iRangecc cmdName) {
    iForIndices(i, specs_) {
        const iDocPrefsSpec *spec = &specs_[i];
        if (spec->flags & manual_DocPrefsSpecFlag) {
            continue;
        }
        if ((spec->cmd && equal_Rangecc(cmdName, spec->cmd)) ||
            (spec->type == bool_DocPrefsType && isChangeCommand_(spec, cmdName))) {
            return spec;
        }
    }
    return NULL;
}

static const iDocPrefsSpec *findKeySpec_(const iString *key) {
    iForIndices(i, specs_) {
        if (!cmp_String(key, specs_[i].key)) {
            return &specs_[i];
        }
    }
    return NULL;
}

const char *groupId_DocPrefs(enum iDocPrefsGroup group) {
    iAssert(group >= 0 && group < max_DocPrefsGroup);
    return groupIds_[group];
}

enum iDocPrefsGroup findGroup_DocPrefs(iRangecc id) {
    for (int i = 1; i < max_DocPrefsGroup; i++) {
        if (equal_Rangecc(id, groupIds_[i])) {
            return i;
        }
    }
    return none_DocPrefsGroup;
}

/*----------------------------------------------------------------------------------------------*/

iDefineTypeConstruction(DocPrefs)

void init_DocPrefs(iDocPrefs *d) {
    iZap(*d);
}

void deinit_DocPrefs(iDocPrefs *d) {
    iForIndices(i, d->strings) {
        delete_String(d->strings[i]);
    }
}

void initCopy_DocPrefs(iDocPrefs *d, const iDocPrefs *other) {
    init_DocPrefs(d);
    set_DocPrefs(d, other);
}

void set_DocPrefs(iDocPrefs *d, const iDocPrefs *other) {
    if (d == other) {
        return;
    }
    memcpy(d->bools, other->bools, sizeof(d->bools));
    memcpy(d->ints, other->ints, sizeof(d->ints));
    memcpy(d->floats, other->floats, sizeof(d->floats));
    iForIndices(i, d->strings) {
        delete_String(d->strings[i]);
        d->strings[i] = other->strings[i] ? copy_String(other->strings[i]) : NULL;
    }
}

iBool isDefined_DocPrefs(const iDocPrefs *d, const iDocPrefsSpec *spec) {
    switch (spec->type) {
        case bool_DocPrefsType:
            return d->bools[spec->index].isDefined;
        case int_DocPrefsType:
            return d->ints[spec->index].isDefined;
        case float_DocPrefsType:
            return d->floats[spec->index].isDefined;
        case string_DocPrefsType:
            return d->strings[spec->index] != NULL;
    }
    return iFalse;
}

void undefine_DocPrefs(iDocPrefs *d, const iDocPrefsSpec *spec) {
    switch (spec->type) {
        case bool_DocPrefsType:
            iZap(d->bools[spec->index]);
            break;
        case int_DocPrefsType:
            iZap(d->ints[spec->index]);
            break;
        case float_DocPrefsType:
            iZap(d->floats[spec->index]);
            break;
        case string_DocPrefsType:
            delete_String(d->strings[spec->index]);
            d->strings[spec->index] = NULL;
            break;
    }
}

iBool equalField_DocPrefs(const iDocPrefs *d, const iDocPrefs *other, const iDocPrefsSpec *spec) {
    const iBool isDef = isDefined_DocPrefs(d, spec);
    if (isDef != isDefined_DocPrefs(other, spec)) {
        return iFalse;
    }
    if (!isDef) {
        return iTrue;
    }
    switch (spec->type) {
        case bool_DocPrefsType:
            return d->bools[spec->index].value == other->bools[spec->index].value;
        case int_DocPrefsType:
            return d->ints[spec->index].value == other->ints[spec->index].value;
        case float_DocPrefsType:
            return d->floats[spec->index].value == other->floats[spec->index].value;
        case string_DocPrefsType:
            return equal_String(d->strings[spec->index], other->strings[spec->index]);
    }
    return iFalse;
}

void copyField_DocPrefs(iDocPrefs *d, const iDocPrefs *other, const iDocPrefsSpec *spec) {
    switch (spec->type) {
        case bool_DocPrefsType:
            d->bools[spec->index] = other->bools[spec->index];
            break;
        case int_DocPrefsType:
            d->ints[spec->index] = other->ints[spec->index];
            break;
        case float_DocPrefsType:
            d->floats[spec->index] = other->floats[spec->index];
            break;
        case string_DocPrefsType:
            if (d != other) {
                delete_String(d->strings[spec->index]);
                d->strings[spec->index] =
                    other->strings[spec->index] ? copy_String(other->strings[spec->index]) : NULL;
            }
            break;
    }
}

uint32_t diff_DocPrefs(const iDocPrefs *d, const iDocPrefs *other) {
    uint32_t flags = 0;
    iForIndices(i, specs_) {
        if (!equalField_DocPrefs(d, other, &specs_[i])) {
            flags |= specs_[i].flags | iBit(32); /* always nonzero when different */
        }
    }
    return flags & ~(uint32_t) manual_DocPrefsSpecFlag;
}

iBool equal_DocPrefs(const iDocPrefs *d, const iDocPrefs *other) {
    iForIndices(i, specs_) {
        if (!equalField_DocPrefs(d, other, &specs_[i])) {
            return iFalse;
        }
    }
    return iTrue;
}

static void appendValue_DocPrefs_(const iDocPrefs *d, const iDocPrefsSpec *spec, iString *out) {
    switch (spec->type) {
        case bool_DocPrefsType:
            appendCStr_String(out, d->bools[spec->index].value ? "true" : "false");
            break;
        case int_DocPrefsType:
            appendFormat_String(out, "%d", d->ints[spec->index].value);
            break;
        case float_DocPrefsType:
            appendFormat_String(out, "%.3f", d->floats[spec->index].value);
            break;
        case string_DocPrefsType:
            appendCStr_String(out, "\"");
            append_String(out, collect_String(quote_String(d->strings[spec->index], iFalse)));
            appendCStr_String(out, "\"");
            break;
    }
}

static void appendKeyValue_DocPrefs_(const iDocPrefs *d, const iDocPrefsSpec *spec, iString *out) {
    appendFormat_String(out, "%s = ", spec->key);
    appendValue_DocPrefs_(d, spec, out);
    appendCStr_String(out, "\n");
}

uint32_t hash_DocPrefs(const iDocPrefs *d) {
    iString str;
    init_String(&str);
    iForIndices(i, specs_) {
        if (isDefined_DocPrefs(d, &specs_[i])) {
            appendKeyValue_DocPrefs_(d, &specs_[i], &str);
        }
    }
    const uint32_t hash = crc32_Block(&str.chars);
    deinit_String(&str);
    return hash;
}

iBool setBool_DocPrefs(iDocPrefs *d, enum iDocPrefsBool id, iBool value) {
    iOptBool *opt = &d->bools[id];
    if (opt->isDefined && opt->value == value) {
        return iFalse;
    }
    *opt = (iOptBool){ value, iTrue };
    return iTrue;
}

iBool setInt_DocPrefs(iDocPrefs *d, enum iDocPrefsInt id, int value) {
    switch (id) {
        case lineWidth_DocPrefsInt:
            value = iMax(20, value);
            break;
        case tabWidth_DocPrefsInt:
            value = iMax(1, value);
            break;
        case zoomPercent_DocPrefsInt:
            value = iClamp(value, 50, 200);
            break;
        case collapsePre_DocPrefsInt:
            value = iClamp(value, never_Collapse, always_Collapse);
            break;
        case docThemeDark_DocPrefsInt:
        case docThemeLight_DocPrefsInt:
            value = iClamp(value, 0, max_GmDocumentTheme - 1);
            break;
        default:
            break;
    }
    iOptInt *opt = &d->ints[id];
    if (opt->isDefined && opt->value == value) {
        return iFalse;
    }
    *opt = (iOptInt){ value, iTrue };
    return iTrue;
}

iBool setFloat_DocPrefs(iDocPrefs *d, enum iDocPrefsFloat id, float value) {
    switch (id) {
        case lineSpacing_DocPrefsFloat:
            value = iMax(0.5f, value);
            break;
        case saturation_DocPrefsFloat:
            value = iClamp(value, 0.0f, 1.0f);
            break;
        default:
            break;
    }
    iOptFloat *opt = &d->floats[id];
    if (opt->isDefined && opt->value == value) {
        return iFalse;
    }
    *opt = (iOptFloat){ value, iTrue };
    return iTrue;
}

iBool setString_DocPrefs(iDocPrefs *d, enum iDocPrefsString id, const iString *value) {
    iString **str = &d->strings[id];
    if (*str && equal_String(*str, value)) {
        return iFalse;
    }
    if (!*str) {
        *str = new_String();
    }
    set_String(*str, value);
    return iTrue;
}

const iDocPrefs *defaults_DocPrefs(void) {
    static iDocPrefs defs_;
    static iBool     isInitialized_ = iFalse;
    if (!isInitialized_) {
        iDocPrefs *d = &defs_;
        isInitialized_ = iTrue;
        init_DocPrefs(d);
        iForIndices(i, d->bools) {
            d->bools[i].isDefined = iTrue;
        }
        iForIndices(i, d->ints) {
            d->ints[i].isDefined = iTrue;
        }
        iForIndices(i, d->floats) {
            d->floats[i].isDefined = iTrue;
        }
        d->headingFont           = newCStr_String("default");
        d->bodyFont              = newCStr_String("default");
        d->monospaceFont         = newCStr_String("iosevka");
        d->monospaceDocumentFont = newCStr_String("iosevka-body");
        d->monospaceDocument.value = iFalse;
        d->lineWidth.value = deviceType_App() == phone_AppDeviceType ? 1000 /* fill */ : 38;
        d->lineSpacing.value        = 1.0f;
        d->tabWidth.value           = 8;
        d->justifyParagraph.value   = iFalse;
        d->bigFirstParagraph.value  = iTrue;
        d->plainTextWrap.value      = iTrue;
        d->expandToLongLines.value  = iTrue;
        d->centerShortDocs.value    = iTrue;
        d->pageView.value           = iFalse;
        d->collapsePre.value        = notByDefault_Collapse;
        d->italicQuote.value        = iTrue;
        d->quoteIcon.value          = iTrue;
        d->sideIcon.value           = iTrue;
        d->boldLinkVisited.value    = iFalse;
        d->boldLinkDark.value       = iTrue;
        d->boldLinkLight.value      = iTrue;
        d->docThemeDark.value       = colorfulDark_GmDocumentTheme;
        d->docThemeLight.value      = white_GmDocumentTheme;
        d->saturation.value         = 1.0f;
        d->imageStyle.value         = original_ImageStyle;
        d->ansiEscapes.value        = allowFg_AnsiFlag;
        d->geminiStyledGopher.value = iFalse;
        d->markdownAsSource.value   = iTrue;
        d->paletteSeed              = new_String();
        d->zoomPercent.value        = 100;
    }
    return &defs_;
}

/*----------------------------------------------------------------------------------------------*/

iDefineTypeConstructionArgs(ScopeRule, (enum iScopeRuleKind kind), kind)

void init_ScopeRule(iScopeRule *d, enum iScopeRuleKind kind) {
    d->kind = kind;
    init_String(&d->key);
    init_String(&d->uriPattern);
    init_String(&d->mimePattern);
    d->matchMode     = both_ScopeMatchMode;
    d->order         = kind == base_ScopeRuleKind     ? base_ScopeRuleOrder
                       : kind == scheme_ScopeRuleKind ? scheme_ScopeRuleOrder
                       : kind == site_ScopeRuleKind   ? site_ScopeRuleOrder
                                                      : -1; /* custom rules must set it */
    d->enabledGroups = 0;
    init_DocPrefs(&d->prefs);
    d->uriRegExp  = NULL;
    d->mimeRegExp = NULL;
}

void deinit_ScopeRule(iScopeRule *d) {
    iRelease(d->mimeRegExp);
    iRelease(d->uriRegExp);
    deinit_DocPrefs(&d->prefs);
    deinit_String(&d->mimePattern);
    deinit_String(&d->uriPattern);
    deinit_String(&d->key);
}

iBool isGroupEnabled_ScopeRule(const iScopeRule *d, enum iDocPrefsGroup group) {
    return (d->enabledGroups & iBit(group)) != 0;
}

void clear_ScopeRule(iScopeRule *d) {
    d->enabledGroups = 0;
    deinit_DocPrefs(&d->prefs);
    init_DocPrefs(&d->prefs);
}

static iBool isManaged_ScopeRule_(const iScopeRule *d) {
    return d->kind == scheme_ScopeRuleKind || d->kind == site_ScopeRuleKind;
}

/* In site and scheme rules, a field counts only if its group is enabled. */
static iBool isActive_ScopeRule_(const iScopeRule *d, const iDocPrefsSpec *spec) {
    if (!isDefined_DocPrefs(&d->prefs, spec)) {
        return iFalse;
    }
    if (isManaged_ScopeRule_(d) && spec->group != none_DocPrefsGroup) {
        return isGroupEnabled_ScopeRule(d, spec->group);
    }
    return iTrue;
}

iBool hasEffect_ScopeRule(const iScopeRule *d) {
    iForIndices(i, specs_) {
        if (isActive_ScopeRule_(d, &specs_[i])) {
            if (specs_[i].type == string_DocPrefsType && specs_[i].group == none_DocPrefsGroup &&
                isEmpty_String(d->prefs.strings[specs_[i].index])) {
                continue; /* e.g., an empty palette seed */
            }
            return iTrue;
        }
    }
    return iFalse;
}

static void parseEnabledGroups_ScopeRule_(iScopeRule *d, const iString *ids) {
    d->enabledGroups = 0;
    iRangecc seg = iNullRange;
    while (nextSplit_Rangecc(range_String(ids), " ", &seg)) {
        const enum iDocPrefsGroup group = findGroup_DocPrefs(seg);
        if (group != none_DocPrefsGroup) {
            d->enabledGroups |= iBit(group);
        }
    }
}

iBool parseKey_ScopeRule(iScopeRule *d, const iString *key, const iTomlValue *value) {
    if (isManaged_ScopeRule_(d) && !cmp_String(key, "enabled")) {
        if (value->type == string_TomlType) {
            parseEnabledGroups_ScopeRule_(d, value->value.string);
        }
        return iTrue;
    }
    if (d->kind == custom_ScopeRuleKind) {
        if (!cmp_String(key, "uri") || !cmp_String(key, "mime")) {
            if (value->type == string_TomlType) {
                set_String(!cmp_String(key, "uri") ? &d->uriPattern : &d->mimePattern,
                           value->value.string);
            }
            return iTrue;
        }
        if (!cmp_String(key, "matchMode")) {
            if (value->type == string_TomlType) {
                d->matchMode = !cmpCase_String(value->value.string, "either") ? either_ScopeMatchMode
                                                                              : both_ScopeMatchMode;
            }
            return iTrue;
        }
        if (!cmp_String(key, "order")) {
            d->order = (int) number_TomlValue(value);
            return iTrue;
        }
    }
    const iDocPrefsSpec *spec = findKeySpec_(key);
    if (!spec) {
        return iFalse;
    }
    switch (spec->type) {
        case bool_DocPrefsType:
            if (value->type != string_TomlType) {
                setBool_DocPrefs(&d->prefs, spec->index, number_TomlValue(value) != 0);
            }
            break;
        case int_DocPrefsType:
            if (value->type != string_TomlType) {
                setInt_DocPrefs(&d->prefs, spec->index, (int) number_TomlValue(value));
            }
            break;
        case float_DocPrefsType:
            if (value->type != string_TomlType) {
                setFloat_DocPrefs(&d->prefs, spec->index, (float) number_TomlValue(value));
            }
            break;
        case string_DocPrefsType:
            if (value->type == string_TomlType) {
                setString_DocPrefs(&d->prefs, spec->index, value->value.string);
            }
            break;
    }
    return iTrue;
}

void serialize_ScopeRule(const iScopeRule *d, iString *out) {
    if (isManaged_ScopeRule_(d) && d->enabledGroups) {
        appendCStr_String(out, "enabled = \"");
        iBool isFirst = iTrue;
        for (int i = 1; i < max_DocPrefsGroup; i++) {
            if (isGroupEnabled_ScopeRule(d, i)) {
                appendFormat_String(out, "%s%s", isFirst ? "" : " ", groupIds_[i]);
                isFirst = iFalse;
            }
        }
        appendCStr_String(out, "\"\n");
    }
    iForIndices(i, specs_) {
        const iDocPrefsSpec *spec = &specs_[i];
        if (!isDefined_DocPrefs(&d->prefs, spec)) {
            continue;
        }
        /* The base layer only stores values that differ from the built-in defaults. */
        if (d->kind == base_ScopeRuleKind &&
            equalField_DocPrefs(&d->prefs, defaults_DocPrefs(), spec)) {
            continue;
        }
        if (spec->type == string_DocPrefsType && spec->group == none_DocPrefsGroup &&
            isEmpty_String(d->prefs.strings[spec->index])) {
            continue;
        }
        iBeginCollect();
        appendKeyValue_DocPrefs_(&d->prefs, spec, out);
        iEndCollect();
    }
}

/* Note: A malformed pattern compiles into a RegExp that never matches. */
static void compile_ScopeRule_(iScopeRule *d) {
    iAssert(d->kind == custom_ScopeRuleKind);
    if (!isEmpty_String(&d->uriPattern)) {
        d->uriRegExp = new_RegExp(cstr_String(&d->uriPattern), caseInsensitive_RegExpOption);
    }
    if (!isEmpty_String(&d->mimePattern)) {
        d->mimeRegExp = new_RegExp(cstr_String(&d->mimePattern), caseInsensitive_RegExpOption);
    }
}

static iBool matches_ScopeRule_(const iScopeRule *d, const iString *url, const iString *mime) {
    if (!d->uriRegExp && !d->mimeRegExp) {
        return iTrue;
    }
    iRegExpMatch m;
    init_RegExpMatch(&m);
    const iBool uriOk = d->uriRegExp && url && matchString_RegExp(d->uriRegExp, url, &m);
    init_RegExpMatch(&m);
    const iBool mimeOk = d->mimeRegExp && mime && matchString_RegExp(d->mimeRegExp, mime, &m);
    if (d->matchMode == either_ScopeMatchMode) {
        return uriOk || mimeOk;
    }
    return (!d->uriRegExp || uriOk) && (!d->mimeRegExp || mimeOk);
}

/*----------------------------------------------------------------------------------------------*/

iDefineTypeConstruction(ScopeKey)

void init_ScopeKey(iScopeKey *d) {
    init_String(&d->scheme);
    init_String(&d->site);
}

void deinit_ScopeKey(iScopeKey *d) {
    deinit_String(&d->site);
    deinit_String(&d->scheme);
}

void setScheme_ScopeKey(iScopeKey *d, iRangecc scheme) {
    setRange_String(&d->scheme, scheme);
    set_String(&d->scheme, collect_String(lower_String(&d->scheme)));
    if (isGopherScheme_Rangecc(range_String(&d->scheme))) {
        setCStr_String(&d->scheme, "gopher");
    }
}

void initUrl_ScopeKey(iScopeKey *d, const iString *url) {
    init_ScopeKey(d);
    setScheme_ScopeKey(d, urlScheme_String(url));
    setRange_String(&d->site, urlRoot_String(url));
}

void initCopy_ScopeKey(iScopeKey *d, const iScopeKey *other) {
    initCopy_String(&d->scheme, &other->scheme);
    initCopy_String(&d->site, &other->site);
}

void set_ScopeKey(iScopeKey *d, const iScopeKey *other) {
    set_String(&d->scheme, &other->scheme);
    set_String(&d->site, &other->site);
}

/*----------------------------------------------------------------------------------------------*/

iDeclareType(ScopeRules)

struct Impl_ScopeRules {
    iMutex      mtx; /* requests may resolve in background threads */
    iString     saveDir;
    iScopeRule  base;
    iPtrArray   schemes; /* owned */
    iPtrArray   builtIn; /* owned; compiled-in scheme defaults */
    iPtrArray   custom;  /* owned; sorted by order */
    iBool       isDirty[custom_ScopeRuleKind];
    iScopeRule *loadRule;
    enum iImportMethod loadMethod;
};

static iScopeRules rules_;

static const char *baseFileName_ScopeRules_    = "document.ini";
static const char *schemesFileName_ScopeRules_ = "protocols.ini";
static const char *customFileName_ScopeRules_  = "custom.ini";

static iScopeRule *findIn_ScopeRules_(const iPtrArray *rules, const iString *key) {
    iConstForEach(PtrArray, i, rules) {
        const iScopeRule *rule = i.ptr;
        if (equal_String(&rule->key, key)) {
            return iConstCast(iScopeRule *, rule);
        }
    }
    return NULL;
}

static iScopeRule *schemeRule_ScopeRules_(iScopeRules *d, const iString *scheme, iBool create) {
    iScopeRule *rule = findIn_ScopeRules_(&d->schemes, scheme);
    if (!rule && create) {
        rule = new_ScopeRule(scheme_ScopeRuleKind);
        set_String(&rule->key, scheme);
        pushBack_PtrArray(&d->schemes, rule);
    }
    return rule;
}

static void addBuiltIn_ScopeRules_(iScopeRules *d, const char *scheme, enum iDocPrefsBool id,
                                   iBool value) {
    const iString *key = collectNewCStr_String(scheme);
    iScopeRule *rule = findIn_ScopeRules_(&d->builtIn, key);
    if (!rule) {
        rule = new_ScopeRule(scheme_ScopeRuleKind);
        rule->order = builtInScheme_ScopeRuleOrder;
        set_String(&rule->key, key);
        pushBack_PtrArray(&d->builtIn, rule);
    }
    setBool_DocPrefs(&rule->prefs, id, value);
    rule->enabledGroups |= iBit(findSpec_DocPrefs(bool_DocPrefsType, id)->group);
}

/*----------------------------------------------------------------------------------------------*/

static enum iScopeRuleKind parseScope_(const char *scope, iString *key_out) {
    clear_String(key_out);
    if (startsWith_CStr(scope, "scheme:")) {
        iScopeKey key;
        init_ScopeKey(&key);
        setScheme_ScopeKey(&key, range_CStr(scope + 7));
        set_String(key_out, &key.scheme);
        deinit_ScopeKey(&key);
        return scheme_ScopeRuleKind;
    }
    if (startsWith_CStr(scope, "root:")) {
        setCStr_String(key_out, scope + 5);
        return site_ScopeRuleKind;
    }
    return base_ScopeRuleKind;
}

static void initScope_ScopeKey_(iScopeKey *d, enum iScopeRuleKind kind, const iString *ruleKey) {
    if (kind == site_ScopeRuleKind) {
        initUrl_ScopeKey(d, ruleKey);
    }
    else {
        init_ScopeKey(d);
        if (kind == scheme_ScopeRuleKind) {
            set_String(&d->scheme, ruleKey);
        }
    }
}

void initScope_ScopeKey(iScopeKey *d, const char *scope) {
    iString ruleKey;
    init_String(&ruleKey);
    const enum iScopeRuleKind kind = parseScope_(scope, &ruleKey);
    initScope_ScopeKey_(d, kind, &ruleKey);
    deinit_String(&ruleKey);
}

static iScopeRule *rule_ScopeRules_(iScopeRules *d, const char *scope, iBool create) {
    iString key;
    init_String(&key);
    iScopeRule *rule = NULL;
    switch (parseScope_(scope, &key)) {
        case base_ScopeRuleKind:
            rule = &d->base;
            break;
        case scheme_ScopeRuleKind:
            rule = schemeRule_ScopeRules_(d, &key, create);
            break;
        case site_ScopeRuleKind:
            rule = create ? rule_SiteSpec(&key) : iConstCast(iScopeRule *, constRule_SiteSpec(&key));
            break;
        default:
            break;
    }
    deinit_String(&key);
    return rule;
}

static void resolve_ScopeRules_(const iScopeRules *d, iDocPrefs *resolved, const iScopeKey *key,
                                const iString *url, const iString *mimeType,
                                const iScopeRule *skipRule, enum iDocPrefsGroup skipGroup) {
    iPtrArray chain;
    init_PtrArray(&chain);
    const iScopeRule *managed[] = {
        !isEmpty_String(&key->site) ? constRule_SiteSpec(&key->site) : NULL,
        findIn_ScopeRules_(&d->schemes, &key->scheme),
        findIn_ScopeRules_(&d->builtIn, &key->scheme),
        &d->base,
    };
    static const int managedOrder[] = {
        site_ScopeRuleOrder, scheme_ScopeRuleOrder, builtInScheme_ScopeRuleOrder, base_ScopeRuleOrder,
    };
    iConstForEach(PtrArray, c, &d->custom) {
        const iScopeRule *custom = c.ptr;
        iForIndices(i, managed) {
            if (managed[i] && managedOrder[i] <= custom->order) {
                pushBack_PtrArray(&chain, managed[i]);
                managed[i] = NULL;
            }
        }
        if (matches_ScopeRule_(custom, url, mimeType)) {
            pushBack_PtrArray(&chain, custom);
        }
    }
    iForIndices(i, managed) {
        if (managed[i]) {
            pushBack_PtrArray(&chain, managed[i]);
        }
    }
    /* The first defined value of each field wins. */
    iForIndices(i, specs_) {
        const iDocPrefsSpec *spec = &specs_[i];
        const iDocPrefs *src = defaults_DocPrefs();
        iConstForEach(PtrArray, r, &chain) {
            const iScopeRule *rule = r.ptr;
            if (rule == skipRule && spec->group == skipGroup) {
                continue;
            }
            if (isActive_ScopeRule_(rule, spec)) {
                src = &rule->prefs;
                break;
            }
        }
        copyField_DocPrefs(resolved, src, spec);
    }
    deinit_PtrArray(&chain);
}

void resolve_DocPrefs(iDocPrefs *resolved, const iScopeKey *key, const iString *url,
                      const iString *mimeType) {
    iScopeRules *d = &rules_;
    lock_Mutex(&d->mtx);
    resolve_ScopeRules_(d, resolved, key, url, mimeType, NULL, none_DocPrefsGroup);
    unlock_Mutex(&d->mtx);
}

void resolveUrl_DocPrefs(iDocPrefs *resolved, const iString *url, const iString *mimeType) {
    iScopeKey key;
    initUrl_ScopeKey(&key, url);
    resolve_DocPrefs(resolved, &key, url, mimeType);
    deinit_ScopeKey(&key);
}

static void resolveInherited_ScopeRules_(const iScopeRules *d, iDocPrefs *resolved,
                                         const iScopeKey *key, const iScopeRule *skipRule,
                                         enum iDocPrefsGroup group) {
    resolve_ScopeRules_(d, resolved, key, !isEmpty_String(&key->site) ? &key->site : NULL, NULL,
                        skipRule != &d->base ? skipRule : NULL, group);
}

void resolveInherited_DocPrefs(iDocPrefs *resolved, const iScopeKey *key, const char *scope,
                               enum iDocPrefsGroup group) {
    iScopeRules *d = &rules_;
    lock_Mutex(&d->mtx);
    resolveInherited_ScopeRules_(d, resolved, key, rule_ScopeRules_(d, scope, iFalse), group);
    unlock_Mutex(&d->mtx);
}

static void fillMissing_ScopeRules_(iScopeRules *d, iScopeRule *rule) {
    if (!isManaged_ScopeRule_(rule) || !rule->enabledGroups) {
        return;
    }
    iScopeKey key;
    initScope_ScopeKey_(&key, rule->kind, &rule->key);
    for (int group = 1; group < max_DocPrefsGroup; group++) {
        if (!isGroupEnabled_ScopeRule(rule, group)) {
            continue;
        }
        iDocPrefs inherited;
        init_DocPrefs(&inherited);
        iBool isResolved = iFalse;
        iForIndices(i, specs_) {
            const iDocPrefsSpec *spec = &specs_[i];
            if (spec->group == group && !isDefined_DocPrefs(&rule->prefs, spec)) {
                if (!isResolved) {
                    resolveInherited_ScopeRules_(d, &inherited, &key, rule, group);
                    isResolved = iTrue;
                }
                copyField_DocPrefs(&rule->prefs, &inherited, spec);
            }
        }
        deinit_DocPrefs(&inherited);
    }
    deinit_ScopeKey(&key);
}

void fillMissing_ScopeRules(iScopeRule *rule) {
    iScopeRules *d = &rules_;
    lock_Mutex(&d->mtx);
    fillMissing_ScopeRules_(d, rule);
    unlock_Mutex(&d->mtx);
}

const iScopeRule *constRule_ScopeRules(const char *scope) {
    return rule_ScopeRules_(&rules_, scope, iFalse);
}

static void markDirty_ScopeRules_(iScopeRules *d, const iScopeRule *rule) {
    if (rule->kind < custom_ScopeRuleKind) {
        d->isDirty[rule->kind] = iTrue;
    }
}

iBool setGroupEnabled_ScopeRules(const char *scope, enum iDocPrefsGroup group, iBool enable) {
    iScopeRules *d = &rules_;
    iBool wasChanged = iFalse;
    lock_Mutex(&d->mtx);
    iScopeRule *rule = rule_ScopeRules_(d, scope, enable);
    if (rule && isManaged_ScopeRule_(rule) && group != none_DocPrefsGroup &&
        isGroupEnabled_ScopeRule(rule, group) != enable) {
        iChangeFlags(rule->enabledGroups, iBit(group), enable);
        /* Starts out with the inherited values so nothing changes visibly. */
        fillMissing_ScopeRules_(d, rule);
        markDirty_ScopeRules_(d, rule);
        wasChanged = iTrue;
    }
    unlock_Mutex(&d->mtx);
    return wasChanged;
}

#define iDefineScopeRulesSetter_(typeName, valueType) \
    iBool set##typeName##_ScopeRules(const char *scope, enum iDocPrefs##typeName id, \
                                     valueType value) { \
        iScopeRules *d = &rules_; \
        lock_Mutex(&d->mtx); \
        iScopeRule *rule = rule_ScopeRules_(d, scope, iTrue); \
        const iBool wasChanged = set##typeName##_DocPrefs(&rule->prefs, id, value); \
        if (wasChanged) { \
            markDirty_ScopeRules_(d, rule); \
        } \
        unlock_Mutex(&d->mtx); \
        return wasChanged; \
    }

iDefineScopeRulesSetter_(Bool, iBool)
iDefineScopeRulesSetter_(Int, int)
iDefineScopeRulesSetter_(Float, float)
iDefineScopeRulesSetter_(String, const iString *)

void reset_ScopeRules(const char *scope) {
    iScopeRules *d = &rules_;
    lock_Mutex(&d->mtx);
    iScopeRule *rule = rule_ScopeRules_(d, scope, iFalse);
    if (rule) {
        clear_ScopeRule(rule);
        if (rule == &d->base) {
            set_DocPrefs(&rule->prefs, defaults_DocPrefs());
        }
        markDirty_ScopeRules_(d, rule);
    }
    unlock_Mutex(&d->mtx);
}

/*----------------------------------------------------------------------------------------------*/

static const iString *filePath_ScopeRules_(const iScopeRules *d, const char *name) {
    return collect_String(concatCStr_Path(&d->saveDir, name));
}

static void handleSchemeTable_ScopeRules_(void *context, const iString *table, iBool isStart) {
    iScopeRules *d = context;
    if (isStart) {
        iScopeKey key;
        init_ScopeKey(&key);
        setScheme_ScopeKey(&key, range_String(table));
        const iBool exists = findIn_ScopeRules_(&d->schemes, &key.scheme) != NULL;
        if (exists && d->loadMethod == ifMissing_ImportMethod) {
            d->loadRule = NULL;
        }
        else {
            d->loadRule = schemeRule_ScopeRules_(d, &key.scheme, iTrue);
            clear_ScopeRule(d->loadRule);
        }
        deinit_ScopeKey(&key);
    }
    else {
        d->loadRule = NULL;
    }
}

static void handleCustomTable_ScopeRules_(void *context, const iString *table, iBool isStart) {
    iScopeRules *d = context;
    if (isStart) {
        d->loadRule = !cmp_String(table, "[scope]") ? new_ScopeRule(custom_ScopeRuleKind) : NULL;
        return;
    }
    iScopeRule *rule = d->loadRule;
    d->loadRule = NULL;
    if (!rule) {
        return;
    }
    if (rule->order < 0 || rule->order >= base_ScopeRuleOrder) {
        fprintf(stderr, "[ScopeRules] custom rule skipped: order must be in the range 0...%d\n",
                base_ScopeRuleOrder - 1);
        delete_ScopeRule(rule);
        return;
    }
    compile_ScopeRule_(rule);
    /* Keep sorted by order. */
    size_t pos = 0;
    iConstForEach(PtrArray, i, &d->custom) {
        if (((const iScopeRule *) i.ptr)->order > rule->order) {
            break;
        }
        pos++;
    }
    insert_PtrArray(&d->custom, pos, rule);
}

static void handleKeyValue_ScopeRules_(void *context, const iString *table, const iString *key,
                                       const iTomlValue *value) {
    iScopeRules *d = context;
    iUnused(table);
    if (d->loadRule) {
        parseKey_ScopeRule(d->loadRule, key, value);
    }
}

static void parse_ScopeRules_(iScopeRules *d, const iString *src, iTomlTableFunc tableFunc) {
    iTomlParser *toml = new_TomlParser();
    setHandlers_TomlParser(toml, tableFunc, handleKeyValue_ScopeRules_, d);
    parse_TomlParser(toml, src);
    delete_TomlParser(toml);
    d->loadRule = NULL;
}

static void deserialize_ScopeRules_(iScopeRules *d, enum iScopeRuleKind kind, const iString *src,
                                    enum iImportMethod method) {
    d->loadMethod = method;
    switch (kind) {
        case base_ScopeRuleKind:
            if (method == all_ImportMethod) {
                set_DocPrefs(&d->base.prefs, defaults_DocPrefs()); /* always fully specified */
                d->loadRule = &d->base;
                parse_ScopeRules_(d, src, NULL);
            }
            break;
        case scheme_ScopeRuleKind: {
            parse_ScopeRules_(d, src, handleSchemeTable_ScopeRules_);
            iForEach(PtrArray, i, &d->schemes) {
                fillMissing_ScopeRules_(d, i.ptr);
            }
            break;
        }
        case custom_ScopeRuleKind: {
            iForEach(PtrArray, j, &d->custom) {
                delete_ScopeRule(j.ptr);
            }
            clear_PtrArray(&d->custom);
            parse_ScopeRules_(d, src, handleCustomTable_ScopeRules_);
            break;
        }
        default:
            break;
    }
}

void deserialize_ScopeRules(enum iScopeRuleKind kind, const iString *src,
                            enum iImportMethod method) {
    iScopeRules *d = &rules_;
    if (method == none_ImportMethod || kind == custom_ScopeRuleKind) {
        return;
    }
    lock_Mutex(&d->mtx);
    deserialize_ScopeRules_(d, kind, src, method);
    d->isDirty[kind] = iTrue;
    unlock_Mutex(&d->mtx);
}

static void loadFile_ScopeRules_(iScopeRules *d, enum iScopeRuleKind kind, const char *name) {
    iFile *f = new_File(filePath_ScopeRules_(d, name));
    if (open_File(f, readOnly_FileMode | text_FileMode)) {
        deserialize_ScopeRules_(d, kind, collect_String(readString_File(f)), all_ImportMethod);
    }
    else if (kind == custom_ScopeRuleKind) {
        deserialize_ScopeRules_(d, kind, collectNew_String(), all_ImportMethod);
    }
    iRelease(f);
}

void reloadCustom_ScopeRules(void) {
    iScopeRules *d = &rules_;
    lock_Mutex(&d->mtx);
    loadFile_ScopeRules_(d, custom_ScopeRuleKind, customFileName_ScopeRules_);
    unlock_Mutex(&d->mtx);
}

const iString *customPath_ScopeRules(void) {
    return filePath_ScopeRules_(&rules_, customFileName_ScopeRules_);
}

static void writeFile_ScopeRules_(const iScopeRules *d, const char *name, const iString *contents) {
    makeDirs_Path(&d->saveDir);
    const iString *path     = filePath_ScopeRules_(d, name);
    const iString *tempPath = collect_String(concatCStr_Path(&d->saveDir, format_CStr("%s.tmp", name)));
    iFile *f = new_File(tempPath);
    if (open_File(f, writeOnly_FileMode | text_FileMode)) {
        write_File(f, utf8_String(contents));
        close_File(f);
        commitFile_Core(cstr_String(path), cstr_String(tempPath));
    }
    iRelease(f);
}

static void serialize_ScopeRules_(const iScopeRules *d, enum iScopeRuleKind kind, iString *out) {
    if (kind == base_ScopeRuleKind) {
        serialize_ScopeRule(&d->base, out);
    }
    else if (kind == scheme_ScopeRuleKind) {
        iConstForEach(PtrArray, i, &d->schemes) {
            const iScopeRule *rule = i.ptr;
            iString *table = collectNew_String();
            serialize_ScopeRule(rule, table);
            if (!isEmpty_String(table)) {
                appendFormat_String(out, "[%s]\n%s\n", cstr_String(&rule->key), cstr_String(table));
            }
        }
    }
}

void serialize_ScopeRules(enum iScopeRuleKind kind, iString *out) {
    iScopeRules *d = &rules_;
    iBeginCollect();
    lock_Mutex(&d->mtx);
    serialize_ScopeRules_(d, kind, out);
    unlock_Mutex(&d->mtx);
    iEndCollect();
}

void save_ScopeRules(enum iScopeRuleKind kind) {
    iScopeRules *d = &rules_;
    if (kind == site_ScopeRuleKind) {
        d->isDirty[kind] = iFalse;
        save_SiteSpec();
        return;
    }
    if (kind == custom_ScopeRuleKind) {
        return; /* never written by the app */
    }
    iBeginCollect();
    lock_Mutex(&d->mtx);
    iString *out = collectNew_String();
    serialize_ScopeRules_(d, kind, out);
    writeFile_ScopeRules_(d, kind == base_ScopeRuleKind ? baseFileName_ScopeRules_
                                                       : schemesFileName_ScopeRules_, out);
    d->isDirty[kind] = iFalse;
    unlock_Mutex(&d->mtx);
    iEndCollect();
}

void saveDirty_ScopeRules(void) {
    const iScopeRules *d = &rules_;
    iForIndices(i, d->isDirty) {
        if (d->isDirty[i]) {
            save_ScopeRules(i);
        }
    }
}

void init_ScopeRules(const char *saveDir) {
    iScopeRules *d = &rules_;
    init_Mutex(&d->mtx);
    initCStr_String(&d->saveDir, concatPath_CStr(saveDir, "prefs"));
    init_ScopeRule(&d->base, base_ScopeRuleKind);
    set_DocPrefs(&d->base.prefs, defaults_DocPrefs()); /* always fully specified */
    init_PtrArray(&d->schemes);
    init_PtrArray(&d->builtIn);
    init_PtrArray(&d->custom);
    iZap(d->isDirty);
    d->loadRule   = NULL;
    d->loadMethod = all_ImportMethod;
    addBuiltIn_ScopeRules_(d, "gopher", monospaceDocument_DocPrefsBool, iTrue);
    /* A missing file counts as empty. Custom rules affect what the others inherit. */
    loadFile_ScopeRules_(d, custom_ScopeRuleKind, customFileName_ScopeRules_);
    loadFile_ScopeRules_(d, base_ScopeRuleKind, baseFileName_ScopeRules_);
    loadFile_ScopeRules_(d, scheme_ScopeRuleKind, schemesFileName_ScopeRules_);
}

void deinit_ScopeRules(void) {
    iScopeRules *d = &rules_;
    iPtrArray *arrays[] = { &d->schemes, &d->builtIn, &d->custom };
    iForIndices(i, arrays) {
        iForEach(PtrArray, j, arrays[i]) {
            delete_ScopeRule(j.ptr);
        }
        deinit_PtrArray(arrays[i]);
    }
    deinit_ScopeRule(&d->base);
    deinit_String(&d->saveDir);
    deinit_Mutex(&d->mtx);
}

const iString *prefsDir_ScopeRules(void) {
    return &rules_.saveDir;
}
