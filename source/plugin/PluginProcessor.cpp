#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "CrashReport.h"

#include <cmath>
#include <cstring>
#include <optional>

namespace mperoll
{

namespace
{
    constexpr uint32_t liveInputTag = 2;
    constexpr uint32_t auditionTag = 3;
    constexpr double continuityTolerance = 1.0e-3;   // beats

    MpeEncoder::Handle makeHandle (uint32_t tag, uint32_t id) noexcept
    {
        return ((uint64_t) tag << 32) | id;
    }

    void mergeLane (ExpressionCurve& target, const ExpressionCurve& recorded)
    {
        if (recorded.isEmpty())
            return;

        target.replaceRange (recorded.getPoints().front().beat, recorded.getPoints().back().beat, recorded.getPoints());
    }
}

//==============================================================================
void MpeRollProcessor::LiveRouter::handleEvent (const MpeDecoder::Event& e)
{
    using Type = MpeDecoder::Event::Type;
    const auto handle = makeHandle (liveInputTag, e.noteId);

    switch (e.type)
    {
        case Type::notePrepare:     encoder->prepareNote (*out, samplePos, handle, e.key); break;
        case Type::noteStart:       encoder->startNote (*out, samplePos, handle, e.value); break;
        case Type::noteStop:        encoder->stopNote (*out, samplePos, handle, e.value); break;
        case Type::notePitch:       encoder->setPitch (*out, samplePos, handle, e.value); break;
        case Type::notePressure:    encoder->setPressure (*out, samplePos, handle, e.value); break;
        case Type::noteTimbre:      encoder->setTimbre (*out, samplePos, handle, e.value); break;
        case Type::noteController:  encoder->setController (*out, samplePos, handle, e.controller, e.value); break;
        case Type::zonePitch:       encoder->setZonePitch (*out, samplePos, e.value); break;
        case Type::zonePressure:    encoder->setZonePressure (*out, samplePos, e.value); break;
        case Type::zoneTimbre:      encoder->setZoneTimbre (*out, samplePos, e.value); break;
        case Type::zoneController:  encoder->setZoneController (*out, samplePos, e.controller, e.value); break;
    }
}

//==============================================================================
MpeRollProcessor::MpeRollProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      document ([this] (std::shared_ptr<const EngineSnapshot> s) { publish (std::move (s)); })
{
    // The standalone app reports its own crashes; inside a DAW, the DAW does.
    if (juce::PluginHostType::getPluginLoadedAs() == wrapperType_Standalone)
        CrashReport::install();

    recordEvents.resize ((size_t) recordFifo.getTotalSize());

    const auto& settings = document.getSettings();
    lastOutputSettings = settings.output;
    lastInputSettings = settings.input;
    encoder.setSettings (settings.output);
    liveDecoder.setSettings (settings.input);
    recordDecoder.setSettings (settings.input);

    liveRouter.encoder = &encoder;
    liveRouter.out = &synthMidi;

    synth.onPluginRemoving = [this] (juce::AudioPluginInstance& outgoing) { synthParameters.synthRemoving (outgoing); };

    synth.onPluginChanged = [this]
    {
        synthParameters.synthChanged();
        updateLatency();
        configurationRequested = true;

        if (onStatusChanged)
            onStatusChanged();
    };

    // Pass on what the synth tells its host (us), so FL hears it too.
    synth.onSynthReportedChange = [this] (const juce::AudioProcessorListener::ChangeDetails& details)
    {
        if (details.latencyChanged)
            updateLatency();

        if (details.parameterInfoChanged || details.nonParameterStateChanged)
            updateHostDisplay (ChangeDetails().withParameterInfoChanged (details.parameterInfoChanged)
                                              .withNonParameterStateChanged (details.nonParameterStateChanged));
    };

    startTimerHz (30);
}

MpeRollProcessor::~MpeRollProcessor()
{
    stopTimer();
    synth.onPluginRemoving = nullptr;
    synth.onPluginChanged = nullptr;
    synth.onSynthReportedChange = nullptr;
}

//==============================================================================
void MpeRollProcessor::publish (std::shared_ptr<const EngineSnapshot> snapshot)
{
    const auto version = snapshot->version;

    {
        const juce::SpinLock::ScopedLockType lock (snapshotLock);
        publishedSnapshot = snapshot;
    }

    publishedVersion.store (version, std::memory_order_release);
    releasePool.push_back (std::move (snapshot));
}

void MpeRollProcessor::timerCallback()
{
    // Snapshots are only ever freed here, on the message thread, once nobody else holds them.
    releasePool.erase (std::remove_if (releasePool.begin(), releasePool.end(),
                                       [] (const auto& s) { return s.use_count() <= 1; }),
                       releasePool.end());

    drainRecording();
}

//==============================================================================
void MpeRollProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    currentSampleRate = sampleRate;
    synth.prepare (sampleRate, samplesPerBlock);

