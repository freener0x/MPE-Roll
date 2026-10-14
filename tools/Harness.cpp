// Developer harness for MPE Roll (built with -DMPEROLL_BUILD_DEV_TOOLS=ON).
//
//   MpeRollHarness snapshot <out.png> [width height]   renders the editor with a demo clip
//   MpeRollHarness settings <out.png>                   renders the MPE settings panel
//   MpeRollHarness render <synth.vst3> <out.wav>        plays a test clip through a hosted synth and
//                                                       checks that per-note glides reach it
//   MpeRollHarness selftest                             live input, recording, state, note trigger, undo

#include "plugin/PluginEditor.h"
#include "plugin/PluginProcessor.h"
#include "plugin/ui/SettingsPanel.h"

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_audio_utils/juce_audio_utils.h>

#include <cstdio>
#include <map>

namespace
{
using namespace mperoll;

MpeNote note (int key, double start, double length, float strike)
{
    MpeNote n;
    n.key = key;
    n.start = start;
    n.length = length;
    n.strike = strike;
    return n;
}

MpeClip makeDemoClip()
{
    MpeClip clip;
    clip.name = "Demo";
    clip.length = 16.0;

    // A chord whose top note scoops up and whose root swells.
    auto root = note (55, 0.0, 3.5, 0.75f);
    root.lanes.pressure = { { 0.0, 0.1f }, { 1.5, 0.9f }, { 3.5, 0.3f } };
    root.lanes.timbre = { { 0.0, 0.2f }, { 3.5, 0.8f } };
    clip.addNote (root);

    clip.addNote (note (59, 0.0, 3.5, 0.6f));

    auto top = note (62, 0.0, 3.5, 0.9f);
    top.lanes.pitch = { { 0.0, -2.0f }, { 0.6, 0.0f }, { 2.6, 0.0f }, { 3.5, 2.0f } };
    top.lanes.pressure = { { 0.0, 0.3f }, { 1.0, 0.7f }, { 3.5, 0.5f } };
    clip.addNote (top);

    // A melody with vibrato and a long glide.
    auto lead = note (67, 4.0, 2.0, 0.85f);

    for (int i = 0; i <= 200; ++i)
    {
        const auto t = i * 0.01;
        lead.lanes.pitch.append (t, t < 0.5 ? 0.0f : 0.35f * (float) std::sin ((t - 0.5) * 30.0) * (float) juce::jmin (1.0, (t - 0.5) * 2.0));
    }

    lead.lanes.pressure = { { 0.0, 0.2f }, { 1.0, 1.0f }, { 2.0, 0.4f } };
    clip.addNote (lead);

    auto glide = note (69, 6.0, 2.0, 0.7f);
    glide.lanes.pitch = { { 0.0, 0.0f }, { 0.5, 0.0f }, { 1.5, 5.0f }, { 2.0, 5.0f } };
    glide.lanes.timbre = { { 0.0, 0.5f }, { 2.0, 1.0f } };
    clip.addNote (glide);

    for (int i = 0; i < 4; ++i)
    {
        auto n = note (60 + (i % 2) * 3, 8.0 + i, 0.75, 0.5f + 0.1f * (float) i);
        n.lanes.pressure = { { 0.0, 0.0f }, { 0.3, 0.6f + 0.1f * (float) i }, { 0.75, 0.0f } };
        clip.addNote (n);
    }

    auto fall = note (72, 12.0, 3.0, 1.0f);
    fall.lanes.pitch = { { 0.0, 0.0f }, { 2.0, 0.0f }, { 3.0, -7.0f } };
    clip.addNote (fall);

    clip.zone.get (LaneId::cc (64)) = { { 0.0, 1.0f }, { 3.9, 1.0f }, { 3.95, 0.0f } };
    return clip;
}

bool writePng (const juce::Image& image, const juce::File& file)
{
    file.deleteFile();
    juce::FileOutputStream out (file);
    juce::PNGImageFormat png;
    return out.openedOk() && png.writeImageToStream (image, out);
}

int snapshot (const juce::File& target, int width, int height)
{
    MpeRollProcessor processor;
    auto& document = processor.getDocument();
    document.replaceClip (0, makeDemoClip(), "Demo");

    for (const auto& n : document.getEditClip().notes)
        if (n.key == 62 || n.key == 67)
            document.selection.insert (n.id);

    std::unique_ptr<juce::AudioProcessorEditor> editor (processor.createEditorAndMakeActive());
    editor->setSize (width, height);
    juce::MessageManager::getInstance()->runDispatchLoopUntil (400);

    const auto ok = writePng (editor->createComponentSnapshot (editor->getLocalBounds(), true, 1.0f), target);
    processor.editorBeingDeleted (editor.get());
    editor.reset();
    std::printf ("%s %s\n", ok ? "Wrote" : "Failed to write", target.getFullPathName().toRawUTF8());
    return ok ? 0 : 1;
}

int settingsSnapshot (const juce::File& target)
{
    MpeRollProcessor processor;
    MpeLookAndFeel lookAndFeel;
    juce::LookAndFeel::setDefaultLookAndFeel (&lookAndFeel);

    {
        SettingsPanel panel (processor);
        panel.setSize (460, 980);
        juce::MessageManager::getInstance()->runDispatchLoopUntil (200);
        writePng (panel.createComponentSnapshot (panel.getLocalBounds(), true, 1.0f), target);
    }

    juce::LookAndFeel::setDefaultLookAndFeel (nullptr);
    std::printf ("Wrote %s\n", target.getFullPathName().toRawUTF8());
    return 0;
}

//==============================================================================
struct FakePlayHead : juce::AudioPlayHead
{
    double ppq = 0.0;
    double bpm = 120.0;
    bool playing = true;

    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo info;
        info.setBpm (bpm);
        info.setPpqPosition (ppq);
        info.setIsPlaying (playing);
        info.setTimeInSamples ((juce::int64) (ppq / bpm * 60.0 * 48000.0));
        return info;
    }
};

