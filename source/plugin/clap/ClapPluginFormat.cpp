#include "ClapHosting.h"
#include "ClapModule.h"

#include <map>
#include <mutex>

namespace mperoll
{

namespace
{
    // Deliberately never destroyed, so modules released late in shutdown still find them.
    std::mutex& moduleMutex()
    {
        static auto* mutex = new std::mutex();
        return *mutex;
    }

    struct ModuleUse
    {
        ClapModule* module = nullptr;
        int users = 0;
    };

    std::map<juce::String, ModuleUse>& moduleTable()
    {
        static auto* table = new std::map<juce::String, ModuleUse>();
        return *table;
    }

    juce::File binaryInside (const juce::File& file)
    {
       #if JUCE_MAC
        // On macOS a .clap is a bundle; the code is in Contents/MacOS.
        if (file.isDirectory())
        {
            const auto macOS = file.getChildFile ("Contents").getChildFile ("MacOS");
            const auto named = macOS.getChildFile (file.getFileNameWithoutExtension());

            if (named.existsAsFile())
                return named;

            for (const auto& entry : juce::RangedDirectoryIterator (macOS, false, "*", juce::File::findFiles))
                if (! entry.isHidden())
                    return entry.getFile();
        }
       #endif

        return file;
    }

    juce::String fromClapString (const char* text)
    {
        return text != nullptr ? juce::String::fromUTF8 (text) : juce::String();
    }
}

//==============================================================================
std::shared_ptr<ClapModule> ClapModule::load (const juce::File& file, juce::String& error)
{
    const std::lock_guard lock (moduleMutex());
    auto& table = moduleTable();
    const auto key = file.getFullPathName();
    ClapModule* module = nullptr;

    if (auto found = table.find (key); found != table.end())
    {
        module = found->second.module;
        ++found->second.users;
    }
    else
    {
        std::unique_ptr<ClapModule> created (new ClapModule (file));

        if (! created->open (error))
            return {};

        module = created.release();
        table[key] = { module, 1 };
    }

    return std::shared_ptr<ClapModule> (module, [key] (ClapModule* m) { release (key, m); });
}

void ClapModule::release (const juce::String& key, ClapModule* module)
{
    const std::lock_guard lock (moduleMutex());
    auto& table = moduleTable();

    if (auto found = table.find (key); found != table.end() && --found->second.users <= 0)
    {
        table.erase (found);
        delete module;      // deinit() runs under the lock too, so it never overlaps an init()
    }
}

bool ClapModule::open (juce::String& error)
{
    if (! library.open (binaryInside (file).getFullPathName()))
    {
        error = file.getFileName() + " couldn't be loaded.";
        return false;
    }

    entry = static_cast<const clap_plugin_entry_t*> (library.getFunction ("clap_entry"));

    if (entry == nullptr || entry->init == nullptr || entry->get_factory == nullptr)
    {
        error = file.getFileName() + " isn't a CLAP plugin.";
        return false;
    }

    if (! clap_version_is_compatible (entry->clap_version))
    {
        error = file.getFileName() + " needs a newer version of CLAP.";
        return false;
    }

    if (! entry->init (file.getFullPathName().toRawUTF8()))
    {
        error = file.getFileName() + " failed to start.";
        return false;
    }

    initialised = true;
    factory = static_cast<const clap_plugin_factory_t*> (entry->get_factory (CLAP_PLUGIN_FACTORY_ID));

    if (factory == nullptr || factory->get_plugin_count == nullptr || factory->get_plugin_descriptor == nullptr
        || factory->create_plugin == nullptr)
    {
        factory = nullptr;
        error = file.getFileName() + " doesn't offer any plugins.";
        return false;
    }

    return true;
}

ClapModule::~ClapModule()
{
    if (initialised && entry->deinit != nullptr)
        entry->deinit();

    library.close();
}

int ClapModule::getNumPlugins() const
{
    return factory != nullptr ? (int) factory->get_plugin_count (factory) : 0;
}

const clap_plugin_descriptor_t* ClapModule::getDescriptor (int index) const
{
    if (! juce::isPositiveAndBelow (index, getNumPlugins()))
        return nullptr;

    const auto* d = factory->get_plugin_descriptor (factory, (uint32_t) index);

    if (d == nullptr || d->id == nullptr || d->name == nullptr || ! clap_version_is_compatible (d->clap_version))
        return nullptr;

    return d;
}

int ClapModule::uniqueIdFor (const char* clapPluginId)
{
    return juce::String::fromUTF8 (clapPluginId).hashCode();
}

const clap_plugin_descriptor_t* ClapModule::findDescriptor (const juce::PluginDescription& description) const
{
    for (int i = 0; i < getNumPlugins(); ++i)
        if (auto* d = getDescriptor (i))
            if (uniqueIdFor (d->id) == description.uniqueId)
                return d;

    return nullptr;
}

juce::PluginDescription ClapModule::describe (const clap_plugin_descriptor_t& d) const
{
    juce::PluginDescription result;
    result.name = fromClapString (d.name);
    result.descriptiveName = fromClapString (d.description).isNotEmpty() ? fromClapString (d.description) : result.name;
    result.pluginFormatName = ClapPluginFormat::formatName;
    result.manufacturerName = fromClapString (d.vendor);
    result.version = fromClapString (d.version);
    result.fileOrIdentifier = file.getFullPathName();
    result.lastFileModTime = file.getLastModificationTime();
    result.lastInfoUpdateTime = juce::Time::getCurrentTime();
    result.uniqueId = result.deprecatedUid = uniqueIdFor (d.id);
    result.hasSharedContainer = getNumPlugins() > 1;
    result.numInputChannels = 0;
    result.numOutputChannels = 2;

    juce::StringArray features;

    for (auto* f = d.features; f != nullptr && *f != nullptr; ++f)
        features.add (fromClapString (*f));

    result.isInstrument = features.contains (CLAP_PLUGIN_FEATURE_INSTRUMENT);

    if (features.contains (CLAP_PLUGIN_FEATURE_SYNTHESIZER))        result.category = "Synth";
    else if (features.contains (CLAP_PLUGIN_FEATURE_SAMPLER))       result.category = "Sampler";
    else if (features.contains (CLAP_PLUGIN_FEATURE_DRUM_MACHINE)
             || features.contains (CLAP_PLUGIN_FEATURE_DRUM))       result.category = "Drums";
    else if (result.isInstrument)                                   result.category = "Instrument";
    else if (features.contains (CLAP_PLUGIN_FEATURE_NOTE_EFFECT))   result.category = "Note effect";
    else                                                            result.category = "Effect";

    return result;
}

//==============================================================================
void ClapPluginFormat::findAllTypesForFile (juce::OwnedArray<juce::PluginDescription>& results,
                                            const juce::String& fileOrIdentifier)
{
    if (! fileMightContainThisPluginType (fileOrIdentifier))
        return;

    juce::String error;

    if (auto module = ClapModule::load (juce::File (fileOrIdentifier), error))
        for (int i = 0; i < module->getNumPlugins(); ++i)
            if (auto* d = module->getDescriptor (i))
                results.add (new juce::PluginDescription (module->describe (*d)));
}

bool ClapPluginFormat::fileMightContainThisPluginType (const juce::String& fileOrIdentifier)
{
    if (! juce::File::isAbsolutePath (fileOrIdentifier))
        return false;

    const juce::File file (fileOrIdentifier);
    return file.hasFileExtension (".clap") && file.exists();
}

juce::String ClapPluginFormat::getNameOfPluginFromIdentifier (const juce::String& fileOrIdentifier)
{
    return juce::File::isAbsolutePath (fileOrIdentifier) ? juce::File (fileOrIdentifier).getFileNameWithoutExtension()
                                                         : fileOrIdentifier;
}

bool ClapPluginFormat::pluginNeedsRescanning (const juce::PluginDescription& description)
{
    return juce::File (description.fileOrIdentifier).getLastModificationTime() != description.lastFileModTime;
}

bool ClapPluginFormat::doesPluginStillExist (const juce::PluginDescription& description)
{
    return fileMightContainThisPluginType (description.fileOrIdentifier);
}

static void findClapFiles (const juce::File& directory, bool recursive, juce::StringArray& results)
{
    for (const auto& entry : juce::RangedDirectoryIterator (directory, false, "*", juce::File::findFilesAndDirectories))
    {
        const auto file = entry.getFile();

        if (file.hasFileExtension (".clap"))
            results.addIfNotAlreadyThere (file.getFullPathName());
        else if (recursive && entry.isDirectory())
            findClapFiles (file, true, results);    // a .clap bundle is never searched inside
    }
}

juce::StringArray ClapPluginFormat::searchPathsForPlugins (const juce::FileSearchPath& directories, bool recursive, bool)
{
    juce::StringArray results;

    for (int i = 0; i < directories.getNumPaths(); ++i)
        findClapFiles (directories[i], recursive, results);

    return results;
}

juce::FileSearchPath ClapPluginFormat::getDefaultLocationsToSearch()
{
    // Where the CLAP specification says plugins live, plus anything in CLAP_PATH.
    juce::FileSearchPath paths;
    const auto home = juce::File::getSpecialLocation (juce::File::userHomeDirectory);

   #if JUCE_WINDOWS
    const auto commonFiles = juce::SystemStats::getEnvironmentVariable ("COMMONPROGRAMFILES", "C:\\Program Files\\Common Files");
    paths.add (juce::File (commonFiles).getChildFile ("CLAP"));

    const auto localAppData = juce::SystemStats::getEnvironmentVariable ("LOCALAPPDATA", {});

    if (juce::File::isAbsolutePath (localAppData))
        paths.add (juce::File (localAppData).getChildFile ("Programs").getChildFile ("Common").getChildFile ("CLAP"));

    const auto separator = ";";
   #elif JUCE_MAC
    paths.add (juce::File ("/Library/Audio/Plug-Ins/CLAP"));
    paths.add (home.getChildFile ("Library/Audio/Plug-Ins/CLAP"));
    const auto separator = ":";
   #else
    paths.add (home.getChildFile (".clap"));
    paths.add (juce::File ("/usr/lib/clap"));
    paths.add (juce::File ("/usr/local/lib/clap"));
    const auto separator = ":";
   #endif

    for (const auto& extra : juce::StringArray::fromTokens (juce::SystemStats::getEnvironmentVariable ("CLAP_PATH", {}),
                                                            separator, {}))
        if (juce::File::isAbsolutePath (extra.trim()))
            paths.add (juce::File (extra.trim()));

    juce::ignoreUnused (home);
    return paths;
}

void ClapPluginFormat::createPluginInstance (const juce::PluginDescription& description, double initialSampleRate,
                                             int initialBufferSize, PluginCreationCallback callback)
{
    juce::String error;
    std::unique_ptr<juce::AudioPluginInstance> instance;
    const juce::File file (description.fileOrIdentifier);

    if (! fileMightContainThisPluginType (description.fileOrIdentifier))
    {
        error = description.name + " can't be found (it was at " + description.fileOrIdentifier + ").";
    }
    else if (auto module = ClapModule::load (file, error))
    {
        if (auto* descriptor = module->findDescriptor (description))
            instance = createClapPluginInstance (module, *descriptor, description, error);
        else
            error = file.getFileName() + " no longer contains " + description.name + ".";
    }

    if (instance != nullptr)
        instance->setRateAndBufferSizeDetails (initialSampleRate, initialBufferSize);
    else if (error.isEmpty())
        error = description.name + " couldn't be created.";

    callback (std::move (instance), error);
}

} // namespace mperoll
