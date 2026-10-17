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

#include "lagrange/tools.h"

#include <lagrange/lang.h>
#include <lagrange/gmutil.h>

#include <the_Foundation/file.h>
#include <the_Foundation/fileinfo.h>
#include <the_Foundation/mutex.h>
#include <the_Foundation/path.h>
#include <the_Foundation/process.h>
#include <the_Foundation/stringlist.h>
#include <the_Foundation/toml.h>
#include <the_Foundation/xml.h>

iDefineTypeConstruction(Tool)

void init_Tool(iTool *d) {
    init_String(&d->id);
    init_String(&d->name);
    d->command = new_StringArray();
    init_String(&d->mimePattern);
    d->mimeRegex = NULL;
    d->isEnabled = iTrue;
    d->isAuto    = iTrue;
    d->inMenu    = iFalse;
    init_String(&d->key);
}

void deinit_Tool(iTool *d) {
    deinit_String(&d->key);
    iRelease(d->mimeRegex);
    deinit_String(&d->mimePattern);
    iRelease(d->command);
    deinit_String(&d->name);
    deinit_String(&d->id);
}

static void setMimePattern_Tool_(iTool *d, const iString *pattern) {
    iReleasePtr(&d->mimeRegex);
    set_String(&d->mimePattern, pattern);
    d->mimeRegex = new_RegExp(cstr_String(pattern), caseInsensitive_RegExpOption);
}

iBool isUsable_Tool(const iTool *d) {
    return d->isEnabled && !isEmpty_StringArray(d->command);
}

static iBool hasPath_Tool_(const iTool *d) {
    /* Executables without a path are searched in PATH. */
    const iString *exec = constAt_StringArray(d->command, 0);
    return contains_String(exec, '/') || contains_String(exec, '\\');
}

iBool matchMime_Tool(const iTool *d, const iString *mime) {
    if (!d->mimeRegex) {
        return iTrue;
    }
    iRegExpMatch m;
    init_RegExpMatch(&m);
    return matchString_RegExp(d->mimeRegex, mime, &m);
}

iBool parseOutput_Tool(const iBlock *output, iString *mime_out, iBlock *body_out) {
    const iRangecc src = range_Block(output);
    const char *eol = memchr(src.start, '\n', size_Range(&src));
    if (!eol || !startsWith_Rangecc(src, "20 ")) {
        return iFalse;
    }
    iRangecc meta = { src.start + 3, eol };
    trim_Rangecc(&meta);
    setRange_String(mime_out, meta);
    setData_Block(body_out, eol + 1, src.end - eol - 1);
    return !isEmpty_String(mime_out);
}

/*----------------------------------------------------------------------------------------------*/

struct Impl_ToolProcess {
    iMutex    mtx;
    iProcess *proc;
    iBool     isCancelled;
};

iDefineTypeConstruction(ToolProcess)

void init_ToolProcess(iToolProcess *d) {
    init_Mutex(&d->mtx);
    d->proc        = NULL;
    d->isCancelled = iFalse;
}

void deinit_ToolProcess(iToolProcess *d) {
    iAssert(!d->proc);
    deinit_Mutex(&d->mtx);
}

static void setProcess_ToolProcess_(iToolProcess *d, iProcess *proc) {
    lock_Mutex(&d->mtx);
    d->proc = proc;
    if (proc && d->isCancelled) {
        kill_Process(proc); /* cancelled while starting */
    }
    unlock_Mutex(&d->mtx);
}

iBlock *run_ToolProcess(iToolProcess *d, const iTool *tool, const iString *mime,
                        const iBlock *body, const iString *requestUrl) {
    if (isCancelled_ToolProcess(d)) {
        return NULL;
    }
    iStringList *args = newArray_StringList(tool->command);
    iStringList *env = new_StringList();
    pushBack_StringList(env, collectNewFormat_String("CONTENT_TYPE=%s", cstr_String(mime)));
    if (!isEmpty_String(requestUrl)) {
        pushBack_StringList(env,
                            collectNewFormat_String("REQUEST_URL=%s", cstr_String(requestUrl)));
    }
    iBlock *output = NULL;
    iProcess *proc = new_Process();
    setArguments_Process(proc, args);
    setEnvironment_Process(proc, env);
    if (start_Process(proc)) {
        setProcess_ToolProcess_(d, proc);
        output = communicate_Process(proc, body);
        setProcess_ToolProcess_(d, NULL);
        waitForFinished_Process(proc); /* reap the child */
        if (isCancelled_ToolProcess(d) || !startsWith_Rangecc(range_Block(output), "20")) {
            /* Cancelled, or didn't produce valid output. */
            delete_Block(output);
            output = NULL;
        }
    }
    iRelease(proc);
    iRelease(env);
    iRelease(args);
    return output;
}

