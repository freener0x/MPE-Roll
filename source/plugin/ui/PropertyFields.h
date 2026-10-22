#pragma once

#include "MpeLookAndFeel.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

namespace mperoll
{

/** Property components bound to getters and setters, for panels built from a list of fields
    (the MPE settings, the tool dialogs). */

class ChoiceProp : public juce::ChoicePropertyComponent
{
public:
    ChoiceProp (const juce::String& name, const juce::StringArray& options,
                std::function<int()> getterFn, std::function<void (int)> setterFn)
        : ChoicePropertyComponent (name), getter (std::move (getterFn)), setter (std::move (setterFn))
    {
        choices = options;
    }

    void setIndex (int index) override  { setter (index); }
    int getIndex() const override       { return getter(); }

private:
    std::function<int()> getter;
    std::function<void (int)> setter;
};

class NumberProp : public juce::SliderPropertyComponent
{
public:
    NumberProp (const juce::String& name, double min, double max, double interval,
                std::function<double()> getterFn, std::function<void (double)> setterFn)
        : SliderPropertyComponent (name, min, max, interval), getter (std::move (getterFn)), setter (std::move (setterFn))
    {
    }

    void setValue (double value) override   { setter (value); }
    double getValue() const override        { return getter(); }

private:
    std::function<double()> getter;
    std::function<void (double)> setter;
};

class BoolProp : public juce::BooleanPropertyComponent
{
public:
    BoolProp (const juce::String& name, std::function<bool()> getterFn, std::function<void (bool)> setterFn)
        : BooleanPropertyComponent (name, "On", "Off"), getter (std::move (getterFn)), setter (std::move (setterFn))
    {
    }

    void setState (bool state) override     { setter (state); }
    bool getState() const override          { return getter(); }

private:
    std::function<bool()> getter;
    std::function<void (bool)> setter;
};

/** A read-only line of explanatory text that can change. */
class InfoProp : public juce::PropertyComponent
{
public:
    InfoProp (const juce::String& name, std::function<juce::String()> textFn)
        : PropertyComponent (name, 44), text (std::move (textFn))
    {
        label.setColour (juce::Label::textColourId, Theme::textDim);
        label.setFont (juce::FontOptions (12.5f));
        label.setJustificationType (juce::Justification::centredLeft);
        label.setMinimumHorizontalScale (1.0f);
        addAndMakeVisible (label);
        refresh();
    }

    void refresh() override     { label.setText (text(), juce::dontSendNotification); }

    void lookAndFeelChanged() override
    {
        label.setColour (juce::Label::textColourId, Theme::textDim);
    }

private:
    std::function<juce::String()> text;
    juce::Label label;
};

} // namespace mperoll
