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
#include "audio/player.h"
#include "ui/command.h"
#include "ui/metrics.h"
#include "ui/mobile.h"
#include "ui/window.h"

#include <the_Foundation/archive.h>
#include <the_Foundation/atomic.h>
#include <the_Foundation/buffer.h>
#include <the_Foundation/commandline.h>
#include <the_Foundation/file.h>
#include <the_Foundation/fileinfo.h>
#include <the_Foundation/mutex.h>
#include <the_Foundation/path.h>

#include <SDL.h>
#include <stdio.h>
#include <unistd.h>
#include <pthread.h>
#include <android/log.h>
#include <jni.h>

static JavaVM *javaVm_        = NULL; /* cached at startup; used for thread-agnostic JNI access */
static jobject cachedActivity_ = NULL; /* JNI global ref to activity; valid from any thread */

/* Background blocking: when the app is in the background with no SDL audio playing, the event
   loop blocks on a condition variable to avoid spinning. The mutex protects all access to the
   condition variable, ensuring correct memory visibility across CPU cores. */
static iAtomicInt  isAppInBackground_;
static iCondition  blockCond_;
static iMutex      blockMutex_;

iDeclareType(AndroidAudioPlayer);

enum iAndroidAudioPlayerState {
    initialized_AndroidAudioPlayerState,
    playing_AndroidAudioPlayerState,
    paused_AndroidAudioPlayerState,
};

struct Impl_AndroidAudioPlayer {
    iString cacheFilePath;
    iBool   isStreaming;  /* using MediaDataSource path instead of a cache file */
    iBool   isFinished;  /* set when Java MediaPlayer sends audio.finished */
    float   volume;
    float   currentTime; /* seconds */
    float   duration;    /* seconds */
    enum iAndroidAudioPlayerState state;
};

iBool isAppInBackground_Android(void) {
    return value_Atomic(&isAppInBackground_);
}

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
        removePath_CStr(cstr_String(path_FileInfo(dir.value)));
    }
}

static int pfd_[2];
static pthread_t thr_;
static const char *tag_ = "fi.skyjake.lagrange";
static float topInset_;
static float bottomInset_;
static float leftInset_;
static float rightInset_;

static void *loggerThreadFunc_(void *p) {
    ssize_t rdsz;
    char buf[512];
    while((rdsz = read(pfd_[0], buf, sizeof(buf) - 1)) > 0) {
        if(buf[rdsz - 1] == '\n') {
            --rdsz;
        }
        buf[rdsz] = 0;  /* add null-terminator */
        __android_log_write(ANDROID_LOG_DEBUG, tag_, buf);
    }
    return 0;
}

static int startLogOutputThread_(void) {
    /* make stdout line-buffered and stderr unbuffered */
    setvbuf(stdout, 0, _IOLBF, 0);
    setvbuf(stderr, 0, _IONBF, 0);
    /* create the pipe and redirect stdout and stderr */
    pipe(pfd_);
    dup2(pfd_[1], 1);
    dup2(pfd_[1], 2);
    /* spawn the logging thread */
    if (pthread_create(&thr_, 0, loggerThreadFunc_, 0) == -1) {
        return -1;
    }
    pthread_detach(thr_);
    return 0;
}

