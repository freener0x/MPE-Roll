#pragma once

#include "core/NoteTools.h"
#include "plugin/ClipDocument.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <functional>
#include <set>

namespace mperoll
{

/** The piano roll's Tools, as in FL Studio: which there are, their keys, and the dialog each opens.
    A dialog shows the tool's parameters and previews the result on the clip as they change (the
    synth plays it, too), until Accept keeps it as one undo step or Cancel puts the clip back. The
    "quick" versions apply a tool with its current settings at once. */
class NoteToolDialog : public juce::Component
{
public:
    enum class Kind { quantize, chop, glue, arpeggiate, strum, flam, legato, articulate, limit, flip, randomize, scaleLevels };
    static constexpr int numKinds = 12;

    struct Info
    {
        Kind kind;
        const char* name;
        const char* hint;           // what it does, in a line
        juce::KeyPress key;         // FL's key for the dialog, if it has one
        juce::KeyPress quickKey;    // FL's key for the quick version, if it has one
    };

    static const Info& info (Kind);
    static const std::array<Info, numKinds>& all();

    /** "Alt+U", "Ctrl+Q"; empty for an invalid key. */
    static juce::String describeKey (const juce::KeyPress&);
    static bool matches (const juce::KeyPress& wanted, const juce::KeyPress& pressed);

    /** What each tool was last used with (a quick key repeats it), starting from FL-like defaults.
        Grid choices are snap option indices (see Toolbar); -1 means the grid in use. */
    struct Settings
    {
        int quantizeGrid = -1, chopGrid = -1, arpeggioStep = -1;
        notetools::QuantizeOptions quantize { 0.25, 1.0f, true, true };   // FL's quick quantize lands ends too
        notetools::ChopOptions chop;
        notetools::GlueOptions glue;
        notetools::ArpeggiateOptions arpeggiate;
        notetools::StrumOptions strum;
        notetools::FlamOptions flam;
        notetools::LegatoOptions legato;
        notetools::ArticulateOptions articulate;
        notetools::LimitOptions limit;
        notetools::FlipOptions flip;
        notetools::RandomizeOptions randomize;
        notetools::ScaleLevelsOptions scaleLevels;
    };

    static Settings& settings();

    /** Runs a tool on the clip with the settings, on the selection or every note. Returns what it made. */
    static std::set<uint32_t> run (Kind, MpeClip&, const std::set<uint32_t>& selection, const Settings&, double gridBeats);

    /** Applies a tool with its current settings as one undo step, selecting what it made. */
    static void applyNow (Kind, ClipDocument&, double gridBeats);

    NoteToolDialog (Kind, ClipDocument&, double gridBeats);
    ~NoteToolDialog() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void parentHierarchyChanged() override;
    bool keyPressed (const juce::KeyPress&) override;

    void accept();
    void cancel();

    /** For the editor going before the call-out holding the dialog is deleted: withdraws the preview
        and cuts the dialog off from the document. */
    void detach();

    /** Called once, after Accept or Cancel, so the owner can close the call-out. */
    std::function<void()> onFinished;

private:
    Kind kind;
    ClipDocument* document;
    double grid;
    MpeClip original;
    std::set<uint32_t> originalSelection, targets;
    juce::Label title, hint;
    juce::PropertyPanel panel;
    juce::TextButton acceptButton { "Accept" }, cancelButton { "Cancel" };
    bool finished = false;

    void buildFields();
    void preview();
    void finish (bool keep);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NoteToolDialog)
};

} // namespace mperoll
