#include "RackView.h"
#include "Palette.h"
#include "../audio/PatchBundle.h"

#include <GLFW/glfw3.h>
#define NANOVG_GL3 1
#include <nanovg_gl.h>
#include <nanovg_gl_utils.h>

#include <algorithm>
#include <cmath>

namespace signalpatch::v2
{
namespace
{
    constexpr float railHeight = 10.0f;
    constexpr float headerHeight = 34.0f;
    constexpr float portSpacing = 19.0f;
    constexpr float firstPortY = 64.0f;
    constexpr float knobRowHeight = 94.0f;
    constexpr float stompZoneHeight = 44.0f;
    constexpr float controlsTop = 152.0f; // same numbers as the JUCE rack: patches lay out identically
    constexpr float previewTop = railHeight + headerHeight + 10.0f;
    constexpr float previewHeight = 78.0f;
    constexpr float corner = 6.0f;

    juce::String formatValue (const DspParameter& parameter)
    {
        const auto value = parameter.getValue();
        const auto& unit = parameter.unit;
        if (unit == "Hz")
            return value >= 1000.0f ? juce::String (value / 1000.0f, 2) + " kHz"
                                    : juce::String (value, value < 10.0f ? 2 : 0) + " Hz";
        if (unit == "ms")
            return juce::String (value, value >= 100.0f ? 0 : 1) + " ms";
        if (unit == "dB")
            return juce::String (value, 1) + " dB";
        if (unit == "%")
            return juce::String (value, 1) + "%";
        if (unit == ":1")
            return juce::String (value, 1) + ":1";
        return juce::String (value, 2);
    }