void cancel_ToolProcess(iToolProcess *d) {
    lock_Mutex(&d->mtx);
    d->isCancelled = iTrue;
    if (d->proc) {
        kill_Process(d->proc);
    }
    unlock_Mutex(&d->mtx);
}

iBool isCancelled_ToolProcess(const iToolProcess *d) {
    iBool cancelled;
    iGuardMutex(&d->mtx, cancelled = d->isCancelled);
    return cancelled;
}

/*----------------------------------------------------------------------------------------------*/

static iRegExp *xmlMimePattern_(void) {
    static iRegExp *xmlMime_;
    if (!xmlMime_) {
        xmlMime_ = new_RegExp("(application|text)/(atom\\+)?xml", caseInsensitive_RegExpOption);
    }
    return xmlMime_;
}

static iBlock *translateAtomXmlToGeminiFeed_(const iString *mime, const iBlock *source,
                                             const iString *requestUrl) {
    iUnused(requestUrl); /* TODO: Use for what? */
    iRegExpMatch m;
    init_RegExpMatch(&m);
    if (!matchString_RegExp(xmlMimePattern_(), mime, &m)) {
        return NULL;
    }
    iBlock *      output = NULL;
    iXmlDocument *doc    = new_XmlDocument();
    iString       src;
    initBlock_String(&src, source); /* assume it's UTF-8 */
    if (!parse_XmlDocument(doc, &src)) {
        goto finished;
    }
    const iXmlElement *feed = &doc->root;
    if (!equal_Rangecc(feed->name, "feed")) {
        goto finished;
    }
    if (!equal_Rangecc(attribute_XmlElement(feed, "xmlns"), "http://www.w3.org/2005/Atom")) {
        goto finished;
    }
    iString *title = collect_String(decodedContent_XmlElement(child_XmlElement(feed, "title")));
    if (isEmpty_String(title)) {
        goto finished;
    }
    iString *subtitle = collect_String(decodedContent_XmlElement(child_XmlElement(feed, "subtitle")));
    iString out;
    init_String(&out);
    format_String(&out,
                  "20 text/gemini\r\n"
                  "# %s\n\n",
                  cstr_String(title));
    if (!isEmpty_String(subtitle)) {
        appendFormat_String(&out, "## %s\n\n", cstr_String(subtitle));
    }
    appendCStr_String(&out, cstr_Lang("feeds.atom.translated"));
    appendCStr_String(&out, "\n\n");
    iRegExp *datePattern =
        iClob(new_RegExp("^\\s*([0-9][0-9][0-9][0-9]-[0-1][0-9]-[0-3][0-9])(T|\\s).*",
                         caseSensitive_RegExpOption));
    iBeginCollect();
    iConstForEach(PtrArray, i, &feed->children) {
        iEndCollect();
        iBeginCollect();
        const iXmlElement *entry = i.ptr;
        if (!equal_Rangecc(entry->name, "entry")) {
            continue;
        }
        title = collect_String(decodedContent_XmlElement(child_XmlElement(entry, "title")));
        if (isEmpty_String(title)) {
            continue;
        }
        const iString *published =
            collect_String(decodedContent_XmlElement(child_XmlElement(entry, "published")));
        const iString *updated =
            collect_String(decodedContent_XmlElement(child_XmlElement(entry, "updated")));
        iRegExpMatch m;
        init_RegExpMatch(&m);
        if (!matchString_RegExp(datePattern, updated, &m)) {
            init_RegExpMatch(&m);
            if (!matchString_RegExp(datePattern, published, &m)) {
                continue;
            }
        }
        iRangecc url = iNullRange;
        iConstForEach(PtrArray, j, &entry->children) {
            const iXmlElement *link = j.ptr;
            if (!equal_Rangecc(link->name, "link")) {
                continue;
            }
            const iRangecc href = attribute_XmlElement(link, "href");
            const iRangecc rel  = attribute_XmlElement(link, "rel");
            const iRangecc type = attribute_XmlElement(link, "type");
            if (startsWithCase_Rangecc(href, "gemini:")) {
                url = href;
                /* We're happy with the first gemini URL. */
                /* TODO: Are we? */
                break;
            }
            url = href;
            iUnused(rel, type);
        }
        if (isEmpty_Range(&url)) {
            continue;
        }
        appendFormat_String(&out, "=> %s %s - %s\n",
                            cstr_Rangecc(url),
                            cstr_Rangecc(capturedRange_RegExpMatch(&m, 1)),
                            cstr_String(title));
    }
    iEndCollect();
    output = copy_Block(utf8_String(&out));
    deinit_String(&out);
finished:
    delete_XmlDocument(doc);
    deinit_String(&src);
    return output;
}


