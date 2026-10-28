#include "DonatePanel.h"
#include "DonateData.h"
#include "MpeLookAndFeel.h"
#include "WindowPlacement.h"

namespace mperoll
{

namespace
{
    constexpr int panelWidth = 640;
    constexpr int qrSize = 116;
    constexpr int columnGap = 16, rowGap = 18;

    void styleHeading (juce::Label& label, const juce::String& text, float size)
    {
        label.setText (text, juce::dontSendNotification);
        label.setFont (juce::FontOptions (size, juce::Font::bold));
        label.setColour (juce::Label::textColourId, Theme::text);
    }

    void styleNote (juce::Label& label, const juce::String& text)
    {
        label.setText (text, juce::dontSendNotification);
        label.setFont (juce::FontOptions (12.0f));
        label.setColour (juce::Label::textColourId, Theme::textDim);
        label.setMinimumHorizontalScale (1.0f);
        label.setJustificationType (juce::Justification::topLeft);
    }
}

/** One coin: its QR code, name, address and Copy button, and a note. */
struct DonatePanel::CoinRow
{
    juce::String id, address;
    std::unique_ptr<juce::Drawable> qr;
    juce::Label name, note;
    juce::TextEditor addressBox;
    juce::TextButton copyButton { "Copy address" };

    CoinRow (juce::Component& owner, const donate::Coin& coin) : id (coin.id), address (coin.address)
    {
        qr = juce::Drawable::createFromSVGString (juce::String (coin.qrSvg));

        styleHeading (name, coin.name, 14.0f);
        styleNote (note, coin.note);

        // Read-only, but selectable, for wallets that want the address typed or pasted in parts.
        addressBox.setReadOnly (true);
        addressBox.setMultiLine (true, true);
        addressBox.setScrollbarsShown (false);
        addressBox.setCaretVisible (false);
        addressBox.setFont (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), 12.5f, juce::Font::plain));
        addressBox.setText (address, false);
        addressBox.setColour (juce::TextEditor::backgroundColourId, Theme::background);
        addressBox.setColour (juce::TextEditor::textColourId, Theme::text);

        copyButton.setTooltip ("Puts the " + juce::String (coin.name) + " address on the clipboard");
        copyButton.onClick = [this]
        {
            juce::SystemClipboard::copyTextToClipboard (address);
            copyButton.setButtonText (juce::String::fromUTF8 ("Copied \xe2\x9c\x93"));

            juce::Timer::callAfterDelay (2000, [safe = juce::Component::SafePointer<juce::TextButton> (&copyButton)]
            {
                if (safe != nullptr)
                    safe->setButtonText ("Copy address");
            });
        };

        for (auto* c : std::initializer_list<juce::Component*> { &name, &note, &addressBox, &copyButton })
            owner.addAndMakeVisible (c);
    }

    juce::Rectangle<int> qrArea;

    /** The address box's height at this width: as many lines as the address takes (Monero's is long). */
    int addressHeight (int width)
    {
        // Where the address wraps is the editor's own layout's call, so lay it out at this width
        // with no room to spare and read back the height its text took.
        addressBox.setSize (width, 1);
        return addressBox.getTextHeight() + addressBox.getTopIndent() + addressBox.getBorder().getTopAndBottom();
    }

    /** Lays the coin out from topLeft at this width and returns the height it takes: the code on
        the left, name, address and Copy beside it, and the note, if any, under both. */
    int layOut (juce::Point<int> topLeft, int width)
    {
        qrArea = { topLeft.x, topLeft.y, qrSize, qrSize };

        juce::Rectangle<int> column (qrArea.getRight() + 12, topLeft.y, width - qrSize - 12, 10000);
        name.setBounds (column.removeFromTop (20));
        column.removeFromTop (2);
        addressBox.setBounds (column.removeFromTop (addressHeight (column.getWidth())));
        column.removeFromTop (6);
        copyButton.setBounds (column.removeFromTop (26).withWidth (120));

        auto bottom = juce::jmax (qrArea.getBottom(), column.getY());

        if (note.getText().isNotEmpty())
        {
            note.setBounds (topLeft.x, bottom + 6, width, 34);
            bottom = note.getBottom();
        }

        return bottom - topLeft.y;
    }

    void paint (juce::Graphics& g) const
    {
        // A white square behind the code, as scanners expect dark on light whatever the skin.
        g.setColour (juce::Colours::white);
        g.fillRoundedRectangle (qrArea.toFloat(), 4.0f);

        if (qr != nullptr)
            qr->drawWithin (g, qrArea.toFloat().reduced (2.0f), juce::RectanglePlacement::centred, 1.0f);
    }
};

