#include "CopyRouter.h"

namespace mperoll
{

namespace
{
    constexpr float stealLift = 0.5f;
    constexpr size_t discardBytes = 65536;     // a copy's encoder forgetting its notes writes far less

    /** The stream read back exactly as MpeEncoder writes it in copy mode. Its layout stays the stream's
        own, whatever configuration messages in it say.
    */
    InputSettings streamInputSettings (const OutputSettings& s)
    {
        const auto stream = copyStreamSettings (s);

        InputSettings in;
        in.mode = stream.mode == OutputSettings::Mode::singleChannel ? InputSettings::Mode::singleChannel
                                                                     : InputSettings::Mode::mpe;
        in.zones = stream.zones;
        in.followConfigurationMessages = false;
        in.memberBendRange = stream.memberBendRange;
        in.masterBendRange = stream.masterBendRange;
        in.conventionalBendRange = stream.masterBendRange;
        in.timbreController = stream.timbreController;
        in.timbreLsbController = stream.timbreLsbController;
        in.mpePlus = stream.mpePlus;
        return in;
    }

    /** What the copies are played with, less what doesn't change the MIDI a copy is sent. A change
        to any of it, or to which keys go to every copy, stops the notes first.
    */
    OutputSettings howCopiesPlay (const OutputSettings& s)
    {
        auto play = copyPlaySettings (s);
        const OutputSettings unused;
        play.zones = unused.zones;
        play.splitKey = unused.splitKey;
        play.memberBendRange = unused.memberBendRange;
        play.sendConfiguration = unused.sendConfiguration;
        play.overflow = unused.overflow;
        play.clapNoteExpressions = unused.clapNoteExpressions;
        play.copies = unused.copies;
        play.copiesSleep = unused.copiesSleep;
        play.copiesUseCores = unused.copiesUseCores;
        play.copiesFollow = unused.copiesFollow;
        return play;
    }
}

CopyRouter::CopyRouter()
{
    encoders.reserve ((size_t) maxCopies);

    for (int i = 0; i < maxCopies; ++i)
        encoders.push_back (std::make_unique<MpeEncoder>());

    discard.ensureSize (discardBytes);

    settings.mode = OutputSettings::Mode::copies;
    playSettings = copyPlaySettings (settings);
    numCopies = juce::jlimit (OutputSettings::minCopies, maxCopies, settings.copies);
    decoder.setSettings (streamInputSettings (settings));

    for (auto& encoder : encoders)
        encoder->setSettings (playSettings);
}

CopyRouter::~CopyRouter() = default;

//==============================================================================
void CopyRouter::setSettings (const OutputSettings& newSettings, juce::MidiBuffer* outs, int samplePos)
{
    const auto newStream = streamInputSettings (newSettings);
    const bool streamChanged = ! (newStream == decoder.getSettings());
    const bool playChanged = ! (howCopiesPlay (newSettings) == howCopiesPlay (settings));

    if (streamChanged || playChanged)
    {
        bool holdingNotes = false;

        for (const auto& n : notes)
            holdingNotes = holdingNotes || n.inUse;

        if (holdingNotes)
            stopAll (outs, samplePos);
    }

    settings = newSettings;
    playSettings = copyPlaySettings (newSettings);

    for (auto& encoder : encoders)
        encoder->setSettings (playSettings);

    if (streamChanged)
        decoder.setSettings (newStream);

    const auto newNumCopies = juce::jlimit (OutputSettings::minCopies, maxCopies, settings.copies);

    for (int c = newNumCopies; c < numCopies; ++c)
        dropCopy (c);

    // Copies coming into use learn the zone values, so their notes combine with them. They're sent
    // them when they're introduced.
    for (int c = numCopies; c < newNumCopies; ++c)
    {
        sendZoneState (c, discard, 0);
        encoders[(size_t) c]->forgetZoneValues();
        discard.clear();
    }

    numCopies = newNumCopies;

    // A copy may read what it was sent differently now (another channel, another bend range), so
    // every zone value is sent again.
    if (playChanged)
    {
        for (int c = 0; c < numCopies; ++c)
        {
            if (outs != nullptr)
                sendZoneState (c, outs[c], samplePos);
            else
                encoders[(size_t) c]->forgetZoneValues();
        }
    }
}

//==============================================================================
void CopyRouter::route (const juce::MidiBuffer& in, juce::MidiBuffer* outs, const CopySet& available,
                        const CopySet& quiet, CopySet& asleep)
{
    outputs = outs;
    availableCopies = &available;
    quietCopies = &quiet;
    sleepingCopies = &asleep;

    for (const auto metadata : in)
    {
        // Only channel messages mean anything here, and a longer one (SysEx) would allocate.
        if (metadata.numBytes > 3)
            continue;

        position = metadata.samplePosition;
        decoder.process (metadata.getMessage(), *this);
    }

    outputs = nullptr;
    availableCopies = nullptr;
    quietCopies = nullptr;
    sleepingCopies = nullptr;
}

void CopyRouter::handleEvent (const MpeDecoder::Event& e)
{
    using Type = MpeDecoder::Event::Type;
    jassert (outputs != nullptr);

    switch (e.type)
    {
        case Type::zonePitch:
            zonePitch = e.value;
            zonePitchSet = true;

            for (int c = 0; c < numCopies; ++c)
                encoders[(size_t) c]->setZonePitch (outputs[c], position, e.value);

            return;

        case Type::zonePressure:
            zonePressure = e.value;
            zonePressureSet = true;

            for (int c = 0; c < numCopies; ++c)
                encoders[(size_t) c]->setZonePressure (outputs[c], position, e.value);

            return;

        case Type::zoneTimbre:
            zoneTimbre = e.value;
            zoneTimbreSet = true;

            for (int c = 0; c < numCopies; ++c)
                encoders[(size_t) c]->setZoneTimbre (outputs[c], position, e.value);

            return;

        case Type::zoneController:
        {
            if (! juce::isPositiveAndBelow (e.controller, 128))
                return;

            zoneControllers[(size_t) e.controller] = e.value;
            zoneControllersSet.set ((size_t) e.controller);

            for (int c = 0; c < numCopies; ++c)
                encoders[(size_t) c]->setZoneController (outputs[c], position, e.controller, e.value);

            if (e.controller == 64)
            {
                if (MpeEncoder::toSevenBit (e.value) >= 64)
                    pedalDown = true;
                else if (pedalDown)
                    releasePedal();
            }

            return;
        }

        case Type::notePrepare:
        {
            auto* note = addNote (e.noteId, e.key);

            if (note == nullptr)
                return;

            if (settings.isEveryCopyKey (note->key))
            {
                note->everyCopyKey = true;
                return;
            }

            const auto c = chooseCopy (note->key);

            if (c < 0)
                return;

            wake (c);
            note->copy = c;
            ++copies[(size_t) c].held;
            copies[(size_t) c].newestStart = note->started;
            encoders[(size_t) c]->prepareNote (outputs[c], position, e.noteId, note->key);
            return;
        }

        case Type::noteStart:
        {
            auto* note = findNote (e.noteId);

            if (note == nullptr || note->startedPlaying)
                return;

            note->startedPlaying = true;

            if (note->everyCopyKey)
            {
                const auto velocity = (juce::uint8) juce::jmax (1, MpeEncoder::toSevenBit (e.value));

                for (int c = 0; c < numCopies; ++c)
                {
                    if (! (*availableCopies)[(size_t) c])
                        continue;

                    wake (c);
                    outputs[c].addEvent (juce::MidiMessage::noteOn (channel(), note->key, velocity), position);
                    note->everyCopy.set ((size_t) c);
                    ++copies[(size_t) c].everyCopyKeys;
                }
            }
            else if (note->copy >= 0)
            {
                encoders[(size_t) note->copy]->startNote (outputs[note->copy], position, e.noteId, e.value);
            }

            return;
        }

        case Type::noteStop:
        {
            auto* note = findNote (e.noteId);

            if (note == nullptr)
                return;

            if (note->everyCopyKey)
            {
                const auto velocity = (juce::uint8) MpeEncoder::toSevenBit (e.value);

                for (int c = 0; c < maxCopies; ++c)
                {
                    if (! note->everyCopy[(size_t) c])
                        continue;

                    outputs[c].addEvent (juce::MidiMessage::noteOff (channel(), note->key, velocity), position);
                    auto& keys = copies[(size_t) c].everyCopyKeys;
                    keys = juce::jmax (0, keys - 1);
                }
            }
            else if (note->copy >= 0)
            {
                encoders[(size_t) note->copy]->stopNote (outputs[note->copy], position, e.noteId, e.value);
                noteEnded (*note);
            }

            *note = Note {};
            return;
        }

        case Type::notePitch:
        case Type::notePressure:
        case Type::noteTimbre:
        case Type::noteController:
            break;
    }

    // A note's own expression moves its copy alone.
    auto* note = findNote (e.noteId);

    if (note == nullptr || note->everyCopyKey || note->copy < 0)
        return;

    // Before a note without a timbre or controller lane of its own, the stream sets its channel back
    // to the usual value. Plain MIDI leaves those to the preset (a synth with its filter on CC74 keeps
    // its sound), so the copy isn't sent them either; its encoder still puts back what an earlier note
    // of the copy changed.
    if (! note->startedPlaying)
    {
        if (e.type == Type::noteTimbre && MpeEncoder::toSevenBit (e.value) == MpeEncoder::toSevenBit (settings.defaultTimbre))
            return;

        if (e.type == Type::noteController
            && MpeEncoder::toSevenBit (e.value) == MpeEncoder::toSevenBit (defaultControllerValue (e.controller)))
            return;
    }

    auto& encoder = *encoders[(size_t) note->copy];
    auto& out = outputs[note->copy];

    switch (e.type)
    {
        case Type::notePitch:       encoder.setPitch (out, position, e.noteId, e.value); break;
        case Type::notePressure:    encoder.setPressure (out, position, e.noteId, e.value); break;
        case Type::noteTimbre:      encoder.setTimbre (out, position, e.noteId, e.value); break;
        case Type::noteController:  encoder.setController (out, position, e.noteId, e.controller, e.value); break;

        case Type::notePrepare:
        case Type::noteStart:
        case Type::noteStop:
        case Type::zonePitch:
        case Type::zonePressure:
        case Type::zoneTimbre:
        case Type::zoneController:
            break;
    }
}

//==============================================================================
CopyRouter::Note* CopyRouter::findNote (uint32_t id) noexcept
{
    for (auto& n : notes)
        if (n.inUse && n.id == id)
            return &n;

    return nullptr;
}

CopyRouter::Note* CopyRouter::addNote (uint32_t id, int key) noexcept
{
    for (auto& n : notes)
    {
        if (! n.inUse)
        {
            n = Note {};
            n.id = id;
            n.key = juce::jlimit (0, 127, key);
            n.started = ++clock;
            n.inUse = true;
            return &n;
        }
    }

    // The decoder ends a note before it reports one more than it can hold, so this can't happen.
    jassertfalse;
    return nullptr;
}

int CopyRouter::chooseCopy (int)
{
    auto mayTake = [this] (int c) { return (*availableCopies)[(size_t) c]; };
    int best = -1;

    // A free copy, quiet ones first, then one still sounding: the one whose last note ended longest ago.
    for (const bool quiet : { true, false })
    {
        for (int c = 0; c < numCopies; ++c)
        {
            const auto& candidate = copies[(size_t) c];

            if (! mayTake (c) || candidate.held > 0 || candidate.sustained > 0 || (*quietCopies)[(size_t) c] != quiet)
                continue;

            if (best < 0 || candidate.lastEnded < copies[(size_t) best].lastEnded)
                best = c;
        }

        if (best >= 0)
            return best;
    }

    // A copy only the sustain pedal holds, the one released longest ago.
    for (int c = 0; c < numCopies; ++c)
    {
        const auto& candidate = copies[(size_t) c];

        if (! mayTake (c) || candidate.held > 0)
            continue;

        if (best < 0 || candidate.lastEnded < copies[(size_t) best].lastEnded)
            best = c;
    }

    if (best >= 0)
        return best;

    // Every copy holds notes.
    if (settings.overflow == OutputSettings::Overflow::stealOldest)
    {
        Note* oldest = nullptr;

        for (auto& n : notes)
            if (n.inUse && n.copy >= 0 && n.copy < numCopies && mayTake (n.copy)
                && (oldest == nullptr || n.started < oldest->started))
                oldest = &n;

        if (oldest != nullptr)
        {
            const auto c = oldest->copy;
            encoders[(size_t) c]->stopNote (outputs[c], position, oldest->id, stealLift);

            auto& stolen = copies[(size_t) c];
            stolen.held = juce::jmax (0, stolen.held - 1);
            stolen.lastEnded = ++clock;
            oldest->copy = -1;     // still tracked, so its later events are ignored
            return c;
        }
    }

    for (int c = 0; c < numCopies; ++c)
    {
        if (! mayTake (c))
            continue;

        const auto& candidate = copies[(size_t) c];

        if (best < 0
            || candidate.held < copies[(size_t) best].held
            || (candidate.held == copies[(size_t) best].held && candidate.newestStart < copies[(size_t) best].newestStart))
            best = c;
    }

    return best;
}

void CopyRouter::wake (int copy)
{
    if (sleepingCopies == nullptr || ! (*sleepingCopies)[(size_t) copy])
        return;

    sendZoneState (copy, outputs[copy], position);
    sleepingCopies->reset ((size_t) copy);
}

void CopyRouter::sendZoneState (int copy, juce::MidiBuffer& out, int samplePos)
{
    auto& encoder = *encoders[(size_t) copy];
    encoder.forgetZoneValues();

    if (zonePitchSet)
        encoder.setZonePitch (out, samplePos, zonePitch);

    if (zonePressureSet)
        encoder.setZonePressure (out, samplePos, zonePressure);

    if (zoneTimbreSet)
        encoder.setZoneTimbre (out, samplePos, zoneTimbre);

    for (int cc = 0; cc < 128; ++cc)
        if (zoneControllersSet[(size_t) cc])
            encoder.setZoneController (out, samplePos, cc, zoneControllers[(size_t) cc]);
}

void CopyRouter::noteEnded (Note& note)
{
    auto& c = copies[(size_t) note.copy];
    c.held = juce::jmax (0, c.held - 1);
    c.lastEnded = ++clock;

    if (pedalDown)
        ++c.sustained;
}

void CopyRouter::releasePedal()
{
    pedalDown = false;

    for (auto& c : copies)
    {
        if (c.sustained > 0)
        {
            c.sustained = 0;
            c.lastEnded = ++clock;
        }
    }
}

//==============================================================================
void CopyRouter::stopAll (juce::MidiBuffer* outs, int samplePos)
{
    for (int c = 0; c < maxCopies; ++c)
        encoders[(size_t) c]->stopAllNotes (outs != nullptr ? outs[c] : discard, samplePos);

    for (const auto& n : notes)
    {
        if (! n.inUse || ! n.everyCopyKey)
            continue;

        for (int c = 0; c < maxCopies; ++c)
            if (n.everyCopy[(size_t) c])
                (outs != nullptr ? outs[c] : discard).addEvent (juce::MidiMessage::noteOff (channel(), n.key, (juce::uint8) 64), samplePos);
    }

    discard.clear();

    // The stream's later events for these notes are ignored. What they leave sounding is the newest.
    for (auto& c : copies)
    {
        if (c.held > 0 || c.sustained > 0 || c.everyCopyKeys > 0)
            c.lastEnded = ++clock;

        c.held = c.sustained = c.everyCopyKeys = 0;
    }

    for (auto& n : notes)
        n = Note {};
}

void CopyRouter::dropCopy (int copy)
{
    if (! juce::isPositiveAndBelow (copy, maxCopies))
        return;

    for (auto& n : notes)
    {
        if (! n.inUse)
            continue;

        if (n.copy == copy)
            n.copy = -1;

        n.everyCopy.reset ((size_t) copy);
    }

    auto& encoder = *encoders[(size_t) copy];
    encoder.stopAllNotes (discard, 0);
    encoder.forgetZoneValues();
    discard.clear();

    copies[(size_t) copy] = Copy {};
}

void CopyRouter::introduce (int copy, juce::MidiBuffer& out, int samplePos)
{
    if (! juce::isPositiveAndBelow (copy, maxCopies))
        return;

    if (settings.sendConfiguration)
        encoders[(size_t) copy]->writeConfiguration (out, samplePos);

    sendZoneState (copy, out, samplePos);
}

void CopyRouter::writeConfiguration (juce::MidiBuffer* outs, int samplePos)
{
    if (! settings.sendConfiguration)
        return;

    for (int c = 0; c < numCopies; ++c)
        encoders[(size_t) c]->writeConfiguration (outs[c], samplePos);
}

bool CopyRouter::isBusy (int copy) const noexcept
{
    if (! juce::isPositiveAndBelow (copy, maxCopies))
        return false;

    const auto& c = copies[(size_t) copy];
    return c.held > 0 || c.sustained > 0 || c.everyCopyKeys > 0;
}

int CopyRouter::getNumCopiesPlaying() const noexcept
{
    int count = 0;

    for (const auto& c : copies)
        count += c.held > 0 ? 1 : 0;

    return count;
}

void CopyRouter::reset()
{
    decoder.reset();

    // Every encoder starts afresh, its zone values back at rest too: the stream's are forgotten.
    for (auto& encoder : encoders)
    {
        encoder->stopAllNotes (discard, 0);
        encoder->resetControllers (discard, 0);
        encoder->forgetZoneValues();
        discard.clear();
    }

    for (auto& n : notes)
        n = Note {};

    for (auto& c : copies)
        c = Copy {};

    clock = 0;
    zonePitch = zonePressure = 0.0f;
    zoneTimbre = 0.5f;
    zonePitchSet = zonePressureSet = zoneTimbreSet = false;
    zoneControllers.fill (0.0f);
    zoneControllersSet.reset();
    pedalDown = false;
}

} // namespace mperoll
