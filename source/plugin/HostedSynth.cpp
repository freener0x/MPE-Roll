#include "HostedSynth.h"

#if MPEROLL_HOST_CLAP
 #include "clap/ClapHosting.h"
#endif

namespace mperoll
{

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
HostedSynth::HostedSynth()
{
    juce::PropertiesFile::Options options;
    options.applicationName = "MPE Roll";
    options.folderName = "MPE Roll";
    options.filenameSuffix = "settings";
    options.osxLibrarySubFolder = "Application Support";
    options.storageFormat = juce::PropertiesFile::storeAsXML;
    appProperties.setStorageParameters (options);

    juce::addDefaultFormatsToManager (formatManager);

   #if MPEROLL_HOST_CLAP
    formatManager.addFormat (std::make_unique<ClapPluginFormat>());
   #endif

    if (auto* settings = getUserSettings())
        if (auto xml = settings->getXmlValue ("knownPlugins"))
            knownPlugins.recreateFromXml (*xml);

    knownPlugins.addChangeListener (this);
}

HostedSynth::~HostedSynth()
{
    ++loadGeneration;
    closeEditor();
    knownPlugins.removeChangeListener (this);
    takeInstance().reset();
}

juce::File HostedSynth::getDeadMansPedalFile()
{
    if (auto* settings = getUserSettings())
        return settings->getFile().getSiblingFile ("RecentlyCrashedPluginsList");

    return juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("MPE Roll crashed plugins");
}

void HostedSynth::changeListenerCallback (juce::ChangeBroadcaster*)
{
    if (auto* settings = getUserSettings())
    {
        if (auto xml = knownPlugins.createXml())
        {
            settings->setValue ("knownPlugins", xml.get());
            settings->saveIfNeeded();
        }
    }
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
    const auto generation = ++loadGeneration;
    loading = true;

    formatManager.createPluginInstanceAsync (desc, sampleRate, blockSize,
        [this, generation, desc, onDone, alive = std::weak_ptr<int> (lifetime)]
        (std::unique_ptr<juce::AudioPluginInstance> plugin, const juce::String& error)
        {
            if (alive.expired() || generation != loadGeneration)
                return;

            loading = false;

            if (plugin == nullptr)
            {
                if (onDone)
                    onDone (error.isNotEmpty() ? error : juce::String ("The plugin couldn't be loaded."));

                return;
            }

            install (std::move (plugin), desc);

            if (onDone)
                onDone ({});
        });
}

void HostedSynth::loadFromFile (const juce::File& file, LoadCallback onDone)
{
    juce::OwnedArray<juce::PluginDescription> found;

    for (auto* format : formatManager.getFormats())
        if (format->fileMightContainThisPluginType (file.getFullPathName()))
            format->findAllTypesForFile (found, file.getFullPathName());

    if (found.isEmpty())
    {
        if (onDone)
            onDone ("No plugin was found in " + file.getFileName() + ".");

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

    load (*chosen, std::move (onDone));
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

       #if MPEROLL_HOST_CLAP
        clapControls = dynamic_cast<ClapSynthControls*> (instance.get());
       #endif
    }

    instance->addListener (this);
    description = std::make_unique<juce::PluginDescription> (desc);
    unresolvedState.reset();

    if (old != nullptr)
        old->releaseResources();

    old.reset();

    if (onPluginChanged)
        onPluginChanged();
}

void HostedSynth::unload()
{
    ++loadGeneration;
    loading = false;
    closeEditor();

    auto old = takeInstance();
    description.reset();
    unresolvedState.reset();

    if (old != nullptr)
        old->releaseResources();

    old.reset();

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

    editorWindow = std::make_unique<EditorWindow> (*instance, [this, alive = std::weak_ptr<int> (lifetime)]
    {
        // Deleting the window from inside its own callback isn't safe, so do it right after.
        juce::MessageManager::callAsync ([this, alive]
        {
            if (! alive.expired())
                closeEditor();
        });
    });
}

void HostedSynth::closeEditor()
{
    editorWindow.reset();
}

//==============================================================================
void HostedSynth::prepare (double newSampleRate, int maximumBlockSize)
{
    const MessageThreadLock messageThread;
    const ScopedExclusive exclusive (guard);

    sampleRate = newSampleRate;
    blockSize = juce::jmax (1, maximumBlockSize);
    prepared = true;

    int channels = 2;

    if (instance != nullptr)
    {
        instance->setRateAndBufferSizeDetails (sampleRate, blockSize);
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

   #if MPEROLL_HOST_CLAP
    if (clapControls != nullptr && clapControls->playsNoteExpressions (settings))
        return "CLAP notes, each with its own tuning, pressure, brightness, volume, pan, vibrato and expression.";
   #endif

    const auto midi = settings.mode == OutputSettings::Mode::singleChannel ? juce::String ("Plain MIDI")
                                                                           : juce::String ("MPE over MIDI");
    return clapControls != nullptr ? midi + " (the CLAP synth accepts MIDI)." : midi + ".";
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
        ++loadGeneration;   // a load still in flight would otherwise replace it
        loading = false;
        unresolvedState.reset();

        if (state.getSize() > 0)
            instance->setStateInformation (state.getData(), (int) state.getSize());

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
