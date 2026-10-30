/* Page setup of the Lagrange web app: the Emscripten module and single-instance handling. */

/* Only one browser tab runs Lagrange at a time, because all tabs share the same
   storage. The running tab holds a Web Lock for its lifetime. */
const lockName = 'lagrange-app';
const channel = new BroadcastChannel('lagrange-app');
const params = new URLSearchParams(location.search);
const wasMoved = params.has('moved');
const isSoftwareRender = params.has('sw');
const dataSizeMeta = document.querySelector('meta[name="lagrange-data-size"]');
const dataSize = dataSizeMeta ? Number(dataSizeMeta.content) : 0;

/* UI variants of the app. The desktop variant is at the site root. On mobile devices,
   a variant opened directly or chosen in the app is remembered; otherwise the root page
   picks one based on the device. Desktop devices always get the desktop variant. */
const variantPaths = { desktop: '', tablet: 'tablet/', phone: 'phone/' };
const variantMeta = document.querySelector('meta[name="lagrange-variant"]');
const currentVariant = variantMeta ? variantMeta.content : 'desktop';
const variantKey = 'lagrange-variant';

function variantUrl(name) {
    const root = currentVariant === 'desktop' ? '' : '../';
    return new URL(root + variantPaths[name], location.href);
}

function rememberVariant(name) {
    try {
        localStorage.setItem(variantKey, name);
    }
    catch (error) {}
}

function rememberedVariant() {
    try {
        return localStorage.getItem(variantKey);
    }
    catch (error) {
        return null;
    }
}

function deviceVariant() {
    const ua = navigator.userAgent;
    if ((navigator.userAgentData && navigator.userAgentData.mobile) || /iPhone|iPod/.test(ua)) {
        return 'phone';
    }
    /* iPadOS Safari identifies itself as macOS. */
    if (/iPad/.test(ua) || (/Macintosh/.test(ua) && navigator.maxTouchPoints > 1)) {
        return 'tablet';
    }
    if (/Android/.test(ua)) {
        return /Mobile/.test(ua) ? 'phone' : 'tablet';
    }
    return 'desktop';
}

function variantRedirect() {
    if (currentVariant !== 'desktop') {
        if (!params.has('auto')) {
            rememberVariant(currentVariant);
        }
        return null;
    }
    const device = deviceVariant();
    const target = device === 'desktop' ? device : rememberedVariant() || device;
    if (target === currentVariant || !variantPaths.hasOwnProperty(target)) {
        return null;
    }
    const url = variantUrl(target);
    url.search = location.search;
    url.searchParams.set('auto', '1');
    return url;
}

const redirectUrl = variantRedirect();

let startUrl = params.get('url');
if (startUrl && startUrl.startsWith('web+')) {
    startUrl = startUrl.substring(4); /* from a web+gemini: protocol handler */
}
if (location.search && !redirectUrl) {
    /* Reloading won't repeat these. */
    history.replaceState(history.state, '', location.pathname);
}
const statusElement = document.getElementById('status');
const canvasElement = document.getElementById('canvas');
canvasElement.addEventListener('contextmenu', (event) => event.preventDefault());
/* SDL cannot recreate its WebGL resources, so the app is restarted if the context is lost.
   If this keeps happening, the app switches to software rendering. */
canvasElement.addEventListener('webglcontextlost', (event) => {
    event.preventDefault();
    const lostKey = 'lagrange-webgl-lost';
    let wasLostRecently = false;
    try {
        wasLostRecently = Date.now() - Number(sessionStorage.getItem(lostKey) || 0) < 60000;
        sessionStorage.setItem(lostKey, String(Date.now()));
    }
    catch (error) {}
    console.warn('WebGL context lost; restarting');
    stopApp(true, location.pathname + (wasLostRecently ? '?sw' : ''));
});
/* Clicks do not move the focus, so a focused text field keeps the on-screen keyboard. */
canvasElement.addEventListener('mousedown', (event) => event.preventDefault());
let isStarted = false;
let isStopped = false;
let isRuntimeReady = false;

function showStatus(text, buttonLabel, action) {
    statusElement.replaceChildren(text);
    if (buttonLabel) {
        const button = document.createElement('button');
        button.textContent = buttonLabel;
        button.onclick = action;
        statusElement.appendChild(button);
    }
    statusElement.style.display = 'flex';
}

var Module = {
    canvas: canvasElement,
    arguments: (isSoftwareRender ? ['--sw'] : []).concat(startUrl ? [startUrl] : []),
    print: (text) => console.log(text),
    printErr: (text) => console.error(text),
    setStatus: (text) => {
        if (!text) {
            return;
        }
        /* The data file may be compressed in transfer, so the loader's total (from
           Content-Length) cannot be used for progress. */
        const progress = text.match(/\((\d+)\/\d+\)/);
        if (progress && dataSize > 0) {
            const percent = Math.min(100, Math.floor(100 * Number(progress[1]) / dataSize));
            showStatus('Loading Lagrange... ' + percent + '%');
        }
        else {
            showStatus('Loading Lagrange...');
        }
    },
    preRun: [() => {
        Module.addRunDependency('storage');
        LagrangeStorage.load().then(() => Module.removeRunDependency('storage'));
    }],
    onRuntimeInitialized: () => {
        LagrangeStorage.install(Module.FS);
        statusElement.style.display = 'none';
        isRuntimeReady = true;
    },
    syncStorage: () => LagrangeStorage.sync(Module.FS),
    switchVariant: (name) => {
        rememberVariant(name);
        setTimeout(() => stopApp(true, variantUrl(name)), 0);
    },
    isStoragePersistent: () => LagrangeStorage.isPersistent(),
    onAbort: (what) => {
        /* SDL hides the page's other elements while the canvas fills the page. */
        const error = document.createElement('div');
        error.className = 'error';
        error.textContent = 'Lagrange has stopped: ' + what;
        document.body.appendChild(error);
    },
};

