// A plain MIDI test synth, with no MPE: every note on a MIDI channel shares that channel's pitch
// bend, as on most synths. It's only used by the developer harness, to show that playing one copy
// of a synth per note still gives each note a glide of its own.
//
// It's written by hand rather than with JUCE's synthesiser classes, so what it does with each
// message is plain to see. Note On on any channel starts a voice; pitch bend, its range (RPN 0) and
// the sustain pedal belong to the channel; All Notes Off and All Sound Off act on their channel.

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>

#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <stdexcept>

namespace
{

enum class Waveform
{
    sine,
    square,
    saw
};

constexpr std::array<const char*, 4> programNames { "Sine", "Square", "Saw", "Square (quiet)" };
constexpr int quietProgram = 3;         // the square wave, set without telling the host

constexpr int stateMagic = 0x6e616c50;  // the state starts with the bytes "Plan"
constexpr int stateVersion = 2;
constexpr int numParameters = 6;

/** What a MIDI channel holds for every note on it. */
struct Channel
{
    int pitchBend = 8192;
    int rangeSemitones = 2;
    int rangeCents = 0;
    int rpnMsb = 127, rpnLsb = 127;     // the RPN that Data Entry changes: 127, 127 is none
    bool sustain = false;

    void setBendRange (int semitones) noexcept
    {
        rangeSemitones = semitones;
        rangeCents = 0;
    }

    bool isBendRangeSelected() const noexcept   { return rpnMsb == 0 && rpnLsb == 0; }

