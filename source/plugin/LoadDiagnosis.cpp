#include "LoadDiagnosis.h"

#include <array>
#include <cstring>
#include <optional>
#include <string_view>

#if JUCE_PLUGINHOST_VST3
 // Declarations only: JUCE's own VST3 hosting code defines the interface IDs, and compiles the SDK
 // sources this uses (FUID, the moduleinfo.json parser, the SDK's host application).
 #define JUCE_VST3HEADERS_INCLUDE_HEADERS_ONLY 1
 #include <juce_audio_processors_headless/format_types/juce_VST3Headers.h>
 #undef JUCE_VST3HEADERS_INCLUDE_HEADERS_ONLY

 JUCE_BEGIN_IGNORE_WARNINGS_MSVC (4100 4458)
 JUCE_BEGIN_IGNORE_WARNINGS_GCC_LIKE ("-Wnon-virtual-dtor", "-Wshadow", "-Wsign-conversion", "-Wconversion",
                                      "-Wextra-semi", "-Wzero-as-null-pointer-constant", "-Wunused-parameter",
                                      "-Wpedantic", "-Wpragma-pack", "-Wnewline-eof", "-Wdeprecated")
 #include <public.sdk/source/vst/hosting/hostclasses.h>
 JUCE_END_IGNORE_WARNINGS_GCC_LIKE
 JUCE_END_IGNORE_WARNINGS_MSVC
#endif

#if JUCE_PLUGINHOST_VST
 // The VST2 declarations, in a namespace of their own as JUCE keeps its own.
 namespace mperoll_fst
 {
     #define FST_QUIET 1     // without FST's notes about what it hasn't documented yet
     JUCE_BEGIN_IGNORE_WARNINGS_GCC_LIKE ("-Wzero-as-null-pointer-constant", "-Wpedantic", "-Wunused-macros")
     #include <pluginterfaces/fst/fst.h>
     JUCE_END_IGNORE_WARNINGS_GCC_LIKE
 }
#endif

#if JUCE_WINDOWS
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #ifndef WIN32_LEAN_AND_MEAN
  #define WIN32_LEAN_AND_MEAN
 #endif
 #include <windows.h>
#elif JUCE_LINUX || JUCE_BSD
 #include <dlfcn.h>
 #include <sys/utsname.h>
#endif

// Where plugin binaries are loaded and looked into here (and where the scanner runs): not macOS.
#define MPEROLL_LOADS_BINARIES (JUCE_WINDOWS || JUCE_LINUX || JUCE_BSD)

namespace mperoll::LoadDiagnosis
{

namespace
{
    using Say = std::function<void (const juce::String&)>;

    bool isVst3 (const juce::PluginDescription& d)  { return d.pluginFormatName == "VST3"; }
    bool isVst2 (const juce::PluginDescription& d)  { return d.pluginFormatName == "VST"; }

    juce::String hex (int value)
    {
        return "0x" + juce::String::toHexString ((juce::uint32) value).paddedLeft ('0', 8);
    }

    juce::String quoted (const juce::String& s)    { return "\"" + s + "\""; }

    juce::String verdict (const juce::String& sentence)  { return juce::String (verdictPrefix) + sentence; }

    juce::String milliseconds (double start)
    {
        return juce::String (juce::roundToInt (juce::Time::getMillisecondCounterHiRes() - start)) + " ms";
    }

   #if MPEROLL_LOADS_BINARIES
    // What MPE Roll tells the user about a plugin that won't start, as for one a scan finds empty.
    const char* const activationAdvice =
        "A plugin usually does this when it isn't activated or authorised on this computer, or its maker's "
        "app isn't running (Arturia Software Center, Native Access, iLok License Manager, Roland Cloud "
        "Manager...). Open it once directly in FL Studio, check it's authorised there, then try again.";

    //==========================================================================
    // Loading a binary as JUCE does, with the system's reason when it can't be.

    struct Loaded
    {
        void* handle = nullptr;
        juce::String error;     // why it couldn't be loaded, in words
    };

   #if JUCE_WINDOWS
    juce::String systemMessage (DWORD code, const juce::String& fileName)
    {
        wchar_t* text = nullptr;
        const auto length = FormatMessageW (FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                                            nullptr, code, 0, (LPWSTR) &text, 0, nullptr);
        juce::String message;

        // Some name the file as %1 ("%1 is not a valid Win32 application" for 193).
        if (length > 0 && text != nullptr)
            message = juce::String (text).trim().trimCharactersAtEnd (".")
                          .replace ("%1", fileName.isNotEmpty() ? fileName : juce::String ("the file"));

        LocalFree (text);
        return message;
    }

    bool isLoadedHere (const juce::File& binary)
    {
        HMODULE module = nullptr;
        return GetModuleHandleExW (GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                   binary.getFullPathName().toWideCharPointer(), &module) != 0;
    }
   #endif

