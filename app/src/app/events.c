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

#include "impl.h"

#include <lagrange/core.h>
#include <the_Foundation/thread.h>
#include "feeds.h"
#include "periodic.h"
#include "render/text.h"
#include <lagrange/visited.h>
#include "ui/command.h"
#include "ui/gamepad.h"
#include "ui/inputwidget.h"
#include "ui/listwidget.h"
#include "ui/lookupwidget.h"
#include "ui/sidebarwidget.h"
#include "ui/documentwidget.h"
#include "ui/keys.h"
#include "ui/root.h"
#include "ui/touch.h"
#include "ui/util.h"
#include "ui/window.h"

#if defined (iPlatformAppleDesktop)
#   include "platform/macos.h"
#endif
#if defined (iPlatformAppleMobile)
#   include "platform/ios.h"
#   include <CoreFoundation/CoreFoundation.h>
#endif
#if defined (iPlatformAndroidMobile)
#   include "platform/android.h"
#endif
#if defined (iPlatformMsys) || defined (iPlatformWindows)
#   include "platform/win32.h"
#endif
#if defined (LAGRANGE_ENABLE_X11_XLIB)
#   include "platform/x11.h"
#endif

static const int idleThreshold_App_ = 1000; /* ms */

int cmp_Ticker_(const void *a, const void *b) {
    const iTicker *elems[2] = { a, b };
    const int cmp = iCmp(elems[0]->context, elems[1]->context);
    if (cmp) {
        return cmp;
    }
    return iCmp((void *) elems[0]->callback, (void *) elems[1]->callback);
}

#if defined (LAGRANGE_ENABLE_IDLE_SLEEP)
uint32_t checkAsleep_App_(void *param, SDL_TimerID timerID, uint32_t interval) {
    iApp *d = param;
    iUnused(d, timerID);
    SDL_Event ev = { .type = SDL_EVENT_USER };
    ev.user.code = asleep_UserEventCode;
    SDL_PushEvent(&ev);
    return interval;
}
#endif

#if defined (iPlatformAppleMobile)
static bool wakeRunLoopOnEvent_App_(void *userdata, SDL_Event *event) {
    /* This is a callback for more efficient event waiting. */
    iUnused(userdata, event);
    CFRunLoopRef rl = CFRunLoopGetMain();
    CFRunLoopStop(rl);
    CFRunLoopWakeUp(rl);
    return false;
}
#endif

uint32_t postAutoReloadCommand_App_(void *userdata, SDL_TimerID timerID, uint32_t interval) {
    iUnused(userdata, timerID);
    notify_Root(NULL, "document.autoreload");
    return interval;
}

iLocalDef iBool isWaitingAllowed_App_(iApp *d) {
    if (d->warmupFrames > 0) {
        return iFalse;
    }
#if defined (LAGRANGE_ENABLE_IDLE_SLEEP)
    if (d->isIdling) {
        return iFalse;
    }
#endif
    return !isRefreshPending_App();
}

static uint32_t             eventProcessingStartTime_;
static uint32_t             numPendingMotionEvents_;
static iBool                pendingMotionPosted_;
static SDL_MouseMotionEvent pendingMotion_;

static iBool nextEvent_App_(iApp *d, enum iAppEventMode eventMode, SDL_Event *event) {
#if defined (iPlatformAndroidMobile)
    blockWhileAppInBackground_Android(); /* unless audio playing/streaming */
#endif
#if !defined (iPlatformApple)
    /* If there is accumulated mouse motion, don't spend too long processing events.
        We want to refresh the UI ASAP, if necessary. */
    if (eventProcessingStartTime_ == 0) {
        eventProcessingStartTime_ = SDL_GetTicks();
    }
    else if (SDL_GetTicks() - eventProcessingStartTime_ > 16 /* ms; 60 Hz */ &&
             numPendingMotionEvents_ > 0) {
        /* Time to submit the pending motion, if there has been some. */
        if (!pendingMotionPosted_) {
            memcpy(event, &pendingMotion_, sizeof(pendingMotion_));
            iZap(pendingMotion_);
            pendingMotionPosted_ = iTrue;
            return iTrue;
        }
        return iFalse; /* spending too much time processing events, check back later */
    }
#endif /* iPlatformApple */
    if (eventMode == waitForNewEvents_AppEventMode && isWaitingAllowed_App_(d)) {
        /* We may be allowed to block here until an event comes in. */
        if (isWaitingAllowed_App_(d)) {
#if defined (iPlatformAppleMobile)
            if (SDL_PollEvent(event)) {
                /* Events already queued, no need to wait. */
                return iTrue;
            }
            /* Block on the iOS run loop instead of polling via SDL_WaitEvent,
               which would spin internally and use ~4x more CPU.
               `returnAfterSourceHandled=false` lets the loop process unrelated
               system sources (display sync, etc.) internally; we only break out
               when `wakeRunLoopOnEvent_App_` stops the loop in response to an
               actual SDL event being pushed. */
            CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.250, false);
            SDL_PumpEvents();
            return SDL_PollEvent(event);
#else
            if (isAppleDesktop_Platform() && d->window && d->window->type == main_WindowType &&
                as_MainWindow(d->window)->enableBackBuf) {
                /* SDL Metal workaround: if we block here for too long, there will be a longer
                   ~100ms stutter later on after refresh resumes, when the render pipeline does
                   some kind of an update (?). */
                return SDL_WaitEventTimeout(event, 250);
            }
            /* Wait indefinitely. */
            return SDL_WaitEvent(event);
#endif
        }
    }
    /* SDL regression circa 2.0.18? SDL_PollEvent() doesn't always return
       events posted immediately beforehand. Waiting with a very short timeout
       seems to work better. */
