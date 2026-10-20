#pragma once

#include "MpeClip.h"
#include "MpeEncoder.h"

#include <algorithm>
#include <array>

namespace mperoll
{

/** Plays an MpeClip through an MpeEncoder, sample-accurately.

    Notes start and stop at their exact sample positions; expression curves are sampled every
    control interval (the encoder drops values that haven't changed). Real-time safe.
*/
class ClipPlayer
{
public:
    explicit ClipPlayer (uint32_t handleTag = 1) : tag (handleTag) {}

    void setControlInterval (int samples) noexcept      { controlInterval = std::max (1, samples); }
    void setChaseNotes (bool shouldChase) noexcept      { chase = shouldChase; }

    /** Samples per second, which vibrato needs to keep its rate in Hz whatever the tempo. */
    void setSampleRate (double newRate) noexcept        { sampleRate = newRate > 0.0 ? newRate : 48000.0; }

    /** Renders a block during which clip time advances linearly from clipStartBeat.

        With loop == true the clip wraps at clip.length; notes running past the loop end are cut there.
    */
    void renderBlock (const MpeClip& clip, MpeEncoder& encoder, juce::MidiBuffer& out,
                      double clipStartBeat, double beatsPerSample, int startSample, int numSamples, bool loop);

    /** Ends every note this player started. The next render will chase notes under the playhead. */
    void stop (MpeEncoder& encoder, juce::MidiBuffer& out, int samplePos);

    int getNumActiveNotes() const noexcept              { return numActive; }
    MpeEncoder::Handle handleFor (uint32_t noteId) const noexcept { return ((uint64_t) tag << 32) | noteId; }

    static constexpr int maxActiveNotes = 512;

private:
    struct ActiveNote
    {
        uint32_t id = 0;
        size_t index = 0;
        int key = 0;
        float lift = 0.5f;
        double vibratoPhase = 0.0;  // cycles, kept per note so tempo changes don't make it jump
    };

    uint32_t tag;
    int controlInterval = 48;
    double sampleRate = 48000.0;
    bool chase = true;
    bool needsChase = true;
    std::array<ActiveNote, maxActiveNotes> active;
    int numActive = 0;
    double nextOnsFrom = -1.0;  // where the previous span ended, so no note start falls between blocks
    double zoneVibratoPhase = 0.0;

    bool isActive (uint32_t id) const noexcept;
    void validateActiveNotes (const MpeClip&, MpeEncoder&, juce::MidiBuffer&, int samplePos);
    void sendZoneExpression (const MpeClip&, MpeEncoder&, juce::MidiBuffer&, int samplePos, double clipBeat);
    void renderSpan (const MpeClip&, MpeEncoder&, juce::MidiBuffer&, double startBeat, double beatsPerSample,
                     int startSample, int numSamples, double loopEnd);
    void startNote (const MpeNote&, size_t index, double offsetInNote, double vibratoPhase,
                    MpeEncoder&, juce::MidiBuffer&, int samplePos);
    void sendNoteExpression (const MpeNote&, MpeEncoder&, juce::MidiBuffer&, int samplePos, double offsetInNote,
                             double vibratoPhase);
    void removeActive (int position) noexcept;
};

} // namespace mperoll
