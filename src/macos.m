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

#import <AppKit/AppKit.h>

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
    /* TODO: Test if SDL 2.0.16 works better (no stutters with Metal?). */
    return iFalse; /*
    const iInt2 ver = macVer_();
    return ver.x > 10 || ver.y > 13;*/
}

static void ignoreImmediateKeyDownEvents_(void) {
    /* SDL ignores menu key equivalents so the keydown events will be posted regardless.
       However, we shouldn't double-activate menu items when a shortcut key is used in our
       widgets. Quite a kludge: take advantage of Window's focus-acquisition threshold to
       ignore the immediately following key down events. */
    get_Window()->focusGainedAt = SDL_GetTicks();
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
    [super initWithIdentifier:identifier];
    self.view = [NSButton buttonWithTitle:title target:self action:@selector(buttonPressed)];
    command = cmd;
    return self;
}

- (id)initWithIdentifier:(NSTouchBarItemIdentifier)identifier
                   image:(NSImage *)image
                  widget:(iWidget *)widget
                 command:(NSString *)cmd {
    [super initWithIdentifier:identifier];
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
    commands = [[NSMutableDictionary<NSString *, NSString *> alloc] init];
    source = NULL;
    return self;
}

- (void)setCommand:(NSString *)command forMenuItem:(NSMenuItem *)menuItem {
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

/*----------------------------------------------------------------------------------------------*/

@interface MyDelegate : NSResponder<NSApplicationDelegate, NSTouchBarDelegate> {
    enum iTouchBarVariant touchBarVariant;
    NSString *currentAppearanceName;
    NSObject<NSApplicationDelegate> *sdlDelegate;
    //NSMutableDictionary<NSString *, NSString*> *menuCommands;
    MenuCommands *menuCommands;
}
- (id)initWithSDLDelegate:(NSObject<NSApplicationDelegate> *)sdl;
- (NSTouchBar *)makeTouchBar;
- (BOOL)application:(NSApplication *)app openFile:(NSString *)filename;
- (void)application:(NSApplication *)app openFiles:(NSArray<NSString *> *)filenames;
- (void)application:(NSApplication *)app openURLs:(NSArray<NSURL *> *)urls;
- (void)applicationDidFinishLaunching:(NSNotification *)notifications;
@end

@implementation MyDelegate

- (id)initWithSDLDelegate:(NSObject<NSApplicationDelegate> *)sdl {
    [super init];
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
                                                 command:@"tabs.new"];
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
    postCommandf_App("~open url:%s", cstr_String(str));
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

void setupApplication_MacOS(void) {    
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
    NSMenuItem *prefsItem = [appMenu itemWithTitle:@"Preferences…"];
    prefsItem.target = myDel;
    prefsItem.action = @selector(showPreferences);
    /* Get rid of the default window close item */
    NSMenu *windowMenu = [[[NSApp mainMenu] itemWithTitle:@"Window"] submenu];
    NSMenuItem *windowCloseItem = [windowMenu itemWithTitle:@"Close"];
    windowCloseItem.target = myDel;
    windowCloseItem.action = @selector(closeTab);
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

enum iColorId removeColorEscapes_String(iString *d) {
    enum iColorId color = none_ColorId;
    for (;;) {
        const char *esc = strchr(cstr_String(d), '\v');
        if (esc) {
            const char *endp;
            color = parseEscape_Color(esc, &endp);
            remove_Block(&d->chars, esc - cstr_String(d), endp - esc);
        }
        else break;
    }
    return color;
}

/* returns the selected item, if any */
static NSMenuItem *makeMenuItems_(NSMenu *menu, MenuCommands *commands, const iMenuItem *items, size_t n) {
    NSMenuItem *selectedItem = nil;
    for (size_t i = 0; i < n && items[i].label; ++i) {
        const char *label = translateCStr_Lang(items[i].label);
        if (equal_CStr(label, "---")) {
            [menu addItem:[NSMenuItem separatorItem]];
        }
        else {
            const iBool hasCommand = (items[i].command && items[i].command[0]);
            iBool isChecked = iFalse;
            iBool isDisabled = iFalse;
            if (startsWith_CStr(label, "###")) {
                isChecked = iTrue;
                label += 3;
            }
            else if (startsWith_CStr(label, "///")) {
                isDisabled = iTrue;
                label += 3;
            }
            iString itemTitle;
            initCStr_String(&itemTitle, label);
            removeIconPrefix_String(&itemTitle);
            if (removeColorEscapes_String(&itemTitle) == uiTextCaution_ColorId) {
//                prependCStr_String(&itemTitle, "\u26a0\ufe0f ");
            }
            NSMenuItem *item = [menu addItemWithTitle:[NSString stringWithUTF8String:cstr_String(&itemTitle)]
                                               action:(hasCommand ? @selector(postMenuItemCommand:) : nil)
                                        keyEquivalent:@""];
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

void insertMenuItems_MacOS(const char *menuLabel, int atIndex, const iMenuItem *items, size_t count) {
    NSApplication *app = [NSApplication sharedApplication];
    MyDelegate *myDel = (MyDelegate *) app.delegate;
    NSMenu *appMenu = [app mainMenu];
    menuLabel = translateCStr_Lang(menuLabel);
    NSMenuItem *mainItem = [appMenu insertItemWithTitle:[NSString stringWithUTF8String:menuLabel]
                                                 action:nil
                                          keyEquivalent:@""
                                                atIndex:atIndex];
    NSMenu *menu = [[NSMenu alloc] initWithTitle:[NSString stringWithUTF8String:menuLabel]];
    [menu setAutoenablesItems:NO];
    makeMenuItems_(menu, [myDel menuCommands], items, count);
    [mainItem setSubmenu:menu];
    [menu release];
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
    iWindow *     window       = as_Window(mainWindow_App());
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
    NSMenuItem *selectedItem = makeMenuItems_(menu, menuCommands, items, n);
    [menuCommands setSource:source];
    if (isCentered) {
        NSSize menuSize = [menu size];
        screenPoint.x -= menuSize.width / 2;
        screenPoint.y += menuSize.height / 2;
    }
    [menu setAutoenablesItems:NO];
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
#import <Sparkle/Sparkle.h>

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
