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
#include "audio/player.h"
#include "ui/command.h"
#include "ui/window.h"

#include <the_Foundation/file.h>
#include <the_Foundation/fileinfo.h>
#include <the_Foundation/path.h>
#include <SDL_events.h>
#include <SDL_syswm.h>
#include <SDL_timer.h>

#import <AVFAudio/AVFAudio.h>
#import <CoreHaptics/CoreHaptics.h>
#import <UIKit/UIKit.h>
#import <MediaPlayer/MediaPlayer.h>

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

static void notifyChange_SystemTextInput_(iSystemTextInput *);

/*----------------------------------------------------------------------------------------------*/

API_AVAILABLE(ios(13.0))
@interface HapticState : NSObject
@property (nonatomic, strong) CHHapticEngine *engine;
@property (nonatomic, strong) NSDictionary   *tapDef;
@property (nonatomic, strong) NSDictionary   *gentleTapDef;
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
                        CHHapticPatternKeyEventDuration:@0.1,
                        CHHapticPatternKeyEventParameters: @[
                            @{
                                CHHapticPatternKeyParameterID: CHHapticEventParameterIDHapticIntensity,
                                CHHapticPatternKeyParameterValue: @1.0
                            }
                        ]
                    },
                },
            ]
    };
    self.gentleTapDef = @{
        CHHapticPatternKeyPattern:
            @[
                @{
                    CHHapticPatternKeyEvent: @{
                        CHHapticPatternKeyEventType:    CHHapticEventTypeHapticTransient,
                        CHHapticPatternKeyTime:         @0.0,
                        CHHapticPatternKeyEventDuration:@0.1,
                        CHHapticPatternKeyEventParameters: @[
                            @{
                                CHHapticPatternKeyParameterID: CHHapticEventParameterIDHapticIntensity,
                                CHHapticPatternKeyParameterValue: @0.33
                            }
                        ]
                    },
                },
            ]
    };
}


-(void)playHapticEffect:(NSDictionary *)def {
    NSError *error = nil;
    CHHapticPattern *pattern = [[CHHapticPattern alloc] initWithDictionary:def
                                                                     error:&error];
    // TODO: Check the error.
    id<CHHapticPatternPlayer> player = [self.engine createPlayerWithPattern:pattern error:&error];
    // TODO: Check the error.
    [self.engine startWithCompletionHandler:^(NSError *err){
        if (err == nil) {
            NSError *startError = nil;
            [player startAtTime:0.0 error:&startError];
        }
    }];
}

@end

/*----------------------------------------------------------------------------------------------*/

@interface AppState : NSObject<UIDocumentPickerDelegate, UITextFieldDelegate> {
    iString *fileBeingSaved;
    iString *pickFileCommand;
    iSystemTextInput *sysCtrl;
}
@property (nonatomic, assign) BOOL isHapticsAvailable;
@property (nonatomic, strong) NSObject *haptic;
@end

static AppState *appState_;

@implementation AppState

-(instancetype)init {
    self = [super init];
    fileBeingSaved = NULL;
    pickFileCommand = NULL;
    sysCtrl = NULL;
    return self;
}

-(void)setSystemTextInput:(iSystemTextInput *)sys {
    sysCtrl = sys;
}

-(iSystemTextInput *)systemTextInput {
    return sysCtrl;
}

-(void)setPickFileCommand:(const char *)command {
    if (!pickFileCommand) {
        pickFileCommand = new_String();
    }
    setCStr_String(pickFileCommand, command);
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
    if (fileBeingSaved) {
        [self removeSavedFile];
    }
    else {
        /* A file is being opened. */
        NSURL *url = [urls firstObject];
        iString *path = localFilePathFromUrl_String(collectNewCStr_String([[url absoluteString]
                                                                           UTF8String]));
        postCommandf_App("%s temp:1 path:%s",
                         cstr_String(pickFileCommand),
                         cstrCollect_String(path));
        delete_String(pickFileCommand);
        pickFileCommand = NULL;
    }
}

- (void)documentPickerWasCancelled:(UIDocumentPickerViewController *)controller {
    if (fileBeingSaved) {
        [self removeSavedFile];
    }
    if (pickFileCommand) {
        delete_String(pickFileCommand);
        pickFileCommand = NULL;
    }
}

-(void)keyboardOnScreen:(NSNotification *)notification {
    NSDictionary *info   = notification.userInfo;
    NSValue      *value  = info[UIKeyboardFrameEndUserInfoKey];
    CGRect rawFrame      = [value CGRectValue];
    UIView *view         = [viewController_(get_Window()) view];
    CGRect keyboardFrame = [view convertRect:rawFrame fromView:nil];
//    NSLog(@"keyboardFrame: %@", NSStringFromCGRect(keyboardFrame));
    iMainWindow *window = get_MainWindow();
    const iInt2 rootSize = size_Root(window->base.roots[0]);
    const int keyTop = keyboardFrame.origin.y * window->base.pixelRatio;
    setKeyboardHeight_MainWindow(window, rootSize.y - keyTop);
}

