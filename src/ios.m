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
#include "ui/touch.h"

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
static BOOL isNewlineAllowed_SystemTextInput_(const iSystemTextInput *);

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

@interface AppState : NSObject<UIDocumentPickerDelegate, UITextFieldDelegate, UITextViewDelegate,
                               UIScrollViewDelegate, NSLayoutManagerDelegate> {
    iString *fileBeingSaved;
    iString *pickFileCommand;
    iSystemTextInput *sysCtrl;
    float sysCtrlLineSpacing;
}
@property (nonatomic, assign) BOOL isHapticsAvailable;
@property (nonatomic, strong) NSObject *haptic;
@end

static AppState *appState_;
static UIScrollView *statusBarTapper_; /* dummy scroll view just for getting notified of taps */

@implementation AppState

-(instancetype)init {
    self = [super init];
    fileBeingSaved = NULL;
    pickFileCommand = NULL;
    sysCtrl = NULL;
    sysCtrlLineSpacing = 0.0f;
    return self;
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

- (BOOL)scrollViewShouldScrollToTop:(UIScrollView *)scrollView {
    postCommand_App("scroll.top smooth:1");
    return NO;
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

static void sendReturnKeyPress_(void) {
    SDL_Event ev = { .type = SDL_KEYDOWN };
    ev.key.timestamp = SDL_GetTicks();
    ev.key.keysym.sym = SDLK_RETURN;
    ev.key.state = SDL_PRESSED;
    SDL_PushEvent(&ev);
    ev.type = SDL_KEYUP;
    ev.key.state = SDL_RELEASED;
    SDL_PushEvent(&ev);
}

- (BOOL)textFieldShouldReturn:(UITextField *)textField {
    sendReturnKeyPress_();
    return NO;
}

-(void)setSystemTextInput:(iSystemTextInput *)sys {
    sysCtrl = sys;
}

-(void)setSystemTextLineSpacing:(float)height {
    sysCtrlLineSpacing = height;
}

-(iSystemTextInput *)systemTextInput {
    return sysCtrl;
}

- (CGFloat)layoutManager:(NSLayoutManager *)layoutManager
        lineSpacingAfterGlyphAtIndex:(NSUInteger)glyphIndex
        withProposedLineFragmentRect:(CGRect)rect {
    return sysCtrlLineSpacing / get_MainWindow()->base.pixelRatio;
}

- (BOOL)textField:(UITextField *)textField shouldChangeCharactersInRange:(NSRange)range
replacementString:(NSString *)string {
    iSystemTextInput *sysCtrl = [appState_ systemTextInput];
    notifyChange_SystemTextInput_(sysCtrl);
    return YES;
}

- (BOOL)textView:(UITextView *)textView shouldChangeTextInRange:(NSRange)range
 replacementText:(NSString *)text {
    if ([text isEqualToString:@"\n"]) {
        if (!isNewlineAllowed_SystemTextInput_([appState_ systemTextInput])) {
            sendReturnKeyPress_();
            return NO;
        }
    }
    return YES;
}

- (void)textViewDidChange:(UITextView *)textView {
    iSystemTextInput *sysCtrl = [appState_ systemTextInput];
    notifyChange_SystemTextInput_(sysCtrl);
}

@end

/*----------------------------------------------------------------------------------------------*/

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
    /* A hack to get notified on status bar taps. We create a thin dummy UIScrollView
       that occupies the top of the screen where the status bar is located. */ {
        CGRect statusBarFrame = [UIApplication sharedApplication].statusBarFrame;
        statusBarTapper_ = [[UIScrollView alloc] initWithFrame:statusBarFrame];
//        [statusBarTapper_ setBackgroundColor:[UIColor greenColor]]; /* to see where it is */
        [statusBarTapper_ setShowsVerticalScrollIndicator:NO];
        [statusBarTapper_ setShowsHorizontalScrollIndicator:NO];
        [statusBarTapper_ setContentSize:(CGSize){ 10000, 10000 }];
        [statusBarTapper_ setContentOffset:(CGPoint){ 0, 1000 }];
        [statusBarTapper_ setScrollsToTop:YES];
        [statusBarTapper_ setDelegate:appState_];
        [ctl.view addSubview:statusBarTapper_];
    }
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
    if (ev->type == SDL_DISPLAYEVENT) {
        if (deviceType_App() == phone_AppDeviceType) {
            [statusBarTapper_ setHidden:(ev->display.data1 == SDL_ORIENTATION_LANDSCAPE ||
                                     ev->display.data1 == SDL_ORIENTATION_LANDSCAPE_FLIPPED)];
        }
        [statusBarTapper_ setFrame:[UIApplication sharedApplication].statusBarFrame];
        return iFalse;
    }
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

void pickFile_iOS(const char *command) {
    [appState_ setPickFileCommand:command];
    UIDocumentPickerViewController *picker = [[UIDocumentPickerViewController alloc]
                                              initWithDocumentTypes:@[@"public.data"]
                                              inMode:UIDocumentPickerModeImport];
    picker.delegate = appState_;
    [viewController_(get_Window()) presentViewController:picker animated:YES completion:nil];
}

static void openActivityView_(NSArray *activityItems) {
    UIActivityViewController *actView =
        [[UIActivityViewController alloc]
         initWithActivityItems:activityItems
         applicationActivities:nil];
    iWindow *win = get_Window();
    UIViewController *viewCtl = viewController_(win);
    UIPopoverPresentationController *popover = [actView popoverPresentationController];
    if (popover) {
        [popover setSourceView:[viewCtl view]];
        iInt2 tapPos = latestTapPosition_Touch();
        tapPos.x /= win->pixelRatio;
        tapPos.y /= win->pixelRatio;
        [popover setSourceRect:(CGRect){{tapPos.x - 10, tapPos.y - 10}, {20, 20}}];
        [popover setCanOverlapSourceViewRect:YES];
    }
    [viewCtl presentViewController:actView animated:YES completion:nil];
}

void openTextActivityView_iOS(const iString *text) {
    openActivityView_(@[[NSString stringWithUTF8String:cstr_String(text)]]);
}

void openFileActivityView_iOS(const iString *path) {
    NSURL *url = [NSURL fileURLWithPath:[[NSString alloc] initWithCString:cstr_String(path)
                                                                 encoding:NSUTF8StringEncoding]];
    openActivityView_(@[url]);
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
    if (d->player) {
        CFBridgingRelease(d->player);
        d->player = nil;
    }
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
    int flags;
    void *field; /* single-line text field */
    void *view;  /* multi-line text view */
    void (*textChangedFunc)(iSystemTextInput *, void *);
    void *textChangedContext;
};

iDefineTypeConstructionArgs(SystemTextInput, (iRect rect, int flags), rect, flags)

#define REF_d_field  (__bridge UITextField *)d->field
#define REF_d_view   (__bridge UITextView *)d->view

static CGRect convertToCGRect_(const iRect *rect, iBool expanded) {
    const iWindow *win = get_Window();
    CGRect frame;
    // TODO: Convert coordinates properly!
    frame.origin.x = rect->pos.x / win->pixelRatio;
    frame.origin.y = (rect->pos.y - gap_UI * 0.875f) / win->pixelRatio;
    frame.size.width = rect->size.x / win->pixelRatio;
    frame.size.height = rect->size.y / win->pixelRatio;
    /* Some padding to account for insets. If we just zero out the insets, the insertion point
       may be clipped at the edges. */
    const float inset = gap_UI / get_Window()->pixelRatio;
    if (expanded) {
        frame.origin.x -= 2 * inset;
        frame.origin.y -= inset;
        frame.size.width += 4 * inset;
        frame.size.height += 2.5f * inset;
    }
    return frame;
}

static UIColor *makeUIColor_(enum iColorId colorId) {
    iColor color = get_Color(colorId);
    return [UIColor colorWithRed:color.r / 255.0
                           green:color.g / 255.0
                            blue:color.b / 255.0
                           alpha:color.a / 255.0];
}

void init_SystemTextInput(iSystemTextInput *d, iRect rect, int flags) {
    d->flags = flags;
    d->field = NULL;
    d->view  = NULL;
    CGRect frame = convertToCGRect_(&rect, (flags & multiLine_SystemTextInputFlags) != 0);
    if (flags & multiLine_SystemTextInputFlags) {
        d->view = (void *) CFBridgingRetain([[UITextView alloc] initWithFrame:frame textContainer:nil]);
        [[viewController_(get_Window()) view] addSubview:REF_d_view];
    }
    else {
        d->field = (void *) CFBridgingRetain([[UITextField alloc] initWithFrame:frame]);
        [[viewController_(get_Window()) view] addSubview:REF_d_field];
    }
    UIControl<UITextInputTraits> *traits = (UIControl<UITextInputTraits> *) (d->view ? REF_d_view : REF_d_field);
    if (~flags & insertNewlines_SystemTextInputFlag) {
        [traits setReturnKeyType:UIReturnKeyDone];
    }
    if (flags & returnGo_SystemTextInputFlags) {
        [traits setReturnKeyType:UIReturnKeyGo];
    }
    if (flags & returnSend_SystemTextInputFlags) {
        [traits setReturnKeyType:UIReturnKeySend];
    }
    if (flags & disableAutocorrect_SystemTextInputFlag) {
        [traits setAutocorrectionType:UITextAutocorrectionTypeNo];
        [traits setSpellCheckingType:UITextSpellCheckingTypeNo];
    }
    if (flags & disableAutocapitalize_SystemTextInputFlag) {
        [traits setAutocapitalizationType:UITextAutocapitalizationTypeNone];
    }
    if (flags & alignRight_SystemTextInputFlag) {
        if (d->field) {
            [REF_d_field setTextAlignment:NSTextAlignmentRight];
        }
        if (d->view) {
            [REF_d_view setTextAlignment:NSTextAlignmentRight];
        }
    }
    UIColor *textColor       = makeUIColor_(uiInputTextFocused_ColorId);
    UIColor *backgroundColor = makeUIColor_(uiInputBackgroundFocused_ColorId);
    UIColor *tintColor       = makeUIColor_(uiInputCursor_ColorId);
    [appState_ setSystemTextInput:d];
    const float inset = gap_UI / get_Window()->pixelRatio;
    if (d->field) {
        UITextField *field = REF_d_field;
        [field setTextColor:textColor];
        [field setTintColor:tintColor];
//        [field setBackgroundColor:[UIColor colorWithRed:1.0f green:0.0f blue:0.0f alpha:0.5f]];
        [field setDelegate:appState_];
        [field becomeFirstResponder];
    }
    else {
        UITextView *view = REF_d_view;
        [[view layoutManager] setDelegate:appState_];
        [view setBackgroundColor:[UIColor colorWithWhite:1.0f alpha:0.0f]];
//        [view setBackgroundColor:[UIColor colorWithRed:1.0f green:0.0f blue:0.0f alpha:0.5f]];
        [view setTextColor:textColor];
        [view setTintColor:tintColor];
        if (flags & extraPadding_SystemTextInputFlag) {
            [view setContentInset:(UIEdgeInsets){ -inset * 0.125f, inset * 0.875f, 3 * inset, 0}];
        }
        else {
            [view setContentInset:(UIEdgeInsets){ inset / 2, inset * 0.875f, -inset / 2, inset / 2 }];
        }
        [view setEditable:YES];
        [view setDelegate:appState_];
        [view becomeFirstResponder];
    }
    d->textChangedFunc = NULL;
    d->textChangedContext = NULL;
}

void deinit_SystemTextInput(iSystemTextInput *d) {
    [appState_ setSystemTextInput:nil];
    if (d->field) {
        [REF_d_field removeFromSuperview];
        CFBridgingRelease(d->field);
        d->field = nil;
    }
    if (d->view) {
        [REF_d_view removeFromSuperview];
        CFBridgingRelease(d->view);
        d->view = nil;
    }
}

void selectAll_SystemTextInput(iSystemTextInput *d) {
    if (d->field) {
        [REF_d_field selectAll:nil];
    }
    if (d->view) {
        [REF_d_view selectAll:nil];
    }
}

void setText_SystemTextInput(iSystemTextInput *d, const iString *text, iBool allowUndo) {
    NSString *str = [NSString stringWithUTF8String:cstr_String(text)];
    if (d->field) {
        [REF_d_field setText:str];
        if (d->flags & selectAll_SystemTextInputFlags) {
            [REF_d_field selectAll:nil];
        }
    }
    else {
        UITextView *view = REF_d_view;
//        if (allowUndo) {
//            [view selectAll:nil];
//            if ([view shouldChangeTextInRange:[view selectedTextRange] replacementText:@""]) {
//                [[view textStorage] beginEditing];
//                [[view textStorage] replaceCharactersInRange:[view selectedRange] withString:@""];
//                [[view textStorage] endEditing];
//            }
//        }
//        else {
        // TODO: How to implement `allowUndo`, given that UITextView does not exist when unfocused?
        // Maybe keep the UITextStorage (if it has the undo?)?
        [view setText:str];
//        }
        if (d->flags & selectAll_SystemTextInputFlags) {
            [view selectAll:nil];
        }
    }
}

int preferredHeight_SystemTextInput(const iSystemTextInput *d) {
    if (d->view) {
        CGRect usedRect = [[REF_d_view layoutManager] usedRectForTextContainer:[REF_d_view textContainer]];
        return usedRect.size.height * get_Window()->pixelRatio;
    }
    return 0;
}

void setFont_SystemTextInput(iSystemTextInput *d, int fontId) {
    float height = lineHeight_Text(fontId) / get_Window()->pixelRatio;
    UIFont *font;
//            for (NSString *name in [UIFont familyNames]) {
//                printf("family: %s\n", [name cStringUsingEncoding:NSUTF8StringEncoding]);
//            }
    if (fontId / maxVariants_Fonts * maxVariants_Fonts == monospace_FontId) {
//        font = [UIFont monospacedSystemFontOfSize:0.8f * height weight:UIFontWeightRegular];
//        for (NSString *name in [UIFont fontNamesForFamilyName:@"Iosevka Term"]) {
//            printf("fontname: %s\n", [name cStringUsingEncoding:NSUTF8StringEncoding]);
//        }
        font = [UIFont fontWithName:@"Iosevka-Term-Extended" size:height * 0.82f];
        [appState_ setSystemTextLineSpacing:0.0f];
    }
    else {
        font = [UIFont fontWithName:@"Roboto-Regular" size:height * 0.66f];
        [appState_ setSystemTextLineSpacing:height * 0.66f];
    }
    if (d->field) {
        [REF_d_field setFont:font];
    }
    if (d->view) {
        [REF_d_view setFont:font];
    }
}

const iString *text_SystemTextInput(const iSystemTextInput *d) {
    if (d->field) {
        return collectNewCStr_String([[REF_d_field text] cStringUsingEncoding:NSUTF8StringEncoding]);
    }
    if (d->view) {
        return collectNewCStr_String([[REF_d_view text] cStringUsingEncoding:NSUTF8StringEncoding]);
    }
    return NULL;
}

void setRect_SystemTextInput(iSystemTextInput *d, iRect rect) {
    CGRect frame = convertToCGRect_(&rect, (d->flags & multiLine_SystemTextInputFlags) != 0);
    if (d->field) {
        [REF_d_field setFrame:frame];
    }
    else {
        [REF_d_view setFrame:frame];
    }
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

static BOOL isNewlineAllowed_SystemTextInput_(const iSystemTextInput *d) {
    return (d->flags & insertNewlines_SystemTextInputFlag) != 0;
}
