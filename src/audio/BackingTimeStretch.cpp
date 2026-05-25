#include "BackingTimeStretch.h"
#include <juce_core/juce_core.h>
#include <cmath>
#include <iostream>

namespace
{
constexpr double kTempoBypassEpsilon = 1.0e-4;

// V1 MUSICIAN PRACTICE PROFILE (locked) — realtime SoundTouch tuning for
// preserve-pitch practice slowdown. Do not tweak without a deliberate V2 pass.
//   SETTING_USE_QUICKSEEK = 0
//   SETTING_USE_AA_FILTER = 1
//   SETTING_SEQUENCE_MS   = 60
//   SETTING_SEEKWINDOW_MS = 20
//   SETTING_OVERLAP_MS    = 12
void configureSoundTouchForMusic(soundtouch::SoundTouch& stretch)
{
    stretch.setSetting(SETTING_USE_QUICKSEEK, 0);
    stretch.setSetting(SETTING_USE_AA_FILTER, 1);
    stretch.setSetting(SETTING_SEQUENCE_MS, 60);
    stretch.setSetting(SETTING_SEEKWINDOW_MS, 20);
    stretch.setSetting(SETTING_OVERLAP_MS, 12);
}
}

void BackingTimeStretch::prepare(double sampleRateIn, int maxBlockSizeIn)
{
    sampleRate = sampleRateIn > 0.0 ? sampleRateIn : 48000.0;
    maxBlockSize = juce::jmax(64, maxBlockSizeIn);
    stretch.setSampleRate(static_cast<uint>(sampleRate + 0.5));
    stretch.setChannels(2);
    configureSoundTouchForMusic(stretch);
    stretch.setTempo(static_cast<float>(tempo));
    resamplerPull.setSize(2, maxBlockSize * 2, false, false, true);
    interleavedIn.resize(static_cast<size_t>(maxBlockSize) * 4u);
    interleavedOut.resize(static_cast<size_t>(maxBlockSize) * 2u);
    if (!loggedEnabled)
    {
        std::cerr << "[AudioEngine] SoundTouch enabled" << std::endl;
        DBG("[AudioEngine] SoundTouch tuned for music playback");
        loggedEnabled = true;
    }
}

void BackingTimeStretch::reset()
{
    stretch.clear();
}

void BackingTimeStretch::setTempo(double newTempo)
{
    if (!std::isfinite(newTempo) || newTempo <= 0.0)
        return;
    tempo = newTempo;
    stretch.setTempo(static_cast<float>(tempo));
    std::cerr << "[AudioEngine] setTempo(" << tempo << ")" << std::endl;
    if (!isBypassed())
        std::cerr << "[AudioEngine] timestretch active" << std::endl;
}

bool BackingTimeStretch::isBypassed() const
{
    return std::abs(tempo - 1.0) < kTempoBypassEpsilon;
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

    if (isBypassed())
    {
        juce::AudioSourceChannelInfo info(&dest, 0, numSamples);
        resampler.getNextAudioBlock(info);
        heardPositionSec = transport.getCurrentPosition();
        return;
    }

    int outFrames = 0;
    const int maxFeedPerLoop = juce::jmax(64, maxBlockSize);
    int idleFeeds = 0;

    while (outFrames < numSamples)
    {
        const uint ready = stretch.numSamples();
        if (ready < static_cast<uint>(numSamples - outFrames))
        {
            if (!transport.isPlaying())
                break;

            const int deficit = numSamples - outFrames;
            const int toPull = juce::jlimit(
                64,
                maxFeedPerLoop,
                static_cast<int>(std::ceil(static_cast<double>(deficit) * tempo * 1.5)) + 32);
            pullFromResampler(resampler, toPull);
            ++idleFeeds;
            if (stretch.numSamples() == ready && idleFeeds > 8)
                break;
        }
        else
        {
            idleFeeds = 0;
        }

        if ((int) interleavedOut.size() < (numSamples - outFrames) * 2)
            interleavedOut.resize(static_cast<size_t>(numSamples - outFrames) * 2u);

        const uint maxRecv = static_cast<uint>(numSamples - outFrames);
        const uint received = stretch.receiveSamples(interleavedOut.data(), maxRecv);
        if (received == 0)
            break;

        for (uint i = 0; i < received; ++i)
        {
            const int idx = outFrames + static_cast<int>(i);
            dest.setSample(0, idx, interleavedOut[static_cast<size_t>(i) * 2u]);
            if (dest.getNumChannels() > 1)
                dest.setSample(1, idx, interleavedOut[static_cast<size_t>(i) * 2u + 1u]);
        }
        outFrames += static_cast<int>(received);
    }

    if (outFrames < numSamples)
        dest.clear(0, outFrames, numSamples - outFrames);

    // Output frames play over (outFrames / sampleRate) wall seconds; at tempo T
    // the song timeline advances T times slower than real time.
    heardPositionSec += (static_cast<double>(outFrames) / sampleRate) * tempo;
}
