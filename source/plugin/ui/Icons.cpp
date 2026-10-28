#include "Icons.h"
#include "MpeLookAndFeel.h"

#include <cmath>

namespace mperoll
{

namespace
{
    constexpr float box = 24.0f;
    constexpr float pi = juce::MathConstants<float>::pi;

    void line (juce::Path& p, float x1, float y1, float x2, float y2)
    {
        p.startNewSubPath (x1, y1);
        p.lineTo (x2, y2);
    }

    void polygon (juce::Path& p, std::initializer_list<juce::Point<float>> points)
    {
        bool first = true;

        for (auto pt : points)
        {
            if (first)
                p.startNewSubPath (pt);
            else
                p.lineTo (pt);

            first = false;
        }

        p.closeSubPath();
    }

    /** A rectangle outline drawn as dashes, for the selection marquee. */
    void dashedRectangle (juce::Path& p, juce::Rectangle<float> r, float dash, float gap)
    {
        const juce::Point<float> corners[] = { r.getTopLeft(), r.getTopRight(), r.getBottomRight(), r.getBottomLeft() };

        for (int side = 0; side < 4; ++side)
        {
            const auto from = corners[side], to = corners[(side + 1) % 4];
            const auto length = from.getDistanceFrom (to);
            const auto direction = (to - from) / length;

            for (float at = 0.0f; at < length; at += dash + gap)
            {
                const auto end = juce::jmin (length, at + dash);
                line (p, from.x + direction.x * at, from.y + direction.y * at, from.x + direction.x * end, from.y + direction.y * end);
            }
        }
    }

