#!/usr/bin/env node
/**
 * Structural smoke test for the packaged ET: Legacy web build.
 *
 * This does NOT need a browser or the (non-redistributable) retail paks. It
 * deterministically checks that the packaged output the CI produces is
 * well-formed, so packaging regressions (a missing engine file, a truncated
 * wasm, a mod pk3 that lacks the cgame/ui side modules) are caught before the
 * build is published to GitHub Pages / attached to a release.
 *
 * Usage:
 *   node tools/web-smoke/verify-dist.mjs <dist/etlegacy-web>
 *
 * License: GPL-3.0 (same as ET: Legacy)
 */

import fs from 'node:fs';
import path from 'node:path';
import { execFileSync } from 'node:child_process';

const dir = process.argv[2];
if (!dir) {
    console.error('Usage: node verify-dist.mjs <web-dist-dir>');
    process.exit(2);
}

let failures = 0;
function check(ok, message) {
    console.log((ok ? 'PASS ' : 'FAIL ') + message);
    if (!ok) failures++;
}

function exists(rel) {
    return fs.existsSync(path.join(dir, rel));
}

// 1. Core engine files must be present. etl.data is the preloaded
//    virtual-filesystem image (browser default config) produced by the
//    --preload-file link option in cmake/ETLBuildClient.cmake.
for (const f of ['etl.html', 'index.html', 'etl.js', 'etl.wasm', 'etl.data']) {
    check(exists(f), `engine file present: ${f}`);
}

// 2. etl.wasm must start with the WebAssembly magic number "\0asm".
if (exists('etl.wasm')) {
    const fd = fs.openSync(path.join(dir, 'etl.wasm'), 'r');
    const magic = Buffer.alloc(4);
    fs.readSync(fd, magic, 0, 4, 0);
    fs.closeSync(fd);
    check(
        magic[0] === 0x00 && magic[1] === 0x61 && magic[2] === 0x73 && magic[3] === 0x6d,
        'etl.wasm has a valid WebAssembly header'
    );
}

// 3. The shell must no longer carry the unsubstituted emcc {{{ SCRIPT }}}
//    placeholder (etl.js must have been injected).
if (exists('etl.html')) {
    const html = fs.readFileSync(path.join(dir, 'etl.html'), 'utf8');
    check(!html.includes('{{{'), 'etl.html has the emcc script placeholder substituted');
}

// 4. The standalone cgame/ui/qagame side modules must be present in legacy/ AND
//    be valid WebAssembly. A frequent packaging/build regression is shipping an
//    `ar` static archive (starts with "!<arch>", 0x21 0x3C 0x61 0x72) instead
//    of a linked wasm side module (starts with "\0asm", 0x00 0x61 0x73 0x6D),
//    e.g. when CMake downgrades the MODULE library to a static library. The
//    engine's dlopen() then rejects it with "does not start with the
//    WebAssembly magic number", so validate the magic here, not just presence.
const WASM_MAGIC = Buffer.from([0x00, 0x61, 0x73, 0x6d]);

function firstBytes(buf, count = 4) {
    return Array.from(buf.subarray(0, count))
        .map((b) => b.toString(16).padStart(2, '0'))
        .join(' ');
}

function hasWasmMagic(buf) {
    return buf.length >= 4 && buf.subarray(0, 4).equals(WASM_MAGIC);
}

// Every game logic module of a build declares the VM ABI it speaks with this
// export (VM_WASM_ABI_SYMBOL in src/qcommon/q_shared.h), the version being part
// of the symbol name. It is how the launcher (src/web/shell.html) recognises a
// module of *another* version, whose differently shaped syscall pointer would
// trap the whole browser tab with "indirect call signature mismatch". A build
// that lost the export would silently give that detection up - the modules of
// this build would look like a third-party mod's, which are loaded as they are -
// so verify it here.
const VM_ABI_SYMBOL = 'vmWasmAbi1';

function exportsAbiMarker(buf) {
    try {
        const exports = WebAssembly.Module.exports(new WebAssembly.Module(buf));
        return exports.some((e) => e.name === VM_ABI_SYMBOL ||
            e.name === `_${VM_ABI_SYMBOL}`);
    } catch (e) {
        return false;
    }
}

