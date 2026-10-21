#pragma once

#include "AudioExport.h"
#include "PluginProcessor.h"
#include "ui/ExpressionLane.h"
#include "ui/MpeLookAndFeel.h"
#include "ui/NoteGrid.h"
#include "ui/PianoKeys.h"
#include "ui/Skins.h"
#include "ui/TimeRuler.h"
#include "ui/Toolbar.h"
#include "ui/ViewState.h"

namespace mperoll
{

class MpeRollEditor : public juce::AudioProcessorEditor,
                      public juce::DragAndDropContainer,
                      public juce::FileDragAndDropTarget,
                      private juce::Timer,
                      private juce::ChangeListener,
                      private juce::ScrollBar::Listener,
                      private ViewState::Listener
{
public:
    explicit MpeRollEditor (MpeRollProcessor&);
    ~MpeRollEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void parentHierarchyChanged() override;
    bool keyPressed (const juce::KeyPress&) override;

    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void filesDropped (const juce::StringArray& files, int x, int y) override;

private:
    class Splitter;
    class MidiDragListener;

    MpeRollProcessor& rollProcessor;
    ClipDocument& document;
    juce::SharedResourcePointer<SkinManager> skins;     // before the look and feel, which takes up its colours
    MpeLookAndFeel lookAndFeel;
    ViewState view;

    Toolbar toolbar;
    TimeRuler ruler;
    PianoKeys keys;
    NoteGrid grid;
    juce::ScrollBar hScroll { false }, vScroll { true };
    ExpressionLane lane;
    std::unique_ptr<Splitter> splitter;
    std::unique_ptr<MidiDragListener> midiDragListener;
    std::unique_ptr<juce::FileChooser> chooser;
    juce::Component::SafePointer<juce::CallOutBox> settingsBox;     // the MPE settings, while they're open
    juce::TooltipWindow tooltips { this, 700 };

    int laneHeight = 200;
    int lastEditSlot = -1;
    int ticksSinceWindowCheck = 0;
    bool updatingToolbar = false;
    juce::String transientStatus;
    juce::uint32 transientStatusTime = 0;
    juce::ScopedMessageBox silenceBox;

    void timerCallback() override;
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void scrollBarMoved (juce::ScrollBar*, double newRangeStart) override;
    void viewChanged() override;

    void wireToolbar();
    void refreshToolbar();
    void updateScrollBars();
    void scrollToContent();
    void setStatus (const juce::String& message);
    juce::String describeOutput() const;

    void showSynthMenu();
    void showPluginScanner();
    void browseForPlugin();
    void loadSynth (const juce::PluginDescription&);
    juce::String describeLoaded (const juce::String& synthName) const;
    void warnSynthSilent();

    void showMidiMenu();
    void importMidi();
    void importMidiFile (const juce::File&);
    void exportMidi();
    void exportAudio (AudioExport::Format);
    juce::File writeTemporaryMidiFile();
    void sendToFl();
    void showSettings();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MpeRollEditor)
};

} // namespace mperoll
