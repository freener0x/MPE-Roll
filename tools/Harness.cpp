// SPDX-FileCopyrightText: 2026 The MPE Roll contributors
// SPDX-License-Identifier: AGPL-3.0-only

// Developer harness for MPE Roll (built with -DMPEROLL_BUILD_DEV_TOOLS=ON).
//
//   MpeRollHarness snapshot <out.png> [width height]   renders the editor with a demo clip
//   MpeRollHarness settings <out.png>                   renders the MPE settings panel
//   MpeRollHarness skins <folder>                       renders the editor in every skin, and a sheet of them
//   MpeRollHarness shoot <state> <out.png> [scale] [synth]
//                                                       renders a state of the editor for the website, in the
//                                                       FL Studio skin, at a scale (2 by default): hero,
//                                                       lanes, vibrato, glide, tools, settings, copies, skins
//                                                       (settings and copies with the synth loaded, if given;
//                                                       vibrato-narrow, glide-narrow and skins-narrow for a
//                                                       phone's width)
//   MpeRollHarness demo <folder> [scale]                writes the frames of the website's demo film at a scale
//                                                       (2 by default), and its poster (tools/make_demo.sh
//                                                       makes the video)
//   MpeRollHarness export <synth> <folder>              exports a clip as WAV and MP3 and reads them back
//   MpeRollHarness render <synth.vst3> <out.wav>        plays a test clip through a hosted synth and
//                                                       checks that per-note glides reach it
//   MpeRollHarness selftest                             live input, recording, state, note trigger, undo
//   MpeRollHarness uitest                               simulated mouse and keyboard editing
//   MpeRollHarness params <synth.vst3>                  synth parameters exposed for host automation
//   MpeRollHarness copies <plain synth>                 one copy per note, through a synth without MPE
//   MpeRollHarness bendtweaks <plain synth>             a synth's parameter following the bend isn't an
//                                                       edit to the host
//   MpeRollHarness gatetest <plain synth>               the clip stopped by the host's All Notes Off
//                                                       (Follow FL, Note trigger), and sounding only
//                                                       while the host holds a note
//   MpeRollHarness vst3hostmidi <MPE Roll.vst3>         MPE Roll's own VST3 stopped by an All Notes Off
//                                                       sent the way a VST3 host sends controllers
//   MpeRollHarness crashtest <report.txt> [overflow]    Windows: crashes on purpose (or overflows the stack)
//                                                       to check the crash report
//   MpeRollHarness scantest <scanner> <synth> <crashing.clap> <hanging.clap>
//                                                       plugin files are looked at in the scanner process
//   MpeRollHarness loadfailtest <scanner> <synth.vst3> <VST2 synth>
//                                                       a load that fails says why
//   MpeRollHarness updatetest <tools/fake_releases_server.py>
//                                                       the update check, against a stand-in for GitHub
//                                                       run with Python on this computer

#include "core/FlScriptExporter.h"
#include "plugin/BugReport.h"
#include "plugin/BuildInfo.h"
#include "plugin/CopySafety.h"
#include "plugin/CrashReport.h"
#include "plugin/Links.h"
#include "plugin/PluginEditor.h"
#include "plugin/PluginLog.h"
#include "plugin/PluginProcessor.h"
#include "plugin/PluginScanner.h"
#include "plugin/UpdateChecker.h"
#include "plugin/ui/DonatePanel.h"
#include "plugin/ui/NoteToolDialog.h"
#include "plugin/ui/SettingsPanel.h"
#include "plugin/ui/WindowPlacement.h"
#include "plugin/ui/Toolbar.h"
#include "plugin/ui/WindowDrawing.h"

#if MPEROLL_HOST_CLAP
 #include "plugin/clap/ClapHosting.h"
#endif

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_audio_utils/juce_audio_utils.h>

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <thread>

#if JUCE_LINUX
 #include <dlfcn.h>
#endif

namespace
{
using namespace mperoll;

/** JUCE only catches X errors in apps with a JUCEApplication, and Xlib's own handler ends the process.
    Under Xvfb there's no window manager, so some of the window properties JUCE sets are refused. */
void ignoreXErrors()
{
   #if JUCE_LINUX
    using Handler = int (*) (void*, void*);
    using SetHandler = Handler (*) (Handler);

    if (auto* x11 = dlopen ("libX11.so.6", RTLD_LAZY))
        if (auto setHandler = reinterpret_cast<SetHandler> (dlsym (x11, "XSetErrorHandler")))
            setHandler ([] (void*, void*) { return 0; });
   #endif
}

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

    // A melody whose vibrato fades in, and a long glide.
    auto lead = note (67, 4.0, 2.0, 0.85f);
    lead.lanes.vibrato = { { 0.0, 0.0f }, { 0.5, 0.0f }, { 1.0, 0.35f } };
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

template <typename ComponentType>
ComponentType* findChild (juce::Component& parent)
{
    for (auto* child : parent.getChildren())
        if (auto* match = dynamic_cast<ComponentType*> (child))
            return match;

    return nullptr;
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

int settingsSnapshot (const juce::File& target, const juce::String& openSections)
{
    MpeRollProcessor processor;
    MpeLookAndFeel lookAndFeel;
    juce::LookAndFeel::setDefaultLookAndFeel (&lookAndFeel);
    juce::SharedResourcePointer<UpdateChecker> updates;     // an editor's, which the panel borrows

    {
        SettingsPanel panel (processor, updates.getObject());
        panel.setSize (460, 980);

        // Given section names (comma-separated), only those are open.
        if (auto* sections = findChild<juce::PropertyPanel> (panel); sections != nullptr && openSections.isNotEmpty())
        {
            const auto wanted = juce::StringArray::fromTokens (openSections, ",", "");
            const auto names = sections->getSectionNames();

            for (int i = 0; i < names.size(); ++i)
                sections->setSectionOpen (i, wanted.contains (names[i]));
        }

        juce::MessageManager::getInstance()->runDispatchLoopUntil (200);
        writePng (panel.createComponentSnapshot (panel.getLocalBounds(), true, 1.0f), target);
    }

    juce::LookAndFeel::setDefaultLookAndFeel (nullptr);
    std::printf ("Wrote %s\n", target.getFullPathName().toRawUTF8());
    return 0;
}

/** A test picture: a gradient with diagonal stripes, so it's plain to see whether and how strongly it's drawn. */
juce::Image testPicture()
{
    juce::Image image (juce::Image::RGB, 320, 200, true);
    juce::Graphics g (image);
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff2a6cc4), 0.0f, 0.0f, juce::Colour (0xffc4462a), 320.0f, 200.0f, false));
    g.fillAll();
    g.setColour (juce::Colours::white.withAlpha (0.5f));

    for (int x = -200; x < 320; x += 40)
        g.drawLine ((float) x, 200.0f, (float) x + 200.0f, 0.0f, 8.0f);

    return image;
}

/** Renders the editor in every skin, once more with a background picture, and a sheet of them all. */
int skinsSnapshots (const juce::File& folder)
{
    folder.createDirectory();
    juce::SharedResourcePointer<SkinManager> skins;
    const auto look = folder.getChildFile ("look.settings");
    look.deleteFile();
    skins->useSettingsFile (look);

    MpeRollProcessor processor;
    auto& document = processor.getDocument();
    document.replaceClip (0, makeDemoClip(), "Demo");

    for (const auto& n : document.getEditClip().notes)
        if (n.key == 62 || n.key == 67)
            document.selection.insert (n.id);

    std::unique_ptr<juce::AudioProcessorEditor> editor (processor.createEditorAndMakeActive());
    editor->setSize (1200, 760);
    auto& mm = *juce::MessageManager::getInstance();
    mm.runDispatchLoopUntil (300);

    const auto& presets = presetSkins();
    constexpr int thumbWidth = 600, thumbHeight = 380, columns = 2;
    const auto count = (int) presets.size() + 1;
    juce::Image sheet (juce::Image::RGB, thumbWidth * columns, thumbHeight * ((count + columns - 1) / columns), true);
    juce::Graphics sheetGraphics (sheet);
    sheetGraphics.setImageResamplingQuality (juce::Graphics::highResamplingQuality);

    for (int i = 0; i < count; ++i)
    {
        const auto withPicture = i == (int) presets.size();

        if (withPicture)
        {
            const auto pictureFile = folder.getChildFile ("picture.png");
            writePng (testPicture(), pictureFile);
            skins->setPreset (0);
            skins->setBackgroundFile (pictureFile);
            skins->setBackgroundStrength (0.4f);
        }
        else
        {
            skins->setPreset (i);
        }

        mm.runDispatchLoopUntil (100);
        const auto image = editor->createComponentSnapshot (editor->getLocalBounds(), true, 1.0f);
        const auto name = withPicture ? juce::String ("MPE Roll with a picture") : presets[(size_t) i].name;
        writePng (image, folder.getChildFile ("skin-" + juce::String (i + 1).paddedLeft ('0', 2) + "-"
                                              + name.toLowerCase().replaceCharacter (' ', '-') + ".png"));

        juce::Rectangle<int> cell ((i % columns) * thumbWidth, (i / columns) * thumbHeight, thumbWidth, thumbHeight);
        sheetGraphics.drawImage (image, cell.toFloat(), juce::RectanglePlacement::stretchToFit);
        const auto label = cell.removeFromBottom (26).removeFromLeft (230);
        sheetGraphics.setColour (juce::Colours::black.withAlpha (0.7f));
        sheetGraphics.fillRect (label);
        sheetGraphics.setColour (juce::Colours::white);
        sheetGraphics.setFont (juce::FontOptions (16.0f, juce::Font::bold));
        sheetGraphics.drawText (juce::String (i + 1) + ". " + name, label.reduced (8, 0), juce::Justification::centredLeft);
    }

    const auto sheetFile = folder.getChildFile ("skins.png");
    const auto ok = writePng (sheet, sheetFile);

    skins->clearBackground();
    skins->setPreset (0);
    skins->flush();
    processor.editorBeingDeleted (editor.get());
    editor.reset();
    std::printf ("%s %s\n", ok ? "Wrote" : "Failed to write", sheetFile.getFullPathName().toRawUTF8());
    return ok ? 0 : 1;
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

struct Check
{
    int failures = 0;

    void operator() (bool ok, const char* what)
    {
        std::printf ("  [%s] %s\n", ok ? "ok" : "FAIL", what);
        failures += ok ? 0 : 1;
    }

    void operator() (bool ok, const juce::String& what)     { (*this) (ok, what.toRawUTF8()); }
};

/** The average frequency of a sine between two samples, from its upward zero crossings. */
double sineFrequency (const juce::AudioBuffer<float>& audio, int from, int to, double sampleRate)
{
    const auto* data = audio.getReadPointer (0);
    double first = -1.0, last = -1.0;
    int cycles = -1;

    for (auto i = juce::jmax (1, from); i < juce::jmin (to, audio.getNumSamples()); ++i)
    {
        if (data[i - 1] < 0.0f && data[i] >= 0.0f)
        {
            last = i - 1 + data[i - 1] / (data[i - 1] - data[i]);
            first = first < 0.0 ? last : first;
            ++cycles;
        }
    }

    return cycles > 0 ? sampleRate * cycles / (last - first) : 0.0;
}

/** Exports a clip as WAV and MP3 through a copy of the synth, the way the File menu does, and reads both back. */
bool loadSynth (MpeRollProcessor&, const juce::File&);   // further down

/** Checks for the export fixes: a cancel while the file is written, the plugin going mid-export,
    and (CLAP) the synth copy being told it renders offline.
*/
void exportFixChecks (Check& check, const juce::File& synthFile, const juce::File& folder, double sampleRate)
{
    auto& mm = *juce::MessageManager::getInstance();

    std::printf ("Cancelling while the file is written\n");
    {
        // Three seconds of noise: long enough to be written in several pieces.
        juce::AudioBuffer<float> audio (2, (int) (3.0 * sampleRate));
        juce::Random random (1);

        for (int ch = 0; ch < audio.getNumChannels(); ++ch)
            for (int i = 0; i < audio.getNumSamples(); ++i)
                audio.setSample (ch, i, random.nextFloat() * 0.2f - 0.1f);

        auto cancelPartWay = [&] (const juce::File& file, auto&& write)
        {
            file.replaceWithText ("what was there before");
            const auto filesBefore = folder.getNumberOfChildFiles (juce::File::findFiles);
            int asked = 0;

            // Cancelled the second time it asks: after the first piece.
            const auto result = write (file, [&] { return ++asked > 1; });

            check (result == "Export cancelled.", file.getFileExtension() + ": the export says it was cancelled, not: " + result);
            check (file.loadFileAsString() == "what was there before", "the file that was there is left as it was");
            check (folder.getNumberOfChildFiles (juce::File::findFiles) == filesBefore, "and no temporary file is left behind");
        };

        cancelPartWay (folder.getChildFile ("cancelled.wav"), [&] (const juce::File& file, const std::function<bool()>& cancel)
        {
            return AudioExport::writeWav (audio, sampleRate, file, 24, cancel);
        });

        if (AudioExport::canWriteMp3())
            cancelPartWay (folder.getChildFile ("cancelled.mp3"), [&] (const juce::File& file, const std::function<bool()>& cancel)
            {
                return AudioExport::writeMp3 (audio, sampleRate, file, 320, "Cancelled", cancel);
            });
    }

    // The CLAP test synth counts its instances, and how many times one was set to render offline.
    juce::DynamicLibrary hooks;
    using Count = int (*)();
    Count liveInstances = nullptr, offlineRenders = nullptr;

    if (synthFile.hasFileExtension ("clap") && hooks.open (synthFile.getFullPathName()))
    {
        liveInstances = (Count) hooks.getFunction ("mperoll_test_synth_live_instances");
        offlineRenders = (Count) hooks.getFunction ("mperoll_test_synth_offline_renders");
    }

    if (offlineRenders != nullptr)
    {
        std::printf ("The synth copy renders offline\n");
        check (offlineRenders() > 0, "a copy of the CLAP synth was told it renders offline");
    }

    std::printf ("Removing MPE Roll while an export runs\n");
    {
        const auto abandoned = folder.getChildFile ("abandoned.wav");
        abandoned.deleteFile();
        const auto instancesBefore = liveInstances != nullptr ? liveInstances() : 0;
        bool reported = false;

        {
            MpeRollProcessor other;
            other.setRateAndBufferSizeDetails (sampleRate, 512);
            other.prepareToPlay (sampleRate, 512);

            if (loadSynth (other, synthFile))
            {
                // Five minutes of one note in the smallest blocks: far more than any build renders in
                // the moment between seeing the export under way (below) and the plugin going, and
                // the render's buffer for it is about 110 MB.
                MpeClip clip;
                clip.length = 600.0;
                clip.looping = false;
                clip.addNote (note (60, 0.0, 600.0, 0.8f));

                AudioExport::Options options;
                options.sampleRate = sampleRate;
                options.blockSize = 16;
                AudioExport::exportClip (other, clip, abandoned, options, [&] (const juce::String&) { reported = true; });
                auto& jobs = other.getExportJobs();
                check (jobs.getNumRunning() == 1, "the export is one of the plugin's jobs");

                // Wait for the render to be under way, not for a set time: how long the synth copy takes
                // to load varies, and a Release build renders minutes of audio in well under a second,
                // so a half-second wait once saw the export finish and the file written before the
                // plugin went. Progress above 0 means the copy has loaded and is playing the clip;
                // below 0.9, that the file isn't being written yet.
                bool underWay = false;

                for (int i = 0; i < 2000 && ! underWay && jobs.getNumRunning() == 1; ++i)
                {
                    mm.runDispatchLoopUntil (5);
                    const auto progress = jobs.getProgress (0);
                    underWay = progress > 0.0 && progress < 0.9;
                }

                check (underWay, "the export got under way: the synth copy loaded and started playing the clip");
            }

            other.releaseResources();
        }

        mm.runDispatchLoopUntil (200);
        check (! reported && ! abandoned.existsAsFile(), "the export ends with the plugin, reporting and writing nothing");

        if (liveInstances != nullptr)
            check (liveInstances() == instancesBefore, "the synth copy is gone with it");
    }
}

int exportTest (const juce::File& synthFile, const juce::File& folder)
{
    Check check;
    constexpr double sampleRate = 48000.0;
    folder.createDirectory();

    MpeRollProcessor processor;
    processor.setRateAndBufferSizeDetails (sampleRate, 512);
    processor.prepareToPlay (sampleRate, 512);

    auto& mm = *juce::MessageManager::getInstance();
    bool loaded = false;
    juce::String error;
    processor.getSynth().loadFromFile (synthFile, [&] (const juce::String& e) { error = e; loaded = true; });

    for (int i = 0; i < 200 && ! loaded; ++i)
        mm.runDispatchLoopUntil (50);

    if (! loaded || error.isNotEmpty())
    {
        std::printf ("Couldn't load %s: %s\n", synthFile.getFullPathName().toRawUTF8(), error.toRawUTF8());
        return 1;
    }

    // At 120 bpm: A4 for half a second, then C5 for half a second.
    MpeClip clip;
    clip.name = "Export test";
    clip.length = 2.0;
    clip.looping = false;
    clip.addNote (note (69, 0.0, 1.0, 0.8f));
    clip.addNote (note (72, 1.0, 1.0, 0.8f));

    auto exportAs = [&] (AudioExport::Format format, const juce::File& target)
    {
        AudioExport::Options options;
        options.format = format;
        options.sampleRate = sampleRate;
        options.bpm = 120.0;
        options.title = clip.name;

        bool finished = false;
        juce::String result;
        AudioExport::exportClip (processor, clip, target, options, [&] (const juce::String& e) { result = e; finished = true; });

        for (int i = 0; i < 600 && ! finished; ++i)
            mm.runDispatchLoopUntil (50);

        check (finished && result.isEmpty(), "the export finishes without a problem: " + result);
    };

    auto readBack = [] (juce::AudioFormat& format, const juce::File& file, double& rate)
    {
        juce::AudioBuffer<float> audio;
        std::unique_ptr<juce::AudioFormatReader> reader (format.createReaderFor (file.createInputStream().release(), true));

        if (reader != nullptr)
        {
            rate = reader->sampleRate;
            audio.setSize ((int) reader->numChannels, (int) reader->lengthInSamples);
            reader->read (&audio, 0, (int) reader->lengthInSamples, 0, true, true);
        }

        return audio;
    };

    const auto a4 = juce::MidiMessage::getMidiNoteInHertz (69), c5 = juce::MidiMessage::getMidiNoteInHertz (72);

    std::printf ("WAV\n");
    {
        const auto wavFile = folder.getChildFile ("export.wav");
        exportAs (AudioExport::Format::wav, wavFile);

        juce::WavAudioFormat wav;
        double rate = 0.0;
        const auto audio = readBack (wav, wavFile, rate);
        const auto seconds = audio.getNumSamples() / juce::jmax (1.0, rate);
        const auto first = sineFrequency (audio, (int) (0.1 * rate), (int) (0.4 * rate), rate);
        const auto second = sineFrequency (audio, (int) (0.6 * rate), (int) (0.9 * rate), rate);
        std::printf ("%.3f s at %.0f Hz, %d channels: %.1f Hz then %.1f Hz\n", seconds, rate, audio.getNumChannels(), first, second);

        check (audio.getNumChannels() == 2 && std::abs (rate - sampleRate) < 1.0, "a stereo WAV at the project's rate");
        check (seconds > 0.99 && seconds < 1.4, "as long as the clip, plus the synth's tail");
        check (std::abs (first - a4) < 1.0 && std::abs (second - c5) < 1.0, "the notes play in time and in tune");
    }

    if (AudioExport::canWriteMp3())
    {
        std::printf ("MP3\n");
        const auto mp3File = folder.getChildFile ("export.mp3");
        exportAs (AudioExport::Format::mp3, mp3File);

       #if JUCE_USE_MP3AUDIOFORMAT
        juce::MP3AudioFormat mp3;
        double rate = 0.0;
        const auto audio = readBack (mp3, mp3File, rate);
        const auto seconds = audio.getNumSamples() / juce::jmax (1.0, rate);
        const auto first = sineFrequency (audio, (int) (0.15 * rate), (int) (0.4 * rate), rate);
        const auto second = sineFrequency (audio, (int) (0.65 * rate), (int) (0.9 * rate), rate);
        std::printf ("%.3f s at %.0f Hz, %lld bytes: %.1f Hz then %.1f Hz\n", seconds, rate,
                     (long long) mp3File.getSize(), first, second);

        check (std::abs (rate - sampleRate) < 1.0 && audio.getNumChannels() == 2, "a stereo MP3 at 48 kHz");
        check (seconds > 0.95 && seconds < 1.5, "as long as the clip, give or take the encoder's padding");
        check (std::abs (first - a4) < 3.0 && std::abs (second - c5) < 3.0, "and it sounds the same");
       #endif
    }

    exportFixChecks (check, synthFile, folder, sampleRate);
    check (processor.getSynth().isLoaded(), "the synth playing live is still there");

    processor.releaseResources();
    std::printf ("%s: %d failure(s)\n", check.failures == 0 ? "PASS" : "FAIL", check.failures);
    return check.failures == 0 ? 0 : 1;
}

int render (const juce::File& synthFile, const juce::File& target)
{
    constexpr double sampleRate = 48000.0;
    constexpr int blockSize = 512;

    MpeRollProcessor processor;
    FakePlayHead playHead;
    processor.setPlayHead (&playHead);

    // The synth is loaded at another rate and block size, then the host changes both, as FL does
    // when its audio settings change. The pitches below are only right if the synth heard that.
    processor.setRateAndBufferSizeDetails (44100.0, 256);
    processor.prepareToPlay (44100.0, 256);

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
    processor.setRateAndBufferSizeDetails (sampleRate, blockSize);
    processor.prepareToPlay (sampleRate, blockSize);

    if (const auto& vst3 = processor.getSynth().getVst3Support(); vst3.isVst3)
        std::printf ("%s\n", vst3.describe().toRawUTF8());

    std::printf ("Played as: %s\n", processor.getSynth().describeConnection (processor.getDocument().getSettings().output).toRawUTF8());

    // C4 glides up an octave between beats 0.5 and 1.5 while E4 holds still. Then A4 has MPE Roll's
    // vibrato: a semitone either way, five times a second.
    MpeClip clip;
    clip.length = 8.0;
    auto glide = note (60, 0.0, 2.0, 0.8f);
    glide.lanes.pitch = { { 0.0, 0.0f }, { 0.5, 0.0f }, { 1.5, 12.0f } };
    clip.addNote (glide);
    clip.addNote (note (64, 0.0, 2.0, 0.8f));
    auto wobble = note (69, 3.0, 2.0, 0.8f);
    wobble.lanes.vibrato = { { 0.0, 1.0f } };
    clip.addNote (wobble);
    clip.vibratoRate = 5.0;
    processor.getDocument().replaceClip (0, clip, "Test");

    const auto totalSamples = (int) (5.5 / 2.0 * sampleRate);
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

    // The sine's frequency from one upward zero crossing to the next, between beats 3.2 and 4.8.
    double highest = 0.0, lowest = 1.0e9;
    int swings = 0;
    {
        const auto a4 = juce::MidiMessage::getMidiNoteInHertz (69);
        const auto* data = output.getReadPointer (0);
        double lastCrossing = -1.0;
        bool high = false;

        for (auto i = (int) (3.2 / 2.0 * sampleRate); i < (int) (4.8 / 2.0 * sampleRate); ++i)
        {
            if (data[i - 1] >= 0.0f || data[i] < 0.0f)
                continue;

            const auto crossing = i - 1 + data[i - 1] / (data[i - 1] - data[i]);

            if (lastCrossing >= 0.0)
            {
                const auto frequency = sampleRate / (crossing - lastCrossing);
                highest = std::max (highest, frequency);
                lowest = std::min (lowest, frequency);

                if (! high && frequency > a4 * 1.02)
                    ++swings;

                high = frequency > a4 * 1.02 || (high && frequency > a4 / 1.02);
            }

            lastCrossing = crossing;
        }
    }

    // A semitone either way is 415 to 466 Hz; 0.8 seconds at 5 Hz is four swings.
    const bool vibrato = highest > 460.0 && highest < 472.0 && lowest > 410.0 && lowest < 421.0 && swings >= 3 && swings <= 5;
    const bool ok = separateChannels && startsInTune && glidedUp && otherNoteHeld && vibrato;

    std::printf ("Vibrato on A4: %.1f to %.1f Hz, %d swings\n", lowest, highest, swings);
    std::printf ("Separate channels: %s, starts in tune: %s, C4 glided to C5: %s, E4 unaffected: %s, vibrato: %s\n",
                 separateChannels ? "yes" : "NO", startsInTune ? "yes" : "NO",
                 glidedUp ? "yes" : "NO", otherNoteHeld ? "yes" : "NO", vibrato ? "yes" : "NO");
    std::printf ("%s (wrote %s)\n", ok ? "PASS" : "FAIL", target.getFullPathName().toRawUTF8());

    processor.releaseResources();
    return ok ? 0 : 1;
}

//==============================================================================
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

/** The settings every MPE Roll on the computer shares, the scanned plugin list and the Window
    drawing choice among them: one copy in the process, so a change in one MPE Roll is seen by the
    others and kept when any of them saves.
*/
void sharedSettingsChecks (Check& check)
{
    std::printf ("Every MPE Roll in the process shares the computer's settings\n");
    const auto file = juce::File::createTempFile (".settings");

    juce::PluginDescription scanned;
    scanned.name = "Scanned Elsewhere";
    scanned.pluginFormatName = "VST3";
    scanned.fileOrIdentifier = "/plugins/Scanned Elsewhere.vst3";
    scanned.uniqueId = 0x5ca11ed;
    scanned.isInstrument = true;

    // The names of the plugins the settings file lists right now.
    const auto savedNames = [&file]
    {
        juce::PropertiesFile settings (file, {});
        juce::KnownPluginList saved;
        juce::StringArray names;

        if (auto xml = settings.getXmlValue ("knownPlugins"))
            saved.recreateFromXml (*xml);

        for (const auto& type : saved.getTypes())
            names.add (type.name);

        return names;
    };

    {
        HostedSynth first;
        first.useSettingsFile (file);

        {
            HostedSynth second;
            second.getKnownPlugins().addType (scanned);
            const auto types = first.getKnownPlugins().getTypes();
            check (types.size() == 1 && types[0].name == scanned.name, "a plugin scanned in one MPE Roll is listed in the others");

            WindowDrawing::setMode (second.getUserSettings(), WindowDrawing::Mode::software);
            check (WindowDrawing::getMode (first.getUserSettings()) == WindowDrawing::Mode::software, "and so is a Window drawing choice");
            juce::MessageManager::getInstance()->runDispatchLoopUntil (50);     // the list's change message, which saves it
        }

        // The MPE Roll left saving its settings after the other has gone mustn't write an older list back.
        first.getUserSettings()->setValue ("checked", true);
        first.getUserSettings()->saveIfNeeded();

        juce::PropertiesFile settings (file, {});
        check (savedNames() == juce::StringArray { scanned.name }, "and is still in the settings file once that MPE Roll has gone");
        check (WindowDrawing::getMode (&settings) == WindowDrawing::Mode::software, "with the Window drawing choice");
    }

    std::printf ("The last MPE Roll to go saves what a scan found after the list's last change message\n");
    auto late = scanned;
    late.name = "Found Last";
    late.fileOrIdentifier = "/plugins/Found Last.vst3";
    late.uniqueId = 0x1a7e;

    {
        // As FL deleting the last MPE Roll (closing the project, say) while a scan is still finding
        // plugins: the list announces each find with a message, and the last ones' never arrive. The
        // message loop isn't run here, as it wouldn't be then. This is the only MPE Roll in the
        // process, so the settings are let go with it.
        HostedSynth last;
        last.useSettingsFile (file);
        last.getKnownPlugins().addType (late);
    }

    check (savedNames() == juce::StringArray { scanned.name, late.name }, "the settings file lists the plugin found last");
    file.deleteFile();
}

int selfTest()
{
    Check check;
    FakePlayHead playHead;
    double ppq = 0.0;

    // Let go before the settings checks: some need the last MPE Roll in the process to go.
    auto processorHolder = std::make_unique<MpeRollProcessor>();
    auto& processor = *processorHolder;
    processor.setPlayHead (&playHead);
    processor.setRateAndBufferSizeDetails (48000.0, 512);
    processor.prepareToPlay (48000.0, 512);
    auto& document = processor.getDocument();

    std::printf ("Same-named plugins are told apart in the synth menu\n");
    {
        const auto plugin = [] (const juce::String& name, const juce::String& format, const juce::File& file)
        {
            juce::PluginDescription d;
            d.name = name;
            d.pluginFormatName = format;
            d.fileOrIdentifier = file.getFullPathName();
            return d;
        };

        // Two installs of Analog Lab, as a user had: the same name, and the same format twice.
        const auto base = juce::File::getSpecialLocation (juce::File::tempDirectory);
        const auto arturia = base.getChildFile ("Arturia"), x64 = arturia.getChildFile ("Analog Lab V").getChildFile ("x64");
        juce::Array<juce::PluginDescription> types;
        types.add (plugin ("Pigments", "VST3", base.getChildFile ("Pigments.vst3")));
        types.add (plugin ("Analog Lab V", "VST3", x64.getChildFile ("Analog Lab V.vst3")));
        types.add (plugin ("Analog Lab V", "VST", arturia.getChildFile ("Analog Lab V.dll")));
        types.add (plugin ("Analog Lab V", "VST", x64.getChildFile ("Analog Lab V.dll")));

        const auto shown = HostedSynth::withDistinctNames (types);
        check (shown.size() == 4 && shown[0].name == "Pigments", "a name of its own stays as it is");
        check (shown[1].name == "Analog Lab V (VST3)", "one sharing its name gets its format");
        check (shown[2].name == "Analog Lab V (VST, " + arturia.getFullPathName() + ")"
                   && shown[3].name == "Analog Lab V (VST, " + x64.getFullPathName() + ")",
               "two installs of one plugin get their folders");
        check (shown[2].fileOrIdentifier == types[2].fileOrIdentifier && shown[3].fileOrIdentifier == types[3].fileOrIdentifier,
               "in the same order, so a pick finds its plugin");
    }

    std::printf ("A synth's parameter named after a performance control is known by its name\n");
    {
        // Serum 1's wheels, and a parameter a synth gives one channel's or one MIDI controller's
        // bend or wheel (Analog Lab's "channel1 pitch bend" to "channel16 pitch bend").
        for (const auto* name : { "Pitch Bend", "Pitch Wheel", "Mod Wheel", "Modulation Wheel", "MIDI Pitch Bend",
                                  "Aftertouch", "Channel Pressure", "channel1 pitch bend", "channel16 pitch bend",
                                  "Ch 3 Mod Wheel", "MIDI CC1 Mod Wheel", "Chan 12 Aftertouch", "MIDI channel 2 pitch bend",
                                  "Pitch Bend Wheel" })
            check (HostedSynth::namesPerformanceControl (name), juce::String ("\"") + name + "\" follows the notes");

        // A synth's own settings, whatever they say about bending or pressure.
        for (const auto* name : { "Bend Range", "Pitch Bend Range", "Pitch Bend Up", "Pressure", "Mod Wheel Depth",
                                  "Aftertouch Amount", "MIDI Channel", "Chorus", "Cutoff", "Channel 1 Volume", "CC Smoothing" })
            check (! HostedSynth::namesPerformanceControl (name), juce::String ("\"") + name + "\" is a setting of the synth's");
    }

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
    processorHolder.reset();
    sharedSettingsChecks (check);

    std::printf ("%s: %d failure(s)\n", check.failures == 0 ? "PASS" : "FAIL", check.failures);
    return check.failures == 0 ? 0 : 1;
}

//==============================================================================
bool loadSynth (MpeRollProcessor& processor, const juce::File& file)
{
    bool done = false;
    juce::String error;
    processor.getSynth().loadFromFile (file, [&] (const juce::String& e) { error = e; done = true; });

    for (int i = 0; i < 200 && ! done; ++i)
        juce::MessageManager::getInstance()->runDispatchLoopUntil (50);

    if (! done || error.isNotEmpty())
        std::printf ("Couldn't load %s: %s\n", file.getFullPathName().toRawUTF8(), error.toRawUTF8());

    return done && error.isEmpty();
}

struct ValueRecorder : juce::AudioProcessorParameter::Listener
{
    float last = -1.0f;
    int gestures = 0, offMessageThread = 0;

    void parameterValueChanged (int, float v) override  { last = v; note(); }
    void parameterGestureChanged (int, bool) override    { ++gestures; note(); }

    void note()
    {
        if (! juce::MessageManager::existsAndIsCurrentThread())
            ++offMessageThread;
    }
};

/** Checks only the CLAP test synth can help with: its exported hooks act as its user. */
void clapSynthChecks (Check& check, MpeRollProcessor& processor, FakePlayHead& playHead, const juce::File& synthFile)
{
    juce::DynamicLibrary hooks;

    if (! hooks.open (synthFile.getFullPathName()))
    {
        std::printf ("  (the CLAP test synth's hooks aren't reachable here: skipped)\n");
        return;
    }

    using TurnKnob = bool (*) (double);
    using Rebuild = bool (*)();
    using StaleCookies = int (*)();
    auto turnKnob = (TurnKnob) hooks.getFunction ("mperoll_test_synth_turn_tone_knob");
    auto rebuild = (Rebuild) hooks.getFunction ("mperoll_test_synth_rebuild_parameters");
    auto staleCookies = (StaleCookies) hooks.getFunction ("mperoll_test_synth_stale_cookies");
    auto currentCookies = (StaleCookies) hooks.getFunction ("mperoll_test_synth_current_cookies");

    if (turnKnob == nullptr || rebuild == nullptr || staleCookies == nullptr || currentCookies == nullptr)
    {
        std::printf ("  (the CLAP test synth has no hooks: skipped)\n");
        return;
    }

    auto& params = processor.getParameters();

    // One block on another thread, as a host's audio thread would run it.
    auto processOnAudioThread = [&]
    {
        std::thread audioThread ([&]
        {
            juce::AudioBuffer<float> audio (2, 512);
            juce::MidiBuffer midi;
            processor.processBlock (audio, midi);
        });

        audioThread.join();
        juce::MessageManager::getInstance()->runDispatchLoopUntil (100);
    };

    std::printf ("Edits in a CLAP synth's own window reach the host as edits\n");
    {
        ValueRecorder recorder;
        params[1]->addListener (&recorder);
        turnKnob (0.25);
        processOnAudioThread();
        params[1]->removeListener (&recorder);

        check (std::abs (recorder.last - 0.25f) < 1.0e-4f, "the new value reaches the slot");
        check (recorder.gestures == 2, "with its gesture");
        check (recorder.offMessageThread == 0, "on the message thread, where hosts record edits");
    }

    std::printf ("A CLAP synth that rebuilds its parameters gets current cookies\n");
    {
        playHead.playing = false;
        processor.releaseResources();
        check (rebuild(), "the synth rebuilt its parameters while inactive");
        processor.prepareToPlay (48000.0, 512);

        const auto currentBefore = currentCookies();
        params[1]->setValue (0.6f);
        processOnAudioThread();
        check (currentCookies() > currentBefore, "the value reached the synth with its new cookie");
        check (staleCookies() == 0, "no value was sent with an out-of-date cookie");
    }
}

/** A processor with the synth loaded afresh, for playing it notes and asking whether MPE Roll
    warned that it made no sound. Muting the synth stands in for one that isn't listening on MPE
    Roll's channels.
*/
struct SilenceProbe
{
    SilenceProbe (const juce::File& synthFile, OutputSettings::Mode mode, bool muted)
    {
        playHead.playing = false;
        processor.setPlayHead (&playHead);
        processor.setRateAndBufferSizeDetails (48000.0, 512);
        processor.prepareToPlay (48000.0, 512);
        loaded = loadSynth (processor, synthFile);

        auto settings = processor.getDocument().getSettings();
        settings.output.mode = mode;
        processor.getDocument().setSettings (settings);
        mute (muted);
    }

    ~SilenceProbe()
    {
        processor.releaseResources();
    }

    void mute (bool muted)
    {
        for (int i = 0; processor.getSynth().withParameter (i, [muted] (juce::AudioProcessorParameter& p)
                                                            { if (p.getName (64) == "Gain") p.setValue (muted ? 0.0f : 1.0f); }); ++i) {}
    }

    /** Plays a note for this long, then lets it go. */
    void hold (int key, double seconds)
    {
        runBlock (processor, playHead, ppq, { { 0, juce::MidiMessage::noteOn (2, key, (juce::uint8) 100) } });
        rest (seconds - 512.0 / 48000.0);
        runBlock (processor, playHead, ppq, { { 0, juce::MidiMessage::noteOff (2, key) } });
    }

    /** Plays nothing for this long. */
    void rest (double seconds)
    {
        for (int block = 0; block < juce::roundToInt (seconds * 48000.0 / 512.0); ++block)
            runBlock (processor, playHead, ppq);
    }

    bool warned()
    {
        return processor.getSynth().takeSilenceWarning();
    }

    FakePlayHead playHead;
    MpeRollProcessor processor;
    double ppq = 0.0;
    bool loaded = false;
};

/** Loads the synth afresh, plays it a note held for fourteen seconds (past the ten a new synth is
    given to load) and says whether MPE Roll warned that the synth made no sound. Empty if the synth
    didn't load.
*/
std::optional<bool> warnsOfSilence (const juce::File& synthFile, OutputSettings::Mode mode, bool muted)
{
    SilenceProbe probe (synthFile, mode, muted);

    if (! probe.loaded)
        return {};

    probe.hold (60, 14.0);
    return probe.warned();
}

/** The "No sound from ..." warning: given once for a synth that stays silent over MPE, with short
    notes adding up as one long one would, but not while it may still be loading and not once it
    has sounded.
*/
void silenceWatchChecks (Check& check, const juce::File& synthFile)
{
    std::printf ("A synth that makes no sound over MPE is pointed out\n");

    // A CLAP synth played with note expressions gets its notes whatever the channels.
    const auto midiOnly = ! synthFile.hasFileExtension ("clap");
    check (warnsOfSilence (synthFile, OutputSettings::Mode::mpe, true) == midiOnly,
           midiOnly ? "a silent synth played over MPE is pointed out" : "not with note expressions");
    check (warnsOfSilence (synthFile, OutputSettings::Mode::mpe, false) == false, "a synth that sounds isn't");
    check (warnsOfSilence (synthFile, OutputSettings::Mode::singleChannel, true) == false,
           "nor a silent one played with Plain MIDI");

    if (! midiOnly)
        return;

    std::printf ("Not while it may still be loading, and not once it has sounded\n");
    {
        SilenceProbe probe (synthFile, OutputSettings::Mode::mpe, true);
        probe.hold (60, 8.0);
        check (probe.loaded && ! probe.warned(), "a synth silent for its first seconds is given time to load");

        probe.rest (2.5);               // the grace is over
        probe.hold (60, 2.0);           // four seconds of silent notes, none held for three
        probe.hold (62, 2.0);
        check (probe.warned(), "short notes add up to the warning: the synth heard none of them");
        check (probe.processor.getSynth().isSilentOverMpe(), "and the synth is marked silent for the status line");

        probe.hold (60, 3.5);
        check (! probe.warned(), "the warning comes once");
    }

    {
        SilenceProbe probe (synthFile, OutputSettings::Mode::mpe, true);
        probe.hold (60, 14.0);          // warned, but the warning hasn't been shown
        probe.mute (false);
        probe.hold (60, 0.5);
        check (probe.loaded && ! probe.warned(), "a warning not shown yet is taken back once the synth sounds");
        check (! probe.processor.getSynth().isSilentOverMpe(), "and the mark for the status line goes with it");
    }

    {
        SilenceProbe probe (synthFile, OutputSettings::Mode::mpe, true);
        probe.hold (60, 14.0);          // warned, but the warning hasn't been shown

        juce::MemoryBlock state;
        probe.processor.getStateInformation (state);
        probe.processor.setStateInformation (state.getData(), (int) state.getSize());
        juce::MessageManager::getInstance()->runDispatchLoopUntil (50);

        probe.hold (60, 5.0);
        check (probe.loaded && ! probe.warned(), "a synth given a project's state gets the time to load again");

        probe.rest (9.0);               // the grace is over, but nothing is held
        check (! probe.warned(), "and silence with no note held doesn't count against it");

        probe.hold (60, 3.5);
        check (probe.warned(), "a note held for three seconds afterwards is pointed out as before");
    }
}

/** The "No sound from ..." box the editor shows for the warning. Only its Use Plain MIDI and One
    copy per note buttons may change the project: Return and Escape both keep MPE, as closing the
    box does.
*/
void silenceAlertChecks (Check& check, const juce::File& synthFile)
{
    if (synthFile.hasFileExtension ("clap"))     // played with note expressions, so never warned about
        return;

    std::printf ("Only the box's Use Plain MIDI and One copy per note buttons switch the project away from MPE\n");
    ignoreXErrors();
    auto& mm = *juce::MessageManager::getInstance();
    SilenceProbe probe (synthFile, OutputSettings::Mode::mpe, true);
    check (probe.loaded, "the synth loads");

    if (! probe.loaded)
        return;

    std::unique_ptr<juce::AudioProcessorEditor> editor (probe.processor.createEditorAndMakeActive());
    editor->setSize (1200, 760);

    const auto findBox = []() -> juce::AlertWindow*
    {
        auto& desktop = juce::Desktop::getInstance();

        for (int i = 0; i < desktop.getNumComponents(); ++i)
            if (auto* box = dynamic_cast<juce::AlertWindow*> (desktop.getComponent (i)))
                if (box->getName().startsWith ("No sound from"))
                    return box;

        return nullptr;
    };

    const auto boxCount = []
    {
        int count = 0;
        auto& desktop = juce::Desktop::getInstance();

        for (int i = 0; i < desktop.getNumComponents(); ++i)
            if (auto* box = dynamic_cast<juce::AlertWindow*> (desktop.getComponent (i)))
                count += box->getName().startsWith ("No sound from") ? 1 : 0;

        return count;
    };

    // Plays the synth into the warning, which the editor's timer turns into the box.
    const auto provoke = [&]() -> juce::AlertWindow*
    {
        probe.hold (60, 14.0);

        for (int i = 0; i < 20 && findBox() == nullptr; ++i)
            mm.runDispatchLoopUntil (50);

        return findBox();
    };

    // The warning comes once per synth loaded or given a project's state: given its own state back,
    // the synth is listened to afresh.
    const auto listenAgain = [&]
    {
        juce::MemoryBlock state;
        probe.processor.getStateInformation (state);
        probe.processor.setStateInformation (state.getData(), (int) state.getSize());
        mm.runDispatchLoopUntil (50);
    };

    const auto mode = [&] { return probe.processor.getDocument().getSettings().output.mode; };

    // As the window's peer hands a key on (the key handler is the component's).
    const auto press = [&mm] (juce::Component& window, int key)
    {
        window.keyPressed (juce::KeyPress (key));
        mm.runDispatchLoopUntil (100);
    };

    // Whether a label somewhere in the editor (the toolbar's status line) starts with this.
    const std::function<bool (juce::Component&, const juce::String&)> labelSays = [&labelSays] (juce::Component& c, const juce::String& start)
    {
        if (auto* label = dynamic_cast<juce::Label*> (&c))
            if (label->getText().startsWith (start))
                return true;

        for (auto* child : c.getChildren())
            if (labelSays (*child, start))
                return true;

        return false;
    };

    const auto statusSays = [&] (const juce::String& start)
    {
        mm.runDispatchLoopUntil (100);      // the editor's timer refreshes the status line
        return labelSays (*editor, start);
    };

    check (! statusSays ("No sound from"), "the status line says nothing of it before the warning");

    auto* box = provoke();
    check (box != nullptr, "the warning opens the box");

    if (box != nullptr)
    {
        check (box->isAlwaysOnTop(), "which stays above FL's window, as the synth's own window does");
        check (statusSays ("No sound from"), "and the status line says so too");
        press (*box, juce::KeyPress::escapeKey);
        check (findBox() == nullptr && mode() == OutputSettings::Mode::mpe, "Escape closes it and keeps MPE");
        check (statusSays ("No sound from"), "and the status line keeps saying so while nothing is heard");
    }

    listenAgain();
    box = provoke();
    check (box != nullptr, "the box comes back for a synth given its state again");

    if (box != nullptr)
    {
        press (*box, juce::KeyPress::returnKey);
        check (findBox() == nullptr && mode() == OutputSettings::Mode::mpe, "Return closes it and keeps MPE too");
    }

    // A synth given its state again while the box is open (as FL sending a project's state again)
    // is listened to afresh and can warn again: the editor keeps the open box rather than putting
    // a second one over it, which closing the editor wouldn't take down.
    listenAgain();
    box = provoke();

    if (box != nullptr)
    {
        listenAgain();
        provoke();                          // returns at once, a box being up...
        mm.runDispatchLoopUntil (300);      // ...so give the editor's timer the new warning
        check (boxCount() == 1, "warned again while the box is open, the editor keeps that box rather than stacking another");
        press (*box, juce::KeyPress::escapeKey);
        check (findBox() == nullptr && mode() == OutputSettings::Mode::mpe, "and Escape closes it as before");
    }

    listenAgain();
    box = provoke();

    if (box != nullptr)
    {
        if (auto* button = box->getButton ("Use Plain MIDI"))
            button->triggerClick();

        mm.runDispatchLoopUntil (100);
        check (findBox() == nullptr && mode() == OutputSettings::Mode::singleChannel, "Use Plain MIDI switches the project to Plain MIDI");
    }

    // Back to MPE, unheard again, for the box's other way out. Two copies are quick to load.
    {
        auto settings = probe.processor.getDocument().getSettings();
        settings.output.mode = OutputSettings::Mode::mpe;
        settings.output.copies = OutputSettings::minCopies;
        probe.processor.getDocument().setSettings (settings);
    }

    listenAgain();
    box = provoke();

    if (box != nullptr)
    {
        if (auto* button = box->getButton ("One copy per note"))
            button->triggerClick();

        mm.runDispatchLoopUntil (100);
        check (findBox() == nullptr && mode() == OutputSettings::Mode::copies, "One copy per note switches the project to copies");

        // Past the message the switch puts up, the status line counts copies, with no word of the
        // MPE advice that no longer applies.
        mm.runDispatchLoopUntil (6200);
        check (! statusSays ("No sound from") && statusSays ("One copy per note"),
               "and the status line then speaks of the copies, not of turning MPE on");
    }

    probe.processor.editorBeingDeleted (editor.get());
    editor.reset();
    mm.runDispatchLoopUntil (50);
}

/** A synth loaded while a clip plays (in place of another, or where there was none) must get the
    zone lanes' values, which the encoder otherwise sends once and then only as they change.
*/
void synthReloadChecks (Check& check, MpeRollProcessor& processor, FakePlayHead& playHead, const juce::File& synthFile)
{
    std::printf ("A synth loaded while a clip plays gets the zone lanes' values\n");

    // One note under a mod wheel lane that never moves.
    MpeClip clip;
    clip.length = 4.0;
    clip.addNote (note (60, 0.0, 4.0, 0.8f));
    clip.zone.get (LaneId::cc (1)) = { { 0.0, 0.5f } };
    processor.getDocument().replaceClip (0, clip, "Zone lane");

    auto modWheelIn = [] (const std::vector<juce::MidiMessage>& out)
    {
        for (const auto& m : out)
            if (m.isController() && m.getControllerNumber() == 1 && m.getControllerValue() == MpeEncoder::toSevenBit (0.5f))
                return true;

        return false;
    };

    double ppq = 0.0;

    auto playBlocks = [&] (int count)
    {
        bool seen = false;

        for (int block = 0; block < count; ++block)
            seen |= modWheelIn (runBlock (processor, playHead, ppq));

        return seen;
    };

    playHead.playing = true;
    check (playBlocks (4), "the lane's value is sent as the clip starts");
    check (! playBlocks (4), "and not again while it stays the same");

    if (loadSynth (processor, synthFile))
        check (playBlocks (4), "a synth loaded in its place gets the value too");
    else
        check (false, "the synth loads again");

    playHead.playing = false;
    runBlock (processor, playHead, ppq);
}

int parameterTest (const juce::File& synthFile)
{
    Check check;
    FakePlayHead playHead;
    MpeRollProcessor processor;
    processor.setPlayHead (&playHead);
    processor.setRateAndBufferSizeDetails (48000.0, 512);
    processor.prepareToPlay (48000.0, 512);

    if (! loadSynth (processor, synthFile))
        return 1;

    const auto& params = processor.getParameters();
    auto* hosted = processor.getSynth().getInstance();

    std::printf ("The synth's window opens on screen\n");
    {
        auto& mm = *juce::MessageManager::getInstance();
        ignoreXErrors();
        auto& synth = processor.getSynth();
        synth.showEditor (nullptr);
        mm.runDispatchLoopUntil (200);

        juce::DocumentWindow* window = nullptr;

        for (int i = juce::Desktop::getInstance().getNumComponents(); --i >= 0;)
            if (auto* w = dynamic_cast<juce::DocumentWindow*> (juce::Desktop::getInstance().getComponent (i)))
                if (w->getName() == hosted->getName())
                    window = w;

        auto titleBarOnScreen = [&]
        {
            const auto work = WindowPlacement::workAreaNear (window);
            const auto outer = WindowPlacement::frameOf (*window).addedTo (window->getBounds());
            return work.contains (outer.getTopLeft()) && outer.getY() >= work.getY();
        };

        check (window != nullptr && titleBarOnScreen(), "with its title bar inside the display's work area");

        if (window != nullptr)
        {
            // As a window dragged or grown off the top: the synth menu's item brings it back.
            window->setTopLeftPosition (window->getX(), WindowPlacement::workAreaNear (window).getY() - 500);
            synth.bringEditorOnScreen (nullptr);
            mm.runDispatchLoopUntil (100);
            check (titleBarOnScreen(), "and Bring the synth's window back on screen brings back one that went off the top");
        }

        synth.closeEditor();
        mm.runDispatchLoopUntil (50);
    }

    std::printf ("Synth parameters appear as MPE Roll parameters\n");
    check (params.size() == SynthParameterBank::numSlots, "MPE Roll offers a fixed set of automatable slots");
    check (params[0]->getName (64) == "Gain" && params[1]->getName (64) == "Tone", "slots take the synth's parameter names");
    check (params[2]->getName (64).contains ("unused"), "slots beyond the synth's parameters say they're unused");

    std::printf ("Automation reaches the synth, and synth edits reach the host\n");
    params[1]->setValue (0.3f);
    check (std::abs (hosted->getParameters()[1]->getValue() - 0.3f) < 1.0e-4f, "setting a slot sets the synth parameter");

    ValueRecorder recorder;
    params[1]->addListener (&recorder);
    hosted->getParameters()[1]->beginChangeGesture();
    hosted->getParameters()[1]->setValueNotifyingHost (0.75f);
    hosted->getParameters()[1]->endChangeGesture();
    params[1]->removeListener (&recorder);
    check (std::abs (recorder.last - 0.75f) < 1.0e-4f, "a change in the synth is reported to the host");
    check (recorder.gestures == 2, "and so are its gestures (for FL's 'last tweaked')");

    // Gain 0 through the slot must silence the synth.
    params[0]->setValue (0.0f);
    MpeClip clip;
    clip.addNote (note (60, 0.0, 1.0, 1.0f));
    processor.getDocument().replaceClip (0, clip, "Test");

    double ppq = 0.0;
    float peak = 0.0f;

    for (int block = 0; block < 40; ++block)
    {
        juce::AudioBuffer<float> audio (2, 512);
        juce::MidiBuffer midi;
        playHead.ppq = ppq;
        processor.processBlock (audio, midi);
        ppq += 512.0 / 48000.0 * 2.0;
        peak = std::max (peak, audio.getMagnitude (0, 512));
    }

    check (peak < 1.0e-4f, "automating the synth's gain to 0 through MPE Roll silences it");

    std::printf ("Slots survive saving and reloading\n");
    {
        juce::MemoryBlock state;
        processor.getStateInformation (state);

        MpeRollProcessor restored;
        restored.setRateAndBufferSizeDetails (48000.0, 512);
        restored.prepareToPlay (48000.0, 512);
        restored.setStateInformation (state.getData(), (int) state.getSize());

        for (int i = 0; i < 100 && ! restored.getSynth().isLoaded(); ++i)
            juce::MessageManager::getInstance()->runDispatchLoopUntil (50);

        juce::MessageManager::getInstance()->runDispatchLoopUntil (100);
        check (restored.getParameters()[1]->getName (64) == "Tone", "the restored slots point at the same parameters");
        check (std::abs (restored.getParameters()[1]->getValue() - 0.75f) < 1.0e-3f, "and the synth's values come back");
        restored.releaseResources();
    }

    std::printf ("A slot whose parameter has gone stays empty\n");
    {
        auto saveXml = [&]
        {
            juce::MemoryBlock state;
            processor.getStateInformation (state);
            return juce::AudioProcessor::getXmlFromBinary (state.getData(), (int) state.getSize());
        };

        // The same synth is loaded, so this restores into it in place.
        auto restoreXml = [&] (const juce::XmlElement& xml)
        {
            juce::MemoryBlock state;
            juce::AudioProcessor::copyXmlToBinary (xml, state);
            processor.setStateInformation (state.getData(), (int) state.getSize());
            juce::MessageManager::getInstance()->runDispatchLoopUntil (100);
        };

        auto firstSlotOf = [] (juce::XmlElement* xml)
        {
            auto* slots = xml != nullptr ? xml->getChildByName ("SynthParameters") : nullptr;
            return slots != nullptr ? slots->getChildByName ("Slot") : nullptr;
        };

        auto xml = saveXml();
        auto* first = firstSlotOf (xml.get());
        check (first != nullptr && first->getIntAttribute ("index", -1) == 0, "the project lists the slots");

        if (first != nullptr)
        {
            const juce::String goneId ("a-parameter-this-synth-no-longer-has");
            first->setAttribute ("id", goneId);
            restoreXml (*xml);

            check (params[0]->getName (64).contains ("unused"), "its slot isn't handed to another parameter");
            check (params[1]->getName (64) == "Tone", "the other slots keep their parameters");
            check (params[2]->getName (64) == "Gain", "a parameter without a slot gets a free one");

            auto again = saveXml();
            auto* kept = firstSlotOf (again.get());
            check (kept != nullptr && kept->getStringAttribute ("id") == goneId, "saving keeps the slot for its parameter");

            xml->getChildByName ("SynthParameters")->setAttribute ("synth", "another synth");
            restoreXml (*xml);
            check (params[0]->getName (64) == "Gain" && params[1]->getName (64) == "Tone"
                       && params[2]->getName (64).contains ("unused"),
                   "slots saved for a different synth start afresh");
        }
    }

    if (synthFile.hasFileExtension ("clap"))
        clapSynthChecks (check, processor, playHead, synthFile);

    std::printf ("A block longer than the host announced still plays\n");
    {
        params[0]->setValue (1.0f);
        playHead.playing = true;
        playHead.ppq = 0.0;

        juce::AudioBuffer<float> audio (2, 2048);     // prepared for 512
        juce::MidiBuffer midi;
        processor.processBlock (audio, midi);
        check (audio.getMagnitude (0, 0, 512) > 1.0e-3f && audio.getMagnitude (0, 1536, 512) > 1.0e-3f,
               "the synth sounds all the way through it");

        // The CLAP test synth reports where the transport said its last piece started.
        juce::DynamicLibrary hooks;

        if (synthFile.hasFileExtension ("clap") && hooks.open (synthFile.getFullPathName()))
        {
            using LastBeats = double (*)();

            if (auto lastBeats = (LastBeats) hooks.getFunction ("mperoll_test_synth_last_song_beats"))
            {
                const auto expected = 1536.0 / 48000.0 * playHead.bpm / 60.0;
                check (std::abs (lastBeats() - expected) < 1.0e-4, "each piece hears where in the song it starts");
            }
        }
    }

    silenceWatchChecks (check, synthFile);
    silenceAlertChecks (check, synthFile);

    synthReloadChecks (check, processor, playHead, synthFile);

    processor.releaseResources();
    std::printf ("%s: %d failure(s)\n", check.failures == 0 ? "PASS" : "FAIL", check.failures);
    return check.failures == 0 ? 0 : 1;
}

//==============================================================================
/** Plays one held note through the CLAP test synth with CLAP note expressions on, then off.
    The test synth ignores MIDI, so it should only sound when MPE Roll sends it CLAP notes.
*/
int clapDialectTest (const juce::File& synthFile)
{
    Check check;
    FakePlayHead playHead;
    MpeRollProcessor processor;
    processor.setPlayHead (&playHead);
    processor.setRateAndBufferSizeDetails (48000.0, 512);
    processor.prepareToPlay (48000.0, 512);

    if (! loadSynth (processor, synthFile))
        return 1;

    MpeClip clip;
    clip.length = 4.0;
    clip.addNote (note (60, 0.0, 4.0, 0.8f));
    processor.getDocument().replaceClip (0, clip, "Test");

    auto peakWith = [&] (bool noteExpressions)
    {
        auto s = processor.getDocument().getSettings();
        s.output.clapNoteExpressions = noteExpressions;
        processor.getDocument().setSettings (s);

        std::printf ("Played with: %s\n", processor.getSynth().describeConnection (s.output).toRawUTF8());

        // Stop, so the note starts again under the new setting.
        playHead.playing = false;
        double ppq = 0.0;
        runBlock (processor, playHead, ppq);
        playHead.playing = true;

        float peak = 0.0f;

        for (int block = 0; block < 40; ++block)
        {
            juce::AudioBuffer<float> audio (2, 512);
            juce::MidiBuffer midi;
            playHead.ppq = ppq;
            processor.processBlock (audio, midi);
            ppq += 512.0 / 48000.0 * 2.0;

            if (block >= 4)
                peak = std::max (peak, audio.getMagnitude (0, 512));
        }

        return peak;
    };

    std::printf ("A CLAP synth is played with CLAP notes and note expressions\n");
    const auto withExpressions = peakWith (true);
    check (withExpressions > 0.01f, "with note expressions on, the synth plays");

    const auto withMidi = peakWith (false);
    check (withMidi < 1.0e-4f, "with them off it gets MPE MIDI instead (which this synth ignores)");

    const auto again = peakWith (true);
    check (again > 0.01f, "and switching back works without reloading the synth");

    processor.releaseResources();
    std::printf ("%s: %d failure(s)\n", check.failures == 0 ? "PASS" : "FAIL", check.failures);
    return check.failures == 0 ? 0 : 1;
}

/** The VST3 note expression path, with a synth that maps pitch bend on MIDI channel 1 only and takes
    tuning and pressure as note expressions (tools/Vst3NoteExpressionTestSynth.cpp), and a JUCE-built
    one that maps every channel. The glides themselves are checked by render.
*/
int vst3DialectTest (const juce::File& noteExpressionSynth, const juce::File& midiSynth)
{
    Check check;

    // The synth's controller echoes each move of its MIDI-mapped parameter to the host as an edit
    // (begin, perform, end), as some synths do: MPE Roll mustn't pass those on either.
   #if JUCE_WINDOWS
    _putenv_s ("MPEROLL_TEST_ECHO_EDITS", "1");
   #else
    setenv ("MPEROLL_TEST_ECHO_EDITS", "1", 1);
   #endif

    FakePlayHead playHead;
    MpeRollProcessor processor;
    processor.setPlayHead (&playHead);
    processor.setRateAndBufferSizeDetails (48000.0, 512);
    processor.prepareToPlay (48000.0, 512);

    if (! loadSynth (processor, noteExpressionSynth))
        return 1;

    auto& synth = processor.getSynth();
    const auto& support = synth.getVst3Support();
    std::printf ("%s\n", support.describe().toRawUTF8());

    std::printf ("MPE Roll asks a VST3 synth what it takes\n");
    check (support.isVst3 && support.hasMidiMapping && support.channelsWithBend == 1 && ! support.ownBendOnMemberChannels,
           "it sees pitch bend mapped on one MIDI channel only, so MPE over MIDI would miss the notes");
    check (support.takesTuning() && support.typeIds[(size_t) Vst3Support::pressure] == 100000 && support.scaled[(size_t) Vst3Support::pressure],
           "and that the synth takes tuning, and pressure as its own type through its physical UI mapping");
    check (synth.playsNoteExpressions (processor.getDocument().getSettings().output)
               && synth.describeConnection (processor.getDocument().getSettings().output).contains ("VST3 notes"),
           "so it plays it with VST3 note expressions, and says so");

    auto setNoteExpressions = [&] (bool on)
    {
        auto s = processor.getDocument().getSettings();
        s.output.clapNoteExpressions = on;
        processor.getDocument().setSettings (s);
    };

    // A note held for the whole clip, at a steady pressure.
    auto play = [&] (float pressure, int blocks, bool fromStart = true)
    {
        if (fromStart)
        {
            MpeClip clip;
            clip.length = 8.0;
            auto held = note (60, 0.0, 8.0, 0.8f);
            held.lanes.pressure = { { 0.0, pressure } };
            clip.addNote (held);
            processor.getDocument().replaceClip (0, clip, "Test");

            playHead.playing = false;
            double ppq = 0.0;
            runBlock (processor, playHead, ppq);
            playHead.playing = true;
            playHead.ppq = 0.0;
        }

        float peak = 0.0f;

        for (int block = 0; block < blocks; ++block)
        {
            juce::AudioBuffer<float> audio (2, 512);
            juce::MidiBuffer midi;
            processor.processBlock (audio, midi);
            playHead.ppq += 512.0 / 48000.0 * 2.0;

            if (block >= blocks / 2)
                peak = std::max (peak, audio.getMagnitude (0, 512));
        }

        return peak;
    };

    auto stopAndListen = [&]
    {
        playHead.playing = false;
        float peak = 0.0f;

        for (int block = 0; block < 20; ++block)
        {
            juce::AudioBuffer<float> audio (2, 512);
            juce::MidiBuffer midi;
            processor.processBlock (audio, midi);

            if (block >= 10)
                peak = std::max (peak, audio.getMagnitude (0, 512));
        }

        playHead.playing = true;
        return peak;
    };

    std::printf ("Each note's pressure reaches it as the synth's own pressure type\n");
    const auto soft = play (0.0f, 20), hard = play (1.0f, 20);
    std::printf ("  peak %.4f at no pressure, %.4f at full\n", soft, hard);
    check (soft > 0.01f && hard > 3.0f * soft, "a note pressed fully is about four times louder, as the synth scales it");
    check (stopAndListen() < 1.0e-4f, "and it ends when playback stops");

    std::printf ("Switching note expressions off and on leaves no note behind\n");
    setNoteExpressions (false);
    check (! synth.playsNoteExpressions (processor.getDocument().getSettings().output)
               && synth.describeConnection (processor.getDocument().getSettings().output).contains ("can't reach it one by one"),
           "off, it's played with MPE over MIDI, and MPE Roll says the notes can't be reached one by one");
    check (play (1.0f, 20) > 0.01f, "where it still plays (all at the same pitch bend)");
    setNoteExpressions (true);
    play (1.0f, 20, false);     // the note that went as MIDI is held while note expressions come back on
    check (stopAndListen() < 1.0e-4f, "a note held while note expressions came on doesn't hang on");
    play (1.0f, 20);
    setNoteExpressions (false);
    play (1.0f, 20, false);
    check (stopAndListen() < 1.0e-4f, "nor one held while they went off");
    setNoteExpressions (true);

    std::printf ("The synth's pitch-bend parameter following the notes isn't a knob tweak to the host\n");
    {
        // Analog Lab and Kontakt map each channel's pitch bend to a parameter of their own ("channel1
        // pitch bend"...), which JUCE moves with every bend MPE Roll sends. Reported to FL as a change,
        // each bend would fill its undo history and record as automation: MPE Roll tells such a
        // parameter by the synth's MIDI mapping, not by its name.
        setNoteExpressions (false);     // over MIDI, a zone bend goes to the synth's pitch bend on channel 1
        juce::AudioProcessorParameter* bendSlot = nullptr;

        for (auto* p : processor.getParameters())
            if (p->getName (100) == "channel1 pitch bend")
                bendSlot = p;

        check (bendSlot != nullptr, "the synth's channel1 pitch bend is one of MPE Roll's parameters");

        struct HeardByHost : juce::AudioProcessorParameter::Listener
        {
            void parameterValueChanged (int, float) override        { ++changes; }
            void parameterGestureChanged (int, bool) override       { ++gestures; }
            int changes = 0, gestures = 0;
        };

        HeardByHost heard;

        if (bendSlot != nullptr)
            bendSlot->addListener (&heard);

        MpeClip clip;
        clip.length = 8.0;
        clip.addNote (note (60, 0.0, 8.0, 0.8f));
        clip.zone.pitch = { { 0.0, 0.0f }, { 2.0, 1.0f } };     // the zone bends up a semitone over two beats
        processor.getDocument().replaceClip (0, clip, "Test");

        playHead.playing = false;
        double ppq = 0.0;
        runBlock (processor, playHead, ppq);
        playHead.playing = true;
        playHead.ppq = 0.0;
        play (0.0f, 40, false);

        const auto synthBend = synth.getInstance() != nullptr ? synth.getInstance()->getParameters()[0]->getValue() : 0.5f;
        std::printf ("  the synth's parameter is at %.3f; the host heard %d change(s) and %d gesture(s)\n",
                     (double) synthBend, heard.changes, heard.gestures);
        check (std::abs (synthBend - 0.5f) > 0.05f, "the synth's own parameter moved with the bend");
        check (heard.changes == 0, "and the host heard nothing of it");
        // The synth's controller echoes each move as an edit (MPEROLL_TEST_ECHO_EDITS, set at the start):
        // a host that keeps its undo history by edits would make an entry of each.
        check (heard.gestures == 0, "nor of the edits the synth's controller echoes it as");

        if (bendSlot != nullptr)
            bendSlot->removeListener (&heard);

        stopAndListen();
        setNoteExpressions (true);
    }

    std::printf ("A zone bend that went as MIDI doesn't stay on the synth once note expressions are back\n");
    {
        // A held C4 with the zone bent up a semitone. Over MIDI the synth takes the zone's bend on
        // channel 1 (its global bend); with note expressions the bend is part of each note's tuning,
        // so the global bend has to go back to rest, or the note sounds a whole tone up. The synth
        // is loaded afresh with note expressions off, so it has only ever been played over MIDI:
        // MPE Roll's own reset at the switch goes into the translator (and is dropped there), so
        // only the switch itself puts the bend back.
        setNoteExpressions (false);

        if (! loadSynth (processor, noteExpressionSynth))
            return 1;

        MpeClip clip;
        clip.length = 8.0;
        clip.addNote (note (60, 0.0, 8.0, 0.8f));
        clip.zone.pitch = { { 0.0, 1.0f } };
        processor.getDocument().replaceClip (0, clip, "Test");

        playHead.playing = false;
        double ppq = 0.0;
        runBlock (processor, playHead, ppq);
        playHead.playing = true;
        playHead.ppq = 0.0;
        play (0.0f, 20, false);
        setNoteExpressions (true);

        // 16 blocks: long enough to tell a semitone from a whole tone apart at this pitch.
        juce::AudioBuffer<float> tail (1, 16 * 512);

        for (int block = 0; block < 40; ++block)
        {
            juce::AudioBuffer<float> audio (2, 512);
            juce::MidiBuffer midi;
            processor.processBlock (audio, midi);
            playHead.ppq += 512.0 / 48000.0 * 2.0;

            if (block >= 24)
                tail.copyFrom (0, (block - 24) * 512, audio, 0, 0, 512);
        }

        const auto bentUp = goertzel (tail.getReadPointer (0), tail.getNumSamples(), juce::MidiMessage::getMidiNoteInHertz (61), 48000.0);
        const auto twice = goertzel (tail.getReadPointer (0), tail.getNumSamples(), juce::MidiMessage::getMidiNoteInHertz (62), 48000.0);
        std::printf ("  C#4 %.4f, D4 %.4f\n", bentUp, twice);
        check (bentUp > 0.005 && bentUp > 5.0 * twice, "the note sounds a semitone up (the zone's bend), not a whole tone");
        stopAndListen();
    }

    std::printf ("A VST3 synth that maps pitch bend on every channel keeps MPE over MIDI\n");

    if (! loadSynth (processor, midiSynth))
        return 1;

    std::printf ("%s\n", synth.getVst3Support().describe().toRawUTF8());
    check (synth.getVst3Support().ownBendOnMemberChannels && ! synth.getVst3Support().midiMissesNotes()
               && ! synth.playsNoteExpressions (processor.getDocument().getSettings().output),
           "it sees each channel's own pitch bend, and plays it with MPE over MIDI");

    processor.releaseResources();
    std::printf ("%s: %d failure(s)\n", check.failures == 0 ? "PASS" : "FAIL", check.failures);
    return check.failures == 0 ? 0 : 1;
}

//==============================================================================

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

// The scanner's part of the editor: the scan window, and what Load plugin file... says of FL's own plugins.
void scannerUiTest (Check& check)
{
    auto& mm = *juce::MessageManager::getInstance();
    ignoreXErrors();

    const auto scanWindows = []
    {
        int count = 0;
        auto& desktop = juce::Desktop::getInstance();

        for (int i = 0; i < desktop.getNumComponents(); ++i)
            if (auto* window = dynamic_cast<juce::DialogWindow*> (desktop.getComponent (i)))
                count += window->getName() == "MPE Roll: scan for plugins" ? 1 : 0;

        return count;
    };

    std::printf ("The scan window\n");
    {
        auto processor = std::make_unique<MpeRollProcessor>();
        processor->getSynth().showScanWindow();
        mm.runDispatchLoopUntil (100);
        check (scanWindows() == 1, "Scan for plugins... opens it");

        processor->getSynth().showScanWindow();
        mm.runDispatchLoopUntil (50);
        check (scanWindows() == 1, "and brings it to the front the second time, instead of opening another");

        // As FL deleting MPE Roll while it's open: the window scans into the synth's list and settings.
        processor.reset();
        mm.runDispatchLoopUntil (100);
        check (scanWindows() == 0, "it goes with MPE Roll, so it can't scan into a synth that's gone");
    }

    std::printf ("FL Studio's own plugins\n");
    {
        const auto temp = juce::File::getSpecialLocation (juce::File::tempDirectory);
        const auto harmor = temp.getChildFile ("Image-Line").getChildFile ("FL Studio 2024").getChildFile ("Plugins")
                                .getChildFile ("Fruity").getChildFile ("Generators").getChildFile ("Harmor").getChildFile ("Harmor_x64.dll");
        check (HostedSynth::isFlStudioNativePlugin (harmor), "a file under FL's Plugins\\Fruity folder is one");
        check (! HostedSynth::isFlStudioNativePlugin (temp.getChildFile ("VST3").getChildFile ("Surge XT.vst3")), "a synth elsewhere isn't");

        HostedSynth synth;
        juce::String error;
        bool answered = false;
        synth.loadFromFile (harmor, [&] (const juce::String& e) { error = e; answered = true; });

        for (int i = 0; i < 100 && ! answered; ++i)
            mm.runDispatchLoopUntil (50);

        check (answered && error.contains ("FL Studio's own") && error.contains ("Send to FL piano roll"),
               "Load plugin file... says only FL can load it, and points to Send to FL piano roll");
    }
}

/** The first label under a component whose text contains the given words. */
juce::Label* findLabelSaying (juce::Component& parent, const juce::String& words)
{
    for (auto* child : parent.getChildren())
    {
        if (auto* label = dynamic_cast<juce::Label*> (child); label != nullptr && label->getText().contains (words))
            return label;

        if (auto* found = findLabelSaying (*child, words))
            return found;
    }

    return nullptr;
}

/** The first component under a component with the given name (a property's is what it's called). */
juce::Component* findNamed (juce::Component& parent, const juce::String& name)
{
    for (auto* child : parent.getChildren())
    {
        if (child->getName() == name)
            return child;

        if (auto* found = findNamed (*child, name))
            return found;
    }

    return nullptr;
}

/** The alert box on screen with this title, if there's one. */
juce::AlertWindow* findAlert (const juce::String& title)
{
    auto& desktop = juce::Desktop::getInstance();

    for (int i = 0; i < desktop.getNumComponents(); ++i)
        if (auto* box = dynamic_cast<juce::AlertWindow*> (desktop.getComponent (i)); box != nullptr && box->getName() == title)
            return box;

    return nullptr;
}

/** A menu's items, top to bottom, separators left out: each one's text, and whether it can be
    chosen. (Read from the menu the editor makes rather than from one shown on screen, which closes
    itself at once on a machine where this process isn't the one in front.) */
std::vector<std::pair<juce::String, bool>> menuItems (const juce::PopupMenu& menu)
{
    std::vector<std::pair<juce::String, bool>> items;

    for (juce::PopupMenu::MenuItemIterator it (menu); it.next();)
        if (const auto& item = it.getItem(); ! item.isSeparator)
            items.emplace_back (item.text, item.isEnabled);

    return items;
}

juce::String describeMenu (const std::vector<std::pair<juce::String, bool>>& items)
{
    juce::StringArray texts;

    for (const auto& [text, enabled] : items)
        texts.add (text + (enabled ? "" : " (disabled)"));

    return texts.joinIntoString (" | ");
}

/** The address Report a bug... opens: a new issue on the repository's tracker, from the form whose
    inputs (ids build, system and synth in .github/ISSUE_TEMPLATE) the parameters fill. Built and read
    only: nothing here opens a browser. */
void bugReportChecks (Check& check)
{
    std::printf ("Report a bug: the issue form's address\n");
    const juce::String build ("MPE Roll 0.2.0, build 7, abc1234, 2026-11-03"), system ("Windows 11, in FL64.exe 26.1.7.5291, VST3");
    const auto url = BugReport::issueUrl (BugReport::bugTemplate, build, system);
    check (url.getScheme() == "https" && url.getDomain() == "github.com" && url.getSubPath() == "freener0x/MPE-Roll/issues/new",
           "is a new issue on the repository's tracker: " + url.toString (false));
    check (url.getParameterNames() == juce::StringArray ("template", "build", "system")
               && url.getParameterValues() == juce::StringArray ("bug.yml", build, system),
           "from the bug form, with the build and the system filled in");
    check (url.toString (true).contains ("?template=bug.yml&build=MPE%20Roll%200.2.0%2C%20build%207%2C%20abc1234%2C%202026-11-03&system="),
           "escaped once, spaces and commas included: " + url.toString (true));
    check (juce::URL (url.toString (true)).getParameterValues() == url.getParameterValues(), "and read back the same from the address");

    const auto synthForm = BugReport::issueUrl (BugReport::synthWontLoadTemplate, getBuildName(), BugReport::systemLine(),
                                                "Analog Lab V 5.10.1, VST2");
    check (synthForm.getParameterNames() == juce::StringArray ("template", "build", "system", "synth")
               && synthForm.getParameterValues()[0] == "synth-wont-load.yml" && synthForm.getParameterValues()[1] == getBuildName()
               && synthForm.getParameterValues()[3] == "Analog Lab V 5.10.1, VST2",
           "the form for a synth that won't load gets this build's name and the synth too");
    check (juce::String (BugReport::noSoundTemplate) == "no-sound-or-expression.yml",
           "and the one for a synth that plays wrong is no-sound-or-expression.yml");

    juce::PluginDescription synth;
    synth.name = "Analog Lab V";
    synth.version = "5.10.1";
    synth.pluginFormatName = "VST";
    check (BugReport::synthLine (synth) == "Analog Lab V 5.10.1, VST2", "a synth is named with its version and format, VST2 for JUCE's VST: "
                                                                           + BugReport::synthLine (synth));
    check (BugReport::systemLine().startsWith (juce::SystemStats::getOperatingSystemName() + ", in "),
           "and the system with the host: " + BugReport::systemLine());
}

/** The editor's UI: the glide tool dragged back from where it was pressed, the paintbrush pointer, and
    the settings call-out's Window line and lifetime. Has an editor of its own, so it can take the
    editor and the processor down with the settings open. */
void editorUiChecks (Check& check)
{
    auto& mm = *juce::MessageManager::getInstance();
    ignoreXErrors();
    juce::SharedResourcePointer<SkinManager> skins;
    const auto lookFile = juce::File::createTempFile (".settings");
    skins->useSettingsFile (lookFile);
    juce::Component::SafePointer<juce::CallOutBox> settingsBox;

    {
        MpeRollProcessor processor;
        auto& document = processor.getDocument();
        std::unique_ptr<juce::AudioProcessorEditor> editor (processor.createEditorAndMakeActive());
        editor->setSize (1200, 760);
        mm.runDispatchLoopUntil (100);

        auto* grid = findChild<NoteGrid> (*editor);
        auto* bar = findChild<Toolbar> (*editor);
        std::printf ("Glide tool, dragging back\n");
        check (grid != nullptr && bar != nullptr, "the editor has a grid and a toolbar");

        if (grid != nullptr && bar != nullptr)
        {
            const auto& view = grid->getView();
            auto at = [&] (double beat, float pitch) { return juce::Point<float> ((float) view.beatToX (beat), view.pitchToY (pitch)); };
            auto clip = [&]() -> const MpeClip& { return document.getEditClip(); };

            gesture (*grid, at (2.03, 60.0f), at (2.03, 60.0f));
            check (clip().notes.size() == 1 && clip().notes[0].lanes.pitch.isEmpty(), "a new note has no glide");

            // Pressed on the grid at beat 3, a beat into the note, and dragged back to beat 2.5 and up 2 st.
            grid->keyPressed (juce::KeyPress ('g'));
            gesture (*grid, at (2.97, 60.0f), at (2.5, 62.0f));

            if (clip().notes.size() == 1)
            {
                const auto& scoop = clip().notes[0].lanes.pitch;
                const auto& points = scoop.getPoints();
                check (points.size() == 2
                           && std::abs (points[0].beat - 0.5) < 1.0e-9 && std::abs (points[0].value - 2.0f) < 1.0e-6f
                           && std::abs (points[1].beat - 1.0) < 1.0e-9 && std::abs (points[1].value) < 1.0e-6f,
                       "dragged back from where it was pressed, a snapped glide scoops straight into the note's pitch there");
                check (std::abs (scoop.valueAt (0.0, 0.0f) - 2.0f) < 1.0e-6f && std::abs (scoop.valueAt (0.25, 0.0f) - 2.0f) < 1.0e-6f,
                       "holding the pen's pitch from the note's start rather than rising to it");
            }

            // The same drawn freely, with Alt held: the scoop comes out the same way, in the pen's own shape.
            const auto doubleClick = [&] (juce::Point<float> where)
            {
                const auto now = juce::Time::getCurrentTime();
                grid->mouseDoubleClick (juce::MouseEvent (juce::Desktop::getInstance().getMainMouseSource(), where, juce::ModifierKeys(),
                                                          1.0f, 0.0f, 0.0f, 0.0f, 0.0f, grid, grid, now, where, now, 2, false));
            };
            const juce::ModifierKeys alt (juce::ModifierKeys::leftButtonModifier | juce::ModifierKeys::altModifier);

            doubleClick (at (2.5, 60.0f));
            check (clip().notes.size() == 1 && clip().notes[0].lanes.pitch.isEmpty(), "double-click clears the glide");
            gesture (*grid, at (2.97, 60.0f), at (2.5, 62.0f), alt);

            if (clip().notes.size() == 1)
            {
                const auto& scoop = clip().notes[0].lanes.pitch;
                const auto& points = scoop.getPoints();
                check (points.size() >= 2
                           && std::abs (points.front().beat - 0.5) < 1.0e-4 && std::abs (points.front().value - 2.0f) < 1.0e-3f
                           && std::abs (points.back().beat - 0.97) < 1.0e-4 && std::abs (points.back().value) < 1.0e-3f,
                       "drawn freely with Alt held, a glide dragged back scoops into the note's pitch where it was pressed");
                check (std::abs (scoop.valueAt (0.0, 0.0f) - 2.0f) < 1.0e-3f && std::abs (scoop.valueAt (0.25, 0.0f) - 2.0f) < 1.0e-3f,
                       "holding the pen's pitch from the note's start");
            }

            // Drawn forwards from inside the note, the note's own pitch is held up to where it was pressed.
            doubleClick (at (2.5, 60.0f));
            gesture (*grid, at (2.5, 60.4f), at (2.97, 62.0f), alt);

            if (clip().notes.size() == 1)
            {
                const auto& glide = clip().notes[0].lanes.pitch;
                check (! glide.isEmpty() && std::abs (glide.valueAt (0.0, 9.0f)) < 1.0e-3f && std::abs (glide.valueAt (0.5, 9.0f) - 0.4f) < 1.0e-3f
                           && std::abs (glide.valueAt (0.97, 0.0f) - 2.0f) < 1.0e-3f,
                       "drawn forwards from inside the note, it leaves the note's own pitch, held from the start");
            }

            // Pressed 0.4 st above the note's pitch and let go without moving, or slipping a pixel either
            // way as clicks do: the point under the pen would otherwise be the whole note's pitch.
            for (const int slip : { 0, 1, -1 })
            {
                doubleClick (at (2.5, 60.0f));
                gesture (*grid, at (2.5, 60.4f), at (2.5, 60.4f).translated ((float) slip, 0.0f), alt);

                if (clip().notes.size() == 1)
                    check (clip().notes[0].lanes.pitch.isEmpty(),
                           "an Alt-click on a note without a glide draws nothing, leaving the note at its own pitch"
                               + (slip == 0 ? juce::String() : " (slipping " + juce::String (slip) + " px)"));
            }

            std::printf ("Paint tool pointer\n");
            {
                grid->keyPressed (juce::KeyPress ('b'));
                const auto now = juce::Time::getCurrentTime();
                const juce::MouseEvent hover (juce::Desktop::getInstance().getMainMouseSource(), at (5.0, 60.0f), juce::ModifierKeys(),
                                              1.0f, 0.0f, 0.0f, 0.0f, 0.0f, grid, grid, now, at (5.0, 60.0f), now, 0, false);
                grid->mouseMove (hover);
                const auto brush = grid->getMouseCursor();
                check (brush != juce::MouseCursor::NormalCursor, "the Paint tool has a pointer of its own");

                // A skin other than the one showing (a look never chosen, as here, is FL Studio's).
                skins->setPreset (skins->getPreset() == 0 ? 1 : 0);
                mm.runDispatchLoopUntil (50);
                grid->mouseMove (hover);
                check (grid->getMouseCursor() != brush, "which is drawn again in a new skin's colours");
                grid->keyPressed (juce::KeyPress ('p'));
            }

            editor->addToDesktop (0);
            editor->setVisible (true);
            mm.runDispatchLoopUntil (200);

            // The menus the File and Load synth buttons show. Their Report items open a web browser,
            // so they're only looked at here, never chosen.
            std::printf ("The File and Load synth menus\n");
            auto* rollEditor = dynamic_cast<MpeRollEditor*> (editor.get());
            check (rollEditor != nullptr, "the editor is MPE Roll's");

            if (rollEditor != nullptr)
            {
                auto items = menuItems (rollEditor->makeMidiMenu());
                check (items.size() >= 2 && items.back().first == getBuildName() && ! items.back().second
                           && items[items.size() - 2].first == "Report a bug..." && items[items.size() - 2].second,
                       "the File menu ends with Report a bug..., then the build's name: " + describeMenu (items));
                check (std::none_of (items.begin(), items.end(), [] (const auto& item) { return item.first.containsIgnoreCase ("donate"); }),
                       "and has no donation item: that's at the foot of MPE settings");

                items = menuItems (rollEditor->makeSynthMenu());
                check (items.size() >= 2 && items.back().first == "Report a synth that won't load..."
                           && items[items.size() - 2].first.startsWith ("Show the plugin log"),
                       "with no synth loaded, the Load synth menu ends with Report a synth that won't load...: " + describeMenu (items));
            }

            // The File button shows that menu (closed again unchecked: on screen it can't be read
            // everywhere, as above).
            bar->midiButton.triggerClick();
            mm.runDispatchLoopUntil (100);
            juce::PopupMenu::dismissAllActiveMenus();
            mm.runDispatchLoopUntil (100);

           #if ! JUCE_WINDOWS
            // Send to FL piano roll, with FL's Settings folder on the computer but no scripts folder in
            // it yet: the script goes straight in, with no dialog. In a home folder of the test's own, where
            // Documents (and FL's folder in it) is looked for; on Windows the system says where Documents
            // is, and a missing folder would open the system's save dialog, so this runs elsewhere only.
            std::printf ("Send to FL piano roll\n");
            {
                const juce::String realHome (std::getenv ("HOME"));
                const auto home = juce::File::createTempFile ("home");
                const auto scripts = home.getChildFile ("Documents").getChildFile ("Image-Line").getChildFile ("FL Studio")
                                         .getChildFile ("Settings").getChildFile ("Piano roll scripts");
                scripts.getParentDirectory().createDirectory();
                setenv ("HOME", home.getFullPathName().toRawUTF8(), 1);
                check (FlScriptExporter::defaultScriptFolder() == scripts,
                       "FL's scripts folder is looked for in FL's user data folder: " + scripts.getFullPathName());

                // The box saying where the script went has no callback, so with modal loops allowed, as
                // here, it runs modally: it's read and closed from inside the loop it runs.
                juce::String message;
                juce::Timer::callAfterDelay (300, [&message]
                {
                    if (auto* box = findAlert ("Sent to FL piano roll"))
                    {
                        message = box->getDescription();
                        box->exitModalState (0);
                    }
                });

                bar->flButton.triggerClick();
                mm.runDispatchLoopUntil (1000);

                const auto written = scripts.getChildFile (FlScriptExporter::defaultScriptFileName);
                check (written.existsAsFile() && written.loadFileAsString().startsWith ("# Generated by MPE Roll"),
                       "the script is written there, the scripts folder made, with no dialog");
                check (message.contains ("into FL's scripts folder:\n" + scripts.getFullPathName())
                           && message.contains ("open the piano roll menu (top left) > Tools, and run \"MPE Roll Import\"")
                           && message.contains ("FL Studio 21.1 or newer") && message.contains ("Ctrl+Alt+Y"),
                       "and the box names the folder and where FL runs it");
                check (bar->status.getText() == "FL script written: MPE Roll Import.pyscript. In FL: piano roll menu > Tools > MPE Roll Import",
                       "as does the status line: " + bar->status.getText());

                setenv ("HOME", realHome.toRawUTF8(), 1);
                home.deleteRecursively();
            }
           #endif

            std::printf ("The settings call-out\n");
            bar->settingsButton.triggerClick();
            mm.runDispatchLoopUntil (50);
            settingsBox = findChild<juce::CallOutBox> (*editor);
            check (settingsBox != nullptr, "the MPE settings button opens them in a call-out");

            if (auto* box = settingsBox.getComponent())
            {
                check (findLabelSaying (*box, "scale, drawn by") != nullptr,
                       "whose Window line says how the window is drawn from the moment it opens");

                auto* panel = findChild<SettingsPanel> (*box);
                auto* link = panel != nullptr ? findChild<juce::HyperlinkButton> (*panel) : nullptr;
                check (link != nullptr && link->isShowing() && panel->getLocalBounds().contains (link->getBounds()),
                       "a Support MPE Roll link is in view at the foot of the settings");

                // Its words fit, in this system's font: a link of the same height sized to them is no wider.
                if (link != nullptr)
                {
                    juce::HyperlinkButton sized (link->getButtonText(), {});
                    sized.setSize (100, link->getHeight());
                    sized.changeWidthToFitText();
                    check (link->getWidth() >= sized.getWidth(),
                           "with room for all its words (" + juce::String (link->getWidth()) + " px for "
                               + juce::String (sized.getWidth()) + ")");
                }

                // Beside it, in the dim text colour so it asks for no attention, the website's share page.
                // A web address, so it isn't clicked here.
                juce::HyperlinkButton* share = nullptr;

                if (panel != nullptr)
                    for (auto* child : panel->getChildren())
                        if (auto* other = dynamic_cast<juce::HyperlinkButton*> (child); other != nullptr && other != link)
                            share = other;

                check (share != nullptr && share->getButtonText() == "Share MPE Roll" && share->isShowing()
                           && panel->getLocalBounds().contains (share->getBounds())
                           && link != nullptr && share->getX() >= link->getRight(),
                       "and right of it, a Share MPE Roll link");

                if (share != nullptr)
                {
                    check (share->getURL().toString (true) == links::share && share->getURL().isWellFormed(),
                           "which leads to the website's share page: " + share->getURL().toString (true));
                    check (share->findColour (juce::HyperlinkButton::textColourId) == Theme::textDim
                               && share->getTooltip() == "Opens the website's share page: ready-made posts for X, Instagram and Facebook",
                           "in the dim text colour, its tooltip saying where it leads");

                    juce::HyperlinkButton sized (share->getButtonText(), {});
                    sized.setSize (100, share->getHeight());
                    sized.changeWidthToFitText();
                    check (share->getWidth() >= sized.getWidth(), "with room for all its words");
                }

                // So a tester can tell which build their DAW loaded: all of its name, which is no use cut
                // short, beside the links or on a line of its own above them.
                auto* build = panel != nullptr ? findLabelSaying (*panel, getBuildName()) : nullptr;
                check (build != nullptr && build->isShowing() && panel->getLocalBounds().contains (build->getBounds())
                           && ! build->getBounds().intersects (link != nullptr ? link->getBounds() : juce::Rectangle<int>())
                           && ! build->getBounds().intersects (share != nullptr ? share->getBounds() : juce::Rectangle<int>()),
                       "and beside them, which build this is: " + getBuildName());

                if (build != nullptr)
                {
                    const auto needed = juce::GlyphArrangement::getStringWidth (build->getFont(), build->getText())
                                      + (float) build->getBorderSize().getLeftAndRight();
                    check ((float) build->getWidth() >= needed,
                           "with room for all of it (" + juce::String (build->getWidth()) + " px for "
                               + juce::String (juce::roundToInt (std::ceil (needed))) + ")");
                }

                // Follow FL gated by FL's notes, in Playback: its tooltip shows over the switch too, which
                // takes up most of the row, and the switch sets it.
                auto* gateRow = panel != nullptr ? findNamed (*panel, "Follow FL only while FL holds a note") : nullptr;
                auto* gate = dynamic_cast<juce::BooleanPropertyComponent*> (gateRow);
                auto* gateSwitch = gate != nullptr ? findChild<juce::ToggleButton> (*gate) : nullptr;
                check (gateSwitch != nullptr, "Playback has Follow FL only while FL holds a note");

                if (gateSwitch != nullptr)
                {
                    const auto tooltip = gate->getTooltip();
                    check (tooltip.startsWith ("Put a note under MPE Roll's channel in FL's piano roll or playlist")
                               && tooltip.endsWith ("Those notes only open the gate: they aren't played.")
                               && gateSwitch->getTooltip() == tooltip,
                           "with its tooltip over its name and its switch");

                    const auto wasOn = document.getSettings().playback.gateByHostNotes;
                    gateSwitch->triggerClick();
                    mm.runDispatchLoopUntil (50);
                    check (! wasOn && document.getSettings().playback.gateByHostNotes, "off at first, and the switch turns it on");
                    gateSwitch->triggerClick();
                    mm.runDispatchLoopUntil (50);
                    check (! document.getSettings().playback.gateByHostNotes, "and off again");
                }

                // It opens the donation details in MPE Roll itself, not a web page that may not be there.
                if (link != nullptr)
                {
                    check (! link->getURL().isWellFormed(), "and leads to no web address");
                    link->triggerClick();
                    mm.runDispatchLoopUntil (100);
                }

                juce::DialogWindow* support = nullptr;

                for (int i = juce::Desktop::getInstance().getNumComponents(); --i >= 0;)
                    if (auto* window = dynamic_cast<juce::DialogWindow*> (juce::Desktop::getInstance().getComponent (i)))
                        if (window->getName() == "Support MPE Roll")
                            support = window;

                auto* donatePanel = support != nullptr ? dynamic_cast<DonatePanel*> (support->getContentComponent()) : nullptr;
                check (donatePanel != nullptr, "but opens a Support MPE Roll window of its own");

                if (donatePanel != nullptr)
                {
                    check (donatePanel->getNumQrCodes() == 4, "with the four QR codes");
                    check (donatePanel->getAddress ("BTC") == "bc1qw07yj62hae5nflfz6pwt8yyr57377adkclsy3t"
                               && donatePanel->getAddress ("XMR").startsWith ("84L4Twd2DcNjHevR6qUzz"),
                           "and the addresses from docs/donate.json");

                    if (auto* copy = donatePanel->getCopyButton ("ETH"))
                    {
                        copy->triggerClick();       // posted, so let it through
                        juce::MessageManager::getInstance()->runDispatchLoopUntil (50);
                        check (juce::SystemClipboard::getTextFromClipboard() == "0x79Ff39eCD0D2520c5353e011ac1317C51dc2AC3A"
                                   && copy->getButtonText().startsWith ("Copied"),
                               "whose Copy button puts the address on the clipboard");
                    }

                    const auto work = WindowPlacement::workAreaNear (editor.get());
                    const auto outer = WindowPlacement::frameOf (*support).addedTo (support->getBounds());
                    check (work.contains (outer.getTopLeft()), "and which opens with its title bar on screen");
                }

                delete support;
            }

            // As FL closing the window and then the plugin with the settings open, while a skin change
            // (from another MPE Roll's settings) is still on its way: the call-out is deleted later, by
            // the message loop, and must no longer reach the processor.
            skins->setPreset (2);
            editor->removeFromDesktop();
        }

        processor.editorBeingDeleted (editor.get());
        editor.reset();
    }

    mm.runDispatchLoopUntil (100);
    check (settingsBox == nullptr, "closing the window and the plugin with the settings open is safe, and the call-out goes");

    skins->setPreset (0);
    skins->flush();
    mm.runDispatchLoopUntil (50);
    lookFile.deleteFile();
}

// What MPE Roll says about the MPE of synths it knows, matched on their names and makers.
void synthProfileChecks (Check& check)
{
    std::printf ("Known synths are recognised, with where their MPE switch is\n");

    const auto find = [] (const char* name, const char* maker, const char* version = "")
    {
        juce::PluginDescription d;
        d.name = name;
        d.manufacturerName = maker;
        d.version = version;
        return SynthProfile::find (d);
    };

    using Mpe = SynthProfile::Mpe;
    const auto serum = find ("Serum", "Xfer Records"), serum2 = find ("Serum2", "Xfer Records");
    check (serum.has_value() && serum->name == "Serum" && serum->mpe == Mpe::needsSwitch && serum->advice.contains ("MPE Enabled")
               && serum2.has_value() && serum2->name == "Serum 2" && ! find ("Serum FX", "Xfer Records").has_value(),
           "Serum and Serum 2, with their MPE Enabled switch, and not Serum FX");

    const auto analogLab = find ("Analog Lab V", "Arturia"), zenology = find ("ZENOLOGY", "Roland");
    check (analogLab.has_value() && analogLab->mpe == Mpe::none && analogLab->advice.contains ("Pigments")
               && analogLab->advice.contains ("Plain MIDI") && zenology.has_value() && zenology->mpe == Mpe::none,
           "Analog Lab and ZENOLOGY as having no MPE, with Plain MIDI to use instead");

    const auto omni2 = find ("Omnisphere", "Spectrasonics", "2.8.6f"), omni3 = find ("Omnisphere", "Spectrasonics", "3.0.1c");
    check (omni2.has_value() && omni2->mpe == Mpe::none && omni3.has_value() && omni3->mpe == Mpe::needsSwitch
               && omni3->advice.contains ("System page"),
           "Omnisphere 2 without MPE, and 3 with it on its System page");

    check (find ("Vital", "Vital Audio").has_value() && find ("Surge XT", "Surge Synth Team")->mpe == Mpe::followsSetup
               && ! find ("MPE Roll Test Synth", "Glowing Pancake").has_value(),
           "Vital and Surge XT, and nothing for a synth it doesn't know");

    // What the list says for any instrument of a maker goes by the instrument's own name; and a maker
    // matched exactly ("Waves") isn't one whose name merely starts the same.
    const auto newArturia = find ("Brand New V", "Arturia"), waves = find ("Some Synth", "Waves");
    const auto cherry = find ("CA2600", "Cherry Audio");
    check (newArturia.has_value() && newArturia->name == "Brand New V" && ! find ("Brand New V", "").has_value()
               && cherry.has_value() && cherry->name == "CA2600" && cherry->mpe == Mpe::none
               && waves.has_value() && waves->mpe == Mpe::none && ! find ("Some Synth", "Wavesfactory").has_value(),
           "a maker's whole range by the synth's own name, and exact makers");

    // From the audit (docs/MPE_SYNTHS.md): an MPE switch, one whose maker never mentions MPE, and
    // names that start the same.
    const auto icarus = find ("Icarus", "Tone2"), one = find ("FabFilter One", "FabFilter"), nexus = find ("Nexus", "reFX");
    const auto massive = find ("Massive", "Native Instruments"), massiveX = find ("Massive X", "Native Instruments");
    check (icarus.has_value() && icarus->mpe == Mpe::needsSwitch && icarus->advice.containsIgnoreCase ("force MPE")
               && one.has_value() && one->mpe == Mpe::none && ! one->certain && one->describeNone().startsWith ("most likely")
               && nexus.has_value() && nexus->name == "Nexus" && ! nexus->certain
               && massive.has_value() && massive->name == "Massive" && massiveX.has_value() && massiveX->name == "Massive X",
           "audited synths: Icarus's switch, 'most likely no MPE' for FabFilter One and Nexus, Massive vs Massive X");
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

    // Off the grid and between semitones on purpose: the glide still lands on beat 4.25 and +3.
    grid->mouseDoubleClick (juce::MouseEvent (juce::Desktop::getInstance().getMainMouseSource(), at (3.5, 62.0f),
                                              juce::ModifierKeys(), 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, grid, grid,
                                              juce::Time::getCurrentTime(), at (3.5, 62.0f), juce::Time::getCurrentTime(), 2, false));
    gesture (*grid, at (3.6, 62.0f), at (4.37, 64.6f));
    {
        const auto& points = clip().notes[0].lanes.pitch.getPoints();
        bool onGrid = ! points.empty();

        for (const auto& p : points)
            onGrid = onGrid && std::abs (p.beat * 4.0 - std::round (p.beat * 4.0)) < 1.0e-9
                            && std::abs (p.value - std::round (p.value)) < 1.0e-6f;

        const auto& snapped = clip().notes[0].lanes.pitch;
        check (onGrid && std::abs (snapped.valueAt (0.5, 9.0f)) < 1.0e-6f && std::abs (snapped.valueAt (1.25, 0.0f) - 3.0f) < 1.0e-6f
                   && std::abs (snapped.valueAt (2.0, 0.0f) - 3.0f) < 1.0e-6f,
               "a snapped glide runs straight from the grid to a whole semitone on the grid");
    }

    gesture (*grid, at (3.0, 62.0f), at (4.9, 63.6f),
             juce::ModifierKeys (juce::ModifierKeys::leftButtonModifier | juce::ModifierKeys::altModifier), 20);
    {
        bool between = false;

        for (const auto& p : clip().notes[0].lanes.pitch.getPoints())
            between = between || std::abs (p.value - std::round (p.value)) > 0.1f;

        check (between, "with Alt held, it's drawn freely");
    }

    auto* bar = findChild<Toolbar> (*editor);
    check (bar != nullptr && bar->glideSnapButton.getToggleState(), "Snap glide is on to start with");

    if (bar != nullptr)
    {
        bar->glideSnapButton.triggerClick();
        juce::MessageManager::getInstance()->runDispatchLoopUntil (50);
        check (! view.snapGlide, "and the toolbar turns it off");
        bar->glideSnapButton.triggerClick();
        juce::MessageManager::getInstance()->runDispatchLoopUntil (50);
    }

    std::printf ("Paint tool\n");
    {
        // The last note placed is 2 beats long, so a drag from beat 8.1 to 13.9 paints at 8, 10 and 12.
        grid->keyPressed (juce::KeyPress ('b'));
        check (view.tool == ViewState::Tool::paint, "B picks the Paint tool");

        const auto before = clip().notes.size();
        const auto left = juce::ModifierKeys (juce::ModifierKeys::leftButtonModifier);
        gesture (*grid, at (8.1, 72.0f), at (13.9, 72.0f), left, 12);

        std::vector<double> starts;

        for (const auto& n : clip().notes)
            if (n.key == 72)
                starts.push_back (n.start);

        check (clip().notes.size() == before + 3 && (starts == std::vector<double> { 8.0, 10.0, 12.0 }),
               "dragging paints snapped notes one after another");
        check (std::all_of (clip().notes.begin(), clip().notes.end(),
                            [] (const MpeNote& n) { return n.key != 72 || std::abs (n.length - 2.0) < 1.0e-9; }),
               "each as long as the last note placed");

        gesture (*grid, at (13.9, 72.0f), at (8.1, 72.0f), left, 12);
        check (clip().notes.size() == before + 3, "painting back over them adds nothing (and leaves nothing to undo)");

        document.getUndoManager().undo();
        check (clip().notes.size() == before, "one undo takes the whole stroke away");
    }

    grid->keyPressed (juce::KeyPress ('p'));

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

        lane->setTarget (ExpressionLane::Target::noteVibrato);
        gesture (*lane, laneAt (3.0, 0.6f), laneAt (5.0, 0.6f), juce::ModifierKeys (juce::ModifierKeys::leftButtonModifier), 40);
        const auto& vibrato = clip().notes[0].lanes.vibrato;
        check (! vibrato.isEmpty() && std::abs (vibrato.valueAt (1.0, 0.0f) - 0.6f) < 0.03f, "the vibrato lane draws depth onto the note");

        if (auto* rate = findChild<juce::Slider> (*lane))
        {
            rate->setValue (7.0, juce::sendNotificationSync);
            check (std::abs (clip().vibratoRate - 7.0) < 1.0e-9, "its speed is set next to the lane menu");
            document.getUndoManager().undo();
            check (std::abs (clip().vibratoRate - defaultVibratoRate) < 1.0e-9, "and can be undone");
        }
        else
        {
            check (false, "the vibrato lane has a speed control");
        }

        lane->setTarget (ExpressionLane::Target::notePressure);
    }

    std::printf ("Selection, clipboard and undo\n");
    {
        grid->keyPressed (juce::KeyPress ('a', juce::ModifierKeys::commandModifier, 0));
        grid->keyPressed (juce::KeyPress ('b', juce::ModifierKeys::commandModifier, 0));
        check (clip().notes.size() == 2 && std::abs (clip().notes[1].start - 5.0) < 1.0e-9, "Ctrl+B duplicates after the selection (FL's key)");

        grid->keyPressed (juce::KeyPress (juce::KeyPress::upKey));
        check (clip().notes[1].key == 63, "Up transposes the selection");

        gesture (*grid, at (3.5, 62.0f), at (3.5, 62.0f), right);
        check (clip().notes.size() == 1, "right-click deletes a note");

        document.getUndoManager().undo();
        check (clip().notes.size() == 2, "undo brings it back");
    }

    std::printf ("FL's tools and keys\n");
    {
        auto& mm = *juce::MessageManager::getInstance();
        ignoreXErrors();

        // A known clip: a C major chord two beats long at beat 8, and a lone note at beat 12 that
        // glides up two semitones.
        document.perform ("Test notes", [] (MpeClip& c)
        {
            c.notes.clear();

            for (int key : { 60, 64, 67 })
            {
                MpeNote n;
                n.key = key;
                n.start = 8.0;
                n.length = 2.0;
                c.addNote (n);
            }

            MpeNote lone;
            lone.key = 72;
            lone.start = 12.0;
            lone.length = 1.0;
            lone.lanes.pitch = ExpressionCurve ({ { 0.0, 0.0f }, { 1.0, 2.0f } });
            c.addNote (lone);
        });
        document.selection.clear();
        document.selectionChanged();

        // Notes are kept sorted by start, so they're found by key here.
        auto noteWithKey = [&] (int key) -> const MpeNote*
        {
            for (const auto& n : clip().notes)
                if (n.key == key)
                    return &n;

            return nullptr;
        };

        // FL's two quick quantizes: Shift+Q lands starts on the grid, Ctrl+Q ends as well.
        document.perform ("Off the grid", [] (MpeClip& c)
        {
            for (auto& n : c.notes)
                if (n.key == 60)
                {
                    n.start = 8.1;
                    n.length = 1.9;
                }
        });
        grid->keyPressed (juce::KeyPress ('q', juce::ModifierKeys::shiftModifier, 0));
        check (noteWithKey (60) != nullptr && std::abs (noteWithKey (60)->start - 8.0) < 1.0e-9
                   && std::abs (noteWithKey (60)->length - 1.9) < 1.0e-9,
               "Shift+Q quantizes starts, leaving lengths alone");
        editor->keyPressed (juce::KeyPress ('q', juce::ModifierKeys::commandModifier, 0));
        check (noteWithKey (60) != nullptr && std::abs (noteWithKey (60)->start - 8.0) < 1.0e-9
                   && std::abs (noteWithKey (60)->length - 2.0) < 1.0e-9,
               "Ctrl+Q quantizes ends too, as FL's quick quantize does");

        grid->keyPressed (juce::KeyPress ('c'));
        check (view.tool == ViewState::Tool::slice, "C picks the Slice tool, as in FL");
        grid->keyPressed (juce::KeyPress ('d'));
        check (view.tool == ViewState::Tool::erase, "D the Delete tool");
        grid->keyPressed (juce::KeyPress ('e'));
        check (view.tool == ViewState::Tool::select, "E the Select tool");
        grid->keyPressed (juce::KeyPress ('b'));
        check (view.tool == ViewState::Tool::paint, "B the Paint tool");
        grid->keyPressed (juce::KeyPress ('p'));
        check (view.tool == ViewState::Tool::edit, "P the Draw tool");

        check (bar != nullptr && bar->drawToolButton.getToggleState() && ! bar->sliceToolButton.getToggleState(),
               "and the toolbar's icon buttons follow");

        // Slice: a click cuts the lone note at the snapped beat (12.53 lands on 12.5), the glide
        // carrying on across the cut.
        grid->keyPressed (juce::KeyPress ('c'));
        gesture (*grid, at (12.53, 72.0f), at (12.53, 72.0f));
        {
            std::vector<const MpeNote*> pieces;

            for (const auto& n : clip().notes)
                if (n.key == 72)
                    pieces.push_back (&n);

            check (pieces.size() == 2 && std::abs (pieces[0]->length - 0.5) < 1.0e-9
                       && std::abs (pieces[1]->start - 12.5) < 1.0e-9 && std::abs (pieces[1]->length - 0.5) < 1.0e-9,
                   "a click with the Slice tool cuts the note in two at the snapped beat");
            check (pieces.size() == 2
                       && std::abs (pieces[0]->lanes.pitch.valueAt (0.5, 0.0f) - 1.0f) < 1.0e-5f
                       && std::abs (pieces[1]->lanes.pitch.valueAt (0.0, 0.0f) - 1.0f) < 1.0e-5f
                       && std::abs (pieces[1]->lanes.pitch.valueAt (0.5, 0.0f) - 2.0f) < 1.0e-5f,
                   "and the glide carries on across the cut");
        }

        gesture (*grid, at (9.02, 68.0f), at (9.02, 59.0f));
        check (clip().notes.size() == 8, "dragging a slice line down across the chord cuts every note it crosses");
        document.getUndoManager().undo();
        check (clip().notes.size() == 5, "as one undo step");

        grid->keyPressed (juce::KeyPress ('d'));
        gesture (*grid, at (12.2, 72.0f), at (12.2, 72.0f));
        check (clip().notes.size() == 4, "the Delete tool removes the note under a click");

        grid->keyPressed (juce::KeyPress ('e'));
        gesture (*grid, at (7.5, 69.0f), at (10.5, 58.0f));
        check (document.selection.size() == 3, "the Select tool's box selects the chord");
        gesture (*grid, at (12.7, 72.0f), at (12.7, 72.0f));
        check (document.selection.size() == 1 && clip().findNote (*document.selection.begin()) != nullptr
                   && clip().findNote (*document.selection.begin())->key == 72,
               "a click selects just that note");
        gesture (*grid, at (14.0, 50.0f), at (14.0, 50.0f));
        check (document.selection.empty(), "and a click on empty space selects nothing");

        grid->keyPressed (juce::KeyPress ('i', juce::ModifierKeys::shiftModifier, 0));
        check (document.selection.size() == 4, "Shift+I inverts the selection, as in FL");
        grid->keyPressed (juce::KeyPress (juce::KeyPress::upKey, juce::ModifierKeys::commandModifier, 0));
        check (noteWithKey (60) == nullptr && noteWithKey (76) != nullptr && noteWithKey (84) != nullptr,
               "Ctrl+Up transposes an octave, as in FL");
        document.getUndoManager().undo();
        grid->keyPressed (juce::KeyPress ('i', juce::ModifierKeys::shiftModifier, 0));
        check (document.selection.empty(), "and again puts it back");

        // The Tools' keys reach the editor. Nothing selected: every note is worked on, as in FL.
        editor->keyPressed (juce::KeyPress ('u', juce::ModifierKeys::commandModifier, 0));
        check (clip().notes.size() == 3 * 8 + 2, "Ctrl+U quick-chops every note at the grid when nothing is selected");
        document.getUndoManager().undo();
        check (clip().notes.size() == 4, "in one undo step");

        editor->keyPressed (juce::KeyPress ('l', juce::ModifierKeys::commandModifier, 0));
        check (std::all_of (clip().notes.begin(), clip().notes.end(),
                            [] (const MpeNote& n) { return n.key == 72 || std::abs (n.length - 4.5) < 1.0e-9; }),
               "Ctrl+L quick legato lengthens the chord to where the next note starts");
        document.getUndoManager().undo();

        // Alt+A opens the Arpeggiate dialog, which previews on the clip: Escape puts the notes
        // back and leaves nothing to undo, Return keeps the result as one undo step.
        editor->addToDesktop (0);
        editor->setVisible (true);
        mm.runDispatchLoopUntil (100);
        const auto undoBefore = document.getUndoManager().getUndoDescription();

        auto openDialog = [&]() -> NoteToolDialog*
        {
            editor->keyPressed (juce::KeyPress ('a', juce::ModifierKeys::altModifier, 0));
            mm.runDispatchLoopUntil (100);
            auto* box = findChild<juce::CallOutBox> (*editor);
            return box != nullptr ? findChild<NoteToolDialog> (*box) : nullptr;
        };

        auto* dialog = openDialog();
        check (dialog != nullptr, "Alt+A opens the Arpeggiate dialog in a call-out");
        check (clip().notes.size() == 10, "which previews the arpeggio on the clip: eight steps for the chord, two for the lone note");

        if (dialog != nullptr)
        {
            dialog->keyPressed (juce::KeyPress (juce::KeyPress::escapeKey));
            mm.runDispatchLoopUntil (100);
        }

        check (clip().notes.size() == 4 && document.getUndoManager().getUndoDescription() == undoBefore,
               "Escape puts the notes back and leaves nothing to undo");

        dialog = openDialog();

        if (dialog != nullptr)
        {
            dialog->keyPressed (juce::KeyPress (juce::KeyPress::returnKey));
            mm.runDispatchLoopUntil (100);
        }

        check (clip().notes.size() == 10 && document.getUndoManager().getUndoDescription() == "Arpeggiate",
               "Return keeps the arpeggio as one undo step");
        document.getUndoManager().undo();
        check (clip().notes.size() == 4, "which undoes it whole");
        editor->removeFromDesktop();
        grid->keyPressed (juce::KeyPress ('p'));
    }

    std::printf ("Windows are placed on screen\n");
    {
        // A synth window taller than the screen, as the user saw: 1000 x 1400 on a 1920 x 1040 work
        // area under a 31-pixel title bar.
        const juce::Rectangle<int> work (0, 0, 1920, 1040), tall (0, 0, 1000, 1400);
        const juce::BorderSize<int> frame (31, 8, 8, 8);

        const auto fixed = WindowPlacement::place (tall, frame, work, false, true);
        check (fixed.getY() == 31 && fixed.getWidth() == 1000 && fixed.getHeight() == 1400 && fixed.getX() == 460,
               "a window too tall to fit keeps its size, with its title bar at the top of the screen");

        const auto shrunk = WindowPlacement::place (tall, frame, work, true, true);
        check (shrunk.getY() == 31 && shrunk.getBottom() == 1040 - 8 && shrunk.getWidth() == 1000,
               "one that may be resized is shrunk to fit");

        const auto second = WindowPlacement::place ({ 0, 0, 800, 600 }, frame, { 1920, 0, 1280, 984 }, false, true);
        check (second.getX() == 1920 + 240 && second.getY() == 31 + (984 - 31 - 8 - 600) / 2,
               "and a window is centred on the display it's put on, not the main one");

        const auto off = WindowPlacement::place ({ -300, -200, 800, 600 }, frame, work, false, false);
        check (off.getX() == 8 && off.getY() == 31, "one gone off the top left is brought back inside");
    }

    std::printf ("The editor's window\n");
    {
        auto& mm = *juce::MessageManager::getInstance();
        ignoreXErrors();

        auto openWindow = [&]
        {
            editor->addToDesktop (0);
            editor->setVisible (true);
            mm.runDispatchLoopUntil (200);
        };

        openWindow();
        check (WindowDrawing::describe (*editor).contains ("scale, drawn by"), "Settings can say how the window is drawn");
        check (! WindowDrawing::resync (*editor), "a window JUCE knows the size of is left alone");

        // As a host closing the plugin window and opening a new one.
        editor->removeFromDesktop();
        mm.runDispatchLoopUntil (100);
        openWindow();

        // Through the window, as the mouse reaches the editor: it lands where it was clicked.
        if (auto* peer = editor->getPeer())
        {
            const auto where = grid->getPosition().toFloat() + at (0.53, 67.0f);
            const auto source = juce::MouseInputSource::InputSourceType::mouse;
            const auto down = juce::ModifierKeys (juce::ModifierKeys::leftButtonModifier);
            const auto time = juce::Time::currentTimeMillis();
            peer->handleMouseEvent (source, where, {}, juce::MouseInputSource::defaultPressure, 0.0f, time);
            peer->handleMouseEvent (source, where, down, juce::MouseInputSource::defaultPressure, 0.0f, time + 10);
            peer->handleMouseEvent (source, where, {}, juce::MouseInputSource::defaultPressure, 0.0f, time + 20);
        }

        const auto landed = std::any_of (clip().notes.begin(), clip().notes.end(), [] (const MpeNote& n)
                                         { return n.key == 67 && std::abs (n.start - 0.5) < 1.0e-9; });
        check (landed, "after the window is reopened, a click adds its note under the mouse");

        if (WindowDrawing::isChoiceAvailable())
        {
            WindowDrawing::apply (*editor, WindowDrawing::Mode::software);
            mm.runDispatchLoopUntil (100);
            check (WindowDrawing::describe (*editor).contains ("Software Renderer"), "the window can be drawn in software");
            WindowDrawing::apply (*editor, WindowDrawing::Mode::gpu);
            mm.runDispatchLoopUntil (100);
            check (WindowDrawing::describe (*editor).contains ("Direct2D"), "and back with Direct2D");
        }

        auto settingsFile = juce::File::createTempFile (".settings");
        {
            juce::PropertiesFile settings (settingsFile, {});
            check (WindowDrawing::getMode (&settings) == WindowDrawing::Mode::automatic, "window drawing starts on Automatic");
            WindowDrawing::setMode (&settings, WindowDrawing::Mode::software);
            check (WindowDrawing::getMode (&settings) == WindowDrawing::Mode::software, "and remembers a choice");
        }
        settingsFile.deleteFile();

        editor->removeFromDesktop();
    }

    std::printf ("Skins\n");
    {
        auto& mm = *juce::MessageManager::getInstance();
        juce::SharedResourcePointer<SkinManager> skins;
        const auto lookFile = juce::File::createTempFile (".settings");
        skins->useSettingsFile (lookFile);

        const auto& presets = presetSkins();
        check (presets.size() == 10, "there are ten skins");
        check (presets[(size_t) skins->getPreset()].name == "FL Studio", "a look never chosen is the FL Studio skin");

        skins->setPreset (5);
        mm.runDispatchLoopUntil (50);
        check (Theme::rowWhite == presets[5].colours[(size_t) SkinRole::gridWhiteRows], "picking a skin recolours the grid");
        check (editor->getLookAndFeel().findColour (juce::TextButton::buttonColourId) == presets[5].colours[(size_t) SkinRole::buttons],
               "and the editor's buttons");

        skins->setColour (SkinRole::notes, juce::Colours::red);
        check (Theme::note == juce::Colours::red && skins->hasCustomColours(), "a colour of your own goes on top of the skin");

        const auto pictureFile = juce::File::createTempFile (".png");
        writePng (testPicture(), pictureFile);
        check (skins->setBackgroundFile (pictureFile) && skins->getBackgroundFor ({ 0, 0, 200, 100 }).getWidth() == 200,
               "a picture can go behind the grid, scaled to cover it");
        skins->setBackgroundStrength (0.6f);

        const auto notAPicture = juce::File::createTempFile (".png");
        notAPicture.replaceWithText ("not a picture");
        check (! skins->setBackgroundFile (notAPicture) && skins->getBackgroundFile() == pictureFile
                   && skins->getBackgroundFor ({ 0, 0, 200, 100 }).isValid(),
               "a file that isn't a picture is turned down, and the picture showing stays");
        skins->flush();

        {
            SkinManager other;
            other.useSettingsFile (lookFile);
            check (other.getPreset() == 5 && other.getColour (SkinRole::notes) == juce::Colours::red
                       && other.getBackgroundFile() == pictureFile && std::abs (other.getBackgroundStrength() - 0.6f) < 1.0e-6f,
                   "the look is kept, for every MPE Roll on the computer");
        }

        skins->setPreset (5);
        check (! skins->hasCustomColours() && Theme::note == presets[5].colours[(size_t) SkinRole::notes],
               "picking a skin again starts from its own colours");

        skins->clearBackground();
        skins->setPreset (0);
        skins->flush();
        mm.runDispatchLoopUntil (50);

        {
            SkinManager kept;
            kept.useSettingsFile (lookFile);
            check (kept.getPreset() == 0 && presets[0].name == "MPE Roll", "and a skin chosen stays, MPE Roll's included");
        }

        for (const auto& f : { lookFile, pictureFile, notAPicture })
            f.deleteFile();
    }

    scannerUiTest (check);
    editorUiChecks (check);
    synthProfileChecks (check);
    bugReportChecks (check);

    processor.editorBeingDeleted (editor.get());
    editor.reset();
    std::printf ("%s: %d failure(s)\n", check.failures == 0 ? "PASS" : "FAIL", check.failures);
    return check.failures == 0 ? 0 : 1;
}

// Plugin files are looked at in the scanner process: a crash or hang there doesn't reach MPE Roll.
// Stands in for MPE Roll Scanner when scantest runs the harness as one. It makes up a plugin named after
// the file without loading anything. For a file named "too old" it turns the job down without reading
// it, as a scanner from another version of MPE Roll does; for "never starts" it goes down before it
// gets to the file, for "crashes" while it's looking, and for "crashes on the way out" once it has
// written what it found, as a plugin does whose clean-up code crashes. In "won't start" it finds
// nothing, as JUCE does in a VST3 whose component won't initialise (an unauthorised plugin).
int standInScanner (const juce::File& jobFile)
{
    PluginScanner::Job job;

    if (! PluginScanner::Job::parse (jobFile.loadFileAsString(), job))
        return 1;

    juce::PluginDescription d;
    d.pluginFormatName = job.formatName;
    d.fileOrIdentifier = job.fileOrIdentifier;
    d.name = juce::File (d.fileOrIdentifier).getFileNameWithoutExtension();
    d.uniqueId = d.deprecatedUid = d.name.hashCode();

    if (d.name == "too old")
        std::_Exit (1);

    PluginScanner::readyMarkerFor (jobFile).create();

    if (d.name == "never starts")
        std::_Exit (3);

    PluginScanner::startedMarkerFor (jobFile).create();

    if (d.name == "crashes")
        std::_Exit (3);

    juce::XmlElement list ("LIST");

    if (d.name != "won't start")
        list.addChildElement (d.createXml().release());

    PluginScanner::resultFileFor (jobFile).replaceWithText (list.toString());

    if (d.name == "crashes on the way out")
        std::_Exit (3);

    return 0;
}

int scanTest (const juce::File& scanner, const juce::File& synthFile, const juce::File& crashing, const juce::File& hanging)
{
    Check check;
    PluginScanner::setExecutableForTesting (scanner);
    ignoreXErrors();     // the scan window and the message box about an out-of-date scanner

    const auto formatOf = [] (const juce::File& f)
    {
        return f.hasFileExtension (".clap") ? juce::String ("CLAP")
             : f.hasFileExtension (".vst3") ? juce::String ("VST3") : juce::String ("VST");
    };

    // Load plugin file... answers on the message thread once its check is done.
    const auto waitUntil = [] (const bool& flag, int ms)
    {
        const auto end = juce::Time::getMillisecondCounterHiRes() + ms;

        while (! flag && juce::Time::getMillisecondCounterHiRes() < end)
            juce::MessageManager::getInstance()->runDispatchLoopUntil (20);
    };

    std::printf ("The scanner looks inside a plugin file\n");
    {
        juce::OwnedArray<juce::PluginDescription> found;
        const auto outcome = PluginScanner::findTypes (formatOf (synthFile), synthFile.getFullPathName(), found, 60000);
        check (outcome == PluginScanner::Outcome::found, "it reports what it found");
        check (found.size() == 1 && found[0]->name == "MPE Roll Test Synth", "which is the test synth");
    }

    std::printf ("A plugin that crashes or hangs only takes the scanner down\n");
    {
        juce::OwnedArray<juce::PluginDescription> found;
        const auto crashStart = juce::Time::getMillisecondCounterHiRes();
        check (PluginScanner::findTypes ("CLAP", crashing.getFullPathName(), found, 60000) == PluginScanner::Outcome::crashed,
               "a crash is reported as one");
        check (juce::Time::getMillisecondCounterHiRes() - crashStart < 5000, "at once");

        const auto start = juce::Time::getMillisecondCounterHiRes();
        check (PluginScanner::findTypes ("CLAP", hanging.getFullPathName(), found, 3000) == PluginScanner::Outcome::timedOut,
               "a hang is reported as one");
        check (juce::Time::getMillisecondCounterHiRes() - start < 15000, "once the time allowed is up");
        check (found.isEmpty(), "and neither lists a plugin");
    }

    std::printf ("A scan carries on past them\n");
    {
        juce::AudioPluginFormatManager formats;
        juce::addDefaultFormatsToManager (formats);
       #if MPEROLL_HOST_CLAP
        formats.addFormat (std::make_unique<ClapPluginFormat>());
       #endif

        const auto formatNamed = [&formats] (const juce::String& name) -> juce::AudioPluginFormat*
        {
            for (auto* format : formats.getFormats())
                if (format->getName() == name)
                    return format;

            return nullptr;
        };

        juce::KnownPluginList list;
        list.setCustomScanner (std::make_unique<PluginScanner::OutOfProcessScanner> (3000));
        juce::OwnedArray<juce::PluginDescription> found;
        auto* clap = formatNamed ("CLAP");
        auto* synthFormat = formatNamed (formatOf (synthFile));

        if (clap != nullptr && synthFormat != nullptr)
        {
            list.scanAndAddFile (crashing.getFullPathName(), false, found, *clap);
            list.scanAndAddFile (hanging.getFullPathName(), false, found, *clap);
            list.scanAndAddFile (synthFile.getFullPathName(), false, found, *synthFormat);
        }

        const auto& setAside = list.getBlacklistedFiles();
        check (setAside.contains (crashing.getFullPathName()) && setAside.contains (hanging.getFullPathName()),
               "the crashing and hanging files are set aside");
        check (list.getNumTypes() == 1 && list.getTypes()[0].name == "MPE Roll Test Synth",
               "and the synth after them is listed");
    }

    std::printf ("A file is only blamed for what happened while it was being looked at\n");
    {
        PluginScanner::setExecutableForTesting (juce::File::getSpecialLocation (juce::File::currentExecutableFile));
        const auto folder = juce::File::getSpecialLocation (juce::File::tempDirectory);
        const auto pathOf = [&folder] (const char* name) { return folder.getChildFile (juce::String (name) + ".vst3").getFullPathName(); };
        juce::OwnedArray<juce::PluginDescription> found;

        check (PluginScanner::findTypes ("VST3", pathOf ("crashes on the way out"), found, 10000) == PluginScanner::Outcome::found
                   && found.size() == 1,
               "a plugin that crashes after it has been looked at keeps what was found");
        check (PluginScanner::findTypes ("VST3", pathOf ("crashes"), found, 10000) == PluginScanner::Outcome::crashed,
               "one that crashes while it's looked at is reported");
        check (PluginScanner::findTypes ("VST3", pathOf ("never starts"), found, 10000) == PluginScanner::Outcome::unavailable,
               "a scanner that goes down before it gets to the file says nothing about the file");
        check (PluginScanner::findTypes ("VST3", pathOf ("too old"), found, 10000) == PluginScanner::Outcome::outdated,
               "one from another version of MPE Roll, which turns the job down, is reported as out of date");

        juce::KnownPluginList list;
        list.setCustomScanner (std::make_unique<PluginScanner::OutOfProcessScanner> (10000));
        juce::VST3PluginFormat format;      // only its name reaches the stand-in scanner
        list.scanAndAddFile (pathOf ("never starts"), false, found, format);
        check (list.getBlacklistedFiles().isEmpty(), "so a scan doesn't set that file aside");
        list.scanAndAddFile (pathOf ("too old"), false, found, format);
        check (list.getBlacklistedFiles().isEmpty(), "nor the file an out-of-date scanner couldn't look at");

        // One of FL's own plugins isn't looked at at all, and a box at the end of the scan says what it
        // is. A scan of its own, so nothing else from the scans above ends up in that box.
        juce::KnownPluginList endList;
        endList.setCustomScanner (std::make_unique<PluginScanner::OutOfProcessScanner> (10000));
        const auto fruity = folder.getChildFile ("Image-Line/FL Studio 2025/Plugins/Fruity/Generators/Harmor/Harmor_x64.dll");
        fruity.create();
        found.clear();
        endList.scanAndAddFile (fruity.getFullPathName(), false, found, format);
        check (found.isEmpty() && endList.getBlacklistedFiles().isEmpty() && endList.getNumTypes() == 0,
               "one of FL's own plugins is left out of a scan without being set aside");

        endList.scanAndAddFile (pathOf ("won't start"), false, found, format);
        check (found.isEmpty() && endList.getBlacklistedFiles().isEmpty() && endList.getNumTypes() == 0,
               "a VST3 that won't start is left out without being set aside, so it's scanned again next time");

        endList.scanFinished();
        const auto endBox = []() -> juce::AlertWindow*
        {
            auto& desktop = juce::Desktop::getInstance();

            for (int i = 0; i < desktop.getNumComponents(); ++i)
                if (auto* box = dynamic_cast<juce::AlertWindow*> (desktop.getComponent (i)))
                    if (box->getName() == "Some plugins weren't added")
                        return box;

            return nullptr;
        };

        for (int i = 0; i < 20 && endBox() == nullptr; ++i)
            juce::MessageManager::getInstance()->runDispatchLoopUntil (50);

        const auto said = PluginScanner::describeFlStudioNativeFiles ({ fruity.getFileName() });
        check (endBox() != nullptr && said.contains ("Harmor_x64.dll") && said.contains ("Send to FL piano roll"),
               "and the scan ends with a box naming FL's plugin and where to find it instead");

        const auto refused = PluginScanner::describeRefusedToStart (juce::StringArray ("won't start.vst3"), true);
        check (refused.contains ("won't start.vst3") && refused.contains ("authorised") && refused.contains ("Roland Cloud Manager"),
               "and the plugin that didn't start, with what to check");

        if (auto* box = endBox())
            box->exitModalState (0);

        folder.getChildFile ("Image-Line").deleteRecursively();

        // Load plugin file... on it says the same.
        const juce::File wontStart (pathOf ("won't start"));
        wontStart.create();
        {
            HostedSynth synth;
            juce::String error;
            bool answered = false;
            synth.loadFromFile (wontStart, [&] (const juce::String& e) { error = e; answered = true; });
            waitUntil (answered, 30000);
            check (answered && error.contains ("didn't start") && error.contains ("authorised") && ! synth.isLoaded(),
                   "Load plugin file... on a plugin that won't start says it may not be authorised");
        }
        wontStart.deleteFile();

        // Load plugin file... only tries files that exist. Empty, so looking inside it here would find nothing.
        const juce::File tooOld (pathOf ("too old"));
        tooOld.create();
        {
            HostedSynth synth;
            juce::String error;
            bool answered = false;
            synth.loadFromFile (tooOld, [&] (const juce::String& e) { error = e; answered = true; });
            waitUntil (answered, 30000);
            check (answered && error.contains ("older than this MPE Roll") && ! synth.isLoaded(),
                   "Load plugin file... says the scanner is out of date, rather than looking inside the file here");
        }
        tooOld.deleteFile();
        PluginScanner::setExecutableForTesting (scanner);
    }

    std::printf ("Load plugin file... checks the file off the message thread, and refuses one that crashes\n");
    {
        HostedSynth synth;
        juce::String error = "(no answer)";
        bool answered = false, checked = false;
        synth.loadFromFile (crashing, [&] (const juce::String& e) { error = e; answered = true; }, [&checked] { checked = true; });
        check (! answered && synth.isLoading(), "the check runs on a thread of its own, so FL's window isn't held up");
        waitUntil (answered, 30000);
        check (error.contains ("crashed"), "the user is told it crashed");
        check (! synth.isLoaded() && ! synth.isLoading() && ! checked, "and nothing is loaded, or said to be loading");
    }

    std::printf ("A check that's still running stops when the synth goes, or another load starts\n");
    {
        bool answered = false;
        const auto start = juce::Time::getMillisecondCounterHiRes();

        {
            HostedSynth synth;
            synth.loadFromFile (hanging, [&answered] (const juce::String&) { answered = true; });
        }

        check (juce::Time::getMillisecondCounterHiRes() - start < 5000.0, "the synth is gone within moments of a check that's stuck in a plugin");
        juce::MessageManager::getInstance()->runDispatchLoopUntil (200);
        check (! answered, "and the check doesn't answer afterwards");

        HostedSynth synth;
        bool first = false, second = false, checkedBeforeLoading = false;
        juce::String error;
        synth.loadFromFile (hanging, [&first] (const juce::String&) { first = true; });
        synth.loadFromFile (synthFile, [&] (const juce::String& e) { error = e; second = true; },
                            [&] { checkedBeforeLoading = ! second && ! synth.isLoaded(); });
        waitUntil (second, 30000);
        check (second && error.isEmpty() && synth.isLoaded(), "a second Load plugin file... loads its synth");
        check (checkedBeforeLoading, "saying when the check is over and the loading starts (the status goes from Checking to Loading)");
        check (! first, "and the first never answers");

        // A project giving the loaded synth its state (FL does on opening a project, or when the plugin
        // is bridged) while a check is stuck in a plugin: the check and its scanner stop then too, as
        // they do when another load starts. The scanner's job folder is there for as long as it runs.
        const auto scanFolders = []
        {
            return juce::File::getSpecialLocation (juce::File::tempDirectory)
                       .findChildFiles (juce::File::findDirectories, false, "MPE Roll scan *").size();
        };

        const auto foldersBefore = scanFolders();
        bool third = false;
        synth.loadFromFile (hanging, [&third] (const juce::String&) { third = true; });
        const auto end = juce::Time::getMillisecondCounterHiRes() + 5000.0;

        while (scanFolders() == foldersBefore && juce::Time::getMillisecondCounterHiRes() < end)
            juce::MessageManager::getInstance()->runDispatchLoopUntil (20);

        check (scanFolders() == foldersBefore + 1 && synth.isLoading(), "a check stuck in a plugin has its scanner running");

        if (auto saved = synth.saveState())
            synth.restoreState (*saved);

        // The folder goes with the check; a moment's grace for the file system to let it go.
        const auto stopEnd = juce::Time::getMillisecondCounterHiRes() + 2000.0;

        while (scanFolders() != foldersBefore && juce::Time::getMillisecondCounterHiRes() < stopEnd)
            juce::MessageManager::getInstance()->runDispatchLoopUntil (20);

        check (scanFolders() == foldersBefore && ! synth.isLoading(), "a project giving the loaded synth its state stops the check, scanner and all");
        juce::MessageManager::getInstance()->runDispatchLoopUntil (200);
        check (! third && synth.isLoaded(), "the check never answers, and the synth stays");
    }

    std::printf ("A scanner ends by itself when MPE Roll has gone, or its time is long up\n");
    {
        // Started by hand, as MPE Roll would, then left alone: how long until it ends, or -1 if it didn't.
        const auto runAlone = [&] (juce::int64 parentProcess, int timeoutMs) -> double
        {
            const auto folder = juce::File::getSpecialLocation (juce::File::tempDirectory)
                                    .getChildFile ("MPE Roll scan watchdog " + juce::Uuid().toDashedString());
            folder.createDirectory();
            const auto jobFile = folder.getChildFile ("job.txt");

            PluginScanner::Job job;
            job.formatName = "CLAP";
            job.fileOrIdentifier = hanging.getFullPathName();
            job.parentProcessId = parentProcess;
            job.timeoutMs = timeoutMs;
            jobFile.replaceWithText (job.toText());

            juce::ChildProcess process;
            const auto start = juce::Time::getMillisecondCounterHiRes();
            double took = -1.0;

            if (process.start (juce::StringArray { scanner.getFullPathName(), PluginScanner::scanArgument, jobFile.getFullPathName() }, 0))
            {
                if (process.waitForProcessToFinish (20000))
                    took = juce::Time::getMillisecondCounterHiRes() - start;
                else
                    process.kill();
            }

            folder.deleteRecursively();
            return took;
        };

        const auto noParent = runAlone (0x7fffffff, 60000);     // no process has this ID
        check (noParent >= 0.0 && noParent < 10000.0, "at once when the process that started it isn't there");

        const auto stuck = runAlone (PluginScanner::currentProcessId(), 1000);
        check (stuck >= 1000.0 && stuck < 15000.0, "and once the job's time and a margin are up while it's stuck in a plugin");
    }

    std::printf ("Files dropped on the scan window are scanned on the scan's thread\n");
    {
        HostedSynth synth;
        auto& known = synth.getKnownPlugins();

        for (const auto& type : known.getTypes())       // from an earlier run, maybe
            if (type.name == "MPE Roll Test Synth")
                known.removeType (type);

        synth.showScanWindow();
        juce::MessageManager::getInstance()->runDispatchLoopUntil (100);

        juce::PluginListComponent* list = nullptr;
        auto& desktop = juce::Desktop::getInstance();

        for (int i = 0; i < desktop.getNumComponents(); ++i)
            if (auto* window = dynamic_cast<juce::DialogWindow*> (desktop.getComponent (i)))
                if (window->getName() == "MPE Roll: scan for plugins")
                    list = dynamic_cast<juce::PluginListComponent*> (window->getContentComponent());

        check (list != nullptr, "the scan window shows the plugin list");

        if (list != nullptr)
        {
            juce::FileDragAndDropTarget& target = *list;
            target.filesDropped (juce::StringArray { synthFile.getFullPathName() }, 0, 0);
            check (list->isScanning(), "a drop starts a scan and comes back at once");

            const auto end = juce::Time::getMillisecondCounterHiRes() + 30000.0;

            while (list->isScanning() && juce::Time::getMillisecondCounterHiRes() < end)
                juce::MessageManager::getInstance()->runDispatchLoopUntil (20);

            bool listed = false;

            for (const auto& type : known.getTypes())
                listed = listed || type.name == "MPE Roll Test Synth";

            check (! list->isScanning() && listed, "and the synth is listed once the scan is done");
        }
    }

    PluginScanner::setExecutableForTesting ({});
    std::printf ("%s: %d failure(s)\n", check.failures == 0 ? "PASS" : "FAIL", check.failures);
    return check.failures == 0 ? 0 : 1;
}

// A load that fails says why: the system's reason for a file it can't load, and otherwise the step
// that failed when the scanner retraced the load (LoadDiagnosis).
int loadFailTest (const juce::File& scanner, const juce::File& vst3Synth, const juce::File& vst2Synth, const juce::File& flakySynth)
{
    Check check;
    PluginScanner::setExecutableForTesting (scanner);
    const auto folder = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("MPE Roll load failures");
    folder.deleteRecursively();
    folder.createDirectory();
    PluginLog::setFileForTesting (folder.getChildFile ("Plugin log.txt"));

    // The settings every synth here shares (where loading markers go), in a file of the test's own:
    // kept open by this one for as long as the test runs.
    HostedSynth keeper;
    keeper.useSettingsFile (folder.getChildFile ("MPE Roll.settings"));

    const auto waitFor = [] (bool& answered)
    {
        const auto end = juce::Time::getMillisecondCounterHiRes() + 120000.0;

        while (! answered && juce::Time::getMillisecondCounterHiRes() < end)
            juce::MessageManager::getInstance()->runDispatchLoopUntil (20);

        return answered;
    };

    const auto load = [&check, &waitFor] (const juce::PluginDescription& description)
    {
        HostedSynth synth;
        juce::String error;
        bool answered = false;
        synth.load (description, [&] (const juce::String& e) { error = e; answered = true; });
        waitFor (answered);

        check (answered && ! synth.isLoaded() && ! synth.isLoading() && ! synth.isFindingOutWhy(),
               "  (" + description.name + " didn't load, and the answer came)");
        std::printf ("    %s\n", error.replace ("\n", "\n    ").toRawUTF8());
        return error;
    };

    /** Loads into a synth that stays, and answers with the error (or "no answer"). */
    const auto loadInto = [&waitFor] (HostedSynth& synth, const juce::PluginDescription& description)
    {
        juce::String error;
        bool answered = false;
        synth.load (description, [&] (const juce::String& e) { error = e; answered = true; });
        return waitFor (answered) ? error : juce::String ("no answer");
    };

    const auto describeOnly = [] (const juce::File& file)
    {
        juce::OwnedArray<juce::PluginDescription> found;

        if (file.hasFileExtension (".vst3") && file.exists())
        {
            juce::VST3PluginFormat vst3;
            vst3.findAllTypesForFile (found, file.getFullPathName());
        }
       #if JUCE_PLUGINHOST_VST
        else if (file.existsAsFile())
        {
            juce::VSTPluginFormat vst2;
            vst2.findAllTypesForFile (found, file.getFullPathName());
        }
       #endif

        return found.isEmpty() ? juce::PluginDescription() : *found[0];
    };

    std::printf ("A file the system can't load: its reason\n");
    {
        juce::PluginDescription d;
        d.pluginFormatName = "VST3";
        d.name = "Broken Synth";
        d.numOutputChannels = 2;
        const auto bundle = folder.getChildFile ("Broken Synth.vst3");

       #if JUCE_WINDOWS
        d.fileOrIdentifier = bundle.getFullPathName();
        bundle.replaceWithText ("not a plugin");
       #else
        // On Linux a VST3 is always a bundle, with the library inside.
        bundle.createDirectory();
        d.fileOrIdentifier = bundle.getFullPathName();
        const auto inside = LoadDiagnosis::findBinaries (d)[0];
        inside.getParentDirectory().createDirectory();
        inside.replaceWithText ("not a plugin");
       #endif

        const auto error = load (d);
       #if JUCE_WINDOWS
        check (error.contains ("Windows couldn't load Broken Synth.vst3") && error.contains ("isn't a 64-bit Windows plugin")
                   && error.contains ("Windows error 193"),
               "Windows' own reason, in words, with its error number");
        check (! error.contains ("%1"), "the file named where Windows' own message has a placeholder");
       #else
        check (error.contains ("Broken Synth.so couldn't be loaded:"), "the system's own reason");
       #endif
    }

    const auto realVst3 = describeOnly (vst3Synth);
    check (realVst3.name == "MPE Roll Test Synth", "the test synth's VST3 is found");

   #if JUCE_PLUGINHOST_VST
    std::printf ("A library that isn't a plugin, loaded as VST2: it says so\n");
    if (realVst3.name.isNotEmpty())
    {
        // The VST3 test synth's own library has no VST2 entry point.
       #if JUCE_WINDOWS
        const auto library = folder.getChildFile ("Not A Synth.dll");
       #else
        const auto library = folder.getChildFile ("Not A Synth.so");
       #endif
        LoadDiagnosis::findBinaries (realVst3)[0].copyFileTo (library);

        juce::PluginDescription d;
        d.pluginFormatName = "VST";
        d.name = "Not A Synth";
        d.fileOrIdentifier = library.getFullPathName();
        const auto error = load (d);
        check (error.contains ("has no VSTPluginMain or main function, so it isn't a VST2 plugin"), "no VST2 entry point");
    }
   #endif

    std::printf ("A VST3 that no longer holds the class a scan listed: the scanner finds which\n");
    if (realVst3.name.isNotEmpty())
    {
        auto changed = realVst3;
        changed.uniqueId ^= 0x5a5a;
        changed.deprecatedUid ^= 0x5a5a;
        const auto error = load (changed);
        check (error.contains ("\"MPE Roll Test Synth\" has another ID"), "the class with its name has another ID");
        check (error.contains ("Load plugin file...") || error.contains ("moduleinfo.json"), "and what to do");

        const auto log = PluginLog::getFile().loadFileAsString();
        check (log.contains ("Retracing the load in the scanner") && log.contains ("Calling its GetPluginFactory")
                   && log.contains ("No class is \"MPE Roll Test Synth\""),
               "the plugin log has each step the scanner took");
        std::printf ("    The plugin log so far:\n%s\n", ("      " + log.trimEnd().replace ("\n", "\n      ")).toRawUTF8());

        auto renamed = realVst3;
        renamed.name = "MPE Roll Old Synth";
        check (load (renamed).contains ("\"MPE Roll Test Synth\" has the ID, under another name"), "and a renamed class too");

        std::printf ("Load plugin file... on it, as the box says: the listing that no longer loads is replaced\n");
        HostedSynth synth;
        auto& known = synth.getKnownPlugins();
        known.addType (changed);

        bool answered = false;
        juce::String loadError;
        synth.loadFromFile (juce::File (realVst3.fileOrIdentifier), [&] (const juce::String& e) { loadError = e; answered = true; });
        const auto end = juce::Time::getMillisecondCounterHiRes() + 120000.0;

        while (! answered && juce::Time::getMillisecondCounterHiRes() < end)
            juce::MessageManager::getInstance()->runDispatchLoopUntil (20);

        juce::Array<juce::PluginDescription> listed;

        for (const auto& type : known.getTypes())
            if (type.pluginFormatName == "VST3" && type.fileOrIdentifier == realVst3.fileOrIdentifier)
                listed.add (type);

        check (answered && loadError.isEmpty() && synth.isLoaded(), "it loads");
        check (listed.size() == 1 && listed[0].isDuplicateOf (realVst3), "and the file is listed once, as it is now");
        synth.unload();
    }

    std::printf ("Plugins that load fine, retraced anyway: the plugin itself works\n");
    for (const auto& synthFile : { vst3Synth, vst2Synth })
    {
        const auto d = describeOnly (synthFile);

        if (d.name.isEmpty())
            continue;

        const auto diagnosis = PluginScanner::diagnose (d, 60000);
        const auto why = LoadDiagnosis::summarise (diagnosis.lines, false, false, d.name);
        check (diagnosis.outcome == PluginScanner::Outcome::found && why.contains ("loads fine in a separate process"),
               d.pluginFormatName + ": " + why.upToFirstOccurrenceOf (",", false, false));
    }

    std::printf ("A scanner from before diagnoses turns the job down\n");
    {
        // The harness stands in for a scanner that only knows scan jobs.
        PluginScanner::setExecutableForTesting (juce::File::getSpecialLocation (juce::File::currentExecutableFile));
        check (PluginScanner::diagnose (realVst3, 20000).outcome == PluginScanner::Outcome::outdated, "and is reported as out of date");

        if (realVst3.name.isNotEmpty())
        {
            auto changed = realVst3;
            changed.uniqueId ^= 0x5a5a;
            changed.deprecatedUid ^= 0x5a5a;
            const auto error = load (changed);
            check (error.contains ("couldn't find out why MPE Roll Test Synth didn't load") && error.contains ("older than this MPE Roll"),
                   "a failed load says the scanner couldn't find out why, and that it's out of date");
        }
    }

    PluginScanner::setExecutableForTesting (scanner);

   #if JUCE_PLUGINHOST_VST
    std::printf ("A plugin that won't start the first time in a process is loaded again once the scanner finds it works\n");
    if (flakySynth.existsAsFile())
    {
        // Described by hand: asking the file for its plugins would be its first start in this process.
        juce::PluginDescription d;
        d.pluginFormatName = "VST";
        d.name = "MPE Roll Flaky Test Synth";
        d.manufacturerName = "Glowing Pancake";
        d.fileOrIdentifier = flakySynth.getFullPathName();
        d.uniqueId = d.deprecatedUid = 0x466c6b79;    // 'Flky'
        d.numOutputChannels = 2;
        d.isInstrument = true;

        HostedSynth synth;
        const auto error = loadInto (synth, d);
        std::printf ("    %s\n", error.isEmpty() ? "loaded" : error.replace ("\n", "\n    ").toRawUTF8());
        check (error.isEmpty() && synth.isLoaded(), "it loads, on the second try");
        const auto log = PluginLog::tail (40);
        check (log.contains ("started in a separate process when MPE Roll called it directly"), "the scanner having started it directly");
        const bool retried = log.contains ("Loading it again, since it works in a separate process")
                          && log.contains ("Loading again MPE Roll Flaky Test Synth");
        check (retried, "and MPE Roll loaded it again by itself");

        if (! retried)
            std::printf ("%s\n", log.replace ("\n", "\n      ").toRawUTF8());

        synth.unload();
    }
    else
    {
        std::printf ("  (no flaky test synth given: not checked)\n");
    }
   #endif

    std::printf ("Another install of the loaded synth: the first is let go before the second loads\n");
    if (realVst3.name.isNotEmpty())
    {
        // A copy of the VST3 test synth in another folder, as a second install is.
        const juce::File bundle (realVst3.fileOrIdentifier);
        const auto secondFolder = folder.getChildFile ("second install");
        secondFolder.createDirectory();
        const auto copy = secondFolder.getChildFile (bundle.getFileName());
        const auto copied = bundle.isDirectory() ? bundle.copyDirectoryTo (copy) : bundle.copyFileTo (copy);
        const auto second = describeOnly (copy);
        check (copied && second.name == realVst3.name && second.fileOrIdentifier == copy.getFullPathName(),
               "a copy of the synth in another folder is found there");

        HostedSynth synth;
        check (loadInto (synth, realVst3).isEmpty() && synth.isLoaded(), "the first install loads");
        check (loadInto (synth, second).isEmpty() && synth.isLoaded() && synth.getDescription() != nullptr
                   && synth.getDescription()->fileOrIdentifier == copy.getFullPathName(),
               "and the second loads in its place");
        check (PluginLog::tail (30).contains ("first: another install of it is about to be loaded"), "the first let go before it");
        synth.unload();
    }

    std::printf ("A synth that took the program down while loading isn't loaded again by a project on its own\n");
    if (realVst3.name.isNotEmpty())
    {
        auto* settings = keeper.getUserSettings();
        check (settings != nullptr, "the test has settings of its own");

        const auto processId = CopySafety::getProcessId();
        const auto started = CopySafety::getProcessStartTime (processId);
        const auto identity = realVst3.name + "|" + realVst3.pluginFormatName + ":"
                            + juce::String (realVst3.uniqueId != 0 ? realVst3.uniqueId : realVst3.deprecatedUid) + "|" + realVst3.fileOrIdentifier;
        const auto markers = [settings]
        {
            int count = 0;

            for (const auto& key : settings->getAllProperties().getAllKeys())
                count += key.startsWith ("synthLoading.") ? 1 : 0;

            return count;
        };

        // The project names the synth.
        juce::XmlElement project ("HostedSynth");
        project.addChildElement (realVst3.createXml().release());

        const auto restoreInto = [&waitFor, &project] (HostedSynth& synth)
        {
            juce::String error;
            bool answered = false;
            synth.restoreState (project, [&] (const juce::String& e) { error = e; answered = true; });
            return waitFor (answered) ? error : juce::String ("no answer");
        };

        // What a run that closed while loading it leaves behind: its process ID has since gone to
        // another process (this one), which started later, so it isn't the one.
        settings->setValue ("synthLoading.earlier", "another run|" + juce::String (processId) + "@" + juce::String (started - 1) + "|" + identity);

        HostedSynth synth;
        auto error = restoreInto (synth);
        std::printf ("    %s\n", error.toRawUTF8());
        check (! synth.isLoaded() && error.contains ("closed while it was loading last time") && synth.getLoadProblem() == error,
               "the project opens without it, and says why");
        check (synth.getMissingSynthName() == realVst3.name, "the synth button can name it");
        check (! settings->containsKey ("synthLoading.earlier") && settings->getValue ("synthCrashedWhileLoading").contains (identity),
               "the marker is gone, and the crash remembered");

        // Picked by name, it loads, and the crash is forgotten.
        error = loadInto (synth, realVst3);
        check (error.isEmpty() && synth.isLoaded() && synth.getLoadProblem().isEmpty(), "Load synth loads it");
        check (! settings->getValue ("synthCrashedWhileLoading").contains (identity) && markers() == 0,
               "the crash is forgotten, and no marker is left once it has loaded");

        // Another program loading it right now (the MPE Roll app beside FL, say) isn't a crash.
        settings->setValue ("synthLoading.elsewhere", "another program|" + juce::String (processId) + "@" + juce::String (started) + "|" + identity);
        synth.unload();
        check (restoreInto (synth).isEmpty() && synth.isLoaded(), "a running program's marker doesn't keep a project from loading it");
        check (settings->containsKey ("synthLoading.elsewhere"), "and is left for it");
        settings->removeValue ("synthLoading.elsewhere");
        synth.unload();
    }

    PluginScanner::setExecutableForTesting ({});
    PluginLog::setFileForTesting ({});
    folder.deleteRecursively();
    std::printf ("%s: %d failure(s)\n", check.failures == 0 ? "PASS" : "FAIL", check.failures);
    return check.failures == 0 ? 0 : 1;
}

#if JUCE_WINDOWS
 #if JUCE_MSVC
__declspec (noinline)
 #endif
void crashOnPurpose (volatile int* target)
{
    *target = 42;
}

volatile bool keepRecursing = true;     // always true, but the compiler can't know that

 #if JUCE_MSVC
__declspec (noinline)
 #endif
int overflowOnPurpose (int depth)
{
    volatile char frame[4096];
    frame[0] = (char) depth;

    if (! keepRecursing)
        return 0;

    return overflowOnPurpose (depth + 1) + frame[0];
}
#endif

// Windows: the standalone app's crash report, checked by crashing on purpose. Never returns there.
int crashTest (const juce::File& reportFile, bool overflow)
{
   #if JUCE_WINDOWS
    static volatile int* volatile nowhere = nullptr;
    CrashReport::install (reportFile, false);
    CrashReport::setActivity ("running the crash test");

    if (overflow)
        overflowOnPurpose (0);
    else
        crashOnPurpose (nowhere);

    std::printf ("FAIL: it didn't crash\n");
    return 1;
   #else
    juce::ignoreUnused (reportFile, overflow);
    std::printf ("Crash reports are only made on Windows.\n");
    return 0;
   #endif
}

} // namespace

//==============================================================================
/** One copy per note, through the plain test synth: a synth without MPE, where every note on a
    channel shares its pitch bend. Copies give each note its own; the copies follow the first one's
    parameters and sound, sleep when quiet, play the same on several cores as on one, survive a
    project being saved and opened, and stop loading when memory runs low or when loading them
    crashed last time.
*/
struct CopyRig
{
    static constexpr double rate = 48000.0;
    static constexpr int block = 512;

    CopyRig()
    {
        processor.setPlayHead (&playHead);
        processor.setRateAndBufferSizeDetails (rate, block);
        processor.prepareToPlay (rate, block);
    }

    ~CopyRig()
    {
        processor.releaseResources();
    }

    HostedSynth& synth()                                { return processor.getSynth(); }
    HostedSynth::CopyStatus status()                    { return synth().getCopyStatus(); }

    static void pump (int ms)                           { juce::MessageManager::getInstance()->runDispatchLoopUntil (ms); }

    void setOutput (const std::function<void (OutputSettings&)>& change)
    {
        auto s = processor.getDocument().getSettings();
        change (s.output);
        processor.getDocument().setSettings (s);
        pump (30);      // the document's change message reaches the processor, which tells the synth
    }

    /** Blocks with the transport stopped, in about real time: the message loop runs between them
        for as long as a block lasts, so timers (the copies' checks for a changed sound) fire as
        they would. */
    void idle (int milliseconds)
    {
        playHead.playing = false;
        const auto blockMs = juce::jmax (1, (int) (block / rate * 1000.0));
        const auto blocks = juce::jmax (1, milliseconds / blockMs);

        for (int i = 0; i < blocks; ++i)
        {
            juce::AudioBuffer<float> audio (2, block);
            juce::MidiBuffer midi;
            processor.processBlock (audio, midi);
            pump (blockMs);
        }
    }

    /** Waits, playing silence, until the copies asked for are loaded (or loading has stopped
        short) and none is taking a sound. */
    bool settle (int timeoutMs = 30000)
    {
        auto loaded = std::make_shared<bool> (false);
        synth().whenCopiesLoaded ([loaded] { *loaded = true; });

        for (int waited = 0; waited < timeoutMs; waited += 50)
        {
            idle (50);

            if (*loaded && ! status().copyingSound)
            {
                idle (50);
                return true;
            }
        }

        return false;
    }

    /** Plays the clip in slot 1 from its start for this long and returns what came out. Given
        `during`, it's called before each block with the time, and the blocks come in about real
        time, the message loop running between them (timers fire as they would). */
    juce::AudioBuffer<float> play (const MpeClip& clip, double seconds, const std::function<void (double)>& during = {})
    {
        processor.getDocument().replaceClip (0, clip, "Test");
        pump (10);

        const auto total = (int) (seconds * rate);
        const auto blockMs = juce::jmax (1, (int) (block / rate * 1000.0));
        juce::AudioBuffer<float> output (2, total);
        output.clear();
        playHead.playing = true;

        for (int pos = 0; pos < total; pos += block)
        {
            if (during)
                during (pos / rate);

            const auto n = juce::jmin (block, total - pos);
            juce::AudioBuffer<float> audio (2, n);
            juce::MidiBuffer midi;
            playHead.ppq = pos / rate * playHead.bpm / 60.0;
            processor.processBlock (audio, midi);

            for (int ch = 0; ch < 2; ++ch)
                output.copyFrom (ch, pos, audio, ch, 0, n);

            if (during)
                pump (blockMs);
        }

        playHead.playing = false;
        idle (20);
        return output;
    }

    double level (const juce::AudioBuffer<float>& audio, double atSeconds, double frequency) const
    {
        constexpr int window = 4096;
        const auto centre = (int) (atSeconds * rate);
        return goertzel (audio.getReadPointer (0, centre - window / 2), window, frequency, rate);
    }

    juce::MemoryBlock stateOf (int copy)
    {
        juce::MemoryBlock state;

        if (auto* plugin = synth().getCopy (copy))
            plugin->getStateInformation (state);

        return state;
    }

    /** True if every loaded copy has the first copy's state. */
    bool copiesMatchFirst()
    {
        const auto first = stateOf (0);

        for (int c = 1; c < OutputSettings::maxCopies; ++c)
        {
            if (synth().getCopy (c) == nullptr)
                continue;

            if (const auto state = stateOf (c); state != first)
            {
                size_t at = 0;

                while (at < juce::jmin (state.getSize(), first.getSize()) && state[at] == first[at])
                    ++at;

                std::printf ("  copy %d's state (%d bytes) differs from the first's (%d bytes) at byte %d\n", c + 1,
                             (int) state.getSize(), (int) first.getSize(), (int) at);
                std::printf ("%s\n", PluginLog::tail (6).toRawUTF8());
                return false;
            }
        }

        return true;
    }

    FakePlayHead playHead;
    MpeRollProcessor processor;
};

/** C4 glides up an octave between beats 0.5 and 1.5 while E4 holds (as in the render test). */
MpeClip glideClip()
{
    MpeClip clip;
    clip.length = 8.0;
    auto glide = note (60, 0.0, 2.0, 0.8f);
    glide.lanes.pitch = { { 0.0, 0.0f }, { 0.5, 0.0f }, { 1.5, 12.0f } };
    clip.addNote (glide);
    clip.addNote (note (64, 0.0, 2.0, 0.8f));
    return clip;
}

int copiesTest (const juce::File& synthFile)
{
    Check check;
    ignoreXErrors();     // the synth's window, opened and closed below

    // The settings every MPE Roll shares (where the loading markers go) in a file of the test's own.
    const auto settingsFile = juce::File::createTempFile ("settings");

    // Where the test synth notes any call made to it after it crashed (before it's loaded).
    const auto afterCrash = juce::File::createTempFile ("after-crash");
    afterCrash.deleteFile();
   #if JUCE_WINDOWS
    _putenv_s ("MPEROLL_TEST_AFTER_CRASH", afterCrash.getFullPathName().toRawUTF8());
   #else
    setenv ("MPEROLL_TEST_AFTER_CRASH", afterCrash.getFullPathName().toRawUTF8(), 1);
   #endif

    auto rig = std::make_unique<CopyRig>();
    rig->synth().useSettingsFile (settingsFile);

    rig->setOutput ([] (OutputSettings& o)
    {
        o.mode = OutputSettings::Mode::copies;
        o.copies = 4;
        o.masterBendRange = 12.0f;      // the copies are told it (RPN 0), and the synth follows
    });

    if (! loadSynth (rig->processor, synthFile))
        return 1;

    std::printf ("Hosting: %s\n", rig->synth().getName().toRawUTF8());
    check (rig->settle() && rig->status().loaded == 4, "four copies load, one at a time");

    // The synth's own bend range set to MPE Roll's, as its user would: a VST3 built with JUCE gets
    // MIDI controllers as parameter changes, in which an RPN's messages lose their order, so the
    // range MPE Roll sends (RPN 0) only reaches the VST2. Through FL automation, so every copy has it.
    for (auto* p : rig->processor.getParameters())
        if (p->getName (100) == "Bend range")
            p->setValue (p->getValueForText ("12"));

    rig->idle (100);
    std::printf ("  %s\n", rig->synth().describeConnection (rig->processor.getDocument().getSettings().output).toRawUTF8());

    const auto c4 = juce::MidiMessage::getMidiNoteInHertz (60);
    const auto e4 = juce::MidiMessage::getMidiNoteInHertz (64);
    const auto c5 = juce::MidiMessage::getMidiNoteInHertz (72);

    std::printf ("Each note glides on its own copy\n");
    juce::AudioBuffer<float> withCores;
    {
        withCores = rig->play (glideClip(), 1.0);
        const auto earlyC4 = rig->level (withCores, 0.125, c4), earlyE4 = rig->level (withCores, 0.125, e4);
        const auto lateC5 = rig->level (withCores, 0.925, c5), lateC4 = rig->level (withCores, 0.925, c4);
        const auto lateE4 = rig->level (withCores, 0.925, e4);
        std::printf ("  start: C4 %.4f E4 %.4f   end: C5 %.4f C4 %.4f E4 %.4f\n", earlyC4, earlyE4, lateC5, lateC4, lateE4);
        check (earlyC4 > 0.01 && earlyE4 > 0.01, "both notes start in tune");
        check (lateC5 > 10.0 * lateC4, "C4 glides up the octave (the copy took the 12-semitone bend range)");
        check (lateE4 > 0.5 * earlyE4, "E4 holds still while it does");
    }

    std::printf ("A glide past the Zone bend range is reported\n");
    {
        check (rig->processor.getRecentBendOverflow().isEmpty(), "the octave glide fitted the 12-semitone range");

        // The stream carries each note's bend scaled to the copies' range, so the octave stops at 2.
        rig->setOutput ([] (OutputSettings& o) { o.masterBendRange = 2.0f; });
        rig->play (glideClip(), 1.0);
        const auto over = rig->processor.getRecentBendOverflow();
        std::printf ("  reached %g semitones, stopped at %g\n", (double) over.asked, (double) over.range);
        check (std::abs (over.asked - 12.0f) < 0.01f && std::abs (over.range - 2.0f) < 0.01f,
               "the octave glide stopped at 2 semitones, and the processor says so");
        check (over.semitonesReached() == 12 && over.suggestedRange() == 12, "with 12 the range to set");

        const auto text = rig->synth().describeConnection (rig->processor.getDocument().getSettings().output, over);
        std::printf ("  %s\n", text.toRawUTF8());
        check (text.contains ("A glide just reached 12 semitones and stopped at 2: set both to 12."), "and MPE settings says what to set");

        rig->setOutput ([] (OutputSettings& o) { o.masterBendRange = 12.0f; });
        check (rig->processor.getRecentBendOverflow().isEmpty(), "a changed Zone bend range drops the report");
        rig->play (glideClip(), 1.0);
        check (rig->processor.getRecentBendOverflow().isEmpty(), "and the octave fits again");
    }

    std::printf ("The same on one core as on several\n");
    {
        rig->setOutput ([] (OutputSettings& o) { o.copiesUseCores = false; });
        check (rig->status().workers == 0, "no worker threads with Use several CPU cores off");
        rig->processor.prepareToPlay (CopyRig::rate, CopyRig::block);
        const auto alone = rig->play (glideClip(), 1.0);
        rig->setOutput ([] (OutputSettings& o) { o.copiesUseCores = true; });
        check (rig->status().workers > 0, "worker threads with it on");
        rig->processor.prepareToPlay (CopyRig::rate, CopyRig::block);
        const auto spread = rig->play (glideClip(), 1.0);

        float largest = 0.0f;

        for (int i = 0; i < alone.getNumSamples(); ++i)
            largest = juce::jmax (largest, std::abs (alone.getSample (0, i) - spread.getSample (0, i)));

        std::printf ("  largest difference %g\n", (double) largest);
        check (largest < 1.0e-6f, "the copies sound exactly the same spread over cores");
    }

    std::printf ("Without copies, the synth bends every note together\n");
    {
        rig->setOutput ([] (OutputSettings& o) { o.mode = OutputSettings::Mode::singleChannel; });
        check (rig->status().loaded == 0 && rig->synth().getCopy (1) == nullptr, "Plain MIDI lets the copies go");

        // With Plain MIDI the newest note's glide bends the channel: C4 starts just after E4.
        MpeClip clip;
        clip.length = 8.0;
        clip.addNote (note (64, 0.0, 2.0, 0.8f));
        auto glide = note (60, 0.05, 1.95, 0.8f);
        glide.lanes.pitch = { { 0.0, 0.0f }, { 0.45, 0.0f }, { 1.45, 12.0f } };
        clip.addNote (glide);
        const auto plain = rig->play (clip, 1.0);
        const auto earlyE4 = rig->level (plain, 0.125, e4), lateE4 = rig->level (plain, 0.925, e4);
        std::printf ("  E4 at the start %.4f, at the end %.4f\n", earlyE4, lateE4);
        check (lateE4 < 0.2 * earlyE4, "E4 is carried off by C4's glide (what copies are for)");

        rig->setOutput ([] (OutputSettings& o) { o.mode = OutputSettings::Mode::copies; });
        check (rig->settle() && rig->status().loaded == 4, "back to one copy per note, four copies load again");
    }

    std::printf ("The copies follow the first one's parameters\n");
    {
        rig->setOutput ([] (OutputSettings& o) { o.copiesSleep = false; });
        juce::AudioProcessorParameter* gain = nullptr;

        for (auto* p : rig->processor.getParameters())
            if (p->getName (100) == "Gain")
                gain = p;

        check (gain != nullptr, "the synth's Gain is one of MPE Roll's parameters");

        auto copiesHave = [&] (float value)
        {
            for (int c = 1; c < 4; ++c)
                if (auto* copy = rig->synth().getCopy (c); copy == nullptr || std::abs (copy->getParameters()[0]->getValue() - value) > 1.0e-4f)
                    return false;

            return true;
        };

        if (gain != nullptr)
        {
            gain->setValue (0.25f);     // as host automation does
            rig->idle (50);
            check (copiesHave (0.25f), "automating Gain reaches every copy");
        }

        rig->synth().getCopy (0)->getParameters()[0]->setValueNotifyingHost (0.5f);   // as the synth's own window does
        rig->idle (50);
        check (copiesHave (0.5f), "turning Gain in the first copy reaches every copy");

        rig->synth().getCopy (0)->getParameters()[0]->setValueNotifyingHost (1.0f);
        rig->idle (50);

        // A synth's own pitch-wheel parameter (Serum 1's "Pitch Bend") follows the notes the first
        // copy is sent, and the synth may report it from its window as the wheel it draws moves:
        // that's not handed on. Automating it from the host still reaches every copy.
        constexpr int pitchWheel = 5;
        juce::AudioProcessorParameter* pitchWheelSlot = nullptr;

        for (auto* p : rig->processor.getParameters())
            if (p->getName (100) == "Pitch Bend")
                pitchWheelSlot = p;

        auto copiesWheelAt = [&] (float value)
        {
            for (int c = 1; c < 4; ++c)
                if (auto* copy = rig->synth().getCopy (c); copy == nullptr || std::abs (copy->getParameters()[pitchWheel]->getValue() - value) > 1.0e-4f)
                    return false;

            return true;
        };

        // Nor is it told to the host, which could send it back as automation for every copy.
        struct HeardByHost : juce::AudioProcessorParameter::Listener
        {
            void parameterValueChanged (int, float) override        { ++changes; }
            void parameterGestureChanged (int, bool) override       { ++gestures; }
            int changes = 0, gestures = 0;
        };

        HeardByHost heard;

        if (pitchWheelSlot != nullptr)
            pitchWheelSlot->addListener (&heard);

        rig->synth().getCopy (0)->getParameters()[pitchWheel]->setValueNotifyingHost (0.9f);
        rig->idle (50);
        check (copiesWheelAt (0.5f), "the first copy's own Pitch Bend parameter moving reaches no other copy");
        check (pitchWheelSlot != nullptr && heard.changes == 0 && heard.gestures == 0,
               "nor the host, which could send it back as automation");

        if (pitchWheelSlot != nullptr)
            pitchWheelSlot->removeListener (&heard);

        check (pitchWheelSlot != nullptr, "the synth's Pitch Bend is one of MPE Roll's parameters");

        if (pitchWheelSlot != nullptr)
        {
            pitchWheelSlot->setValue (0.7f);
            rig->idle (50);
            check (copiesWheelAt (0.7f), "automating it from the host reaches every copy");
            pitchWheelSlot->setValue (0.5f);
            rig->idle (50);
        }

        rig->setOutput ([] (OutputSettings& o) { o.copiesSleep = true; });
    }

    std::printf ("The copies follow the first one's sound\n");
    {
        // A synth picking presets may report several changes in a row: the copies get the sound
        // once it has settled, 0.4 seconds after the last.
        rig->synth().getCopy (0)->setCurrentProgram (1);     // Square, and the synth says its program changed
        rig->idle (800);
        check (rig->settle() && rig->copiesMatchFirst(), "a program the synth reports reaches every copy");

        rig->synth().getCopy (0)->setCurrentProgram (3);     // Square (quiet): the synth doesn't say
        rig->idle (50);
        rig->synth().showEditor (nullptr);
        rig->idle (100);
        rig->synth().closeEditor();
        rig->idle (300);
        check (rig->settle() && rig->copiesMatchFirst(), "a change it doesn't report reaches them when its window closes");

        rig->synth().getCopy (0)->setCurrentProgram (0);
        rig->idle (800);
        rig->synth().copySoundToAllCopies();
        check (rig->settle() && rig->copiesMatchFirst(), "Give every copy copy 1's sound does");
    }

    std::printf ("Notes held while the copies are given a sound play on\n");
    {
        rig->setOutput ([] (OutputSettings& o) { o.copiesSleep = false; });

        MpeClip chord;
        chord.length = 8.0;

        for (const auto key : { 60, 64, 67 })
            chord.addNote (note (key, 0.0, 3.0, 0.8f));     // 1.5 seconds at 120 bpm

        // A program the synth reports, partway through: the copies are given it 0.4 s later, those
        // holding the chord once it has ended.
        bool changed = false;
        const auto audio = rig->play (chord, 2.0, [&] (double t)
        {
            if (t >= 0.3 && ! changed)
            {
                changed = true;
                rig->synth().getCopy (0)->setCurrentProgram (2);     // Saw
            }
        });

        auto quietest = 1.0;

        for (const auto key : { 60, 64, 67 })
            quietest = juce::jmin (quietest, rig->level (audio, 1.3, juce::MidiMessage::getMidiNoteInHertz (key)));

        std::printf ("  quietest note of the chord at 1.3 s: %.4f\n", quietest);
        check (quietest > 0.01, "the chord sounds to its end");
        check (rig->settle() && rig->copiesMatchFirst(), "and the copies that held it have the new sound after it");

        // A knob turned with the synth's window open: the copies get it as it moves, and the look
        // taken every 1.5 s while the window is open finds they have the sound already.
        rig->synth().showEditor (nullptr);
        PluginLog::add ("Harness: turning Release");
        const auto turned = rig->play (chord, 3.5, [&] (double t)
        {
            if (t > 0.2 && t < 0.6)
                rig->synth().getCopy (0)->getParameters()[2]->setValueNotifyingHost ((float) t);
        });

        rig->synth().closeEditor();
        rig->idle (300);
        const auto since = PluginLog::tail (40).fromLastOccurrenceOf ("Harness: turning Release", false, false);
        std::printf ("  log since:%s\n", since.replace ("\n", "\n    ").toRawUTF8());
        check (since.contains ("had its sound already") && ! since.contains ("Gave"),
               "the copies, found to have the knob already, aren't given the whole sound again");
        check (rig->settle() && rig->copiesMatchFirst(), "and they match the first copy");
        juce::ignoreUnused (turned);

        rig->synth().getCopy (0)->setCurrentProgram (0);
        rig->synth().getCopy (0)->getParameters()[2]->setValueNotifyingHost (0.0f);
        rig->idle (800);
        check (rig->settle() && rig->copiesMatchFirst(), "back to the first program");
        rig->setOutput ([] (OutputSettings& o) { o.copiesSleep = true; });
    }

    std::printf ("Notes started while the copies are given a sound play to their end\n");
    {
        // A run of notes a quarter of a second apart, each 0.4 s long, and a new program given to
        // every copy as it starts: each copy is given it in turn, between notes, and the copy about
        // to be given it is given no new note meanwhile.
        rig->setOutput ([] (OutputSettings& o) { o.copiesSleep = false; });

        MpeClip run;
        run.length = 8.0;

        for (int i = 0; i < 6; ++i)
            run.addNote (note (60 + 2 * i, 0.5 * i, 0.8, 0.8f));

        bool asked = false;
        const auto audio = rig->play (run, 2.2, [&] (double t)
        {
            if (t >= 0.2 && ! asked)
            {
                asked = true;
                rig->synth().getCopy (0)->setCurrentProgram (2);     // Saw
                rig->synth().copySoundToAllCopies();
            }
        });

        auto quietest = 1.0;

        for (int i = 0; i < 6; ++i)
            quietest = juce::jmin (quietest, rig->level (audio, 0.25 * i + 0.3, juce::MidiMessage::getMidiNoteInHertz (60 + 2 * i)));

        std::printf ("  quietest note near its end: %.4f\n", quietest);
        check (quietest > 0.01, "every note sounds to its end");
        check (rig->settle() && rig->copiesMatchFirst(), "and every copy has the new program after");

        rig->synth().getCopy (0)->setCurrentProgram (0);
        rig->synth().copySoundToAllCopies();
        check (rig->settle() && rig->copiesMatchFirst(), "back to the first program");
        rig->setOutput ([] (OutputSettings& o) { o.copiesSleep = true; });
    }

    std::printf ("Quiet copies sleep, and a note wakes one\n");
    {
        rig->idle (1600);
        auto s = rig->status();
        std::printf ("  after the notes: %d asleep of %d\n", s.asleep, s.loaded);
        check (s.asleep == 4, "a second after the last note, every copy sleeps");

        MpeClip single;
        single.length = 4.0;
        single.addNote (note (69, 0.0, 1.0, 0.8f));
        const auto audio = rig->play (single, 0.4);
        const auto a4 = rig->level (audio, 0.25, juce::MidiMessage::getMidiNoteInHertz (69));
        const auto off = rig->level (audio, 0.25, juce::MidiMessage::getMidiNoteInHertz (69) * 1.06);
        std::printf ("  A4 %.4f, a semitone off %.4f\n", a4, off);
        check (a4 > 0.01 && a4 > 10.0 * off, "a note wakes a copy and plays in tune");
    }

    std::printf ("Keys to every copy\n");
    {
        rig->setOutput ([] (OutputSettings& o) { o.everyCopyKeys = parseKeySet ("C3"); o.copiesSleep = false; });   // key 36
        MpeClip one, every;
        one.length = every.length = 4.0;
        one.addNote (note (38, 0.0, 1.0, 0.8f));
        every.addNote (note (36, 0.0, 1.0, 0.8f));
        const auto oneAudio = rig->play (one, 0.4);
        rig->idle (300);
        const auto everyAudio = rig->play (every, 0.4);
        const auto single = rig->level (oneAudio, 0.25, juce::MidiMessage::getMidiNoteInHertz (38));
        const auto all = rig->level (everyAudio, 0.25, juce::MidiMessage::getMidiNoteInHertz (36));
        std::printf ("  a key of its own %.4f, a key to every copy %.4f\n", single, all);
        check (all > 3.5 * single && all < 4.5 * single, "a key in the every-copy set sounds in all four copies");
        rig->setOutput ([] (OutputSettings& o) { o.everyCopyKeys.reset(); o.copiesSleep = true; });
    }

    std::printf ("More copies, fewer copies\n");
    {
        rig->setOutput ([] (OutputSettings& o) { o.copies = 2; });
        check (rig->settle() && rig->status().loaded == 2 && rig->synth().getCopy (2) == nullptr, "down to two copies");
        rig->setOutput ([] (OutputSettings& o) { o.copies = 6; });
        rig->synth().getCopy (0)->setCurrentProgram (1);     // Square, picked while they load
        check (rig->settle() && rig->status().loaded == 6, "up to six copies");
        rig->idle (800);
        check (rig->settle() && rig->copiesMatchFirst(), "the new copies have the first one's sound, picked while they loaded");

        rig->synth().getCopy (0)->setCurrentProgram (0);
        rig->synth().copySoundToAllCopies();
        rig->settle();
    }

    std::printf ("Loading stops when memory runs low\n");
    {
        CopySafety::pretendAvailableMemoryMB (500);
        rig->setOutput ([] (OutputSettings& o) { o.copies = 8; });
        rig->settle();
        auto s = rig->status();
        std::printf ("  %s\n", s.problem.toRawUTF8());
        check (s.loaded == 6 && s.problem.contains ("memory"), "no more copies with 500 MB free, and it says why");

        CopySafety::pretendAvailableMemoryMB (-2);
        rig->synth().reloadCopies();
        check (rig->settle() && rig->status().loaded == 8 && rig->status().problem.isEmpty(), "Load the copies again loads the rest");
    }

    std::printf ("A crash while loading copies isn't repeated\n");
    {
        const auto* d = rig->synth().getDescription();
        const auto identity = d->pluginFormatName + ":" + juce::String (d->uniqueId != 0 ? d->uniqueId : d->deprecatedUid) + ":" + d->name;

        // Another program loading copies right now (the MPE Roll app beside FL, say) isn't a crash.
        const auto processId = CopySafety::getProcessId();
        const auto started = CopySafety::getProcessStartTime (processId);
        check (started != 0, "a process's start time can be told");
        rig->synth().getUserSettings()->setValue ("copiesLoading.elsewhere",
                                                  "another program|" + juce::String (processId) + "@" + juce::String (started) + "|" + identity);
        rig->setOutput ([] (OutputSettings& o) { o.mode = OutputSettings::Mode::singleChannel; });
        rig->setOutput ([] (OutputSettings& o) { o.mode = OutputSettings::Mode::copies; o.copies = 4; });
        check (rig->settle() && rig->status().loaded == 4 && rig->status().problem.isEmpty(),
               "a running program's marker doesn't stop copies loading");
        check (rig->synth().getUserSettings()->containsKey ("copiesLoading.elsewhere"), "and is left for it");
        rig->synth().getUserSettings()->removeValue ("copiesLoading.elsewhere");

        // What a run that closed while loading a copy leaves behind. Its process ID has since been
        // given to another (this one): that process started later, so it isn't the one.
        rig->synth().getUserSettings()->setValue ("copiesLoading.earlier", "another run|" + juce::String (processId) + "@"
                                                                               + juce::String (started - 1) + "|" + identity);
        rig->setOutput ([] (OutputSettings& o) { o.mode = OutputSettings::Mode::singleChannel; });
        rig->setOutput ([] (OutputSettings& o) { o.mode = OutputSettings::Mode::copies; o.copies = 4; });
        rig->settle();
        auto s = rig->status();
        std::printf ("  %s\n", s.problem.toRawUTF8());
        check (s.loaded == 1 && s.problem.contains ("closed while loading copies"), "no copies are loaded on their own, and it says why");

        rig->synth().reloadCopies();
        check (rig->settle() && rig->status().loaded == 4, "Load the copies again loads them");
        check (! rig->synth().getUserSettings()->containsKey ("copiesLoading.earlier"), "and the marker is gone");
    }

    // A copy that crashes, or throws a C++ exception the synth doesn't catch, in the first piece
    // of a block longer than prepared, with a knob of the first copy just turned (so a change is
    // on its way to it): it's switched off, the rest play on, and nothing calls it again, not the
    // later pieces, nor the host's own timers passing the change on.
    auto copyGoesWrong = [&] (int parameter, const char* how)
    {
        std::printf ("A copy that %s is switched off, and the rest play on\n", how);
        afterCrash.deleteFile();
        rig->setOutput ([] (OutputSettings& o) { o.copiesSleep = false; });

        rig->synth().getCopy (0)->getParameters()[2]->setValueNotifyingHost (0.3f);
        rig->synth().getCopy (2)->getParameters()[parameter]->setValue (1.0f);     // the synth's Crash or Throw
        {
            juce::AudioBuffer<float> longBlock (2, CopyRig::block * 3);
            juce::MidiBuffer midi;
            rig->processor.processBlock (longBlock, midi);
        }

        rig->idle (600);
        auto s = rig->status();
        std::printf ("  %s\n", s.problem.toRawUTF8());
        check (s.crashed == 1 && s.problem.contains ("crashed"), "the copy is switched off, and it says so");

        const auto calledAfter = afterCrash.loadFileAsString().trim();
        check (calledAfter.isEmpty(), "and is never called again" + (calledAfter.isEmpty() ? juce::String() : ": " + calledAfter));

        const auto audio = rig->play (glideClip(), 1.0);
        check (rig->level (audio, 0.125, e4) > 0.01, "the other copies still play");

        rig->synth().reloadCopies();
        check (rig->settle() && rig->status().loaded == 4 && rig->status().crashed == 0, "Load the copies again replaces it");
        check (afterCrash.loadFileAsString().trim().isEmpty(), "and the one that went wrong is still never called");
        rig->synth().getCopy (0)->getParameters()[2]->setValueNotifyingHost (0.0f);
        rig->setOutput ([] (OutputSettings& o) { o.copiesSleep = true; });
    };

    copyGoesWrong (4, "throws");

    if (CopySafety::canCatchCrashes())
        copyGoesWrong (3, "crashes");
    else
        std::printf ("(A crashing copy can only be caught on Windows: not checked here.)\n");


    std::printf ("An export plays its own copies\n");
    {
        const auto output = rig->processor.getDocument().getSettings().output;
        HostedSynth exportSynth;
        exportSynth.setCopySettings (output);
        bool loaded = false;
        exportSynth.restoreState (*rig->synth().saveState(), [&] (const juce::String&) { exportSynth.whenCopiesLoaded ([&] { loaded = true; }); });

        for (int i = 0; i < 600 && ! loaded; ++i)
            CopyRig::pump (50);

        check (loaded && exportSynth.getCopyStatus().loaded == 4, "the export's synth loads four copies of its own");

        AudioExport::Options options;
        options.sampleRate = CopyRig::rate;
        options.bpm = 120.0;
        options.maxTailSeconds = 0.5;
        AudioExport::prepareForRender (exportSynth, options);
        const auto audio = AudioExport::render (glideClip(), output, exportSynth, options);
        const auto lateC5 = rig->level (audio, 0.925, c5), lateE4 = rig->level (audio, 0.925, e4), earlyE4 = rig->level (audio, 0.125, e4);
        std::printf ("  end: C5 %.4f E4 %.4f (E4 at the start %.4f)\n", lateC5, lateE4, earlyE4);
        check (lateC5 > 0.01 && lateE4 > 0.5 * earlyE4, "in the export too, C4 glides while E4 holds");

        // Through the export itself: its window shows while the copies load, and an export that
        // can't load them all says so rather than playing notes together.
        auto exportOnce = [&] (const juce::File& target)
        {
            AudioExport::Options exportOptions;
            exportOptions.sampleRate = CopyRig::rate;
            exportOptions.bpm = 120.0;
            exportOptions.maxTailSeconds = 0.5;
            bool finished = false;
            juce::String result;
            AudioExport::exportClip (rig->processor, glideClip(), target, exportOptions,
                                     [&] (const juce::String& e) { result = e; finished = true; });

            for (int i = 0; i < 1200 && ! finished; ++i)
                CopyRig::pump (50);

            return finished ? result : juce::String ("(didn't finish)");
        };

        const auto wav = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("MPE Roll copies export.wav");
        wav.deleteFile();
        const auto fine = exportOnce (wav);
        check (fine.isEmpty() && wav.existsAsFile(), "an export with copies finishes: " + fine);
        wav.deleteFile();

        CopySafety::pretendAvailableMemoryMB (500);
        const auto shortOfMemory = exportOnce (wav);
        CopySafety::pretendAvailableMemoryMB (-2);
        std::printf ("  %s\n", shortOfMemory.toRawUTF8());
        check (shortOfMemory.contains ("The export needs 4 copies") && ! wav.existsAsFile(),
               "an export that can't load every copy says so, and writes nothing");
    }

    std::printf ("The audio thread plays on while the copies come and go\n");
    {
        // As in FL: blocks on a thread of their own, a chord looping, while the message thread
        // changes the copies under them (more, fewer, threads on and off, sleep, new sounds).
        rig->setOutput ([] (OutputSettings& o) { o.copies = 4; o.copiesSleep = false; });
        rig->settle();

        MpeClip loop;
        loop.length = 2.0;
        loop.looping = true;

        for (const auto key : { 60, 64, 67, 71 })
            loop.addNote (note (key, 0.0, 1.5, 0.8f));

        rig->processor.getDocument().replaceClip (0, loop, "Stress");
        CopyRig::pump (20);

        std::atomic<bool> stop { false };
        std::atomic<int> blocks { 0 };
        rig->playHead.playing = true;

        std::thread audio ([&]
        {
            juce::AudioBuffer<float> buffer (2, CopyRig::block);
            juce::MidiBuffer midi;

            for (juce::int64 pos = 0; ! stop.load(); pos += CopyRig::block)
            {
                buffer.clear();
                midi.clear();
                rig->playHead.ppq = (double) pos / CopyRig::rate * rig->playHead.bpm / 60.0;
                rig->processor.processBlock (buffer, midi);
                ++blocks;
                std::this_thread::sleep_for (std::chrono::milliseconds (2));
            }
        });

        for (int round = 0; round < 8; ++round)
        {
            rig->setOutput ([round] (OutputSettings& o)
            {
                o.copies = round % 2 == 0 ? 8 : 3;
                o.copiesUseCores = round % 3 != 0;
                o.copiesSleep = round % 2 == 1;
            });

            CopyRig::pump (250);
            rig->synth().copySoundToAllCopies();
            CopyRig::pump (150);
            rig->synth().getCopy (0)->setCurrentProgram (round % 3);
            CopyRig::pump (450);
        }

        rig->synth().reloadCopies();
        CopyRig::pump (1500);
        stop = true;
        audio.join();
        rig->playHead.playing = false;

        std::printf ("  %d blocks played meanwhile\n", blocks.load());
        check (blocks.load() > 200, "the audio thread played throughout, never held up for long");
        check (rig->settle() && rig->status().loaded == 3 && rig->copiesMatchFirst(), "and the copies end up loaded, with the first one's sound");
        rig->setOutput ([] (OutputSettings& o) { o.copies = 4; o.copiesSleep = true; o.copiesUseCores = true; });
        rig->settle();
    }

    std::printf ("A project keeps its copies\n");
    {
        rig->synth().getCopy (0)->setCurrentProgram (2);     // Saw
        rig->idle (100);
        rig->settle();
        juce::MemoryBlock project;
        rig->processor.getStateInformation (project);
        rig.reset();

        auto reopened = std::make_unique<CopyRig>();
        reopened->synth().useSettingsFile (settingsFile);     // not the real MPE Roll settings
        reopened->processor.setStateInformation (project.getData(), (int) project.getSize());

        for (int i = 0; i < 200 && reopened->synth().getCopy (0) == nullptr; ++i)
            CopyRig::pump (50);

        check (reopened->settle() && reopened->status().loaded == 4, "opening the project loads the four copies");
        check (reopened->copiesMatchFirst(), "with the project's sound in every one");
    }

    settingsFile.deleteFile();
    afterCrash.deleteFile();
    std::printf ("%s\n", check.failures == 0 ? "PASS" : "FAIL");
    return check.failures == 0 ? 0 : 1;
}

/** A synth with a parameter that follows the pitch bend it's sent (Analog Lab and Kontakt give each
    channel one) reports every move from its block, with or without an edit gesture around it. That's
    the synth following its notes, not an edit: passed on to the host, each move would be an entry in
    FL's undo history. Checked in Plain MIDI mode and in One copy per note, with the plain test synth's
    "channel1 pitch bend" (which only its name and the block it's reported from tell apart, as for a
    VST2 synth, or a VST3 whose mapping says nothing); a setting of the synth's changed from elsewhere
    still reaches the host.
*/
int bendTweaksTest (const juce::File& synthFile)
{
    Check check;
    ignoreXErrors();

    // Each move of the follower comes in a gesture of its own, as a synth whose window echoes the
    // move might send it (its VST3 build passes gestures on only from the message thread).
   #if JUCE_WINDOWS
    _putenv_s ("MPEROLL_TEST_BEND_GESTURES", "1");
   #else
    setenv ("MPEROLL_TEST_BEND_GESTURES", "1", 1);
   #endif

    const auto settingsFile = juce::File::createTempFile ("settings");
    auto rig = std::make_unique<CopyRig>();
    rig->synth().useSettingsFile (settingsFile);
    rig->setOutput ([] (OutputSettings& o)
    {
        o.mode = OutputSettings::Mode::singleChannel;
        o.masterBendRange = 12.0f;
    });

    if (! loadSynth (rig->processor, synthFile))
        return 1;

    std::printf ("Hosting: %s\n", rig->synth().getName().toRawUTF8());

    struct HeardByHost : juce::AudioProcessorParameter::Listener
    {
        void parameterValueChanged (int, float) override        { ++changes; }
        void parameterGestureChanged (int, bool) override       { ++gestures; }
        int changes = 0, gestures = 0;
    };

    auto parameterOf = [] (juce::AudioProcessor* plugin, const juce::String& name) -> juce::AudioProcessorParameter*
    {
        if (plugin != nullptr)
            for (auto* p : plugin->getParameters())
                if (p->getName (100) == name)
                    return p;

        return nullptr;
    };

    auto* followerSlot = parameterOf (&rig->processor, "channel1 pitch bend");
    auto* rangeSlot = parameterOf (&rig->processor, "Bend range");
    check (followerSlot != nullptr && rangeSlot != nullptr,
           "the synth's channel1 pitch bend and Bend range are among MPE Roll's parameters");

    if (followerSlot == nullptr || rangeSlot == nullptr)
    {
        rig.reset();
        settingsFile.deleteFile();
        return 1;
    }

    // One note, with the zone bending up six semitones over two beats: on Plain MIDI's one channel,
    // that's pitch bend on channel 1, which the synth's follower tracks.
    MpeClip bending;
    bending.length = 8.0;
    bending.addNote (note (60, 0.0, 4.0, 0.8f));
    bending.zone.pitch = { { 0.0, 0.0f }, { 2.0, 6.0f } };

    // How far from rest (0.5) a plugin's follower gets while a clip plays: read as it plays, since
    // stopping the transport sends the bend back to the centre.
    auto farthestWhilePlaying = [&] (const MpeClip& clip, double seconds, const std::function<juce::AudioProcessor*()>& plugin)
    {
        float farthest = 0.5f;

        rig->play (clip, seconds, [&] (double)
        {
            if (auto* p = parameterOf (plugin(), "channel1 pitch bend"))
                if (std::abs (p->getValue() - 0.5f) > std::abs (farthest - 0.5f))
                    farthest = p->getValue();
        });

        return farthest;
    };

    std::printf ("Plain MIDI: the synth's channel1 pitch bend follows the bend, unheard by the host\n");
    {
        HeardByHost heard;
        followerSlot->addListener (&heard);
        const auto reached = farthestWhilePlaying (bending, 1.5, [&] { return rig->synth().getInstance(); });
        followerSlot->removeListener (&heard);

        std::printf ("  the synth's parameter reached %.3f; the host heard %d change(s) and %d gesture(s)\n",
                     (double) reached, heard.changes, heard.gestures);
        check (std::abs (reached - 0.5f) > 0.05f, "the synth's parameter moved with the bend");
        check (heard.changes == 0, "the host heard no change of it");
        check (heard.gestures == 0, "nor a gesture around one");
    }

    std::printf ("A setting of the synth's, changed from elsewhere, still reaches the host\n");
    {
        HeardByHost heard;
        rangeSlot->addListener (&heard);

        // From the message thread, as the synth's own window would change it.
        if (auto* range = parameterOf (rig->synth().getInstance(), "Bend range"))
            range->setValueNotifyingHost (0.5f);

        rig->idle (50);
        rangeSlot->removeListener (&heard);
        std::printf ("  the host heard %d change(s)\n", heard.changes);
        check (heard.changes >= 1, "as a change");
    }

    std::printf ("One copy per note: the gliding copy's follower moves neither the other copies' nor the host\n");
    {
        rig->setOutput ([] (OutputSettings& o)
        {
            o.mode = OutputSettings::Mode::copies;
            o.copies = 4;
            o.masterBendRange = 12.0f;
        });
        check (rig->settle() && rig->status().loaded == 4, "four copies load");

        HeardByHost heard;
        followerSlot->addListener (&heard);
        std::array<float, 4> farthest { 0.5f, 0.5f, 0.5f, 0.5f };

        // C4 glides up an octave on its copy while E4 holds on another.
        rig->play (glideClip(), 1.0, [&] (double)
        {
            for (int c = 0; c < 4; ++c)
                if (auto* p = parameterOf (rig->synth().getCopy (c), "channel1 pitch bend"))
                    if (std::abs (p->getValue() - 0.5f) > std::abs (farthest[(size_t) c] - 0.5f))
                        farthest[(size_t) c] = p->getValue();
        });

        followerSlot->removeListener (&heard);

        int moved = 0, atRest = 0;

        for (const auto value : farthest)
            ++(std::abs (value - 0.5f) > 0.05f ? moved : atRest);

        std::printf ("  followers moved on %d copy(ies), at rest on %d; the host heard %d change(s) and %d gesture(s)\n",
                     moved, atRest, heard.changes, heard.gestures);
        check (moved == 1, "the copy that played the glide moved its follower");
        check (atRest == 3, "the others kept theirs at rest: the move wasn't copied to them");
        check (heard.changes == 0 && heard.gestures == 0, "and the host heard nothing");
    }

    rig.reset();
    settingsFile.deleteFile();
    std::printf ("%s\n", check.failures == 0 ? "PASS" : "FAIL");
    return check.failures == 0 ? 0 : 1;
}

/** The clip stopped by the host, through the plain test synth. All Notes Off (CC 123) or All Sound
    Off (CC 120) from the host, which FL may send for a channel switched off in the Channel Rack,
    silences Follow FL until the transport restarts or jumps (a loop included) or the host starts a
    note, whatever clip slot is picked meanwhile, and the first is logged; one sent as the transport
    starts or jumps is the host tidying up, which ends the notes but pauses nothing. In Note trigger
    mode it stops the clip as the trigger note's note-off would. With the clip following the host's
    notes (PlaybackSettings::gateByHostNotes) it sounds only while the host holds a note, at the
    song position, without a break where two of them touch; the host's notes themselves aren't
    played, but are recorded, and switching the gate on ends one being played through. Messages
    come at samples inside blocks as well as at their starts. Everything is read from the stream
    MPE Roll sends the synth, which its MIDI output carries. MPE Roll's processBlock is called
    directly, so how a host's MIDI reaches its VST3 or VST2 isn't covered here (vst3hostmidi
    covers the VST3's controller mapping).
*/
int gateTest (const juce::File& synthFile)
{
    Check check;
    ignoreXErrors();

    const auto settingsFile = juce::File::createTempFile ("settings");
    const auto logFile = juce::File::createTempFile ("log");
    PluginLog::setFileForTesting (logFile);

    auto rig = std::make_unique<CopyRig>();
    rig->synth().useSettingsFile (settingsFile);     // not the real MPE Roll settings
    rig->setOutput ([] (OutputSettings& o) { o.mode = OutputSettings::Mode::singleChannel; });

    auto finish = [&]
    {
        rig.reset();
        PluginLog::setFileForTesting ({});
        logFile.deleteFile();
        settingsFile.deleteFile();
    };

    if (! loadSynth (rig->processor, synthFile))
    {
        finish();
        return 1;
    }

    std::printf ("Hosting: %s\n", rig->synth().getName().toRawUTF8());

    // C4 held for the whole clip, so something always sounds; G4 every half beat for the first two
    // beats and E4 every half beat after them (four note-ons a second at 120 bpm), so note-ons keep
    // coming while the clip plays and say where in the clip it is.
    MpeClip clip;
    clip.length = 16.0;
    clip.addNote (note (60, 0.0, 16.0, 0.8f));

    for (int i = 0; i < 32; ++i)
        clip.addNote (note (i < 4 ? 67 : 64, i * 0.5, 0.4, 0.8f));

    rig->processor.getDocument().replaceClip (0, clip, "Test");
    CopyRig::pump (10);

    auto setPlayback = [&] (const std::function<void (PlaybackSettings&)>& change)
    {
        auto s = rig->processor.getDocument().getSettings();
        change (s.playback);
        rig->processor.getDocument().setSettings (s);
        CopyRig::pump (30);
    };

    auto setGate = [&] (bool on) { setPlayback ([on] (PlaybackSettings& p) { p.gateByHostNotes = on; }); };

    // The host's notes, below the clip's, so they'd stand out if they were played.
    constexpr int gateKey = 36, otherKey = 38;  // C2, D2

    // What MPE Roll sent the synth: each message with the time its block started at, and its own
    // sample, counted from the start of the play.
    struct Sent { double time; int sample; juce::MidiMessage message; };

    // A message from the host: in the first block starting at or after `time`, `offset` samples into
    // that block (at its start unless given).
    struct HostEvent { double time; juce::MidiMessage message; int offset = 0; };
    using Input = std::function<void (double, juce::MidiBuffer&)>;

    auto hostSends = [] (std::vector<HostEvent> events) -> Input
    {
        auto pending = std::make_shared<std::vector<HostEvent>> (std::move (events));

        return [pending] (double time, juce::MidiBuffer& midi)
        {
            for (auto it = pending->begin(); it != pending->end();)
            {
                if (time < it->time)
                {
                    ++it;
                    continue;
                }

                midi.addEvent (it->message, it->offset);
                it = pending->erase (it);
            }
        };
    };

    // The sample, counted from the start of a play, that a host message at `time` and `offset` is at.
    const auto sampleOf = [] (double time, int offset)
    {
        return (int) std::ceil (time * CopyRig::rate / CopyRig::block) * CopyRig::block + offset;
    };

    // Plays the transport from the clip's start for this long, the host's MIDI in each block being
    // what `input` puts there, and returns everything MPE Roll sent the synth. From `jumpAt` seconds
    // on, the song is `jumpBy` seconds further on: a second, as after a click on FL's timeline, or
    // back to an earlier time, as when FL loops a pattern or the song.
    auto play = [&] (double seconds, const Input& input, double jumpAt = -1.0, double jumpBy = 1.0)
    {
        std::vector<Sent> sent;
        const auto total = (int) (seconds * CopyRig::rate);
        rig->playHead.playing = true;

        for (int pos = 0; pos < total; pos += CopyRig::block)
        {
            const auto time = pos / CopyRig::rate;
            const auto songTime = jumpAt >= 0.0 && time >= jumpAt ? time + jumpBy : time;
            const auto n = juce::jmin (CopyRig::block, total - pos);
            juce::AudioBuffer<float> audio (2, n);
            juce::MidiBuffer midi;

            if (input)
                input (time, midi);

            rig->playHead.ppq = songTime * rig->playHead.bpm / 60.0;
            rig->processor.processBlock (audio, midi);

            for (const auto m : midi)
                sent.push_back ({ time, pos + m.samplePosition, m.getMessage() });
        }

        rig->playHead.playing = false;
        rig->idle (100);
        return sent;
    };

    // The messages MPE Roll sent from `from` up to `to` seconds that are `which`.
    auto count = [] (const std::vector<Sent>& sent, double from, double to,
                     const std::function<bool (const juce::MidiMessage&)>& which)
    {
        int n = 0;

        for (const auto& s : sent)
            if (s.time >= from && s.time < to && which (s.message))
                ++n;

        return n;
    };

    // Note-ons of one key, or of the clip's (C4 and up).
    auto noteOns = [&] (const std::vector<Sent>& sent, double from, double to, int key = -1)
    {
        return count (sent, from, to, [key] (const juce::MidiMessage& m)
                      { return m.isNoteOn() && (key < 0 ? m.getNoteNumber() >= 60 : m.getNoteNumber() == key); });
    };

    // Note-offs of one key, or of any.
    auto noteOffs = [&] (const std::vector<Sent>& sent, double from, double to, int key = -1)
    {
        return count (sent, from, to, [key] (const juce::MidiMessage& m)
                      { return m.isNoteOff() && (key < 0 || m.getNoteNumber() == key); });
    };

    // The same by the messages' own samples, from `from` up to `to`, for what happens inside a block.
    auto countAt = [] (const std::vector<Sent>& sent, int from, int to,
                       const std::function<bool (const juce::MidiMessage&)>& which)
    {
        int n = 0;

        for (const auto& s : sent)
            if (s.sample >= from && s.sample < to && which (s.message))
                ++n;

        return n;
    };

    auto noteOnsAt = [&] (const std::vector<Sent>& sent, int from, int to, int key = -1)
    {
        return countAt (sent, from, to, [key] (const juce::MidiMessage& m)
                        { return m.isNoteOn() && (key < 0 ? m.getNoteNumber() >= 60 : m.getNoteNumber() == key); });
    };

    auto noteOffsAt = [&] (const std::vector<Sent>& sent, int from, int to, int key = -1)
    {
        return countAt (sent, from, to, [key] (const juce::MidiMessage& m)
                        { return m.isNoteOff() && (key < 0 || m.getNoteNumber() == key); });
    };

    // A message for time t comes at the start of the first block starting at or after t, and what
    // MPE Roll sends is stamped with its block's start: so from t on is what came with the message
    // and after it, and up to t what came before. The clip starts a note every 0.25 s.
    const auto noteOn = [] (int key) { return juce::MidiMessage::noteOn (1, key, (juce::uint8) 100); };
    const auto noteOff = [] (int key) { return juce::MidiMessage::noteOff (1, key); };

    std::printf ("Follow FL plays the clip while the transport runs\n");
    {
        const auto sent = play (1.0, {});
        std::printf ("  %d note-ons in a second\n", noteOns (sent, 0.0, 1.0));
        check (noteOns (sent, 0.0, 1.0) >= 4, "the clip's notes reach the synth");
    }

    std::printf ("All Notes Off from the host silences the clip until the transport restarts\n");
    {
        const auto sent = play (2.0, hostSends ({ { 1.0, juce::MidiMessage::allNotesOff (1) } }));
        std::printf ("  before it: %d note-ons; with it: %d note-offs (%d of the held C4); after it: %d note-ons\n",
                     noteOns (sent, 0.0, 1.0), noteOffs (sent, 1.0, 1.02), noteOffs (sent, 1.0, 1.02, 60),
                     noteOns (sent, 1.0, 2.0));
        check (noteOns (sent, 0.0, 1.0) >= 4, "the clip played until then");
        check (noteOffs (sent, 1.0, 1.02, 60) >= 1, "the synth was sent note-offs for what sounded");
        check (noteOns (sent, 1.0, 2.0) == 0, "and no more notes for the rest of that play");

        const auto again = play (1.0, {});
        std::printf ("  stopped and started: %d note-ons in a second\n", noteOns (again, 0.0, 1.0));
        check (noteOns (again, 0.0, 1.0) >= 4, "stopped and started, the clip plays again");

        const auto log = PluginLog::tail (3);
        std::printf ("  log: %s\n", log.trimEnd().replace ("\n", "\n       ").toRawUTF8());
        check (log.contains ("The host sent All Notes Off (CC 123): playback paused until the transport restarts"),
               "the plugin log says the host sent it");
    }

    std::printf ("All Sound Off too, but not as the transport starts; a jump or a note from the host ends the silence\n");
    {
        const auto sent = play (2.5, hostSends ({ { 0.0, juce::MidiMessage::allSoundOff (1) },
                                                  { 0.5, juce::MidiMessage::allSoundOff (1) },
                                                  { 1.5, juce::MidiMessage::allSoundOff (1) },
                                                  { 2.0, noteOn (gateKey) },
                                                  { 2.25, noteOff (gateKey) } }),
                                1.0);
        std::printf ("  note-ons: %d with the transport's start, %d after the next mid-play, %d after the jump,"
                     " %d after the next, %d after the host's note (and %d of its key)\n",
                     noteOns (sent, 0.0, 0.5), noteOns (sent, 0.5, 1.0), noteOns (sent, 1.0, 1.5),
                     noteOns (sent, 1.5, 2.0), noteOns (sent, 2.0, 2.5), noteOns (sent, 2.0, 2.5, gateKey));
        check (noteOns (sent, 0.0, 0.5) >= 2, "one sent as the transport starts leaves the clip playing");
        check (noteOns (sent, 0.5, 1.0) == 0, "one mid-play silences it");
        check (noteOns (sent, 1.0, 1.5) >= 1, "a jump of the song position plays it again");
        check (noteOns (sent, 1.5, 2.0) == 0, "silenced again");
        check (noteOns (sent, 2.0, 2.5) >= 1, "a note from the host plays it again");
        check (noteOns (sent, 2.0, 2.5, gateKey) == 1, "and the note itself is played, the gate being off");
    }

    std::printf ("Switched on, the gate ends a note the host was playing through MPE Roll\n");
    {
        // With the transport stopped: the gate would take the note's note-off, so it's ended now.
        int ons = 0, offs = 0;

        auto block = [&] (const std::vector<juce::MidiMessage>& input)
        {
            juce::AudioBuffer<float> audio (2, CopyRig::block);
            juce::MidiBuffer midi;

            for (const auto& m : input)
                midi.addEvent (m, 0);

            rig->playHead.playing = false;
            rig->processor.processBlock (audio, midi);

            for (const auto m : midi)
            {
                ons += m.getMessage().isNoteOn() && m.getMessage().getNoteNumber() == gateKey ? 1 : 0;
                offs += m.getMessage().isNoteOff() && m.getMessage().getNoteNumber() == gateKey ? 1 : 0;
            }
        };

        block ({ noteOn (gateKey) });
        setGate (true);
        block ({});
        std::printf ("  the host's note: %d note-on, then %d note-off as the gate was switched on\n", ons, offs);
        check (ons == 1 && offs == 1, "it ends when the gate is switched on, instead of sounding on");

        block ({ noteOff (gateKey) });
        check (offs == 1, "and its own note-off, taken by the gate, adds nothing");
    }

    std::printf ("Following the host's notes: nothing sounds without one\n");
    {
        setGate (true);
        const auto sent = play (2.0, {});
        std::printf ("  %d note-ons in two seconds\n", noteOns (sent, 0.0, 2.0));
        check (noteOns (sent, 0.0, 2.0) == 0, "no note reaches the synth in two seconds of transport");
    }

    std::printf ("A note from the host opens the gate, unplayed itself; its note-off shuts it\n");
    {
        // Held from beat 1 (0.5 s), in the clip's G4 half, to beat 3 (1.5 s); the clip plays E4 from beat 2.
        const auto sent = play (2.5, hostSends ({ { 0.5, noteOn (gateKey) }, { 1.5, noteOff (gateKey) } }));
        std::printf ("  before: %d note-ons; held: %d note-ons (C4 %d, then from beat 2 E4 %d and G4 %d), %d of"
                     " the host's key; let go: %d note-offs, then %d note-ons\n",
                     noteOns (sent, 0.0, 0.5), noteOns (sent, 0.5, 1.5), noteOns (sent, 0.5, 1.5, 60),
                     noteOns (sent, 1.0, 1.5, 64), noteOns (sent, 1.0, 1.5, 67), noteOns (sent, 0.0, 2.5, gateKey),
                     noteOffs (sent, 1.5, 1.52), noteOns (sent, 1.5, 2.5));
        check (noteOns (sent, 0.0, 0.5) == 0, "nothing before the host's note");
        check (noteOns (sent, 0.5, 1.5) >= 3, "the clip plays while it's held");
        check (noteOns (sent, 0.5, 0.52, 60) == 1, "with what's already under the playhead (C4, held from the clip's start)");
        check (noteOns (sent, 1.0, 1.5, 64) >= 1 && noteOns (sent, 1.0, 1.5, 67) == 0,
               "at the song position, not from the clip's start");
        check (noteOns (sent, 0.0, 2.5, gateKey) == 0, "the host's note itself doesn't reach the synth");
        check (noteOffs (sent, 1.5, 1.52) >= 1, "its note-off stops what sounded");
        check (noteOns (sent, 1.5, 2.5) == 0, "and nothing more plays");
    }

    std::printf ("All Notes Off while the host's note is held shuts the gate; the host's next note opens it\n");
    {
        // After the first All Notes Off the song position jumps, which ends the silence, so only the
        // gate keeps the clip quiet. The note-off of the D2 the second one cleared then comes while
        // C2 holds the gate open: no longer counted, it must not shut it.
        const auto sent = play (3.0, hostSends ({ { 0.0, noteOn (gateKey) },
                                                  { 0.5, juce::MidiMessage::allNotesOff (1) },
                                                  { 1.0, noteOn (otherKey) },
                                                  { 1.5, juce::MidiMessage::allNotesOff (1) },
                                                  { 2.0, noteOn (gateKey) },
                                                  { 2.25, noteOff (otherKey) },
                                                  { 2.75, noteOff (gateKey) } }),
                                0.75);
        std::printf ("  note-ons: %d with C2 held; All Notes Off: %d note-offs, then %d (a jump among them); D2: %d;"
                     " All Notes Off: %d; C2: %d; D2's note-off: then %d; C2's: then %d\n",
                     noteOns (sent, 0.0, 0.5), noteOffs (sent, 0.5, 0.52), noteOns (sent, 0.5, 1.0),
                     noteOns (sent, 1.0, 1.5), noteOns (sent, 1.5, 2.0), noteOns (sent, 2.0, 2.25),
                     noteOns (sent, 2.25, 2.75), noteOns (sent, 2.75, 3.0));
        check (noteOns (sent, 0.0, 0.5) >= 2, "the clip plays while the host's note is held");
        check (noteOffs (sent, 0.5, 0.52) >= 1, "All Notes Off stops what sounded");
        check (noteOns (sent, 0.5, 1.0) == 0, "and shuts the gate: silent, even after the song position jumps");
        check (noteOns (sent, 1.0, 1.5) >= 1, "the host's next note opens it");
        check (noteOns (sent, 1.5, 2.0) == 0, "All Notes Off again: silent");
        check (noteOns (sent, 2.0, 2.25) >= 1, "the host's next note opens it again");
        check (noteOns (sent, 2.25, 2.75) >= 1, "the note-off of a note All Notes Off cleared leaves it open");
        check (noteOns (sent, 2.75, 3.0) == 0, "the held note's note-off shuts it");
    }

    std::printf ("Notes from the host that touch keep the gate open, whichever of the two comes first\n");
    {
        // C2 held from the start. Then, each pair at one sample inside its block: C2's note-off and C2
        // again; C2's note-off and D2's note-on; C2's note-on and D2's note-off. Then C2's note-off
        // shuts the gate. C4, held through the clip, mustn't start again where the notes touch.
        const auto shut = sampleOf (2.0, 50);
        const auto end = (int) (2.5 * CopyRig::rate);
        const auto sent = play (2.5, hostSends ({ { 0.0, noteOn (gateKey) },
                                                  { 0.5, noteOff (gateKey), 300 }, { 0.5, noteOn (gateKey), 300 },
                                                  { 1.0, noteOff (gateKey), 200 }, { 1.0, noteOn (otherKey), 200 },
                                                  { 1.5, noteOn (gateKey), 100 }, { 1.5, noteOff (otherKey), 100 },
                                                  { 2.0, noteOff (gateKey), 50 } }));
        const auto between = noteOnsAt (sent, sampleOf (0.5, 300), shut, 64) + noteOnsAt (sent, sampleOf (0.5, 300), shut, 67);
        std::printf ("  C4: %d note-on(s), %d note-off(s) before the last note-off, %d at it; E4 and G4 in between:"
                     " %d note-ons; after it: %d\n",
                     noteOnsAt (sent, 0, end, 60), noteOffsAt (sent, 0, shut, 60), noteOffsAt (sent, shut, shut + 1, 60),
                     between, noteOnsAt (sent, shut + 1, end));
        check (noteOnsAt (sent, 0, end, 60) == 1 && noteOffsAt (sent, 0, shut, 60) == 0,
               "C4 starts once and plays on where the host's notes touch, note-off first or note-on first");
        check (between >= 5, "and the clip's other notes keep coming");
        check (noteOffsAt (sent, shut, shut + 1, 60) == 1, "the last note-off shuts the gate, at its sample");
        check (noteOnsAt (sent, shut + 1, end) == 0, "and nothing plays after it");
        check (noteOnsAt (sent, 0, end, gateKey) + noteOnsAt (sent, 0, end, otherKey) == 0, "the host's notes aren't played");
    }

    std::printf ("All Notes Off in the block the transport starts or the song jumps in shuts the gate too\n");
    {
        // The host holds C2 and never sends its note-off, only All Notes Off: in the block the song
        // jumps in, in the one the transport starts in, and later in a jump's block. It holds no note
        // after it, so the gate shuts, though the clip isn't paused as it is for one mid-play.
        const auto inJump = play (1.5, hostSends ({ { 0.0, noteOn (gateKey) }, { 0.5, juce::MidiMessage::allNotesOff (1) } }),
                                  0.5);
        const auto restarted = play (1.0, {});
        play (0.5, hostSends ({ { 0.0, noteOn (gateKey) } }));     // stopped with C2 held
        const auto atStart = play (1.0, hostSends ({ { 0.0, juce::MidiMessage::allNotesOff (1) } }));
        const auto at = sampleOf (0.5, 200);
        const auto later = play (1.0, hostSends ({ { 0.0, noteOn (gateKey) }, { 0.5, juce::MidiMessage::allNotesOff (1), 200 } }),
                                 0.5);
        std::printf ("  note-ons: %d with C2 held, %d after a jump with All Notes Off, %d on the next play, %d after one"
                     " as the transport starts; one later in a jump's block: C4's note-off at it %d, then %d note-ons\n",
                     noteOns (inJump, 0.0, 0.5), noteOns (inJump, 0.5, 1.5), noteOns (restarted, 0.0, 1.0),
                     noteOns (atStart, 0.0, 1.0), noteOffsAt (later, at, at + 1, 60), noteOnsAt (later, at + 1, (int) CopyRig::rate));
        check (noteOns (inJump, 0.0, 0.5) >= 2, "the clip plays while the host's note is held");
        check (noteOns (inJump, 0.5, 1.5) == 0, "All Notes Off in the block the song jumps in shuts the gate");
        check (noteOns (restarted, 0.0, 1.0) == 0, "which stays shut when the transport starts again");
        check (noteOns (atStart, 0.0, 1.0) == 0, "All Notes Off as the transport starts shuts a gate left open");
        check (noteOffsAt (later, at, at + 1, 60) == 1 && noteOnsAt (later, at + 1, (int) CopyRig::rate) == 0,
               "one later in a jump's block ends the notes started at the jump, at its sample");
    }

    std::printf ("Picking another clip slot leaves the host's silence standing\n");
    {
        setGate (false);
        auto& doc = rig->processor.getDocument();
        doc.replaceClip (1, clip, "Test");
        CopyRig::pump (10);

        // All Notes Off mid-play; slot 2 picked at 1 s, while silent; a note from the host at 1.5 s;
        // slot 1 picked again at 2 s, with All Notes Off in the same block.
        const auto host = hostSends ({ { 0.5, juce::MidiMessage::allNotesOff (1), 128 },
                                       { 1.5, noteOn (otherKey) }, { 1.75, noteOff (otherKey) },
                                       { 2.0, juce::MidiMessage::allNotesOff (1) } });
        const auto sent = play (2.5, [&doc, host] (double time, juce::MidiBuffer& midi)
        {
            doc.setEditSlot (time >= 1.0 && time < 2.0 ? 1 : 0);
            host (time, midi);
        });
        std::printf ("  note-ons: %d before All Notes Off, %d after it with slot 2 picked, %d after the host's note,"
                     " %d after slot 1 is picked with All Notes Off\n",
                     noteOns (sent, 0.0, 0.5), noteOns (sent, 0.5, 1.5), noteOns (sent, 1.5, 2.0), noteOns (sent, 2.0, 2.5));
        check (noteOns (sent, 0.0, 0.5) >= 2, "the clip plays until All Notes Off");
        check (noteOns (sent, 0.5, 1.5) == 0, "picking another slot while it's silent doesn't play that one");
        check (noteOns (sent, 1.5, 2.0) >= 1, "a note from the host does");
        check (noteOns (sent, 2.0, 2.5) == 0, "All Notes Off in the block a slot is picked in silences it: that's no restart");
        doc.setEditSlot (0);
    }

    std::printf ("A loop of the song or pattern ends the silence, as a jump does\n");
    {
        // The song goes back to its start at 0.75 s, as when FL loops a pattern.
        const auto sent = play (1.5, hostSends ({ { 0.25, juce::MidiMessage::allNotesOff (1), 64 } }), 0.75, -0.75);
        std::printf ("  note-ons: %d after All Notes Off, %d after the song starts over\n",
                     noteOns (sent, 0.25, 0.75), noteOns (sent, 0.75, 1.5));
        check (noteOns (sent, 0.25, 0.75) == 0, "All Notes Off silences the clip");
        check (noteOns (sent, 0.75, 1.5) >= 2, "the song starting over plays it again");
    }

    std::printf ("All Notes Off inside a block: the clip plays up to its sample and stops there\n");
    {
        // E4 starts at beat 2, sample 48000 give or take one, 384 samples into its block; All Notes Off
        // comes 400 samples into the same block.
        const auto block = sampleOf (0.99, 0);
        const auto at = sampleOf (0.99, 400);
        const auto end = (int) (1.5 * CopyRig::rate);
        const auto sent = play (1.5, hostSends ({ { 0.99, juce::MidiMessage::allNotesOff (1), 400 } }));
        std::printf ("  E4's note-on near sample 48000: %d; at sample %d, All Notes Off's: note-offs of C4 %d and E4 %d;"
                     " note-ons after it: %d\n",
                     noteOnsAt (sent, 47999, 48002, 64), at, noteOffsAt (sent, at, at + 1, 60), noteOffsAt (sent, at, at + 1, 64),
                     noteOnsAt (sent, at + 1, end));
        check (noteOnsAt (sent, 47999, 48002, 64) == 1 && noteOnsAt (sent, block, at, 64) == 1,
               "E4 starts at its own sample, before the message in its block");
        check (noteOffsAt (sent, at, at + 1, 60) == 1 && noteOffsAt (sent, at, at + 1, 64) == 1,
               "C4 and E4 end at the message's sample");
        check (noteOnsAt (sent, at + 1, end) == 0, "and nothing plays after it");
    }

    std::printf ("MPE output, the clip on the zone's member channels: the gate and All Notes Off alike\n");
    {
        rig->setOutput ([] (OutputSettings& o) { o.mode = OutputSettings::Mode::mpe; });
        setGate (true);
        const auto at = sampleOf (1.0, 200);
        const auto end = (int) (1.5 * CopyRig::rate);
        const auto sent = play (1.5, hostSends ({ { 0.0, noteOn (gateKey) },
                                                  { 0.5, noteOff (gateKey), 300 }, { 0.5, noteOn (otherKey), 300 },
                                                  { 1.0, juce::MidiMessage::allNotesOff (1), 200 } }));
        int channel = 0;

        for (const auto& s : sent)
            if (s.message.isNoteOn() && s.message.getNoteNumber() == 60)
                channel = s.message.getChannel();

        std::printf ("  C4 on channel %d: %d note-on(s), %d note-off(s) before All Notes Off, %d at it; note-ons after it: %d\n",
                     channel, noteOnsAt (sent, 0, end, 60), noteOffsAt (sent, 0, at, 60), noteOffsAt (sent, at, at + 1, 60),
                     noteOnsAt (sent, at + 1, end));
        check (channel >= 2, "C4 is played on a member channel");
        check (noteOnsAt (sent, 0, end, 60) == 1 && noteOffsAt (sent, 0, at, 60) == 0, "notes from the host that touch keep it playing");
        check (noteOffsAt (sent, at, at + 1, 60) == 1 && noteOnsAt (sent, at + 1, end) == 0,
               "All Notes Off ends it at its sample, and shuts the gate");
        rig->setOutput ([] (OutputSettings& o) { o.mode = OutputSettings::Mode::singleChannel; });
    }

    std::printf ("Note trigger: All Sound Off or All Notes Off from the host stops a clip a note started\n");
    {
        setPlayback ([] (PlaybackSettings& p) { p.mode = PlaybackSettings::Mode::noteTrigger; p.triggerBaseKey = 0; });

        // Key 0 starts slot 1's clip, and All Sound Off comes inside a block, with no note-off for the
        // key. Then the key's note-off, late, which changes nothing; the key again, and its note-off.
        const auto at = sampleOf (0.5, 100);
        const auto stopped = play (1.0, hostSends ({ { 0.0, noteOn (0) }, { 0.5, juce::MidiMessage::allSoundOff (1), 100 } }));
        const auto later = play (1.0, {});
        const auto off = sampleOf (1.0, 50);
        const auto again = play (1.5, hostSends ({ { 0.0, noteOff (0) }, { 0.25, noteOn (0) }, { 1.0, noteOff (0), 50 } }));
        std::printf ("  note-ons: %d before All Sound Off; at its sample C4's note-off %d, then %d note-ons, and %d on the"
                     " next play; the key again: %d, then at its note-off C4's %d, and %d note-ons after\n",
                     noteOns (stopped, 0.0, 0.5), noteOffsAt (stopped, at, at + 1, 60), noteOnsAt (stopped, at + 1, (int) CopyRig::rate),
                     noteOns (later, 0.0, 1.0), noteOns (again, 0.25, 1.0), noteOffsAt (again, off, off + 1, 60),
                     noteOnsAt (again, off + 1, (int) (1.5 * CopyRig::rate)));
        check (noteOns (stopped, 0.0, 0.5) >= 2, "key 0 starts slot 1's clip");
        check (noteOffsAt (stopped, at, at + 1, 60) == 1 && noteOnsAt (stopped, at + 1, (int) CopyRig::rate) == 0,
               "All Sound Off stops it at its sample, as the key's note-off would");
        check (noteOns (later, 0.0, 1.0) == 0, "and it stays stopped while the transport plays");
        check (noteOns (again, 0.25, 1.0) >= 2, "the key starts it again");
        check (noteOffsAt (again, off, off + 1, 60) == 1 && noteOnsAt (again, off + 1, (int) (1.5 * CopyRig::rate)) == 0,
               "and its note-off stops it");
        setPlayback ([] (PlaybackSettings& p) { p.mode = PlaybackSettings::Mode::followHost; });
    }

    std::printf ("The host's notes are recorded when Rec is armed\n");
    {
        rig->processor.setRecordArmed (true);
        play (1.0, hostSends ({ { 0.25, noteOn (gateKey) }, { 0.75, noteOff (gateKey) } }));
        rig->processor.setRecordArmed (false);
        CopyRig::pump (250);

        bool recorded = false;

        for (const auto& n : rig->processor.getDocument().getClip (0).notes)
            recorded = recorded || n.key == gateKey;

        check (recorded, "the host's note is in the clip");
    }

    int logged = 0;

    for (const auto& line : juce::StringArray::fromLines (PluginLog::tail (200)))
        logged += line.contains ("The host sent") ? 1 : 0;

    check (logged == 1, "the plugin log has the first of the host's silences only");

    std::printf ("Note trigger, in a load of its own: the plugin log notes the first clip All Notes Off stopped\n");
    {
        CopyRig other;      // no synth: only the log is read
        other.synth().useSettingsFile (settingsFile);
        auto& doc = other.processor.getDocument();
        auto s = doc.getSettings();
        s.playback.mode = PlaybackSettings::Mode::noteTrigger;
        doc.setSettings (s);
        doc.replaceClip (0, clip, "Test");
        CopyRig::pump (30);

        for (int i = 0; i < 40; ++i)
        {
            juce::AudioBuffer<float> audio (2, CopyRig::block);
            juce::MidiBuffer midi;

            if (i == 0)
                midi.addEvent (noteOn (0), 0);
            else if (i == 20)
                midi.addEvent (juce::MidiMessage::allNotesOff (1), 0);

            other.processor.processBlock (audio, midi);
        }

        CopyRig::pump (200);        // the processor's timer writes the line
        const auto log = PluginLog::tail (10);
        check (log.contains ("The host sent All Notes Off (CC 123): the clip playing in Note trigger mode stopped"),
               "the plugin log says the host sent it, and that it stopped the clip");
    }

    finish();
    std::printf ("%s\n", check.failures == 0 ? "PASS" : "FAIL");
    return check.failures == 0 ? 0 : 1;
}

/** MPE Roll's own VST3, loaded as a host loads it, playing a clip in Follow FL mode. A VST3 gets
    the host's MIDI controllers only as changes of the parameters it maps them to (its IMidiMapping:
    JUCE's wrapper gives every controller a hidden parameter and turns its changes back into MIDI),
    and JUCE's host sends them that way. All Notes Off sent so must stop the clip until the
    transport restarts, as gatetest checks inside the engine. Whether FL sends controllers to a VST3
    like this isn't covered: only that MPE Roll's VST3 acts on one that comes so. */
int vst3HostMidiTest (const juce::File& pluginFile)
{
    Check check;
    ignoreXErrors();

    juce::VST3PluginFormat format;
    juce::OwnedArray<juce::PluginDescription> found;
    format.findAllTypesForFile (found, pluginFile.getFullPathName());

    if (found.isEmpty())
    {
        std::printf ("No VST3 in %s\n", pluginFile.getFullPathName().toRawUTF8());
        return 1;
    }

    juce::String error;
    auto plugin = format.createInstanceFromDescription (*found[0], CopyRig::rate, CopyRig::block, error);

    if (plugin == nullptr)
    {
        std::printf ("Couldn't load %s: %s\n", pluginFile.getFullPathName().toRawUTF8(), error.toRawUTF8());
        return 1;
    }

    std::printf ("Loaded %s %s (VST3)\n", found[0]->name.toRawUTF8(), found[0]->version.toRawUTF8());

    // C4 held through the clip and E4 every half beat, given to the VST3 as a project saved by MPE
    // Roll's code here, in the form JUCE's host keeps a VST3's state in: the component's state,
    // base64, in a VST3PluginState element.
    {
        const auto settingsFile = juce::File::createTempFile ("settings");
        auto maker = std::make_unique<MpeRollProcessor>();
        maker->getSynth().useSettingsFile (settingsFile);

        MpeClip clip;
        clip.length = 16.0;
        clip.addNote (note (60, 0.0, 16.0, 0.8f));

        for (int i = 0; i < 32; ++i)
            clip.addNote (note (64, i * 0.5, 0.4, 0.8f));

        maker->getDocument().replaceClip (0, clip, "Test");
        juce::MemoryBlock state;
        maker->getStateInformation (state);
        maker.reset();
        settingsFile.deleteFile();

        juce::XmlElement hostState ("VST3PluginState");
        hostState.createNewChildElement ("IComponent")->addTextElement (state.toBase64Encoding());
        juce::MemoryBlock saved;
        juce::AudioProcessor::copyXmlToBinary (hostState, saved);
        plugin->setStateInformation (saved.getData(), (int) saved.getSize());
    }

    FakePlayHead playHead;
    plugin->setPlayHead (&playHead);
    plugin->prepareToPlay (CopyRig::rate, CopyRig::block);
    CopyRig::pump (200);

    // Plays the transport from the clip's start for this long, the host sending All Notes Off on
    // channel 1 in the first block at or after `allNotesOffAt` seconds, and returns the VST3's MIDI
    // output (the notes MPE Roll plays its synth), each with its block's time.
    auto play = [&] (double seconds, double allNotesOffAt)
    {
        std::vector<std::pair<double, juce::MidiMessage>> out;
        const auto total = (int) (seconds * CopyRig::rate);
        bool sent = false;
        playHead.playing = true;

        for (int pos = 0; pos < total; pos += CopyRig::block)
        {
            const auto time = pos / CopyRig::rate;
            juce::AudioBuffer<float> audio (2, CopyRig::block);
            juce::MidiBuffer midi;

            if (! sent && allNotesOffAt >= 0.0 && time >= allNotesOffAt)
            {
                midi.addEvent (juce::MidiMessage::allNotesOff (1), 0);
                sent = true;
            }

            playHead.ppq = time * playHead.bpm / 60.0;
            plugin->processBlock (audio, midi);

            for (const auto m : midi)
                out.emplace_back (time, m.getMessage());
        }

        playHead.playing = false;

        for (int i = 0; i < 10; ++i)
        {
            juce::AudioBuffer<float> audio (2, CopyRig::block);
            juce::MidiBuffer midi;
            plugin->processBlock (audio, midi);
        }

        CopyRig::pump (50);
        return out;
    };

    auto noteOns = [] (const std::vector<std::pair<double, juce::MidiMessage>>& out, double from, double to)
    {
        int n = 0;

        for (const auto& [time, m] : out)
            if (time >= from && time < to && m.isNoteOn() && m.getNoteNumber() >= 60)
                ++n;

        return n;
    };

    const auto stopped = play (2.0, 1.0);
    const auto restarted = play (2.0, -1.0);
    std::printf ("  note-ons: %d before All Notes Off, %d after it; played again: %d in the first second, %d in the next\n",
                 noteOns (stopped, 0.0, 1.0), noteOns (stopped, 1.0, 2.0), noteOns (restarted, 0.0, 1.0),
                 noteOns (restarted, 1.0, 2.0));
    check (noteOns (stopped, 0.0, 1.0) >= 4, "the VST3 plays its clip with the transport, its notes in its MIDI output");
    check (noteOns (stopped, 1.0, 2.0) == 0, "All Notes Off from the host, through the VST3's controller mapping, stops it");
    check (noteOns (restarted, 0.0, 1.0) >= 4 && noteOns (restarted, 1.0, 2.0) >= 4,
           "the transport restarted, it plays again, and on through the second second without one");

    plugin->releaseResources();
    plugin.reset();
    CopyRig::pump (100);

    std::printf ("%s\n", check.failures == 0 ? "PASS" : "FAIL");
    return check.failures == 0 ? 0 : 1;
}

//==============================================================================
/** The update check, against tools/fake_releases_server.py standing in for GitHub over plain HTTP
    on this computer (the Linux build has no TLS, and this never asks GitHub itself): nothing is sent
    while it's off, which it is until turned on, or once turned off; turned on, it asks once a day,
    as MPE Roll opens, and shows a newer release as the Update button and in the File menu, which
    stay until it's installed or skipped, whether the daily check is on or not; the release page's
    redirect answers when the API refuses or doesn't answer in time; a pre-release or a draft is
    never offered, nor a skipped version until a newer one is out; failures are said only when
    asked; two MPE Rolls running at once (each with a copy of the computer's settings, and a checker,
    of its own) keep to what the other switched or skipped; and a request under way is called off at
    once as the last window closes.
*/
int updateCheckTest (const juce::File& serverScript)
{
    Check check;
    auto& mm = *juce::MessageManager::getInstance();
    ignoreXErrors();

    const auto folder = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("MPE Roll update check");
    folder.deleteRecursively();
    folder.createDirectory();
    PluginLog::setFileForTesting (folder.getChildFile ("Plugin log.txt"));

    // The settings every MPE Roll here shares, in a file of the test's own, kept open by this one for
    // as long as the test runs; and the look's.
    HostedSynth keeper;
    keeper.useSettingsFile (folder.getChildFile ("MPE Roll.settings"));
    auto* settings = keeper.getUserSettings();
    juce::SharedResourcePointer<SkinManager> skins;
    skins->useSettingsFile (folder.getChildFile ("Look.settings"));

    // The update check's own settings, beside those: read here as a fresh copy of the file has them,
    // and written there, as another MPE Roll running at the same time would.
    const auto updateSettings = UpdateChecker::getSettingsFile (settings);
    check (updateSettings == folder.getChildFile ("Update check.settings"),
           "the update check keeps its settings in Update check.settings, beside MPE Roll.settings: "
               + updateSettings.getFullPathName());

    const auto keyOptions = []
    {
        juce::PropertiesFile::Options options;
        options.storageFormat = juce::PropertiesFile::storeAsXML;
        options.millisecondsBeforeSaving = -1;
        return options;
    };

    const auto getKey = [&] (const char* key) { return juce::PropertiesFile (updateSettings, keyOptions()).getValue (key); };
    const auto hasKey = [&] (const char* key) { return juce::PropertiesFile (updateSettings, keyOptions()).containsKey (key); };

    const auto setKey = [&] (const char* key, const juce::var& value)
    {
        juce::PropertiesFile keys (updateSettings, keyOptions());
        keys.setValue (key, value);
        keys.saveIfNeeded();
    };

    const auto removeKey = [&] (const char* key)
    {
        juce::PropertiesFile keys (updateSettings, keyOptions());
        keys.removeValue (key);
        keys.saveIfNeeded();
    };

    const auto version = getVersionString();
    const juce::String apiPath ("/repos/freener0x/MPE-Roll/releases/latest"), pagePath ("/freener0x/MPE-Roll/releases/latest");
    const auto now = [] { return juce::Time::getMillisecondCounterHiRes(); };

    const auto setEnvironment = [] (const char* name, const juce::String& value)
    {
       #if JUCE_WINDOWS
        _putenv_s (name, value.toRawUTF8());
       #else
        setenv (name, value.toRawUTF8(), 1);
       #endif
    };

   #if ! JUCE_WINDOWS
    // JUCE's Linux networking sends every request through http_proxy when that's set, and a proxy
    // can't reach a server on this computer.
    unsetenv ("http_proxy");
   #endif

    // The check is pointed at the stand-in by these environment variables, which JUCE reads from the
    // system rather than from the C library that sets them here. Were they not to reach it, the check
    // would ask GitHub itself, which no test does: stopped before anything is asked.
    {
        const juce::String probe ("http://127.0.0.1:9/probe");
        setEnvironment ("MPEROLL_UPDATE_URL", probe);
        const auto seen = juce::SystemStats::getEnvironmentVariable ("MPEROLL_UPDATE_URL", {});
        setEnvironment ("MPEROLL_UPDATE_URL", {});

        if (seen != probe || juce::SystemStats::getEnvironmentVariable ("MPEROLL_UPDATE_URL", {}).isNotEmpty())
        {
            std::printf ("  [FAIL] MPEROLL_UPDATE_URL set here reads back in JUCE as '%s', not '%s': the check would ask "
                         "GitHub itself, so nothing is tested\nFAIL: 1 failure(s)\n", seen.toRawUTF8(), probe.toRawUTF8());
            PluginLog::setFileForTesting ({});
            UpdateChecker::setBrowserForTesting ({});
            return 1;
        }
    }

    // Release pages are noted here rather than opened in a browser.
    juce::StringArray pagesOpened;
    UpdateChecker::setBrowserForTesting ([&pagesOpened] (const juce::URL& page)
    {
        pagesOpened.add (page.toString (false));
        return true;
    });

    //==========================================================================
    // The stand-in for GitHub, started afresh for each way it's to answer, with a log of its own.
    std::unique_ptr<juce::ChildProcess> server;
    juce::File requestLog;
    int port = 0, serversStarted = 0;

    const auto stopServer = [&]
    {
        if (server != nullptr)
            server->kill();

        server.reset();
    };

    const auto startServer = [&] (const juce::StringArray& options)
    {
        stopServer();
        ++serversStarted;
        requestLog = folder.getChildFile ("requests " + juce::String (serversStarted) + ".jsonl");
        const auto portFile = folder.getChildFile ("port " + juce::String (serversStarted) + ".txt");
        port = 0;
        juce::StringArray tried;

        // python3 where there's one, else python (as on Windows). Its output isn't wanted: what it's
        // asked goes to its log, and a pipe nobody reads would stop it once full. It stops by itself
        // within two minutes, longer than any section here takes: on Windows it doesn't see this
        // process go, and, holding handles of this one's, would keep a crashed run's step waiting.
        for (const auto* python : { "python3", "python" })
        {
            juce::StringArray command { python, serverScript.getFullPathName(), "--port-file", portFile.getFullPathName(),
                                        "--log", requestLog.getFullPathName(), "--max-seconds", "120" };
            command.addArray (options);
            auto process = std::make_unique<juce::ChildProcess>();

            if (! process->start (command, 0))
            {
                tried.add (juce::String (python) + " (didn't start)");
                continue;
            }

            for (const auto end = now() + 10000.0; port <= 0 && process->isRunning() && now() < end;)
            {
                juce::Thread::sleep (20);
                port = portFile.existsAsFile() ? portFile.loadFileAsString().trim().getIntValue() : 0;
            }

            if (port > 0)
            {
                server = std::move (process);
                break;
            }

            tried.add (juce::String (python) + (process->isRunning() ? " (no port within 10 s)"
                                                                     : " (ended, exit code " + juce::String (process->getExitCode()) + ")"));
            process->kill();
        }

        const auto address = "http://127.0.0.1:" + juce::String (port);
        setEnvironment ("MPEROLL_UPDATE_URL", address + apiPath);
        setEnvironment ("MPEROLL_UPDATE_PAGE_URL", address + pagePath);
        const auto pointed = juce::SystemStats::getEnvironmentVariable ("MPEROLL_UPDATE_URL", {}) == address + apiPath
                          && juce::SystemStats::getEnvironmentVariable ("MPEROLL_UPDATE_PAGE_URL", {}) == address + pagePath;
        check (port > 0 && pointed, "  (the stand-in for GitHub answers " + options.joinIntoString (" ") + ")"
                                        + (port > 0 ? juce::String() : ": no Python started it: " + tried.joinIntoString (", "))
                                        + (pointed ? juce::String() : ": the check isn't pointed at it"));
    };

    /** What the server was asked, oldest first: each request's path, its headers, as [name, value]
        pairs in the order they came, and when ("t", seconds since the server started). Whole lines
        only: one being written as this reads is counted next time. */
    const auto requests = [&]
    {
        std::vector<juce::var> found;
        const auto written = requestLog.loadFileAsString().upToLastOccurrenceOf ("\n", false, false);

        for (const auto& line : juce::StringArray::fromLines (written))
            if (line.isNotEmpty())
                found.push_back (juce::JSON::parse (line));

        return found;
    };

    const auto pathsAsked = [&]
    {
        juce::StringArray paths;

        for (const auto& request : requests())
            paths.add (request["path"].toString());

        return paths;
    };

    /** The requests as the server saw them, for a failure to say what happened and when: a system's
        networking that tried again, or waited longer than it was told, shows here. */
    const auto describeRequests = [&]
    {
        juce::StringArray lines;

        for (const auto& request : requests())
            lines.add (request["path"].toString() + " at " + juce::String ((double) request["t"], 2) + " s");

        return lines.isEmpty() ? juce::String ("none") : lines.joinIntoString (", ");
    };

    // The headers the system's networking adds to every request of its own accord, which carry nothing
    // of MPE Roll's: JUCE's own on Linux adds these two. WinINet and Apple's URL session add more of
    // the standard ones (what answers they accept, caching), which a run there lists if it fails.
   #if JUCE_LINUX
    const juce::StringArray systemHeaders { "Host", "Connection" };
   #else
    const juce::StringArray systemHeaders { "Host", "Connection", "Accept", "Accept-Encoding", "Accept-Language",
                                            "Cache-Control", "Pragma" };
   #endif

    /** What's wrong with a request's headers, or nothing: it must carry MPE Roll's User-Agent, and no
        header but those MPE Roll sends and the system's own, each once. Said as the server got them,
        so that a run on a system whose networking keeps a header of its own beside MPE Roll's (WinINet
        its session's agent, "juce", beside MPE Roll's User-Agent, or its Accept of any type beside
        the API's), or adds one that isn't listed here, says which, and with what. */
    const auto headerProblems = [&version, &systemHeaders] (const juce::var& request, const juce::StringArray& mpeRolls)
    {
        juce::StringArray names, problems;
        std::map<juce::String, juce::StringArray> sent;     // the values, by name in lower case

        if (const auto* headers = request["headers"].getArray())
            for (const auto& header : *headers)
            {
                names.add (header[0].toString());
                sent[header[0].toString().toLowerCase()].add ("'" + header[1].toString() + "'");
            }

        const auto& agents = sent["user-agent"];

        if (agents.size() != 1 || agents[0] != "'MPE Roll/" + version + "'")
            problems.add ("User-Agent " + (agents.isEmpty() ? juce::String ("missing") : "sent as " + agents.joinIntoString (" and "))
                          + ", not once as 'MPE Roll/" + version + "'");

        for (const auto& [name, values] : sent)
            if (values.size() > 1 && name != "user-agent")
                problems.add (name + " sent " + juce::String (values.size()) + " times: " + values.joinIntoString (", "));

        for (const auto& name : names)
            if (! mpeRolls.contains (name, true) && ! systemHeaders.contains (name, true))
                problems.addIfNotAlreadyThere ("a header not expected: " + name + ": " + sent[name.toLowerCase()].joinIntoString (", "));

        return problems.joinIntoString ("; ");
    };

    const auto describeRequest = [] (const juce::var& request) { return juce::JSON::toString (request["headers"], true); };

    /** A check of a request's headers, whose failure says what was wrong and everything sent. */
    const auto checkHeaders = [&] (const juce::var& request, const juce::StringArray& mpeRolls, const juce::String& what)
    {
        const auto problems = headerProblems (request, mpeRolls);
        check (problems.isEmpty(), what + (problems.isEmpty() ? juce::String() : ": " + problems + ". All it carried: " + describeRequest (request)));
    };

    //==========================================================================
    // MPE Roll's window, opened and closed as FL opens and closes it.
    MpeRollProcessor processor;
    std::unique_ptr<juce::AudioProcessorEditor> editor;

    // On the desktop, as a host shows it: MPE settings opens in a call-out over it.
    const auto open = [&]
    {
        editor.reset (processor.createEditorAndMakeActive());
        editor->setSize (1200, 760);
        editor->addToDesktop (0);
        editor->setVisible (true);
        mm.runDispatchLoopUntil (100);
    };

    /** Closes the window and answers how long deleting it took, in milliseconds. */
    const auto close = [&]
    {
        editor->removeFromDesktop();
        const auto start = now();
        processor.editorBeingDeleted (editor.get());
        editor.reset();
        const auto took = now() - start;
        mm.runDispatchLoopUntil (100);
        return took;
    };

    const auto toolbar = [&]() -> Toolbar& { return *findChild<Toolbar> (*editor); };
    const auto status = [&] { return toolbar().status.getText(); };
    const auto updateButton = [&]() -> juce::TextButton& { return toolbar().updateButton; };

    /** Waits for the check under way to end, and its news to reach the windows. False if it didn't. */
    const auto waitForCheck = [&] (double timeoutMs)
    {
        juce::SharedResourcePointer<UpdateChecker> updates;     // the windows', while one is open

        for (const auto end = now() + timeoutMs; updates->getState().phase == UpdateChecker::Phase::checking && now() < end;)
            mm.runDispatchLoopUntil (20);

        mm.runDispatchLoopUntil (100);
        return updates->getState().phase != UpdateChecker::Phase::checking;
    };

    // The File menu as its button would show it now (read from the editor, as the uitest does: on
    // screen it closes itself at once where this process isn't the one in front).
    const auto fileMenu = [&]
    {
        auto* rollEditor = dynamic_cast<MpeRollEditor*> (editor.get());
        return rollEditor != nullptr ? menuItems (rollEditor->makeMidiMenu()) : std::vector<std::pair<juce::String, bool>>();
    };

    const auto menuHas = [] (const std::vector<std::pair<juce::String, bool>>& items, const juce::String& text)
    {
        return std::any_of (items.begin(), items.end(), [&text] (const auto& item) { return item.first == text && item.second; });
    };

    const auto menuEndsAsBefore = [] (const std::vector<std::pair<juce::String, bool>>& items)
    {
        return items.size() >= 2 && items.back().first == getBuildName() && items[items.size() - 2].first == "Report a bug...";
    };

    /** The window's MPE settings, opened at the Updates section. */
    const auto openSettings = [&]() -> juce::Component*
    {
        toolbar().settingsButton.triggerClick();
        mm.runDispatchLoopUntil (100);
        auto* box = findChild<juce::CallOutBox> (*editor);
        auto* panel = box != nullptr ? findChild<SettingsPanel> (*box) : nullptr;

        if (auto* sections = panel != nullptr ? findChild<juce::PropertyPanel> (*panel) : nullptr)
            sections->setSectionOpen (sections->getSectionNames().indexOf ("Updates"), true);

        return panel;
    };

    const auto lineOf = [] (juce::Component* panel, const juce::String& name)
    {
        auto* row = panel != nullptr ? findNamed (*panel, name) : nullptr;
        auto* label = row != nullptr ? findChild<juce::Label> (*row) : nullptr;
        return label != nullptr ? label->getText() : juce::String ("(no " + name + " line)");
    };

    const auto buttonIn = [] (juce::Component* panel, const juce::String& name) -> juce::Button*
    {
        auto* row = panel != nullptr ? findNamed (*panel, name) : nullptr;
        return row != nullptr ? findChild<juce::Button> (*row) : nullptr;
    };

    const auto click = [&mm] (juce::Button* button)
    {
        if (button != nullptr)
            button->triggerClick();

        mm.runDispatchLoopUntil (50);       // the click is posted
    };

    const auto anyAlert = []
    {
        auto& desktop = juce::Desktop::getInstance();

        for (int i = 0; i < desktop.getNumComponents(); ++i)
            if (dynamic_cast<juce::AlertWindow*> (desktop.getComponent (i)) != nullptr)
                return true;

        return false;
    };

    const juce::String automaticName ("Check for updates when MPE Roll opens (once a day)");
    const auto lastAttempt = [&] { return getKey ("updateCheckLast").getLargeIntValue(); };
    const auto outcome = [&] { return getKey ("updateCheckLastOutcome"); };
    const auto dueNow = [&] { setKey ("updateCheckLast", "0"); };

    //==========================================================================
    std::printf ("Off until it's turned on: nothing is sent\n");
    {
        startServer ({ "--tag", "v9.9.0" });
        open();
        mm.runDispatchLoopUntil (1500);
        check (! hasKey ("updateCheck") && requests().empty(), "MPE Roll opened with the check never turned on sends nothing");
        check (! updateButton().isVisible(), "and shows no Update button");

        auto items = fileMenu();
        check (menuHas (items, "Check for updates now...") && ! menuHas (items, "Get MPE Roll 9.9.0 (release page)...")
                   && menuEndsAsBefore (items),
               "the File menu offers Check for updates now..., and still ends with Report a bug... and the build: "
                   + describeMenu (items));

        // Nor is anything written: the window, the File menu and MPE settings (opened at Updates, and
        // left open a while) only read.
        openSettings();
        mm.runDispatchLoopUntil (1500);
        check (! updateSettings.existsAsFile() && requests().empty(),
               "the window, its File menu and MPE settings open, with the check never used, write no Update check.settings");

        // As a user who turned it off has it: off, with the last attempt a day ago or more.
        setKey ("updateCheck", false);
        setKey ("updateCheckLast", juce::String (juce::Time::currentTimeMillis() - 2 * updates::checkIntervalMs));
        close();
        open();
        mm.runDispatchLoopUntil (1500);
        check (requests().empty() && ! updateButton().isVisible(),
               "turned off, with the last attempt two days ago, MPE Roll opening sends nothing either");
        removeKey ("updateCheck");
        removeKey ("updateCheckLast");

        auto* panel = openSettings();
        auto* automatic = dynamic_cast<juce::ToggleButton*> (buttonIn (panel, automaticName));
        check (automatic != nullptr && ! automatic->getToggleState(), "MPE settings has the daily check, off");
        check (lineOf (panel, "Last check").startsWith ("Never. Nothing is sent until you turn the check on"),
               "and says nothing has been sent: " + lineOf (panel, "Last check"));
       #if JUCE_LINUX
        // The Linux build can't make HTTPS requests: it says it sends nothing, rather than what the
        // Windows and macOS builds send.
        check (lineOf (panel, "What it sends").startsWith ("Nothing: this Linux build can't make HTTPS requests"),
               "and that this build sends nothing: " + lineOf (panel, "What it sends"));
       #else
        check (lineOf (panel, "What it sends").contains ("(MPE Roll/" + version + ")"),
               "and what a check would send: " + lineOf (panel, "What it sends"));
       #endif

        auto* none = buttonIn (panel, "Newer release");
        check (none != nullptr && ! none->isEnabled() && none->getButtonText() == "None known",
               "with no newer release known, which it doesn't call one not found: "
                   + (none != nullptr ? none->getButtonText() : juce::String ("(no row)")));
        mm.runDispatchLoopUntil (1000);
        check (requests().empty(), "opening MPE settings sends nothing either");

        std::printf ("Turned on, it asks at once, quietly, and a newer release is the Update button\n");
        click (automatic);
        check (UpdateChecker::isAutomatic (settings) && getKey ("updateCheck") == "1" && ! settings->containsKey ("updateCheck"),
               "the switch turns the daily check on, kept in the update check's settings (not MPE Roll.settings)");
        // Waited for before the checks, rather than in them: a check's words can be put together before
        // its condition is worked out.
        const auto answered = waitForCheck (20000.0);
        check (answered && pathsAsked() == juce::StringArray (apiPath),
               "and it asks GitHub's API at once, never having asked: " + describeRequests());
        check (updateButton().isVisible() && updateButton().getButtonText() == "Update to 9.9.0",
               "9.9.0 being out, the Update button says so: " + updateButton().getButtonText());
        check (updateButton().getTooltip().contains ("MPE Roll 9.9.0 is out (you have " + version + ")"),
               "its tooltip names this version: " + updateButton().getTooltip());
        check (! status().contains ("9.9.0") && ! anyAlert(), "and the status line says nothing of it, with no box either: " + status());
        check (getKey ("updateCheckLastTag") == "v9.9.0" && outcome() == "newer"
                   && std::abs (juce::Time::currentTimeMillis() - lastAttempt()) < 60000,
               "the settings keep when it asked and what it found");
        check (lineOf (panel, "Last check").contains ("MPE Roll 9.9.0 is out (you have " + version + ")"),
               "as MPE settings' Last check line says: " + lineOf (panel, "Last check"));

        if (! requests().empty())
            checkHeaders (requests()[0], { "User-Agent", "Accept", "X-GitHub-Api-Version" },
                          "the request carried MPE Roll's name and version and the API's format, nothing else");

        items = fileMenu();
        check (menuHas (items, "Get MPE Roll 9.9.0 (release page)...") && menuHas (items, "Skip version 9.9.0")
                   && menuEndsAsBefore (items),
               "the File menu offers to get it or skip it: " + describeMenu (items));

        const auto page = "http://127.0.0.1:" + juce::String (port) + "/freener0x/MPE-Roll/releases/tag/v9.9.0";
        click (&updateButton());
        check (pagesOpened == juce::StringArray (page), "the Update button opens the release's page: " + pagesOpened.joinIntoString (", "));

        auto* get = buttonIn (panel, "Newer release");
        check (get != nullptr && get->isEnabled() && get->getButtonText() == "Get MPE Roll 9.9.0 (release page)...",
               "as does MPE settings' Get MPE Roll 9.9.0 (release page)...");
        click (get);
        check (pagesOpened.size() == 2 && pagesOpened[1] == page, "to the same page");
        close();
    }

    std::printf ("Once a day, counted from the last attempt\n");
    {
        // Every window closed, so the checker went with them, as when FL is started again later.
        open();
        mm.runDispatchLoopUntil (1000);
        check (requests().size() == 1, "opening MPE Roll again the same day asks nothing");
        check (updateButton().isVisible() && updateButton().getButtonText() == "Update to 9.9.0",
               "and the release found is still offered: the notice stands until it's installed or skipped");

        MpeRollProcessor another;
        std::unique_ptr<juce::AudioProcessorEditor> second (another.createEditorAndMakeActive());
        second->setSize (1200, 760);
        mm.runDispatchLoopUntil (500);
        auto* secondBar = findChild<Toolbar> (*second);
        check (requests().size() == 1 && secondBar != nullptr && secondBar->updateButton.isVisible(),
               "a second MPE Roll opened meanwhile asks nothing either, and offers it too");
        another.editorBeingDeleted (second.get());
        second.reset();
        close();
    }

    std::printf ("A day later it asks again; the same version as this one is no news\n");
    {
        startServer ({ "--tag", "v" + version });
        setKey ("updateCheckLast", juce::String (juce::Time::currentTimeMillis() - updates::checkIntervalMs - 1000));
        open();
        check (waitForCheck (20000.0) && pathsAsked() == juce::StringArray (apiPath), "a day after the last attempt, it asks again");
        check (! updateButton().isVisible() && outcome() == "latest" && ! status().contains (version + " is the latest"),
               "with this version the latest: no button, and nothing said");
        close();
    }

    std::printf ("A build newer than the latest release (one made between releases) is told of nothing\n");
    {
        startServer ({ "--tag", "v0.0.0" });
        dueNow();
        open();
        check (waitForCheck (20000.0) && requests().size() == 1 && ! updateButton().isVisible() && outcome() == "ahead"
                   && ! status().contains ("0.0.0"),
               "the daily check finds an older release: no button, and nothing said");

        {
            juce::SharedResourcePointer<UpdateChecker> updates;
            updates->checkNow (settings);   // as File > Check for updates now... does
            const auto answered = waitForCheck (20000.0);
            check (answered && status() == "This MPE Roll, " + version + ", is newer than the latest release, 0.0.0."
                       && ! updateButton().isVisible(),
                   "asked, the status line says this build is the newer one: " + status());
        }

        close();
    }

    std::printf ("A skipped version is no news until a newer one is out\n");
    {
        startServer ({ "--tag", "v9.9.0" });
        setKey ("updateCheckSkippedTag", "v9.9.0");
        dueNow();
        open();
        check (waitForCheck (20000.0) && requests().size() == 1 && ! updateButton().isVisible() && outcome() == "newer",
               "9.9.0, skipped, shows no button");

        {
            juce::SharedResourcePointer<UpdateChecker> updates;
            check (updates->describeLastCheck (settings).contains ("MPE Roll 9.9.0 is out, and you chose to skip it"),
                   "MPE settings says it's out and skipped: " + updates->describeLastCheck (settings));

            // Asked, it's said, as the answer to the question: the user wants to know what there is.
            // But the user chose not to be told of it, so nothing more: no button, and nothing in
            // the File menu, which would have no way to hide either.
            updates->checkNow (settings);
            const auto answered = waitForCheck (20000.0);
            check (answered && status().startsWith ("MPE Roll 9.9.0 is out (you have " + version + "), but you chose to skip it."),
                   "asked, the status line names it, and that it's skipped: " + status());
            check (! updateButton().isVisible(), "but shows no button for it");

            const auto items = fileMenu();
            check (! menuHas (items, "Get MPE Roll 9.9.0 (release page)...") && ! menuHas (items, "Skip version 9.9.0"),
                   "nor offers it in the File menu: " + describeMenu (items));

            // MPE settings, where the user goes to look, still has its page.
            auto* panel = openSettings();
            auto* get = buttonIn (panel, "Newer release");
            auto* skip = buttonIn (panel, "Skip it");
            check (get != nullptr && get->isEnabled() && get->getButtonText() == "Get MPE Roll 9.9.0 (release page)...",
                   "MPE settings still offers its page: " + (get != nullptr ? get->getButtonText() : juce::String ("(no row)")));
            check (skip != nullptr && ! skip->isEnabled() && skip->getButtonText() == "Skipped",
                   "and says it's skipped: " + (skip != nullptr ? skip->getButtonText() : juce::String ("(no row)")));
        }

        close();
        startServer ({ "--tag", "v9.9.1" });
        dueNow();
        open();
        check (waitForCheck (20000.0) && updateButton().isVisible() && updateButton().getButtonText() == "Update to 9.9.1",
               "a release newer than the skipped one is offered");

        auto* panel = openSettings();
        auto* skip = buttonIn (panel, "Skip it");
        check (skip != nullptr && skip->isEnabled() && skip->getButtonText() == "Skip version 9.9.1",
               "MPE settings offers Skip version 9.9.1");
        click (skip);
        check (! updateButton().isVisible() && getKey ("updateCheckSkippedTag") == "v9.9.1",
               "which hides the button, and is kept");
        check (skip != nullptr && ! skip->isEnabled() && skip->getButtonText() == "Skipped", "and can't be clicked again");

        // Skipping an older version (one a menu still showed, say) doesn't bring a newer skipped one
        // back.
        {
            updates::Release older;
            older.tag = "v9.9.0";
            juce::SharedResourcePointer<UpdateChecker> checker;
            checker->skip (settings, older);
            check (getKey ("updateCheckSkippedTag") == "v9.9.1" && ! updateButton().isVisible(),
                   "skipping an older version leaves the newer one skipped: " + getKey ("updateCheckSkippedTag"));
        }

        close();
        removeKey ("updateCheckSkippedTag");
    }

    std::printf ("GitHub's API refusing (its limit of requests): the release page's redirect names the release\n");
    {
        startServer ({ "--tag", "v9.9.2", "--api", "403" });
        dueNow();
        open();
        check (waitForCheck (20000.0) && updateButton().getButtonText() == "Update to 9.9.2" && outcome() == "newer",
               "the release is found all the same");
        check (pathsAsked() == juce::StringArray (apiPath, pagePath), "from the release page, after the API: " + describeRequests());

        if (requests().size() == 2)
            checkHeaders (requests()[1], { "User-Agent" }, "which was sent MPE Roll's name and version alone");

        close();
    }

    std::printf ("What an earlier check found stands when one gets no answer\n");
    {
        stopServer();       // 9.9.2 was found just now; nothing answers from here on
        dueNow();
        open();
        check (updateButton().getButtonText() == "Update to 9.9.2", "as MPE Roll opens, the release found is offered");
        auto answered = waitForCheck (20000.0);
        check (answered && outcome() == "failed: no answer from GitHub" && updateButton().isVisible()
                   && updateButton().getButtonText() == "Update to 9.9.2" && ! status().contains ("Couldn't check"),
               "and still is after a daily check with no answer, which says nothing: " + updateButton().getButtonText());

        {
            juce::SharedResourcePointer<UpdateChecker> updates;
            updates->checkNow (settings);
            check (updateButton().isVisible(), "while a check is under way too");
            answered = waitForCheck (20000.0);
            check (answered && status().startsWith ("Couldn't check for updates") && updateButton().getButtonText() == "Update to 9.9.2",
                   "asked, it says it couldn't check, and the button stays: " + status());
        }

        close();
    }

    std::printf ("Pre-releases and drafts are never offered\n");
    {
        removeKey ("updateCheckLastTag");       // what an earlier check found would be offered
        startServer ({ "--tag", "v9.9.3-beta1", "--api", "prerelease" });
        dueNow();
        open();
        const auto answered = waitForCheck (20000.0);
        check (answered && ! updateButton().isVisible() && outcome().startsWith ("failed: "),
               "a pre-release, from the API and from the release page alike, isn't: " + outcome());
        check (! status().contains ("Couldn't check"), "and, unasked, nothing is said of it");
        close();

        startServer ({ "--tag", "v9.9.4", "--api", "draft", "--page-tag", "v" + version });
        dueNow();
        open();
        check (waitForCheck (20000.0) && ! updateButton().isVisible() && outcome() == "latest" && pathsAsked().size() == 2,
               "nor a draft: the release page's latest, this version, answers instead");
        close();
    }

    std::printf ("No answer: said only when asked\n");
    {
        stopServer();       // nothing listening: refused at once, as when offline
        dueNow();
        open();
        auto answered = waitForCheck (20000.0);
        check (answered && ! updateButton().isVisible() && ! status().contains ("Couldn't check") && ! anyAlert(),
               "unasked, a check with no answer says nothing: " + status());
        check (outcome() == "failed: no answer from GitHub", "and the settings keep what happened: " + outcome());

        auto* panel = openSettings();
        check (lineOf (panel, "Last check").endsWith (": no answer from GitHub."), "as MPE settings' Last check line says: " + lineOf (panel, "Last check"));

        click (buttonIn (panel, "Check now"));
        answered = waitForCheck (20000.0);
        check (answered && status().startsWith ("Couldn't check for updates: no answer from GitHub."),
               "asked with Check now, the status line says so: " + status());
        check (! anyAlert(), "and nothing else does: no box to close");
        close();
    }

    std::printf ("Check now works with the daily check off, and answers in the status line\n");
    {
        startServer ({ "--tag", "v" + version });
        open();
        auto* panel = openSettings();
        auto* automatic = dynamic_cast<juce::ToggleButton*> (buttonIn (panel, automaticName));
        click (automatic);
        check (! UpdateChecker::isAutomatic (settings) && requests().empty(), "the switch turns the daily check off, which sends nothing");
        check (automatic != nullptr && ! automatic->getToggleState() && automatic->getButtonText() == "Off",
               "and shows it off");

        click (buttonIn (panel, "Check now"));
        check (waitForCheck (20000.0) && requests().size() == 1, "Check now asks all the same");
        check (status() == "MPE Roll " + version + " is the latest release." && ! anyAlert(),
               "and says this version is the latest in the status line, and nowhere else: " + status());
        close();

        startServer ({ "--tag", "v9.9.5" });
        open();

        {
            juce::SharedResourcePointer<UpdateChecker> updates;
            updates->checkNow (settings);   // as File > Check for updates now... does
            const auto answered = waitForCheck (20000.0);
            check (answered && updateButton().isVisible()
                       && status() == "MPE Roll 9.9.5 is out (you have " + version + "): click Update to 9.9.5 to get it.",
                   "a newer release, asked for, is named in the status line, with the button: " + status());
        }

        // The daily check still off, every window closed and one opened again (FL started again,
        // say): what the check found is offered until it's installed or skipped, as MPE settings'
        // Last check line says it's out.
        close();
        open();
        mm.runDispatchLoopUntil (500);
        check (requests().size() == 1 && updateButton().isVisible() && updateButton().getButtonText() == "Update to 9.9.5",
               "opened again, the release found is still offered, and nothing is asked: "
                   + updateButton().getButtonText());

        const auto items = fileMenu();
        check (menuHas (items, "Get MPE Roll 9.9.5 (release page)...") && menuHas (items, "Skip version 9.9.5"),
               "in the File menu too: " + describeMenu (items));

        panel = openSettings();
        auto* get = buttonIn (panel, "Newer release");
        check (lineOf (panel, "Last check").contains ("MPE Roll 9.9.5 is out")
                   && get != nullptr && get->isEnabled() && get->getButtonText() == "Get MPE Roll 9.9.5 (release page)...",
               "and in MPE settings, whose Last check line agrees: " + lineOf (panel, "Last check") + " / "
                   + (get != nullptr ? get->getButtonText() : juce::String ("(no row)")));
        close();
    }

    std::printf ("Two MPE Rolls at once, each with the computer's settings as it read them when it started (the VST2 "
                 "and the VST3 in one FL project): what's switched or skipped in one holds in the other\n");
    {
        startServer ({ "--tag", "v9.9.8" });

        // The check on, and a day past the last attempt, when the project opened and each MPE Roll read
        // the computer's settings: this process's (the VST3's, say), and the other plugin file's, which
        // has a copy of its own, and a checker of its own.
        setKey ("updateCheck", true);
        setKey ("updateCheckLast", juce::String (juce::Time::currentTimeMillis() - 2 * updates::checkIntervalMs));

        juce::PropertiesFile::Options sharedOptions;
        sharedOptions.storageFormat = juce::PropertiesFile::storeAsXML;
        juce::PropertiesFile otherSettings (settings->getFile(), sharedOptions);
        UpdateChecker here, other;

        const auto waitFor = [&] (UpdateChecker& checker)
        {
            for (const auto end = now() + 20000.0; checker.getState().phase == UpdateChecker::Phase::checking && now() < end;)
                mm.runDispatchLoopUntil (20);

            mm.runDispatchLoopUntil (50);
        };

        // Whether the check is off for both, and for a third MPE Roll started now.
        const auto offForAll = [&]
        {
            juce::PropertiesFile third (settings->getFile(), sharedOptions);
            return ! UpdateChecker::isAutomatic (settings) && ! UpdateChecker::isAutomatic (&otherSettings)
                && ! UpdateChecker::isAutomatic (&third);
        };

        here.setAutomatic (settings, false);        // switched off in this one's MPE settings
        check (offForAll(), "switched off in one MPE Roll, it's off in the other too");

        other.checkIfDue (&otherSettings);          // the other's window opens
        waitFor (other);
        check (requests().empty(), "whose window opening sends nothing: " + describeRequests());

        // Anything else the other saves (the marker a synth being loaded leaves, say) leaves it off.
        otherSettings.setValue ("synthLoading", "marker");
        otherSettings.saveIfNeeded();
        otherSettings.removeValue ("synthLoading");
        otherSettings.saveIfNeeded();
        check (offForAll(), "and a save of anything else in the other leaves it off, for every MPE Roll");

        // Switched on again in this one, which asks at once (a day having passed): the other doesn't ask
        // again that day, and offers what this one found.
        here.setAutomatic (settings, true);
        waitFor (here);
        check (pathsAsked() == juce::StringArray (apiPath) && here.getState().newer.has_value(), "switched on again in one, it asks");
        other.checkIfDue (&otherSettings);
        waitFor (other);
        check (requests().size() == 1 && other.getState().newer.has_value() && other.getState().newer->tag == "v9.9.8",
               "the other asks nothing that day, and offers what the first found: " + describeRequests());

        // Skipped in this one: no longer offered in the other once its window opens, and still skipped
        // after the other saves.
        if (const auto found = here.getState().newer)
            here.skip (settings, *found);

        other.checkIfDue (&otherSettings);
        waitFor (other);
        otherSettings.setValue ("synthLoading", "marker");
        otherSettings.saveIfNeeded();
        check (requests().size() == 1 && ! here.getState().newer.has_value() && ! other.getState().newer.has_value()
                   && getKey ("updateCheckSkippedTag") == "v9.9.8",
               "skipped in one, it's offered in neither");

        otherSettings.removeValue ("synthLoading");
        otherSettings.saveIfNeeded();
        removeKey ("updateCheckSkippedTag");
        removeKey ("updateCheckLastTag");
        setKey ("updateCheck", false);
    }

    /** Waits, running the message loop, for a checker's check to end. */
    const auto waitForChecker = [&] (UpdateChecker& checker)
    {
        for (const auto end = now() + 20000.0; checker.getState().phase == UpdateChecker::Phase::checking && now() < end;)
            mm.runDispatchLoopUntil (20);

        mm.runDispatchLoopUntil (50);
    };

    // A click as JUCE acts on it once the mouse is let go, but at once: a click on the screen is a
    // message, and others (a timer's) could come first, which a test of what a click does to what's
    // shown can't have.
    const auto clickAtOnce = [] (juce::Button* button)
    {
        if (button != nullptr && button->onClick != nullptr)
            button->onClick();
    };

    // A little longer than an MPE Roll takes to look at the update check's file again.
    constexpr int lookAgainMs = 1600;

    std::printf ("Two MPE Rolls' MPE settings open at once: a switch does what it shows, and soon shows what the "
                 "other did\n");
    {
        startServer ({ "--tag", "v9.9.8" });
        setKey ("updateCheck", true);
        setKey ("updateCheckLast", juce::String (juce::Time::currentTimeMillis() - 2 * updates::checkIntervalMs));

        // Two MPE Rolls (the VST3 and the VST2 in one FL project, say), each with a checker of its
        // own, and each with its MPE settings open: one plugin file's call-out isn't closed by clicks
        // in the other's window. Both panels get this process's settings, but what the update check
        // keeps is in its own file, read afresh, so they're two MPE Rolls as far as it goes.
        UpdateChecker checkerA, checkerB;
        SettingsPanel panelA (processor, checkerA), panelB (processor, checkerB);
        auto* switchA = buttonIn (&panelA, automaticName);
        auto* switchB = buttonIn (&panelB, automaticName);

        const auto shows = [] (juce::Button* s)
        {
            return s == nullptr ? juce::String ("(no switch)") : s->getToggleState() ? juce::String ("On") : juce::String ("Off");
        };

        check (shows (switchA) == "On" && shows (switchB) == "On", "both show the daily check on: " + shows (switchA) + ", " + shows (switchB));

        clickAtOnce (switchA);      // switched off in one
        check (getKey ("updateCheck") == "0" && shows (switchA) == "Off",
               "switched off in one, which shows it off at once: updateCheck " + getKey ("updateCheck") + ", " + shows (switchA));
        check (shows (switchB) == "On", "while the other's, not looked at since, still shows it on");

        // The user turns it off in the other too, where it still shows on: off it stays.
        clickAtOnce (switchB);
        mm.runDispatchLoopUntil (lookAgainMs);
        check (getKey ("updateCheck") == "0" && requests().empty() && checkerB.getState().phase != UpdateChecker::Phase::checking,
               "clicking the other's switch, shown on, leaves the check off, and sends nothing: updateCheck "
                   + getKey ("updateCheck") + ", requests: " + describeRequests());
        check (shows (switchB) == "Off", "and it shows it off: " + shows (switchB));

        // The other way round: switched on in one (no check due today), then clicked on in the other,
        // where it still shows off: on it stays.
        setKey ("updateCheckLast", juce::String (juce::Time::currentTimeMillis()));
        clickAtOnce (switchA);
        check (getKey ("updateCheck") == "1" && shows (switchB) == "Off", "switched on in one, while the other still shows it off");
        clickAtOnce (switchB);
        check (getKey ("updateCheck") == "1" && shows (switchB) == "On",
               "clicking the other's, shown off, leaves it on: updateCheck " + getKey ("updateCheck") + ", " + shows (switchB));

        // Left alone, each shows what the other did a moment later.
        clickAtOnce (switchA);
        mm.runDispatchLoopUntil (lookAgainMs);
        check (getKey ("updateCheck") == "0" && shows (switchB) == "Off",
               "switched off in one, the other's MPE settings shows it off a moment later, unclicked: " + shows (switchB));
        clickAtOnce (switchA);
        mm.runDispatchLoopUntil (lookAgainMs);
        check (getKey ("updateCheck") == "1" && shows (switchB) == "On", "and on again: " + shows (switchB));
        clickAtOnce (switchA);
        check (requests().empty(), "nothing was sent throughout: " + describeRequests());

        // The newer release's rows act on the release they show, even if another MPE Roll has found a
        // newer one since they were drawn: Skip version 9.9.0 never skips 9.9.1.
        setKey ("updateCheckLastTag", "v9.9.0");
        mm.runDispatchLoopUntil (lookAgainMs);
        auto* getB = buttonIn (&panelB, "Newer release");
        auto* skipB = buttonIn (&panelB, "Skip it");
        check (skipB != nullptr && skipB->getButtonText() == "Skip version 9.9.0"
                   && getB != nullptr && getB->getButtonText() == "Get MPE Roll 9.9.0 (release page)...",
               "found in one, the other's MPE settings offers 9.9.0 a moment later: "
                   + (skipB != nullptr ? skipB->getButtonText() : juce::String ("(no row)")));

        setKey ("updateCheckLastTag", "v9.9.1");    // another MPE Roll finds 9.9.1 meanwhile
        pagesOpened.clear();
        clickAtOnce (getB);
        check (pagesOpened.size() == 1 && pagesOpened[0].endsWith ("/releases/tag/v9.9.0"),
               "Get MPE Roll 9.9.0 opens 9.9.0's page: " + pagesOpened.joinIntoString (", "));
        clickAtOnce (skipB);
        check (getKey ("updateCheckSkippedTag") == "v9.9.0", "and Skip version 9.9.0 skips 9.9.0: " + getKey ("updateCheckSkippedTag"));
        mm.runDispatchLoopUntil (lookAgainMs);
        check (skipB != nullptr && skipB->getButtonText() == "Skip version 9.9.1" && skipB->isEnabled(),
               "so 9.9.1 is still offered: " + (skipB != nullptr ? skipB->getButtonText() : juce::String ("(no row)")));

        removeKey ("updateCheckSkippedTag");
        removeKey ("updateCheckLastTag");
        setKey ("updateCheck", false);
        pagesOpened.clear();
    }

    std::printf ("What another MPE Roll finds or skips shows in this one's window, open all the while\n");
    {
        startServer ({ "--tag", "v9.9.9" });
        open();
        check (! updateButton().isVisible(), "the window opens with nothing to offer");

        // Another MPE Roll, with its window open too: its own checker, and its own copy of the settings.
        juce::PropertiesFile::Options sharedOptions;
        sharedOptions.storageFormat = juce::PropertiesFile::storeAsXML;
        juce::PropertiesFile otherSettings (settings->getFile(), sharedOptions);
        UpdateChecker other;
        other.checkIfDue (&otherSettings);

        other.checkNow (&otherSettings);        // File > Check for updates now... there
        waitForChecker (other);
        mm.runDispatchLoopUntil (lookAgainMs);
        auto items = fileMenu();
        check (other.getState().newer.has_value() && updateButton().isVisible() && updateButton().getButtonText() == "Update to 9.9.9"
                   && menuHas (items, "Get MPE Roll 9.9.9 (release page)...") && menuHas (items, "Skip version 9.9.9"),
               "9.9.9, found by the other, is offered here a moment later, by the button and the File menu: "
                   + updateButton().getButtonText() + " / " + describeMenu (items));

        if (const auto found = other.getState().newer)
            other.skip (&otherSettings, *found);    // File > Skip version 9.9.9 there

        mm.runDispatchLoopUntil (lookAgainMs);
        items = fileMenu();
        check (! updateButton().isVisible() && ! menuHas (items, "Skip version 9.9.9") && ! menuHas (items, "Get MPE Roll 9.9.9 (release page)..."),
               "skipped there, it's gone from here a moment later: " + describeMenu (items));

        // And the other way round: found here, offered there.
        startServer ({ "--tag", "v9.9.10" });

        {
            juce::SharedResourcePointer<UpdateChecker> updates;
            updates->checkNow (settings);
            waitForCheck (20000.0);
        }

        mm.runDispatchLoopUntil (lookAgainMs);
        check (updateButton().getButtonText() == "Update to 9.9.10" && other.getState().newer.has_value()
                   && other.getState().newer->tag == "v9.9.10",
               "found here, 9.9.10 is offered there a moment later");
        close();
        removeKey ("updateCheckSkippedTag");
        removeKey ("updateCheckLastTag");
    }

    std::printf ("Switched off while a daily check waits for GitHub: the check is called off, and nothing more is "
                 "sent\n");
    {
        // In MPE settings of the MPE Roll whose window started it.
        startServer ({ "--tag", "v9.9.6", "--api", "hang" });
        setKey ("updateCheck", true);
        dueNow();
        open();

        for (const auto end = now() + 5000.0; requests().empty() && now() < end;)
            mm.runDispatchLoopUntil (20);

        auto* panel = openSettings();

        {
            juce::SharedResourcePointer<UpdateChecker> updates;
            check (requests().size() == 1 && updates->getState().phase == UpdateChecker::Phase::checking,
                   "the daily check's request is under way, and GitHub (here) isn't answering: " + describeRequests());

            click (buttonIn (panel, automaticName));
            check (! UpdateChecker::isAutomatic (settings) && updates->getState().phase != UpdateChecker::Phase::checking,
                   "switched off in MPE settings, the check is called off at once");
        }

        mm.runDispatchLoopUntil (9500);         // past the API's 8 s, when the release page would be asked
        check (requests().size() == 1, "and nothing more is sent, the release page included: " + describeRequests());
        check (outcome() == "failed: the check was switched off before GitHub answered", "the settings say why it ended: " + outcome());
        check (lineOf (panel, "Last check").endsWith (": the check was switched off before GitHub answered."),
               "as MPE settings does: " + lineOf (panel, "Last check"));
        check (! updateButton().isVisible() && ! status().contains ("Couldn't") && ! anyAlert(), "and nothing else is said: " + status());
        close();

        // In another MPE Roll, while this one's daily check waits: this one calls it off on its next look
        // at the file. Meanwhile a third's MPE settings says a check is under way, not that one ended.
        startServer ({ "--tag", "v9.9.6", "--api", "hang" });
        setKey ("updateCheck", true);
        dueNow();

        {
            UpdateChecker here;
            here.checkIfDue (settings);         // its window opens

            for (const auto end = now() + 5000.0; requests().empty() && now() < end;)
                mm.runDispatchLoopUntil (20);

            const auto meanwhile = UpdateChecker().describeLastCheck (settings);
            check (meanwhile.startsWith ("Asked GitHub ") && meanwhile.endsWith ("; no answer yet."),
                   "while it waits, another MPE Roll's MPE settings says so: " + meanwhile);

            setKey ("updateCheck", false);      // as another MPE Roll's switch writes it
            mm.runDispatchLoopUntil (lookAgainMs);
            check (here.getState().phase != UpdateChecker::Phase::checking && requests().size() == 1
                       && outcome() == "failed: the check was switched off before GitHub answered",
                   "switched off in another MPE Roll, the check is called off a moment later: " + outcome() + ", requests: "
                       + describeRequests());
        }

        // Switched off in another MPE Roll just as the API refuses, before this one has looked at the
        // file again (its message loop isn't run meanwhile): the release page isn't asked either.
        startServer ({ "--tag", "v9.9.6", "--api", "403", "--api-delay", "1" });
        setKey ("updateCheck", true);
        dueNow();

        {
            UpdateChecker here;
            here.checkIfDue (settings);
            setKey ("updateCheck", false);
            juce::Thread::sleep (2500);         // the API refuses after a second
            mm.runDispatchLoopUntil (300);
            check (requests().size() == 1 && here.getState().phase == UpdateChecker::Phase::failed
                       && outcome() == "failed: the check was switched off before GitHub answered" && ! here.getState().newer.has_value(),
                   "switched off as the API refuses, the release page isn't asked: " + describeRequests() + ", " + outcome());
        }
    }

    std::printf ("Closing the last MPE Roll during a check calls the request off at once\n");
    {
        startServer ({ "--tag", "v9.9.6", "--api", "hang" });
        setKey ("updateCheck", true);
        dueNow();
        open();

        for (const auto end = now() + 5000.0; requests().empty() && now() < end;)
            mm.runDispatchLoopUntil (20);

        check (requests().size() == 1, "the request is under way, and GitHub (here) isn't answering: " + describeRequests());
        const auto took = close();
        check (took < 2000.0, "closing the window took " + juce::String (juce::roundToInt (took))
                                  + " ms: the request was called off, not waited for (8 s)");
        check (outcome() == "failed: MPE Roll closed before GitHub answered",
               "and the settings say the check didn't finish, and why: " + outcome());

        open();
        auto* panel = openSettings();
        check (requests().size() == 1, "that attempt counts for the day: opening MPE Roll again asks nothing");
        check (lineOf (panel, "Last check").endsWith (": MPE Roll closed before GitHub answered."),
               "and MPE settings says so: " + lineOf (panel, "Last check"));

        // Asked for, while GitHub (here) takes its time: the status line says it's asking, and closing
        // calls this one off too.
        click (buttonIn (panel, "Check now"));

        for (const auto end = now() + 5000.0; requests().size() < 2 && now() < end;)
            mm.runDispatchLoopUntil (20);

        check (requests().size() == 2 && status() == "Checking for updates...", "Check now says it's asking: " + status());
        const auto tookAsked = close();
        check (tookAsked < 2000.0, "and closing the window meanwhile is as quick: " + juce::String (juce::roundToInt (tookAsked)) + " ms");
    }

    std::printf ("No answer from the API in 8 s: the release page is asked instead\n");
    {
        // MPE Roll gives the system's networking 8 s for an answer (on Windows, WinINet's receive
        // timeout), after which the request ends once, unrepeated. What the stand-in saw, with its
        // times, says what the networking did if not.
        dueNow();       // the same server, whose API still doesn't answer
        const auto start = now();
        open();
        const auto answered = waitForCheck (30000.0);
        const auto took = now() - start;
        check (answered && updateButton().getButtonText() == "Update to 9.9.6",
               "the release is found from the release page: " + (answered ? updateButton().getButtonText() : juce::String ("no end to the check in 30 s"))
                   + "; requests: " + describeRequests());
        check (took >= 7000.0 && took < 20000.0,
               "after the API's 8 s were up: found " + juce::String (took / 1000.0, 1) + " s after the window opened; requests: "
                   + describeRequests());
        const auto asked = pathsAsked();
        check (asked.size() == 4 && asked[2] == apiPath && asked[3] == pagePath,
               "the API once, then the release page, neither asked again: " + describeRequests());
        close();
    }

    std::printf ("For testing by hand, updateCheckUrl in the settings points the API elsewhere\n");
    {
        startServer ({ "--tag", "v9.9.7" });
        setKey ("updateCheckUrl", juce::SystemStats::getEnvironmentVariable ("MPEROLL_UPDATE_URL", {}));
        setEnvironment ("MPEROLL_UPDATE_URL", {});
        dueNow();
        open();
        check (waitForCheck (20000.0) && pathsAsked() == juce::StringArray (apiPath) && updateButton().getButtonText() == "Update to 9.9.7",
               "the API asked is the one the settings name");
        close();
        removeKey ("updateCheckUrl");
    }

    // Nothing of the check's went into the settings every MPE Roll writes whole.
    {
        juce::PropertiesFile::Options sharedOptions;
        sharedOptions.storageFormat = juce::PropertiesFile::storeAsXML;
        juce::PropertiesFile onDisk (settings->getFile(), sharedOptions);
        check (! onDisk.getAllProperties().getAllKeys().joinIntoString (" ").contains ("updateCheck")
                   && ! settings->getAllProperties().getAllKeys().joinIntoString (" ").contains ("updateCheck"),
               "MPE Roll.settings holds nothing of the update check's: "
                   + onDisk.getAllProperties().getAllKeys().joinIntoString (", "));
    }

   #if JUCE_LINUX
    // GitHub's own addresses: on Linux the check stops before a thread or a connection is made.
    std::printf ("On Linux, with no HTTPS, GitHub isn't asked, and asked for a check it says why\n");
    {
        stopServer();
        setEnvironment ("MPEROLL_UPDATE_URL", {});
        setEnvironment ("MPEROLL_UPDATE_PAGE_URL", {});
        open();

        {
            juce::SharedResourcePointer<UpdateChecker> updates;
            updates->checkNow (settings);
            const auto answered = waitForCheck (5000.0);
            check (answered && status().startsWith ("Couldn't check for updates: the Linux build can't make HTTPS requests, "
                                                    "so it can't ask GitHub."),
                   "the status line: " + status());
        }

        close();
    }
   #endif

    stopServer();
    setEnvironment ("MPEROLL_UPDATE_URL", {});
    setEnvironment ("MPEROLL_UPDATE_PAGE_URL", {});
    UpdateChecker::setBrowserForTesting ({});
    PluginLog::setFileForTesting ({});
    folder.deleteRecursively();
    std::printf ("%s: %d failure(s)\n", check.failures == 0 ? "PASS" : "FAIL", check.failures);
    return check.failures == 0 ? 0 : 1;
}

//==============================================================================
// Pictures for the website (tools/shoot_images.sh and tools/make_demo.sh): states of the editor in the
// FL Studio skin, the look of a user who never picked one, drawn at a scale (2 for screens with twice
// the pixels) and cropped to what each one shows; and the frames of a short film of the editor at work.

/** The preset skin with this name (the first if none has it). */
int presetNamed (const juce::String& name)
{
    const auto& presets = presetSkins();

    for (size_t i = 0; i < presets.size(); ++i)
        if (presets[i].name == name)
            return (int) i;

    return 0;
}

/** A mouse button held down on a component, pressed as the uitest's gestures press it but a step at a
    time, so the editor can be drawn in the middle of a drag. Positions are the component's own. */
struct Pen
{
    explicit Pen (juce::Component& c, juce::ModifierKeys m = juce::ModifierKeys (juce::ModifierKeys::leftButtonModifier))
        : target (c), mods (m) {}

    juce::MouseEvent event (juce::Point<float> pos, bool dragged) const
    {
        return juce::MouseEvent (juce::Desktop::getInstance().getMainMouseSource(), pos, mods, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                                 &target, &target, when, from, when, 1, dragged);
    }

    void down (juce::Point<float> pos)
    {
        from = last = pos;
        when = juce::Time::getCurrentTime();
        target.mouseDown (event (pos, false));
    }

    void dragTo (juce::Point<float> pos)    { last = pos; target.mouseDrag (event (pos, pos != from)); }
    void up()                               { target.mouseUp (event (last, last != from)); }

    juce::Component& target;
    juce::ModifierKeys mods;
    juce::Point<float> from, last;
    juce::Time when;
};

/** An editor on screen with a clip in it, in a look of its own: a settings file nothing else reads,
    so the computer's own MPE Roll look is left alone, with the FL Studio skin picked. */
struct ShootRig
{
    explicit ShootRig (const MpeClip& clip, juce::Rectangle<int> bounds = { 0, 0, 1200, 760 })
    {
        ignoreXErrors();
        skins->useSettingsFile (lookFile);
        skins->setPreset (presetNamed ("FL Studio"));

        processor.setRateAndBufferSizeDetails (48000.0, 512);
        processor.prepareToPlay (48000.0, 512);
        document().replaceClip (0, clip, clip.name);
        document().getUndoManager().clearUndoHistory();

        editor.reset (processor.createEditorAndMakeActive());
        editor->setBounds (bounds);
        editor->addToDesktop (0);
        editor->setVisible (true);
        pump (300);

        grid = findChild<NoteGrid> (*editor);
        lane = findChild<ExpressionLane> (*editor);
        bar = findChild<Toolbar> (*editor);
    }

    ~ShootRig()
    {
        juce::PopupMenu::dismissAllActiveMenus();
        pump (50);
        editor->removeFromDesktop();
        processor.editorBeingDeleted (editor.get());
        editor.reset();
        pump (100);
        processor.releaseResources();
        skins->flush();
        lookFile.deleteFile();
    }

    bool ok() const                     { return grid != nullptr && lane != nullptr && bar != nullptr; }
    ClipDocument& document()            { return processor.getDocument(); }
    static void pump (int ms)           { juce::MessageManager::getInstance()->runDispatchLoopUntil (ms); }

    /** Selects the notes with these keys, and no others. */
    void select (std::initializer_list<int> keys)
    {
        document().selection.clear();

        for (const auto& n : document().getEditClip().notes)
            if (std::find (keys.begin(), keys.end(), n.key) != keys.end())
                document().selection.insert (n.id);

        document().selectionChanged();
    }

    /** A point in the grid, in its own coordinates, at a beat and a pitch (a key and any glide). */
    juce::Point<float> gridAt (double beat, float pitch) const
    {
        const auto& view = grid->getView();
        return { (float) view.beatToX (beat), view.pitchToY (pitch) };
    }

    /** A point in the expression lane, in its own coordinates, at a beat and a value from 0 to 1. */
    juce::Point<float> laneAt (double beat, float value) const
    {
        const auto top = (float) ExpressionLane::headerHeight + 4.0f, bottom = (float) lane->getHeight() - 4.0f;
        return { (float) ViewState::keysWidth + (float) grid->getView().beatToX (beat), bottom - value * (bottom - top) };
    }

    /** Where a component (the editor's, or one inside it) is in the editor. */
    juce::Rectangle<int> inEditor (const juce::Component& c) const
    {
        return editor->getLocalArea (&c, c.getLocalBounds());
    }

    /** The pop-up menus showing: windows of their own that hold the items (the shadows round them
        hold nothing). */
    std::vector<juce::Component*> menuWindows() const
    {
        std::vector<juce::Component*> windows;
        auto& desktop = juce::Desktop::getInstance();

        for (int i = 0; i < desktop.getNumComponents(); ++i)
            if (auto* window = desktop.getComponent (i);
                window != editor.get() && window->isVisible() && window->getNumChildComponents() > 0)
                windows.push_back (window);

        return windows;
    }

    /** Where a menu is, in the editor's coordinates. */
    juce::Rectangle<int> inEditorFromScreen (const juce::Component& window) const
    {
        return window.getScreenBounds() - editor->getScreenPosition();
    }

    /** The editor as drawn now, at a scale, with the menus showing drawn over it where they are. */
    juce::Image picture (float scale) const
    {
        auto image = editor->createComponentSnapshot (editor->getLocalBounds(), true, scale);
        juce::Graphics g (image);

        for (auto* window : menuWindows())
        {
            const auto area = (inEditorFromScreen (*window).toFloat() * scale).toNearestInt();
            juce::DropShadow (juce::Colours::black.withAlpha (0.6f), juce::roundToInt (12.0f * scale),
                              { 0, juce::roundToInt (3.0f * scale) }).drawForRectangle (g, area);
            g.setOpacity (1.0f);    // images are drawn at the colour's opacity: the shadow's would let the grid through
            g.drawImageAt (window->createComponentSnapshot (window->getLocalBounds(), true, scale), area.getX(), area.getY());
        }

        return image;
    }

    juce::SharedResourcePointer<SkinManager> skins;
    juce::File lookFile = juce::File::createTempFile (".settings");
    MpeRollProcessor processor;
    std::unique_ptr<juce::AudioProcessorEditor> editor;
    NoteGrid* grid = nullptr;
    ExpressionLane* lane = nullptr;
    Toolbar* bar = nullptr;
};

/** Part of a picture drawn at a scale, the area given in the editor's coordinates. */
juce::Image cropped (const juce::Image& image, juce::Rectangle<int> area, float scale)
{
    const auto pixels = (area.toFloat() * scale).getSmallestIntegerContainer().getIntersection (image.getBounds());
    juce::Image out (juce::Image::RGB, juce::jmax (1, pixels.getWidth()), juce::jmax (1, pixels.getHeight()), true);
    juce::Graphics g (out);
    g.drawImageAt (image, -pixels.getX(), -pixels.getY());
    return out;
}

/** A clip of four chords, a bar each, for a tool to work on: C, A minor, F and G. */
MpeClip chordsClip()
{
    MpeClip clip;
    clip.name = "Chords";
    clip.length = 16.0;
    const int chords[4][3] = { { 60, 64, 67 }, { 57, 60, 64 }, { 53, 57, 60 }, { 55, 59, 62 } };

    for (int c = 0; c < 4; ++c)
        for (int key : chords[c])
            clip.addNote (note (key, c * 4.0, 4.0, 0.7f + 0.05f * (float) c));

    return clip;
}

/** A line of notes sliding into one another, the last one held, for a glide to be drawn on. */
MpeClip glideShotClip()
{
    MpeClip clip;
    clip.name = "Glides";
    clip.length = 16.0;

    auto first = note (60, 0.0, 2.0, 0.8f);
    first.lanes.pitch = { { 0.0, 0.0f }, { 1.5, 0.0f }, { 2.0, 4.0f } };
    first.lanes.pressure = { { 0.0, 0.3f }, { 1.0, 0.8f }, { 2.0, 0.5f } };
    clip.addNote (first);

    auto second = note (64, 2.0, 2.0, 0.85f);
    second.lanes.pitch = { { 0.0, 0.0f }, { 1.5, 0.0f }, { 2.0, 3.0f } };
    second.lanes.pressure = { { 0.0, 0.5f }, { 2.0, 0.7f } };
    clip.addNote (second);

    auto held = note (67, 4.0, 4.0, 0.9f);
    held.lanes.pressure = { { 0.0, 0.6f }, { 2.0, 1.0f }, { 4.0, 0.4f } };
    clip.addNote (held);

    clip.addNote (note (55, 0.0, 8.0, 0.6f));
    return clip;
}

/** The editor with the demo clip, D5 and G5 selected and their pressure in the lane. */
juce::Image heroPicture (float scale)
{
    ShootRig rig (makeDemoClip());

    if (! rig.ok())
        return {};

    rig.select ({ 62, 67 });
    ShootRig::pump (100);
    return rig.picture (scale);
}

/** The expression lane with its menu open over the grid, as it opens in a host's window. */
juce::Image lanesPicture (float scale)
{
    // At the foot of the screen, so the menu has no room below and opens upwards.
    const auto screen = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay()->userBounds.toNearestInt();
    ShootRig rig (makeDemoClip(), { screen.getX(), screen.getBottom() - 760, 1200, 760 });
    auto* laneButton = rig.ok() ? findChild<juce::TextButton> (*rig.lane) : nullptr;     // the first: the lane menu's

    if (laneButton == nullptr || laneButton->onClick == nullptr)
        return {};

    rig.select ({ 62 });
    ShootRig::pump (100);
    laneButton->onClick();      // at once: a click would be posted, and the menu closes as soon as it isn't in front
    const auto menus = rig.menuWindows();

    if (menus.empty())
        return {};

    // From just above the menu down through the lane, and from the keys to the lane's Clear button, so
    // the menu's words show at their size on a page: the lane's hint, to the right of Clear, is left
    // out whole rather than cut.
    const auto menu = rig.inEditorFromScreen (*menus.front());
    auto right = menu.getRight() + 8;

    for (auto* child : rig.lane->getChildren())
        if (auto* button = dynamic_cast<juce::TextButton*> (child); button != nullptr && button->getButtonText() == "Clear")
            right = juce::jmax (right, rig.inEditor (*button).getRight() + 8);

    const auto area = juce::Rectangle<int>::leftTopRightBottom (0, menu.getY() - 24, right, rig.editor->getHeight());
    return cropped (rig.picture (scale), area, scale);
}

/** Two held notes whose vibrato comes in late and grows, the second scooped up into, over a low note
    without any. */
MpeClip vibratoShotClip()
{
    MpeClip clip;
    clip.name = "Vibrato";
    clip.length = 16.0;

    auto first = note (62, 0.0, 4.0, 0.8f);
    first.lanes.vibrato = { { 0.0, 0.0f }, { 1.0, 0.0f }, { 2.5, 0.45f }, { 4.0, 0.45f } };
    first.lanes.pressure = { { 0.0, 0.4f }, { 2.0, 0.9f }, { 4.0, 0.6f } };
    clip.addNote (first);

    auto second = note (67, 4.0, 4.5, 0.9f);
    second.lanes.pitch = { { 0.0, -2.0f }, { 0.5, 0.0f } };
    second.lanes.vibrato = { { 0.0, 0.0f }, { 1.5, 0.0f }, { 3.0, 0.6f }, { 4.5, 0.6f } };
    second.lanes.pressure = { { 0.0, 0.5f }, { 3.0, 1.0f }, { 4.5, 0.7f } };
    clip.addNote (second);

    clip.addNote (note (55, 0.0, 8.5, 0.6f));
    clip.addNote (note (59, 0.0, 4.0, 0.6f));
    return clip;
}

/** The vibrato lane, with the vibrato drawn on the notes as it will play: a band of the window from
    the toolbar down, all of its width. Narrow (for a phone's width): the keys and the first note only,
    to just before the second, so its vibrato and the lane's buttons up to the speed show at a size
    there. */
juce::Image vibratoPicture (float scale, bool narrow)
{
    ShootRig rig (vibratoShotClip(), { 0, 0, 1040, 640 });

    if (! rig.ok())
        return {};

    rig.select ({ 62, 67 });
    rig.lane->setTarget (ExpressionLane::Target::noteVibrato);
    ShootRig::pump (100);

    auto area = rig.editor->getLocalBounds().withTrimmedTop (Toolbar::preferredHeight);

    if (narrow)     // short of the second note's scoop, whose dot starts a few pixels before it
        area = area.withRight (rig.inEditor (*rig.grid).getX() + (int) rig.gridAt (3.94, 0.0f).x);

    return cropped (rig.picture (scale), area, scale);
}

/** The pointer the Glide tool shows, a crosshair, drawn at a point of the editor in a picture at a scale. */
void drawCrosshair (juce::Image& image, juce::Point<float> at, float scale)
{
    juce::Graphics g (image);
    const auto centre = at * scale;
    juce::Path cross;
    cross.addRectangle (-10.0f, -1.0f, 20.0f, 2.0f);
    cross.addRectangle (-1.0f, -10.0f, 2.0f, 20.0f);
    cross.applyTransform (juce::AffineTransform::scale (scale).translated (centre));
    g.setColour (juce::Colours::black);
    g.strokePath (cross, juce::PathStrokeType (1.2f * scale));
    g.setColour (juce::Colours::white);
    g.fillPath (cross);
}

/** A glide being dragged up from a held note: the line lands on a whole semitone, the interval and
    the note it reaches beside the pen. Narrow (for a phone's width): the held note, the glide and its
    label alone, without the keys. */
juce::Image glidePicture (float scale, bool narrow)
{
    ShootRig rig (glideShotClip());

    if (! rig.ok())
        return {};

    rig.grid->keyPressed (juce::KeyPress ('g'));       // the Glide tool, as G picks it
    ShootRig::pump (50);

    Pen pen (*rig.grid);
    pen.down (rig.gridAt (5.52, 67.0f));

    for (int i = 1; i <= 16; ++i)
        pen.dragTo (rig.gridAt (5.52 + 2.0 * i / 16.0, 67.0f + 5.2f * (float) i / 16.0f));

    ShootRig::pump (50);
    auto image = rig.picture (scale);
    drawCrosshair (image, rig.editor->getLocalPoint (rig.grid, pen.last), scale);
    pen.up();

    const auto grid = rig.inEditor (*rig.grid);
    const auto area = narrow
        ? juce::Rectangle<int>::leftTopRightBottom (grid.getX() + (int) rig.gridAt (3.55, 0.0f).x,
                                                    juce::jmax (0, grid.getY() + (int) rig.gridAt (0.0, 75.5f).y),
                                                    grid.getX() + (int) rig.gridAt (8.6, 0.0f).x,
                                                    grid.getY() + (int) rig.gridAt (0.0, 61.5f).y)
        : juce::Rectangle<int>::leftTopRightBottom (0, juce::jmax (0, grid.getY() + (int) rig.gridAt (0.0, 77.0f).y),
                                                    grid.getX() + (int) rig.gridAt (8.6, 0.0f).x,
                                                    grid.getY() + (int) rig.gridAt (0.0, 53.0f).y);
    return cropped (image, area.getIntersection (grid.withLeft (0)), scale);
}

/** The Arpeggiate dialog (Alt+A, as in FL), previewing its arpeggios on four chords. */
juce::Image toolsPicture (float scale)
{
    ShootRig rig (chordsClip());

    if (! rig.ok())
        return {};

    rig.editor->keyPressed (juce::KeyPress ('a', juce::ModifierKeys::altModifier, 0));
    ShootRig::pump (300);
    auto* box = findChild<juce::CallOutBox> (*rig.editor);

    if (box == nullptr)
        return {};

    // From the toolbar's second row, where the Tools button is, down past the dialog; on the right, a
    // little past the dialog, so its words show near their size on a page, and short of the status
    // line's words, which would be cut.
    const auto dialog = box->getBoundsInParent();
    const auto& status = rig.bar->status;
    const auto words = (int) std::ceil (juce::GlyphArrangement::getStringWidth (status.getFont(), status.getText()))
                     + status.getBorderSize().getRight();
    const auto right = juce::jmin (dialog.getRight() + 32, rig.inEditor (status).getRight() - words - 16);
    const auto area = juce::Rectangle<int>::leftTopRightBottom (0, Toolbar::rowHeight + 2, right,
                                                                juce::jmin (rig.editor->getHeight(), dialog.getBottom() + 40));
    return cropped (rig.picture (scale), area, scale);
}

/** MPE settings open on one section: Output to synth, or (copies) One copy per note with the synth
    played that way. With a synth file, that synth is loaded first, so the settings say how it's
    played; its name shows in neither section. The build's name is left out, as it would date the
    picture. */
juce::Image settingsPicture (float scale, bool copies, const juce::File& synthFile)
{
    // Wide and tall enough that the call-out, hung off the settings button at the right, covers no
    // other toolbar button and ends above the lane's words, which would be cut at its edges.
    ShootRig rig (makeDemoClip(), { 0, 0, 1300, 900 });

    if (! rig.ok())
        return {};

    if (copies)
    {
        auto settings = rig.document().getSettings();
        settings.output.mode = OutputSettings::Mode::copies;
        rig.document().setSettings (settings);
    }

    if (synthFile != juce::File())
    {
        if (! loadSynth (rig.processor, synthFile))
            return {};

        // The copies load one after another, on their own.
        for (int i = 0; i < 200; ++i)
        {
            const auto status = rig.processor.getSynth().getCopyStatus();

            if (! copies || (status.on && ! status.loading && status.loaded == status.wanted))
                break;

            ShootRig::pump (50);
        }
    }

    rig.bar->settingsButton.triggerClick();
    ShootRig::pump (300);
    auto* box = findChild<juce::CallOutBox> (*rig.editor);
    auto* panel = box != nullptr ? findChild<SettingsPanel> (*box) : nullptr;
    auto* sections = panel != nullptr ? findChild<juce::PropertyPanel> (*panel) : nullptr;

    if (sections == nullptr)
        return {};

    const auto names = sections->getSectionNames();

    for (int i = 0; i < names.size(); ++i)
        sections->setSectionOpen (i, names[i] == (copies ? "One copy per note" : "Output to synth"));

    if (auto* build = findLabelSaying (*panel, getBuildName()))
        build->setVisible (false);

    rig.bar->status.setVisible (false);     // behind the call-out, but for a stray letter or two past its edge

    ShootRig::pump (800);       // the panel's lines are brought up to date twice a second

    // The panel is as tall as the settings window always is; a section shorter than that (One copy per
    // note) would leave it empty above the links at its foot. Shorter, then, by what the sections leave
    // empty: the call-out follows the panel's size.
    if (const auto spare = sections->getHeight() - sections->getTotalContentHeight(); spare > 0)
    {
        panel->setSize (panel->getWidth(), panel->getHeight() - spare);
        ShootRig::pump (100);
    }

    const auto area = box->getBoundsInParent().getUnion (rig.inEditor (rig.bar->settingsButton)).expanded (14, 10);
    return cropped (rig.picture (scale), area.getIntersection (rig.editor->getLocalBounds()), scale);
}

/** The ten skins side by side, five to a row (two for a phone's width), each the whole window, named.
    Transparent between them. */
juce::Image skinsPicture (float scale, int columns)
{
    ShootRig rig (makeDemoClip());

    if (! rig.ok())
        return {};

    rig.select ({ 62, 67 });
    const auto& presets = presetSkins();
    const auto bounds = rig.editor->getLocalBounds();
    const auto tileScale = scale * 200.0f / (float) bounds.getWidth();      // 200 points wide
    const auto tileWidth = juce::roundToInt ((float) bounds.getWidth() * tileScale);
    const auto tileHeight = juce::roundToInt ((float) bounds.getHeight() * tileScale);
    const auto gap = juce::roundToInt (12.0f * scale);
    const auto rows = ((int) presets.size() + columns - 1) / columns;

    juce::Image sheet (juce::Image::ARGB, columns * tileWidth + (columns - 1) * gap, rows * tileHeight + (rows - 1) * gap, true);
    juce::Graphics g (sheet);

    for (size_t i = 0; i < presets.size(); ++i)
    {
        rig.skins->setPreset ((int) i);
        ShootRig::pump (100);

        const juce::Rectangle<int> tile (((int) i % columns) * (tileWidth + gap), ((int) i / columns) * (tileHeight + gap),
                                         tileWidth, tileHeight);
        g.drawImageAt (rig.editor->createComponentSnapshot (bounds, true, tileScale), tile.getX(), tile.getY());

        const auto name = presets[i].name + (presets[i].name == "FL Studio" ? " (default)" : "");
        const juce::Font font (juce::FontOptions (12.0f * scale, juce::Font::bold));
        const auto label = juce::Rectangle<float> ((float) tile.getX() + 5.0f * scale, (float) tile.getBottom() - 25.0f * scale,
                                                   juce::GlyphArrangement::getStringWidth (font, name) + 12.0f * scale, 20.0f * scale);
        g.setColour (juce::Colours::black.withAlpha (0.78f));
        g.fillRoundedRectangle (label, 4.0f * scale);
        g.setColour (juce::Colours::white);
        g.setFont (font);
        g.drawText (name, label, juce::Justification::centred);
    }

    rig.skins->setPreset (presetNamed ("FL Studio"));
    return sheet;
}

/** Whether the website's pictures can be drawn at a scale: from 0.25 to 4 (a word given for one reads as 0). */
bool usableScale (float scale)
{
    if (scale >= 0.25f && scale <= 4.0f)
        return true;

    std::printf ("The scale must be a number from 0.25 to 4: 2 for screens with twice the pixels\n");
    return false;
}

int shoot (const juce::String& state, const juce::File& target, float scale, const juce::File& synthFile)
{
    if (! usableScale (scale))
        return 2;

    juce::Image image;

    // The -narrow states are the same pictures for a phone's width (docs/_data/screenshots.yml, narrow).
    if (state == "hero")                    image = heroPicture (scale);
    else if (state == "lanes")              image = lanesPicture (scale);
    else if (state == "vibrato")            image = vibratoPicture (scale, false);
    else if (state == "vibrato-narrow")     image = vibratoPicture (scale, true);
    else if (state == "glide")              image = glidePicture (scale, false);
    else if (state == "glide-narrow")       image = glidePicture (scale, true);
    else if (state == "tools")              image = toolsPicture (scale);
    else if (state == "settings")           image = settingsPicture (scale, false, synthFile);
    else if (state == "copies")             image = settingsPicture (scale, true, synthFile);
    else if (state == "skins")              image = skinsPicture (scale, 5);
    else if (state == "skins-narrow")       image = skinsPicture (scale, 2);
    else
    {
        std::printf ("No state called %s: hero, lanes, vibrato, vibrato-narrow, glide, glide-narrow, tools, settings, "
                     "copies, skins or skins-narrow\n", state.toRawUTF8());
        return 2;
    }

    const auto ok = image.isValid() && writePng (image, target);
    std::printf ("%s %s (%s, %d x %d)\n", ok ? "Wrote" : "Failed to write", target.getFullPathName().toRawUTF8(),
                 state.toRawUTF8(), image.getWidth(), image.getHeight());
    return ok ? 0 : 1;
}

/** Makes the frames of a short film of the editor at work, 15 a second, with the pointer drawn where
    the mouse is: steps done with the uitest's synthetic mouse events, a frame after each, and frames
    between them where the pointer only moves. */
class DemoFilm
{
public:
    static constexpr int fps = 15;

    DemoFilm (ShootRig& r, const juce::File& f, float s) : rig (r), folder (f), scale (s) {}

    int getNumFrames() const noexcept   { return count; }

    /** A frame of the editor as it is now. Drawn again only when it may have changed. */
    void frame (bool changed = true)
    {
        if (changed || ! editorPicture.isValid())
        {
            ShootRig::pump (20);
            editorPicture = rig.picture (scale);
        }

        auto image = editorPicture.createCopy();
        drawPointer (image);
        writePng (image, folder.getChildFile ("frame-" + juce::String (++count).paddedLeft ('0', 4) + ".png"));
    }

    void hold (double seconds)
    {
        for (int i = frames (seconds); --i >= 0;)
            frame (false);
    }

    /** The editor as it is now, without the pointer, as poster.png: what a page shows before the film plays. */
    void poster()
    {
        ShootRig::pump (20);
        writePng (rig.picture (scale), folder.getChildFile ("poster.png"));
    }

    /** The pointer moving to a point in the editor, the button up, easing in and out. */
    void moveTo (juce::Point<float> where, double seconds)
    {
        const auto from = pointer;
        const auto n = juce::jmax (1, frames (seconds));

        for (int i = 1; i <= n; ++i)
        {
            const auto t = (float) i / (float) n;
            pointer = from + (where - from) * (t * t * (3.0f - 2.0f * t));
            frame (false);
        }
    }

    void moveTo (juce::Component& c, juce::Point<float> where, double seconds)
    {
        moveTo (rig.editor->getLocalPoint (&c, where), seconds);
    }

    /** Presses on a component at the first point of a path, drags along the rest of it and lets go: a
        frame for each step. One point is a click. */
    void drag (juce::Component& target, const std::vector<juce::Point<float>>& path, double seconds,
               juce::ModifierKeys mods = juce::ModifierKeys (juce::ModifierKeys::leftButtonModifier))
    {
        moveTo (target, path.front(), 0.35);
        Pen pen (target, mods);
        pressed = true;
        pen.down (path.front());
        frame();

        if (path.size() > 1)
        {
            const auto n = juce::jmax (1, frames (seconds));

            for (int i = 1; i <= n; ++i)
            {
                const auto p = along (path, (float) i / (float) n);
                pen.dragTo (p);
                pointer = rig.editor->getLocalPoint (&target, p);
                frame();
            }
        }
        else
        {
            frame (false);
        }

        pen.up();
        pressed = false;
        frame();
    }

    /** The pointer going to a toolbar button and clicking it. */
    void click (juce::Button& button)
    {
        moveTo (button, button.getLocalBounds().getCentre().toFloat(), 0.4);
        pressed = true;
        frame (false);
        button.triggerClick();
        ShootRig::pump (200);       // the click comes as a message, and the button then shows pressed for
                                    // 100 ms: wait for both, or the next frame depends on how busy the computer is
        frame();
        pressed = false;
        frame (false);
    }

    juce::Point<float> pointer;

private:
    ShootRig& rig;
    juce::File folder;
    float scale;
    int count = 0;
    bool pressed = false;
    juce::Image editorPicture;

    static int frames (double seconds)      { return juce::roundToInt (seconds * fps); }

    static juce::Point<float> along (const std::vector<juce::Point<float>>& path, float t)
    {
        const auto position = t * (float) (path.size() - 1);
        const auto i = juce::jlimit (0, (int) path.size() - 2, (int) std::floor (position));
        return path[(size_t) i] + (path[(size_t) i + 1] - path[(size_t) i]) * (position - (float) i);
    }

    /** An arrow, as a system draws the mouse pointer, with a ring round its tip while the button is down. */
    void drawPointer (juce::Image& image) const
    {
        juce::Graphics g (image);
        const auto tip = pointer * scale;

        if (pressed)
        {
            g.setColour (juce::Colours::white.withAlpha (0.4f));
            g.fillEllipse (juce::Rectangle<float> (26.0f * scale, 26.0f * scale).withCentre (tip));
        }

        juce::Path arrow;
        arrow.startNewSubPath (0.0f, 0.0f);
        arrow.lineTo (0.0f, 17.0f);
        arrow.lineTo (4.2f, 13.3f);
        arrow.lineTo (7.0f, 19.6f);
        arrow.lineTo (9.9f, 18.4f);
        arrow.lineTo (7.2f, 12.2f);
        arrow.lineTo (12.6f, 12.2f);
        arrow.closeSubPath();
        arrow.applyTransform (juce::AffineTransform::scale (scale).translated (tip));

        g.setColour (juce::Colours::white);
        g.fillPath (arrow);
        g.setColour (juce::Colours::black);
        g.strokePath (arrow, juce::PathStrokeType (1.3f * scale, juce::PathStrokeType::mitered));
    }
};

/** The website's demo film: three notes drawn as a chord, the first lengthened; a glide dragged up from
    the top note with the Glide tool; a swell of pressure painted in the lane under them all; the skins
    gone through; and the clip cleared again, so the film loops. The clip is a bar long, and fills the
    grid, so the drawing is as large as it can be on a phone's screen too. */
int demoFrames (const juce::File& folder, float scale)
{
    if (! usableScale (scale))
        return 2;

    folder.createDirectory();

    for (const auto& old : folder.findChildFiles (juce::File::findFiles, false, "frame-*.png;poster.png"))
        old.deleteFile();

    MpeClip empty;
    empty.name = "Demo";
    empty.length = 4.0;
    ShootRig rig (empty, { 0, 0, 960, 600 });

    if (! rig.ok())
        return 1;

    auto& grid = *rig.grid;
    auto& lane = *rig.lane;

    // Zoomed in as a user would, with the wheel: Ctrl for the bar to fill the grid (six steps of 15 %
    // from 96 pixels a beat), Alt for taller rows, and then scrolled so the notes to come are in the
    // middle.
    {
        const auto wheel = [&grid] (int modifiers, juce::Point<float> at, float deltaY)
        {
            juce::MouseWheelDetails details;
            details.deltaX = 0.0f;
            details.deltaY = deltaY;
            details.isReversed = details.isSmooth = details.isInertial = false;
            grid.mouseWheelMove (Pen (grid, juce::ModifierKeys (modifiers)).event (at, false), details);
        };

        for (int i = 0; i < 6; ++i)
            wheel (juce::ModifierKeys::commandModifier, { 0.0f, 0.0f }, 1.0f);

        for (int i = 0; i < 4; ++i)
            wheel (juce::ModifierKeys::altModifier, rig.gridAt (0.0, 64.5f), 1.0f);

        const auto& view = grid.getView();
        const auto wanted = 64.5f + 0.5f + (float) grid.getHeight() * 0.5f / view.rowHeight;
        wheel (0, { 1.0f, 1.0f }, (wanted - view.scrollKey) / 30.0f);
    }

    DemoFilm film (rig, folder, scale);
    const auto rest = rig.editor->getLocalPoint (&grid, rig.gridAt (3.3, 62.0f));     // between the notes to come
    film.pointer = rest;
    film.hold (0.5);

    // A note, lengthened from its end to a bar, and two more over it, as long as it now is. The end is
    // taken within the last 7 pixels of the note, where a drag resizes it.
    film.drag (grid, { rig.gridAt (0.1, 60.0f) }, 0.0);
    film.drag (grid, { rig.gridAt (0.985, 60.0f), rig.gridAt (3.985, 60.0f) }, 0.6);
    film.drag (grid, { rig.gridAt (0.1, 64.0f) }, 0.0);
    film.drag (grid, { rig.gridAt (0.1, 67.0f) }, 0.0);
    film.hold (0.2);

    // The Glide tool, and a glide up a whole tone from halfway through the top note.
    film.click (rig.bar->glideToolButton);
    film.drag (grid, { rig.gridAt (2.02, 67.0f), rig.gridAt (2.6, 67.6f), rig.gridAt (3.27, 68.6f), rig.gridAt (3.52, 69.1f) }, 0.8);
    film.hold (0.3);

    // Nothing selected (Ctrl+D, as in FL), so the lane draws onto every note under the pen: a swell.
    grid.keyPressed (juce::KeyPress ('d', juce::ModifierKeys::commandModifier, 0));
    film.frame();
    std::vector<juce::Point<float>> swell;

    for (int i = 0; i <= 24; ++i)
    {
        const auto beat = 4.0 * i / 24.0;      // the notes' whole length, so no stretch of them is left at none
        const auto rise = std::sin (juce::MathConstants<double>::pi * juce::jmin (1.0, beat / 2.6) * 0.5);
        const auto value = 0.12 + 0.8 * rise - (beat > 2.6 ? 0.45 * (beat - 2.6) / 1.4 : 0.0);
        swell.push_back (rig.laneAt (beat, (float) value));
    }

    film.drag (lane, swell, 1.2);
    film.poster();
    film.hold (0.5);

    // The look: three more skins, then FL Studio's again.
    for (const auto* name : { "Midnight", "Synthwave", "Paper", "FL Studio" })
    {
        rig.skins->setPreset (presetNamed (name));
        film.frame();
        film.hold (0.45);
    }

    // Everything selected and deleted, so the film starts again where it began.
    grid.keyPressed (juce::KeyPress ('p'));
    grid.keyPressed (juce::KeyPress ('a', juce::ModifierKeys::commandModifier, 0));
    film.frame();
    film.hold (0.3);
    grid.keyPressed (juce::KeyPress (juce::KeyPress::deleteKey));
    film.frame();
    film.moveTo (rest, 0.5);

    std::printf ("Wrote %d frames (%.1f s at %d a second) to %s\n", film.getNumFrames(),
                 film.getNumFrames() / (double) DemoFilm::fps, DemoFilm::fps, folder.getFullPathName().toRawUTF8());
    return 0;
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    const juce::StringArray args (argv + 1, argc - 1);
    const auto cwd = juce::File::getCurrentWorkingDirectory();

    if (args.size() >= 2 && args[0] == PluginScanner::scanArgument)
        return standInScanner (cwd.getChildFile (args[1]));

    if (args.size() >= 2 && args[0] == "snapshot")
        return snapshot (cwd.getChildFile (args[1]),
                         args.size() >= 4 ? args[2].getIntValue() : 1200,
                         args.size() >= 4 ? args[3].getIntValue() : 760);

    if (args.size() >= 2 && args[0] == "settings")
        return settingsSnapshot (cwd.getChildFile (args[1]), args.size() >= 3 ? args[2] : juce::String());

    if (args.size() >= 2 && args[0] == "skins")
        return skinsSnapshots (cwd.getChildFile (args[1]));

    if (args.size() >= 3 && args[0] == "shoot")
        return shoot (args[1], cwd.getChildFile (args[2]), args.size() >= 4 ? (float) args[3].getDoubleValue() : 2.0f,
                      args.size() >= 5 ? cwd.getChildFile (args[4]) : juce::File());

    if (args.size() >= 2 && args[0] == "demo")
        return demoFrames (cwd.getChildFile (args[1]), args.size() >= 3 ? (float) args[2].getDoubleValue() : 2.0f);

    if (args.size() >= 2 && args[0] == "donate")
    {
        // The Support MPE Roll window's content, at twice the size, for its QR codes to be read back.
        MpeLookAndFeel lookAndFeel;
        juce::LookAndFeel::setDefaultLookAndFeel (&lookAndFeel);
        bool ok = false;
        {
            DonatePanel panel;
            juce::MessageManager::getInstance()->runDispatchLoopUntil (100);
            ok = writePng (panel.createComponentSnapshot (panel.getLocalBounds(), true, 2.0f), cwd.getChildFile (args[1]));
        }
        juce::LookAndFeel::setDefaultLookAndFeel (nullptr);
        std::printf ("%s %s\n", ok ? "Wrote" : "Failed to write", cwd.getChildFile (args[1]).getFullPathName().toRawUTF8());
        return ok ? 0 : 1;
    }

    if (args.size() >= 3 && args[0] == "export")
    {
        ignoreXErrors();     // the progress window
        return exportTest (cwd.getChildFile (args[1]), cwd.getChildFile (args[2]));
    }

    if (args.size() >= 3 && args[0] == "render")
        return render (cwd.getChildFile (args[1]), cwd.getChildFile (args[2]));

    if (args.size() >= 1 && args[0] == "selftest")
        return selfTest();

    if (args.size() >= 1 && args[0] == "uitest")
        return uiTest();

    if (args.size() >= 2 && args[0] == "params")
        return parameterTest (cwd.getChildFile (args[1]));

    if (args.size() >= 2 && args[0] == "clapdialect")
        return clapDialectTest (cwd.getChildFile (args[1]));

    if (args.size() >= 3 && args[0] == "vst3dialect")
        return vst3DialectTest (cwd.getChildFile (args[1]), cwd.getChildFile (args[2]));

    if (args.size() >= 2 && args[0] == "copies")
        return copiesTest (cwd.getChildFile (args[1]));

    if (args.size() >= 2 && args[0] == "bendtweaks")
        return bendTweaksTest (cwd.getChildFile (args[1]));

    if (args.size() >= 2 && args[0] == "gatetest")
        return gateTest (cwd.getChildFile (args[1]));

    if (args.size() >= 2 && args[0] == "vst3hostmidi")
        return vst3HostMidiTest (cwd.getChildFile (args[1]));

    if (args.size() >= 2 && args[0] == "crashtest")
        return crashTest (cwd.getChildFile (args[1]), args.size() >= 3 && args[2] == "overflow");

    if (args.size() >= 5 && args[0] == "scantest")
        return scanTest (cwd.getChildFile (args[1]), cwd.getChildFile (args[2]), cwd.getChildFile (args[3]),
                         cwd.getChildFile (args[4]));

    if (args.size() >= 4 && args[0] == "loadfailtest")
        return loadFailTest (cwd.getChildFile (args[1]), cwd.getChildFile (args[2]), cwd.getChildFile (args[3]),
                             args.size() >= 5 ? cwd.getChildFile (args[4]) : juce::File());

    if (args.size() >= 2 && args[0] == "updatetest")
        return updateCheckTest (cwd.getChildFile (args[1]));

    std::printf ("Usage:\n  MpeRollHarness snapshot <out.png> [width height]\n"
                 "  MpeRollHarness settings <out.png>\n"
                 "  MpeRollHarness skins <folder>\n"
                 "  MpeRollHarness shoot <hero|lanes|vibrato|glide|tools|settings|copies|skins> <out.png> [scale] [synth]\n"
                 "                       (vibrato, glide and skins also as vibrato-narrow and so on, for a phone)\n"
                 "  MpeRollHarness demo <folder> [scale]\n"
                 "  MpeRollHarness donate <out.png>\n"
                 "  MpeRollHarness export <synth> <folder>\n"
                 "  MpeRollHarness render <synth.vst3> <out.wav>\n"
                 "  MpeRollHarness selftest\n"
                 "  MpeRollHarness uitest\n"
                 "  MpeRollHarness params <synth.vst3>\n"
                 "  MpeRollHarness clapdialect <MPE Roll Test Synth.clap>\n"
                 "  MpeRollHarness vst3dialect <MPE Roll Note Expression Test Synth.vst3> <MPE Roll Test Synth.vst3>\n"
                 "  MpeRollHarness copies <MPE Roll Plain Test Synth>\n"
                 "  MpeRollHarness bendtweaks <MPE Roll Plain Test Synth>\n"
                 "  MpeRollHarness gatetest <MPE Roll Plain Test Synth>\n"
                 "  MpeRollHarness vst3hostmidi <MPE Roll.vst3>\n"
                 "  MpeRollHarness crashtest <report.txt> [overflow]\n"
                 "  MpeRollHarness scantest <MPE Roll Scanner> <synth> <crashing.clap> <hanging.clap>\n"
                 "  MpeRollHarness loadfailtest <MPE Roll Scanner> <synth.vst3> <VST2 synth>\n"
                 "  MpeRollHarness updatetest <tools/fake_releases_server.py>\n");
    return 2;
}
