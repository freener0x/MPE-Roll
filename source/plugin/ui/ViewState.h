#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <cmath>

namespace mperoll
{

/** Zoom, scroll, snap and tool state shared by the grid, ruler, keyboard and expression lane. */
class ViewState
{
public:
    enum class Tool { edit, paint, glide };

    struct Listener
    {
        virtual ~Listener() = default;
        virtual void viewChanged() = 0;
    };

    double pixelsPerBeat = 96.0;
    double scrollBeat = 0.0;        // beat at the left edge of the grid
    float rowHeight = 14.0f;
    float scrollKey = 90.0f;        // the key just above the top edge: the top row shows key (scrollKey - 1)
    double snap = 0.25;             // beats, 0 = off
    bool snapGlide = true;          // glides land on whole semitones (and on the grid)
    Tool tool = Tool::edit;
    double lastNoteLength = 1.0;

    static constexpr int keysWidth = 64;
    static constexpr int scrollbarSize = 12;
    static constexpr double minPixelsPerBeat = 6.0, maxPixelsPerBeat = 1600.0;
    static constexpr float minRowHeight = 6.0f, maxRowHeight = 40.0f;

    double beatToX (double beat) const noexcept         { return (beat - scrollBeat) * pixelsPerBeat; }
    double xToBeat (double x) const noexcept            { return scrollBeat + x / pixelsPerBeat; }

    /** Top edge of a key's row. */
    float keyToY (int key) const noexcept               { return (scrollKey - (float) key - 1.0f) * rowHeight; }

    /** Vertical centre for a continuous pitch (key + glide). */
    float pitchToY (float pitch) const noexcept         { return (scrollKey - pitch - 0.5f) * rowHeight; }
    float yToPitch (float y) const noexcept             { return scrollKey - 0.5f - y / rowHeight; }
    int yToKey (float y) const noexcept                 { return (int) std::floor (scrollKey - y / rowHeight); }

    double snapRound (double beat, bool bypass) const noexcept
    {
        return (snap <= 0.0 || bypass) ? beat : std::round (beat / snap) * snap;
    }

    double snapFloor (double beat, bool bypass) const noexcept
    {
        return (snap <= 0.0 || bypass) ? beat : std::floor (beat / snap + 1.0e-9) * snap;
    }

    /** Snap step to use for keyboard nudges when snapping is off. */
    double nudgeStep() const noexcept                   { return snap > 0.0 ? snap : 0.25; }

    void zoomHorizontally (double factor, double anchorX)
    {
        const auto anchorBeat = xToBeat (anchorX);
        pixelsPerBeat = juce::jlimit (minPixelsPerBeat, maxPixelsPerBeat, pixelsPerBeat * factor);
        scrollBeat = juce::jmax (0.0, anchorBeat - anchorX / pixelsPerBeat);
        changed();
    }

    void zoomVertically (float factor, float anchorY, float visibleHeight)
    {
        const auto anchorPitch = scrollKey - anchorY / rowHeight;
        rowHeight = juce::jlimit (minRowHeight, maxRowHeight, rowHeight * factor);
        scrollKey = anchorPitch + anchorY / rowHeight;
        clampVertical (visibleHeight);
        changed();
    }

    void scrollHorizontally (double beats)
    {
        scrollBeat = juce::jmax (0.0, scrollBeat + beats);
        changed();
    }

    void scrollVertically (float keys, float visibleHeight)
    {
        scrollKey += keys;
        clampVertical (visibleHeight);
        changed();
    }

    void clampVertical (float visibleHeight)
    {
        const auto visibleRows = visibleHeight / rowHeight;
        scrollKey = juce::jlimit (juce::jmin (128.0f, visibleRows), 128.0f, scrollKey);
    }

    void addListener (Listener* l)                      { listeners.add (l); }
    void removeListener (Listener* l)                   { listeners.remove (l); }
    void changed()                                      { listeners.call ([] (Listener& l) { l.viewChanged(); }); }

private:
    juce::ListenerList<Listener> listeners;
};

} // namespace mperoll
