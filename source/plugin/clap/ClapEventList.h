#pragma once

#include <clap/clap.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

namespace mperoll
{

/** A list of CLAP events that a plugin can read through clap_input_events.

    The storage is reserved up front and never grows, so filling it on the audio thread doesn't
    allocate. Events have to be added in time order, as CLAP requires.
*/
class ClapEventList
{
public:
    ClapEventList()
    {
        input.ctx = this;
        input.size = [] (const clap_input_events* list) -> uint32_t
        {
            return static_cast<const ClapEventList*> (list->ctx)->size();
        };
        input.get = [] (const clap_input_events* list, uint32_t index) -> const clap_event_header_t*
        {
            return static_cast<const ClapEventList*> (list->ctx)->get (index);
        };

        reserve (64 * 1024, 4096);
    }

    ClapEventList (const ClapEventList&) = delete;
    ClapEventList& operator= (const ClapEventList&) = delete;

    /** Not real-time safe: call when preparing. */
    void reserve (size_t bytes, size_t events)
    {
        storage.assign ((bytes + sizeof (uint64_t) - 1) / sizeof (uint64_t), 0);
        offsets.clear();
        offsets.reserve (events);
        used = 0;
    }

    void clear() noexcept
    {
        offsets.clear();    // keeps its capacity
        used = 0;
    }

    /** Copies an event (anything starting with a clap_event_header whose size is filled in).
        Returns false, and drops it, when the list is full.
    */
    bool push (const clap_event_header_t& event) noexcept
    {
        const auto words = ((size_t) event.size + sizeof (uint64_t) - 1) / sizeof (uint64_t);

        if (event.size < sizeof (clap_event_header_t) || used + words > storage.size()
            || offsets.size() >= offsets.capacity())
            return false;

        std::memcpy (storage.data() + used, &event, event.size);
        offsets.push_back ((uint32_t) used);
        used += words;
        return true;
    }

    template <typename Event>
    bool push (const Event& event) noexcept
    {
        static_assert (offsetof (Event, header) == 0);
        return push (event.header);
    }

    uint32_t size() const noexcept      { return (uint32_t) offsets.size(); }
    bool isEmpty() const noexcept       { return offsets.empty(); }

    const clap_event_header_t* get (uint32_t index) const noexcept
    {
        if (index >= offsets.size())
            return nullptr;

        return reinterpret_cast<const clap_event_header_t*> (storage.data() + offsets[index]);
    }

    const clap_input_events_t* getInputEvents() const noexcept     { return &input; }

private:
    std::vector<uint64_t> storage;      // 8-byte words keep every event's doubles aligned
    std::vector<uint32_t> offsets;      // in words
    size_t used = 0;
    clap_input_events_t input {};
};

/** Fills in a CLAP event header. */
template <typename Event>
inline clap_event_header_t makeClapHeader (uint16_t type, uint32_t time) noexcept
{
    clap_event_header_t header {};
    header.size = sizeof (Event);
    header.time = time;
    header.space_id = CLAP_CORE_EVENT_SPACE_ID;
    header.type = type;
    header.flags = 0;
    return header;
}

} // namespace mperoll