void setupApplication_Android(void) {
    init_Condition(&blockCond_);
    init_Mutex(&blockMutex_);
    /* Cache the JavaVM pointer and activity global ref for use from non-SDL threads. */
    if (!javaVm_) {
        JNIEnv *env = (JNIEnv *) SDL_AndroidGetJNIEnv();
        (*env)->GetJavaVM(env, &javaVm_);
        jobject localActivity = (jobject) SDL_AndroidGetActivity();
        cachedActivity_ = (*env)->NewGlobalRef(env, localActivity);
        (*env)->DeleteLocalRef(env, localActivity);
    }
#if !defined (NDEBUG)
    startLogOutputThread_();
#endif
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
    javaCommand_Android("ostheme.query");
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

/**
 * Get a JNI environment for the current thread, attaching it to the JVM if needed.
 * Sets *needDetach to iTrue if DetachCurrentThread must be called afterwards.
 * Works on both SDL-managed threads and native POSIX threads (e.g., network I/O threads).
 */
static JNIEnv *currentJNIEnv_(jboolean *needDetach) {
    JNIEnv *env = NULL;
    *needDetach = JNI_FALSE;
    if (!javaVm_) return NULL;
    if ((*javaVm_)->GetEnv(javaVm_, (void **) &env, JNI_VERSION_1_6) == JNI_EDETACHED) {
        (*javaVm_)->AttachCurrentThread(javaVm_, &env, NULL);
        *needDetach = JNI_TRUE;
    }
    return env;
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
static iRangei           lastSelectionRange_;

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

void setSelection_SystemTextInput(iSystemTextInput *d, iRangei selection) {
    javaCommand_Android("input.select id:%d start:%d end:%d", d->id, selection.start, selection.end);
}

iRangei lastInputSelectionRange_Android(void) {
    return lastSelectionRange_;
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
    generatePartial_Export(backup, bookmarks_ExportFlag | identitiesAndTrust_ExportFlag |
                           snippets_ExportFlag);
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
    else if (equal_Command(cmd, "android.input.selrange")) {
        lastSelectionRange_ = (iRangei){ argLabel_Command(cmd, "start"),
                                         argLabel_Command(cmd, "end") };
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
                              none_ImportMethod, none_ImportMethod, none_ImportMethod,
                              all_ImportMethod);
            }
            delete_Export(backup);
        }
        iRelease(archive);
        delete_Block(decoded);
        return iTrue;
    }
    else if (equal_Command(cmd, "android.audio.time")) {
        iAndroidAudioPlayer *plr = pointerLabel_Command(cmd, "player");
        if (plr) {
            plr->currentTime = argLabel_Command(cmd, "pos") / 1000.0f;
            plr->duration    = argLabel_Command(cmd, "dur") / 1000.0f;
        }
        return iTrue;
    }
    else if (equal_Command(cmd, "android.audio.finished")) {
        iAndroidAudioPlayer *plr = pointerLabel_Command(cmd, "player");
        if (plr) {
            plr->isFinished = iTrue;
            postCommand_App("media.player.update");
        }
        return iTrue;
    }
    else if (equal_Command(cmd, "android.insets")) {
        topInset_    = (float) argLabel_Command(cmd, "top");
        bottomInset_ = (float) argLabel_Command(cmd, "bottom");
        leftInset_   = (float) argLabel_Command(cmd, "left");
        rightInset_  = (float) argLabel_Command(cmd, "right");
        postCommand_App("window.resized"); /* force a layout update */
    }
    return iFalse;
}

/*----------------------------------------------------------------------------------------------*/

void notifySDLAudioStarted_Android(void) {
    iGuardMutex(&blockMutex_, {
        signal_Condition(&blockCond_);
    });
}

void blockWhileAppInBackground_Android(void) {
    /* On Android, we control event loop blocking manually so we can play audio in the
       background using the SDL audio thread. */
    if (!isAppInBackground_Android() || numActiveSDLAudio_Player() > 0) return;
    iGuardMutex(&blockMutex_,
        /* We will block here until the app returns to the foreground, or
           there is audio playing. */
        while (isAppInBackground_Android() && numActiveSDLAudio_Player() == 0) {
            iTime timeout;
            initSeconds_Time(&timeout, 1.0);
            waitTimeout_Condition(&blockCond_, &blockMutex_, &timeout);
        }
    );
}

JNIEXPORT void JNICALL Java_fi_skyjake_lagrange_LagrangeActivity_notifyAppPaused(
        JNIEnv *env, jclass jcls)
{
    iUnused(env, jcls);
    iGuardMutex(&blockMutex_, set_Atomic(&isAppInBackground_, 1))
}

JNIEXPORT void JNICALL Java_fi_skyjake_lagrange_LagrangeActivity_notifyAppResumed(
        JNIEnv *env, jclass jcls)
{
    iUnused(env, jcls);
    iGuardMutex(&blockMutex_, {
        set_Atomic(&isAppInBackground_, 0);
        signal_Condition(&blockCond_);
    })
}

JNIEXPORT void JNICALL Java_fi_skyjake_lagrange_LagrangeActivity_notifyAppStopping(
        JNIEnv *env, jclass jcls) {
    iUnused(env, jcls);
    /* Called synchronously from Java onStop(); saves full state including cached tab
       content before the process may be killed. */
    saveState_App();
}

