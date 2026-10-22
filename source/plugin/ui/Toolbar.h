#pragma once

#include "Icons.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace mperoll
{

/** The two rows of controls above the piano roll. The editor wires up the callbacks. Buttons show
    icons; their words, and FL Studio's keys for them, are in the tooltips. */
class Toolbar : public juce::Component
{
public:
    Toolbar();

    void paint (juce::Graphics&) override;
    void resized() override;
    void lookAndFeelChanged() override;

    static constexpr int rowHeight = 32;
    static constexpr int preferredHeight = rowHeight * 2 + 6;

    // Row 1: synth, clip and transport.
    juce::TextButton synthButton, showSynthButton { "Edit synth" };
    juce::ComboBox slotBox, lengthBox, modeBox;
    IconButton loopButton { "Loop", Icon::loop, {} };
    IconButton recordButton { "Record", Icon::record, {} };
    IconButton settingsButton { "MPE settings", Icon::settings, {} };

    // Row 2: the tools (with FL Studio's keys), snapping, undo and exchange.
    IconButton drawToolButton { "Draw", Icon::draw, {} };
    IconButton paintToolButton { "Paint", Icon::paint, {} };
    IconButton eraseToolButton { "Delete", Icon::erase, {} };
    IconButton sliceToolButton { "Slice", Icon::slice, {} };
    IconButton selectToolButton { "Select", Icon::select, {} };
    IconButton glideToolButton { "Glide", Icon::glide, {} };
    IconButton glideSnapButton { "Snap glides", Icon::snapGlide, {} };
    juce::ComboBox snapBox;
    IconButton undoButton { "Undo", Icon::undo, {} };
    IconButton redoButton { "Redo", Icon::redo, {} };
    IconButton toolsButton { "Tools", Icon::tools, {} };
    IconButton midiButton { "File", Icon::file, {} };
    IconButton flButton { "Send to FL piano roll", Icon::sendToFl, {} };
    juce::Label status;

    /** Snap values in beats, matching the snap box item order (0 is off). */
    static double snapForIndex (int index);
    static int indexForSnap (double beats);
    static int numSnapOptions();
    static juce::String snapNameForIndex (int index);     // "bar", "1/16"...

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Toolbar)
};

} // namespace mperoll