#if !defined (iPlatformTerminal) && defined (iPlatformLinux) && SDL_VERSION_ATLEAST(2, 0, 18)
    return SDL_WaitEventTimeout(event, 1);
#else
    return SDL_PollEvent(event);
#endif
}

void processEvents_App(enum iAppEventMode eventMode) {
    iApp *d = &app_;
    iRoot *oldCurrentRoot = current_Root(); /* restored afterwards */
    SDL_Event ev;
#if defined (LAGRANGE_ENABLE_IDLE_SLEEP)
    iBool gotEvents = iFalse;
#endif
    iBool gotRefresh = iFalse;
    iPtrArray windows;
    init_PtrArray(&windows);
    /* A bit of global state, alas, but we need to protect against a flood of mouse
       motion events that get us stuck here in the event processing loop for too long. */
    eventProcessingStartTime_ = 0;
    numPendingMotionEvents_ = 0;
    pendingMotionPosted_ = iFalse;
    iZap(pendingMotion_);
    while (nextEvent_App_(d, gotRefresh ? postedEventsOnly_AppEventMode : eventMode, &ev)) {
#if defined (iPlatformAppleMobile)
        if (processEvent_iOS(&ev)) {
            continue;
        }
#endif
        switch (ev.type) {
            case SDL_EVENT_QUIT:
                if (isDesktop_Platform() || isMobileLinux_Platform() || isHandheld_Platform()) {
                    d->isRunning = iFalse;
                    if (findWidget_App("prefs")) {
                        /* Make sure changed preferences get saved. */
                        postCommand_Root(NULL, "prefs.dismiss");
                        processEvents_App(postedEventsOnly_AppEventMode);
                    }
                    goto backToMainLoop;
                }
                break;
            case SDL_EVENT_DROP_FILE: {
                if (isDesktop_Platform() && !d->window) {
                    /* Need to open an empty window now. */
                    handleNonWindowRelatedCommand_App_(d, "window.new url:");
                    iAssert(d->window != NULL);
                }
                iBool wasUsed = iFalse;
                if (d->window) {
                    wasUsed = processEvent_Window(as_Window(d->window), &ev);
                }
                if (!wasUsed) {
                    if (startsWithCase_CStr(ev.drop.data, "gemini:") ||
                        startsWithCase_CStr(ev.drop.data, "gopher:") ||
                        startsWithCase_CStr(ev.drop.data, "spartan:") ||
                        startsWithCase_CStr(ev.drop.data, "nex:") ||
                        startsWithCase_CStr(ev.drop.data, "misfin:") ||
                        startsWithCase_CStr(ev.drop.data, "file:")) {
                        postCommandf_Root(NULL, "~open newtab:1 url:%s", ev.drop.data);
                    }
                    else {
                        postCommandf_Root(NULL,
                            "~open newtab:1 url:%s", makeFileUrl_CStr(ev.drop.data));
                    }
                }
                /* Note: ev.drop.data is owned by SDL and must not be freed. */
                break;
            }
            default: {
                if (ev.type == SDL_EVENT_USER && ev.user.code == periodic_UserEventCode) {
                    dispatchCommands_Periodic(&d->periodic);
                    continue;
                }
                if (ev.type == SDL_EVENT_USER && ev.user.code == releaseObject_UserEventCode) {
                    iRelease(ev.user.data1);
                    continue;
                }
                if (ev.type == SDL_EVENT_USER && ev.user.code == refresh_UserEventCode) {
                    gotRefresh = iTrue;
                    continue;
                }
#if defined (LAGRANGE_ENABLE_IDLE_SLEEP)
                if (ev.type == SDL_EVENT_USER && ev.user.code == asleep_UserEventCode) {
                    if (SDL_GetTicks() - d->lastEventTime > idleThreshold_App_ &&
                        isEmpty_SortedArray(&d->tickers)) {
                        if (!d->isIdling) {
# if !defined (NDEBUG) && !defined (iPlatformTerminal)
                            printf("[App] idling...\n");
                            fflush(stdout);
# endif
                        }
                        d->isIdling = iTrue;
                    }
                    continue;
                }
                d->lastEventTime = SDL_GetTicks();
                if (d->isIdling) {
# if !defined (NDEBUG) && !defined (iPlatformTerminal)
                    printf("[App] ...woke up\n");
                    fflush(stdout);
# endif
                }
                d->isIdling = iFalse;
                gotEvents = iTrue;
#endif /* LAGRANGE_ENABLE_IDLE_SLEEP */
                if (processEvent_Gamepad(d->gamepad, &ev)) {
                    /* Controller events are eaten and turned into key/button and wheel events. */
                    continue;
                }
                /* Keyboard modifier mapping. */
                if (ev.type == SDL_EVENT_KEY_DOWN || ev.type == SDL_EVENT_KEY_UP) {
                    if (d->prefs.capsLockKeyModifier) {
                        /* Track Caps Lock state as a modifier. */
                        if (ev.key.key == SDLK_CAPSLOCK) {
                            setCapsLockDown_Keys(ev.key.down);
                        }
                    }
                    else {
                        ev.key.mod &= ~SDL_KMOD_CAPS;
                    }
                    if (!isTextInputActive_App()) {
                        ev.key.mod = mapMods_Keys(ev.key.mod & ~SDL_KMOD_CAPS);
                    }
                }
#if defined (iPlatformAndroidMobile)
                /* Use the system Back button to close panels, if they're open. */
                if (ev.type == SDL_EVENT_KEY_DOWN && ev.key.key == SDLK_AC_BACK) {
                    if (ev.key.repeat) {
                        /* Holding down the Back button should not keep going back in history. */
                        continue;
                    }
                    const uint32_t now = SDL_GetTicks();
                    if (now - d->lastBackButtonTime < 100) {
                        /* Suspiciously rapid, must be a double-posted event. The behavior of
                           the back button/gesture has been changing over the years, so on some
                           versions of Android it may get handled via multiple mechanisms. */
                        continue;
                    }
                    d->lastBackButtonTime = now;
                    SDL_UserEvent panelBackCmd = { .type = SDL_EVENT_USER,
                                                   .code = command_UserEventCode,
                                                   .data1 = iDupStr("panel.close"),
                                                   .data2 = d->window->keyRoot };
                    if (dispatchEvent_Window(d->window, (SDL_Event *) &panelBackCmd)) {
                        continue; /* Was handled by someone. */
                    }
                }
                /* Ignore all mouse events; just use touch. */
                if (ev.type == SDL_EVENT_MOUSE_BUTTON_DOWN ||
                    ev.type == SDL_EVENT_MOUSE_BUTTON_UP ||
                    ev.type == SDL_EVENT_MOUSE_MOTION ||
                    ev.type == SDL_EVENT_MOUSE_WHEEL) {
                    continue;
                }
#endif /* iPlatformAndroidMobile */
#if defined (iPlatformMsys) || defined (iPlatformWindows)
                /* Scroll events may be per-pixel or mouse wheel steps. */
                if (ev.type == SDL_EVENT_MOUSE_WHEEL) {
                    ev.wheel.x = -ev.wheel.x;
                }
#endif /* iPlatformMsys */
#if !defined (iPlatformApple)
                /* High-precision mice may emit way too many motion events. We'll just
                   accumulate the extra ones here. If too much time passes, we'll break the
                   processing loop to check if refresh is needed, after posting an
                   accumulated motion event. */
                if (ev.type == SDL_EVENT_MOUSE_MOTION && !pendingMotionPosted_) {
                    if (numPendingMotionEvents_++ > 0) {
                        pendingMotion_.type      = SDL_EVENT_MOUSE_MOTION;
                        pendingMotion_.timestamp = SDL_GetTicksNS();
                        pendingMotion_.state     = ev.motion.state;
                        pendingMotion_.which     = ev.motion.which;
                        pendingMotion_.windowID  = ev.motion.windowID;
                        pendingMotion_.x         = ev.motion.x;
                        pendingMotion_.y         = ev.motion.y;
                        pendingMotion_.xrel     += ev.motion.xrel;
                        pendingMotion_.yrel     += ev.motion.yrel;
                        continue;
                    }
                }
#endif /* iPlatformApple */
#if defined (LAGRANGE_ENABLE_MOUSE_TOUCH_EMULATION)
                /* Convert mouse events to finger events to test the touch handling. */ {
                    static float xPrev = 0.0f;
                    static float yPrev = 0.0f;
                    if (ev.type == SDL_EVENT_MOUSE_BUTTON_DOWN || ev.type == SDL_EVENT_MOUSE_BUTTON_UP) {
                        const float xf = (d->window->pixelRatio * ev.button.x) / (float) d->window->size.x;
                        const float yf = (d->window->pixelRatio * ev.button.y) / (float) d->window->size.y;
                        ev.type = (ev.type == SDL_EVENT_MOUSE_BUTTON_DOWN ? SDL_EVENT_FINGER_DOWN : SDL_EVENT_FINGER_UP);
                        ev.tfinger.x = xf;
                        ev.tfinger.y = yf;
                        ev.tfinger.dx = xf - xPrev;
                        ev.tfinger.dy = yf - yPrev;
                        xPrev = xf;
                        yPrev = yf;
                        ev.tfinger.fingerID = 0x1234;
                        ev.tfinger.pressure = 1.0f;
                        ev.tfinger.timestamp = SDL_GetTicksNS();
                        ev.tfinger.touchID = SDL_TOUCH_MOUSEID;
                    }
                    else if (ev.type == SDL_EVENT_MOUSE_MOTION) {
                        if (~ev.motion.state & SDL_BUTTON_MASK(SDL_BUTTON_LEFT)) {
                            continue; /* only when pressing a button */
                        }
                        const float xf = (d->window->pixelRatio * ev.motion.x) / (float) d->window->size.x;
                        const float yf = (d->window->pixelRatio * ev.motion.y) / (float) d->window->size.y;
                        ev.type = SDL_EVENT_FINGER_MOTION;
                        ev.tfinger.x = xf;
                        ev.tfinger.y = yf;
                        ev.tfinger.dx = xf - xPrev;
                        ev.tfinger.dy = yf - yPrev;
                        xPrev = xf;
                        yPrev = yf;
                        ev.tfinger.fingerID = 0x1234;
                        ev.tfinger.pressure = 1.0f;
                        ev.tfinger.timestamp = SDL_GetTicksNS();
                        ev.tfinger.touchID = SDL_TOUCH_MOUSEID;
                    }
                }
#endif /* LAGRANGE_ENABLE_MOUSE_TOUCH_EMULATION */
                iBool wasUsed = iFalse;
                /* Per-window processing. */
                if (!wasUsed && (!isEmpty_PtrArray(&d->mainWindows) ||
                                 !isEmpty_PtrArray(&d->extraWindows))) {
                    listWindows_App_(d, &windows);
                    iConstForEach(PtrArray, iter, &windows) {
                        iWindow *window = iter.ptr;
                        setCurrent_Window(window);
                        /* Focus navigation events take priority over regular processing. */
                        /* Keyboard focus navigation with arrow keys. */
                        if (focus_Widget() && ev.type == SDL_EVENT_KEY_DOWN &&
                            keyMods_Sym(ev.key.mod) == 0) {
                            if (moveFocusInsideMenu_App(&ev)) {
                                wasUsed = iTrue;
                            }
                            else {
                                const int key = ev.key.key;
                                if ((key == SDLK_DOWN || key == SDLK_UP || key == SDLK_LEFT ||
                                     key == SDLK_RIGHT) &&
                                    /* Some widgets handle arrow keys themselves: */
                                    !isInstance_Object(focus_Widget(), &Class_DocumentWidget) &&
                                    !isInstance_Object(focus_Widget(), &Class_ListWidget) &&
                                    !isInstance_Object(focus_Widget(), &Class_InputWidget) &&
                                    !isInstance_Object(focus_Widget(), &Class_LookupWidget)) {
                                    wasUsed = moveFocusWithArrows_App(&ev);
                                }
                            }
                        }
                        if (wasUsed) {
                            break;
                        }
                        window->lastHover = window->hover;
                        /* When clicking a mouse button, we need to be able to close previously
                           existing popup windows. However, after the event has been processed,
                           a new popup menu may have just opened, so we first take a copy
                           of the existing list of popups. */
                        const iPtrArray *lastPopupWindows = ev.type == SDL_EVENT_MOUSE_BUTTON_DOWN ?
                            collect_PtrArray(copy_PtrArray(&d->popupWindows)) : NULL;
                        wasUsed = processEvent_Window(window, &ev);
                        if (wasUsed) {
                            if (ev.type == SDL_EVENT_MOUSE_BUTTON_DOWN && window->type != popup_WindowType &&
                                !isEmpty_Array(lastPopupWindows)) {
                                /* Clicking outside the open popups is supposed to close all of them. */
                                iConstForEach(PtrArray, i, lastPopupWindows) {
                                    iWindow *pop = i.ptr;
                                    iRoot *popRoot = pop->roots[0];
                                    if (popRoot && !isRecentlyDeleted_Widget(popRoot->widget)) {
                                        postCommandf_Root(popRoot, "menu.cancel");
                                    }
                                }
                            }
                            break;
                        }
                        if (!wasUsed && window == d->window) {
                            /* Keybindings are handled after the frontmost window's widgets. */
                            wasUsed = processEvent_Keys(&ev);
                            if (wasUsed) {
                                break;
                            }
                        }
                    }
                }
                setCurrent_Window(d->window);
                if (!wasUsed) {
                    /* Focus cycling. */
                    if (ev.type == SDL_EVENT_KEY_DOWN && ev.key.key == SDLK_TAB && current_Root()) {
                        iWidget *startFrom = focus_Widget();
                        const iBool isLimitedFocus = focusRoot_Widget(startFrom) != get_Root()->widget;
                        /* Go to a sidebar if one is visible. */
                        if (!startFrom && !isLimitedFocus &&
                            isVisible_Widget(findWidget_App("sidebar"))) {
                            setFocus_Widget(as_Widget(
                                list_SidebarWidget((iSidebarWidget *) findWidget_App("sidebar"))));
                        }
                        else if (!startFrom && !isLimitedFocus &&
                            isVisible_Widget(findWidget_App("sidebar2"))) {
                            setFocus_Widget(as_Widget(
                                list_SidebarWidget((iSidebarWidget *) findWidget_App("sidebar2"))));
                        }
                        else {
                            setFocus_Widget(findFocusable_Widget(startFrom,
                                                                 ev.key.mod & SDL_KMOD_SHIFT
                                                                 ? backward_WidgetFocusDir
                                                                 : forward_WidgetFocusDir));
                        }
                        wasUsed = iTrue;
                    }
                }
                if (!wasUsed && ev.type == SDL_EVENT_KEY_DOWN && ev.key.key == SDLK_ESCAPE &&
                    current_Root() && focus_Widget() &&
                    focusRoot_Widget(focus_Widget()) == get_Root()->widget) {
                    /* Pressing Escape will clear focus. */
                    setFocus_Widget(NULL);
                    wasUsed = iTrue;
                }
                if (!wasUsed) {
                    if (!isMobile_Platform() && ev.type == SDL_EVENT_KEY_DOWN &&
                        ev.key.key == SDLK_RETURN &&
                        focusRoot_Widget(focus_Widget()) == get_Root()->widget &&
                        !keyMods_Sym(ev.key.mod)) {
                        /* The Return key is hardcoded key for focusing the URL field.
                           Note that you can't bind anything to Return normally, and it is
                           of course used when entering text. */
                        postCommand_App("navigate.focus");
                        wasUsed = iTrue;
                    }
                }
                if (!wasUsed) {
                    /* ^G is an alternative for Escape. */
                    if (ev.type == SDL_EVENT_KEY_DOWN && ev.key.key == 'g' &&
                        keyMods_Sym(ev.key.mod) == SDL_KMOD_CTRL) {
                        SDL_KeyboardEvent esc = ev.key;
                        esc.key = SDLK_ESCAPE;
                        esc.mod = 0;
                        SDL_PushEvent((SDL_Event *) &esc);
                        esc.down = false;
                        esc.type  = SDL_EVENT_KEY_UP;
                        esc.timestamp++;
                        SDL_PushEvent((SDL_Event *) &esc);
                        wasUsed = iTrue;
                    }
                }
                if (ev.type == SDL_EVENT_USER && ev.user.code == command_UserEventCode) {
#if !defined (iPlatformTerminal)
#   if defined (iPlatformAppleDesktop)
                    handleCommand_MacOS(command_UserEvent(&ev));
#   endif
#   if defined (iPlatformAndroidMobile)
                    handleCommand_Android(command_UserEvent(&ev));
#   endif
#   if defined (iPlatformMsys) || defined (iPlatformWindows)
                    handleCommand_Win32(command_UserEvent(&ev));
#   endif
#   if defined (LAGRANGE_ENABLE_X11_XLIB)
                    handleCommand_X11(command_UserEvent(&ev));
#   endif
#endif /* !defined (iPlatformTerminal )*/
                    if (isMetricsChange_UserEvent(&ev)) {
                        listWindows_App_(d, &windows);
                        iConstForEach(PtrArray, iter, &windows) {
                            iWindow *window = iter.ptr;
                            iForIndices(i, window->roots) {
                                iRoot *root = window->roots[i];
                                if (root) {
                                    arrange_Widget(root->widget);
                                }
                            }
                        }
                    }
                    if (!wasUsed) {
                        /* No widget handled the command, so we'll do it. */
                        setCurrent_Window(d->window);
                        handleCommand_App(ev.user.data1);
                    }
                    /* Allocated by postCommand_Apps(). */
                    free(ev.user.data1);
                }
                /* Refresh after hover changes. */ {
                    listWindows_App_(d, &windows);
                    iConstForEach(PtrArray, iter, &windows) {
                        iWindow *window = iter.ptr;
                        if (window->lastHover != window->hover) {
                            refresh_Widget(window->lastHover);
                            refresh_Widget(window->hover);
                        }
                    }
                }
                break;
            }
        }
    }
    if (pendingMotion_.type != 0 && !pendingMotionPosted_) {
        /* We didn't get to post the pending motion yet. Must do it now or the events are lost. */
        SDL_Event ev;
        memcpy(&ev, &pendingMotion_, sizeof(pendingMotion_));
        SDL_PushEvent(&ev);
    }
#if defined (LAGRANGE_ENABLE_IDLE_SLEEP)
    if (d->isIdling && !gotEvents) {
        /* This is where we spend most of our time when idle. The sleep delay depends on the
           display refresh rate. iOS SDL_WaitEvent() seems to use 10x more CPU time compared to
           just sleeping (2.0.18). */
        SDL_Delay(d->idleSleepDelayMs);
    }
#endif
backToMainLoop:;
    deinit_PtrArray(&windows);
    setCurrent_Root(oldCurrentRoot);
}