/*----------------------------------------------------------------------------------------------*/

iDefineTypeConstruction(AndroidAudioPlayer)

static const char *audioCacheDir_(void) {
    return concatPath_CStr(cachePath_(), "Audio");
}

static const char *audioFileExt_(const iString *mimeType) {
    /* Note: There is a similar type-to-extension mapping on the Java audio side. */
    if (startsWithCase_String(mimeType, "audio/mpeg") ||
        startsWithCase_String(mimeType, "audio/mp3")) {
        return ".mp3";
    }
    if (startsWithCase_String(mimeType, "audio/ogg")) {
        return ".ogg";
    }
    if (startsWithCase_String(mimeType, "audio/mp4") ||
        startsWithCase_String(mimeType, "audio/mpeg4") ||
        startsWithCase_String(mimeType, "audio/m4a") ||
        startsWithCase_String(mimeType, "audio/x-m4a")) {
        return ".m4a";
    }
    if (startsWithCase_String(mimeType, "audio/aac") ||
        startsWithCase_String(mimeType, "audio/x-aac")) {
        return ".aac";
    }
    if (startsWithCase_String(mimeType, "audio/flac") ||
        startsWithCase_String(mimeType, "audio/x-flac")) {
        return ".flac";
    }
    if (startsWithCase_String(mimeType, "audio/3gpp")) {
        return ".3gp";
    }
#if !defined (LAGRANGE_ENABLE_OPUS)
    /* Opus in Ogg: opusfile is not compiled for Android, use MediaPlayer (API 21+). */
    if (startsWithCase_String(mimeType, "audio/opus")) {
        return ".opus";
    }
#endif
    return "";
}

void init_AndroidAudioPlayer(iAndroidAudioPlayer *d) {
    init_String(&d->cacheFilePath);
    d->volume      = 1.0f;
    d->currentTime = 0.0f;
    d->duration    = 0.0f;
    d->state       = initialized_AndroidAudioPlayerState;
    d->isStreaming = iFalse;
    d->isFinished  = iFalse;
}

void deinit_AndroidAudioPlayer(iAndroidAudioPlayer *d) {
    javaCommand_Android("audio.deinit player:%p", d);
    setInput_AndroidAudioPlayer(d, NULL, NULL);
}

void setupData_AndroidAudioPlayer(iAndroidAudioPlayer *d, const iString *mediaType) {
    d->isStreaming = iTrue;
    javaCommand_Android("audio.init player:%p volume:%.4f mime:%s",
                        d, d->volume, cstr_String(mediaType));
}

void appendData_AndroidAudioPlayer(iAndroidAudioPlayer *d, const void *bytes, size_t size) {
    jboolean   needDetach;
    JNIEnv    *env     = currentJNIEnv_(&needDetach);
    jobject    activity = cachedActivity_; /* global ref — valid from any thread */
    jclass     cls      = (*env)->GetObjectClass(env, activity);
    jmethodID  mid      = (*env)->GetMethodID(env, cls, "appendAudioData", "(Ljava/lang/String;[B)V");
    iString   *ptrStr   = newFormat_String("%p", d);
    jstring    ptrJStr  = (*env)->NewStringUTF(env, cstr_String(ptrStr));
    jbyteArray arr      = (*env)->NewByteArray(env, (jsize) size);
    (*env)->SetByteArrayRegion(env, arr, 0, (jsize) size, (const jbyte *) bytes);
    (*env)->CallVoidMethod(env, activity, mid, ptrJStr, arr);
    (*env)->DeleteLocalRef(env, arr);
    (*env)->DeleteLocalRef(env, ptrJStr);
    (*env)->DeleteLocalRef(env, cls);
    delete_String(ptrStr);
    if (needDetach) (*javaVm_)->DetachCurrentThread(javaVm_);
}

void setComplete_AndroidAudioPlayer(iAndroidAudioPlayer *d) {
    /* TODO: replace this; we can use `appendData_AndroidAudioPlayer` with bytes==NULL */
    jboolean  needDetach;
    JNIEnv   *env      = currentJNIEnv_(&needDetach);
    jobject   activity = cachedActivity_; /* global ref — valid from any thread */
    jclass    cls      = (*env)->GetObjectClass(env, activity);
    jmethodID mid      = (*env)->GetMethodID(env, cls, "completeAudioData", "(Ljava/lang/String;)V");
    iString  *ptrStr   = newFormat_String("%p", d);
    jstring   ptrJStr  = (*env)->NewStringUTF(env, cstr_String(ptrStr));
    (*env)->CallVoidMethod(env, activity, mid, ptrJStr);
    (*env)->DeleteLocalRef(env, ptrJStr);
    (*env)->DeleteLocalRef(env, cls);
    delete_String(ptrStr);
    if (needDetach) (*javaVm_)->DetachCurrentThread(javaVm_);
}

