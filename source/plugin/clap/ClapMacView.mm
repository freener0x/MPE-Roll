// macOS only: a plain NSView for a CLAP plugin to put its own view into.

#import <AppKit/AppKit.h>

#include <juce_gui_extra/juce_gui_extra.h>

namespace mperoll
{

void setClapContainerView (juce::NSViewComponent&);

void setClapContainerView (juce::NSViewComponent& component)
{
    NSView* view = [[NSView alloc] initWithFrame: NSMakeRect (0, 0, 100, 100)];
    component.setView (view);   // the component keeps its own reference

   #if ! __has_feature (objc_arc)
    [view release];
   #endif
}

} // namespace mperoll
