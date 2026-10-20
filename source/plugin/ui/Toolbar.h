#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace mperoll
{

/** The two rows of controls above the piano roll. The editor wires up the callbacks. */
class Toolbar : public juce::Component
{
public:
    Toolbar();

    void paint (juce::Graphics&) override;
    void resized() override;

    static constexpr int rowHeight = 32;
    static constexpr int preferredHeight = rowHeight * 2 + 6;

    // Row 1: synth, clip and transport.
    juce::TextButton synthButton, showSynthButton { "Edit synth" };
    juce::ComboBox slotBox, lengthBox, modeBox;
    juce::TextButton loopButton { "Loop" }, recordButton, settingsButton { "MPE settings" };

    // Row 2: editing and exchange.
    juce::TextButton editToolButton { "Edit" }, paintToolButton { "Paint" }, glideToolButton { "Glide" },
                     glideSnapButton { "Snap glide" };
    juce::ComboBox snapBox;
    juce::TextButton undoButton { "Undo" }, redoButton { "Redo" }, midiButton, flButton { "Send to FL piano roll" };
    juce::Label status;

    /** Snap values in beats, matching the snap box item order. */
    static double snapForIndex (int index);
    static int indexForSnap (double beats);

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Toolbar)
};

} // namespace mperoll
