/* Copyright 2022 Jaakko Keränen <jaakko.keranen@iki.fi>

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

#include "android.h"
#include "app.h"
#include "export.h"
#include "resources.h"
#include "ui/command.h"
#include "ui/metrics.h"
#include "ui/mobile.h"
#include "ui/window.h"

#include <the_Foundation/archive.h>
#include <the_Foundation/buffer.h>
#include <the_Foundation/commandline.h>
#include <the_Foundation/file.h>
#include <the_Foundation/fileinfo.h>
#include <the_Foundation/path.h>
#include <jni.h>
#include <SDL.h>

JNIEXPORT void JNICALL Java_fi_skyjake_lagrange_LagrangeActivity_postAppCommand(
        JNIEnv* env, jclass jcls, jstring command)
{
    iUnused(jcls);
    const char *cmd = (*env)->GetStringUTFChars(env, command, NULL);
    postCommand_Root(NULL, cmd);
    (*env)->ReleaseStringUTFChars(env, command, cmd);
}

static const char *monospaceFontPath_(void) {
    return concatPath_CStr(SDL_AndroidGetExternalStoragePath(), "IosevkaTerm-Extended.ttf");
}

static const char *cachePath_(void) {
    return concatPath_CStr(SDL_AndroidGetExternalStoragePath(), "Cache");
}

static void clearCachedFiles_(void) {
    iForEach(DirFileInfo, dir, iClob(newCStr_DirFileInfo(cachePath_()))) {
        remove(cstr_String(path_FileInfo(dir.value)));
    }
}

void setupApplication_Android(void) {
    /* Cache the monospace font into a file where it can be loaded directly by the Java code. */
    const char *path = monospaceFontPath_();
    const iBlock *iosevka = dataCStr_Archive(archive_Resources(), "fonts/IosevkaTerm-Extended.ttf");
    if (!fileExistsCStr_FileInfo(path) || fileSizeCStr_FileInfo(path) != size_Block(iosevka)) {
        iFile *f = newCStr_File(path);
        if (open_File(f, writeOnly_FileMode)) {
            write_File(f, iosevka);
        }
        iRelease(f);
    }
    /* Tell the Java code where we expect cached file contents to be stored. */
    const iString *cachePath = collectNewCStr_String(cachePath_());
    if (!fileExists_FileInfo(cachePath)) {
        makeDirs_Path(cachePath);
    }
    clearCachedFiles_(); /* old stuff is not needed any more */
    javaCommand_Android("cache.set path:%s/", cstr_String(cachePath));
}

void pickFile_Android(const char *cmd) {
    javaCommand_Android("file.open cmd:%s", cmd);
}

void exportDownloadedFile_Android(const iString *localPath, const iString *mime) {
    javaCommand_Android("file.save mime:%s path:%s", cstr_String(mime), cstr_String(localPath));
}

float displayDensity_Android(void) {
    return toFloat_String(at_CommandLine(commandLine_App(), 1));
}

void javaCommand_Android(const char *format, ...) {
    /* Prepare the argument string. */
    va_list args;
    va_start(args, format);
    iString cmd;
    init_String(&cmd);
    vprintf_Block(&cmd.chars, format, args);
    va_end(args);
    /* Do the call into Java virtual machine. */
    JNIEnv *  env       = (JNIEnv *) SDL_AndroidGetJNIEnv();
    jobject   activity  = (jobject) SDL_AndroidGetActivity();
    jclass    class     = (*env)->GetObjectClass(env, activity);
    jmethodID methodId  = (*env)->GetMethodID(env, class,
                                              "handleJavaCommand",
                                              "(Ljava/lang/String;)V");
    jobject   cmdStr    = (*env)->NewStringUTF(env, constData_Block(utf8_String(&cmd)));
    (*env)->CallVoidMethod(env, activity, methodId, cmdStr);
    (*env)->DeleteLocalRef(env, cmdStr);
    (*env)->DeleteLocalRef(env, activity);
    (*env)->DeleteLocalRef(env, class);
}

/*----------------------------------------------------------------------------------------------*/

static int               inputIdGen_; /* unique IDs for SystemTextInputs */
static iSystemTextInput *currentInput_;

struct Impl_SystemTextInput {
    int id;
    int flags;
    int font;
    iString text;
    int numLines;
    void (*textChangedFunc)(iSystemTextInput *, void *);
    void * textChangedContext;
};

