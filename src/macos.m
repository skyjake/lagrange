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

#include "macos.h"
#include "app.h"
#include "ui/command.h"
#include "ui/widget.h"
#include "ui/color.h"

#import <AppKit/AppKit.h>

#if 0
static NSTouchBarItemIdentifier play_TouchId_         = @"fi.skyjake.BitwiseHarmony.play";
static NSTouchBarItemIdentifier restart_TouchId_      = @"fi.skyjake.BitwiseHarmony.restart";

static NSTouchBarItemIdentifier seqMoveUp_TouchId_    = @"fi.skyjake.BitwiseHarmony.sequence.move.up";
static NSTouchBarItemIdentifier seqMoveDown_TouchId_  = @"fi.skyjake.BitwiseHarmony.sequence.move.down";

static NSTouchBarItemIdentifier goto_TouchId_         = @"fi.skyjake.BitwiseHarmony.goto";
static NSTouchBarItemIdentifier mute_TouchId_         = @"fi.skyjake.BitwiseHarmony.mute";
static NSTouchBarItemIdentifier solo_TouchId_         = @"fi.skyjake.BitwiseHarmony.solo";
static NSTouchBarItemIdentifier color_TouchId_        = @"fi.skyjake.BitwiseHarmony.color";
static NSTouchBarItemIdentifier event_TouchId_        = @"fi.skyjake.BitwiseHarmony.event";

static NSTouchBarItemIdentifier eventList_TouchId_          = @"fi.skyjake.BitwiseHarmony.eventlist";
static NSTouchBarItemIdentifier masterGainEvent_TouchId_    = @"fi.skyjake.BitwiseHarmony.event.mastergain";
static NSTouchBarItemIdentifier resetEvent_TouchId_         = @"fi.skyjake.BitwiseHarmony.event.reset";
static NSTouchBarItemIdentifier voiceEvent_TouchId_         = @"fi.skyjake.BitwiseHarmony.event.voice";
static NSTouchBarItemIdentifier panEvent_TouchId_           = @"fi.skyjake.BitwiseHarmony.event.pan";
static NSTouchBarItemIdentifier gainEvent_TouchId_          = @"fi.skyjake.BitwiseHarmony.event.gain";
static NSTouchBarItemIdentifier fadeEvent_TouchId_          = @"fi.skyjake.BitwiseHarmony.event.fade";
static NSTouchBarItemIdentifier pitchSpeedEvent_TouchId_    = @"fi.skyjake.BitwiseHarmony.event.pitchspeed";
static NSTouchBarItemIdentifier pitchBendUpEvent_TouchId_   = @"fi.skyjake.BitwiseHarmony.event.pitchbendup";
static NSTouchBarItemIdentifier pitchBendDownEvent_TouchId_ = @"fi.skyjake.BitwiseHarmony.event.pitchbenddown";
static NSTouchBarItemIdentifier tremoloEvent_TouchId_       = @"fi.skyjake.BitwiseHarmony.event.tremolo";
#endif

enum iTouchBarVariant {
    default_TouchBarVariant,
};

#if 0
@interface CommandButton : NSButtonTouchBarItem {
    NSString *command;
    iWidget *widget;
}
- (id)initWithIdentifier:(NSTouchBarItemIdentifier)identifier
                   title:(NSString *)title
                 command:(NSString *)cmd;
- (id)initWithIdentifier:(NSTouchBarItemIdentifier)identifier
                   title:(NSString *)title
                  widget:(iWidget *)widget
                 command:(NSString *)cmd;
- (void)dealloc;
@end

@implementation CommandButton

- (id)initWithIdentifier:(NSTouchBarItemIdentifier)identifier
                   title:(NSString *)title
                 command:(NSString *)cmd {
    [super initWithIdentifier:identifier];
    self.title = title;
    self.target = self;
    self.action = @selector(buttonPressed);
    command = cmd;
    return self;
}

