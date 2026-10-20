#include "AudioExport.h"
#include "PluginProcessor.h"
#include "core/ClipPlayer.h"
#include "core/MpeEncoder.h"

#include <juce_audio_formats/juce_audio_formats.h>

#if MPEROLL_MP3
 #include <lame.h>
#endif

#include <cmath>

namespace mperoll::AudioExport
{

namespace
{
    constexpr double leadInSeconds = 0.25;      // the synth runs a moment before the clip starts, to settle
    constexpr float silence = 1.0e-4f;          // -80 dBFS
    constexpr double quietEnough = 0.3;         // seconds of silence after the last note that end the tail

    /** The transport the synth sees: playing at the export's tempo from the clip's start, in 4/4. */
    struct RenderHead final : juce::AudioPlayHead
    {
        double bpm = 120.0, sampleRate = 48000.0;
        juce::int64 sample = 0;                 // from the clip's start; negative during the lead-in

        juce::Optional<PositionInfo> getPosition() const override
        {
            const auto seconds = (double) sample / sampleRate;
            const auto beats = seconds * bpm / 60.0;

            PositionInfo info;
            info.setBpm (bpm);
            info.setTimeSignature (juce::AudioPlayHead::TimeSignature { 4, 4 });
            info.setIsPlaying (sample >= 0);
            info.setTimeInSamples (sample);
            info.setTimeInSeconds (seconds);
            info.setPpqPosition (beats);
            info.setPpqPositionOfLastBarStart (std::floor (beats / 4.0) * 4.0);
            return info;
        }
    };

    double renderRate (const Options& options)      { return options.sampleRate > 0.0 ? options.sampleRate : 48000.0; }
    int renderBlockSize (const Options& options)    { return juce::jmax (16, options.blockSize); }

    float peakOf (const juce::AudioBuffer<float>& audio, int start, int length)
    {
        float peak = 0.0f;

        for (int ch = 0; ch < audio.getNumChannels(); ++ch)
            peak = juce::jmax (peak, audio.getMagnitude (ch, start, length));

        return peak;
    }

    /** Writes through a temporary file, so a failed export never leaves half a file behind. */
    template <typename WriteFn>
    juce::String writeVia (const juce::File& target, WriteFn&& write)
    {
        if (! target.getParentDirectory().createDirectory())
            return "Couldn't make the folder " + target.getParentDirectory().getFullPathName();

        juce::TemporaryFile temp (target);
        auto error = write (temp.getFile());

        if (error.isEmpty() && ! temp.overwriteTargetFileWithTemporary())
            error = "Couldn't write " + target.getFullPathName() + ". Is it open in another program?";

        return error;
    }

    /** Plays the export: copies of the clip and settings, the synth copy, and a progress window. */
    class ExportJob final : public juce::ThreadWithProgressWindow
    {
    public:
        ExportJob (std::unique_ptr<HostedSynth> s, MpeClip c, OutputSettings o, juce::File t, Options opts,
                   std::function<void (const juce::String&)> d)
            : ThreadWithProgressWindow ("Exporting audio", true, true, 10000, "Cancel"),
              synth (std::move (s)), clip (std::move (c)), output (o), target (std::move (t)), options (std::move (opts)),
              done (std::move (d))
        {
            prepareForRender (*synth, options);
            setStatusMessage ("Playing the clip through " + synth->getName() + "...");
        }

        void run() override
        {
            const auto audio = render (clip, output, *synth, options, [this] (double p)
            {
                setProgress (p * 0.9);
                return ! threadShouldExit();
            });

            if (threadShouldExit())
                return;

            if (audio.getNumSamples() == 0)
            {
                error = "Nothing was rendered.";
                return;
            }

            setStatusMessage ("Writing " + target.getFileName() + "...");
            setProgress (0.95);

            error = options.format == Format::mp3 ? writeMp3 (audio, options.sampleRate, target, options.mp3Kbps, options.title)
                                                  : writeWav (audio, options.sampleRate, target, options.wavBits);

            if (error.isEmpty() && peakOf (audio, 0, audio.getNumSamples()) < silence)
                error = "The file was written, but it's silent: " + synth->getName() + " made no sound while the clip played. "
                        "Check that it plays from MPE Roll's keyboard, and its MPE or MIDI channel setup.";
        }

        void threadComplete (bool userPressedCancel) override
        {
            auto callback = std::move (done);
            const auto outcome = userPressedCancel ? juce::String ("Export cancelled.") : error;

            // The window is finished with this object only once this returns.
            juce::MessageManager::callAsync ([this, callback, outcome]
            {
                delete this;

                if (callback)
                    callback (outcome);
            });
        }

