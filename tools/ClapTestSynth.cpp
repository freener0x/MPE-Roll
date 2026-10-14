// A minimal CLAP sine synth, written directly against the CLAP API. It's only used by the
// developer harness to check MPE Roll's CLAP hosting.
//
// It takes its pitch only from CLAP tuning note expressions and ignores MIDI, so a note that
// glides in a render proves MPE Roll turned its MPE into note expressions.

#include <clap/clap.h>

#include <array>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace
{

constexpr clap_id gainId = 1;
constexpr clap_id toneId = 2;
constexpr double twoPi = 6.283185307179586;

struct Voice
{
    bool active = false;
    int32_t noteId = -1;
    int16_t channel = 0, key = 0;
    double velocity = 0.0, tuning = 0.0, phase = 0.0;

    bool matches (const clap_event_note_t& e) const noexcept
    {
        return active && (e.note_id < 0 || e.note_id == noteId)
                      && (e.channel < 0 || e.channel == channel)
                      && (e.key < 0 || e.key == key);
    }

    bool matches (const clap_event_note_expression_t& e) const noexcept
    {
        return active && (e.note_id < 0 || e.note_id == noteId)
                      && (e.channel < 0 || e.channel == channel)
                      && (e.key < 0 || e.key == key);
    }
};

struct TestSynth
{
    clap_plugin_t plugin {};
    const clap_host_t* host = nullptr;
    double sampleRate = 44100.0;
    std::array<Voice, 16> voices {};
    std::atomic<double> gain { 1.0 }, tone { 0.5 };

    static TestSynth& of (const clap_plugin_t* p)       { return *static_cast<TestSynth*> (p->plugin_data); }

    void handleEvent (const clap_event_header_t& header, const clap_output_events_t* out)
    {
        if (header.space_id != CLAP_CORE_EVENT_SPACE_ID)
            return;

        switch (header.type)
        {
            case CLAP_EVENT_NOTE_ON:
            {
                const auto& e = reinterpret_cast<const clap_event_note_t&> (header);

                for (auto& v : voices)
                {
                    if (! v.active)
                    {
                        v = Voice {};
                        v.active = true;
                        v.noteId = e.note_id;
                        v.channel = e.channel;
                        v.key = e.key;
                        v.velocity = e.velocity;
                        break;
                    }
                }

                break;
            }

            case CLAP_EVENT_NOTE_OFF:
            case CLAP_EVENT_NOTE_CHOKE:
            {
                const auto& e = reinterpret_cast<const clap_event_note_t&> (header);

                for (auto& v : voices)
                {
                    if (! v.matches (e))
                        continue;

                    v.active = false;

                    if (out != nullptr)
                    {
                        clap_event_note_t end {};
                        end.header = { sizeof (end), header.time, CLAP_CORE_EVENT_SPACE_ID, CLAP_EVENT_NOTE_END, 0 };
                        end.note_id = v.noteId;
                        end.port_index = 0;
                        end.channel = v.channel;
                        end.key = v.key;
                        out->try_push (out, &end.header);
                    }
                }

                break;
            }

            case CLAP_EVENT_NOTE_EXPRESSION:
            {
                const auto& e = reinterpret_cast<const clap_event_note_expression_t&> (header);

                if (e.expression_id == CLAP_NOTE_EXPRESSION_TUNING)
                    for (auto& v : voices)
                        if (v.matches (e))
                            v.tuning = e.value;

                break;
            }

            case CLAP_EVENT_PARAM_VALUE:
            {
                const auto& e = reinterpret_cast<const clap_event_param_value_t&> (header);

                if (e.param_id == gainId)       gain = e.value;
                else if (e.param_id == toneId)  tone = e.value;

                break;
            }

            default:
                break;      // MIDI included: see the comment at the top
        }
    }

    void render (float* left, float* right, uint32_t from, uint32_t to)
    {
        const auto g = gain.load();

        for (auto& v : voices)
        {
            if (! v.active)
                continue;

            const auto frequency = 440.0 * std::pow (2.0, ((double) v.key + v.tuning - 69.0) / 12.0);
            const auto increment = twoPi * frequency / sampleRate;
            const auto level = (0.1 + 0.2 * v.velocity) * g;

            for (auto i = from; i < to; ++i)
            {
                const auto sample = (float) (std::sin (v.phase) * level);
                v.phase = std::fmod (v.phase + increment, twoPi);
                left[i] += sample;
                right[i] += sample;
            }
        }
    }
};

//==============================================================================
const char* const features[] = { CLAP_PLUGIN_FEATURE_INSTRUMENT, CLAP_PLUGIN_FEATURE_SYNTHESIZER,
                                 CLAP_PLUGIN_FEATURE_STEREO, nullptr };

const clap_plugin_descriptor_t descriptor
{
    CLAP_VERSION_INIT,
    "com.glowingpancake.mperoll-test-synth",
    "MPE Roll Test Synth",
    "Glowing Pancake",
    "https://github.com/freener0x/glowing-pancake",
    "",
    "",
    "0.1.0",
    "A sine synth that plays CLAP note expressions, for testing MPE Roll",
    features
};

const clap_plugin_audio_ports_t audioPorts
{
    [] (const clap_plugin_t*, bool isInput) -> uint32_t { return isInput ? 0 : 1; },
    [] (const clap_plugin_t*, uint32_t index, bool isInput, clap_audio_port_info_t* info)
    {
        if (isInput || index != 0)
            return false;

        info->id = 0;
        std::snprintf (info->name, sizeof (info->name), "%s", "Output");
        info->flags = CLAP_AUDIO_PORT_IS_MAIN;
        info->channel_count = 2;
        info->port_type = CLAP_PORT_STEREO;
        info->in_place_pair = CLAP_INVALID_ID;
        return true;
    }
};

const clap_plugin_note_ports_t notePorts
{
    [] (const clap_plugin_t*, bool isInput) -> uint32_t { return isInput ? 1 : 0; },
    [] (const clap_plugin_t*, uint32_t index, bool isInput, clap_note_port_info_t* info)
    {
        if (! isInput || index != 0)
            return false;

        info->id = 0;
        info->supported_dialects = CLAP_NOTE_DIALECT_CLAP | CLAP_NOTE_DIALECT_MIDI;
        info->preferred_dialect = CLAP_NOTE_DIALECT_CLAP;
        std::snprintf (info->name, sizeof (info->name), "%s", "Notes");
        return true;
    }
};

const clap_plugin_params_t params
{
    [] (const clap_plugin_t*) -> uint32_t { return 2; },
    [] (const clap_plugin_t*, uint32_t index, clap_param_info_t* info)
    {
        if (index > 1)
            return false;

        *info = {};
        info->id = index == 0 ? gainId : toneId;
        info->flags = CLAP_PARAM_IS_AUTOMATABLE;
        std::snprintf (info->name, sizeof (info->name), "%s", index == 0 ? "Gain" : "Tone");
        info->min_value = 0.0;
        info->max_value = 1.0;
        info->default_value = index == 0 ? 1.0 : 0.5;
        return true;
    },
    [] (const clap_plugin_t* p, clap_id id, double* value)
    {
        if (id != gainId && id != toneId)
            return false;

        *value = id == gainId ? TestSynth::of (p).gain.load() : TestSynth::of (p).tone.load();
        return true;
    },
    [] (const clap_plugin_t*, clap_id id, double value, char* text, uint32_t size)
    {
        if (id != gainId && id != toneId)
            return false;

        std::snprintf (text, size, "%d%%", (int) std::lround (value * 100.0));
        return true;
    },
    [] (const clap_plugin_t*, clap_id id, const char* text, double* value)
    {
        if (id != gainId && id != toneId)
            return false;

        *value = std::atof (text) / 100.0;
        return true;
    },
    [] (const clap_plugin_t* p, const clap_input_events_t* in, const clap_output_events_t* out)
    {
        for (uint32_t i = 0; i < in->size (in); ++i)
            TestSynth::of (p).handleEvent (*in->get (in, i), out);
    }
};

const clap_plugin_state_t state
{
    [] (const clap_plugin_t* p, const clap_ostream_t* stream)
    {
        const double values[] = { TestSynth::of (p).gain.load(), TestSynth::of (p).tone.load() };
        return stream->write (stream, values, sizeof (values)) == (int64_t) sizeof (values);
    },
    [] (const clap_plugin_t* p, const clap_istream_t* stream)
    {
        double values[2] = {};

        if (stream->read (stream, values, sizeof (values)) != (int64_t) sizeof (values))
            return false;

        TestSynth::of (p).gain = values[0];
        TestSynth::of (p).tone = values[1];
        return true;
    }
};

//==============================================================================
const clap_plugin_t* createPlugin (const clap_host_t* host)
{
    auto* instance = new TestSynth();
    instance->host = host;

    auto& p = instance->plugin;
    p.desc = &descriptor;
    p.plugin_data = instance;
    p.init = [] (const clap_plugin_t*) { return true; };
    p.destroy = [] (const clap_plugin_t* plugin) { delete &TestSynth::of (plugin); };
    p.activate = [] (const clap_plugin_t* plugin, double rate, uint32_t, uint32_t)
    {
        TestSynth::of (plugin).sampleRate = rate;
        return true;
    };
    p.deactivate = [] (const clap_plugin_t* plugin)
    {
        for (auto& v : TestSynth::of (plugin).voices)
            v.active = false;
    };
    p.start_processing = [] (const clap_plugin_t*) { return true; };
    p.stop_processing = [] (const clap_plugin_t*) {};
    p.reset = [] (const clap_plugin_t* plugin)
    {
        for (auto& v : TestSynth::of (plugin).voices)
            v.active = false;
    };
    p.process = [] (const clap_plugin_t* plugin, const clap_process_t* process) -> clap_process_status
    {
        auto& synth = TestSynth::of (plugin);

        if (process->audio_outputs_count < 1 || process->audio_outputs[0].channel_count < 2)
            return CLAP_PROCESS_ERROR;

        auto* left = process->audio_outputs[0].data32[0];
        auto* right = process->audio_outputs[0].data32[1];
        const auto frames = process->frames_count;

        std::memset (left, 0, frames * sizeof (float));
        std::memset (right, 0, frames * sizeof (float));

        // Render up to each event, then apply it: sample-accurate.
        const auto* in = process->in_events;
        uint32_t rendered = 0;

        for (uint32_t i = 0; i < in->size (in); ++i)
        {
            const auto* event = in->get (in, i);
            const auto time = event->time < frames ? event->time : frames;

            synth.render (left, right, rendered, time);
            rendered = time;
            synth.handleEvent (*event, process->out_events);
        }

        synth.render (left, right, rendered, frames);
        return CLAP_PROCESS_CONTINUE;
    };
    p.get_extension = [] (const clap_plugin_t*, const char* id) -> const void*
    {
        if (std::strcmp (id, CLAP_EXT_AUDIO_PORTS) == 0)    return &audioPorts;
        if (std::strcmp (id, CLAP_EXT_NOTE_PORTS) == 0)     return &notePorts;
        if (std::strcmp (id, CLAP_EXT_PARAMS) == 0)         return &params;
        if (std::strcmp (id, CLAP_EXT_STATE) == 0)          return &state;
        return nullptr;
    };
    p.on_main_thread = [] (const clap_plugin_t*) {};

    return &p;
}

const clap_plugin_factory_t factory
{
    [] (const clap_plugin_factory_t*) -> uint32_t { return 1; },
    [] (const clap_plugin_factory_t*, uint32_t index) -> const clap_plugin_descriptor_t*
    {
        return index == 0 ? &descriptor : nullptr;
    },
    [] (const clap_plugin_factory_t*, const clap_host_t* host, const char* id) -> const clap_plugin_t*
    {
        if (! clap_version_is_compatible (host->clap_version) || std::strcmp (id, descriptor.id) != 0)
            return nullptr;

        return createPlugin (host);
    }
};

} // namespace

extern "C" CLAP_EXPORT const clap_plugin_entry_t clap_entry
{
    CLAP_VERSION_INIT,
    [] (const char*) { return true; },
    [] () {},
    [] (const char* id) -> const void*
    {
        return std::strcmp (id, CLAP_PLUGIN_FACTORY_ID) == 0 ? &factory : nullptr;
    }
};
