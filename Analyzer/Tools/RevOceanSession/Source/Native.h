#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace revocean
{

/** The number the window server gives the window of `component`, or 0 without one. With it the system's
    screencapture can take a picture of this window alone (`screencapture -l <number>`). */
int nativeWindowNumber (juce::Component& component);

/** Tells macOS that this process works for its user until it ends. Without it App Nap slows a session
    whose window is hidden or covered to a tenth of its speed, and an idle machine may go to sleep. */
void stayAwake();

} // namespace revocean
