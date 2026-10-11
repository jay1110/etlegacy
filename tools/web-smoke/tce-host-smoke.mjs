#!/usr/bin/env node

// Structural proof for the TrueCombat host picker. The authoritative map
// lists are embedded in shell.html at build time, so this test verifies their
// exact sizes, that every entry can be downloaded through maplist.json, and
// that TCETest uses the TCE Omni-bot payload instead of disabling bots.

import assert from 'node:assert/strict';
import fs from 'node:fs';
import vm from 'node:vm';

const htmlPath = process.argv[2];
const mapListPath = process.argv[3];
if (!htmlPath || !mapListPath) {
    console.error('Usage: node tce-host-smoke.mjs <etl.html|shell.html> <maplist.json>');
    process.exit(2);
}

const html = fs.readFileSync(htmlPath, 'utf8');
const mapList = JSON.parse(fs.readFileSync(mapListPath, 'utf8'));
const match = html.match(/var TRUECOMBAT_MAPS_BY_GAMETYPE = (\{[\s\S]*?\n\s*\});/);
assert.ok(match, 'TCE game-type map table is embedded');
const modes = vm.runInNewContext(`(${match[1]})`);
assert.deepEqual(Object.keys(modes).sort(), ['2', '5', '7']);
assert.equal(modes[2].length, 173, 'GT2 contains all 173 supplied maps');
assert.equal(modes[5].length, 228, 'GT5 contains all 228 supplied maps');
assert.equal(modes[7].length, 263,
    'GT7 contains its 243 scripted maps plus all 20 supplied maps without GT scripts');

const bodycountMapsWithoutGtScripts = [
    'obj_ae_sniper',
    'obj_ae_sniper_b',
    'obj_central_barn',
    'obj_closecombat_day',
    'obj_crossing_foggy1',
    'obj_dust',
    'obj_favela_b3',
    'obj_favela_rc',
    'obj_HicksTown',
    'obj_omg9',
    'obj_ruins',
    'obj_scouser',
    'obj_slums_b3',
    'obj_sniper_Lake_b2',
    'obj_sniper_marina_b2',
    'obj_stonehenge_koth',
    'obj_summer_palace_b1',
    'obj_tm_killhouse2_3',
    'obj_ujejungle_sniper',
    'obj_ujewarzone_sniper'
];
bodycountMapsWithoutGtScripts.forEach(name => {
    assert.ok(modes[7].includes(name), `${name} is available for GT7 Bodycount`);
});

const available = new Set(Object.keys(mapList).map(name => name.toLowerCase()));
for (const [gameType, names] of Object.entries(modes)) {
    assert.equal(new Set(names.map(name => name.toLowerCase())).size, names.length,
        `GT${gameType} has no duplicate map names`);
    const missing = names.filter(name => !available.has(name.toLowerCase()));
    assert.equal(missing.length, 0,
        `every GT${gameType} map is present in maplist.json`);
}

assert.match(html, /<option value="" selected>Choose a game type first<\/option>/,
    'a fresh TCE host requires a game-type choice');
assert.match(html, /function hostMapNames\(modKey, gameType\)[\s\S]*TRUECOMBAT_MAPS_BY_GAMETYPE\[gameType\]/,
    'the host map picker is filtered by the selected game type');
assert.match(html, /if \(modKey === 'tce2' \|\| modKey === 'tcetest'\)[\s\S]*tce2\/omni-bot/,
    'TCETest and TCE2 use the shared TCE Omni-bot payload');
assert.doesNotMatch(html, /modKey !== 'tcetest'/,
    'TCETest is not excluded from Omni-bot hosting');
assert.match(html, /if \(assets\.fsGame === 'tcetest'\)[\s\S]*fetchStandaloneSideModule\(name, assets\)/,
    'TCETest uses its authoritative loose private WASM modules, not mp_bin.pk3');
assert.match(html, /bytesContainAscii\(bytes, 'TCETest web Omni-bot bootstrap'\)/,
    'an old TCETest qagame without the browser Omni-bot bootstrap is rejected');
assert.match(html, /\^et\\\/nav\\\/\[\^\/\]\+\\\.way\$[\s\S]*name\.toLowerCase\(\)/,
    'navigation files receive lowercase MEMFS aliases');

console.log('PASS TCE host: mandatory game type, 173/228/263 filtered maps, TCETest Omni-bot enabled');
