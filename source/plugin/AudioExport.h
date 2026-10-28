#pragma once

#include "HostedSynth.h"
#include "core/MpeClip.h"
#include "core/MpeSettings.h"

#include <functional>
#include <memory>
#include <vector>

namespace mperoll
{
class MpeRollProcessor;

/** Exporting a clip as audio: it's played through a copy of the loaded synth (same plugin, same
    sound), faster than real time and away from the audio thread, then written as WAV or MP3. The
    synth playing live isn't touched.
*/
namespace AudioExport
{
    enum class Format { wav, mp3 };

    /** True if this build can write MP3 (it's built with the LAME encoder). */
    bool canWriteMp3();

    struct Options
    {
        Format format = Format::wav;
        double sampleRate = 48000.0;
        double bpm = 120.0;
        int blockSize = 512;
        double controlIntervalMs = 1.0;     // expression resolution, as in playback
        double maxTailSeconds = 10.0;       // how long to let the synth ring on after the last note
        int wavBits = 24;
        int mp3Kbps = 320;
        juce::String title;
    };

    /** Gets the synth ready for render(): offline quality, then its rate and block size. Message
        thread (VST3 synths must be prepared there).
    */
    void prepareForRender (HostedSynth&, const Options&);

    /** Plays the clip once from its start through the synth and returns what it made, ending once
        the synth has gone quiet after the last note or after maxTailSeconds. progress is called
        now and then with 0..1; it returns false to cancel, which returns an empty buffer. Any
        thread but the audio thread, with nothing else using the synth, after prepareForRender().
    */
    juce::AudioBuffer<float> render (const MpeClip&, const OutputSettings&, HostedSynth&, const Options&,
                                     const std::function<bool (double)>& progress = {});

    /** These return an error message, or an empty string when the file was written. shouldCancel,
        if given, is asked now and then while writing, up to the moment the file takes the target's
        place: once it says yes, nothing is written (a file already at the target is left as it was)
        and the message is "Export cancelled.". A cancel after that is too late to undo the file.
    */
    juce::String writeWav (const juce::AudioBuffer<float>&, double sampleRate, const juce::File&, int bitsPerSample,
                           const std::function<bool()>& shouldCancel = {});
    juce::String writeMp3 (const juce::AudioBuffer<float>&, double sampleRate, const juce::File&, int kbps,
                           const juce::String& title, const std::function<bool()>& shouldCancel = {});

    class Job;

    /** The exports an MPE Roll instance has running: each a thread playing a copy of the synth,
        under a progress window. The instance owns them through this, so when it goes (FL removing
        the plugin, or closing the project) every export is stopped and waited for first, rather
        than running on with its owner gone and, if that was the last plugin, JUCE shut down under it.
    */
    class Jobs
    {
    public:
        Jobs();
        ~Jobs();

        int getNumRunning() const noexcept;

        /** How far the running job at index has got, 0..1, as its window shows it: 0 until the synth
            copy has loaded and started playing, under 0.9 while the clip renders, 0.95 while the
            file is written. For checks that want to catch an export part way. Message thread.
        */
        double getProgress (int index) const noexcept;

    private:
        friend class Job;

        void remove (Job&);

        std::vector<std::unique_ptr<Job>> running;
        std::shared_ptr<int> lifetime = std::make_shared<int> (0);   // lets a job's deferred steps see that we're gone

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Jobs)
    };

    /** The whole export, with a progress window: copies the loaded synth, renders the clip with the
        project's output settings and writes the file. done gets an error message, or an empty
        string; it isn't called if the processor goes away first, which ends the export. Cancel
        gives "Export cancelled." and leaves no file, unless the file was already finished when it
        was pressed: then the file is kept and reported as exported. Message thread.
    */
    void exportClip (MpeRollProcessor&, const MpeClip&, const juce::File& target, const Options&,
                     std::function<void (const juce::String& error)> done);
}

} // namespace mperoll
