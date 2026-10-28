#pragma once

#include "MpeClip.h"
#include "MpeSettings.h"

#include <juce_audio_basics/juce_audio_basics.h>

namespace mperoll::MidiFileIO
{

struct ExportOptions
{
    int ticksPerQuarterNote = 960;
    double tempoBpm = 120.0;
    int controlIntervalTicks = 10;      // expression resolution (960 ppq / 10 = ~1/96 beat)
    bool includeConfiguration = true;   // MCM + pitch bend sensitivity at the start
    bool loopOnce = false;              // render one pass of a looping clip up to its length
};

/** Renders a clip to a single-track MPE MIDI sequence (timestamps in ticks). */
juce::MidiMessageSequence renderToSequence (const MpeClip& clip, const OutputSettings& output, const ExportOptions& options);

/** Builds a type 1 Standard MIDI File: a tempo track plus the MPE track. */
juce::MidiFile exportClip (const MpeClip& clip, const OutputSettings& output, const ExportOptions& options);

bool writeClip (const MpeClip& clip, const OutputSettings& output, const ExportOptions& options, juce::OutputStream& stream);

struct ImportResult
{
    bool ok = false;
    juce::String error;
    MpeClip clip;
    ZoneLayout detectedZones;   // after any MPE Configuration Messages in the file
    double tempoBpm = 0.0;      // first tempo found, 0 if none
};

/** Reads any Standard MIDI File (MPE or not) into a clip, merging all tracks. */
ImportResult importClip (const juce::MidiFile& file, const InputSettings& input);
ImportResult importClip (juce::InputStream& stream, const InputSettings& input);

} // namespace mperoll::MidiFileIO
