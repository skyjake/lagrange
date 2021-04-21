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
#include "ui/command.h"
#include "ui/window.h"

#include <the_Foundation/file.h>
#include <the_Foundation/fileinfo.h>
#include <the_Foundation/path.h>
#include <SDL_events.h>
#include <SDL_syswm.h>
#include <SDL_timer.h>

#import <UIKit/UIKit.h>
#import <CoreHaptics/CoreHaptics.h>
#import <AVFAudio/AVFAudio.h>

static iBool isSystemDarkMode_ = iFalse;
static iBool isPhone_          = iFalse;

static UIWindow *uiWindow_(iWindow *window) {
    SDL_SysWMinfo wm;
    SDL_VERSION(&wm.version);
    if (SDL_GetWindowWMInfo(window->win, &wm)) {
        return wm.info.uikit.window;
    }
    iAssert(false);
    return NULL;
}

static UIViewController *viewController_(iWindow *window) {
    UIWindow *uiWin = uiWindow_(window);
    if (uiWin) {
        return uiWin.rootViewController;
    }
    iAssert(false);
    return NULL;
}

/*----------------------------------------------------------------------------------------------*/

API_AVAILABLE(ios(13.0))
@interface HapticState : NSObject
@property (nonatomic, strong) CHHapticEngine *engine;
@property (nonatomic, strong) NSDictionary   *tapDef;
@end

@implementation HapticState

-(void)setup {
    NSError *error;
    self.engine = [[CHHapticEngine alloc] initAndReturnError:&error];
    __weak HapticState *hs = self;
    [self.engine setResetHandler:^{
        NSLog(@"Haptic engine reset");
        NSError *startupError;
        [hs.engine startAndReturnError:&startupError];
        if (startupError) {
            NSLog(@"Engine couldn't restart");
        }
        else {
            // TODO: Create pattern players.
        }
    }];
    [self.engine setStoppedHandler:^(CHHapticEngineStoppedReason reason){
        NSLog(@"Haptic engine stopped");
        switch (reason) {
            case CHHapticEngineStoppedReasonAudioSessionInterrupt:
                break;
            case CHHapticEngineStoppedReasonApplicationSuspended:
                break;
            case CHHapticEngineStoppedReasonIdleTimeout:
                break;
            case CHHapticEngineStoppedReasonSystemError:
                break;
            default:
                break;
        }
    }];
    self.tapDef = @{
        CHHapticPatternKeyPattern:
            @[
                @{
                    CHHapticPatternKeyEvent: @{
                        CHHapticPatternKeyEventType:    CHHapticEventTypeHapticTransient,
                        CHHapticPatternKeyTime:         @0.0,
                        CHHapticPatternKeyEventDuration:@0.1
                    },
                },
            ],
    };
}

-(void)playTapEffect {
    NSError *error = nil;
    CHHapticPattern *pattern = [[CHHapticPattern alloc] initWithDictionary:self.tapDef
                                                                     error:&error];
    // TODO: Check the error.
    id<CHHapticPatternPlayer> player = [self.engine createPlayerWithPattern:pattern error:&error];
    // TODO: Check the error.
    [self.engine startWithCompletionHandler:^(NSError *err){
        if (err == nil) {
            /* Just keep it running. */
//            [self.engine notifyWhenPlayersFinished:^(NSError * _Nullable error) {
//                return CHHapticEngineFinishedActionStopEngine;
//            }];
            NSError *startError = nil;
            [player startAtTime:0.0 error:&startError];
        }
    }];
}

@end

/*----------------------------------------------------------------------------------------------*/

@interface AppState : NSObject<UIDocumentPickerDelegate> {
    iString *fileBeingSaved;
}
@property (nonatomic, assign) BOOL isHapticsAvailable;
@property (nonatomic, strong) NSObject *haptic;
@end

static AppState *appState_;

@implementation AppState

-(instancetype)init {
    self = [super init];
    fileBeingSaved = NULL;
    return self;
}

-(void)setFileBeingSaved:(const iString *)path {
    fileBeingSaved = copy_String(path);
}

