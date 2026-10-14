#pragma once

#include "HostedSynth.h"

#include <array>
#include <atomic>

namespace mperoll
{

class SynthParameterBank;

/** One of MPE Roll's own parameters that stands in for a parameter of the hosted synth, so the
    host (FL Studio) can automate it. Names, values and text all come from the synth's parameter.
*/
class SynthParameterProxy : public juce::AudioProcessorParameterWithID
{
public:
    SynthParameterProxy (SynthParameterBank& bank, int slot);

    float getValue() const override;
    void setValue (float newValue) override;
    float getDefaultValue() const override;
    juce::String getName (int maximumStringLength) const override;
    juce::String getLabel() const override;
    juce::String getText (float value, int maximumStringLength) const override;
    float getValueForText (const juce::String& text) const override;
    int getNumSteps() const override;
    bool isDiscrete() const override;
    bool isBoolean() const override;

private:
    SynthParameterBank& bank;
    const int slot;
};

/** Mirrors the hosted synth's parameters as MPE Roll parameters.

    Slot n stands for the synth's n-th parameter. When the synth's own window changes a parameter,
    the matching slot tells the host, so "last tweaked" automation works. The parameter IDs are
    saved with the project; if a synth update reorders its parameters, slots follow the IDs.
*/
class SynthParameterBank : private juce::AudioProcessorParameter::Listener
{
public:
    static constexpr int numSlots = 1024;

    SynthParameterBank (juce::AudioProcessor& owner, HostedSynth& synth);
    ~SynthParameterBank() override;

    /** Call on the message thread after the synth was loaded or unloaded. */
    void synthChanged();

    /** Call on the message thread before the synth instance is deleted. */
    void synthRemoving (juce::AudioPluginInstance& outgoing);

    std::unique_ptr<juce::XmlElement> toXml() const;
    void fromXml (const juce::XmlElement* xml);

    int getNumMappedSlots() const noexcept              { return numMapped.load(); }
    int getHostedIndex (int slot) const noexcept;

private:
    friend class SynthParameterProxy;

    juce::AudioProcessor& owner;
    HostedSynth& synth;
    std::array<SynthParameterProxy*, numSlots> proxies {};
    std::array<std::atomic<int>, numSlots> slotToHosted;
    mutable std::array<std::atomic<float>, numSlots> cachedValues;
    std::vector<int> hostedToSlot;      // only touched on the message thread, or with no listeners attached
    std::atomic<int> numMapped { 0 };
    juce::StringArray savedIds;         // parameter IDs from the project, applied when the synth arrives
    juce::AudioPluginInstance* listenedTo = nullptr;

    void parameterValueChanged (int hostedIndex, float newValue) override;
    void parameterGestureChanged (int hostedIndex, bool gestureIsStarting) override;

    float getValue (int slot) const;
    void setValue (int slot, float value);
    juce::AudioProcessorParameter* hostedParameter (int slot) const;   // message thread only
    void detach();
};

} // namespace mperoll
