#include "HostedSynth.h"
#include "CrashReport.h"
#include "PluginScanner.h"

#if MPEROLL_HOST_CLAP
 #include "clap/ClapHosting.h"
#endif

namespace mperoll
{

namespace
{
    // For crash reports: which synth, in which format, from which file.
    juce::String describe (const juce::PluginDescription& d)
    {
        return d.name + " (" + d.pluginFormatName + ", " + d.fileOrIdentifier + ")";
    }

    juce::String describeFlStudioNativePlugin (const juce::File& file)
    {
        return file.getFileName() + " is one of FL Studio's own plugins. Those come in Image-Line's own format, "
               "which only FL Studio can load, so MPE Roll can't host it. To play it with what you write in MPE Roll, "
               "use Send to FL piano roll instead, which turns the clip into notes for FL's piano roll.";
    }

    /** The scan window's list. Files dropped on it are scanned the way Options > Scan does: on the
        scan's own thread, with its progress window and Cancel button. JUCE's list looks at them right
        there on the message thread, which would hold up FL's window while the scanner waits on a plugin.
    */
    class ScanList final : public juce::PluginListComponent
    {
    public:
        ScanList (juce::AudioPluginFormatManager& fm, juce::KnownPluginList& knownList, const juce::File& deadMansPedal,
                  juce::PropertiesFile* settings)
            : PluginListComponent (fm, knownList, deadMansPedal, settings, true), formats (fm)
        {
        }

    private:
        void filesDropped (const juce::StringArray& files, int, int) override
        {
            if (isScanning())
                return;

            // A scan is of one format at a time, so a drop of mixed formats scans the first one's files.
            for (auto* format : formats.getFormats())
            {
                juce::StringArray matching;

                for (const auto& file : files)
                    if (format->fileMightContainThisPluginType (file))
                        matching.add (file);

                if (! matching.isEmpty())
                {
                    scanFor (*format, matching);
                    return;
                }
            }
        }

        juce::AudioPluginFormatManager& formats;
    };
}

class HostedSynth::EditorWindow : public juce::DocumentWindow
{
public:
    EditorWindow (juce::AudioPluginInstance& p, std::function<void()> onClose)
        : DocumentWindow (p.getName(), juce::Colour (0xff1e1f22),
                          DocumentWindow::closeButton | DocumentWindow::minimiseButton),
          plugin (p),
          closed (std::move (onClose))
    {
        setUsingNativeTitleBar (true);

        std::unique_ptr<juce::AudioProcessorEditor> editor;

        if (plugin.hasEditor())
            editor.reset (plugin.createEditorAndMakeActive());

        if (editor == nullptr)
            editor = std::make_unique<juce::GenericAudioProcessorEditor> (plugin);

        setResizable (editor->isResizable(), false);
        setContentOwned (editor.release(), true);

        // Plugin windows aren't owned by the host's window, so keep the synth from hiding behind it.
        setAlwaysOnTop (true);
        centreWithSize (getWidth(), getHeight());
        setVisible (true);
    }

    ~EditorWindow() override
    {
        if (auto* editor = dynamic_cast<juce::AudioProcessorEditor*> (getContentComponent()))
            plugin.editorBeingDeleted (editor);

        clearContentComponent();
    }

