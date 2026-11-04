// SPDX-FileCopyrightText: 2026 The MPE Roll contributors
// SPDX-License-Identifier: AGPL-3.0-only

#include "SynthParameters.h"
#include "PluginLog.h"

#include <map>
#include <optional>

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

    /** The message thread, or a lock on it. */
    struct MessageThreadAccess
    {
        MessageThreadAccess()
        {
            if (! juce::MessageManager::existsAndIsCurrentThread())
                lock.emplace();
        }

        bool granted() const    { return ! lock.has_value() || lock->lockWasGained(); }

        std::optional<juce::MessageManagerLock> lock;
    };

    // Which synth a set of slots belongs to. Leaves out the file path, so moving the plugin, or
    // opening the project on another computer, keeps the slots.
    juce::String identityOf (const juce::PluginDescription& d)
    {
        return d.pluginFormatName + ":" + juce::String (d.uniqueId != 0 ? d.uniqueId : d.deprecatedUid);
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

// Text conversion can need the message thread: JUCE's VST3 host locks it, and CLAP only allows
// it there. A host asking from another thread therefore locks the message thread first, so the
// synth can't be swapped meanwhile and nothing waits on us while we wait for it.
juce::String SynthParameterProxy::getText (float value, int maximumStringLength) const
{
    juce::String result = juce::String (value, 3);
    const MessageThreadAccess access;

    if (access.granted())
        bank.synth.withParameter (bank.getHostedIndex (slot), [&] (juce::AudioProcessorParameter& p)
                                  { result = p.getText (value, maximumStringLength); });

    return result;
}

float SynthParameterProxy::getValueForText (const juce::String& text) const
{
    float result = text.getFloatValue();
    const MessageThreadAccess access;

    if (access.granted())
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
        gestureForwarded[(size_t) i] = false;

        auto proxy = std::make_unique<SynthParameterProxy> (*this, i);
        proxies[(size_t) i] = proxy.get();
        owner.addParameter (proxy.release());
    }

    resetDiagnostics();
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
    synth.setParameterValue (getHostedIndex (slot), value);     // and the same on every copy of it
}

void SynthParameterBank::parameterValueChanged (int hostedIndex, float newValue)
{
    if (! juce::isPositiveAndBelow (hostedIndex, (int) hostedToSlot.size()))
        return;

    const auto slot = hostedToSlot[(size_t) hostedIndex];

    if (slot >= 0)
    {
        cachedValues[(size_t) slot] = newValue;

        // The synth following the notes it's sent (its wheels, a parameter its MIDI mapping moves
        // with each channel's pitch bend, anything it reports while it plays a block with
        // expression in it, or anything its first copy moves while it plays in copy mode) isn't an
        // edit: reported to the host, it would fill its undo history, record as automation, and
        // come back to move every copy with the first one's note.
        if (! synth.isFollowingItsNotes (hostedIndex))
        {
            proxies[(size_t) slot]->sendValueChangedMessageToListeners (newValue);
            notePassed (hostedIndex);
        }
    }
}

void SynthParameterBank::parameterGestureChanged (int hostedIndex, bool gestureIsStarting)
{
    if (! juce::isPositiveAndBelow (hostedIndex, (int) hostedToSlot.size()))
        return;

    const auto slot = hostedToSlot[(size_t) hostedIndex];

    if (slot < 0)
        return;

    // A gesture around a change that follows the notes isn't passed on either: a host that keeps
    // its undo history by gestures (FL Studio does) would make an entry of each. One that began
    // before the parameter was known to follow the notes is still ended, so the host's begin and
    // end stay paired.
    auto& forwarded = gestureForwarded[(size_t) slot];

    if (gestureIsStarting)
    {
        if (synth.isFollowingItsNotes (hostedIndex))
            return;

        if (! forwarded.exchange (true))
            proxies[(size_t) slot]->beginChangeGesture();
    }
    else if (forwarded.exchange (false))
    {
        proxies[(size_t) slot]->endChangeGesture();
    }
}

//==============================================================================
void SynthParameterBank::notePassed (int hostedIndex) noexcept
{
    passedCount.fetch_add (1, std::memory_order_relaxed);

    // The first few distinct parameters, claimed first come first served; a full list drops the rest.
    for (auto& noted : passedIndices)
    {
        auto expected = -1;

        if (noted.load (std::memory_order_relaxed) == hostedIndex
            || noted.compare_exchange_strong (expected, hostedIndex, std::memory_order_relaxed))
            return;
    }
}

void SynthParameterBank::resetDiagnostics()
{
    for (auto& noted : passedIndices)
        noted.store (-1, std::memory_order_relaxed);

    passedCount.store (0, std::memory_order_relaxed);
    passedLogged = 0;
}

