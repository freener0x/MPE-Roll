// SPDX-FileCopyrightText: 2026 The MPE Roll contributors
// SPDX-License-Identifier: AGPL-3.0-only

#include "SettingsPanel.h"
#include "MpeLookAndFeel.h"
#include "PropertyFields.h"
#include "WindowDrawing.h"
#include "core/ControllerPresets.h"
#include "DonatePanel.h"
#include "plugin/BuildInfo.h"
#include "plugin/Links.h"
#include "plugin/UpdateChecker.h"

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
        ActionProp (const juce::String& name, const juce::String& buttonText, std::function<void()> actionFn)
            : ActionProp (name, [buttonText] { return buttonText; }, {}, std::move (actionFn))
        {
        }

        /** A button whose words, and whether it can be pressed, follow what it acts on. */
        ActionProp (const juce::String& name, std::function<juce::String()> textFn, std::function<bool()> enabledFn,
                    std::function<void()> actionFn)
            : ButtonPropertyComponent (name, false), text (std::move (textFn)), enabled (std::move (enabledFn)),
              action (std::move (actionFn))
        {
            refresh();
        }

        void refresh() override
        {
            if (enabled != nullptr)
                setEnabled (enabled());

            ButtonPropertyComponent::refresh();
        }

        void buttonClicked() override               { action(); }
        juce::String getButtonText() const override { return text(); }

    private:
        std::function<juce::String()> text;
        std::function<bool()> enabled;
        std::function<void()> action;
    };

    /** A button for the newer release the update check knows of, which acts on the release it shows:
        another MPE Roll may have found a newer one since the row was drawn, and Skip version 0.2.0
        must never skip 0.2.1. */
    class ReleaseProp : public juce::ButtonPropertyComponent
    {
    public:
        using Known = std::optional<UpdateChecker::KnownRelease>;

        ReleaseProp (const juce::String& name, std::function<Known()> findFn, std::function<juce::String (const Known&)> textFn,
                     std::function<bool (const Known&)> enabledFn, std::function<void (const UpdateChecker::KnownRelease&)> actionFn)
            : ButtonPropertyComponent (name, false), find (std::move (findFn)), text (std::move (textFn)),
              enabled (std::move (enabledFn)), action (std::move (actionFn))
        {
            refresh();
        }

        void refresh() override
        {
            shown = find();
            setEnabled (enabled (shown));
            ButtonPropertyComponent::refresh();
        }

        void buttonClicked() override
        {
            if (shown.has_value() && enabled (shown))
            {
                const juce::Component::SafePointer<ReleaseProp> safe (this);
                action (*shown);

                if (safe != nullptr)
                    refresh();
            }
        }

        juce::String getButtonText() const override     { return text (shown); }

    private:
        std::function<Known()> find;
        std::function<juce::String (const Known&)> text;
        std::function<bool (const Known&)> enabled;
        std::function<void (const UpdateChecker::KnownRelease&)> action;
        Known shown;
    };

    /** A line of text to type in, taken when Return is pressed or the field loses focus. */
    class TextProp : public juce::TextPropertyComponent
    {
    public:
        TextProp (const juce::String& name, std::function<juce::String()> getterFn, std::function<void (const juce::String&)> setterFn)
            : TextPropertyComponent (name, 200, false), getter (std::move (getterFn)), setter (std::move (setterFn))
        {
            refresh();
        }

        // Shows what was taken: text that comes to the setting as it was isn't sent back otherwise.
        void setText (const juce::String& newText) override     { setter (newText); refresh(); }
        juce::String getText() const override                   { return getter(); }

    private:
        std::function<juce::String()> getter;
        std::function<void (const juce::String&)> setter;
    };

    /** Keys that go to every copy, ready made for some instruments. Names as MPE Roll's keyboard
        has them (middle C is C5, as in FL Studio). Kontakt libraries mostly keep their keyswitches
        below its C1, which is C3 here. Output Arcade's Samplers play their 15 samples from the
        white keys between its C2 and C4, the black keys between (modifiers) change them, and a
        two-octave window of keys below C2 (Playable Pitch: G-1 to G1 in G major) retunes what's
        playing: those go to every copy, so every copy's sample hears them. Output gives no note
        numbers. The first Arcade preset reads its C2 as MIDI 48 (C4 here; middle C named C3, as
        Ableton and Cubase do), the second as MIDI 36 (C3 here). Arcade's Instruments are
        chromatic, without modifiers: None for them. */
    struct KeyPreset
    {
        const char* name;
        const char* keys;
    };

    constexpr KeyPreset keyPresets[] =
    {
        { "None", "" },
        { "Keyswitches (C0-B2)", "C0-B2" },
        { "Output Arcade Samplers (pitch keys and modifiers)", "C0-B3, black C4-C6" },
        { "Output Arcade Samplers, an octave lower", "C0-B2, black C3-C5" },
    };
}