    // The working directory JUCE sets while it loads a plugin, which is one of the places a plugin's
    // DLLs are looked for: the folder of the path it loads from, which on Linux is the VST3 bundle.
    juce::File workingDirectoryFor (const juce::PluginDescription& d, const juce::File& binary)
    {
       #if JUCE_LINUX || JUCE_BSD
        if (d.pluginFormatName == "VST3")
            return juce::File (d.fileOrIdentifier).getParentDirectory();
       #else
        juce::ignoreUnused (d);
       #endif

        return binary.getParentDirectory();
    }

    Loaded loadBinary (const juce::PluginDescription& d, const juce::File& binary)
    {
        Loaded loaded;
        const auto previous = juce::File::getCurrentWorkingDirectory();
        workingDirectoryFor (d, binary).setAsCurrentWorkingDirectory();

       #if JUCE_WINDOWS
        // Plain LoadLibraryW, as JUCE's, so DLLs are looked for in the same places.
        loaded.handle = (void*) LoadLibraryW (binary.getFullPathName().toWideCharPointer());

        if (loaded.handle == nullptr)
            loaded.error = describeWindowsError ((juce::uint32) GetLastError(), binary.getFileName());
       #elif JUCE_LINUX || JUCE_BSD
        loaded.handle = dlopen (binary.getFullPathName().toRawUTF8(), RTLD_LOCAL | RTLD_NOW);

        if (loaded.handle == nullptr)
            if (const auto* error = dlerror())
                loaded.error = error;
       #endif

        previous.setAsCurrentWorkingDirectory();
        return loaded;
    }

    void* findFunction (void* handle, const char* name)
    {
       #if JUCE_WINDOWS
        return (void*) GetProcAddress ((HMODULE) handle, name);
       #else
        return dlsym (handle, name);
       #endif
    }

    void unloadBinary (void* handle)
    {
       #if JUCE_WINDOWS
        FreeLibrary ((HMODULE) handle);
       #else
        dlclose (handle);
       #endif
    }

    juce::String couldNotLoad (const juce::File& binary, const juce::String& error)
    {
       #if JUCE_WINDOWS
        return "Windows couldn't load " + binary.getFileName() + ": " + error + ".";
       #else
        return binary.getFileName() + " couldn't be loaded: " + error + ".";
       #endif
    }

    // The VST3 module's entry point, which JUCE calls before asking for its factory.
   #if JUCE_WINDOWS
    constexpr const char* vst3EntryName = "InitDll";
   #else
    constexpr const char* vst3EntryName = "ModuleEntry";
   #endif

    // What describeMachine() says about a binary built for the processor this runs on.
   #if defined (_M_ARM64) || defined (__aarch64__)
    constexpr const char* thisMachine = "ARM64";
   #else
    constexpr const char* thisMachine = "x64";
   #endif
   #endif

    //==========================================================================
   #if JUCE_PLUGINHOST_VST3
    using namespace Steinberg;

    // As JUCE turns a class ID into PluginDescription::uniqueId and deprecatedUid
    // (juce_VST3PluginFormatImpl.h), to say which of them a class doesn't match.
    template <typename Range>
    int hashOf (const Range& range)
    {
        juce::uint32 value = 0;

        for (const auto& item : range)
            value = (value * 31) + (juce::uint32) item;

        return (int) value;
    }

    int uniqueIdOf (const TUID& cid)
    {
        const FUID fuid { cid };
        const std::array<juce::uint32, 4> longs { fuid.getLong1(), fuid.getLong2(), fuid.getLong3(), fuid.getLong4() };
        return hashOf (longs);
    }

    int deprecatedUidOf (const TUID& cid)
    {
        return hashOf (cid);
    }

   #if MPEROLL_LOADS_BINARIES
    juce::String describeCid (const TUID& cid)
    {
        return VST3::UID::fromTUID (cid).toString();
    }

    juce::String nameOf (const char8* text, size_t size)
    {
        return juce::String::fromUTF8 (text, (int) strnlen (text, size)).trim();
    }

    juce::String describeResult (tresult result)
    {
        if (result == kResultOk)          return "kResultOk";
        if (result == kResultFalse)       return "kResultFalse";
        if (result == kNoInterface)       return "kNoInterface";
        if (result == kInvalidArgument)   return "kInvalidArgument";
        if (result == kNotImplemented)    return "kNotImplemented";
        if (result == kInternalError)     return "kInternalError";
        if (result == kNotInitialized)    return "kNotInitialized";
        if (result == kOutOfMemory)       return "kOutOfMemory";
        return "result " + juce::String ((int) result);
    }

    // The host context the plugin gets: the SDK's own example host, named as MPE Roll is inside FL.
    struct DiagnosisHost final : public Vst::HostApplication
    {
        tresult PLUGIN_API getName (Vst::String128 name) override
        {
            const juce::String ourName ("MPE Roll");

            for (int i = 0; i <= ourName.length(); ++i)
                name[i] = (Vst::TChar) ourName[i];

            return kResultOk;
        }
    };
   #endif

    struct ModuleInfoClass
    {
        juce::String name, cid;
        bool matchesName = false, matchesId = false;
    };

