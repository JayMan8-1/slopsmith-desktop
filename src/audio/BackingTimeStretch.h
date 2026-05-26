#pragma once

#include <SoundTouch.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_devices/juce_audio_devices.h>
#include <array>
#include <deque>
#include <vector>

// Real-time tempo change (preserve pitch) for the JUCE backing track.
// Feeds device-rate PCM from a ResamplingAudioSource (SR conversion only)
// through SoundTouch; bypassed at tempo == 1.0 for bit-transparent playback.
class BackingTimeStretch
{
public:
    BackingTimeStretch() = default;

    void prepare(double sampleRate, int maxBlockSize);
    void reset();
    void setTempo(double tempo);
    double getTempo() const { return targetTempo; }
    bool isBypassed() const;

    double getHeardPositionSec() const { return heardPositionSec; }
    void setHeardPositionSec(double seconds) { heardPositionSec = seconds; }

    // Fill `dest` with `numSamples` stereo frames at device rate.
    void process(juce::ResamplingAudioSource& resampler,
                 juce::AudioTransportSource& transport,
                 juce::AudioBuffer<float>& dest,
                 int numSamples);

private:
    void applyProfileForTempo(double tempo);
    void advanceTempoRamp(int numSamples);
    void applyPracticeSmoothing(juce::AudioBuffer<float>& dest, int numSamples);
    void enqueueDryFrames(const juce::AudioBuffer<float>& buf, int numFrames);
    void trimDryFifo();
    void clearDryFifo();
    void deinterleavePut(const juce::AudioBuffer<float>& buf, int numFrames);
    int pullFromResampler(juce::ResamplingAudioSource& resampler, int numFrames);

    soundtouch::SoundTouch stretch;
    double sampleRate = 48000.0;
    double targetTempo = 1.0;
    double activeStretchTempo = 1.0;
    double heardPositionSec = 0.0;
    float practiceSmoothL = 0.0f;
    float practiceSmoothR = 0.0f;
    float practiceSmoothCoeff = 0.0f;
    int maxBlockSize = 512;
    bool loggedEnabled = false;
    bool nearUnityProfileActive = false;
    bool profileApplied = false;

    juce::AudioBuffer<float> resamplerPull;
    std::vector<float> interleavedIn;
    std::vector<float> interleavedOut;
    std::deque<std::array<float, 2>> dryFifo;
    size_t dryFifoMaxFrames = 48000;
};
