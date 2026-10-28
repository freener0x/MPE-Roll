#pragma once

#include "MpeSettings.h"

#include <vector>

namespace mperoll
{

/** How to read a particular MPE (or poly aftertouch) controller, plus a hint for setting it up. */
struct ControllerPreset
{
    juce::String name;
    juce::String setupTip;
    InputSettings input;
};

/** Presets for popular MPE controllers. The first entry is the generic MPE default. */
const std::vector<ControllerPreset>& controllerPresets();

/** Index of the preset matching these settings, or -1 if they've been customised. */
int findControllerPreset (const InputSettings& settings);

} // namespace mperoll