    synthMidi.ensureSize (1 << 16);
    hostMidi.ensureSize (1 << 16);

    juce::MidiBuffer discard;
    discard.ensureSize (1 << 14);
    player.stop (encoder, discard, 0);
    encoder.stopAllNotes (discard, 0);
    liveDecoder.reset();

    needsConfiguration = true;
    followPlaying = false;
    playingSlot = -1;
    triggeredSlot = -1;
    triggerKey = -1;
    auditionHeld.fill (false);

    updateLatency();
}

void MpeRollProcessor::releaseResources()
{
    synth.release();
}

bool MpeRollProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    return (out == juce::AudioChannelSet::stereo() || out == juce::AudioChannelSet::mono())
        && layouts.getMainInputChannelSet().isDisabled();
}

double MpeRollProcessor::getTailLengthSeconds() const
{
    return synth.getTailLengthSeconds();
}

void MpeRollProcessor::updateLatency()
{
    setLatencySamples (synth.getLatencySamples());
}

//==============================================================================
void MpeRollProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;
    const auto numSamples = buffer.getNumSamples();

    if (audioSnapshot == nullptr || publishedVersion.load (std::memory_order_acquire) != audioSnapshot->version)
    {
        const juce::SpinLock::ScopedLockType lock (snapshotLock);
        audioSnapshot = publishedSnapshot;
    }

    if (audioSnapshot == nullptr)
    {
        buffer.clear();
        midiMessages.clear();
        return;
    }

    const auto& snapshot = *audioSnapshot;
    synthMidi.clear();
    applySettings (snapshot, numSamples);

    // Transport.
    juce::Optional<juce::AudioPlayHead::PositionInfo> position;

    if (auto* hostPlayHead = getPlayHead())
        position = hostPlayHead->getPosition();

    const auto bpm = juce::jlimit (10.0, 999.0, position.hasValue() ? position->getBpm().orFallback (120.0) : 120.0);
    const auto bps = bpm / 60.0 / currentSampleRate;
    double blockStartBeat = 0.0;

    if (snapshot.settings.playback.mode == PlaybackSettings::Mode::followHost)
    {
        const auto ppq = position.hasValue() ? position->getPpqPosition() : juce::Optional<double>();
        const bool playing = position.hasValue() && position->getIsPlaying() && ppq.hasValue();

        if (playing)
        {
            auto beat = *ppq;
            const bool jumped = std::abs (beat - expectedBeat) > continuityTolerance;

            if (! followPlaying || snapshot.editSlot != playingSlot || jumped)
            {
                player.stop (encoder, synthMidi, 0);

                if (! followPlaying)
                    configurationRequested = true;
            }
            else
            {
                beat = expectedBeat;   // keep sample-exact continuity between blocks
            }

            playingSlot = snapshot.editSlot;
            blockStartBeat = beat;
            expectedBeat = beat + numSamples * bps;
        }
        else if (followPlaying)
        {
            player.stop (encoder, synthMidi, 0);
            encoder.resetZoneControllers (synthMidi, 0);
            playingSlot = -1;
        }

        followPlaying = playing;
    }

    if (configurationRequested.exchange (false))
        needsConfiguration = true;

    if (needsConfiguration)
    {
        if (snapshot.settings.output.sendConfiguration)
            encoder.writeConfiguration (synthMidi, 0);

        needsConfiguration = false;
    }

    // A take ends when the clock stops or recording is disarmed.
    if (recordingTake && (! recordArmed.load() || ! clockRunning()))
    {
        RecordEvent end;
        end.kind = RecordEvent::Kind::takeEnd;
        pushRecordEvent (end);
        recordingTake = false;
    }

    handleAudition();

    // Interleave incoming MIDI with playback so everything reaches the encoder in time order.
    int cursor = 0;

    for (const auto metadata : midiMessages)
    {
        const auto pos = juce::jlimit (0, juce::jmax (0, numSamples - 1), metadata.samplePosition);

        if (pos > cursor)
        {
            renderPlayback (snapshot, cursor, pos, bps, blockStartBeat);
            cursor = pos;
        }

        handleInput (metadata.getMessage(), pos, snapshot, bps, blockStartBeat);
    }

    renderPlayback (snapshot, cursor, numSamples, bps, blockStartBeat);

    // Report the transport to the UI.
    const bool running = clockRunning();
    const auto slot = snapshot.settings.playback.mode == PlaybackSettings::Mode::followHost ? playingSlot : triggeredSlot;
    double uiBeat = 0.0;

    if (running && juce::isPositiveAndBelow (slot, numClipSlots))
    {
        const auto raw = snapshot.settings.playback.mode == PlaybackSettings::Mode::followHost
                             ? blockStartBeat + numSamples * bps
                             : triggerBeat;
        uiBeat = clipBeatAt (snapshot, slot, raw);
    }

    transportPlaying.store (running);
    transportSlot.store (running ? slot : -1);
    transportBeat.store (uiBeat);
    transportBpm.store (bpm);

    if (running)
        recordElapsed += numSamples * bps;

    // Play the synth. Our own MIDI output carries the MPE stream as well.
    hostMidi.clear();
    hostMidi.addEvents (synthMidi, 0, numSamples, 0);
    synth.setOutputSettings (snapshot.settings.output);
    synth.process (buffer, hostMidi, getPlayHead());
    midiMessages.swapWith (synthMidi);
}