    // The instrument and effect classes a bundle's moduleinfo.json lists, if it has one: what a scan
    // reads instead of running the plugin's code.
    std::optional<std::vector<ModuleInfoClass>> readModuleInfo (const juce::File& bundle, const juce::PluginDescription& d)
    {
        for (const auto& file : { bundle.getChildFile ("Contents/Resources/moduleinfo.json"),
                                  bundle.getChildFile ("Contents/moduleinfo.json") })
        {
            juce::MemoryBlock data;

            if (! file.existsAsFile() || ! file.loadFileAsData (data))
                continue;

            const auto parsed = ModuleInfoLib::parseJson (std::string_view (static_cast<const char*> (data.getData()), data.getSize()), nullptr);

            if (! parsed)
                continue;

            std::vector<ModuleInfoClass> classes;

            for (const auto& c : parsed->classes)
            {
                if (c.category != kVstAudioEffectClass)
                    continue;

                ModuleInfoClass info;
                info.name = juce::String::fromUTF8 (c.name.c_str());
                info.cid = c.cid;
                info.matchesName = info.name == d.name;     // untrimmed, as JUCE takes the name from here

                if (const auto uid = VST3::UID::fromString (c.cid))
                    info.matchesId = uniqueIdOf (uid->data()) == d.uniqueId || deprecatedUidOf (uid->data()) == d.deprecatedUid;

                classes.push_back (info);
            }

            return classes;
        }

        return std::nullopt;
    }
   #endif
}

//==============================================================================
juce::Array<juce::File> findBinaries (const juce::PluginDescription& d)
{
    juce::Array<juce::File> binaries;
    const auto file = juce::File::createFileWithoutCheckingPath (d.fileOrIdentifier);

    if (isVst2 (d))
    {
        binaries.add (file);
    }
    else if (isVst3 (d))
    {
       #if JUCE_WINDOWS
        // A bundle folder: every .vst3 file inside it, as JUCE looks for them.
        if (file.isDirectory())
        {
            for (const auto& entry : juce::RangedDirectoryIterator (file, true, "*.vst3", juce::File::findFiles))
                binaries.add (entry.getFile());

            return binaries;
        }
       #elif JUCE_LINUX || JUCE_BSD
        // Always Contents/<machine>-linux/<name>.so, as JUCE loads it, whatever the path is.
        struct utsname names {};
        const juce::String machine (uname (&names) == 0 ? names.machine : "");
        binaries.add (file.getChildFile ("Contents").getChildFile (machine + "-linux")
                          .getChildFile (file.getFileNameWithoutExtension() + ".so"));
        return binaries;
       #endif

        binaries.add (file);
    }

    return binaries;
}

juce::String describe (const juce::PluginDescription& d)
{
    juce::String s;
    s << d.pluginFormatName << " " << quoted (d.name) << ", uniqueId " << hex (d.uniqueId)
      << ", deprecatedUid " << hex (d.deprecatedUid);

    if (isVst3 (d))
    {
        // A scan that reads moduleinfo.json fills in no channels; one that runs the plugin does.
        if (d.numInputChannels == 0 && d.numOutputChannels == 0)
            s << "; listed from the bundle's moduleinfo.json, without running the plugin";
        else
            s << "; listed by running the plugin (" << d.numInputChannels << " in, " << d.numOutputChannels << " out)";
    }

    return s;
}

juce::String describeMachine (const juce::File& file)
{
    juce::FileInputStream in (file);

    if (! in.openedOk())
        return {};

    char mz[2] {};

    if (in.read (mz, 2) != 2 || mz[0] != 'M' || mz[1] != 'Z' || ! in.setPosition (0x3c))
        return {};

    const auto peOffset = in.readInt();
    char pe[4] {};

    if (peOffset <= 0 || ! in.setPosition (peOffset) || in.read (pe, 4) != 4 || std::memcmp (pe, "PE\0\0", 4) != 0)
        return {};

    switch ((juce::uint16) in.readShort())
    {
        case 0x8664: return "x64";
        case 0x014c: return "x86 (32-bit)";
        case 0xaa64: return "ARM64";
        case 0xa641: return "ARM64EC";
        case 0xa64e: return "ARM64X";
        default:     return "an unusual machine type";
    }
}

juce::StringArray inspectFiles (const juce::PluginDescription& d)
{
    juce::StringArray lines;
    const auto file = juce::File::createFileWithoutCheckingPath (d.fileOrIdentifier);

    if (! (isVst2 (d) || isVst3 (d)))
        return lines;

    if (! file.exists())
    {
        lines.add (file.getFullPathName() + " isn't there");
        return lines;
    }

    const auto binaries = findBinaries (d);

    if (binaries.isEmpty())
        lines.add ("There's no .vst3 file inside " + file.getFullPathName());

    for (const auto& binary : binaries)
    {
        auto line = binary.getFullPathName();

        if (binary.isDirectory())
        {
            line << ": a bundle";      // macOS, where the bundle is what's loaded
        }
        else if (! binary.existsAsFile())
        {
            line << " isn't there";
        }
        else
        {
            line << ": " << juce::File::descriptionOfSizeInBytes (binary.getSize())
                 << ", changed " << binary.getLastModificationTime().toString (true, true, false, true);

            if (const auto machine = describeMachine (binary); machine.isNotEmpty())
                line << ", built for " << machine;
        }

        lines.add (line);
    }

   #if JUCE_PLUGINHOST_VST3
    if (isVst3 (d) && file.isDirectory())
    {
        if (const auto classes = readModuleInfo (file, d))
        {
            juce::StringArray listed;

            for (const auto& c : *classes)
                listed.add (quoted (c.name) + " " + c.cid + (c.matchesName && c.matchesId ? " (the one loaded)" : ""));

            lines.add ("Its moduleinfo.json lists " + (listed.isEmpty() ? juce::String ("no instrument or effect") : listed.joinIntoString (", ")));
        }
        else
        {
            lines.add ("It has no moduleinfo.json, so a scan runs the plugin to see what's in it");
        }
    }
   #endif

    return lines;
}

juce::String describeWindowsError (juce::uint32 code, const juce::String& fileName)
{
    const auto plain = [code]() -> juce::String
    {
        switch (code)
        {
            case 2:
            case 3:     return "the file, or its folder, isn't there";
            case 5:     return "Windows denied access to it (file permissions, or antivirus)";
            case 126:   return "a DLL it needs is missing, often a Microsoft Visual C++ runtime or a file of its own that "
                               "an installer left out (reinstalling it usually brings it back)";
            case 127:   return "a DLL it needs isn't the version it expects, often because another copy of that DLL, from "
                               "FL Studio or another plugin, is loaded already";
            case 193:
            case 216:   return "it isn't a 64-bit Windows plugin (or the file is damaged)";
            case 225:   return "antivirus blocked it";
            case 577:   return "Windows couldn't verify its digital signature";
            case 1114:  return "its own start-up code (DllMain) failed";
            case 1455:  return "Windows ran out of memory loading it";
            case 4551:  return "Windows' app control (Smart App Control, or a policy set by whoever manages the computer) blocked it";
            case 14001: return "the Visual C++ runtime it uses isn't installed properly (reinstalling the Microsoft Visual C++ "
                               "Redistributable usually fixes that)";
            default:    return {};
        }
    }();

   #if JUCE_WINDOWS
    const auto system = systemMessage ((DWORD) code, fileName);
   #else
    const juce::String system;
    juce::ignoreUnused (fileName);
   #endif

    auto s = plain.isNotEmpty() ? plain : system.isNotEmpty() ? system : juce::String ("Windows gave no reason");
    s << " (Windows error " << (int) code;

    if (plain.isNotEmpty() && system.isNotEmpty())
        s << ": " << system;

    return s + ")";
}

//==============================================================================
PreFlight::PreFlight ([[maybe_unused]] const juce::PluginDescription& d)
{
   #if MPEROLL_LOADS_BINARIES
    if (! (isVst2 (d) || isVst3 (d)))
        return;

    const auto binaries = findBinaries (d);

    if (binaries.isEmpty())
        problem = "There's no .vst3 file inside " + juce::File (d.fileOrIdentifier).getFileName()
                + ", so its installation is incomplete: reinstall it.";

    // JUCE tries a bundle's binaries in turn (one for each kind of processor, say), so a binary that
    // can't be loaded is only the problem if none can: then it's the one built for this processor.
    juce::String firstProblem, problemHere;

    const auto noteProblem = [&] (const juce::File& binary, const juce::String& what)
    {
        if (firstProblem.isEmpty())
            firstProblem = what;

        if (problemHere.isEmpty() && describeMachine (binary) == thisMachine)
            problemHere = what;
    };

    for (const auto& binary : binaries)
    {
        if (! binary.existsAsFile())
        {
            lines.add ("Before loading: " + binary.getFullPathName() + " isn't there");
            noteProblem (binary, binary.getFullPathName() + " isn't there any more: scan for plugins again, or reinstall it.");
            continue;
        }

       #if JUCE_WINDOWS
        const auto loadedAlready = isLoadedHere (binary);
       #else
        const auto loadedAlready = false;
       #endif

        const auto started = juce::Time::getMillisecondCounterHiRes();
        const auto loaded = loadBinary (d, binary);

        if (loaded.handle == nullptr)
        {
            lines.add ("Before loading: " + couldNotLoad (binary, loaded.error));
            noteProblem (binary, couldNotLoad (binary, loaded.error));
            continue;
        }

        handles.push_back (loaded.handle);

        juce::String line ("Before loading: the system loaded " + binary.getFullPathName() + " in " + milliseconds (started));
        auto usable = true;

        if (loadedAlready)
            line << " (it was loaded in this process already)";

        if (isVst3 (d))
        {
            if (findFunction (loaded.handle, "GetPluginFactory") == nullptr)
            {
                line << "; it has no GetPluginFactory";
                usable = false;
                noteProblem (binary, binary.getFileName() + " has no GetPluginFactory function, so it isn't a VST3 plugin, "
                                     "or it's a damaged one: reinstall it.");
            }

            line << "; " << vst3EntryName << (findFunction (loaded.handle, vst3EntryName) != nullptr ? " is there" : " isn't (allowed)");
        }
        else if (findFunction (loaded.handle, "VSTPluginMain") == nullptr && findFunction (loaded.handle, "main") == nullptr)
        {
            line << "; it has neither VSTPluginMain nor main";
            usable = false;
            noteProblem (binary, binary.getFileName() + " has no VSTPluginMain or main function, so it isn't a VST2 plugin: "
                                 "some .dll files in plugin folders are libraries plugins use, not plugins.");
        }

        lines.add (line);

        // The one JUCE will go on with: what happens next is for the scanner to find out.
        if (usable)
            return;
    }

    if (problem.isEmpty())
        problem = problemHere.isNotEmpty() ? problemHere : firstProblem;
   #endif
}

PreFlight::~PreFlight()
{
   #if MPEROLL_LOADS_BINARIES
    for (auto* handle : handles)
        unloadBinary (handle);
   #endif
}

//==============================================================================
namespace
{
   #if JUCE_PLUGINHOST_VST3 && MPEROLL_LOADS_BINARIES
    void listClasses (IPluginFactory& factory, const Say& say)
    {
        const auto count = factory.countClasses();
        juce::StringArray classes;

        for (int32 i = 0; i < count; ++i)
        {
            PClassInfo info {};

            if (factory.getClassInfo (i, &info) != kResultOk)
            {
                classes.add ("class " + juce::String (i) + " (getClassInfo failed)");
                continue;
            }

            auto c = quoted (nameOf (info.name, sizeof (info.name))) + " " + nameOf (info.category, sizeof (info.category))
                   + " " + describeCid (info.cid);

            if (info.cardinality == 1)
                c << ", one instance at a time";

            classes.add (c);
        }

        say ("Its factory has " + juce::String (count) + (count == 1 ? " class: " : " classes: ") + classes.joinIntoString ("; "));
    }

