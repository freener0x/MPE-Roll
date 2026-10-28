#include "MidiFileIO.h"
#include "ClipBuilder.h"
#include "ClipPlayer.h"
#include "MpeDecoder.h"
#include "MpeEncoder.h"

#include <cmath>

namespace mperoll::MidiFileIO
{

juce::MidiMessageSequence renderToSequence (const MpeClip& clip, const OutputSettings& output, const ExportOptions& options)
{
    const auto ppq = juce::jmax (24, options.ticksPerQuarterNote);

    MpeEncoder encoder;
    encoder.setSettings (output);

    ClipPlayer player;
    player.setControlInterval (juce::jmax (1, options.controlIntervalTicks));
    player.setChaseNotes (false);
    player.setSampleRate (ppq * juce::jmax (1.0, options.tempoBpm) / 60.0);

    // Render with one "sample" per tick so sample positions are tick positions.
    juce::MidiBuffer buffer;

    if (options.includeConfiguration)
        encoder.writeConfiguration (buffer, 0);

    const bool loopOnce = options.loopOnce && clip.looping;
    const auto endBeat = loopOnce ? clip.length : clip.contentEnd();
    const auto totalTicks = juce::jmax (1, (int) std::ceil (endBeat * ppq) + (loopOnce ? 0 : 1));

    player.renderBlock (clip, encoder, buffer, 0.0, 1.0 / ppq, 0, totalTicks, loopOnce);
    player.stop (encoder, buffer, totalTicks);

    juce::MidiMessageSequence sequence;

    for (const auto metadata : buffer)
    {
        auto message = metadata.getMessage();
        message.setTimeStamp ((double) metadata.samplePosition);
        sequence.addEvent (message);
    }

    sequence.updateMatchedPairs();
    return sequence;
}

juce::MidiFile exportClip (const MpeClip& clip, const OutputSettings& output, const ExportOptions& options)
{
    juce::MidiFile file;
    file.setTicksPerQuarterNote (juce::jmax (24, options.ticksPerQuarterNote));

    juce::MidiMessageSequence conductor;
    conductor.addEvent (juce::MidiMessage::timeSignatureMetaEvent (4, 4));
    conductor.addEvent (juce::MidiMessage::tempoMetaEvent ((int) std::lround (60'000'000.0 / juce::jmax (1.0, options.tempoBpm))));
    file.addTrack (conductor);

    auto track = renderToSequence (clip, output, options);

    if (clip.name.isNotEmpty())
        track.addEvent (juce::MidiMessage::textMetaEvent (3, clip.name));

    file.addTrack (track);
    return file;
}

bool writeClip (const MpeClip& clip, const OutputSettings& output, const ExportOptions& options, juce::OutputStream& stream)
{
    return exportClip (clip, output, options).writeTo (stream, 1);
}

//==============================================================================
ImportResult importClip (const juce::MidiFile& source, const InputSettings& input)
{
    ImportResult result;

    juce::MidiFile file (source);
    double beatsPerUnit = 1.0;

    if (file.getTimeFormat() > 0)
    {
        beatsPerUnit = 1.0 / (double) file.getTimeFormat();
    }
    else
    {
        // SMPTE timing: convert to seconds and assume 120 bpm.
        file.convertTimestampTicksToSeconds();
        beatsPerUnit = 2.0;
    }

    juce::MidiMessageSequence merged;

    for (int i = 0; i < file.getNumTracks(); ++i)
        merged.addSequence (*file.getTrack (i), 0.0);

    MpeDecoder decoder;
    decoder.setSettings (input);

    ClipBuilder builder;
    double lastBeat = 0.0;

    for (int i = 0; i < merged.getNumEvents(); ++i)
    {
        const auto& message = merged.getEventPointer (i)->message;
        const auto beat = juce::jmax (0.0, message.getTimeStamp() * beatsPerUnit);
        lastBeat = juce::jmax (lastBeat, beat);

        if (message.isMetaEvent())
        {
            if (message.isTempoMetaEvent() && result.tempoBpm <= 0.0)
                result.tempoBpm = 60.0 / message.getTempoSecondsPerQuarterNote();
            else if (message.isTrackNameEvent() && result.clip.name.isEmpty())
                result.clip.name = message.getTextFromTextMetaEvent();

            continue;
        }

        builder.setTime (beat, beat);
        decoder.process (message, builder);
    }

    builder.setTime (lastBeat, lastBeat);
    decoder.stopAllNotes (builder);
    builder.finish();

    for (auto& note : builder.takeNotes())
        result.clip.addNote (std::move (note));

    result.clip.zone = builder.takeZone();

    const auto end = result.clip.contentEnd();
    result.clip.length = juce::jmax (4.0, std::ceil (end / 4.0 - 1.0e-9) * 4.0);
    result.clip.looping = true;
    result.detectedZones = decoder.getZones();
    result.ok = true;
    return result;
}

ImportResult importClip (juce::InputStream& stream, const InputSettings& input)
{
    juce::MidiFile file;

    if (! file.readFrom (stream, true))
    {
        ImportResult result;
        result.error = "This doesn't look like a Standard MIDI File.";
        return result;
    }

    return importClip (file, input);
}

} // namespace mperoll::MidiFileIO