SettingsPanel::SettingsPanel (MpeRollProcessor& p, UpdateChecker& u)
    : processor (p), document (p.getDocument()), updates (&u)
{
    auto settings = [this] { return document.getSettings(); };

    using Comps = juce::Array<juce::PropertyComponent*>;

    // Output to the synth.
    Comps output;
    // Room for what to do in the synth, too: which switch turns its MPE on.
    output.add (playedWith = new InfoProp ("Synth is played with", [this, settings]
                                           { return processor.getSynth().describeConnection (settings().output, processor.getRecentBendOverflow()); }, 96));
   #if MPEROLL_HOST_CLAP
    output.add (new BoolProp ("Note expressions (CLAP, VST3)",
        [settings] { return settings().output.clapNoteExpressions; },
        [this] (bool b) { edit ([b] (EngineSettings& e) { e.output.clapNoteExpressions = b; }); }));
   #endif
    output.add (new ChoiceProp ("Synth expects", { "MPE", "Plain MIDI (one channel)", "One copy per note (any synth)" },
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

    // One copy of the synth per note, each played with Plain MIDI.
    Comps copies;
    copies.add (copyStatus = new InfoProp ("Copies now", [this, settings]
    {
        const auto status = processor.getSynth().getCopyStatus();

        if (! status.on && settings().output.mode == OutputSettings::Mode::copies)
            return "Load a synth: MPE Roll loads " + juce::String (settings().output.copies) + " copies of it, one note to each.";

        if (! status.on)
            return juce::String ("Set Synth expects to One copy per note to play a synth without MPE with each note's own "
                                 "glides, pressure and timbre: MPE Roll loads that many copies of it, one note to each.");

        juce::String s;
        s << status.loaded << " of " << status.wanted << " loaded" << (status.loading ? " (loading)" : "")
          << ", " << status.playing << " playing, " << status.asleep << " asleep";

        if (status.crashed > 0)
            s << ", " << status.crashed << " switched off";

        if (status.copyingSound)
            s << ". Giving them copy 1's sound (" << status.inStep << " done)";

        if (status.problem.isNotEmpty())
            s << ". " << status.problem;

        return s;
    }, 110));       // room for a problem and what to do about it
    copies.add (new NumberProp ("Copies", OutputSettings::minCopies, OutputSettings::maxCopies, 1,
        [settings] { return (double) settings().output.copies; },
        [this] (double v) { edit ([v] (EngineSettings& e) { e.output.copies = (int) v; }); }));

    juce::StringArray keyPresetNames;

    for (const auto& preset : keyPresets)
        keyPresetNames.add (preset.name);

    keyPresetNames.add ("Your own (below)");

    copies.add (new ChoiceProp ("Keys to every copy", keyPresetNames,
        [settings]
        {
            const auto keys = settings().output.everyCopyKeys;

            for (int i = 0; i < (int) std::size (keyPresets); ++i)
                if (parseKeySet (keyPresets[i].keys) == keys)
                    return i;

            return (int) std::size (keyPresets);
        },
        [this] (int i)
        {
            if (juce::isPositiveAndBelow (i, (int) std::size (keyPresets)))
                edit ([i] (EngineSettings& e) { e.output.everyCopyKeys = parseKeySet (keyPresets[i].keys); });
        }));
    copies.add (new TextProp ("Every-copy keys",
        [settings] { return formatKeySet (settings().output.everyCopyKeys); },
        [this] (const juce::String& text) { edit ([text] (EngineSettings& e) { e.output.everyCopyKeys = parseKeySet (text); }); }));
    copies.add (new BoolProp ("Sleep while quiet",
        [settings] { return settings().output.copiesSleep; },
        [this] (bool b) { edit ([b] (EngineSettings& e) { e.output.copiesSleep = b; }); }));
    copies.add (new BoolProp ("Use several CPU cores",
        [settings] { return settings().output.copiesUseCores; },
        [this] (bool b) { edit ([b] (EngineSettings& e) { e.output.copiesUseCores = b; }); }));
    copies.add (new BoolProp ("Follow copy 1's sound",
        [settings] { return settings().output.copiesFollow; },
        [this] (bool b) { edit ([b] (EngineSettings& e) { e.output.copiesFollow = b; }); }));
    copies.add (new ActionProp ("Sound", "Give every copy copy 1's sound", [this] { processor.getSynth().copySoundToAllCopies(); }));
    copies.add (new ActionProp ("After a problem", "Load the copies again", [this] { processor.getSynth().reloadCopies(); }));
    panel.addSection ("One copy per note", copies);

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

    // Follow FL with FL's own notes for the channel saying when the clip sounds, so FL's Channel Rack
    // switch stops it too. The song position alone can't: it runs on with the channel switched off.
    auto* gate = new BoolProp ("Follow FL only while FL holds a note",
        [settings] { return settings().playback.gateByHostNotes; },
        [this] (bool b) { edit ([b] (EngineSettings& e) { e.playback.gateByHostNotes = b; }); });
    gate->setTooltip ("Put a note under MPE Roll's channel in FL's piano roll or playlist; the clip sounds only while it "
                      "lasts, at the song position, so the Channel Rack switch, patterns and the playlist control it. "
                      "Those notes only open the gate: they aren't played.");
    gate->setPreferredHeight (40);      // two lines for its name, which one line would cut short
    playback.add (gate);
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

    // Updates: whether MPE Roll asks GitHub for a newer release as it opens, once a day (off unless
    // turned on here), what it found last time, and asking now. Kept for the computer, beside the
    // window's settings, in a file every MPE Roll running goes by: the rows are drawn from it again
    // whenever the checker says it's changed, which it does a moment after another MPE Roll changes
    // it too. Detached, the panel has no checker, and its lines say nothing.
    Comps updateItems;

    // The newer release the last check found, for its page even if it's skipped: the Update button
    // and the File menu leave a skipped one out, so this is where it can still be got.
    const auto known = [this, userSettings]
    {
        return updates != nullptr ? UpdateChecker::findNewerRelease (userSettings()) : std::nullopt;
    };

    auto* automatic = new BoolProp ("Check for updates when MPE Roll opens (once a day)",
        [userSettings] { return UpdateChecker::isAutomatic (userSettings()); },
        [this, userSettings] (bool b)
        {
            if (updates != nullptr)
                updates->setAutomatic (userSettings(), b);
        });
    automatic->setTooltip (UpdateChecker::reachesGitHub()
                               ? "Once a day, as MPE Roll opens, asks GitHub whether a newer MPE Roll is out, and shows one as an "
                                 "Update button beside the status line. Off unless you turn it on."
                               : "On Windows and macOS, asks GitHub once a day, as MPE Roll opens, whether a newer MPE Roll is "
                                 "out. This Linux build can't make HTTPS requests, so it sends nothing. Off unless you turn it on.");
    automatic->setPreferredHeight (40);     // two lines for its name, which one line would cut short
    updateItems.add (automatic);
    updateItems.add (new InfoProp ("Last check", [this, userSettings]
    {
        return updates != nullptr ? updates->describeLastCheck (userSettings()) : juce::String();
    }, 60));
    updateItems.add (new ActionProp ("Check now",
        [this]
        {
            return updates != nullptr && updates->getState().phase == UpdateChecker::Phase::checking
                     ? juce::String ("Checking...") : juce::String ("Check for updates now");
        },
        [this] { return updates != nullptr && updates->getState().phase != UpdateChecker::Phase::checking; },
        [this, userSettings]
        {
            if (updates != nullptr)
                updates->checkNow (userSettings());
        }));
    updateItems.add (new ReleaseProp ("Newer release", known,
        [] (const ReleaseProp::Known& release)
        {
            return release.has_value() ? "Get MPE Roll " + release->release.version() + " (release page)..." : juce::String ("None known");
        },
        [] (const ReleaseProp::Known& release) { return release.has_value(); },
        [] (const UpdateChecker::KnownRelease& release) { UpdateChecker::openReleasePage (release.release); }));
    updateItems.add (new ReleaseProp ("Skip it", known,
        [] (const ReleaseProp::Known& release)
        {
            return ! release.has_value() ? juce::String ("Skip version")
                 : release->skipped      ? juce::String ("Skipped")
                                         : "Skip version " + release->release.version();
        },
        [] (const ReleaseProp::Known& release) { return release.has_value() && ! release->skipped; },
        [this, userSettings] (const UpdateChecker::KnownRelease& release)
        {
            if (updates != nullptr)
                updates->skip (userSettings(), release.release);
        }));
    // Room for its lines in JUCE's default typeface, which on Windows and macOS (Verdana, Lucida
    // Grande) is wider than the one it was laid out with here on Linux, where the Windows wording
    // takes eight lines of 112 pixels: ten there.
   #if JUCE_LINUX
    constexpr int sendsHeight = 112;
   #else
    constexpr int sendsHeight = 150;
   #endif

    updateItems.add (new InfoProp ("What it sends", []
    {
        // The Linux build can't make HTTPS requests: it sends GitHub nothing, whatever the switch says.
        if (! UpdateChecker::reachesGitHub())
            return juce::String ("Nothing: this Linux build can't make HTTPS requests, so it never asks GitHub, and a check only "
                                 "says so. On Windows and macOS a check sends GitHub one request, with MPE Roll's name and "
                                 "version; MPE Roll's Privacy page has the details.");

       #if JUCE_MAC
        const juce::String systemHeaders ("the headers macOS adds (your Mac's languages, and any github.com cookies the host keeps)");
       #else
        const juce::String systemHeaders ("the standard headers your system adds");
       #endif

        return juce::String (UpdateChecker::asksReleasePage()
                                 ? "One request to GitHub for the latest release (a second, to the release page, if that fails). "
                                 : "One request to GitHub's API for the latest release. ")
             + "Nothing about you, your computer or your projects goes with it beyond what every web request carries: your IP "
               "address, " + systemHeaders + ", and MPE Roll's name and version (MPE Roll/" + getVersionString()
             + "). MPE Roll's Privacy page has the details.";
    }, sendsHeight));
    updateRows = updateItems;
    panel.addSection ("Updates", updateItems, false);

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
    supportLink.setURL ({});
    supportLink.setTooltip ("Shows how to donate: Cash App, or crypto by QR code");
    supportLink.onClick = [this] { DonatePanel::show (findParentComponentOfClass<juce::Component>()); };
    supportLink.setJustificationType (juce::Justification::centredLeft);
    supportLink.setColour (juce::HyperlinkButton::textColourId, Theme::glide);
    addAndMakeVisible (supportLink);

    // And a word to friends: the website has posts ready to put on X, Instagram and Facebook. In the
    // dim text colour, so it asks for no attention of its own.
    shareLink.setButtonText ("Share MPE Roll");
    shareLink.setURL (juce::URL (juce::String (links::share)));
    shareLink.setTooltip ("Opens the website's share page: ready-made posts for X, Instagram and Facebook");
    shareLink.setJustificationType (juce::Justification::centredLeft);
    shareLink.setColour (juce::HyperlinkButton::textColourId, Theme::textDim);
    addAndMakeVisible (shareLink);

    // Which build this is, so a tester can tell which one the DAW loaded.
    buildName.setText (getBuildName(), juce::dontSendNotification);
    buildName.setJustificationType (juce::Justification::centredRight);
    buildName.setFont (juce::FontOptions (12.0f));
    buildName.setMinimumHorizontalScale (0.7f);
    buildName.setColour (juce::Label::textColourId, Theme::textDim);
    addAndMakeVisible (buildName);

    document.addChangeListener (this);
    skins->addChangeListener (this);
    updates->addChangeListener (this);
    setSize (460, 640);

    // The copies load, sleep and wake on their own: their lines are kept up to date.
    startTimer (500);
}

SettingsPanel::~SettingsPanel()
{
    stopTimer();

    if (! detached)
    {
        updates->removeChangeListener (this);
        skins->removeChangeListener (this);
        document.removeChangeListener (this);
    }
}

void SettingsPanel::detach()
{
    if (detached)
        return;

    detached = true;
    stopTimer();
    copyStatus = nullptr;
    playedWith = nullptr;
    updateRows.clear();
    updates->removeChangeListener (this);
    updates = nullptr;      // the editor's, which goes with it
    skins->removeChangeListener (this);
    document.removeChangeListener (this);
    panel.clear();      // and with the properties go their callbacks into the processor and document
}

void SettingsPanel::timerCallback()
{
    if (copyStatus != nullptr && copyStatus->isShowing())
        copyStatus->refresh();

    if (playedWith != nullptr && playedWith->isShowing())
        playedWith->refresh();
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

void SettingsPanel::changeListenerCallback (juce::ChangeBroadcaster* source)
{
    // The update check's news, which another MPE Roll's change to its file brings too: its rows
    // alone, so a line being typed elsewhere in the panel is left as it is.
    if (updates != nullptr && source == updates)
    {
        for (auto* row : updateRows)
            row->refresh();

        return;
    }

    panel.refreshAll();
    supportLink.setColour (juce::HyperlinkButton::textColourId, Theme::glide);   // the skin may have changed
    shareLink.setColour (juce::HyperlinkButton::textColourId, Theme::textDim);
    buildName.setColour (juce::Label::textColourId, Theme::textDim);
    repaint();
}

void SettingsPanel::paint (juce::Graphics& g)
{
    g.fillAll (Theme::panel);
}

void SettingsPanel::resized()
{
    auto area = getLocalBounds().reduced (4);
    // The links as wide as their words in whatever font the system has, the build's name in the rest,
    // or on a line of its own above them when the rest is too narrow for all of it: a build's name
    // ("MPE Roll 0.2.0, build 123, f2ea3c4, 2026-11-03") is only any use whole.
    auto bottom = area.removeFromBottom (24).withTrimmedLeft (4);
    supportLink.setBounds (bottom);
    supportLink.changeWidthToFitText();
    bottom.removeFromLeft (supportLink.getWidth() + 8);
    shareLink.setBounds (bottom);
    shareLink.changeWidthToFitText();
    bottom.removeFromLeft (shareLink.getWidth() + 8);

    const auto buildWidth = juce::GlyphArrangement::getStringWidth (buildName.getFont(), buildName.getText())
                          + (float) buildName.getBorderSize().getLeftAndRight();
    buildName.setBounds ((float) bottom.getWidth() >= buildWidth ? bottom : area.removeFromBottom (20).withTrimmedLeft (4));
    panel.setBounds (area);
}

} // namespace mperoll
