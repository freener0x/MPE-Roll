#include "WindowPlacement.h"

namespace mperoll::WindowPlacement
{

juce::Rectangle<int> workAreaNear (const juce::Component* near)
{
    const auto& displays = juce::Desktop::getInstance().getDisplays();
    const juce::Displays::Display* display = nullptr;

    if (near != nullptr && near->isShowing())
        display = displays.getDisplayForRect (near->getScreenBounds());

    if (display == nullptr)
        display = displays.getDisplayForPoint (juce::Desktop::getMousePosition().toFloat());

    if (display == nullptr)
        display = displays.getPrimaryDisplay();

    return display != nullptr ? display->userBounds.toNearestInt() : juce::Rectangle<int> (0, 0, 1280, 720);
}

juce::BorderSize<int> frameOf (const juce::Component& window)
{
    if (auto* peer = window.getPeer())
        if (const auto frame = peer->getFrameSizeIfPresent())
            return *frame;

    // Not on screen yet: a Windows 11 title bar and border at 100 %, which is near enough to
    // keep the title bar reachable until the real frame is known.
    return { 31, 8, 8, 8 };
}

juce::Rectangle<int> place (juce::Rectangle<int> client, juce::BorderSize<int> frame, juce::Rectangle<int> workArea,
                            bool mayShrink, bool centre, juce::Point<int> minimumSize)
{
    if (workArea.isEmpty())
        return client;

    // The room the client has once its frame is inside the area.
    const auto room = frame.subtractedFrom (workArea);

    if (mayShrink)
    {
        client.setWidth (juce::jlimit (juce::jmin (minimumSize.x, room.getWidth()), juce::jmax (1, room.getWidth()), client.getWidth()));
        client.setHeight (juce::jlimit (juce::jmin (minimumSize.y, room.getHeight()), juce::jmax (1, room.getHeight()), client.getHeight()));
    }

    if (centre)
        client.setCentre (room.getCentre());

    // Right and bottom first, then left and top, so a window bigger than the room ends up with its
    // title bar and left edge inside the area: the part that can be dragged and the close button's
    // side stay reachable, and the rest runs off the bottom and right.
    if (client.getRight() > room.getRight())     client.setX (room.getRight() - client.getWidth());
    if (client.getBottom() > room.getBottom())   client.setY (room.getBottom() - client.getHeight());
    if (client.getX() < room.getX())             client.setX (room.getX());
    if (client.getY() < room.getY())             client.setY (room.getY());

    return client;
}

namespace
{
    juce::Point<int> minimumSizeOf (juce::Component& window)
    {
        if (auto* resizable = dynamic_cast<juce::ResizableWindow*> (&window))
            if (auto* constrainer = resizable->getConstrainer())
                return { juce::jmax (160, constrainer->getMinimumWidth()), juce::jmax (100, constrainer->getMinimumHeight()) };

        return { 160, 100 };
    }

    bool apply (juce::Component& window, juce::Rectangle<int> workArea, bool mayShrink, bool centre)
    {
        const auto before = window.getBounds();
        const auto after = place (before, frameOf (window), workArea, mayShrink, centre, minimumSizeOf (window));

        if (after == before)
            return false;

        window.setBounds (after);
        return after.getWidth() < before.getWidth() || after.getHeight() < before.getHeight();
    }
}

bool showOnDisplayOf (juce::Component& window, const juce::Component* near, bool mayShrink)
{
    return apply (window, workAreaNear (near), mayShrink, true);
}

bool keepOnScreen (juce::Component& window, bool mayShrink)
{
    const auto& displays = juce::Desktop::getInstance().getDisplays();

    if (const auto* display = displays.getDisplayForRect (window.getScreenBounds()))
        return apply (window, display->userBounds.toNearestInt(), mayShrink, false);

    return false;
}

} // namespace mperoll::WindowPlacement
