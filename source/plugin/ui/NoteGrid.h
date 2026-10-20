#pragma once

#include "Skins.h"
#include "ViewState.h"
#include "plugin/PluginProcessor.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace mperoll
{

/** The piano roll grid: notes plus their glides, pressure and timbre drawn on top.

    Mouse (FL Studio conventions): click empty space to add a note and drag to place it, drag a
    note to move it, drag its right edge to resize, right-click/drag to delete, Ctrl-drag to
    select, Shift-drag to duplicate, Alt to bypass snap. With the Paint tool, drag to lay down notes
    one after another, each as long as the last note placed, from a snapped start (right-drag deletes,
    as with the Edit tool). With the Glide tool, drag from a point on a
    note to the pitch the glide should reach: with glide snap on it lands on a whole semitone and the
    grid, in a straight line from where the note's pitch was; hold Alt to draw freehand instead.
    Right-drag erases a glide, double-click clears it.
*/
class NoteGrid : public juce::Component,
                 private ViewState::Listener,
                 private juce::ChangeListener
{
public:
    NoteGrid (MpeRollProcessor&, ViewState&);
    ~NoteGrid() override;

    void paint (juce::Graphics&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    bool keyPressed (const juce::KeyPress&) override;

    void setPlayhead (double beat, bool visible);
    void refreshRecording()                     { repaint(); }
    const ViewState& getView() const noexcept   { return view; }

    void selectAll();
    void deleteSelection();
    void copySelection();
    void cutSelection();
    void paste();
    void duplicateSelection();
    void quantizeSelection();
    void transposeSelection (int semitones);
    void nudgeSelection (double beats);

private:
    enum class DragMode { none, move, resize, marquee, erase, paint, glide, glideErase };

    struct Original
    {
        uint32_t id = 0;
        double start = 0.0, length = 0.0;
        int key = 0;
    };

    MpeRollProcessor& processor;
    ClipDocument& document;
    ViewState& view;
    juce::SharedResourcePointer<SkinManager> skins;

    DragMode dragMode = DragMode::none;
    std::vector<Original> originals;
    uint32_t anchorId = 0;
    juce::Point<float> downPos;
    juce::Rectangle<float> marquee;
    std::set<uint32_t> marqueeBase;
    int auditionKey = -1;
    double playheadBeat = 0.0;
    bool playheadVisible = false;
    float lastVelocity = 0.8f;

    double paintOrigin = 0.0;                   // where the first painted note starts...
    double paintLength = 1.0;                   // ...and how far apart (and how long) the notes are
    long paintSlot = 0;                         // the last slot painted, counted in notes from the origin

    uint32_t glideNote = 0;
    bool glideStarted = false, glideSnapped = false;
    double glideLastTime = 0.0;
    float glideLastValue = 0.0f;
    ExpressionCurve glideOriginal;              // the note's glide before this drag
    double glideAnchorTime = 0.0;               // snapped: where the glide starts...
    float glideAnchorValue = 0.0f;              // ...and the pitch it starts from
    juce::String glideLabel;                    // snapped: the interval and note it lands on
    juce::Point<float> glideLabelPosition;

    static std::vector<MpeNote> clipboard;

    void viewChanged() override                                 { repaint(); }
    void changeListenerCallback (juce::ChangeBroadcaster*) override { repaint(); }

    juce::Rectangle<float> noteBounds (const MpeNote&) const;
    const MpeNote* noteAt (juce::Point<float> position) const;
    bool isNearEnd (const MpeNote&, juce::Point<float> position) const;
    void captureOriginals();
    void beginMove (bool duplicate);
    void dragMove (const juce::MouseEvent&);
    void dragResize (const juce::MouseEvent&);
    void eraseAt (juce::Point<float> position);
    void startPaint (juce::Point<float> position, bool bypassSnap);
    void paintTo (juce::Point<float> position, bool first);
    void startGlide (const MpeNote&, juce::Point<float> position, bool snapped);
    void drawGlide (juce::Point<float> position);
    void eraseGlide (juce::Point<float> position);
    void startAudition (int key, float velocity);
    void stopAudition();

    void paintBackground (juce::Graphics&, const MpeClip&) const;
    void paintNote (juce::Graphics&, const MpeNote&, bool selected, bool outsideLoop, juce::Colour base) const;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NoteGrid)
};

} // namespace mperoll
