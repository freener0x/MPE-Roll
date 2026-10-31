// A VST2 plugin that won't start the first time it's called in a process and starts the second time,
// as a plugin waiting for something (a licence check, its maker's app) might: a user's Analog Lab did.
// The harness checks that MPE Roll, once its scanner finds such a plugin starts on its own, loads it
// again by itself. The first call is remembered in the process's environment, which outlives the
// library: the host lets it go after a failed load. Silent, with no parameters; it takes MIDI as a
// synth does and ignores it.

#include <cstdio>
#include <cstdlib>
#include <cstring>

#if defined (_WIN32)
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #ifndef WIN32_LEAN_AND_MEAN
  #define WIN32_LEAN_AND_MEAN
 #endif
 #include <windows.h>
#else
 #include <unistd.h>
#endif

// The VST2 declarations (the clean-room FST headers), without their notes about what they haven't
// documented, and without the pedantry a header meant for C and C++ both trips.
#if defined (__GNUC__)
 #pragma GCC diagnostic push
 #pragma GCC diagnostic ignored "-Wpedantic"
 #pragma GCC diagnostic ignored "-Wunused-macros"
 #pragma GCC diagnostic ignored "-Wzero-as-null-pointer-constant"
#elif defined (_MSC_VER)
 #pragma warning (push, 0)
#endif
#define FST_QUIET 1
#include <pluginterfaces/fst/fst.h>
#if defined (__GNUC__)
 #pragma GCC diagnostic pop
#elif defined (_MSC_VER)
 #pragma warning (pop)
#endif

namespace
{

constexpr const char* pluginName = "MPE Roll Flaky Test Synth";
constexpr const char* vendorName = "Glowing Pancake";
constexpr t_fstInt32 pluginId = 0x466c6b79;    // 'Flky'

// The effect's flags, numbered as every VST2 plugin numbers them.
constexpr t_fstPtrInt flagCanReplacing = 1 << 4;
constexpr t_fstPtrInt flagIsSynth = 1 << 8;

/** Whether this process has called VSTPluginMain before, and that it has now. */
bool calledBefore()
{
    char name[64];

   #if defined (_WIN32)
    std::snprintf (name, sizeof (name), "MPEROLL_FLAKY_SYNTH_STARTED_%lu", static_cast<unsigned long> (GetCurrentProcessId()));
    char value[8];
    const bool seen = GetEnvironmentVariableA (name, value, static_cast<DWORD> (sizeof (value))) != 0;
    SetEnvironmentVariableA (name, "1");
   #else
    std::snprintf (name, sizeof (name), "MPEROLL_FLAKY_SYNTH_STARTED_%ld", static_cast<long> (getpid()));
    const bool seen = std::getenv (name) != nullptr;
    setenv (name, "1", 1);
   #endif

    return seen;
}

void copyString (void* destination, const char* text, size_t maxLength)
{
    if (destination == nullptr)
        return;

    auto* chars = static_cast<char*> (destination);
    std::strncpy (chars, text, maxLength - 1);
    chars[maxLength - 1] = 0;
}

t_fstPtrInt dispatcher (AEffect* effect, int opcode, int, t_fstPtrInt, void* const ptr, float)
{
    switch (opcode)
    {
        case effClose:
            delete effect;
            return 0;

        case effGetEffectName:
        case effGetProductString:
            copyString (ptr, pluginName, 64);
            return 1;

        case effGetVendorString:
            copyString (ptr, vendorName, 64);
            return 1;

        case effGetProgramName:
            copyString (ptr, "Default", 24);
            return 0;

        case effGetVendorVersion:   return 1;
        case effGetPlugCategory:    return kPlugCategSynth;
        case effGetVstVersion:      return 2400;

        case effCanDo:
            if (ptr != nullptr)
            {
                const auto* what = static_cast<const char*> (ptr);

                if (std::strcmp (what, "receiveVstMidiEvent") == 0 || std::strcmp (what, "receiveVstEvents") == 0)
                    return 1;
            }

            return 0;

        default:
            return 0;
    }
}

void processReplacing (AEffect*, float**, float** outputs, int frames)
{
    for (int channel = 0; channel < 2; ++channel)
        if (outputs[channel] != nullptr)
            std::memset (outputs[channel], 0, sizeof (float) * static_cast<size_t> (frames));
}

void processAccumulating (AEffect*, float**, float**, int) {}    // nothing to add
void setParameter (AEffect*, int, float) {}
float getParameter (AEffect*, int) { return 0.0f; }

} // namespace

#if defined (_WIN32)
 #define FLAKY_EXPORT __declspec (dllexport)
#else
 #define FLAKY_EXPORT __attribute__ ((visibility ("default")))
#endif

extern "C" FLAKY_EXPORT AEffect* VSTPluginMain (audioMasterCallback);

extern "C" FLAKY_EXPORT AEffect* VSTPluginMain (audioMasterCallback)
{
    // The first time: as a plugin that won't start yet.
    if (! calledBefore())
        return nullptr;

    auto* effect = new AEffect();
    effect->magic = kEffectMagic;
    effect->dispatcher = dispatcher;
    effect->process = processAccumulating;
    effect->setParameter = setParameter;
    effect->getParameter = getParameter;
    effect->numPrograms = 1;
    effect->numParams = 0;
    effect->numInputs = 0;
    effect->numOutputs = 2;
    effect->flags = flagCanReplacing | flagIsSynth;
    effect->uniqueID = pluginId;
    effect->version = 1;
    effect->processReplacing = processReplacing;
    return effect;
}
