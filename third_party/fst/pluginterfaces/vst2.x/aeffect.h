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

#include "../fst/aeffect.h"

#if defined (__GNUC__) || defined (__clang__)
 #pragma GCC diagnostic pop
#elif defined (_MSC_VER)
 #pragma warning (pop)
#endif
