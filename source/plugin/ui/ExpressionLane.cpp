#include "ExpressionLane.h"
#include "MpeLookAndFeel.h"

namespace mperoll
{

namespace
{
    constexpr double anchorGap = 1.0e-4;   // beats between a drawn stretch and the curve it was drawn into
    constexpr float defaultStrike = 0.8f;
    constexpr float defaultLift = 0.5f;

    const int perNoteControllers[] = { 10, 7, 11, 1, 2, 71, 72, 73, 74, 75, 76, 77, 78, 91, 93 };
    const int zoneControllers[] = { 64, 66, 67, 68, 69, 65, 5, 1, 2, 4, 11, 7, 10, 91, 93 };

    enum MenuIds
    {
        idStrike = 1, idLift, idPitch, idPressure, idTimbre, idZonePitch, idZonePressure, idZoneTimbre,
        idVibrato, idZoneVibrato,
        idOtherNoteCC = 99, idNoteCCBase = 100, idOtherZoneCC = 299, idZoneCCBase = 300
    };
}

ExpressionLane::ExpressionLane (MpeRollProcessor& p, ViewState& v)
    : document (p.getDocument()), view (v)
{
    for (auto* b : { &laneButton, &drawButton, &lineButton, &eraseButton, &rangeButton, &clearButton })
        addAndMakeVisible (b);

    // Vibrato speed is one setting for the whole clip; dragging it is one undo step.
    rateSlider.setRange (minVibratoRate, maxVibratoRate, 0.1);
    rateSlider.setTextValueSuffix (" Hz");
    rateSlider.setTooltip ("Vibrato speed for every note in this clip");
    rateSlider.onDragStart = [this] { draggingRate = true; document.beginEdit ("Vibrato speed"); };
    rateSlider.onDragEnd = [this] { draggingRate = false; document.endEdit(); };
    rateSlider.onValueChange = [this] { setVibratoRate (rateSlider.getValue()); };
    addChildComponent (rateSlider);

    for (auto* b : { &drawButton, &lineButton, &eraseButton })
    {
        b->setClickingTogglesState (true);
        b->setRadioGroupId (1);
    }

    drawButton.setToggleState (true, juce::dontSendNotification);
    drawButton.onClick = [this] { tool = Tool::draw; };
    lineButton.onClick = [this] { tool = Tool::line; };
    eraseButton.onClick = [this] { tool = Tool::erase; };
    laneButton.onClick = [this] { showLaneMenu(); };
    rangeButton.onClick = [this] { showRangeMenu(); };
    clearButton.onClick = [this] { clearLane(); };

    drawButton.setTooltip ("Freehand: replaces the stretch you draw over");
    lineButton.setTooltip ("Straight line from where you press to where you let go");
    eraseButton.setTooltip ("Remove points (right-drag erases with any tool)");
    clearButton.setTooltip ("Clear this lane on the selected notes (or all notes)");

    hint.setText ("Edits apply to selected notes, or every note under the pen. Right-drag erases, "
                  "double-click clears. Pitch snaps to semitones while Snap glide is on (Alt draws freely, Shift always snaps).",
                  juce::dontSendNotification);
    hint.setColour (juce::Label::textColourId, Theme::textDim);
    hint.setFont (juce::FontOptions (12.0f));
    addAndMakeVisible (hint);

    view.addListener (this);
    document.addChangeListener (this);
    updateHeader();
}

ExpressionLane::~ExpressionLane()
{
    document.removeChangeListener (this);
    view.removeListener (this);
}

//==============================================================================
void ExpressionLane::setTarget (Target newTarget, int newController)
{
    target = newTarget;

    if (target == Target::noteController || target == Target::zoneController)
        controller = newController;

    updateHeader();
    repaint();
}

juce::String ExpressionLane::getTargetName() const
{
    const auto timbreCC = document.getSettings().output.timbreController;

    switch (target)
    {
        case Target::strike:          return "Velocity (strike)";
        case Target::lift:            return "Release velocity (lift)";
        case Target::notePitch:       return "Pitch glide";
        case Target::notePressure:    return "Pressure";
        case Target::noteTimbre:      return "Timbre (slide, CC" + juce::String (timbreCC) + ")";
        case Target::noteController:  return controllerName (controller) + " (CC" + juce::String (controller) + ") per note";
        case Target::zonePitch:       return "Zone pitch bend";
        case Target::zonePressure:    return "Zone pressure";
        case Target::zoneTimbre:      return "Zone timbre";
        case Target::zoneController:  return "Zone " + controllerName (controller) + " (CC" + juce::String (controller) + ")";
        case Target::noteVibrato:     return "Vibrato depth";
        case Target::zoneVibrato:     return "Zone vibrato depth";
    }

    return {};
}

bool ExpressionLane::isPerNoteCurve() const noexcept
{
    return target == Target::notePitch || target == Target::notePressure
        || target == Target::noteTimbre || target == Target::noteController || target == Target::noteVibrato;
}

bool ExpressionLane::isZone() const noexcept
{
    return target == Target::zonePitch || target == Target::zonePressure
        || target == Target::zoneTimbre || target == Target::zoneController || target == Target::zoneVibrato;
}

bool ExpressionLane::isPitch() const noexcept
{
    return target == Target::notePitch || target == Target::zonePitch;
}

LaneId ExpressionLane::laneId() const noexcept
{
    switch (target)
    {
        case Target::notePitch:
        case Target::zonePitch:         return LaneId::pitch();
        case Target::notePressure:
        case Target::zonePressure:      return LaneId::pressure();
        case Target::noteTimbre:
        case Target::zoneTimbre:        return LaneId::timbre();
        case Target::noteController:
        case Target::zoneController:    return LaneId::cc (controller);
        case Target::noteVibrato:
        case Target::zoneVibrato:       return LaneId::vibrato();
        case Target::strike:
        case Target::lift:              break;
    }

    return LaneId::pressure();
}

float ExpressionLane::maxValue() const
{
    if (target == Target::notePitch)
        return pitchRange;

    if (target == Target::zonePitch)
        return document.getSettings().output.masterBendRange;

    return 1.0f;
}

float ExpressionLane::minValue() const
{
    return isPitch() ? -maxValue() : 0.0f;
}

float ExpressionLane::defaultValue() const
{
    if (target == Target::strike)
        return defaultStrike;

    if (target == Target::lift)
        return defaultLift;

    return defaultLaneValue (laneId(), document.getSettings().output.defaultTimbre);
}

juce::Colour ExpressionLane::laneColour() const
{
    if (isVelocity())
        return Theme::velocity;

    switch (laneId().kind)
    {
        case LaneId::Kind::pitch:
        case LaneId::Kind::vibrato:     return Theme::glide;
        case LaneId::Kind::pressure:    return Theme::pressure;
        case LaneId::Kind::timbre:      return Theme::timbre;
        case LaneId::Kind::controller:  return Theme::controller;
    }

    return Theme::controller;
}

juce::Rectangle<float> ExpressionLane::plotArea() const
{
    return juce::Rectangle<float> ((float) ViewState::keysWidth, (float) headerHeight + 4.0f,
                                   (float) (getWidth() - ViewState::keysWidth - ViewState::scrollbarSize),
                                   (float) getHeight() - (float) headerHeight - 8.0f);
}

float ExpressionLane::valueToY (float value) const
{
    const auto plot = plotArea();
    const auto norm = (value - minValue()) / (maxValue() - minValue());
    return plot.getBottom() - juce::jlimit (0.0f, 1.0f, norm) * plot.getHeight();
}

float ExpressionLane::yToValue (float y) const
{
    const auto plot = plotArea();
    const auto norm = juce::jlimit (0.0f, 1.0f, (plot.getBottom() - y) / juce::jmax (1.0f, plot.getHeight()));
    return minValue() + norm * (maxValue() - minValue());
}

//==============================================================================
void ExpressionLane::resized()
{
    auto header = getLocalBounds().removeFromTop (headerHeight).reduced (4, 3);
    header.removeFromLeft (ViewState::keysWidth - 4);

    laneButton.setBounds (header.removeFromLeft (230));
    header.removeFromLeft (8);
    drawButton.setBounds (header.removeFromLeft (50));
    lineButton.setBounds (header.removeFromLeft (46));
    eraseButton.setBounds (header.removeFromLeft (50));
    header.removeFromLeft (8);

    if (target == Target::notePitch)
    {
        rangeButton.setBounds (header.removeFromLeft (70));
        header.removeFromLeft (4);
    }

    if (isVibrato())
    {
        rateSlider.setBounds (header.removeFromLeft (110));
        header.removeFromLeft (4);
    }

    clearButton.setBounds (header.removeFromLeft (52));
    header.removeFromLeft (10);
    hint.setBounds (header);
}

void ExpressionLane::updateHeader()
{
    laneButton.setButtonText (getTargetName() + juce::String::fromUTF8 (" \xe2\x96\xbe"));
    rangeButton.setVisible (target == Target::notePitch);
    rangeButton.setButtonText (juce::String::fromUTF8 ("\xc2\xb1") + juce::String ((int) pitchRange) + " st");
    rateSlider.setVisible (isVibrato());
    rateSlider.setValue (document.getEditClip().vibratoRate, juce::dontSendNotification);
    resized();
}

void ExpressionLane::lookAndFeelChanged()
{
    hint.setColour (juce::Label::textColourId, Theme::textDim);
}

void ExpressionLane::changeListenerCallback (juce::ChangeBroadcaster*)
{
    if (! draggingRate)
        rateSlider.setValue (document.getEditClip().vibratoRate, juce::dontSendNotification);

    repaint();
}

void ExpressionLane::setVibratoRate (double hz)
{
    hz = juce::jlimit (minVibratoRate, maxVibratoRate, hz);

    if (draggingRate)
    {
        document.editClip().vibratoRate = hz;
        document.changed();
    }
    else if (std::abs (document.getEditClip().vibratoRate - hz) > 1.0e-9)
    {
        document.perform ("Vibrato speed", [hz] (MpeClip& clip) { clip.vibratoRate = hz; });
    }
}

void ExpressionLane::paint (juce::Graphics& g)
{
    g.fillAll (Theme::background);
    g.setColour (Theme::panel);
    g.fillRect (getLocalBounds().removeFromTop (headerHeight));
    g.setColour (Theme::outline);
    g.fillRect (0, 0, getWidth(), 1);

    const auto plot = plotArea();
    const auto& clip = document.getEditClip();
    const auto& selection = document.selection;
    const auto colour = laneColour();

    g.setColour (Theme::rowWhite);
    g.fillRect (plot);

    if (const auto picture = skins->getBackgroundFor (plot.toNearestInt()); picture.isValid())
    {
        g.setOpacity (skins->getBackgroundStrength());
        g.drawImageAt (picture, juce::roundToInt (plot.getX()), juce::roundToInt (plot.getY()));
        g.setOpacity (1.0f);
    }

    // Scale.
    g.setFont (juce::FontOptions (10.5f));

    auto guide = [&] (float value, const juce::String& label, bool strong)
    {
        const auto y = valueToY (value);
        g.setColour (strong ? Theme::lineBar : Theme::lineSub);
        g.fillRect (plot.getX(), y, plot.getWidth(), 1.0f);

        if (label.isNotEmpty())
        {
            g.setColour (Theme::textDim);
            g.drawText (label, juce::Rectangle<float> (2.0f, y - 7.0f, plot.getX() - 6.0f, 14.0f),
                        juce::Justification::centredRight, false);
        }
    };

    if (isPitch())
    {
        const auto range = maxValue();
        const auto pixelsPerSemitone = plot.getHeight() / (2.0f * range);
        const auto step = pixelsPerSemitone >= 5.0f ? 1 : (pixelsPerSemitone >= 1.0f ? 12 : 24);

        for (int st = -(int) range; st <= (int) range; ++st)
            if (st % step == 0)
                guide ((float) st, st % 12 == 0 ? (st > 0 ? "+" : "") + juce::String (st) : juce::String(), st % 12 == 0);
    }
    else
    {
        const bool isController = laneId().kind == LaneId::Kind::controller && ! isVelocity();

        for (int i = 0; i <= 4; ++i)
        {
            const auto v = (float) i / 4.0f;
            const auto label = isVibrato()   ? juce::String::fromUTF8 ("\xc2\xb1") + juce::String (v * maxVibratoDepth, 1) + " st"
                             : isController ? juce::String (juce::roundToInt (v * 127.0f))
                                            : juce::String (juce::roundToInt (v * 100.0f)) + "%";
            guide (v, (i % 2 == 0) ? label : juce::String(), i == 0 || i == 4);
        }
    }

    g.saveState();
    g.reduceClipRegion (plot.toNearestInt());

    // Beat and bar lines.
    for (auto b = std::ceil (view.scrollBeat); b <= view.xToBeat (plot.getWidth()); b += 1.0)
    {
        g.setColour (juce::roundToInt (b) % 4 == 0 ? Theme::lineBar.withAlpha (0.6f) : Theme::lineSub);
        g.fillRect (plot.getX() + (float) view.beatToX (b), plot.getY(), 1.0f, plot.getHeight());
    }

    if (clip.looping)
    {
        const auto loopEnd = plot.getX() + (float) view.beatToX (clip.length);
        g.setColour (juce::Colours::black.withAlpha (0.35f));
        g.fillRect (juce::Rectangle<float>::leftTopRightBottom (loopEnd, plot.getY(), plot.getRight(), plot.getBottom()));
    }

    auto xOf = [&] (double beat) { return plot.getX() + (float) view.beatToX (beat); };

    auto curvePath = [&] (const ExpressionCurve& curve, double offset, double length)
    {
        juce::Path p;
        const auto fallback = defaultValue();
        auto last = juce::Point<float> (xOf (offset), valueToY (curve.valueAt (0.0, fallback)));
        p.startNewSubPath (last);

        for (const auto& pt : curve.getPoints())
        {
            if (pt.beat <= 0.0 || pt.beat >= length)
                continue;

            const juce::Point<float> next (xOf (offset + pt.beat), valueToY (pt.value));

            if (next.x - last.x >= 1.0f || std::abs (next.y - last.y) >= 3.0f)
            {
                p.lineTo (next);
                last = next;
            }
        }

        p.lineTo (xOf (offset + length), valueToY (curve.valueAt (length, fallback)));
        return p;
    };

    if (isZone())
    {
        const auto* curve = clip.zone.find (laneId());
        const auto visibleEnd = view.xToBeat (plot.getWidth());

        if (curve == nullptr || curve->isEmpty())
        {
            g.setColour (colour.withAlpha (0.25f));
            g.fillRect (plot.getX(), valueToY (defaultValue()), plot.getWidth(), 1.5f);
        }
        else
        {
            g.setColour (colour);
            g.strokePath (curvePath (*curve, 0.0, juce::jmax (visibleEnd, clip.length)), juce::PathStrokeType (2.0f));
        }
    }
    else
    {
        for (const auto& n : clip.notes)
        {
            const auto x0 = xOf (n.start);

            if (x0 > plot.getRight())
                break;

            if (xOf (n.end()) < plot.getX())
                continue;

            const bool selected = selection.count (n.id) > 0;
            const auto alpha = selected || selection.empty() ? 1.0f : 0.35f;

            // A faint bar along the bottom shows where each note lives.
            g.setColour (Theme::note.withAlpha (0.18f * alpha));
            g.fillRect (juce::Rectangle<float>::leftTopRightBottom (x0, plot.getBottom() - 3.0f, xOf (n.end()), plot.getBottom()));

            if (isVelocity())
            {
                const auto value = target == Target::strike ? n.strike : n.lift;
                const auto x = target == Target::strike ? x0 : xOf (n.end());
                const auto y = valueToY (value);
                g.setColour (colour.withAlpha (alpha));
                g.fillRect (x - 1.0f, y, 2.0f, plot.getBottom() - y);
                g.fillEllipse (juce::Rectangle<float> (7.0f, 7.0f).withCentre ({ x, y }));
                continue;
            }

            const auto* curve = n.lanes.find (laneId());

            if (curve == nullptr || curve->isEmpty())
            {
                g.setColour (colour.withAlpha (0.2f * alpha));
                g.fillRect (juce::Rectangle<float>::leftTopRightBottom (x0, valueToY (defaultValue()) - 0.5f,
                                                                        xOf (n.end()), valueToY (defaultValue()) + 1.0f));
                continue;
            }

            g.setColour (colour.withAlpha (alpha));
            g.strokePath (curvePath (*curve, n.start, n.length), juce::PathStrokeType (selected ? 2.2f : 1.6f));
        }
    }

    if (dragging && tool == Tool::line && stroke.size() >= 2 && ! erasing)
    {
        g.setColour (Theme::text.withAlpha (0.7f));
        g.drawLine (xOf (stroke.front().x), valueToY ((float) stroke.front().y),
                    xOf (stroke.back().x), valueToY ((float) stroke.back().y), 1.0f);
    }

    g.restoreState();
}

//==============================================================================
juce::Point<double> ExpressionLane::strokePoint (const juce::MouseEvent& e) const
{
    const auto plot = plotArea();
    auto beat = juce::jmax (0.0, view.xToBeat (e.position.x - plot.getX()));
    auto value = yToValue (e.position.y);

    // Pitch snaps as glides drawn on the grid do: whole semitones, and lines start and end on the grid.
    // Shift snaps even with Snap glide off.
    if (isPitch() && ((view.snapGlide && ! e.mods.isAltDown()) || e.mods.isShiftDown()))
    {
        value = std::round (value);

        if (tool == Tool::line && ! erasing)
            beat = view.snapRound (beat, false);
    }

    return { beat, (double) value };
}

std::vector<juce::Point<double>> ExpressionLane::sortedStroke() const
{
    if (stroke.empty())
        return {};

    if (tool == Tool::line && ! erasing)
    {
        auto a = stroke.front(), b = stroke.back();

        if (b.x < a.x)
            std::swap (a, b);

        return { a, b };
    }

    // Freehand: the latest value wins for every pixel column the pen passed over.
    std::map<long, juce::Point<double>> columns;

    for (const auto& p : stroke)
        columns[std::lround (view.beatToX (p.x))] = p;

    std::vector<juce::Point<double>> result;
    result.reserve (columns.size());

    for (const auto& [x, p] : columns)
        result.push_back (p);

    return result;
}

float ExpressionLane::strokeValueAt (const std::vector<juce::Point<double>>& points, double beat) const
{
    if (beat <= points.front().x)
        return (float) points.front().y;

    if (beat >= points.back().x)
        return (float) points.back().y;

    for (size_t i = 1; i < points.size(); ++i)
    {
        if (beat <= points[i].x)
        {
            const auto& a = points[i - 1];
            const auto& b = points[i];
            const auto t = b.x > a.x ? (beat - a.x) / (b.x - a.x) : 1.0;
            return (float) (a.y + (b.y - a.y) * t);
        }
    }

    return (float) points.back().y;
}

bool ExpressionLane::noteIsEditable (const MpeNote& n, double minBeat, double maxBeat) const
{
    const auto& selection = document.selection;
    return (selection.empty() || selection.count (n.id) > 0) && n.start <= maxBeat && n.end() >= minBeat;
}

void ExpressionLane::applyToCurve (ExpressionCurve& curve, const ExpressionCurve& original, double offset, double length,
                                   const std::vector<juce::Point<double>>& points)
{
    const auto t0 = juce::jmax (0.0, points.front().x - offset);
    const auto t1 = juce::jmin (length, points.back().x - offset);
    curve = original;

    if (t1 < t0)
        return;

    if (erasing)
    {
        curve.removeRange (t0, t1);
        return;
    }

    // Keep the curve as it was on both sides of the stretch being drawn.
    const auto fallback = defaultValue();
    const auto before = original.valueAt (t0 - anchorGap, fallback);
    const auto after = original.valueAt (t1 + anchorGap, fallback);

    curve.removeRange (t0 - anchorGap * 0.5, t1 + anchorGap * 0.5);

    if (t0 > anchorGap)
        curve.addPoint (t0 - anchorGap, before);

    if (t1 < length - anchorGap)
        curve.addPoint (t1 + anchorGap, after);

    curve.addPoint (t0, strokeValueAt (points, t0 + offset));

    for (const auto& p : points)
    {
        const auto t = p.x - offset;

        if (t > t0 && t < t1)
            curve.addPoint (t, (float) p.y);
    }

    curve.addPoint (t1, strokeValueAt (points, t1 + offset));
}

void ExpressionLane::applyStroke()
{
    const auto points = sortedStroke();

    if (points.empty())
        return;

    const auto minBeat = points.front().x;
    const auto maxBeat = points.back().x;
    auto& clip = document.editClip();

    if (isVelocity())
    {
        const auto reach = 5.0 / view.pixelsPerBeat;
        const auto& selection = document.selection;

        for (auto& n : clip.notes)
        {
            if (! selection.empty() && selection.count (n.id) == 0)
                continue;

            const auto at = target == Target::strike ? n.start : n.end();

            if (at < minBeat - reach || at > maxBeat + reach)
                continue;

            const auto value = erasing ? defaultValue() : juce::jlimit (0.01f, 1.0f, strokeValueAt (points, at));
            (target == Target::strike ? n.strike : n.lift) = value;
        }
    }
    else if (isZone())
    {
        applyToCurve (clip.zone.get (laneId()), originalZone, 0.0, 1.0e9, points);
        clip.zone.removeEmptyControllers();
    }
    else
    {
        for (auto& n : clip.notes)
        {
            if (! noteIsEditable (n, minBeat, maxBeat))
                continue;

            if (originalCurves.count (n.id) == 0)
            {
                const auto* existing = n.lanes.find (laneId());
                originalCurves[n.id] = existing != nullptr ? *existing : ExpressionCurve();
            }

            applyToCurve (n.lanes.get (laneId()), originalCurves[n.id], n.start, n.length, points);
            n.lanes.removeEmptyControllers();
        }
    }

    document.changed();
}

void ExpressionLane::mouseDown (const juce::MouseEvent& e)
{
    if (! plotArea().contains (e.position))
        return;

    erasing = e.mods.isPopupMenu() || tool == Tool::erase;
    dragging = true;
    stroke = { strokePoint (e) };
    originalCurves.clear();
    originalZone = {};

    if (isZone())
    {
        if (const auto* curve = document.getEditClip().zone.find (laneId()))
            originalZone = *curve;
    }

    document.beginEdit (getTargetName() + (erasing ? " erase" : " edit"));
    applyStroke();
}

void ExpressionLane::mouseDrag (const juce::MouseEvent& e)
{
    if (! dragging)
        return;

    if (tool == Tool::line && ! erasing)
        stroke = { stroke.front(), strokePoint (e) };
    else
        stroke.push_back (strokePoint (e));

    applyStroke();
}

void ExpressionLane::mouseUp (const juce::MouseEvent&)
{
    if (dragging)
        document.endEdit();

    dragging = false;
    stroke.clear();
    originalCurves.clear();
    repaint();
}

void ExpressionLane::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (! plotArea().contains (e.position) || isVelocity())
        return;