DonatePanel::DonatePanel()
{
    styleHeading (title, "Support MPE Roll", 20.0f);
    styleNote (lead, donate::lead);
    lead.setFont (juce::FontOptions (14.0f));
    lead.setColour (juce::Label::textColourId, Theme::text.withAlpha (0.85f));

    for (auto* c : std::initializer_list<juce::Component*> { &title, &lead })
        addAndMakeVisible (c);

    if (! donate::links.empty())
    {
        styleHeading (linksHeading, "Send a donation", 15.0f);
        addAndMakeVisible (linksHeading);

        for (const auto& link : donate::links)
        {
            auto* button = linkButtons.add (new juce::TextButton (link.name));
            button->setColour (juce::TextButton::buttonColourId, Theme::glide);
            button->setColour (juce::TextButton::textColourOffId, juce::Colours::black);
            button->setTooltip ("Opens " + juce::String (link.url) + " in your browser");
            button->onClick = [url = juce::String (link.url)] { juce::URL (url).launchInDefaultBrowser(); };
            addAndMakeVisible (button);

            auto* note = linkNotes.add (new juce::Label());
            styleNote (*note, juce::String (link.note).isNotEmpty()
                                  ? juce::String (link.note)
                                  : juce::String ("Opens the payment site in your browser; nothing from MPE Roll is sent with it."));
            addAndMakeVisible (note);
        }
    }

    if (! donate::coins.empty())
    {
        styleHeading (cryptoHeading, "Crypto", 15.0f);
        addAndMakeVisible (cryptoHeading);

        for (const auto& coin : donate::coins)
            coins.push_back (std::make_unique<CoinRow> (*this, coin));
    }

    setSize (panelWidth, layOut (panelWidth));
}

DonatePanel::~DonatePanel() = default;

void DonatePanel::paint (juce::Graphics& g)
{
    g.fillAll (Theme::panel);

    for (const auto& coin : coins)
        coin->paint (g);
}

void DonatePanel::resized()
{
    layOut (getWidth());
}

int DonatePanel::layOut (int width)
{
    auto area = juce::Rectangle<int> (width, 10000).reduced (20, 16);
    title.setBounds (area.removeFromTop (30));
    lead.setBounds (area.removeFromTop (40));
    area.removeFromTop (4);

    if (! linkButtons.isEmpty())
    {
        linksHeading.setBounds (area.removeFromTop (26));
        area.removeFromTop (6);

        for (int i = 0; i < linkButtons.size(); ++i)
        {
            auto row = area.removeFromTop (34);
            linkButtons[i]->setBounds (row.removeFromLeft (140));
            row.removeFromLeft (12);
            linkNotes[i]->setBounds (row.withSizeKeepingCentre (row.getWidth(), 20));
            area.removeFromTop (6);
        }
    }

    if (! coins.empty())
    {
        area.removeFromTop (6);
        cryptoHeading.setBounds (area.removeFromTop (26));
        area.removeFromTop (8);

        // Two coins to a row, each row as tall as its taller coin.
        const auto cellWidth = (area.getWidth() - columnGap) / 2;

        for (size_t i = 0; i < coins.size(); i += 2)
        {
            auto rowHeight = coins[i]->layOut (area.getPosition(), cellWidth);

            if (i + 1 < coins.size())
                rowHeight = juce::jmax (rowHeight, coins[i + 1]->layOut (area.getPosition().translated (cellWidth + columnGap, 0), cellWidth));

            area.removeFromTop (rowHeight + (i + 2 < coins.size() ? rowGap : 0));
        }
    }

    return area.getY() + 12;
}

juce::String DonatePanel::getAddress (const juce::String& coinId) const
{
    for (const auto& coin : coins)
        if (coin->id == coinId)
            return coin->addressBox.getText();

    return {};
}

juce::Button* DonatePanel::getCopyButton (const juce::String& coinId) const
{
    for (const auto& coin : coins)
        if (coin->id == coinId)
            return &coin->copyButton;

    return nullptr;
}

int DonatePanel::getNumQrCodes() const
{
    int n = 0;

    for (const auto& coin : coins)
        n += coin->qr != nullptr ? 1 : 0;

    return n;
}

void DonatePanel::show (juce::Component* near)
{
    juce::DialogWindow::LaunchOptions options;
    options.content.setOwned (new DonatePanel());
    options.dialogTitle = "Support MPE Roll";
    options.dialogBackgroundColour = Theme::panel;
    options.useNativeTitleBar = true;
    options.resizable = false;
    options.escapeKeyTriggersCloseButton = true;

    if (auto* window = options.launchAsync())
    {
        // Plugin windows aren't owned by FL's, which would otherwise cover this one.
        window->setAlwaysOnTop (true);
        WindowPlacement::showOnDisplayOf (*window, near, false);
        window->toFront (true);
    }
}

} // namespace mperoll
