#pragma once

#include <SoundTouch.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_devices/juce_audio_devices.h>
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
    double getTempo() const { return tempo; }
    bool isBypassed() const;

    double getHeardPositionSec() const { return heardPositionSec; }
    void setHeardPositionSec(double seconds) { heardPositionSec = seconds; }

    // Fill `dest` with `numSamples` stereo frames at device rate.
    void process(juce::ResamplingAudioSource& resampler,
                 juce::AudioTransportSource& transport,
                 juce::AudioBuffer<float>& dest,
                 int numSamples);

private:
    void deinterleavePut(const juce::AudioBuffer<float>& buf, int numFrames);
    int pullFromResampler(juce::ResamplingAudioSource& resampler, int numFrames);

    soundtouch::SoundTouch stretch;
    double sampleRate = 48000.0;
    double tempo = 1.0;
    double heardPositionSec = 0.0;
    int maxBlockSize = 512;
    bool loggedEnabled = false;

    juce::AudioBuffer<float> resamplerPull;
    std::vector<float> interleavedIn;
    std::vector<float> interleavedOut;
};