- (id)initWithIdentifier:(NSTouchBarItemIdentifier)identifier
                   title:(NSString *)title
                  widget:(iWidget *)aWidget
                 command:(NSString *)cmd {
    [self initWithIdentifier:identifier title:title command:[cmd retain]];
    widget = aWidget;
    return self;
}

- (void)dealloc {
    [command release];
    [super dealloc];
}

- (void)buttonPressed {
    const char *cmd = [command cStringUsingEncoding:NSUTF8StringEncoding];
    if (widget) {
        postCommand_Widget(widget, "%s", cmd);
    }
    else {
        postCommand_App(cmd);
    }
}

@end
#endif

@interface MyDelegate : NSResponder<NSApplicationDelegate, NSTouchBarDelegate> {
    enum iTouchBarVariant touchBarVariant;
    NSString *currentAppearanceName;
    NSObject<NSApplicationDelegate> *sdlDelegate;
    NSMutableDictionary<NSString *, NSString*> *menuCommands;
}
- (id)initWithSDLDelegate:(NSObject<NSApplicationDelegate> *)sdl;
//- (NSTouchBar *)makeTouchBar;
- (BOOL)application:(NSApplication *)app openFile:(NSString *)filename;
- (void)application:(NSApplication *)app openFiles:(NSArray<NSString *> *)filenames;
- (void)application:(NSApplication *)app openURLs:(NSArray<NSURL *> *)urls;
- (void)applicationDidFinishLaunching:(NSNotification *)notifications;
@end

@implementation MyDelegate

- (id)initWithSDLDelegate:(NSObject<NSApplicationDelegate> *)sdl {
    [super init];
    currentAppearanceName = nil;
    menuCommands = [[NSMutableDictionary<NSString *, NSString *> alloc] init];
    touchBarVariant = default_TouchBarVariant;
    sdlDelegate = sdl;
    return self;
}

- (void)dealloc {
    [menuCommands release];
    [currentAppearanceName release];
    [super dealloc];
}

- (void)setTouchBarVariant:(enum iTouchBarVariant)variant {
    touchBarVariant = variant;
    self.touchBar = nil;
}

static void appearanceChanged_MacOS_(NSString *name) {
    const iBool isDark = [name containsString:@"Dark"];
    const iBool isHighContrast = [name containsString:@"HighContrast"];
    postCommandf_App("~os.theme.changed dark:%d contrast:%d", isDark ? 1 : 0, isHighContrast ? 1 : 0);
//    printf("Effective appearance changed: %s\n", [name cStringUsingEncoding:NSUTF8StringEncoding]);
//    fflush(stdout);
}

- (void)setAppearance:(NSString *)name {
    if (!currentAppearanceName || ![name isEqualToString:currentAppearanceName]) {
        if (currentAppearanceName) {
            [currentAppearanceName release];
        }
        currentAppearanceName = [name retain];
        appearanceChanged_MacOS_(currentAppearanceName);
    }
}

- (void)setCommand:(NSString *)command forMenuItem:(NSMenuItem *)menuItem {
    [menuCommands setObject:command forKey:[menuItem title]];
}

- (BOOL)application:(NSApplication *)app openFile:(NSString *)filename {
    return [sdlDelegate application:app openFile:filename];
}

- (void)application:(NSApplication *)app openFiles:(NSArray<NSString *> *)filenames {
    /* TODO: According to AppKit docs, this method won't be called when openURLs is defined. */
    for (NSString *fn in filenames) {
        NSLog(@"openFiles: %@", fn);
        [self application:app openFile:fn];
    }
}

- (void)application:(NSApplication *)app openURLs:(NSArray<NSURL *> *)urls {
    for (NSURL *url in urls) {
        NSLog(@"openURLs: %@", [url absoluteString]);
        [sdlDelegate application:app openFile:[url absoluteString]];
    }
}

- (void)applicationDidFinishLaunching:(NSNotification *)notification {
    [sdlDelegate applicationDidFinishLaunching:notification];
}