    // JUCE's choice of class: an instrument or effect with the description's name and one of its IDs.
    int32 findMatchingClass (IPluginFactory& factory, const juce::PluginDescription& d, juce::StringArray& nearMisses)
    {
        const auto count = factory.countClasses();

        for (int32 i = 0; i < count; ++i)
        {
            PClassInfo info {};
            factory.getClassInfo (i, &info);    // its result unchecked, as JUCE leaves it

            if (std::strcmp (info.category, kVstAudioEffectClass) != 0)
                continue;

            const auto name = nameOf (info.name, sizeof (info.name));
            const auto nameMatches = name == d.name;
            const auto idMatches = uniqueIdOf (info.cid) == d.uniqueId || deprecatedUidOf (info.cid) == d.deprecatedUid;

            if (nameMatches && idMatches)
                return i;

            nearMisses.add (quoted (name) + (nameMatches ? " has another ID (" + hex (uniqueIdOf (info.cid)) + ")"
                                           : idMatches ? " has the ID, under another name"
                                                       : " has another name and ID"));
        }

        return -1;
    }

    void diagnoseVst3 (const juce::PluginDescription& d, const Say& say)
    {
        const auto binaries = findBinaries (d);

        if (binaries.isEmpty())
        {
            say (verdict ("There's no .vst3 file inside " + juce::File (d.fileOrIdentifier).getFileName()
                          + ", so its installation is incomplete: reinstall " + d.name + "."));
            return;
        }

        juce::String outcome;
        auto loadedOne = false;

        // Each binary as JUCE tries them (normally there's one; a bundle may hold one for each kind
        // of processor). The first that gets through decides; otherwise what stopped the deepest one,
        // since a binary for another processor not loading says nothing.
        for (const auto& binary : binaries)
        {
            say ("Loading " + binary.getFullPathName() + "...");
            const auto loaded = loadBinary (d, binary);

            if (loaded.handle == nullptr)
            {
                say ("It couldn't be loaded: " + loaded.error);

                if (! loadedOne)
                    outcome = couldNotLoad (binary, loaded.error);

                continue;
            }

            loadedOne = true;

            if (auto* entry = findFunction (loaded.handle, vst3EntryName))
            {
                say (juce::String ("Calling its ") + vst3EntryName + "...");

               #if JUCE_WINDOWS
                const auto entered = ((bool (PLUGIN_API*)()) entry)();
               #else
                const auto entered = ((bool (PLUGIN_API*) (void*)) entry) (loaded.handle);
               #endif

                if (! entered)
                {
                    say (juce::String (vst3EntryName) + " returned false");
                    outcome = d.name + "'s start-up function (" + vst3EntryName + ") refused to start it. " + activationAdvice;
                    continue;
                }
            }

            auto* getFactory = (IPluginFactory* (PLUGIN_API*)()) findFunction (loaded.handle, "GetPluginFactory");

            if (getFactory == nullptr)
            {
                say ("It has no GetPluginFactory");
                outcome = binary.getFileName() + " has no GetPluginFactory function, so it isn't a VST3 plugin, or it's a "
                          "damaged one: reinstall " + d.name + ".";
                continue;
            }

            say ("Calling its GetPluginFactory...");
            auto* factory = getFactory();

            if (factory == nullptr)
            {
                say ("GetPluginFactory returned nothing");
                outcome = d.name + "'s GetPluginFactory returned nothing, which a plugin does when it won't start. " + activationAdvice;
                continue;
            }

            PFactoryInfo factoryInfo {};

            if (factory->getFactoryInfo (&factoryInfo) == kResultOk)
                say ("Its factory's vendor: " + nameOf (factoryInfo.vendor, sizeof (factoryInfo.vendor)));

            listClasses (*factory, say);

            // Before choosing a class, as JUCE does: a shell may list its classes only then.
            auto* host = new DiagnosisHost();     // never freed: the scanner leaves without cleaning up
            IPluginFactory3* factory3 = nullptr;

            if (factory->queryInterface (IPluginFactory3::iid, (void**) &factory3) == kResultOk && factory3 != nullptr)
            {
                const auto before = factory->countClasses();
                say ("setHostContext: " + describeResult (factory3->setHostContext (host)));

                if (factory->countClasses() != before)
                    listClasses (*factory, say);
            }

            juce::StringArray nearMisses;
            const auto index = findMatchingClass (*factory, d, nearMisses);

            if (index < 0)
            {
                const auto inThePlugin = nearMisses.isEmpty() ? juce::String ("it has no instrument or effect")
                                                              : "in the plugin, " + nearMisses.joinIntoString ("; ");
                say ("No class is " + quoted (d.name) + " with uniqueId " + hex (d.uniqueId) + " or deprecatedUid "
                     + hex (d.deprecatedUid) + ": " + inThePlugin);

                // Listed from moduleinfo.json, which still says the same: the file and the plugin disagree,
                // and scanning again would only read the file again.
                auto moduleInfoAgrees = false;

                if (d.numInputChannels == 0 && d.numOutputChannels == 0)
                    if (const auto classes = readModuleInfo (juce::File (d.fileOrIdentifier), d))
                        for (const auto& c : *classes)
                            moduleInfoAgrees = moduleInfoAgrees || (c.matchesName && c.matchesId);

                // On Windows the binary inside the bundle loads as a VST3 of its own, with no moduleinfo.json
                // beside it. On Linux it's taken for a VST2, so there's no file that makes JUCE run the plugin.
               #if JUCE_WINDOWS
                const auto readThePlugin = " Use Load synth > Load plugin file... on " + binary.getFullPathName()
                                         + ": MPE Roll then reads the plugin itself.";
               #else
                const juce::String readThePlugin (" Its maker would need to correct that file. Meanwhile, try its other "
                                                  "format (VST2 or CLAP), and please send the details.");
               #endif

                // Load plugin file... reads a file afresh, where a scan skips one it has listed unless the
                // bundle's folder changed, which an update replacing the files inside it doesn't do.
                outcome = moduleInfoAgrees
                    ? d.name + "'s moduleinfo.json, which a scan reads instead of running the plugin, doesn't match the "
                      "plugin itself: " + inThePlugin + "." + readThePlugin
                    : d.name + " has changed since MPE Roll's list of plugins was made (by an update, say): " + inThePlugin
                      + ". Use Load synth > Load plugin file... on " + juce::File (d.fileOrIdentifier).getFullPathName()
                      + ", which reads it afresh (a scan skips a plugin it has listed already).";
                continue;
            }

            PClassInfo info {};
            factory->getClassInfo (index, &info);
            say ("The class to load: " + quoted (nameOf (info.name, sizeof (info.name))) + " " + describeCid (info.cid));

            say ("Creating its component...");
            Vst::IComponent* component = nullptr;
            auto result = factory->createInstance (info.cid, Vst::IComponent::iid, (void**) &component);

            if (result != kResultOk || component == nullptr)
            {
                say ("createInstance: " + describeResult (result));
                outcome = d.name + " wouldn't create its audio component (" + describeResult (result) + "). " + activationAdvice;
                continue;
            }

            say ("Starting its component (initialize)...");
            result = component->initialize (host);

            if (result != kResultOk)
            {
                say ("initialize: " + describeResult (result));
                outcome = d.name + "'s audio component refused to start (" + describeResult (result) + "). " + activationAdvice;
                continue;
            }

            // Its controller, found as JUCE finds it: the component itself, else the class the component
            // names, else each controller class in the factory in turn, the last one deciding.
            say ("Finding its controller...");
            Vst::IEditController* controller = nullptr;
            auto separate = false;

            if (component->queryInterface (Vst::IEditController::iid, (void**) &controller) == kResultOk && controller != nullptr)
            {
                say ("Its component is its controller too");
            }
            else
            {
                controller = nullptr;
                separate = true;
                TUID controllerCid {};

                if (component->getControllerClassId (controllerCid) == kResultTrue && FUID (controllerCid).isValid())
                {
                    result = factory->createInstance (controllerCid, Vst::IEditController::iid, (void**) &controller);
                    say ("Creating the controller it names: " + describeResult (result));

                    if (result != kResultOk)
                        controller = nullptr;
                }

                if (controller == nullptr)
                {
                    for (int32 i = 0; i < factory->countClasses(); ++i)
                    {
                        PClassInfo c {};
                        factory->getClassInfo (i, &c);

                        if (std::strcmp (c.category, kVstComponentControllerClass) != 0)
                            continue;

                        controller = nullptr;
                        result = factory->createInstance (c.cid, Vst::IEditController::iid, (void**) &controller);
                        say ("Creating the controller class " + quoted (nameOf (c.name, sizeof (c.name))) + ": " + describeResult (result));

                        if (result != kResultOk)
                            controller = nullptr;
                    }
                }
            }

            if (controller == nullptr)
            {
                outcome = d.name + "'s controller, the part that runs its window and parameters, couldn't be found or "
                          "created, so JUCE won't load it. Please send the details: this is one for MPE Roll to look into.";
                continue;
            }

            // A separate controller is started as JUCE starts it (which ignores the answer); a component
            // that is its own controller has been started already.
            if (separate)
                say ("Its controller is there; starting it: " + describeResult (controller->initialize (host)));

            // Every step JUCE takes worked here, although JUCE's own load didn't a moment ago. What to
            // make of that is said where the load is tried once more (HostedSynth).
            say (verdict (d.name + " got through each of JUCE's steps" + separateProcessPhrase + " when MPE Roll took them "
                          "one at a time, but not when JUCE loaded it in one go."));
            return;
        }

        say (verdict (outcome));
    }
   #endif

