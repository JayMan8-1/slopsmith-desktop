# Stem-aware slowdown — future roadmap (not implemented)

V1 realtime practice uses the **full PSARC stereo backing mix** through SoundTouch.
This is the standard, shippable mode: one stream, one tempo, minimal latency.

## Why stems matter later

Full-mix timestretch must reconcile drums, bass, vocals, and distorted guitars in one
WSOLA surface. Perceived flutter and phasing on dense harmonics (cymbals, high-gain
rhythm) are often **psychoacoustic masking problems**, not transport failures.

Stem-aware processing can improve practice quality without replacing V1 architecture:

| Future capability | Benefit |
|-------------------|---------|
| Per-stem tempo (same ratio) | Less inter-source beating in the stretcher |
| Stem-weighted blend | Emphasize guitar/drum practice stems; duck noise |
| Offline stem cache | Higher quality for repeated section loops |
| Selective routing | Metronome/click separate from stretched backing |

## Constraints for any future work

- Do not break JUCE transport or gameplay sync contracts.
- Realtime path remains default; stem modes are opt-in enhancements.
- UI preset/snap slowdown UX stays the single musician control surface.

## Related V1 stabilization (shipped)

- No SoundTouch `clear()` on routine speed changes (track load / seek / profile switch only).
- ~30ms native tempo ramp at the audio boundary.
- Optional subtle post-stretch HF softening in `BackingTimeStretch`.

See `docs/realtime-practice-engine-v1.md` for locked profiles and boundaries.
