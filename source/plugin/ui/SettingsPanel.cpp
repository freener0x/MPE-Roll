#include "SettingsPanel.h"
#include "MpeLookAndFeel.h"

namespace mperoll
{

namespace
{
    class ChoiceProp : public juce::ChoicePropertyComponent
    {
    public:
        ChoiceProp (const juce::String& name, const juce::StringArray& options,
                    std::function<int()> getterFn, std::function<void (int)> setterFn)
            : ChoicePropertyComponent (name), getter (std::move (getterFn)), setter (std::move (setterFn))
        {
            choices = options;
        }

        void setIndex (int index) override  { setter (index); }
        int getIndex() const override       { return getter(); }

    private:
        std::function<int()> getter;
        std::function<void (int)> setter;
    };

    class NumberProp : public juce::SliderPropertyComponent
    {
    public:
        NumberProp (const juce::String& name, double min, double max, double interval,
                    std::function<double()> getterFn, std::function<void (double)> setterFn)
            : SliderPropertyComponent (name, min, max, interval), getter (std::move (getterFn)), setter (std::move (setterFn))
        {
        }

        void setValue (double value) override   { setter (value); }
        double getValue() const override        { return getter(); }

    private:
        std::function<double()> getter;
        std::function<void (double)> setter;
    };

    class BoolProp : public juce::BooleanPropertyComponent
    {
    public:
        BoolProp (const juce::String& name, std::function<bool()> getterFn, std::function<void (bool)> setterFn)
            : BooleanPropertyComponent (name, "On", "Off"), getter (std::move (getterFn)), setter (std::move (setterFn))
        {
        }

        void setState (bool state) override     { setter (state); }
        bool getState() const override          { return getter(); }

    private:
        std::function<bool()> getter;
        std::function<void (bool)> setter;
    };

    class ActionProp : public juce::ButtonPropertyComponent
    {
    public:
        ActionProp (const juce::String& name, const juce::String& text, std::function<void()> actionFn)
            : ButtonPropertyComponent (name, false), buttonText (text), action (std::move (actionFn))
        {
            refresh();
        }

        void buttonClicked() override               { action(); }
        juce::String getButtonText() const override { return buttonText; }

