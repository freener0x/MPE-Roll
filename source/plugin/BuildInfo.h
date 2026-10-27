#pragma once

#include <juce_core/juce_core.h>

namespace mperoll
{

/** Which build this is, e.g. "MPE Roll 0.1.0, build 63, f2ea3c4, 2026-10-26" from CI or "MPE Roll
    0.1.0, local build": shown in the File menu, MPE settings and the plugin log, so a tester can tell
    which build their DAW loaded.
*/
juce::String getBuildName();

} // namespace mperoll