iBool setInput_AndroidAudioPlayer(iAndroidAudioPlayer *d, const iString *mimeType,
                                  const iBlock *audioFileData) {
    if (!isEmpty_String(&d->cacheFilePath)) {
        remove(cstr_String(&d->cacheFilePath));
        clear_String(&d->cacheFilePath);
    }
    if (mimeType && audioFileData) {
        const char *ext = audioFileExt_(mimeType);
        if (!*ext) return iFalse;
        const iString *dir = collectNewCStr_String(audioCacheDir_());
        makeDirs_Path(dir);
        iFile *f = newCStr_File(format_CStr("%s/%u%s", cstr_String(dir), SDL_GetTicks(), ext));
        if (open_File(f, writeOnly_FileMode)) {
            write_File(f, audioFileData);
            set_String(&d->cacheFilePath, path_File(f));
        }
        iRelease(f);
    }
    return !isEmpty_String(&d->cacheFilePath);
}

void play_AndroidAudioPlayer(iAndroidAudioPlayer *d) {
    if (d->state == playing_AndroidAudioPlayerState) {
        return;
    }
    d->currentTime = 0.0f; /* playing from the start */
    if (d->isStreaming) {
        javaCommand_Android("audio.play player:%p volume:%.4f", d, d->volume);
        d->state = playing_AndroidAudioPlayerState;
    }
    else if (!isEmpty_String(&d->cacheFilePath)) {
        javaCommand_Android("audio.play player:%p volume:%.4f path:%s",
                            d, d->volume, cstr_String(&d->cacheFilePath));
        d->state = playing_AndroidAudioPlayerState;
    }
}

void stop_AndroidAudioPlayer(iAndroidAudioPlayer *d) {
    if (d->state != initialized_AndroidAudioPlayerState) {
        javaCommand_Android("audio.stop player:%p", d);
        d->state = initialized_AndroidAudioPlayerState;
    }
}

void setPaused_AndroidAudioPlayer(iAndroidAudioPlayer *d, iBool paused) {
    if (d->state == initialized_AndroidAudioPlayerState) {
        return; /* not started yet */
    }
    if (paused && d->state != paused_AndroidAudioPlayerState) {
        javaCommand_Android("audio.pause player:%p", d);
        d->state = paused_AndroidAudioPlayerState;
    }
    else if (!paused && d->state == paused_AndroidAudioPlayerState) {
        javaCommand_Android("audio.resume player:%p", d);
        d->state = playing_AndroidAudioPlayerState;
    }
}

void setVolume_AndroidAudioPlayer(iAndroidAudioPlayer *d, float volume) {
    d->volume = volume;
    if (d->state != initialized_AndroidAudioPlayerState) {
        javaCommand_Android("audio.volume player:%p volume:%.4f", d, volume);
    }
}

iBool isStarted_AndroidAudioPlayer(const iAndroidAudioPlayer *d) {
    return d->state != initialized_AndroidAudioPlayerState;
}

iBool isPaused_AndroidAudioPlayer(const iAndroidAudioPlayer *d) {
    return d->state == paused_AndroidAudioPlayerState;
}

iBool isFinished_AndroidAudioPlayer(const iAndroidAudioPlayer *d) {
    return d->isFinished;
}

float currentTime_AndroidAudioPlayer(const iAndroidAudioPlayer *d) {
    return d->currentTime;
}

float duration_AndroidAudioPlayer(const iAndroidAudioPlayer *d) {
    return d->duration;
}

/*----------------------------------------------------------------------------------------------*/

void safeAreaInsets_Mobile(float *left, float *top, float *right, float *bottom) {
    if (left) *left = leftInset_;
    if (top) *top = topInset_;
    if (right) *right = rightInset_;
    if (bottom) *bottom = bottomInset_;
}