void SynthParameterBank::flushDiagnostics()
{
    // Once when the first changes have been passed on, and again when they've become a flood:
    // what a host's undo history fills with, named.
    const auto count = passedCount.load (std::memory_order_relaxed);

    if (count == 0 || (passedLogged > 0 && (count < 500 || passedLogged >= 500)))
        return;

    passedLogged = count;
    const auto* description = synth.getDescription();

    if (description == nullptr)
        return;

    juce::StringArray names;

    for (const auto& noted : passedIndices)
    {
        const auto index = noted.load (std::memory_order_relaxed);
        juce::String name;

        if (index >= 0 && synth.withParameter (index, [&] (juce::AudioProcessorParameter& p) { name = p.getName (64); }))
            names.add (name.quoted() + " (#" + juce::String (index + 1) + ")");
    }

    juce::String line;
    line << description->name << " (" << description->pluginFormatName << "): " << juce::String ((int) count)
         << " parameter change" << (count == 1 ? "" : "s") << " reported by the synth passed on to the host as edits";

    if (! names.isEmpty())
        line << ": " << names.joinIntoString (", ");

    line << ". A parameter that follows the notes should be told apart by its name or the synth's MIDI mapping.";
    PluginLog::add (line);
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

    // A gesture the synth began and never ended is ended for it, so the host's pairs stay even.
    for (int slot = 0; slot < numSlots; ++slot)
        if (gestureForwarded[(size_t) slot].exchange (false))
            proxies[(size_t) slot]->endChangeGesture();
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
    resetDiagnostics();

    for (auto& s : slotToHosted)
        s = -1;

    hostedToSlot.clear();
    int mapped = 0;

    auto* instance = synth.getInstance();
    const auto* description = synth.getDescription();

    if (instance != nullptr && description != nullptr)
    {
        const auto identity = identityOf (*description);

        // The slots were made for another synth: start afresh.
        if (slotSynth.isNotEmpty() && slotSynth != identity)
            slotIds.clear();

        slotSynth = identity;

        while (slotIds.size() < numSlots)
            slotIds.add ({});

        const auto& params = instance->getParameters();
        hostedToSlot.assign ((size_t) params.size(), -1);

        auto assign = [&] (int slot, int hosted)
        {
            slotToHosted[(size_t) slot] = hosted;
            hostedToSlot[(size_t) hosted] = slot;
            cachedValues[(size_t) slot] = params.getUnchecked (hosted)->getValue();
            ++mapped;
        };

        // Slots that already stand for a parameter follow its ID, wherever the synth now lists it.
        // A plugin may (wrongly) repeat an ID, so each ID lists every parameter that has it.
        std::map<juce::String, std::vector<int>> byId;

        for (int i = 0; i < params.size(); ++i)
            byId[parameterIdOf (*params.getUnchecked (i))].push_back (i);

        for (int slot = 0; slot < numSlots; ++slot)
        {
            if (slotIds[slot].isEmpty())
                continue;

            const auto found = byId.find (slotIds[slot]);

            if (found == byId.end())
                continue;   // the parameter has gone: keep the slot for it, unused

            for (auto hosted : found->second)
            {
                if (hostedToSlot[(size_t) hosted] < 0 && params.getUnchecked (hosted) != instance->getBypassParameter())
                {
                    assign (slot, hosted);
                    break;
                }
            }
        }

        // New parameters go into the free slots, in order. Only parameters a host could automate:
        // JUCE-built synths, for instance, also list a bypass and thousands of hidden MIDI CC
        // parameters that would otherwise crowd out the real ones.
        int nextSlot = 0;

        for (int hosted = 0; hosted < params.size(); ++hosted)
        {
            auto* p = params.getUnchecked (hosted);

            if (hostedToSlot[(size_t) hosted] >= 0 || ! p->isAutomatable() || p == instance->getBypassParameter())
                continue;

            while (nextSlot < numSlots && slotIds[nextSlot].isNotEmpty())
                ++nextSlot;

            if (nextSlot >= numSlots)
                break;

            slotIds.set (nextSlot, parameterIdOf (*p));
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

    if (slotSynth.isNotEmpty())
        xml->setAttribute ("synth", slotSynth);

    for (int slot = 0; slot < slotIds.size(); ++slot)
    {
        if (slotIds[slot].isEmpty())
            continue;

        auto* child = xml->createNewChildElement ("Slot");
        child->setAttribute ("index", slot);
        child->setAttribute ("id", slotIds[slot]);
    }

    return xml;
}

void SynthParameterBank::fromXml (const juce::XmlElement* xml)
{
    slotIds.clear();
    slotSynth.clear();

    if (xml == nullptr)
        return;

    slotSynth = xml->getStringAttribute ("synth");

    for (auto* child : xml->getChildWithTagNameIterator ("Slot"))
    {
        const auto slot = child->getIntAttribute ("index", -1);

        if (! juce::isPositiveAndBelow (slot, numSlots))
            continue;

        while (slotIds.size() <= slot)
            slotIds.add ({});

        slotIds.set (slot, child->getStringAttribute ("id"));
    }
}

} // namespace mperoll
