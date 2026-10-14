#include "ClapNoteTranslator.h"

namespace mperoll
{

namespace
{
    // What each expression is when nothing has set it: CLAP's own defaults.
    constexpr std::array<double, 7> neutralValues
    {
        1.0,    // CLAP_NOTE_EXPRESSION_VOLUME: a gain
        0.5,    // CLAP_NOTE_EXPRESSION_PAN: centre
        0.0,    // CLAP_NOTE_EXPRESSION_TUNING: semitones
        0.0,    // CLAP_NOTE_EXPRESSION_VIBRATO
        1.0,    // CLAP_NOTE_EXPRESSION_EXPRESSION
        0.5,    // CLAP_NOTE_EXPRESSION_BRIGHTNESS
        0.0     // CLAP_NOTE_EXPRESSION_PRESSURE
    };

    constexpr double maxTuning = 120.0;     // the range CLAP defines for tuning, in semitones

    bool isNoteEvent (const juce::MidiMessage& m) noexcept
    {
        return m.isNoteOnOrOff();
    }
}

ClapNoteTranslator::ClapNoteTranslator()
{
    decoder.setSettings (decoderSettingsFor (output));
}

InputSettings ClapNoteTranslator::decoderSettingsFor (const OutputSettings& s)
{
    // Read the stream exactly as MpeEncoder writes it.
    InputSettings in;
    in.mode = s.mode == OutputSettings::Mode::singleChannel ? InputSettings::Mode::singleChannel
                                                            : InputSettings::Mode::mpe;
    in.zones = s.zones;
    in.followConfigurationMessages = true;
    in.memberBendRange = s.memberBendRange;
    in.masterBendRange = s.masterBendRange;
    in.conventionalBendRange = s.masterBendRange;   // the encoder bends a single channel with the master range
    in.timbreController = s.timbreController;
    in.timbreLsbController = s.timbreLsbController;
    in.mpePlus = s.mpePlus;
    return in;
}

void ClapNoteTranslator::setOutputSettings (const OutputSettings& newSettings)
{
    if (newSettings == output)
        return;

    output = newSettings;
    settingsChanged = true;
}

void ClapNoteTranslator::setDialects (bool synthTakesNoteEvents, bool synthTakesMidi)
{
    if (synthTakesNoteEvents == takesNoteEvents && synthTakesMidi == takesMidi)
        return;

    takesNoteEvents = synthTakesNoteEvents;
    takesMidi = synthTakesMidi;
    settingsChanged = true;
}

int ClapNoteTranslator::getNumHeldNotes() const noexcept
{
    int count = 0;

    for (const auto& n : notes)
        count += n.inUse ? 1 : 0;

    return count;
}

void ClapNoteTranslator::reset()
{
    decoder.reset();

    for (auto& n : notes)
        n = Note {};

    zonePitch.fill (0.0f);

    for (auto& known : zoneKnown)
        known.fill (false);

    midiSent = false;
}

void ClapNoteTranslator::forgetNotes()
{
    decoder.forgetNotes();

    for (auto& n : notes)
        n = Note {};
}

void ClapNoteTranslator::reconfigure (ClapEventList& out)
{
    // Whatever the old configuration started has to stop, in the dialect it was started in.
    endAllNotes (out, 0);

    // MIDI the synth was sent leaves state behind - notes, bends, pressure, pedals - that the new
    // configuration knows nothing about (and won't reset itself, since bends become tuning).
    if (midiSent && takesMidi)
    {
        target = &out;
        time = 0;

        for (int channel = 1; channel <= 16; ++channel)
        {
            sendMidi (juce::MidiMessage::allNotesOff (channel));
            sendMidi (juce::MidiMessage::controllerEvent (channel, 121, 0));     // reset all controllers
            sendMidi (juce::MidiMessage::pitchWheel (channel, 8192));
            sendMidi (juce::MidiMessage::channelPressureChange (channel, 0));
        }

        target = nullptr;
    }

    reset();
    decoder.setSettings (decoderSettingsFor (output));
    useNoteEvents = takesNoteEvents && (output.clapNoteExpressions || ! takesMidi);
    settingsChanged = false;
}

//==============================================================================
void ClapNoteTranslator::translate (const juce::MidiBuffer& midi, ClapEventList& out, int numSamples)
{
    if (settingsChanged)
        reconfigure (out);

    target = &out;
    time = 0;
    retryPendingStops();

    const auto lastSample = juce::jmax (0, numSamples - 1);

    for (const auto metadata : midi)
    {
        time = (uint32_t) juce::jlimit (0, lastSample, metadata.samplePosition);
        const auto message = metadata.getMessage();

        if (! useNoteEvents)
        {
            sendMidi (message);
            continue;
        }

        current = &message;
        forwardAsMidi = forwardToZone = false;
        decoder.process (message, *this);
        current = nullptr;

        // Notes travel as CLAP notes, never also as MIDI.
        if (isNoteEvent (message) || isProtocolMessage (message) || message.isSysEx())
            continue;

        const bool channelVoice = message.isAftertouch() || message.isChannelPressure() || message.isController();

        if (forwardToZone)
            sendToZone (message);
        else if (forwardAsMidi || ! channelVoice)
            sendMidi (message);     // a controller with no note expression, a program change...
    }

    target = nullptr;
}

void ClapNoteTranslator::endAllNotes (ClapEventList& out, uint32_t when)
{
    target = &out;
    time = when;

    for (auto& n : notes)
    {
        if (n.inUse && (n.started || n.stopPending))
            sendNote (CLAP_EVENT_NOTE_OFF, n, 0.0);

        n = Note {};
    }

    target = nullptr;
}

void ClapNoteTranslator::retryPendingStops()
{
    for (auto& n : notes)
        if (n.inUse && n.stopPending && sendNote (CLAP_EVENT_NOTE_OFF, n, 0.0))
            n = Note {};
}

//==============================================================================
ClapNoteTranslator::Note* ClapNoteTranslator::findNote (uint32_t decoderId) noexcept
{
    for (auto& n : notes)
        if (n.inUse && ! n.stopPending && n.decoderId == decoderId)
            return &n;

    return nullptr;
}

int ClapNoteTranslator::scopeChannelOf (const Note& n) const noexcept
{
    // The channel whose zone-wide messages reach this note.
    if (decoder.getSettings().mode == InputSettings::Mode::singleChannel)
        return n.channel;

    const auto& zones = decoder.getZones();
    const auto zone = zones.zoneOfChannel (n.channel);
    return zone >= 0 ? zones.masterChannel (zone) : n.channel;
}

int ClapNoteTranslator::expressionForController (int controller) const noexcept
{
    if (output.pressureTarget == OutputSettings::PressureTarget::controller && controller == output.pressureController)
        return CLAP_NOTE_EXPRESSION_PRESSURE;

    switch (controller)
    {
        case 1:     return CLAP_NOTE_EXPRESSION_VIBRATO;
        case 7:     return CLAP_NOTE_EXPRESSION_VOLUME;
        case 10:    return CLAP_NOTE_EXPRESSION_PAN;
        case 11:    return CLAP_NOTE_EXPRESSION_EXPRESSION;
        default:    return -1;
    }
}

bool ClapNoteTranslator::zoneControllerGoesAsMidi (int expression) const noexcept
{
    // A zone's mod wheel, volume, pan or expression means whatever the synth maps it to, so a synth
    // that takes MIDI gets it as MIDI. Pressure sent on a controller is still pressure.
    return takesMidi && expression != CLAP_NOTE_EXPRESSION_PRESSURE;
}

double ClapNoteTranslator::expressionValue (int expression, float normalised) noexcept
{
    const auto v = (double) juce::jlimit (0.0f, 1.0f, normalised);

    switch (expression)
    {
        case CLAP_NOTE_EXPRESSION_VOLUME:
        {
            // CLAP volume is a gain (1 = unchanged, up to 4). CC7 follows the General MIDI curve,
            // relative to its default of 100, so a channel left at the default plays at full level.
            const auto cc = std::round (v * 127.0);
            return juce::jlimit (1.0e-4, 4.0, (cc / 100.0) * (cc / 100.0));
        }

        case CLAP_NOTE_EXPRESSION_PAN:
        {
            // Put CC10's centre (64) exactly on CLAP's centre (0.5).
            const auto cc = std::round (v * 127.0);
            return cc <= 64.0 ? cc / 128.0 : 0.5 + (cc - 64.0) / 126.0;
        }

        default:
            return v;
    }
}

double ClapNoteTranslator::combine (int expression, double noteValue, double zoneValue) noexcept
{
    switch (expression)
    {
        case CLAP_NOTE_EXPRESSION_VOLUME:       return juce::jlimit (1.0e-4, 4.0, noteValue * zoneValue);
        case CLAP_NOTE_EXPRESSION_EXPRESSION:   return juce::jlimit (0.0, 1.0, noteValue * zoneValue);
        case CLAP_NOTE_EXPRESSION_PAN:
        case CLAP_NOTE_EXPRESSION_BRIGHTNESS:   return juce::jlimit (0.0, 1.0, noteValue + zoneValue - 0.5);
        default:                                return juce::jlimit (0.0, 1.0, noteValue + zoneValue);
    }
}

double ClapNoteTranslator::tuningOf (const Note& n) const noexcept
{
    return juce::jlimit (-maxTuning, maxTuning, (double) n.pitch + zonePitch[(size_t) scopeChannelOf (n)]);
}

void ClapNoteTranslator::sendCombined (const Note& n, int expression)
{
    const auto x = (size_t) expression;
    const auto scope = (size_t) scopeChannelOf (n);
    const auto zoneHas = zoneKnown[scope][x];

    if (! n.known[x] && ! zoneHas)
        return;     // nothing has set it: the synth's default stands

    const auto own = n.known[x] ? n.values[x] : neutralValues[x];
    sendExpression (n, expression, zoneHas ? combine (expression, own, zoneValues[scope][x]) : own);
}

void ClapNoteTranslator::setExpression (Note& n, int expression, double value)
{
    n.values[(size_t) expression] = value;
    n.known[(size_t) expression] = true;

    if (n.started)
        sendCombined (n, expression);
}

void ClapNoteTranslator::handleEvent (const MpeDecoder::Event& e)
{
    using Type = MpeDecoder::Event::Type;

    if (e.type == Type::notePrepare)
    {
        Note* slot = nullptr;

        for (auto& n : notes)
        {
            if (! n.inUse)
            {
                slot = &n;
                break;
            }
        }

        if (slot == nullptr)
        {
            // The decoder has as many slots as we do and frees one first, so this shouldn't happen.
            slot = &notes[0];
            sendNote (CLAP_EVENT_NOTE_OFF, *slot, 0.0);
        }

        *slot = Note {};
        slot->decoderId = e.noteId;
        slot->clapId = (int32_t) (e.noteId & 0x7fffffffu);
        slot->key = e.key;
        slot->channel = e.channel;
        slot->inUse = true;
        return;
    }

    if (e.type >= Type::zonePitch)
    {
        handleZoneEvent (e);
        return;
    }

    auto* n = findNote (e.noteId);

    if (n == nullptr)
        return;

    switch (e.type)
    {
        case Type::notePitch:
            n->pitch = e.value;

            if (n->started)
                sendExpression (*n, CLAP_NOTE_EXPRESSION_TUNING, tuningOf (*n));

            break;

        case Type::notePressure:
            setExpression (*n, CLAP_NOTE_EXPRESSION_PRESSURE, juce::jlimit (0.0f, 1.0f, e.value));
            break;

        case Type::noteTimbre:
            setExpression (*n, CLAP_NOTE_EXPRESSION_BRIGHTNESS, juce::jlimit (0.0f, 1.0f, e.value));
            break;

        case Type::noteController:
        {
            const auto expression = expressionForController (e.controller);

            if (expression >= 0)
                setExpression (*n, expression, expressionValue (expression, e.value));
            else if (current != nullptr && current->isController())
                forwardAsMidi = true;   // the controller itself, once however many notes share its channel
            else
                sendMidi (juce::MidiMessage::controllerEvent (e.channel, e.controller,
                                                              juce::jlimit (0, 127, juce::roundToInt (e.value * 127.0f))));

            break;
        }

        case Type::noteStart:
        {
            n->started = true;
            sendNote (CLAP_EVENT_NOTE_ON, *n, e.value);

            const auto tuning = tuningOf (*n);

            if (std::abs (tuning) > 1.0e-6)
                sendExpression (*n, CLAP_NOTE_EXPRESSION_TUNING, tuning);

            for (int expression = 0; expression < numExpressions; ++expression)
                if (expression != CLAP_NOTE_EXPRESSION_TUNING)
                    sendCombined (*n, expression);

            break;
        }

        case Type::noteStop:
            if (! n->started)
            {
                *n = Note {};
            }
            else if (sendNote (CLAP_EVENT_NOTE_OFF, *n, e.value))
            {
                *n = Note {};
            }
            else
            {
                n->stopPending = true;      // no room left in this block: end it at the start of the next
            }

            break;

        case Type::notePrepare:
        case Type::zonePitch:
        case Type::zonePressure:
        case Type::zoneTimbre:
        case Type::zoneController:
            break;
    }
}

void ClapNoteTranslator::handleZoneEvent (const MpeDecoder::Event& e)
{
    using Type = MpeDecoder::Event::Type;
    const auto channel = (size_t) juce::jlimit (1, 16, e.channel);

    if (e.type == Type::zonePitch)
    {
        zonePitch[channel] = e.value;

        for (auto& n : notes)
            if (n.inUse && n.started && ! n.stopPending && (size_t) scopeChannelOf (n) == channel)
                sendExpression (n, CLAP_NOTE_EXPRESSION_TUNING, tuningOf (n));

        return;
    }

    int expression = -1;
    double value = 0.0;

    if (e.type == Type::zonePressure)
    {
        expression = CLAP_NOTE_EXPRESSION_PRESSURE;
        value = juce::jlimit (0.0f, 1.0f, e.value);
    }
    else if (e.type == Type::zoneTimbre)
    {
        expression = CLAP_NOTE_EXPRESSION_BRIGHTNESS;
        value = juce::jlimit (0.0f, 1.0f, e.value);
    }
    else if (e.type == Type::zoneController)
    {
        expression = expressionForController (e.controller);

        if (expression < 0 || zoneControllerGoesAsMidi (expression))
        {
            forwardToZone = true;
            return;
        }

        value = expressionValue (expression, e.value);
    }

    if (expression < 0)
        return;

    // Combined into every note of the zone, as an MPE synth combines a zone's values with each note's.
    zoneValues[channel][(size_t) expression] = value;
    zoneKnown[channel][(size_t) expression] = true;

    for (auto& n : notes)
        if (n.inUse && n.started && ! n.stopPending && (size_t) scopeChannelOf (n) == channel)
            sendCombined (n, expression);
}

//==============================================================================
bool ClapNoteTranslator::sendNote (uint16_t type, const Note& n, double velocity)
{
    if (target == nullptr)
        return false;

    clap_event_note_t event {};
    event.header = makeClapHeader<clap_event_note_t> (type, time);
    event.note_id = n.clapId;
    event.port_index = 0;
    event.channel = (int16_t) (n.channel - 1);
    event.key = (int16_t) n.key;
    event.velocity = juce::jlimit (0.0, 1.0, velocity);
    return target->push (event, ClapEventList::Priority::essential);
}

void ClapNoteTranslator::sendExpression (const Note& n, int expression, double value)
{
    if (target == nullptr)
        return;

    clap_event_note_expression_t event {};
    event.header = makeClapHeader<clap_event_note_expression_t> (CLAP_EVENT_NOTE_EXPRESSION, time);
    event.expression_id = expression;
    event.note_id = n.clapId;
    event.port_index = 0;
    event.channel = (int16_t) (n.channel - 1);
    event.key = (int16_t) n.key;
    event.value = value;
    target->push (event);
}

void ClapNoteTranslator::sendMidi (const juce::MidiMessage& message)
{
    if (target == nullptr || ! takesMidi || message.isSysEx())
        return;

    const auto size = message.getRawDataSize();

    if (size < 1 || size > 3)
        return;

    clap_event_midi_t event {};
    event.header = makeClapHeader<clap_event_midi_t> (CLAP_EVENT_MIDI, time);
    event.port_index = 0;

    for (int i = 0; i < size; ++i)
        event.data[i] = message.getRawData()[i];

    // Notes, and controllers such as the sustain pedal, mustn't be lost; a bend or pressure value
    // is soon replaced by the next one.
    const auto essential = message.isNoteOnOrOff() || message.isController() || message.isProgramChange();

    if (target->push (event, essential ? ClapEventList::Priority::essential : ClapEventList::Priority::normal))
        midiSent = true;
}

void ClapNoteTranslator::sendToZone (const juce::MidiMessage& message)
{
    sendMidi (message);

    // A synth that isn't in MPE mode applies a channel's controllers to the notes on that channel,
    // and the notes are on the member channels.
    const auto& zones = decoder.getZones();
    const auto channel = message.getChannel();

    if (decoder.getSettings().mode == InputSettings::Mode::singleChannel || ! zones.isMasterChannel (channel))
        return;

    const auto zone = zones.zoneOfChannel (channel);

    for (int i = 0; i < zones.numMembers (zone); ++i)
    {
        auto copy = message;
        copy.setChannel (zones.memberChannel (zone, i));
        sendMidi (copy);
    }
}

bool ClapNoteTranslator::isProtocolMessage (const juce::MidiMessage& message) const noexcept
{
    if (message.isPitchWheel())
        return true;    // bends always become tuning

    if (! message.isController())
        return false;

    // Data entry and (N)RPN selection: configuration and bend ranges, already applied to tuning.
    switch (message.getControllerNumber())
    {
        case 6: case 38: case 96: case 97: case 98: case 99: case 100: case 101:
            return true;

        case 87:
            return output.mpePlus;

        case 88:
            return true;    // high resolution velocity prefix

        default:
            return false;
    }
}

} // namespace mperoll
