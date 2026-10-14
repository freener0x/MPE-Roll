#include "PianoKeys.h"
#include "MpeLookAndFeel.h"

namespace mperoll
{

PianoKeys::PianoKeys (MpeRollProcessor& p, ViewState& v) : processor (p), view (v)
{
    view.addListener (this);
}

PianoKeys::~PianoKeys()
{
    release();
    view.removeListener (this);
}

void PianoKeys::paint (juce::Graphics& g)
{
    const auto w = (float) getWidth();
    const auto h = (float) getHeight();
    g.fillAll (Theme::panel);

    const auto topKey = juce::jlimit (0, 127, view.yToKey (0.0f));
    const auto bottomKey = juce::jlimit (0, 127, view.yToKey (h));

    for (int key = bottomKey; key <= topKey; ++key)
    {
        const auto y = view.keyToY (key);
        const bool black = juce::MidiMessage::isMidiNoteBlack (key);
        const auto keyArea = juce::Rectangle<float> (0.0f, y, black ? w * 0.62f : w, view.rowHeight);

        auto colour = black ? juce::Colour (0xff23252b) : juce::Colour (0xffd9dce2);

        if (key == heldKey)
            colour = Theme::note;

        g.setColour (colour);
        g.fillRect (keyArea.reduced (0.0f, 0.5f));

        if (! black && key % 12 == 0)
        {
            g.setColour (juce::Colour (0xff4a4f59));
            g.setFont (juce::FontOptions (juce::jmin (11.0f, view.rowHeight - 1.0f)));
            g.drawText (juce::MidiMessage::getMidiNoteName (key, true, true, 5),
                        keyArea.withTrimmedRight (4.0f), juce::Justification::centredRight, false);
        }
    }

    g.setColour (Theme::outline);
    g.fillRect (w - 1.0f, 0.0f, 1.0f, h);
}

void PianoKeys::play (const juce::MouseEvent& e)
{
    const auto key = juce::jlimit (0, 127, view.yToKey (e.position.y));

    if (key == heldKey)
        return;

    release();
    const auto velocity = juce::jlimit (0.2f, 1.0f, e.position.x / (float) juce::jmax (1, getWidth()));
    processor.auditionNote (key, velocity, true);
    heldKey = key;
    repaint();
}

void PianoKeys::release()
{
    if (heldKey >= 0)
    {
        processor.auditionNote (heldKey, 0.0f, false);
        heldKey = -1;
        repaint();
    }
}

void PianoKeys::mouseDown (const juce::MouseEvent& e)  { play (e); }
void PianoKeys::mouseDrag (const juce::MouseEvent& e)  { play (e); }
void PianoKeys::mouseUp (const juce::MouseEvent&)      { release(); }

void PianoKeys::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    if (e.mods.isAltDown() || e.mods.isCommandDown())
        view.zoomVertically (wheel.deltaY > 0 ? 1.1f : 1.0f / 1.1f, e.position.y, (float) getHeight());
    else
        view.scrollVertically (wheel.deltaY * 30.0f, (float) getHeight());
}

} // namespace mperoll
