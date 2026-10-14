#include "TimeRuler.h"
#include "MpeLookAndFeel.h"

namespace mperoll
{

TimeRuler::TimeRuler (ClipDocument& d, ViewState& v) : document (d), view (v)
{
    view.addListener (this);
    document.addChangeListener (this);
}

TimeRuler::~TimeRuler()
{
    document.removeChangeListener (this);
    view.removeListener (this);
}

void TimeRuler::paint (juce::Graphics& g)
{
    const auto w = (float) getWidth();
    const auto h = (float) getHeight();
    const auto& clip = document.getEditClip();

    g.fillAll (Theme::panel);

    // Loop / clip region.
    const auto endX = (float) view.beatToX (clip.length);
    g.setColour (clip.looping ? Theme::loopRegion : Theme::panelLight);
    g.fillRect (juce::Rectangle<float> ((float) view.beatToX (0.0), h - 6.0f, endX - (float) view.beatToX (0.0), 6.0f));

    // Ticks and bar numbers; skip labels when bars get too narrow.
    const auto barWidth = 4.0 * view.pixelsPerBeat;
    const int labelEvery = barWidth >= 36.0 ? 1 : barWidth >= 18.0 ? 2 : barWidth >= 9.0 ? 4 : 8;
    const auto firstBar = (int) std::floor (view.scrollBeat / 4.0);
    const auto lastBar = (int) std::ceil (view.xToBeat (w) / 4.0);

    g.setFont (juce::FontOptions (11.0f));

    for (int bar = firstBar; bar <= lastBar; ++bar)
    {
        const auto x = (float) view.beatToX (bar * 4.0);
        g.setColour (Theme::lineBar);
        g.fillRect (x, h * 0.35f, 1.0f, h * 0.65f);

        if (bar % labelEvery == 0)
        {
            g.setColour (Theme::textDim);
            g.drawText (juce::String (bar + 1), juce::Rectangle<float> (x + 3.0f, 0.0f, 40.0f, h * 0.6f),
                        juce::Justification::centredLeft, false);
        }

        if (view.pixelsPerBeat >= 10.0)
        {
            g.setColour (Theme::lineBeat);

            for (int beat = 1; beat < 4; ++beat)
                g.fillRect ((float) view.beatToX (bar * 4.0 + beat), h * 0.65f, 1.0f, h * 0.35f);
        }
    }

    // Loop end handle.
    juce::Path handle;
    handle.addTriangle (endX - 6.0f, 0.0f, endX + 6.0f, 0.0f, endX, 9.0f);
    g.setColour (Theme::loopRegion.brighter (0.6f));
    g.fillPath (handle);
    g.fillRect (endX - 1.0f, 0.0f, 2.0f, h);

    if (playheadVisible)
    {
        const auto x = (float) view.beatToX (playheadBeat);
        juce::Path marker;
        marker.addTriangle (x - 5.0f, h - 9.0f, x + 5.0f, h - 9.0f, x, h);
        g.setColour (Theme::playhead);
        g.fillPath (marker);
    }

    g.setColour (Theme::outline);
    g.fillRect (0.0f, h - 1.0f, w, 1.0f);
}

void TimeRuler::setPlayhead (double beat, bool visible)
{
    if (visible == playheadVisible && std::abs (beat - playheadBeat) * view.pixelsPerBeat < 0.5)
        return;

    playheadBeat = beat;
    playheadVisible = visible;
    repaint();
}

bool TimeRuler::isOverEndHandle (float x) const
{
    return std::abs (x - (float) view.beatToX (document.getEditClip().length)) <= 7.0f;
}

void TimeRuler::mouseMove (const juce::MouseEvent& e)
{
    setMouseCursor (isOverEndHandle (e.position.x) ? juce::MouseCursor::LeftRightResizeCursor
                                                   : juce::MouseCursor::NormalCursor);
}

void TimeRuler::mouseDown (const juce::MouseEvent& e)
{
    draggingEnd = isOverEndHandle (e.position.x);

    if (draggingEnd)
        document.beginEdit ("Clip length");
}

void TimeRuler::mouseDrag (const juce::MouseEvent& e)
{
    if (! draggingEnd)
        return;

    const auto step = e.mods.isAltDown() ? 0.25 : (e.mods.isShiftDown() ? 1.0 : 4.0);
    const auto beat = std::round (view.xToBeat (e.position.x) / step) * step;
    document.editClip().length = juce::jmax (step, beat);
    document.changed();
}

void TimeRuler::mouseUp (const juce::MouseEvent&)
{
    if (draggingEnd)
        document.endEdit();

    draggingEnd = false;
}

void TimeRuler::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    const auto delta = std::abs (wheel.deltaX) > std::abs (wheel.deltaY) ? wheel.deltaX : wheel.deltaY;

    if (e.mods.isShiftDown() || std::abs (wheel.deltaX) > std::abs (wheel.deltaY))
        view.scrollHorizontally (-delta * 600.0 / view.pixelsPerBeat);
    else
        view.zoomHorizontally (delta > 0 ? 1.15 : 1.0 / 1.15, e.position.x);
}

} // namespace mperoll