iDefineTypeConstructionArgs(SystemTextInput, (iRect rect, int flags), rect, flags)

static iRect nativeRect_SystemTextInput_(const iSystemTextInput *d, iRect rect) {
    iUnused(d);
    return moved_Rect(rect, init_I2(0, -0.75f * gap_UI));
}

void init_SystemTextInput(iSystemTextInput *d, iRect rect, int flags) {
    currentInput_ = d;
    d->id = ++inputIdGen_;
    d->flags = flags;
    d->font = uiInput_FontId;
    init_String(&d->text);
    d->textChangedFunc = NULL;
    d->textChangedContext = NULL;
    d->numLines = 0;
    rect = nativeRect_SystemTextInput_(d, rect);
    const iColor fg = get_Color(uiInputTextFocused_ColorId);
    const iColor bg = get_Color(uiInputBackgroundFocused_ColorId);
    const iColor hl = get_Color(uiInputCursor_ColorId);
    javaCommand_Android("input.init id:%d "
                        "x:%d y:%d w:%d h:%d "
                        "gap:%d fontsize:%d "
                        "newlines:%d "
                        "correct:%d "
                        "autocap:%d "
                        "sendkey:%d "
                        "gokey:%d "
                        "multi:%d "
                        "alignright:%d "
                        "fg0:%d fg1:%d fg2:%d "
                        "bg0:%d bg1:%d bg2:%d "
                        "hl0:%d hl1:%d hl2:%d",
                        d->id,
                        rect.pos.x, rect.pos.y, rect.size.x, rect.size.y,
                        gap_UI, lineHeight_Text(default_FontId),
                        (flags & insertNewlines_SystemTextInputFlag) != 0,
                        (flags & disableAutocorrect_SystemTextInputFlag) == 0,
                        (flags & disableAutocapitalize_SystemTextInputFlag) == 0,
                        (flags & returnSend_SystemTextInputFlags) != 0,
                        (flags & returnGo_SystemTextInputFlags) != 0,
                        (flags & multiLine_SystemTextInputFlags) != 0,
                        (flags & alignRight_SystemTextInputFlag) != 0,
                        fg.r, fg.g, fg.b,
                        bg.r, bg.g, bg.b,
                        hl.r, hl.g, hl.b);
}

void deinit_SystemTextInput(iSystemTextInput *d) {
    javaCommand_Android("input.deinit id:%d", d->id);
    deinit_String(&d->text);
    if (inputIdGen_ == d->id) { /* no new inputs started already? */
        currentInput_ = NULL;
    }
}

void setRect_SystemTextInput(iSystemTextInput *d, iRect rect) {
    rect = nativeRect_SystemTextInput_(d, rect);
    javaCommand_Android("input.setrect id:%d x:%d y:%d w:%d h:%d",
                        d->id, rect.pos.x, rect.pos.y, rect.size.x, rect.size.y);
}

void setText_SystemTextInput(iSystemTextInput *d, const iString *text, iBool allowUndo) {
    set_String(&d->text, text);
    javaCommand_Android("input.set id:%d text:%s", d->id, cstr_String(text));
    if (d->flags & selectAll_SystemTextInputFlags) {
        javaCommand_Android("input.selectall id:%d", d->id);
    }
}

void setFont_SystemTextInput(iSystemTextInput *d, int fontId) {
    d->font = fontId;
    const char *ttfPath = "";
    if (fontId / maxVariants_Fonts * maxVariants_Fonts == monospace_FontId) {
        ttfPath = monospaceFontPath_();
    }
    javaCommand_Android("input.setfont id:%d size:%d ttfpath:%s", d->id, lineHeight_Text(fontId), ttfPath);
}

void setTextChangedFunc_SystemTextInput
        (iSystemTextInput *d, void (*textChangedFunc)(iSystemTextInput *, void *), void *context) {
    d->textChangedFunc = textChangedFunc;
    d->textChangedContext = context;
}

void selectAll_SystemTextInput(iSystemTextInput *d) {
    javaCommand_Android("input.selectall id:%d", d->id);
}

const iString *text_SystemTextInput(const iSystemTextInput *d) {
    return &d->text;
}

int preferredHeight_SystemTextInput(const iSystemTextInput *d) {
    return d->numLines * lineHeight_Text(d->font);
}

/*----------------------------------------------------------------------------------------------*/

static int userBackupTimer_;

