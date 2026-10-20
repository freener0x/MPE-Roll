#include "Skins.h"
#include "MpeLookAndFeel.h"

namespace mperoll
{

namespace
{
    // In SkinRole order: background, panel, buttons, outline, white rows, black rows, bar, beat and
    // snap lines, notes, selected notes, glide, pressure, timbre, controllers, playhead, recording,
    // text, dim text, accent, white keys, black keys.
    Skin skin (const char* name, std::initializer_list<juce::uint32> argb)
    {
        jassert ((int) argb.size() == numSkinRoles);
        Skin s { name, {} };
        size_t i = 0;

        for (auto c : argb)
            s.colours[i++] = juce::Colour (0xff000000 | c);

        return s;
    }

    constexpr auto lookFileName = "Look";
}

const std::vector<Skin>& presetSkins()
{
    static const std::vector<Skin> skins
    {
        skin ("MPE Roll", { 0x16181c, 0x1e2127, 0x292d35, 0x363b45, 0x20232a, 0x1a1c21, 0x4c5260, 0x323743, 0x272a32,
                            0x6fcf97, 0xb9f5d0, 0xffb04a, 0x58a6ff, 0xd291ff, 0x4fd6d6, 0xf2f4f7, 0xff5a5a,
                            0xd7dbe2, 0x8a909c, 0x2f6f4c, 0xd9dce2, 0x23252b }),
        skin ("FL Studio", { 0x1d2326, 0x2b3337, 0x3a454a, 0x4b585e, 0x2e3a3e, 0x263034, 0x5d6d73, 0x435156, 0x364347,
                             0x9ed36a, 0xf07a7a, 0xffc457, 0x63b3ff, 0xc49bff, 0x5fd3c6, 0x9ed36a, 0xff4d4d,
                             0xdfe6e8, 0x8f9ca1, 0x56742f, 0xe6e9ea, 0x1c2225 }),
        skin ("Midnight", { 0x0b1020, 0x121a30, 0x1c2642, 0x2a3658, 0x141d36, 0x0f162b, 0x3a4a78, 0x26335a, 0x1b2646,
                            0x59c3ff, 0xc4ecff, 0xffb86b, 0x7aa2ff, 0xe08cff, 0x5ee6d0, 0xffffff, 0xff5c7a,
                            0xd8e0f5, 0x7f8bb0, 0x2b4f8c, 0xd5dbea, 0x151b2e }),
        skin ("Synthwave", { 0x140a1f, 0x1e1030, 0x2c1846, 0x45276a, 0x221238, 0x1a0d2b, 0x6a3a9a, 0x46266a, 0x31194b,
                             0xff4fd8, 0xffc2f3, 0xffd23f, 0x3ff0ff, 0xb388ff, 0x4dffb8, 0xfff1ff, 0xff3860,
                             0xf3e6ff, 0xa58bc4, 0x7a2a8f, 0xecdcf7, 0x1c0e2a }),
        skin ("Solarized", { 0x00212b, 0x002b36, 0x073642, 0x1f4a55, 0x073642, 0x002b36, 0x586e75, 0x2f525c, 0x15434f,
                             0x859900, 0xe6f0a0, 0xb58900, 0x268bd2, 0x6c71c4, 0x2aa198, 0xfdf6e3, 0xdc322f,
                             0x93a1a1, 0x657b83, 0x3a6b12, 0xeee8d5, 0x073642 }),
        skin ("Paper", { 0xe9e6df, 0xf4f2ec, 0xe3e0d8, 0xc4bfb3, 0xfbfaf7, 0xefece5, 0xa9a396, 0xd0cbc0, 0xe2ded5,
                         0x3f9d6b, 0x1c6b45, 0xd9822b, 0x2f74c0, 0x8e44ad, 0x159a9a, 0x222222, 0xd93636,
                         0x2b2a27, 0x7a766d, 0x3f8a5f, 0xffffff, 0x3a3a3a }),
        skin ("Forest", { 0x101712, 0x17211a, 0x223024, 0x324535, 0x1a261d, 0x141e17, 0x4a6650, 0x2f4334, 0x223127,
                          0xa3d977, 0xe2f7c8, 0xf2c14e, 0x6fb7d6, 0xc29fe0, 0x7fd1a8, 0xf0f5e8, 0xe8604c,
                          0xdfe8d8, 0x8a9a86, 0x3e6b35, 0xe4ead9, 0x18211a }),
        skin ("Ember", { 0x1a1210, 0x241915, 0x33231c, 0x4a3328, 0x271b16, 0x1f1612, 0x6b4a3a, 0x45302a, 0x33241e,
                         0xff8a3d, 0xffd2a8, 0xffd35c, 0x5cb8ff, 0xe48cff, 0x6fe0c8, 0xfff3e6, 0xff4a3d,
                         0xf2e3d7, 0xa08a7c, 0x8a3f1c, 0xf0e4d8, 0x22160f }),
        skin ("Ocean", { 0x0b1a1f, 0x10242b, 0x17323b, 0x22474f, 0x12292f, 0x0e2127, 0x36636e, 0x22444c, 0x18343b,
                         0x3fd0c9, 0xb6f3ef, 0xffbe5c, 0x5aa9ff, 0xc6a0ff, 0x7ee081, 0xe8fbff, 0xff6b6b,
                         0xd4eef0, 0x7fa3a8, 0x1f6d6a, 0xd8eef0, 0x0f2329 }),
        skin ("High contrast", { 0x000000, 0x0a0a0a, 0x1f1f1f, 0x6b6b6b, 0x141414, 0x000000, 0x9a9a9a, 0x5a5a5a, 0x333333,
                                 0xffff00, 0xffffff, 0xff9900, 0x00b7ff, 0xff55ff, 0x00ffcc, 0xffffff, 0xff2a2a,
                                 0xffffff, 0xc8c8c8, 0x0050c8, 0xffffff, 0x000000 }),
    };

    return skins;
}

juce::String skinRoleName (SkinRole role)
{
    switch (role)
    {
        case SkinRole::background:      return "Window background";
        case SkinRole::panel:           return "Panels";
        case SkinRole::buttons:         return "Buttons";
        case SkinRole::outline:         return "Outlines";
        case SkinRole::gridWhiteRows:   return "Grid (white-key rows)";
        case SkinRole::gridBlackRows:   return "Grid (black-key rows)";
        case SkinRole::barLines:        return "Bar lines";
        case SkinRole::beatLines:       return "Beat lines";
        case SkinRole::snapLines:       return "Snap lines";
        case SkinRole::notes:           return "Notes";
        case SkinRole::selectedNotes:   return "Selected notes";
        case SkinRole::glide:           return "Glide and vibrato";
        case SkinRole::pressure:        return "Pressure";
        case SkinRole::timbre:          return "Timbre";
        case SkinRole::controllers:     return "Controllers";
        case SkinRole::playhead:        return "Playhead";
        case SkinRole::recording:       return "Recording";
        case SkinRole::text:            return "Text";
        case SkinRole::dimText:         return "Dim text";
        case SkinRole::accent:          return "Accent (loop, active buttons)";
        case SkinRole::whiteKeys:       return "White keys";
        case SkinRole::blackKeys:       return "Black keys";
        case SkinRole::count:           break;
    }

    return {};
}

//==============================================================================
SkinManager::SkinManager()
{
    juce::PropertiesFile::Options options;
    options.applicationName = "MPE Roll";
    options.folderName = "MPE Roll";
    options.filenameSuffix = "settings";
    options.osxLibrarySubFolder = "Application Support";
    options.storageFormat = juce::PropertiesFile::storeAsXML;
    options.millisecondsBeforeSaving = 500;     // a colour being dragged in its picker changes many times a second
    settings = std::make_unique<juce::PropertiesFile> (options.getDefaultFile().getSiblingFile (juce::String (lookFileName) + ".settings"),
                                                       options);
    load();
}

SkinManager::~SkinManager()
{
    if (settings != nullptr)
        settings->saveIfNeeded();
}

void SkinManager::useSettingsFile (const juce::File& file)
{
    juce::PropertiesFile::Options options;
    options.storageFormat = juce::PropertiesFile::storeAsXML;
    options.millisecondsBeforeSaving = 500;

    if (settings != nullptr)
        settings->saveIfNeeded();

    settings = std::make_unique<juce::PropertiesFile> (file, options);
    load();
}

void SkinManager::flush()
{
    settings->saveIfNeeded();
}

void SkinManager::load()
{
    preset = juce::jlimit (0, (int) presetSkins().size() - 1, settings->getIntValue ("skin", 0));
    custom.clear();

    if (auto xml = settings->getXmlValue ("colours"))
    {
        for (auto* c : xml->getChildIterator())
        {
            const auto role = c->getIntAttribute ("role", -1);

            if (juce::isPositiveAndBelow (role, numSkinRoles))
                custom[role] = juce::Colour::fromString (c->getStringAttribute ("argb"));
        }
    }

    backgroundStrength = juce::jlimit (0.0f, 1.0f, (float) settings->getDoubleValue ("backgroundStrength", 0.35));
    const auto picture = settings->getValue ("background");
    loadBackground (juce::File::isAbsolutePath (picture) ? juce::File (picture) : juce::File());
    apply();
}

void SkinManager::save()
{
    settings->setValue ("skin", preset);

    juce::XmlElement colours ("COLOURS");

    for (const auto& [role, colour] : custom)
    {
        auto* c = colours.createNewChildElement ("COLOUR");
        c->setAttribute ("role", role);
        c->setAttribute ("argb", colour.toString());
    }

    settings->setValue ("colours", &colours);
    settings->setValue ("background", backgroundFile.getFullPathName());
    settings->setValue ("backgroundStrength", backgroundStrength);
}

void SkinManager::setPreset (int index)
{
    preset = juce::jlimit (0, (int) presetSkins().size() - 1, index);
    custom.clear();
    save();
    apply();
}

juce::Colour SkinManager::getColour (SkinRole role) const
{
    if (auto it = custom.find ((int) role); it != custom.end())
        return it->second;

    return presetSkins()[(size_t) preset].colours[(size_t) role];
}

void SkinManager::setColour (SkinRole role, juce::Colour colour)
{
    custom[(int) role] = colour.withAlpha (1.0f);
    save();
    apply();
}

void SkinManager::resetColours()
{
    custom.clear();
    save();
    apply();
}

void SkinManager::loadBackground (const juce::File& file)
{
    background = {};
    scaled.clear();
    backgroundFile = file;

    if (file != juce::File() && file.existsAsFile())
        background = juce::ImageFileFormat::loadFrom (file);

    if (! background.isValid())
        backgroundFile = juce::File();
}

bool SkinManager::setBackgroundFile (const juce::File& file)
{
    loadBackground (file);
    save();
    apply();
    return background.isValid();
}

void SkinManager::clearBackground()
{
    loadBackground ({});
    save();
    apply();
}

void SkinManager::setBackgroundStrength (float strength)
{
    backgroundStrength = juce::jlimit (0.0f, 1.0f, strength);
    save();
    sendChangeMessage();
}

juce::Image SkinManager::getBackgroundFor (juce::Rectangle<int> area)
{
    if (! background.isValid() || area.isEmpty())
        return {};

    const auto key = std::pair { area.getWidth(), area.getHeight() };

    if (auto it = scaled.find (key); it != scaled.end())
        return it->second;

    if (scaled.size() >= 6)
        scaled.clear();

    // Cover the area, keeping the picture's shape, and crop what spills over equally on each side.
    juce::Image image (juce::Image::RGB, area.getWidth(), area.getHeight(), true);
    juce::Graphics g (image);
    g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
    g.drawImage (background, image.getBounds().toFloat(), juce::RectanglePlacement::fillDestination);
    return scaled[key] = image;
}

void SkinManager::chooseBackground()
{
    chooser = std::make_unique<juce::FileChooser> ("Background picture",
                                                   backgroundFile.existsAsFile() ? backgroundFile
                                                                                 : juce::File::getSpecialLocation (juce::File::userPicturesDirectory),
                                                   "*.png;*.jpg;*.jpeg;*.gif");

    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [this] (const juce::FileChooser& fc)
    {
        const auto file = fc.getResult();

        if (file.existsAsFile() && ! setBackgroundFile (file))
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Background picture",
                                                    "Couldn't read " + file.getFullPathName()
                                                        + " as a picture. PNG, JPEG and GIF files work.");
    });
}

