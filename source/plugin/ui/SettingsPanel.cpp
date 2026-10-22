#include "SettingsPanel.h"
#include "MpeLookAndFeel.h"
#include "PropertyFields.h"
#include "WindowDrawing.h"
#include "core/ControllerPresets.h"
#include "plugin/Links.h"

namespace mperoll
{

namespace
{
    /** A colour picker that changes one of the skin's colours as it moves. It owns its link to the
        skin, so it can outlive the settings panel that opened it.
    */
    class SkinColourPicker : public juce::ColourSelector,
                             private juce::ChangeListener
    {
    public:
        explicit SkinColourPicker (SkinRole r)
            : ColourSelector (ColourSelector::showColourAtTop | ColourSelector::showSliders | ColourSelector::showColourspace),
              role (r)
        {
            setCurrentColour (skins->getColour (role), juce::dontSendNotification);
            addChangeListener (this);
            setSize (300, 320);
        }

        ~SkinColourPicker() override
        {
            removeChangeListener (this);
        }

    private:
        void changeListenerCallback (juce::ChangeBroadcaster*) override
        {
            skins->setColour (role, getCurrentColour());
        }

        juce::SharedResourcePointer<SkinManager> skins;
        SkinRole role;
    };

    /** One of the skin's colours: a swatch that opens a picker. */
    class ColourProp : public juce::PropertyComponent
    {
    public:
        ColourProp (SkinManager& s, SkinRole r) : PropertyComponent (skinRoleName (r), 26), skins (s), role (r)
        {
            swatch.onClick = [this] { pick(); };
            swatch.setTooltip ("Pick a colour");
            addAndMakeVisible (swatch);
            refresh();
        }

        void refresh() override
        {
            const auto colour = skins.getColour (role);
            swatch.setColour (juce::TextButton::buttonColourId, colour);
            swatch.setColour (juce::TextButton::textColourOffId, colour.contrasting (0.8f));
            swatch.setButtonText ("#" + colour.toDisplayString (false));
        }

    private:
        SkinManager& skins;
        SkinRole role;
        juce::TextButton swatch;

        void pick()
        {
            auto* top = getTopLevelComponent();
            juce::CallOutBox::launchAsynchronously (std::make_unique<SkinColourPicker> (role),
                                                    top->getLocalArea (&swatch, swatch.getLocalBounds()), top);
        }
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
    output.add (new InfoProp ("Synth is played with", [this, settings]
                              { return processor.getSynth().describeConnection (settings().output); }));
   #if MPEROLL_HOST_CLAP
    output.add (new BoolProp ("CLAP note expressions",
        [settings] { return settings().output.clapNoteExpressions; },
        [this] (bool b) { edit ([b] (EngineSettings& e) { e.output.clapNoteExpressions = b; }); }));
   #endif
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
    output.add (new BoolProp ("MPE+ high resolution (CC87)",
        [settings] { return settings().output.mpePlus; },
        [this] (bool b) { edit ([b] (EngineSettings& e) { e.output.mpePlus = b; }); }));
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
    juce::StringArray presetNames;

    for (const auto& preset : controllerPresets())
        presetNames.add (preset.name);

    presetNames.add ("Custom");

    input.add (new ChoiceProp ("Controller", presetNames,
        [settings]
        {
            const auto index = findControllerPreset (settings().input);
            return index >= 0 ? index : (int) controllerPresets().size();
        },
        [this] (int i)
        {
            if (juce::isPositiveAndBelow (i, (int) controllerPresets().size()))
                edit ([i] (EngineSettings& e) { e.input = controllerPresets()[(size_t) i].input; });
        }));
    input.add (new InfoProp ("Setup", [settings]
    {
        const auto index = findControllerPreset (settings().input);
        return index >= 0 ? controllerPresets()[(size_t) index].setupTip
                          : juce::String ("Custom settings. Pick a controller above to start from its defaults.");
    }));
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
    input.add (new BoolProp ("MPE+ high resolution (CC87)",
        [settings] { return settings().input.mpePlus; },
        [this] (bool b) { edit ([b] (EngineSettings& e) { e.input.mpePlus = b; }); }));
    input.add (new NumberProp ("Plain MIDI bend range", 1, 96, 1,
        [settings] { return (double) settings().input.conventionalBendRange; },
        [this] (double v) { edit ([v] (EngineSettings& e) { e.input.conventionalBendRange = (float) v; }); }));
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

