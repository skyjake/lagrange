/* Persists the app's /data directory in the browser's origin-private file system (OPFS).

   The app itself uses an in-memory file system. Files are copied from OPFS before the app
   starts, and changed files are written back asynchronously every few seconds and after
   the app has saved its data. Only asynchronous OPFS APIs are used, because in some browsers (WebKit) the
   synchronous ones cannot complete while the main thread waits for them. */

const LagrangeStorage = (() => {
    const dataPath = '/data';
    const autoSyncInterval = 2000; /* ms; the app writes some files outside of saving */
    const synced = new Map(); /* path -> size and mtime when last written to OPFS */
    let root = null;
    let loaded = [];
    let syncing = null;
    let isPending = false;
    let autoSync = null;

    function isDirMode(mode) {
        return (mode & 0o170000) === 0o040000;
    }

    function stateKey(stat) {
        return stat.size + ':' + Number(stat.mtime);
    }

    function baseName(path) {
        return path.substring(path.lastIndexOf('/') + 1);
    }

    async function readTree(dir, path) {
        for await (const [name, handle] of dir.entries()) {
            const childPath = path + '/' + name;
            if (handle.kind === 'directory') {
                loaded.push({ path: childPath });
                await readTree(handle, childPath);
            }
            else {
                const file = await handle.getFile();
                loaded.push({ path: childPath, data: new Uint8Array(await file.arrayBuffer()) });
            }
        }
    }

    /* Reads the stored files into memory. Called before the runtime starts. */
    async function load() {
        try {
            root = await navigator.storage.getDirectory();
            await readTree(root, dataPath);
        }
        catch (error) {
            console.warn('Persistent storage is not available:', error);
            root = null;
            loaded = [];
        }
    }

    /* Writes the loaded files into the app's file system and starts syncing changes.
       Called before main(). */
    function install(FS) {
        FS.mkdirTree(dataPath);
        for (const entry of loaded) {
            if (entry.data) {
                FS.writeFile(entry.path, entry.data);
                synced.set(entry.path, stateKey(FS.stat(entry.path)));
            }
            else {
                FS.mkdirTree(entry.path);
            }
        }
        loaded = [];
        if (root) {
            autoSync = setInterval(() => sync(FS), autoSyncInterval);
        }
    }

    function listFiles(FS, path, files) {
        for (const name of FS.readdir(path)) {
            if (name === '.' || name === '..') {
                continue;
            }
            const childPath = path + '/' + name;
            const stat = FS.stat(childPath);
            if (isDirMode(stat.mode)) {
                listFiles(FS, childPath, files);
            }
            else {
                files.set(childPath, stat);
            }
        }
        return files;
    }

    async function parentHandle(path, create) {
        let dir = root;
        /* Skip the leading empty component, "data", and the file name. */
        for (const part of path.split('/').slice(2, -1)) {
            dir = await dir.getDirectoryHandle(part, { create });
        }
        return dir;
    }

    async function syncOnce(FS) {
        const files = listFiles(FS, dataPath, new Map());
        for (const [path, stat] of files) {
            const key = stateKey(stat);
            if (synced.get(path) === key) {
                continue;
            }
            const data = FS.readFile(path);
            const dir = await parentHandle(path, true);
            const handle = await dir.getFileHandle(baseName(path), { create: true });
            const writable = await handle.createWritable();
            await writable.write(data);
            await writable.close();
            synced.set(path, key);
        }
        for (const path of [...synced.keys()]) {
            if (!files.has(path)) {
                try {
                    const dir = await parentHandle(path, false);
                    await dir.removeEntry(baseName(path));
                }
                catch (error) {
                    /* Already gone. */
                }
                synced.delete(path);
            }
        }
    }

    /* Writes changed files to OPFS. Returns a promise that resolves when done. */
    function sync(FS) {
        if (!root) {
            return Promise.resolve();
        }
        if (syncing) {
            isPending = true;
            return syncing;
        }
        syncing = (async () => {
            do {
                isPending = false;
                try {
                    await syncOnce(FS);
                }
                catch (error) {
                    console.error('Failed to write persistent storage:', error);
                }
            } while (isPending);
            syncing = null;
        })();
        return syncing;
    }

    /* No more writes after another browser tab has taken over. */
    function stop() {
        clearInterval(autoSync);
        root = null;
    }

    return {
        load,
        install,
        sync,
        stop,
        isPersistent: () => root !== null,
    };
})();