    void closeButtonPressed() override
    {
        if (closed)
            closed();
    }

private:
    juce::AudioPluginInstance& plugin;
    std::function<void()> closed;
};

//==============================================================================
HostedSynth::SharedSettings::SharedSettings()
{
    juce::PropertiesFile::Options options;
    options.applicationName = "MPE Roll";
    options.folderName = "MPE Roll";
    options.filenameSuffix = "settings";
    options.osxLibrarySubFolder = "Application Support";
    options.storageFormat = juce::PropertiesFile::storeAsXML;
    open (options.getDefaultFile(), options);

    // Scans look at each plugin file in the scanner process, so one that crashes can't take FL with it.
    knownPlugins.setCustomScanner (std::make_unique<PluginScanner::OutOfProcessScanner>());
    knownPlugins.addChangeListener (this);
}

HostedSynth::SharedSettings::~SharedSettings()
{
    knownPlugins.removeChangeListener (this);

    // The list only announces a change with a message, which never arrives when FL deletes the last
    // MPE Roll in the middle of a scan: write what it holds now, for the settings file to save as it
    // goes. Nothing is written if the list hasn't changed.
    if (auto xml = knownPlugins.createXml())
        settings->setValue ("knownPlugins", xml.get());
}

void HostedSynth::SharedSettings::useSettingsFile (const juce::File& file)
{
    juce::PropertiesFile::Options options;
    options.storageFormat = juce::PropertiesFile::storeAsXML;
    open (file, options);
}

void HostedSynth::SharedSettings::open (const juce::File& file, const juce::PropertiesFile::Options& options)
{
    if (settings != nullptr)
        settings->saveIfNeeded();

    settings = std::make_unique<juce::PropertiesFile> (file, options);

    if (auto xml = settings->getXmlValue ("knownPlugins"))
        knownPlugins.recreateFromXml (*xml);
    else
        knownPlugins.clear();
}

void HostedSynth::SharedSettings::changeListenerCallback (juce::ChangeBroadcaster*)
{
    if (auto xml = knownPlugins.createXml())
    {
        settings->setValue ("knownPlugins", xml.get());
        settings->saveIfNeeded();
    }
}

//==============================================================================
HostedSynth::HostedSynth()
    : knownPlugins (shared->knownPlugins)
{
    juce::addDefaultFormatsToManager (formatManager);

   #if MPEROLL_HOST_CLAP
    formatManager.addFormat (std::make_unique<ClapPluginFormat>());
   #endif
}

HostedSynth::~HostedSynth()
{
    ++loadGeneration;

    // First of all: the scan window shows this list, saves into these settings and scans with this
    // scanner, so it mustn't outlive them (FL deletes MPE Roll with the window open when the channel
    // goes or another project is opened). Deleting it ends a scan that's running.
    scanWindow.deleteAndZero();
    stopCheck();
    closeEditor();

    if (instance != nullptr)
        CrashReport::setActivity ("closing the synth " + getName());

    takeInstance().reset();
}

juce::File HostedSynth::getDeadMansPedalFile()
{
    if (auto* settings = getUserSettings())
        return settings->getFile().getSiblingFile ("RecentlyCrashedPluginsList");

    return juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("MPE Roll crashed plugins");
}

void HostedSynth::useSettingsFile (const juce::File& file)
{
    shared->useSettingsFile (file);
}

juce::Array<juce::PluginDescription> HostedSynth::getInstrumentTypes() const
{
    juce::Array<juce::PluginDescription> instruments;

    for (const auto& type : knownPlugins.getTypes())
        if (type.isInstrument)
            instruments.add (type);

    return instruments.isEmpty() ? knownPlugins.getTypes() : instruments;
}

//==============================================================================
void HostedSynth::load (const juce::PluginDescription& desc, LoadCallback onDone)
{
    stopCheck();
    const auto generation = ++loadGeneration;
    loading = true;
    CrashReport::setActivity ("loading the synth " + describe (desc));

    formatManager.createPluginInstanceAsync (desc, sampleRate, blockSize,
        [this, generation, desc, onDone, alive = std::weak_ptr<int> (lifetime)]
        (std::unique_ptr<juce::AudioPluginInstance> plugin, const juce::String& error)
        {
            if (alive.expired() || generation != loadGeneration)
                return;

            loading = false;

            if (plugin == nullptr)
            {
                CrashReport::setActivity ("failed to load the synth " + describe (desc));

                if (onDone)
                    onDone (error.isNotEmpty() ? error : juce::String ("The plugin couldn't be loaded."));

                return;
            }

            CrashReport::setActivity ("setting up the synth " + describe (desc));
            install (std::move (plugin), desc);
            CrashReport::renew();
            CrashReport::setActivity ("playing the synth " + describe (desc));

            if (onDone)
                onDone ({});
        });
}

void HostedSynth::loadFromFile (const juce::File& file, LoadCallback onDone, std::function<void()> onChecked)
{
    stopCheck();
    const auto generation = ++loadGeneration;
    const auto path = file.getFullPathName();
    CrashReport::setActivity ("reading the plugin file " + path);

    juce::StringArray formatNames;

    for (auto* format : formatManager.getFormats())
        if (format->fileMightContainThisPluginType (path))
            formatNames.add (format->getName());

    // Without the scanner, the file is looked at here, as before.
    if (formatNames.isEmpty() || PluginScanner::findExecutable() == juce::File())
    {
        juce::OwnedArray<juce::PluginDescription> found;
        finishLoadFromFile (file, PluginScanner::Outcome::unavailable, found, std::move (onDone), onChecked);
        return;
    }

    // Look inside it in the scanner process first: a plugin that crashes there isn't loaded here.
    // On a thread of its own, since a plugin that hangs or shows a dialog there would otherwise
    // hold up FL's window for the length of the timeout. The outcome comes back to the message
    // thread, where the loading itself happens; a load started meanwhile makes it moot.
    loading = true;
    checkCancelled = std::make_shared<std::atomic<bool>> (false);

    checkThread = std::thread ([this, file, path, formatNames, generation, onDone, onChecked,
                                cancelled = checkCancelled, alive = std::weak_ptr<int> (lifetime)]
    {
        auto found = std::make_shared<juce::OwnedArray<juce::PluginDescription>>();
        auto outcome = PluginScanner::Outcome::found;

        for (const auto& formatName : formatNames)
        {
            outcome = PluginScanner::findTypes (formatName, path, *found, PluginScanner::defaultTimeoutMs,
                                                [&cancelled] { return cancelled->load(); });

            if (outcome != PluginScanner::Outcome::found)
                break;
        }

        juce::MessageManager::callAsync ([this, file, found, outcome, generation, onDone, onChecked, alive]
        {
            if (alive.expired() || generation != loadGeneration)
                return;

            finishLoadFromFile (file, outcome, *found, onDone, onChecked);
        });
    });
}

void HostedSynth::finishLoadFromFile (const juce::File& file, PluginScanner::Outcome outcome,
                                      juce::OwnedArray<juce::PluginDescription>& found, LoadCallback onDone,
                                      const std::function<void()>& onChecked)
{
    loading = false;

    if (outcome == PluginScanner::Outcome::unavailable)
    {
        // No scanner, or one that went down before it got to the file: look inside it here.
        for (auto* format : formatManager.getFormats())
            if (format->fileMightContainThisPluginType (file.getFullPathName()))
                format->findAllTypesForFile (found, file.getFullPathName());
    }
    else if (outcome != PluginScanner::Outcome::found)
    {
        CrashReport::setActivity ("refused to load " + file.getFullPathName());

        if (onDone)
            onDone (PluginScanner::describeFailure (outcome, file.getFileName()));

        return;
    }

    if (found.isEmpty())
    {
        // FL's own plugins have no VST entry point, so a scan finds nothing in them.
        if (onDone)
            onDone (isFlStudioNativePlugin (file) ? describeFlStudioNativePlugin (file)
                                                  : "No plugin was found in " + file.getFileName() + ".");

        return;
    }

    auto* chosen = found.getFirst();

    for (auto* d : found)
    {
        if (d->isInstrument)
        {
            chosen = d;
            break;
        }
    }

    // A file can hold several plugins (a CLAP bundle, a VST2 shell): list them all in the synth
    // menu, so the others can be picked from there.
    for (auto* d : found)
        knownPlugins.addType (*d);

    if (onChecked)
        onChecked();

    load (*chosen, std::move (onDone));
}

void HostedSynth::stopCheck()
{
    if (checkCancelled != nullptr)
        *checkCancelled = true;

    // Quick: the check notices the flag within moments and stops its scanner.
    if (checkThread.joinable())
        checkThread.join();

    checkCancelled.reset();
}

bool HostedSynth::isFlStudioNativePlugin (const juce::File& file)
{
    return PluginScanner::isFlStudioNativePluginFile (file.getFullPathName());
}

void HostedSynth::showScanWindow (juce::Colour background)
{
    if (scanWindow != nullptr)
    {
        scanWindow->toFront (true);
        return;
    }

    juce::DialogWindow::LaunchOptions options;
    options.content.setOwned (new ScanList (formatManager, knownPlugins, getDeadMansPedalFile(), getUserSettings()));
    options.content->setSize (760, 520);
    options.dialogTitle = "MPE Roll: scan for plugins";
    options.dialogBackgroundColour = background;
    options.useNativeTitleBar = true;
    options.resizable = true;
    scanWindow = options.launchAsync();
}

juce::String HostedSynth::getPluginFilePatterns()
{
    juce::StringArray patterns { "*.vst3" };

   #if JUCE_PLUGINHOST_AU && JUCE_MAC
    patterns.add ("*.component");
   #endif

   #if JUCE_PLUGINHOST_VST
    #if JUCE_WINDOWS
     patterns.add ("*.dll");
    #elif JUCE_MAC
     patterns.add ("*.vst");
    #else
     patterns.add ("*.so");
    #endif
   #endif

   #if MPEROLL_HOST_CLAP
    patterns.add ("*.clap");
   #endif

    return patterns.joinIntoString (";");
}

bool HostedSynth::isSameSynth (const juce::PluginDescription& a, const juce::PluginDescription& b)
{
    if (a.pluginFormatName != b.pluginFormatName)
        return false;

    // The ID, not the file: the same synth installed somewhere else is still the same synth.
    if (a.uniqueId != 0 || b.uniqueId != 0)
        return a.uniqueId == b.uniqueId;

    if (a.deprecatedUid != 0 || b.deprecatedUid != 0)
        return a.deprecatedUid == b.deprecatedUid;

    return a.isDuplicateOf (b);
}

juce::String HostedSynth::describeHostedFormats()
{
    juce::StringArray formats { "VST3" };

   #if JUCE_PLUGINHOST_VST
    formats.add ("VST2");
   #endif

   #if MPEROLL_HOST_CLAP
    formats.add ("CLAP");
   #endif

   #if JUCE_PLUGINHOST_AU && JUCE_MAC
    formats.add ("AU");
   #endif

    if (formats.size() == 1)
        return formats[0];

    const auto last = formats[formats.size() - 1];
    formats.remove (formats.size() - 1);
    return formats.joinIntoString (", ") + " or " + last;
}

void HostedSynth::configure (juce::AudioPluginInstance& plugin)
{
    plugin.disableNonMainBuses();

    auto layout = plugin.getBusesLayout();

    if (! layout.outputBuses.isEmpty() && layout.outputBuses[0] != juce::AudioChannelSet::stereo())
    {
        auto stereo = layout;
        stereo.outputBuses.getReference (0) = juce::AudioChannelSet::stereo();
        plugin.setBusesLayout (stereo);
    }

    plugin.setRateAndBufferSizeDetails (sampleRate, blockSize);

    if (prepared)
        plugin.prepareToPlay (sampleRate, blockSize);
}

void HostedSynth::install (std::unique_ptr<juce::AudioPluginInstance> newInstance, const juce::PluginDescription& desc)
{
    closeEditor();
    configure (*newInstance);

    const auto channels = juce::jmax (2, newInstance->getTotalNumInputChannels(), newInstance->getTotalNumOutputChannels());
    auto old = takeInstance();

    {
        const ScopedExclusive exclusive (guard);
        scratch.setSize (juce::jmax (channels, scratch.getNumChannels()), juce::jmax (blockSize, scratch.getNumSamples()));
        instance = std::move (newInstance);
        silenceWatch = {};
        silenceWarning = false;
        silentOverMpe = false;

       #if MPEROLL_HOST_CLAP
        clapControls = dynamic_cast<ClapSynthControls*> (instance.get());
       #endif
    }

    instance->addListener (this);
    const auto oldName = description != nullptr ? describe (*description) : juce::String();
    description = std::make_unique<juce::PluginDescription> (desc);
    unresolvedState.reset();

    if (old != nullptr)
    {
        CrashReport::setActivity ("closing the synth " + oldName + " to make way for " + describe (desc));
        old->releaseResources();
        old.reset();
        CrashReport::setActivity ("setting up the synth " + describe (desc));
    }

    if (onPluginChanged)
        onPluginChanged();
}

void HostedSynth::unload()
{
    stopCheck();
    ++loadGeneration;
    loading = false;
    closeEditor();

    auto old = takeInstance();

    if (description != nullptr)
        CrashReport::setActivity ("closing the synth " + describe (*description));

    description.reset();
    unresolvedState.reset();

    if (old != nullptr)
        old->releaseResources();

    old.reset();
    CrashReport::setActivity ("no synth loaded");

    if (onPluginChanged)
        onPluginChanged();
}

std::unique_ptr<juce::AudioPluginInstance> HostedSynth::takeInstance()
{
    if (instance == nullptr)
        return {};

    if (onPluginRemoving)
        onPluginRemoving (*instance);

    instance->removeListener (this);
    std::unique_ptr<juce::AudioPluginInstance> old;

    {
        const ScopedExclusive exclusive (guard);
        old = std::move (instance);
        clapControls = nullptr;
    }

    return old;
}

void HostedSynth::audioProcessorChanged (juce::AudioProcessor*, const ChangeDetails& details)
{
    // Plugins report these from any thread.
    juce::MessageManager::callAsync ([this, details, alive = std::weak_ptr<int> (lifetime)]
    {
        if (! alive.expired() && onSynthReportedChange)
            onSynthReportedChange (details);
    });
}

bool HostedSynth::isLoaded() const
{
    return instance != nullptr;
}

juce::String HostedSynth::getName() const
{
    return instance != nullptr ? instance->getName() : juce::String();
}

juce::String HostedSynth::getMissingSynthName() const
{
    if (instance != nullptr || unresolvedState == nullptr)
        return {};

    if (const auto* descXml = unresolvedState->getChildByName ("PLUGIN"))
        return descXml->getStringAttribute ("name");

    return {};
}

void HostedSynth::showEditor()
{
    if (instance == nullptr)
        return;

    if (editorWindow != nullptr)
    {
        editorWindow->toFront (true);
        return;
    }

    CrashReport::setActivity ("opening the window of the synth " + getName());

    editorWindow = std::make_unique<EditorWindow> (*instance, [this, alive = std::weak_ptr<int> (lifetime)]
    {
        // Deleting the window from inside its own callback isn't safe, so do it right after.
        juce::MessageManager::callAsync ([this, alive]
        {
            if (! alive.expired())
                closeEditor();
        });
    });

    CrashReport::setActivity ("playing the synth " + getName() + ", with its window open");
}

void HostedSynth::closeEditor()
{
    if (editorWindow == nullptr)
        return;

    CrashReport::setActivity ("closing the window of the synth " + getName());
    editorWindow.reset();
    CrashReport::setActivity ("playing the synth " + getName());
}

//==============================================================================
void HostedSynth::prepare (double newSampleRate, int maximumBlockSize)
{
    const MessageThreadLock messageThread;
    const ScopedExclusive exclusive (guard);

    const auto wasPrepared = std::exchange (prepared, true);
    sampleRate = newSampleRate;
    blockSize = juce::jmax (1, maximumBlockSize);
    silenceWatch.heldNotes = 0;     // MPE Roll stops its notes when it's prepared

    int channels = 2;

    if (instance != nullptr)
    {
        // A synth that's running is stopped before it hears the new rate or block size: JUCE's
        // VST3 host skips prepareToPlay for an active plugin whose rate and size look unchanged,
        // and VST2 synths may only be given them while suspended.
        if (wasPrepared && (! juce::approximatelyEqual (instance->getSampleRate(), sampleRate)
                              || instance->getBlockSize() != blockSize))
            instance->releaseResources();

        instance->prepareToPlay (sampleRate, blockSize);
        channels = juce::jmax (channels, instance->getTotalNumInputChannels(), instance->getTotalNumOutputChannels());
    }

    scratch.setSize (juce::jmax (channels, 8), blockSize);
    midiPiece.ensureSize (1 << 14);
}

void HostedSynth::release()
{
    const MessageThreadLock messageThread;
    const ScopedExclusive exclusive (guard);
    prepared = false;

    if (instance != nullptr)
        instance->releaseResources();
}

void HostedSynth::process (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi, juce::AudioPlayHead* playHead)
{
    const auto numSamples = buffer.getNumSamples();
    const ScopedUse use (guard);

    if (! use.entered || instance == nullptr || ! prepared)
    {
        buffer.clear();
        return;
    }

    auto& plugin = *instance;
    const auto outputs = plugin.getTotalNumOutputChannels();
    const auto needed = juce::jmax (plugin.getTotalNumInputChannels(), outputs);
    const auto pieceSize = scratch.getNumSamples();

    if (needed > scratch.getNumChannels() || pieceSize <= 0 || plugin.isSuspended())
    {
        buffer.clear();
        return;
    }

   #if MPEROLL_HOST_CLAP
    if (clapControls != nullptr)
        clapControls->setMpeOutputSettings (outputSettings);
   #endif

    countHeldNotes (midi);

    // Hosts may send a longer block than prepare() said. The synth gets it in pieces it was
    // prepared for, so the block (and its note-offs) isn't lost. Each piece's transport position
    // is moved on to where that piece starts.
    for (int start = 0; start < numSamples; start += pieceSize)
    {
        const auto length = juce::jmin (pieceSize, numSamples - start);
        auto* pieceMidi = &midi;

        if (length < numSamples)
        {
            midiPiece.clear();
            midiPiece.addEvents (midi, start, length, -start);
            pieceMidi = &midiPiece;
        }

        pieceHead.source = playHead;
        pieceHead.offset = start;
        pieceHead.sampleRate = sampleRate;
        plugin.setPlayHead (start == 0 || playHead == nullptr ? playHead : &pieceHead);

        juce::AudioBuffer<float> view (scratch.getArrayOfWritePointers(), needed, length);
        view.clear();
        plugin.processBlock (view, *pieceMidi);

        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        {
            if (outputs > 0)
                buffer.copyFrom (ch, start, view, juce::jmin (ch, outputs - 1), 0, length);
            else
                buffer.clear (ch, start, length);
        }
    }

    plugin.setPlayHead (playHead);
    listenForSound (buffer);
}

void HostedSynth::countHeldNotes (const juce::MidiBuffer& midi) noexcept
{
    // Counted even once the watch is finished: a project's state starts it again with the notes
    // held at that moment.
    for (const auto metadata : midi)
    {
        const auto msg = metadata.getMessage();

        if (msg.isNoteOn())
            ++silenceWatch.heldNotes;
        else if (msg.isNoteOff())
            silenceWatch.heldNotes = juce::jmax (0, silenceWatch.heldNotes - 1);
        else if (msg.isAllNotesOff() || msg.isAllSoundOff())
            silenceWatch.heldNotes = 0;
    }
}

void HostedSynth::listenForSound (const juce::AudioBuffer<float>& output) noexcept
{
    constexpr float silent = 1.0e-3f;               // -60 dB: a noise floor or hum isn't the synth sounding
    constexpr double secondsBeforeWarning = 3.0;    // of notes held in all, so a slow attack isn't taken for silence
    constexpr double graceAfterLoad = 10.0;         // seconds after it got its state: a sample library may be loading

    auto& watch = silenceWatch;
    const auto seconds = output.getNumSamples() / sampleRate;

    // A sound settles it, and withdraws a warning that hasn't been shown yet: the user may have
    // turned on MPE in the synth's own window while MPE Roll's was closed.
    if (output.getMagnitude (0, output.getNumSamples()) > silent)
    {
        watch.finished = true;
        silenceWarning = false;
        silentOverMpe = false;
        return;
    }

    if (watch.finished)
        return;

    if (watch.graceSeconds < graceAfterLoad)
    {
        watch.graceSeconds += seconds;
        return;
    }

    // Only MPE over MIDI spreads notes over channels a synth may not be listening to.
    auto overMpe = outputSettings.mode == OutputSettings::Mode::mpe;

   #if MPEROLL_HOST_CLAP
    if (clapControls != nullptr && clapControls->playsNoteExpressions (outputSettings))
        overMpe = false;
   #endif

    if (! overMpe)
    {
        watch.silentSeconds = 0.0;
        return;
    }

    // Silence with no note held says nothing. Short notes add up: a synth that hears none of them
    // is as silent through ten short notes as through one long one.
    if (watch.heldNotes == 0)
        return;

    watch.silentSeconds += seconds;

    if (watch.silentSeconds >= secondsBeforeWarning)
    {
        watch.finished = true;
        silenceWarning = true;
        silentOverMpe = true;
    }
}

juce::Optional<juce::AudioPlayHead::PositionInfo> HostedSynth::PieceHead::getPosition() const
{
    if (source == nullptr)
        return {};

    auto position = source->getPosition();

    if (! position.hasValue() || offset == 0 || sampleRate <= 0.0 || ! position->getIsPlaying())
        return position;

    const auto seconds = offset / sampleRate;

    if (const auto samples = position->getTimeInSamples())
        position->setTimeInSamples (*samples + offset);

    if (const auto time = position->getTimeInSeconds())
        position->setTimeInSeconds (*time + seconds);

    if (const auto ppq = position->getPpqPosition())
        if (const auto bpm = position->getBpm())
            position->setPpqPosition (*ppq + seconds * *bpm / 60.0);

    return position;
}

juce::String HostedSynth::describeConnection (const OutputSettings& settings) const
{
    if (instance == nullptr)
        return "No synth loaded.";

    if (playsNoteExpressions (settings))
        return "CLAP notes, each with its own tuning, pressure, brightness, volume, pan, vibrato and expression.";

    const auto via = clapControls != nullptr ? juce::String (" (the CLAP synth accepts MIDI)") : juce::String();

    if (settings.mode == OutputSettings::Mode::singleChannel)
        return "Plain MIDI on channel " + juce::String (settings.singleChannel) + via + ".";

    // A synth that isn't in MPE mode often listens on channel 1 only, and hears nothing.
    const auto channels = settings.zones.describeMemberChannels();

    if (channels.isEmpty())
        return "MPE over MIDI" + via + ".";

    return "MPE over MIDI" + via + ", notes on channels " + channels + ". Turn on MPE in the synth, "
           "or it may not hear them.";
}

bool HostedSynth::playsNoteExpressions (const OutputSettings& settings) const
{
   #if MPEROLL_HOST_CLAP
    return clapControls != nullptr && clapControls->playsNoteExpressions (settings);
   #else
    juce::ignoreUnused (settings);
    return false;
   #endif
}

int HostedSynth::getLatencySamples() const
{
    const ScopedUse use (guard);
    return use.entered && instance != nullptr ? instance->getLatencySamples() : 0;
}

double HostedSynth::getTailLengthSeconds() const
{
    // Hosts ask for this from any thread.
    const ScopedUse use (guard);
    return use.entered && instance != nullptr ? instance->getTailLengthSeconds() : 0.0;
}

void HostedSynth::setNonRealtime (bool isNonRealtime)
{
    const ScopedExclusive exclusive (guard);

    if (instance != nullptr)
        instance->setNonRealtime (isNonRealtime);
}

//==============================================================================
std::unique_ptr<juce::XmlElement> HostedSynth::saveState() const
{
    if (instance == nullptr || description == nullptr)
        return unresolvedState != nullptr ? std::make_unique<juce::XmlElement> (*unresolvedState) : nullptr;

    auto xml = std::make_unique<juce::XmlElement> ("HostedSynth");
    xml->addChildElement (description->createXml().release());

    juce::MemoryBlock state;
    instance->getStateInformation (state);
    xml->createNewChildElement ("State")->addTextElement (state.toBase64Encoding());
    return xml;
}

void HostedSynth::restoreState (const juce::XmlElement& xml, LoadCallback onDone)
{
    juce::PluginDescription desc;
    const auto* descXml = xml.getChildByName ("PLUGIN");

    if (descXml == nullptr || ! desc.loadFromXml (*descXml))
    {
        if (onDone)
            onDone ("The saved synth couldn't be read.");

        return;
    }

    juce::MemoryBlock state;

    if (const auto* stateXml = xml.getChildByName ("State"))
        state.fromBase64Encoding (stateXml->getAllSubText());

    // The same synth is already here (wherever it was installed when saved): just give it the state.
    if (instance != nullptr && description != nullptr && isSameSynth (*description, desc))
    {
        stopCheck();        // a Load plugin file... check still running would otherwise keep its scanner going
        ++loadGeneration;   // a load still in flight would otherwise replace it
        loading = false;
        unresolvedState.reset();

        if (state.getSize() > 0)
        {
            instance->setStateInformation (state.getData(), (int) state.getSize());

            // New state can mean new sounds to load: listen afresh, with the grace a new synth gets.
            const ScopedExclusive exclusive (guard);
            silenceWatch.graceSeconds = 0.0;
            silenceWatch.silentSeconds = 0.0;
            silenceWatch.finished = false;
            silenceWarning = false;
            silentOverMpe = false;
        }

        if (onPluginChanged)
            onPluginChanged();

        if (onDone)
            onDone ({});

        return;
    }

    // A different synth: let the current one go now. If the new one fails to load, the project
    // then shows it as missing (and keeps it when saved) instead of quietly keeping the old synth.
    unload();
    unresolvedState = std::make_unique<juce::XmlElement> (xml);

    // A project from another computer may name a path that doesn't exist here: use the same synth
    // where it's installed on this one, if a scan found it.
    if (! formatManager.doesPluginStillExist (desc))
    {
        for (const auto& known : knownPlugins.getTypes())
        {
            if (isSameSynth (known, desc) && formatManager.doesPluginStillExist (known))
            {
                desc = known;
                break;
            }
        }
    }

    load (desc, [this, state, desc, onDone] (const juce::String& error)
    {
        if (error.isEmpty() && instance != nullptr && state.getSize() > 0)
            instance->setStateInformation (state.getData(), (int) state.getSize());

        if (onDone)
            onDone (error.isEmpty() ? error : desc.name + ": " + error);
    });
}

} // namespace mperoll
