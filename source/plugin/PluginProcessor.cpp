#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "CrashReport.h"
#include "PluginLog.h"

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

    // A bend past its range in one atomic word: how far it was asked to go in the high half, so
    // that comparing the words compares that first (positive floats' bits order as their values do).
    uint64_t packOverflow (const MpeEncoder::BendOverflow& o) noexcept
    {
        uint32_t asked = 0, range = 0;
        std::memcpy (&asked, &o.asked, sizeof (asked));
        std::memcpy (&range, &o.range, sizeof (range));
        return ((uint64_t) asked << 32) | range;
    }

    MpeEncoder::BendOverflow unpackOverflow (uint64_t packed) noexcept
    {
        const auto asked = (uint32_t) (packed >> 32), range = (uint32_t) packed;
        MpeEncoder::BendOverflow o;
        std::memcpy (&o.asked, &asked, sizeof (asked));
        std::memcpy (&o.range, &range, sizeof (range));
        return o;
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
        synthChanged = true;

        if (onStatusChanged)
            onStatusChanged();
    };

    // The synth is played another way now (its own MPE switch changed): the zone lanes go again.
    synth.onPlayingChanged = [this]
    {
        synthChanged = true;

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

    // One copy per note: the copies are loaded as the settings ask, and the status line follows them.
    synth.onCopiesChanged = [this]
    {
        if (onCopiesChanged)
            onCopiesChanged();
    };

    synth.setCopySettings (settings.output);
    document.addChangeListener (this);

    startTimerHz (30);
}

MpeRollProcessor::~MpeRollProcessor()
{
    stopTimer();
    document.removeChangeListener (this);
    synth.onPluginRemoving = nullptr;
    synth.onPluginChanged = nullptr;
    synth.onPlayingChanged = nullptr;
    synth.onSynthReportedChange = nullptr;
    synth.onCopiesChanged = nullptr;
}

