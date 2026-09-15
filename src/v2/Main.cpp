// SignalPatch 2: the GPU rack. GLFW window (native Wayland where available),
// OpenGL 3.3 + NanoVG for the drawing, the same PatchEngine underneath. JUCE
// is used headless: its message pump runs from the frame loop so engine
// timers, async model loads and change broadcasts keep working.
#include <JuceHeader.h>

#include "../audio/PatchEngine.h"
#include "RackView.h"

#include <GLFW/glfw3.h>
#include <nanovg.h>
#define NANOVG_GL3 1 // declarations only; the implementation lives in NanoVGImpl.cpp
#include <nanovg_gl.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace
{
    signalpatch::v2::RackView* rackFor (GLFWwindow* window)
    {
        return static_cast<signalpatch::v2::RackView*> (glfwGetWindowUserPointer (window));
    }

    int loadFont (NVGcontext* vg)
    {
        const char* candidates[] {
            "/usr/share/fonts/TTF/Inter-Regular.ttf",
            "/usr/share/fonts/inter/Inter-Regular.ttf",
            "/usr/share/fonts/TTF/Roboto-Regular.ttf",
            "/usr/share/fonts/noto/NotoSans-Regular.ttf",
            "/usr/share/fonts/TTF/DejaVuSans.ttf",
            "/usr/share/fonts/dejavu/DejaVuSans.ttf",
        };
        for (const auto* path : candidates)
            if (juce::File (path).existsAsFile())
            {
                const auto id = nvgCreateFont (vg, "sans", path);
                if (id >= 0)
                    return id;
            }
        std::fprintf (stderr, "SignalPatch 2: no usable TTF font found\n");
        return -1;
    }
} // namespace

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI juceInit; // message manager + core services, no GUI

    if (! glfwInit())
    {
        std::fprintf (stderr, "GLFW init failed\n");
        return 1;
    }
    glfwWindowHint (GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint (GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint (GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint (GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
    glfwWindowHint (GLFW_STENCIL_BITS, 8);
    glfwWindowHint (GLFW_SAMPLES, 0);

    // Flags that shape the window come first; the rest wait for the rack.
    bool kiosk = false;
    for (int index = 1; index < argc; ++index)
    {
        const juce::String argument (argv[index]);
        if (argument == "--kiosk")
            kiosk = true;
        else if (argument == "--help" || argument == "-h")
        {
            std::printf ("SignalPatch [options] [patch.signalpatch | bundle.zip]\n"
                         "  --board        start on the pedalboard view\n"
                         "  --kiosk        fullscreen on the primary monitor, no decorations (handheld / stage)\n"
                         "  --unmute       fade in right away instead of the safe muted start\n"
                         "  --scale=1.25   UI scale (also SIGNALPATCH_SCALE, and Ctrl +/- in the app)\n");
            glfwTerminate();
            return 0;
        }
    }
    GLFWmonitor* monitor = nullptr;
    int windowW = 1600, windowH = 1000;
    if (kiosk)
    {
        monitor = glfwGetPrimaryMonitor();
        if (const auto* mode = monitor != nullptr ? glfwGetVideoMode (monitor) : nullptr)
        {
            windowW = mode->width;
            windowH = mode->height;
            glfwWindowHint (GLFW_RED_BITS, mode->redBits);
            glfwWindowHint (GLFW_GREEN_BITS, mode->greenBits);
            glfwWindowHint (GLFW_BLUE_BITS, mode->blueBits);
            glfwWindowHint (GLFW_REFRESH_RATE, mode->refreshRate);
        }
        glfwWindowHint (GLFW_DECORATED, GLFW_FALSE);
        glfwWindowHint (GLFW_AUTO_ICONIFY, GLFW_FALSE); // losing focus must not drop the rig off the screen
    }
    auto* window = glfwCreateWindow (windowW, windowH, "SignalPatch", monitor, nullptr);
    if (window == nullptr)
    {
        std::fprintf (stderr, "GLFW window creation failed\n");
        glfwTerminate();
        return 1;
    }
    glfwMakeContextCurrent (window);
    glfwSwapInterval (1);

    auto* vg = nvgCreateGL3 (NVG_ANTIALIAS | NVG_STENCIL_STROKES);
    if (vg == nullptr)
    {
        std::fprintf (stderr, "NanoVG init failed\n");
        return 1;
    }
    const auto font = loadFont (vg);

    signalpatch::PatchEngine engine;
    const auto result = engine.initialise();
    if (result.failed())
        std::fprintf (stderr, "Audio offline: %s\n", result.getErrorMessage().toRawUTF8());
    signalpatch::v2::RackView rack (engine, vg, font);
    bool startOnBoard = false;
    {
        float contentX = 1.0f, contentY = 1.0f;
        glfwGetWindowContentScale (window, &contentX, &contentY);
        if (contentX > 1.05f && std::abs (rack.getUiScale() - 1.0f) < 1.0e-3f)
            rack.setUiScale (contentX); // seed from the display the first time
        if (const auto* env = std::getenv ("SIGNALPATCH_SCALE"))
            rack.setUiScale (static_cast<float> (std::atof (env)));
    }
    bool unmute = false;
    for (int index = 1; index < argc; ++index)
    {
        const juce::String argument (argv[index]);
        if (argument == "--board")
            startOnBoard = true;
        else if (argument == "--unmute")
            unmute = true;
        else if (argument.startsWith ("--scale="))
            rack.setUiScale (argument.fromFirstOccurrenceOf ("=", false, false).getFloatValue());
        else if (argv[index][0] != '-')
            rack.loadPatchFromCommandLine (juce::File::getCurrentWorkingDirectory().getChildFile (argv[index]));
    }
    if (unmute)
        rack.unmuteAtStart(); // a stage boot: the rig should sound without a key press
    glfwSetWindowUserPointer (window, &rack);
    glfwSetCursorPosCallback (window, [] (GLFWwindow* w, double x, double y) { rackFor (w)->mouseMove (x, y); });
    glfwSetMouseButtonCallback (window, [] (GLFWwindow* w, int button, int action, int mods)
    {
        double x = 0.0, y = 0.0;
        glfwGetCursorPos (w, &x, &y);
        rackFor (w)->mouseButton (button, action == GLFW_PRESS, mods, x, y);
    });
    glfwSetScrollCallback (window, [] (GLFWwindow* w, double dx, double dy)
    {
        double x = 0.0, y = 0.0;
        glfwGetCursorPos (w, &x, &y);
        int mods = 0;
        if (glfwGetKey (w, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS || glfwGetKey (w, GLFW_KEY_RIGHT_SHIFT) == GLFW_PRESS)
            mods |= GLFW_MOD_SHIFT;
        rackFor (w)->scroll (dx, dy, mods, x, y);
    });
    glfwSetKeyCallback (window, [] (GLFWwindow* w, int key, int, int action, int mods)
    {
        if (action == GLFW_PRESS || action == GLFW_REPEAT)
            rackFor (w)->key (key, true, mods);
    });
    glfwSetCharCallback (window, [] (GLFWwindow* w, unsigned int codepoint) { rackFor (w)->character (codepoint); });
    glfwSetWindowCloseCallback (window, [] (GLFWwindow* w)
    {
        glfwSetWindowShouldClose (w, GLFW_FALSE); // the rack decides after the unsaved-changes question
        rackFor (w)->requestQuit();
    });

    {
        int width = 0, height = 0;
        glfwGetWindowSize (window, &width, &height);
        rack.fitToPatch (static_cast<int> (width / rack.getUiScale()), static_cast<int> (height / rack.getUiScale()));
        if (startOnBoard)
            rack.showBoard();
    }

    while (! glfwWindowShouldClose (window))
    {
        // Sleep until input arrives unless something is moving; the JUCE pump
        // still needs a turn now and then for timers and async loads.
        if (rack.isAnimating() || rack.needsRender())
            glfwPollEvents();
        else
            glfwWaitEventsTimeout (engine.hasMidiInputs() ? 0.004 : 0.02); // MIDI is applied on this loop: keep it snappy
        juce::MessageManager::getInstance()->runDispatchLoopUntil (1);

        const auto now = glfwGetTime();
        rack.tick (now);
        if (! rack.needsRender())
            continue;

        int width = 0, height = 0, fbWidth = 0, fbHeight = 0;
        glfwGetWindowSize (window, &width, &height);
        glfwGetFramebufferSize (window, &fbWidth, &fbHeight);
        glViewport (0, 0, fbWidth, fbHeight);
        glClearColor (0.05f, 0.067f, 0.078f, 1.0f);
        glClear (GL_COLOR_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
        rack.render (width, height, width > 0 ? static_cast<float> (fbWidth) / static_cast<float> (width) : 1.0f, now);
        glfwSwapBuffers (window);
    }

    engine.shutdown();
    nvgDeleteGL3 (vg);
    glfwDestroyWindow (window);
    glfwTerminate();
    return 0;
}
