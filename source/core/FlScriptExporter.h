#pragma once

#include "MpeClip.h"

#include <juce_core/juce_core.h>

/** The bridge to FL Studio's native plugins.

    FL's own synths (FLEX, Sytrus, Harmor, 3xOsc, ...) never see MPE, but FL's piano roll has its
    own per-note tools: fine pitch, Mod X / Mod Y, pan, release and slide notes - and slide notes
    only bend notes of the same colour. This exporter writes a piano roll script (.pyscript) that
    rebuilds a clip with those tools:

    - every note gets a colour no overlapping note uses, so its slides only bend that note
    - pitch glides (and MPE Roll's vibrato) become chains of slide notes, the starting offset becomes fine pitch
    - pressure and timbre become Mod X / Mod Y (static values plus ramps on the slide notes)
    - strike / lift become velocity / release, the per-note pan controller becomes pan
    - zone pitch bend can be folded into every note, the sustain pedal into note lengths

    The note data is embedded in the script, so it needs no file access inside FL.
*/
namespace mperoll::FlScriptExporter
{

struct Options
{
    juce::String title;
    float glideTolerance = 0.05f;           // semitones; smaller = more slide notes
    float rampTolerance = 0.04f;            // 0..1; smaller = more Mod X / Mod Y steps
    double minSlideLength = 1.0 / 64.0;     // beats
    int maxSlidesPerNote = 48;
    int maxSlidesPerVibratoNote = 1000;     // vibrato needs two slides a cycle on top, up to this many in all
    bool bakeZonePitch = true;              // fold master-channel pitch bend into every note
    bool applySustainPedal = true;          // extend notes while the zone's CC64 is held
    double tempoBpm = 120.0;                // vibrato is in Hz, so baking it into slides needs the tempo
};

struct Slide
{
    double offset = 0.0;        // beats from the note start
    double length = 0.0;
    float pitch = 0.0f;         // semitones relative to the note's key, reached at the end of the slide
    float pressure = 0.0f;      // values reached at the end of the slide
    float timbre = 0.5f;
    bool pitchMoves = false;
};

/** One note as it will appear in FL's piano roll. */
struct FlNote
{
    double start = 0.0, length = 0.0;
    int key = 60;
    float strike = 0.8f, lift = 0.5f, pan = 0.5f;
    float initialPitch = 0.0f;
    float initialPressure = 0.0f, averagePressure = 0.0f;
    float peakPressure = -1.0f;             // -1 if the note has no pressure lane
    float initialTimbre = 0.5f, averageTimbre = 0.5f;
    int colour = 0;
    std::vector<Slide> slides;
};

struct Result
{
    juce::String script;
    std::vector<FlNote> notes;
    int numSlides = 0;
    int numColourConflicts = 0;     // notes that had to share a colour (more than 16 overlapping)
};

Result generate (const MpeClip& clip, const Options& options);

/** Where FL Studio looks for piano roll scripts: Documents/Image-Line/FL Studio/Settings/Piano roll scripts. */
juce::File defaultScriptFolder();

inline constexpr const char* defaultScriptFileName = "MPE Roll Import.pyscript";

} // namespace mperoll::FlScriptExporter
