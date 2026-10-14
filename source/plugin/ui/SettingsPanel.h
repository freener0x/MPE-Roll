#pragma once

#include "plugin/PluginProcessor.h"

namespace mperoll
{

/** MPE output, input and playback settings, shown in a call-out from the toolbar. */
class SettingsPanel : public juce::Component,
                      private juce::ChangeListener
{
public:
    explicit SettingsPanel (MpeRollProcessor&);
    ~SettingsPanel() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    MpeRollProcessor& processor;
    ClipDocument& document;
    juce::PropertyPanel panel;

    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void edit (const std::function<void (EngineSettings&)>& change);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SettingsPanel)
};

} // namespace mperoll
