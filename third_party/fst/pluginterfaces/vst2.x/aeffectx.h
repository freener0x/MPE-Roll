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

// fst.h gives opcodes nobody had documented a placeholder value (100000 and up) that no plugin
// would ever send or answer. Every opcode JUCE's VST2 host uses has to be the real one.
#define MPEROLL_FST_REAL_OPCODE(name) static_assert (name < 100000, #name " is still an FST placeholder");

MPEROLL_FST_REAL_OPCODE (audioMasterIdle)                       MPEROLL_FST_REAL_OPCODE (audioMasterPinConnected)
MPEROLL_FST_REAL_OPCODE (audioMasterSetTime)                    MPEROLL_FST_REAL_OPCODE (audioMasterGetNumAutomatableParameters)
MPEROLL_FST_REAL_OPCODE (audioMasterGetParameterQuantization)   MPEROLL_FST_REAL_OPCODE (audioMasterIOChanged)
MPEROLL_FST_REAL_OPCODE (audioMasterNeedIdle)                   MPEROLL_FST_REAL_OPCODE (audioMasterGetInputLatency)
MPEROLL_FST_REAL_OPCODE (audioMasterGetOutputLatency)           MPEROLL_FST_REAL_OPCODE (audioMasterGetPreviousPlug)
MPEROLL_FST_REAL_OPCODE (audioMasterGetNextPlug)                MPEROLL_FST_REAL_OPCODE (audioMasterWillReplaceOrAccumulate)
MPEROLL_FST_REAL_OPCODE (audioMasterGetAutomationState)         MPEROLL_FST_REAL_OPCODE (audioMasterOfflineStart)
MPEROLL_FST_REAL_OPCODE (audioMasterOfflineRead)                MPEROLL_FST_REAL_OPCODE (audioMasterOfflineWrite)
MPEROLL_FST_REAL_OPCODE (audioMasterOfflineGetCurrentPass)      MPEROLL_FST_REAL_OPCODE (audioMasterOfflineGetCurrentMetaPass)
MPEROLL_FST_REAL_OPCODE (audioMasterSetOutputSampleRate)        MPEROLL_FST_REAL_OPCODE (audioMasterGetOutputSpeakerArrangement)
MPEROLL_FST_REAL_OPCODE (audioMasterVendorSpecific)             MPEROLL_FST_REAL_OPCODE (audioMasterSetIcon)
MPEROLL_FST_REAL_OPCODE (audioMasterGetLanguage)                MPEROLL_FST_REAL_OPCODE (audioMasterOpenWindow)
MPEROLL_FST_REAL_OPCODE (audioMasterCloseWindow)                MPEROLL_FST_REAL_OPCODE (audioMasterGetDirectory)
MPEROLL_FST_REAL_OPCODE (effConnectInput)                       MPEROLL_FST_REAL_OPCODE (effConnectOutput)
MPEROLL_FST_REAL_OPCODE (effEditTop)                            MPEROLL_FST_REAL_OPCODE (effSetBypass)
MPEROLL_FST_REAL_OPCODE (effGetTailSize)                        MPEROLL_FST_REAL_OPCODE (effIdle)
MPEROLL_FST_REAL_OPCODE (effKeysRequired)

#undef MPEROLL_FST_REAL_OPCODE

static_assert (sizeof (VstIntPtr) == sizeof (void*), "VST2 passes pointers in VstIntPtr");

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
