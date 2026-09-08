#!/usr/bin/env node
// Storage-only acceptance: no wasm engine, retail assets, or live game.
// Uses the production database_storage.js with real browser IndexedDB.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import http from 'node:http';
import path from 'node:path';
import { createRequire } from 'node:module';
const { chromium } = createRequire(import.meta.url)('playwright');
const source = fs.readFileSync(new URL('../../src/web/database_storage.js', import.meta.url));
const glue = process.argv[2] ? path.resolve(process.argv[2]) : null;
const server = http.createServer((req, res) => {
    if (req.url === '/database_storage.js') { res.setHeader('Content-Type', 'text/javascript'); res.end(source); }
    else if (glue && (req.url === '/db-c.js' || req.url === '/db-c.wasm')) {
        res.setHeader('Content-Type', req.url.endsWith('.wasm') ? 'application/wasm' : 'text/javascript');
        res.end(fs.readFileSync(path.join(glue, req.url.slice(1))));
    }
    else if (req.url === '/') { res.setHeader('Content-Type', 'text/html'); res.end('<!doctype html><script src="/database_storage.js"></script>'); }
    else { res.statusCode = 404; res.end(); }
});
await new Promise(resolve => server.listen(0, '127.0.0.1', resolve));
let browser;
let checks = 0;
function check(actual, expected, label) { assert.deepEqual(actual, expected, label); checks++; }
const base = 'http://127.0.0.1:' + server.address().port;
const key = '/home/web_user/.etlegacy/nitmod/NITMOD_DB.sqlite';
async function setup(page) {
    await page.goto(base);
    await page.evaluate(async () => {
        window.store = new ETLDatabaseStorage({ name: 'etl-storage-acceptance' });
        window.sample = marker => {
            // Opaque database transport payload; SQL correctness is checked by
            // the mod's SQLite fixtures, not by this storage transport test.
            const data = new Uint8Array(128);
            for (const [i, char] of Array.from('SQLite format 3\0').entries()) data[i] = char.charCodeAt(0);
            data[100] = marker; return data;
        };
        await store.open();
    });
}
async function request(page, operation, revision, marker, selectedKey = key) {
    return page.evaluate(({ operation, revision, marker, key }) =>
        store.request(operation, key, revision, marker === undefined ? undefined : sample(marker)),
        { operation, revision, marker, key: selectedKey });
}
async function result(page, token) {
    await page.waitForFunction(token => store.poll(token)?.status !== 'pending', token, { timeout: 10000 });
    return page.evaluate(token => {
        const info = store.poll(token), data = new Uint8Array(info.length);
        const copied = store.copy(token, data); const released = store.release(token);
        return { ...info, marker: data[100] || 0, copied, released };
    }, token);
}
async function hold(page) {
    await page.evaluate(() => {
        window.keepLock = true;
        window.holder = store.db.transaction('images', 'readwrite');
        const images = holder.objectStore('images');
        function pump() { images.get('__test-lock__').onsuccess = () => { if (keepLock) pump(); }; }
        pump();
    });
}
async function unlock(page) { await page.evaluate(() => { keepLock = false; }); }
try {
    browser = await chromium.launch({ headless: true,
        ...(process.env.ETL_CHROMIUM_EXECUTABLE ? { executablePath: process.env.ETL_CHROMIUM_EXECUTABLE } : {}) });
    const context = await browser.newContext();
    const first = await context.newPage(), second = await context.newPage();
    await Promise.all([setup(first), setup(second)]);
    check(await first.evaluate(() => crossOriginIsolated), false, 'No COOP/COEP requirement');
    let read = await result(first, await request(first, 'read', 0));
    check([read.status, read.revision, read.length], ['read', 0, 0], 'Missing key read');
    check(await request(first, 'cas', 0, 1, '/home/../escape.sqlite'), 0, 'Traversal rejected');
    check(await first.evaluate(() => store.request('cas', '/valid.sqlite', 0, new Uint8Array(128))), 0, 'Invalid payload rejected');
    check(await request(first, 'cas', -1, 1), 0, 'Negative revision rejected');
    check(await request(first, 'cas', 0x7fffffff, 1), 0, 'Revision overflow rejected');

    await hold(first);
    const ids = await Promise.all([request(first, 'cas', 0, 11), request(second, 'cas', 0, 22)]);
    check(await first.evaluate(id => store.poll(id).status, ids[0]), 'pending', 'First queued request is not success');
    check(await second.evaluate(id => store.poll(id).status, ids[1]), 'pending', 'Second queued request is not success');
    check(await first.evaluate(id => store.release(id), ids[0]), false, 'Unknown outcome cannot be released');
    await unlock(first);
    const race = await Promise.all([result(first, ids[0]), result(second, ids[1])]);
    check(race.map(r => r.status).sort(), ['committed', 'conflict'], 'Exactly one concurrent compare-and-swap wins');
    check(race.map(r => r.revision), [1, 1], 'Both observe the same committed revision');
    check(race[0].marker, race[1].marker, 'Conflict carries current winner image');
    check(race.every(r => r.copied === 128 && r.released), true, 'Both images delivered and released');
    const stale = await result(second, await request(second, 'cas', 0, 99));
    check([stale.status, stale.revision, stale.marker], ['conflict', 1, race[0].marker], 'Stale tab never overwrites winner');

    const updated = await result(second, await request(second, 'cas', 1, 33));
    check([updated.status, updated.revision, updated.marker], ['committed', 2, 33], 'Retry against fresh revision commits');
    read = await result(first, await request(first, 'read', 0));
    check([read.revision, read.marker], [2, 33], 'Other browser instance reads committed update');

    await hold(first);
    const abortId = await request(second, 'cas', 2, 44);
    await second.waitForFunction(id => store.requests.get(id).transaction !== null, abortId);
    check(await second.evaluate(id => store.cancel(id), abortId), true, 'Abort active queued transaction');
    const aborted = await result(second, abortId);
    check(aborted.status, 'cancelled', 'Aborted transaction never reports committed');
    await unlock(first);
    read = await result(first, await request(first, 'read', 0));
    check([read.revision, read.marker], [2, 33], 'Abort leaves persisted image unchanged');

    await hold(first);
    const copiedId = await second.evaluate(key => {
        const input = sample(55), token = store.request('cas', key, 2, input);
        input[100] = 66; return token;
    }, key);
    await unlock(first);
    check((await result(second, copiedId)).marker, 55, 'Request owns input before asynchronous execution');

    // A completely fresh page/connection must retrieve persistence, not a
    // JavaScript singleton or another tab's MEMFS snapshot.
    await setup(second);
    read = await result(second, await request(second, 'read', 0));
    check([read.status, read.revision, read.marker], ['read', 3, 55], 'Reload sees durable committed revision');

    const batch = await first.evaluate(key => {
        const ids = [];
        for (let i = 0; i < 9; i++) ids.push(store.request('read', key));
        return ids;
    }, key);
    check(batch.slice(0, 8).every(Boolean) && batch[8] === 0, true, 'Outstanding request bound');
    for (const id of batch.slice(0, 8)) await result(first, id);
    check(await first.evaluate(() => store.close()), true, 'Completed connection closes cleanly');
    check(await first.evaluate(() => store.poll(999999)), null, 'Unknown token never reports success');
    let cChecks = 0;
    if (glue) {
        await first.addScriptTag({ url: base + '/db-c.js' });
        await first.evaluate(async () => {
            window.c = await DBFixture();
            c.etlDatabaseStorage = new ETLDatabaseStorage({ name: 'etl-storage-c-acceptance' });
            window.infoPtr = c._malloc(8), window.errorPtr = c._malloc(256);
            window.cPoll = id => c._Sys_WebDatabasePoll(id, infoPtr, infoPtr + 4, errorPtr, 256);
        });
        cChecks = await first.evaluate(() => c._DBFixtureKeyChecks());
        check(cChecks, 14, 'Actual FS key functions, relative path validation, homepath alias and game namespace');
        const cRead = await first.evaluate(() => c.ccall('Sys_WebDatabaseRequest', 'number',
            ['number', 'string', 'number', 'number', 'number'], [1, 'NITMOD_DB.sqlite', 0, 0, 0]));
        await first.waitForFunction(id => cPoll(id) !== 0, cRead);
        check(await first.evaluate(id => [cPoll(id), c.getValue(infoPtr, 'i32'), c.getValue(infoPtr + 4, 'i32')], cRead),
            [1, 0, 0], 'Actual C read status and metadata');
        check(await first.evaluate(id => c._Sys_WebDatabaseRelease(id), cRead), 1, 'Actual C release');
        const cWrite = await first.evaluate(() => {
            const image = c._DBFixtureImage(77);
            const id = c.ccall('Sys_WebDatabaseRequest', 'number',
                ['number', 'string', 'number', 'number', 'number'], [2, 'NITMOD_DB.sqlite', 0, image, 128]);
            c._free(image);
            return { id, immediate: cPoll(id), releasePending: c._Sys_WebDatabaseRelease(id) };
        });
        check([cWrite.immediate, cWrite.releasePending], [0, 0], 'C request is pending; input can be freed after copy');
        await first.waitForFunction(id => cPoll(id) !== 0, cWrite.id);
        const cDone = await first.evaluate(id => {
            const status = cPoll(id), data = c._malloc(128);
            const small = c._Sys_WebDatabaseCopy(id, data, 1);
            const copied = c._Sys_WebDatabaseCopy(id, data, 128);
            const result = [status, c.getValue(infoPtr, 'i32'), c.getValue(infoPtr + 4, 'i32'), small, copied, c.getValue(data + 100, 'i8')];
            c._free(data); c._Sys_WebDatabaseRelease(id); return result;
        }, cWrite.id);
        check(cDone, [2, 1, 128, -1, 128, 77], 'Actual C durable completion and bounded copy');
        const reader = await second.evaluate(async () => {
            store = new ETLDatabaseStorage({ name: 'etl-storage-c-acceptance' });
            await store.open();
            return store.request('read', '/home/web_user/.etlegacy/nitmod/NITMOD_DB.sqlite');
        });
        const cOther = await result(second, reader);
        check([cOther.revision, cOther.marker], [1, 77], 'Separate page reads image committed through C engine API');
        check(await first.evaluate(() => c._Sys_WebDatabasePoll(999999, infoPtr, infoPtr + 4, errorPtr, 256)), -3,
            'Actual C invalid token is explicit failure');
        await first.evaluate(() => { c._free(infoPtr); c._free(errorPtr); });
    }
    console.log(JSON.stringify({ status: 'PASS', checks, cKeyChecks: cChecks, browser: browser.version(),
        scope: 'production host storage, real IndexedDB, two same-origin pages, no game',
        integration: 'asynchronous host service; synchronous mod continuations not yet connected' }, null, 2));
} finally {
    if (browser) await browser.close();
    await new Promise(resolve => server.close(resolve));
}
