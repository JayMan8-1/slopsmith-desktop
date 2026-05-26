# Realtime Practice Engine — V1 (locked)

Desktop native backing uses **SoundTouch** for preserve-pitch tempo change while gameplay
sync stays on the existing JUCE transport.

## Status

**Production-stable for musician practice.** V1 DSP is locked after final micro-polish (dry blend + tempo ramp). Do not reopen DSP rewrites for V1.

## Adaptive SoundTouch profiles (V1)

Both profiles share `SETTING_USE_QUICKSEEK = 0` and `SETTING_USE_AA_FILTER = 1`.
`BackingTimeStretch::setTempo()` selects by tempo (backing speed):

| Region | When | SEQUENCE_MS | SEEKWINDOW_MS | OVERLAP_MS |
|--------|------|-------------|---------------|------------|
| **Near-unity** | tempo ≥ 0.80 | 40 | 15 | 8 |
| **Deep** | tempo &lt; 0.80 | 60 | 20 | 12 |

Rationale: longer WSOLA windows stabilize deep slowdown (50–60%); shorter windows
reduce chorusing/modulation near 90–75%. Profile switches call `stretch.clear()` only
when crossing the 0.80 boundary — not on every preset tweak.

## Psychoacoustic stabilization (V1)

Routine speed changes **do not** call `BackingTimeStretch::reset()` or resampler flush.
`clear()` / `reset()` remain for track load, seek, and device re-prepare.

| Technique | Where | Purpose |
|-----------|--------|---------|
| **No reset on speed** | `AudioEngine::setBackingSpeed` | Preserve WSOLA overlap continuity |
| **~70ms tempo ramp** | `BackingTimeStretch::advanceTempoRamp` | Avoid abrupt `setTempo` into WSOLA |
| **Post-stretch HF soften** | `applyPracticeSmoothing` | Subtle flutter masking (~11 kHz blend) |
| **Near-unity dry blend** | `dryFifo` + `nearUnityDryBlend` | Pre-stretch input mixed into wet (≈15% @ 90%, ≈9% @ 85%, 0% @ ≤80%) |

Dry samples are queued from the same resampler pull as `putSamples` (no second transport).
Bypass engages only when both target and ramped tempo are ≈ 1.0.

## Architecture (unchanged)

- Resampler: device sample-rate match only.
- Tempo: `BackingTimeStretch` → `soundtouch::SoundTouch`.
- Bypass at tempo ≈ 1.0 (transparent full-speed playback).
- `AudioEngine::setBackingSpeed()` skips redundant applies when Δ < 0.001.

## Build

SoundTouch is fetched at build time if missing:

```bash
npm run build:audio
```

Vendor path: `src/audio/third_party/soundtouch` (clone via `scripts/build-audio.sh`).

## Bridge

- Renderer: `preload.ts` → `audio:setBackingSpeed` IPC.
- Main: `audio-bridge.ts` → native `setBackingSpeed`.
- Duplicate invoke guard at preload, IPC, and engine layers.

## Artifact expectations

Realtime timestretch artifacts below ~0.75× are **expected** (WSOLA-class limitations).
V1 optimizes for **stable practice**, not studio mastering quality.

Future upgrades (not V1): Rubber Band, offline stretch cache, stem-aware routing.
See `docs/stem-aware-slowdown-roadmap.md`.

## Milestone

- Tag: **`v1-realtime-practice-engine`**
- Paired UI commit: same tag in `slopsmith`
- Full spec (player UX): `slopsmith/docs/realtime-practice-engine-v1.md`

## What ships next (V1.1+)

Section practice and loop workflow in the web player; native transport stays unchanged.
Planning: `slopsmith/docs/loop-section-practice-plan.md`.
