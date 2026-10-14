#pragma once

#include "ViewState.h"
#include "plugin/PluginProcessor.h"

#include <map>

namespace mperoll
{

/** The expression editor under the grid. Shows and edits one lane at a time: velocity, release
    velocity, pitch, pressure, timbre or any controller - per note or zone-wide.

    Per-note edits apply to the selected notes, or to every note under the pen when nothing is
    selected. Drawing only replaces the stretch you draw over; the rest of the curve is kept.
*/
class ExpressionLane : public juce::Component,
                       private ViewState::Listener,
                       private juce::ChangeListener
{
public:
    enum class Target
    {
        strike, lift,
        notePitch, notePressure, noteTimbre, noteController,
        zonePitch, zonePressure, zoneTimbre, zoneController
    };

    enum class Tool { draw, line, erase };

    ExpressionLane (MpeRollProcessor&, ViewState&);
    ~ExpressionLane() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

    void setTarget (Target newTarget, int newController = 0);
    juce::String getTargetName() const;

    static constexpr int headerHeight = 28;

private:
    ClipDocument& document;
    ViewState& view;

    Target target = Target::notePressure;
    int controller = 10;
    Tool tool = Tool::draw;
    float pitchRange = 12.0f;

    juce::TextButton laneButton, drawButton { "Draw" }, lineButton { "Line" }, eraseButton { "Erase" },
                     rangeButton, clearButton { "Clear" };
    juce::Label hint;

    // The stroke being drawn and the curves as they were before it started.
    std::vector<juce::Point<double>> stroke;   // (beat, value)
    std::map<uint32_t, ExpressionCurve> originalCurves;
    ExpressionCurve originalZone;
    bool dragging = false, erasing = false;

    void viewChanged() override                                     { repaint(); }
    void changeListenerCallback (juce::ChangeBroadcaster*) override { repaint(); }

    juce::Rectangle<float> plotArea() const;
    bool isPerNoteCurve() const noexcept;
    bool isZone() const noexcept;
    bool isPitch() const noexcept;
    bool isVelocity() const noexcept    { return target == Target::strike || target == Target::lift; }
    LaneId laneId() const noexcept;
    float minValue() const;
    float maxValue() const;
    float defaultValue() const;
    float valueToY (float value) const;
    float yToValue (float y) const;
    juce::Colour laneColour() const;

    juce::Point<double> strokePoint (const juce::MouseEvent&) const;
    std::vector<juce::Point<double>> sortedStroke() const;
    float strokeValueAt (const std::vector<juce::Point<double>>&, double beat) const;
    bool noteIsEditable (const MpeNote&, double minBeat, double maxBeat) const;
    void applyStroke();
    void applyToCurve (ExpressionCurve& curve, const ExpressionCurve& original, double offset, double length,
                       const std::vector<juce::Point<double>>& points);
    void clearLane();

    void showLaneMenu();
    void showRangeMenu();
    void askForController (bool zone);
    void updateHeader();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ExpressionLane)
};

} // namespace mperoll
