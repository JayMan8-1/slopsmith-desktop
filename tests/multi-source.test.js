// Multi-input source API smoke test (Phase 1).
//
// Exercises the native source pool + source-indexed scoring API without a live
// audio device: addSource/removeSource/listSources and the *Source* methods keep
// their shapes, the pool is bounded, sources[0] is permanent, and the legacy
// un-suffixed methods still target source 0 (backward compatibility).

const test = require('node:test');
const assert = require('node:assert/strict');
const path = require('node:path');

const ADDON = path.join(__dirname, '..', 'build', 'Release', 'slopsmith_audio.node');

let audio;
try {
    audio = require(ADDON);
} catch (e) {
    test('multi-source (skipped — addon not built)', { skip: true }, () => {});
}

const CHART = {
    arrangement: 'guitar',
    stringCount: 6,
    tuningOffsets: [0, 0, 0, 0, 0, 0],
    timingTolerance: 0.1,
    notes: [{ id: 'n0', t: 1.0, s: 0, f: 3, sus: 0 }],
};
const SCORE_REQ = {
    arrangement: 'guitar',
    stringCount: 6,
    offsets: [0, 0, 0, 0, 0, 0],
    notes: [{ s: 0, f: 3 }, { s: 1, f: 2 }],
};

if (audio) {
    test('multi-input source API', async (t) => {
        audio.init();
        t.after(() => { try { audio.shutdown(); } catch { /* ignore */ } });

        await t.test('listSources starts with the permanent source 0', () => {
            const list = audio.listSources();
            assert.ok(Array.isArray(list), 'listSources returns an array');
            assert.equal(list.length, 1, 'only source 0 active at start');
            assert.equal(list[0].id, 0);
            assert.equal(list[0].active, true);
        });

        let sid;
        await t.test('addSource activates a pooled chain bound to a channel', () => {
            sid = audio.addSource(1);
            assert.equal(typeof sid, 'number');
            assert.ok(sid >= 1, 'new source id is >= 1 (0 is permanent)');
            const list = audio.listSources();
            assert.equal(list.length, 2, 'two sources active');
            const added = list.find((s) => s.id === sid);
            assert.ok(added, 'added source is listed');
            assert.equal(added.inputChannel, 1, 'bound to requested channel');
        });

        await t.test('setSourceChart accepts a chart for a valid id, rejects a bad id', () => {
            assert.equal(audio.setSourceChart(sid, CHART), true, 'valid id + chart -> true');
            assert.equal(audio.setSourceChart(999, CHART), false, 'out-of-range id -> false');
            assert.equal(audio.setSourceChart(sid, { stringCount: 6, tuningOffsets: [0], notes: [] }), false,
                'malformed chart (offsets != stringCount) -> false');
        });

        await t.test('scoreSourceChord keeps its shape; bad id -> empty failure', () => {
            const res = audio.scoreSourceChord(sid, SCORE_REQ);
            assert.ok(res && typeof res === 'object');
            for (const k of ['score', 'hitStrings', 'totalStrings', 'isHit', 'results'])
                assert.ok(k in res, `result has ${k}`);
            assert.equal(res.results.length, 2, 'one result per note');
            const bad = audio.scoreSourceChord(999, SCORE_REQ);
            assert.equal(bad.totalStrings, 0, 'bad id -> no-request failure shape');
        });

        await t.test('getSourceNoteVerdicts / getSourceRawAudioFrame / getSourcePitchDetection', () => {
            const verdicts = audio.getSourceNoteVerdicts(sid, 1.0, true);
            assert.ok(Array.isArray(verdicts), 'verdicts is an array (empty without a device)');
            assert.equal(audio.getSourceNoteVerdicts(999), null, 'bad id -> null');

            const frame = audio.getSourceRawAudioFrame(sid, 2048);
            assert.ok(frame instanceof Float32Array && frame.length === 2048, 'raw frame sized');
            assert.equal(audio.getSourceRawAudioFrame(999).length, 0, 'bad id -> empty');

            for (const fn of ['getSourcePitchDetection', 'getSourceRawPitchDetection']) {
                const det = audio[fn](sid);
                for (const k of ['frequency', 'confidence', 'midiNote', 'cents', 'noteName'])
                    assert.ok(k in det, `${fn} detection has ${k}`);
                assert.equal(audio[fn](999).frequency, -1, `${fn} bad id -> no-detection shape`);
            }
        });

        await t.test('the pool is bounded and the 0 source is permanent', () => {
            // Fill the remaining slots (pool is 8; source 0 + sid already used).
            const added = [];
            for (let i = 0; i < 8; i++) {
                const id = audio.addSource(-1);
                if (id < 0) break;
                added.push(id);
            }
            assert.equal(audio.addSource(-1), -1, 'addSource returns -1 when the pool is full');
            assert.equal(audio.removeSource(0), false, 'source 0 cannot be removed');
            // Clean the ones we added in this subtest.
            for (const id of added) assert.equal(audio.removeSource(id), true, `removeSource(${id})`);
        });

        await t.test('removeSource deactivates and frees the slot for reuse', () => {
            assert.equal(audio.removeSource(sid), true, 'removeSource(sid) -> true');
            assert.equal(audio.removeSource(sid), false, 'removing twice -> false');
            const list = audio.listSources();
            assert.equal(list.length, 1, 'back to just source 0');
            const reused = audio.addSource(2);
            assert.ok(reused >= 1, 'a freed slot is reusable');
            audio.removeSource(reused);
        });

        await t.test('legacy methods still target source 0 (backward compat)', () => {
            assert.equal(audio.setChart(CHART), true, 'legacy setChart works');
            const res = audio.scoreChord(SCORE_REQ);
            assert.equal(res.results.length, 2, 'legacy scoreChord works');
            assert.ok(Array.isArray(audio.getNoteVerdicts()), 'legacy getNoteVerdicts works');
        });
    });
}