    const auto beat = view.xToBeat (e.position.x - plotArea().getX());
    const auto lane = laneId();

    document.perform ("Clear " + getTargetName(), [&] (MpeClip& clip)
    {
        if (isZone())
        {
            if (auto* curve = clip.zone.find (lane))
                curve->clear();

            clip.zone.removeEmptyControllers();
            return;
        }

        for (auto& n : clip.notes)
        {
            if (noteIsEditable (n, beat, beat))
            {
                if (auto* curve = n.lanes.find (lane))
                    curve->clear();

                n.lanes.removeEmptyControllers();
            }
        }
    });
}

void ExpressionLane::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    const auto delta = std::abs (wheel.deltaX) > std::abs (wheel.deltaY) ? wheel.deltaX : wheel.deltaY;
    const auto x = e.position.x - plotArea().getX();

    if (e.mods.isCommandDown())
        view.zoomHorizontally (delta > 0 ? 1.15 : 1.0 / 1.15, x);
    else
        view.scrollHorizontally (-delta * 600.0 / view.pixelsPerBeat);
}

void ExpressionLane::clearLane()
{
    const auto lane = laneId();
    const auto selection = document.selection;
    const auto t = target;
    const auto zone = isZone();

    document.perform ("Clear " + getTargetName(), [&] (MpeClip& clip)
    {
        if (zone)
        {
            if (auto* curve = clip.zone.find (lane))
                curve->clear();

            clip.zone.removeEmptyControllers();
            return;
        }

        for (auto& n : clip.notes)
        {
            if (! selection.empty() && selection.count (n.id) == 0)
                continue;

            if (t == Target::strike)
                n.strike = defaultStrike;
            else if (t == Target::lift)
                n.lift = defaultLift;
            else if (auto* curve = n.lanes.find (lane))
                curve->clear();

            n.lanes.removeEmptyControllers();
        }
    });
}