static const char *fileName_Tools_ = "tools.ini";

struct Impl_Tools {
    iPtrArray    tools;
    iPtrArray    builtins; /* iToolFilterFunc pointers */
    iStringList *errors;
};

static iTools *theTools_;

iDefineTypeConstruction(Tools)

iTools *get_Tools(void) {
    return theTools_;
}

void init_Tools(iTools *d) {
    theTools_ = d; /* global instance */
    init_PtrArray(&d->tools);
    init_PtrArray(&d->builtins);
    d->errors = new_StringList();
    addBuiltinFilter_Tools(d, translateAtomXmlToGeminiFeed_);
}

void deinit_Tools(iTools *d) {
    iForEach(PtrArray, i, &d->tools) {
        delete_Tool(i.ptr);
    }
    iRelease(d->errors);
    deinit_PtrArray(&d->builtins);
    deinit_PtrArray(&d->tools);
}

void addError_Tools(iTools *d, const iTool *tool, const char *msg) {
    pushBack_StringList(d->errors,
                        tool ? collectNewFormat_String("[%s] %s", cstr_String(&tool->id), msg)
                             : collectNewCStr_String(msg));
}

static iBool isValidId_Tool_(const iString *id) {
    iConstForEach(String, i, id) {
        if (!isAlphaNumeric_Char(i.value) && i.value != '_' && i.value != '-') {
            return iFalse;
        }
    }
    return iTrue;
}

const iPtrArray *list_Tools(const iTools *d) {
    return &d->tools;
}

const iTool *find_Tools(const iTools *d, const iString *id) {
    iConstForEach(PtrArray, i, &d->tools) {
        const iTool *tool = i.ptr;
        if (equal_String(&tool->id, id)) {
            return tool;
        }
    }
    return NULL;
}

static void handleTable_Tools_(void *context, const iString *table, iBool isStart) {
    iTools *d = context;
    if (isStart) {
        iTool *tool = new_Tool();
        set_String(&tool->id, table);
        if (!isValidId_Tool_(table)) {
            addError_Tools(d, tool, "invalid tool ID (use letters, numbers, '_', and '-')");
            tool->isEnabled = iFalse;
        }
        else if (find_Tools(d, table)) {
            addError_Tools(d, tool, "duplicate tool ID");
            tool->isEnabled = iFalse;
        }
        pushBack_PtrArray(&d->tools, tool);
        return;
    }
    iTool *tool = back_PtrArray(&d->tools);
    if (isEmpty_String(&tool->name)) {
        set_String(&tool->name, &tool->id);
    }
    if (isEmpty_StringArray(tool->command)) {
        addError_Tools(d, tool, "command not specified");
    }
    else if (hasPath_Tool_(tool) && !fileExists_FileInfo(constAt_StringArray(tool->command, 0))) {
        addError_Tools(d, tool, "executable not found");
    }
}

static void handleKeyValue_Tools_(void *context, const iString *table, const iString *key,
                                  const iTomlValue *value) {
    iTools *d = context;
    if (isEmpty_String(table) || isEmpty_PtrArray(&d->tools)) {
        return;
    }
    iTool *tool    = back_PtrArray(&d->tools);
    iBool  isValid = iTrue;
    if (!cmp_String(key, "name")) {
        isValid = (value->type == string_TomlType);
        if (isValid) {
            set_String(&tool->name, value->value.string);
        }
    }
    else if (!cmp_String(key, "command")) {
        isValid = (value->type == stringArray_TomlType);
        if (isValid) {
            iChangeRef(tool->command, value->value.stringArray);
        }
    }
    else if (!cmp_String(key, "mime")) {
        isValid = (value->type == string_TomlType);
        if (isValid) {
            setMimePattern_Tool_(tool, value->value.string);
        }
    }
    else if (!cmp_String(key, "enabled")) {
        isValid = (value->type == boolean_TomlType);
        if (isValid) {
            tool->isEnabled &= value->value.boolean;
        }
    }
    else if (!cmp_String(key, "auto")) {
        isValid = (value->type == boolean_TomlType);
        if (isValid) {
            tool->isAuto = value->value.boolean;
        }
    }
    else if (!cmp_String(key, "menu")) {
        isValid = (value->type == boolean_TomlType);
        if (isValid) {
            tool->inMenu = value->value.boolean;
        }
    }
    else if (!cmp_String(key, "key")) {
        isValid = (value->type == string_TomlType);
        if (isValid) {
            set_String(&tool->key, value->value.string);
        }
    }
    if (!isValid) {
        addError_Tools(d, tool, format_CStr("invalid value for \"%s\"", cstr_String(key)));
    }
}

