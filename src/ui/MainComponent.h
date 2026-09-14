#pragma once

#include "PatchCanvas.h"
#include "Theme.h"

#include <functional>

namespace signalpatch::ui
{
class MainComponent final : public juce::Component,
                            public juce::MenuBarModel,
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

    /** --unmute: skip the restore-muted safety pause (appliance opt-in). */
    void fadeInNow();

    /** Runs `proceed` now if nothing is unsaved, otherwise after the user
        chooses Save / Don't save (Cancel drops it). Used by New, Open, Quit. */
    void confirmDiscardChanges (std::function<void()> proceed);

    // juce::MenuBarModel
    juce::StringArray getMenuBarNames() override;
    juce::PopupMenu getMenuForIndex (int index, const juce::String& name) override;
    void menuItemSelected (int itemId, int index) override;

    enum MenuItem
    {
        menuNew = 1, menuOpen, menuSave, menuSaveAs, menuImport, menuExportBundle, menuAudioSetup, menuQuit,
        menuUndo, menuRedo, menuDelete, menuDuplicate, menuRename, menuPanic,
        menuZoomIn, menuZoomOut, menuZoomReset,
        menuOpenModelsFolder, menuOpenIrFolder, menuAbout, menuWebsite,
        menuRecentBase = 1000
    };

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
    void saveCurrent();
    void showImportDialog();
    void showExportBundleDialog();
    [[nodiscard]] static juce::File projectsFolder();
    void newPatch();
    void setCurrentFile (const juce::File& file);
    void rememberRecentFile (const juce::File& file);
    void updateWindowTitle();
    void duplicateSelected();
    void renameSelected();
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

    juce::MenuBarComponent menuBar;
    std::unique_ptr<juce::PropertiesFile> settings;
    juce::RecentlyOpenedFilesList recentFiles;
    juce::File currentFile;
    juce::String lastWindowTitle;

    std::unique_ptr<juce::FileChooser> fileChooser;
    NodeId inspectorNodeId = 0;
    std::optional<Connection> inspectorConnection;
    bool messageIsError = false;
    bool engineRunning = false;
    float canvasZoom = 1.0f;

    static constexpr int menuHeight = 26;
    static constexpr int headerBarHeight = 62;
    static constexpr int headerHeight = menuHeight + headerBarHeight; // top of the content area
    static constexpr int paletteWidth = 194;
    static constexpr int inspectorWidth = 264;
    static constexpr int footerHeight = 30;
};
} // namespace signalpatch::ui