- (void)observeValueForKeyPath:(NSString *)keyPath
                      ofObject:(id)object
                        change:(NSDictionary *)change
                       context:(void *)context {
    iUnused(object, change);
    if ([keyPath isEqualToString:@"effectiveAppearance"] && context == self) {
        [self setAppearance:[[NSApp effectiveAppearance] name]];
    }
}

#if 0
- (NSTouchBar *)makeTouchBar {
    NSTouchBar *bar = [[NSTouchBar alloc] init];
    bar.delegate = self;
    switch (touchBarVariant) {
        case default_TouchBarVariant:
            bar.defaultItemIdentifiers = @[ play_TouchId_, restart_TouchId_,
                                            NSTouchBarItemIdentifierFixedSpaceSmall,
                                            NSTouchBarItemIdentifierOtherItemsProxy ];
            break;
        case sequence_TouchBarVariant:
            bar.defaultItemIdentifiers = @[ play_TouchId_, restart_TouchId_,
                                            NSTouchBarItemIdentifierFlexibleSpace,
                                            seqMoveUp_TouchId_, seqMoveDown_TouchId_,
                                            NSTouchBarItemIdentifierFlexibleSpace,
                                            NSTouchBarItemIdentifierOtherItemsProxy];
            break;
        case tracker_TouchBarVariant:
            bar.defaultItemIdentifiers = @[ play_TouchId_, restart_TouchId_,
                                            NSTouchBarItemIdentifierFlexibleSpace,
                                            goto_TouchId_,
                                            event_TouchId_,
                                            NSTouchBarItemIdentifierFlexibleSpace,
                                            solo_TouchId_, mute_TouchId_, color_TouchId_,
                                            NSTouchBarItemIdentifierFlexibleSpace,
                                            NSTouchBarItemIdentifierOtherItemsProxy ];
            break;
        case wide_TouchBarVariant:
            bar.defaultItemIdentifiers = @[ play_TouchId_, restart_TouchId_,
                                            NSTouchBarItemIdentifierFlexibleSpace,
                                            event_TouchId_,
                                            NSTouchBarItemIdentifierFlexibleSpace,
                                            solo_TouchId_, mute_TouchId_, color_TouchId_,
                                            NSTouchBarItemIdentifierFlexibleSpace,
                                            seqMoveUp_TouchId_, seqMoveDown_TouchId_,
                                            NSTouchBarItemIdentifierFlexibleSpace,
                                            NSTouchBarItemIdentifierOtherItemsProxy ];
            break;
    }
    return bar;
}
#endif

- (void)showPreferences {
    postCommand_App("preferences");
}

- (void)closeTab {
    postCommand_App("tabs.close");
}

- (void)postMenuItemCommand:(id)sender {
    NSString *command = [menuCommands objectForKey:[(NSMenuItem *)sender title]];
    if (command) {
        postCommand_App([command cStringUsingEncoding:NSUTF8StringEncoding]);
    }
}

