#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace mperoll
{

/** How the editor's window is drawn, and keeping the window's idea of its size honest.

    On Windows JUCE draws with Direct2D, which stretches its picture to fit the window when the two
    disagree about the window's size, so what's drawn and where the mouse lands drift apart. That
    has been seen after FL Studio closes and reopens a plugin window, so in FL the window is drawn
    by JUCE's software renderer (what JUCE 7 plugins use) unless chosen otherwise.
*/
namespace WindowDrawing
{
    enum class Mode { automatic, software, gpu };

    /** The modes, in the order the settings list them. */
    juce::StringArray modeNames();

    /** True where there is a choice to make (Windows). */
    bool isChoiceAvailable();

    Mode getMode (juce::PropertiesFile*);
    void setMode (juce::PropertiesFile*, Mode);

    /** Draws the component's window the way the mode asks for, if the component is in one. */
    void apply (juce::Component&, Mode);

    /** Tells the component's window to read its real size and scale again, in case it was changed
        behind JUCE's back (a host hiding, moving or reparenting the window). Returns true if
        anything was out of date.
    */
    bool resync (juce::Component&);

    /** One line about the component's window: its scale, how it's drawn, and its size. */
    juce::String describe (const juce::Component&);
}

} // namespace mperoll
