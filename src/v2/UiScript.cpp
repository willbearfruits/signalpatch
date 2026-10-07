#include "UiScript.h"
#include "RackView.h"

#include <glad/gl.h>
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <cstdio>
#include <vector>

namespace signalpatch::v2
{
namespace
{
    int keyCodeFor (const juce::String& name)
    {
        const auto upper = name.toUpperCase();
        if (upper.length() == 1)
        {
            const auto c = upper[0];
            if (c >= 'A' && c <= 'Z')
                return GLFW_KEY_A + (c - 'A');
            if (c >= '0' && c <= '9')
                return GLFW_KEY_0 + (c - '0');
        }
        static const std::pair<const char*, int> named[] {
            { "ENTER", GLFW_KEY_ENTER }, { "ESCAPE", GLFW_KEY_ESCAPE }, { "TAB", GLFW_KEY_TAB },
            { "BACKSPACE", GLFW_KEY_BACKSPACE }, { "DELETE", GLFW_KEY_DELETE }, { "LEFT", GLFW_KEY_LEFT },
            { "RIGHT", GLFW_KEY_RIGHT }, { "UP", GLFW_KEY_UP }, { "DOWN", GLFW_KEY_DOWN },
            { "PAGEUP", GLFW_KEY_PAGE_UP }, { "PAGEDOWN", GLFW_KEY_PAGE_DOWN }, { "SPACE", GLFW_KEY_SPACE },
        };
        for (const auto& [text, code] : named)
            if (upper == text)
                return code;
        return GLFW_KEY_UNKNOWN;
    }
} // namespace

UiScript::UiScript (GLFWwindow* windowToDrive, RackView& rackToDrive, const juce::File& file)
    : window (windowToDrive), rack (rackToDrive)
{
    lines.addLines (file.loadFileAsString());
    if (lines.isEmpty())
        std::fprintf (stderr, "UI script: nothing to play in %s\n", file.getFullPathName().toRawUTF8());
}

void UiScript::move (float x, float y)
{
    const auto scale = rack.getUiScale();
    rack.mouseMove (x * scale, y * scale);
}

void UiScript::press (float x, float y, bool down)
{
    const auto scale = rack.getUiScale();
    rack.mouseMove (x * scale, y * scale);
    rack.mouseButton (GLFW_MOUSE_BUTTON_LEFT, down, 0, x * scale, y * scale);
}

bool UiScript::step (double now)
{
    if (gesture.has_value())
    {
        const auto& g = *gesture;
        const auto t = g.duration > 0.0 ? juce::jlimit (0.0, 1.0, (now - g.start) / g.duration) : 1.0;
        if (g.dragging)
            move (g.x1 + (g.x2 - g.x1) * static_cast<float> (t), g.y1 + (g.y2 - g.y1) * static_cast<float> (t));
        if (t < 1.0)
            return false;
        press (g.x2, g.y2, false);
        gesture.reset();
        resumeAt = now + 0.05;
        return false;
    }
    while (next < lines.size() && now >= resumeAt)
    {
        const auto line = lines[next++].upToFirstOccurrenceOf ("#", false, false).trim();
        if (line.isEmpty())
            continue;
        juce::StringArray words;
        words.addTokens (line, " \t", "");
        words.removeEmptyStrings();
        const auto command = words[0].toLowerCase();
        auto number = [&words] (int index) { return words[index].getFloatValue(); };
        if (command == "wait")
        {
            resumeAt = now + number (1);
            return false;
        }
        if (command == "tap" || command == "hold")
        {
            press (number (1), number (2), true);
            gesture = Gesture { number (1), number (2), number (1), number (2), now, command == "tap" ? 0.06 : static_cast<double> (number (3)), false };
            return false;
        }
        if (command == "drag")
        {
            press (number (1), number (2), true);
            gesture = Gesture { number (1), number (2), number (3), number (4), now, 0.4, true };
            return false;
        }
        if (command == "key")
        {
            int mods = 0;
            if (words.contains ("ctrl", true))
                mods |= GLFW_MOD_CONTROL;
            if (words.contains ("shift", true))
                mods |= GLFW_MOD_SHIFT;
            const auto code = keyCodeFor (words[1]);
            rack.key (code, true, mods);
            // GLFW follows a printable key with its character.
            if (words[1].length() == 1 && (mods & GLFW_MOD_CONTROL) == 0)
                rack.character (static_cast<unsigned int> ((mods & GLFW_MOD_SHIFT) != 0 ? words[1].toUpperCase()[0] : words[1].toLowerCase()[0]));
            resumeAt = now + 0.05;
            return false;
        }
        if (command == "text")
        {
            for (const auto c : line.fromFirstOccurrenceOf (" ", false, false))
                rack.character (static_cast<unsigned int> (c));
            continue;
        }
        if (command == "shot")
        {
            shotFile = juce::File (words[1]);
            resumeAt = now + 0.05;
            return true;
        }
        if (command == "quit")
        {
            glfwSetWindowShouldClose (window, GLFW_TRUE);
            return false;
        }
        std::fprintf (stderr, "UI script: unknown command '%s'\n", line.toRawUTF8());
    }
    return false;
}

void UiScript::writeShot (int framebufferWidth, int framebufferHeight)
{
    if (shotFile == juce::File() || framebufferWidth <= 0 || framebufferHeight <= 0)
        return;
    std::vector<unsigned char> pixels (static_cast<std::size_t> (framebufferWidth) * static_cast<std::size_t> (framebufferHeight) * 4);
    glPixelStorei (GL_PACK_ALIGNMENT, 1);
    glReadPixels (0, 0, framebufferWidth, framebufferHeight, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    juce::Image image (juce::Image::RGB, framebufferWidth, framebufferHeight, false);
    {
        juce::Image::BitmapData bitmap (image, juce::Image::BitmapData::writeOnly);
        for (int y = 0; y < framebufferHeight; ++y)
        {
            const auto* row = pixels.data() + static_cast<std::size_t> (framebufferHeight - 1 - y) * static_cast<std::size_t> (framebufferWidth) * 4;
            for (int x = 0; x < framebufferWidth; ++x)
                bitmap.setPixelColour (x, y, juce::Colour (row[x * 4], row[x * 4 + 1], row[x * 4 + 2]));
        }
    }
    shotFile.deleteFile();
    juce::FileOutputStream stream (shotFile);
    juce::PNGImageFormat png;
    if (! stream.openedOk() || ! png.writeImageToStream (image, stream))
        std::fprintf (stderr, "UI script: could not write %s\n", shotFile.getFullPathName().toRawUTF8());
    shotFile = juce::File();
}
} // namespace signalpatch::v2
