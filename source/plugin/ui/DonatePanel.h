#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>
#include <vector>

namespace mperoll
{

/** The Support MPE Roll window's content: the same donation details as the website (DONATE.md),
    built into the plugin from ui/DonateData.h so it works offline and whatever becomes of the site.
    The payment buttons open the payment site in the browser; each crypto address has its QR code,
    a box it can be selected from, and a Copy button. */
class DonatePanel : public juce::Component
{
public:
    DonatePanel();
    ~DonatePanel() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    /** Opens the panel in a window of its own on the display `near` is on. */
    static void show (juce::Component* near);

    /** For tests: the address shown for a coin ("BTC"...), and its Copy button. */
    juce::String getAddress (const juce::String& coinId) const;
    juce::Button* getCopyButton (const juce::String& coinId) const;
    int getNumQrCodes() const;

private:
    struct CoinRow;

    /** Lays the children out at this width and returns the height they take. */
    int layOut (int width);

    juce::Label title, lead, linksHeading, cryptoHeading;
    juce::OwnedArray<juce::TextButton> linkButtons;
    juce::OwnedArray<juce::Label> linkNotes;
    std::vector<std::unique_ptr<CoinRow>> coins;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DonatePanel)
};

} // namespace mperoll
