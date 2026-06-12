const test = require('node:test');
const assert = require('node:assert/strict');
const path = require('node:path');
const fs = require('node:fs');

const ROOT = path.join(__dirname, '..');

function readJson(relpath) {
    return JSON.parse(fs.readFileSync(path.join(ROOT, relpath), 'utf8'));
}

function readText(relpath) {
    return fs.readFileSync(path.join(ROOT, relpath), 'utf8');
}

test('renderer manifest declares active playback observer', () => {
    const manifest = readJson('src/renderer/plugin.json');
    const playback = manifest.capabilities.playback;

    assert.deepEqual(playback.roles, ['observer']);
    assert.equal(playback.kind, 'lifecycle');
    assert.deepEqual(playback.observes, ['loading', 'ready', 'stopped', 'ended']);
    assert.equal(playback.mode, 'active');
    assert.equal(playback.compatibility, 'shim-allowed');
    assert.equal(playback.ownership, 'observer-only');
    assert.equal(playback.safety, 'safe');
    assert.equal(playback.version, 1);
});

test('renderer uses playback lifecycle instead of global transport wrappers', () => {
    const source = readText('src/renderer/screen.js');

    assert.equal(source.includes('window.playSong ='), false);
    assert.equal(source.includes('window.stopSong ='), false);
    assert.equal(source.includes('registerObserver'), true);
    assert.equal(source.includes("playback:loading"), true);
    assert.equal(source.includes("playback:ready"), true);
    assert.equal(source.includes("playback:stopped"), true);
    assert.equal(source.includes("playback:ended"), true);
    assert.equal(source.includes('settingsKey'), true);
    assert.equal(source.includes('_slopsmithPlaybackSettingsKey'), true);
});

test('renderer migrates legacy filename tone mappings to playback settings key', () => {
    const source = readText('src/renderer/screen.js');
    const docs = readText('docs/CAPABILITY-MIGRATION.md');

    assert.equal(source.includes('migrateToneMappingsToPlaybackSettingsKey'), true);
    assert.equal(source.includes('window._aeMigrateToneMappingsToPlaybackSettingsKey'), true);
    assert.equal(source.includes("migrateBucket('songs')"), true);
    assert.equal(source.includes("migrateBucket('midiPC')"), true);

    assert.equal(docs.includes('Automatic Migration For Existing Mappings'), true);
    assert.equal(docs.includes('Existing `settingsKey` buckets win and are not overwritten.'), true);
});

test('renderer registers native audio-mix fader participants', () => {
    const source = readText('src/renderer/screen.js');

    assert.equal(source.includes('registerMixParticipant'), true);
    assert.equal(source.includes('audio_engine.input_gain'), true);
    assert.equal(source.includes('audio_engine.chain_gain'), true);
    assert.equal(source.includes("'fader.get-value'"), true);
    assert.equal(source.includes("'fader.set-value'"), true);
});

