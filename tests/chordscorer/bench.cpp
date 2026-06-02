// Controlled benchmark for the harmonic-comb verifier (ChordScorer +
// OnsetDetector) — the path the live app actually uses for guitar AND bass
// (the renderer drives it via setChart with harmonicVerify/bypassMl, NOT the
// ML detector). mlnd_bench measures the unused ML path; THIS measures the real
// one, so a bass-tuning change to ChordScorer/OnsetDetector can be measured
// (recall / timing) instead of guessed from noisy live takes.
//
// It replays a DI recording the way NoteVerifier does: an OnsetDetector pass
// over the whole stream gives pick-attack times; for each chart note, the
// harmonic-comb (ChordScorer, harmonicVerify) is asked "is this pitch present"
// across its timing window, and a note ever-present is a hit. Timing for a hit
// comes from the nearest detected onset, mirroring NoteVerifier.
//
// Build: see CMakeLists.txt.  Run:
//   ./cs_bench <di-take.wav> <chart.txt> [arrangement] [stringCount] [channel]
//              [harmonicSnr] [fundamentalRatio] [pitchCheckCents]
//     chart.txt   : one "<chartTimeSec> <midi> [sustainSec]" per line
//                   (jq from a note_detect diagnostic: .events[] | "\(.t) \(.ex)").
//     arrangement : bass (default) | guitar
//     stringCount : 4 (default for bass) | 5 | 6 | 7 | 8
//     channel     : mix (default) | left | right
//   The verify knobs default to the per-arrangement values the plugin sends;
//   pass them to sweep without a rebuild.
//
// The recording and the chart start at an unknown relative offset, so the
// harness searches the offset that best aligns them, then reports at it. Run
// the SAME WAV/chart through with `arrangement guitar` as a regression guard —
// all bass changes are arrangement-gated, so guitar numbers must not move.