-(void)removeSavedFile {
    /* The file was copied to an external location, so the cached copy is not needed. */
    remove(cstr_String(fileBeingSaved));
    delete_String(fileBeingSaved);
    fileBeingSaved = NULL;
}

-(void)setupHaptics {
    if (@available(iOS 13.0, *)) {
        self.isHapticsAvailable = CHHapticEngine.capabilitiesForHardware.supportsHaptics;
        if (self.isHapticsAvailable) {
            HapticState *hs = [[HapticState alloc] init];
            [hs setup];
            self.haptic = hs;
            /* We start the engine and keep it running. */
            NSError *err;
            [hs.engine startAndReturnError:&err];
        }
    } else {
        self.isHapticsAvailable = NO;
    }
}

- (void)documentPicker:(UIDocumentPickerViewController *)controller
didPickDocumentsAtURLs:(NSArray<NSURL *> *)urls {
    [self removeSavedFile];
}

- (void)documentPickerWasCancelled:(UIDocumentPickerViewController *)controller {
    [self removeSavedFile];
}

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

void setupApplication_iOS(void) {
    enableMouse_(iFalse);
    NSString *deviceModel = [[UIDevice currentDevice] model];
    if ([deviceModel isEqualToString:@"iPhone"]) {
        isPhone_ = iTrue;
    }
    appState_ = [[AppState alloc] init];
    [appState_ setupHaptics];
    NSNotificationCenter *center = [NSNotificationCenter defaultCenter];
    [center addObserver:appState_
               selector:@selector(keyboardOnScreen:)
                   name:UIKeyboardWillShowNotification
                 object:nil];
    [center addObserver:appState_
               selector:@selector(keyboardOffScreen:)
                   name:UIKeyboardWillHideNotification
                 object:nil];
    [[AVAudioSession sharedInstance] setCategory:AVAudioSessionCategoryPlayback error:nil];
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

int displayRefreshRate_iOS(void) {
    return uiWindow_(get_Window()).screen.maximumFramesPerSecond;
}

void setupWindow_iOS(iWindow *window) {
    UIViewController *ctl = viewController_(window);
    isSystemDarkMode_ = isDarkMode_(window);
    postCommandf_App("~os.theme.changed dark:%d contrast:1", isSystemDarkMode_ ? 1 : 0);
}

void playHapticEffect_iOS(enum iHapticEffect effect) {
    if (@available(iOS 13.0, *)) {
        HapticState *hs = (HapticState *) appState_.haptic;
        switch(effect) {
            case tap_HapticEffect:
                [hs playTapEffect];
                break;
        }
    }
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
    else if (ev->type == SDL_USEREVENT && ev->user.code == command_UserEventCode) {
        const char *cmd = command_UserEvent(ev);
        if (equal_Command(cmd, "ostheme")) {
            if (arg_Command(cmd)) {
                postCommandf_App("os.theme.changed dark:%d contrast:1", isSystemDarkMode_ ? 1 : 0);
            }
        }
    }
    return iFalse; /* allow normal processing */
}

void exportDownloadedFile_iOS(const iString *path) {
    NSURL *url = [NSURL fileURLWithPath:[[NSString alloc] initWithCString:cstr_String(path)
                                                                 encoding:NSUTF8StringEncoding]];
    UIDocumentPickerViewController *picker = [[UIDocumentPickerViewController alloc]
                                              initWithURL:url
                                              inMode:UIDocumentPickerModeExportToService];
    picker.delegate = appState_;
    [appState_ setFileBeingSaved:path];
    [viewController_(get_Window()) presentViewController:picker animated:YES completion:nil];
}

/*----------------------------------------------------------------------------------------------*/

enum iAVFAudioPlayerState {
    initialized_AVFAudioPlayerState,
    playing_AVFAudioPlayerState,
    paused_AVFAudioPlayerState
};

struct Impl_AVFAudioPlayer {
    iString cacheFilePath;
    AVAudioPlayer *player;
    float volume;
    enum iAVFAudioPlayerState state;
};

iDefineTypeConstruction(AVFAudioPlayer)

void init_AVFAudioPlayer(iAVFAudioPlayer *d) {
    init_String(&d->cacheFilePath);
    d->player = NULL;
    d->volume = 1.0f;
    d->state = initialized_AVFAudioPlayerState;
}

void deinit_AVFAudioPlayer(iAVFAudioPlayer *d) {
    setInput_AVFAudioPlayer(d, NULL, NULL);
}

static const char *cacheDir_ = "~/Library/Caches/Audio";

static const char *fileExt_(const iString *mimeType) {
    /* Media types that AVFAudioPlayer will try to play. */
    if (startsWithCase_String(mimeType, "audio/aiff") ||
        startsWithCase_String(mimeType, "audio/x-aiff")) {
        return ".aiff";
    }
    if (startsWithCase_String(mimeType, "audio/3gpp"))  return ".3gpp";
    if (startsWithCase_String(mimeType, "audio/mpeg"))  return ".mp3";
    if (startsWithCase_String(mimeType, "audio/mp3"))   return ".mp3";
    if (startsWithCase_String(mimeType, "audio/mp4"))   return ".mp4";
    if (startsWithCase_String(mimeType, "audio/mpeg4")) return ".mp4";
    if (startsWithCase_String(mimeType, "audio/aac"))   return ".aac";
    return "";
}

iBool setInput_AVFAudioPlayer(iAVFAudioPlayer *d, const iString *mimeType, const iBlock *audioFileData) {
    if (!isEmpty_String(&d->cacheFilePath)) {
        remove(cstr_String(&d->cacheFilePath));
        clear_String(&d->cacheFilePath);
    }
    if (d->player) {
        d->player = nil;
    }
    if (mimeType && audioFileData && iCmpStr(fileExt_(mimeType), "")) {
        makeDirs_Path(collectNewCStr_String(cacheDir_));
        iFile *f = new_File(collectNewFormat_String("%s/%u%s", cacheDir_, SDL_GetTicks(), fileExt_(mimeType)));
        if (open_File(f, writeOnly_FileMode)) {
            write_File(f, audioFileData);
            set_String(&d->cacheFilePath, path_File(f));
            NSError *error = nil;
            d->player = [[AVAudioPlayer alloc]
                         initWithContentsOfURL:[NSURL fileURLWithPath:
                                                [NSString stringWithUTF8String:cstr_String(&d->cacheFilePath)]]
                         error:&error];
            if (error) {
                d->player = nil;
            }
            [d->player setVolume:d->volume];
        }
        iRelease(f);
    }
    return d->player != nil;
}

void play_AVFAudioPlayer(iAVFAudioPlayer *d) {
    if (d->state != playing_AVFAudioPlayerState) {
        [d->player play];
        d->state = playing_AVFAudioPlayerState;
    }
}

void stop_AVFAudioPlayer(iAVFAudioPlayer *d) {
    [d->player stop];
    d->state = initialized_AVFAudioPlayerState;
}

void setPaused_AVFAudioPlayer(iAVFAudioPlayer *d, iBool paused) {
    if (paused && d->state != paused_AVFAudioPlayerState) {
        [d->player pause];
        d->state = paused_AVFAudioPlayerState;
    }
    else if (!paused && d->state != playing_AVFAudioPlayerState) {
        [d->player play];
        d->state = playing_AVFAudioPlayerState;
    }
}

void setVolume_AVFAudioPlayer(iAVFAudioPlayer *d, float volume) {
    d->volume = volume;
    if (d->player) {
        [d->player setVolume:volume];
    }
}

double currentTime_AVFAudioPlayer(const iAVFAudioPlayer *d) {
    return [d->player currentTime];
}

double duration_AVFAudioPlayer(const iAVFAudioPlayer *d) {
    return [d->player duration];
}

iBool isStarted_AVFAudioPlayer(const iAVFAudioPlayer *d) {
    return d->state != initialized_AVFAudioPlayerState;
}

iBool isPaused_AVFAudioPlayer(const iAVFAudioPlayer *d) {
    return d->state == paused_AVFAudioPlayerState;
}
