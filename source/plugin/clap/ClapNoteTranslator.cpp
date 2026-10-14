#include "ClapNoteTranslator.h"

namespace mperoll
{

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

    midiNotesMaySound = false;
}

void ClapNoteTranslator::reconfigure (ClapEventList& out)
{
    // Whatever the old configuration started has to stop, in the dialect it was started in.
    endAllNotes (out, 0);

    if (midiNotesMaySound && takesMidi)
    {
        target = &out;
        time = 0;

        for (int channel = 1; channel <= 16; ++channel)
            sendMidi (juce::MidiMessage::allNotesOff (channel));

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
    const auto lastSample = juce::jmax (0, numSamples - 1);

    for (const auto metadata : midi)
    {
        time = (uint32_t) juce::jlimit (0, lastSample, metadata.samplePosition);
        const auto message = metadata.getMessage();

        if (! useNoteEvents)
        {
            sendMidi (message);
            midiNotesMaySound = midiNotesMaySound || message.isNoteOn();
            continue;
        }

        forwardAsMidi = false;
        decoder.process (message, *this);

        const bool channelVoice = message.isNoteOnOrOff() || message.isAftertouch() || message.isPitchWheel()
                               || message.isChannelPressure() || message.isController();

        // Notes and their expression now travel as CLAP events. What's left - zone-wide controllers,
        // controllers with no note expression, program changes - goes on as MIDI.
        if ((forwardAsMidi || (! channelVoice && ! message.isSysEx())) && ! isProtocolMessage (message))
            sendMidi (message);
    }

    target = nullptr;
}

void ClapNoteTranslator::endAllNotes (ClapEventList& out, uint32_t when)
{
    target = &out;
    time = when;

    for (auto& n : notes)
    {
        if (n.inUse && n.started)
            sendNote (CLAP_EVENT_NOTE_OFF, n, 0.0);

        n = Note {};
    }

    target = nullptr;
}

//==============================================================================
ClapNoteTranslator::Note* ClapNoteTranslator::findNote (uint32_t decoderId) noexcept
{
    for (auto& n : notes)
        if (n.inUse && n.decoderId == decoderId)
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

double ClapNoteTranslator::expressionValue (int expression, float normalised) noexcept
{
    const auto v = (double) juce::jlimit (0.0f, 1.0f, normalised);

    switch (expression)
    {
        case CLAP_NOTE_EXPRESSION_VOLUME:
            // CLAP volume is a gain (1 = unchanged, above 0). CC7 follows the General MIDI curve.
            return juce::jmax (1.0e-4, v * v);

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

void ClapNoteTranslator::setExpression (Note& n, int expression, double value)
{
    n.values[(size_t) expression] = value;
    n.known[(size_t) expression] = true;

    if (n.started)
        sendExpression (n, expression, value);
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
                sendExpression (*n, CLAP_NOTE_EXPRESSION_TUNING, n->pitch + zonePitch[(size_t) scopeChannelOf (*n)]);

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
            else
                forwardAsMidi = true;

            break;
        }

        case Type::noteStart:
        {
            n->started = true;
            sendNote (CLAP_EVENT_NOTE_ON, *n, e.value);

            const auto scope = (size_t) scopeChannelOf (*n);
            const auto tuning = n->pitch + zonePitch[scope];

            if (std::abs (tuning) > 1.0e-6f)
                sendExpression (*n, CLAP_NOTE_EXPRESSION_TUNING, tuning);

            for (int expression = 0; expression < numExpressions; ++expression)
            {
                if (expression == CLAP_NOTE_EXPRESSION_TUNING)
                    continue;

                if (n->known[(size_t) expression])
                    sendExpression (*n, expression, n->values[(size_t) expression]);
                else if (zoneKnown[scope][(size_t) expression])
                    sendExpression (*n, expression, zoneValues[scope][(size_t) expression]);
            }

            break;
        }

        case Type::noteStop:
            if (n->started)
                sendNote (CLAP_EVENT_NOTE_OFF, *n, e.value);

            *n = Note {};
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
            if (n.inUse && n.started && (size_t) scopeChannelOf (n) == channel)
                sendExpression (n, CLAP_NOTE_EXPRESSION_TUNING, n.pitch + e.value);

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
        value = expressionValue (expression, e.value);
    }

    // A synth that takes MIDI gets zone-wide messages as they are: it knows best how its own
    // aftertouch, mod wheel or sustain should combine with each note.
    if (takesMidi || expression < 0)
    {
        forwardAsMidi = true;
        return;
    }

    // Otherwise apply them to every note in the zone that has no value of its own.
    zoneValues[channel][(size_t) expression] = value;
    zoneKnown[channel][(size_t) expression] = true;

    for (auto& n : notes)
        if (n.inUse && n.started && ! n.known[(size_t) expression] && (size_t) scopeChannelOf (n) == channel)
            sendExpression (n, expression, value);
}

//==============================================================================
void ClapNoteTranslator::sendNote (uint16_t type, const Note& n, double velocity)
{
    if (target == nullptr)
        return;

    clap_event_note_t event {};
    event.header = makeClapHeader<clap_event_note_t> (type, time);
    event.note_id = n.clapId;
    event.port_index = 0;
    event.channel = (int16_t) (n.channel - 1);
    event.key = (int16_t) n.key;
    event.velocity = juce::jlimit (0.0, 1.0, velocity);
    target->push (event);
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

    target->push (event);
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
