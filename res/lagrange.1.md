% LAGRANGE(1)
% Jaakko Keränen (jaakko.keranen@iki.fi)
% November 2022

# NAME

lagrange - a beautiful Gemini client

# SYNOPSIS

**lagrange** \[options\]\ \[_URL_\]...\ \[_FILE_\]...

# DESCRIPTION

Lagrange is a graphical client for the Gemini, Gopher, and Finger protocols.
It offers modern conveniences familiar from web browsers, such as smooth scrolling, inline image viewing, multiple tabs, visual themes, Unicode fonts, and bookmarks.

# OPTIONS

When multiple URLs and/or local files are specified, they are opened in separate tabs.

**\--capslock**
:   Enable Caps Lock as a modifier for keybindings.

**-d**, **\--dump**
:   Print contents of URLs/paths to stdout and quit.

**-I**, **\--dump-identity** _ARG_
:   Use identity ARG with **\--dump**. ARG can be a complete or partial client certificate fingerprint or common name.

**-E**, **\--echo**
:   Print all internal application events to stdout. Useful for debugging.

**\--help**
:   List the available command line options.

**\--replace-tab** URL
:   Open a URL, replacing contents of the active tab. Without this option, any URLs on the command line are opened in new tabs.

**-u**, **\--url-or-search** _URL_ | _TEXT_
:   Open a URL, or make a search query with given text. This only works if the search query URL has been configured.

**-U**, **\--user** _DIR_
:   Store user data in the directory DIR instead of the default location.

**-V**, **\--version**
:   Output the version number.

## Window options:

**-h**, **\--height** _N_
:   Set initial window height to _N_ pixels.

**\--prefs-sheet**
:   Open Preferences as a sheet inside the active window.

**\--sw**
:   Disable hardware-accelerated rendering.

**-w**, **\--width** _N_
:   Set initial window width to _N_ pixels.

## Control options:

These options are used to control the currently running Lagrange instance via the command line.

**\--close-tab**
:   Close the current tab.

**-L**, **\--list-tab-urls**
:   Print the URLs of open tabs to stdout. If the app isn't running, nothing is printed.

**\--new-tab** [_URL_]
:   Open a new tab. If the URL argument is omitted, the user's homepage is opened.

**\--tab-url**
:   Print the URL of the active tab.

**-t**, **\--theme** [_ARG_]
:   Change the current UI color theme to ARG ("black", "dark", "light", "white").

# ENVIRONMENT

`LAGRANGE_OVERRIDE_DPI`
:   Override the autodetected screen DPI with a user-provided value. Some window systems and/or monitors may not provide an appropriate DPI value, so this enables further tuning the UI scaling in addition to the "UI scale factor" found in Preferences.

# FILES

User-specific files such as bookmarks and navigation history are stored in the following operating system dependent locations:

- Windows: "C:\\Users\\Name\\AppData\\Roaming\\fi.skyjake.Lagrange"
- macOS: "~/Library/Application Support/fi.skyjake.Lagrange"
- Other: "~/.config/lagrange" 

Use the **\--user** option to store user data in a custom location.

The directory contains:

**bindings.txt**
:   Customized key bindings.

**bookmarks.ini**
:   Bookmarks in TOML format.

**feeds.txt**
:   State of subscribed feeds: all the known entries and latest update timestamps.

**fonts.ini**
:   Custom fonts to load at launch.

**idents.lgr**
:   Information about identities.

**idents/**
:   Subdirectory containing client certificates and private keys in PEM format.

**modmap.txt**
:   Customized keyboard modifier mapping.

**mimehooks.txt**
:   Configuration of external programs to filter page contents depending on MIME type.

**palette.txt**
:   Colors of the UI palette.

**prefs.cfg**
:   User's preferences. This is a list of UI events that gets executed at launch (cf. output of **\--echo**).

**state.lgr**
:   Serialized UI state, specifying open tabs and sidebar state.

**sitespec.ini**
:   Site-specific preferences in TOML format.

**trusted.2.txt**
:   Fingerprints of trusted server certificates.

**visited.2.txt**
:   List of visited URLs with timestamps.

# STANDARDS

* [Gemini Protocol Specification](https://gemini.circumlunar.space/docs/specification.gmi)
* [Gempub Specification](https://codeberg.org/oppenlab/gempub)
* [RFC 1436: The Internet Gopher Protocol](https://datatracker.ietf.org/doc/html/rfc1436)
* [RFC 1288: The Finger User Information Protocol](https://datatracker.ietf.org/doc/html/rfc1288)

# SEE ALSO

Open "about:help" in the application to view the complete Help page.
