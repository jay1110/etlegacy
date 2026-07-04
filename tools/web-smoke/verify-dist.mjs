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

// 1. Core engine files must be present.
for (const f of ['etl.html', 'index.html', 'etl.js', 'etl.wasm']) {
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

// 4. The standalone cgame/ui side modules must be present in legacy/ AND be
//    valid WebAssembly. A frequent packaging/build regression is shipping an
//    `ar` static archive (starts with "!<arch>", 0x21 0x3C 0x61 0x72) instead
//    of a linked wasm side module (starts with "\0asm", 0x00 0x61 0x73 0x6D),
//    e.g. when CMake downgrades the MODULE library to a static library. The
//    engine's dlopen() then rejects it with "does not start with the
//    WebAssembly magic number", so validate the magic here, not just presence.
const WASM_MAGIC = Buffer.from([0x00, 0x61, 0x73, 0x6d]);

function firstBytes(buf, n = 4) {
    return Array.from(buf.subarray(0, n))
        .map((b) => b.toString(16).padStart(2, '0'))
        .join(' ');
}

function hasWasmMagic(buf) {
    return buf.length >= 4 && buf.subarray(0, 4).equals(WASM_MAGIC);
}

const sideModules = ['cgame.mp.wasm32.so', 'ui.mp.wasm32.so'];
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
            }
        }
    } catch (e) {
        check(false, `mod pk3 is a readable zip (${e.message})`);
    }
}

if (failures) {
    console.error(`\n${failures} check(s) failed.`);
    process.exit(1);
}
console.log('\nAll structural checks passed.');
