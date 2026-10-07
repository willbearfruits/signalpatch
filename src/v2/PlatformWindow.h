#pragma once

struct GLFWwindow;

namespace signalpatch::v2
{
/** Platform settings for the rack's window that GLFW has no hint for. On Windows:
    a finger's press arrives at once (Windows' own press-and-hold, which holds the
    press back and turns it into a right click, is switched off; the rack has its
    own long press), and no touch feedback rings are drawn over the rack.
    Elsewhere it does nothing. */
void preparePlatformWindow (GLFWwindow* window);
} // namespace signalpatch::v2