   #if JUCE_PLUGINHOST_VST && MPEROLL_LOADS_BINARIES
    // What a VST2 plugin asks the host while VSTPluginMain runs, answered as JUCE answers it.
    struct Vst2Calls
    {
        juce::StringArray asked;
        mperoll_fst::t_fstPtrInt shellId = 0;
    };

    Vst2Calls vst2Calls;    // one diagnosis per scanner process

    juce::String describeHostOpcode (int opcode, const void* ptr)
    {
        using namespace mperoll_fst;

        switch (opcode)
        {
            case audioMasterVersion:           return "Version";
            case audioMasterCurrentId:         return "CurrentId";
            case audioMasterCanDo:             return "CanDo " + quoted (ptr != nullptr ? juce::String ((const char*) ptr) : juce::String());
            case audioMasterGetVendorString:   return "GetVendorString";
            case audioMasterGetProductString:  return "GetProductString";
            case audioMasterGetVendorVersion:  return "GetVendorVersion";
            case audioMasterGetSampleRate:     return "GetSampleRate";
            case audioMasterGetBlockSize:      return "GetBlockSize";
            case audioMasterGetTime:           return "GetTime";
            case audioMasterAutomate:          return "Automate";
            default:                           return "opcode " + juce::String (opcode);
        }
    }

