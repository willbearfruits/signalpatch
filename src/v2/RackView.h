#pragma once

#include "../audio/PatchEngine.h"

#include <nanovg.h>

#include <optional>
#include <unordered_map>
#include <vector>

struct NVGLUframebuffer;

namespace signalpatch::v2
{
// The GPU rack. Draws straight from the engine's document and telemetry every
// frame it is asked to (the app only asks when input, engine state, telemetry
// or an animation changed), so idle costs nothing and a drag follows the
// pointer at display rate with no repaint machinery in between.
class RackView final : private juce::ChangeListener
{
public:
    RackView (PatchEngine& engine, NVGcontext* vg, int fontId);
    ~RackView() override;

    // Frame loop hooks.
    void tick (double nowSeconds);
    [[nodiscard]] bool needsRender() const noexcept { return dirty; }
    [[nodiscard]] bool isAnimating() const noexcept { return animating; }
    void render (int width, int height, float pixelRatio, double nowSeconds);

    // Input (window coordinates in logical pixels).
    void mouseMove (double x, double y);
    void mouseButton (int button, bool pressed, int mods, double x, double y);
    void scroll (double dx, double dy, int mods, double x, double y);
    void key (int key, bool pressed, int mods);

    void fitToPatch (int width, int height);

private:
    struct Layout
    {
        NodeId id = 0;
        NodeKind kind = NodeKind::gain;
        float w = 236.0f, h = 200.0f;
        int inputs = 0, outputs = 0;
        std::vector<int> knobParameters; // parameter indices drawn as knobs
        float previewY = 0.0f, previewH = 64.0f, controlsTop = 0.0f;
        bool hardware = false, stomp = false;
    };

    struct KnobDrag
    {
        NodeId node = 0;
        int parameter = -1;
        double startY = 0.0;
        float startNormalised = 0.0f;
    };

    struct CableDrag
    {
        NodeId sourceNode = 0;
        int sourcePort = 0;
        SignalType type = SignalType::audio;
        float x = 0.0f, y = 0.0f; // current end, world space
    };

    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void rebuildLayouts();
    [[nodiscard]] const Layout* layoutFor (NodeId id) const noexcept;
    [[nodiscard]] juce::Point<float> nodePosition (NodeId id) const noexcept;
    [[nodiscard]] juce::Point<float> inputPortCentre (const Layout&, juce::Point<float> origin, int port) const noexcept;
    [[nodiscard]] juce::Point<float> outputPortCentre (const Layout&, juce::Point<float> origin, int port) const noexcept;
    [[nodiscard]] juce::Point<float> knobCentre (const Layout&, juce::Point<float> origin, int knobIndex) const noexcept;
    [[nodiscard]] juce::Point<float> stompCentre (const Layout&, juce::Point<float> origin) const noexcept;
    [[nodiscard]] juce::Point<float> toWorld (double x, double y) const noexcept;
    [[nodiscard]] std::optional<Connection> cableNear (juce::Point<float> world, float radius) const;

    // Each module's static face plate (plate, rails, header, ports, knob arcs,
    // preview well) lives in a framebuffer rasterised at the current zoom and
    // redrawn only when the module or the settled zoom changes; a frame blits
    // it and paints the live parts (LED, port glow, scope, status) on top.
    struct PlateCache
    {
        NVGLUframebuffer* framebuffer = nullptr;
        int pixelWidth = 0, pixelHeight = 0;
        float scale = 0.0f;
        bool dirty = true;
    };
    static constexpr float platePadding = 8.0f; // room for the drop shadow

    void updatePlateCache (const Layout& layout, float scale);
    void invalidatePlate (NodeId id);
    void invalidateAllPlates();
    void releasePlates();

    void drawBackground (int width, int height);
    void drawCable (const Connection& connection, bool selected, double now);
    void drawPlateStatic (const Layout& layout, const NodeModel& model);
    void drawNode (const Layout& layout, double now);
    void drawHud (int width, int height, double now);
    void drawKnob (juce::Point<float> centre, float radius, float normalised, NVGcolor accent,
                   const juce::String& label, const juce::String& value);
    void bezier (juce::Point<float> a, juce::Point<float> b, juce::Point<float>& c1, juce::Point<float>& c2) const noexcept;

    void deleteSelection();
    void markDirty() noexcept { dirty = true; }

    PatchEngine& engine;
    NVGcontext* vg;
    int font;
    std::vector<Layout> layouts;
    std::unordered_map<NodeId, PlateCache> plates;
    float cachedPlateScale = 0.0f;
    float pixelRatio = 1.0f;
    bool structureDirty = true;
    bool dirty = true;
    bool animating = false;
    juce::uint32 lastTelemetry = 0;

    // Camera: pan in window pixels, zoom eased toward a target.
    double panX = 0.0, panY = 0.0;
    double zoom = 1.0, targetZoom = 1.0;
    double zoomAnchorX = 0.0, zoomAnchorY = 0.0;
    double lastTick = 0.0;

    double mouseX = 0.0, mouseY = 0.0;
    bool panning = false;
    double panStartX = 0.0, panStartY = 0.0, panOriginX = 0.0, panOriginY = 0.0;
    std::optional<NodeId> draggingNode;
    juce::Point<float> dragOffset;
    std::optional<KnobDrag> knobDrag;
    std::optional<CableDrag> cableDrag;

    NodeId selectedNode = 0;
    std::optional<Connection> selectedCable;
    juce::String message;
    double messageUntil = 0.0;
    double lastFrameMs = 0.0;
    double fps = 0.0;
    double lastRenderTime = 0.0;
    int plateRenders = 0; // plates rasterised in the last frame (HUD diagnostic)
};
} // namespace signalpatch::v2