double goertzel (const float* samples, int numSamples, double frequency, double sampleRate)
{
    const auto w = juce::MathConstants<double>::twoPi * frequency / sampleRate;
    const auto coefficient = 2.0 * std::cos (w);
    double s1 = 0.0, s2 = 0.0;

    for (int i = 0; i < numSamples; ++i)
    {
        // Hann window to keep neighbouring notes from leaking into each other.
        const auto window = 0.5 - 0.5 * std::cos (juce::MathConstants<double>::twoPi * i / (numSamples - 1));
        const auto s = samples[i] * window + coefficient * s1 - s2;
        s2 = s1;
        s1 = s;
    }

    return std::sqrt (s1 * s1 + s2 * s2 - coefficient * s1 * s2) / numSamples;
}

int render (const juce::File& synthFile, const juce::File& target)
{
    constexpr double sampleRate = 48000.0;
    constexpr int blockSize = 512;

    MpeRollProcessor processor;
    FakePlayHead playHead;
    processor.setPlayHead (&playHead);
    processor.setRateAndBufferSizeDetails (sampleRate, blockSize);
    processor.prepareToPlay (sampleRate, blockSize);

    bool done = false;
    juce::String error;
    processor.getSynth().loadFromFile (synthFile, [&] (const juce::String& e) { error = e; done = true; });

    for (int i = 0; i < 200 && ! done; ++i)
        juce::MessageManager::getInstance()->runDispatchLoopUntil (50);

    if (! done || error.isNotEmpty())
    {
        std::printf ("Couldn't load %s: %s\n", synthFile.getFullPathName().toRawUTF8(), error.toRawUTF8());
        return 1;
    }

    std::printf ("Hosting: %s\n", processor.getSynth().getName().toRawUTF8());

    // C4 glides up an octave between beats 0.5 and 1.5 while E4 holds still.
    MpeClip clip;
    clip.length = 4.0;
    auto glide = note (60, 0.0, 2.0, 0.8f);
    glide.lanes.pitch = { { 0.0, 0.0f }, { 0.5, 0.0f }, { 1.5, 12.0f } };
    clip.addNote (glide);
    clip.addNote (note (64, 0.0, 2.0, 0.8f));
    processor.getDocument().replaceClip (0, clip, "Test");

    const auto totalSamples = (int) (2.5 / 2.0 * sampleRate);
    juce::AudioBuffer<float> output (2, totalSamples);
    output.clear();

    std::map<int, int> noteOnChannels, bendsPerChannel;

    for (int pos = 0; pos < totalSamples; pos += blockSize)
    {
        const auto n = std::min (blockSize, totalSamples - pos);
        juce::AudioBuffer<float> block (2, n);
        juce::MidiBuffer midi;
        playHead.ppq = pos / sampleRate * 2.0;
        processor.processBlock (block, midi);

        for (int ch = 0; ch < 2; ++ch)
            output.copyFrom (ch, pos, block, ch, 0, n);

        for (const auto m : midi)
        {
            const auto msg = m.getMessage();

            if (msg.isNoteOn())
                noteOnChannels[msg.getNoteNumber()] = msg.getChannel();

            if (msg.isPitchWheel())
                bendsPerChannel[msg.getChannel()]++;
        }
    }

    {
        target.deleteFile();
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::OutputStream> stream (target.createOutputStream());

        if (auto writer = wav.createWriterFor (stream, juce::AudioFormatWriterOptions{}.withSampleRate (sampleRate)
                                                                                     .withNumChannels (2)
                                                                                     .withBitsPerSample (24)))
            writer->writeFromAudioSampleBuffer (output, 0, totalSamples);
    }

    const auto c4 = juce::MidiMessage::getMidiNoteInHertz (60);
    const auto e4 = juce::MidiMessage::getMidiNoteInHertz (64);
    const auto c5 = juce::MidiMessage::getMidiNoteInHertz (72);
    constexpr int window = 4096;

    auto measure = [&] (double beat, double frequency)
    {
        const auto centre = (int) (beat / 2.0 * sampleRate);
        return goertzel (output.getReadPointer (0, centre - window / 2), window, frequency, sampleRate);
    };

    const auto earlyC4 = measure (0.25, c4), earlyE4 = measure (0.25, e4), earlyC5 = measure (0.25, c5);
    const auto lateC4 = measure (1.85, c4), lateE4 = measure (1.85, e4), lateC5 = measure (1.85, c5);

    std::printf ("Note-on channels: C4 -> %d, E4 -> %d\n", noteOnChannels[60], noteOnChannels[64]);
    std::printf ("Pitch bends: C4's channel %d, E4's channel %d\n",
                 bendsPerChannel[noteOnChannels[60]], bendsPerChannel[noteOnChannels[64]]);
    std::printf ("Beat 0.25:  C4 %.4f  E4 %.4f  C5 %.4f\n", earlyC4, earlyE4, earlyC5);
    std::printf ("Beat 1.85:  C4 %.4f  E4 %.4f  C5 %.4f\n", lateC4, lateE4, lateC5);

    const bool separateChannels = noteOnChannels[60] != noteOnChannels[64];
    const bool startsInTune = earlyC4 > 10.0 * earlyC5 && earlyE4 > 0.01;
    const bool glidedUp = lateC5 > 10.0 * lateC4;
    const bool otherNoteHeld = lateE4 > 0.5 * earlyE4;
    const bool ok = separateChannels && startsInTune && glidedUp && otherNoteHeld;

    std::printf ("Separate channels: %s, starts in tune: %s, C4 glided to C5: %s, E4 unaffected: %s\n",
                 separateChannels ? "yes" : "NO", startsInTune ? "yes" : "NO",
                 glidedUp ? "yes" : "NO", otherNoteHeld ? "yes" : "NO");
    std::printf ("%s (wrote %s)\n", ok ? "PASS" : "FAIL", target.getFullPathName().toRawUTF8());

    processor.releaseResources();
    return ok ? 0 : 1;
}

