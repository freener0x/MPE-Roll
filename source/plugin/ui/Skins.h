#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <map>

namespace mperoll
{

/** The colours a skin is made of. Theme's colours are set from these. */
enum class SkinRole
{
    background, panel, buttons, outline,
    gridWhiteRows, gridBlackRows, barLines, beatLines, snapLines,
    notes, selectedNotes, glide, pressure, timbre, controllers,
    playhead, recording, text, dimText, accent, whiteKeys, blackKeys,
    count
};

inline constexpr int numSkinRoles = (int) SkinRole::count;

using SkinColours = std::array<juce::Colour, (size_t) numSkinRoles>;

struct Skin
{
    juce::String name;
    SkinColours colours;
};

/** The skins MPE Roll comes with. The first is the default. */
const std::vector<Skin>& presetSkins();

/** What each colour is called in the settings. */
juce::String skinRoleName (SkinRole);

/** The look of MPE Roll's windows: a preset skin, the user's own colours on top of it, and a
    background picture behind the grid. It's kept in the computer's MPE Roll settings (a file of
    its own, so every MPE Roll in a project shares it) and applied to Theme at once.

    Hold one with juce::SharedResourcePointer; use it on the message thread only.
*/
class SkinManager : public juce::ChangeBroadcaster
{
public:
    SkinManager();
    ~SkinManager() override;

    int getPreset() const noexcept                          { return preset; }

    /** Picks a preset skin. The user's own colours are cleared; the background picture stays. */
    void setPreset (int index);

    juce::Colour getColour (SkinRole) const;
    void setColour (SkinRole, juce::Colour);
    bool hasCustomColours() const noexcept                  { return ! custom.empty(); }
    void resetColours();

    const juce::File& getBackgroundFile() const noexcept    { return backgroundFile; }

    /** Shows a picture behind the grid (PNG, JPEG or GIF). Returns false if it couldn't be read. */
    bool setBackgroundFile (const juce::File&);
    void clearBackground();
    bool hasBackground() const noexcept                     { return background.isValid(); }

    /** How strongly the picture shows through the grid, 0..1. */
    float getBackgroundStrength() const noexcept            { return backgroundStrength; }
    void setBackgroundStrength (float);

    /** The picture scaled to cover an area of the given size, cropped to the middle (cached). */
    juce::Image getBackgroundFor (juce::Rectangle<int> area);

    /** Opens a file browser for the background picture. */
    void chooseBackground();

    /** Where the look is kept, for tests: from now on it's read from and saved to this file. */
    void useSettingsFile (const juce::File&);

    /** Writes any change not saved yet (changes are saved shortly after they're made). */
    void flush();

private:
    int preset = 0;
    std::map<int, juce::Colour> custom;     // by role
    juce::File backgroundFile;
    juce::Image background;
    float backgroundStrength = 0.35f;
    std::map<std::pair<int, int>, juce::Image> scaled;
    std::unique_ptr<juce::PropertiesFile> settings;
    std::unique_ptr<juce::FileChooser> chooser;

    void load();
    void save();
    void apply();
    void loadBackground (const juce::File&);
};

} // namespace mperoll
