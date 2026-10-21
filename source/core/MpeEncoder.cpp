#include "MpeEncoder.h"

#include <cmath>

namespace mperoll
{

namespace
{
    constexpr int pitchBendCentre = 8192;
    constexpr int centreKey = pitchBendCentre << 7;
    constexpr int velocityPrefixController = 88;
    constexpr int mpePlusPrefixController = 87;
    constexpr float mpePlusFullScale = 16256.0f;    // 0x3f80: MPE+ keeps 127 as the top MSB
    constexpr float defaultLift = 0.5f;
}

void MpeEncoder::ChannelState::clearValues() noexcept
{
    pitchBend = -1;
    pressure = -1;
    timbre = -1;
    controllers.fill (-1);
    polyPressure.fill (-1);
    controllersTouched.reset();
    pitchTouched = pressureTouched = timbreTouched = false;
}

// The voices live on the heap, allocated once here, so an encoder itself stays small: with each
// voice carrying its own controller values a full table is some 50 KB, and the unit tests hold
// dozens of encoders as locals of one function, which overflowed the 1 MB main-thread stack on
// Windows (MSVC gives every local its own slot). Nothing else allocates after construction.
MpeEncoder::MpeEncoder()
    : voices ((size_t) maxVoices)
{
    static_assert (sizeof (MpeEncoder) < 16384, "MpeEncoder should stay small enough to live on the stack");

    for (auto& c : channels)
        c.clearValues();
}

void MpeEncoder::setSettings (const OutputSettings& newSettings)
{
    // The deduplication caches are in a format that depends on these, so start over when they change.
    if (newSettings.mpePlus != settings.mpePlus || newSettings.timbreController != settings.timbreController
        || newSettings.timbreLsbController != settings.timbreLsbController)
        for (auto& c : channels)
            c.clearValues();

    settings = newSettings;
    settings.zones = settings.zones.normalised();
    settings.singleChannel = juce::jlimit (1, 16, settings.singleChannel);
}

//==============================================================================
int MpeEncoder::semitonesToPitchBend (float semitones, float range) noexcept
{
    if (range <= 0.0f)
        return pitchBendCentre;

    const auto normalised = juce::jlimit (-1.0f, 1.0f, semitones / range);
    const auto scaled = normalised >= 0.0f ? normalised * 8191.0f : normalised * 8192.0f;
    return juce::jlimit (0, 16383, pitchBendCentre + (int) std::lround (scaled));
}

double MpeEncoder::semitonesToPitchBendFine (float semitones, float range) noexcept
{
    if (range <= 0.0f)
        return pitchBendCentre;

    const auto normalised = juce::jlimit (-1.0, 1.0, (double) semitones / range);
    return juce::jlimit (0.0, 16383.0, pitchBendCentre + (normalised >= 0.0 ? normalised * 8191.0 : normalised * 8192.0));
}

float MpeEncoder::pitchBendToSemitones (int value, float range) noexcept
{
    const auto offset = value - pitchBendCentre;
    return offset >= 0 ? (float) offset / 8191.0f * range
                       : (float) offset / 8192.0f * range;
}

int MpeEncoder::toSevenBit (float value) noexcept
{
    return juce::jlimit (0, 127, (int) std::lround (value * 127.0f));
}

int MpeEncoder::toFourteenBit (float value) noexcept
{
    return juce::jlimit (0, 16383, (int) std::lround (value * 16383.0f));
}

//==============================================================================
bool MpeEncoder::isSingleChannelMode() const noexcept
{
    return settings.mode == OutputSettings::Mode::singleChannel || ! settings.zones.hasAnyZone();
}

bool MpeEncoder::combinesZonePressure() const noexcept
{
    return isSingleChannelMode() && settings.pressureTarget == OutputSettings::PressureTarget::channelPressure;
}

int MpeEncoder::zoneForKey (int key) const noexcept
{
    const auto& z = settings.zones;

    if (z.hasLowerZone() && z.hasUpperZone())
        return key < settings.splitKey ? 0 : 1;

    return z.hasLowerZone() ? 0 : 1;
}

template <typename Fn>
void MpeEncoder::forEachZoneMaster (Fn&& fn) const
{
    if (isSingleChannelMode())
    {
        fn (settings.singleChannel);
        return;
    }

    if (settings.zones.hasLowerZone())
        fn (1);

    if (settings.zones.hasUpperZone())
        fn (16);
}

void MpeEncoder::forgetZoneValues() noexcept
{
    forEachZoneMaster ([this] (int ch) { channels[(size_t) ch].clearValues(); });
}

MpeEncoder::Voice* MpeEncoder::findVoice (Handle handle) noexcept
{
    for (int i = 0; i < numActive; ++i)
        if (voices[(size_t) activeList[(size_t) i]].handle == handle)
            return &voices[(size_t) activeList[(size_t) i]];

    return nullptr;
}

const MpeEncoder::Voice* MpeEncoder::findVoice (Handle handle) const noexcept
{
    return const_cast<MpeEncoder*> (this)->findVoice (handle);
}

MpeEncoder::Voice* MpeEncoder::findVoiceOnChannel (int channel, int key) noexcept
{
    for (int i = 0; i < numActive; ++i)
    {
        auto& v = voices[(size_t) activeList[(size_t) i]];

        if (v.channel == channel && v.key == key)
            return &v;
    }

    return nullptr;
}

MpeEncoder::Voice* MpeEncoder::oldestVoice (int zone) noexcept
{
    Voice* oldest = nullptr;

    for (int i = 0; i < numActive; ++i)
    {
        auto& v = voices[(size_t) activeList[(size_t) i]];

        if ((zone < 0 || settings.zones.zoneOfChannel (v.channel) == zone)
            && (oldest == nullptr || v.order < oldest->order))
            oldest = &v;
    }

    return oldest;
}

const MpeEncoder::Voice* MpeEncoder::leadVoice() const noexcept
{
    const Voice* lead = nullptr;

    for (int i = 0; i < numActive; ++i)
    {
        const auto& v = voices[(size_t) activeList[(size_t) i]];

        if (lead == nullptr || v.order > lead->order)
            lead = &v;
    }

    return lead;
}

bool MpeEncoder::isLead (const Voice& v) const noexcept
{
    return leadVoice() == &v;
}

MpeEncoder::Voice* MpeEncoder::allocateVoice (juce::MidiBuffer& out, int samplePos)
{
    if (numActive >= maxVoices)
        if (auto* oldest = oldestVoice (-1))
            releaseVoice (out, samplePos, *oldest, defaultLift);

    for (auto& v : voices)
        if (! v.inUse)
            return &v;

    jassertfalse;
    return nullptr;
}

void MpeEncoder::releaseVoice (juce::MidiBuffer& out, int samplePos, Voice& v, float lift)
{
    const bool single = isSingleChannelMode();
    const bool wasLead = single && isLead (v);

    if (v.started)
    {
        if (settings.highResVelocity)
        {
            const auto v14 = toFourteenBit (lift);
            out.addEvent (juce::MidiMessage::controllerEvent (v.channel, velocityPrefixController, v14 & 127), samplePos);
            out.addEvent (juce::MidiMessage::noteOff (v.channel, v.key, (juce::uint8) (v14 >> 7)), samplePos);
        }
        else
        {
            out.addEvent (juce::MidiMessage::noteOff (v.channel, v.key, (juce::uint8) toSevenBit (lift)), samplePos);
        }
    }

    auto& st = channels[(size_t) v.channel];
    st.numVoices = juce::jmax (0, st.numVoices - 1);
    st.lastUsed = ++clock;

    const auto index = (int) (&v - voices.data());

    for (int i = 0; i < numActive; ++i)
    {
        if (activeList[(size_t) i] == index)
        {
            activeList[(size_t) i] = activeList[(size_t) (numActive - 1)];
            --numActive;
            break;
        }
    }

    // The next newest note takes over the channel. After the last one the channel keeps its values, even
    // as zone lanes change, so the release tail doesn't jump; the next note sends its own before its Note On.
    if (wasLead && numActive == 0)
    {
        tailPitch = v.pitch;
        tailPressure = v.pressure;
    }

    v = Voice {};

    if (wasLead && numActive > 0)
        sendLeadValues (out, samplePos);
}

int MpeEncoder::chooseMemberChannel (juce::MidiBuffer& out, int samplePos, int zone, int key)
{
    const auto numMembers = settings.zones.numMembers (zone);
    int best = -1;

    // Prefer an idle channel, the one released longest ago so release tails aren't disturbed.
    for (int i = 0; i < numMembers; ++i)
    {
        const auto ch = settings.zones.memberChannel (zone, i);
        const auto& st = channels[(size_t) ch];

        if (st.numVoices == 0 && (best < 0 || st.lastUsed < channels[(size_t) best].lastUsed))
            best = ch;
    }

    if (best >= 0)
        return best;

    if (settings.overflow == OutputSettings::Overflow::stealOldest)
    {
        if (auto* oldest = oldestVoice (zone))
        {
            const auto ch = oldest->channel;
            releaseVoice (out, samplePos, *oldest, defaultLift);

            if (auto* sameKey = findVoiceOnChannel (ch, key))
                releaseVoice (out, samplePos, *sameKey, defaultLift);

            return ch;
        }
    }

    // Share the least busy channel. Notes on it will follow each other's expression.
    for (int i = 0; i < numMembers; ++i)
    {
        const auto ch = settings.zones.memberChannel (zone, i);

        if (findVoiceOnChannel (ch, key) != nullptr)
            continue;

        const auto& st = channels[(size_t) ch];

        if (best < 0
            || st.numVoices < channels[(size_t) best].numVoices
            || (st.numVoices == channels[(size_t) best].numVoices && st.lastUsed < channels[(size_t) best].lastUsed))
            best = ch;
    }

    if (best < 0)
    {
        best = settings.zones.memberChannel (zone, 0);

        if (auto* sameKey = findVoiceOnChannel (best, key))
            releaseVoice (out, samplePos, *sameKey, defaultLift);
    }

    return best;
}

//==============================================================================
void MpeEncoder::sendRpn (juce::MidiBuffer& out, int samplePos, int channel, int parameter, int msb, int lsb, bool sendLsb)
{
    out.addEvent (juce::MidiMessage::controllerEvent (channel, 101, (parameter >> 7) & 127), samplePos);
    out.addEvent (juce::MidiMessage::controllerEvent (channel, 100, parameter & 127), samplePos);
    out.addEvent (juce::MidiMessage::controllerEvent (channel, 6, juce::jlimit (0, 127, msb)), samplePos);

    if (sendLsb)
        out.addEvent (juce::MidiMessage::controllerEvent (channel, 38, juce::jlimit (0, 127, lsb)), samplePos);

    out.addEvent (juce::MidiMessage::controllerEvent (channel, 101, 127), samplePos);
    out.addEvent (juce::MidiMessage::controllerEvent (channel, 100, 127), samplePos);
}

void MpeEncoder::writeConfiguration (juce::MidiBuffer& out, int samplePos)
{
    auto sendBendRange = [&] (int channel, float range)
    {
        const auto semitones = (int) std::floor (range);
        const auto cents = (int) std::lround ((range - (float) semitones) * 100.0f);
        sendRpn (out, samplePos, channel, 0, semitones, cents, true);
    };

    if (isSingleChannelMode())
    {
        sendBendRange (settings.singleChannel, settings.masterBendRange);
        return;
    }

    for (int zone = 0; zone < 2; ++zone)
    {
        const auto members = settings.zones.numMembers (zone);

        if (members <= 0)
            continue;

        const auto master = settings.zones.masterChannel (zone);

        // MPE Configuration Message (RPN 6). Receivers reset bend ranges to 48/2 when they see it,
        // so the sensitivities are sent afterwards.
        sendRpn (out, samplePos, master, 6, members, 0, false);
        sendBendRange (master, settings.masterBendRange);

        for (int i = 0; i < members; ++i)
            sendBendRange (settings.zones.memberChannel (zone, i), settings.memberBendRange);
    }
}

//==============================================================================
void MpeEncoder::prepareNote (juce::MidiBuffer& out, int samplePos, Handle handle, int key)
{
    key = juce::jlimit (0, 127, key);

    if (auto* existing = findVoice (handle))
        releaseVoice (out, samplePos, *existing, defaultLift);

    auto* voice = allocateVoice (out, samplePos);

    if (voice == nullptr)
        return;

    int channel = settings.singleChannel;

    if (isSingleChannelMode())
    {
        if (auto* sameKey = findVoiceOnChannel (channel, key))
            releaseVoice (out, samplePos, *sameKey, defaultLift);
    }
    else
    {
        channel = chooseMemberChannel (out, samplePos, zoneForKey (key), key);
    }

    auto& st = channels[(size_t) channel];
    st.numVoices++;
    st.lastUsed = ++clock;

    if (st.numVoices == 1)
    {
        st.pitchTouched = st.pressureTouched = st.timbreTouched = false;
        st.controllersTouched.reset();
    }

    voice->handle = handle;
    voice->channel = channel;
    voice->key = key;
    voice->inUse = true;
    voice->started = false;
    voice->order = ++clock;
    voice->pitch = 0.0f;
    voice->pressure = 0.0f;
    voice->timbre = settings.defaultTimbre;
    voice->ownControllers.reset();
    tailPitch = tailPressure = 0.0f;

    activeList[(size_t) numActive++] = (int) (voice - voices.data());
}

void MpeEncoder::startNote (juce::MidiBuffer& out, int samplePos, Handle handle, float strike)
{
    auto* v = findVoice (handle);

    if (v == nullptr || v->started)
        return;

    const auto ch = v->channel;
    auto& st = channels[(size_t) ch];

    if (isSingleChannelMode())
    {
        sendLeadValues (out, samplePos);
    }
    else if (st.numVoices == 1)
    {
        // This note owns its channel: make sure nothing left over from the previous note leaks into it.
        // A lane of its own on the timbre or pressure controller has already sent its value.
        const bool pressureOnController = settings.pressureTarget == OutputSettings::PressureTarget::controller;

        if (! st.pitchTouched)
            sendPitchBend (out, samplePos, ch, pitchBendCentre, true);

        if (! st.timbreTouched && ! st.controllersTouched[(size_t) settings.timbreController])
            sendTimbre (out, samplePos, ch, settings.defaultTimbre, true);

        if (! st.pressureTouched && ! (pressureOnController && st.controllersTouched[(size_t) settings.pressureController]))
            sendNotePressure (out, samplePos, *v, 0.0f, true);

        for (int cc = 0; cc < 128; ++cc)
        {
            if (st.controllers[(size_t) cc] < 0 || st.controllersTouched[(size_t) cc]
                || cc == settings.timbreController || cc == settings.timbreLsbController
                || (pressureOnController && cc == settings.pressureController))
                continue;

            sendController (out, samplePos, ch, cc, toSevenBit (defaultControllerValue (cc)), false);
        }
    }

    if (settings.highResVelocity)
    {
        auto v14 = toFourteenBit (strike);

        if ((v14 >> 7) == 0)
            v14 = 1 << 7;

        out.addEvent (juce::MidiMessage::controllerEvent (ch, velocityPrefixController, v14 & 127), samplePos);
        out.addEvent (juce::MidiMessage::noteOn (ch, v->key, (juce::uint8) (v14 >> 7)), samplePos);
    }
    else
    {
        out.addEvent (juce::MidiMessage::noteOn (ch, v->key, (juce::uint8) juce::jmax (1, toSevenBit (strike))), samplePos);
    }

    v->started = true;
}

void MpeEncoder::stopNote (juce::MidiBuffer& out, int samplePos, Handle handle, float lift)
{
    if (auto* v = findVoice (handle))
        releaseVoice (out, samplePos, *v, lift);
}

void MpeEncoder::stopAllNotes (juce::MidiBuffer& out, int samplePos)
{
    while (numActive > 0)
        releaseVoice (out, samplePos, voices[(size_t) activeList[(size_t) (numActive - 1)]], defaultLift);
}

void MpeEncoder::resetControllers (juce::MidiBuffer& out, int samplePos)
{
    zonePitch = 0.0f;
    zonePressure = 0.0f;
    tailPitch = tailPressure = 0.0f;

    // Called before settings change: a controller carrying pressure may not carry it from now on,
    // so it goes back to its own rest value (Expression to full) rather than staying where pressure left it.
    const auto pressureController = settings.pressureTarget == OutputSettings::PressureTarget::controller
                                        ? settings.pressureController : -1;

    // Likewise what the last note's lanes left on the single channel, which may be a zone master from
    // now on: its Expression, pan or timbre go back to their usual values, as the next note would put them.
    if (timbreFromNote || noteControllers[(size_t) settings.timbreController])
        sendTimbre (out, samplePos, settings.singleChannel, settings.defaultTimbre, false);

    for (int cc = 0; cc < 128; ++cc)
    {
        if (! noteControllers[(size_t) cc]
            || cc == settings.timbreController || cc == settings.timbreLsbController || cc == pressureController)
            continue;

        sendController (out, samplePos, settings.singleChannel, cc, toSevenBit (defaultControllerValue (cc)), false);
    }

    noteControllers.reset();
    timbreFromNote = false;

    for (int ch = 1; ch <= 16; ++ch)
    {
        auto& st = channels[(size_t) ch];

        if (st.pitchBend >= 0 && st.pitchBend != centreKey)
            sendPitchBend (out, samplePos, ch, pitchBendCentre, true);

        if (st.pressure > 0)
            sendChannelPressure (out, samplePos, ch, 0.0f, true);

        for (auto pedal : { 64, 66, 67, 68, 69 })
            if (st.controllers[(size_t) pedal] > 0)
                sendController (out, samplePos, ch, pedal, 0, true);

        if (pressureController >= 0)
        {
            const auto rest = toSevenBit (defaultControllerValue (pressureController));

            if (st.controllers[(size_t) pressureController] >= 0 && st.controllers[(size_t) pressureController] != rest)
                sendController (out, samplePos, ch, pressureController, rest, true);
        }
    }
}

void MpeEncoder::resetZoneControllers (juce::MidiBuffer& out, int samplePos)
{
    if (isSingleChannelMode())
    {
        zonePitch = 0.0f;
        tailPitch = tailPressure = 0.0f;
        sendSingleChannelPitch (out, samplePos, false);
    }

    if (combinesZonePressure())
    {
        zonePressure = 0.0f;
        sendSingleChannelPressure (out, samplePos, false);
    }

    forEachZoneMaster ([&] (int ch)
    {
        auto& st = channels[(size_t) ch];

        if (! isSingleChannelMode() && st.pitchBend >= 0 && st.pitchBend != centreKey)
            sendPitchBend (out, samplePos, ch, pitchBendCentre, true);

        if (! combinesZonePressure() && st.pressure > 0)
            sendChannelPressure (out, samplePos, ch, 0.0f, true);

        for (auto pedal : { 64, 66, 67, 68, 69 })
            if (st.controllers[(size_t) pedal] > 0)
                sendController (out, samplePos, ch, pedal, 0, true);
    });
}

//==============================================================================
void MpeEncoder::setPitch (juce::MidiBuffer& out, int samplePos, Handle handle, float semitones)
{
    auto* v = findVoice (handle);

    if (v == nullptr)
        return;

    v->pitch = semitones;

    if (isSingleChannelMode())
    {
        if (isLead (*v))
            sendSingleChannelPitch (out, samplePos, ! v->started);

        return;
    }

    sendPitchBend (out, samplePos, v->channel, semitonesToPitchBendFine (semitones, settings.memberBendRange), ! v->started);
    channels[(size_t) v->channel].pitchTouched = true;
}

void MpeEncoder::setPressure (juce::MidiBuffer& out, int samplePos, Handle handle, float value)
{
    auto* v = findVoice (handle);

    if (v == nullptr)
        return;

    v->pressure = value;

    if (isSingleChannelMode() && settings.pressureTarget != OutputSettings::PressureTarget::polyAftertouch)
    {
        if (isLead (*v))
            sendSingleChannelPressure (out, samplePos, ! v->started);

        return;
    }

    sendNotePressure (out, samplePos, *v, value, ! v->started);
    channels[(size_t) v->channel].pressureTouched = true;
}

void MpeEncoder::setTimbre (juce::MidiBuffer& out, int samplePos, Handle handle, float value)
{
    auto* v = findVoice (handle);

    if (v == nullptr)
        return;

    v->timbre = value;

    if (isSingleChannelMode())
    {
        if (! isLead (*v))
            return;

        timbreFromNote = true;
    }

    sendTimbre (out, samplePos, v->channel, value, ! v->started);
    channels[(size_t) v->channel].timbreTouched = true;
}

void MpeEncoder::setController (juce::MidiBuffer& out, int samplePos, Handle handle, int controller, float value)
{
    if (isReservedController (controller) || (settings.mpePlus && controller == mpePlusPrefixController))
        return;

    auto* v = findVoice (handle);

    if (v == nullptr)
        return;

    v->ownControllers.set ((size_t) controller);
    v->ownValues[(size_t) controller] = (int8_t) toSevenBit (value);

    if (isSingleChannelMode())
    {
        // Not the newest note: the value is kept for when the note takes the channel over.
        if (! isLead (*v))
            return;

        // The next note to take over the channel puts it back, unless it has a lane for it too.
        noteControllers.set ((size_t) controller);

        if (controller == settings.timbreController)
            timbreFromNote = true;
    }

    sendController (out, samplePos, v->channel, controller, toSevenBit (value), ! v->started);
    channels[(size_t) v->channel].controllersTouched.set ((size_t) controller);
}

void MpeEncoder::setZonePitch (juce::MidiBuffer& out, int samplePos, float semitones)
{
    if (isSingleChannelMode())
    {
        zonePitch = semitones;
        sendSingleChannelPitch (out, samplePos, false);
        return;
    }

    const auto value = semitonesToPitchBendFine (semitones, settings.masterBendRange);
    forEachZoneMaster ([&] (int ch) { sendPitchBend (out, samplePos, ch, value, false); });
}

void MpeEncoder::setZonePressure (juce::MidiBuffer& out, int samplePos, float value)
{
    if (combinesZonePressure())
    {
        zonePressure = value;
        sendSingleChannelPressure (out, samplePos, false);
        return;
    }

    forEachZoneMaster ([&] (int ch) { sendChannelPressure (out, samplePos, ch, value, false); });
}

void MpeEncoder::setZoneTimbre (juce::MidiBuffer& out, int samplePos, float value)
{
    timbreFromNote = false;
    forEachZoneMaster ([&] (int ch) { sendTimbre (out, samplePos, ch, value, false); });
}

void MpeEncoder::setZoneController (juce::MidiBuffer& out, int samplePos, int controller, float value)
{
    if (isReservedController (controller))
        return;

    // In single channel mode a zone lane drives the controller now, so notes leave it alone.
    noteControllers.reset ((size_t) controller);

    if (isSingleChannelMode() && controller == settings.timbreController)
        timbreFromNote = false;

    const auto v7 = toSevenBit (value);
    forEachZoneMaster ([&] (int ch) { sendController (out, samplePos, ch, controller, v7, false); });
}

//==============================================================================
void MpeEncoder::sendLeadValues (juce::MidiBuffer& out, int samplePos)
{
    // The newest note has taken over the single channel. It brings its own pitch, pressure and timbre,
    // except where a lane of its own on that controller sets it, and returns controllers that only an
    // earlier note's lanes had set to their usual values, so one note's volume or pan doesn't stay on
    // the next.
    const auto* lead = leadVoice();

    if (lead == nullptr)
        return;

    const auto pressureController = settings.pressureTarget == OutputSettings::PressureTarget::controller
                                        ? settings.pressureController : -1;

    sendSingleChannelPitch (out, samplePos, false);

    if (pressureController < 0 || ! lead->ownControllers[(size_t) pressureController])
        sendSingleChannelPressure (out, samplePos, false);

    if (! lead->ownControllers[(size_t) settings.timbreController])
        sendSingleChannelTimbre (out, samplePos);

    for (int cc = 0; cc < 128; ++cc)
    {
        if (lead->ownControllers[(size_t) cc])
        {
            // Its own lane's value, which wasn't sent if the lane moved while another note led the channel.
            // Nothing is repeated: a value the channel already has is deduplicated.
            sendController (out, samplePos, settings.singleChannel, cc, lead->ownValues[(size_t) cc], false);
            noteControllers.set ((size_t) cc);

            if (cc == settings.timbreController)
                timbreFromNote = true;

            continue;
        }

        if (! noteControllers[(size_t) cc]
            || cc == settings.timbreController || cc == settings.timbreLsbController || cc == pressureController)
            continue;

        sendController (out, samplePos, settings.singleChannel, cc, toSevenBit (defaultControllerValue (cc)), false);
        noteControllers.reset ((size_t) cc);
    }
}

void MpeEncoder::sendSingleChannelPitch (juce::MidiBuffer& out, int samplePos, bool force)
{
    const auto* lead = leadVoice();
    const auto semitones = zonePitch + (lead != nullptr ? lead->pitch : tailPitch);
    sendPitchBend (out, samplePos, settings.singleChannel,
                   semitonesToPitchBendFine (semitones, settings.masterBendRange), force);
}

void MpeEncoder::sendSingleChannelPressure (juce::MidiBuffer& out, int samplePos, bool force)
{
    // As poly aftertouch every note keeps its own pressure. Otherwise the newest note's stands for the
    // channel, as its pitch does; sent as channel pressure, it's added to the zone's.
    if (settings.pressureTarget == OutputSettings::PressureTarget::polyAftertouch)
        return;

    const auto* lead = leadVoice();
    const auto own = lead != nullptr ? lead->pressure : tailPressure;

    if (settings.pressureTarget == OutputSettings::PressureTarget::controller)
        sendController (out, samplePos, settings.singleChannel, settings.pressureController, toSevenBit (own), force);
    else
        sendChannelPressure (out, samplePos, settings.singleChannel, zonePressure + own, force);
}

void MpeEncoder::sendSingleChannelTimbre (juce::MidiBuffer& out, int samplePos)
{
    // Only once a note has set the timbre: a synth that has CC74 on its filter keeps the preset's
    // setting through clips that never use timbre, and a zone timbre lane isn't fought over.
    if (! timbreFromNote)
        return;

    if (const auto* lead = leadVoice())
        sendTimbre (out, samplePos, settings.singleChannel, lead->timbre, false);
}

void MpeEncoder::sendPitchBend (juce::MidiBuffer& out, int samplePos, int channel, double value, bool force)
{
    auto& st = channels[(size_t) channel];
    value = juce::jlimit (0.0, 16383.0, value);

    // With MPE+, a CC87 just before the bend carries 7 more bits of resolution.
    auto msb = settings.mpePlus ? (int) std::floor (value) : (int) std::lround (value);
    auto lsb = settings.mpePlus ? (int) std::lround ((value - msb) * 128.0) : 0;

    if (lsb >= 128)
    {
        ++msb;
        lsb = 0;
    }

    if (msb >= 16383)
    {
        msb = 16383;
        lsb = 0;
    }

    const auto key = (msb << 7) | lsb;

    if (! force && st.pitchBend == key)
        return;

    if (lsb > 0)
        out.addEvent (juce::MidiMessage::controllerEvent (channel, mpePlusPrefixController, lsb), samplePos);

    out.addEvent (juce::MidiMessage::pitchWheel (channel, msb), samplePos);
    st.pitchBend = key;
}

void MpeEncoder::sendChannelPressure (juce::MidiBuffer& out, int samplePos, int channel, float value, bool force)
{
    auto& st = channels[(size_t) channel];
    value = juce::jlimit (0.0f, 1.0f, value);

    int msb = toSevenBit (value), lsb = 0;

    if (settings.mpePlus)
    {
        const auto fine = (int) std::lround (value * mpePlusFullScale);
        msb = fine >> 7;
        lsb = fine & 127;
    }

    const auto key = (msb << 7) | lsb;

    if (! force && st.pressure == key)
        return;

    if (lsb > 0)
        out.addEvent (juce::MidiMessage::controllerEvent (channel, mpePlusPrefixController, lsb), samplePos);

    out.addEvent (juce::MidiMessage::channelPressureChange (channel, msb), samplePos);
    st.pressure = key;
}

void MpeEncoder::sendPolyPressure (juce::MidiBuffer& out, int samplePos, int channel, int key, int value, bool force)
{
    auto& st = channels[(size_t) channel];

    if (! force && st.polyPressure[(size_t) key] == value)
        return;

    out.addEvent (juce::MidiMessage::aftertouchChange (channel, key, value), samplePos);
    st.polyPressure[(size_t) key] = (int8_t) value;
}

void MpeEncoder::sendController (juce::MidiBuffer& out, int samplePos, int channel, int controller, int value, bool force)
{
    auto& st = channels[(size_t) channel];

    if (! force && st.controllers[(size_t) controller] == value)
        return;

    out.addEvent (juce::MidiMessage::controllerEvent (channel, controller, value), samplePos);
    st.controllers[(size_t) controller] = (int8_t) value;

    // A controller lane on the timbre controller: the next timbre must be sent even if it's unchanged.
    if (controller == settings.timbreController || controller == settings.timbreLsbController)
        st.timbre = -1;
}

void MpeEncoder::sendTimbre (juce::MidiBuffer& out, int samplePos, int channel, float value, bool force)
{
    auto& st = channels[(size_t) channel];
    const auto useLsb = settings.timbreLsbController >= 0 && settings.timbreLsbController != settings.timbreController;

    if (settings.mpePlus && ! useLsb)
    {
        const auto fine = (int) std::lround (juce::jlimit (0.0f, 1.0f, value) * mpePlusFullScale);

        if (! force && st.timbre == fine)
            return;

        if ((fine & 127) > 0)
            out.addEvent (juce::MidiMessage::controllerEvent (channel, mpePlusPrefixController, fine & 127), samplePos);

        out.addEvent (juce::MidiMessage::controllerEvent (channel, settings.timbreController, fine >> 7), samplePos);
        st.controllers[(size_t) settings.timbreController] = (int8_t) (fine >> 7);
        st.timbre = fine;
        return;
    }

    const auto v14 = useLsb ? toFourteenBit (value) : (toSevenBit (value) << 7);

    if (! force && st.timbre == v14)
        return;

    out.addEvent (juce::MidiMessage::controllerEvent (channel, settings.timbreController, v14 >> 7), samplePos);
    st.controllers[(size_t) settings.timbreController] = (int8_t) (v14 >> 7);

    if (useLsb)
    {
        out.addEvent (juce::MidiMessage::controllerEvent (channel, settings.timbreLsbController, v14 & 127), samplePos);
        st.controllers[(size_t) settings.timbreLsbController] = (int8_t) (v14 & 127);
    }

    st.timbre = v14;
}

void MpeEncoder::sendNotePressure (juce::MidiBuffer& out, int samplePos, const Voice& v, float value, bool force)
{
    const auto v7 = toSevenBit (value);

    if (isSingleChannelMode())
    {
        sendPolyPressure (out, samplePos, v.channel, v.key, v7, force);
        return;
    }

    switch (settings.pressureTarget)
    {
        case OutputSettings::PressureTarget::channelPressure:
            sendChannelPressure (out, samplePos, v.channel, value, force);
            break;

        case OutputSettings::PressureTarget::polyAftertouch:
            sendPolyPressure (out, samplePos, v.channel, v.key, v7, force);
            break;

        case OutputSettings::PressureTarget::controller:
            sendController (out, samplePos, v.channel, settings.pressureController, v7, force);
            break;
    }
}

//==============================================================================
bool MpeEncoder::isPlaying (Handle handle) const noexcept
{
    return findVoice (handle) != nullptr;
}

int MpeEncoder::getChannel (Handle handle) const noexcept
{
    if (auto* v = findVoice (handle))
        return v->channel;

    return 0;
}

} // namespace mperoll
