// A minimal MPE sine synth built on JUCE's MPESynthesiser. It's only used by the developer
// harness, as an independent MPE implementation to check MPE Roll's output against.

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>

namespace
{

class SineVoice : public juce::MPESynthesiserVoice
{
public:
    void noteStarted() override
    {
        phase = 0.0;
        level = 0.1f + 0.2f * currentlyPlayingNote.noteOnVelocity.asUnsignedFloat();
    }

    void noteStopped (bool) override                { clearCurrentNote(); }
    void notePressureChanged() override             {}
    void notePitchbendChanged() override            {}
    void noteTimbreChanged() override               {}
    void noteKeyStateChanged() override             {}

    using MPESynthesiserVoice::renderNextBlock;

    void renderNextBlock (juce::AudioBuffer<float>& output, int startSample, int numSamples) override
    {
        if (! isActive())
            return;

        const auto increment = juce::MathConstants<double>::twoPi * currentlyPlayingNote.getFrequencyInHertz()
                             / getSampleRate();

        for (int i = 0; i < numSamples; ++i)
        {
            const auto sample = (float) std::sin (phase) * level;
            phase += increment;

            for (int ch = 0; ch < output.getNumChannels(); ++ch)
                output.addSample (ch, startSample + i, sample);
        }
    }

private:
    double phase = 0.0;
    float level = 0.2f;
};

class TestSynthProcessor : public juce::AudioProcessor
{
public:
    TestSynthProcessor()
        : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true))
    {
        for (int i = 0; i < 16; ++i)
            synth.addVoice (new SineVoice());
    }

    void prepareToPlay (double sampleRate, int) override        { synth.setCurrentPlaybackSampleRate (sampleRate); }
    void releaseResources() override                            {}

    void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) override
    {
        buffer.clear();
        synth.renderNextBlock (buffer, midi, 0, buffer.getNumSamples());
    }

    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override         { return nullptr; }
    bool hasEditor() const override                             { return false; }
    const juce::String getName() const override                 { return "MPE Roll Test Synth"; }
    bool acceptsMidi() const override                           { return true; }
    bool producesMidi() const override                          { return false; }
    double getTailLengthSeconds() const override                { return 0.0; }
    int getNumPrograms() override                               { return 1; }
    int getCurrentProgram() override                            { return 0; }
    void setCurrentProgram (int) override                       {}
    const juce::String getProgramName (int) override            { return {}; }
    void changeProgramName (int, const juce::String&) override  {}
    void getStateInformation (juce::MemoryBlock&) override      {}
    void setStateInformation (const void*, int) override        {}

private:
    juce::MPESynthesiser synth;
};

} // namespace

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new TestSynthProcessor();
}
