// Developer harness for MPE Roll (built with -DMPEROLL_BUILD_DEV_TOOLS=ON).
//
//   MpeRollHarness snapshot <out.png> [width height]   renders the editor with a demo clip
//   MpeRollHarness settings <out.png>                   renders the MPE settings panel
//   MpeRollHarness skins <folder>                       renders the editor in every skin, and a sheet of them
//   MpeRollHarness export <synth> <folder>              exports a clip as WAV and MP3 and reads them back
//   MpeRollHarness render <synth.vst3> <out.wav>        plays a test clip through a hosted synth and
//                                                       checks that per-note glides reach it
//   MpeRollHarness selftest                             live input, recording, state, note trigger, undo
//   MpeRollHarness uitest                               simulated mouse and keyboard editing
//   MpeRollHarness params <synth.vst3>                  synth parameters exposed for host automation
//   MpeRollHarness crashtest <report.txt> [overflow]    Windows: crashes on purpose (or overflows the stack)
//                                                       to check the crash report
//   MpeRollHarness scantest <scanner> <synth> <crashing.clap> <hanging.clap>
//                                                       plugin files are looked at in the scanner process

#include "plugin/CrashReport.h"
#include "plugin/PluginEditor.h"
#include "plugin/PluginProcessor.h"
#include "plugin/PluginScanner.h"
#include "plugin/ui/SettingsPanel.h"
#include "plugin/ui/Toolbar.h"
#include "plugin/ui/WindowDrawing.h"

#if MPEROLL_HOST_CLAP
 #include "plugin/clap/ClapHosting.h"
#endif

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_audio_utils/juce_audio_utils.h>

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

    {
        SettingsPanel panel (processor);
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

/** The "No sound from ..." warning: given for a synth that stays silent over MPE, but not while it
    may still be loading, not once it has sounded, and not for silence added up from short notes.
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
        probe.hold (60, 2.0);           // four seconds of silent notes, but none held for three
        probe.hold (62, 2.0);
        check (! probe.warned(), "short notes don't add up to the warning");

        probe.hold (60, 3.5);
        check (probe.warned(), "a note held for three seconds without a sound does");
    }

    {
        SilenceProbe probe (synthFile, OutputSettings::Mode::mpe, true);
        probe.hold (60, 14.0);          // warned, but the warning hasn't been shown
        probe.mute (false);
        probe.hold (60, 0.5);
        check (probe.loaded && ! probe.warned(), "a warning not shown yet is taken back once the synth sounds");
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

/** The "No sound from ..." box the editor shows for the warning. Only its Use Plain MIDI button may
    change the project: Return and Escape both keep MPE, as closing the box does.
*/
void silenceAlertChecks (Check& check, const juce::File& synthFile)
{
    if (synthFile.hasFileExtension ("clap"))     // played with note expressions, so never warned about
        return;

    std::printf ("Only the box's Use Plain MIDI button switches the project to Plain MIDI\n");
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

    auto* box = provoke();
    check (box != nullptr, "the warning opens the box");

    if (box != nullptr)
    {
        press (*box, juce::KeyPress::escapeKey);
        check (findBox() == nullptr && mode() == OutputSettings::Mode::mpe, "Escape closes it and keeps MPE");
    }

    listenAgain();
    box = provoke();
    check (box != nullptr, "the box comes back for a synth given its state again");

    if (box != nullptr)
    {
        press (*box, juce::KeyPress::returnKey);
        check (findBox() == nullptr && mode() == OutputSettings::Mode::mpe, "Return closes it and keeps MPE too");
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

            std::printf ("Paint tool pointer\n");
            {
                grid->keyPressed (juce::KeyPress ('b'));
                const auto now = juce::Time::getCurrentTime();
                const juce::MouseEvent hover (juce::Desktop::getInstance().getMainMouseSource(), at (5.0, 60.0f), juce::ModifierKeys(),
                                              1.0f, 0.0f, 0.0f, 0.0f, 0.0f, grid, grid, now, at (5.0, 60.0f), now, 0, false);
                grid->mouseMove (hover);
                const auto brush = grid->getMouseCursor();
                check (brush != juce::MouseCursor::NormalCursor, "the Paint tool has a pointer of its own");

                skins->setPreset (1);
                mm.runDispatchLoopUntil (50);
                grid->mouseMove (hover);
                check (grid->getMouseCursor() != brush, "which is drawn again in a new skin's colours");
                grid->keyPressed (juce::KeyPress ('e'));
            }

            std::printf ("The settings call-out\n");
            editor->addToDesktop (0);
            editor->setVisible (true);
            mm.runDispatchLoopUntil (200);

            bar->settingsButton.triggerClick();
            mm.runDispatchLoopUntil (50);
            settingsBox = findChild<juce::CallOutBox> (*editor);
            check (settingsBox != nullptr, "the MPE settings button opens them in a call-out");

            if (auto* box = settingsBox.getComponent())
                check (findLabelSaying (*box, "scale, drawn by") != nullptr,
                       "whose Window line says how the window is drawn from the moment it opens");

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
        grid->keyPressed (juce::KeyPress ('d', juce::ModifierKeys::commandModifier, 0));
        check (clip().notes.size() == 2 && std::abs (clip().notes[1].start - 5.0) < 1.0e-9, "Ctrl+D duplicates after the selection");

        grid->keyPressed (juce::KeyPress (juce::KeyPress::upKey));
        check (clip().notes[1].key == 63, "Up transposes the selection");

        gesture (*grid, at (3.5, 62.0f), at (3.5, 62.0f), right);
        check (clip().notes.size() == 1, "right-click deletes a note");

        document.getUndoManager().undo();
        check (clip().notes.size() == 2, "undo brings it back");
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

        for (const auto& f : { lookFile, pictureFile, notAPicture })
            f.deleteFile();
    }

    scannerUiTest (check);
    editorUiChecks (check);

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
// written what it found, as a plugin does whose clean-up code crashes.
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

    if (args.size() >= 2 && args[0] == "crashtest")
        return crashTest (cwd.getChildFile (args[1]), args.size() >= 3 && args[2] == "overflow");

    if (args.size() >= 5 && args[0] == "scantest")
        return scanTest (cwd.getChildFile (args[1]), cwd.getChildFile (args[2]), cwd.getChildFile (args[3]),
                         cwd.getChildFile (args[4]));

    std::printf ("Usage:\n  MpeRollHarness snapshot <out.png> [width height]\n"
                 "  MpeRollHarness settings <out.png>\n"
                 "  MpeRollHarness skins <folder>\n"
                 "  MpeRollHarness export <synth> <folder>\n"
                 "  MpeRollHarness render <synth.vst3> <out.wav>\n"
                 "  MpeRollHarness selftest\n"
                 "  MpeRollHarness uitest\n"
                 "  MpeRollHarness params <synth.vst3>\n"
                 "  MpeRollHarness clapdialect <MPE Roll Test Synth.clap>\n"
                 "  MpeRollHarness crashtest <report.txt> [overflow]\n"
                 "  MpeRollHarness scantest <MPE Roll Scanner> <synth> <crashing.clap> <hanging.clap>\n");
    return 2;
}