    private:
        std::unique_ptr<HostedSynth> synth;
        MpeClip clip;
        OutputSettings output;
        juce::File target;
        Options options;
        std::function<void (const juce::String&)> done;
        juce::String error;
    };
}

bool canWriteMp3()
{
    return MPEROLL_MP3 != 0;
}

void prepareForRender (HostedSynth& synth, const Options& options)
{
    synth.prepare (renderRate (options), renderBlockSize (options));
    synth.setNonRealtime (true);
}

juce::AudioBuffer<float> render (const MpeClip& clip, const OutputSettings& output, HostedSynth& synth, const Options& options,
                                 const std::function<bool (double)>& progress)
{
    const auto sampleRate = renderRate (options);
    const auto blockSize = renderBlockSize (options);
    const auto beatsPerSample = juce::jmax (1.0, options.bpm) / 60.0 / sampleRate;

    synth.setOutputSettings (output);

    MpeEncoder encoder;
    encoder.setSettings (output);

    ClipPlayer player (2);
    player.setSampleRate (sampleRate);
    player.setChaseNotes (false);
    player.setControlInterval ((int) std::lround (juce::jmax (0.05, options.controlIntervalMs) * sampleRate / 1000.0));

    RenderHead head;
    head.bpm = juce::jmax (1.0, options.bpm);
    head.sampleRate = sampleRate;

    // A looping clip plays once over its length; one that doesn't loop, up to its last note.
    const auto clipBeats = clip.looping ? clip.length : juce::jmax (clip.contentEnd(), 1.0e-3);
    const auto clipSamples = (juce::int64) std::ceil (clipBeats / beatsPerSample);
    const auto maxTail = (juce::int64) std::lround (juce::jmax (0.0, options.maxTailSeconds) * sampleRate);
    const auto latency = (juce::int64) juce::jmax (0, synth.getLatencySamples());
    const auto lastSample = clipSamples + maxTail + latency;

    juce::AudioBuffer<float> block (2, blockSize);
    juce::MidiBuffer midi;

    auto play = [&] (int length)
    {
        juce::AudioBuffer<float> view (block.getArrayOfWritePointers(), 2, length);
        view.clear();
        synth.process (view, midi, &head);
        midi.clear();
    };

    // The lead-in: the synth gets the MPE setup and settles. Nothing of it is kept.
    if (output.sendConfiguration)
        encoder.writeConfiguration (midi, 0);

    for (auto pos = -(juce::int64) std::lround (leadInSeconds * sampleRate); pos < 0; pos += blockSize)
    {
        head.sample = pos;
        play ((int) juce::jmin ((juce::int64) blockSize, -pos));
    }

    juce::AudioBuffer<float> result (2, (int) juce::jmin (lastSample, clipSamples + (juce::int64) sampleRate));
    result.clear();
    juce::int64 end = 0, quietSince = -1;
    auto lastReport = (juce::int64) 0;

    for (juce::int64 pos = 0; pos < lastSample;)
    {
        const auto inClip = pos < clipSamples;
        const auto length = (int) juce::jmin ((juce::int64) blockSize, (inClip ? clipSamples : lastSample) - pos);

        if (inClip)
        {
            player.renderBlock (clip, encoder, midi, (double) pos * beatsPerSample, beatsPerSample, 0, length, false);

            if (pos + length >= clipSamples)
                player.stop (encoder, midi, length - 1);
        }

        head.sample = pos;
        juce::AudioBuffer<float> view (block.getArrayOfWritePointers(), 2, length);
        view.clear();
        synth.process (view, midi, &head);
        midi.clear();

        if (pos + length > result.getNumSamples())
            result.setSize (2, (int) juce::jmin (lastSample, (juce::int64) result.getNumSamples() * 2 + length), true, true);

        for (int ch = 0; ch < 2; ++ch)
            result.copyFrom (ch, (int) pos, view, ch, 0, length);

        pos += length;
        end = pos;

        // After the last note, the tail ends once the synth has been quiet for a moment.
        if (pos > clipSamples + latency)
        {
            if (peakOf (view, 0, length) > silence)
                quietSince = -1;
            else if (quietSince < 0)
                quietSince = pos - length;

            if (quietSince >= 0 && pos - quietSince >= (juce::int64) (quietEnough * sampleRate))
            {
                end = juce::jmax (clipSamples + latency, quietSince);
                break;
            }
        }

        if (progress && pos - lastReport >= (juce::int64) (sampleRate / 4))
        {
            lastReport = pos;

            if (! progress ((double) pos / (double) (clipSamples + juce::jmin (maxTail, (juce::int64) sampleRate))))
                return {};
        }
    }

    // The synth's latency delays everything it plays: take that much off the start.
    const auto length = (int) juce::jmax ((juce::int64) 0, end - latency);
    juce::AudioBuffer<float> trimmed (2, juce::jmax (1, length));
    trimmed.clear();

    for (int ch = 0; ch < 2; ++ch)
        if (length > 0)
            trimmed.copyFrom (ch, 0, result, ch, (int) latency, length);

    return trimmed;
}

juce::String writeWav (const juce::AudioBuffer<float>& audio, double sampleRate, const juce::File& target, int bitsPerSample)
{
    return writeVia (target, [&] (const juce::File& file) -> juce::String
    {
        std::unique_ptr<juce::OutputStream> stream (file.createOutputStream());

        if (stream == nullptr)
            return "Couldn't write " + target.getFullPathName();

        juce::WavAudioFormat wav;
        auto writer = wav.createWriterFor (stream, juce::AudioFormatWriterOptions{}.withSampleRate (sampleRate)
                                                                                 .withNumChannels (audio.getNumChannels())
                                                                                 .withBitsPerSample (bitsPerSample));

        if (writer == nullptr)
            return "Couldn't write a " + juce::String (bitsPerSample) + "-bit WAV file at " + juce::String (sampleRate) + " Hz.";

        if (! writer->writeFromAudioSampleBuffer (audio, 0, audio.getNumSamples()))
            return "Couldn't write all of " + target.getFullPathName() + ". Is the disk full?";

        return {};
    });
}

juce::String writeMp3 (const juce::AudioBuffer<float>& audio, double sampleRate, const juce::File& target, int kbps,
                       [[maybe_unused]] const juce::String& title)
{
   #if MPEROLL_MP3
    return writeVia (target, [&] (const juce::File& file) -> juce::String
    {
        auto* lame = lame_init();

        if (lame == nullptr)
            return "The MP3 encoder couldn't start.";

        const juce::ScopeGuard closeEncoder { [lame] { lame_close (lame); } };

        // MP3 goes up to 48 kHz; LAME converts higher rates down.
        const auto rate = juce::roundToInt (sampleRate);
        const auto outRate = (rate == 32000 || rate == 44100 || rate == 48000) ? rate : (rate % 44100 == 0 ? 44100 : 48000);

        lame_set_in_samplerate (lame, rate);
        lame_set_out_samplerate (lame, outRate);
        lame_set_num_channels (lame, 2);
        lame_set_mode (lame, JOINT_STEREO);
        lame_set_VBR (lame, vbr_off);
        lame_set_brate (lame, juce::jlimit (64, 320, kbps));
        lame_set_quality (lame, 2);

        if (lame_init_params (lame) < 0)
            return "The MP3 encoder didn't accept " + juce::String (rate) + " Hz audio.";

        juce::FileOutputStream out (file);

        if (! out.openedOk())
            return "Couldn't write " + target.getFullPathName();

        out.setPosition (0);
        out.truncate();

        constexpr int chunk = 8192;
        std::vector<unsigned char> encoded ((size_t) (chunk * 5 / 4 + 7200));
        const auto* left = audio.getReadPointer (0);
        const auto* right = audio.getReadPointer (juce::jmin (1, audio.getNumChannels() - 1));

        for (int pos = 0; pos < audio.getNumSamples(); pos += chunk)
        {
            const auto length = juce::jmin (chunk, audio.getNumSamples() - pos);
            const auto bytes = lame_encode_buffer_ieee_float (lame, left + pos, right + pos, length,
                                                              encoded.data(), (int) encoded.size());

            if (bytes < 0 || ! out.write (encoded.data(), (size_t) bytes))
                return "The MP3 encoder stopped part way (" + juce::String (bytes) + ").";
        }

        const auto last = lame_encode_flush (lame, encoded.data(), (int) encoded.size());

        if (last < 0 || ! out.write (encoded.data(), (size_t) last))
            return "The MP3 encoder couldn't finish the file.";

        // The first frame is kept for the tag that tells players the exact length (and the encoder's delay).
        unsigned char tag[2880];
        const auto tagSize = lame_get_lametag_frame (lame, tag, sizeof (tag));

        if (tagSize > 0 && tagSize <= sizeof (tag))
        {
            out.flush();

            if (out.setPosition (0))
                out.write (tag, tagSize);
        }

        out.flush();
        return out.getStatus().wasOk() ? juce::String() : "Couldn't write all of " + target.getFullPathName();
    });
   #else
    juce::ignoreUnused (audio, sampleRate, target, kbps);
    return "This build of MPE Roll can't write MP3 files. Export a WAV file instead.";
   #endif
}

void exportClip (MpeRollProcessor& processor, const MpeClip& clip, const juce::File& target, const Options& options,
                 std::function<void (const juce::String&)> done)
{
    auto& live = processor.getSynth();

    if (! live.isLoaded())
    {
        done ("Load a synth first: the clip is exported as the synth plays it.");
        return;
    }

    const auto state = live.saveState();

    if (state == nullptr)
    {
        done ("The synth's sound couldn't be copied for the export.");
        return;
    }

    // A second copy of the synth, with the same sound, plays the export so the one playing live
    // isn't disturbed. It loads like any synth, which can take a while for big sample libraries.
    auto copy = std::make_shared<std::unique_ptr<HostedSynth>> (std::make_unique<HostedSynth>());
    auto* synth = copy->get();
    const auto output = processor.getDocument().getSettings().output;

    synth->restoreState (*state, [copy, clip, output, target, options, done = std::move (done)] (const juce::String& error) mutable
    {
        if (error.isNotEmpty() || *copy == nullptr || ! (*copy)->isLoaded())
        {
            // This runs inside the copy's own loading, so it's let go of only afterwards.
            juce::MessageManager::callAsync ([copy] {});
            done ("A copy of the synth couldn't be loaded for the export. " + error);
            return;
        }

        auto* job = new ExportJob (std::move (*copy), std::move (clip), output, target, options, std::move (done));
        job->launchThread();
    });
}

} // namespace mperoll::AudioExport
