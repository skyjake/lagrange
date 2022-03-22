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
#include "ui/command.h"
#include "ui/metrics.h"
#include "ui/mobile.h"

#include <the_Foundation/commandline.h>
#include <jni.h>
#include <SDL.h>

JNIEXPORT void JNICALL Java_fi_skyjake_lagrange_LagrangeActivity_postAppCommand(
        JNIEnv* env, jclass jcls, jstring command)
{
    const char *cmd = (*env)->GetStringUTFChars(env, command, NULL);
    postCommand_Root(NULL, cmd);
    (*env)->ReleaseStringUTFChars(env, command, cmd);
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
    /* Convert the return value string. */
//    iString *result = NULL;
//    const char *chars = (*env)->GetStringUTFChars(env, retVal, NULL);
//    result = newCStr_String(chars);
//    (*env)->ReleaseStringUTFChars(env, retVal, chars);
    (*env)->DeleteLocalRef(env, cmdStr);
    (*env)->DeleteLocalRef(env, activity);
    (*env)->DeleteLocalRef(env, class);
//    deinit_String(&cmd);
//    if (isEmpty_String(result)) {
//        delete_String(result);
//        result = NULL;
//    }
//    return result;
}

/*----------------------------------------------------------------------------------------------*/

struct Impl_SystemTextInput {
    int flags;
    iString text;
    void (*textChangedFunc)(iSystemTextInput *, void *);
    void * textChangedContext;
};

iDefineTypeConstructionArgs(SystemTextInput, (iRect rect, int flags), rect, flags)

static iRect nativeRect_SystemTextInput_(const iSystemTextInput *d, iRect rect) {
    iUnused(d);
    return moved_Rect(rect, init_I2(0, -0.75f * gap_UI));
}

void init_SystemTextInput(iSystemTextInput *d, iRect rect, int flags) {
    d->flags = flags;
    init_String(&d->text);
    d->textChangedFunc = NULL;
    d->textChangedContext = NULL;
    rect = nativeRect_SystemTextInput_(d, rect);
    const iColor fg = get_Color(uiInputTextFocused_ColorId);
    const iColor bg = get_Color(uiInputBackgroundFocused_ColorId);
    const iColor hl = get_Color(uiInputCursor_ColorId);
    javaCommand_Android("input.init ptr:%p x:%d y:%d w:%d h:%d "
                        "gap:%d fontsize:%d "
                        "correct:%d "
                        "autocap:%d "
                        "sendkey:%d "
                        "gokey:%d "
                        "fg0:%d fg1:%d fg2:%d "
                        "bg0:%d bg1:%d bg2:%d "
                        "hl0:%d hl1:%d hl2:%d",
                        d, rect.pos.x, rect.pos.y, rect.size.x, rect.size.y,
                        gap_UI, lineHeight_Text(default_FontId),
                        (flags & disableAutocorrect_SystemTextInputFlag) == 0,
                        (flags & disableAutocapitalize_SystemTextInputFlag) == 0,
                        (flags & returnSend_SystemTextInputFlags) != 0,
                        (flags & returnGo_SystemTextInputFlags) != 0,
                        fg.r, fg.g, fg.b,
                        bg.r, bg.g, bg.b,
                        hl.r, hl.g, hl.b);
}

void deinit_SystemTextInput(iSystemTextInput *d) {
    javaCommand_Android("input.deinit");
    deinit_String(&d->text);
}

void setRect_SystemTextInput(iSystemTextInput *d, iRect rect) {
    rect = nativeRect_SystemTextInput_(d, rect);
    javaCommand_Android("input.setrect x:%d y:%d w:%d h:%d",
                        rect.pos.x, rect.pos.y, rect.size.x, rect.size.y);
}

void setText_SystemTextInput(iSystemTextInput *d, const iString *text, iBool allowUndo) {
    set_String(&d->text, text);
    javaCommand_Android("input.set text:%s", cstr_String(text));
    if (d->flags & selectAll_SystemTextInputFlags) {
        javaCommand_Android("input.selectall");
    }
}

void setFont_SystemTextInput(iSystemTextInput *d, int fontId) {
    javaCommand_Android("input.setfont size:%d", lineHeight_Text(fontId));
}

void setTextChangedFunc_SystemTextInput
        (iSystemTextInput *d, void (*textChangedFunc)(iSystemTextInput *, void *), void *context) {
    d->textChangedFunc = textChangedFunc;
    d->textChangedContext = context;
}

void selectAll_SystemTextInput(iSystemTextInput *d) {
    javaCommand_Android("input.selectall");
}

const iString *text_SystemTextInput(const iSystemTextInput *d) {
    return &d->text;
}

int preferredHeight_SystemTextInput(const iSystemTextInput *d) {
    //iString *res = javaCommand_Android("input.preferredheight");
//    int pref = toInt_String(res);
//    delete_String(res);
    return lineHeight_Text(default_FontId);
}

iBool handleCommand_Android(const char *cmd) {
    if (equal_Command(cmd, "android.input.changed")) {
        iSystemTextInput *sys = pointer_Command(cmd);
        const char *newText = suffixPtr_Command(cmd, "text");
        if (cmp_String(&sys->text, newText)) {
            setCStr_String(&sys->text, newText);
            if (sys->textChangedFunc) {
                sys->textChangedFunc(sys, sys->textChangedContext);
            }
        }
    }
    return iFalse;
}
