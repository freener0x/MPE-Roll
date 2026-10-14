// Maps the VST2 SDK include path that JUCE's VST2 hosting expects onto FST's clean-room headers.
#pragma once

#ifndef FST_QUIET
 #define FST_QUIET 1                    // FST's notes about undocumented symbols are for FST developers
#endif

#ifndef FST_DONT_DEPRECATE_UNKNOWN
 #define FST_DONT_DEPRECATE_UNKNOWN     // defined exactly as fst.h does on MSVC, so it isn't a redefinition
#endif

#if defined (__GNUC__) || defined (__clang__)
 #pragma GCC diagnostic push
 #pragma GCC diagnostic ignored "-Wpedantic"
#elif defined (_MSC_VER)
 #pragma warning (push)
 #pragma warning (disable: 4200)        // VstEvents ends in an open-ended array, as in the original SDK
#endif

#include "../fst/aeffectx.h"

#if defined (__GNUC__) || defined (__clang__)
 #pragma GCC diagnostic pop
#elif defined (_MSC_VER)
 #pragma warning (pop)
#endif

// FST doesn't describe MIDI key names. JUCE's VST2 host declares a query for them, which MPE Roll
// never makes; these placeholders let it compile, and use an opcode no plugin implements, so the
// query would simply report that there's no name.
#ifndef MPEROLL_FST_KEY_NAME_PLACEHOLDERS
 #define MPEROLL_FST_KEY_NAME_PLACEHOLDERS 1

enum { kVstMaxNameLen = 64 };
enum { effGetMidiKeyName = 0x4d50524c };

typedef struct MidiKeyName
{
    int thisProgramIndex;
    int thisKeyNumber;
    char keyName[kVstMaxNameLen];
} MidiKeyName;
#endif