    mperoll_fst::t_fstPtrInt hostCallback (mperoll_fst::AEffect*, int opcode, int, mperoll_fst::t_fstPtrInt, void* const ptr, float)
    {
        using namespace mperoll_fst;

        if (vst2Calls.asked.size() < 40)
            vst2Calls.asked.add (describeHostOpcode (opcode, ptr));

        switch (opcode)
        {
            case audioMasterVersion:           return 2400;
            case audioMasterCurrentId:         return vst2Calls.shellId;
            case audioMasterGetVendorVersion:  return 0x0101;
            case audioMasterGetSampleRate:     return 44100;
            case audioMasterGetBlockSize:      return 512;

            case audioMasterGetVendorString:
            case audioMasterGetProductString:
                if (ptr != nullptr)
                    std::strcpy ((char*) ptr, "Juce VST Host");    // what JUCE says from inside a plugin
                return 1;

            case audioMasterCanDo:
            {
                if (ptr == nullptr)
                    return 0;

                for (const auto* canDo : { "supplyIdle", "sendVstEvents", "sendVstMidiEvent", "sendVstTimeInfo", "receiveVstEvents",
                                           "receiveVstMidiEvent", "supportShell", "sizeWindow", "shellCategory" })
                    if (std::strcmp (canDo, (const char*) ptr) == 0)
                        return 1;

                return 0;
            }

            default:
                return 0;
        }
    }