void MpeRollProcessor::changeListenerCallback (juce::ChangeBroadcaster*)
{
    // Most changes are to notes; the synth does nothing unless the copy settings changed.
    synth.setCopySettings (document.getSettings().output);
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
    synthParameters.flushDiagnostics();

    // The first All Notes Off or All Sound Off this load that stopped the clip, seen on the audio
    // thread: the log then tells whether FL sends one for a channel switched off.
    if (const auto logged = hostSilenceToLog.exchange (0); logged != 0)
    {
        const auto controller = logged > 0 ? logged : -logged;
        PluginLog::add (juce::String ("The host sent ")
                        + (controller == 120 ? "All Sound Off (CC 120)" : "All Notes Off (CC 123)")
                        + (logged > 0 ? ": playback paused until the transport restarts"
                                      : ": the clip playing in Note trigger mode stopped"));
    }
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
    encoder.forgetZoneValues();     // the synth (and its copies' router) start afresh, so every lane is sent again
    liveDecoder.reset();

    needsConfiguration = true;
    followPlaying = false;
    playingSlot = -1;
    triggeredSlot = -1;
    triggerKey = -1;
    hostSilenced = false;
    followRestarted = false;
    gateHeld.fill (0);
    gateHeldCount = 0;
    gateClosesAt = -1;
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
        followRestarted = false;

        if (playing)
        {
            auto beat = *ppq;
            const bool jumped = std::abs (beat - expectedBeat) > continuityTolerance;

            if (! followPlaying || snapshot.editSlot != playingSlot || jumped)
            {
                player.stop (encoder, synthMidi, 0);

                // The host's All Notes Off holds until the transport restarts or jumps (a loop of
                // the pattern or song is a jump too). Another clip slot picked in MPE Roll leaves
                // it standing: the host hasn't said to play again.
                if (! followPlaying || jumped)
                {
                    hostSilenced = false;
                    followRestarted = true;
                }

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
            hostSilenced = false;
        }

        followPlaying = playing;
    }

    // A synth just loaded (or given new state by a project) has heard none of the zone lanes, whose
    // values are otherwise sent once and then only as they change. What was playing is stopped and
    // picked up again, and the values sent so far are forgotten, so every lane reaches it now.
    if (synthChanged.exchange (false))
    {
        player.stop (encoder, synthMidi, 0);
        encoder.stopAllNotes (synthMidi, 0);
        encoder.forgetZoneValues();
        needsConfiguration = true;
    }

    if (configurationRequested.exchange (false))
        needsConfiguration = true;

    if (needsConfiguration)
    {
        if (snapshot.settings.output.sendConfiguration)
        {
            encoder.writeConfiguration (synthMidi, 0);
            synth.requestConfiguration();       // each copy's bend range, in copy mode
        }

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

    // Interleave incoming MIDI with playback so everything reaches the encoder in time order. The
    // gate shuts only once every message at the sample of the note-off that shut it has been seen.
    int cursor = 0;

    for (const auto metadata : midiMessages)
    {
        const auto pos = juce::jlimit (0, juce::jmax (0, numSamples - 1), metadata.samplePosition);

        if (pos > cursor)
        {
            settleGate();
            renderPlayback (snapshot, cursor, pos, bps, blockStartBeat);
            cursor = pos;
        }

        handleInput (metadata.getMessage(), pos, snapshot, bps, blockStartBeat);
    }

    settleGate();
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

    // A bend asked past its range was sent at the range's edge, so the synth heard a glide stop
    // short: for the status line. With one copy per note the encoder writes the copies' stream with
    // a note's bend scaled to their range already, so it sees a glide past it first; a copy's bend
    // goes past when a zone bend and the note's own add up to more.
    auto overflow = encoder.takeBendOverflow();

    if (const auto copies = synth.takeCopyBendOverflow(); copies.asked > overflow.asked)
        overflow = copies;

    if (! overflow.isEmpty())
    {
        // The furthest since the message thread last looked.
        const auto packed = packOverflow (overflow);
        auto seen = bendOverflow.load (std::memory_order_relaxed);

        while (packed > seen && ! bendOverflow.compare_exchange_weak (seen, packed, std::memory_order_release, std::memory_order_relaxed))
        {}
    }
}

MpeEncoder::BendOverflow MpeRollProcessor::getRecentBendOverflow()
{
    constexpr juce::uint32 keptMs = 60000;    // long enough to stop, read the status line and find MPE settings
    const auto now = juce::Time::getMillisecondCounter();
    const auto& output = document.getSettings().output;

    if (const auto packed = bendOverflow.exchange (0, std::memory_order_acquire); packed != 0)
    {
        recentBendOverflow = unpackOverflow (packed);
        recentBendOverflowMode = output.mode;
        recentBendOverflowAt = now;
    }
    else if (now - recentBendOverflowAt >= keptMs)
    {
        recentBendOverflow = {};
    }

    // Stale once the range it was scaled to, or the way the synth is played, has changed: the next
    // glide says if it still stops short.
    const auto rangeStands = [&] (float range) { return std::abs (recentBendOverflow.range - range) < 0.01f; };

    if (output.mode != recentBendOverflowMode || ! (rangeStands (output.masterBendRange) || rangeStands (output.memberBendRange)))
        recentBendOverflow = {};

    return recentBendOverflow;
}

void MpeRollProcessor::applySettings (const EngineSnapshot& snapshot, int)
{
    const auto& s = snapshot.settings;

    // Settings only the copies care about (how many, sleep, cores, following the first) change
    // nothing in what's played: the notes play on, and the copies take them up themselves.
    const auto playedAlike = [] (OutputSettings a, const OutputSettings& b)
    {
        a.copies = b.copies;
        a.copiesSleep = b.copiesSleep;
        a.copiesUseCores = b.copiesUseCores;
        a.copiesFollow = b.copiesFollow;
        return a == b;
    };

    if (playedAlike (s.output, lastOutputSettings))
    {
        lastOutputSettings = s.output;
    }
    else
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

    // The host's notes gate the clip in Follow FL mode only. Switched on, the gate is shut until the
    // host's next note, and the notes played through until now end here, since the gate would take
    // their note-offs. Switched off, the clip plays on from where the song is.
    const bool gated = s.playback.mode == PlaybackSettings::Mode::followHost && s.playback.gateByHostNotes;

    if (gated != gateActive)
    {
        gateHeld.fill (0);
        gateHeldCount = 0;
        gateActive = gated;

        if (gated)
        {
            liveRouter.samplePos = 0;
            liveDecoder.stopAllNotes (liveRouter);
            closeGate (0);
        }
    }

    player.setControlInterval ((int) std::lround (s.playback.controlIntervalMs * currentSampleRate / 1000.0));
    player.setChaseNotes (s.playback.chaseNotes);
    player.setSampleRate (currentSampleRate);
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
    const bool follow = playback.mode == PlaybackSettings::Mode::followHost;

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

    // The host's All Notes Off or All Sound Off (FL may end the notes of a channel switched off that
    // way) stops the clip too: in Follow FL until the transport restarts or jumps, or the host starts
    // a note; in Note trigger mode as the trigger note's note-off would. The CC still goes on to the
    // synth below, as it always did.
    if (message.isAllNotesOff() || message.isAllSoundOff())
        stopForHost (message.getControllerNumber(), samplePos, follow);
    else if (follow && message.isNoteOn())
        hostSilenced = false;

    if (recordArmed.load() && clockRunning() && message.getRawDataSize() <= 3 && ! message.isMetaEvent())
    {
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

    // With the gate on, the host's notes say when the clip sounds and aren't played themselves
    // (recorded, though, above).
    if (gateActive && (message.isNoteOn() || message.isNoteOff()))
    {
        updateGate (message, samplePos);
        return;
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
        if (! followPlaying || ! juce::isPositiveAndBelow (playingSlot, numClipSlots)
            || hostSilenced || (gateActive && gateHeldCount == 0))
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

void MpeRollProcessor::stopForHost (int controller, int samplePos, bool follow)
{
    // What the host asks of a synth, done for the clip as well: every note off and the controllers
    // at rest. The host holds no note after it, so the gate shuts too, and a clip a note started
    // stops as at that note's note-off.
    const bool triggered = triggeredSlot >= 0;

    if (follow)
        player.stop (encoder, synthMidi, samplePos);
    else
        stopTrigger (samplePos);

    encoder.stopAllNotes (synthMidi, samplePos);
    encoder.resetControllers (synthMidi, samplePos);
    gateHeld.fill (0);
    gateHeldCount = 0;
    gateClosesAt = -1;

    if (follow)
    {
        // With the transport stopped there's nothing to pause, and one in the block the transport
        // starts or jumps in is the host tidying up, so the clip isn't paused for it.
        if (! followPlaying || followRestarted)
            return;

        hostSilenced = true;
    }
    else if (! triggered)
    {
        return;
    }

    // The first that stopped the clip this load; the message thread writes the line.
    if (! hostSilenceSeen)
    {
        hostSilenceSeen = true;
        hostSilenceToLog.store (follow ? controller : -controller, std::memory_order_relaxed);
    }
}

void MpeRollProcessor::updateGate (const juce::MidiMessage& message, int samplePos)
{
    // A note-off for a key that isn't held (its note-on came before the gate was switched on, or
    // before an All Notes Off) is left out.
    auto& held = gateHeld[(size_t) (message.getNoteNumber() & 0x7f)];

    if (message.isNoteOn())
    {
        if (held < 255)
        {
            ++held;
            ++gateHeldCount;
        }
    }
    else if (held > 0)
    {
        --held;

        // Shut once the rest of this sample's messages are in (settleGate): when two of the host's
        // notes touch, the next one's note-on can come after this note-off at the same sample, and
        // the clip would otherwise stop there and start again.
        if (--gateHeldCount == 0)
            gateClosesAt = samplePos;
    }
}

void MpeRollProcessor::settleGate()
{
    // Every message at that sample is in: unless one of them started a note again, the gate shuts.
    if (gateClosesAt >= 0 && gateHeldCount == 0)
        closeGate (gateClosesAt);

    gateClosesAt = -1;
}

void MpeRollProcessor::closeGate (int samplePos)
{
    // As when the transport stops: a held pedal or bend of the clip's would otherwise keep its notes
    // ringing, or the next ones out of tune.
    player.stop (encoder, synthMidi, samplePos);
    encoder.stopAllNotes (synthMidi, samplePos);
    encoder.resetZoneControllers (synthMidi, samplePos);
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
