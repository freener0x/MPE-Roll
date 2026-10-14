#pragma once

#include "core/MpeClip.h"

#include <juce_audio_basics/juce_audio_basics.h>

#include <vector>

namespace mperoll::test
{

struct TimedMessage
{
    int sample = 0;
    juce::MidiMessage message;
};

inline std::vector<TimedMessage> collect (const juce::MidiBuffer& buffer)
{
    std::vector<TimedMessage> result;

    for (const auto metadata : buffer)
        result.push_back ({ metadata.samplePosition, metadata.getMessage() });

    return result;
}

inline int countIf (const std::vector<TimedMessage>& messages, const std::function<bool (const juce::MidiMessage&)>& predicate)
{
    int count = 0;

    for (const auto& m : messages)
        count += predicate (m.message) ? 1 : 0;

    return count;
}

inline MpeNote makeNote (int key, double start, double length, float strike = 0.8f)
{
    MpeNote n;
    n.key = key;
    n.start = start;
    n.length = length;
    n.strike = strike;
    return n;
}

} // namespace mperoll::test