-(void)keyboardOffScreen:(NSNotification *)notification {
    setKeyboardHeight_MainWindow(get_MainWindow(), 0);
}

- (BOOL)textFieldShouldReturn:(UITextField *)textField {
    SDL_Event ev = { .type = SDL_KEYDOWN };
    ev.key.keysym.sym = SDLK_RETURN;
    SDL_PushEvent(&ev);
    printf("Return pressed\n");
    return NO;
}

- (BOOL)textField:(UITextField *)textField shouldChangeCharactersInRange:(NSRange)range replacementString:(NSString *)string {
    iSystemTextInput *sysCtrl = [appState_ systemTextInput];
    notifyChange_SystemTextInput_(sysCtrl);
    return YES;
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
    /* Media player remote controls. */
    MPRemoteCommandCenter *commandCenter = [MPRemoteCommandCenter sharedCommandCenter];
    [[commandCenter pauseCommand] addTargetWithHandler:^MPRemoteCommandHandlerStatus(MPRemoteCommandEvent * _Nonnull event) {
        iPlayer *player = active_Player();
        if (player) {
            setPaused_Player(player, iTrue);
            return MPRemoteCommandHandlerStatusSuccess;
        }
        return MPRemoteCommandHandlerStatusCommandFailed;
    }];
    [[commandCenter playCommand] addTargetWithHandler:^MPRemoteCommandHandlerStatus(MPRemoteCommandEvent * _Nonnull event) {
        iPlayer *player = active_Player();
        if (player) {
            if (isPaused_Player(player)) {
                setPaused_Player(player, iFalse);
            }
            else {
                start_Player(player);
            }
            return MPRemoteCommandHandlerStatusSuccess;
        }
        return MPRemoteCommandHandlerStatusCommandFailed;
    }];
    [[commandCenter stopCommand] addTargetWithHandler:^MPRemoteCommandHandlerStatus(MPRemoteCommandEvent * _Nonnull event) {
        iPlayer *player = active_Player();
        if (player) {
            stop_Player(player);
            return MPRemoteCommandHandlerStatusSuccess;
        }
        return MPRemoteCommandHandlerStatusCommandFailed;
    }];
    [[commandCenter togglePlayPauseCommand] addTargetWithHandler:^MPRemoteCommandHandlerStatus(MPRemoteCommandEvent * _Nonnull event) {
        iPlayer *player = active_Player();
        if (player) {
            setPaused_Player(player, !isPaused_Player(player));
            return MPRemoteCommandHandlerStatusSuccess;
        }
        return MPRemoteCommandHandlerStatusCommandFailed;
    }];
    [[commandCenter nextTrackCommand] setEnabled:NO];
    [[commandCenter previousTrackCommand] setEnabled:NO];
    [[commandCenter changeRepeatModeCommand] setEnabled:NO];
    [[commandCenter changeShuffleModeCommand] setEnabled:NO];
    [[commandCenter changePlaybackRateCommand] setEnabled:NO];
    [[commandCenter seekForwardCommand] setEnabled:NO];
    [[commandCenter seekBackwardCommand] setEnabled:NO];
    [[commandCenter skipForwardCommand] setEnabled:NO];
    [[commandCenter skipBackwardCommand] setEnabled:NO];
    [[commandCenter changePlaybackPositionCommand] setEnabled:NO];
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
    return (int) uiWindow_(get_Window()).screen.maximumFramesPerSecond;
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
                [hs playHapticEffect:hs.tapDef];
                break;
            case gentleTap_HapticEffect:
                [hs playHapticEffect:hs.gentleTapDef];
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
        else if (equal_Command(cmd, "theme.changed")) {
            if (@available(iOS 13.0, *)) {
                /* SDL doesn't expose this as a setting, so we'll rely on a hack.
                   Adding an SDL hint for this would be a cleaner solution than calling
                   a private method. */
                UIViewController *vc = viewController_(get_Window());
                SEL sel = NSSelectorFromString(@"setStatusStyle:"); /* custom method */
                if ([vc respondsToSelector:sel]) {
                    NSInvocation *call = [NSInvocation invocationWithMethodSignature:
                                          [NSMethodSignature signatureWithObjCTypes:"v@:i"]];
                    [call setSelector:sel];
                    int style = isDark_ColorTheme(colorTheme_App()) ?
                        UIStatusBarStyleLightContent : UIStatusBarStyleDarkContent;
                    [call setArgument:&style atIndex:2];
                    [call invokeWithTarget:vc];
                }
            }
        }
    }
    return iFalse; /* allow normal processing */
}

