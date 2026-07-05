#!/usr/bin/env node
/**
 * Headless-browser boot smoke test for the ET: Legacy web build.
 *
 * Serves the packaged web build over a local static HTTP server (with the
 * correct application/wasm MIME type) and loads it in headless Chromium via
 * Playwright to confirm the wasm engine actually *boots* in a browser: the
 * shell script runs, etl.wasm is fetched and instantiated, and the engine
 * reaches its asset-bootstrap stage.
 *
 * The retail paks (pak0-2.pk3) are NOT redistributable, so a fully playable
 * "reaches the main menu" assertion is not possible in CI. Without the paks the
 * engine boots and then reports a known "could not load game data" error - that
 * is the SUCCESS condition here, because reaching it proves everything up to and
 * including wasm instantiation and the JS/asset bootstrap works. The test only
 * FAILS on a genuine fatal error (an uncaught page exception, a wasm abort, or a
 * failure to load etl.js/etl.wasm at all).
 *
 * If the retail paks happen to be present next to the build, the engine will
 * instead start normally, which is also treated as success.
 *
 * Usage:
 *   node tools/web-smoke/boot-smoke.mjs <dist/etlegacy-web> [timeoutMs]
 *
 * Requires Playwright's chromium (npx playwright install --with-deps chromium).
 *
 * License: GPL-3.0 (same as ET: Legacy)
 */

import fs from 'node:fs';
import http from 'node:http';
import path from 'node:path';

const dir = process.argv[2];
const timeoutMs = parseInt(process.argv[3] || '60000', 10);
if (!dir) {
    console.error('Usage: node boot-smoke.mjs <web-dist-dir> [timeoutMs]');
    process.exit(2);
}

const MIME = {
    '.html': 'text/html; charset=utf-8',
    '.js': 'text/javascript; charset=utf-8',
    '.mjs': 'text/javascript; charset=utf-8',
    '.wasm': 'application/wasm',
    '.pk3': 'application/octet-stream',
    '.so': 'application/wasm',
    '.data': 'application/octet-stream',
    '.txt': 'text/plain; charset=utf-8'
};

// Minimal static file server rooted at the build directory.
const server = http.createServer((req, res) => {
    const root = path.resolve(dir);
    const urlPath = decodeURIComponent(req.url.split('?')[0]);
    const rel = urlPath === '/' ? 'index.html' : urlPath.replace(/^\/+/, '');
    const filePath = path.resolve(root, rel);
    // Confine the resolved path to the served root (block path traversal).
    const relative = path.relative(root, filePath);
    if (relative === '' || relative.startsWith('..') || path.isAbsolute(relative)) {
        res.writeHead(403).end();
        return;
    }
    fs.readFile(filePath, (err, data) => {
        if (err) {
            res.writeHead(404).end('not found');
            return;
        }
        res.writeHead(200, { 'Content-Type': MIME[path.extname(filePath)] || 'application/octet-stream' });
        res.end(data);
    });
});

async function main() {
    const { chromium } = await import('playwright');

    await new Promise((resolve) => server.listen(0, '127.0.0.1', resolve));
    const port = server.address().port;
    const url = `http://127.0.0.1:${port}/etl.html`;
    console.log(`Serving ${dir} at ${url}`);

    const browser = await chromium.launch({
        args: ['--use-gl=swiftshader', '--enable-unsafe-swiftshader']
    });
    const page = await browser.newPage();

    const fatal = [];
    page.on('pageerror', (err) => fatal.push('pageerror: ' + err.message));
    page.on('requestfailed', (req) => {
        const u = req.url();
        // Only the core engine files are mandatory; a missing pak is expected.
        if (/etl\.(js|wasm)(\?|$)/.test(u)) {
            fatal.push('requestfailed: ' + u + ' ' + (req.failure()?.errorText || ''));
        }
    });

    let outcome = null;
    try {
        await page.goto(url, { waitUntil: 'domcontentloaded', timeout: timeoutMs });

        // The build marker proves the shell HTML/JS itself loaded.
        await page.waitForSelector('#build-marker', { timeout: timeoutMs });

        // Success = engine booted far enough to either start normally or report
        // the expected missing-game-data error. Failure = a wasm abort surfaced
        // in the error overlay, or the watchdog/other fatal path.
        const result = await page.waitForFunction(() => {
            const err = document.getElementById('error-overlay');
            const errText = (document.getElementById('error-text') || {}).textContent || '';
            const overlay = document.getElementById('loading-overlay');
            const status = (document.getElementById('progress-text') || {}).textContent || '';
            const errorShown = err && getComputedStyle(err).display !== 'none';

            if (errorShown) {
                if (/could not load game data|make sure pak0/i.test(errText)) {
                    return { ok: true, reason: 'expected missing-paks error: reached asset bootstrap' };
                }
                if (/abort|runtimeerror|exception was thrown|wasm/i.test(errText)) {
                    return { ok: false, reason: 'fatal error overlay: ' + errText.slice(0, 300) };
                }
                return { ok: false, reason: 'unexpected error overlay: ' + errText.slice(0, 300) };
            }
            // Engine progressed to downloading/starting, or hid the overlay.
            if (/downloading (pak|etl)|starting et: legacy/i.test(status)) {
                return { ok: true, reason: 'engine reached asset/start stage: ' + status };
            }
            // The launcher asking how to provide pak0.pk3 (no paks in CI) or
            // showing the run-game menu proves the asset bootstrap is alive.
            const setup = document.getElementById('launcher-setup');
            if (setup && getComputedStyle(setup).display !== 'none') {
                return { ok: true, reason: 'launcher game-data setup shown: reached asset bootstrap' };
            }
            const menu = document.getElementById('launcher-menu');
            if (menu && getComputedStyle(menu).display !== 'none') {
                return { ok: true, reason: 'launcher run-game menu shown: assets ready' };
            }
            if (overlay && overlay.classList.contains('hidden')) {
                return { ok: true, reason: 'loading overlay dismissed: engine started' };
            }
            return false;
        }, { timeout: timeoutMs, polling: 250 });

        outcome = await result.jsonValue();
    } catch (e) {
        outcome = { ok: false, reason: 'timeout/exception waiting for boot: ' + e.message };
    } finally {
        await browser.close();
        server.close();
    }

    if (fatal.length) {
        console.error('Fatal browser errors:\n  ' + fatal.join('\n  '));
        // A pageerror during a run that otherwise reached the expected state is
        // still a failure - the engine must boot cleanly.
        outcome = { ok: false, reason: 'fatal browser error(s) during boot' };
    }

    console.log((outcome.ok ? 'PASS ' : 'FAIL ') + outcome.reason);
    process.exit(outcome.ok ? 0 : 1);
}

main().catch((e) => {
    console.error('boot-smoke crashed: ' + e.stack);
    server.close();
    process.exit(1);
});