const sideModules = ['cgame.mp.wasm32.so', 'ui.mp.wasm32.so', 'qagame.mp.wasm32.so'];
for (const so of sideModules) {
    const rel = path.join('legacy', so);
    const present = exists(rel);
    check(present, `side module present: legacy/${so}`);
    if (present) {
        const buf = fs.readFileSync(path.join(dir, rel));
        check(
            hasWasmMagic(buf),
            `side module legacy/${so} is valid WebAssembly (got ${firstBytes(buf)}, expected 00 61 73 6d)`
        );
        check(
            exportsAbiMarker(buf),
            `side module legacy/${so} exports ${VM_ABI_SYMBOL}`
        );
    }
}

// 5. The mod pk3 must exist and be a valid zip that contains the side modules,
//    and each embedded side module must itself be valid WebAssembly.
const legacyDir = path.join(dir, 'legacy');
let pk3 = null;
if (fs.existsSync(legacyDir)) {
    pk3 = fs.readdirSync(legacyDir).find((f) => /^legacy_.*\.pk3$/.test(f));
}
check(Boolean(pk3), 'mod pk3 (legacy_*.pk3) present in legacy/');
if (pk3) {
    const pk3Path = path.join(legacyDir, pk3);
    try {
        // `unzip -l` works everywhere on the CI image and avoids extra deps.
        const listing = execFileSync('unzip', ['-l', pk3Path], {
            encoding: 'utf8'
        });
        for (const so of sideModules) {
            const contained = listing.includes(so);
            check(contained, `mod pk3 contains ${so}`);
            if (contained) {
                // Extract the entry to stdout and verify its wasm magic.
                const bytes = execFileSync('unzip', ['-p', pk3Path, so], {
                    maxBuffer: 256 * 1024 * 1024
                });
                check(
                    hasWasmMagic(bytes),
                    `mod pk3 ${so} is valid WebAssembly (got ${firstBytes(bytes)}, expected 00 61 73 6d)`
                );
                check(
                    exportsAbiMarker(bytes),
                    `mod pk3 ${so} exports ${VM_ABI_SYMBOL}`
                );
            }
        }
    } catch (e) {
        check(false, `mod pk3 is a readable zip (${e.message})`);
    }
}

