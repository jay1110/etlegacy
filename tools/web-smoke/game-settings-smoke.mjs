#!/usr/bin/env node

// Regression coverage for the expensive in-game Apply path. Cached maps must
// not write IndexedDB at all; multiple new maps must be committed once, not
// once per archive. The button wiring is checked as part of the same contract.

import assert from 'node:assert/strict';
import fs from 'node:fs';
import vm from 'node:vm';

const input = process.argv[2];
if (!input) {
    console.error('Usage: node game-settings-smoke.mjs <etl.html|shell.html>');
    process.exit(2);
}

const source = fs.readFileSync(input, 'utf8');

function between(startText, endText) {
    const start = source.indexOf(startText);
    const end = source.indexOf(endText, start + startText.length);
    assert.notEqual(start, -1, `missing ${startText}`);
    assert.notEqual(end, -1, `missing marker after ${startText}`);
    return source.slice(start, end);
}

const ensureSource = between(
    'function ensureMapInstalled(map, opts)',
    'function statusFor(name, received, total)'
);
const downloadSource = between(
    'function downloadInto(sourceUrl, target, label, opts)',
    '// Fetch a large installer'
);
const installSource = between(
    'function installRotation(rotation, onProgress)',
    '// Read + clamp the "Host game" form'
);

const files = new Set();
const maps = {
    cached_a: 'https://maps.invalid/cached_a.pk3',
    cached_b: 'https://maps.invalid/cached_b.pk3',
    fresh_a: 'https://maps.invalid/fresh_a.pk3',
    fresh_b: 'https://maps.invalid/fresh_b.pk3'
};
let fetches = 0;
let syncs = 0;
let lastFetchUrl = '';
let lastFetchOptions = null;

const context = vm.createContext({
    console,
    Promise,
    Uint8Array,
    URL,
    window: { location: { href: 'https://example.invalid/etl/' } },
    ETMAIN_DIR: '/etmain',
    Module: {
        setStatus() {},
        FS: {
            analyzePath(path) { return { exists: files.has(path) }; },
            writeFile(path) { files.add(path); }
        }
    },
    loadMapList: () => Promise.resolve(maps),
    downloadFetchUrl: value => value,
    fetch: (url, options) => {
        fetches += 1;
        lastFetchUrl = url;
        lastFetchOptions = options;
        return Promise.resolve({
            ok: true,
            headers: { get: () => '4' },
            body: null,
            arrayBuffer: () => Promise.resolve(new ArrayBuffer(4))
        });
    },
    persist: () => {
        syncs += 1;
        return Promise.resolve();
    }
});

vm.runInContext(`
function pk3NameForUrl(url) { return new URL(url).pathname.split('/').pop(); }
function statusFor() {}
${downloadSource}
${ensureSource}
${installSource}
`, context, { filename: input });

files.add('/etmain/cached_a.pk3');
files.add('/etmain/cached_b.pk3');
await context.installRotation(['cached_a', 'cached_b']);
assert.equal(fetches, 0, 'cached maps were downloaded again');
assert.equal(syncs, 0, 'cached maps triggered an IndexedDB sync');

await context.installRotation(['fresh_a', 'fresh_b']);
assert.equal(fetches, 2, 'new maps were not downloaded exactly once');
assert.equal(syncs, 1, 'a rotation must use one IndexedDB sync');

files.add('/etmain/omni-bot-data.zip');
await context.downloadInto('https://example.invalid/omni-bot-data.zip',
    '/etmain/omni-bot-data.zip', 'Omni-bot data',
    { refresh: true, cache: 'no-store', cacheBust: true });
assert.match(lastFetchUrl, /[?&]etl_refresh=\d+$/,
    'updated same-name archive did not bypass intermediary caches');
assert.equal(lastFetchOptions.cache, 'no-store',
    'updated same-name archive reused the browser HTTP cache');

assert.match(source, /id="game-apply">Apply settings<\/button>/);
assert.match(source, /id="game-changemap">Apply &amp; play map<\/button>/);
assert.match(source, /applyGameSettings\(\{ playMap: map \}\)/,
    'Play selected map bypasses the settings apply path');
assert.doesNotMatch(installSource, /showLoading\(/,
    'rotation preparation must not cover a running game with the loading screen');

console.log('PASS cached rotation: 0 downloads, 0 IndexedDB syncs');
console.log('PASS two new maps: 2 downloads, 1 IndexedDB sync');
console.log('PASS Apply and Apply & play use one settings pipeline');
console.log('PASS same-name asset refresh bypasses browser and proxy caches');
