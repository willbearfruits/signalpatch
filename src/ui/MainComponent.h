#pragma once

#include "PatchCanvas.h"
#include "Theme.h"

namespace signalpatch::ui
{
class MainComponent final : public juce::Component,
                            private juce::Timer,
                            private juce::ChangeListener
{
public:
    MainComponent();
    ~MainComponent() override;

    void paint (juce::Graphics& graphics) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress& key) override;

    /** Loads a patch file (used for the command-line argument). The patch
        starts panic-muted like any other load. */
    void loadPatchFile (const juce::File& file);

private:
    class PaletteButton final : public juce::Button
    {
    public:
        PaletteButton (NodeKind kindToUse, const juce::String& name);
        void paintButton (juce::Graphics& graphics,
                          bool isMouseOverButton,
                          bool isButtonDown) override;

    private:
        NodeKind kind;
    };

    void timerCallback() override;
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void updateStatus();
    void showAudioSetup();
    void showSaveDialog();
    void showLoadDialog();
    void setMessage (juce::String message, bool error = false);
    void setCanvasZoom (float newZoom);
    void layoutPalette();
    [[nodiscard]] juce::Rectangle<int> visibleCanvasArea() const;

    LabLookAndFeel lookAndFeel;
    PatchEngine engine;
    PatchCanvas canvas;
    juce::Component canvasHost;
    juce::Viewport viewport;

    juce::Component paletteHost;
    juce::Viewport paletteViewport;
    std::vector<std::unique_ptr<PaletteButton>> paletteButtons;
    std::vector<std::unique_ptr<juce::Label>> paletteGroupLabels;

    juce::TextButton audioSetupButton { "AUDIO SETUP" };
    juce::TextButton saveButton { "SAVE" };
    juce::TextButton loadButton { "LOAD" };
    juce::TextButton panicButton { "PANIC" };
    juce::TextButton zoomOutButton { "-" };
    juce::TextButton zoomResetButton { "100%" };
    juce::TextButton zoomInButton { "+" };
    juce::Label deviceLabel;
    juce::Label performanceLabel;
    juce::Label messageLabel;
    juce::TooltipWindow tooltipWindow { this, 450 };

    std::unique_ptr<juce::FileChooser> fileChooser;
    NodeId inspectorNodeId = 0;
    std::optional<Connection> inspectorConnection;
    bool messageIsError = false;
    bool engineRunning = false;
    float canvasZoom = 1.0f;

    static constexpr int headerHeight = 62;
    static constexpr int paletteWidth = 194;
    static constexpr int inspectorWidth = 264;
    static constexpr int footerHeight = 30;
};
} // namespace signalpatch::ui
