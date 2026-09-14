#include <cstdlib>
#include <JuceHeader.h>

#include "ui/MainComponent.h"

namespace signalpatch
{
class SignalPatchApplication final : public juce::JUCEApplication
{
public:
    [[nodiscard]] const juce::String getApplicationName() override { return "SignalPatch"; }
    [[nodiscard]] const juce::String getApplicationVersion() override { return ProjectInfo::versionString; }
    [[nodiscard]] bool moreThanOneInstanceAllowed() override { return true; }

    void initialise (const juce::String&) override
    {
        // Flags for appliance/handheld use: --kiosk fullscreens without
        // chrome, --unmute skips the restore-muted safety pause (explicit
        // opt-in for a dedicated machine). A bare argument is a patch file.
        Options options;
        for (const auto& argument : getCommandLineParameterArray())
        {
            if (argument == "--kiosk")
                options.kiosk = true;
            else if (argument == "--gl")
                options.openGL = true;
            else if (argument == "--unmute")
                options.unmute = true;
            else if (! argument.startsWith ("-"))
                options.patchPath = argument.unquoted();
        }
        mainWindow = std::make_unique<MainWindow> (getApplicationName(), options);
    }

    void shutdown() override
    {
        mainWindow.reset();
    }

    void systemRequestedQuit() override
    {
        if (mainWindow != nullptr && mainWindow->content != nullptr)
            mainWindow->content->confirmDiscardChanges ([] { juce::JUCEApplication::quit(); });
        else
            quit();
    }

private:
    struct Options
    {
        juce::String patchPath;
        bool kiosk = false;
        bool unmute = false;
        bool openGL = false; // --gl: route painting through juce::OpenGLContext (measured slower here; opt-in)
    };

    class MainWindow final : public juce::DocumentWindow
    {
    public:
        MainWindow (const juce::String& name, const Options& options)
            : DocumentWindow (name,
                              ui::colours::workspace,
                              juce::DocumentWindow::allButtons,
                              true)
        {
            setUsingNativeTitleBar (true);
            setResizable (true, true);
            setResizeLimits (1080, 680, 3840, 2160);
            content = new ui::MainComponent();
            setContentOwned (content, true);
            // Optional GPU compositing (--gl or SIGNALPATCH_GL=1). JUCE's GL
            // renderer still builds path geometry on the CPU and adds a
            // full-frame sync, so on the reference desktop (RTX 3060, XWayland)
            // it measured ~2.5x the CPU of the software renderer; kept for
            // machines where the trade-off differs.
            if (options.openGL || std::getenv ("SIGNALPATCH_GL") != nullptr)
            {
                openGLContext.setContinuousRepainting (false);
                // No vsync: the NVIDIA GLX swap busy-waits for vblank, which
                // turns a 30 Hz repaint into a spinning core. Repaints are
                // already timer-paced, so tearing is not a concern.
                openGLContext.setSwapInterval (0);
                openGLContext.attachTo (*content);
            }
            if (options.patchPath.isNotEmpty())
                content->loadPatchFile (juce::File::getCurrentWorkingDirectory()
                                            .getChildFile (options.patchPath));
            if (options.unmute)
                content->fadeInNow();
            centreWithSize (1440, 900);
            setVisible (true);
            if (options.kiosk)
            {
                setTitleBarHeight (0);
                setFullScreen (true);
                juce::Desktop::getInstance().setKioskModeComponent (this, false);
            }
        }

        ~MainWindow() override
        {
            openGLContext.detach(); // before the content component goes away
        }

        void closeButtonPressed() override
        {
            juce::JUCEApplication::getInstance()->systemRequestedQuit();
        }

        ui::MainComponent* content = nullptr; // owned by the window's content

    private:
        juce::OpenGLContext openGLContext;
    };

    std::unique_ptr<MainWindow> mainWindow;
};
} // namespace signalpatch

START_JUCE_APPLICATION (signalpatch::SignalPatchApplication)