void updateNowPlayingInfo_iOS(void) {
    const iPlayer *player = active_Player();
    if (!player) {
        clearNowPlayingInfo_iOS();
        return;
    }
    NSMutableDictionary<NSString *, id> *info = [[NSMutableDictionary<NSString *, id> alloc] init];
    [info setObject:[NSNumber numberWithDouble:time_Player(player)]
            forKey:MPNowPlayingInfoPropertyElapsedPlaybackTime];
    [info setObject:[NSNumber numberWithInt:MPNowPlayingInfoMediaTypeAudio]
            forKey:MPNowPlayingInfoPropertyMediaType];
    [info setObject:[NSNumber numberWithDouble:duration_Player(player)]
             forKey:MPMediaItemPropertyPlaybackDuration];
    const iString *title  = tag_Player(player, title_PlayerTag);
    const iString *artist = tag_Player(player, artist_PlayerTag);
    if (isEmpty_String(title)) {
        title = collectNewCStr_String("Audio"); /* TODO: Use link label or URL file name */
    }
    if (isEmpty_String(artist)) {
        artist = collectNewCStr_String("Lagrange"); /* TODO: Use domain or base URL */
    }
    [info setObject:[NSString stringWithUTF8String:cstr_String(title)]
             forKey:MPMediaItemPropertyTitle];
    [info setObject:[NSString stringWithUTF8String:cstr_String(artist)]
             forKey:MPMediaItemPropertyArtist];
    [[MPNowPlayingInfoCenter defaultCenter] setNowPlayingInfo:info];
}