    // The editor's window. Kept with the computer's MPE Roll settings, which every MPE Roll in the
    // process shares, rather than the project.
    Comps window;
    auto userSettings = [this] { return processor.getSynth().getUserSettings(); };

    if (WindowDrawing::isChoiceAvailable())
        window.add (new ChoiceProp ("Window drawing", WindowDrawing::modeNames(),
            [userSettings] { return (int) WindowDrawing::getMode (userSettings()); },
            [this, userSettings] (int i)
            {
                WindowDrawing::setMode (userSettings(), (WindowDrawing::Mode) i);
                WindowDrawing::apply (*this, (WindowDrawing::Mode) i);
                panel.refreshAll();
            }));

    window.add (new InfoProp ("Window", [this] { return WindowDrawing::describe (*this); }));
    panel.addSection ("Window", window, false);

    // The look: a skin, the user's own colours on top of it, and a picture behind the grid. Kept for
    // every MPE Roll on this computer.
    Comps look;
    juce::StringArray skinNames;

    for (const auto& s : presetSkins())
        skinNames.add (s.name);

    look.add (new ChoiceProp ("Skin", skinNames,
        [this] { return skins->getPreset(); },
        [this] (int i) { skins->setPreset (i); }));
    look.add (new InfoProp ("Colours", [this]
    {
        return skins->hasCustomColours() ? juce::String ("Some are your own (see Colours below).")
                                         : juce::String ("The skin's own. Change any of them under Colours below.");
    }));
    look.add (new ActionProp ("Your colours", "Back to the skin's colours", [this] { skins->resetColours(); }));
    look.add (new InfoProp ("Background picture", [this]
    {
        return skins->hasBackground() ? skins->getBackgroundFile().getFileName()
                                      : juce::String ("None. Pick one to show it behind the grid.");
    }));
    look.add (new ActionProp ("Picture", "Choose a picture...", [this] { skins->chooseBackground(); }));
    look.add (new ActionProp ("Remove picture", "Remove the picture", [this] { skins->clearBackground(); }));
    look.add (new NumberProp ("Picture strength (%)", 0, 100, 1,
        [this] { return std::round (skins->getBackgroundStrength() * 100.0); },
        [this] (double v) { skins->setBackgroundStrength ((float) (v / 100.0)); }));
    panel.addSection ("Look", look, false);

    Comps colours;

    for (int role = 0; role < numSkinRoles; ++role)
        colours.add (new ColourProp (skins.getObject(), (SkinRole) role));

    panel.addSection ("Colours", colours, false);

    addAndMakeVisible (panel);

    // MPE Roll is free; the donation page is a click away, below the settings.
    supportLink.setButtonText ("Support MPE Roll (donate)");
    supportLink.setURL (juce::URL (links::donate));
    supportLink.setTooltip ("Opens the donation page in your browser: Cash App, or crypto by QR code");
    supportLink.setJustificationType (juce::Justification::centredLeft);
    supportLink.setColour (juce::HyperlinkButton::textColourId, Theme::glide);
    addAndMakeVisible (supportLink);

    document.addChangeListener (this);
    skins->addChangeListener (this);
    setSize (460, 640);
}

SettingsPanel::~SettingsPanel()
{
    if (! detached)
    {
        skins->removeChangeListener (this);
        document.removeChangeListener (this);
    }
}

void SettingsPanel::detach()
{
    if (detached)
        return;

    detached = true;
    skins->removeChangeListener (this);
    document.removeChangeListener (this);
    panel.clear();      // and with the properties go their callbacks into the processor and document
}

void SettingsPanel::parentHierarchyChanged()
{
    // The Window line can only say how the window is drawn once the panel is in one.
    if (getPeer() != nullptr)
        panel.refreshAll();
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
    supportLink.setColour (juce::HyperlinkButton::textColourId, Theme::glide);   // the skin may have changed
    repaint();
}

void SettingsPanel::paint (juce::Graphics& g)
{
    g.fillAll (Theme::panel);
}

void SettingsPanel::resized()
{
    auto area = getLocalBounds().reduced (4);
    supportLink.setBounds (area.removeFromBottom (24).withTrimmedLeft (4));
    panel.setBounds (area);
}

} // namespace mperoll
