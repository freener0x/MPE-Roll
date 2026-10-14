#pragma once

#include "ClipDocument.h"
#include "HostedSynth.h"
#include "SynthParameters.h"
#include "core/ClipBuilder.h"
#include "core/ClipPlayer.h"
#include "core/MpeDecoder.h"
#include "core/MpeEncoder.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <atomic>

namespace mperoll
{

/** MPE Roll: an MPE piano roll that plays a hosted synth.

    Audio thread: reads the latest EngineSnapshot, plays the clip in sync with the host (or
    triggered by notes), passes incoming MIDI/MPE through to the synth and timestamps it for
    recording. Message thread: owns the ClipDocument and the HostedSynth, and turns recorded MIDI
    into notes.
*/
class MpeRollProcessor : public juce::AudioProcessor,
                         private juce::Timer
{
public:
    MpeRollProcessor();
    ~MpeRollProcessor() override;

    //==============================================================================
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override                     { return true; }

    const juce::String getName() const override         { return JucePlugin_Name; }
    bool acceptsMidi() const override                   { return true; }
    bool producesMidi() const override                  { return true; }
    bool isMidiEffect() const override                  { return false; }
    double getTailLengthSeconds() const override;

    int getNumPrograms() override                       { return 1; }
    int getCurrentProgram() override                    { return 0; }
    void setCurrentProgram (int) override               {}
    const juce::String getProgramName (int) override    { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    //==============================================================================
    ClipDocument& getDocument() noexcept                { return document; }
    HostedSynth& getSynth() noexcept                    { return synth; }
    SynthParameterBank& getSynthParameters() noexcept   { return synthParameters; }

    /** Plays a note on the hosted synth from the UI (piano keys, note previews). */
    void auditionNote (int key, float velocity, bool isNoteOn);

    /** Sends the MPE Configuration Message and bend ranges to the synth on the next block. */
    void requestConfiguration() noexcept                { configurationRequested = true; }

    void setRecordArmed (bool shouldRecord);
    bool isRecordArmed() const noexcept                 { return recordArmed.load(); }

    /** The take being recorded right now, for drawing (message thread). */
    const ClipBuilder& getRecordingPreview() const noexcept { return recordBuilder; }
    int getRecordingSlot() const noexcept               { return recordSlot; }

    struct Transport
    {
        bool playing = false;
        int slot = -1;          // the clip slot being played, -1 if none
        double clipBeat = 0.0;  // playhead position within that clip
        double bpm = 120.0;
    };

    Transport getTransport() const noexcept;

    /** Status messages for the UI (synth load errors...). */
    juce::String getStatusMessage() const               { return statusMessage; }
    std::function<void()> onStatusChanged;

private:
    //==============================================================================
    struct LiveRouter : MpeDecoder::Listener
    {
        MpeEncoder* encoder = nullptr;
        juce::MidiBuffer* out = nullptr;
        int samplePos = 0;

        void handleEvent (const MpeDecoder::Event&) override;
    };

    struct RecordEvent
    {
        enum class Kind : uint8_t { midi, takeEnd };

        Kind kind = Kind::midi;
        uint8_t size = 0;
        uint8_t bytes[3] {};
        int8_t slot = 0;
        double clipBeat = 0.0;
        double elapsedBeat = 0.0;
    };

    struct AuditionEvent
    {
        int key = 60;
        float velocity = 0.8f;
        bool on = true;
    };

    //==============================================================================
    // Snapshot hand-over from the message thread to the audio thread. Declared before the document,
    // which publishes its first snapshot while being constructed.
    juce::SpinLock snapshotLock;
    std::shared_ptr<const EngineSnapshot> publishedSnapshot;
    std::atomic<uint64_t> publishedVersion { 0 };
    std::shared_ptr<const EngineSnapshot> audioSnapshot;
    std::vector<std::shared_ptr<const EngineSnapshot>> releasePool;

    ClipDocument document;
    HostedSynth synth;
    SynthParameterBank synthParameters { *this, synth };   // after the synth: it detaches first on destruction
    std::shared_ptr<int> lifetime = std::make_shared<int> (0);

    // Audio thread state.
    MpeEncoder encoder;
    MpeDecoder liveDecoder;
    LiveRouter liveRouter;
    ClipPlayer player { 1 };
    juce::MidiBuffer synthMidi, hostMidi;
    double currentSampleRate = 44100.0;
    OutputSettings lastOutputSettings;
    InputSettings lastInputSettings;
    PlaybackSettings::Mode lastPlaybackMode = PlaybackSettings::Mode::followHost;
    bool needsConfiguration = true;
    std::atomic<bool> configurationRequested { true };
    bool followPlaying = false;
    double expectedBeat = 0.0;
    int playingSlot = -1;
    int triggeredSlot = -1;
    int triggerKey = -1;
    double triggerBeat = 0.0;
    bool recordingTake = false;
    double recordElapsed = 0.0;

    // UI -> audio
    juce::AbstractFifo auditionFifo { 256 };
    std::array<AuditionEvent, 256> auditionEvents;
    std::array<bool, 128> auditionHeld {};

    // audio -> message thread
    juce::AbstractFifo recordFifo { 1 << 16 };
    std::vector<RecordEvent> recordEvents;
    std::atomic<bool> recordArmed { false };
    std::atomic<bool> transportPlaying { false };
    std::atomic<int> transportSlot { -1 };
    std::atomic<double> transportBeat { 0.0 };
    std::atomic<double> transportBpm { 120.0 };

    // Message thread recording.
    MpeDecoder recordDecoder;
    ClipBuilder recordBuilder;
    int recordSlot = -1;
    juce::String statusMessage;

    //==============================================================================
    void publish (std::shared_ptr<const EngineSnapshot> snapshot);
    void timerCallback() override;
    void drainRecording();
    void finishTake();
    void setStatus (const juce::String& message);
    void updateLatency();

    // processBlock helpers
    void applySettings (const EngineSnapshot&, int numSamples);
    void handleAudition();
    void handleInput (const juce::MidiMessage&, int samplePos, const EngineSnapshot&, double bps, double blockStartBeat);
    void renderPlayback (const EngineSnapshot&, int from, int to, double bps, double blockStartBeat);
    void pushRecordEvent (const RecordEvent&);
    double clipBeatAt (const EngineSnapshot&, int slot, double rawBeat) const;
    bool clockRunning() const noexcept;
    void startTrigger (int slot, int key, int samplePos);
    void stopTrigger (int samplePos);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MpeRollProcessor)
};

} // namespace mperoll
