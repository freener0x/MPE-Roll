#include "MpeDecoder.h"
#include "MpeEncoder.h"

namespace mperoll
{

namespace
{
    constexpr int defaultReleaseVelocity = 64;
    constexpr float specMemberBendRange = 48.0f;
    constexpr float specMasterBendRange = 2.0f;
    constexpr float mpePlusFullScale = 16256.0f;    // 0x3f80: MPE+ keeps 127 as the top MSB
}

void MpeDecoder::ChannelState::reset (float range) noexcept
{
    pitchBend = 8192.0;
    bendRange = range;
    pressure = -1.0f;
    timbre = -1.0f;
    timbreMsb = -1;
    timbreLsb = 0;
    hiResPrefix = -1;
    controllers.fill (-1);
    polyPressure.fill (-1);
    rpnMsb = rpnLsb = 127;
    nrpnSelected = false;
    dataMsb = -1;
    velocityPrefix = -1;
}

MpeDecoder::MpeDecoder()
{
    setSettings ({});
}

void MpeDecoder::setSettings (const InputSettings& newSettings)
{
    settings = newSettings;
    zones = settings.zones.normalised();
    reset();
}

void MpeDecoder::reset()
{
    for (int ch = 1; ch <= 16; ++ch)
        channels[(size_t) ch].reset (defaultBendRange (ch));

    forgetNotes();
}

void MpeDecoder::forgetNotes()
{
    for (auto& n : notes)
        n = HeldNote {};
}

int MpeDecoder::getNumHeldNotes() const noexcept
{
    int count = 0;

    for (const auto& n : notes)
        count += n.inUse ? 1 : 0;

    return count;
}

//==============================================================================
bool MpeDecoder::isZoneWide (int channel) const noexcept
{
    return settings.mode == InputSettings::Mode::singleChannel || zones.isMasterChannel (channel);
}

float MpeDecoder::defaultBendRange (int channel) const noexcept
{
    if (settings.mode == InputSettings::Mode::singleChannel)
        return settings.conventionalBendRange;

    if (zones.isMasterChannel (channel))
        return settings.masterBendRange;

    if (zones.isMemberChannel (channel))
        return settings.memberBendRange;

    return settings.conventionalBendRange;
}

float MpeDecoder::timbreValue (const ChannelState& st) const noexcept
{
    return st.timbre >= 0.0f ? st.timbre : 0.5f;
}

float MpeDecoder::channelPitch (int channel) const noexcept
{
    const auto& st = channels[(size_t) channel];
    const auto offset = st.pitchBend - 8192.0;
    return (float) (offset >= 0.0 ? offset / 8191.0 * st.bendRange : offset / 8192.0 * st.bendRange);
}

int MpeDecoder::takeHiResPrefix (ChannelState& st) const noexcept
{
    // MPE+: a CC87 sent just before a timbre, pressure or bend message carries 7 extra low bits.
    const auto prefix = settings.mpePlus && st.hiResPrefix >= 0 ? st.hiResPrefix : 0;
    st.hiResPrefix = -1;
    return prefix;
}

template <typename Fn>
void MpeDecoder::forEachNoteOnChannel (int channel, Fn&& fn)
{
    for (auto& n : notes)
        if (n.inUse && n.channel == channel)
            fn (n);
}

void MpeDecoder::emit (Listener& listener, Event::Type type, uint32_t id, int key, int channel, float value, int controller)
{
    Event e;
    e.type = type;
    e.noteId = id;
    e.key = key;
    e.channel = channel;
    e.value = value;
    e.controller = controller;
    listener.handleEvent (e);
}

//==============================================================================
void MpeDecoder::process (const juce::MidiMessage& message, Listener& listener)
{
    const auto channel = message.getChannel();

    if (channel < 1 || channel > 16)
        return;

    auto& st = channels[(size_t) channel];

    if (message.isNoteOn())
    {
        handleNoteOn (channel, message.getNoteNumber(), message.getVelocity(), listener);
    }
    else if (message.isNoteOff (true))
    {
        const auto velocity = message.isNoteOn (true) ? defaultReleaseVelocity : (int) message.getVelocity();
        handleNoteOff (channel, message.getNoteNumber(), velocity, listener);
    }
    else if (message.isPitchWheel())
    {
        st.pitchBend = message.getPitchWheelValue() + takeHiResPrefix (st) / 128.0;
        const auto semitones = channelPitch (channel);

        if (isZoneWide (channel))
            emit (listener, Event::Type::zonePitch, 0, 0, channel, semitones);
        else
            forEachNoteOnChannel (channel, [&] (const HeldNote& n)
                                  { emit (listener, Event::Type::notePitch, n.id, n.key, channel, semitones); });
    }
    else if (message.isChannelPressure())
    {
        const auto msb = message.getChannelPressureValue();
        st.pressure = settings.mpePlus ? juce::jmin (1.0f, (float) ((msb << 7) | takeHiResPrefix (st)) / mpePlusFullScale)
                                       : (float) msb / 127.0f;
        const auto value = st.pressure;

        if (isZoneWide (channel))
            emit (listener, Event::Type::zonePressure, 0, 0, channel, value);
        else
            forEachNoteOnChannel (channel, [&] (const HeldNote& n)
                                  { emit (listener, Event::Type::notePressure, n.id, n.key, channel, value); });
    }
    else if (message.isAftertouch())
    {
        const auto key = message.getNoteNumber();
        const auto value = (float) message.getAfterTouchValue() / 127.0f;
        st.polyPressure[(size_t) key] = (int8_t) message.getAfterTouchValue();

        forEachNoteOnChannel (channel, [&] (const HeldNote& n)
                              {
                                  if (n.key == key)
                                      emit (listener, Event::Type::notePressure, n.id, n.key, channel, value);
                              });
    }
    else if (message.isController())
    {
        handleController (channel, message.getControllerNumber(), message.getControllerValue(), listener);
    }
}

void MpeDecoder::handleNoteOn (int channel, int key, int velocity, Listener& listener)
{
    // A repeated key on the same channel ends the previous note.
    forEachNoteOnChannel (channel, [&] (HeldNote& n)
                          {
                              if (n.key == key)
                              {
                                  emit (listener, Event::Type::noteStop, n.id, n.key, channel, 0.5f);
                                  n = HeldNote {};
                              }
                          });

    HeldNote* slot = nullptr;

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
        // Out of slots: end the oldest note.
        slot = &notes[0];

        for (auto& n : notes)
            if (n.id < slot->id)
                slot = &n;

        emit (listener, Event::Type::noteStop, slot->id, slot->key, slot->channel, 0.5f);
    }

