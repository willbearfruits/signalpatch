#pragma once

#include "NodeComponent.h"

#include <optional>

namespace signalpatch::ui
{
class PatchCanvas final : public juce::Component,
                          private juce::ChangeListener,
                          private juce::Timer
{
public:
    explicit PatchCanvas (PatchEngine& engine);
    ~PatchCanvas() override;

    void addNodeAtVisibleCentre (NodeKind kind, juce::Rectangle<int> visibleArea);
    void deleteSelection();
    [[nodiscard]] NodeId selectedNode() const noexcept { return selectedNodeId; }
    void setSelectedNode (NodeId id) { selectNode (id); }

    std::function<void (const juce::String&)> onStatus;
    std::function<void (NodeId)> onSelectionChanged;
    std::function<void (std::optional<Connection>)> onCableSelectionChanged;

    void paint (juce::Graphics& graphics) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent& event) override;
    void mouseDrag (const juce::MouseEvent& event) override;
    void mouseUp (const juce::MouseEvent& event) override;
    bool keyPressed (const juce::KeyPress& key) override;

private:
    struct CableDrag
    {
        NodeId sourceNode = 0;
        int sourcePort = -1;
        SignalType type = SignalType::audio;
        juce::Point<float> start;
        juce::Point<float> current;
    };

    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void timerCallback() override;
    void ensureBackgroundCache();
    void showAddNodeMenu (juce::Point<float> position);
    void syncNodeComponents();
    void selectNode (NodeId id);
    void insertNodeOnCable (const Connection& cable, NodeKind kind, juce::Point<float> position);
    [[nodiscard]] NodeComponent* componentForNode (NodeId id) const noexcept;
    [[nodiscard]] juce::Path cablePath (juce::Point<float> start, juce::Point<float> end) const;
    [[nodiscard]] std::optional<Connection> cableNear (juce::Point<float> point) const;
    void beginCableDrag (NodeId sourceNode, int sourcePort, juce::Point<float> start);
    void updateCableDrag (juce::Point<float> point);
    void endCableDrag (juce::Point<float> point);
    void drawCable (juce::Graphics& graphics, const Connection& connection, bool selected);

    PatchEngine& engine;
    juce::Image backgroundCache;
    std::vector<std::unique_ptr<NodeComponent>> nodeComponents;
    NodeId selectedNodeId = 0;
    std::optional<Connection> selectedConnection;
    std::optional<CableDrag> cableDrag;
    std::vector<std::pair<Connection, juce::uint32>> cableVersions;

    // Per-cable cache: geometry flattened once (comets and dirty regions
    // walk the polyline instead of re-flattening the curve), and the
    // shadow+core / full-level glow rasterised once and blitted.
    struct CableCache
    {
        Connection connection;
        juce::Point<float> start, end;
        juce::Colour colour;
        SignalType type = SignalType::audio;
        bool feedback = false;
        juce::Path path;
        std::vector<juce::Point<float>> points;
        std::vector<float> distances; // cumulative, same size as points
        float length = 0.0f;
        juce::Rectangle<int> bounds;
        juce::Image core, glow;

        [[nodiscard]] juce::Point<float> pointAlong (float distance) const noexcept;
    };
    std::vector<CableCache> cableCaches;
    const CableCache* cableCacheFor (const Connection& connection); // last-painted source telemetry per cable
    float animationPhase = 0.0f;
    bool panning = false;
    juce::Point<int> panMouseDown;
    juce::Point<int> panViewStart;
};
} // namespace signalpatch::ui
