#pragma once

#include "ViewState.h"
#include "plugin/ClipDocument.h"

namespace mperoll
{

/** Bar numbers above the grid. Drag the loop-end handle to change the clip length. */
class TimeRuler : public juce::Component,
                  private ViewState::Listener,
                  private juce::ChangeListener
{
public:
    TimeRuler (ClipDocument&, ViewState&);
    ~TimeRuler() override;

    void paint (juce::Graphics&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

    void setPlayhead (double beat, bool visible);

private:
    ClipDocument& document;
    ViewState& view;
    bool draggingEnd = false;
    double playheadBeat = 0.0;
    bool playheadVisible = false;

    bool isOverEndHandle (float x) const;
    void viewChanged() override                                     { repaint(); }
    void changeListenerCallback (juce::ChangeBroadcaster*) override { repaint(); }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TimeRuler)
};

} // namespace mperoll