void SkinManager::apply()
{
    auto c = [this] (SkinRole role) { return getColour (role); };

    Theme::background   = c (SkinRole::background);
    Theme::panel        = c (SkinRole::panel);
    Theme::panelLight   = c (SkinRole::buttons);
    Theme::outline      = c (SkinRole::outline);
    Theme::rowWhite     = c (SkinRole::gridWhiteRows);
    Theme::rowBlack     = c (SkinRole::gridBlackRows);
    Theme::lineBar      = c (SkinRole::barLines);
    Theme::lineBeat     = c (SkinRole::beatLines);
    Theme::lineSub      = c (SkinRole::snapLines);
    Theme::note         = c (SkinRole::notes);
    Theme::velocity     = c (SkinRole::notes);
    Theme::noteSelected = c (SkinRole::selectedNotes);
    Theme::glide        = c (SkinRole::glide);
    Theme::pressure     = c (SkinRole::pressure);
    Theme::timbre       = c (SkinRole::timbre);
    Theme::controller   = c (SkinRole::controllers);
    Theme::playhead     = c (SkinRole::playhead);
    Theme::record       = c (SkinRole::recording);
    Theme::text         = c (SkinRole::text);
    Theme::textDim      = c (SkinRole::dimText);
    Theme::loopRegion   = c (SkinRole::accent);
    Theme::whiteKey     = c (SkinRole::whiteKeys);
    Theme::blackKey     = c (SkinRole::blackKeys);

    // Labels that stay readable on whatever the notes and keys are.
    Theme::noteText = Theme::note.getPerceivedBrightness() > 0.55f ? juce::Colours::black.withAlpha (0.75f)
                                                                   : juce::Colours::white.withAlpha (0.9f);
    Theme::keyLabel = Theme::whiteKey.getPerceivedBrightness() > 0.55f ? Theme::whiteKey.darker (2.2f)
                                                                       : Theme::whiteKey.brighter (2.2f);
    sendChangeMessage();
}

} // namespace mperoll