static void handleLifecycleEvent_App_(iApp *d, const SDL_Event *ev) {
    switch (ev->type) {
        case SDL_EVENT_TERMINATING: {
#if defined (iPlatformAndroidMobile)
            /* Sent from the Java UI thread (SDLActivity.onDestroy()): app and widget
               state must not be touched. App state was already saved on the SDL main
               thread when the app went to the background, and will be saved again by
               deinit_App(). */
#else
            iForEach(PtrArray, i, &d->mainWindows) {
                setFreezeDraw_MainWindow(*i.value, iTrue);
            }
#  if defined (iPlatformAppleMobile)
            /* SDL docs warn that we may not get any execution time after this event,
               so deinitialize everything immediately. */
            deinit_App(d);
            d->isRunning = iFalse; /* stop the main loop; no further event processing */
#  else
            savePrefs_App_(d);
            saveState_App_(d, iTrue);
#  endif
#endif
            break;
        }
        case SDL_EVENT_LOW_MEMORY:
            clearCache_App_();
            break;
        case SDL_EVENT_WILL_ENTER_FOREGROUND:
            invalidate_Window(as_Window(d->window));
            d->isSuspended = iFalse;
            break;
        case SDL_EVENT_DID_ENTER_FOREGROUND:
            d->warmupFrames = 5;
#if defined (LAGRANGE_ENABLE_IDLE_SLEEP)
            d->isIdling = iFalse;
            d->lastEventTime = SDL_GetTicks();
#endif
            postRefreshAllWindows_App();
            if (d->isTextInputActive) {
                SDL_StartTextInput(as_Window(d->window)->win);
            }
            notify_App("media.player.update"); /* in case there are any */
            break;
        case SDL_EVENT_WILL_ENTER_BACKGROUND: {
#if defined (iPlatformAppleMobile)
            updateNowPlayingInfo_iOS();
#endif
#if defined (iPlatformAndroidMobile)
            postCommand_App("backup.now"); /* ensure there's a copy of user data */
            saveIdentities_GmCerts(certs_App());
            save_Bookmarks(d->bookmarks, dataDir_App_());
#endif
            iForEach(PtrArray, i, &d->mainWindows) {
                setFreezeDraw_MainWindow(*i.value, iTrue);
            }
            if (d->pendingVisitedSave) {
                save_Visited(visited_App(), dataDir_App_());
                d->pendingVisitedSave = iFalse;
            }
            savePrefs_App_(d);
            saveState_App_(d, iTrue);
            d->isSuspended = iTrue;
            if (d->isTextInputActive) {
                SDL_StopTextInput(as_Window(d->window)->win);
            }
            break;
        }
    }
}