- (nullable NSTouchBarItem *)touchBar:(NSTouchBar *)touchBar
                makeItemForIdentifier:(NSTouchBarItemIdentifier)identifier {
    iUnused(touchBar);
#if 0
    if ([identifier isEqualToString:play_TouchId_]) {
        return [NSButtonTouchBarItem
            buttonTouchBarItemWithIdentifier:identifier
                                       image:[NSImage imageNamed:NSImageNameTouchBarPlayPauseTemplate]
                                      target:self
                                      action:@selector(playPressed)];
    }
    else if ([identifier isEqualToString:restart_TouchId_]) {
        return [NSButtonTouchBarItem
            buttonTouchBarItemWithIdentifier:identifier
                                       image:[NSImage imageNamed:NSImageNameTouchBarSkipToStartTemplate]
                                      target:self
                                      action:@selector(restartPressed)];
    }
    else if ([identifier isEqualToString:seqMoveUp_TouchId_]) {
        return [[CommandButton alloc] initWithIdentifier:identifier
                                                   title:@"Seq\u2b06"
                                                  widget:findWidget_App("sequence")
                                                 command:@"sequence.swap arg:-1"];
    }
    else if ([identifier isEqualToString:seqMoveDown_TouchId_]) {
        return [[CommandButton alloc] initWithIdentifier:identifier
                                                   title:@"Seq\u2b07"
                                                  widget:findWidget_App("sequence")
                                                 command:@"sequence.swap arg:1"];
    }
    else if ([identifier isEqualToString:goto_TouchId_]) {
        return [[CommandButton alloc] initWithIdentifier:identifier
                                                   title:@"Go to…"
                                                 command:@"pattern.goto arg:-1"];
    }
    else if ([identifier isEqualToString:event_TouchId_]) {
        NSTouchBar *events = [[NSTouchBar alloc] init];
        events.delegate = self;
        events.defaultItemIdentifiers = @[ eventList_TouchId_ ];
        NSPopoverTouchBarItem *pop = [[NSPopoverTouchBarItem alloc] initWithIdentifier:identifier];
        pop.collapsedRepresentationLabel = @"Event";
        pop.popoverTouchBar = events;
        [events release];
        return pop;
    }
    else if ([identifier isEqualToString:eventList_TouchId_]) {
        const struct {
            NSTouchBarItemIdentifier id;
            const char *title;
            const char *command;
        } buttonDefs_[] = {
            { voiceEvent_TouchId_, "Voice", "tracker.setevent type:2" },
            { panEvent_TouchId_, "Pan", "tracker.setevent type:3 arg:128" },
            { gainEvent_TouchId_, "Gain", "tracker.setevent type:4 arg:128" },
            { fadeEvent_TouchId_, "Fade", "tracker.setevent type:5" },
            { tremoloEvent_TouchId_, "Trem", "tracker.setevent type:9" },
            { pitchSpeedEvent_TouchId_, "P.Spd", "tracker.setevent type:6" },
            { pitchBendUpEvent_TouchId_, "BnUp", "tracker.setevent type:7" },
            { pitchBendDownEvent_TouchId_, "BnDn", "tracker.setevent type:8" },
            { masterGainEvent_TouchId_, "M.Gain", "tracker.setevent type:10 arg:64" },
            { resetEvent_TouchId_, "Reset", "tracker.setevent type:1" },
        };
        NSMutableArray *items = [[NSMutableArray alloc] init];
        iForIndices(i, buttonDefs_) {
            CommandButton *button = [[CommandButton alloc]
                initWithIdentifier:buttonDefs_[i].id
                             title:[NSString stringWithUTF8String:buttonDefs_[i].title]
                            widget:findWidget_App("tracker")
                           command:[NSString stringWithUTF8String:buttonDefs_[i].command]
            ];
            [items addObject:button];
        }
        NSGroupTouchBarItem *group = [NSGroupTouchBarItem groupItemWithIdentifier:identifier
                                                                            items:items];
        [items release];
        return group;
    }
    else if ([identifier isEqualToString:mute_TouchId_]) {
        return [[CommandButton alloc] initWithIdentifier:identifier
                                                   title:@"Mute"
                                                  widget:findWidget_App("tracker")
                                                 command:@"tracker.mute"];
    }
    else if ([identifier isEqualToString:solo_TouchId_]) {
        return [[CommandButton alloc] initWithIdentifier:identifier
                                                   title:@"Solo"
                                                  widget:findWidget_App("tracker")
                                                 command:@"tracker.solo"];
    }
    else if ([identifier isEqualToString:color_TouchId_]) {
        NSTouchBar *colors = [[NSTouchBar alloc] init];
        colors.delegate = self;
        colors.defaultItemIdentifiers = @[ NSTouchBarItemIdentifierFlexibleSpace,
                                           whiteColor_TouchId_,
                                           yellowColor_TouchId_,
                                           orangeColor_TouchId_,
                                           redColor_TouchId_,
                                           magentaColor_TouchId_,
                                           blueColor_TouchId_,
                                           cyanColor_TouchId_,
                                           greenColor_TouchId_,
                                           NSTouchBarItemIdentifierFlexibleSpace ];
        NSPopoverTouchBarItem *pop = [[NSPopoverTouchBarItem alloc] initWithIdentifier:identifier];
        pop.collapsedRepresentationImage = [NSImage imageNamed:NSImageNameTouchBarColorPickerFill];
        pop.popoverTouchBar = colors;
        [colors release];
        return pop;
    }
    else if ([identifier isEqualToString:whiteColor_TouchId_]) {
        return [[ColorButton alloc] initWithIdentifier:identifier
                                            trackColor:white_TrackColor];
    }
    else if ([identifier isEqualToString:yellowColor_TouchId_]) {
        return [[ColorButton alloc] initWithIdentifier:identifier
                                            trackColor:yellow_TrackColor];
    }
    else if ([identifier isEqualToString:orangeColor_TouchId_]) {
        return [[ColorButton alloc] initWithIdentifier:identifier
                                            trackColor:orange_TrackColor];
    }
    else if ([identifier isEqualToString:redColor_TouchId_]) {
        return [[ColorButton alloc] initWithIdentifier:identifier
                                            trackColor:red_TrackColor];
    }
    else if ([identifier isEqualToString:magentaColor_TouchId_]) {
        return [[ColorButton alloc] initWithIdentifier:identifier
                                            trackColor:magenta_TrackColor];
    }
    else if ([identifier isEqualToString:blueColor_TouchId_]) {
        return [[ColorButton alloc] initWithIdentifier:identifier
                                            trackColor:blue_TrackColor];
    }
    else if ([identifier isEqualToString:cyanColor_TouchId_]) {
        return [[ColorButton alloc] initWithIdentifier:identifier
                                            trackColor:cyan_TrackColor];
    }
    else if ([identifier isEqualToString:greenColor_TouchId_]) {
        return [[ColorButton alloc] initWithIdentifier:identifier
                                            trackColor:green_TrackColor];
    }
#endif
    return nil;
}

