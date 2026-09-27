#!/usr/bin/env node

// Execute the launcher's real settings serializer/deserializer in isolation.
// Browser-hosted map changes reload the page, so a rotation only works when
// the active map and its index survive this exact JSON round trip.

import assert from 'node:assert/strict';
import fs from 'node:fs';
import vm from 'node:vm';

const input = process.argv[2];
if (!input) {
    console.error('Usage: node rotation-settings-smoke.mjs <etl.html|shell.html>');
    process.exit(2);
}

const source = fs.readFileSync(input === '-' ? 0 : input, 'utf8');

function between(startText, endText) {
    const start = source.indexOf(startText);
    const end = source.indexOf(endText, start + startText.length);
    assert.notEqual(start, -1, `missing ${startText}`);
    assert.notEqual(end, -1, `missing marker after ${startText}`);
    return source.slice(start, end);
}

const serializeSource = between(
    'function etlSettingsToJson(settings)',
    'var HOST_PROFILE_FILE'
);
const deserializeSource = between(
    'function etlSettingsFromJson(text)',
    '// Fill a <select> with the map list'
);

const maps = [
    'oasis', 'goldrush', 'radar', 'railgun', 'fueldump',
    'battery', 'tc_base', 'mp_beach', 'venice', 'supply'
];
const context = vm.createContext({
    console,
    JSON,
    Math,
    Number,
    String,
    Object,
    Array,
    isFinite,
    SETTINGS_FILE_KIND: 'etlegacy-web-host-settings',
    SETTINGS_FILE_VERSION: 1,
    MAX_SETTINGS_FILE_SIZE: 64 * 1024,
    MAX_ROTATION_MAPS: 10,
    RANDOM_MAP: '__random__',
    DEFAULT_MOD: 'legacy',
    HOST_MODS: { legacy: { available: true } },
    mapNames: () => maps.slice(),
    cleanCvarSet: value => value || {},
    cleanCustomCvarSet: value => value || {},
    cleanOmniBotSettings: value => value || {}
});

vm.runInContext(`${serializeSource}\n${deserializeSource}`, context,
    { filename: input });

const base = {
    name: 'Rotation proof',
    map: maps[0],
    rotation: maps.slice(),
    rotationIndex: 0,
    maxPlayers: 8,
    bots: 0,
    timeLimit: 0,
    private: false,
    developerMode: false,
    mod: 'legacy',
    balancedTeams: false,
    doWarmup: true,
    friendlyFire: true,
    warmup: 60,
    alliedRespawn: 0,
    axisRespawn: 0,
    cvars: {},
    customCvars: {},
    omniBot: {}
};

let state = { ...base, rotation: base.rotation.slice() };
const observed = [state.map];
for (let transition = 0; transition < 20; transition += 1) {
    const nextIndex = (state.rotationIndex + 1) % state.rotation.length;
    state = { ...state, map: state.rotation[nextIndex], rotationIndex: nextIndex };

    const saved = context.etlSettingsToJson(state);
    assert.equal(saved.map, state.map,
        `transition ${transition + 1}: serializer lost the active map`);

    const restored = context.etlSettingsFromJson(JSON.stringify(saved));
    assert.equal(restored.error, '');
    assert.equal(restored.settings.map, state.map,
        `transition ${transition + 1}: reload returned to the first map`);
    assert.equal(restored.settings.rotationIndex, nextIndex,
        `transition ${transition + 1}: reload lost the rotation index`);
    state = restored.settings;
    observed.push(state.map);
}

assert.deepEqual(observed,
    Array.from({ length: 21 }, (_, index) => maps[index % maps.length]));

// Backward compatibility: old saved sessions did not contain `map`. Their
// stored index must recover the active map rather than falling back to Oasis.
const oldSave = context.etlSettingsToJson({ ...base, rotationIndex: 2, map: 'radar' });
delete oldSave.map;
const restoredOld = context.etlSettingsFromJson(JSON.stringify(oldSave));
assert.equal(restoredOld.settings.map, 'radar');
assert.equal(restoredOld.settings.rotationIndex, 2);

console.log(`PASS 10-map rotation across 20 reloads: ${observed.join(' -> ')}`);
console.log('PASS legacy session fallback: rotationIndex 2 -> radar');