void addBuiltinFilter_Tools(iTools *d, iToolFilterFunc func) {
    pushBack_PtrArray(&d->builtins, (void *) func);
}

iBool willTryFilter_Tools(const iTools *d, const iString *mime) {
    /* TODO: Combine this function with tryFilter_Tools! */
    iConstForEach(PtrArray, i, &d->tools) {
        const iTool *tool = i.ptr;
        if (isUsable_Tool(tool) && tool->isAuto && matchMime_Tool(tool, mime)) {
            return iTrue;
        }
    }
    /* Built-in filters. */
    iRegExpMatch m;
    init_RegExpMatch(&m);
    if (matchString_RegExp(xmlMimePattern_(), mime, &m)) {
        return iTrue;
    }
    return iFalse;
}

iBlock *tryFilter_Tools(const iTools *d, iToolProcess *proc, const iString *mime,
                        const iBlock *body, const iString *requestUrl) {
    iConstForEach(PtrArray, i, &d->tools) {
        const iTool *tool = i.ptr;
        if (isUsable_Tool(tool) && tool->isAuto && matchMime_Tool(tool, mime)) {
            iBlock *result = run_ToolProcess(proc, tool, mime, body, requestUrl);
            if (result) {
                return result;
            }
        }
    }
    if (isCancelled_ToolProcess(proc)) {
        return NULL;
    }
    /* Built-in filters. */
    iConstForEach(PtrArray, bi, &d->builtins) {
        iBlock *result = ((iToolFilterFunc) bi.ptr)(mime, body, requestUrl);
        if (result) {
            return result;
        }
    }
    return NULL;
}

iBool load_Tools(iTools *d, const char *saveDir) {
    iFile *f = iClob(newCStr_File(concatPath_CStr(saveDir, fileName_Tools_)));
    if (open_File(f, readOnly_FileMode | text_FileMode)) {
        iString *src = collect_String(readString_File(f));
        close_File(f);
        iTomlParser *toml = new_TomlParser();
        setHandlers_TomlParser(toml, handleTable_Tools_, handleKeyValue_Tools_, d);
        if (!parse_TomlParser(toml, src)) {
            addError_Tools(d, NULL, "syntax error");
        }
        delete_TomlParser(toml);
    }
    return isEmpty_StringList(d->errors);
}

const iString *debugInfo_Tools(const iTools *d) {
    iString *str = collectNew_String();
    if (!isEmpty_StringList(d->errors)) {
        appendFormat_String(str, "### ⚠ Errors in %s\n", fileName_Tools_);
        iConstForEach(StringList, e, d->errors) {
            appendFormat_String(str, "* %s\n", cstr_String(e.value));
        }
    }
    iConstForEach(PtrArray, i, &d->tools) {
        const iTool *tool = i.ptr;
        appendFormat_String(str, "### %s: %s\n", cstr_String(&tool->id), cstr_String(&tool->name));
        if (!tool->isEnabled) {
            appendCStr_String(str, "Disabled\n");
        }
        appendFormat_String(str, "Automatic filter: %s\nIn menu: %s\n",
                            tool->isAuto ? "yes" : "no", tool->inMenu ? "yes" : "no");
        if (!isEmpty_String(&tool->key)) {
            appendFormat_String(str, "Key: %s\n", cstr_String(&tool->key));
        }
        appendFormat_String(str, "MIME regex:\n```\n%s\n```\n",
                            isEmpty_String(&tool->mimePattern) ? "(any)"
                                                               : cstr_String(&tool->mimePattern));
        if (isEmpty_StringArray(tool->command)) {
            appendCStr_String(str, "⚠ Command not specified!\n");
            continue;
        }
        appendFormat_String(str, "Command:%s\n```\n",
                            !hasPath_Tool_(tool) ? " (executable searched in PATH)"
                            : fileExists_FileInfo(constAt_StringArray(tool->command, 0))
                                ? ""
                                : " ⚠ EXECUTABLE NOT FOUND");
        iConstForEach(StringArray, a, tool->command) {
            appendFormat_String(str, "%s\n", cstr_String(a.value));
        }
        appendCStr_String(str, "```\n");
    }
    return str;
}