@end

void enableMomentumScroll_MacOS(void) {
    [[NSUserDefaults standardUserDefaults] setBool: YES
                                            forKey: @"AppleMomentumScrollSupported"];
}

@interface UrlHandler : NSObject
- (void)handleURLEvent:(NSAppleEventDescriptor*)event
        withReplyEvent:(NSAppleEventDescriptor*)replyEvent;
@end

@implementation UrlHandler
- (void)handleURLEvent:(NSAppleEventDescriptor*)event
        withReplyEvent:(NSAppleEventDescriptor*)replyEvent {
    NSString *url = [[event paramDescriptorForKeyword:keyDirectObject] stringValue];
    postCommandf_App("~open url:%s", [url cStringUsingEncoding:NSUTF8StringEncoding]);
}
@end

void registerURLHandler_MacOS(void) {
    UrlHandler *handler = [[UrlHandler alloc] init];
    [[NSAppleEventManager sharedAppleEventManager]
        setEventHandler:handler
            andSelector:@selector(handleURLEvent:withReplyEvent:)
          forEventClass:kInternetEventClass
             andEventID:kAEGetURL];
}

void setupApplication_MacOS(void) {
    NSApplication *app = [NSApplication sharedApplication];
    /* Our delegate will override SDL's delegate. */
    MyDelegate *myDel = [[MyDelegate alloc] initWithSDLDelegate:app.delegate];
    [myDel setAppearance:[[app effectiveAppearance] name]];
    app.delegate = myDel;
    NSMenu *appMenu = [[[NSApp mainMenu] itemAtIndex:0] submenu];
    NSMenuItem *prefsItem = [appMenu itemWithTitle:@"Preferences…"];
    prefsItem.target = myDel;
    prefsItem.action = @selector(showPreferences);
    /* Get rid of the default window close item */
    NSMenu *windowMenu = [[[NSApp mainMenu] itemWithTitle:@"Window"] submenu];
    NSMenuItem *windowCloseItem = [windowMenu itemWithTitle:@"Close"];
    windowCloseItem.target = myDel;
    windowCloseItem.action = @selector(closeTab);
}

