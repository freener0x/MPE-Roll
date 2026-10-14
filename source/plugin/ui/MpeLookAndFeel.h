#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace mperoll
{

namespace Theme
{
    inline const juce::Colour background   { 0xff16181c };
    inline const juce::Colour panel        { 0xff1e2127 };
    inline const juce::Colour panelLight   { 0xff292d35 };
    inline const juce::Colour outline      { 0xff363b45 };
    inline const juce::Colour rowWhite     { 0xff20232a };
    inline const juce::Colour rowBlack     { 0xff1a1c21 };
    inline const juce::Colour lineBar      { 0xff4c5260 };
    inline const juce::Colour lineBeat     { 0xff323743 };
    inline const juce::Colour lineSub      { 0xff272a32 };
    inline const juce::Colour note         { 0xff6fcf97 };
    inline const juce::Colour noteSelected { 0xffb9f5d0 };
    inline const juce::Colour glide        { 0xffffb04a };
    inline const juce::Colour pressure     { 0xff58a6ff };
    inline const juce::Colour timbre       { 0xffd291ff };
    inline const juce::Colour controller   { 0xff4fd6d6 };
    inline const juce::Colour velocity     { 0xff6fcf97 };
    inline const juce::Colour playhead     { 0xfff2f4f7 };
    inline const juce::Colour record       { 0xffff5a5a };
    inline const juce::Colour text         { 0xffd7dbe2 };
    inline const juce::Colour textDim      { 0xff8a909c };
    inline const juce::Colour loopRegion   { 0xff2f6f4c };
}

class MpeLookAndFeel : public juce::LookAndFeel_V4
{
public:
    MpeLookAndFeel();

    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour& backgroundColour,
                               bool isMouseOverButton, bool isButtonDown) override;

    juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override;
    juce::Font getLabelFont (juce::Label&) override;
};

} // namespace mperoll