void MpeRollProcessor::applySettings (const EngineSnapshot& snapshot, int)
{
    const auto& s = snapshot.settings;

    if (! (s.output == lastOutputSettings))
    {
        player.stop (encoder, synthMidi, 0);
        encoder.stopAllNotes (synthMidi, 0);
        encoder.resetControllers (synthMidi, 0);
        liveDecoder.reset();
        encoder.setSettings (s.output);

        // The synth (and, for a CLAP synth, how its notes are made) may be set up afresh now, so
        // every zone lane is sent again instead of assuming the synth still has its value.
        encoder.forgetZoneValues();
        lastOutputSettings = s.output;
        needsConfiguration = true;
    }

    if (! (s.input == lastInputSettings))
    {
        liveRouter.samplePos = 0;
        liveDecoder.stopAllNotes (liveRouter);
        liveDecoder.setSettings (s.input);
        lastInputSettings = s.input;
    }

    if (s.playback.mode != lastPlaybackMode)
    {
        player.stop (encoder, synthMidi, 0);
        followPlaying = false;
        playingSlot = -1;
        triggeredSlot = -1;
        triggerKey = -1;
        lastPlaybackMode = s.playback.mode;
    }

    player.setControlInterval ((int) std::lround (s.playback.controlIntervalMs * currentSampleRate / 1000.0));
    player.setChaseNotes (s.playback.chaseNotes);
}

void MpeRollProcessor::handleAudition()
{
    const auto scope = auditionFifo.read (auditionFifo.getNumReady());

    auto handle = [this] (int index)
    {
        const auto& e = auditionEvents[(size_t) index];
        const auto key = juce::jlimit (0, 127, e.key);
        const auto h = makeHandle (auditionTag, (uint32_t) key);

        if (auditionHeld[(size_t) key])
            encoder.stopNote (synthMidi, 0, h, 0.5f);

        auditionHeld[(size_t) key] = e.on;

        if (e.on)
        {
            encoder.prepareNote (synthMidi, 0, h, key);
            encoder.startNote (synthMidi, 0, h, e.velocity);
        }
    };

    scope.forEach ([&] (int index) { handle (index); });
}