//==============================================================================
struct Check
{
    int failures = 0;

    void operator() (bool ok, const char* what)
    {
        std::printf ("  [%s] %s\n", ok ? "ok" : "FAIL", what);
        failures += ok ? 0 : 1;
    }
};

std::vector<juce::MidiMessage> runBlock (MpeRollProcessor& processor, FakePlayHead& playHead, double& ppq,
                                         std::initializer_list<std::pair<int, juce::MidiMessage>> input = {})
{
    constexpr int blockSize = 512;
    juce::AudioBuffer<float> audio (2, blockSize);
    juce::MidiBuffer midi;

    for (const auto& [pos, message] : input)
        midi.addEvent (message, pos);

    playHead.ppq = ppq;
    processor.processBlock (audio, midi);

    if (playHead.playing)
        ppq += blockSize / 48000.0 * playHead.bpm / 60.0;

    std::vector<juce::MidiMessage> out;

    for (const auto m : midi)
        out.push_back (m.getMessage());

    return out;
}

int selfTest()
{
    Check check;
    FakePlayHead playHead;
    double ppq = 0.0;

    MpeRollProcessor processor;
    processor.setPlayHead (&playHead);
    processor.setRateAndBufferSizeDetails (48000.0, 512);
    processor.prepareToPlay (48000.0, 512);
    auto& document = processor.getDocument();

    std::printf ("Live input passes through as MPE\n");
    {
        playHead.playing = false;
        runBlock (processor, playHead, ppq);   // first block sends the MPE configuration
        const auto out = runBlock (processor, playHead, ppq,
                                   { { 0, juce::MidiMessage::pitchWheel (2, 8192 + 2048) },
                                     { 10, juce::MidiMessage::noteOn (2, 60, (juce::uint8) 100) } });
        int noteChannel = 0, bendBefore = -1;

        for (const auto& m : out)
        {
            if (m.isPitchWheel() && noteChannel == 0)
                bendBefore = m.getPitchWheelValue();

            if (m.isNoteOn() && m.getNoteNumber() == 60)
                noteChannel = m.getChannel();
        }

        check (noteChannel >= 2, "note arrives on a member channel");
        check (bendBefore == 8192 + 2048, "its pitch bend is sent before the Note On");
        runBlock (processor, playHead, ppq, { { 0, juce::MidiMessage::noteOff (2, 60, (juce::uint8) 64) } });
    }

    std::printf ("Recording a take from an MPE controller\n");
    {
        playHead.playing = true;
        ppq = 0.0;
        processor.setRecordArmed (true);

        runBlock (processor, playHead, ppq);
        runBlock (processor, playHead, ppq, { { 0, juce::MidiMessage::noteOn (3, 64, (juce::uint8) 90) } });

        for (int i = 1; i <= 20; ++i)
            runBlock (processor, playHead, ppq, { { 0, juce::MidiMessage::pitchWheel (3, MpeEncoder::semitonesToPitchBend ((float) i * 0.1f, 48.0f)) },
                                                  { 0, juce::MidiMessage::channelPressureChange (3, i * 5) } });

        runBlock (processor, playHead, ppq, { { 0, juce::MidiMessage::noteOff (3, 64, (juce::uint8) 40) } });
        playHead.playing = false;
        runBlock (processor, playHead, ppq);
        processor.setRecordArmed (false);
        juce::MessageManager::getInstance()->runDispatchLoopUntil (250);

        const auto& clip = document.getClip (0);
        const MpeNote* recorded = nullptr;

        for (const auto& n : clip.notes)
            if (n.key == 64)
                recorded = &n;

        check (recorded != nullptr, "the recorded note is in the clip");

        if (recorded != nullptr)
        {
            const auto blockBeats = 512.0 / 48000.0 * 2.0;
            check (std::abs (recorded->start - blockBeats) < 1.0e-6, "it starts where it was played");
            check (std::abs (recorded->lanes.pitch.valueAt (recorded->length, 0.0f) - 2.0f) < 0.05f, "its glide was captured");
            check (! recorded->lanes.pressure.isEmpty(), "its pressure was captured");
            check (std::abs (recorded->lift - 40.0f / 127.0f) < 1.0e-4f, "its release velocity was captured");
        }

        check (document.getUndoManager().canUndo(), "the take can be undone");
    }

    std::printf ("Saving and restoring the plugin state\n");
    {
        auto settings = document.getSettings();
        settings.output.memberBendRange = 24.0f;
        settings.output.highResVelocity = true;
        document.setSettings (settings);

        juce::MemoryBlock state;
        processor.getStateInformation (state);

        MpeRollProcessor restored;
        restored.setStateInformation (state.getData(), (int) state.getSize());
        check (restored.getDocument().getClip (0) == document.getClip (0), "clips are restored");
        check (restored.getDocument().getSettings() == document.getSettings(), "settings are restored");
    }

    std::printf ("A synth that isn't installed is remembered\n");
    {
        juce::XmlElement state ("MpeRoll");
        auto* synthXml = state.createNewChildElement ("HostedSynth");
        juce::PluginDescription missing;
        missing.name = "Not Installed Synth";
        missing.pluginFormatName = "VST3";
        missing.fileOrIdentifier = "/nowhere/Not Installed Synth.vst3";
        synthXml->addChildElement (missing.createXml().release());
        synthXml->createNewChildElement ("State")->addTextElement ("AAAA");

        juce::MemoryBlock data;
        juce::AudioProcessor::copyXmlToBinary (state, data);

        MpeRollProcessor other;
        other.setStateInformation (data.getData(), (int) data.getSize());
        juce::MessageManager::getInstance()->runDispatchLoopUntil (300);

        juce::MemoryBlock saved;
        other.getStateInformation (saved);
        const auto savedXml = juce::AudioProcessor::getXmlFromBinary (saved.getData(), (int) saved.getSize());
        const auto* kept = savedXml != nullptr ? savedXml->getChildByName ("HostedSynth") : nullptr;
        check (kept != nullptr && kept->getChildByName ("PLUGIN") != nullptr
                   && kept->getChildByName ("PLUGIN")->getStringAttribute ("name") == missing.name,
               "saving keeps the missing synth in the project");
        check (other.getSynth().getMissingSynthName() == missing.name, "the UI can say which synth is missing");
    }

    std::printf ("Note-trigger mode launches clip slots\n");
    {
        auto settings = document.getSettings();
        settings.playback.mode = PlaybackSettings::Mode::noteTrigger;
        settings.playback.triggerBaseKey = 24;
        document.setSettings (settings);

        MpeClip clip;
        clip.addNote ([] { MpeNote n; n.key = 77; n.start = 0.0; n.length = 1.0; return n; }());
        document.replaceClip (3, clip, "Slot 4");

        playHead.playing = false;
        const auto out = runBlock (processor, playHead, ppq, { { 100, juce::MidiMessage::noteOn (1, 27, (juce::uint8) 100) } });
        bool clipNote = false, triggerLeaked = false;

        for (const auto& m : out)
        {
            clipNote |= m.isNoteOn() && m.getNoteNumber() == 77;
            triggerLeaked |= m.isNoteOn() && m.getNoteNumber() == 27;
        }

        check (clipNote, "key 27 starts slot 4's note");
        check (! triggerLeaked, "the trigger key itself isn't played");
    }

    std::printf ("Undo\n");
    {
        const auto before = document.getClip (3);
        document.setEditSlot (3);
        document.perform ("Clear", [] (MpeClip& c) { c.notes.clear(); });
        check (document.getClip (3).notes.empty(), "edit applied");
        document.getUndoManager().undo();
        check (document.getClip (3) == before, "undo restores the clip");
    }

    processor.releaseResources();
    std::printf ("%s: %d failure(s)\n", check.failures == 0 ? "PASS" : "FAIL", check.failures);
    return check.failures == 0 ? 0 : 1;
}