    private:
        juce::String buttonText;
        std::function<void()> action;
    };
}

SettingsPanel::SettingsPanel (MpeRollProcessor& p)
    : processor (p), document (p.getDocument())
{
    auto settings = [this] { return document.getSettings(); };

    using Comps = juce::Array<juce::PropertyComponent*>;

    // Output to the synth.
    Comps output;
    output.add (new ChoiceProp ("Synth expects", { "MPE", "Plain MIDI (one channel)" },
        [settings] { return (int) settings().output.mode; },
        [this] (int i) { edit ([i] (EngineSettings& e) { e.output.mode = (OutputSettings::Mode) i; }); }));
    output.add (new NumberProp ("Lower zone channels", 0, 15, 1,
        [settings] { return (double) settings().output.zones.lowerMembers; },
        [this] (double v) { edit ([v] (EngineSettings& e) { e.output.zones.lowerMembers = (int) v; }); }));
    output.add (new NumberProp ("Upper zone channels", 0, 15, 1,
        [settings] { return (double) settings().output.zones.upperMembers; },
        [this] (double v) { edit ([v] (EngineSettings& e) { e.output.zones.upperMembers = (int) v; }); }));
    output.add (new NumberProp ("Zone split key", 0, 127, 1,
        [settings] { return (double) settings().output.splitKey; },
        [this] (double v) { edit ([v] (EngineSettings& e) { e.output.splitKey = (int) v; }); }));
    output.add (new NumberProp ("Per-note bend range", 1, 96, 1,
        [settings] { return (double) settings().output.memberBendRange; },
        [this] (double v) { edit ([v] (EngineSettings& e) { e.output.memberBendRange = (float) v; }); }));
    output.add (new NumberProp ("Zone bend range", 1, 96, 1,
        [settings] { return (double) settings().output.masterBendRange; },
        [this] (double v) { edit ([v] (EngineSettings& e) { e.output.masterBendRange = (float) v; }); }));
    output.add (new ChoiceProp ("Pressure sent as", { "Channel pressure", "Poly aftertouch", "Controller" },
        [settings] { return (int) settings().output.pressureTarget; },
        [this] (int i) { edit ([i] (EngineSettings& e) { e.output.pressureTarget = (OutputSettings::PressureTarget) i; }); }));
    output.add (new NumberProp ("Pressure controller", 0, 119, 1,
        [settings] { return (double) settings().output.pressureController; },
        [this] (double v) { edit ([v] (EngineSettings& e) { e.output.pressureController = (int) v; }); }));
    output.add (new NumberProp ("Timbre controller", 0, 119, 1,
        [settings] { return (double) settings().output.timbreController; },
        [this] (double v) { edit ([v] (EngineSettings& e) { e.output.timbreController = (int) v; }); }));
    output.add (new NumberProp ("Timbre LSB controller (-1 off)", -1, 119, 1,
        [settings] { return (double) settings().output.timbreLsbController; },
        [this] (double v) { edit ([v] (EngineSettings& e) { e.output.timbreLsbController = (int) v; }); }));
    output.add (new NumberProp ("Default timbre", 0, 127, 1,
        [settings] { return std::round (settings().output.defaultTimbre * 127.0); },
        [this] (double v) { edit ([v] (EngineSettings& e) { e.output.defaultTimbre = (float) (v / 127.0); }); }));
    output.add (new BoolProp ("14-bit velocity (CC88)",
        [settings] { return settings().output.highResVelocity; },
        [this] (bool b) { edit ([b] (EngineSettings& e) { e.output.highResVelocity = b; }); }));
    output.add (new ChoiceProp ("Out of channels", { "Share a channel", "Steal oldest note" },
        [settings] { return (int) settings().output.overflow; },
        [this] (int i) { edit ([i] (EngineSettings& e) { e.output.overflow = (OutputSettings::Overflow) i; }); }));
    output.add (new NumberProp ("Plain MIDI channel", 1, 16, 1,
        [settings] { return (double) settings().output.singleChannel; },
        [this] (double v) { edit ([v] (EngineSettings& e) { e.output.singleChannel = (int) v; }); }));
    output.add (new BoolProp ("Send MPE setup to synth",
        [settings] { return settings().output.sendConfiguration; },
        [this] (bool b) { edit ([b] (EngineSettings& e) { e.output.sendConfiguration = b; }); }));
    output.add (new ActionProp ("Synth setup", "Send MPE setup now", [this] { processor.requestConfiguration(); }));
    panel.addSection ("Output to synth", output);

    // Input from controllers, MIDI files and FL.
    Comps input;
    input.add (new ChoiceProp ("Input is", { "MPE", "Plain MIDI" },
        [settings] { return (int) settings().input.mode; },
        [this] (int i) { edit ([i] (EngineSettings& e) { e.input.mode = (InputSettings::Mode) i; }); }));
    input.add (new BoolProp ("Follow controller's MPE setup",
        [settings] { return settings().input.followConfigurationMessages; },
        [this] (bool b) { edit ([b] (EngineSettings& e) { e.input.followConfigurationMessages = b; }); }));
    input.add (new NumberProp ("Lower zone channels", 0, 15, 1,
        [settings] { return (double) settings().input.zones.lowerMembers; },
        [this] (double v) { edit ([v] (EngineSettings& e) { e.input.zones.lowerMembers = (int) v; }); }));
    input.add (new NumberProp ("Upper zone channels", 0, 15, 1,
        [settings] { return (double) settings().input.zones.upperMembers; },
        [this] (double v) { edit ([v] (EngineSettings& e) { e.input.zones.upperMembers = (int) v; }); }));
    input.add (new NumberProp ("Per-note bend range", 1, 96, 1,
        [settings] { return (double) settings().input.memberBendRange; },
        [this] (double v) { edit ([v] (EngineSettings& e) { e.input.memberBendRange = (float) v; }); }));
    input.add (new NumberProp ("Zone bend range", 1, 96, 1,
        [settings] { return (double) settings().input.masterBendRange; },
        [this] (double v) { edit ([v] (EngineSettings& e) { e.input.masterBendRange = (float) v; }); }));
    input.add (new NumberProp ("Timbre controller", 0, 119, 1,
        [settings] { return (double) settings().input.timbreController; },
        [this] (double v) { edit ([v] (EngineSettings& e) { e.input.timbreController = (int) v; }); }));
    input.add (new NumberProp ("Timbre LSB controller (-1 off)", -1, 119, 1,
        [settings] { return (double) settings().input.timbreLsbController; },
        [this] (double v) { edit ([v] (EngineSettings& e) { e.input.timbreLsbController = (int) v; }); }));
    panel.addSection ("Input from controller / FL", input);

    // Playback.
    Comps playback;
    playback.add (new BoolProp ("Play incoming MIDI",
        [settings] { return settings().playback.passThroughInput; },
        [this] (bool b) { edit ([b] (EngineSettings& e) { e.playback.passThroughInput = b; }); }));
    playback.add (new NumberProp ("Trigger key for clip 1 (MIDI)", 0, 112, 1,
        [settings] { return (double) settings().playback.triggerBaseKey; },
        [this] (double v) { edit ([v] (EngineSettings& e) { e.playback.triggerBaseKey = (int) v; }); }));
    playback.add (new BoolProp ("Chase notes",
        [settings] { return settings().playback.chaseNotes; },
        [this] (bool b) { edit ([b] (EngineSettings& e) { e.playback.chaseNotes = b; }); }));
    playback.add (new NumberProp ("Expression resolution (ms)", 0.1, 20.0, 0.1,
        [settings] { return (double) settings().playback.controlIntervalMs; },
        [this] (double v) { edit ([v] (EngineSettings& e) { e.playback.controlIntervalMs = (float) v; }); }));
    panel.addSection ("Playback", playback);

    addAndMakeVisible (panel);
    document.addChangeListener (this);
    setSize (460, 640);
}

SettingsPanel::~SettingsPanel()
{
    document.removeChangeListener (this);
}

void SettingsPanel::edit (const std::function<void (EngineSettings&)>& change)
{
    auto s = document.getSettings();
    change (s);
    document.setSettings (s);
}

void SettingsPanel::changeListenerCallback (juce::ChangeBroadcaster*)
{
    panel.refreshAll();
}

void SettingsPanel::paint (juce::Graphics& g)
{
    g.fillAll (Theme::panel);
}

void SettingsPanel::resized()
{
    panel.setBounds (getLocalBounds().reduced (4));
}

} // namespace mperoll
