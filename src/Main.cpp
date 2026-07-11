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
        quit();
    }

private:
    struct Options
    {
        juce::String patchPath;
        bool kiosk = false;
        bool unmute = false;
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
            auto* content = new ui::MainComponent();
            setContentOwned (content, true);
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

        void closeButtonPressed() override
        {
            juce::JUCEApplication::getInstance()->systemRequestedQuit();
        }
    };

    std::unique_ptr<MainWindow> mainWindow;
};
} // namespace signalpatch

START_JUCE_APPLICATION (signalpatch::SignalPatchApplication)
