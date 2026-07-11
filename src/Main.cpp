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

    void initialise (const juce::String& commandLine) override
    {
        mainWindow = std::make_unique<MainWindow> (getApplicationName(), commandLine.unquoted().trim());
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
    class MainWindow final : public juce::DocumentWindow
    {
    public:
        explicit MainWindow (const juce::String& name, const juce::String& patchPath)
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
            if (patchPath.isNotEmpty())
                content->loadPatchFile (juce::File::getCurrentWorkingDirectory().getChildFile (patchPath));
            centreWithSize (1440, 900);
            setVisible (true);
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
