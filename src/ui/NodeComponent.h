#pragma once

#include "../audio/PatchEngine.h"
#include "Theme.h"

#include <functional>

namespace signalpatch::ui
{
class NodeComponent final : public juce::Component
{
public:
    NodeComponent (PatchEngine& engine, NodeId nodeId);
    ~NodeComponent() override;

    /** Repaints only the live layer (LED, scope, port glow, status) — the
        face plate is cached, so the 30 Hz canvas timer never re-rasterises it. */
    void repaintLive();

    [[nodiscard]] NodeId getNodeId() const noexcept { return id; }
    [[nodiscard]] int preferredWidth() const noexcept;
    [[nodiscard]] int preferredHeight() const noexcept;
    [[nodiscard]] int signature() const noexcept;

    void setSelected (bool shouldBeSelected);
    [[nodiscard]] bool isSelected() const noexcept { return selected; }

    [[nodiscard]] juce::Point<float> inputPortCentreInParent (int port) const;
    [[nodiscard]] juce::Point<float> outputPortCentreInParent (int port) const;
    [[nodiscard]] std::optional<int> inputPortNear (juce::Point<float> pointInParent, float radius) const;

    std::function<void (NodeId)> onSelected;
    std::function<void (NodeId)> onRemoveRequested;
    std::function<void (NodeId, int, juce::Point<float>)> onCableDragStarted;
    std::function<void (juce::Point<float>)> onCableDragged;
    std::function<void (juce::Point<float>)> onCableDragEnded;

    void paint (juce::Graphics& graphics) override;
    void paintPlate (juce::Graphics& graphics);
    void paintLive (juce::Graphics& graphics);
    void resized() override;
    void mouseDown (const juce::MouseEvent& event) override;
    void mouseDrag (const juce::MouseEvent& event) override;
    void mouseUp (const juce::MouseEvent& event) override;
    void mouseDoubleClick (const juce::MouseEvent& event) override;

    /** Rename dialog shared by the Edit menu, F2 and the node context menu. */
    static void showRenameDialog (juce::Component* parent, PatchEngine& engine, NodeId id);

private:
    struct ParameterWidgets
    {
        int parameterIndex = -1;
        std::unique_ptr<juce::Slider> value;
        std::unique_ptr<juce::Slider> depth;
    };

    struct CommandButton
    {
        std::unique_ptr<juce::TextButton> button;
        juce::String command;
        juce::Colour activeColour;
    };

    [[nodiscard]] const NodeModel* model() const noexcept;
    [[nodiscard]] juce::Point<float> localInputPortCentre (int port) const;
    [[nodiscard]] juce::Point<float> localOutputPortCentre (int port) const;
    [[nodiscard]] std::optional<int> outputPortNear (juce::Point<float> localPoint, float radius) const;
    [[nodiscard]] juce::Rectangle<float> previewBounds() const noexcept;
    void drawPort (juce::Graphics& graphics,
                   juce::Point<float> centre,
                   const PortInfo& port,
                   bool output,
                   bool showLabel = true,
                   float signalLevel = 0.0f);
    void drawWaveform (juce::Graphics& graphics,
                       juce::Rectangle<float> area,
                       const WaveformSnapshot& waveform,
                       juce::Colour colour,
                       float alpha);
    void drawPreviewWell (juce::Graphics& graphics);
    void drawPreview (juce::Graphics& graphics, const NodeModel& node);
    bool editSequencerStepAt (juce::Point<float> localPoint);
    bool editDrumCellAt (juce::Point<float> localPoint);
    void paintOverChildren (juce::Graphics& graphics) override;
    void refreshCommandButtons();
    void showNodeMenu();
    void showParameterMenu (int parameterIndex);
    void showPortMenu (bool output, int port);
    void showSetValueDialog (int parameterIndex);
    void chooseNamModel();
    void chooseImpulse (int slot);
    void applyScriptText();
    [[nodiscard]] bool hasStompSwitch() const noexcept;
    [[nodiscard]] juce::Point<float> stompCentre() const noexcept;

    class LiveLayer;

    PatchEngine& engine;
    NodeId id;
    std::unique_ptr<LiveLayer> liveLayer;
    std::vector<ParameterWidgets> parameterWidgets;
    std::unique_ptr<juce::TextButton> safetyResetButton;
    std::vector<CommandButton> commandButtons;
    std::unique_ptr<juce::TextEditor> scriptEditor;
    std::unique_ptr<juce::TextButton> scriptApplyButton;
    std::unique_ptr<juce::FileChooser> modelChooser;
    // The static face plate is rasterised into this image once per change
    // (size, selection); paint() then blits it. Unlike setBufferedToImage the
    // cache survives the live layer's 30 Hz repaints.
    juce::Image plateCache;
    bool plateDirty = true;
    juce::uint32 lastLiveVersion = 0xffffffffu;
    juce::String lastLiveStatus;
    int lastLiveStep = -2;
    bool lastLiveTripped = false;
    bool selected = false;
    bool draggingNode = false;
    bool draggingCable = false;
    bool editingSequencerStep = false;
    int draggingOutputPort = -1;
    juce::Point<float> dragOffset;

    static constexpr float railHeight = 10.0f;
    static constexpr float headerHeight = 34.0f;
    static constexpr float portSpacing = 19.0f;
    static constexpr float firstPortY = 64.0f;
    static constexpr float controlsTop = 152.0f;
    static constexpr float controlRowHeight = 94.0f;
    static constexpr int stompZoneHeight = 44;
};
} // namespace signalpatch::ui
