# Realtime Practice Engine — V1 (locked)

Desktop native backing uses **SoundTouch** for preserve-pitch tempo change while gameplay
sync stays on the existing JUCE transport.

## Status

**Production-stable for musician practice.** Do not reopen DSP rewrites for V1.

## Musician Practice Profile V1 (locked)

Configured in `src/audio/BackingTimeStretch.cpp` → `configureSoundTouchForMusic()`:

| Setting | Value | Rationale |
|---------|-------|-----------|
| `SETTING_USE_QUICKSEEK` | 0 | Full seek — less wobble |
| `SETTING_USE_AA_FILTER` | 1 | Anti-alias on pitch path |
| `SETTING_SEQUENCE_MS` | 60 | Longer sequences — smoother low tempo |
| `SETTING_SEEKWINDOW_MS` | 20 | Stable overlap search |
| `SETTING_OVERLAP_MS` | 12 | Smoother crossfades |

**Do not change** without a deliberate V2 profile and regression listening pass.

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

Future upgrades (not V1): Rubber Band, offline stretch cache, spectral/transient-aware engines.

## Milestone

Tag: **`v1-realtime-practice-engine`** (paired with `slopsmith` UI commit).
