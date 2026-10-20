#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace mperoll
{

/** The colours everything is drawn with. The skin (see Skins.h) sets them, on the message thread;
    these are the default skin's.
*/
namespace Theme
{
    inline juce::Colour background   { 0xff16181c };
    inline juce::Colour panel        { 0xff1e2127 };
    inline juce::Colour panelLight   { 0xff292d35 };
    inline juce::Colour outline      { 0xff363b45 };
    inline juce::Colour rowWhite     { 0xff20232a };
    inline juce::Colour rowBlack     { 0xff1a1c21 };
    inline juce::Colour lineBar      { 0xff4c5260 };
    inline juce::Colour lineBeat     { 0xff323743 };
    inline juce::Colour lineSub      { 0xff272a32 };
    inline juce::Colour note         { 0xff6fcf97 };
    inline juce::Colour noteSelected { 0xffb9f5d0 };
    inline juce::Colour noteText     { 0xbf000000 };
    inline juce::Colour glide        { 0xffffb04a };
    inline juce::Colour pressure     { 0xff58a6ff };
    inline juce::Colour timbre       { 0xffd291ff };
    inline juce::Colour controller   { 0xff4fd6d6 };
    inline juce::Colour velocity     { 0xff6fcf97 };
    inline juce::Colour playhead     { 0xfff2f4f7 };
    inline juce::Colour record       { 0xffff5a5a };
    inline juce::Colour text         { 0xffd7dbe2 };
    inline juce::Colour textDim      { 0xff8a909c };
    inline juce::Colour loopRegion   { 0xff2f6f4c };
    inline juce::Colour whiteKey     { 0xffd9dce2 };
    inline juce::Colour blackKey     { 0xff23252b };
    inline juce::Colour keyLabel     { 0xff4a4f59 };
}

class MpeLookAndFeel : public juce::LookAndFeel_V4
{
public:
    MpeLookAndFeel();

    /** Takes up Theme's colours again, after the skin has changed them. */
    void applyTheme();

    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour& backgroundColour,
                               bool isMouseOverButton, bool isButtonDown) override;

    juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override;
    juce::Font getLabelFont (juce::Label&) override;
};

} // namespace mperoll