void insertMenuItems_MacOS(const char *menuLabel, int atIndex, const iMenuItem *items, size_t count) {
    NSApplication *app = [NSApplication sharedApplication];
    MyDelegate *myDel = (MyDelegate *) app.delegate;
    [app addObserver:myDel
          forKeyPath:@"effectiveAppearance"
             options:0
             context:myDel];
    NSMenu *appMenu = [app mainMenu];
    NSMenuItem *mainItem = [appMenu insertItemWithTitle:[NSString stringWithUTF8String:menuLabel]
                                                 action:nil
                                          keyEquivalent:@""
                                                atIndex:atIndex];
    NSMenu *menu = [[NSMenu alloc] initWithTitle:[NSString stringWithUTF8String:menuLabel]];
    for (size_t i = 0; i < count; ++i) {
        const char *label = items[i].label;
        if (label[0] == '\r') {
            /* Skip the formatting escape. */
            label += 2;
        }
        if (equal_CStr(label, "---")) {
            [menu addItem:[NSMenuItem separatorItem]];
        }
        else {
            const iBool hasCommand = (items[i].command && items[i].command[0]);
            iString key;
            init_String(&key);
            if (items[i].key == SDLK_LEFT) {
                appendChar_String(&key, 0x2190);
            }
            else if (items[i].key == SDLK_RIGHT) {
                appendChar_String(&key, 0x2192);
            }
            else if (items[i].key) {
                appendChar_String(&key, items[i].key);
            }
            NSMenuItem *item = [menu addItemWithTitle:[NSString stringWithUTF8String:label]
                                               action:(hasCommand ? @selector(postMenuItemCommand:) : nil)
                                        keyEquivalent:[NSString stringWithUTF8String:cstr_String(&key)]];
            NSEventModifierFlags modMask = 0;
            if (items[i].kmods & KMOD_GUI) {
                modMask |= NSEventModifierFlagCommand;
            }
            if (items[i].kmods & KMOD_ALT) {
                modMask |= NSEventModifierFlagOption;
            }
            if (items[i].kmods & KMOD_CTRL) {
                modMask |= NSEventModifierFlagControl;
            }
            if (items[i].kmods & KMOD_SHIFT) {
                modMask |= NSEventModifierFlagShift;
            }
            [item setKeyEquivalentModifierMask:modMask];
            if (hasCommand) {
                [myDel setCommand:[NSString stringWithUTF8String:items[i].command] forMenuItem:item];
            }
            deinit_String(&key);
        }
    }
    [mainItem setSubmenu:menu];
    [menu release];
}

void handleCommand_MacOS(const char *cmd) {
    if (equal_Command(cmd, "prefs.ostheme.changed")) {
        if (arg_Command(cmd)) {
            appearanceChanged_MacOS_([[NSApp effectiveAppearance] name]);
        }
    }
#if 0
    if (equal_Command(cmd, "tabs.changed")) {
        MyDelegate *myDel = (MyDelegate *) [[NSApplication sharedApplication] delegate];
        const char *tabId = suffixPtr_Command(cmd, "id");
        if (equal_CStr(tabId, "tracker")) {
            [myDel setTouchBarVariant:tracker_TouchBarVariant];
        }
        else if (equal_CStr(tabId, "sequence")) {
            [myDel setTouchBarVariant:sequence_TouchBarVariant];
        }
        else if (equal_CStr(tabId, "trackertab")) {
            [myDel setTouchBarVariant:wide_TouchBarVariant];
        }
        else {
            [myDel setTouchBarVariant:default_TouchBarVariant];
        }
    }
#endif
}

void log_MacOS(const char *msg) {
    NSLog(@"%s", msg);
}