//==============================================================================
void ExpressionLane::showLaneMenu()
{
    const auto& clip = document.getEditClip();
    std::set<int> usedNote, usedZone;

    for (const auto& n : clip.notes)
        for (const auto& c : n.lanes.controllers)
            usedNote.insert (c.controller);

    for (const auto& c : clip.zone.controllers)
        usedZone.insert (c.controller);

    auto tick = [this] (Target t, int cc = -1) { return target == t && (cc < 0 || cc == controller); };

    juce::PopupMenu menu;
    menu.addSectionHeader ("Per note (member channel)");
    menu.addItem (idStrike, "Velocity (strike)", true, tick (Target::strike));
    menu.addItem (idLift, "Release velocity (lift)", true, tick (Target::lift));
    menu.addItem (idPitch, "Pitch glide", true, tick (Target::notePitch));
    menu.addItem (idPressure, "Pressure (press)", true, tick (Target::notePressure));
    menu.addItem (idTimbre, "Timbre (slide)", true, tick (Target::noteTimbre));
    menu.addItem (idVibrato, "Vibrato (played by MPE Roll, any synth)", true, tick (Target::noteVibrato));

    juce::PopupMenu noteControllers;

    for (auto cc : usedNote)
        noteControllers.addItem (idNoteCCBase + cc, controllerName (cc) + " (CC" + juce::String (cc) + ")  - in this clip",
                                 true, tick (Target::noteController, cc));

    if (! usedNote.empty())
        noteControllers.addSeparator();

    for (auto cc : perNoteControllers)
        if (usedNote.count (cc) == 0)
            noteControllers.addItem (idNoteCCBase + cc, controllerName (cc) + " (CC" + juce::String (cc) + ")",
                                     true, tick (Target::noteController, cc));

    noteControllers.addSeparator();
    noteControllers.addItem (idOtherNoteCC, "Other controller...");
    menu.addSubMenu ("Per-note controller", noteControllers);

    menu.addSectionHeader ("Zone-wide (master channel)");
    menu.addItem (idZonePitch, "Pitch bend", true, tick (Target::zonePitch));
    menu.addItem (idZonePressure, "Channel pressure", true, tick (Target::zonePressure));
    menu.addItem (idZoneTimbre, "Timbre", true, tick (Target::zoneTimbre));
    menu.addItem (idZoneVibrato, "Vibrato", true, tick (Target::zoneVibrato));

    juce::PopupMenu zoneMenu;

    for (auto cc : usedZone)
        zoneMenu.addItem (idZoneCCBase + cc, controllerName (cc) + " (CC" + juce::String (cc) + ")  - in this clip",
                          true, tick (Target::zoneController, cc));

    if (! usedZone.empty())
        zoneMenu.addSeparator();

    for (auto cc : zoneControllers)
        if (usedZone.count (cc) == 0)
            zoneMenu.addItem (idZoneCCBase + cc, controllerName (cc) + " (CC" + juce::String (cc) + ")",
                              true, tick (Target::zoneController, cc));

    zoneMenu.addSeparator();
    zoneMenu.addItem (idOtherZoneCC, "Other controller...");
    menu.addSubMenu ("Zone controller", zoneMenu);

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (laneButton),
                        [safe = juce::Component::SafePointer<ExpressionLane> (this)] (int result)
    {
        if (safe == nullptr || result == 0)
            return;

        auto& self = *safe;

        switch (result)
        {
            case idStrike:          self.setTarget (Target::strike); return;
            case idLift:            self.setTarget (Target::lift); return;
            case idPitch:           self.setTarget (Target::notePitch); return;
            case idPressure:        self.setTarget (Target::notePressure); return;
            case idTimbre:          self.setTarget (Target::noteTimbre); return;
            case idZonePitch:       self.setTarget (Target::zonePitch); return;
            case idZonePressure:    self.setTarget (Target::zonePressure); return;
            case idZoneTimbre:      self.setTarget (Target::zoneTimbre); return;
            case idVibrato:         self.setTarget (Target::noteVibrato); return;
            case idZoneVibrato:     self.setTarget (Target::zoneVibrato); return;
            case idOtherNoteCC:     self.askForController (false); return;
            case idOtherZoneCC:     self.askForController (true); return;
            default: break;
        }

        if (result >= idZoneCCBase)
            self.setTarget (Target::zoneController, result - idZoneCCBase);
        else if (result >= idNoteCCBase)
            self.setTarget (Target::noteController, result - idNoteCCBase);
    });
}

