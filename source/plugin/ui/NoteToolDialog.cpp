#include "NoteToolDialog.h"
#include "MpeLookAndFeel.h"
#include "PropertyFields.h"
#include "Toolbar.h"

#include <juce_audio_basics/juce_audio_basics.h>

namespace mperoll
{

namespace
{
    constexpr int alt = juce::ModifierKeys::altModifier;
    constexpr int ctrl = juce::ModifierKeys::commandModifier;   // Ctrl on Windows and Linux, as FL has it

    juce::KeyPress keyOf (char letter, int modifiers)   { return { letter, juce::ModifierKeys (modifiers), 0 }; }
    juce::KeyPress noKey()                              { return {}; }

    const std::array<NoteToolDialog::Info, NoteToolDialog::numKinds> tools = { {
        { NoteToolDialog::Kind::quantize,    "Quantize",     "Moves note starts (and ends) onto the grid, by the given strength.", keyOf ('q', alt), keyOf ('q', ctrl) },
        { NoteToolDialog::Kind::chop,        "Chop",         "Cuts each note into pieces of one length.",                           keyOf ('u', alt), keyOf ('u', ctrl) },
        { NoteToolDialog::Kind::glue,        "Glue",         "Joins notes of the same pitch that touch or overlap into one.",       noKey(),          keyOf ('g', ctrl) },
        { NoteToolDialog::Kind::arpeggiate,  "Arpeggiate",   "Turns each chord into a run of its notes, one per step.",            keyOf ('a', alt), noKey() },
        { NoteToolDialog::Kind::strum,       "Strum",        "Spreads the notes of each chord in time, like a strummed guitar.",   keyOf ('s', alt), noKey() },
        { NoteToolDialog::Kind::flam,        "Flam",         "Adds a short grace note just before each note.",                     keyOf ('f', alt), noKey() },
        { NoteToolDialog::Kind::legato,      "Legato",       "Lengthens each note to where the next one starts.",                  noKey(),          keyOf ('l', ctrl) },
        { NoteToolDialog::Kind::articulate,  "Articulate",   "Scales note lengths: shorter for staccato, longer for legato.",      keyOf ('l', alt), noKey() },
        { NoteToolDialog::Kind::limit,       "Limit",        "Moves notes by octaves into a key range.",                           keyOf ('k', alt), noKey() },
        { NoteToolDialog::Kind::flip,        "Flip",         "Mirrors pitches, or reverses time, within the notes.",               keyOf ('y', alt), noKey() },
        { NoteToolDialog::Kind::randomize,   "Randomize",    "Shakes velocities, pitches, lengths, starts and starting expression by chance.", keyOf ('r', alt), noKey() },
        { NoteToolDialog::Kind::scaleLevels, "Scale levels", "Scales and offsets velocities and expression lanes.",                keyOf ('x', alt), noKey() },
    } };

    double gridFor (int snapIndex, double current)
    {
        return snapIndex < 0 ? current : Toolbar::snapForIndex (snapIndex);
    }

    /** The grid choices: the grid in use first, then every snap value. */
    juce::StringArray gridNames (double current)
    {
        juce::StringArray names;
        names.add ("Current snap (" + Toolbar::snapNameForIndex (Toolbar::indexForSnap (current)) + ")");

        for (int i = 1; i < Toolbar::numSnapOptions(); ++i)
            names.add (Toolbar::snapNameForIndex (i));

        return names;
    }

