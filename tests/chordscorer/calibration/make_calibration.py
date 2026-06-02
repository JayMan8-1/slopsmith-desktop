#!/usr/bin/env python3
"""Generate the bass calibration etude: a machine chart for cs_bench, a click
track to lock the player to the chart's timing, and a per-note table.

The chart's note times are absolute (90 BPM, 4/4). cs_bench only searches for a
single start-offset between the recording and the chart, NOT tempo — so the
player MUST play to click.wav (in headphones; the DI stays bass-only). With the
click, every note lands at chart-time + a constant capture latency, which the
offset search removes.

Outputs (in this directory):
  chart.txt   "<t_seconds> <midi> <sustain_seconds>" per line  (cs_bench input)
  click.wav   90 BPM click, 4-beat count-in, accented downbeats (play along)
  notes.csv   t_s, beat, section, string, fret, midi, sus_s, note  (for the score)
"""
import csv
import math
import struct
import wave
from pathlib import Path

# Anchor outputs next to this script (as the module docstring promises),
# independent of the caller's working directory.
OUT_DIR = Path(__file__).resolve().parent

BPM = 90
SR = 48000
BEAT = 60.0 / BPM                  # 0.6667 s
EIGHTH = BEAT / 2.0
COUNT_IN_BEATS = 4                 # clicks before the first played note

# 4-string standard tuning, low->high (string index 0 = low E).
TUNING4 = [28, 33, 38, 43]         # E1 A1 D2 G2
TUNING_NAMES = ["E", "A", "D", "G"]


def string_fret(midi, tuning):
    """Lowest-fret (string, fret) realising `midi`, or (None, None)."""
    for s in range(len(tuning) - 1, -1, -1):
        fret = midi - tuning[s]
        if 0 <= fret <= 24:
            return s, fret
    return None, None


# (beat_offset_from_first_note, midi, sustain_beats, section, technique)
events = []


def add(beat, midi, sus_beats, section, tech=""):
    events.append((beat, midi, sus_beats, section, tech))


# ── A: open strings (presence + tuning baseline) — quarter notes ────────────
for i, m in enumerate([28, 28, 33, 33]):
    add(0 + i, m, 0.9, "A: open strings", "let ring")
for i, m in enumerate([38, 38, 43, 43]):
    add(4 + i, m, 0.9, "A: open strings", "let ring")

# ── B: chromatic walk frets 0-3 on each string (pitch precision low range) ──
for s, lo in enumerate([28, 33, 38, 43]):
    for f in range(4):
        add(8 + s * 4 + f, lo + f, 0.5, "B: chromatic walk", "")

# ── C: long sustains (sustain / ring presence) — whole notes ────────────────
for i, m in enumerate([28, 33, 38, 43]):
    add(24 + i * 4, m, 4.0, "C: sustains", "let ring full bar")

# ── D: fast eighth-note runs (onset density) ────────────────────────────────
# repeated open E
for i in range(8):
    add(40 + i * 0.5, 28, EIGHTH / BEAT * 0.9, "D: fast eighths", "open E x8")
# A-string melodic eighths (A B C# D E D C# B)
for i, m in enumerate([33, 35, 37, 38, 40, 38, 37, 35]):
    add(44 + i * 0.5, m, EIGHTH / BEAT * 0.9, "D: fast eighths", "A walk")
# octave jumps E1<->E2
for i in range(8):
    add(48 + i * 0.5, 28 if i % 2 == 0 else 40, EIGHTH / BEAT * 0.9,
        "D: fast eighths", "octave jump")

LAST_4STRING_BEAT = 52.0   # everything above is playable on a 4-string

# ── E (OPTIONAL, 5-string only): low B section — skip on a 4-string ─────────
add(54, 23, 2.0, "E: low B (5-string only)", "open low B, let ring")
add(57, 23, 0.9, "E: low B (5-string only)", "")
for i, m in enumerate([23, 24, 25, 26]):
    add(58 + i, m, 0.5, "E: low B (5-string only)", "B string frets 0-3")

events.sort(key=lambda e: e[0])

# ── Write the machine chart + notes table ───────────────────────────────────
with open(OUT_DIR / "chart.txt", "w") as cf, \
     open(OUT_DIR / "notes.csv", "w", newline="") as nf:
    w = csv.writer(nf)
    w.writerow(["t_s", "beat", "section", "string", "fret", "midi", "sus_s", "note"])
    for beat, midi, sus_beats, section, tech in events:
        t = beat * BEAT
        sus = sus_beats * BEAT
        cf.write(f"{t:.4f} {midi} {sus:.3f}\n")
        # tab string/fret only meaningful for the standard 4-string set here;
        # the low-B section maps onto a 5-string B string (index -1 → "B").
        s, fr = string_fret(midi, TUNING4)
        sname = TUNING_NAMES[s] if s is not None else "B"
        if s is None:  # below E1 → 5-string B string
            fr = midi - 23
        w.writerow([f"{t:.3f}", f"{beat:g}", section, sname, fr, midi,
                    f"{sus:.2f}", tech])

# ── Render the click track ──────────────────────────────────────────────────
last_beat = max(e[0] for e in events)
total_beats = COUNT_IN_BEATS + last_beat + 4   # tail so the last note rings
nsamp = int(total_beats * BEAT * SR)
buf = [0.0] * nsamp


def ping(start_s, freq, ms=35, gain=0.5):
    n = int(ms / 1000.0 * SR)
    s0 = int(start_s * SR)
    for i in range(n):
        if s0 + i >= nsamp:
            break
        env = math.exp(-i / (0.010 * SR))         # fast decay click
        buf[s0 + i] += gain * env * math.sin(2 * math.pi * freq * i / SR)


# Count-in clicks, then one click per beat; accent (higher) on each bar's beat 1.
first_note_at = COUNT_IN_BEATS * BEAT
beat_idx = 0
t = 0.0
while t < total_beats * BEAT:
    # beats relative to the first played note; downbeat = multiple of 4
    rel = beat_idx - COUNT_IN_BEATS
    downbeat = (rel % 4 == 0)
    in_countin = beat_idx < COUNT_IN_BEATS
    freq = 1760.0 if (downbeat or in_countin) else 1175.0
    gain = 0.6 if (downbeat or in_countin) else 0.35
    ping(t, freq, gain=gain)
    beat_idx += 1
    t = beat_idx * BEAT

pcm = b"".join(struct.pack("<h", max(-32767, min(32767, int(v * 32767))))
               for v in buf)
with wave.open(str(OUT_DIR / "click.wav"), "wb") as wv:
    wv.setnchannels(1)
    wv.setsampwidth(2)
    wv.setframerate(SR)
    wv.writeframes(pcm)

print(f"chart.txt: {len(events)} notes, {last_beat * BEAT:.1f}s of music")
print(f"click.wav: {total_beats * BEAT:.1f}s, {BPM} BPM, {COUNT_IN_BEATS}-beat count-in, "
      f"first note at {first_note_at:.2f}s")
print(f"4-string players: stop after Section D ({LAST_4STRING_BEAT * BEAT:.1f}s "
      f"+ ring); Section E is 5-string-only.")
