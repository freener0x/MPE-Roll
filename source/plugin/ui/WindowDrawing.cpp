#include "WindowDrawing.h"

#include <juce_audio_processors/juce_audio_processors.h>

namespace mperoll::WindowDrawing
{

namespace
{
    constexpr auto settingsKey = "windowDrawing";
    constexpr auto softwareEngine = "Software Renderer";
    constexpr auto gpuEngine = "Direct2D";
}

juce::StringArray modeNames()
{
    return { "Automatic", "Software", "Direct2D (GPU)" };
}

bool isChoiceAvailable()
{
   #if JUCE_WINDOWS
    return true;
   #else
    return false;
   #endif
}

Mode getMode (juce::PropertiesFile* settings)
{
    const auto value = settings != nullptr ? settings->getIntValue (settingsKey, 0) : 0;
    return juce::isPositiveAndBelow (value, modeNames().size()) ? (Mode) value : Mode::automatic;
}

void setMode (juce::PropertiesFile* settings, Mode mode)
{
    if (settings != nullptr)
        settings->setValue (settingsKey, (int) mode);
}

void apply (juce::Component& component, Mode mode)
{
    auto* peer = component.getPeer();

    if (peer == nullptr || ! isChoiceAvailable())
        return;

    const auto wanted = std::invoke ([mode]
    {
        switch (mode)
        {
            case Mode::software:  return softwareEngine;
            case Mode::gpu:       return gpuEngine;
            case Mode::automatic: break;
        }

        return juce::PluginHostType().isFruityLoops() ? softwareEngine : gpuEngine;
    });

    const auto index = peer->getAvailableRenderingEngines().indexOf (wanted);

    if (index >= 0 && index != peer->getCurrentRenderingEngine())
        peer->setCurrentRenderingEngine (index);
}

bool resync (juce::Component& component)
{
    auto* peer = component.getPeer();

    if (peer == nullptr)
        return false;

    auto& window = peer->getComponent();
    const auto actual = (peer->getBounds().toFloat() / window.getDesktopScaleFactor()).toNearestInt();
    const auto stale = std::abs (actual.getWidth() - window.getWidth()) > 1
                    || std::abs (actual.getHeight() - window.getHeight()) > 1;

    if (stale)
    {
        peer->handleMovedOrResized();
        window.repaint();
    }

    return stale;
}

juce::String describe (const juce::Component& component)
{
    auto* peer = component.getPeer();

    if (peer == nullptr)
        return "Not on screen";

    const auto& window = peer->getComponent();
    const auto scale = peer->getPlatformScaleFactor() * window.getDesktopScaleFactor();
    const auto engine = peer->getAvailableRenderingEngines()[peer->getCurrentRenderingEngine()];
    const auto physical = (window.getLocalBounds().toDouble() * scale).toNearestInt();

    return juce::String (juce::roundToInt (scale * 100.0)) + "% scale, drawn by " + engine + ", "
         + juce::String (window.getWidth()) + " x " + juce::String (window.getHeight()) + " ("
         + juce::String (physical.getWidth()) + " x " + juce::String (physical.getHeight()) + " pixels)";
}

} // namespace mperoll::WindowDrawing
