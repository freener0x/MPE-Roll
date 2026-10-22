#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace mperoll::WindowPlacement
{

/** The work area (the display without the taskbar) of the display showing the given component, or
    the one under the mouse when there's none, or the main display. */
juce::Rectangle<int> workAreaNear (const juce::Component* near);

/** The window's frame (title bar and borders) once it's on screen, or a typical one before that. */
juce::BorderSize<int> frameOf (const juce::Component& window);

/** Where a window of the given client size and frame goes in the work area. Pure geometry, so it
    can be tested: the frame always fits inside the area when the client does; a client too big for
    the area is shrunk when it may be, and otherwise kept with its title bar at the top of the area
    (so it can be dragged and closed) and its left edge inside it. With centre, the window is
    centred, else it stays where it is unless that leaves part of the frame outside.
    Returns the client bounds. */
juce::Rectangle<int> place (juce::Rectangle<int> client, juce::BorderSize<int> frame, juce::Rectangle<int> workArea,
                            bool mayShrink, bool centre, juce::Point<int> minimumSize = { 160, 100 });

/** Puts a top-level window on the display showing `near`, centred and fitted (see place()). The
    window must already be on the desktop, so its frame is known. Returns whether it was shrunk. */
bool showOnDisplayOf (juce::Component& window, const juce::Component* near, bool mayShrink);

/** Moves a window that has gone partly off its display (a synth that grew, say) back inside, without
    centring it. Returns whether it moved. */
bool keepOnScreen (juce::Component& window, bool mayShrink);

} // namespace mperoll::WindowPlacement
