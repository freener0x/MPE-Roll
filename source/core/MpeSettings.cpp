#include "MpeSettings.h"

namespace mperoll
{

ZoneLayout ZoneLayout::normalised() const noexcept
{
    ZoneLayout result;
    result.lowerMembers = juce::jlimit (0, 15, lowerMembers);
    result.upperMembers = juce::jlimit (0, 15, upperMembers);

    if (result.lowerMembers > 0 && result.upperMembers > 0)
        result.upperMembers = juce::jmax (0, juce::jmin (result.upperMembers, 14 - result.lowerMembers));

    return result;
}

int ZoneLayout::zoneOfChannel (int channel) const noexcept
{
    if (lowerMembers > 0 && channel >= 1 && channel <= 1 + lowerMembers)
        return 0;

    if (upperMembers > 0 && channel <= 16 && channel >= 16 - upperMembers)
        return 1;

    return -1;
}

bool ZoneLayout::isMasterChannel (int channel) const noexcept
{
    return (channel == 1 && lowerMembers > 0) || (channel == 16 && upperMembers > 0);
}

bool ZoneLayout::isMemberChannel (int channel) const noexcept
{
    return zoneOfChannel (channel) >= 0 && ! isMasterChannel (channel);
}

int ZoneLayout::memberChannel (int zone, int index) const noexcept
{
    return zone == 0 ? 2 + index : 15 - index;
}

juce::String ZoneLayout::describeMemberChannels() const
{
    const auto zones = normalised();
    juce::StringArray ranges;

    auto range = [] (int first, int last)
    {
        return first == last ? juce::String (first) : juce::String (first) + "-" + juce::String (last);
    };

    if (zones.hasLowerZone())
        ranges.add (range (2, 1 + zones.lowerMembers));

    if (zones.hasUpperZone())
        ranges.add (range (16 - zones.upperMembers, 15));

    return ranges.joinIntoString (" and ");
}

//==============================================================================
namespace
{
    template <typename Enum>
    Enum enumFromInt (int value, Enum fallback, int numValues)
    {
        return juce::isPositiveAndBelow (value, numValues) ? (Enum) value : fallback;
    }
}

std::unique_ptr<juce::XmlElement> EngineSettings::toXml() const
{
    auto xml = std::make_unique<juce::XmlElement> ("EngineSettings");

    auto* out = xml->createNewChildElement ("Output");
    out->setAttribute ("mode", (int) output.mode);
    out->setAttribute ("lowerMembers", output.zones.lowerMembers);
    out->setAttribute ("upperMembers", output.zones.upperMembers);
    out->setAttribute ("splitKey", output.splitKey);
    out->setAttribute ("memberBendRange", output.memberBendRange);
    out->setAttribute ("masterBendRange", output.masterBendRange);
    out->setAttribute ("pressureTarget", (int) output.pressureTarget);
    out->setAttribute ("pressureController", output.pressureController);
    out->setAttribute ("timbreController", output.timbreController);
    out->setAttribute ("timbreLsbController", output.timbreLsbController);
    out->setAttribute ("mpePlus", output.mpePlus);
    out->setAttribute ("highResVelocity", output.highResVelocity);
    out->setAttribute ("sendConfiguration", output.sendConfiguration);
    out->setAttribute ("overflow", (int) output.overflow);
    out->setAttribute ("defaultTimbre", output.defaultTimbre);
    out->setAttribute ("singleChannel", output.singleChannel);
    out->setAttribute ("clapNoteExpressions", output.clapNoteExpressions);

    auto* in = xml->createNewChildElement ("Input");
    in->setAttribute ("mode", (int) input.mode);
    in->setAttribute ("lowerMembers", input.zones.lowerMembers);
    in->setAttribute ("upperMembers", input.zones.upperMembers);
    in->setAttribute ("followConfigurationMessages", input.followConfigurationMessages);
    in->setAttribute ("memberBendRange", input.memberBendRange);
    in->setAttribute ("masterBendRange", input.masterBendRange);
    in->setAttribute ("conventionalBendRange", input.conventionalBendRange);
    in->setAttribute ("timbreController", input.timbreController);
    in->setAttribute ("timbreLsbController", input.timbreLsbController);
    in->setAttribute ("mpePlus", input.mpePlus);
    in->setAttribute ("presetIndex", input.presetIndex);

    auto* play = xml->createNewChildElement ("Playback");
    play->setAttribute ("mode", (int) playback.mode);
    play->setAttribute ("triggerBaseKey", playback.triggerBaseKey);
    play->setAttribute ("chaseNotes", playback.chaseNotes);
    play->setAttribute ("controlIntervalMs", playback.controlIntervalMs);
    play->setAttribute ("passThroughInput", playback.passThroughInput);

    return xml;
}

EngineSettings EngineSettings::fromXml (const juce::XmlElement* xml)
{
    EngineSettings s;

    if (xml == nullptr)
        return s;

    if (auto* out = xml->getChildByName ("Output"))
    {
        auto& o = s.output;
        o.mode = enumFromInt (out->getIntAttribute ("mode"), o.mode, 2);
        o.zones.lowerMembers = out->getIntAttribute ("lowerMembers", o.zones.lowerMembers);
        o.zones.upperMembers = out->getIntAttribute ("upperMembers", o.zones.upperMembers);
        o.zones = o.zones.normalised();
        o.splitKey = juce::jlimit (0, 127, out->getIntAttribute ("splitKey", o.splitKey));
        o.memberBendRange = juce::jlimit (1.0f, 96.0f, (float) out->getDoubleAttribute ("memberBendRange", o.memberBendRange));
        o.masterBendRange = juce::jlimit (1.0f, 96.0f, (float) out->getDoubleAttribute ("masterBendRange", o.masterBendRange));
        o.pressureTarget = enumFromInt (out->getIntAttribute ("pressureTarget"), o.pressureTarget, 3);
        o.pressureController = juce::jlimit (0, 127, out->getIntAttribute ("pressureController", o.pressureController));
        o.timbreController = juce::jlimit (0, 127, out->getIntAttribute ("timbreController", o.timbreController));
        o.timbreLsbController = juce::jlimit (-1, 127, out->getIntAttribute ("timbreLsbController", o.timbreLsbController));
        o.mpePlus = out->getBoolAttribute ("mpePlus", o.mpePlus);
        o.highResVelocity = out->getBoolAttribute ("highResVelocity", o.highResVelocity);
        o.sendConfiguration = out->getBoolAttribute ("sendConfiguration", o.sendConfiguration);
        o.overflow = enumFromInt (out->getIntAttribute ("overflow"), o.overflow, 2);
        o.defaultTimbre = juce::jlimit (0.0f, 1.0f, (float) out->getDoubleAttribute ("defaultTimbre", o.defaultTimbre));
        o.singleChannel = juce::jlimit (1, 16, out->getIntAttribute ("singleChannel", o.singleChannel));
        o.clapNoteExpressions = out->getBoolAttribute ("clapNoteExpressions", o.clapNoteExpressions);
    }

    if (auto* in = xml->getChildByName ("Input"))
    {
        auto& i = s.input;
        i.mode = enumFromInt (in->getIntAttribute ("mode"), i.mode, 2);
        i.zones.lowerMembers = in->getIntAttribute ("lowerMembers", i.zones.lowerMembers);
        i.zones.upperMembers = in->getIntAttribute ("upperMembers", i.zones.upperMembers);
        i.zones = i.zones.normalised();
        i.followConfigurationMessages = in->getBoolAttribute ("followConfigurationMessages", i.followConfigurationMessages);
        i.memberBendRange = juce::jlimit (1.0f, 96.0f, (float) in->getDoubleAttribute ("memberBendRange", i.memberBendRange));
        i.masterBendRange = juce::jlimit (1.0f, 96.0f, (float) in->getDoubleAttribute ("masterBendRange", i.masterBendRange));
        i.conventionalBendRange = juce::jlimit (1.0f, 96.0f, (float) in->getDoubleAttribute ("conventionalBendRange", i.conventionalBendRange));
        i.timbreController = juce::jlimit (0, 127, in->getIntAttribute ("timbreController", i.timbreController));
        i.timbreLsbController = juce::jlimit (-1, 127, in->getIntAttribute ("timbreLsbController", i.timbreLsbController));
        i.mpePlus = in->getBoolAttribute ("mpePlus", i.mpePlus);
        i.presetIndex = in->getIntAttribute ("presetIndex", i.presetIndex);
    }

    if (auto* play = xml->getChildByName ("Playback"))
    {
        auto& p = s.playback;
        p.mode = enumFromInt (play->getIntAttribute ("mode"), p.mode, 2);
        p.triggerBaseKey = juce::jlimit (0, 127, play->getIntAttribute ("triggerBaseKey", p.triggerBaseKey));
        p.chaseNotes = play->getBoolAttribute ("chaseNotes", p.chaseNotes);
        p.controlIntervalMs = juce::jlimit (0.1f, 50.0f, (float) play->getDoubleAttribute ("controlIntervalMs", p.controlIntervalMs));
        p.passThroughInput = play->getBoolAttribute ("passThroughInput", p.passThroughInput);
    }

    return s;
}

//==============================================================================
float defaultControllerValue (int controller) noexcept
{
    switch (controller)
    {
        case 7:   return 100.0f / 127.0f;   // channel volume
        case 8:                              // balance
        case 10:                             // pan
        case 74:  return 0.5f;               // brightness / MPE timbre
        case 11:  return 1.0f;               // expression
        default:  return 0.0f;
    }
}

bool isReservedController (int controller) noexcept
{
    switch (controller)
    {
        case 0: case 32:                    // bank select
        case 6: case 38:                    // data entry
        case 88:                            // high resolution velocity prefix
        case 96: case 97:                   // data increment / decrement
        case 98: case 99: case 100: case 101: // NRPN / RPN select
            return true;

        default:
            return controller < 0 || controller >= 120;   // channel mode messages
    }
}

juce::String controllerName (int controller)
{
    switch (controller)
    {
        case 1:   return "Mod Wheel";
        case 2:   return "Breath";
        case 4:   return "Foot";
        case 5:   return "Portamento Time";
        case 7:   return "Volume";
        case 8:   return "Balance";
        case 10:  return "Pan";
        case 11:  return "Expression";
        case 12:  return "Effect 1";
        case 13:  return "Effect 2";
        case 64:  return "Sustain";
        case 65:  return "Portamento";
        case 66:  return "Sostenuto";
        case 67:  return "Soft Pedal";
        case 68:  return "Legato";
        case 69:  return "Hold 2";
        case 70:  return "Sound Variation";
        case 71:  return "Resonance";
        case 72:  return "Release Time";
        case 73:  return "Attack Time";
        case 74:  return "Brightness";
        case 75:  return "Decay Time";
        case 76:  return "Vibrato Rate";
        case 77:  return "Vibrato Depth";
        case 78:  return "Vibrato Delay";
        case 79:  return "Sound Ctrl 10";
        case 84:  return "Portamento Ctrl";
        case 91:  return "Reverb";
        case 92:  return "Tremolo";
        case 93:  return "Chorus";
        case 94:  return "Detune";
        case 95:  return "Phaser";
        default:  return "CC " + juce::String (controller);
    }
}

} // namespace mperoll