void MpeRollProcessor::handleInput (const juce::MidiMessage& message, int samplePos, const EngineSnapshot& snapshot,
                                    double bps, double blockStartBeat)
{
    const auto& playback = snapshot.settings.playback;

    if (playback.mode == PlaybackSettings::Mode::noteTrigger && (message.isNoteOn() || message.isNoteOff()))
    {
        const auto key = message.getNoteNumber();
        const auto slot = key - playback.triggerBaseKey;

        if (juce::isPositiveAndBelow (slot, numClipSlots))
        {
            if (message.isNoteOn())
                startTrigger (slot, key, samplePos);
            else if (key == triggerKey)
                stopTrigger (samplePos);

            return;
        }
    }

    if (recordArmed.load() && clockRunning() && message.getRawDataSize() <= 3 && ! message.isMetaEvent())
    {
        const bool follow = playback.mode == PlaybackSettings::Mode::followHost;
        const auto slot = follow ? playingSlot : triggeredSlot;

        if (juce::isPositiveAndBelow (slot, numClipSlots))
        {
            RecordEvent e;
            e.kind = RecordEvent::Kind::midi;
            e.size = (uint8_t) message.getRawDataSize();
            std::memcpy (e.bytes, message.getRawData(), e.size);
            e.slot = (int8_t) slot;
            e.clipBeat = clipBeatAt (snapshot, slot, follow ? blockStartBeat + samplePos * bps : triggerBeat);
            e.elapsedBeat = recordElapsed + samplePos * bps;
            pushRecordEvent (e);
            recordingTake = true;
        }
    }

    if (playback.passThroughInput)
    {
        liveRouter.samplePos = samplePos;
        liveDecoder.process (message, liveRouter);
    }
}

void MpeRollProcessor::renderPlayback (const EngineSnapshot& snapshot, int from, int to, double bps, double blockStartBeat)
{
    if (to <= from)
        return;

    if (snapshot.settings.playback.mode == PlaybackSettings::Mode::followHost)
    {
        if (! followPlaying || ! juce::isPositiveAndBelow (playingSlot, numClipSlots))
            return;

        const auto& clip = *snapshot.clips[(size_t) playingSlot];
        player.renderBlock (clip, encoder, synthMidi, blockStartBeat + from * bps, bps, from, to - from, clip.looping);
        return;
    }

    if (! juce::isPositiveAndBelow (triggeredSlot, numClipSlots))
        return;

    const auto& clip = *snapshot.clips[(size_t) triggeredSlot];
    player.renderBlock (clip, encoder, synthMidi, triggerBeat, bps, from, to - from, clip.looping);
    triggerBeat += (to - from) * bps;
}

double MpeRollProcessor::clipBeatAt (const EngineSnapshot& snapshot, int slot, double rawBeat) const
{
    const auto& clip = *snapshot.clips[(size_t) slot];

    if (! clip.looping || clip.length <= 0.0)
        return rawBeat;

    auto beat = std::fmod (rawBeat, clip.length);
    return beat < 0.0 ? beat + clip.length : beat;
}

bool MpeRollProcessor::clockRunning() const noexcept
{
    return lastPlaybackMode == PlaybackSettings::Mode::followHost ? followPlaying : triggeredSlot >= 0;
}

void MpeRollProcessor::startTrigger (int slot, int key, int samplePos)
{
    player.stop (encoder, synthMidi, samplePos);

    if (recordingTake)
    {
        RecordEvent end;
        end.kind = RecordEvent::Kind::takeEnd;
        pushRecordEvent (end);
        recordingTake = false;
    }

    triggeredSlot = slot;
    triggerKey = key;
    triggerBeat = 0.0;
}

void MpeRollProcessor::stopTrigger (int samplePos)
{
    player.stop (encoder, synthMidi, samplePos);
    encoder.resetZoneControllers (synthMidi, samplePos);
    triggeredSlot = -1;
    triggerKey = -1;
}

void MpeRollProcessor::pushRecordEvent (const RecordEvent& e)
{
    const auto scope = recordFifo.write (1);

    if (scope.blockSize1 > 0)
        recordEvents[(size_t) scope.startIndex1] = e;
}

//==============================================================================
void MpeRollProcessor::auditionNote (int key, float velocity, bool isNoteOn)
{
    const auto scope = auditionFifo.write (1);

    if (scope.blockSize1 > 0)
        auditionEvents[(size_t) scope.startIndex1] = { key, velocity, isNoteOn };
}

void MpeRollProcessor::setRecordArmed (bool shouldRecord)
{
    recordArmed.store (shouldRecord);

    if (! shouldRecord)
        drainRecording();
}

MpeRollProcessor::Transport MpeRollProcessor::getTransport() const noexcept
{
    return { transportPlaying.load(), transportSlot.load(), transportBeat.load(), transportBpm.load() };
}

