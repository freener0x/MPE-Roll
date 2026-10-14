#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <clap/clap.h>

#include <memory>

namespace mperoll
{

/** A loaded .clap file.

    Every user of the same file shares one module. clap_entry's init() runs when the first one
    loads it and deinit() when the last one lets go, never at the same time as each other.
*/
class ClapModule
{
public:
    static std::shared_ptr<ClapModule> load (const juce::File& file, juce::String& error);

    ~ClapModule();

    const juce::File& getFile() const noexcept                  { return file; }
    const clap_plugin_factory_t& getFactory() const noexcept    { return *factory; }

    int getNumPlugins() const;
    const clap_plugin_descriptor_t* getDescriptor (int index) const;

    /** The descriptor a PluginDescription made by describe() refers to. */
    const clap_plugin_descriptor_t* findDescriptor (const juce::PluginDescription&) const;

    juce::PluginDescription describe (const clap_plugin_descriptor_t&) const;

    /** The number JUCE uses to tell the plugins in one file apart. */
    static int uniqueIdFor (const char* clapPluginId);

private:
    explicit ClapModule (const juce::File& f) : file (f) {}
    bool open (juce::String& error);
    static void release (const juce::String& key, ClapModule*);

    juce::File file;
    juce::DynamicLibrary library;
    const clap_plugin_entry_t* entry = nullptr;
    const clap_plugin_factory_t* factory = nullptr;
    bool initialised = false;

    JUCE_DECLARE_NON_COPYABLE (ClapModule)
};

/** Instantiates a plugin from a module (defined with the plugin instance). */
std::unique_ptr<juce::AudioPluginInstance> createClapPluginInstance (std::shared_ptr<ClapModule>,
                                                                     const clap_plugin_descriptor_t&,
                                                                     const juce::PluginDescription&,
                                                                     juce::String& error);

} // namespace mperoll