static void runTickers_App_(iApp *d) {
    const uint32_t now = SDL_GetTicks();
    d->elapsedSinceLastTicker = (d->lastTickerTime ? now - d->lastTickerTime : 0);
    d->lastTickerTime = now;
    if (isEmpty_SortedArray(&d->tickers)) {
        d->lastTickerTime = 0;
        return;
    }
    /* Update window state. */ {
        iPtrArray *winList = listWindows_App();
        iForEach(PtrArray, i, winList) {
            iWindow *win = *i.value;
            /* Tickers must see the same animation time that the upcoming draw will use;
               otherwise anything a ticker positions from an Anim value is stale by one
               frame relative to content drawn straight from the same Anim. */
            win->frameTime = now;
            iForIndices(i, win->roots) {
                iRoot *root = win->roots[i];
                if (root) {
                    root->didAnimateVisualOffsets = iFalse;
                }
            }
        }
        delete_PtrArray(winList);
    }
    /* Tickers may add themselves again, so we'll run off a copy. */
    iSortedArray *pending = copy_SortedArray(&d->tickers);
    clear_SortedArray(&d->tickers);
    iConstForEach(Array, i, &pending->values) {
        const iTicker *ticker = i.value;
        if (ticker->callback) {
            if (ticker->root) {
                setCurrent_Window(ticker->root->window);
            }
            setCurrent_Root(ticker->root); /* root might be NULL */
            ticker->callback(ticker->context);
        }
    }
    setCurrent_Root(NULL);
    delete_SortedArray(pending);
    if (isEmpty_SortedArray(&d->tickers)) {
        d->lastTickerTime = 0;
    }
}

