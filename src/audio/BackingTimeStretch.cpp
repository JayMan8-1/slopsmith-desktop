#include "BackingTimeStretch.h"
#include <juce_core/juce_core.h>
#include <array>
#include <cmath>
#include <deque>
#include <iostream>

namespace
{
constexpr double kTempoBypassEpsilon = 1.0e-4;
constexpr double kNearUnityTempoThreshold = 0.80;
constexpr double kTempoRampSec = 0.07;

// Shared across profiles (V1 musician practice).
constexpr int kUseQuickSeek = 0;
constexpr int kUseAaFilter = 1;

// Deep slowdown (tempo < 0.80): longer WSOLA windows — stable at 50–60%.
constexpr int kDeepSequenceMs = 60;
constexpr int kDeepSeekWindowMs = 20;
constexpr int kDeepOverlapMs = 12;

// Near-unity (tempo >= 0.80): shorter windows — less modulation at 90–75%.
constexpr int kNearUnitySequenceMs = 40;
constexpr int kNearUnitySeekWindowMs = 15;
constexpr int kNearUnityOverlapMs = 8;

// Subtle HF softening on stretch output (psychoacoustic flutter masking).
constexpr bool kPracticeSmoothingEnabled = true;
constexpr float kPracticeSmoothMix = 0.12f;
constexpr double kPracticeSmoothCutoffHz = 11000.0;

enum class StretchProfile { NearUnity, Deep };

StretchProfile profileForTempo(double tempo)
{
    return tempo >= kNearUnityTempoThreshold ? StretchProfile::NearUnity
                                             : StretchProfile::Deep;
}

void applyStretchProfile(soundtouch::SoundTouch& stretch, StretchProfile profile)
{
    stretch.setSetting(SETTING_USE_QUICKSEEK, kUseQuickSeek);
    stretch.setSetting(SETTING_USE_AA_FILTER, kUseAaFilter);
    if (profile == StretchProfile::NearUnity)
    {
        stretch.setSetting(SETTING_SEQUENCE_MS, kNearUnitySequenceMs);
        stretch.setSetting(SETTING_SEEKWINDOW_MS, kNearUnitySeekWindowMs);
        stretch.setSetting(SETTING_OVERLAP_MS, kNearUnityOverlapMs);
    }
    else
    {
        stretch.setSetting(SETTING_SEQUENCE_MS, kDeepSequenceMs);
        stretch.setSetting(SETTING_SEEKWINDOW_MS, kDeepSeekWindowMs);
        stretch.setSetting(SETTING_OVERLAP_MS, kDeepOverlapMs);
    }
}

float practiceSmoothOnePoleCoeff(double sampleRate)
{
    if (sampleRate <= 0.0)
        return 0.0f;
    return static_cast<float>(std::exp(-2.0 * juce::MathConstants<double>::pi
                                       * kPracticeSmoothCutoffHz / sampleRate));
}

// Near-unity dry blend (pre-stretch input aligned via FIFO). 80% and below: 0%.
double nearUnityDryBlend(double tempo)
{
    if (tempo < 0.80 || tempo >= 1.0 - kTempoBypassEpsilon)
        return 0.0;
    if (tempo >= 0.90)
    {
        const double t = (tempo - 0.90) / (0.10 - kTempoBypassEpsilon);
        return 0.15 * (1.0 - juce::jlimit(0.0, 1.0, t));
    }
    if (tempo >= 0.85)
    {
        const double t = (tempo - 0.85) / 0.05;
        return 0.09 + (0.15 - 0.09) * t;
    }
    const double t = (tempo - 0.80) / 0.05;
    return 0.09 * t;
}
}

void BackingTimeStretch::applyProfileForTempo(double tempoIn)
{
    const auto desired = profileForTempo(tempoIn);
    const bool wantNearUnity = desired == StretchProfile::NearUnity;
    if (profileApplied && wantNearUnity == nearUnityProfileActive)
        return;

    nearUnityProfileActive = wantNearUnity;
    profileApplied = true;
    // Profile switch changes WSOLA geometry — clear internal overlap only then.
    stretch.clear();
    clearDryFifo();
    applyStretchProfile(stretch, desired);
    // Profile geometry changed — snap tempo so SoundTouch does not stay at the old
    // active value (often 1.0) while target is already at a practice slowdown.
    activeStretchTempo = targetTempo;
    stretch.setTempo(static_cast<float>(targetTempo));

    const char* name = nearUnityProfileActive ? "near-unity" : "deep";
    std::cerr << "[AudioEngine] SoundTouch profile: " << name
              << " (tempo=" << tempoIn << ")" << std::endl;
}