    juce::Point<float> cubicPoint (juce::Point<float> p0, juce::Point<float> p1,
                                   juce::Point<float> p2, juce::Point<float> p3, float t) noexcept
    {
        const auto u = 1.0f - t;
        return p0 * (u * u * u) + p1 * (3.0f * u * u * t) + p2 * (3.0f * u * t * t) + p3 * (t * t * t);
    }
} // namespace

RackView::RackView (PatchEngine& engineToUse, NVGcontext* context, int fontId)
    : engine (engineToUse), vg (context), font (fontId), menu (context, fontId), prompt (context, fontId), browser (context, fontId)
{
    engine.addChangeListener (this);
}

RackView::~RackView()
{
    engine.removeChangeListener (this);
    releasePlates();
}

void RackView::changeListenerCallback (juce::ChangeBroadcaster*)
{
    structureDirty = true;
    invalidateAllPlates();
    dirty = true;
}

void RackView::invalidatePlate (NodeId id)
{
    const auto found = plates.find (id);
    if (found != plates.end())
        found->second.dirty = true;
}

void RackView::invalidateAllPlates()
{
    for (auto& entry : plates)
        entry.second.dirty = true;
}

void RackView::releasePlates()
{
    for (auto& entry : plates)
        if (entry.second.framebuffer != nullptr)
            nvgluDeleteFramebuffer (entry.second.framebuffer);
    plates.clear();
}

// ---------------------------------------------------------------- layout

void RackView::rebuildLayouts()
{
    layouts.clear();
    for (const auto& node : engine.getDocument().getNodes())
    {
        Layout layout;
        layout.id = node.id;
        layout.kind = node.processor->getKind();
        layout.hardware = node.hardware;
        layout.stomp = node.processor->isBypassable();
        layout.inputs = node.processor->getNumInputPorts();
        layout.outputs = node.processor->getNumOutputPorts();
        layout.w = node.hardware ? 300.0f : 236.0f;
        for (int index = 0; index < node.processor->getNumParameters(); ++index)
        {
            if (layout.kind == NodeKind::stepSequencer && index >= 2)
                continue;
            if (layout.kind == NodeKind::drumMachine && index >= 5)
                continue;
            layout.knobParameters.push_back (index);
        }
        auto button = [&layout] (const char* label, const char* command, NVGcolor active, int row = 0)
        {
            layout.buttons.push_back ({ label, command, row, active });
        };
        switch (layout.kind)
        {
            case NodeKind::sampler:
                button ("REC", "rec", palette::warning);
                button ("PLAY", "play", palette::okay);
                button ("CLR", "clear", palette::mutedText);
                break;
            case NodeKind::fourTrack:
                button ("PLAY", "play", palette::okay);
                button ("REC", "rec", palette::warning);
                button ("RTZ", "rtz", palette::mutedText);
                for (int track = 1; track <= 4; ++track)
                    button (juce::String (track).toRawUTF8(), ("arm" + juce::String (track)).toRawUTF8(), palette::warning, 1);
                break;
            case NodeKind::neuralAmpPlaceholder:
            case NodeKind::neuralPedal:
                button ("<", "prev-model", palette::panelRaised);
                button ("LOAD", "load", palette::panelRaised);
                button (">", "next-model", palette::panelRaised);
                break;
            case NodeKind::cabinet:
                button ("<", "prev-ir", palette::panelRaised);
                button ("IR A", "load-a", palette::panelRaised);
                button ("IR B", "load-b", palette::panelRaised);
                button (">", "next-ir", palette::panelRaised);
                break;
            case NodeKind::feedbackGuard:
                button ("RESET LOOP", "reset-loop", palette::feedback);
                break;
            default: break;
        }
        const auto portRows = juce::jmax (layout.inputs, layout.outputs);
        layout.previewY = previewTop;
        layout.previewH = previewHeight;
        layout.controlsTop = controlsTop;
        const auto knobRows = (static_cast<int> (layout.knobParameters.size()) + 1) / 2;
        const auto portsHeight = firstPortY + static_cast<float> (portRows) * portSpacing + 18.0f;
        const auto controlsHeight = controlsTop + static_cast<float> (knobRows) * knobRowHeight + 12.0f;
        auto height = juce::jmax (layout.hardware ? 150.0f : 210.0f, portsHeight, controlsHeight);
        if (layout.kind == NodeKind::script)
            height += 92.0f;
        if (layout.kind == NodeKind::sampler || layout.kind == NodeKind::neuralAmpPlaceholder || layout.kind == NodeKind::neuralPedal
            || layout.kind == NodeKind::cabinet)
            height += 32.0f; // room for the button row under the knobs
        if (layout.kind == NodeKind::fourTrack)
            height += 62.0f;
        layout.h = height;
        layouts.push_back (std::move (layout));
    }
    structureDirty = false;
}

const RackView::Layout* RackView::layoutFor (NodeId id) const noexcept
{
    for (const auto& layout : layouts)
        if (layout.id == id)
            return &layout;
    return nullptr;
}

juce::Point<float> RackView::nodePosition (NodeId id) const noexcept
{
    if (const auto* node = engine.getDocument().findNode (id))
        return node->position;
    return {};
}

juce::Point<float> RackView::inputPortCentre (const Layout& layout, juce::Point<float> origin, int port) const noexcept
{
    juce::ignoreUnused (layout);
    return { origin.x + 9.0f, origin.y + firstPortY + static_cast<float> (port) * portSpacing };
}

juce::Point<float> RackView::outputPortCentre (const Layout& layout, juce::Point<float> origin, int port) const noexcept
{
    return { origin.x + layout.w - 9.0f, origin.y + firstPortY + static_cast<float> (port) * portSpacing };
}

juce::Point<float> RackView::knobCentre (const Layout& layout, juce::Point<float> origin, int knobIndex) const noexcept
{
    const auto column = knobIndex % 2;
    const auto row = knobIndex / 2;
    const auto columnWidth = (layout.w - 28.0f) * 0.5f;
    return { origin.x + 75.0f + static_cast<float> (column) * columnWidth,
             origin.y + layout.controlsTop + static_cast<float> (row) * knobRowHeight + 39.0f };
}

juce::Point<float> RackView::stompCentre (const Layout& layout, juce::Point<float> origin) const noexcept
{
    return { origin.x + layout.w * 0.5f + 20.0f, origin.y + layout.h - railHeight - stompZoneHeight * 0.5f - 4.0f };
}

juce::Point<float> RackView::toWorld (double x, double y) const noexcept
{
    return { static_cast<float> ((x - panX) / zoom), static_cast<float> ((y - panY) / zoom) };
}

void RackView::bezier (juce::Point<float> a, juce::Point<float> b, juce::Point<float>& c1, juce::Point<float>& c2) const noexcept
{
    const auto reach = juce::jlimit (40.0f, 220.0f, std::abs (b.x - a.x) * 0.5f + 30.0f);
    c1 = { a.x + reach, a.y + 8.0f };
    c2 = { b.x - reach, b.y + 8.0f };
}

std::optional<Connection> RackView::cableNear (juce::Point<float> world, float radius) const
{
    std::optional<Connection> best;
    auto bestDistance = radius;
    for (const auto& connection : engine.getDocument().getConnections())
    {
        const auto* source = layoutFor (connection.sourceNode);
        const auto* destination = layoutFor (connection.destinationNode);
        if (source == nullptr || destination == nullptr)
            continue;
        const auto a = outputPortCentre (*source, nodePosition (source->id), connection.sourcePort);
        const auto b = inputPortCentre (*destination, nodePosition (destination->id), connection.destinationPort);
        juce::Point<float> c1, c2;
        bezier (a, b, c1, c2);
        for (int step = 0; step <= 32; ++step)
        {
            const auto point = cubicPoint (a, c1, c2, b, static_cast<float> (step) / 32.0f);
            const auto distance = point.getDistanceFrom (world);
            if (distance < bestDistance)
            {
                bestDistance = distance;
                best = connection;
            }
        }
    }
    return best;
}

void RackView::fitToPatch (int width, int height)
{
    if (structureDirty)
        rebuildLayouts();
    if (layouts.empty())
        return;
    float minX = 1.0e9f, minY = 1.0e9f, maxX = -1.0e9f, maxY = -1.0e9f;
    for (const auto& layout : layouts)
    {
        const auto origin = nodePosition (layout.id);
        minX = juce::jmin (minX, origin.x);
        minY = juce::jmin (minY, origin.y);
        maxX = juce::jmax (maxX, origin.x + layout.w);
        maxY = juce::jmax (maxY, origin.y + layout.h);
    }
    const auto margin = 60.0f;
    const auto contentW = maxX - minX + 2.0f * margin;
    const auto contentH = maxY - minY + 2.0f * margin + 40.0f;
    const auto left = paletteVisible ? static_cast<double> (paletteWidth) : 0.0;
    const auto availableW = width - left;
    const auto fit = juce::jlimit (0.25, 1.5, std::min (availableW / static_cast<double> (contentW), height / static_cast<double> (contentH)));
    targetZoom = zoom = fit;
    panX = left + (availableW - contentW * fit) * 0.5 - (minX - margin) * fit;
    panY = (height - contentH * fit) * 0.5 - (minY - margin) * fit + 20.0;
    dirty = true;
}

juce::Rectangle<float> RackView::buttonBounds (const Layout& layout, juce::Point<float> origin, int index) const noexcept
{
    const auto& button = layout.buttons[static_cast<std::size_t> (index)];
    int rowCount = 0, indexInRow = 0;
    for (int i = 0; i < static_cast<int> (layout.buttons.size()); ++i)
        if (layout.buttons[static_cast<std::size_t> (i)].row == button.row)
        {
            if (i == index)
                indexInRow = rowCount;
            ++rowCount;
        }
    const auto rows = 1 + (layout.kind == NodeKind::fourTrack ? 1 : 0);
    const auto bottom = origin.y + layout.h - railHeight - 6.0f - (layout.stomp ? stompZoneHeight : 0.0f);
    const auto y = bottom - 26.0f - static_cast<float> (rows - 1 - button.row) * 28.0f;
    const auto width = (layout.w - 24.0f - static_cast<float> (rowCount - 1) * 6.0f) / static_cast<float> (rowCount);
    return { origin.x + 12.0f + static_cast<float> (indexInRow) * (width + 6.0f), y, width, 22.0f };
}

void RackView::say (const juce::String& text)
{
    message = text;
    messageUntil = lastTick + 3.0;
    dirty = true;
}

juce::File RackView::documentsFolder (const char* sub)
{
    auto folder = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("SignalPatch").getChildFile (sub);
    folder.createDirectory();
    return folder;
}

void RackView::openPatchFile (const juce::File& fileToLoad)
{
    auto file = fileToLoad;
    if (file.hasFileExtension ("zip"))
    {
        juce::File extracted;
        const auto unzip = bundle::extractBundle (file, documentsFolder ("projects").getChildFile (file.getFileNameWithoutExtension()), extracted);
        if (unzip.failed())
        {
            say (unzip.getErrorMessage());
            return;
        }
        file = extracted;
    }
    const auto result = engine.loadPatch (file);
    if (result.wasOk())
    {
        currentFile = file;
        selectedNode = 0;
        selectedCable.reset();
        say ("Loaded muted: " + file.getFileName() + " - M to fade in");
        structureDirty = true;
        fitToPatch (windowW, windowH);
    }
    else
        say (result.getErrorMessage());
}

void RackView::saveCurrentPatch()
{
    if (currentFile == juce::File())
    {
        saveAsPrompt();
        return;
    }
    const auto result = engine.savePatch (currentFile);
    say (result.wasOk() ? "Saved " + currentFile.getFileName() : result.getErrorMessage());
}

void RackView::saveAsPrompt()
{
    const auto initial = currentFile == juce::File() ? juce::String ("my rig") : currentFile.getFileNameWithoutExtension();
    prompt.open ("Save as (in ~/Documents/SignalPatch/patches)", initial, [this] (const juce::String& name)
    {
        if (name.trim().isEmpty())
            return;
        auto file = documentsFolder ("patches").getChildFile (name.trim());
        if (! file.hasFileExtension ("signalpatch"))
            file = file.withFileExtension ("signalpatch");
        const auto result = engine.savePatch (file);
        if (result.wasOk())
            currentFile = file;
        say (result.wasOk() ? "Saved " + file.getFullPathName() : result.getErrorMessage());
    });
}

void RackView::requestQuit()
{
    auto* window = glfwGetCurrentContext();
    if (! engine.hasUnsavedChanges())
    {
        glfwSetWindowShouldClose (window, GLFW_TRUE);
        return;
    }
    std::vector<MenuItem> items;
    items.push_back (MenuItem::sectionHeader ("UNSAVED CHANGES IN " + (currentFile == juce::File() ? juce::String ("THIS PATCH") : currentFile.getFileName().toUpperCase())));
    items.push_back (MenuItem::item (1, "Save and quit"));
    items.push_back (MenuItem::item (2, "Quit without saving"));
    items.push_back (MenuItem::item (3, "Cancel", "Esc"));
    menu.open (std::move (items), windowW * 0.5f - 120.0f, windowH * 0.4f, [this, window] (int picked)
    {
        if (picked == 2)
            glfwSetWindowShouldClose (window, GLFW_TRUE);
        else if (picked == 1)
        {
            if (currentFile != juce::File())
            {
                if (engine.savePatch (currentFile).wasOk())
                    glfwSetWindowShouldClose (window, GLFW_TRUE);
                return;
            }
            prompt.open ("Save as (in ~/Documents/SignalPatch/patches) then quit", "my rig", [this, window] (const juce::String& name)
            {
                if (name.trim().isEmpty())
                    return;
                auto file = documentsFolder ("patches").getChildFile (name.trim());
                if (! file.hasFileExtension ("signalpatch"))
                    file = file.withFileExtension ("signalpatch");
                if (engine.savePatch (file).wasOk())
                    glfwSetWindowShouldClose (window, GLFW_TRUE);
            });
        }
        dirty = true;
    });
    dirty = true;
}

void RackView::showFileMenu (double x, double y)
{
    enum { newPatch = 1, openPatch, save, saveAs, exportBundle, unmute, quit };
    std::vector<MenuItem> items;
    items.push_back (MenuItem::sectionHeader (currentFile == juce::File() ? "UNTITLED" : currentFile.getFileName().toUpperCase()));
    items.push_back (MenuItem::item (newPatch, "New patch", "Ctrl+N"));
    items.push_back (MenuItem::item (openPatch, "Open patch or project zip...", "Ctrl+O"));
    items.push_back (MenuItem::item (save, "Save", "Ctrl+S"));
    items.push_back (MenuItem::item (saveAs, "Save as...", "Ctrl+Shift+S"));
    items.push_back (MenuItem::item (exportBundle, "Export portable project (.zip)..."));
    items.push_back (MenuItem::line());
    items.push_back (MenuItem::item (unmute, engine.isPanicMuted() ? "Unmute (fade in)" : "Panic mute", "M"));
    items.push_back (MenuItem::item (quit, "Quit", "Ctrl+Q"));
    menu.open (std::move (items), static_cast<float> (x), static_cast<float> (y), [this] (int picked)
    {
        switch (picked)
        {
            case newPatch:
                engine.newPatch();
                currentFile = juce::File();
                say ("New patch (muted) - M to fade in");
                break;
            case openPatch:
                browser.open ("Open patch", currentFile != juce::File() ? currentFile.getParentDirectory() : documentsFolder ("patches"),
                              { "signalpatch", "zip" }, [this] (const juce::File& file) { openPatchFile (file); });
                break;
            case save: saveCurrentPatch(); break;
            case saveAs: saveAsPrompt(); break;
            case exportBundle:
                prompt.open ("Export zip name (in ~/Documents/SignalPatch/projects)",
                             currentFile == juce::File() ? juce::String ("my rig") : currentFile.getFileNameWithoutExtension(),
                             [this] (const juce::String& name)
                {
                    if (name.trim().isEmpty())
                        return;
                    auto file = documentsFolder ("projects").getChildFile (name.trim());
                    if (! file.hasFileExtension ("zip"))
                        file = file.withFileExtension ("zip");
                    const auto result = engine.exportBundle (file);
                    say (result.wasOk() ? "Exported " + file.getFullPathName() : result.getErrorMessage());
                });
                break;
            case unmute: engine.togglePanic(); say (engine.isPanicMuted() ? "Muted" : "Fading in"); break;
            case quit: requestQuit(); break;
            default: break;
        }
        dirty = true;
    });
    dirty = true;
}

void RackView::runButton (const Layout& layout, const Button& button)
{
    const auto id = layout.id;
    const auto* model = engine.getDocument().findNode (id);
    if (model == nullptr)
        return;
    if (button.command == "load")
    {
        const juce::File currentModel (model->processor->getExtraState().getProperty ("model", "").toString());
        browser.open ("Load NAM model", currentModel.getParentDirectory().isDirectory() ? currentModel.getParentDirectory() : documentsFolder ("models"),
                      { "nam" }, [this, id] (const juce::File& file)
        {
            auto* object = new juce::DynamicObject();
            object->setProperty ("model", file.getFullPathName());
            engine.applyNodeExtraState (id, juce::var (object));
            say ("Loading " + file.getFileName());
        });
    }
    else if (button.command == "load-a" || button.command == "load-b")
    {
        const bool slotB = button.command == "load-b";
        const juce::File currentIr (model->processor->getExtraState().getProperty ("ir", "").toString());
        auto start = currentIr.getParentDirectory();
        if (! start.isDirectory())
            start = documentsFolder ("irs");
        if (start.getNumberOfChildFiles (juce::File::findFiles, "*.wav") == 0 && juce::File ("/usr/share/gx_head/sounds/amps").isDirectory())
            start = juce::File ("/usr/share/gx_head/sounds/amps");
        browser.open (slotB ? "Load cab impulse B" : "Load cab impulse A", start, { "wav", "aif", "aiff", "flac" },
                      [this, id, slotB] (const juce::File& file)
        {
            const auto* node = engine.getDocument().findNode (id);
            if (node == nullptr)
                return;
            auto state = node->processor->getExtraState();
            if (state.getDynamicObject() == nullptr)
                state = juce::var (new juce::DynamicObject());
            state.getDynamicObject()->setProperty (slotB ? "irB" : "ir", file.getFullPathName());
            engine.applyNodeExtraState (id, state);
            say ("Loading " + file.getFileName());
        });
    }
    else if (button.command == "reset-loop")
        engine.resetNodeSafety (layout.id);
    else
        engine.sendNodeCommand (layout.id, button.command);
    dirty = true;
}

void RackView::showCanvasMenu (double x, double y)
{
    std::vector<MenuItem> items;
    juce::String group;
    std::vector<MenuItem> groupItems;
    for (const auto& entry : moduleCatalogue())
    {
        if (group != entry.group)
        {
            if (group.isNotEmpty())
                items.push_back (MenuItem::sub (group, std::move (groupItems)));
            groupItems.clear();
            group = entry.group;
        }
        groupItems.push_back (MenuItem::item (1000 + static_cast<int> (entry.kind), entry.label));
    }
    if (group.isNotEmpty())
        items.push_back (MenuItem::sub (group, std::move (groupItems)));
    items.push_back (MenuItem::line());
    items.push_back (MenuItem::item (1, engine.canUndo() ? "Undo " + engine.getUndoDescription() : juce::String ("Undo"), "Ctrl+Z", engine.canUndo()));
    items.push_back (MenuItem::item (2, engine.canRedo() ? "Redo " + engine.getRedoDescription() : juce::String ("Redo"), "Ctrl+Shift+Z", engine.canRedo()));
    items.push_back (MenuItem::line());
    items.push_back (MenuItem::item (3, engine.isPanicMuted() ? "Unmute (fade in)" : "Panic mute", "M"));
    items.push_back (MenuItem::item (4, "Fit patch to window", "F"));
    const auto world = toWorld (x, y);
    menu.open (std::move (items), static_cast<float> (x), static_cast<float> (y), [this, world] (int id)
    {
        if (id >= 1000)
        {
            const auto kind = static_cast<NodeKind> (id - 1000);
            const auto created = engine.addNode (kind, world - juce::Point<float> (118.0f, 40.0f));
            if (created != 0)
            {
                selectedNode = created;
                say (nodeKindName (kind) + " added");
            }
        }
        else if (id == 1) say (engine.undo() ? "Undo" : "Nothing to undo");
        else if (id == 2) say (engine.redo() ? "Redo" : "Nothing to redo");
        else if (id == 3) { engine.togglePanic(); say (engine.isPanicMuted() ? "Muted" : "Fading in"); }
        else if (id == 4) fitToPatch (windowW, windowH);
        dirty = true;
    });
    dirty = true;
}

void RackView::showModuleMenu (const Layout& layout, double x, double y)
{
    const auto* model = engine.getDocument().findNode (layout.id);
    if (model == nullptr)
        return;
    int cableCount = 0;
    for (const auto& connection : engine.getDocument().getConnections())
        if (connection.sourceNode == layout.id || connection.destinationNode == layout.id)
            ++cableCount;
    enum { bypass = 1, rename, duplicate, resetKnobs, disconnectAll, remove, prevModel, nextModel, prevIr, nextIr, clearIrB };
    std::vector<MenuItem> items;
    items.push_back (MenuItem::sectionHeader (model->processor->getName().toUpperCase()));
    if (layout.stomp)
        items.push_back (MenuItem::item (bypass, model->processor->isBypassed() ? "Enable (unbypass)" : "Bypass", "stomp"));
    if (! layout.hardware)
    {
        items.push_back (MenuItem::item (rename, "Rename...", "dbl-click"));
        items.push_back (MenuItem::item (duplicate, "Duplicate", "Ctrl+D"));
        items.push_back (MenuItem::item (resetKnobs, "Reset knobs to defaults", {}, ! layout.knobParameters.empty()));
    }
    items.push_back (MenuItem::item (disconnectAll, "Disconnect all cables (" + juce::String (cableCount) + ")", {}, cableCount > 0));
    if (layout.kind == NodeKind::neuralAmpPlaceholder || layout.kind == NodeKind::neuralPedal)
    {
        items.push_back (MenuItem::line());
        items.push_back (MenuItem::item (prevModel, "Previous model in folder"));
        items.push_back (MenuItem::item (nextModel, "Next model in folder"));
    }
    else if (layout.kind == NodeKind::cabinet)
    {
        items.push_back (MenuItem::line());
        items.push_back (MenuItem::item (prevIr, "Previous impulse in folder"));
        items.push_back (MenuItem::item (nextIr, "Next impulse in folder"));
        items.push_back (MenuItem::item (clearIrB, "Clear impulse B", {}, model->processor->getExtraState().hasProperty ("irB")));
    }
    if (! layout.hardware)
    {
        items.push_back (MenuItem::line());
        items.push_back (MenuItem::item (remove, "Delete", "Del"));
    }
    const auto id = layout.id;
    menu.open (std::move (items), static_cast<float> (x), static_cast<float> (y), [this, id] (int picked)
    {
        const auto* current = engine.getDocument().findNode (id);
        if (current == nullptr)
            return;
        switch (picked)
        {
            case bypass: engine.setNodeBypassed (id, ! current->processor->isBypassed()); break;
            case rename:
                prompt.open ("Rename module", current->processor->getName(), [this, id] (const juce::String& name)
                {
                    const auto result = engine.renameNode (id, name);
                    say (result.wasOk() ? "Renamed" : result.getErrorMessage());
                });
                break;
            case duplicate:
            {
                const auto copy = engine.duplicateNode (id);
                if (copy != 0) { selectedNode = copy; say ("Duplicated"); }
                break;
            }
            case resetKnobs:
                for (int index = 0; index < current->processor->getNumParameters(); ++index)
                    engine.setParameter (id, index, current->processor->getParameter (index).defaultValue);
                engine.closeEditGesture();
                invalidatePlate (id);
                break;
            case disconnectAll:
            {
                const auto cables = engine.getDocument().getConnections();
                for (const auto& cable : cables)
                    if (cable.sourceNode == id || cable.destinationNode == id)
                        engine.disconnect (cable);
                break;
            }
            case remove:
                if (engine.removeNode (id)) { selectedNode = 0; say ("Module removed"); }
                break;
            case prevModel: engine.sendNodeCommand (id, "prev-model"); break;
            case nextModel: engine.sendNodeCommand (id, "next-model"); break;
            case prevIr:    engine.sendNodeCommand (id, "prev-ir"); break;
            case nextIr:    engine.sendNodeCommand (id, "next-ir"); break;
            case clearIrB:
            {
                auto state = current->processor->getExtraState();
                if (auto* object = state.getDynamicObject())
                {
                    object->removeProperty ("irB");
                    engine.applyNodeExtraState (id, state);
                }
                break;
            }
            default: break;
        }
        dirty = true;
    });
    dirty = true;
}

void RackView::showKnobMenu (const Layout& layout, int parameterIndex, double x, double y)
{
    const auto* model = engine.getDocument().findNode (layout.id);
    if (model == nullptr)
        return;
    const auto& parameter = model->processor->getParameter (parameterIndex);
    std::optional<Connection> modulation;
    if (parameter.inputPortIndex >= 0)
        for (const auto& connection : engine.getDocument().getConnections())
            if (connection.destinationNode == layout.id && connection.destinationPort == parameter.inputPortIndex)
                modulation = connection;
    enum { reset = 1, setValue, zeroDepth, fullDepth, removeModulation };
    std::vector<MenuItem> items;
    items.push_back (MenuItem::sectionHeader (parameter.name.toUpperCase()));
    items.push_back (MenuItem::item (reset, "Reset to default (" + juce::String (parameter.defaultValue, 2) + ")", "dbl-click"));
    items.push_back (MenuItem::item (setValue, "Set value..."));
    if (parameter.inputPortIndex >= 0)
    {
        items.push_back (MenuItem::line());
        items.push_back (MenuItem::item (zeroDepth, "Mod depth 0", {}, parameter.getModulationDepth() > 0.0f));
        items.push_back (MenuItem::item (fullDepth, "Mod depth 100%", {}, parameter.getModulationDepth() < 1.0f));
        items.push_back (MenuItem::item (removeModulation, "Disconnect modulation cable", {}, modulation.has_value()));
    }
    const auto id = layout.id;
    menu.open (std::move (items), static_cast<float> (x), static_cast<float> (y), [this, id, parameterIndex, modulation] (int picked)
    {
        const auto* current = engine.getDocument().findNode (id);
        if (current == nullptr)
            return;
        const auto& current_parameter = current->processor->getParameter (parameterIndex);
        switch (picked)
        {
            case reset: engine.setParameter (id, parameterIndex, current_parameter.defaultValue); break;
            case setValue:
                prompt.open ("Set " + current_parameter.name + (current_parameter.unit.isNotEmpty() ? " (" + current_parameter.unit + ")" : juce::String()),
                             juce::String (current_parameter.getValue(), 3), [this, id, parameterIndex] (const juce::String& text)
                {
                    const auto* node = engine.getDocument().findNode (id);
                    if (node == nullptr)
                        return;
                    const auto& range = node->processor->getParameter (parameterIndex).range;
                    engine.setParameter (id, parameterIndex, juce::jlimit (range.start, range.end, text.getFloatValue()));
                    engine.closeEditGesture();
                    invalidatePlate (id);
                    dirty = true;
                });
                return;
            case zeroDepth: engine.setModulationDepth (id, parameterIndex, 0.0f); break;
            case fullDepth: engine.setModulationDepth (id, parameterIndex, 1.0f); break;
            case removeModulation:
                if (modulation.has_value())
                    engine.disconnect (*modulation);
                break;
            default: return;
        }
        engine.closeEditGesture();
        invalidatePlate (id);
        dirty = true;
    });
    dirty = true;
}

void RackView::showPortMenu (const Layout& layout, bool output, int port, double x, double y)
{
    const auto* model = engine.getDocument().findNode (layout.id);
    if (model == nullptr)
        return;
    std::vector<Connection> cables;
    for (const auto& connection : engine.getDocument().getConnections())
        if ((output && connection.sourceNode == layout.id && connection.sourcePort == port)
            || (! output && connection.destinationNode == layout.id && connection.destinationPort == port))
            cables.push_back (connection);
    const auto& info = output ? model->processor->getOutputPort (port) : model->processor->getInputPort (port);
    std::vector<MenuItem> items;
    items.push_back (MenuItem::sectionHeader ((output ? "OUT  " : "IN  ") + info.name.toUpperCase()));
    if (cables.empty())
        items.push_back (MenuItem::item (0, "No cables - drag from the port to connect", {}, false));
    else
    {
        items.push_back (MenuItem::item (1, "Disconnect all (" + juce::String (cables.size()) + ")"));
        items.push_back (MenuItem::line());
        for (std::size_t index = 0; index < cables.size(); ++index)
        {
            const auto& cable = cables[index];
            const auto otherId = output ? cable.destinationNode : cable.sourceNode;
            const auto* other = engine.getDocument().findNode (otherId);
            const auto otherName = other != nullptr ? other->processor->getName() : juce::String ("?");
            const auto otherPort = other == nullptr ? juce::String()
                : (output ? other->processor->getInputPort (cable.destinationPort).name
                          : other->processor->getOutputPort (cable.sourcePort).name);
            items.push_back (MenuItem::item (100 + static_cast<int> (index),
                                             juce::String (output ? "Remove cable to " : "Remove cable from ") + otherName + " / " + otherPort));
        }
    }
    menu.open (std::move (items), static_cast<float> (x), static_cast<float> (y), [this, cables] (int picked)
    {
        if (picked == 1)
            for (const auto& cable : cables)
                engine.disconnect (cable);
        else if (picked >= 100 && juce::isPositiveAndBelow (picked - 100, static_cast<int> (cables.size())))
            engine.disconnect (cables[static_cast<std::size_t> (picked - 100)]);
        dirty = true;
    });
    dirty = true;
}

void RackView::insertNodeOnCable (const Connection& cable, NodeKind kind, juce::Point<float> world)
{
    const auto id = engine.addNode (kind, world - juce::Point<float> (118.0f, 40.0f));
    if (id == 0)
        return;
    const auto* node = engine.getDocument().findNode (id);
    if (node == nullptr || node->processor->getNumInputPorts() == 0 || node->processor->getNumOutputPorts() == 0)
    {
        say (nodeKindName (kind) + " added (could not splice it into the cable)");
        return;
    }
    engine.disconnect (cable);
    const auto in = engine.connect ({ cable.sourceNode, cable.sourcePort, id, 0 });
    const auto out = engine.connect ({ id, 0, cable.destinationNode, cable.destinationPort });
    selectedNode = id;
    say (in.wasOk() && out.wasOk() ? nodeKindName (kind) + " inserted into the cable"
                                   : (in.failed() ? in.getErrorMessage() : out.getErrorMessage()));
}

void RackView::showCableMenu (const Connection& cable, double x, double y)
{
    const auto* source = engine.getDocument().findNode (cable.sourceNode);
    const bool audioCable = source != nullptr && source->processor->getOutputPort (cable.sourcePort).type == SignalType::audio;
    std::vector<MenuItem> items;
    items.push_back (MenuItem::item (1, "Disconnect cable", "Del"));
    if (audioCable)
    {
        std::vector<MenuItem> insert;
        juce::String group;
        std::vector<MenuItem> groupItems;
        for (const auto& entry : moduleCatalogue())
        {
            const juce::String entryGroup (entry.group);
            if (entryGroup != "EFFECTS" && entryGroup != "NEURAL" && entryGroup != "DYNAMICS" && entryGroup != "VOICE" && entry.kind != NodeKind::gain)
                continue;
            if (group != entryGroup)
            {
                if (group.isNotEmpty())
                    insert.push_back (MenuItem::sub (group, std::move (groupItems)));
                groupItems.clear();
                group = entryGroup;
            }
            groupItems.push_back (MenuItem::item (1000 + static_cast<int> (entry.kind), entry.label));
        }
        if (group.isNotEmpty())
            insert.push_back (MenuItem::sub (group, std::move (groupItems)));
        items.push_back (MenuItem::sub ("Insert module here", std::move (insert)));
    }
    const auto world = toWorld (x, y);
    menu.open (std::move (items), static_cast<float> (x), static_cast<float> (y), [this, cable, world] (int picked)
    {
        if (picked == 1)
        {
            engine.disconnect (cable);
            say ("Cable removed");
        }
        else if (picked >= 1000)
            insertNodeOnCable (cable, static_cast<NodeKind> (picked - 1000), world);
        dirty = true;
    });
    dirty = true;
}

juce::Rectangle<float> RackView::previewArea (const Layout& layout, juce::Point<float> origin) const noexcept
{
    return { origin.x + 68.0f, origin.y + layout.previewY, layout.w - 136.0f, layout.previewH };
}

void RackView::drawPreviewContent (const Layout& layout, const NodeModel& model, juce::Point<float> origin)
{
    const auto area = previewArea (layout, origin);
    const auto colour = accent (layout.kind);
    const auto kind = layout.kind;

    if (kind == NodeKind::stepSequencer)
    {
        const auto active = model.processor->currentStep();
        const auto gap = 3.0f;
        const auto width = (area.getWidth() - gap * 9.0f) / 8.0f;
        for (int step = 0; step < 8; ++step)
        {
            const auto value = model.processor->getParameter (2 + step).getValue();
            const juce::Rectangle<float> bar (area.getX() + gap + static_cast<float> (step) * (width + gap), area.getY() + 5.0f, width, area.getHeight() - 10.0f);
            nvgBeginPath (vg);
            nvgRoundedRect (vg, bar.getX(), area.getCentreY() - 1.0f, bar.getWidth(), 2.0f, 1.0f);
            nvgFillColor (vg, alpha (palette::grid, 0.5f));
            nvgFill (vg);
            const auto filled = value >= 0.0f
                ? juce::Rectangle<float> (bar.getX(), area.getCentreY() - value * bar.getHeight() * 0.48f, bar.getWidth(), value * bar.getHeight() * 0.48f)
                : juce::Rectangle<float> (bar.getX(), area.getCentreY(), bar.getWidth(), -value * bar.getHeight() * 0.48f);
            if (filled.getHeight() > 0.5f)
            {
                nvgBeginPath (vg);
                nvgRoundedRect (vg, filled.getX(), filled.getY(), filled.getWidth(), filled.getHeight(), 1.5f);
                nvgFillColor (vg, step == active ? palette::selection : alpha (colour, 0.7f));
                nvgFill (vg);
            }
            if (step == active)
            {
                nvgBeginPath (vg);
                nvgRoundedRect (vg, bar.getX(), bar.getY(), bar.getWidth(), bar.getHeight(), 2.0f);
                nvgFillColor (vg, alpha (palette::selection, 0.22f));
                nvgFill (vg);
            }
        }
        return;
    }

    if (kind == NodeKind::drumMachine)
    {
        const auto active = model.processor->currentStep();
        const auto grid = area.reduced (3.0f);
        const auto cellW = grid.getWidth() / 8.0f, cellH = grid.getHeight() / 3.0f;
        const NVGcolor lanes[3] { colour, palette::okay, palette::audio };
        for (int row = 0; row < 3; ++row)
            for (int column = 0; column < 8; ++column)
            {
                const auto cell = juce::Rectangle<float> (grid.getX() + static_cast<float> (column) * cellW, grid.getY() + static_cast<float> (row) * cellH, cellW, cellH).reduced (1.5f);
                const bool on = model.processor->getParameter (5 + row * 8 + column).getValue() > 0.5f;
                if (column == active)
                {
                    nvgBeginPath (vg);
                    nvgRoundedRect (vg, cell.getX() - 1.0f, cell.getY() - 1.0f, cell.getWidth() + 2.0f, cell.getHeight() + 2.0f, 2.0f);
                    nvgFillColor (vg, alpha (palette::selection, 0.18f));
                    nvgFill (vg);
                }
                nvgBeginPath (vg);
                nvgRoundedRect (vg, cell.getX(), cell.getY(), cell.getWidth(), cell.getHeight(), 2.0f);
                nvgFillColor (vg, on ? alpha (lanes[row], column == active ? 1.0f : 0.8f) : lighter (palette::nodeDark, 0.15f));
                nvgFill (vg);
                if (! on)
                {
                    nvgStrokeColor (vg, lighter (palette::grid, 0.15f));
                    nvgStrokeWidth (vg, 0.8f);
                    nvgStroke (vg);
                }
            }
        return;
    }

    if (kind == NodeKind::compressor || kind == NodeKind::limiter || kind == NodeKind::gate)
    {
        const auto reduction = juce::jlimit (0.0f, 24.0f, model.processor->gainReductionDb());
        const auto meter = area.reduced (10.0f, 20.0f);
        nvgBeginPath (vg);
        nvgRoundedRect (vg, meter.getX(), meter.getY(), meter.getWidth(), meter.getHeight(), 2.0f);
        nvgFillColor (vg, lighter (palette::nodeDark, 0.12f));
        nvgFill (vg);
        for (int tick = 1; tick < 4; ++tick)
        {
            const auto tickX = meter.getX() + meter.getWidth() * static_cast<float> (tick) / 4.0f;
            nvgBeginPath (vg);
            nvgMoveTo (vg, tickX, meter.getY());
            nvgLineTo (vg, tickX, meter.getBottom());
            nvgStrokeColor (vg, lighter (palette::grid, 0.2f));
            nvgStrokeWidth (vg, 1.0f);
            nvgStroke (vg);
        }
        const auto fillW = meter.getWidth() * reduction / 24.0f;
        if (fillW > 0.5f)
        {
            nvgBeginPath (vg);
            nvgRoundedRect (vg, meter.getX(), meter.getY(), fillW, meter.getHeight(), 2.0f);
            nvgFillPaint (vg, nvgLinearGradient (vg, meter.getX(), 0, meter.getRight(), 0, palette::okay, palette::feedback));
            nvgFill (vg);
        }
        nvgFontFaceId (vg, font);
        nvgFontSize (vg, 11.0f);
        nvgTextAlign (vg, NVG_ALIGN_CENTER | NVG_ALIGN_BOTTOM);
        nvgFillColor (vg, palette::text);
        nvgText (vg, area.getCentreX(), area.getBottom() - 3.0f, (juce::String (reduction, 1) + " dB GR").toRawUTF8(), nullptr);
        return;
    }

    if (kind == NodeKind::delay)
    {
        const auto feedback = model.processor->getParameter (1).getValue() * 0.01f;
        for (int echo = 0; echo < 6; ++echo)
        {
            const auto height = juce::jmax (2.0f, area.getHeight() * 0.68f * std::pow (feedback, static_cast<float> (echo)));
            const auto ex = area.getX() + 10.0f + static_cast<float> (echo) * (area.getWidth() - 20.0f) / 5.0f;
            nvgBeginPath (vg);
            nvgRoundedRect (vg, ex - 1.8f, area.getCentreY() - height * 0.5f, 3.6f, height, 1.8f);
            nvgFillColor (vg, alpha (colour, 0.85f - 0.12f * static_cast<float> (echo)));
            nvgFill (vg);
        }
        return;
    }

    if (kind == NodeKind::hardwareInput)
    {
        const auto lanes = juce::jmin (8, layout.outputs);
        const auto laneH = (area.getHeight() - 10.0f) / static_cast<float> (juce::jmax (1, lanes));
        for (int lane = 0; lane < lanes; ++lane)
        {
            float laneRms = 0.0f;
            for (int port = lane; port < layout.outputs; port += lanes)
                laneRms = juce::jmax (laneRms, model.processor->outputRms (port));
            const auto level = juce::jlimit (0.0f, 1.0f, std::sqrt (laneRms));
            const auto ly = area.getY() + 5.0f + static_cast<float> (lane) * laneH;
            nvgBeginPath (vg);
            nvgRoundedRect (vg, area.getX() + 5.0f, ly, area.getWidth() - 10.0f, juce::jmax (2.0f, laneH - 3.0f), 1.5f);
            nvgFillColor (vg, lighter (palette::nodeDark, 0.15f));
            nvgFill (vg);
            if (level > 0.005f)
            {
                nvgBeginPath (vg);
                nvgRoundedRect (vg, area.getX() + 5.0f, ly, (area.getWidth() - 10.0f) * level, juce::jmax (2.0f, laneH - 3.0f), 1.5f);
                nvgFillPaint (vg, nvgLinearGradient (vg, area.getX(), 0, area.getRight(), 0, alpha (colour, 0.9f), palette::warning));
                nvgFill (vg);
            }
        }
        return;
    }

    // Everything else: a scope of the output (input for the hardware output),
    // with the distortion showing its input faintly behind.
    auto drawWave = [&] (const WaveformSnapshot& snapshot, NVGcolor waveColour, float waveAlpha)
    {
        const auto inner = area.reduced (4.0f);
        const auto centreY = inner.getCentreY(), amplitude = inner.getHeight() * 0.46f;
        nvgBeginPath (vg);
        for (int bucket = 0; bucket < WaveformSnapshot::bucketCount; ++bucket)
        {
            const auto px = inner.getX() + inner.getWidth() * static_cast<float> (bucket) / static_cast<float> (WaveformSnapshot::bucketCount - 1);
            const auto py = centreY - juce::jlimit (-1.0f, 1.0f, snapshot.high[static_cast<std::size_t> (bucket)]) * amplitude;
            if (bucket == 0) nvgMoveTo (vg, px, py); else nvgLineTo (vg, px, py);
        }
        for (int bucket = WaveformSnapshot::bucketCount - 1; bucket >= 0; --bucket)
        {
            const auto px = inner.getX() + inner.getWidth() * static_cast<float> (bucket) / static_cast<float> (WaveformSnapshot::bucketCount - 1);
            const auto py = centreY - juce::jlimit (-1.0f, 1.0f, snapshot.low[static_cast<std::size_t> (bucket)]) * amplitude;
            nvgLineTo (vg, px, py);
        }
        nvgClosePath (vg);
        nvgFillPaint (vg, nvgLinearGradient (vg, 0, inner.getY(), 0, inner.getBottom(), alpha (waveColour, waveAlpha), alpha (waveColour, waveAlpha * 0.15f)));
        nvgFill (vg);
        nvgStrokeColor (vg, alpha (waveColour, juce::jmin (1.0f, waveAlpha + 0.35f)));
        nvgStrokeWidth (vg, 1.2f);
        nvgStroke (vg);
    };
    if (kind == NodeKind::hardwareOutput)
    {
        drawWave (model.processor->inputWaveform(), colour, 0.5f);
        return;
    }
    if (kind == NodeKind::distortion)
        drawWave (model.processor->inputWaveform(), palette::text, 0.18f);
    drawWave (layout.outputs > 0 ? model.processor->outputWaveform (0) : model.processor->inputWaveform(), colour, 0.5f);
}

bool RackView::editPreviewAt (const Layout& layout, juce::Point<float> origin, juce::Point<float> world, bool firstPress)
{
    const auto area = previewArea (layout, origin).reduced (3.0f);
    if (! area.contains (world))
        return false;
    if (layout.kind == NodeKind::stepSequencer)
    {
        const auto step = juce::jlimit (0, 7, static_cast<int> ((world.x - area.getX()) / (area.getWidth() / 8.0f)));
        const auto value = juce::jlimit (-1.0f, 1.0f, (area.getCentreY() - world.y) / (area.getHeight() * 0.48f));
        engine.setParameter (layout.id, 2 + step, value);
        return true;
    }
    if (layout.kind == NodeKind::drumMachine && firstPress)
    {
        const auto column = juce::jlimit (0, 7, static_cast<int> ((world.x - area.getX()) / (area.getWidth() / 8.0f)));
        const auto row = juce::jlimit (0, 2, static_cast<int> ((world.y - area.getY()) / (area.getHeight() / 3.0f)));
        const auto index = 5 + row * 8 + column;
        const auto* model = engine.getDocument().findNode (layout.id);
        if (model == nullptr)
            return false;
        engine.setParameter (layout.id, index, model->processor->getParameter (index).getValue() > 0.5f ? 0.0f : 1.0f);
        engine.closeEditGesture();
        return true;
    }
    return false;
}

void RackView::showAudioMenu (double x, double y)
{
    auto& manager = engine.getDeviceManager();
    const auto setup = manager.getAudioDeviceSetup();
    auto* device = manager.getCurrentAudioDevice();
    std::vector<MenuItem> items;
    items.push_back (MenuItem::sectionHeader (device != nullptr ? device->getName().toUpperCase() : juce::String ("NO DEVICE")));

    std::vector<MenuItem> backends;
    int id = 100;
    for (auto* type : manager.getAvailableDeviceTypes())
        backends.push_back (MenuItem::item (id++, type->getTypeName() + (manager.getCurrentAudioDeviceType() == type->getTypeName() ? "  (current)" : juce::String())));
    items.push_back (MenuItem::sub ("Backend", std::move (backends)));

    if (auto* type = manager.getCurrentDeviceTypeObject())
    {
        type->scanForDevices();
        std::vector<MenuItem> outputs, inputs;
        id = 200;
        for (const auto& name : type->getDeviceNames (false))
            outputs.push_back (MenuItem::item (id++, name + (name == setup.outputDeviceName ? "  (current)" : juce::String())));
        id = 300;
        for (const auto& name : type->getDeviceNames (true))
            inputs.push_back (MenuItem::item (id++, name + (name == setup.inputDeviceName ? "  (current)" : juce::String())));
        items.push_back (MenuItem::sub ("Output device", std::move (outputs)));
        items.push_back (MenuItem::sub ("Input device", std::move (inputs)));
    }
    if (device != nullptr)
    {
        std::vector<MenuItem> buffers, rates;
        id = 400;
        for (const auto size : device->getAvailableBufferSizes())
            buffers.push_back (MenuItem::item (id++, juce::String (size) + " samples  (" + juce::String (size * 1000.0 / juce::jmax (1.0, device->getCurrentSampleRate()), 2) + " ms)"
                                                      + (size == device->getCurrentBufferSizeSamples() ? "  (current)" : juce::String())));
        id = 500;
        for (const auto rate : device->getAvailableSampleRates())
            rates.push_back (MenuItem::item (id++, juce::String (rate / 1000.0, 1) + " kHz" + (std::abs (rate - device->getCurrentSampleRate()) < 1.0 ? "  (current)" : juce::String())));
        items.push_back (MenuItem::sub ("Buffer size", std::move (buffers)));
        items.push_back (MenuItem::sub ("Sample rate", std::move (rates)));
    }
    menu.open (std::move (items), static_cast<float> (x), static_cast<float> (y), [this] (int picked)
    {
        auto& deviceManager = engine.getDeviceManager();
        auto current = deviceManager.getAudioDeviceSetup();
        juce::String error;
        if (picked >= 100 && picked < 200)
        {
            const auto& types = deviceManager.getAvailableDeviceTypes();
            if (juce::isPositiveAndBelow (picked - 100, types.size()))
                deviceManager.setCurrentAudioDeviceType (types[picked - 100]->getTypeName(), true);
        }
        else if (auto* type = deviceManager.getCurrentDeviceTypeObject(); type != nullptr && picked >= 200 && picked < 400)
        {
            const auto names = type->getDeviceNames (picked >= 300);
            const auto index = picked >= 300 ? picked - 300 : picked - 200;
            if (juce::isPositiveAndBelow (index, names.size()))
            {
                if (picked >= 300) current.inputDeviceName = names[index]; else current.outputDeviceName = names[index];
                current.useDefaultInputChannels = current.useDefaultOutputChannels = true;
                error = deviceManager.setAudioDeviceSetup (current, true);
            }
        }
        else if (auto* device = deviceManager.getCurrentAudioDevice(); device != nullptr && picked >= 400 && picked < 600)
        {
            if (picked < 500)
            {
                const auto sizes = device->getAvailableBufferSizes();
                if (juce::isPositiveAndBelow (picked - 400, sizes.size()))
                    current.bufferSize = sizes[picked - 400];
            }
            else
            {
                const auto rates = device->getAvailableSampleRates();
                if (juce::isPositiveAndBelow (picked - 500, rates.size()))
                    current.sampleRate = rates[picked - 500];
            }
            error = deviceManager.setAudioDeviceSetup (current, true);
        }
        say (error.isNotEmpty() ? error : "Audio device updated");
        dirty = true;
    });
    dirty = true;
}

// ---------------------------------------------------------------- palette

float RackView::paletteRowTop (int row) const noexcept
{
    // Group headings take a taller row before the first entry of each group.
    const auto& catalogue = moduleCatalogue();
    float y = hudHeight + 12.0f - paletteScroll;
    juce::String group;
    for (int index = 0; index <= row && index < static_cast<int> (catalogue.size()); ++index)
    {
        if (group != catalogue[static_cast<std::size_t> (index)].group)
        {
            group = catalogue[static_cast<std::size_t> (index)].group;
            y += 22.0f;
        }
        if (index == row)
            return y;
        y += 26.0f;
    }
    return y;
}

int RackView::paletteRowAt (double x, double y) const noexcept
{
    if (! paletteVisible || x < 0.0 || x > paletteWidth || y < hudHeight)
        return -1;
    const auto& catalogue = moduleCatalogue();
    for (int index = 0; index < static_cast<int> (catalogue.size()); ++index)
    {
        const auto top = paletteRowTop (index);
        if (y >= top && y < top + 26.0f)
            return index;
    }
    return -1;
}

void RackView::drawPalette (int height)
{
    if (! paletteVisible)
        return;
    nvgBeginPath (vg);
    nvgRect (vg, 0, hudHeight, paletteWidth, static_cast<float> (height) - hudHeight);
    nvgFillColor (vg, alpha (palette::panel, 0.96f));
    nvgFill (vg);
    nvgBeginPath (vg);
    nvgMoveTo (vg, paletteWidth, hudHeight);
    nvgLineTo (vg, paletteWidth, static_cast<float> (height));
    nvgStrokeColor (vg, nvgRGBAf (0, 0, 0, 0.4f));
    nvgStrokeWidth (vg, 1.0f);
    nvgStroke (vg);

    nvgSave (vg);
    nvgScissor (vg, 0, hudHeight, paletteWidth, static_cast<float> (height) - hudHeight);
    nvgFontFaceId (vg, font);
    const auto& catalogue = moduleCatalogue();
    juce::String group;
    for (int index = 0; index < static_cast<int> (catalogue.size()); ++index)
    {
        const auto& entry = catalogue[static_cast<std::size_t> (index)];
        const auto top = paletteRowTop (index);
        if (group != entry.group)
        {
            group = entry.group;
            nvgFontSize (vg, 9.5f);
            nvgTextLetterSpacing (vg, 1.0f);
            nvgTextAlign (vg, NVG_ALIGN_LEFT | NVG_ALIGN_BOTTOM);
            nvgFillColor (vg, palette::mutedText);
            nvgText (vg, 14.0f, top - 5.0f, group.toRawUTF8(), nullptr);
            nvgTextLetterSpacing (vg, 0.0f);
        }
        const bool hovered = index == paletteHover;
        const auto colour = accent (entry.kind);
        nvgBeginPath (vg);
        nvgRoundedRect (vg, 10.0f, top, paletteWidth - 20.0f, 23.0f, 4.0f);
        nvgFillColor (vg, hovered ? lighter (palette::panelRaised, 0.12f) : palette::panelRaised);
        nvgFill (vg);
        nvgBeginPath (vg);
        nvgRoundedRect (vg, 12.5f, top + 4.0f, 3.0f, 15.0f, 1.5f);
        nvgFillColor (vg, alpha (colour, hovered ? 1.0f : 0.8f));
        nvgFill (vg);
        nvgFontSize (vg, 11.0f);
        nvgTextAlign (vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
        nvgFillColor (vg, alpha (palette::text, hovered ? 1.0f : 0.88f));
        nvgText (vg, 24.0f, top + 11.5f, entry.label, nullptr);
    }
    nvgRestore (vg);

    // Ghost of the module being dragged out.
    if (paletteDrag.has_value() && paletteDrag->moved)
    {
        const auto& entry = moduleCatalogue()[static_cast<std::size_t> (paletteHover >= 0 ? paletteHover : 0)];
        juce::ignoreUnused (entry);
        const auto colour = accent (paletteDrag->kind);
        const auto gx = static_cast<float> (mouseX) - 60.0f, gy = static_cast<float> (mouseY) - 14.0f;
        nvgBeginPath (vg);
        nvgRoundedRect (vg, gx, gy, 120.0f, 28.0f, 5.0f);
        nvgFillColor (vg, alpha (mix (palette::nodeTop, colour, 0.3f), 0.85f));
        nvgFill (vg);
        nvgStrokeColor (vg, alpha (colour, 0.9f));
        nvgStrokeWidth (vg, 1.2f);
        nvgStroke (vg);
        nvgFontSize (vg, 11.0f);
        nvgTextAlign (vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
        nvgFillColor (vg, palette::text);
        nvgText (vg, gx + 60.0f, gy + 14.0f, nodeKindName (paletteDrag->kind).toUpperCase().toRawUTF8(), nullptr);
    }
}

// ---------------------------------------------------------------- frame loop

void RackView::tick (double now)
{
    const auto dt = lastTick > 0.0 ? juce::jlimit (0.0, 0.1, now - lastTick) : 0.016;
    lastTick = now;
    animating = false;

    if (structureDirty)
    {
        rebuildLayouts();
        dirty = true;
    }

    // Zoom eases toward the wheel target while keeping the anchor point still.
    if (std::abs (targetZoom - zoom) > 0.0005)
    {
        const auto worldX = (zoomAnchorX - panX) / zoom;
        const auto worldY = (zoomAnchorY - panY) / zoom;
        const auto rate = 1.0 - std::exp (-dt * 18.0);
        zoom += (targetZoom - zoom) * rate;
        if (std::abs (targetZoom - zoom) < 0.0005)
            zoom = targetZoom;
        panX = zoomAnchorX - worldX * zoom;
        panY = zoomAnchorY - worldY * zoom;
        animating = true;
        dirty = true;
    }

    // Live signal: scopes, LEDs and cable comets move while anything is audible.
    juce::uint32 telemetry = 0;
    for (const auto& node : engine.getDocument().getNodes())
        telemetry += node.processor->telemetryVersion();
    if (telemetry != lastTelemetry)
    {
        lastTelemetry = telemetry;
        animating = true;
        dirty = true;
    }

    if (message.isNotEmpty() && now > messageUntil)
    {
        message.clear();
        dirty = true;
    }
    if (prompt.isOpen())
    {
        animating = true; // caret blink
        dirty = true;
    }
}

// ---------------------------------------------------------------- drawing

void RackView::drawBackground (int width, int height)
{
    nvgBeginPath (vg);
    nvgRect (vg, 0, 0, static_cast<float> (width), static_cast<float> (height));
    nvgFillColor (vg, palette::workspace);
    nvgFill (vg);

    // Dot grid in world space; spacing doubles as the view zooms out so the
    // density on screen stays readable and the dot count bounded.
    double spacing = 24.0;
    while (spacing * zoom < 18.0)
        spacing *= 2.0;
    const auto x0 = std::floor ((-panX / zoom) / spacing) * spacing;
    const auto y0 = std::floor ((-panY / zoom) / spacing) * spacing;
    const auto x1 = (width - panX) / zoom;
    const auto y1 = (height - panY) / zoom;
    nvgBeginPath (vg);
    for (double y = y0; y < y1; y += spacing)
        for (double x = x0; x < x1; x += spacing)
            nvgRect (vg, static_cast<float> (x * zoom + panX) - 1.0f, static_cast<float> (y * zoom + panY) - 1.0f, 2.0f, 2.0f);
    nvgFillColor (vg, palette::gridDot);
    nvgFill (vg);
}

void RackView::drawCable (const Connection& connection, bool selected, double now)
{
    const auto* source = layoutFor (connection.sourceNode);
    const auto* destination = layoutFor (connection.destinationNode);
    const auto* sourceModel = engine.getDocument().findNode (connection.sourceNode);
    const auto* destinationModel = engine.getDocument().findNode (connection.destinationNode);
    if (source == nullptr || destination == nullptr || sourceModel == nullptr || destinationModel == nullptr)
        return;

    const auto a = outputPortCentre (*source, nodePosition (source->id), connection.sourcePort);
    const auto b = inputPortCentre (*destination, nodePosition (destination->id), connection.destinationPort);
    juce::Point<float> c1, c2;
    bezier (a, b, c1, c2);
    const auto type = sourceModel->processor->getOutputPort (connection.sourcePort).type;
    const bool feedback = sourceModel->processor->isFeedbackGuard() || destinationModel->processor->isFeedbackGuard();
    const auto colour = selected ? palette::selection : feedback ? palette::feedback : accent (source->kind);
    const auto energy = juce::jlimit (0.0f, 1.0f, std::sqrt (juce::jmax (0.0f, sourceModel->processor->outputRms (connection.sourcePort))));
    const auto width = (selected ? 3.4f : 2.0f) + energy * 2.6f;

    auto path = [&]
    {
        nvgBeginPath (vg);
        nvgMoveTo (vg, a.x, a.y);
        nvgBezierTo (vg, c1.x, c1.y, c2.x, c2.y, b.x, b.y);
    };
    nvgLineCap (vg, NVG_ROUND);

    // Shadow, glow halos scaled by the live level, then the core.
    nvgSave (vg);
    nvgTranslate (vg, 0.0f, 2.0f);
    path();
    nvgStrokeColor (vg, nvgRGBAf (0, 0, 0, 0.5f));
    nvgStrokeWidth (vg, width + 3.5f);
    nvgStroke (vg);
    nvgRestore (vg);

    path();
    nvgStrokeColor (vg, alpha (colour, 0.06f + energy * 0.30f));
    nvgStrokeWidth (vg, width + 7.0f);
    nvgStroke (vg);
    path();
    nvgStrokeColor (vg, alpha (colour, 0.12f + energy * 0.30f));
    nvgStrokeWidth (vg, width + 3.0f);
    nvgStroke (vg);

    path();
    if (type == SignalType::control)
        nvgStrokeColor (vg, alpha (colour, 0.55f + energy * 0.4f));
    else
        nvgStrokePaint (vg, nvgLinearGradient (vg, a.x, a.y, b.x, b.y, lighter (colour, 0.15f), darker (colour, 0.1f)));
    nvgStrokeWidth (vg, type == SignalType::control ? width * 0.7f : width);
    nvgStroke (vg);

    // Comets: bright heads with fading tails marching source -> destination.
    if (energy > 0.012f || type == SignalType::control)
    {
        const auto length = a.getDistanceFrom (b) + 1.0f;
        const auto count = juce::jlimit (2, 5, static_cast<int> (length / 110.0f) + 2);
        const auto speed = type == SignalType::control && ! feedback ? 0.25 : 0.4; // cycles per second
        for (int comet = 0; comet < count; ++comet)
        {
            const auto head = static_cast<float> (std::fmod (now * speed + comet / static_cast<double> (count), 1.0));
            for (int tail = 0; tail < 4; ++tail)
            {
                const auto t = head - 0.02f * static_cast<float> (tail);
                if (t < 0.0f)
                    continue;
                const auto point = cubicPoint (a, c1, c2, b, t);
                const auto fade = 1.0f - static_cast<float> (tail) / 4.0f;
                nvgBeginPath (vg);
                nvgCircle (vg, point.x, point.y, (1.4f + energy * 1.6f) * fade + 0.6f);
                nvgFillColor (vg, alpha (lighter (colour, 0.7f), (0.35f + 0.55f * energy) * fade));
                nvgFill (vg);
            }
        }
    }
}

void RackView::drawKnob (juce::Point<float> centre, float radius, float normalised, NVGcolor colour,
                         const juce::String& label, const juce::String& value)
{
    const auto start = juce::MathConstants<float>::pi * 0.75f;
    const auto end = juce::MathConstants<float>::pi * 2.25f;
    nvgBeginPath (vg);
    nvgArc (vg, centre.x, centre.y, radius, start, end, NVG_CW);
    nvgStrokeColor (vg, palette::nodeDark);
    nvgStrokeWidth (vg, 5.0f);
    nvgLineCap (vg, NVG_ROUND);
    nvgStroke (vg);

    const auto angle = start + (end - start) * juce::jlimit (0.0f, 1.0f, normalised);
    if (normalised > 0.005f)
    {
        nvgBeginPath (vg);
        nvgArc (vg, centre.x, centre.y, radius, start, angle, NVG_CW);
        nvgStrokeColor (vg, colour);
        nvgStrokeWidth (vg, 5.0f);
        nvgStroke (vg);
    }
    // Cap with a pointer.
    nvgBeginPath (vg);
    nvgCircle (vg, centre.x, centre.y, radius - 6.0f);
    nvgFillPaint (vg, nvgRadialGradient (vg, centre.x - 3.0f, centre.y - 4.0f, 2.0f, radius,
                                          lighter (palette::nodeTop, 0.18f), palette::nodeDark));
    nvgFill (vg);
    nvgBeginPath (vg);
    nvgMoveTo (vg, centre.x + std::cos (angle) * (radius - 14.0f), centre.y + std::sin (angle) * (radius - 14.0f));
    nvgLineTo (vg, centre.x + std::cos (angle) * (radius - 7.0f), centre.y + std::sin (angle) * (radius - 7.0f));
    nvgStrokeColor (vg, colour);
    nvgStrokeWidth (vg, 2.2f);
    nvgStroke (vg);

    nvgFontFaceId (vg, font);
    nvgTextAlign (vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
    nvgFontSize (vg, 10.0f);
    nvgFillColor (vg, palette::text);
    nvgText (vg, centre.x, centre.y - radius - 12.0f, label.toRawUTF8(), nullptr);
    nvgFontSize (vg, 10.5f);
    nvgFillColor (vg, palette::mutedText);
    nvgText (vg, centre.x, centre.y + radius + 11.0f, value.toRawUTF8(), nullptr);
}

void RackView::drawPlateStatic (const Layout& layout, const NodeModel& model)
{
    // Draws the module at origin (0, 0); the caller has translated for padding.
    const auto x = 0.0f, y = 0.0f, w = layout.w, h = layout.h;
    const auto colour = accent (layout.kind);

    nvgBeginPath (vg);
    nvgRoundedRect (vg, x - 2.0f, y + 4.0f, w + 4.0f, h + 2.0f, corner + 2.0f);
    nvgFillColor (vg, nvgRGBAf (0, 0, 0, 0.32f));
    nvgFill (vg);
    nvgBeginPath (vg);
    nvgRoundedRect (vg, x, y, w, h, corner);
    nvgFillPaint (vg, nvgLinearGradient (vg, x, y, x, y + h, mix (palette::nodeTop, colour, 0.22f),
                                          mix (darker (palette::node, 0.1f), colour, 0.12f)));
    nvgFill (vg);

    for (const auto railY : { y, y + h - railHeight })
    {
        nvgBeginPath (vg);
        nvgRect (vg, x + 1.0f, railY + 1.0f, w - 2.0f, railHeight - 2.0f);
        nvgFillColor (vg, lighter (palette::nodeDark, 0.12f));
        nvgFill (vg);
        for (const auto screwX : { x + 11.0f, x + w - 11.0f })
        {
            nvgBeginPath (vg);
            nvgCircle (vg, screwX, railY + railHeight * 0.5f, 3.2f);
            nvgFillColor (vg, nvgRGBAf (0, 0, 0, 0.55f));
            nvgFill (vg);
        }
    }

    const auto headerY = y + railHeight;
    nvgBeginPath (vg);
    nvgRect (vg, x, headerY, w, headerHeight);
    nvgFillPaint (vg, nvgLinearGradient (vg, x, headerY, x, headerY + headerHeight, darker (colour, 0.02f), darker (colour, 0.35f)));
    nvgFill (vg);
    const bool brightHeader = (colour.r * 0.3f + colour.g * 0.59f + colour.b * 0.11f) > 0.62f;
    const auto titleColour = brightHeader ? palette::nodeDark : palette::text;
    nvgFontFaceId (vg, font);
    nvgFontSize (vg, 13.5f);
    nvgTextLetterSpacing (vg, 0.6f);
    nvgTextAlign (vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
    nvgFillColor (vg, titleColour);
    nvgText (vg, x + 14.0f, headerY + headerHeight * 0.5f, model.processor->getName().toUpperCase().toRawUTF8(), nullptr);
    nvgTextLetterSpacing (vg, 0.0f);
    nvgFontSize (vg, 8.5f);
    nvgTextAlign (vg, NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE);
    nvgFillColor (vg, alpha (titleColour, 0.6f));
    nvgText (vg, x + w - 14.0f, headerY + headerHeight * 0.5f, ("#" + juce::String (layout.id)).toRawUTF8(), nullptr);

    // Ports (sockets and labels; the output glow is live).
    nvgFontSize (vg, 8.5f);
    const juce::Point<float> origin { x, y };
    for (int port = 0; port < layout.inputs; ++port)
    {
        const auto& info = model.processor->getInputPort (port);
        const auto centre = inputPortCentre (layout, origin, port);
        const auto portColour = info.type == SignalType::audio ? palette::audio : palette::control;
        nvgBeginPath (vg);
        if (info.type == SignalType::audio)
            nvgCircle (vg, centre.x, centre.y, 4.5f);
        else
        {
            nvgMoveTo (vg, centre.x, centre.y - 5.0f);
            nvgLineTo (vg, centre.x + 5.0f, centre.y);
            nvgLineTo (vg, centre.x, centre.y + 5.0f);
            nvgLineTo (vg, centre.x - 5.0f, centre.y);
            nvgClosePath (vg);
        }
        nvgFillColor (vg, palette::nodeDark);
        nvgFill (vg);
        nvgStrokeColor (vg, alpha (portColour, info.active ? 0.9f : 0.3f));
        nvgStrokeWidth (vg, 1.4f);
        nvgStroke (vg);
        if (! (layout.inputs > 6 && info.type == SignalType::control))
        {
            nvgTextAlign (vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
            nvgFillColor (vg, alpha (palette::mutedText, 0.9f));
            nvgText (vg, centre.x + 9.0f, centre.y, info.name.toRawUTF8(), nullptr);
        }
    }
    for (int port = 0; port < layout.outputs; ++port)
    {
        const auto& info = model.processor->getOutputPort (port);
        const auto centre = outputPortCentre (layout, origin, port);
        nvgTextAlign (vg, NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE);
        nvgFillColor (vg, alpha (palette::mutedText, 0.9f));
        nvgText (vg, centre.x - 9.0f, centre.y, info.name.toRawUTF8(), nullptr);
    }

    // Preview well.
    const auto previewX = x + 68.0f, previewW = w - 136.0f;
    const auto previewY = y + layout.previewY, previewH = layout.previewH;
    nvgBeginPath (vg);
    nvgRoundedRect (vg, previewX, previewY, previewW, previewH, 5.0f);
    nvgFillColor (vg, palette::nodeDark);
    nvgFill (vg);
    nvgBeginPath (vg);
    nvgMoveTo (vg, previewX + 3.0f, previewY + previewH * 0.5f);
    nvgLineTo (vg, previewX + previewW - 3.0f, previewY + previewH * 0.5f);
    nvgStrokeColor (vg, alpha (palette::grid, 0.7f));
    nvgStrokeWidth (vg, 1.0f);
    nvgStroke (vg);

    // Knobs.
    for (std::size_t knob = 0; knob < layout.knobParameters.size(); ++knob)
    {
        const auto& parameter = model.processor->getParameter (layout.knobParameters[knob]);
        drawKnob (knobCentre (layout, origin, static_cast<int> (knob)), 22.0f, parameter.getNormalisedValue(), colour,
                  parameter.name.toUpperCase(), formatValue (parameter));
    }

    // Stomp switch body (its LED and label are live).
    if (layout.stomp)
    {
        const auto centre = stompCentre (layout, origin);
        nvgBeginPath (vg);
        nvgMoveTo (vg, x + 14.0f, centre.y - stompZoneHeight * 0.5f);
        nvgLineTo (vg, x + w - 14.0f, centre.y - stompZoneHeight * 0.5f);
        nvgStrokeColor (vg, nvgRGBAf (1, 1, 1, 0.05f));
        nvgStrokeWidth (vg, 1.0f);
        nvgStroke (vg);
        nvgBeginPath (vg);
        nvgCircle (vg, centre.x, centre.y, 15.0f);
        nvgFillColor (vg, nvgRGBAf (0, 0, 0, 0.5f));
        nvgFill (vg);
        nvgBeginPath (vg);
        nvgCircle (vg, centre.x, centre.y, 13.0f);
        nvgFillPaint (vg, nvgLinearGradient (vg, centre.x, centre.y - 13.0f, centre.x, centre.y + 13.0f,
                                              lighter (palette::nodeTop, 0.25f), lighter (palette::nodeDark, 0.1f)));
        nvgFill (vg);
        nvgBeginPath (vg);
        nvgCircle (vg, centre.x, centre.y, 10.0f);
        nvgStrokeColor (vg, nvgRGBAf (1, 1, 1, 0.12f));
        nvgStrokeWidth (vg, 1.2f);
        nvgStroke (vg);
    }

    nvgBeginPath (vg);
    nvgRoundedRect (vg, x, y, w, h, corner);
    nvgStrokeColor (vg, nvgRGBAf (1, 1, 1, 0.07f));
    nvgStrokeWidth (vg, 1.0f);
    nvgStroke (vg);
}

void RackView::updatePlateCache (const Layout& layout, float scale)
{
    auto& cache = plates[layout.id];
    const auto logicalW = layout.w + 2.0f * platePadding;
    const auto logicalH = layout.h + 2.0f * platePadding;
    const auto pixelW = juce::jmax (1, static_cast<int> (std::ceil (logicalW * scale)));
    const auto pixelH = juce::jmax (1, static_cast<int> (std::ceil (logicalH * scale)));
    if (cache.framebuffer != nullptr && ! cache.dirty && cache.pixelWidth == pixelW && cache.pixelHeight == pixelH)
        return;
    const auto* model = engine.getDocument().findNode (layout.id);
    if (model == nullptr)
        return;
    if (cache.framebuffer == nullptr || cache.pixelWidth != pixelW || cache.pixelHeight != pixelH)
    {
        if (cache.framebuffer != nullptr)
            nvgluDeleteFramebuffer (cache.framebuffer);
        cache.framebuffer = nvgluCreateFramebuffer (vg, pixelW, pixelH, 0);
        cache.pixelWidth = pixelW;
        cache.pixelHeight = pixelH;
    }
    cache.scale = scale;
    cache.dirty = false;
    ++plateRenders;

    nvgluBindFramebuffer (cache.framebuffer);
    glViewport (0, 0, pixelW, pixelH);
    glClearColor (0.0f, 0.0f, 0.0f, 0.0f);
    glClear (GL_COLOR_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
    nvgBeginFrame (vg, logicalW, logicalH, scale);
    nvgTranslate (vg, platePadding, platePadding);
    drawPlateStatic (layout, *model);
    nvgEndFrame (vg);
    nvgluBindFramebuffer (nullptr);
}

void RackView::drawNode (const Layout& layout, double now)
{
    const auto* model = engine.getDocument().findNode (layout.id);
    if (model == nullptr)
        return;
    const auto origin = nodePosition (layout.id);
    const auto x = origin.x, y = origin.y, w = layout.w, h = layout.h;
    const auto colour = accent (layout.kind);
    const bool selected = selectedNode == layout.id;
    const bool bypassed = model->processor->isBypassed();

    // Cached plate.
    const auto cached = plates.find (layout.id);
    if (cached != plates.end() && cached->second.framebuffer != nullptr)
    {
        const auto logicalW = w + 2.0f * platePadding, logicalH = h + 2.0f * platePadding;
        nvgBeginPath (vg);
        nvgRect (vg, x - platePadding, y - platePadding, logicalW, logicalH);
        nvgFillPaint (vg, nvgImagePattern (vg, x - platePadding, y - platePadding, logicalW, logicalH, 0.0f,
                                           cached->second.framebuffer->image, 1.0f));
        nvgFill (vg);
    }

    // Live: LED, output port glow, scope, status, stomp LED, bypass dim, border.
    const auto headerY = y + railHeight;
    if (layout.outputs > 0)
    {
        float activity = 0.0f;
        for (int port = 0; port < layout.outputs; ++port)
            activity = juce::jmax (activity, model->processor->outputRms (port));
        activity = juce::jlimit (0.0f, 1.0f, std::sqrt (activity));
        const auto ledX = x + w - 50.0f, ledY = headerY + headerHeight * 0.5f;
        if (activity > 0.01f)
        {
            nvgBeginPath (vg);
            nvgCircle (vg, ledX, ledY, 7.0f);
            nvgFillColor (vg, alpha (palette::okay, 0.35f * activity));
            nvgFill (vg);
        }
        nvgBeginPath (vg);
        nvgCircle (vg, ledX, ledY, 2.8f);
        nvgFillColor (vg, activity > 0.01f ? alpha (palette::okay, 0.35f + 0.65f * activity) : lighter (palette::nodeDark, 0.2f));
        nvgFill (vg);
    }
    for (int port = 0; port < layout.outputs; ++port)
    {
        const auto& info = model->processor->getOutputPort (port);
        const auto centre = outputPortCentre (layout, origin, port);
        const auto level = juce::jlimit (0.0f, 1.0f, std::sqrt (juce::jmax (0.0f, model->processor->outputRms (port))));
        const auto portColour = info.type == SignalType::audio ? palette::audio : palette::control;
        if (level > 0.01f)
        {
            nvgBeginPath (vg);
            nvgCircle (vg, centre.x, centre.y, 4.5f + 6.0f * level);
            nvgFillColor (vg, alpha (portColour, 0.25f * level));
            nvgFill (vg);
        }
        nvgBeginPath (vg);
        nvgCircle (vg, centre.x, centre.y, 4.5f);
        nvgFillColor (vg, level > 0.01f ? mix (palette::nodeDark, portColour, level) : palette::nodeDark);
        nvgFill (vg);
        nvgStrokeColor (vg, alpha (portColour, info.active ? 0.9f : 0.3f));
        nvgStrokeWidth (vg, 1.4f);
        nvgStroke (vg);
    }

    drawPreviewContent (layout, *model, origin);

    const auto status = model->processor->statusText();
    if (status.isNotEmpty())
    {
        nvgFontFaceId (vg, font);
        nvgFontSize (vg, 9.0f);
        nvgTextAlign (vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
        nvgFillColor (vg, model->processor->safetyTripped() ? palette::warning : palette::mutedText);
        nvgText (vg, x + w * 0.5f, y + railHeight + headerHeight + 98.0f, status.toRawUTF8(), nullptr);
    }

    if (layout.stomp)
    {
        const auto centre = stompCentre (layout, origin);
        const auto ledX = centre.x - 44.0f;
        if (! bypassed)
        {
            nvgBeginPath (vg);
            nvgCircle (vg, ledX, centre.y, 7.0f);
            nvgFillColor (vg, alpha (colour, 0.35f));
            nvgFill (vg);
        }
        nvgBeginPath (vg);
        nvgCircle (vg, ledX, centre.y, 3.5f);
        nvgFillColor (vg, bypassed ? palette::nodeDark : colour);
        nvgFill (vg);
        nvgFontFaceId (vg, font);
        nvgFontSize (vg, 8.0f);
        nvgTextAlign (vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
        nvgFillColor (vg, alpha (palette::mutedText, 0.8f));
        nvgText (vg, centre.x + 22.0f, centre.y, bypassed ? "BYP" : "ON", nullptr);
    }

    for (std::size_t index = 0; index < layout.buttons.size(); ++index)
    {
        const auto& button = layout.buttons[index];
        const auto bounds = buttonBounds (layout, origin, static_cast<int> (index));
        const bool lit = model->processor->uiToggleState (button.command);
        nvgBeginPath (vg);
        nvgRoundedRect (vg, bounds.getX(), bounds.getY(), bounds.getWidth(), bounds.getHeight(), 4.0f);
        nvgFillColor (vg, lit ? button.active : palette::panelRaised);
        nvgFill (vg);
        nvgStrokeColor (vg, nvgRGBAf (1, 1, 1, lit ? 0.18f : 0.08f));
        nvgStrokeWidth (vg, 1.0f);
        nvgStroke (vg);
        nvgFontFaceId (vg, font);
        nvgFontSize (vg, 10.0f);
        nvgTextAlign (vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
        nvgFillColor (vg, lit ? palette::nodeDark : palette::text);
        nvgText (vg, bounds.getCentreX(), bounds.getCentreY(), button.label.toRawUTF8(), nullptr);
    }

    if (bypassed)
    {
        nvgBeginPath (vg);
        nvgRoundedRect (vg, x + 2.0f, y + 2.0f, w - 4.0f, h - 4.0f - (layout.stomp ? stompZoneHeight + railHeight : 0.0f), 8.0f);
        nvgFillColor (vg, alpha (palette::workspace, 0.55f));
        nvgFill (vg);
    }

    if (model->processor->safetyTripped() || selected)
    {
        nvgBeginPath (vg);
        nvgRoundedRect (vg, x, y, w, h, corner);
        if (model->processor->safetyTripped())
        {
            const auto pulse = 0.5f + 0.5f * static_cast<float> (std::sin (now * 7.0));
            nvgStrokeColor (vg, alpha (palette::warning, 0.5f + 0.5f * pulse));
            nvgStrokeWidth (vg, 2.5f);
        }
        else
        {
            nvgStrokeColor (vg, palette::selection);
            nvgStrokeWidth (vg, 2.0f);
        }
        nvgStroke (vg);
    }
}

void RackView::drawHud (int width, int height, double now)
{
    juce::ignoreUnused (now);
    const auto status = engine.getStatus();
    nvgBeginPath (vg);
    nvgRect (vg, 0, 0, static_cast<float> (width), 34.0f);
    nvgFillColor (vg, alpha (palette::panel, 0.92f));
    nvgFill (vg);
    nvgFontFaceId (vg, font);
    nvgFontSize (vg, 13.0f);
    nvgTextLetterSpacing (vg, 0.8f);
    nvgTextAlign (vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
    nvgFillColor (vg, palette::text);
    nvgText (vg, 16.0f, 17.0f, "SIGNALPATCH 2", nullptr);
    nvgTextLetterSpacing (vg, 0.0f);
    nvgFontSize (vg, 11.0f);
    nvgFillColor (vg, palette::mutedText);
    const auto line = status.deviceName + "  /  " + status.backendName
                    + "   " + juce::String (status.sampleRate / 1000.0, 1) + " kHz  "
                    + juce::String (status.bufferSize) + " smp ("
                    + juce::String (status.bufferSize * 1000.0 / juce::jmax (1.0, status.sampleRate), 2) + " ms)   "
                    + "DSP " + juce::String (status.cpuLoad * 100.0f, 1) + "%   xruns " + juce::String (status.xruns)
                    + "   |   " + juce::String (fps, 0) + " fps  " + juce::String (lastFrameMs, 2) + " ms/frame  "
                    + juce::String (plateRenders) + " plates rasterised";
    nvgText (vg, 150.0f, 17.0f, line.toRawUTF8(), nullptr);

    // AUDIO and FILE buttons and the current patch name.
    nvgBeginPath (vg);
    nvgRoundedRect (vg, static_cast<float> (width) - 300.0f, 6.0f, 54.0f, 22.0f, 4.0f);
    nvgFillColor (vg, palette::panelRaised);
    nvgFill (vg);
    nvgFontSize (vg, 10.5f);
    nvgTextLetterSpacing (vg, 0.8f);
    nvgTextAlign (vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
    nvgFillColor (vg, palette::text);
    nvgText (vg, static_cast<float> (width) - 273.0f, 17.0f, "AUDIO", nullptr);
    nvgBeginPath (vg);
    nvgRoundedRect (vg, static_cast<float> (width) - 240.0f, 6.0f, 50.0f, 22.0f, 4.0f);
    nvgFillColor (vg, palette::panelRaised);
    nvgFill (vg);
    nvgFontSize (vg, 10.5f);
    nvgTextLetterSpacing (vg, 0.8f);
    nvgTextAlign (vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
    nvgFillColor (vg, palette::text);
    nvgText (vg, static_cast<float> (width) - 215.0f, 17.0f, "FILE", nullptr);
    nvgTextLetterSpacing (vg, 0.0f);
    nvgTextAlign (vg, NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE);
    nvgFillColor (vg, palette::mutedText);
    nvgText (vg, static_cast<float> (width) - 310.0f, 17.0f,
             (currentFile == juce::File() ? juce::String ("untitled") : currentFile.getFileName()).toRawUTF8(), nullptr);

    if (status.panicMuted)
    {
        nvgBeginPath (vg);
        nvgRoundedRect (vg, static_cast<float> (width) - 176.0f, 6.0f, 160.0f, 22.0f, 4.0f);
        nvgFillColor (vg, darker (palette::warning, 0.4f));
        nvgFill (vg);
        nvgFontSize (vg, 11.0f);
        nvgTextAlign (vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
        nvgFillColor (vg, palette::text);
        nvgText (vg, static_cast<float> (width) - 96.0f, 17.0f, "MUTED - press M to fade in", nullptr);
    }

    nvgFontSize (vg, 10.5f);
    nvgTextAlign (vg, NVG_ALIGN_LEFT | NVG_ALIGN_BOTTOM);
    nvgFillColor (vg, alpha (palette::mutedText, 0.75f));
    const auto hint = message.isNotEmpty() ? message
        : juce::String ("palette: click adds, drag drops (P hides)  |  drag a port to cable  |  drag the space to pan  |  wheel zooms  |  Del  Ctrl+Z  Ctrl+D  M mute  F fit");
    nvgText (vg, 16.0f, static_cast<float> (height) - 10.0f, hint.toRawUTF8(), nullptr);
}

void RackView::render (int width, int height, float ratio, double now)
{
    const auto start = juce::Time::getMillisecondCounterHiRes();
    pixelRatio = ratio;
    windowW = width;
    windowH = height;
    menu.setWindowSize (width, height);
    if (structureDirty)
        rebuildLayouts();

    // Plates are rasterised at the settled zoom; while the zoom eases they
    // are drawn scaled, then re-rasterised once when it lands.
    const auto wantedScale = juce::jlimit (0.5f, 2.5f, static_cast<float> (zoom) * ratio);
    const bool zoomSettled = std::abs (targetZoom - zoom) < 0.0005;
    if (zoomSettled && std::abs (wantedScale - cachedPlateScale) > 0.01f)
    {
        cachedPlateScale = wantedScale;
        invalidateAllPlates();
    }
    plateRenders = 0;
    if (cachedPlateScale <= 0.0f)
        cachedPlateScale = wantedScale;
    for (const auto& layout : layouts)
        updatePlateCache (layout, cachedPlateScale);
    // Drop caches for modules that no longer exist.
    for (auto it = plates.begin(); it != plates.end();)
    {
        if (layoutFor (it->first) == nullptr)
        {
            if (it->second.framebuffer != nullptr)
                nvgluDeleteFramebuffer (it->second.framebuffer);
            it = plates.erase (it);
        }
        else
            ++it;
    }
    glViewport (0, 0, static_cast<int> (width * ratio), static_cast<int> (height * ratio));

    nvgBeginFrame (vg, static_cast<float> (width), static_cast<float> (height), ratio);
    drawBackground (width, height);

    nvgSave (vg);
    nvgTranslate (vg, static_cast<float> (panX), static_cast<float> (panY));
    nvgScale (vg, static_cast<float> (zoom), static_cast<float> (zoom));

    for (const auto& connection : engine.getDocument().getConnections())
        drawCable (connection, selectedCable.has_value() && *selectedCable == connection, now);

    if (cableDrag.has_value())
    {
        if (const auto* source = layoutFor (cableDrag->sourceNode))
        {
            const auto a = outputPortCentre (*source, nodePosition (source->id), cableDrag->sourcePort);
            const juce::Point<float> b { cableDrag->x, cableDrag->y };
            juce::Point<float> c1, c2;
            bezier (a, b, c1, c2);
            const auto colour = accent (source->kind);
            nvgBeginPath (vg);
            nvgMoveTo (vg, a.x, a.y);
            nvgBezierTo (vg, c1.x, c1.y, c2.x, c2.y, b.x, b.y);
            nvgStrokeColor (vg, alpha (colour, 0.25f));
            nvgStrokeWidth (vg, 6.0f);
            nvgStroke (vg);
            nvgStrokeColor (vg, alpha (colour, 0.9f));
            nvgStrokeWidth (vg, 2.2f);
            nvgStroke (vg);
            nvgBeginPath (vg);
            nvgCircle (vg, b.x, b.y, 4.0f);
            nvgFillColor (vg, colour);
            nvgFill (vg);
        }
    }

    for (const auto& layout : layouts)
        drawNode (layout, now);
    nvgRestore (vg);

    drawPalette (height);
    drawHud (width, height, now);
    menu.draw (width, height);
    browser.draw (width, height);
    prompt.draw (width, height, now);
    nvgEndFrame (vg);

    lastFrameMs = juce::Time::getMillisecondCounterHiRes() - start;
    if (lastRenderTime > 0.0)
    {
        const auto instantaneous = 1.0 / juce::jmax (1.0e-3, now - lastRenderTime);
        fps = fps > 0.0 ? fps * 0.9 + instantaneous * 0.1 : instantaneous;
    }
    lastRenderTime = now;
    dirty = false;
}

// ---------------------------------------------------------------- input

void RackView::character (unsigned int codepoint)
{
    if (prompt.character (static_cast<juce::juce_wchar> (codepoint)) || browser.character (static_cast<juce::juce_wchar> (codepoint)))
        dirty = true;
}

void RackView::mouseMove (double x, double y)
{
    mouseX = x;
    mouseY = y;
    if (browser.isOpen())
    {
        browser.mouseMove (static_cast<float> (x), static_cast<float> (y));
        dirty = true;
        return;
    }
    if (menu.isOpen())
    {
        menu.mouseMove (static_cast<float> (x), static_cast<float> (y));
        dirty = true;
        return;
    }
    const auto world = toWorld (x, y);
    if (paletteDrag.has_value())
    {
        if (juce::Point<double> (x, y).getDistanceFrom ({ paletteDrag->startX, paletteDrag->startY }) > 6.0)
            paletteDrag->moved = true;
        dirty = true;
        return;
    }
    {
        const auto hovered = paletteRowAt (x, y);
        if (hovered != paletteHover)
        {
            paletteHover = hovered;
            dirty = true;
        }
    }
    if (draggingNode.has_value())
    {
        engine.moveNode (*draggingNode, world - dragOffset);
        dirty = true;
    }
    else if (knobDrag.has_value())
    {
        const auto* node = engine.getDocument().findNode (knobDrag->node);
        if (node != nullptr)
        {
            const auto& parameter = node->processor->getParameter (knobDrag->parameter);
            const auto fine = glfwGetKey (glfwGetCurrentContext(), GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS ? 0.2f : 1.0f;
            const auto normalised = juce::jlimit (0.0f, 1.0f,
                knobDrag->startNormalised + static_cast<float> (knobDrag->startY - y) / 180.0f * fine);
            engine.setParameter (knobDrag->node, knobDrag->parameter, parameter.range.convertFrom0to1 (normalised));
            invalidatePlate (knobDrag->node);
            dirty = true;
        }
    }
    else if (sequencerDrag.has_value())
    {
        if (const auto* layout = layoutFor (*sequencerDrag))
        {
            const auto origin = nodePosition (layout->id);
            const auto area = previewArea (*layout, origin).reduced (3.0f);
            editPreviewAt (*layout, origin, { juce::jlimit (area.getX(), area.getRight() - 0.01f, world.x), world.y }, false);
        }
        dirty = true;
    }
    else if (cableDrag.has_value())
    {
        cableDrag->x = world.x;
        cableDrag->y = world.y;
        dirty = true;
    }
    else if (panning)
    {
        panX = panOriginX + (x - panStartX);
        panY = panOriginY + (y - panStartY);
        dirty = true;
    }
}

void RackView::mouseButton (int button, bool pressed, int mods, double x, double y)
{
    juce::ignoreUnused (mods);
    if (prompt.isOpen())
        return;
    if (browser.isOpen())
    {
        browser.mouseButton (button, pressed, static_cast<float> (x), static_cast<float> (y), lastTick);
        dirty = true;
        return;
    }
    if (menu.isOpen())
    {
        menu.mouseButton (button, pressed, static_cast<float> (x), static_cast<float> (y));
        dirty = true;
        return;
    }
    // HUD: the FILE button top-right of the header strip.
    if (pressed && button == GLFW_MOUSE_BUTTON_LEFT && y < 34.0 && x >= windowW - 240.0 && x < windowW - 190.0)
    {
        showFileMenu (x, 34.0);
        return;
    }
    if (pressed && button == GLFW_MOUSE_BUTTON_LEFT && y < 34.0 && x >= windowW - 300.0 && x < windowW - 246.0)
    {
        showAudioMenu (x, 34.0);
        return;
    }
    const auto world = toWorld (x, y);
    const auto hit = static_cast<float> (1.0 / zoom);

    if (! pressed && paletteDrag.has_value())
    {
        const auto kind = paletteDrag->kind;
        const bool dropOnCanvas = paletteDrag->moved && x > paletteWidth;
        const bool click = ! paletteDrag->moved;
        paletteDrag.reset();
        if (dropOnCanvas || click)
        {
            const auto centre = toWorld ((paletteWidth + windowW) * 0.5, (hudHeight + windowH) * 0.5);
            const auto position = dropOnCanvas ? world - juce::Point<float> (118.0f, 22.0f) : centre - juce::Point<float> (118.0f, 100.0f);
            const auto created = engine.addNode (kind, position);
            if (created != 0)
            {
                selectedNode = created;
                selectedCable.reset();
                say (nodeKindName (kind) + " added");
            }
        }
        dirty = true;
        return;
    }
    if (pressed && button == GLFW_MOUSE_BUTTON_LEFT && paletteVisible && x < paletteWidth && y >= hudHeight)
    {
        const auto row = paletteRowAt (x, y);
        if (row >= 0)
            paletteDrag = PaletteDrag { moduleCatalogue()[static_cast<std::size_t> (row)].kind, x, y, false };
        return;
    }
    if (pressed && paletteVisible && x < paletteWidth && y >= hudHeight)
        return; // clicks on the panel never reach the canvas

    if (pressed && button == GLFW_MOUSE_BUTTON_RIGHT)
    {
        for (auto it = layouts.rbegin(); it != layouts.rend(); ++it)
        {
            const auto origin = nodePosition (it->id);
            const juce::Rectangle<float> bounds (origin.x, origin.y, it->w, it->h);
            if (! bounds.expanded (10.0f * hit).contains (world))
                continue;
            for (std::size_t knob = 0; knob < it->knobParameters.size(); ++knob)
                if (knobCentre (*it, origin, static_cast<int> (knob)).getDistanceFrom (world) <= 26.0f)
                {
                    showKnobMenu (*it, it->knobParameters[knob], x, y);
                    return;
                }
            for (int port = 0; port < it->outputs; ++port)
                if (outputPortCentre (*it, origin, port).getDistanceFrom (world) <= 11.0f * hit)
                {
                    showPortMenu (*it, true, port, x, y);
                    return;
                }
            for (int port = 0; port < it->inputs; ++port)
                if (inputPortCentre (*it, origin, port).getDistanceFrom (world) <= 11.0f * hit)
                {
                    showPortMenu (*it, false, port, x, y);
                    return;
                }
            if (bounds.contains (world))
            {
                selectedNode = it->id;
                selectedCable.reset();
                showModuleMenu (*it, x, y);
                return;
            }
        }
        if (const auto cable = cableNear (world, 7.0f * hit))
        {
            selectedCable = cable;
            selectedNode = 0;
            showCableMenu (*cable, x, y);
            return;
        }
        showCanvasMenu (x, y);
        return;
    }

    if (! pressed)
    {
        if (draggingNode.has_value() || knobDrag.has_value() || sequencerDrag.has_value())
            engine.closeEditGesture();
        sequencerDrag.reset();
        if (cableDrag.has_value())
        {
            bool connected = false;
            for (auto it = layouts.rbegin(); it != layouts.rend(); ++it)
            {
                const auto origin = nodePosition (it->id);
                for (int port = 0; port < it->inputs; ++port)
                    if (inputPortCentre (*it, origin, port).getDistanceFrom (world) <= 14.0f * hit)
                    {
                        const auto result = engine.connect ({ cableDrag->sourceNode, cableDrag->sourcePort, it->id, port });
                        message = result.wasOk() ? "Cable connected" : result.getErrorMessage();
                        messageUntil = lastTick + 3.0;
                        connected = true;
                        break;
                    }
                if (connected)
                    break;
            }
        }
        draggingNode.reset();
        knobDrag.reset();
        cableDrag.reset();
        panning = false;
        dirty = true;
        return;
    }

    if (button == GLFW_MOUSE_BUTTON_MIDDLE)
    {
        panning = true;
        panStartX = x; panStartY = y; panOriginX = panX; panOriginY = panY;
        return;
    }
    if (button != GLFW_MOUSE_BUTTON_LEFT)
        return;

    for (auto it = layouts.rbegin(); it != layouts.rend(); ++it)
    {
        const auto& layout = *it;
        const auto origin = nodePosition (layout.id);
        const juce::Rectangle<float> bounds (origin.x, origin.y, layout.w, layout.h);
        if (! bounds.expanded (10.0f * hit).contains (world))
            continue;
        const auto* model = engine.getDocument().findNode (layout.id);
        if (model == nullptr)
            continue;

        for (std::size_t index = 0; index < layout.buttons.size(); ++index)
            if (buttonBounds (layout, origin, static_cast<int> (index)).contains (world))
            {
                runButton (layout, layout.buttons[index]);
                return;
            }
        if (editPreviewAt (layout, origin, world, true))
        {
            if (layout.kind == NodeKind::stepSequencer)
                sequencerDrag = layout.id;
            dirty = true;
            return;
        }
        // Double-click: name plate renames, a knob resets to its default.
        const auto clickTime = lastTick;
        const bool doubleClick = clickTime - lastClickTime < 0.35 && juce::Point<double> (x, y).getDistanceFrom ({ lastClickX, lastClickY }) < 6.0;
        lastClickTime = clickTime;
        lastClickX = x;
        lastClickY = y;
        if (doubleClick && world.y <= origin.y + railHeight + headerHeight && ! layout.hardware)
        {
            const auto id = layout.id;
            prompt.open ("Rename module", model->processor->getName(), [this, id] (const juce::String& name)
            {
                const auto result = engine.renameNode (id, name);
                say (result.wasOk() ? "Renamed" : result.getErrorMessage());
            });
            dirty = true;
            return;
        }
        for (std::size_t knob = 0; knob < layout.knobParameters.size(); ++knob)
            if (knobCentre (layout, origin, static_cast<int> (knob)).getDistanceFrom (world) <= 26.0f)
            {
                const auto parameterIndex = layout.knobParameters[knob];
                if (doubleClick)
                {
                    engine.setParameter (layout.id, parameterIndex, model->processor->getParameter (parameterIndex).defaultValue);
                    engine.closeEditGesture();
                    invalidatePlate (layout.id);
                    dirty = true;
                    return;
                }
                knobDrag = KnobDrag { layout.id, parameterIndex, y,
                                      model->processor->getParameter (parameterIndex).getNormalisedValue() };
                selectedNode = layout.id;
                selectedCable.reset();
                dirty = true;
                return;
            }
        if (layout.stomp && stompCentre (layout, origin).getDistanceFrom (world) <= 16.0f)
        {
            engine.setNodeBypassed (layout.id, ! model->processor->isBypassed());
            dirty = true;
            return;
        }
        for (int port = 0; port < layout.outputs; ++port)
            if (outputPortCentre (layout, origin, port).getDistanceFrom (world) <= 11.0f * hit)
            {
                cableDrag = CableDrag { layout.id, port, model->processor->getOutputPort (port).type, world.x, world.y };
                return;
            }
        for (int port = 0; port < layout.inputs; ++port)
            if (inputPortCentre (layout, origin, port).getDistanceFrom (world) <= 11.0f * hit)
            {
                // Grab an existing cable by its plug and re-route it.
                for (const auto& connection : engine.getDocument().getConnections())
                    if (connection.destinationNode == layout.id && connection.destinationPort == port)
                    {
                        const auto* source = engine.getDocument().findNode (connection.sourceNode);
                        if (source == nullptr)
                            break;
                        cableDrag = CableDrag { connection.sourceNode, connection.sourcePort,
                                                source->processor->getOutputPort (connection.sourcePort).type, world.x, world.y };
                        engine.disconnect (connection);
                        dirty = true;
                        return;
                    }
                return;
            }
        if (! bounds.contains (world))
            continue;
        selectedNode = layout.id;
        selectedCable.reset();
        draggingNode = layout.id;
        dragOffset = world - origin;
        dirty = true;
        return;
    }

    if (const auto cable = cableNear (world, 7.0f * hit))
    {
        selectedCable = cable;
        selectedNode = 0;
        dirty = true;
        return;
    }
    selectedNode = 0;
    selectedCable.reset();
    panning = true;
    panStartX = x; panStartY = y; panOriginX = panX; panOriginY = panY;
    dirty = true;
}

void RackView::scroll (double dx, double dy, int mods, double x, double y)
{
    if (browser.isOpen())
    {
        browser.scroll (dy);
        dirty = true;
        return;
    }
    if (paletteVisible && x < paletteWidth && y >= hudHeight)
    {
        const auto contentHeight = paletteRowTop (static_cast<int> (moduleCatalogue().size()) - 1) + paletteScroll + 40.0f - hudHeight;
        paletteScroll = juce::jlimit (0.0f, juce::jmax (0.0f, contentHeight - (windowH - hudHeight)), paletteScroll - static_cast<float> (dy) * 40.0f);
        paletteHover = paletteRowAt (x, y);
        dirty = true;
        return;
    }
    if ((mods & GLFW_MOD_SHIFT) != 0 || std::abs (dx) > 0.0)
    {
        panX += (std::abs (dx) > 0.0 ? dx : dy) * 40.0;
        panY += (std::abs (dx) > 0.0 ? 0.0 : 0.0);
        dirty = true;
        return;
    }
    targetZoom = juce::jlimit (0.25, 3.0, targetZoom * std::pow (1.12, dy));
    zoomAnchorX = x;
    zoomAnchorY = y;
    dirty = true;
}

void RackView::deleteSelection()
{
    if (selectedCable.has_value())
    {
        engine.disconnect (*selectedCable);
        selectedCable.reset();
        message = "Cable removed";
    }
    else if (selectedNode != 0)
    {
        const auto* node = engine.getDocument().findNode (selectedNode);
        if (node != nullptr && ! node->hardware && engine.removeNode (selectedNode))
            message = "Module removed";
        selectedNode = 0;
    }
    messageUntil = lastTick + 2.0;
    dirty = true;
}

void RackView::key (int keyCode, bool pressed, int mods)
{
    if (! pressed)
        return;
    if (prompt.isOpen())
    {
        prompt.key (keyCode, mods);
        dirty = true;
        return;
    }
    if (browser.isOpen())
    {
        browser.key (keyCode, mods);
        dirty = true;
        return;
    }
    if (menu.isOpen())
    {
        menu.key (keyCode);
        dirty = true;
        return;
    }
    const bool ctrl = (mods & GLFW_MOD_CONTROL) != 0;
    const bool shift = (mods & GLFW_MOD_SHIFT) != 0;
    auto say = [this] (const juce::String& text) { message = text; messageUntil = lastTick + 2.5; dirty = true; };

    if (keyCode == GLFW_KEY_DELETE || keyCode == GLFW_KEY_BACKSPACE)
        deleteSelection();
    else if (ctrl && keyCode == GLFW_KEY_Z && shift)
        say (engine.redo() ? "Redo" : "Nothing to redo");
    else if (ctrl && keyCode == GLFW_KEY_Z)
        say (engine.undo() ? "Undo" : "Nothing to undo");
    else if (ctrl && keyCode == GLFW_KEY_Y)
        say (engine.redo() ? "Redo" : "Nothing to redo");
    else if (ctrl && keyCode == GLFW_KEY_D)
    {
        const auto copy = selectedNode != 0 ? engine.duplicateNode (selectedNode) : 0;
        if (copy != 0)
            selectedNode = copy;
        say (copy != 0 ? "Duplicated" : "Select a module first");
    }
    else if (ctrl && keyCode == GLFW_KEY_S)
    {
        if (shift)
            saveAsPrompt();
        else
            saveCurrentPatch();
    }
    else if (ctrl && keyCode == GLFW_KEY_O)
        browser.open ("Open patch", currentFile != juce::File() ? currentFile.getParentDirectory() : documentsFolder ("patches"),
                      { "signalpatch", "zip" }, [this] (const juce::File& file) { openPatchFile (file); });
    else if (ctrl && keyCode == GLFW_KEY_N)
    {
        engine.newPatch();
        currentFile = juce::File();
        say ("New patch (muted) - M to fade in");
    }
    else if (ctrl && keyCode == GLFW_KEY_Q)
        requestQuit();
    else if (keyCode == GLFW_KEY_P && ! ctrl)
    {
        paletteVisible = ! paletteVisible;
        dirty = true;
    }
    else if (keyCode == GLFW_KEY_M)
    {
        engine.togglePanic();
        say (engine.isPanicMuted() ? "Muted" : "Fading in");
    }
    else if (keyCode == GLFW_KEY_ESCAPE)
    {
        engine.setPanicMuted (true);
        say ("PANIC: muted");
    }
    else if (keyCode == GLFW_KEY_F)
    {
        int width = 0, height = 0;
        glfwGetWindowSize (glfwGetCurrentContext(), &width, &height);
        fitToPatch (width, height);
    }
    else if (keyCode == GLFW_KEY_0 && ctrl)
    {
        targetZoom = 1.0;
        zoomAnchorX = mouseX;
        zoomAnchorY = mouseY;
        dirty = true;
    }
}
} // namespace signalpatch::v2
