#pragma once

#include "core/MpeClip.h"
#include "core/MpeSettings.h"

#include <juce_data_structures/juce_data_structures.h>
#include <juce_events/juce_events.h>

#include <array>
#include <functional>
#include <memory>
#include <set>

namespace mperoll
{

static constexpr int numClipSlots = 16;

/** An immutable copy of everything the audio thread needs. Published by ClipDocument. */
struct EngineSnapshot
{
    std::array<std::shared_ptr<const MpeClip>, numClipSlots> clips;
    EngineSettings settings;
    int editSlot = 0;
    uint64_t version = 0;
};

/** The editable model behind the piano roll. Lives on the message thread.

    Edits are made in gestures: beginEdit() remembers the clip, editClip() hands out the working
    copy, changed() publishes it to the audio thread (so you hear edits while dragging) and
    endEdit() records a single undo step.
*/
class ClipDocument : public juce::ChangeBroadcaster
{
public:
    using Publisher = std::function<void (std::shared_ptr<const EngineSnapshot>)>;

    explicit ClipDocument (Publisher publisher);
    ~ClipDocument() override;

    //==============================================================================
    const MpeClip& getClip (int slot) const;
    const MpeClip& getEditClip() const                  { return getClip (editSlot); }
    int getEditSlot() const noexcept                    { return editSlot; }
    void setEditSlot (int slot);

    const EngineSettings& getSettings() const noexcept  { return settings; }
    void setSettings (const EngineSettings& newSettings);

    //==============================================================================
    void beginEdit (const juce::String& actionName);
    MpeClip& editClip();
    void changed();
    void endEdit();
    bool isInGesture() const noexcept                   { return gesture != nullptr; }

    /** A complete undoable edit in one call. */
    void perform (const juce::String& actionName, const std::function<void (MpeClip&)>& edit);

    /** Replaces a slot's clip (import, recording...) as one undo step. */
    void replaceClip (int slot, MpeClip newClip, const juce::String& actionName);

    juce::UndoManager& getUndoManager() noexcept        { return undoManager; }

    //==============================================================================
    /** Selected note ids in the edit clip. Not part of the undo history. */
    std::set<uint32_t> selection;
    void selectionChanged()                             { sendChangeMessage(); }

    //==============================================================================
    std::unique_ptr<juce::XmlElement> toXml() const;
    void fromXml (const juce::XmlElement& xml);

    /** Builds the XML for a snapshot, so state can be saved from any thread. */
    static std::unique_ptr<juce::XmlElement> snapshotToXml (const EngineSnapshot& snapshot);

    std::shared_ptr<const EngineSnapshot> getLatestSnapshot() const { return latest; }

private:
    class ClipAction;

    Publisher publish;
    std::array<MpeClip, numClipSlots> clips;
    std::array<std::shared_ptr<const MpeClip>, numClipSlots> published;
    EngineSettings settings;
    int editSlot = 0;
    uint64_t version = 0;
    std::shared_ptr<const EngineSnapshot> latest;
    juce::UndoManager undoManager { 4'000'000, 100 };

    struct Gesture
    {
        juce::String name;
        int slot = 0;
        MpeClip before;
    };

    std::unique_ptr<Gesture> gesture;

    void setClipInternal (int slot, const MpeClip& clip);
    void republish (int changedSlot);
};

} // namespace mperoll