    void diagnoseVst2 (const juce::PluginDescription& d, const Say& say)
    {
        using namespace mperoll_fst;
        const juce::File binary (d.fileOrIdentifier);

        say ("Loading " + binary.getFullPathName() + "...");
        const auto loaded = loadBinary (d, binary);

        if (loaded.handle == nullptr)
        {
            say ("It couldn't be loaded: " + loaded.error);
            say (verdict (couldNotLoad (binary, loaded.error)));
            return;
        }

        auto* entry = findFunction (loaded.handle, "VSTPluginMain");

        if (entry == nullptr)
            entry = findFunction (loaded.handle, "main");

        if (entry == nullptr)
        {
            say (verdict (binary.getFileName() + " has no VSTPluginMain or main function, so it isn't a VST2 plugin: some "
                          ".dll files in plugin folders are libraries plugins use, not plugins."));
            return;
        }

        // The plugin in a shell (WaveShell, say) that the description names, as JUCE asks for it.
        vst2Calls.shellId = d.uniqueId != 0 ? d.uniqueId : d.deprecatedUid;

        say ("Calling its VSTPluginMain...");
        const auto started = juce::Time::getMillisecondCounterHiRes();
        mperoll_fst::AEffect* effect = nullptr;
        auto threw = false;

        try
        {
            effect = ((mperoll_fst::AEffect* (*) (mperoll_fst::audioMasterCallback)) entry) (hostCallback);
        }
        catch (...)
        {
            threw = true;
        }

        const auto took = milliseconds (started);

        if (! vst2Calls.asked.isEmpty())
            say ("While starting, it asked the host: " + vst2Calls.asked.joinIntoString (", "));

        if (threw)
        {
            say (verdict (d.name + "'s VSTPluginMain threw an exception after " + took + " instead of starting. " + activationAdvice));
            return;
        }

        if (effect == nullptr)
        {
            say (verdict (d.name + "'s VSTPluginMain returned nothing after " + took + ", which a plugin does when it won't "
                          "start. " + activationAdvice));
            return;
        }

        if (effect->magic != mperoll_fst::kEffectMagic)
        {
            say (verdict (d.name + "'s VSTPluginMain returned something that isn't a VST2 plugin (" + hex (effect->magic)
                          + " where 'VstP' belongs)."));
            return;
        }

        say ("It started in " + took + ": ID " + hex (effect->uniqueID) + ", " + juce::String (effect->numInputs) + " in, "
             + juce::String (effect->numOutputs) + " out, flags " + hex ((int) effect->flags));
        say (verdict (d.name + " started" + separateProcessPhrase + " when MPE Roll called it directly, though JUCE's own "
                      "load failed a moment before."));
    }
   #endif
}

void diagnose (const juce::PluginDescription& d, juce::AudioPluginFormatManager& formats, const Say& say)
{
    // What the files say is in the plugin log already, from MPE Roll's side.
    say ("Retracing the load of " + describe (d));

    // As MPE Roll loads it, first: if that works here, the plugin is fine, and what stopped it is
    // something about the process it failed in.
    say ("Loading it as MPE Roll does...");
    const auto started = juce::Time::getMillisecondCounterHiRes();
    juce::String error;
    auto instance = formats.createPluginInstance (d, 44100.0, 512, error);

    if (instance != nullptr)
    {
        say ("It loaded in " + milliseconds (started) + ": " + instance->getName());
        (void) instance.release();     // the scanner leaves without cleaning up after plugins

        // What to make of that is said where the load is tried once more (HostedSynth).
        say (verdict (d.name + " loads fine" + separateProcessPhrase + ", so the plugin itself works."));
        return;
    }

    say ("JUCE's load failed after " + milliseconds (started) + ": " + error);

   #if JUCE_PLUGINHOST_VST3 && MPEROLL_LOADS_BINARIES
    if (isVst3 (d))
        return diagnoseVst3 (d, say);
   #endif

   #if JUCE_PLUGINHOST_VST && MPEROLL_LOADS_BINARIES
    if (isVst2 (d))
        return diagnoseVst2 (d, say);
   #endif

    say (verdict ({}));
}

bool worksElsewhere (const juce::StringArray& lines)
{
    for (int i = lines.size(); --i >= 0;)
        if (lines[i].startsWith (verdictPrefix))
            return lines[i].contains (separateProcessPhrase);

    return false;
}

juce::String summarise (const juce::StringArray& lines, bool crashed, bool timedOut, const juce::String& pluginName)
{
    for (int i = lines.size(); --i >= 0;)
        if (lines[i].startsWith (verdictPrefix))
            return lines[i].substring ((int) std::strlen (verdictPrefix)).trim();

    // Cut short by the plugin: the last line says what it was doing.
    juce::String lastStep;

    for (int i = lines.size(); --i >= 0 && lastStep.isEmpty();)
        lastStep = lines[i].trim();

    const auto where = lastStep.isNotEmpty() ? ", at this step: " + lastStep.trimCharactersAtEnd (".") + "." : juce::String (".");

    if (crashed)
        return pluginName + " crashed in a separate process when MPE Roll retraced its load step by step" + where;

    if (timedOut)
        return pluginName + " didn't finish loading in a separate process when MPE Roll retraced its load step by step" + where
             + " It may have been waiting for a window of its own (a licence or sign-in), which can open behind FL "
               "Studio's: open it once directly in FL Studio, then try again.";

    return {};
}

} // namespace mperoll::LoadDiagnosis
