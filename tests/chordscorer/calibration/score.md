# Bass detection calibration etude

A ~41-second exercise that stresses every bass weak-spot in the detector: open
low strings (weak fundamentals), a chromatic walk (pitch precision across the
low range), long sustains, a fast eighth-note run (onset density), octave jumps,
and an optional 5-string low-B section.

Play it **locked to `click.wav`** so your notes line up with the reference
chart — the scorer only corrects a single start-offset, not tempo drift.

---

## Recording setup (please follow these)

- **Clean DI only.** Bass → interface, *before* any amp sim / NAM / pedals /
  EQ. This is what the detector taps.
- **Play to the click in headphones.** Open `click.wav` in your DAW/player and
  monitor it in headphones; keep it out of the recorded DI (a tiny bit of bleed
  is harmless — the click is high-pitched and won't match any bass note).
- **Tempo:** 90 BPM, 4/4. The click has a **4-beat count-in**; the **first note
  lands on the next downbeat** (≈2.67 s into `click.wav`).
- **Record two passes**, same etude, so I can tune for both attacks:
  - `bass_finger.wav` — fingerstyle
  - `bass_pick.wav` — with a pick
- Format: WAV, **16-bit PCM**, 48 kHz preferred (44.1/96 fine), mono or stereo.
- Let sustained/ringing notes **ring fully** — don't mute early.
- **4-string bass:** stop after Section D (~35 s). **5-string:** continue into
  Section E.

When done, drop the WAV(s) anywhere and tell me the path — I'll run the bench.

---

## The etude

Notation: bass tab, one digit = fret on that string. `·` = that string silent.
"q" = quarter note (one per click), "8th" = two per click. Let ring unless noted.

### Section A — open strings (bars 2–3, quarter notes)
Pluck each open string twice; **let every note ring**.
```text
Bar 2:  E(open)  E(open)  A(open)  A(open)
Bar 3:  D(open)  D(open)  G(open)  G(open)
```

### Section B — chromatic walk, frets 0–3 (bars 4–7, quarter notes)
One note per click, up each string:
```text
E string:  0  1  2  3
A string:  0  1  2  3
D string:  0  1  2  3
G string:  0  1  2  3
```

### Section C — sustains (bars 8–11, whole notes — let ring the full bar)
One open string per bar, held for all 4 beats:
```text
E (open) ——ring—— | A (open) ——ring—— | D (open) ——ring—— | G (open) ——ring——
```

### Section D — fast eighth notes (bars 12–14, two notes per click)
Bar 12 — open **E** eighths ×8 (steady, even):
```text
E| 0 0 0 0 0 0 0 0
```
Bar 13 — **A-string walk** eighths (A B C# D E D C# B):
```text
A| 0 2 4 · · · 4 2
D| · · · 0 2 0 · ·
```
Bar 14 — **octave jumps** E1↔E2 eighths (alternate, ×8):
```text
D| · 2 · 2 · 2 · 2
E| 0 · 0 · 0 · 0 ·
```

### Section E — low B (5-STRING ONLY — 4-string players skip)
Open low **B** ring, then a B-string chromatic 0–3 (B C C# D):
```text
B (open) ——ring—— ,  B(0) ,  B: 0  1  2  3
```

---

## What I do with it
```bash
cd tests/chordscorer
cmake --build build --target cs_bench
./build/cs_bench <bass_finger.wav> calibration/chart.txt bass 4   # or "bass 5"
./build/cs_bench <bass_pick.wav>   calibration/chart.txt bass 5
```
The chart (`chart.txt`) is `"<time_s> <midi> <sustain_s>"` per line, generated
by `make_calibration.py`. I sweep `fundamentalRatio` / onset profile / cents
against your real take, then re-run the **guitar** bench on `di-take.wav` to
prove zero guitar regression.
