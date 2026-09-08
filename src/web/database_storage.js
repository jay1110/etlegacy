/* ET: Legacy browser database image transactions. GPL-3.0-or-later.
 * Deliberately separate from IDBFS: a stale tab's syncfs must never replace
 * another tab's committed database. This API is ASYNCHRONOUS. A request token
 * is not success; only poll(token).status === "committed" confirms commit.
 */
(function (root) {
    'use strict';
    var MAX_IMAGE = 64 * 1024 * 1024;
    var MAX_REQUESTS = 8;
    var MAX_REVISION = 0x7fffffff;
    var STORE = 'images';

    function validKey(key) {
        return typeof key === 'string' && key.length > 1 && key.length < 1024 &&
            key[0] === '/' && !/[\\\x00-\x1f]/.test(key) &&
            !key.split('/').some(function (p) { return p === '.' || p === '..'; });
    }
    function validImage(image) {
        if (!(image instanceof Uint8Array) || image.length < 100 || image.length > MAX_IMAGE) return false;
        // Integrity/schema validation remains with the SQLite-owning mod.
        var signature = 'SQLite format 3\x00';
        for (var i = 0; i < signature.length; i++) if (image[i] !== signature.charCodeAt(i)) return false;
        return true;
    }
    function validRecord(record) {
        return record === undefined || (record && Number.isInteger(record.revision) && record.revision > 0 &&
            record.revision <= MAX_REVISION && validImage(record.image));
    }
    function DatabaseStorage(options) {
        options = options || {};
        this.name = options.name || 'etlegacy-database-images-v1';
        this.indexedDB = options.indexedDB || root.indexedDB;
        this.db = null;
        this.opening = null;
        this.requests = new Map();
        this.nextToken = 1;
    }
    DatabaseStorage.prototype.open = function () {
        var self = this;
        if (self.db) return Promise.resolve(self.db);
        if (self.opening) return self.opening;
        self.opening = new Promise(function (resolve, reject) {
            if (!self.indexedDB) { reject(new Error('IndexedDB unavailable')); return; }
            var request = self.indexedDB.open(self.name, 1);
            request.onupgradeneeded = function () {
                if (!request.result.objectStoreNames.contains(STORE)) request.result.createObjectStore(STORE);
            };
            request.onerror = function () { reject(request.error || new Error('Database open failed')); };
            request.onsuccess = function () {
                self.db = request.result;
                self.db.onversionchange = function () { self.db.close(); self.db = null; };
                resolve(self.db);
            };
        }).then(function (db) { self.opening = null; return db; }, function (error) {
            self.opening = null; throw error;
        });
        return self.opening;
    };
    DatabaseStorage.prototype.request = function (operation, key, revision, image) {
        var self = this;
        if ((operation !== 'read' && operation !== 'cas') || !validKey(key) ||
            self.requests.size >= MAX_REQUESTS || self.nextToken > MAX_REVISION) return 0;
        if (operation === 'cas' && (!Number.isInteger(revision) || revision < 0 ||
            revision >= MAX_REVISION || !validImage(image))) return 0;
        var token = self.nextToken++;
        var result = { status: 'pending', revision: 0, image: null, error: '', transaction: null, cancelled: false };
        self.requests.set(token, result);
        // Never retain a view into growable wasm memory or caller-owned input.
        var input = operation === 'cas' ? image.slice() : null;
        self.open().then(function (db) {
            if (result.cancelled) return;
            var tx, outcome, current;
            try {
                tx = operation === 'cas' ? db.transaction(STORE, 'readwrite', { durability: 'strict' }) :
                    db.transaction(STORE, 'readonly');
                result.transaction = tx;
                tx.onabort = function () {
                    result.transaction = null;
                    result.status = result.cancelled ? 'cancelled' : 'error';
                    if (!result.error) result.error = tx.error ? tx.error.name + ': ' + tx.error.message : 'Transaction aborted';
                    if (result.releaseWhenDone) self.requests.delete(token);
                };
                tx.onerror = function () { /* onabort owns the final result */ };
                tx.oncomplete = function () {
                    result.transaction = null;
                    result.status = outcome;
                    result.revision = current ? current.revision : 0;
                    result.image = current ? current.image : null;
                    if (result.releaseWhenDone) self.requests.delete(token);
                };
                // Unsupported strict durability is an explicit error, not a
                // silent downgrade to acknowledgement before durable commit.
                if (operation === 'cas' && tx.durability !== 'strict') {
                    result.error = 'Strict IndexedDB durability unavailable'; tx.abort(); return;
                }
                var store = tx.objectStore(STORE);
                var read = store.get(key);
                read.onsuccess = function () {
                    current = read.result;
                    if (!validRecord(current)) { result.error = 'Invalid stored database record'; tx.abort(); return; }
                    if (operation === 'read') { outcome = 'read'; return; }
                    if ((current ? current.revision : 0) !== revision) { outcome = 'conflict'; return; }
                    current = { revision: revision + 1, image: input };
                    store.put(current, key);
                    // A successful put request is still not a committed tx.
                    outcome = 'committed';
                };
            } catch (error) {
                result.error = error.name + ': ' + error.message;
                if (tx) { try { tx.abort(); } catch (_) { result.status = 'error'; } }
                else result.status = 'error';
            }
        }).catch(function (error) {
            if (!result.cancelled) { result.status = 'error'; result.error = error.name + ': ' + error.message; }
            if (result.releaseWhenDone) self.requests.delete(token);
        });
        return token;
    };
    DatabaseStorage.prototype.poll = function (token) {
        var result = this.requests.get(token);
        return result ? { status: result.status, revision: result.revision,
            length: result.image ? result.image.length : 0, error: result.error } : null;
    };
    DatabaseStorage.prototype.copy = function (token, target) {
        var result = this.requests.get(token);
        if (!result || result.status === 'pending' || !(target instanceof Uint8Array) ||
            !result.image || target.length < result.image.length) return -1;
        target.set(result.image); return result.image.length;
    };
    DatabaseStorage.prototype.release = function (token) {
        var result = this.requests.get(token);
        if (!result || result.status === 'pending') return false;
        return this.requests.delete(token);
    };
    DatabaseStorage.prototype.cancel = function (token) {
        var result = this.requests.get(token);
        if (!result || result.status !== 'pending') return false;
        result.cancelled = true;
        if (result.transaction) {
            try { result.transaction.abort(); }
            catch (_) { result.cancelled = false; return false; }
        } else result.status = 'cancelled';
        return true;
    };
    DatabaseStorage.prototype.reset = function () {
        var self = this;
        // No callback writes to wasm memory. Outstanding requests own their
        // byte arrays, and final VM unload only abandons these result handles.
        self.requests.forEach(function (result, token) {
            if (result.status === 'pending') {
                result.releaseWhenDone = true;
                self.cancel(token);
            }
            if (result.status !== 'pending') self.requests.delete(token);
        });
        // A transaction that had already committed stays committed. Never
        // erase durable data or reuse a token to simulate successful rollback.
    };
    DatabaseStorage.prototype.close = function () {
        // Closing a connection must not discard an unknown commit outcome.
        if (this.opening || Array.from(this.requests.values()).some(function (r) { return r.status === 'pending'; })) return false;
        if (this.db) this.db.close();
        this.db = null; return true;
    };
    root.ETLDatabaseStorage = DatabaseStorage;
    if (typeof Module !== 'undefined') Module['etlDatabaseStorage'] = new DatabaseStorage();
})(globalThis);