    if (nextId == 0)
        nextId = 1;

    *slot = { nextId++, channel, key, true };
    const auto id = slot->id;
    auto& st = channels[(size_t) channel];

    emit (listener, Event::Type::notePrepare, id, key, channel, 0.0f);

    if (! isZoneWide (channel))
    {
        emit (listener, Event::Type::notePitch, id, key, channel, channelPitch (channel));

        if (st.pressure >= 0.0f)
            emit (listener, Event::Type::notePressure, id, key, channel, st.pressure);

        if (st.timbre >= 0.0f)
            emit (listener, Event::Type::noteTimbre, id, key, channel, timbreValue (st));

        for (int cc = 0; cc < 128; ++cc)
        {
            const auto value = st.controllers[(size_t) cc];

            if (value >= 0 && cc != settings.timbreController && cc != settings.timbreLsbController
                && ! isReservedController (cc))
                emit (listener, Event::Type::noteController, id, key, channel, (float) value / 127.0f, cc);
        }
    }

    // Poly aftertouch is the key's own pressure, sent before the note when it starts pressed.
    if (const auto poly = st.polyPressure[(size_t) key]; poly >= 0)
        emit (listener, Event::Type::notePressure, id, key, channel, (float) poly / 127.0f);

    float strike = (float) velocity / 127.0f;

    if (st.velocityPrefix >= 0)
    {
        strike = (float) ((velocity << 7) | st.velocityPrefix) / 16383.0f;
        st.velocityPrefix = -1;
    }