test('chain panel summarizes provider-managed audio effects mappings', () => {
    const source = readText('src/renderer/screen.js');

    assert.equal(source.includes('fetchAudioEffectMappingsForSong'), true);
    assert.equal(source.includes('/api/audio-effects/mappings?'), true);
    assert.equal(source.includes('summarizeProviderManagedMappings'), true);
    assert.equal(source.includes('rig_builder.effects'), true);
    assert.equal(source.includes('Chain Provider'), true);
    assert.equal(source.includes('ae-open-rig-builder'), true);
    assert.equal(source.includes('hasProviderManagedAudioEffectsChain'), true);
    assert.equal(source.includes('window._aeHasProviderManagedChain'), true);
    assert.equal(source.includes('function shouldShowPlayerChainButton()'), true);
    assert.equal(source.includes("String(inspected?.providerId || '').trim() === 'nam-tone'"), true);
    assert.equal(source.includes('window._aeShouldShowPlayerChainButton'), true);
    assert.equal(source.includes('window._aeInjectPlayerToneButton = injectPlayerToneButton'), true);
    assert.equal(source.includes('function rigBuilderToneOwnershipState()'), true);
    assert.equal(source.includes("window.__rbMegaChainSetting === true"), true);
    assert.equal(source.includes("state: pending ? 'selected' : failed ? 'fallback' : active ? 'loaded' : 'selected'"), true);
    assert.equal(source.includes('function removePlayerChainButton()'), true);
    assert.equal(source.includes("document.getElementById('btn-chain-switch')"), true);
    assert.equal(source.includes('if (!shouldShowPlayerChainButton())'), true);
    assert.equal((source.match(/btn\.id = 'btn-chain-switch'/g) || []).length, 1);
    assert.equal(source.includes('refreshChainButtonForRouteOwner'), true);
    assert.equal(source.includes('window.slopsmith.on(\'audio-effects:released\', refreshChainButtonForRouteOwner);'), true);
    assert.equal(source.includes('if (window._aeInjectPlayerToneButton) window._aeInjectPlayerToneButton();'), true);
    assert.equal(source.includes('inspectProviderManagedAudioEffectsRoute'), true);
    assert.equal(source.includes('window._aeInspectProviderManagedChain'), true);
    assert.equal(source.includes('summarizeActiveProviderManagedRoute'), true);
    assert.equal(source.includes('summarizeProviderManagedMappings(await fetchAudioEffectMappingsForSong(songKey)) || summarizeActiveProviderManagedRoute()'), true);
    assert.equal(source.includes('window.RbMegaChain'), true);
    assert.equal(source.includes("typeof rb.isPending === 'function'"), true);
    assert.equal(source.includes("typeof rb.state === 'function'"), true);
    assert.equal(source.includes("providerId: 'rig_builder.effects'"), true);
    assert.equal(source.includes('const rigBuilderOwner = rigBuilderToneOwnershipState();'), true);
    assert.equal(source.includes("['selected', 'resolving', 'resolved', 'loaded', 'degraded', 'loading', 'fallback'].includes(state)"), true);
    assert.equal(source.includes("if (inspected.state === 'fallback') label = 'Chain failed'"), true);
    assert.equal(source.includes("else if (inspected.state === 'degraded') label = 'Chain degraded'"), true);
    assert.equal(source.includes("['selected', 'resolving', 'loading'].includes(inspected.state)"), true);
    assert.equal(source.includes("'loading'") && source.includes('Loading chain'), true);
    assert.equal(source.includes('Provider-managed audio-effects chain active'), true);
    assert.equal(source.includes("const panelMode = providerManaged ? 'provider'"), true);
    assert.equal(source.includes('if (!providerManaged && toneNamesOrdered.length > 0)'), true);
    assert.equal(source.includes('if (providerChainActive) {'), true);
    assert.equal(source.includes('Provider-managed audio-effects chain active — preserving chain, skipping legacy preset preload'), true);
    assert.equal(source.includes('aeSetMonitorMuteSuppressed(false);'), true);
    assert.equal(source.includes("['selected', 'resolving', 'loading', 'fallback'].includes(providerRoute.state)"), true);
    assert.equal(source.includes('let shouldResolveChainRebuildGuard = false;'), true);
    assert.equal(source.includes('if (shouldResolveChainRebuildGuard) await resolveChainRebuildGuard();'), true);
});

test('legacy midi_amp tone lookup is guarded by plugin availability', () => {
    const source = readText('src/renderer/screen.js');

    assert.equal(source.includes('hasMidiAmpSongTonesEndpoint'), true);
    assert.equal(source.includes('midiAmpSongTonesUnavailable'), true);
    assert.equal(source.includes('midiAmpSongTonesPending'), true);
    assert.equal(source.includes('document.querySelector(\'[data-plugin-id="midi_amp"]\')'), true);
    assert.equal(source.includes('resp.status === 404'), true);
    assert.equal(source.includes('fetchMidiAmpSongTones(key)'), true);
});