static bool lifecycleWatcher_App_(void *user, SDL_Event *event) {
    /* Application lifecycle events are delivered synchronously to event watchers
       (the app may be suspended/terminated before the main loop polls again). */
    switch (event->type) {
        case SDL_EVENT_TERMINATING:
        case SDL_EVENT_LOW_MEMORY:
        case SDL_EVENT_WILL_ENTER_FOREGROUND:
        case SDL_EVENT_DID_ENTER_FOREGROUND:
        case SDL_EVENT_WILL_ENTER_BACKGROUND:
            handleLifecycleEvent_App_(user, event);
            break;
    }
    return false;
}

static bool resizeWatcher_(void *user, SDL_Event *event) {
    iApp *d = user;
    if (event->type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED) {
        const SDL_WindowEvent *winev = &event->window;
#if defined (iPlatformMsys) || defined (iPlatformWindows)
        /* TODO: Investigate if this is still necessary. */
        setCurrent_Window(d->window);
        resetFontCache_Text(text_Window(d->window)); {
            SDL_Event u = { .type = SDL_EVENT_USER };
            u.user.code = command_UserEventCode;
            u.user.data1 = iDupStr("theme.changed auto:1");
            dispatchEvent_Window(as_Window(d->window), &u);
        }
#endif
        /* Find the window that is being resized and redraw it immediately. */
        iForEach(PtrArray, i, &d->mainWindows) {
            iMainWindow *win = i.ptr;
            if (SDL_GetWindowID(win->base.win) == winev->windowID) {
                drawWhileResizing_MainWindow(win, winev->data1, winev->data2);
                break;
            }
        }
    }
    return false;
}