void MpeRollProcessor::drainRecording()
{
    const auto scope = recordFifo.read (recordFifo.getNumReady());
    const auto& inputSettings = document.getSettings().input;

    if (! (recordDecoder.getSettings() == inputSettings))
        recordDecoder.setSettings (inputSettings);

    scope.forEach ([&] (int index)
    {
        const auto& e = recordEvents[(size_t) index];

        if (e.kind == RecordEvent::Kind::takeEnd)
        {
            finishTake();
            return;
        }

        if (recordSlot >= 0 && e.slot != recordSlot)
            finishTake();

        if (recordSlot < 0)
        {
            recordSlot = e.slot;
            recordBuilder.clear();
        }

        recordBuilder.setTime (e.clipBeat, e.elapsedBeat);
        recordDecoder.process (juce::MidiMessage (e.bytes, (int) e.size), recordBuilder);
    });

    if (recordSlot >= 0)
        recordBuilder.updateHeldNotes();
}

void MpeRollProcessor::finishTake()
{
    if (recordSlot < 0)
        return;

    recordDecoder.stopAllNotes (recordBuilder);
    recordBuilder.finish();

    if (recordBuilder.hasData())
    {
        auto clip = document.getClip (recordSlot);
        const auto zone = recordBuilder.takeZone();

        for (auto& note : recordBuilder.takeNotes())
            clip.addNote (std::move (note));

        mergeLane (clip.zone.pitch, zone.pitch);
        mergeLane (clip.zone.pressure, zone.pressure);
        mergeLane (clip.zone.timbre, zone.timbre);

        for (const auto& c : zone.controllers)
            mergeLane (clip.zone.get (LaneId::cc (c.controller)), c.curve);

        if (! clip.looping)
            clip.length = juce::jmax (clip.length, std::ceil (clip.contentEnd() / 4.0) * 4.0);

        document.replaceClip (recordSlot, std::move (clip), "Record");
    }

    recordBuilder.clear();
    recordSlot = -1;
}

void MpeRollProcessor::setStatus (const juce::String& message)
{
    statusMessage = message;

    if (onStatusChanged)
        onStatusChanged();
}

//==============================================================================
juce::AudioProcessorEditor* MpeRollProcessor::createEditor()
{
    return new MpeRollEditor (*this);
}

void MpeRollProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    // The synth and its slots belong to the message thread. A host saving from another thread
    // locks it first, so the synth can't be swapped mid-save (JUCE's VST3 host does the same).
    std::optional<juce::MessageManagerLock> messageLock;

    if (! juce::MessageManager::existsAndIsCurrentThread())
        messageLock.emplace();

    std::shared_ptr<const EngineSnapshot> snapshot;

    {
        const juce::SpinLock::ScopedLockType lock (snapshotLock);
        snapshot = publishedSnapshot;
    }

    juce::XmlElement xml ("MpeRoll");
    xml.setAttribute ("version", 1);

    if (snapshot != nullptr)
        xml.addChildElement (ClipDocument::snapshotToXml (*snapshot).release());

    if (auto synthXml = synth.saveState())
        xml.addChildElement (synthXml.release());

    xml.addChildElement (synthParameters.toXml().release());

    copyXmlToBinary (xml, destData);
}

void MpeRollProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    std::shared_ptr<juce::XmlElement> xml (getXmlFromBinary (data, sizeInBytes));

    if (xml == nullptr || ! xml->hasTagName ("MpeRoll"))
        return;

    auto apply = [this, xml]
    {
        if (auto* doc = xml->getChildByName ("Document"))
            document.fromXml (*doc);

        synthParameters.fromXml (xml->getChildByName ("SynthParameters"));

        if (auto* synthXml = xml->getChildByName ("HostedSynth"))
            synth.restoreState (*synthXml, [this] (const juce::String& error) { setStatus (error); });
        else
            synth.unload();
    };

    if (juce::MessageManager::existsAndIsCurrentThread())
    {
        apply();
        return;
    }

    juce::MessageManager::callAsync ([apply, alive = std::weak_ptr<int> (lifetime)]
    {
        if (! alive.expired())
            apply();
    });
}

} // namespace mperoll

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new mperoll::MpeRollProcessor();
}
