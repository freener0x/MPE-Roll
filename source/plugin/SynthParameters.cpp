#include "SynthParameters.h"

#include <map>

namespace mperoll
{

namespace
{
    juce::String parameterIdOf (juce::AudioProcessorParameter& p)
    {
        if (auto* hosted = dynamic_cast<juce::HostedAudioProcessorParameter*> (&p))
            return hosted->getParameterID();

        return juce::String (p.getParameterIndex());
    }
}

//==============================================================================
SynthParameterProxy::SynthParameterProxy (SynthParameterBank& b, int s)
    : AudioProcessorParameterWithID (juce::ParameterID ("synth" + juce::String (s + 1), 1),
                                     "Synth " + juce::String (s + 1)),
      bank (b), slot (s)
{
}

float SynthParameterProxy::getValue() const         { return bank.getValue (slot); }
void SynthParameterProxy::setValue (float newValue) { bank.setValue (slot, newValue); }

float SynthParameterProxy::getDefaultValue() const
{
    float result = 0.0f;
    bank.synth.withParameter (bank.getHostedIndex (slot), [&] (juce::AudioProcessorParameter& p) { result = p.getDefaultValue(); });
    return result;
}

juce::String SynthParameterProxy::getName (int maximumStringLength) const
{
    juce::String result;

    if (! bank.synth.withParameter (bank.getHostedIndex (slot), [&] (juce::AudioProcessorParameter& p)
                                    { result = p.getName (maximumStringLength); }))
        result = "Synth " + juce::String (slot + 1) + " (unused)";

    return result.substring (0, maximumStringLength);
}

juce::String SynthParameterProxy::getLabel() const
{
    juce::String result;
    bank.synth.withParameter (bank.getHostedIndex (slot), [&] (juce::AudioProcessorParameter& p) { result = p.getLabel(); });
    return result;
}

juce::String SynthParameterProxy::getText (float value, int maximumStringLength) const
{
    juce::String result = juce::String (value, 3);
    bank.synth.withParameter (bank.getHostedIndex (slot), [&] (juce::AudioProcessorParameter& p)
                              { result = p.getText (value, maximumStringLength); });
    return result;
}

float SynthParameterProxy::getValueForText (const juce::String& text) const
{
    float result = text.getFloatValue();
    bank.synth.withParameter (bank.getHostedIndex (slot), [&] (juce::AudioProcessorParameter& p)
                              { result = p.getValueForText (text); });
    return result;
}

int SynthParameterProxy::getNumSteps() const
{
    int result = juce::AudioProcessor::getDefaultNumParameterSteps();
    bank.synth.withParameter (bank.getHostedIndex (slot), [&] (juce::AudioProcessorParameter& p) { result = p.getNumSteps(); });
    return result;
}

bool SynthParameterProxy::isDiscrete() const
{
    bool result = false;
    bank.synth.withParameter (bank.getHostedIndex (slot), [&] (juce::AudioProcessorParameter& p) { result = p.isDiscrete(); });
    return result;
}

bool SynthParameterProxy::isBoolean() const
{
    bool result = false;
    bank.synth.withParameter (bank.getHostedIndex (slot), [&] (juce::AudioProcessorParameter& p) { result = p.isBoolean(); });
    return result;
}

//==============================================================================
SynthParameterBank::SynthParameterBank (juce::AudioProcessor& o, HostedSynth& s)
    : owner (o), synth (s)
{
    for (int i = 0; i < numSlots; ++i)
    {
        slotToHosted[(size_t) i] = -1;
        cachedValues[(size_t) i] = 0.0f;

        auto proxy = std::make_unique<SynthParameterProxy> (*this, i);
        proxies[(size_t) i] = proxy.get();
        owner.addParameter (proxy.release());
    }
}

SynthParameterBank::~SynthParameterBank()
{
    detach();
}

int SynthParameterBank::getHostedIndex (int slot) const noexcept
{
    return juce::isPositiveAndBelow (slot, numSlots) ? slotToHosted[(size_t) slot].load() : -1;
}

float SynthParameterBank::getValue (int slot) const
{
    auto value = cachedValues[(size_t) slot].load();

    if (synth.withParameter (getHostedIndex (slot), [&] (juce::AudioProcessorParameter& p) { value = p.getValue(); }))
        cachedValues[(size_t) slot] = value;

    return value;
}

void SynthParameterBank::setValue (int slot, float value)
{
    cachedValues[(size_t) slot] = value;
    synth.withParameter (getHostedIndex (slot), [value] (juce::AudioProcessorParameter& p) { p.setValue (value); });
}

void SynthParameterBank::parameterValueChanged (int hostedIndex, float newValue)
{
    if (! juce::isPositiveAndBelow (hostedIndex, (int) hostedToSlot.size()))
        return;

    const auto slot = hostedToSlot[(size_t) hostedIndex];

    if (slot >= 0)
    {
        cachedValues[(size_t) slot] = newValue;
        proxies[(size_t) slot]->sendValueChangedMessageToListeners (newValue);
    }
}

void SynthParameterBank::parameterGestureChanged (int hostedIndex, bool gestureIsStarting)
{
    if (! juce::isPositiveAndBelow (hostedIndex, (int) hostedToSlot.size()))
        return;

    const auto slot = hostedToSlot[(size_t) hostedIndex];

    if (slot < 0)
        return;

    if (gestureIsStarting)
        proxies[(size_t) slot]->beginChangeGesture();
    else
        proxies[(size_t) slot]->endChangeGesture();
}

//==============================================================================
void SynthParameterBank::detach()
{
    if (listenedTo == nullptr)
        return;

    // removeListener waits for any callback in flight, so nothing reaches us after this.
    for (auto* p : listenedTo->getParameters())
        p->removeListener (this);

    listenedTo = nullptr;
}

void SynthParameterBank::synthRemoving (juce::AudioPluginInstance& outgoing)
{
    if (&outgoing == listenedTo)
    {
        detach();

        for (auto& s : slotToHosted)
            s = -1;

        numMapped = 0;
    }
}

void SynthParameterBank::synthChanged()
{
    detach();

    for (auto& s : slotToHosted)
        s = -1;

    hostedToSlot.clear();
    int mapped = 0;

    if (auto* instance = synth.getInstance())
    {
        const auto& params = instance->getParameters();
        hostedToSlot.assign ((size_t) params.size(), -1);

        // Only parameters a host could automate. JUCE-built synths, for instance, also list a bypass
        // and thousands of hidden MIDI CC parameters that would otherwise crowd out the real ones.
        auto eligible = [&] (int hosted)
        {
            auto* p = params.getUnchecked (hosted);
            return p->isAutomatable() && p != instance->getBypassParameter();
        };

        auto assign = [&] (int slot, int hosted)
        {
            slotToHosted[(size_t) slot] = hosted;
            hostedToSlot[(size_t) hosted] = slot;
            cachedValues[(size_t) slot] = params.getUnchecked (hosted)->getValue();
            ++mapped;
        };

        // Slots saved with the project follow their parameter IDs, in case the synth reordered them.
        if (! savedIds.isEmpty())
        {
            std::map<juce::String, int> byId;

            for (int i = 0; i < params.size(); ++i)
                byId.emplace (parameterIdOf (*params.getUnchecked (i)), i);

            for (int slot = 0; slot < juce::jmin (numSlots, savedIds.size()); ++slot)
            {
                const auto found = byId.find (savedIds[slot]);

                if (savedIds[slot].isNotEmpty() && found != byId.end() && hostedToSlot[(size_t) found->second] < 0
                    && eligible (found->second))
                    assign (slot, found->second);
            }

            savedIds.clear();
        }

        // Everything else in order, into the free slots.
        int nextSlot = 0;

        for (int hosted = 0; hosted < params.size(); ++hosted)
        {
            if (hostedToSlot[(size_t) hosted] >= 0 || ! eligible (hosted))
                continue;

            while (nextSlot < numSlots && slotToHosted[(size_t) nextSlot] >= 0)
                ++nextSlot;

            if (nextSlot >= numSlots)
                break;

            assign (nextSlot, hosted);
        }

        listenedTo = instance;

        for (auto* p : params)
            p->addListener (this);
    }

    numMapped = mapped;
    owner.updateHostDisplay (juce::AudioProcessor::ChangeDetails().withParameterInfoChanged (true));
}

//==============================================================================
std::unique_ptr<juce::XmlElement> SynthParameterBank::toXml() const
{
    auto xml = std::make_unique<juce::XmlElement> ("SynthParameters");
    auto* instance = synth.getInstance();

    if (instance == nullptr)
    {
        // The synth hasn't loaded (it may be missing on this computer): keep what the project had.
        for (int slot = 0; slot < savedIds.size(); ++slot)
        {
            if (savedIds[slot].isEmpty())
                continue;

            auto* child = xml->createNewChildElement ("Slot");
            child->setAttribute ("index", slot);
            child->setAttribute ("id", savedIds[slot]);
        }

        return xml;
    }

    const auto& params = instance->getParameters();

    for (int slot = 0; slot < numSlots; ++slot)
    {
        const auto hosted = slotToHosted[(size_t) slot].load();

        if (! juce::isPositiveAndBelow (hosted, params.size()))
            continue;

        auto* child = xml->createNewChildElement ("Slot");
        child->setAttribute ("index", slot);
        child->setAttribute ("id", parameterIdOf (*params.getUnchecked (hosted)));
    }

    return xml;
}

void SynthParameterBank::fromXml (const juce::XmlElement* xml)
{
    savedIds.clear();

    if (xml == nullptr)
        return;

    for (auto* child : xml->getChildWithTagNameIterator ("Slot"))
    {
        const auto slot = child->getIntAttribute ("index", -1);

        if (! juce::isPositiveAndBelow (slot, numSlots))
            continue;

        while (savedIds.size() <= slot)
            savedIds.add ({});

        savedIds.set (slot, child->getStringAttribute ("id"));
    }
}

} // namespace mperoll