iLocalDef iBool isResizeDrawEnabled_(void) {
    if (isMobile_Platform()) {
        return iFalse;
    }
#if defined (LAGRANGE_ENABLE_RESIZE_DRAW)
#   if defined (LAGRANGE_ENABLE_X11_XLIB)
    if (!isXSession_X11()) {
        return iFalse; /* not on Wayland */
    }
#   endif
    return iTrue;
#else
    return iFalse;
#endif
}

int run_App_(iApp *d) {
    /* Initial arrangement. */
    iForIndices(i, d->window->roots) {
        if (d->window->roots[i]) {
            arrange_Widget(d->window->roots[i]->widget);
        }
    }
    d->isRunning = iTrue;
    SDL_SetEventEnabled(SDL_EVENT_DROP_FILE, true); /* open files via drag'n'drop */
    SDL_AddEventWatch(lifecycleWatcher_App_, d); /* synchronous handling */
    if (isResizeDrawEnabled_()) {
        SDL_AddEventWatch(resizeWatcher_, d); /* redraw window during resizing */
    }
#if defined (iPlatformAppleMobile)
    /* Wakeup hook for the CFRunLoop-based event wait in nextEvent_App_(). */
    SDL_AddEventWatch(wakeRunLoopOnEvent_App_, NULL);
#endif
    while (d->isRunning) {
        processEvents_App(waitForNewEvents_AppEventMode);
        runTickers_App_(d);
        refresh_App();
        /* Change the widget tree while we are not iterating through it. */
        if (d->window && d->window->type == main_WindowType) {
            checkPendingSplit_MainWindow(as_MainWindow(d->window));
        }
        recycle_Garbage();
    }
    SDL_RemoveEventWatch(resizeWatcher_, d);
    SDL_RemoveEventWatch(lifecycleWatcher_App_, d);
#if defined (iPlatformAppleMobile)
    SDL_RemoveEventWatch(wakeRunLoopOnEvent_App_, NULL);
#endif
    return 0;
}

