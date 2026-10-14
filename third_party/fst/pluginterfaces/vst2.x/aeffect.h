// Maps the VST2 SDK include path that JUCE's VST2 hosting expects onto FST's clean-room headers.
#pragma once

#ifndef FST_QUIET
 #define FST_QUIET 1                    // FST's notes about undocumented symbols are for FST developers
#endif

#ifndef FST_DONT_DEPRECATE_UNKNOWN
 #define FST_DONT_DEPRECATE_UNKNOWN 1
#endif

#if defined (__GNUC__) || defined (__clang__)
 #pragma GCC diagnostic push
 #pragma GCC diagnostic ignored "-Wpedantic"
#endif

#include "../fst/aeffect.h"

#if defined (__GNUC__) || defined (__clang__)
 #pragma GCC diagnostic pop
#endif
