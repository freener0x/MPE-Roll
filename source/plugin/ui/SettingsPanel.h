#pragma once

#include "Skins.h"
#include "plugin/PluginProcessor.h"

namespace mperoll
{

/** MPE output, input and playback settings, plus the window's look, shown in a call-out from the toolbar. */
class SettingsPanel : public juce::Component,
                      private juce::ChangeListener
{
public:
    explicit SettingsPanel (MpeRollProcessor&);
    ~SettingsPanel() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void parentHierarchyChanged() override;

    /** Cuts the panel off from the processor and its document. The call-out holding the panel is
        deleted later, by the message loop, and the host may delete the processor before then; the
        editor calls this on its way out. Afterwards the panel is empty and listens to nothing. */
    void detach();

private:
    MpeRollProcessor& processor;
    ClipDocument& document;
    juce::SharedResourcePointer<SkinManager> skins;
    juce::PropertyPanel panel;
    juce::HyperlinkButton supportLink;
    bool detached = false;

    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void edit (const std::function<void (EngineSettings&)>& change);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SettingsPanel)
};

} // namespace mperoll
