#include "ControllerPresets.h"

namespace mperoll
{

namespace
{
    InputSettings mpe (float memberBendRange = 48.0f)
    {
        InputSettings s;
        s.mode = InputSettings::Mode::mpe;
        s.zones = { 15, 0 };
        s.followConfigurationMessages = true;
        s.memberBendRange = memberBendRange;
        s.masterBendRange = 2.0f;
        s.timbreController = 74;
        return s;
    }

    InputSettings mpePlus (float memberBendRange)
    {
        auto s = mpe (memberBendRange);
        s.mpePlus = true;
        return s;
    }

    InputSettings singleChannel (float bendRange = 2.0f)
    {
        auto s = mpe();
        s.mode = InputSettings::Mode::singleChannel;
        s.conventionalBendRange = bendRange;
        return s;
    }
}

const std::vector<ControllerPreset>& controllerPresets()
{
    static const auto presets = []
    {
        std::vector<ControllerPreset> list {
            { "Generic MPE (bend 48)",
              "Most MPE controllers: lower zone, 48 semitone per-note bend, timbre on CC74, channel pressure.",
              mpe() },

            { "ROLI Seaboard / LUMI",
              "In ROLI Dashboard or ROLI Connect, turn MPE on and keep the pitch bend range at 48.",
              mpe() },

            { "LinnStrument (MPE)",
              "Use Channel Per Note mode with Bend Range 48, Timbre/Y on CC74 and Loudness/Z on channel pressure.",
              mpe() },

            { "LinnStrument (one channel)",
              "One Channel mode: poly pressure becomes per-note pressure; bend and Y apply to every note.",
              singleChannel() },

            { "Haken Continuum / ContinuuMini (MPE+)",
              "Uses Haken's MPE+ high-resolution data. Set the bend range here to the one in your preset (96 or 48).",
              mpePlus (96.0f) },

            { "Expressive E Osmose (MPE)",
              "Choose MPE in the Osmose's MIDI settings. For its Haken (MPE+) output, use the Continuum preset.",
              mpe() },

            { "Ableton Push 3 (MPE)",
              "Set Push's Expression to MPE and per-note pitch bend to On.",
              mpe() },

            { "Keith McMillen K-Board Pro 4 / QuNexus",
              "Put the controller in MPE mode (bend 48, Y on CC74).",
              mpe() },

            { "Sensel Morph / Erae Touch / Joue Play",
              "Use the controller's MPE mode and check its pitch bend range matches (48 is the usual default).",
              mpe() },

            { "Poly aftertouch keyboard (not MPE)",
              "Keyboards with polyphonic aftertouch: each key's pressure is recorded per note; "
              "the pitch and mod wheels become zone-wide lanes.",
              singleChannel() },
        };

        for (size_t i = 0; i < list.size(); ++i)
            list[i].input.presetIndex = (int) i;

        return list;
    }();

    return presets;
}

int findControllerPreset (const InputSettings& settings)
{
    const auto& presets = controllerPresets();

    if (! juce::isPositiveAndBelow (settings.presetIndex, (int) presets.size()))
        return -1;

    auto preset = presets[(size_t) settings.presetIndex].input;
    preset.zones = preset.zones.normalised();
    auto current = settings;
    current.zones = current.zones.normalised();
    return preset == current ? settings.presetIndex : -1;
}

} // namespace mperoll