void BackingTimeStretch::advanceTempoRamp(int numSamples)
{
    if (numSamples <= 0 || sampleRate <= 0.0)
        return;
    if (std::abs(activeStretchTempo - targetTempo) < 1.0e-5)
        return;

    const double alpha = 1.0 - std::exp(-static_cast<double>(numSamples)
                                        / (kTempoRampSec * sampleRate));
    activeStretchTempo += (targetTempo - activeStretchTempo) * alpha;
    if (std::abs(activeStretchTempo - targetTempo) < 1.0e-5)
        activeStretchTempo = targetTempo;

    stretch.setTempo(static_cast<float>(activeStretchTempo));
}

void BackingTimeStretch::applyPracticeSmoothing(juce::AudioBuffer<float>& dest, int numSamples)
{
    if (!kPracticeSmoothingEnabled || numSamples <= 0)
        return;

    const float mix = kPracticeSmoothMix;
    const float keep = 1.0f - mix;
    const float a = practiceSmoothCoeff;

    float* l = dest.getWritePointer(0);
    float* r = dest.getNumChannels() > 1 ? dest.getWritePointer(1) : l;

    for (int i = 0; i < numSamples; ++i)
    {
        const float inL = l[i];
        const float inR = r[i];
        practiceSmoothL += a * (inL - practiceSmoothL);
        practiceSmoothR += a * (inR - practiceSmoothR);
        l[i] = keep * inL + mix * practiceSmoothL;
        r[i] = keep * inR + mix * practiceSmoothR;
    }
}

void BackingTimeStretch::prepare(double sampleRateIn, int maxBlockSizeIn)
{
    sampleRate = sampleRateIn > 0.0 ? sampleRateIn : 48000.0;
    maxBlockSize = juce::jmax(64, maxBlockSizeIn);
    dryFifoMaxFrames = static_cast<size_t>(sampleRate);
    clearDryFifo();
    practiceSmoothCoeff = practiceSmoothOnePoleCoeff(sampleRate);
    practiceSmoothL = 0.0f;
    practiceSmoothR = 0.0f;
    stretch.setSampleRate(static_cast<uint>(sampleRate + 0.5));
    stretch.setChannels(2);
    profileApplied = false;
    applyProfileForTempo(targetTempo);
    activeStretchTempo = targetTempo;
    stretch.setTempo(static_cast<float>(activeStretchTempo));
    resamplerPull.setSize(2, maxBlockSize * 2, false, false, true);
    interleavedIn.resize(static_cast<size_t>(maxBlockSize) * 4u);
    interleavedOut.resize(static_cast<size_t>(maxBlockSize) * 2u);
    if (!loggedEnabled)
    {
        std::cerr << "[AudioEngine] SoundTouch enabled (tempo ramp "
                  << static_cast<int>(kTempoRampSec * 1000.0) << "ms)" << std::endl;
        DBG("[AudioEngine] SoundTouch tuned for music playback");
        loggedEnabled = true;
    }
}

void BackingTimeStretch::clearDryFifo()
{
    dryFifo.clear();
}

void BackingTimeStretch::trimDryFifo()
{
    while (dryFifo.size() > dryFifoMaxFrames)
        dryFifo.pop_front();
}

void BackingTimeStretch::enqueueDryFrames(const juce::AudioBuffer<float>& buf, int numFrames)
{
    if (nearUnityDryBlend(targetTempo) <= 0.0)
        return;

    const int n = juce::jmin(numFrames, buf.getNumSamples());
    const float* l = buf.getReadPointer(0);
    const float* r = buf.getNumChannels() > 1 ? buf.getReadPointer(1) : l;
    for (int i = 0; i < n; ++i)
        dryFifo.push_back({l[i], r[i]});
    trimDryFifo();
}

void BackingTimeStretch::reset()
{
    stretch.clear();
    clearDryFifo();
    activeStretchTempo = targetTempo;
    stretch.setTempo(static_cast<float>(activeStretchTempo));
    practiceSmoothL = 0.0f;
    practiceSmoothR = 0.0f;
}

void BackingTimeStretch::setTempo(double newTempo)
{
    if (!std::isfinite(newTempo) || newTempo <= 0.0)
        return;

    const double prevTarget = targetTempo;
    targetTempo = newTempo;
    applyProfileForTempo(targetTempo);
    activeStretchTempo = targetTempo;
    stretch.setTempo(static_cast<float>(targetTempo));

    if (std::abs(prevTarget - targetTempo) > 0.001)
    {
        if (diagPendingSpeedSummary && diagCallbackCount > 0)
            flushSpeedDiagnostics(diagTransportStartSec);

        std::cerr << "[AudioEngine] setTempo target=" << targetTempo
                  << " active=" << activeStretchTempo << std::endl;
        markSpeedChangeForDiagnostics();
    }
}

