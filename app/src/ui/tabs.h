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

#pragma once

/* Tab pages and the tab bar. */

#include <the_Foundation/string.h>

iDeclareType(LabelWidget)
iDeclareType(Widget)

iWidget *       makeTabs_Widget         (iWidget *parent);
void            setTabBarPosition_Widget(iWidget *tabs, iBool atBottom);
void            setVerticalTabBar_Widget(iWidget *tabs);
void            appendTabPage_Widget    (iWidget *tabs, iWidget *page, const char *label, int key, int kmods);
void            appendFramelessTabPage_Widget(iWidget *tabs, iWidget *page, const char *title, int iconColor, int shortcut, int kmods);
iWidget *       appendTwoColumnTabPage_Widget(iWidget *tabs, const char *title, int iconColor, int shortcut,
                                              iWidget **headings, iWidget **values);
void            prependTabPage_Widget   (iWidget *tabs, iWidget *page, const char *label, int key, int kmods);
iWidget *       removeTabPage_Widget    (iWidget *tabs, size_t index); /* returns the page */
void            moveTabPage_Widget      (iWidget *tabs, size_t index, size_t newIndex);
void            resizeToLargestPage_Widget  (iWidget *tabs);
void            showTabPage_Widget      (iWidget *tabs, const iAnyObject *page);
void            addTabCloseButton_Widget(iWidget *tabs, const iWidget *page, const char *command);
void            setTabPageLabel_Widget  (iWidget *tabs, const iAnyObject *page, const iString *label);
iWidget *       tabPage_Widget          (iWidget *tabs, size_t index);
iLabelWidget *  tabPageButton_Widget    (iWidget *tabs, const iAnyObject *page);
iBool           isVerticalTabBar_Widget (const iWidget *tabs);
iBool           isTabButton_Widget      (const iWidget *);
void            moveTabButtonToEnd_Widget(iWidget *tabButton);
size_t          tabPageIndex_Widget     (const iWidget *tabs, const iAnyObject *page);
const iWidget * currentTabPage_Widget   (const iWidget *tabs);
size_t          tabCount_Widget         (const iWidget *tabs);
