#include "Toolbar.h"
#include "MpeLookAndFeel.h"

namespace mperoll
{

namespace
{
    struct SnapOption
    {
        const char* name;
        double beats;
    };

    const SnapOption snapOptions[] = {
        { "Snap: off", 0.0 },
        { "Snap: bar", 4.0 },
        { "Snap: 1/2", 2.0 },
        { "Snap: beat", 1.0 },
        { "Snap: 1/8", 0.5 },
        { "Snap: 1/8T", 1.0 / 3.0 },
        { "Snap: 1/16", 0.25 },
        { "Snap: 1/16T", 1.0 / 6.0 },
        { "Snap: 1/32", 0.125 },
        { "Snap: 1/64", 0.0625 },
    };
}

double Toolbar::snapForIndex (int index)
{
    return juce::isPositiveAndBelow (index, (int) std::size (snapOptions)) ? snapOptions[index].beats : 0.25;
}

int Toolbar::indexForSnap (double beats)
{
    for (int i = 0; i < (int) std::size (snapOptions); ++i)
        if (std::abs (snapOptions[i].beats - beats) < 1.0e-6)
            return i;

    return 6;
}

Toolbar::Toolbar()
{
    for (auto* c : std::initializer_list<juce::Component*> {
             &synthButton, &showSynthButton, &slotBox, &lengthBox, &modeBox, &loopButton, &recordButton,
             &settingsButton, &editToolButton, &glideToolButton, &snapBox, &undoButton, &redoButton, &midiButton,
             &flButton, &status })
        addAndMakeVisible (c);

    synthButton.setButtonText ("Load synth...");
    synthButton.setTooltip ("Pick the synth MPE Roll plays (VST3 or VST2)");
    showSynthButton.setTooltip ("Open the synth's own window");

    for (int i = 0; i < 16; ++i)
        slotBox.addItem ("Clip " + juce::String (i + 1), i + 1);

    slotBox.setTooltip ("Clip slot being edited. In note-trigger mode, FL notes pick the slot that plays.");

    for (auto bars : { 1, 2, 4, 8, 16, 32, 64 })
        lengthBox.addItem (juce::String (bars) + (bars == 1 ? " bar" : " bars"), bars);

    lengthBox.setTooltip ("Clip length (or drag the green handle on the ruler)");

    modeBox.addItem ("Follow FL", 1);
    modeBox.addItem ("Note trigger", 2);
    modeBox.setTooltip ("Follow FL: the clip plays in sync with FL's song/pattern position.\n"
                        "Note trigger: notes from FL's piano roll launch clip slots (C0 = slot 1 by default).");

    loopButton.setClickingTogglesState (true);
    loopButton.setTooltip ("Loop the clip over its length");

    recordButton.setClickingTogglesState (true);
    recordButton.setButtonText (juce::String::fromUTF8 ("\xe2\x97\x8f Rec"));
    recordButton.setColour (juce::TextButton::buttonOnColourId, Theme::record.darker (0.3f));
    recordButton.setTooltip ("Record MPE from your controller while FL plays (overdubs into the clip)");

    editToolButton.setClickingTogglesState (true);
    glideToolButton.setClickingTogglesState (true);
    editToolButton.setRadioGroupId (11);
    glideToolButton.setRadioGroupId (11);
    editToolButton.setTooltip ("Edit notes (E)");
    glideToolButton.setTooltip ("Draw pitch glides directly on notes (G). Shift snaps to semitones, right-drag erases.");

    for (int i = 0; i < (int) std::size (snapOptions); ++i)
        snapBox.addItem (snapOptions[i].name, i + 1);

    midiButton.setButtonText ("MIDI file" + juce::String::fromUTF8 (" \xe2\x96\xbe"));
    midiButton.setTooltip ("Import or export MPE MIDI files, or drag this button into FL");
    flButton.setTooltip ("Write a piano roll script that rebuilds this clip in FL's own piano roll "
                         "(slide notes, fine pitch, Mod X/Y) for FL's native synths");
    settingsButton.setTooltip ("MPE zones, bend ranges, pressure/timbre routing, input and playback options");

    status.setColour (juce::Label::textColourId, Theme::textDim);
    status.setFont (juce::FontOptions (12.5f));
    status.setJustificationType (juce::Justification::centredRight);
}

void Toolbar::paint (juce::Graphics& g)
{
    g.fillAll (Theme::panel);
    g.setColour (Theme::outline);
    g.fillRect (0, getHeight() - 1, getWidth(), 1);
}

void Toolbar::resized()
{
    auto area = getLocalBounds().reduced (6, 3);
    auto row1 = area.removeFromTop (rowHeight).reduced (0, 2);
    area.removeFromTop (2);
    auto row2 = area.removeFromTop (rowHeight).reduced (0, 2);

    auto place = [] (juce::Rectangle<int>& row, juce::Component& c, int width, int gap = 4)
    {
        c.setBounds (row.removeFromLeft (width));
        row.removeFromLeft (gap);
    };

    place (row1, synthButton, 200);
    place (row1, showSynthButton, 84, 14);
    place (row1, slotBox, 90);
    place (row1, lengthBox, 84);
    place (row1, loopButton, 52, 14);
    place (row1, modeBox, 120);
    place (row1, recordButton, 64);
    settingsButton.setBounds (row1.removeFromRight (110));

    place (row2, editToolButton, 52, 2);
    place (row2, glideToolButton, 56, 10);
    place (row2, snapBox, 110, 14);
    place (row2, undoButton, 52, 2);
    place (row2, redoButton, 52, 14);
    place (row2, midiButton, 96);
    place (row2, flButton, 168, 10);
    status.setBounds (row2);
}

} // namespace mperoll