function startApp() {
    if (isStarted) {
        return;
    }
    isStarted = true;
    showStatus('Loading Lagrange...');
    if (navigator.storage && navigator.storage.persist) {
        navigator.storage.persist().then((granted) => {
            console.log('Persistent storage ' + (granted ? 'granted' : 'not granted'));
        });
    }
    const script = document.createElement('script');
    script.src = 'lagrange.js';
    document.body.appendChild(script);
}

async function stopApp(save, nextUrl) {
    if (!isStarted || isStopped) {
        return;
    }
    isStopped = true;
    if (Module.ccall) {
        Module.ccall('stop_Web', null, ['number'], [save ? 1 : 0]);
        if (save) {
            await LagrangeStorage.sync(Module.FS);
        }
    }
    LagrangeStorage.stop();
    /* Unloading the page releases the lock. */
    location.replace(nextUrl || location.pathname + '?moved');
}

function requestLock(options, onGranted) {
    navigator.locks.request(lockName, options, (lock) => {
        if (!lock) {
            showOtherTab();
            return;
        }
        if (onGranted) {
            onGranted();
        }
        startApp();
        return new Promise(() => {}); /* held until the page is unloaded */
    }).catch(() => {
        stopApp(false); /* another tab took the lock by force */
    });
}

function takeOver() {
    showStatus('Waiting for the other tab to close Lagrange...');
    channel.postMessage({ type: 'takeover' });
    /* The other tab may be frozen and unable to respond. */
    const timer = setTimeout(() => requestLock({ steal: true }), 5000);
    requestLock({}, () => clearTimeout(timer));
}

function showOtherTab() {
    if (startUrl) {
        channel.postMessage({ type: 'open', url: startUrl });
        showStatus('The link was opened in the browser tab where Lagrange is running.',
                   'Use Lagrange Here', takeOver);
    }
    else {
        showStatus('Lagrange is open in another browser tab.', 'Use Lagrange Here', takeOver);
    }
}

/* The browser's Back and Forward buttons navigate inside the app instead of leaving the
   page. The page's history entry sits between two extra entries. Going to either of them
   is passed on to the app as a key press, and the browser is returned to the middle entry.
   Browsers may skip history entries that were added without the user interacting with the
   page, so the entries are added during the first input event. */
const historyKey = 'lagrange';
let isHistoryTrapped = false;

function historyPosition() {
    return history.state ? history.state[historyKey] : undefined;
}

function trapHistory() {
    if (isHistoryTrapped || !isStarted || isStopped) {
        return;
    }
    isHistoryTrapped = true;
    /* After a reload, the entry behind this one still exists. */
    if (historyPosition() !== 'main') {
        history.replaceState({ [historyKey]: 'back' }, '');
        history.pushState({ [historyKey]: 'main' }, '');
    }
    history.pushState({ [historyKey]: 'forward' }, '');
    history.back();
}

window.addEventListener('popstate', () => {
    const position = historyPosition();
    if (!isHistoryTrapped || (position !== 'back' && position !== 'forward')) {
        return;
    }
    const dir = position === 'back' ? -1 : 1;
    history.go(-dir);
    if (!isStopped && isRuntimeReady && Date.now() - sideButtonTime > sideButtonDelay) {
        Module.ccall('navigate_Web', null, ['number'], [dir]);
    }
});

for (const type of ['pointerdown', 'keydown']) {
    window.addEventListener(type, trapHistory, true);
}

/* The app gets the mouse's Back and Forward buttons as mouse events and handles them
   itself. Not all browsers allow preventing their own navigation, so it is also ignored
   when it follows these buttons. */
const sideButtonDelay = 500; /* ms */
let sideButtonTime = 0;

for (const type of ['pointerdown', 'pointerup', 'mousedown', 'mouseup', 'auxclick']) {
    window.addEventListener(type, (event) => {
        if (event.button === 3 || event.button === 4) {
            sideButtonTime = Date.now();
            event.preventDefault();
        }
    }, true);
}

/* The page was restored from the browser's back/forward cache. The lock was released
   when the page was put in the cache, and the canvas has lost its contents. */
window.addEventListener('pageshow', (event) => {
    if (event.persisted) {
        location.reload();
    }
});

channel.onmessage = (event) => {
    if (!isStarted || isStopped) {
        return;
    }
    if (event.data.type === 'takeover') {
        stopApp(true);
    }
    else if (event.data.type === 'open' && Module.ccall) {
        Module.ccall('openUrl_Web', null, ['string'], [event.data.url]);
    }
};

if (redirectUrl) {
    location.replace(redirectUrl);
}
else if (!self.crossOriginIsolated) {
    showStatus('Lagrange requires a cross-origin isolated page (COOP/COEP headers).');
}
else if (!navigator.locks) {
    showStatus('This browser is not supported (Web Locks API missing).');
}
else if (wasMoved) {
    showStatus('Lagrange was moved to another browser tab.', 'Use Lagrange Here', takeOver);
}
else {
    requestLock({ ifAvailable: true });
}