    juce::String keyName (int key)
    {
        return juce::MidiMessage::getMidiNoteName (key, true, true, 5);
    }
}

//==============================================================================
const NoteToolDialog::Info& NoteToolDialog::info (Kind kind)
{
    return tools[(size_t) kind];
}

const std::array<NoteToolDialog::Info, NoteToolDialog::numKinds>& NoteToolDialog::all()
{
    return tools;
}

juce::String NoteToolDialog::describeKey (const juce::KeyPress& key)
{
    if (! key.isValid())
        return {};

    juce::String text;
    const auto mods = key.getModifiers();

    if (mods.isCtrlDown() || mods.isCommandDown())
        text << "Ctrl+";

    if (mods.isAltDown())
        text << "Alt+";

    if (mods.isShiftDown())
        text << "Shift+";

    return text + juce::String::charToString (juce::CharacterFunctions::toUpperCase ((juce::juce_wchar) key.getKeyCode()));
}

bool NoteToolDialog::matches (const juce::KeyPress& wanted, const juce::KeyPress& pressed)
{
    if (! wanted.isValid())
        return false;

    const auto letter = [] (const juce::KeyPress& k) { return juce::CharacterFunctions::toLowerCase ((juce::juce_wchar) k.getKeyCode()); };
    const auto a = wanted.getModifiers(), b = pressed.getModifiers().withoutMouseButtons();

    return letter (wanted) == letter (pressed)
        && a.isAltDown() == b.isAltDown()
        && a.isCommandDown() == b.isCommandDown()
        && a.isShiftDown() == b.isShiftDown();
}

NoteToolDialog::Settings& NoteToolDialog::settings()
{
    static Settings shared;
    return shared;
}

std::set<uint32_t> NoteToolDialog::run (Kind kind, MpeClip& clip, const std::set<uint32_t>& selection,
                                        const Settings& s, double gridBeats)
{
    switch (kind)
    {
        case Kind::quantize:
        {
            auto o = s.quantize;
            o.grid = gridFor (s.quantizeGrid, gridBeats);
            return notetools::quantize (clip, selection, o);
        }

        case Kind::chop:
        {
            auto o = s.chop;
            o.length = gridFor (s.chopGrid, gridBeats);
            return notetools::chop (clip, selection, o);
        }

        case Kind::glue:        return notetools::glue (clip, selection, s.glue);

        case Kind::arpeggiate:
        {
            auto o = s.arpeggiate;
            o.step = gridFor (s.arpeggioStep, gridBeats);
            return notetools::arpeggiate (clip, selection, o);
        }

        case Kind::strum:       return notetools::strum (clip, selection, s.strum);
        case Kind::flam:        return notetools::flam (clip, selection, s.flam);
        case Kind::legato:      return notetools::legato (clip, selection, s.legato);
        case Kind::articulate:  return notetools::articulate (clip, selection, s.articulate);
        case Kind::limit:       return notetools::limit (clip, selection, s.limit);
        case Kind::flip:        return notetools::flip (clip, selection, s.flip);
        case Kind::randomize:   return notetools::randomize (clip, selection, s.randomize);
        case Kind::scaleLevels: return notetools::scaleLevels (clip, selection, s.scaleLevels);
    }

    return {};
}

void NoteToolDialog::applyNow (Kind kind, ClipDocument& document, double gridBeats)
{
    const auto selection = document.selection;
    std::set<uint32_t> made;

    document.perform (info (kind).name, [&] (MpeClip& clip)
    {
        made = run (kind, clip, selection, settings(), gridBeats);
    });

    document.selection = made;
    document.selectionChanged();
}

//==============================================================================
NoteToolDialog::NoteToolDialog (Kind k, ClipDocument& d, double gridBeats)
    : kind (k), document (&d), grid (gridBeats), original (d.getEditClip()),
      originalSelection (d.selection), targets (d.selection)
{
    const auto& i = info (kind);

    title.setText (juce::String (i.name) + (i.key.isValid() ? "   " + describeKey (i.key) : juce::String()),
                   juce::dontSendNotification);
    title.setFont (juce::FontOptions (15.0f, juce::Font::bold));
    title.setColour (juce::Label::textColourId, Theme::text);

    hint.setText (juce::String (i.hint) + (targets.empty() ? " Works on every note, as nothing is selected."
                                                            : " Works on the selected notes."),
                  juce::dontSendNotification);
    hint.setFont (juce::FontOptions (12.5f));
    hint.setColour (juce::Label::textColourId, Theme::textDim);
    hint.setMinimumHorizontalScale (1.0f);

    acceptButton.setTooltip ("Keep this (Return)");
    cancelButton.setTooltip ("Put the notes back (Escape)");
    acceptButton.onClick = [this] { accept(); };
    cancelButton.onClick = [this] { cancel(); };

    for (auto* c : std::initializer_list<juce::Component*> { &title, &hint, &panel, &acceptButton, &cancelButton })
        addAndMakeVisible (c);

    buildFields();
    setWantsKeyboardFocus (true);
    setSize (400, 12 + 24 + 36 + panel.getTotalContentHeight() + 12 + 30 + 10);

    document->beginEdit (i.name);
    preview();
}

NoteToolDialog::~NoteToolDialog()
{
    // Dismissed without a word (a click elsewhere, Escape to the call-out): the preview goes.
    finish (false);
}

void NoteToolDialog::buildFields()
{
    auto& s = settings();
    juce::Array<juce::PropertyComponent*> fields;
    auto changed = [this] { preview(); };

    auto percent = [&] (const juce::String& name, float& value, double min = 0.0, double max = 100.0)
    {
        fields.add (new NumberProp (name, min, max, 1.0,
                                    [&value] { return std::round (value * 100.0f); },
                                    [&value, changed] (double v) { value = (float) (v / 100.0); changed(); }));
    };

    auto beats = [&] (const juce::String& name, double& value, double min, double max, double step)
    {
        fields.add (new NumberProp (name, min, max, step, [&value] { return value; },
                                    [&value, changed] (double v) { value = v; changed(); }));
    };

    auto count = [&] (const juce::String& name, int& value, int min, int max)
    {
        fields.add (new NumberProp (name, min, max, 1.0, [&value] { return (double) value; },
                                    [&value, changed] (double v) { value = (int) std::round (v); changed(); }));
    };

    auto toggle = [&] (const juce::String& name, bool& value)
    {
        fields.add (new BoolProp (name, [&value] { return value; }, [&value, changed] (bool v) { value = v; changed(); }));
    };

    auto gridChoice = [&] (const juce::String& name, int& snapIndex)
    {
        fields.add (new ChoiceProp (name, gridNames (grid),
                                    [&snapIndex] { return snapIndex < 0 ? 0 : snapIndex; },
                                    [&snapIndex, changed] (int v) { snapIndex = v == 0 ? -1 : v; changed(); }));
    };

    auto factor = [&] (const juce::String& name, float& value, double min, double max)
    {
        fields.add (new NumberProp (name, min, max, 0.01, [&value] { return (double) value; },
                                    [&value, changed] (double v) { value = (float) v; changed(); }));
    };

    switch (kind)
    {
        case Kind::quantize:
            gridChoice ("Grid", s.quantizeGrid);
            percent ("Strength (%)", s.quantize.strength);
            toggle ("Starts", s.quantize.starts);
            toggle ("Ends", s.quantize.ends);
            break;

        case Kind::chop:
            gridChoice ("Length", s.chopGrid);
            percent ("Gate (%)", s.chop.gate, 10.0);
            break;

        case Kind::glue:
            beats ("Gap to close (beats)", s.glue.gap, 0.0, 4.0, 0.0625);
            break;

        case Kind::arpeggiate:
        {
            auto& o = s.arpeggiate;
            fields.add (new ChoiceProp ("Pattern", { "Up", "Down", "Up and down", "Down and up", "Random" },
                                        [&o] { return (int) o.pattern; },
                                        [&o, changed] (int v) { o.pattern = (notetools::ArpeggiateOptions::Pattern) v; changed(); }));
            gridChoice ("Step", s.arpeggioStep);
            percent ("Gate (%)", o.gate, 10.0);
            count ("Range (octaves)", o.range, 1, 4);
            fields.add (new NumberProp ("Seed (random pattern)", 1.0, 999.0, 1.0, [&o] { return (double) o.seed; },
                                        [&o, changed] (double v) { o.seed = (uint32_t) v; changed(); }));
            break;
        }

        case Kind::strum:
        {
            auto& o = s.strum;
            beats ("Time between notes (beats)", o.time, 0.01, 1.0, 0.01);
            fields.add (new ChoiceProp ("Direction", { "Upwards (lowest note first)", "Downwards (highest note first)" },
                                        [&o] { return o.upwards ? 0 : 1; },
                                        [&o, changed] (int v) { o.upwards = v == 0; changed(); }));
            percent ("Velocity fall (%)", o.velocityFall);
            toggle ("Keep note ends", o.keepEnds);
            break;
        }

        case Kind::flam:
            beats ("Time before the note (beats)", s.flam.time, 0.01, 1.0, 0.01);
            percent ("Velocity (%)", s.flam.velocity, 5.0);
            count ("Grace notes", s.flam.count, 1, 4);
            break;

        case Kind::legato:
            beats ("Overlap (beats, negative for a gap)", s.legato.overlap, -1.0, 1.0, 0.01);
            break;

        case Kind::articulate:
            percent ("Length (%)", s.articulate.lengthFactor, 10.0, 200.0);
            beats ("Minimum length (beats)", s.articulate.minimumLength, 0.01, 1.0, 0.01);
            break;

        case Kind::limit:
        {
            auto& o = s.limit;
            count ("Lowest key", o.lowKey, 0, 127);
            count ("Highest key", o.highKey, 0, 127);
            fields.add (new InfoProp ("Range", [&o] { return keyName (o.lowKey) + " to " + keyName (o.highKey); }));
            break;
        }

        case Kind::flip:
            toggle ("Vertical (mirror pitches)", s.flip.vertical);
            toggle ("Horizontal (reverse time)", s.flip.horizontal);
            break;

        case Kind::randomize:
        {
            auto& o = s.randomize;
            fields.add (new NumberProp ("Seed", 1.0, 999.0, 1.0, [&o] { return (double) o.seed; },
                                        [&o, changed] (double v) { o.seed = (uint32_t) v; changed(); }));
            percent ("Velocity (%)", o.strike);
            percent ("Release velocity (%)", o.lift);
            percent ("Pressure (%)", o.pressure);
            percent ("Timbre (%)", o.timbre);
            count ("Pitch (semitones)", o.pitch, 0, 24);
            percent ("Pitch chance (%)", o.pitchChance);
            percent ("Length (%)", o.length);
            beats ("Start (beats)", o.start, 0.0, 1.0, 0.01);
            break;
        }

        case Kind::scaleLevels:
        {
            auto& o = s.scaleLevels;
            factor ("Velocity x", o.strikeMultiplier, 0.0, 2.0);
            factor ("Velocity offset", o.strikeOffset, -1.0, 1.0);
            factor ("Release velocity x", o.liftMultiplier, 0.0, 2.0);
            factor ("Release velocity offset", o.liftOffset, -1.0, 1.0);
            factor ("Pressure x", o.pressureMultiplier, 0.0, 2.0);
            factor ("Timbre x (around the middle)", o.timbreMultiplier, 0.0, 2.0);
            break;
        }
    }

    panel.addProperties (fields);
}

void NoteToolDialog::preview()
{
    if (document == nullptr || finished)
        return;

    // Always from the notes as they were when the dialog opened, so a change in a parameter is
    // never applied on top of the last preview.
    auto& clip = document->editClip();
    clip = original;
    const auto made = run (kind, clip, targets, settings(), grid);
    clip.sortNotes();
    document->selection = made;
    document->changed();
    panel.refreshAll();
}

void NoteToolDialog::finish (bool keep)
{
    if (finished)
        return;

    finished = true;

    if (document != nullptr)
    {
        if (! keep)
        {
            document->editClip() = original;
            document->selection = originalSelection;
            document->changed();
        }

        document->endEdit();    // one undo step when kept; none when the clip is as it was
    }

    if (onFinished != nullptr)
        onFinished();
}

void NoteToolDialog::accept()
{
    finish (true);
}

void NoteToolDialog::cancel()
{
    finish (false);
}

void NoteToolDialog::detach()
{
    finish (false);
    document = nullptr;
}

//==============================================================================
void NoteToolDialog::paint (juce::Graphics& g)
{
    g.fillAll (Theme::panel);
}

void NoteToolDialog::resized()
{
    auto area = getLocalBounds().reduced (12, 10);
    title.setBounds (area.removeFromTop (24));
    hint.setBounds (area.removeFromTop (36));
    auto buttons = area.removeFromBottom (30);
    area.removeFromBottom (12);
    panel.setBounds (area);
    acceptButton.setBounds (buttons.removeFromRight (90));
    buttons.removeFromRight (8);
    cancelButton.setBounds (buttons.removeFromRight (90));
}

void NoteToolDialog::parentHierarchyChanged()
{
    if (isShowing())
        grabKeyboardFocus();
}

bool NoteToolDialog::keyPressed (const juce::KeyPress& key)
{
    if (key == juce::KeyPress::returnKey)
    {
        accept();
        return true;
    }

    if (key == juce::KeyPress::escapeKey)
    {
        cancel();
        return true;
    }

    return false;
}

} // namespace mperoll
