#include "Toolbar.h"
#include "MpeLookAndFeel.h"
#include "plugin/HostedSynth.h"

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

    constexpr int iconButtonWidth = 34;
    constexpr int menuButtonWidth = 46;
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

int Toolbar::numSnapOptions()
{
    return (int) std::size (snapOptions);
}

juce::String Toolbar::snapNameForIndex (int index)
{
    return juce::isPositiveAndBelow (index, (int) std::size (snapOptions))
             ? juce::String (snapOptions[index].name).fromFirstOccurrenceOf ("Snap: ", false, false)
             : juce::String ("1/16");
}

Toolbar::Toolbar()
{
    for (auto* c : std::initializer_list<juce::Component*> {
             &synthButton, &showSynthButton, &slotBox, &lengthBox, &modeBox, &loopButton, &recordButton,
             &settingsButton, &drawToolButton, &paintToolButton, &eraseToolButton, &sliceToolButton,
             &selectToolButton, &glideToolButton, &glideSnapButton, &snapBox,
             &undoButton, &redoButton, &toolsButton, &midiButton, &flButton, &status })
        addAndMakeVisible (c);

    synthButton.setButtonText ("Load synth...");
    synthButton.setTooltip ("Pick the synth MPE Roll plays (" + HostedSynth::describeHostedFormats() + ")");
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
    recordButton.setAccent ([] { return Theme::record; });
    recordButton.setColour (juce::TextButton::buttonOnColourId, Theme::record.darker (0.3f));
    recordButton.setTooltip ("Record MPE from your controller while FL plays (overdubs into the clip)");

    settingsButton.setTooltip ("MPE settings: zones, bend ranges, pressure/timbre routing, input and playback "
                               "options, the window's look and skins");

    for (auto* b : { &drawToolButton, &paintToolButton, &eraseToolButton, &sliceToolButton, &selectToolButton, &glideToolButton })
    {
        b->setClickingTogglesState (true);
        b->setRadioGroupId (11);
    }

    // The tools and their keys are FL Studio's piano roll's.
    drawToolButton.setTooltip ("Draw (P): click empty space to add a note and drag to place it; drag a note to move "
                               "it, its right edge to resize. Right-drag deletes, Ctrl-drag selects, Shift-drag "
                               "duplicates, Alt bypasses snap.");
    paintToolButton.setTooltip ("Paint (B): drag to lay down notes one after another on the grid, each as long "
                                "as the last note you placed or resized. Right-drag deletes.");
    eraseToolButton.setTooltip ("Delete (D): click or drag across notes to delete them");
    sliceToolButton.setTooltip ("Slice (C): click a note to cut it there, on the grid, or drag up or down across "
                                "notes to cut them all at that beat. Alt bypasses snap.");
    selectToolButton.setTooltip ("Select (E): drag a box around notes to select them, or click one. Hold Ctrl to "
                                 "add to the selection; click empty space to select nothing.");
    glideToolButton.setTooltip ("Glide (G): drag from where the glide starts on a note to the pitch it reaches. "
                                "Right-drag erases, double-click clears.");
    glideSnapButton.setClickingTogglesState (true);
    glideSnapButton.setTooltip ("Snap glides: they land on whole semitones, starting and ending on the grid, in a "
                                "straight line. Hold Alt while drawing to draw freely.");

    for (int i = 0; i < (int) std::size (snapOptions); ++i)
        snapBox.addItem (snapOptions[i].name, i + 1);

    undoButton.setTooltip ("Undo (Ctrl+Z)");
    redoButton.setTooltip ("Redo (Ctrl+Y)");

    toolsButton.setOpensMenu (true);
    toolsButton.setTooltip ("Tools, as in FL's piano roll: quantize, chop, glue, arpeggiate, strum, flam, legato, "
                            "articulate, limit, flip, randomize and scale levels, each with FL's key");
    midiButton.setOpensMenu (true);
    midiButton.setTooltip ("File: import or export MPE MIDI files, export the clip as audio (WAV or MP3), "
                           "open the donation page, or drag this button into FL to drop the clip as a MIDI file");
    flButton.setTooltip ("Send to FL piano roll: writes a piano roll script that rebuilds this clip in FL's own "
                         "piano roll (slide notes, fine pitch, Mod X/Y) for FL's native synths");

    status.setColour (juce::Label::textColourId, Theme::textDim);
    status.setFont (juce::FontOptions (12.5f));
    status.setJustificationType (juce::Justification::centredRight);
}

void Toolbar::lookAndFeelChanged()
{
    recordButton.setColour (juce::TextButton::buttonOnColourId, Theme::record.darker (0.3f));
    status.setColour (juce::Label::textColourId, Theme::textDim);
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
    place (row1, loopButton, iconButtonWidth, 14);
    place (row1, modeBox, 120);
    place (row1, recordButton, iconButtonWidth);
    settingsButton.setBounds (row1.removeFromRight (iconButtonWidth));

    for (auto* tool : { &drawToolButton, &paintToolButton, &eraseToolButton, &sliceToolButton, &selectToolButton, &glideToolButton })
        place (row2, *tool, iconButtonWidth, 2);

    row2.removeFromLeft (8);
    place (row2, glideSnapButton, iconButtonWidth, 10);
    place (row2, snapBox, 110, 14);
    place (row2, undoButton, iconButtonWidth, 2);
    place (row2, redoButton, iconButtonWidth, 14);
    place (row2, toolsButton, menuButtonWidth);
    place (row2, midiButton, menuButtonWidth);
    place (row2, flButton, iconButtonWidth, 10);
    status.setBounds (row2);
}

} // namespace mperoll
