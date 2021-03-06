/* Copyright 2021 Jaakko Keränen <jaakko.keranen@iki.fi>

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

#include "ios.h"
#include "app.h"
#include "ui/window.h"
#include <SDL_events.h>
#include <SDL_syswm.h>

#import <UIKit/UIKit.h>

static iBool isSystemDarkMode_ = iFalse;
static iBool isPhone_ = iFalse;

static UIViewController *viewController_(iWindow *window) {
    SDL_SysWMinfo wm;
    SDL_VERSION(&wm.version);
    if (SDL_GetWindowWMInfo(window->win, &wm)) {
        return wm.info.uikit.window.rootViewController;
    }
    iAssert(false);
    return NULL;
}

@interface KeyboardObserver : NSObject
-(void)keyboardOnScreen:(NSNotification *)notification;
@end

@implementation KeyboardObserver
-(void)keyboardOnScreen:(NSNotification *)notification {
    NSDictionary *info   = notification.userInfo;
    NSValue      *value  = info[UIKeyboardFrameEndUserInfoKey];
    CGRect rawFrame      = [value CGRectValue];
    UIView *view         = [viewController_(get_Window()) view];
    CGRect keyboardFrame = [view convertRect:rawFrame fromView:nil];
//    NSLog(@"keyboardFrame: %@", NSStringFromCGRect(keyboardFrame));
    iWindow *window = get_Window();
    const iInt2 rootSize = rootSize_Window(window);
    const int keyTop = keyboardFrame.origin.y * window->pixelRatio;
    setKeyboardHeight_Window(window, rootSize.y - keyTop);
}

-(void)keyboardOffScreen:(NSNotification *)notification {
    setKeyboardHeight_Window(get_Window(), 0);
}
@end

static void enableMouse_(iBool yes) {
    SDL_EventState(SDL_MOUSEBUTTONDOWN, yes);
    SDL_EventState(SDL_MOUSEMOTION, yes);
    SDL_EventState(SDL_MOUSEBUTTONUP, yes);
}

KeyboardObserver *keyObs_;

void setupApplication_iOS(void) {
    enableMouse_(iFalse);
    NSString *deviceModel = [[UIDevice currentDevice] model];
    if ([deviceModel isEqualToString:@"iPhone"]) {
        isPhone_ = iTrue;
    }
    keyObs_ = [[KeyboardObserver alloc] init];
    NSNotificationCenter *center = [NSNotificationCenter defaultCenter];
    [center addObserver:keyObs_
               selector:@selector(keyboardOnScreen:)
                   name:UIKeyboardWillShowNotification
                 object:nil];
    [center addObserver:keyObs_
               selector:@selector(keyboardOffScreen:)
                   name:UIKeyboardWillHideNotification
                 object:nil];
}

static iBool isDarkMode_(iWindow *window) {
    UIViewController *ctl = viewController_(window);
    if (ctl) {
        UITraitCollection *traits = ctl.traitCollection;
        if (@available(iOS 12.0, *)) {
            return (traits.userInterfaceStyle == UIUserInterfaceStyleDark);
        }
    }
    return iFalse;
}

void safeAreaInsets_iOS(float *left, float *top, float *right, float *bottom) {
    iWindow *window = get_Window();
    UIViewController *ctl = viewController_(window);
    if (@available(iOS 11.0, *)) {
        const UIEdgeInsets safe = ctl.view.safeAreaInsets;
        if (left) *left = safe.left * window->pixelRatio;
        if (top) *top = safe.top * window->pixelRatio;
        if (right) *right = safe.right * window->pixelRatio;
        if (bottom) *bottom = safe.bottom * window->pixelRatio;
    } else {
        if (left) *left = 0.0f;
        if (top) *top = 0.0f;
        if (right) *right = 0.0f;
        if (bottom) *bottom = 0.0f;
    }
}

iBool isPhone_iOS(void) {
    return isPhone_;
}

void setupWindow_iOS(iWindow *window) {
    UIViewController *ctl = viewController_(window);
    isSystemDarkMode_ = isDarkMode_(window);
    postCommandf_App("~os.theme.changed dark:%d contrast:1", isSystemDarkMode_ ? 1 : 0);
}

iBool processEvent_iOS(const SDL_Event *ev) {
    if (ev->type == SDL_WINDOWEVENT) {
        if (ev->window.event == SDL_WINDOWEVENT_RESTORED) {
            const iBool isDark = isDarkMode_(get_Window());
            if (isDark != isSystemDarkMode_) {
                isSystemDarkMode_ = isDark;
                postCommandf_App("~os.theme.changed dark:%d contrast:1", isSystemDarkMode_ ? 1 : 0);
            }
        }
    }
    return iFalse; /* allow normal processing */
}
