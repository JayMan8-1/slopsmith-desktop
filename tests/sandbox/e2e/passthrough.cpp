// Minimal passthrough VST3 fixture for the sandbox e2e test: doubles its
// input (×2) so the test can prove audio flowed host→sandbox→plugin→host,
// stores a 4-byte state blob so getState/setState round-trips are observable,
// and exposes a trivial editor so the editor open/close path is exercisable.
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

// Trivial fixed-size editor — enough for the sandbox child to create a
// top-level window and round-trip the open/close protocol.
class PassEditor : public juce::AudioProcessorEditor
{
public:
    explicit PassEditor(juce::AudioProcessor& p) : juce::AudioProcessorEditor(p)
    { setSize(320, 200); }
    void paint(juce::Graphics& g) override { g.fillAll(juce::Colours::black); }
};

class PassThrough : public juce::AudioProcessor
{
public:
    PassThrough()
        : juce::AudioProcessor(BusesProperties()
              .withInput("In",  juce::AudioChannelSet::stereo(), true)
              .withOutput("Out", juce::AudioChannelSet::stereo(), true)) {}

    const juce::String getName() const override { return "SlopPassThrough"; }
    void prepareToPlay(double, int) override {}
    void releaseResources() override {}
    void processBlock(juce::AudioBuffer<float>& b, juce::MidiBuffer&) override
    {
        b.applyGain(2.0f);   // ×2 — the e2e asserts output == 2 * input
    }
    double getTailLengthSeconds() const override { return 0.0; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    juce::AudioProcessorEditor* createEditor() override { return new PassEditor(*this); }
    bool hasEditor() const override { return true; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}
    void getStateInformation(juce::MemoryBlock& d) override { d.append("SLOP", 4); }
    void setStateInformation(const void*, int) override {}

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PassThrough)
};

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new PassThrough(); }