    emit (listener, Event::Type::noteStart, id, key, channel, strike);
}

void MpeDecoder::handleNoteOff (int channel, int key, int velocity, Listener& listener)
{
    auto& st = channels[(size_t) channel];
    float lift = (float) velocity / 127.0f;

    if (st.velocityPrefix >= 0)
    {
        lift = (float) ((velocity << 7) | st.velocityPrefix) / 16383.0f;
        st.velocityPrefix = -1;
    }

    for (auto& n : notes)
    {
        if (n.inUse && n.channel == channel && n.key == key)
        {
            emit (listener, Event::Type::noteStop, n.id, n.key, channel, lift);
            n = HeldNote {};
            return;
        }
    }
}

void MpeDecoder::handleController (int channel, int controller, int value, Listener& listener)
{
    auto& st = channels[(size_t) channel];
    const auto zoneWide = isZoneWide (channel);

    auto emitTimbre = [&]
    {
        const auto timbre = timbreValue (st);

        if (zoneWide)
            emit (listener, Event::Type::zoneTimbre, 0, 0, channel, timbre);
        else
            forEachNoteOnChannel (channel, [&] (const HeldNote& n)
                                  { emit (listener, Event::Type::noteTimbre, n.id, n.key, channel, timbre); });
    };

    if (settings.mpePlus && controller == 87)
    {
        st.hiResPrefix = value;
        return;
    }

    switch (controller)
    {
        case 101:   st.rpnMsb = value; st.nrpnSelected = false; return;
        case 100:   st.rpnLsb = value; st.nrpnSelected = false; return;
        case 99:
        case 98:    st.nrpnSelected = true; return;
        case 88:    st.velocityPrefix = value; return;
        case 0: case 32: case 96: case 97: case 122:
            return;

        case 6:
            st.dataMsb = value;
            handleDataEntry (channel, listener);
            return;

        case 38:
            if (! st.nrpnSelected && st.rpnMsb == 0 && st.rpnLsb == 0 && st.dataMsb >= 0)
            {
                const auto range = (float) st.dataMsb + (float) value / 100.0f;

                if (settings.mode == InputSettings::Mode::mpe && zones.isMemberChannel (channel))
                {
                    const auto zone = zones.zoneOfChannel (channel);

                    for (int i = 0; i < zones.numMembers (zone); ++i)
                        channels[(size_t) zones.memberChannel (zone, i)].bendRange = range;
                }
                else
                {
                    st.bendRange = range;
                }
            }
            return;

        case 120: case 123: case 124: case 125: case 126: case 127:
            stopNotes (channel, zones.isMasterChannel (channel) || settings.mode == InputSettings::Mode::singleChannel, listener);
            return;

        case 121:
        {
            st.pitchBend = 8192.0;
            st.pressure = -1.0f;

            if (zoneWide)
            {
                emit (listener, Event::Type::zonePitch, 0, 0, channel, 0.0f);
                emit (listener, Event::Type::zonePressure, 0, 0, channel, 0.0f);
            }
            else
            {
                forEachNoteOnChannel (channel, [&] (const HeldNote& n)
                                      {
                                          emit (listener, Event::Type::notePitch, n.id, n.key, channel, 0.0f);
                                          emit (listener, Event::Type::notePressure, n.id, n.key, channel, 0.0f);
                                      });
            }

            for (int cc : { 1, 11, 64, 65, 66, 67, 68, 69 })
            {
                if (st.controllers[(size_t) cc] < 0)
                    continue;

                const auto rest = cc == 11 ? 1.0f : 0.0f;
                st.controllers[(size_t) cc] = (int8_t) (rest * 127.0f);

                if (zoneWide)
                    emit (listener, Event::Type::zoneController, 0, 0, channel, rest, cc);
                else
                    forEachNoteOnChannel (channel, [&] (const HeldNote& n)
                                          { emit (listener, Event::Type::noteController, n.id, n.key, channel, rest, cc); });
            }
            return;
        }

        default:
            break;
    }

    if (controller == settings.timbreController)
    {
        st.timbreMsb = value;
        st.timbreLsb = 0;

        if (settings.mpePlus)
            st.timbre = juce::jmin (1.0f, (float) ((value << 7) | takeHiResPrefix (st)) / mpePlusFullScale);
        else if (settings.timbreLsbController >= 0)
            st.timbre = (float) (value << 7) / 16383.0f;
        else
            st.timbre = (float) value / 127.0f;

        emitTimbre();
        return;
    }

    if (controller == settings.timbreLsbController)
    {
        st.timbreLsb = value;

        if (st.timbreMsb >= 0)
        {
            st.timbre = (float) ((st.timbreMsb << 7) | st.timbreLsb) / 16383.0f;
            emitTimbre();
        }

        return;
    }

    st.controllers[(size_t) controller] = (int8_t) value;
    const auto normalised = (float) value / 127.0f;

    if (zoneWide)
        emit (listener, Event::Type::zoneController, 0, 0, channel, normalised, controller);
    else
        forEachNoteOnChannel (channel, [&] (const HeldNote& n)
                              { emit (listener, Event::Type::noteController, n.id, n.key, channel, normalised, controller); });
}