void ExpressionLane::askForController (bool zone)
{
    auto* window = new juce::AlertWindow ("Controller lane", "Controller number (0-119):", juce::MessageBoxIconType::NoIcon);
    window->addTextEditor ("cc", juce::String (controller));
    window->addButton ("OK", 1, juce::KeyPress (juce::KeyPress::returnKey));
    window->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));

    window->enterModalState (true, juce::ModalCallbackFunction::create (
        [safe = juce::Component::SafePointer<ExpressionLane> (this), window, zone] (int result)
        {
            const auto cc = window->getTextEditorContents ("cc").getIntValue();

            if (safe == nullptr || result != 1)
                return;

            if (isReservedController (cc) || cc < 0 || cc > 119)
            {
                juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Controller lane",
                                                        "CC " + juce::String (cc) + " carries MIDI protocol data and can't be used as a lane.");
                return;
            }

            safe->setTarget (zone ? Target::zoneController : Target::noteController, cc);
        }), true);
}

void ExpressionLane::showRangeMenu()
{
    juce::PopupMenu menu;

    for (auto range : { 1, 2, 7, 12, 24, 48, 96 })
        menu.addItem (range, juce::String::fromUTF8 ("\xc2\xb1") + juce::String (range) + " semitones", true,
                      juce::roundToInt (pitchRange) == range);

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (rangeButton),
                        [safe = juce::Component::SafePointer<ExpressionLane> (this)] (int result)
    {
        if (safe != nullptr && result > 0)
        {
            safe->pitchRange = (float) result;
            safe->updateHeader();
            safe->repaint();
        }
    });
}

} // namespace mperoll