//==============================================================================
template <typename ComponentType>
ComponentType* findChild (juce::Component& parent)
{
    for (auto* child : parent.getChildren())
        if (auto* match = dynamic_cast<ComponentType*> (child))
            return match;

    return nullptr;
}

/** Drives a component with synthetic mouse gestures. */
void gesture (juce::Component& target, juce::Point<float> from, juce::Point<float> to,
              juce::ModifierKeys mods = juce::ModifierKeys (juce::ModifierKeys::leftButtonModifier), int steps = 10)
{
    const auto now = juce::Time::getCurrentTime();
    auto event = [&] (juce::Point<float> pos, bool dragged)
    {
        return juce::MouseEvent (juce::Desktop::getInstance().getMainMouseSource(), pos, mods, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                                 &target, &target, now, from, now, 1, dragged);
    };

    target.mouseDown (event (from, false));

    for (int i = 1; i <= steps; ++i)
        target.mouseDrag (event (from + (to - from) * ((float) i / (float) steps), from != to));

    target.mouseUp (event (to, from != to));
}

int uiTest()
{
    Check check;
    MpeRollProcessor processor;
    processor.setRateAndBufferSizeDetails (48000.0, 512);
    processor.prepareToPlay (48000.0, 512);
    auto& document = processor.getDocument();

    std::unique_ptr<juce::AudioProcessorEditor> editor (processor.createEditorAndMakeActive());
    editor->setSize (1200, 760);
    juce::MessageManager::getInstance()->runDispatchLoopUntil (100);

    auto* grid = findChild<NoteGrid> (*editor);
    auto* lane = findChild<ExpressionLane> (*editor);

    if (grid == nullptr || lane == nullptr)
    {
        std::printf ("Couldn't find the grid and lane\n");
        return 1;
    }

    const auto& view = grid->getView();
    auto at = [&] (double beat, float pitch) { return juce::Point<float> ((float) view.beatToX (beat), view.pitchToY (pitch)); };
    auto clip = [&]() -> const MpeClip& { return document.getEditClip(); };
    const juce::ModifierKeys right (juce::ModifierKeys::rightButtonModifier);

    std::printf ("Grid editing\n");
    gesture (*grid, at (1.03, 60.0f), at (1.03, 60.0f));
    check (clip().notes.size() == 1 && clip().notes[0].key == 60 && std::abs (clip().notes[0].start - 1.0) < 1.0e-9,
           "click adds a note on the snapped grid");

    gesture (*grid, at (1.5, 60.0f), at (3.5, 62.0f));
    check (clip().notes.size() == 1 && clip().notes[0].key == 62 && std::abs (clip().notes[0].start - 3.0) < 1.0e-9,
           "dragging moves it in time and pitch");

    gesture (*grid, at (3.97, 62.0f), at (4.97, 62.0f));
    check (std::abs (clip().notes[0].length - 2.0) < 1.0e-9, "dragging the right edge resizes it");

    grid->keyPressed (juce::KeyPress ('g'));
    gesture (*grid, at (3.0, 62.0f), at (5.0, 65.0f), juce::ModifierKeys (juce::ModifierKeys::leftButtonModifier), 30);
    const auto& glide = clip().notes[0].lanes.pitch;
    check (! glide.isEmpty() && std::abs (glide.valueAt (2.0, 0.0f) - 3.0f) < 0.2f, "the glide tool draws pitch onto the note");
    grid->keyPressed (juce::KeyPress ('e'));

    std::printf ("Expression lane\n");
    {
        const auto plotTop = (float) ExpressionLane::headerHeight + 4.0f;
        const auto plotBottom = (float) lane->getHeight() - 4.0f;
        auto laneAt = [&] (double beat, float value)
        {
            return juce::Point<float> ((float) ViewState::keysWidth + (float) view.beatToX (beat),
                                       plotBottom - value * (plotBottom - plotTop));
        };

        gesture (*lane, laneAt (3.0, 0.2f), laneAt (5.0, 0.8f), juce::ModifierKeys (juce::ModifierKeys::leftButtonModifier), 40);
        const auto& pressure = clip().notes[0].lanes.pressure;
        check (! pressure.isEmpty(), "drawing in the pressure lane adds a curve");
        check (std::abs (pressure.valueAt (0.0, 0.0f) - 0.2f) < 0.03f && std::abs (pressure.valueAt (2.0, 0.0f) - 0.8f) < 0.03f,
               "the curve follows the pen");

        gesture (*lane, laneAt (3.5, 0.5f), laneAt (4.5, 0.5f), right);
        const auto erased = std::none_of (pressure.getPoints().begin(), pressure.getPoints().end(),
                                          [] (const CurvePoint& p) { return p.beat > 0.55 && p.beat < 1.45; });
        check (erased && ! pressure.isEmpty(), "right-dragging erases just the points under the pen");
    }

    std::printf ("Selection, clipboard and undo\n");
    {
        grid->keyPressed (juce::KeyPress ('a', juce::ModifierKeys::commandModifier, 0));
        grid->keyPressed (juce::KeyPress ('d', juce::ModifierKeys::commandModifier, 0));
        check (clip().notes.size() == 2 && std::abs (clip().notes[1].start - 5.0) < 1.0e-9, "Ctrl+D duplicates after the selection");

        grid->keyPressed (juce::KeyPress (juce::KeyPress::upKey));
        check (clip().notes[1].key == 63, "Up transposes the selection");

        gesture (*grid, at (3.5, 62.0f), at (3.5, 62.0f), right);
        check (clip().notes.size() == 1, "right-click deletes a note");

        document.getUndoManager().undo();
        check (clip().notes.size() == 2, "undo brings it back");
    }

    processor.editorBeingDeleted (editor.get());
    editor.reset();
    std::printf ("%s: %d failure(s)\n", check.failures == 0 ? "PASS" : "FAIL", check.failures);
    return check.failures == 0 ? 0 : 1;
}

} // namespace

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    const juce::StringArray args (argv + 1, argc - 1);
    const auto cwd = juce::File::getCurrentWorkingDirectory();

    if (args.size() >= 2 && args[0] == "snapshot")
        return snapshot (cwd.getChildFile (args[1]),
                         args.size() >= 4 ? args[2].getIntValue() : 1200,
                         args.size() >= 4 ? args[3].getIntValue() : 760);

    if (args.size() >= 2 && args[0] == "settings")
        return settingsSnapshot (cwd.getChildFile (args[1]));

    if (args.size() >= 3 && args[0] == "render")
        return render (cwd.getChildFile (args[1]), cwd.getChildFile (args[2]));

    if (args.size() >= 1 && args[0] == "selftest")
        return selfTest();

    if (args.size() >= 1 && args[0] == "uitest")
        return uiTest();

    std::printf ("Usage:\n  MpeRollHarness snapshot <out.png> [width height]\n"
                 "  MpeRollHarness settings <out.png>\n"
                 "  MpeRollHarness render <synth.vst3> <out.wav>\n"
                 "  MpeRollHarness selftest\n");
    return 2;
}
