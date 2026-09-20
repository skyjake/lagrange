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

#include "impl.h"

#include <lagrange/core.h>
#include "gmutil.h"
#include "ui/window.h"

#include <the_Foundation/file.h>
#include <the_Foundation/fileinfo.h>
#include <the_Foundation/path.h>
#include <the_Foundation/process.h>
#include <the_Foundation/stringset.h>

#if defined (iPlatformAppleDesktop)
#   include "platform/macos.h"
#endif
#if defined (iPlatformAppleMobile)
#   include "platform/ios.h"
#endif
#if defined (iPlatformAndroidMobile)
#   include "platform/android.h"
#endif
#if defined (iPlatformMsys) || defined (iPlatformWindows)
#   include "platform/win32.h"
#endif
#if SDL_VERSION_ATLEAST(2, 0, 14)
#   include <SDL3/SDL_misc.h>
#endif

static const char *defaultDownloadDir_App_ = "~/Downloads";

const char *dataDir_App_(void) {
    iApp *d = &app_;
    if (!d->didCheckDataPathOption) {
        d->didCheckDataPathOption = iTrue;
        const iCommandLineArg *arg;
        if ((arg = iClob(checkArgumentValues_CommandLine(
                &d->args, userDataDir_CommandLineOption, 1))) != NULL) {
            iString *execDir = concatCStr_Path(d->execPath, "..");
            d->overrideDataPath = concat_Path(execDir, value_CommandLineArg(arg, 0));
            delete_String(execDir);
        }
    }
    if (d->overrideDataPath) {
        return cstr_String(d->overrideDataPath);
    }
#if defined (iPlatformLinux) || defined (iPlatformOther)
    const char *configHome = getenv("XDG_CONFIG_HOME");
    if (configHome) {
        return concatPath_CStr(configHome, "lagrange");
    }
#endif
#if defined (iPlatformMsys) || defined (iPlatformWindows)
    /* Check for a portable userdata directory. */
    const char *userDir = concatPath_CStr(cstr_String(d->execPath), "..\\userdata");
    if (fileExistsCStr_FileInfo(userDir)) {
        return userDir;
    }
#endif
#if defined (iPlatformAndroid)
    if (SDL_GetAndroidExternalStorageState() & SDL_ANDROID_EXTERNAL_STORAGE_WRITE) {
        return SDL_GetAndroidExternalStoragePath();
    }
#endif
    if (defaultDataDir_App_) {
        return defaultDataDir_App_;
    }
    return SDL_GetPrefPath("Jaakko Keränen", "fi.skyjake.lagrange");
}

#if defined (iPlatformAndroid)
static iBool copyFile_(const char *srcPath, const char *dstPath) {
    iBool ok = iFalse;
    if (fileExistsCStr_FileInfo(srcPath)) {
        iFile *src = newCStr_File(srcPath);
        iFile *dst = newCStr_File(dstPath);
        if (open_File(src, readOnly_FileMode) && open_File(dst, writeOnly_FileMode)) {
            iBlock *data = readAll_File(src);
            write_File(dst, data);
            delete_Block(data);
            ok = iTrue;
        }
        iRelease(dst);
        iRelease(src);
    }
    return ok;
}

void migrateInternalUserDirToExternalStorage_App_(iApp *d) {
    if (!(SDL_GetAndroidExternalStorageState() & SDL_ANDROID_EXTERNAL_STORAGE_WRITE)) {
        /* As a fallback, user data will be stored in internal storage instead. */
        return;
    }
    /* This is the app-specific "files" directory in internal storage. */
    const char *intDataDir = SDL_GetPrefPath("Jaakko Keränen", "fi.skyjake.lagrange");
    const char *extDataDir = SDL_GetAndroidExternalStoragePath();
    const char *names[] = {
        "bookmarks.ini",
        "prefs.cfg",
        "state.lgr",
        "idents.lgr",
        "trusted.2.txt",
        "visited.2.txt",
    };
    makeDirs_Path(collectNewCStr_String(extDataDir));
    iForIndices(i, names) {
        const char *src = concatPath_CStr(intDataDir, names[i]);
        if (copyFile_(src, concatPath_CStr(extDataDir, names[i]))) {
            removePath_CStr(src);
        }
    }
    /* Copy identities as well. */
    const char *srcIdents = concatPath_CStr(intDataDir, "idents");
    if (fileExistsCStr_FileInfo(srcIdents)) {
        const char *dstIdents = concatPath_CStr(extDataDir, "idents");
        makeDirs_Path(collectNewCStr_String(dstIdents));
        iForEach(DirFileInfo, entry, iClob(newCStr_DirFileInfo(srcIdents))) {
            const iRangecc name = baseName_Path(path_FileInfo(entry.value));
            const char *src = cstr_String(path_FileInfo(entry.value));
            if (copyFile_(src, concatPath_CStr(dstIdents, cstr_Rangecc(name)))) {
                removePath_CStr(src);
            }
        }
        rmdir_Path(collectNewCStr_String(srcIdents));
    }
}
#endif

