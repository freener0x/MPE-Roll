// SPDX-FileCopyrightText: 2026 The MPE Roll contributors
// SPDX-License-Identifier: AGPL-3.0-only

#pragma once

#include "Skins.h"
#include "plugin/PluginProcessor.h"

namespace mperoll
{

class InfoProp;
class UpdateChecker;

/** MPE output, input and playback settings, plus the window's look and the update check, shown in a
    call-out from the toolbar. */
class SettingsPanel : public juce::Component,
                      private juce::ChangeListener,
                      private juce::Timer
{
public:
    /** The update checker is the editor's, which the panel borrows: holding one of its own would
        keep it, and its thread, past the editor and possibly the plugin. */
    SettingsPanel (MpeRollProcessor&, UpdateChecker&);
    ~SettingsPanel() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void parentHierarchyChanged() override;

    /** Cuts the panel off from the processor, its document and the update checker. The call-out
        holding the panel is deleted later, by the message loop, and the host may delete the
        processor before then; the editor calls this on its way out. Afterwards the panel is empty
        and listens to nothing. */
    void detach();

private:
    MpeRollProcessor& processor;
    ClipDocument& document;
    UpdateChecker* updates;     // the editor's; null once detached
    juce::SharedResourcePointer<SkinManager> skins;
    juce::PropertyPanel panel;
    juce::HyperlinkButton supportLink;
    juce::HyperlinkButton shareLink;
    juce::Label buildName;
    bool detached = false;
    InfoProp* copyStatus = nullptr;     // owned by the panel: lines refreshed as the copies change
    InfoProp* playedWith = nullptr;
    juce::Array<juce::PropertyComponent*> updateRows;  // ...and as the update check's file does

    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void timerCallback() override;
    void edit (const std::function<void (EngineSettings&)>& change);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SettingsPanel)
};

} // namespace mperoll
