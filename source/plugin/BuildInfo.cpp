#include "BuildInfo.h"
#include "MpeRollBuildInfo.h"   // made by CMake, in the build folder

namespace mperoll
{

juce::String getBuildName()
{
    return juce::String ("MPE Roll ") + MPEROLL_VERSION + ", " + MPEROLL_BUILD_ID;
}

} // namespace mperoll
