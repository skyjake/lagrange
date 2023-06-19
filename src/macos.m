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
#include "lang.h"
#include "ui/color.h"
#include "ui/command.h"
#include "ui/keys.h"
#include "ui/widget.h"
#include "ui/window.h"

#include <SDL_timer.h>
#include <SDL_syswm.h>
#include <the_Foundation/stringset.h>

#import <AppKit/AppKit.h>

#if defined (LAGRANGE_ENABLE_SPARKLE)
#   import <Sparkle/Sparkle.h>
#endif

static NSTouchBarItemIdentifier goBack_TouchId_      = @"fi.skyjake.Lagrange.back";
static NSTouchBarItemIdentifier goForward_TouchId_   = @"fi.skyjake.Lagrange.forward";
static NSTouchBarItemIdentifier find_TouchId_        = @"fi.skyjake.Lagrange.find";
static NSTouchBarItemIdentifier newTab_TouchId_      = @"fi.skyjake.Lagrange.tabs.new";
static NSTouchBarItemIdentifier sidebarMode_TouchId_ = @"fi.skyjake.Lagrange.sidebar.mode";

enum iTouchBarVariant {
    default_TouchBarVariant,
};

static iInt2 macVer_(void) {
    if ([[NSProcessInfo processInfo] respondsToSelector:@selector(operatingSystemVersion)]) {
        const NSOperatingSystemVersion ver = [[NSProcessInfo processInfo] operatingSystemVersion];
        return init_I2(ver.majorVersion, ver.minorVersion);
    }
    return init_I2(10, 10);
}

static NSWindow *nsWindow_(SDL_Window *window) {
    SDL_SysWMinfo wm;
    SDL_VERSION(&wm.version);
    if (SDL_GetWindowWMInfo(window, &wm)) {
        return wm.info.cocoa.window;
    }
    iAssert(false);
    return nil;
}

static NSString *currentSystemAppearance_(void) {
    /* This API does not exist on 10.13. */
    if ([NSApp respondsToSelector:@selector(effectiveAppearance)]) {
        return [[NSApp effectiveAppearance] name];
    }
    return @"NSAppearanceNameAqua";
}

iBool shouldDefaultToMetalRenderer_MacOS(void) {
    const iInt2 ver = macVer_();
    SDL_DisplayMode dispMode;
    SDL_GetDesktopDisplayMode(0, &dispMode);
    return dispMode.refresh_rate > 60 && (ver.x > 10 || ver.y > 13);
}

static void ignoreImmediateKeyDownEvents_(void) {
    /* SDL ignores menu key equivalents so the keydown events will be posted regardless.
       However, we shouldn't double-activate menu items when a shortcut key is used in our
       widgets. Quite a kludge: take advantage of Window's focus-acquisition threshold to
       ignore the immediately following key down events. */
    iForEach(PtrArray, w, collect_PtrArray(listWindows_App())) {
        as_Window(w.ptr)->focusGainedAt = SDL_GetTicks();
    }
}

/*----------------------------------------------------------------------------------------------*/

