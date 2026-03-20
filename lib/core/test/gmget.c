/* Copyright 2026 Jaakko Keränen <jaakko.keranen@iki.fi>

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

/* gmget: Fetch a Gemini (or related protocol) URL and write the response body
   to stdout or a file. A simple CLI tool for testing the Lagrange core library. */

#include <lagrange/core.h>
#include <lagrange/gmcerts.h>
#include <lagrange/gmrequest.h>
#include <lagrange/prefs.h>
#include <lagrange/sitespec.h>

#include <the_Foundation/commandline.h>
#include <the_Foundation/file.h>
#include <the_Foundation/mutex.h>
#include <the_Foundation/path.h>
#include <the_Foundation/stringlist.h>

#include <stdio.h>
#include <stdlib.h>

#define outputPath_CommandLineOption "output;o"
#define identity_CommandLineOption   "ident"
#define dataDir_CommandLineOption    "data-dir"
#define printMeta_CommandLineOption  "print-meta;q"
#define help_CommandLineOption       "help;?"

iDeclareType(State)

struct Impl_State {
    iMutex     *mutex;
    iCondition *finished;
    size_t      remaining;
    iBool       printMeta;
    iFile      *outputFile; /* NULL means write to stdout */
};

static const char *defaultDataDir_(void) {
#if defined (iPlatformAppleDesktop)
    return "~/Library/Application Support/fi.skyjake.Lagrange";
#elif defined (iPlatformAppleMobile)
    return "~/Library/Application Support";
#elif defined (iPlatformMsys) || defined (iPlatformWindows)
    return "~/AppData/Roaming/fi.skyjake.Lagrange";
#elif defined (iPlatformHaiku)
    return "~/config/settings/lagrange";
#else /* Linux and other Unix */
    const char *xdg = getenv("XDG_CONFIG_HOME");
    if (xdg) {
        static char buf[512];
        snprintf(buf, sizeof(buf), "%s/lagrange", xdg);
        return buf;
    }
    return "~/.config/lagrange";
#endif
}

static void requestFinished_(iAnyObject *obj, iGmRequest *req) {
    iState *d = userData_Object(obj);
    lock_Mutex(d->mutex);
    if (d->printMeta) {
        fprintf(stderr, "%d %s\n", status_GmRequest(req), cstr_String(meta_GmRequest(req)));
    }
    const iBlock *body = body_GmRequest(req);
    if (d->outputFile) {
        write_File(d->outputFile, body);
    }
    else {
        fwrite(constData_Block(body), size_Block(body), 1, stdout);
    }
    if (--d->remaining == 0) {
        signal_Condition(d->finished);
    }
    unlock_Mutex(d->mutex);
}

static void printUsage_(const char *prog) {
    fprintf(stderr,
            "Usage: %s [OPTIONS] URL...\n"
            "\n"
            "Options:\n"
            "  -o FILE, --output=FILE   Write response body to FILE instead of stdout\n"
            "  --ident=NAME             Use the Lagrange identity matching NAME\n"
            "  --data-dir=DIR           Lagrange data directory (default: platform-specific)\n"
            "  -q, --print-meta         Print response status code and meta string to stderr\n"
            "  -?, --help               Print this help and exit\n",
            prog);
}

static void printError_(const char *format, ...) {
    va_list args;
    va_start(args, format);
    fprintf(stderr, "gmget: ");
    vfprintf(stderr, format, args);
    fprintf(stderr, "\n");
    va_end(args);
}

int main(int argc, char *argv[]) {
    init_Foundation();
    /* First check command line arguments. */
    iCommandLine cl;
    init_CommandLine(&cl, argc, argv); {
        defineValues_CommandLine(&cl, outputPath_CommandLineOption, 1);
        defineValues_CommandLine(&cl, identity_CommandLineOption,   1);
        defineValues_CommandLine(&cl, dataDir_CommandLineOption,    1);
        defineValues_CommandLine(&cl, printMeta_CommandLineOption,  0);
        defineValues_CommandLine(&cl, help_CommandLineOption,       0);
    }
    if (contains_CommandLine(&cl, help_CommandLineOption)) {
        printUsage_(argv[0]);
        deinit_CommandLine(&cl);
        deinit_Foundation();
        return 0;
    }
    /* Collect positional URL arguments. */
    iStringList *urls = iClob(new_StringList());
    iConstForEach(CommandLine, cli, &cl) {
        if (cli.argType == value_CommandLineArgType) {
            pushBack_StringList(urls, collectNewRange_String(cli.entry));
        }
    }
    if (isEmpty_StringList(urls)) {
        printError_("no URL given\n");
        printUsage_(argv[0]);
        deinit_CommandLine(&cl);
        deinit_Foundation();
        return 1;
    }
    const iCommandLineArg *arg;
    iState d = {
        .printMeta = contains_CommandLine(&cl, printMeta_CommandLineOption),
    };
    /* Data directory for loading identities. */
    arg = iClob(checkArgumentValues_CommandLine(&cl, dataDir_CommandLineOption, 1));
    const iString *dataDir = collect_String(arg ? cleaned_Path(value_CommandLineArg(arg, 0))
                                                : cleanedCStr_Path(defaultDataDir_()));
    /* Output file (optional). */
    if ((arg = iClob(checkArgumentValues_CommandLine(&cl, outputPath_CommandLineOption, 1))) !=
        NULL) {
        d.outputFile = iClob(new_File(value_CommandLineArg(arg, 0)));
        if (!open_File(d.outputFile, writeOnly_FileMode)) {
            printError_("cannot open output file: %s", cstr_String(value_CommandLineArg(arg, 0)));
            deinit_CommandLine(&cl);
            deinit_Foundation();
            return 1;
        }
    }
    /* Now we can set up the Lagrange core. */
    init_Core();
    init_SiteSpec(cstr_String(dataDir));
    iPrefs   *prefs = new_Prefs();
    iGmCerts *certs = new_GmCerts(cstr_String(dataDir));
    setReadOnly_GmCerts(certs, iTrue);
    /* Optional client identity. */
    const iGmIdentity *ident = NULL;
    if ((arg = iClob(checkArgumentValues_CommandLine(&cl, identity_CommandLineOption, 1))) !=
        NULL) {
        ident = findIdentityFuzzy_GmCerts(certs, value_CommandLineArg(arg, 0));
        if (!ident) {
            printError_("identity not found: %s", cstr_String(value_CommandLineArg(arg, 0)));
        }
    }
    d.mutex     = new_Mutex();
    d.finished  = new_Condition();
    d.remaining = size_StringList(urls);
    iConstForEach(StringList, j, urls) {
        iGmRequest *req = iClob(new_GmRequest(certs));
        iString *url = collect_String(copy_String(j.value));
        if (indexOfCStr_String(url, "://") == iInvalidPos) {
            prependCStr_String(url, "gemini://");
        }
        setUrl_GmRequest(req, url);
        setIdentity_GmRequest(req, ident);
        enableFilters_GmRequest(req, iFalse);
        setUserData_Object(req, &d);
        iConnect(GmRequest, req, finished, req, requestFinished_);
        submit_GmRequest(req);
    }
    /* Block until all requests have finished. */
    iGuardMutex(d.mutex, {
        if (d.remaining > 0) {
            wait_Condition(d.finished, d.mutex);
        }
    });
    delete_Condition(d.finished);
    delete_Mutex(d.mutex);
    delete_GmCerts(certs);
    delete_Prefs(prefs);
    deinit_SiteSpec();
    deinit_Core();
    deinit_CommandLine(&cl);
    deinit_Foundation();
    return 0;
}