    juce::Path gear()
    {
        juce::Path p;
        constexpr int teeth = 8;
        constexpr float outer = 10.5f, inner = 7.6f, centre = 12.0f;
        bool first = true;

        for (int i = 0; i < teeth; ++i)
        {
            const auto a = (float) i * 2.0f * pi / teeth;

            for (auto [radius, offset] : { std::pair { inner, -0.36f }, std::pair { outer, -0.17f },
                                           std::pair { outer, 0.17f }, std::pair { inner, 0.36f } })
            {
                const juce::Point<float> pt (centre + radius * std::sin (a + offset), centre - radius * std::cos (a + offset));

                if (first)
                    p.startNewSubPath (pt);
                else
                    p.lineTo (pt);

                first = false;
            }
        }

        p.closeSubPath();
        p.addEllipse (centre - 3.2f, centre - 3.2f, 6.4f, 6.4f);    // the hole, with even-odd filling
        p.setUsingNonZeroWinding (false);
        return p;
    }
}

IconArt iconArt (Icon icon)
{
    IconArt art;
    auto& s = art.strokes;
    auto& f = art.fills;
    auto& a = art.accentFills;

    switch (icon)
    {
        case Icon::draw:
            // A pencil, tip at the bottom left.
            polygon (s, { { 3.5f, 20.5f }, { 4.5f, 15.5f }, { 16.0f, 4.0f }, { 20.0f, 8.0f }, { 8.5f, 19.5f } });
            line (s, 13.0f, 7.0f, 17.0f, 11.0f);
            polygon (a, { { 3.5f, 20.5f }, { 4.5f, 15.5f }, { 8.5f, 19.5f } });
            break;

        case Icon::paint:
            // The paintbrush the Paint tool's pointer already shows.
            art.strokeWidth = 3.0f;
            line (s, 11.5f, 12.5f, 20.5f, 3.5f);
            a.startNewSubPath (3.0f, 21.0f);
            a.quadraticTo (4.0f, 14.0f, 9.0f, 12.0f);
            a.lineTo (12.0f, 15.0f);
            a.quadraticTo (10.0f, 20.0f, 3.0f, 21.0f);
            a.closeSubPath();
            break;

        case Icon::erase:
            // An eraser on its side, the rubber end in the accent colour.
            polygon (s, { { 3.5f, 15.5f }, { 13.5f, 5.5f }, { 20.5f, 12.5f }, { 13.5f, 19.5f }, { 7.5f, 19.5f } });
            line (s, 8.5f, 10.5f, 15.5f, 17.5f);
            polygon (a, { { 3.5f, 15.5f }, { 8.5f, 10.5f }, { 15.5f, 17.5f }, { 13.5f, 19.5f }, { 7.5f, 19.5f } });
            break;

        case Icon::slice:
            // Scissors, blades to the right.
            s.addEllipse (3.0f, 4.0f, 5.5f, 5.5f);
            s.addEllipse (3.0f, 14.5f, 5.5f, 5.5f);
            line (s, 7.5f, 8.5f, 21.0f, 18.5f);
            line (s, 7.5f, 15.5f, 21.0f, 5.5f);
            break;

        case Icon::select:
            dashedRectangle (s, { 3.5f, 3.5f, 15.0f, 15.0f }, 3.2f, 2.2f);
            polygon (a, { { 12.0f, 12.0f }, { 21.0f, 15.5f }, { 17.0f, 17.0f }, { 15.5f, 21.0f } });
            break;

        case Icon::glide:
            // A note, and its pitch leaving it for a higher one.
            f.addRoundedRectangle (2.5f, 15.0f, 9.0f, 5.0f, 1.5f);
            s.startNewSubPath (11.5f, 17.5f);
            s.quadraticTo (17.0f, 17.5f, 21.5f, 5.0f);
            break;

        case Icon::snapGlide:
            // A horseshoe magnet, poles at the top.
            art.strokeWidth = 2.6f;
            s.startNewSubPath (6.0f, 4.0f);
            s.lineTo (6.0f, 12.5f);
            s.cubicTo (6.0f, 20.5f, 18.0f, 20.5f, 18.0f, 12.5f);
            s.lineTo (18.0f, 4.0f);
            s.startNewSubPath (10.5f, 4.0f);
            s.lineTo (10.5f, 12.5f);
            s.cubicTo (10.5f, 15.0f, 13.5f, 15.0f, 13.5f, 12.5f);
            s.lineTo (13.5f, 4.0f);
            a.addRectangle (4.7f, 3.0f, 7.1f, 3.6f);
            a.addRectangle (12.2f, 3.0f, 7.1f, 3.6f);
            break;

        case Icon::undo:
        case Icon::redo:
            // A curved arrow turning back to the left (mirrored for redo).
            s.startNewSubPath (19.0f, 18.0f);
            s.cubicTo (19.5f, 9.0f, 13.0f, 6.5f, 6.5f, 9.5f);
            line (s, 6.0f, 9.5f, 10.5f, 5.0f);
            line (s, 6.0f, 9.5f, 11.0f, 13.5f);

            if (icon == Icon::redo)
                s.applyTransform (juce::AffineTransform::scale (-1.0f, 1.0f).translated (box, 0.0f));
            break;

        case Icon::loop:
            s.startNewSubPath (4.5f, 12.0f);
            s.cubicTo (4.5f, 3.5f, 19.5f, 3.5f, 19.5f, 12.0f);
            line (s, 19.5f, 12.5f, 16.5f, 9.0f);
            line (s, 19.5f, 12.5f, 22.5f, 9.0f);
            s.startNewSubPath (19.5f, 12.0f);
            s.cubicTo (19.5f, 20.5f, 4.5f, 20.5f, 4.5f, 12.0f);
            line (s, 4.5f, 11.5f, 1.5f, 15.0f);
            line (s, 4.5f, 11.5f, 7.5f, 15.0f);
            break;

        case Icon::record:
            s.addEllipse (3.5f, 3.5f, 17.0f, 17.0f);
            a.addEllipse (7.0f, 7.0f, 10.0f, 10.0f);
            break;

        case Icon::settings:
            f = gear();
            break;

        case Icon::file:
            // A page with a folded corner.
            polygon (s, { { 5.5f, 3.0f }, { 14.0f, 3.0f }, { 19.5f, 8.5f }, { 19.5f, 21.0f }, { 5.5f, 21.0f } });
            s.startNewSubPath (14.0f, 3.0f);
            s.lineTo (14.0f, 8.5f);
            s.lineTo (19.5f, 8.5f);
            line (s, 9.0f, 13.0f, 16.0f, 13.0f);
            line (s, 9.0f, 17.0f, 16.0f, 17.0f);
            break;

        case Icon::sendToFl:
            // An arrow leaving a box to the right.
            s.startNewSubPath (11.0f, 4.5f);
            s.lineTo (3.5f, 4.5f);
            s.lineTo (3.5f, 19.5f);
            s.lineTo (11.0f, 19.5f);
            line (s, 8.5f, 12.0f, 21.0f, 12.0f);
            line (s, 21.0f, 12.0f, 15.5f, 6.5f);
            line (s, 21.0f, 12.0f, 15.5f, 17.5f);
            break;

        case Icon::tools:
            // A wrench: an open head at the top right, the handle down to the bottom left.
            art.strokeWidth = 3.2f;
            s.addCentredArc (16.0f, 8.0f, 4.6f, 4.6f, 0.0f, 1.35f, 2.0f * pi + 0.2f, true);
            line (s, 13.0f, 11.5f, 4.0f, 20.5f);
            break;
    }

    return art;
}

void drawIcon (juce::Graphics& g, Icon icon, juce::Rectangle<float> area, juce::Colour colour, juce::Colour accent)
{
    const auto art = iconArt (icon);
    const auto size = juce::jmin (area.getWidth(), area.getHeight());
    const auto scale = size / box;
    const auto transform = juce::AffineTransform::scale (scale).translated (area.getCentreX() - size * 0.5f,
                                                                           area.getCentreY() - size * 0.5f);

    g.setColour (accent);
    g.fillPath (art.accentFills, transform);
    g.setColour (colour);
    g.fillPath (art.fills, transform);
    g.strokePath (art.strokes, juce::PathStrokeType (juce::jmax (1.4f, art.strokeWidth * scale),
                                                      juce::PathStrokeType::curved, juce::PathStrokeType::rounded),
                  transform);
}

juce::MouseCursor makeIconCursor (Icon icon, juce::Point<float> hotSpot, juce::Colour colour, juce::Colour accent, int size)
{
    // Software image on purpose: a native one would keep the window's graphics system alive until
    // the plugin is unloaded.
    juce::Image image (juce::Image::ARGB, size, size, true, juce::SoftwareImageType());
    juce::Graphics g (image);
    const auto area = juce::Rectangle<float> (0.0f, 0.0f, (float) size, (float) size).reduced (1.0f);

    // A dark outline first, so the pointer stays visible over notes of any colour.
    const auto art = iconArt (icon);
    const auto scale = area.getWidth() / box;
    const auto transform = juce::AffineTransform::scale (scale).translated (area.getX(), area.getY());
    const juce::PathStrokeType outline (juce::jmax (1.4f, art.strokeWidth * scale) + 2.5f,
                                        juce::PathStrokeType::curved, juce::PathStrokeType::rounded);
    g.setColour (juce::Colours::black.withAlpha (0.85f));

    for (auto* p : { &art.strokes, &art.fills, &art.accentFills })
        g.strokePath (*p, outline, transform);

    drawIcon (g, icon, area, colour, accent);

    const auto hot = hotSpot * scale + area.getPosition();
    return { image, juce::roundToInt (hot.x), juce::roundToInt (hot.y) };
}

//==============================================================================
IconButton::IconButton (const juce::String& name, Icon iconToShow, const juce::String& tooltip)
    : juce::Button (name), icon (iconToShow)
{
    setTooltip (tooltip);
    setTitle (name);
}

void IconButton::setIcon (Icon newIcon)
{
    icon = newIcon;
    repaint();
}

void IconButton::setOpensMenu (bool shouldShowArrow)
{
    opensMenu = shouldShowArrow;
    repaint();
}

void IconButton::setAccent (std::function<juce::Colour()> accentFn)
{
    accent = std::move (accentFn);
}

void IconButton::paintButton (juce::Graphics& g, bool isMouseOverButton, bool isButtonDown)
{
    const auto on = getToggleState();
    getLookAndFeel().drawButtonBackground (g, *this,
                                           findColour (on ? juce::TextButton::buttonOnColourId : juce::TextButton::buttonColourId),
                                           isMouseOverButton, isButtonDown);

    auto colour = findColour (on ? juce::TextButton::textColourOnId : juce::TextButton::textColourOffId);
    auto accentColour = accent != nullptr ? accent() : Theme::note;

    if (! isEnabled())
    {
        colour = colour.withAlpha (0.35f);
        accentColour = accentColour.withAlpha (0.35f);
    }

    auto area = getLocalBounds().toFloat();

    if (opensMenu)
    {
        auto arrowArea = area.removeFromRight (11.0f).withTrimmedRight (3.0f);
        juce::Path arrow;
        arrow.startNewSubPath (arrowArea.getX(), arrowArea.getCentreY() - 2.0f);
        arrow.lineTo (arrowArea.getCentreX(), arrowArea.getCentreY() + 2.0f);
        arrow.lineTo (arrowArea.getRight(), arrowArea.getCentreY() - 2.0f);
        g.setColour (colour.withMultipliedAlpha (0.8f));
        g.strokePath (arrow, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    const auto size = juce::jmin (area.getWidth(), area.getHeight()) - 7.0f;
    drawIcon (g, icon, area.withSizeKeepingCentre (size, size), colour, accentColour);
}

} // namespace mperoll
