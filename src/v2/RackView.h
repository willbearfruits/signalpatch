#pragma once

#include "../audio/PatchEngine.h"
#include "Menu.h"
#include "ToneBrowser.h"

#include <nanovg.h>

#include <array>
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
    void character (unsigned int codepoint);

    void fitToPatch (int width, int height);
    /** Global UI scale (logical pixels are multiplied by it); saved in settings. */
    void setUiScale (float scale);
    [[nodiscard]] float getUiScale() const noexcept { return uiScale; }
    void loadPatchFromCommandLine (const juce::File& file) { openPatchFile (file); }
    void unmuteAtStart() { engine.setPanicMuted (false); dirty = true; }
    /** --board: open on the pedalboard instead of the rack. */
    void showBoard() { setMode (Mode::board); }
    /** Close request from the window or Ctrl+Q: asks about unsaved changes first. */
    void requestQuit();

private:
    struct Button
    {
        juce::String label, command;
        int row = 0;
        NVGcolor active {};
    };

    struct Layout
    {
        NodeId id = 0;
        NodeKind kind = NodeKind::gain;
        float w = 236.0f, h = 200.0f;
        int inputs = 0, outputs = 0;
        std::vector<int> knobParameters; // parameter indices drawn as knobs
        std::vector<Button> buttons;     // transport / model / IR commands
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
    void drawPreviewContent (const Layout& layout, const NodeModel& model, juce::Point<float> origin);
    [[nodiscard]] juce::Rectangle<float> previewArea (const Layout& layout, juce::Point<float> origin) const noexcept;
    bool editPreviewAt (const Layout& layout, juce::Point<float> origin, juce::Point<float> world, bool firstPress);
    void showAudioMenu (double x, double y);
    void drawHud (int width, int height, double now);
    void drawKnob (juce::Point<float> centre, float radius, float normalised, NVGcolor accent,
                   const juce::String& label, const juce::String& value);
    void bezier (juce::Point<float> a, juce::Point<float> b, juce::Point<float>& c1, juce::Point<float>& c2) const noexcept;

    void deleteSelection();
    void markDirty() noexcept { dirty = true; }
    void say (const juce::String& text);

    // Context menus, all built from the same MenuItem vocabulary.
    void showCanvasMenu (double x, double y);
    [[nodiscard]] std::vector<MenuItem> moduleCatalogueMenu() const; // ids 1000 + kind
    void showModuleMenu (const Layout& layout, double x, double y);
    void showKnobMenu (const Layout& layout, int parameterIndex, double x, double y);
    void showPortMenu (const Layout& layout, bool output, int port, double x, double y);
    void showCableMenu (const Connection& cable, double x, double y);
    void insertNodeOnCable (const Connection& cable, NodeKind kind, juce::Point<float> world);
    void runButton (const Layout& layout, const Button& button);
    void showFileMenu (double x, double y);
    void openPatchFile (const juce::File& file);
    void saveCurrentPatch();
    void saveAsPrompt();
    [[nodiscard]] static juce::File documentsFolder (const char* sub);
    [[nodiscard]] juce::Rectangle<float> buttonBounds (const Layout& layout, juce::Point<float> origin, int index) const noexcept;

    PatchEngine& engine;
    NVGcontext* vg;
    int font;
    Menu menu;
    TextPrompt prompt;
    FileBrowser browser;
    ToneBrowser toneBrowser;
    void openToneBrowser (NodeId id);
    juce::File currentFile;
    int controllerSlotSent = -2;
    juce::String controllerRigSent;
    int windowW = 1600, windowH = 1000;
    std::vector<Layout> layouts;
    std::unordered_map<NodeId, PlateCache> plates;
    float cachedPlateScale = 0.0f;
    float pixelRatio = 1.0f;
    float uiScale = 1.0f;
    std::unique_ptr<juce::PropertiesFile> settings;
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
    double lastClickTime = -1.0, lastClickX = 0.0, lastClickY = 0.0;
    bool panning = false;
    double panStartX = 0.0, panStartY = 0.0, panOriginX = 0.0, panOriginY = 0.0;
    std::optional<NodeId> draggingNode;
    juce::Point<float> dragOffset;
    std::optional<KnobDrag> knobDrag;
    std::optional<CableDrag> cableDrag;
    std::optional<NodeId> sequencerDrag; // painting step values across the preview

    // Board view: the same patch arranged by signal flow as pedals, with five
    // rig slots along the bottom. Rack and board are two faces of one graph.
    enum class Mode { rack, board };
    struct Pedal
    {
        NodeId id = 0;        // module id, or 0 for a group pedal
        int groupId = -1;     // >= 0 for a group pedal
        NodeKind kind = NodeKind::gain;
        float x = 0.0f, y = 0.0f, w = 150.0f, h = 210.0f;
        int column = 0;
        std::vector<std::pair<NodeId, int>> knobs; // (module, parameter), up to four
        std::vector<NodeId> members;               // group pedals
        std::vector<Button> buttons;               // transport commands (REC, PLAY, TAP...), same as the rack plate
        bool hardware = false, stomp = false, tray = false;
    };
    struct BoardDrag
    {
        NodeId node = 0;
        int groupId = -1;
        juce::Point<float> offset;
        bool moved = false;
    };
    // Slot change with the same modules and cables: knob values glide.
    struct GlideItem
    {
        NodeId node = 0;
        int parameter = -1;
        float fromValue = 0.0f, toValue = 0.0f, fromDepth = 0.0f, toDepth = 0.0f;
    };
    struct Glide
    {
        double start = 0.0, duration = 0.3;
        std::vector<GlideItem> items;
    };
    std::optional<BoardDrag> boardDrag;
    std::optional<Glide> glide;

    // Gamepad (GLFW): the Board with no pointer. D-pad walks pedals, A stomps,
    // B cycles the focused knob, the left stick turns it, LB/RB change slot,
    // Start toggles rack/board, Back mutes, the stick clicks open the pedal's
    // and the board's menus; inside a menu, browser or prompt the d-pad, A and
    // B become arrows, Enter and Escape. Polled once per frame.
    struct GamepadState
    {
        bool present = false;
        std::array<unsigned char, 15> buttons {};
        std::array<bool, 2> triggers {}; // RT / LT held last frame
        double lastRepeat = 0.0;
    };
    GamepadState pad;
    int focusPedal = -1; // index into pedals
    int focusKnob = 0;
    bool stickWasTurning = false;
    void pollGamepad (double now);
    void moveFocus (int dx, int dy);
    [[nodiscard]] juce::Rectangle<float> pedalBounds (const Pedal& pedal) const noexcept { return { pedal.x, pedal.y, pedal.w, pedal.h }; }

    // MIDI learn: the next CC / note / program change binds to this target.
    std::optional<MidiMapping> learnTarget;
    // Expression pedal calibration: watch one CC's sweep, keep its min/max on the mapping.
    struct Calibration
    {
        MidiMapping mapping;
        int low = 127, high = 0;
        double lastAt = 0.0;
    };
    std::optional<Calibration> calibration;
    void beginMidiLearn (MidiMapping target, const juce::String& what);
    void removeMidiMapping (const std::function<bool (const MidiMapping&)>& matches);
    [[nodiscard]] juce::String midiLabelFor (const std::function<bool (const MidiMapping&)>& matches) const;
    [[nodiscard]] std::vector<MenuItem> midiMenuItems (const MidiMapping& target, const juce::String& what, int learnId, int removeId) const;
    std::vector<NodeId> boardSelection; // multi-select for grouping
    int selectedGroup = -1;
    [[nodiscard]] bool isBoardSelected (NodeId id) const noexcept;
    void showBoardMenu (double x, double y);
    void showBoardPedalMenu (const Pedal& pedal, double x, double y);
    void showGroupMenu (const Pedal& pedal, double x, double y);
    void groupSelection();
    void toggleGroupBypass (const Pedal& pedal);
    bool tryGlideToPatch (const juce::var& target);
    void finishGlide();
    Mode mode = Mode::rack;
    std::vector<Pedal> pedals;
    float boardScale = 1.0f;
    double boardPanX = 0.0, boardPanY = 0.0;
    bool boardDirty = true;
    int activeSlot = -1;
    static constexpr int slotCount = 5;
    static constexpr float slotBarHeight = 64.0f;
    void rebuildBoard();
    void fitBoard();
    void drawBoard (int width, int height, double now);
    void drawSlotBar (int width, int height);
    [[nodiscard]] const Pedal* pedalAt (juce::Point<float> board) const noexcept;
    [[nodiscard]] juce::Point<float> toBoard (double x, double y) const noexcept;
    [[nodiscard]] juce::Point<float> pedalKnobCentre (const Pedal& pedal, int knobIndex) const noexcept;
    [[nodiscard]] juce::Point<float> pedalStompCentre (const Pedal& pedal) const noexcept;
    [[nodiscard]] juce::Rectangle<float> pedalButtonBounds (const Pedal& pedal, int index) const noexcept;
    [[nodiscard]] static int pedalButtonRows (const Pedal& pedal) noexcept;
    [[nodiscard]] static juce::File slotFile (int slot);
    void loadSlot (int slot);
    void storeSlot (int slot);
    bool boardMouseButton (int button, bool pressed, int mods, double x, double y);
    void setMode (Mode newMode);

    // Module palette down the left edge: click adds at the view centre, drag
    // drops the module where the pointer lands.
    struct PaletteDrag
    {
        NodeKind kind = NodeKind::gain;
        double startX = 0.0, startY = 0.0;
        bool moved = false;
    };
    bool paletteVisible = true;
    float paletteScroll = 0.0f;
    int paletteHover = -1;
    std::optional<PaletteDrag> paletteDrag;
    static constexpr float paletteWidth = 192.0f;
    static constexpr float hudHeight = 34.0f;
    void drawPalette (int height);
    [[nodiscard]] int paletteRowAt (double x, double y) const noexcept; // index into moduleCatalogue(), -1 none
    [[nodiscard]] float paletteRowTop (int row) const noexcept;

    NodeId selectedNode = 0;
    std::optional<Connection> selectedCable;
    // Rack multi-selection: Shift+click toggles, Shift+drag on the canvas
    // rubber-bands, Ctrl+A takes everything; dragging any selected module
    // moves them all as one undo step; Delete removes them all.
    std::vector<NodeId> rackSelection;
    [[nodiscard]] bool isRackSelected (NodeId id) const noexcept;
    std::optional<juce::Rectangle<float>> marquee; // world coordinates
    juce::Point<float> marqueeStart;
    std::vector<std::pair<NodeId, juce::Point<float>>> dragStartPositions;
    juce::Point<float> dragStartWorld;
    juce::String message;
    double messageUntil = 0.0;
    double lastFrameMs = 0.0;
    double fps = 0.0;
    double lastRenderTime = 0.0;
    int plateRenders = 0; // plates rasterised in the last frame (HUD diagnostic)
};
} // namespace signalpatch::v2