void BackingTimeStretch::markSpeedChangeForDiagnostics()
{
    diagPendingSpeedSummary = true;
    diagCallbackCount = 0;
    diagRequestedOutFrames = 0;
    diagSourceFramesPulled = 0;
    diagStretchReceived = 0;
    diagStretchReadyMax = 0;
    diagTransportStartValid = false;
}

void BackingTimeStretch::flushSpeedDiagnostics(double transportPositionSec)
{
    if (!diagPendingSpeedSummary || diagCallbackCount == 0)
        return;

    const double transportDelta = diagTransportStartValid
                                    ? transportPositionSec - diagTransportStartSec
                                    : 0.0;
    const char* pathName = "unknown";
    switch (lastOutputPath)
    {
        case StretchOutputPath::Bypass: pathName = "bypass"; break;
        case StretchOutputPath::Stretched: pathName = "stretched"; break;
        case StretchOutputPath::StretchedPartial: pathName = "stretched-partial"; break;
        case StretchOutputPath::Silence: pathName = "silence"; break;
    }

    const double callbacks = static_cast<double>(diagCallbackCount);
    std::cerr << "[AudioEngine] stretchDiag speedSummary"
              << " target=" << targetTempo
              << " active=" << activeStretchTempo
              << " callbacks=" << diagCallbackCount
              << " avgReqOut=" << (static_cast<double>(diagRequestedOutFrames) / callbacks)
              << " avgSrcPulled=" << (static_cast<double>(diagSourceFramesPulled) / callbacks)
              << " avgStretchRecv=" << (static_cast<double>(diagStretchReceived) / callbacks)
              << " maxStretchReady=" << diagStretchReadyMax
              << " transportDeltaSec=" << transportDelta
              << " lastPath=" << pathName
              << std::endl;

    diagPendingSpeedSummary = false;
}

bool BackingTimeStretch::isBypassed() const
{
    return std::abs(targetTempo - 1.0) < kTempoBypassEpsilon
           && std::abs(activeStretchTempo - 1.0) < kTempoBypassEpsilon;
}

void BackingTimeStretch::deinterleavePut(const juce::AudioBuffer<float>& buf, int numFrames)
{
    const int n = juce::jmin(numFrames, buf.getNumSamples());
    if (n <= 0)
        return;
    if ((int) interleavedIn.size() < n * 2)
        interleavedIn.resize(static_cast<size_t>(n) * 2u);
    const float* l = buf.getReadPointer(0);
    const float* r = buf.getNumChannels() > 1 ? buf.getReadPointer(1) : l;
    for (int i = 0; i < n; ++i)
    {
        interleavedIn[static_cast<size_t>(i) * 2u] = l[i];
        interleavedIn[static_cast<size_t>(i) * 2u + 1u] = r[i];
    }
    stretch.putSamples(interleavedIn.data(), static_cast<uint>(n));
    enqueueDryFrames(buf, n);
}

int BackingTimeStretch::pullFromResampler(juce::ResamplingAudioSource& resampler, int numFrames)
{
    const int n = juce::jlimit(1, maxBlockSize * 2, numFrames);
    resamplerPull.setSize(2, n, false, false, true);
    resamplerPull.clear();
    juce::AudioSourceChannelInfo info(&resamplerPull, 0, n);
    resampler.getNextAudioBlock(info);
    deinterleavePut(resamplerPull, n);
    return n;
}

