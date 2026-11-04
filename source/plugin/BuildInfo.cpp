// SPDX-FileCopyrightText: 2026 The MPE Roll contributors
// SPDX-License-Identifier: AGPL-3.0-only

#include "BuildInfo.h"
#include "MpeRollBuildInfo.h"   // made by CMake, in the build folder
#include "UpdateCheck.h"

namespace mperoll
{

juce::String getBuildName()
{
    return juce::String ("MPE Roll ") + MPEROLL_VERSION + ", " + MPEROLL_BUILD_ID;
}

juce::String getVersionString()
{
    // A pre-release is built from the release's version in CMakeLists.txt, and named after its tag.
    return updates::buildVersion (MPEROLL_VERSION, MPEROLL_BUILD_ID);
}

} // namespace mperoll