const char *downloadDir_App_(void) {
#if defined (iPlatformAndroidMobile)
    const char *dir = concatPath_CStr(SDL_GetAndroidExternalStoragePath(), "Downloads");
    makeDirs_Path(collectNewCStr_String(dir));
    return dir;
#endif
#if defined (iPlatformLinux) || defined (iPlatformOther)
    /* Parse user-dirs.dirs using the `xdg-user-dir` tool. */
    iProcess *proc = iClob(new_Process());
    setArguments_Process(
        proc, iClob(newStringsCStr_StringList("/usr/bin/env", "xdg-user-dir", "DOWNLOAD", NULL)));
    if (start_Process(proc)) {
        iString *path = collect_String(newLocal_String(collect_Block(
            readOutputUntilClosed_Process(proc))));
        trim_String(path);
        if (!isEmpty_String(path)) {
            return cstr_String(path);
        }
    }
#endif
#if defined (iPlatformAppleMobile)
    /* Save to a local cache directory from where the user can export to the cloud. */
    const iString *dlDir = cleanedCStr_Path("~/Library/Caches/Downloads");
    if (!fileExists_FileInfo(dlDir)) {
        makeDirs_Path(dlDir);
    }
    return cstr_String(dlDir);
#endif
    return defaultDownloadDir_App_;
}

const iString *execPath_App(void) {
    return app_.execPath;
}

const iString *dataDir_App(void) {
    return collect_String(cleanedCStr_Path(dataDir_App_()));
}

const iString *fontsDir_App(void) {
    return collect_String(cleanedCStr_Path(concatPath_CStr(dataDir_App_(), "fonts")));
}

const iString *downloadDir_App(void) {
    return collect_String(cleaned_Path(&app_.prefs.strings[downloadDir_PrefsString]));
}

const iString *fileNameForUrl_App(const iString *url, const iString *mime) {
    /* Figure out a file name from the URL. */
    iUrl parts;
    init_Url(&parts, url);
    iString *urlPath =
        collect_String(urlDecodeExclude_String(collectNewRange_String(parts.path), "\\/:;"));
    iRangecc path = range_String(urlPath);
    while (startsWith_Rangecc(path, "/")) {
        path.start++;
    }
    while (endsWith_Rangecc(path, "/")) {
        path.end--;
    }
    iString *name = collectNewCStr_String("pagecontent");
    if (isEmpty_Range(&path)) {
        if (!isEmpty_Range(&parts.host)) {
            setRange_String(name, parts.host);
            replace_Block(&name->chars, '.', '_');
        }
    }
    else {
        const size_t slashPos = lastIndexOfCStr_Rangecc(path, "/");
        iRangecc fn = { path.start + (slashPos != iInvalidPos ? slashPos + 1 : 0),
                        path.end };
        if (!isEmpty_Range(&fn)) {
            setRange_String(name, fn);
        }
    }
    if (startsWith_String(name, "~")) {
        /* This might be interpreted as a reference to a home directory. */
        remove_Block(&name->chars, 0, 1);
    }
    if (lastIndexOfCStr_String(name, ".") == iInvalidPos) {
        /* TODO: Needs the inverse of `mediaTypeFromFileExtension_String()`. */
        /* No extension specified in URL. */
        if (startsWith_String(mime, "text/gemini")) {
            appendCStr_String(name, ".gmi");
        }
        else if (startsWith_String(mime, "text/")) {
            appendCStr_String(name, ".txt");
        }
        else if (startsWith_String(mime, "image/")) {
            appendCStr_String(name, cstr_String(mime) + 6);
        }
    }
    return name;
}

const iString *downloadPathForUrl_App(const iString *url, const iString *mime) {
    iString *savePath = concat_Path(downloadDir_App(), fileNameForUrl_App(url, mime));
    if (fileExists_FileInfo(savePath)) {
        /* Make it unique. */
        iDate now;
        initCurrent_Date(&now);
        size_t insPos = lastIndexOfCStr_String(savePath, ".");
        if (insPos == iInvalidPos) {
            insPos = size_String(savePath);
        }
        const iString *date = collect_String(format_Date(&now, "_%Y-%m-%d_%H%M%S"));
        insertData_Block(&savePath->chars, insPos, cstr_String(date), size_String(date));
    }
    return collect_String(savePath);
}

