#pragma once

#include <JuceHeader.h>

struct GLFWwindow;

namespace signalpatch::v2
{
class RackView;

// Developer hook for checking touch and pad flows on a machine without a
// touch screen: SIGNALPATCH_UI_SCRIPT=<file> plays the file's commands once
// the window is up, one per line, coordinates in the UI's logical pixels:
//
//   wait <seconds>               tap <x> <y>              hold <x> <y> <seconds>
//   drag <x1> <y1> <x2> <y2>     key <name> [ctrl] [shift] text <characters>
//   shot <file.png>              quit
//
// Key names are letters, digits, Enter, Escape, Tab, Backspace, Delete,
// Left, Right, Up, Down, PageUp, PageDown. '#' starts a comment.
class UiScript
{
public:
    UiScript (GLFWwindow* window, RackView& rack, const juce::File& file);

    /** Runs what is due; true when a screenshot is wanted after this frame's render. */
    bool step (double now);
    /** Writes the frame that was just rendered to the pending shot's file. */
    void writeShot (int framebufferWidth, int framebufferHeight);

private:
    void press (float x, float y, bool down);
    void move (float x, float y);

    GLFWwindow* window;
    RackView& rack;
    juce::StringArray lines;
    int next = 0;
    double resumeAt = 0.0;
    juce::File shotFile;
    // A press that lifts later (tap, hold) or a drag on its way.
    struct Gesture
    {
        float x1 = 0.0f, y1 = 0.0f, x2 = 0.0f, y2 = 0.0f;
        double start = 0.0, duration = 0.0;
        bool dragging = false;
    };
    std::optional<Gesture> gesture;
};
} // namespace signalpatch::v2