// 6. The launcher must APPEND the arguments of the chosen game mode to the
//    array that was assigned to Module.arguments before etl.js was loaded, not
//    replace Module.arguments. Emscripten copies Module.arguments into its
//    internal `arguments_` variable while etl.js is loaded (see
//    makeModuleReceive('arguments_', 'arguments') in emscripten's
//    src/postlibrary.js) and run()/callMain() only ever read that local, so a
//    later assignment is silently ignored. When that happened, every launcher
//    button ("Join ETc server", "Quick single game", a server-list entry,
//    "Host game") started the engine without its +connect/+map and the player
//    was dropped in the main menu. This is invisible in a build, so guard it.
if (exists('etl.html')) {
    const html = fs.readFileSync(path.join(dir, 'etl.html'), 'utf8');
    check(
        /Array\.prototype\.push\.apply\(args, extra\)/.test(html),
        'launcher appends the chosen game mode arguments in place (addEngineArgs)'
    );
    check(
        !/Module\.arguments\s*=\s*args\.concat\(/.test(html),
        'launcher does not replace Module.arguments after startup'
    );
}

// 7. Omni-bot: the bot library is a side module of its own, dlopen()ed by
//    qagame, and it locates its scripts/navigation data relative to itself.
//    Both must therefore be shipped, in one and the same folder, or a game
//    hosted in the browser has no opponents at all.
const omniBotModule = path.join('legacy', 'omni-bot', 'omnibot_et.wasm32.so');
const omniBotPresent = exists(omniBotModule);
check(omniBotPresent, `Omni-bot module present: ${omniBotModule}`);
if (omniBotPresent) {
    const buf = fs.readFileSync(path.join(dir, omniBotModule));
    check(
        hasWasmMagic(buf),
        `${omniBotModule} is valid WebAssembly (got ${firstBytes(buf)}, expected 00 61 73 6d)`
    );
}

const omniBotData = path.join('legacy', 'omni-bot', 'omni-bot-data.zip');
const omniBotDataPresent = exists(omniBotData);
check(omniBotDataPresent, `Omni-bot data pack present: ${omniBotData}`);
if (omniBotDataPresent) {
    try {
        const listing = execFileSync('unzip', ['-l', path.join(dir, omniBotData)], {
            encoding: 'utf8'
        });
        // The bot mounts global_scripts/ and the per-game "et" folder (its
        // scripts and the per-map navigation meshes) from next to the library.
        for (const entry of ['global_scripts/', 'et/scripts/', 'et/nav/']) {
            check(listing.includes(entry), `Omni-bot data pack contains ${entry}`);
        }
    } catch (e) {
        check(false, `Omni-bot data pack is a readable zip (${e.message})`);
    }
}

// 8. Hosting a game in the browser needs two more files next to etl.html: the
//    peer-to-peer transport the page loads with a <script src="etl-p2p.js">
//    tag, and the map list the host picks its map from (and every joining
//    player downloads the map from). Both are fetched at runtime relative to
//    the page, so they must sit in the root of the package.
check(exists('etl-p2p.js'), 'P2P transport present: etl-p2p.js');
if (exists('etl-p2p.js')) {
    const js = fs.readFileSync(path.join(dir, 'etl-p2p.js'), 'utf8');
    // The engine (src/qcommon/net_web.c) and the launcher both talk to the
    // transport through window.ETLP2P; without these entry points a hosted
    // game silently has no networking at all.
    for (const api of ['send', 'receive', 'host', 'join', 'listRooms']) {
        check(new RegExp(`\\b${api}\\s*[:=(]`).test(js),
            `etl-p2p.js exposes ${api}()`);
    }
}

const mapListPresent = exists('maplist.json');
check(mapListPresent, 'map list present: maplist.json');
if (mapListPresent) {
    try {
        const list = JSON.parse(fs.readFileSync(path.join(dir, 'maplist.json'), 'utf8'));
        const names = Object.keys(list);
        check(names.length > 0, 'maplist.json contains at least one map');
        // An entry is either "" (a stock map, no download) or a link to the pk3
        // the map ships in: an absolute http(s) URL, a protocol relative
        // "//host/path" one or a link relative to the page. Anything with
        // another scheme is refused by the launcher (sanitiseMapList), which
        // also aligns the link's scheme with the page's, so an https:// entry
        // works on an http:// page too.
        const bad = names.filter((name) => {
            const url = list[name];
            if (typeof url !== 'string') return true;
            // ET maps in the wild use punctuation such as !, #, brackets,
            // parentheses and apostrophes. Match the launcher's safety rule:
            // block path/control/console separators, not valid BSP basenames.
            if (!name || name.length > 64 || /[\x00-\x20\x7f\\/;"]/.test(name)) return true;
            if (url === '') return false;
            if (/^[A-Za-z][A-Za-z0-9+.-]*:/.test(url) && !/^https?:/i.test(url)) return true;
            return !/\.pk3$/.test(url.split('?')[0].split('#')[0]);
        });
        check(bad.length === 0,
            `maplist.json entries are "<bsp name>": "" or a .pk3 link${bad.length ? ` (bad: ${bad.join(', ')})` : ''}`);
    } catch (e) {
        check(false, `maplist.json is valid JSON (${e.message})`);
    }
}

if (exists('etl.html')) {
    const html = fs.readFileSync(path.join(dir, 'etl.html'), 'utf8');
    check(/<script[^>]+src="etl-p2p\.js"/.test(html),
        'etl.html loads etl-p2p.js');
    // The controls of a running game: leave, settings, invite link.
    for (const id of ['game-sidebar', 'sidebar-exit', 'sidebar-settings',
                      'sidebar-invite', 'game-panel']) {
        check(html.includes(`id="${id}"`), `etl.html contains the #${id} element`);
    }
    // The host settings and the "Join games" browser.
    for (const id of ['host-map', 'host-name', 'host-maxclients', 'host-bots',
                      'host-timelimit', 'host-private', 'menu-join-games',
                      'join-list']) {
        check(html.includes(`id="${id}"`), `etl.html contains the #${id} element`);
    }
}

if (failures) {
    console.error(`\n${failures} check(s) failed.`);
    process.exit(1);
}
console.log('\nAll structural checks passed.');