void refresh_App(void) {
    iApp *d = &app_;
#if defined (LAGRANGE_ENABLE_IDLE_SLEEP)
    if (d->warmupFrames == 0 && d->isIdling) {
        return;
    }
#endif
    iPtrArray windows;
    init_PtrArray(&windows);
    listWindows_App_(d, &windows);
    /* Destroy pending widgets. */ {
        clearRecentlyDeleted_Widget();
        iConstForEach(PtrArray, j, &windows) {
            iWindow *win = j.ptr;
            setCurrent_Window(win);
            iForIndices(i, win->roots) {
                iRoot *root = win->roots[i];
                if (root) {
                    destroyPending_Root(root);
                }
            }
        }
        listWindows_App_(d, &windows); /* maybe some windows were deleted, too */
    }
    /* TODO: `pendingRefresh` should be window-specific. */
    if (d->warmupFrames || exchange_Atomic(&d->pendingRefresh, iFalse)) {
        /* Draw each window. */
        iConstForEach(PtrArray, j, &windows) {
            iWindow *win = j.ptr;
            if (!d->warmupFrames && !exchange_Atomic(&win->isRefreshPending, iFalse)) {
                continue; /* No need to draw this window. */
            }
            setCurrent_Window(win);
            switch (win->type) {
                case main_WindowType: {
//                    iTime draw;
//                    initCurrent_Time(&draw);
                    draw_MainWindow(as_MainWindow(win));
//                    printf("draw: %lld \u03bcs\n", (long long) (elapsedSeconds_Time(&draw) * 1000000));
//                    fflush(stdout);
                    break;
                }
                default:
                    draw_Window(win);
                    break;
            }
            win->frameCount++;
            if (isTerminal_Platform()) {
                sleep_Thread(1.0 / 60.0);
            }
        }
    }
    else {
#if defined (iPlatformApple)
        /* Nothing needs redrawing, however we may have to keep blitting the latest contents.
           The Metal renderer can get stuttery otherwise. */
        iConstForEach(PtrArray, j, &windows) {
            iWindow *win = j.ptr;
            switch (win->type) {
                case main_WindowType:
                    drawQuick_MainWindow(as_MainWindow(win));
                    break;
                default:
                    break;
            }
        }
#endif
    }
    if (d->warmupFrames > 0) {
        d->warmupFrames--;
    }
    deinit_PtrArray(&windows);
}