@interface CommandButton : NSCustomTouchBarItem {
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
- (id)initWithIdentifier:(NSTouchBarItemIdentifier)identifier
                   image:(NSImage *)image
                  widget:(iWidget *)widget
                 command:(NSString *)cmd;
- (void)dealloc;
@end

@implementation CommandButton

- (id)initWithIdentifier:(NSTouchBarItemIdentifier)identifier
                   title:(NSString *)title
                 command:(NSString *)cmd {
    self = [super initWithIdentifier:identifier];
    self.view = [NSButton buttonWithTitle:title target:self action:@selector(buttonPressed)];
    command = cmd;
    return self;
}

- (id)initWithIdentifier:(NSTouchBarItemIdentifier)identifier
                   image:(NSImage *)image
                  widget:(iWidget *)widget
                 command:(NSString *)cmd {
    self = [super initWithIdentifier:identifier];
    self.view = [NSButton buttonWithImage:image target:self action:@selector(buttonPressed)];
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

/*----------------------------------------------------------------------------------------------*/

@interface MenuCommands : NSObject {
    NSMutableDictionary<NSString *, NSString *> *commands;
    iWidget *source;
}
@end

@implementation MenuCommands

- (id)init {
    self = [super init];
    commands = [[NSMutableDictionary<NSString *, NSString *> alloc] init];
    source = NULL;
    return self;
}

- (void)setCommand:(NSString * __nonnull)command forMenuItem:(NSMenuItem * __nonnull)menuItem {
    [commands setObject:command forKey:[menuItem title]];
}

- (void)setSource:(iWidget *)widget {
    source = widget;
}

- (void)clear {
    [commands removeAllObjects];
}

- (NSString *)commandForMenuItem:(NSMenuItem *)menuItem {
    return [commands objectForKey:[menuItem title]];
}

- (void)postMenuItemCommand:(id)sender {
    NSString *command = [commands objectForKey:[(NSMenuItem *)sender title]];
    if (command) {
        const char *cstr = [command cStringUsingEncoding:NSUTF8StringEncoding];
        if (source) {
            postCommand_Widget(source, "%s", cstr);
        }
        else {
            postCommand_Root(NULL, cstr);
        }
        ignoreImmediateKeyDownEvents_();
    }
}

@end

static NSMenuItem *makeMenuItems_(NSMenu *menu, MenuCommands *commands, int atIndex,
                                  const iMenuItem *items, size_t n);

/*----------------------------------------------------------------------------------------------*/

@interface MyDelegate : NSResponder<NSApplicationDelegate, NSTouchBarDelegate
#if defined (LAGRANGE_ENABLE_SPARKLE)
        , SUUpdaterDelegate
#endif                                    
        > {
    enum iTouchBarVariant touchBarVariant;
    NSString *currentAppearanceName;
    NSObject<NSApplicationDelegate> *sdlDelegate;
    MenuCommands *menuCommands;
}
- (id)initWithSDLDelegate:(NSObject<NSApplicationDelegate> *)sdl;
- (NSTouchBar *)makeTouchBar;
- (BOOL)application:(NSApplication *)app openFile:(NSString *)filename;
- (void)application:(NSApplication *)app openFiles:(NSArray<NSString *> *)filenames;
- (void)application:(NSApplication *)app openURLs:(NSArray<NSURL *> *)urls;
- (void)applicationDidFinishLaunching:(NSNotification *)notifications;
- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication *)sender;
- (NSApplicationTerminateReply)applicationShouldTerminate:(NSApplication *)sender;
- (BOOL)applicationShouldHandleReopen:(NSApplication *)sender hasVisibleWindows:(BOOL)flag;

#if defined (LAGRANGE_ENABLE_SPARKLE)
- (void)updaterWillRelaunchApplication:(SUUpdater *)updater;
#endif
@end

@implementation MyDelegate

- (id)initWithSDLDelegate:(NSObject<NSApplicationDelegate> *)sdl {
    self = [super init];
    currentAppearanceName = nil;
    menuCommands = [[MenuCommands alloc] init];
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

- (MenuCommands *)menuCommands {
    return menuCommands;
}

- (void)postMenuItemCommand:(id)sender {
    [menuCommands postMenuItemCommand:sender];
}

static void appearanceChanged_MacOS_(NSString *name) {
    const iBool isDark = [name containsString:@"Dark"];
    const iBool isHighContrast = [name containsString:@"HighContrast"];
    postCommandf_App("~os.theme.changed dark:%d contrast:%d", isDark ? 1 : 0, isHighContrast ? 1 : 0);
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

#if defined (LAGRANGE_ENABLE_SPARKLE)
- (void)updaterWillRelaunchApplication:(SUUpdater *)updater {
    /* Do allow the app to close now. */
    postCommand_App("quit");
}
#endif

- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication *)sender {
    return NO;
}

- (NSApplicationTerminateReply)applicationShouldTerminate:(NSApplication *)sender {
    postCommand_App("quit");
    return NSTerminateCancel;
}

- (BOOL)applicationShouldHandleReopen:(NSApplication *)sender hasVisibleWindows:(BOOL)flag {
    if (!flag) {
        if (numWindows_App() == 0) {
            postCommand_App("window.new");
        }
        else {
            iConstForEach(PtrArray, i, mainWindows_App()) {
                SDL_RaiseWindow(((const iMainWindow *) i.ptr)->base.win);
            }
        }
    }
    return YES;
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

- (NSTouchBar *)makeTouchBar {
    NSTouchBar *bar = [[NSTouchBar alloc] init];
    bar.delegate = self;
    switch (touchBarVariant) {
        case default_TouchBarVariant:
            bar.defaultItemIdentifiers = @[ goBack_TouchId_, goForward_TouchId_,
                                            NSTouchBarItemIdentifierFixedSpaceSmall,
                                            find_TouchId_,
                                            NSTouchBarItemIdentifierFlexibleSpace,
                                            sidebarMode_TouchId_,
                                            NSTouchBarItemIdentifierFlexibleSpace,
                                            newTab_TouchId_,
                                            NSTouchBarItemIdentifierOtherItemsProxy ];
            break;
    }
    return bar;
}

- (void)showPreferences {
    postCommand_App("preferences");
    ignoreImmediateKeyDownEvents_();
}

- (void)closeTab {
    postCommand_App("tabs.close");
    ignoreImmediateKeyDownEvents_();
}

- (void)postQuit {
    postCommand_App("quit");
}

- (void)sidebarModePressed:(id)sender {
    NSSegmentedControl *seg = sender;
    postCommandf_App("sidebar.mode arg:%d toggle:1", (int) [seg selectedSegment]);
}

- (nullable NSTouchBarItem *)touchBar:(NSTouchBar *)touchBar
                makeItemForIdentifier:(NSTouchBarItemIdentifier)identifier {
    iUnused(touchBar);
    if ([identifier isEqualToString:goBack_TouchId_]) {
        return [[CommandButton alloc] initWithIdentifier:identifier
                                                   image:[NSImage imageNamed:NSImageNameTouchBarGoBackTemplate]
                                                  widget:nil
                                                 command:@"navigate.back"];
    }
    else if ([identifier isEqualToString:goForward_TouchId_]) {
        return [[CommandButton alloc] initWithIdentifier:identifier
                                                   image:[NSImage imageNamed:NSImageNameTouchBarGoForwardTemplate]
                                                  widget:nil
                                                 command:@"navigate.forward"];
    }
    else if ([identifier isEqualToString:find_TouchId_]) {
        return [[CommandButton alloc] initWithIdentifier:identifier
                                                   image:[NSImage imageNamed:NSImageNameTouchBarSearchTemplate]
                                                  widget:nil
                                                 command:@"focus.set id:find.input"];
    }
    else if ([identifier isEqualToString:sidebarMode_TouchId_]) {
        NSCustomTouchBarItem *item = [[NSCustomTouchBarItem alloc] initWithIdentifier:sidebarMode_TouchId_];
        NSSegmentedControl *seg =
            [NSSegmentedControl segmentedControlWithLabels:@[ @"Bookmarks", @"Feeds", @"History", @"Idents", @"Outline"]
                                              trackingMode:NSSegmentSwitchTrackingMomentary
                                                    target:[[NSApplication sharedApplication] delegate]
                                                    action:@selector(sidebarModePressed:)];
        item.view = seg;
        return item;
    }
    else if ([identifier isEqualToString:newTab_TouchId_]) {
        return [[CommandButton alloc] initWithIdentifier:identifier
                                                   image:[NSImage imageNamed:NSImageNameTouchBarAddTemplate]
                                                  widget:nil
                                                 command:@"tabs.new append:1"];
    }
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
    iString *str = newCStr_String([url cStringUsingEncoding:NSUTF8StringEncoding]);
    str = urlDecodeExclude_String(collect_String(str), "/#?:");
    postCommandf_App("~open newtab:1 url:%s", cstr_String(str));
    delete_String(str);
}
@end

void registerURLHandler_MacOS(void) {
    UrlHandler *handler = [[UrlHandler alloc] init];
    [[NSAppleEventManager sharedAppleEventManager]
        setEventHandler:handler
            andSelector:@selector(handleURLEvent:withReplyEvent:)
          forEventClass:kInternetEventClass
             andEventID:kAEGetURL];
    [handler release];
}

static iBool processKeyDownEvent_(NSEvent *event) {
    if ((event.modifierFlags & NSEventModifierFlagFunction) && (event.keyCode == 0xe)) {
        /* Globe-E shows the sysetm Character Viewer in recent versions of macOS. */
        postCommand_App("emojipicker");
        return iTrue;
    }
    return iFalse;
}

static int swipeDir_ = 0;
static int preventTapGlitch_ = 0;

static iBool processScrollWheelEvent_(NSEvent *event) {
    const iBool isPerPixel = (event.hasPreciseScrollingDeltas != 0);
    const iBool isInertia  = (event.momentumPhase & (NSEventPhaseBegan | NSEventPhaseChanged)) != 0;
    const iBool isEnded    = event.scrollingDeltaX == 0.0f && event.scrollingDeltaY == 0.0f && !isInertia;
    const iWindow *win     = NULL; //&get_MainWindow()->base;
    /* If this event belongs to one of the MainWindows, handle it and mark it for that window. 
       If it's for an auxiliary window, let the system handle it. */
    iConstForEach(PtrArray, i, regularWindows_App()) {
        if (event.window == nsWindow_(as_Window(i.ptr)->win)) {
            win = i.ptr;
            break;
        }            
    }
    if (!win) { //event.window != nsWindow_(win->win)) {
        /* Not a main window. */
        return iFalse;
    }
    if (isPerPixel) {
        /* On macOS 12.1, stopping ongoing inertia scroll with a tap seems to sometimes produce
           spurious large scroll events. */
        switch (preventTapGlitch_) {
            case 0:
                if (isInertia && event.momentumPhase == NSEventPhaseChanged) {
                    preventTapGlitch_++;
                }
                else {
                    preventTapGlitch_ = 0;
                }
                break;
            case 1:
                if (event.scrollingDeltaY == 0 && event.momentumPhase == NSEventPhaseEnded) {
                    preventTapGlitch_++;
                }
                break;
            case 2:
                if (event.scrollingDeltaY == 0 && event.momentumPhase == 0 && isEnded) {
                    preventTapGlitch_++;
                }
                else {
                    preventTapGlitch_ = 0;
                }
                break;
            case 3:
                if (event.scrollingDeltaY != 0 && event.momentumPhase == 0 && !isInertia) {
                    preventTapGlitch_ = 0;
                    // printf("SPURIOUS\n"); fflush(stdout);
                    return iTrue;
                }
                preventTapGlitch_ = 0;
                break;
        }
    }
    else {
        SDL_MouseWheelEvent e = { .type = SDL_MOUSEWHEEL };
        e.timestamp = SDL_GetTicks();
        e.windowID = id_Window(win);
        e.which = 1; /* Distinction between trackpad and regular mouse. */
        /* Disregard any wheel acceleration. */
        e.x = event.scrollingDeltaX > 0 ? 1 : event.scrollingDeltaX < 0 ? -1 : 0;
        e.y = event.scrollingDeltaY > 0 ? 1 : event.scrollingDeltaY < 0 ? -1 : 0;        
        SDL_PushEvent((SDL_Event *) &e);
        return iTrue;
    }                
    /* Post corresponding MOUSEWHEEL events. */
    SDL_MouseWheelEvent e = { .type = SDL_MOUSEWHEEL };
    e.timestamp = SDL_GetTicks();
    e.windowID = id_Window(win);
    e.which = isPerPixel ? 0 : 1; /* Distinction between trackpad and regular mouse. */
    setPerPixel_MouseWheelEvent(&e, isPerPixel);
    if (isPerPixel) {
        setInertia_MouseWheelEvent(&e, isInertia);
        setScrollFinished_MouseWheelEvent(&e, isEnded);
        e.x = event.scrollingDeltaX * win->pixelRatio;
        e.y = event.scrollingDeltaY * win->pixelRatio;        
        /* Only scroll on one axis at a time. */
        if (swipeDir_ == 0) {
            swipeDir_ = iAbs(e.x) > iAbs(e.y) ? 1 : 2;
        }
        if (swipeDir_ == 1) {
            e.y = 0;
        }
        else if (swipeDir_ == 2) {
            e.x = 0;
        }
        if (isEnded) {
            swipeDir_ = 0;
        }
    }
    else {
        /* Disregard wheel acceleration applied by the OS. */
        e.x = -event.scrollingDeltaX;
        e.y = iSign(event.scrollingDeltaY);
    }
//    printf("#### Window %d [%d] dx:%d dy:%d phase:%ld inertia:%d end:%d\n", e.windowID,
//           preventTapGlitch_, e.x, e.y, (long) event.momentumPhase,
//           isInertia, isEnded); fflush(stdout);
    SDL_PushEvent((SDL_Event *) &e);
    return iTrue;        
}

void setupApplication_MacOS(void) {
    SDL_EventState(SDL_QUIT, SDL_FALSE); /* handle app quit manually */
    NSApplication *app = [NSApplication sharedApplication];
    [app setActivationPolicy:NSApplicationActivationPolicyRegular];
    [app activateIgnoringOtherApps:YES];
    /* Our delegate will override SDL's delegate. */
    MyDelegate *myDel = [[MyDelegate alloc] initWithSDLDelegate:app.delegate];
    [myDel setAppearance:currentSystemAppearance_()];
    [app addObserver:myDel
          forKeyPath:@"effectiveAppearance"
             options:0
             context:myDel];
    app.delegate = myDel;
    NSMenu *appMenu = [[[NSApp mainMenu] itemAtIndex:0] submenu];
    NSMenuItem *prefsItem = [appMenu itemAtIndex:2];
    NSMenuItem *quitItem = [appMenu itemAtIndex:[appMenu numberOfItems] - 1];
    prefsItem.target = myDel;
    prefsItem.action = @selector(showPreferences);
    quitItem.target = myDel;
    quitItem.action = @selector(postQuit);
    /* Get rid of the default window close item */
    NSMenu *windowMenu = [[[NSApp mainMenu] itemWithTitle:@"Window"] submenu];
    NSMenuItem *windowCloseItem = [windowMenu itemWithTitle:@"Close"];
    windowCloseItem.target = myDel;
    windowCloseItem.action = @selector(closeTab);
    
    /* TODO: translate these on lang.changed */
    static const iMenuItem macWindowMenuItems_[] = {
        { "---" },
        { "${menu.tab.next}", 0, 0, "tabs.next" },
        { "${menu.tab.prev}", 0, 0, "tabs.prev" },
        { "${menu.duptab}", 0, 0, "tabs.new duplicate:1" },
        { "---" },
    };
    makeMenuItems_(windowMenu, [myDel menuCommands], 4, macWindowMenuItems_, iElemCount(macWindowMenuItems_));    
    
    [NSEvent addLocalMonitorForEventsMatchingMask:NSEventMaskScrollWheel
                                          handler:^NSEvent*(NSEvent *event){
                                            if (event.type == NSEventTypeScrollWheel &&
                                                processScrollWheelEvent_(event)) {
                                                return nil; /* was eaten */                                                
                                            }
                                            return event;
                                          }];
    [NSEvent addLocalMonitorForEventsMatchingMask:NSEventMaskKeyDown
                                          handler:^NSEvent*(NSEvent *event){
                                              if (event.type == NSEventTypeKeyDown &&
                                                  processKeyDownEvent_(event)) {
                                                  return nil; /* was eaten */                                                
                                              }
                                              return event;
                                          }];
#if defined (LAGRANGE_ENABLE_SPARKLE)
    [[SUUpdater sharedUpdater] setDelegate:myDel];
#endif
}

void hideTitleBar_MacOS(iWindow *window) {
    NSWindow *w = nsWindow_(window->win);
    w.styleMask = 0; /* borderless */
}

void enableMenu_MacOS(const char *menuLabel, iBool enable) {
    menuLabel = translateCStr_Lang(menuLabel);
    NSApplication *app = [NSApplication sharedApplication];
    NSMenu *appMenu = [app mainMenu];
    NSString *label = [NSString stringWithUTF8String:menuLabel];
    NSMenuItem *menuItem = [appMenu itemAtIndex:[appMenu indexOfItemWithTitle:label]];
    [menuItem setEnabled:enable];
}

void enableMenuIndex_MacOS(int index, iBool enable) {
    NSApplication *app = [NSApplication sharedApplication];
    NSMenu *appMenu = [app mainMenu];
    NSMenuItem *menuItem = [appMenu itemAtIndex:index];
    [menuItem setEnabled:enable];        
}

void enableMenuItem_MacOS(const char *menuItemCommand, iBool enable) {
    NSApplication *app = [NSApplication sharedApplication];
    NSMenu *appMenu = [app mainMenu];
    MyDelegate *myDel = (MyDelegate *) app.delegate;
    for (NSMenuItem *mainMenuItem in appMenu.itemArray) {
        NSMenu *menu = mainMenuItem.submenu;
        if (menu) {
            for (NSMenuItem *menuItem in menu.itemArray) {
                NSString *command = [[myDel menuCommands] commandForMenuItem:menuItem];
                if (command) {
                    if (!iCmpStr([command cStringUsingEncoding:NSUTF8StringEncoding],
                                 menuItemCommand)) {
                        [menuItem setEnabled:enable];
                        return;
                    }
                }
            }
        }
    }
}

static iString *composeKeyEquivalent_(int key, int kmods, NSEventModifierFlags *modMask) {
    iString *str = new_String();
    if (key == SDLK_LEFT) {
        appendChar_String(str, 0x2190);
    }
    else if (key == SDLK_RIGHT) {
        appendChar_String(str, 0x2192);
    }
    else if (key == SDLK_UP) {
        appendChar_String(str, 0x2191);
    }
    else if (key == SDLK_DOWN) {
        appendChar_String(str, 0x2193);
    }
    else if (key) {
        appendChar_String(str, key);
    }
    *modMask = 0;
    if (kmods & KMOD_GUI) {
        *modMask |= NSEventModifierFlagCommand;
    }
    if (kmods & KMOD_ALT) {
        *modMask |= NSEventModifierFlagOption;
    }
    if (kmods & KMOD_CTRL) {
        *modMask |= NSEventModifierFlagControl;
    }
    if (kmods & KMOD_SHIFT) {
        *modMask |= NSEventModifierFlagShift;
    }
    return str;
}

void enableMenuItemsByKey_MacOS(int key, int kmods, iBool enable) {
    NSApplication *app = [NSApplication sharedApplication];
    NSMenu *appMenu = [app mainMenu];
    NSEventModifierFlags modMask;
    iString *keyEquiv = composeKeyEquivalent_(key, kmods, &modMask);
    for (NSMenuItem *mainMenuItem in appMenu.itemArray) {
        NSMenu *menu = mainMenuItem.submenu;
        if (menu) {
            for (NSMenuItem *menuItem in menu.itemArray) {
                if (menuItem.keyEquivalentModifierMask == modMask &&
                    !iCmpStr([menuItem.keyEquivalent cStringUsingEncoding:NSUTF8StringEncoding],
                             cstr_String(keyEquiv))) {
                    [menuItem setEnabled:enable];
                }
            }
        }
    }
    delete_String(keyEquiv);
}

void enableMenuItemsOnHomeRow_MacOS(iBool enable) {
    iStringSet *homeRowKeys = new_StringSet();
    const char *keys[] = { /* Note: another array in documentwidget.c */
        "f", "d", "s", "a",
        "j", "k", "l",
        "r", "e", "w", "q",
        "u", "i", "o", "p",
        "v", "c", "x", "z",
        "m", "n",
        "g", "h",
        "b",
        "t", "y"
    };
    iForIndices(i, keys) {
        iString str;
        initCStr_String(&str, keys[i]);
        insert_StringSet(homeRowKeys, &str);
        deinit_String(&str);
    }
    NSApplication *app = [NSApplication sharedApplication];
    NSMenu *appMenu = [app mainMenu];
    for (NSMenuItem *mainMenuItem in appMenu.itemArray) {
        NSMenu *menu = mainMenuItem.submenu;
        if (menu) {
            for (NSMenuItem *menuItem in menu.itemArray) {
                if (menuItem.keyEquivalentModifierMask == 0) {
                    iString equiv;
                    initCStr_String(&equiv, [menuItem.keyEquivalent
                                                cStringUsingEncoding:NSUTF8StringEncoding]);
                    if (contains_StringSet(homeRowKeys, &equiv)) {
                        [menuItem setEnabled:enable];
                        [menu setAutoenablesItems:NO];
                    }
                    deinit_String(&equiv);
                }
            }
        }
    }
    iRelease(homeRowKeys);
}

static void setShortcut_NSMenuItem_(NSMenuItem *item, int key, int kmods) {
    NSEventModifierFlags modMask;
    iString *str = composeKeyEquivalent_(key, kmods, &modMask);
    [item setKeyEquivalentModifierMask:modMask];
    [item setKeyEquivalent:[NSString stringWithUTF8String:cstr_String(str)]];
    delete_String(str);
}

void removeMenu_MacOS(int atIndex) {
    NSApplication *app = [NSApplication sharedApplication];
    NSMenu *appMenu = [app mainMenu];
    [appMenu removeItemAtIndex:atIndex];
}

void removeMenuItems_MacOS(int atIndex, int firstItem, int numItems) {
    NSApplication *app = [NSApplication sharedApplication];
    NSMenu *menu = [[app mainMenu] itemAtIndex:atIndex].menu;
    for (int i = 0; i < numItems; i++) {
        [menu removeItemAtIndex:firstItem];
    }        
}

static NSString *cleanString_(const iString *ansiEscapedText) {
    iString mod;
    initCopy_String(&mod, ansiEscapedText);
    iRegExp *ansi = makeAnsiEscapePattern_Text(iTrue /* with ESC */);
    replaceRegExp_String(&mod, ansi, "", NULL, NULL);
    iRelease(ansi);
    NSString *clean = [NSString stringWithUTF8String:cstr_String(&mod)];    
    deinit_String(&mod);
    return clean;
}

#if 0
static NSAttributedString *makeAttributedString_(const iString *ansiEscapedText) {
    iString mod;
    initCopy_String(&mod, ansiEscapedText);
    NSData *data = [NSData dataWithBytesNoCopy:data_Block(&mod.chars) length:size_String(&mod)];
    NSAttributedString *as = [[NSAttributedString alloc] initWithHTML:data
                                                   documentAttributes:nil];    
    deinit_String(&mod);
    return as;
}
#endif

/* returns the selected item, if any */
static NSMenuItem *makeMenuItems_(NSMenu *menu, MenuCommands *commands, int atIndex,
                                  const iMenuItem *items, size_t n) {
    if (atIndex == 0) {
        atIndex = menu.numberOfItems;
    }
    atIndex = iMin(atIndex, menu.numberOfItems);
    NSMenuItem *selectedItem = nil;
    for (size_t i = 0; i < n && items[i].label; ++i) {
        const char *label = translateCStr_Lang(items[i].label);
        if (equal_CStr(label, "---")) {
            [menu insertItem:[NSMenuItem separatorItem] atIndex:atIndex++];
        }
        else {
            const iBool hasCommand = (items[i].command && items[i].command[0]);
            iBool isChecked = iFalse;
            iBool isDisabled = iFalse;
            if (startsWith_CStr(label, "###")) {
                isChecked = iTrue;
                label += 3;
            }
            else if (startsWith_CStr(label, "///") || startsWith_CStr(label, "```")) {
                isDisabled = iTrue;
                label += 3;
            }
            iString itemTitle;
            initCStr_String(&itemTitle, label);
            removeIconPrefix_String(&itemTitle);
            if (removeColorEscapes_String(&itemTitle) == uiTextCaution_ColorId) {
//                prependCStr_String(&itemTitle, "\u26a0\ufe0f ");
            }
            NSMenuItem *item = [[NSMenuItem alloc] init];
            /* Use attributed string to allow newlines. */
            NSAttributedString *title = [[NSAttributedString alloc] initWithString:cleanString_(&itemTitle)];
            item.attributedTitle = title;
            [title release];
            item.action = (hasCommand ? @selector(postMenuItemCommand:) : nil);
            [menu insertItem:item atIndex:atIndex++];
            deinit_String(&itemTitle);
            [item setTarget:commands];
            if (isChecked) {
#if defined (__MAC_10_13)
                [item setState:NSControlStateValueOn];
#else
                [item setState:NSOnState];
#endif
                selectedItem = item;
            }
            [item setEnabled:!isDisabled];
            int key   = items[i].key;
            int kmods = items[i].kmods;
            if (hasCommand) {
                [commands setCommand:[NSString stringWithUTF8String:items[i].command]
                         forMenuItem:item];
                /* Bindings may have a different key. */
                const iBinding *bind = findCommand_Keys(items[i].command);
                if (bind && bind->id < builtIn_BindingId) {
                    key   = bind->key;
                    kmods = bind->mods;
                }
            }
            setShortcut_NSMenuItem_(item, key, kmods);
        }
    }
    return selectedItem;
}

void insertMenuItems_MacOS(const char *menuLabel, int atIndex, int firstItemIndex,
                           const iMenuItem *items, size_t count) {
    NSApplication *app = [NSApplication sharedApplication];
    MyDelegate *myDel = (MyDelegate *) app.delegate;
    NSMenu *appMenu = [app mainMenu];
    menuLabel = translateCStr_Lang(menuLabel);
    NSMenuItem *mainItem;
    NSMenu *menu;
    if (firstItemIndex == 0) {
        mainItem = [appMenu insertItemWithTitle:[NSString stringWithUTF8String:menuLabel]
                                         action:nil
                                  keyEquivalent:@""
                                        atIndex:atIndex];
        menu = [[NSMenu alloc] initWithTitle:[NSString stringWithUTF8String:menuLabel]];
        [mainItem setSubmenu:menu];
    }
    else {
        mainItem = [appMenu itemAtIndex:atIndex];
        menu = mainItem.submenu;
    }
    [menu setAutoenablesItems:NO];
    makeMenuItems_(menu, [myDel menuCommands], firstItemIndex, items, count);
    if (firstItemIndex == 0) {
        [menu release];
    }
}

void handleCommand_MacOS(const char *cmd) {
    if (equal_Command(cmd, "prefs.ostheme.changed")) {
        if (arg_Command(cmd)) {
            appearanceChanged_MacOS_(currentSystemAppearance_());
        }
    }
    else if (equal_Command(cmd, "bindings.changed")) {
        NSApplication *app = [NSApplication sharedApplication];
        MyDelegate *myDel = (MyDelegate *) app.delegate;
        NSMenu *appMenu = [app mainMenu];
        int mainIndex = 0;
        for (NSMenuItem *mainMenuItem in appMenu.itemArray) {
            NSMenu *menu = mainMenuItem.submenu;
            if (menu) {
                int itemIndex = 0;
                for (NSMenuItem *menuItem in menu.itemArray) {
                    NSString *command = [[myDel menuCommands] commandForMenuItem:menuItem];
                    if (!command && mainIndex == 6 && itemIndex == 0) {
                        /* Window > Close */
                        command = @"tabs.close";
                    }
                    if (command) {
                        const iBinding *bind = findCommand_Keys(
                            [command cStringUsingEncoding:NSUTF8StringEncoding]);
                        if (bind && bind->id < builtIn_BindingId) {
                            setShortcut_NSMenuItem_(menuItem, bind->key, bind->mods);
                        }
                    }
                    itemIndex++;
                }
            }
            mainIndex++;
        }
    }
    else if (equal_Command(cmd, "emojipicker")) {
        [NSApp orderFrontCharacterPalette:nil];
    }
}

void log_MacOS(const char *msg) {
    NSLog(@"%s", msg);
}

void showPopupMenu_MacOS(iWidget *source, iInt2 windowCoord, const iMenuItem *items, size_t n) {
    NSMenu *      menu         = [[NSMenu alloc] init];
    MenuCommands *menuCommands = [[MenuCommands alloc] init];
    iWindow *     window       = activeWindow_App();
    NSWindow *    nsWindow     = nsWindow_(window->win);
    /* View coordinates are flipped. */
    iBool isCentered = iFalse;
    if (isEqual_I2(windowCoord, zero_I2())) {
        windowCoord = divi_I2(window->size, 2);
        isCentered = iTrue;
    }
    windowCoord.y = window->size.y - windowCoord.y;
    windowCoord = divf_I2(windowCoord, window->pixelRatio);
    NSPoint screenPoint = [nsWindow convertRectToScreen:(CGRect){ { windowCoord.x, windowCoord.y }, 
								  { 0, 0 } }].origin;
    NSMenuItem *selectedItem = makeMenuItems_(menu, menuCommands, 0, items, n);
    [menuCommands setSource:source];
    if (isCentered) {
        NSSize menuSize = [menu size];
        screenPoint.x -= menuSize.width / 2;
        screenPoint.y += menuSize.height / 2;
    }
    [menu setAutoenablesItems:NO];
    /* Fake the release of the left mouse button, in case it's also pressed. Otherwise,
       SDL would miss the release event while the context menu's event loop is running. */
    if (SDL_GetMouseState(NULL, NULL) & SDL_BUTTON_LMASK) {
        SDL_MouseButtonEvent mbe = {
            .type = SDL_MOUSEBUTTONUP,
            .timestamp = SDL_GetTicks(),
            .windowID = id_Window(get_Window()),
            0,
            SDL_BUTTON_LEFT,
            SDL_RELEASED,
            1
        };
        SDL_PushEvent((SDL_Event *) &mbe);
    }
    [menu popUpMenuPositioningItem:selectedItem atLocation:screenPoint inView:nil];
    [menu release];
    [menuCommands release];
    /* The right mouse button has now been released so let SDL know about it. The button up event
       was consumed by the popup menu so it got never passed to SDL. Same goes for possible
       keyboard modifiers that were held down when the menu appeared. */
    SEL sel = NSSelectorFromString(@"syncMouseButtonAndKeyboardModifierState"); /* custom method */
    if ([[nsWindow delegate] respondsToSelector:sel]) {
        NSInvocation *call = [NSInvocation invocationWithMethodSignature:
                              [NSMethodSignature signatureWithObjCTypes:"v@:"]];
        [call setSelector:sel];
        [call invokeWithTarget:[nsWindow delegate]];
    }
}

iColor systemAccent_Color(void) {
#if 0
    if (@available(macOS 10.14, *)) {
	NSColor *accent = [[NSColor controlAccentColor] colorUsingColorSpace:
							    [NSColorSpace deviceRGBColorSpace]];
	return (iColor){ iClamp([accent redComponent]   * 255, 0, 255),
		iClamp([accent greenComponent] * 255, 0, 255),
		iClamp([accent blueComponent]  * 255, 0, 255),
		255 };
    }
#endif
    return (iColor){ 255, 255, 255, 255 };
}

#if defined (LAGRANGE_ENABLE_SPARKLE)

void init_Updater(void) {
    SUUpdater *updater = [SUUpdater sharedUpdater];
    /* Add it to the menu. */
    NSMenu *appMenu = [[[NSApp mainMenu] itemAtIndex:0] submenu];
    NSMenuItem *item = [appMenu insertItemWithTitle:@"Check for Updates…"
                                             action:@selector(checkForUpdates:)
                                      keyEquivalent:@""
                                            atIndex:3];
    item.target = updater;
}

#else
/* dummy as a fallback */
void init_Updater(void) {}
#endif

void deinit_Updater(void) {}
void checkNow_Updater(void) {}