static uint32_t backupUserData_Android_(uint32_t interval, void *data) {
    userBackupTimer_ = 0;
    iUnused(interval, data);
    /* This runs in a background thread. We don't want to block the UI thread for saving. */
    iExport *backup = new_Export();
    generatePartial_Export(backup, bookmarks_ExportFlag | identitiesAndTrust_ExportFlag);
    iBuffer *buf = new_Buffer();
    openEmpty_Buffer(buf);
    serialize_Archive(archive_Export(backup), stream_Buffer(buf));
    delete_Export(backup);
    iString *enc = base64Encode_Block(data_Buffer(buf));
    iRelease(buf);
    javaCommand_Android("backup.save data:%s", cstr_String(enc));
    delete_String(enc);
    return 0;
}

iBool handleCommand_Android(const char *cmd) {
    if (equal_Command(cmd, "android.input.changed")) {
        const int id = argLabel_Command(cmd, "id");
        if (!currentInput_ || currentInput_->id != id) {
            return iTrue; /* obsolete notification */
        }
        iBool wasChanged = iFalse;
        if (hasLabel_Command(cmd, "text")) {
            const char *newText = suffixPtr_Command(cmd, "text");
            if (cmp_String(&currentInput_->text, newText)) {
                setCStr_String(&currentInput_->text, newText);
                wasChanged = iTrue;
            }
        }
        const int numLines = argLabel_Command(cmd, "lines");
        if (numLines) {
            if (currentInput_->numLines != numLines) {
                currentInput_->numLines = numLines;
                wasChanged = iTrue;
            }
        }
        if (wasChanged && currentInput_->textChangedFunc) {
            currentInput_->textChangedFunc(currentInput_, currentInput_->textChangedContext);
        }
        return iTrue;
    }
    else if (equal_Command(cmd, "android.input.enter")) {
        const int id = argLabel_Command(cmd, "id");
        if (!currentInput_ || currentInput_->id != id) {
            return iTrue; /* obsolete notification */
        }
        SDL_Event ev = { .type = SDL_KEYDOWN };
        ev.key.timestamp = SDL_GetTicks();
        ev.key.keysym.sym = SDLK_RETURN;
        ev.key.state = SDL_PRESSED;
        SDL_PushEvent(&ev);
        ev.type = SDL_KEYUP;
        ev.key.state = SDL_RELEASED;
        SDL_PushEvent(&ev);
        return iTrue;
    }
    else if (equal_Command(cmd, "theme.changed") || equal_Command(cmd, "tab.changed") ||
             equal_Command(cmd, "document.changed") || equal_Command(cmd, "prefs.dismiss")) {
        const iPrefs *prefs = prefs_App();
        const iColor top = get_Color(prefs->bottomNavBar && prefs->bottomTabBar ?
                    tmBackground_ColorId : uiBackground_ColorId);
        const iColor btm = get_Color(uiBackground_ColorId);
        javaCommand_Android("status.color top:%d bottom:%d",
                            0xff000000 | (top.r << 16) | (top.g << 8) | top.b,
                            0xff000000 | (btm.r << 16) | (btm.g << 8) | btm.b);
    }
    else if (equal_Command(cmd, "android.keyboard.changed")) {
        iMainWindow *mw = get_MainWindow();
        if (mw) {
            setKeyboardHeight_MainWindow(mw, arg_Command(cmd));
        }
        return iTrue;
    }
    else if (equal_Command(cmd, "bookmarks.changed") ||
             equal_Command(cmd, "idents.changed") ||
             equal_Command(cmd, "backup.now")) {
        SDL_RemoveTimer(userBackupTimer_);
        userBackupTimer_ = SDL_AddTimer(1000, backupUserData_Android_, NULL);
        return iFalse;
    }
    else if (equal_Command(cmd, "backup.found")) {
        iString *data = suffix_Command(cmd, "data");
        iBlock *decoded = base64Decode_Block(utf8_String(data));
        delete_String(data);
        iArchive *archive = new_Archive();
        if (openData_Archive(archive, decoded)) {
            iExport *backup = new_Export();
            if (load_Export(backup, archive)) {
                import_Export(backup, ifMissing_ImportMethod, all_ImportMethod,
                              none_ImportMethod, none_ImportMethod, none_ImportMethod);
            }
            delete_Export(backup);
        }
        iRelease(archive);
        delete_Block(decoded);
        return iTrue;
    }
    return iFalse;
}