iBool isRefreshPending_App(void) {
    const iApp *d = &app_;
    return !isEmpty_SortedArray(&d->tickers) || value_Atomic(&app_.pendingRefresh);
}

iBool isSuspended_App(void) {
#if defined (iPlatformAndroidMobile)
    if (isAppInBackground_Android()) return iTrue;
#endif
    return app_.isSuspended;
}

iBool isFinishedLaunching_App(void) {
    return app_.isFinishedLaunching;
}

uint32_t elapsedSinceLastTicker_App(void) {
    return app_.elapsedSinceLastTicker;
}

void addTicker_App(iTickerFunc ticker, iAny *context) {
    iApp *d = &app_;
    insert_SortedArray(&d->tickers, &(iTicker){ context, get_Root(), ticker });
    postRefresh_Window(get_Root()->window);
}

void addTickerRoot_App(iTickerFunc ticker, iRoot *root, iAny *context) {
    iApp *d = &app_;
    insert_SortedArray(&d->tickers, &(iTicker){ context, root, ticker });
    postRefresh_Window(root ? root->window : NULL);
}

void removeTicker_App(iTickerFunc ticker, iAny *context) {
    iApp *d = &app_;
    remove_SortedArray(&d->tickers, &(iTicker){ context, NULL, ticker });
}
