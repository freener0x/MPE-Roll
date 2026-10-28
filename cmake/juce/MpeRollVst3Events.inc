// MPE Roll VST3 events: begin
//==============================================================================
/*  Added by MPE Roll's cmake/PatchJuce.cmake (AGPLv3, like the rest of MPE Roll).

    JUCE builds a hosted VST3's event list from MIDI alone, so a host has no way to give the synth
    note IDs or note expressions. MPE Roll hands them over inside the MIDI buffer as SysEx under
    the non-commercial manufacturer ID, 0x7d, followed by "MRV"; this turns them back into VST3
    events. The layout is set out in MPE Roll's source/plugin/Vst3NoteEvents.h, every value in
    7-bit groups, most significant first:

        F0 7D 'M' 'R' 'V' 01 channel pitch velocity(2) noteId(4) F7          note on
        F0 7D 'M' 'R' 'V' 02 channel pitch velocity(2) noteId(4) F7          note off
        F0 7D 'M' 'R' 'V' 03 noteId(4) typeId(5) value(5) F7                 note expression value

    velocity is 0..16383 for 0..1, and value 0..0xffffffff for 0..1.
*/
struct MpeRollVst3Events
{
    static Optional<Steinberg::Vst::Event> decode (const uint8* data, int size) noexcept
    {
        if (data == nullptr || size < 7 || data[0] != 0xf0 || data[1] != 0x7d
            || data[2] != 'M' || data[3] != 'R' || data[4] != 'V' || data[size - 1] != 0xf7)
            return {};

        const auto read = [data] (int at, int bytes)
        {
            uint64 value = 0;

            for (int i = 0; i < bytes; ++i)
                value = (value << 7) | (uint64) (data[at + i] & 0x7f);

            return value;
        };

        Steinberg::Vst::Event e{};

        switch (data[5])
        {
            case 1:
            case 2:
            {
                if (size != 15)
                    return {};

                const auto channel  = (Steinberg::int16) (data[6] & 0x0f);
                const auto pitch    = (Steinberg::int16) (data[7] & 0x7f);
                const auto velocity = (float) read (8, 2) / 16383.0f;
                const auto noteId   = (Steinberg::int32) read (10, 4);

                if (data[5] == 1)
                {
                    e.type              = Steinberg::Vst::Event::kNoteOnEvent;
                    e.noteOn.channel    = channel;
                    e.noteOn.pitch      = pitch;
                    e.noteOn.velocity   = velocity;
                    e.noteOn.length     = 0;
                    e.noteOn.tuning     = 0.0f;
                    e.noteOn.noteId     = noteId;
                }
                else
                {
                    e.type              = Steinberg::Vst::Event::kNoteOffEvent;
                    e.noteOff.channel   = channel;
                    e.noteOff.pitch     = pitch;
                    e.noteOff.velocity  = velocity;
                    e.noteOff.tuning    = 0.0f;
                    e.noteOff.noteId    = noteId;
                }

                return e;
            }

            case 3:
            {
                if (size != 21)
                    return {};

                e.type                              = Steinberg::Vst::Event::kNoteExpressionValueEvent;
                e.noteExpressionValue.noteId        = (Steinberg::int32) read (6, 4);
                e.noteExpressionValue.typeId        = (Steinberg::Vst::NoteExpressionTypeID) read (10, 5);
                e.noteExpressionValue.value         = (double) read (15, 5) / 4294967295.0;
                return e;
            }

            default:
                return {};
        }
    }
};

// MPE Roll VST3 events: end
