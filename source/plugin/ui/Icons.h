#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace mperoll
{

/** The toolbar's pictures: line art designed in a 24 x 24 box and drawn in the skin's colours at any size. */
enum class Icon
{
    draw,           // pencil: the Draw (edit) tool
    paint,          // brush
    erase,          // eraser: the Delete tool
    slice,          // scissors
    select,         // dashed box with a pointer
    glide,          // a note with a pitch curve leaving it
    snapGlide,      // magnet
    undo,
    redo,
    loop,
    record,
    settings,       // gear
    file,           // a page
    sendToFl,       // an arrow leaving a box
    tools           // wrench
};

/** The strokes and fills of an icon, in the 24 x 24 design box. */
struct IconArt
{
    juce::Path strokes;
    juce::Path fills;           // in the main colour
    juce::Path accentFills;     // in the accent colour (the brush's bristles, the record dot...)
    float strokeWidth = 1.9f;
};

IconArt iconArt (Icon);

/** Draws an icon fitted into the area, keeping its proportions. */
void drawIcon (juce::Graphics&, Icon, juce::Rectangle<float> area, juce::Colour colour, juce::Colour accent);

/** A mouse cursor drawn from an icon in the skin's colours. The hot spot is given in the design box. */
juce::MouseCursor makeIconCursor (Icon, juce::Point<float> hotSpot, juce::Colour colour, juce::Colour accent, int size = 22);

/** A button that shows an icon instead of words (the words belong in the tooltip). It toggles, joins
    radio groups and is enabled or disabled like a TextButton, and the look and feel draws it the
    same background; the icon takes the text colours. */
class IconButton : public juce::Button
{
public:
    IconButton (const juce::String& name, Icon icon, const juce::String& tooltip);

    void setIcon (Icon newIcon);
    Icon getIcon() const noexcept                       { return icon; }

    /** Adds a small down arrow at the right, for a button that opens a menu. */
    void setOpensMenu (bool shouldShowArrow);

    /** The accent colour the icon's accent fills take; the skin's note colour unless set. */
    void setAccent (std::function<juce::Colour()> accentFn);

    void paintButton (juce::Graphics&, bool isMouseOverButton, bool isButtonDown) override;

private:
    Icon icon;
    bool opensMenu = false;
    std::function<juce::Colour()> accent;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (IconButton)
};

} // namespace mperoll
