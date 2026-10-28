#pragma once

#include "ViewState.h"
#include "plugin/PluginProcessor.h"

namespace mperoll
{

/** The keyboard down the left of the grid. Click (or glide across) keys to hear the synth;
    further right on a key plays louder.
*/
class PianoKeys : public juce::Component,
                  private ViewState::Listener
{
public:
    PianoKeys (MpeRollProcessor&, ViewState&);
    ~PianoKeys() override;

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

private:
    MpeRollProcessor& processor;
    ViewState& view;
    int heldKey = -1;

    void viewChanged() override                 { repaint(); }
    void play (const juce::MouseEvent&);
    void release();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PianoKeys)
};

} // namespace mperoll