    double getBendInSemitones() const noexcept
    {
        const auto range = rangeSemitones + rangeCents / 100.0;
        const auto offset = pitchBend - 8192;
        return offset >= 0 ? offset / 8191.0 * range : offset / 8192.0 * range;
    }
};

/** One sounding note: a key on a channel, with its oscillator and envelope. */
struct Voice
{
    int channel = 0;            // 0 to 15
    int key = 0;
    uint64_t started = 0;       // when it started, so that the oldest is the one stolen
    float level = 0.0f;         // from its velocity: Gain is applied as it plays
    float envelope = 0.0f;
    float releaseStep = 0.0f;   // how far the envelope falls each sample once it's released
    double phase = 0.0;         // 0 to 1 through the waveform
    bool active = false;
    bool keyDown = false;
    bool sustained = false;     // its key is up, but the sustain pedal holds it
    bool releasing = false;
};

float oscillator (Waveform shape, double phase) noexcept
{
    switch (shape)
    {
        case Waveform::square:  return phase < 0.5 ? 1.0f : -1.0f;
        case Waveform::saw:     return (float) (2.0 * phase - 1.0);
        case Waveform::sine:    break;
    }

    return (float) std::sin (juce::MathConstants<double>::twoPi * phase);
}

/** Crashes on purpose, to check that a host survives a synth that crashes. The pointer is read
    through a volatile, so the compiler can't tell it's null and leave the write out.
*/
void crashOnPurpose() noexcept
{
    int* volatile nowhere = nullptr;
    *static_cast<volatile int*> (nowhere) = 1;
}

class PlainTestSynthProcessor : public juce::AudioProcessor,
                                private juce::AudioProcessorParameter::Listener
{
public:
    PlainTestSynthProcessor()
        : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true))
    {
        addParameter (gain = new juce::AudioParameterFloat (juce::ParameterID ("gain", 1), "Gain", 0.0f, 1.0f, 1.0f));
        addParameter (bendRange = new juce::AudioParameterInt (juce::ParameterID ("bendrange", 1), "Bend range", 0, 48, 2));
        addParameter (release = new juce::AudioParameterFloat (juce::ParameterID ("release", 1), "Release",
                                                               0.0f, 2.0f, 0.05f));
        addParameter (crash = new juce::AudioParameterBool (juce::ParameterID ("crash", 1), "Crash", false));
        addParameter (throwing = new juce::AudioParameterBool (juce::ParameterID ("throw", 1), "Throw", false));

        // Stands in for a synth's own pitch-wheel parameter (Serum 1 has one, and a Mod Wheel): it
        // does nothing here.
        addParameter (pitchWheel = new juce::AudioParameterFloat (juce::ParameterID ("pitchbend", 1), "Pitch Bend",
                                                                  0.0f, 1.0f, 0.5f));

        jassert (getParameters().size() == numParameters);

        for (auto* p : getParameters())
            p->addListener (this);
    }

    ~PlainTestSynthProcessor() override
    {
        for (auto* p : getParameters())
            p->removeListener (this);
    }

    void prepareToPlay (double newSampleRate, int) override
    {
        notedIfCrashed ("prepareToPlay");
        samplesPerSecond = newSampleRate > 0.0 ? newSampleRate : 44100.0;
        attackStep = (float) (1.0 / (0.002 * samplesPerSecond));
        appliedBendRange = bendRange->get();

        for (auto& voice : voices)
            voice = {};

        for (auto& channel : channels)
        {
            channel = {};
            channel.setBendRange (appliedBendRange);
        }
    }

    void releaseResources() override                            { notedIfCrashed ("releaseResources"); }

    void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) override
    {
        notedIfCrashed ("processBlock");

        if (crash->get())
        {
            crashed = true;
            crashOnPurpose();
        }

        // A C++ exception that escapes, which a host can catch on every system.
        if (throwing->get())
        {
            crashed = true;
            throw std::runtime_error ("MPE Roll Plain Test Synth: thrown on purpose");
        }

        buffer.clear();

        // Changing the Bend range parameter sets every channel to it, whatever RPN 0 said before.
        if (const auto range = bendRange->get(); range != appliedBendRange)
        {
            appliedBendRange = range;

            for (auto& channel : channels)
                channel.setBendRange (range);
        }

        const auto shape = waveform.load();
        const auto gainNow = gain->get();
        const auto numSamples = buffer.getNumSamples();
        int done = 0;

        for (const auto metadata : midi)
        {
            const auto position = juce::jlimit (done, numSamples, metadata.samplePosition);
            render (buffer, done, position - done, shape, gainNow);
            done = position;
            handleMidi (metadata.data, metadata.numBytes);
        }

        render (buffer, done, numSamples - done, shape, gainNow);
    }

    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override         { return nullptr; }
    bool hasEditor() const override                             { return false; }
    const juce::String getName() const override                 { return "MPE Roll Plain Test Synth"; }
    bool acceptsMidi() const override                           { return true; }
    bool producesMidi() const override                          { return false; }
    double getTailLengthSeconds() const override                { return release->get(); }
    int getNumPrograms() override                               { return (int) programNames.size(); }
    int getCurrentProgram() override                            { return currentProgram.load(); }

    void setCurrentProgram (int index) override
    {
        if (! juce::isPositiveAndBelow (index, getNumPrograms()))
            return;

        currentProgram = index;
        waveform = index == quietProgram ? Waveform::square : (Waveform) index;

        // The quiet one doesn't tell the host, to check what happens when a synth changes unseen.
        if (index != quietProgram)
            updateHostDisplay (juce::AudioProcessorListener::ChangeDetails().withProgramChanged (true));
    }

    const juce::String getProgramName (int index) override
    {
        return juce::isPositiveAndBelow (index, getNumPrograms()) ? juce::String (programNames[(size_t) index])
                                                                  : juce::String();
    }

    void changeProgramName (int, const juce::String&) override  {}

    void getStateInformation (juce::MemoryBlock& data) override
    {
        notedIfCrashed ("getStateInformation");
        juce::MemoryOutputStream out (data, false);
        out.writeInt (stateMagic);
        out.writeInt (stateVersion);
        out.writeInt ((int) waveform.load());
        out.writeInt (currentProgram.load());

        for (auto* p : getParameters())
            out.writeFloat (p->getValue());
    }

    void setStateInformation (const void* data, int size) override
    {
        notedIfCrashed ("setStateInformation");
        const auto& parameters = getParameters();
        const auto expectedSize = 4 * (int) sizeof (int) + numParameters * (int) sizeof (float);

        if (data == nullptr || size < expectedSize || parameters.size() != numParameters)
            return;

        juce::MemoryInputStream in (data, (size_t) size, false);

        if (in.readInt() != stateMagic || in.readInt() != stateVersion)
            return;

        const auto shape = in.readInt();
        const auto program = in.readInt();

        if (! juce::isPositiveAndNotGreaterThan (shape, (int) Waveform::saw)
            || ! juce::isPositiveAndBelow (program, getNumPrograms()))
            return;

        std::array<float, numParameters> values {};

        for (auto& value : values)
        {
            value = in.readFloat();

            if (! (value >= 0.0f && value <= 1.0f))
                return;
        }

        waveform = (Waveform) shape;
        currentProgram = program;

        for (int i = 0; i < numParameters; ++i)
            parameters[i]->setValueNotifyingHost (values[(size_t) i]);
    }

