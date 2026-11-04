// SPDX-FileCopyrightText: 2026 The MPE Roll contributors
// SPDX-License-Identifier: AGPL-3.0-only

#include "MpeLookAndFeel.h"

namespace mperoll
{

MpeLookAndFeel::MpeLookAndFeel()
{
    applyTheme();
}

void MpeLookAndFeel::applyTheme()
{
    setColourScheme ({ Theme::panel,          // windowBackground
                       Theme::background,     // widgetBackground
                       Theme::panelLight,     // menuBackground
                       Theme::outline,        // outline
                       Theme::text,           // defaultText
                       Theme::panelLight,     // defaultFill
                       juce::Colours::white,  // highlightedText
                       Theme::loopRegion,     // highlightedFill
                       Theme::text });        // menuText

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
    setColour (juce::ToggleButton::textColourId, Theme::text);
    setColour (juce::TextEditor::backgroundColourId, Theme::background);
    setColour (juce::TextEditor::textColourId, Theme::text);
    setColour (juce::TextEditor::outlineColourId, Theme::outline);
    setColour (juce::Slider::textBoxTextColourId, Theme::text);
    setColour (juce::Slider::textBoxOutlineColourId, Theme::outline);
    setColour (juce::BooleanPropertyComponent::backgroundColourId, Theme::panelLight);
    setColour (juce::BooleanPropertyComponent::outlineColourId, Theme::outline);
    setColour (juce::TooltipWindow::textColourId, Theme::text);
    setColour (juce::TooltipWindow::outlineColourId, Theme::outline);
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

void MpeLookAndFeel::drawPropertyComponentLabel (juce::Graphics& g, int, int height, juce::PropertyComponent& component)
{
    // As JUCE's own, but a long name stops short of the value instead of running under it (JUCE's
    // leaves the indent out of the width), and a row made tall enough for two lines gets them rather
    // than one line squeezed narrow. The indent is JUCE's, whose function for it is private.
    const auto indent = juce::jmin (10, component.getWidth() / 10);
    const auto value = getPropertyComponentContentPosition (component);
    const auto fontHeight = (float) juce::jmin (height, 24) * 0.65f;
    const auto twoLines = (float) value.getHeight() >= fontHeight * 2.4f;

    g.setColour (component.findColour (juce::PropertyComponent::labelTextColourId)
                     .withMultipliedAlpha (component.isEnabled() ? 1.0f : 0.6f));
    g.setFont (fontHeight);
    g.drawFittedText (component.getName(), indent, value.getY(), value.getX() - indent - 5, value.getHeight(),
                      juce::Justification::centredLeft, 2, twoLines ? 1.0f : 0.0f);
}

} // namespace mperoll