void MpeDecoder::handleDataEntry (int channel, Listener&)
{
    auto& st = channels[(size_t) channel];

    if (st.nrpnSelected)
        return;

    const auto parameter = (st.rpnMsb << 7) | st.rpnLsb;

    if (parameter == 0)
    {
        const auto range = (float) st.dataMsb;

        // Pitch bend sensitivity sent on any member channel applies to the whole zone.
        if (settings.mode == InputSettings::Mode::mpe && zones.isMemberChannel (channel))
        {
            const auto zone = zones.zoneOfChannel (channel);

            for (int i = 0; i < zones.numMembers (zone); ++i)
                channels[(size_t) zones.memberChannel (zone, i)].bendRange = range;
        }
        else
        {
            st.bendRange = range;
        }
    }
    else if (parameter == 6 && (channel == 1 || channel == 16)
             && settings.mode == InputSettings::Mode::mpe && settings.followConfigurationMessages)
    {
        applyConfigurationMessage (channel, st.dataMsb);
    }
}

void MpeDecoder::applyConfigurationMessage (int channel, int members)
{
    members = juce::jlimit (0, 15, members);

    if (channel == 1)
    {
        zones.lowerMembers = members;

        if (zones.upperMembers > 0 && zones.lowerMembers + zones.upperMembers > 14)
            zones.upperMembers = juce::jmax (0, 14 - zones.lowerMembers);
    }
    else
    {
        zones.upperMembers = members;

        if (zones.lowerMembers > 0 && zones.lowerMembers + zones.upperMembers > 14)
            zones.lowerMembers = juce::jmax (0, 14 - zones.upperMembers);
    }

    // The MPE specification resets the bend ranges whenever the layout changes.
    for (int ch = 1; ch <= 16; ++ch)
    {
        auto& st = channels[(size_t) ch];

        if (zones.isMasterChannel (ch))
            st.bendRange = specMasterBendRange;
        else if (zones.isMemberChannel (ch))
            st.bendRange = specMemberBendRange;
        else
            st.bendRange = settings.conventionalBendRange;
    }
}

void MpeDecoder::stopNotes (int channel, bool wholeZone, Listener& listener)
{
    const auto zone = zones.zoneOfChannel (channel);
    const bool everything = settings.mode == InputSettings::Mode::singleChannel;

    for (auto& n : notes)
    {
        if (! n.inUse)
            continue;

        const bool matches = n.channel == channel
                          || (wholeZone && (everything || (zone >= 0 && zones.zoneOfChannel (n.channel) == zone)));

        if (matches)
        {
            emit (listener, Event::Type::noteStop, n.id, n.key, n.channel, 0.5f);
            n = HeldNote {};
        }
    }
}

void MpeDecoder::stopAllNotes (Listener& listener)
{
    for (auto& n : notes)
    {
        if (n.inUse)
        {
            emit (listener, Event::Type::noteStop, n.id, n.key, n.channel, 0.5f);
            n = HeldNote {};
        }
    }
}

} // namespace mperoll