private:
    juce::AudioParameterFloat* gain = nullptr;
    juce::AudioParameterInt* bendRange = nullptr;
    juce::AudioParameterFloat* release = nullptr;
    juce::AudioParameterBool* crash = nullptr;
    juce::AudioParameterBool* throwing = nullptr;
    juce::AudioParameterFloat* pitchWheel = nullptr;
    std::atomic<bool> crashed { false };    // it crashed on purpose: a host mustn't call it again

    /** For the harness: a call a host makes after this synth crashed (it never should) is noted
        in the file MPEROLL_TEST_AFTER_CRASH names. */
    void notedIfCrashed (const char* what) const
    {
        if (! crashed.load())
            return;

        const auto path = juce::SystemStats::getEnvironmentVariable ("MPEROLL_TEST_AFTER_CRASH", {});

        if (path.isNotEmpty())
            juce::File (path).appendText (juce::String (what) + "\n");
    }

    void parameterValueChanged (int, float) override            { notedIfCrashed ("a parameter change"); }
    void parameterGestureChanged (int, bool) override           {}

    std::atomic<Waveform> waveform { Waveform::sine };
    std::atomic<int> currentProgram { 0 };

    std::array<Voice, 16> voices;
    std::array<Channel, 16> channels;
    uint64_t clock = 0;
    int appliedBendRange = 2;
    double samplesPerSecond = 44100.0;
    float attackStep = 1.0f / (0.002f * 44100.0f);

    void handleMidi (const juce::uint8* data, int numBytes) noexcept
    {
        // Only notes, controllers and pitch bend matter here, and each has two data bytes. Anything
        // else, or anything cut short, is passed over.
        if (numBytes < 3 || data[0] < 0x80 || data[0] >= 0xf0)
            return;

        const auto channel = data[0] & 0x0f;
        const auto first = data[1] & 0x7f;
        const auto second = data[2] & 0x7f;

        switch (data[0] & 0xf0)
        {
            case 0x80:
                stopNote (channel, first);
                break;

            case 0x90:
                if (second > 0)
                    startNote (channel, first, second);
                else
                    stopNote (channel, first);

                break;

            case 0xb0:
                handleController (channels[(size_t) channel], channel, first, second);
                break;

            case 0xe0:
                channels[(size_t) channel].pitchBend = first | (second << 7);
                break;

            default:
                break;
        }
    }

    void handleController (Channel& state, int channel, int controller, int value) noexcept
    {
        switch (controller)
        {
            case 6:
                if (state.isBendRangeSelected())
                    state.setBendRange (value);

                break;

            case 38:
                if (state.isBendRangeSelected())
                    state.rangeCents = value;

                break;

            case 64:
                state.sustain = value >= 64;

                if (! state.sustain)
                    for (auto& voice : voices)
                        if (voice.active && voice.channel == channel && voice.sustained)
                            releaseVoice (voice);

                break;

            case 98:
            case 99:
                // An NRPN: Data Entry is for that now, not the bend range.
                state.rpnMsb = state.rpnLsb = 127;
                break;

            case 100:
                state.rpnLsb = value;
                break;

            case 101:
                state.rpnMsb = value;
                break;

            case 120:
                for (auto& voice : voices)
                    if (voice.channel == channel)
                        voice.active = false;

                break;

            case 123:
                for (auto& voice : voices)
                    if (voice.active && voice.channel == channel && ! voice.releasing)
                        releaseVoice (voice);

                break;

            default:
                break;
        }
    }

    void startNote (int channel, int key, int velocity) noexcept
    {
        // The same key again on the same channel: the note playing it fades as the new one starts.
        for (auto& voice : voices)
            if (voice.active && voice.keyDown && voice.channel == channel && voice.key == key)
                releaseVoice (voice);

        // A free voice, or else the oldest.
        auto* chosen = &voices.front();

        for (auto& voice : voices)
        {
            if (! voice.active)
            {
                chosen = &voice;
                break;
            }

            if (voice.started < chosen->started)
                chosen = &voice;
        }

        *chosen = {};
        chosen->channel = channel;
        chosen->key = key;
        chosen->started = ++clock;
        chosen->level = 0.1f + 0.2f * (float) velocity / 127.0f;
        chosen->active = true;
        chosen->keyDown = true;
    }

    void stopNote (int channel, int key) noexcept
    {
        const auto sustain = channels[(size_t) channel].sustain;

        for (auto& voice : voices)
        {
            if (voice.active && voice.keyDown && voice.channel == channel && voice.key == key)
            {
                if (sustain)
                {
                    voice.keyDown = false;
                    voice.sustained = true;
                }
                else
                {
                    releaseVoice (voice);
                }
            }
        }
    }

    void releaseVoice (Voice& voice) noexcept
    {
        // It falls from wherever it is to silence over the Release time.
        const auto samples = (double) release->get() * samplesPerSecond;

        voice.keyDown = false;
        voice.sustained = false;
        voice.releasing = true;
        voice.releaseStep = samples > 1.0 ? (float) (voice.envelope / samples) : voice.envelope;
    }

    void render (juce::AudioBuffer<float>& buffer, int start, int numSamples, Waveform shape, float gainNow) noexcept
    {
        if (numSamples <= 0)
            return;

        for (auto& voice : voices)
        {
            if (! voice.active)
                continue;

            // Every note on a channel shares its bend: that's what makes this synth plain MIDI.
            const auto pitch = voice.key - 69 + channels[(size_t) voice.channel].getBendInSemitones();
            const auto increment = 440.0 * std::pow (2.0, pitch / 12.0) / samplesPerSecond;

            for (int i = 0; i < numSamples; ++i)
            {
                if (voice.releasing)
                {
                    voice.envelope -= voice.releaseStep;

                    if (voice.envelope <= 0.0f)
                    {
                        voice.active = false;
                        break;
                    }
                }
                else
                {
                    voice.envelope = std::min (1.0f, voice.envelope + attackStep);
                }

                const auto sample = oscillator (shape, voice.phase) * voice.level * gainNow * voice.envelope;
                voice.phase += increment;
                voice.phase -= std::floor (voice.phase);

                for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
                    buffer.addSample (ch, start + i, sample);
            }
        }
    }
};

} // namespace

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new PlainTestSynthProcessor();
}