void BackingTimeStretch::process(juce::ResamplingAudioSource& resampler,
                                 juce::AudioTransportSource& transport,
                                 juce::AudioBuffer<float>& dest,
                                 int numSamples)
{
    if (numSamples <= 0)
        return;

    const double transportPos = transport.getCurrentPosition();
    if (diagPendingSpeedSummary && !diagTransportStartValid)
    {
        diagTransportStartSec = transportPos;
        diagTransportStartValid = true;
    }

    if (isBypassed())
    {
        juce::AudioSourceChannelInfo info(&dest, 0, numSamples);
        resampler.getNextAudioBlock(info);
        heardPositionSec = transportPos;
        lastOutputPath = StretchOutputPath::Bypass;
        if (diagPendingSpeedSummary)
        {
            ++diagCallbackCount;
            diagRequestedOutFrames += static_cast<uint64_t>(numSamples);
            diagSourceFramesPulled += static_cast<uint64_t>(numSamples);
            diagStretchReceived += static_cast<uint64_t>(numSamples);
        }
        return;
    }

    advanceTempoRamp(numSamples);

    const double stretchTempo = juce::jmax(0.01, activeStretchTempo);
    const float dryMix = static_cast<float>(nearUnityDryBlend(targetTempo));
    const float wetMix = 1.0f - dryMix;
    int sourceFramesPulled = 0;
    int stretchFramesReceived = 0;
    uint stretchReadyMax = stretch.numSamples();
    const int maxSrcPull = maxBlockSize * 2;
    int feedIdle = 0;

    // Feed source only while stretched output buffered for this block is short.
    // SoundTouch output rate = input rate / tempo, so source frames needed ≈
    // outputDeficit * tempo. Do not pull from the resampler when ready already
    // covers the full callback — that was advancing transport at ~1x.
    while (stretch.numSamples() < static_cast<uint>(numSamples))
    {
        if (!transport.isPlaying())
            break;

        const uint ready = stretch.numSamples();
        stretchReadyMax = juce::jmax(stretchReadyMax, ready);
        const int outDeficit = numSamples - static_cast<int>(ready);
        const int srcToPull = juce::jlimit(
            1,
            maxSrcPull,
            static_cast<int>(std::ceil(static_cast<double>(outDeficit) * stretchTempo)));

        sourceFramesPulled += pullFromResampler(resampler, srcToPull);

        const uint readyAfter = stretch.numSamples();
        if (readyAfter <= ready)
        {
            if (++feedIdle > 12)
                break;
        }
        else
        {
            feedIdle = 0;
        }
    }

    stretchReadyMax = juce::jmax(stretchReadyMax, stretch.numSamples());

    if ((int) interleavedOut.size() < numSamples * 2)
        interleavedOut.resize(static_cast<size_t>(numSamples) * 2u);

    auto copyStretchToDest = [&](uint received, int destOffset) {
        for (uint i = 0; i < received; ++i)
        {
            float wetL = interleavedOut[static_cast<size_t>(i) * 2u];
            float wetR = interleavedOut[static_cast<size_t>(i) * 2u + 1u];
            if (dryMix > 0.0f && !dryFifo.empty())
            {
                const auto dry = dryFifo.front();
                dryFifo.pop_front();
                wetL = wetMix * wetL + dryMix * dry[0];
                wetR = wetMix * wetR + dryMix * dry[1];
            }
            const int idx = destOffset + static_cast<int>(i);
            dest.setSample(0, idx, wetL);
            if (dest.getNumChannels() > 1)
                dest.setSample(1, idx, wetR);
        }
    };

    uint received = stretch.receiveSamples(interleavedOut.data(),
                                           static_cast<uint>(numSamples));
    copyStretchToDest(received, 0);
    int outFrames = static_cast<int>(received);
    stretchFramesReceived = outFrames;

    // One recovery feed+receive if still short while transport is playing.
    if (outFrames < numSamples && transport.isPlaying())
    {
        const int remain = numSamples - outFrames;
        const int srcToPull = juce::jlimit(
            1,
            maxSrcPull,
            static_cast<int>(std::ceil(static_cast<double>(remain) * stretchTempo)));
        sourceFramesPulled += pullFromResampler(resampler, srcToPull);

        const uint more = stretch.receiveSamples(interleavedOut.data(),
                                                 static_cast<uint>(remain));
        copyStretchToDest(more, outFrames);
        outFrames += static_cast<int>(more);
        stretchFramesReceived = outFrames;
    }

    if (outFrames < numSamples)
        dest.clear(0, outFrames, numSamples - outFrames);

    if (outFrames > 0)
        applyPracticeSmoothing(dest, outFrames);

    // Heard position follows stretched output (musical time), not transport read head.
    heardPositionSec += (static_cast<double>(outFrames) / sampleRate) * stretchTempo;

    if (outFrames <= 0)
        lastOutputPath = StretchOutputPath::Silence;
    else if (outFrames < numSamples)
        lastOutputPath = StretchOutputPath::StretchedPartial;
    else
        lastOutputPath = StretchOutputPath::Stretched;

    if (diagPendingSpeedSummary)
    {
        ++diagCallbackCount;
        diagRequestedOutFrames += static_cast<uint64_t>(numSamples);
        diagSourceFramesPulled += static_cast<uint64_t>(sourceFramesPulled);
        diagStretchReceived += static_cast<uint64_t>(stretchFramesReceived);
        diagStretchReadyMax =
            juce::jmax<uint64_t>(diagStretchReadyMax,
                                 static_cast<uint64_t>(stretchReadyMax));

        if (diagCallbackCount >= 480)
            flushSpeedDiagnostics(transport.getCurrentPosition());
    }
}
