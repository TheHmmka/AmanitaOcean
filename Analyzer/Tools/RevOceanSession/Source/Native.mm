#include "Native.h"

#import <AppKit/AppKit.h>

namespace revocean
{

int nativeWindowNumber (juce::Component& component)
{
    auto* peer = component.getPeer();
    if (peer == nullptr)
        return 0;

    auto* view = static_cast<NSView*> (peer->getNativeHandle());
    return static_cast<int> ([[view window] windowNumber]);
}

void stayAwake()
{
    // The activity lasts as long as its token; this one is kept until the process ends.
    static id token = [[[NSProcessInfo processInfo] beginActivityWithOptions: NSActivityUserInitiated
                                                                       reason: @"Rev OCEAN session"] retain];
    juce::ignoreUnused (token);
}

} // namespace revocean