const iString *temporaryPathForUrl_App(const iString *url, const iString *mime) {
    iApp *d = &app_;
#if defined (iPlatformMsys) || defined (iPlatformWindows)
    iString *      tmpPath = collectNew_String();
    const iRangecc tmpDir  = range_String(collect_String(tempDirectory_Win32()));
#elif defined (iPlatformAppleMobile)
    iString *      tmpPath  = collectNew_String();
    const char *   cache    = cleanedPath_CStr("~/Library/Caches/Files");
    const iRangecc tmpDir   = range_CStr(cache);
    makeDirs_Path(collectNewCStr_String(cache));
#elif defined (iPlatformAndroid)
    iString *      tmpPath  = collectNew_String();
    const char *   extCache = concatPath_CStr(SDL_GetAndroidExternalStoragePath(), "Cache");
    const iRangecc tmpDir   = range_CStr(extCache);
    makeDirs_Path(collectNewCStr_String(extCache));
#elif defined (P_tmpdir)
    iString *      tmpPath = collectNew_String();
    const iRangecc tmpDir  = range_CStr(P_tmpdir);
#else
    iString *      tmpPath = collectNewCStr_String(tmpnam(NULL));
    const iRangecc tmpDir  = dirName_Path(tmpPath);
#endif
    set_String(
        tmpPath,
        collect_String(concat_Path(collectNewRange_String(tmpDir),
                                   fileNameForUrl_App(url, mime))));
    insert_StringSet(d->tempFilesPendingDeletion, tmpPath); /* deleted in `deinit_App` */
    return tmpPath;
}

void openInDefaultBrowser_App(const iString *url, const iString *mime) {
    /* URL cleanup: at least Firefox seems to want backslashes to be encoded. */ {
        iString *copy = copy_String(url);
        replace_String(copy, "\\", "%5C");
        url = collect_String(copy);
    }
#if defined (iPlatformAppleMobile)
    if (equalCase_Rangecc(urlScheme_String(url), "file")) {
        revealPath_App(collect_String(localFilePathFromUrl_String(url)));
    }
    else {
        openUri_iOS(url);
    }
    return;
#endif
#if SDL_VERSION_ATLEAST(2, 0, 14)
    if (SDL_OpenURL(cstr_String(url)) == 0) {
        return;
    }
#endif
#if defined (iPlatformAndroid)
    javaCommand_Android("file.view mime:%s url:%s", cstr_String(mime), cstr_String(url));
    return;
#endif
    iProcess *proc = new_Process();
#if defined (iPlatformAppleDesktop) || (defined (iPlatformTerminal) && defined (iPlatformApple))
    setArguments_Process(proc, iClob(newStringsCStr_StringList(
        "/usr/bin/env",
        "open",
        cstr_String(url),
        NULL)));
#elif defined (iPlatformLinux) || defined (iPlatformOther) || defined (iPlatformHaiku)
    setArguments_Process(proc, iClob(newStringsCStr_StringList(
        "/usr/bin/env",
        "xdg-open",
        cstr_String(url),
        NULL)));
#elif defined (iPlatformMsys) || defined (iPlatformWindows)
    /* TODO: The prompt window is shown momentarily... */
    setArguments_Process(proc, iClob(newStringsCStr_StringList(
        concatPath_CStr(cstr_String(execPath_App()), "../urlopen.bat"),
        cstr_String(url),
        NULL)));
#endif
    start_Process(proc);
    waitForFinished_Process(proc);
    iRelease(proc);
}

#include <the_Foundation/thread.h>

void revealPath_App(const iString *path) {
#if defined (iPlatformAppleDesktop)
    iProcess *proc = new_Process();
    setArguments_Process(
        proc, iClob(newStringsCStr_StringList("/usr/bin/open", "-R", cstr_String(path), NULL)));
    start_Process(proc);
    iRelease(proc);
#elif defined (iPlatformAppleMobile)
    /* Use a share sheet. */
    openFileActivityView_iOS(path);
#elif defined (iPlatformLinux) || defined (iPlatformHaiku)
    iProcess *proc = NULL;
    /* Try with `dbus-send` first. */ {
        proc = new_Process();
        setArguments_Process(
            proc,
            iClob(newStringsCStr_StringList(
                "/usr/bin/dbus-send",
                "--print-reply",
                "--dest=org.freedesktop.FileManager1",
                "/org/freedesktop/FileManager1",
                "org.freedesktop.FileManager1.ShowItems",
                format_CStr("array:string:%s", makeFileUrl_CStr(cstr_String(path))),
                "string:",
                NULL)));
        start_Process(proc);
        waitForFinished_Process(proc);
        const iBool dbusDidSucceed = (exitStatus_Process(proc) == 0);
        iRelease(proc);
        if (dbusDidSucceed) {
            return;
        }
    }
    iFileInfo *inf = iClob(new_FileInfo(path));
    iRangecc target;
    if (isDirectory_FileInfo(inf)) {
        target = range_String(path);
    }
    else {
        target = dirName_Path(path);
    }
    proc = new_Process();
    setArguments_Process(
        proc, iClob(newStringsCStr_StringList("/usr/bin/env", "xdg-open", cstr_Rangecc(target), NULL)));
    start_Process(proc);
    iRelease(proc);
#else
    iAssert(0 /* File revealing not implemented on this platform */);
#endif
}