void clearNowPlayingInfo_iOS(void) {
    [[MPNowPlayingInfoCenter defaultCenter] setNowPlayingInfo:nil];
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

void pickFileForOpening_iOS(void) {
    pickFile_iOS("file.open");
}

void pickFile_iOS(const char *command) {
    [appState_ setPickFileCommand:command];
    UIDocumentPickerViewController *picker = [[UIDocumentPickerViewController alloc]
                                              initWithDocumentTypes:@[@"public.data"]
                                              inMode:UIDocumentPickerModeImport];
    picker.delegate = appState_;
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
    void *player; /* AVAudioPlayer *, no ARC */
    float volume;
    enum iAVFAudioPlayerState state;
};

iDefineTypeConstruction(AVFAudioPlayer)

void init_AVFAudioPlayer(iAVFAudioPlayer *d) {
    init_String(&d->cacheFilePath);
    d->player = NULL;
    d->volume = 1.0f;
    d->state = initialized_AVFAudioPlayerState;
    /* Playback is imminent. */
    [[AVAudioSession sharedInstance] setCategory:AVAudioSessionCategoryPlayback error:nil];
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

#define REF_d_player    (__bridge AVAudioPlayer *)d->player

iBool setInput_AVFAudioPlayer(iAVFAudioPlayer *d, const iString *mimeType, const iBlock *audioFileData) {
    if (!isEmpty_String(&d->cacheFilePath)) {
        remove(cstr_String(&d->cacheFilePath));
        clear_String(&d->cacheFilePath);
    }
    if (d->player) {
        CFBridgingRelease(d->player);
        d->player = nil;
    }
    if (mimeType && audioFileData && iCmpStr(fileExt_(mimeType), "")) {
        makeDirs_Path(collectNewCStr_String(cacheDir_));
        iFile *f = new_File(collectNewFormat_String("%s/%u%s", cacheDir_, SDL_GetTicks(), fileExt_(mimeType)));
        if (open_File(f, writeOnly_FileMode)) {
            write_File(f, audioFileData);
            set_String(&d->cacheFilePath, path_File(f));
            NSError *error = nil;
            d->player = (void *) CFBridgingRetain([[AVAudioPlayer alloc]
                         initWithContentsOfURL:[NSURL fileURLWithPath:
                                                [NSString stringWithUTF8String:cstr_String(&d->cacheFilePath)]]
                         error:&error]);
            if (error) {
                d->player = nil;
            }
            [REF_d_player setVolume:d->volume];
        }
        iRelease(f);
    }
    return d->player != nil;
}

void play_AVFAudioPlayer(iAVFAudioPlayer *d) {
    if (d->state != playing_AVFAudioPlayerState) {
        [REF_d_player play];
        d->state = playing_AVFAudioPlayerState;
    }
}

void stop_AVFAudioPlayer(iAVFAudioPlayer *d) {
    [REF_d_player stop];
    d->state = initialized_AVFAudioPlayerState;
}

void setPaused_AVFAudioPlayer(iAVFAudioPlayer *d, iBool paused) {
    if (paused && d->state != paused_AVFAudioPlayerState) {
        [REF_d_player pause];
        d->state = paused_AVFAudioPlayerState;
    }
    else if (!paused && d->state != playing_AVFAudioPlayerState) {
        [REF_d_player play];
        d->state = playing_AVFAudioPlayerState;
    }
}

void setVolume_AVFAudioPlayer(iAVFAudioPlayer *d, float volume) {
    d->volume = volume;
    if (d->player) {
        [REF_d_player setVolume:volume];
    }
}

double currentTime_AVFAudioPlayer(const iAVFAudioPlayer *d) {
    return [REF_d_player currentTime];
}

double duration_AVFAudioPlayer(const iAVFAudioPlayer *d) {
    return [REF_d_player duration];
}

iBool isStarted_AVFAudioPlayer(const iAVFAudioPlayer *d) {
    return d->state != initialized_AVFAudioPlayerState;
}

iBool isPaused_AVFAudioPlayer(const iAVFAudioPlayer *d) {
    return d->state == paused_AVFAudioPlayerState;
}

/*----------------------------------------------------------------------------------------------*/

struct Impl_SystemTextInput {
    void *ctrl;
    void (*textChangedFunc)(iSystemTextInput *, void *);
    void *textChangedContext;
};

iDefineTypeConstructionArgs(SystemTextInput, (int flags), flags)

#define REF_d_ctrl  (__bridge UITextField *)d->ctrl

void init_SystemTextInput(iSystemTextInput *d, int flags) {
    d->ctrl = (void *) CFBridgingRetain([[UITextField alloc] init]);
    UITextField *field = REF_d_ctrl;
    // TODO: Use the right font:  https://developer.apple.com/documentation/uikit/text_display_and_fonts/adding_a_custom_font_to_your_app?language=objc
    [[viewController_(get_Window()) view] addSubview:REF_d_ctrl];
    if (flags & returnGo_SystemTextInputFlags) {
        [field setReturnKeyType:UIReturnKeyGo];
    }
    if (flags & returnSend_SystemTextInputFlags) {
        [field setReturnKeyType:UIReturnKeySend];
    }
    if (flags & disableAutocorrect_SystemTextInputFlag) {
        [field setAutocorrectionType:UITextAutocorrectionTypeNo];
        [field setAutocapitalizationType:UITextAutocapitalizationTypeNone];
        [field setSpellCheckingType:UITextSpellCheckingTypeNo];
    }
    if (flags & alignRight_SystemTextInputFlag) {
        [field setTextAlignment:NSTextAlignmentRight];
    }
    [field setDelegate:appState_];
    [field becomeFirstResponder];
    d->textChangedFunc = NULL;
    d->textChangedContext = NULL;
    [appState_ setSystemTextInput:d];
}

void deinit_SystemTextInput(iSystemTextInput *d) {
    [appState_ setSystemTextInput:nil];
    [REF_d_ctrl removeFromSuperview];
    d->ctrl = nil; // TODO: Does this need to be released??
}

void setText_SystemTextInput(iSystemTextInput *d, const iString *text) {
    [REF_d_ctrl setText:[NSString stringWithUTF8String:cstr_String(text)]];
    [REF_d_ctrl selectAll:nil];
}

void setFont_SystemTextInput(iSystemTextInput *d, int fontId) {
    int height = lineHeight_Text(fontId);
    UIFont *font = [UIFont systemFontOfSize:0.65f * height / get_Window()->pixelRatio];
    [REF_d_ctrl setFont:font];
}

const iString *text_SystemTextInput(const iSystemTextInput *d) {
    return collectNewCStr_String([[REF_d_ctrl text] cStringUsingEncoding:NSUTF8StringEncoding]);;
}

void setRect_SystemTextInput(iSystemTextInput *d, iRect rect) {
    const iWindow *win = get_Window();
    CGRect frame;
    frame.origin.x = rect.pos.x / win->pixelRatio;
    frame.origin.y = (rect.pos.y - gap_UI + 2) / win->pixelRatio;
    frame.size.width = rect.size.x / win->pixelRatio;
    frame.size.height = rect.size.y / win->pixelRatio;
    [REF_d_ctrl setFrame:frame];
}

void setTextChangedFunc_SystemTextInput(iSystemTextInput *d,
                                        void (*textChangedFunc)(iSystemTextInput *, void *),
                                        void *context) {
    d->textChangedFunc = textChangedFunc;
    d->textChangedContext = context;
}

static void notifyChange_SystemTextInput_(iSystemTextInput *d) {
    if (d && d->textChangedFunc) {
        d->textChangedFunc(d, d->textChangedContext);
    }
}
