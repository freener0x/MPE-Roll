#include "ClipPlayer.h"

#include <cmath>

namespace mperoll
{

namespace
{
    constexpr double beatEpsilon = 1.0e-9;
    constexpr double maxGapToBridge = 0.05;  // beats: small forward jumps between blocks still trigger skipped notes
}

bool ClipPlayer::isActive (uint32_t id) const noexcept
{
    for (int i = 0; i < numActive; ++i)
        if (active[(size_t) i].id == id)
            return true;

    return false;
}

void ClipPlayer::removeActive (int position) noexcept
{
    active[(size_t) position] = active[(size_t) (numActive - 1)];
    --numActive;
}

void ClipPlayer::stop (MpeEncoder& encoder, juce::MidiBuffer& out, int samplePos)
{
    for (int i = 0; i < numActive; ++i)
        encoder.stopNote (out, samplePos, handleFor (active[(size_t) i].id), active[(size_t) i].lift);

    numActive = 0;
    needsChase = true;
    nextOnsFrom = -1.0;
}

void ClipPlayer::validateActiveNotes (const MpeClip& clip, MpeEncoder& encoder, juce::MidiBuffer& out, int samplePos)
{
    for (int i = numActive - 1; i >= 0; --i)
    {
        auto& a = active[(size_t) i];

        if (a.index < clip.notes.size() && clip.notes[a.index].id == a.id && clip.notes[a.index].key == a.key)
        {
            a.lift = clip.notes[a.index].lift;
            continue;
        }

        // The clip was edited while this note played: find it again, or end it if it's gone or moved key.
        bool found = false;

        for (size_t j = 0; j < clip.notes.size(); ++j)
        {
            if (clip.notes[j].id == a.id)
            {
                if (clip.notes[j].key == a.key)
                {
                    a.index = j;
                    a.lift = clip.notes[j].lift;
                    found = true;
                }

                break;
            }
        }

        if (! found)
        {
            encoder.stopNote (out, samplePos, handleFor (a.id), a.lift);
            removeActive (i);
        }
    }
}

void ClipPlayer::renderBlock (const MpeClip& clip, MpeEncoder& encoder, juce::MidiBuffer& out,
                              double clipStartBeat, double beatsPerSample, int startSample, int numSamples, bool loop)
{
    if (numSamples <= 0 || beatsPerSample <= 0.0)
        return;

    validateActiveNotes (clip, encoder, out, startSample);

    if (! loop || clip.length <= 0.0)
    {
        renderSpan (clip, encoder, out, clipStartBeat, beatsPerSample, startSample, numSamples, -1.0);
        return;
    }

    const auto length = clip.length;
    auto beat = std::fmod (clipStartBeat, length);

    if (beat < 0.0)
        beat += length;

    // Rounding can leave a block that really starts at the loop point a hair before it.
    if (length - beat < beatsPerSample * 0.5)
        beat = 0.0;

    auto sample = startSample;
    auto remaining = numSamples;

    while (remaining > 0)
    {
        const auto samplesToEnd = std::ceil ((length - beat) / beatsPerSample - 1.0e-6);
        const auto span = (int) juce::jlimit (1.0, (double) remaining, samplesToEnd);

        renderSpan (clip, encoder, out, beat, beatsPerSample, sample, span, length);

        sample += span;
        remaining -= span;
        beat += span * beatsPerSample;

        if (beat >= length - beatEpsilon)
        {
            // Loop point: cut everything still sounding and start the next pass at the clip start.
            const auto wrapPos = juce::jmin (sample, startSample + numSamples - 1);

            for (int i = 0; i < numActive; ++i)
                encoder.stopNote (out, wrapPos, handleFor (active[(size_t) i].id), active[(size_t) i].lift);

            numActive = 0;
            beat = 0.0;
            nextOnsFrom = 0.0;
        }
    }
}

void ClipPlayer::renderSpan (const MpeClip& clip, MpeEncoder& encoder, juce::MidiBuffer& out,
                             double startBeat, double beatsPerSample, int startSample, int numSamples, double loopEnd)
{
    const auto& notes = clip.notes;

    auto effectiveEnd = [loopEnd] (const MpeNote& n)
    {
        return loopEnd > 0.0 ? std::min (n.end(), loopEnd) : n.end();
    };

    auto onsFrom = startBeat;

    if (needsChase)
    {
        if (chase)
        {
            for (size_t i = 0; i < notes.size() && notes[i].start < startBeat - beatEpsilon; ++i)
            {
                const auto& n = notes[i];

                if (effectiveEnd (n) > startBeat + beatEpsilon && numActive < maxActiveNotes && ! isActive (n.id))
                    startNote (n, i, startBeat - n.start, encoder, out, startSample);
            }
        }

        needsChase = false;
    }
    else if (nextOnsFrom >= 0.0 && startBeat >= nextOnsFrom && startBeat - nextOnsFrom < maxGapToBridge)
    {
        onsFrom = nextOnsFrom;
    }

    const auto endSample = startSample + numSamples;

    for (int cs = startSample; cs < endSample;)
    {
        const auto ce = std::min (cs + controlInterval, endSample);
        const auto bs = startBeat + (cs - startSample) * beatsPerSample;
        const auto be = startBeat + (ce - startSample) * beatsPerSample;

        auto sampleFor = [&] (double beat)
        {
            return juce::jlimit (cs, ce - 1, cs + (int) std::floor ((beat - bs) / beatsPerSample));
        };

        // Expression for everything already sounding.
        for (int i = 0; i < numActive; ++i)
        {
            const auto& n = notes[active[(size_t) i].index];
            sendNoteExpression (n, encoder, out, cs, bs - n.start);
        }

        sendZoneExpression (clip, encoder, out, cs, bs);

        // Note offs.
        for (int i = numActive - 1; i >= 0; --i)
        {
            const auto& n = notes[active[(size_t) i].index];
            const auto end = effectiveEnd (n);

            if (end < be)
            {
                encoder.stopNote (out, sampleFor (end), handleFor (n.id), n.lift);
                removeActive (i);
            }
        }

        // Note ons.
        const auto from = cs == startSample ? onsFrom : bs;
        auto it = std::lower_bound (notes.begin(), notes.end(), from,
                                    [] (const MpeNote& n, double b) { return n.start < b; });

        for (; it != notes.end() && it->start < be; ++it)
        {
            if (loopEnd > 0.0 && it->start >= loopEnd)
                break;

            if (numActive >= maxActiveNotes)
                break;

            if (isActive (it->id))
                continue;

            const auto pos = sampleFor (std::max (it->start, bs));
            startNote (*it, (size_t) (it - notes.begin()), std::max (0.0, bs - it->start), encoder, out, pos);

            const auto end = effectiveEnd (*it);

            if (end < be)
            {
                encoder.stopNote (out, std::max (pos, sampleFor (end)), handleFor (it->id), it->lift);
                removeActive (numActive - 1);
            }
        }

        cs = ce;
    }

    nextOnsFrom = startBeat + numSamples * beatsPerSample;
}

void ClipPlayer::startNote (const MpeNote& note, size_t index, double offsetInNote,
                            MpeEncoder& encoder, juce::MidiBuffer& out, int samplePos)
{
    const auto handle = handleFor (note.id);

    encoder.prepareNote (out, samplePos, handle, note.key);
    sendNoteExpression (note, encoder, out, samplePos, offsetInNote);
    encoder.startNote (out, samplePos, handle, note.strike);

    active[(size_t) numActive++] = { note.id, index, note.key, note.lift };
}

void ClipPlayer::sendNoteExpression (const MpeNote& note, MpeEncoder& encoder, juce::MidiBuffer& out,
                                     int samplePos, double offsetInNote)
{
    const auto handle = handleFor (note.id);
    const auto& lanes = note.lanes;

    if (! lanes.pitch.isEmpty())
        encoder.setPitch (out, samplePos, handle, lanes.pitch.valueAt (offsetInNote, 0.0f));

    if (! lanes.pressure.isEmpty())
        encoder.setPressure (out, samplePos, handle, lanes.pressure.valueAt (offsetInNote, 0.0f));

    if (! lanes.timbre.isEmpty())
        encoder.setTimbre (out, samplePos, handle, lanes.timbre.valueAt (offsetInNote, 0.5f));

    for (const auto& c : lanes.controllers)
        if (! c.curve.isEmpty())
            encoder.setController (out, samplePos, handle, c.controller, c.curve.valueAt (offsetInNote, 0.0f));
}

void ClipPlayer::sendZoneExpression (const MpeClip& clip, MpeEncoder& encoder, juce::MidiBuffer& out,
                                     int samplePos, double clipBeat)
{
    const auto& zone = clip.zone;

    if (! zone.pitch.isEmpty())
        encoder.setZonePitch (out, samplePos, zone.pitch.valueAt (clipBeat, 0.0f));

    if (! zone.pressure.isEmpty())
        encoder.setZonePressure (out, samplePos, zone.pressure.valueAt (clipBeat, 0.0f));

    if (! zone.timbre.isEmpty())
        encoder.setZoneTimbre (out, samplePos, zone.timbre.valueAt (clipBeat, 0.5f));

    for (const auto& c : zone.controllers)
        if (! c.curve.isEmpty())
            encoder.setZoneController (out, samplePos, c.controller, c.curve.valueAt (clipBeat, 0.0f));
}

} // namespace mperoll
