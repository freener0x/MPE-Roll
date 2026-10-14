#include "MpeLookAndFeel.h"

namespace mperoll
{

MpeLookAndFeel::MpeLookAndFeel()
    : LookAndFeel_V4 (juce::LookAndFeel_V4::ColourScheme {
          Theme::panel,          // windowBackground
          Theme::background,     // widgetBackground
          Theme::panelLight,     // menuBackground
          Theme::outline,        // outline
          Theme::text,           // defaultText
          Theme::panelLight,     // defaultFill
          juce::Colours::white,  // highlightedText
          Theme::loopRegion,     // highlightedFill
          Theme::text })         // menuText
{
    setColour (juce::TextButton::buttonColourId, Theme::panelLight);
    setColour (juce::TextButton::buttonOnColourId, Theme::loopRegion);
    setColour (juce::TextButton::textColourOffId, Theme::text);
    setColour (juce::TextButton::textColourOnId, juce::Colours::white);
    setColour (juce::ComboBox::backgroundColourId, Theme::panelLight);
    setColour (juce::ComboBox::outlineColourId, Theme::outline);
    setColour (juce::ComboBox::arrowColourId, Theme::textDim);
    setColour (juce::PopupMenu::backgroundColourId, Theme::panel);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, Theme::loopRegion);
    setColour (juce::Label::textColourId, Theme::text);
    setColour (juce::ScrollBar::thumbColourId, Theme::outline.brighter (0.2f));
    setColour (juce::PropertyComponent::backgroundColourId, Theme::panel);
    setColour (juce::PropertyComponent::labelTextColourId, Theme::text);
    setColour (juce::AlertWindow::backgroundColourId, Theme::panel);
    setColour (juce::TooltipWindow::backgroundColourId, Theme::panelLight);
    setColour (juce::Slider::thumbColourId, Theme::note);
    setColour (juce::Slider::trackColourId, Theme::loopRegion);
    setColour (juce::Slider::backgroundColourId, Theme::panelLight);
    setColour (juce::ToggleButton::tickColourId, Theme::note);
}

void MpeLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& button, const juce::Colour& backgroundColour,
                                           bool isMouseOverButton, bool isButtonDown)
{
    auto bounds = button.getLocalBounds().toFloat().reduced (0.5f);
    auto colour = backgroundColour;

    if (isButtonDown)
        colour = colour.brighter (0.15f);
    else if (isMouseOverButton)
        colour = colour.brighter (0.07f);

    g.setColour (colour);
    g.fillRoundedRectangle (bounds, 4.0f);
    g.setColour (Theme::outline);
    g.drawRoundedRectangle (bounds, 4.0f, 1.0f);
}

juce::Font MpeLookAndFeel::getTextButtonFont (juce::TextButton&, int buttonHeight)
{
    return juce::Font (juce::FontOptions (juce::jmin (14.0f, (float) buttonHeight * 0.55f)));
}

juce::Font MpeLookAndFeel::getComboBoxFont (juce::ComboBox&)
{
    return juce::Font (juce::FontOptions (13.5f));
}

juce::Font MpeLookAndFeel::getLabelFont (juce::Label& label)
{
    return label.getFont();
}

} // namespace mperoll
