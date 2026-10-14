#include "ClipDocument.h"

namespace mperoll
{

class ClipDocument::ClipAction : public juce::UndoableAction
{
public:
    ClipAction (ClipDocument& d, int s, MpeClip b, MpeClip a)
        : document (d), slot (s), before (std::move (b)), after (std::move (a)) {}

    bool perform() override     { document.setClipInternal (slot, after); return true; }
    bool undo() override        { document.setClipInternal (slot, before); return true; }

    int getSizeInUnits() override
    {
        return 16 + (int) (before.notes.size() + after.notes.size()) * 8;
    }

private:
    ClipDocument& document;
    int slot;
    MpeClip before, after;
};

//==============================================================================
namespace
{
    MpeClip makeDefaultClip (int slot)
    {
        MpeClip clip;
        clip.name = "Clip " + juce::String (slot + 1);
        clip.length = 16.0;
        clip.looping = true;
        return clip;
    }
}

ClipDocument::ClipDocument (Publisher publisher)
    : publish (std::move (publisher))
{
    for (int i = 0; i < numClipSlots; ++i)
    {
        clips[(size_t) i] = makeDefaultClip (i);
        published[(size_t) i] = std::make_shared<const MpeClip> (clips[(size_t) i]);
    }

    republish (-1);
}

ClipDocument::~ClipDocument() = default;

const MpeClip& ClipDocument::getClip (int slot) const
{
    return clips[(size_t) juce::jlimit (0, numClipSlots - 1, slot)];
}

void ClipDocument::setEditSlot (int slot)
{
    slot = juce::jlimit (0, numClipSlots - 1, slot);

    if (slot == editSlot)
        return;

    endEdit();
    editSlot = slot;
    selection.clear();
    republish (-1);
    sendChangeMessage();
}

void ClipDocument::setSettings (const EngineSettings& newSettings)
{
    auto s = newSettings;
    s.output.zones = s.output.zones.normalised();
    s.input.zones = s.input.zones.normalised();

    if (s == settings)
        return;

    settings = s;
    republish (-1);
    sendChangeMessage();
}

//==============================================================================
void ClipDocument::beginEdit (const juce::String& actionName)
{
    endEdit();
    gesture = std::make_unique<Gesture> (Gesture { actionName, editSlot, clips[(size_t) editSlot] });
}

MpeClip& ClipDocument::editClip()
{
    return clips[(size_t) (gesture != nullptr ? gesture->slot : editSlot)];
}

void ClipDocument::changed()
{
    republish (gesture != nullptr ? gesture->slot : editSlot);
    sendChangeMessage();
}

void ClipDocument::endEdit()
{
    if (gesture == nullptr)
        return;

    auto g = std::move (gesture);
    auto& current = clips[(size_t) g->slot];
    current.sortNotes();

    if (current == g->before)
        return;

    undoManager.beginNewTransaction (g->name);
    undoManager.perform (new ClipAction (*this, g->slot, std::move (g->before), current));
}

void ClipDocument::perform (const juce::String& actionName, const std::function<void (MpeClip&)>& edit)
{
    beginEdit (actionName);
    edit (editClip());
    editClip().sortNotes();
    changed();
    endEdit();
}

void ClipDocument::replaceClip (int slot, MpeClip newClip, const juce::String& actionName)
{
    endEdit();
    slot = juce::jlimit (0, numClipSlots - 1, slot);
    newClip.sortNotes();

    undoManager.beginNewTransaction (actionName);
    undoManager.perform (new ClipAction (*this, slot, clips[(size_t) slot], std::move (newClip)));
}

void ClipDocument::setClipInternal (int slot, const MpeClip& clip)
{
    clips[(size_t) slot] = clip;

    if (slot == editSlot)
    {
        for (auto it = selection.begin(); it != selection.end();)
            it = clip.findNote (*it) == nullptr ? selection.erase (it) : std::next (it);
    }

    republish (slot);
    sendChangeMessage();
}

void ClipDocument::republish (int changedSlot)
{
    if (juce::isPositiveAndBelow (changedSlot, numClipSlots))
        published[(size_t) changedSlot] = std::make_shared<const MpeClip> (clips[(size_t) changedSlot]);

    auto snapshot = std::make_shared<EngineSnapshot>();
    snapshot->clips = published;
    snapshot->settings = settings;
    snapshot->editSlot = editSlot;
    snapshot->version = ++version;
    latest = snapshot;

    if (publish)
        publish (snapshot);
}

//==============================================================================
std::unique_ptr<juce::XmlElement> ClipDocument::snapshotToXml (const EngineSnapshot& snapshot)
{
    auto xml = std::make_unique<juce::XmlElement> ("Document");
    xml->setAttribute ("editSlot", snapshot.editSlot);
    xml->addChildElement (snapshot.settings.toXml().release());

    for (int i = 0; i < numClipSlots; ++i)
    {
        if (snapshot.clips[(size_t) i] == nullptr)
            continue;

        auto* clipXml = xml->createNewChildElement ("Clip");
        clipXml->setAttribute ("slot", i);
        clipXml->setAttribute ("data", snapshot.clips[(size_t) i]->toMemoryBlock().toBase64Encoding());
    }

    return xml;
}

std::unique_ptr<juce::XmlElement> ClipDocument::toXml() const
{
    return snapshotToXml (*latest);
}

void ClipDocument::fromXml (const juce::XmlElement& xml)
{
    gesture.reset();

    settings = EngineSettings::fromXml (xml.getChildByName ("EngineSettings"));

    for (int i = 0; i < numClipSlots; ++i)
        clips[(size_t) i] = makeDefaultClip (i);

    for (auto* clipXml : xml.getChildWithTagNameIterator ("Clip"))
    {
        const auto slot = clipXml->getIntAttribute ("slot", -1);

        if (! juce::isPositiveAndBelow (slot, numClipSlots))
            continue;

        juce::MemoryBlock data;

        if (data.fromBase64Encoding (clipXml->getStringAttribute ("data")))
        {
            MpeClip clip;

            if (MpeClip::fromMemoryBlock (data, clip))
                clips[(size_t) slot] = std::move (clip);
        }
    }

    editSlot = juce::jlimit (0, numClipSlots - 1, xml.getIntAttribute ("editSlot", 0));

    for (int i = 0; i < numClipSlots; ++i)
        published[(size_t) i] = std::make_shared<const MpeClip> (clips[(size_t) i]);

    selection.clear();
    undoManager.clearUndoHistory();
    republish (-1);
    sendChangeMessage();
}

} // namespace mperoll