#include "ChordScorer.h"
#include "OnsetDetector.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace
{
uint32_t rdU32(const uint8_t* p) { return p[0] | (p[1]<<8) | (p[2]<<16) | (uint32_t(p[3])<<24); }
uint16_t rdU16(const uint8_t* p) { return uint16_t(p[0] | (p[1]<<8)); }

// Read a 16-bit PCM WAV to mono float. channel: 0 = mix, 1 = left, 2 = right.
// (Verbatim from tests/mlnotedetector/bench.cpp — the canonical reader.)
bool readWav(const std::string& path, int channelMode, std::vector<float>& out, int& sampleRate)
{
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    std::vector<uint8_t> buf((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    if (buf.size() < 44 || std::memcmp(buf.data(), "RIFF", 4) || std::memcmp(buf.data()+8, "WAVE", 4))
        return false;

    uint16_t fmt = 0, channels = 0, bits = 0;
    uint32_t rate = 0, dataLen = 0;
    const uint8_t* data = nullptr;
    size_t pos = 12;
    while (pos + 8 <= buf.size())
    {
        const char* id = reinterpret_cast<const char*>(buf.data() + pos);
        const uint32_t sz = rdU32(buf.data() + pos + 4);
        const uint8_t* body = buf.data() + pos + 8;
        if (!std::memcmp(id, "fmt ", 4) && sz >= 16 && pos + 8 + 16 <= buf.size())
        { fmt = rdU16(body); channels = rdU16(body+2); rate = rdU32(body+4); bits = rdU16(body+14); }
        else if (!std::memcmp(id, "data", 4))
        { data = body; dataLen = std::min<uint32_t>(sz, uint32_t(buf.size() - (pos + 8))); }
        pos += 8 + sz + (sz & 1);
    }
    if (!data || channels == 0 || rate == 0 || fmt != 1 || bits != 16) return false;

    sampleRate = int(rate);
    const size_t frames = dataLen / (size_t(2) * channels);
    out.resize(frames);
    for (size_t i = 0; i < frames; ++i)
    {
        auto sample = [&](int c) -> double {
            return int16_t(rdU16(data + (i * channels + c) * 2)) / 32768.0;
        };
        double v;
        if (channelMode == 1)                       v = sample(0);
        else if (channelMode == 2 && channels > 1)  v = sample(1);
        else { double a = 0; for (int c = 0; c < channels; ++c) a += sample(c); v = a / channels; }
        out[i] = float(v);
    }
    return true;
}

struct ChartNote { double t; int midi; double sus; int string; int fret; };

// Map a MIDI pitch onto a (string, fret) for the given open-string tuning,
// preferring the lowest playable fret (highest string whose open note is at or
// below the pitch). Returns false when no string reaches it within 24 frets.
bool midiToStringFret(int midi, const std::vector<int>& base, int& outString, int& outFret)
{
    int bestString = -1, bestFret = 0;
    for (int s = (int) base.size() - 1; s >= 0; --s)
    {
        const int fret = midi - base[(size_t) s];
        if (fret >= 0 && fret <= 24) { bestString = s; bestFret = fret; break; }
    }
    if (bestString < 0) return false;
    outString = bestString;
    outFret = bestFret;
    return true;
}
} // namespace

int main(int argc, char** argv)
{
    if (argc < 3)
    {
        std::cerr << "usage: cs_bench <di-take.wav> <chart.txt> [arrangement] [stringCount]"
                     " [mix|left|right] [harmonicSnr] [fundamentalRatio] [pitchCheckCents]\n";
        return 2;
    }
    const std::string arrangement = (argc > 3) ? argv[3] : "bass";
    int stringCount = (argc > 4) ? std::atoi(argv[4]) : (arrangement == "bass" ? 4 : 6);
    int channelMode = 0;
    if (argc > 5)
    {
        const std::string c = argv[5];
        channelMode = (c == "left") ? 1 : (c == "right") ? 2 : 0;
    }
    // Per-arrangement verify defaults (overridable for sweeps). Bass relaxes the
    // fundamental-presence gate (DI fundamental is weak) and widens the cents
    // window (low bins resolve pitch coarsely); guitar keeps the shipped values.
    const bool bass = (arrangement == "bass");
    float harmonicSnr      = (argc > 6) ? (float) std::atof(argv[6]) : (bass ? 2.0f : 3.0f);
    float fundamentalRatio = (argc > 7) ? (float) std::atof(argv[7]) : (bass ? 0.08f : 0.20f);
    float pitchCheckCents  = (argc > 8) ? (float) std::atof(argv[8]) : (bass ? 60.0f : 50.0f);

    const std::vector<int>* basePtr = ChordScorer::standardMidiFor(arrangement, stringCount);
    if (!basePtr)
    { std::cerr << "FAIL: unsupported arrangement/stringCount " << arrangement << "/" << stringCount << "\n"; return 1; }
    const std::vector<int>& base = *basePtr;

    std::vector<float> wav;
    int sampleRate = 0;
    if (!readWav(argv[1], channelMode, wav, sampleRate))
    { std::cerr << "FAIL: cannot read 16-bit WAV " << argv[1] << "\n"; return 1; }
    const double wavSec = double(wav.size()) / sampleRate;
    std::cout << "WAV: " << wav.size() << " samples @ " << sampleRate << " Hz (" << wavSec << " s)\n";
    std::cout << "scorer: arrangement=" << arrangement << " strings=" << stringCount
              << " snr=" << harmonicSnr << " fundamentalRatio=" << fundamentalRatio
              << " pitchCents=" << pitchCheckCents << "\n";

    // --- Chart: <t> <midi> [sus]; map each pitch to (string,fret) -----------
    std::vector<ChartNote> chart;
    int unmapped = 0;
    {
        std::ifstream cf(argv[2]);
        if (!cf) { std::cerr << "FAIL: cannot read chart " << argv[2] << "\n"; return 1; }
        std::string line;
        while (std::getline(cf, line))
        {
            std::istringstream is(line);
            ChartNote n{};
            n.sus = 0.0;
            if (!(is >> n.t >> n.midi)) continue;
            is >> n.sus;  // optional
            if (!midiToStringFret(n.midi, base, n.string, n.fret)) { ++unmapped; continue; }
            chart.push_back(n);
        }
    }
    std::cout << "chart: " << chart.size() << " notes";
    if (unmapped) std::cout << "  (" << unmapped << " out of tuning range, skipped)";
    std::cout << "\n";
    if (chart.empty()) { std::cerr << "FAIL: no playable chart notes\n"; return 1; }

    // --- Onset pass over the whole WAV (NoteVerifier feeds it the input ring) -
    OnsetDetector onset;
    onset.prepare((double) sampleRate);
    onset.setProfile(bass);
    std::vector<double> onsetTimes;  // seconds in WAV time
    {
        const int block = 256;
        std::vector<OnsetDetector::Onset> os;
        for (size_t i = 0; i < wav.size(); i += (size_t) block)
        {
            const size_t n = std::min<size_t>((size_t) block, wav.size() - i);
            os.clear();
            onset.process(wav.data() + i, n, i, os);
            for (const auto& o : os) onsetTimes.push_back((double) o.sampleIndex / sampleRate);
        }
    }
    std::sort(onsetTimes.begin(), onsetTimes.end());
    std::cout << "onsets: " << onsetTimes.size() << "\n";

    // --- Presence of one note at a given chart->WAV offset -------------------
    // Mirror NoteVerifier's everPresent: the comb is asked across the note's
    // timing window (onset .. onset+sustain), and ANY present tick is a hit.
    ChordScorer scorer;
    constexpr int   kFrame = 4096;            // matches NoteVerifier's getInputFrame(4096)
    std::vector<float> frame((size_t) kFrame, 0.0f);

    auto presentAt = [&](const ChartNote& cn, double audioTime, double spanCap) -> bool
    {
        // Walk a few analysis frames across the note's sounding span so a
        // ringing bass note is caught even if its attack frame is marginal.
        // Alignment passes spanCap=0 (attack frame only) so a long sustain scan
        // can't blur the offset by catching a same-pitch neighbour late.
        const double span = std::min(std::max(cn.sus, 0.0) + 0.10, std::max(spanCap, 0.0));
        for (double off = 0.0; off <= span + 1e-9; off += 0.040)
        {
            const long long end = (long long) std::llround((audioTime + off) * sampleRate);
            if (end <= 0 || end > (long long) wav.size()) continue;
            const long long start = end - kFrame;
            for (int k = 0; k < kFrame; ++k)
            {
                const long long idx = start + k;
                frame[(size_t) k] = (idx >= 0 && idx < (long long) wav.size()) ? wav[(size_t) idx] : 0.0f;
            }
            ChordScorer::Request req;
            req.numSamples = kFrame;
            req.arrangement = arrangement;
            req.stringCount = stringCount;
            req.tuningOffsets.assign((size_t) stringCount, 0);
            req.capo = 0;
            req.pitchCheckCents = pitchCheckCents;
            req.harmonicVerify = true;
            req.harmonicSnr = harmonicSnr;
            req.fundamentalRatio = fundamentalRatio;
            ChordScorer::Note nt{};
            nt.string = cn.string;
            nt.fret = cn.fret;
            req.notes.push_back(nt);
            const auto r = scorer.scoreChord(frame.data(), kFrame, (double) sampleRate, req);
            if (!r.results.empty() && r.results[0].hit) return true;
        }
        return false;
    };

    // --- Align: coarse then fine search for the offset maximising hits -------
    // The full presence scan is FFT-heavy, so the coarse pass samples up to 300
    // evenly-spaced notes; the fine pass and the final report use all notes.
    auto hitCount = [&](double delta, size_t stride) -> int
    {
        int h = 0;
        for (size_t i = 0; i < chart.size(); i += stride)
            if (presentAt(chart[i], chart[i].t + delta, 0.0)) ++h;  // attack frame only
        return h;
    };
    const size_t coarseStride = std::max<size_t>(1, chart.size() / 300);
    double bestDelta = 0.0;
    int bestHits = -1;
    for (double d = -2.0; d <= 5.0; d += 0.100)
    {
        const int h = hitCount(d, coarseStride);
        if (h > bestHits) { bestHits = h; bestDelta = d; }
    }
    for (double d = bestDelta - 0.150; d <= bestDelta + 0.150; d += 0.010)
    {
        const int h = hitCount(d, 1);
        if (h > bestHits) { bestHits = h; bestDelta = d; }
    }
    std::cout << "\n=== alignment ===\nchart->WAV offset: " << bestDelta << " s\n";

    // --- Report at the best offset ------------------------------------------
    const double tol = 0.10;  // ±100 ms timing-match window (mlnd_bench parity)
    const bool verbose = std::getenv("CS_BENCH_VERBOSE") != nullptr;
    int hits = 0;
    std::vector<double> te;   // timing errors of hits that claimed an onset
    std::vector<const ChartNote*> missed;
    for (const auto& cn : chart)
    {
        const double audioT = cn.t + bestDelta;
        // Full sounding span for the recall report — a sustained bass note that
        // rings into its window is a legitimate hit.
        if (!presentAt(cn, audioT, std::max(cn.sus, 0.0) + 0.10)) { missed.push_back(&cn); continue; }
        ++hits;
        double best = 1e9;
        for (double ot : onsetTimes)
            if (std::fabs(ot - audioT) <= tol && std::fabs(ot - audioT) < std::fabs(best))
                best = ot - audioT;
        if (best < 1e8) te.push_back(best);
    }
    std::sort(te.begin(), te.end());

    std::cout << "\n=== detection quality ===\n";
    std::cout << "recall (present): " << hits << " / " << chart.size()
              << "  (" << (100.0 * hits / (double) chart.size()) << "%)\n";
    std::cout << "onset-timed hits: " << te.size() << " / " << hits
              << "  (rest report on chart time — legato or no detected attack)\n";
    if (!te.empty())
    {
        double sum = 0; for (double x : te) sum += x;
        std::cout << "timing error:     median " << (te[te.size()/2]*1000) << " ms"
                  << "   p10 " << (te[te.size()/10]*1000)
                  << "   p90 " << (te[te.size()*9/10]*1000)
                  << "   mean " << (sum/te.size()*1000) << " ms\n";
    }
    std::cout << "onsets/note:      " << (double(onsetTimes.size()) / (double) chart.size())
              << "  (>1 = extra attacks: noise, double-triggers)\n";

    if (verbose && !missed.empty())
    {
        static const char* names[12] =
            {"C","C#","D","D#","E","F","F#","G","G#","A","A#","B"};
        std::cout << "\n=== missed notes (" << missed.size() << ") ===\n";
        for (const ChartNote* cn : missed)
        {
            const int oct = cn->midi / 12 - 1;
            std::cout << "  t=" << (cn->t + bestDelta) << "s  midi=" << cn->midi
                      << " (" << names[cn->midi % 12] << oct << ")"
                      << "  str=" << cn->string << " fret=" << cn->fret
                      << "  sus=" << cn->sus << "s\n";
        }
    }
    return 0;
}
