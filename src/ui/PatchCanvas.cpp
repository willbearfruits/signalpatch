#include "PatchCanvas.h"

#include <algorithm>
#include <cstdlib>

#include <cmath>

namespace signalpatch::ui
{
PatchCanvas::PatchCanvas (PatchEngine& engineToUse)
    : engine (engineToUse)
{
    setSize (2400, 1600);
    setWantsKeyboardFocus (true);
    engine.addChangeListener (this);
    syncNodeComponents();
    setOpaque (true); // the background cache covers every pixel: nothing beneath needs painting
    startTimerHz (30);
}

PatchCanvas::~PatchCanvas()
{
    stopTimer();
    engine.removeChangeListener (this);
}

void PatchCanvas::addNodeAtVisibleCentre (NodeKind kind, juce::Rectangle<int> visibleArea)
{
    const auto position = juce::Point<float> (static_cast<float> (visibleArea.getCentreX() - 140),
                                               static_cast<float> (visibleArea.getCentreY() - 100));
    const auto id = engine.addNode (kind, position);
    if (id != 0)
    {
        selectNode (id);
        if (onStatus)
            onStatus (nodeKindName (kind) + " added");
    }
}

void PatchCanvas::insertNodeOnCable (const Connection& cable, NodeKind kind, juce::Point<float> position)
{
    const auto id = engine.addNode (kind, position - juce::Point<float> (140.0f, 100.0f));
    if (id == 0)
        return;
    const auto* node = engine.getDocument().findNode (id);
    if (node == nullptr || node->processor->getNumInputPorts() == 0 || node->processor->getNumOutputPorts() == 0)
    {
        if (onStatus)
            onStatus (nodeKindName (kind) + " added (could not splice it into the cable)");
        return;
    }
    engine.disconnect (cable);
    const auto in = engine.connect ({ cable.sourceNode, cable.sourcePort, id, 0 });
    const auto out = engine.connect ({ id, 0, cable.destinationNode, cable.destinationPort });
    selectNode (id);
    if (onStatus)
        onStatus (in.wasOk() && out.wasOk() ? nodeKindName (kind) + " inserted into the cable"
                                            : (in.failed() ? in.getErrorMessage() : out.getErrorMessage()));
}

void PatchCanvas::deleteSelection()
{
    if (selectedConnection.has_value())
    {
        engine.disconnect (*selectedConnection);
        selectedConnection.reset();
        if (onCableSelectionChanged)
            onCableSelectionChanged (std::nullopt);
        if (onStatus)
            onStatus ("Cable removed");
        repaint();
        return;
    }

    if (selectedNodeId != 0)
    {
        const auto* node = engine.getDocument().findNode (selectedNodeId);
        if (node != nullptr && ! node->hardware && engine.removeNode (selectedNodeId))
        {
            selectedNodeId = 0;
            if (onSelectionChanged)
                onSelectionChanged (0);
            if (onStatus)
                onStatus ("Node removed");
        }
    }
}

void PatchCanvas::changeListenerCallback (juce::ChangeBroadcaster*)
{
    cableCaches.clear(); // connections or node identities may have changed
    syncNodeComponents();
    repaint();
}

void PatchCanvas::timerCallback()
{
    // Attribution switches for profiling only.
    static const bool skipTimer = std::getenv ("SIGNALPATCH_NO_TIMER") != nullptr;
    static const bool skipCables = std::getenv ("SIGNALPATCH_NO_CABLES") != nullptr;
    static const bool skipLive = std::getenv ("SIGNALPATCH_NO_LIVE") != nullptr;
    if (skipTimer)
        return;
    animationPhase += 0.013f;
    if (animationPhase >= 1.0f)
        animationPhase -= 1.0f;

    // Only the cables (level glow, comets) and each module's live layer move
    // between ticks; the background and face plates are cached bitmaps.
    for (const auto& connection : engine.getDocument().getConnections())
    {
        if (skipCables)
            break;
        const auto* source = componentForNode (connection.sourceNode);
        const auto* destination = componentForNode (connection.destinationNode);
        if (source == nullptr || destination == nullptr)
            continue;

        // A cable's glow and comet follow its source signal; if that meter has
        // not published since the last tick the cable looks exactly the same.
        const auto* sourceNode = engine.getDocument().findNode (connection.sourceNode);
        const auto version = sourceNode != nullptr
            ? sourceNode->processor->outputTelemetryVersion (connection.sourcePort) : 0u;
        auto cached = std::find_if (cableVersions.begin(), cableVersions.end(),
                                    [&] (const auto& entry) { return entry.first == connection; });
        if (cached == cableVersions.end())
            cached = cableVersions.insert (cableVersions.end(), { connection, version + 1 });
        if (cached->second == version)
            continue;
        cached->second = version;

        // Invalidate a band along the cable, not its bounding box: a long
        // diagonal cable would otherwise dirty half the canvas every tick.
        const auto* cache = cableCacheFor (connection);
        if (cache == nullptr || cache->points.size() < 2)
            continue;
        constexpr int segments = 12;
        auto previous = cache->pointAlong (0.0f);
        for (int segment = 1; segment <= segments; ++segment)
        {
            const auto next = cache->pointAlong (cache->length * static_cast<float> (segment) / segments);
            repaint (juce::Rectangle<float>::leftTopRightBottom (juce::jmin (previous.x, next.x), juce::jmin (previous.y, next.y),
                                                                 juce::jmax (previous.x, next.x), juce::jmax (previous.y, next.y))
                         .expanded (12.0f).toNearestInt());
            previous = next;
        }
    }
    if (! skipLive)
        for (auto& component : nodeComponents)
            component->repaintLive();
}

NodeComponent* PatchCanvas::componentForNode (NodeId id) const noexcept
{
    for (const auto& component : nodeComponents)
        if (component->getNodeId() == id)
            return component.get();
    return nullptr;
}

void PatchCanvas::syncNodeComponents()
{
    const auto& nodes = engine.getDocument().getNodes();
    bool rebuild = nodes.size() != nodeComponents.size();
    if (! rebuild)
    {
        for (const auto& node : nodes)
        {
            auto* component = componentForNode (node.id);
            const auto expectedSignature = node.processor->getNumInputPorts() * 10000
                                         + node.processor->getNumOutputPorts() * 100
                                         + node.processor->getNumParameters();
            if (component == nullptr || component->signature() != expectedSignature)
            {
                rebuild = true;
                break;
            }
        }
    }

    if (rebuild)
    {
        nodeComponents.clear();
        for (const auto& node : nodes)
        {
            auto component = std::make_unique<NodeComponent> (engine, node.id);
            component->onSelected = [this] (NodeId id) { selectNode (id); };
            component->onRemoveRequested = [this] (NodeId id)
            {
                selectNode (id);
                deleteSelection();
            };
            component->onCableDragStarted = [this] (NodeId nodeId, int port, juce::Point<float> point)
            {
                beginCableDrag (nodeId, port, point);
            };
            component->onCableDragged = [this] (juce::Point<float> point) { updateCableDrag (point); };
            component->onCableDragEnded = [this] (juce::Point<float> point) { endCableDrag (point); };
            addAndMakeVisible (*component);
            nodeComponents.push_back (std::move (component));
        }
    }

    for (const auto& node : nodes)
    {
        if (auto* component = componentForNode (node.id))
        {
            component->setBounds (juce::roundToInt (node.position.x),
                                  juce::roundToInt (node.position.y),
                                  component->preferredWidth(),
                                  component->preferredHeight());
            component->setSelected (node.id == selectedNodeId);
        }
    }

    if (selectedNodeId != 0 && engine.getDocument().findNode (selectedNodeId) == nullptr)
        selectNode (0);
}

void PatchCanvas::selectNode (NodeId id)
{
    selectedNodeId = id;
    selectedConnection.reset();
    if (onCableSelectionChanged)
        onCableSelectionChanged (std::nullopt);
    for (auto& component : nodeComponents)
        component->setSelected (component->getNodeId() == id);
    if (onSelectionChanged)
        onSelectionChanged (id);
    grabKeyboardFocus();
    repaint();
}

juce::Path PatchCanvas::cablePath (juce::Point<float> start, juce::Point<float> end) const
{
    // Short straight leads out of the ports read like a real patch cable and
    // keep the curve from folding back over the node edge. The control points
    // droop below the endpoints so cables hang like they would on a pedalboard.
    constexpr auto stub = 9.0f;
    const auto distance = start.getDistanceFrom (end);
    const auto handle = juce::jlimit (60.0f, 240.0f, std::abs (end.x - start.x) * 0.48f + 45.0f);
    const auto droop = juce::jmin (85.0f, distance * 0.16f + 14.0f);
    juce::Path path;
    path.startNewSubPath (start);
    path.lineTo (start.x + stub, start.y);
    path.cubicTo (start.x + stub + handle, start.y + droop,
                  end.x - stub - handle, end.y + droop,
                  end.x - stub, end.y);
    path.lineTo (end);
    return path;
}

std::optional<Connection> PatchCanvas::cableNear (juce::Point<float> point) const
{
    for (const auto& connection : engine.getDocument().getConnections())
    {
        const auto* source = componentForNode (connection.sourceNode);
        const auto* destination = componentForNode (connection.destinationNode);
        if (source == nullptr || destination == nullptr)
            continue;
        const auto path = cablePath (source->outputPortCentreInParent (connection.sourcePort),
                                     destination->inputPortCentreInParent (connection.destinationPort));
        juce::Point<float> nearest;
        path.getNearestPoint (point, nearest);
        if (nearest.getDistanceFrom (point) < 7.0f)
            return connection;
    }
    return std::nullopt;
}

void PatchCanvas::beginCableDrag (NodeId sourceNode, int sourcePort, juce::Point<float> start)
{
    const auto* node = engine.getDocument().findNode (sourceNode);
    if (node == nullptr || ! juce::isPositiveAndBelow (sourcePort, node->processor->getNumOutputPorts()))
        return;
    cableDrag = CableDrag { sourceNode, sourcePort,
                            node->processor->getOutputPort (sourcePort).type,
                            start, start };
    selectNode (sourceNode);
}

void PatchCanvas::updateCableDrag (juce::Point<float> point)
{
    if (cableDrag.has_value())
    {
        cableDrag->current = point;
        repaint();
    }
}

void PatchCanvas::endCableDrag (juce::Point<float> point)
{
    if (! cableDrag.has_value())
        return;

    for (const auto& component : nodeComponents)
    {
        if (const auto port = component->inputPortNear (point, 15.0f))
        {
            const auto* destination = engine.getDocument().findNode (component->getNodeId());
            if (destination == nullptr)
                continue;
            if (destination->processor->getInputPort (*port).type != cableDrag->type)
                continue;
            const Connection connection { cableDrag->sourceNode, cableDrag->sourcePort,
                                          component->getNodeId(), *port };
            const auto result = engine.connect (connection);
            if (onStatus)
                onStatus (result.wasOk() ? "Cable connected" : result.getErrorMessage());
            cableDrag.reset();
            repaint();
            return;
        }
    }

    if (onStatus)
        onStatus ("Cable cancelled");
    cableDrag.reset();
    repaint();
}

juce::Point<float> PatchCanvas::CableCache::pointAlong (float distance) const noexcept
{
    if (points.empty())
        return start;
    distance = juce::jlimit (0.0f, length, distance);
    const auto upper = std::upper_bound (distances.begin(), distances.end(), distance);
    const auto index = static_cast<std::size_t> (juce::jlimit<std::ptrdiff_t> (1, static_cast<std::ptrdiff_t> (points.size()) - 1,
                                                                                upper - distances.begin()));
    const auto segment = distances[index] - distances[index - 1];
    const auto t = segment > 0.0f ? (distance - distances[index - 1]) / segment : 0.0f;
    return points[index - 1] + (points[index] - points[index - 1]) * t;
}

const PatchCanvas::CableCache* PatchCanvas::cableCacheFor (const Connection& connection)
{
    const auto* sourceComponent = componentForNode (connection.sourceNode);
    const auto* destinationComponent = componentForNode (connection.destinationNode);
    const auto* sourceModel = engine.getDocument().findNode (connection.sourceNode);
    const auto* destinationModel = engine.getDocument().findNode (connection.destinationNode);
    if (sourceComponent == nullptr || destinationComponent == nullptr
        || sourceModel == nullptr || destinationModel == nullptr)
        return nullptr;

    const auto start = sourceComponent->outputPortCentreInParent (connection.sourcePort);
    const auto end = destinationComponent->inputPortCentreInParent (connection.destinationPort);
    const bool feedback = sourceModel->processor->isFeedbackGuard()
                       || destinationModel->processor->isFeedbackGuard();
    // Rack wiring: every cable wears its source module's colour. Signal type
    // stays readable through the stroke style (control cables are dashed).
    const auto colour = feedback ? colours::feedback : kindAccent (sourceModel->processor->getKind());
    const auto type = sourceModel->processor->getOutputPort (connection.sourcePort).type;

    auto found = std::find_if (cableCaches.begin(), cableCaches.end(),
                               [&] (const CableCache& entry) { return entry.connection == connection; });
    if (found != cableCaches.end() && found->start == start && found->end == end && found->colour == colour)
        return &*found;
    if (found == cableCaches.end())
        found = cableCaches.insert (cableCaches.end(), CableCache {});

    auto& entry = *found;
    entry.connection = connection;
    entry.start = start;
    entry.end = end;
    entry.colour = colour;
    entry.type = type;
    entry.feedback = feedback;
    entry.path = cablePath (start, end);

    entry.points.clear();
    entry.distances.clear();
    juce::PathFlatteningIterator iterator (entry.path, {}, 1.0f);
    float running = 0.0f;
    while (iterator.next())
    {
        if (entry.points.empty())
        {
            entry.points.push_back ({ iterator.x1, iterator.y1 });
            entry.distances.push_back (0.0f);
        }
        running += juce::Point<float> (iterator.x1, iterator.y1).getDistanceFrom ({ iterator.x2, iterator.y2 });
        entry.points.push_back ({ iterator.x2, iterator.y2 });
        entry.distances.push_back (running);
    }
    entry.length = running;

    entry.bounds = entry.path.getBounds().expanded (14.0f).toNearestInt();
    const auto width = juce::jmax (1, entry.bounds.getWidth());
    const auto height = juce::jmax (1, entry.bounds.getHeight());
    const auto rounded = [] (float strokeWidth)
    {
        return juce::PathStrokeType (strokeWidth, juce::PathStrokeType::curved,
                                     juce::PathStrokeType::rounded);
    };
    const auto shift = juce::AffineTransform::translation (static_cast<float> (-entry.bounds.getX()),
                                                           static_cast<float> (-entry.bounds.getY()));
    constexpr float quietWidth = 2.0f;

    entry.core = juce::Image (juce::Image::ARGB, width, height, true);
    {
        juce::Graphics g (entry.core);
        g.addTransform (shift);
        g.setColour (juce::Colours::black.withAlpha (0.5f));
        g.strokePath (entry.path, rounded (quietWidth + 3.5f), juce::AffineTransform::translation (0.0f, 2.0f));
        g.setColour (colour.withAlpha (0.06f));
        g.strokePath (entry.path, rounded (quietWidth + 7.0f));
        g.setColour (colour.withAlpha (0.12f));
        g.strokePath (entry.path, rounded (quietWidth + 3.0f));
        if (type == SignalType::control)
        {
            juce::Path dashed;
            const float dashes[] { 7.0f, 5.0f };
            rounded (quietWidth).createDashedStroke (dashed, entry.path, dashes, 2);
            g.setColour (colour.withAlpha (0.6f));
            g.fillPath (dashed);
        }
        else
        {
            g.setGradientFill (juce::ColourGradient (colour.brighter (0.15f), start,
                                                     colour.darker (0.1f), end, false));
            g.strokePath (entry.path, rounded (quietWidth));
        }
    }

    // Everything the signal adds at full level: wider halos and a brighter core.
    constexpr float hotWidth = quietWidth + 2.6f;
    entry.glow = juce::Image (juce::Image::ARGB, width, height, true);
    {
        juce::Graphics g (entry.glow);
        g.addTransform (shift);
        g.setColour (colour.withAlpha (0.30f));
        g.strokePath (entry.path, rounded (hotWidth + 7.0f));
        g.setColour (colour.withAlpha (0.30f));
        g.strokePath (entry.path, rounded (hotWidth + 3.0f));
        g.setColour (colour.brighter (0.2f).withAlpha (type == SignalType::control ? 0.4f : 0.9f));
        g.strokePath (entry.path, rounded (type == SignalType::control ? quietWidth : hotWidth));
    }
    return &entry;
}

void PatchCanvas::drawCable (juce::Graphics& graphics, const Connection& connection, bool isSelected)
{
    const auto* cache = cableCacheFor (connection);
    const auto* sourceModel = engine.getDocument().findNode (connection.sourceNode);
    if (cache == nullptr || sourceModel == nullptr)
        return;

    const auto start = cache->start;
    const auto end = cache->end;
    const auto& path = cache->path;
    const auto type = cache->type;
    const bool feedback = cache->feedback;
    const auto colour = cache->colour;
    const auto rms = sourceModel->processor->outputRms (connection.sourcePort);
    const auto energy = juce::jlimit (0.0f, 1.0f, std::sqrt (juce::jmax (0.0f, rms)));
    const auto width = (isSelected ? 3.4f : 2.0f) + energy * 2.6f;
    const auto rounded = [] (float strokeWidth)
    {
        return juce::PathStrokeType (strokeWidth, juce::PathStrokeType::curved,
                                     juce::PathStrokeType::rounded);
    };

    if (isSelected)
    {
        // The selected cable is one-of-a-kind; stroke it live.
        graphics.setColour (juce::Colours::black.withAlpha (0.5f));
        graphics.strokePath (path, rounded (width + 3.5f), juce::AffineTransform::translation (0.0f, 2.0f));
        graphics.setColour (colour.withAlpha (0.06f + energy * 0.30f));
        graphics.strokePath (path, rounded (width + 7.0f));
        graphics.setColour (colour.withAlpha (0.12f + energy * 0.30f));
        graphics.strokePath (path, rounded (width + 3.0f));
        graphics.setColour (colours::selection.withAlpha (0.35f));
        graphics.strokePath (path, rounded (width + 5.0f));
        if (type == SignalType::control)
        {
            juce::Path dashed;
            const float dashes[] { 7.0f, 5.0f };
            rounded (width).createDashedStroke (dashed, path, dashes, 2);
            graphics.setColour (colours::selection.withAlpha (0.6f + energy * 0.4f));
            graphics.fillPath (dashed);
        }
        else
        {
            graphics.setGradientFill (juce::ColourGradient (colours::selection.brighter (0.15f), start,
                                                            colours::selection.darker (0.1f), end, false));
            graphics.strokePath (path, rounded (width));
        }
    }
    else
    {
        // Stroking three halos plus a core for every cable at 30 Hz was the
        // canvas's main cost. The shadow+core and the full-energy glow are
        // rasterised once per geometry and blitted; the glow's opacity follows
        // the live level, which reads the same as the stroke widening did.
        graphics.drawImageAt (cache->core, cache->bounds.getX(), cache->bounds.getY());
        if (energy > 0.01f)
        {
            graphics.setOpacity (energy);
            graphics.drawImageAt (cache->glow, cache->bounds.getX(), cache->bounds.getY());
            graphics.setOpacity (1.0f);
        }
    }

    // Directional comets: a bright head with a fading tail marching from the
    // source to the destination shows which way the signal flows.
    const auto length = cache->length;
    if (length > 1.0f && (energy > 0.012f || type == SignalType::control))
    {
        const auto cometCount = juce::jlimit (2, 5, static_cast<int> (length / 110.0f) + 2);
        const auto speed = type == SignalType::control && ! feedback ? 0.6f : 1.0f;
        const auto tailProportion = juce::jmin (0.08f, 30.0f / length);
        constexpr int tailSegments = 4;
        for (int comet = 0; comet < cometCount; ++comet)
        {
            const auto head = std::fmod (animationPhase * speed
                                             + static_cast<float> (comet) / static_cast<float> (cometCount),
                                         1.0f);
            for (int segment = 0; segment < tailSegments; ++segment)
            {
                const auto p1 = head - tailProportion * static_cast<float> (segment) / tailSegments;
                const auto p0 = head - tailProportion * static_cast<float> (segment + 1) / tailSegments;
                if (p1 <= 0.0f)
                    continue;
                const auto pointA = cache->pointAlong (length * juce::jmax (0.0f, p0));
                const auto pointB = cache->pointAlong (length * p1);
                const auto fade = static_cast<float> (tailSegments - segment) / tailSegments;
                graphics.setColour (colour.brighter (0.7f)
                                        .withAlpha ((0.28f + 0.5f * energy) * fade));
                graphics.drawLine ({ pointA, pointB }, (1.2f + energy * 2.2f) * fade + 0.6f);
            }
            const auto headPoint = cache->pointAlong (length * head);
            graphics.setColour (colour.brighter (0.9f).withAlpha (0.55f + 0.45f * energy));
            graphics.fillEllipse (headPoint.x - 2.0f, headPoint.y - 2.0f, 4.0f, 4.0f);
        }
    }

    if (sourceModel->processor->outputPeak (connection.sourcePort) >= 0.98f)
    {
        const auto pulse = 0.5f + 0.5f * std::sin (animationPhase * juce::MathConstants<float>::twoPi * 3.0f);
        const auto point = cache->pointAlong (length * 0.84f);
        graphics.setColour (colours::warning.withAlpha (0.35f + 0.3f * pulse));
        graphics.fillEllipse (point.x - 7.0f, point.y - 7.0f, 14.0f, 14.0f);
        graphics.setColour (colours::warning);
        graphics.fillEllipse (point.x - 3.6f, point.y - 3.6f, 7.2f, 7.2f);
    }
}

void PatchCanvas::ensureBackgroundCache()
{
    if (backgroundCache.isValid()
        && backgroundCache.getWidth() == getWidth()
        && backgroundCache.getHeight() == getHeight())
        return;

    backgroundCache = juce::Image (juce::Image::ARGB, juce::jmax (1, getWidth()),
                                   juce::jmax (1, getHeight()), false);
    juce::Graphics graphics (backgroundCache);

    const auto width = static_cast<float> (getWidth());
    const auto height = static_cast<float> (getHeight());
    juce::ColourGradient vignette (colours::workspace.brighter (0.06f),
                                   width * 0.5f, height * 0.42f,
                                   colours::workspaceEdge,
                                   0.0f, 0.0f, true);
    vignette.addColour (0.72, colours::workspace);
    graphics.setGradientFill (vignette);
    graphics.fillAll();

    // Laboratory graph paper: a quiet dot lattice with brighter reference
    // crosses. Cached once so the animated repaints only blit an image.
    constexpr int minorStep = 24;
    constexpr int majorStep = 120;
    juce::RectangleList<float> dots;
    for (int x = minorStep; x < getWidth(); x += minorStep)
        for (int y = minorStep; y < getHeight(); y += minorStep)
            if (x % majorStep != 0 || y % majorStep != 0)
                dots.addWithoutMerging ({ static_cast<float> (x) - 0.8f,
                                          static_cast<float> (y) - 0.8f, 1.6f, 1.6f });
    graphics.setColour (colours::gridDot.withAlpha (0.5f));
    graphics.fillRectList (dots);

    graphics.setColour (colours::gridDot.brighter (0.35f).withAlpha (0.65f));
    for (int x = majorStep; x < getWidth(); x += majorStep)
        for (int y = majorStep; y < getHeight(); y += majorStep)
        {
            const auto fx = static_cast<float> (x);
            const auto fy = static_cast<float> (y);
            graphics.drawLine (fx - 4.5f, fy, fx + 4.5f, fy, 1.0f);
            graphics.drawLine (fx, fy - 4.5f, fx, fy + 4.5f, 1.0f);
        }

    // Faint horizontal rack rails with mounting slots suggest rows of a rack
    // without constraining where modules can sit.
    constexpr int railSpacing = 264;
    for (int y = railSpacing; y < getHeight(); y += railSpacing)
    {
        const auto fy = static_cast<float> (y);
        graphics.setColour (juce::Colours::white.withAlpha (0.025f));
        graphics.fillRect (0.0f, fy - 4.0f, width, 8.0f);
        graphics.setColour (juce::Colours::black.withAlpha (0.18f));
        graphics.drawHorizontalLine (y - 4, 0.0f, width);
        graphics.drawHorizontalLine (y + 4, 0.0f, width);
        graphics.setColour (juce::Colours::black.withAlpha (0.3f));
        for (int x = 30; x < getWidth(); x += 96)
            graphics.fillRoundedRectangle (static_cast<float> (x), fy - 1.5f, 12.0f, 3.0f, 1.5f);
    }
}

void PatchCanvas::paint (juce::Graphics& graphics)
{
    ++paintStats::canvasPaints;
    ensureBackgroundCache();
    graphics.drawImageAt (backgroundCache, 0, 0);

    for (const auto& connection : engine.getDocument().getConnections())
        drawCable (graphics, connection,
                   selectedConnection.has_value() && *selectedConnection == connection);

    if (cableDrag.has_value())
    {
        const auto* dragSource = engine.getDocument().findNode (cableDrag->sourceNode);
        const auto colour = dragSource != nullptr
            ? kindAccent (dragSource->processor->getKind())
            : (cableDrag->type == SignalType::audio ? colours::audio : colours::control);
        const auto path = cablePath (cableDrag->start, cableDrag->current);
        graphics.setColour (colour.withAlpha (0.25f));
        graphics.strokePath (path, juce::PathStrokeType (6.0f,
                                                         juce::PathStrokeType::curved,
                                                         juce::PathStrokeType::rounded));
        graphics.setColour (colour.withAlpha (0.9f));
        graphics.strokePath (path, juce::PathStrokeType (2.2f,
                                                         juce::PathStrokeType::curved,
                                                         juce::PathStrokeType::rounded));
        graphics.fillEllipse (cableDrag->current.x - 4.0f, cableDrag->current.y - 4.0f, 8.0f, 8.0f);
        graphics.setColour (colour.withAlpha (0.3f));
        graphics.fillEllipse (cableDrag->current.x - 8.0f, cableDrag->current.y - 8.0f, 16.0f, 16.0f);
    }

    if (engine.getDocument().getConnections().empty())
    {
        auto hint = getLocalBounds().withSizeKeepingCentre (560, 66).toFloat();
        graphics.setColour (colours::panel.withAlpha (0.85f));
        graphics.fillRoundedRectangle (hint, 10.0f);
        graphics.setColour (colours::grid.brighter (0.4f));
        graphics.drawRoundedRectangle (hint, 10.0f, 1.0f);
        graphics.setColour (colours::text.withAlpha (0.85f));
        graphics.setFont (juce::FontOptions (15.0f, juce::Font::bold));
        graphics.drawText ("PATCH SOMETHING",
                           hint.removeFromTop (34.0f).toNearestInt(), juce::Justification::centredBottom);
        graphics.setColour (colours::mutedText);
        graphics.setFont (juce::FontOptions (12.0f));
        graphics.drawText ("drag from a glowing output port to a matching input",
                           hint.toNearestInt(), juce::Justification::centredTop);
    }
}

void PatchCanvas::resized()
{
    syncNodeComponents();
}

void PatchCanvas::showAddNodeMenu (juce::Point<float> position)
{
    juce::PopupMenu menu;
    juce::String currentGroup;
    juce::PopupMenu groupMenu;
    for (const auto& entry : nodePalette())
    {
        if (currentGroup != entry.group)
        {
            if (currentGroup.isNotEmpty())
                menu.addSubMenu (currentGroup, groupMenu);
            groupMenu = {};
            currentGroup = entry.group;
        }
        groupMenu.addItem (static_cast<int> (entry.kind) + 1000, entry.label);
    }
    if (currentGroup.isNotEmpty())
        menu.addSubMenu (currentGroup, groupMenu);

    menu.addSeparator();
    menu.addItem (1, engine.canUndo() ? "Undo " + engine.getUndoDescription() : juce::String ("Undo"), engine.canUndo());
    menu.addItem (2, engine.canRedo() ? "Redo " + engine.getRedoDescription() : juce::String ("Redo"), engine.canRedo());
    menu.addSeparator();
    menu.addItem (3, engine.isPanicMuted() ? "Unmute (fade in)" : "Panic mute");

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this),
                        [this, position] (int result)
    {
        if (result == 1) { const auto text = engine.getUndoDescription(); if (engine.undo() && onStatus) onStatus ("Undo: " + text); return; }
        if (result == 2) { const auto text = engine.getRedoDescription(); if (engine.redo() && onStatus) onStatus ("Redo: " + text); return; }
        if (result == 3) { engine.togglePanic(); if (onStatus) onStatus (engine.isPanicMuted() ? "Panic: muted" : "Fading in"); return; }
        if (result < 1000)
            return;
        const auto kind = static_cast<NodeKind> (result - 1000);
        const auto id = engine.addNode (kind, position - juce::Point<float> (140.0f, 100.0f));
        if (id != 0)
        {
            selectNode (id);
            if (onStatus)
                onStatus (nodeKindName (kind) + " added");
        }
    });
}

void PatchCanvas::mouseDown (const juce::MouseEvent& event)
{
    grabKeyboardFocus();
    if (event.mods.isPopupMenu())
    {
        if (const auto connection = cableNear (event.position))
        {
            selectedConnection = connection;
            if (onCableSelectionChanged)
                onCableSelectionChanged (connection);
            repaint();
            juce::PopupMenu menu;
            menu.addItem (1, "Disconnect cable");
            const auto* source = engine.getDocument().findNode (connection->sourceNode);
            const bool audioCable = source != nullptr
                && source->processor->getOutputPort (connection->sourcePort).type == SignalType::audio;
            if (audioCable)
            {
                // Insert a module into the cable: audio-in/audio-out families only.
                juce::PopupMenu insert;
                juce::String currentGroup;
                juce::PopupMenu groupMenu;
                for (const auto& entry : nodePalette())
                {
                    if (entry.group != "EFFECTS" && entry.group != "NEURAL" && entry.group != "DYNAMICS"
                        && entry.group != "VOICE" && entry.kind != NodeKind::gain)
                        continue;
                    if (currentGroup != entry.group)
                    {
                        if (currentGroup.isNotEmpty())
                            insert.addSubMenu (currentGroup, groupMenu);
                        groupMenu = {};
                        currentGroup = entry.group;
                    }
                    groupMenu.addItem (static_cast<int> (entry.kind) + 1000, entry.label);
                }
                if (currentGroup.isNotEmpty())
                    insert.addSubMenu (currentGroup, groupMenu);
                menu.addSubMenu ("Insert module here", insert);
            }
            const auto cable = *connection;
            const auto position = event.position;
            menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this),
                                [this, cable, position] (int result)
            {
                if (result == 1)
                {
                    selectedConnection = cable;
                    deleteSelection();
                }
                else if (result >= 1000)
                    insertNodeOnCable (cable, static_cast<NodeKind> (result - 1000), position);
            });
            return;
        }
        showAddNodeMenu (event.position);
        return;
    }
    if (event.mods.isMiddleButtonDown() || (event.mods.isAltDown() && event.mods.isLeftButtonDown()))
    {
        if (auto* viewport = findParentComponentOfClass<juce::Viewport>())
        {
            panning = true;
            panMouseDown = event.getScreenPosition();
            panViewStart = viewport->getViewPosition();
            setMouseCursor (juce::MouseCursor::DraggingHandCursor);
            return;
        }
    }
    if (const auto connection = cableNear (event.position))
    {
        selectedConnection = connection;
        selectNode (0);
        selectedConnection = connection;
        if (onCableSelectionChanged)
            onCableSelectionChanged (connection);
        repaint();
        if (onStatus)
            onStatus ("Cable selected - press Delete to remove");
        return;
    }
    selectNode (0);
}

void PatchCanvas::mouseDrag (const juce::MouseEvent& event)
{
    if (! panning)
        return;
    if (auto* viewport = findParentComponentOfClass<juce::Viewport>())
    {
        const auto delta = event.getScreenPosition() - panMouseDown;
        viewport->setViewPosition (panViewStart.x - delta.x, panViewStart.y - delta.y);
    }
}

void PatchCanvas::mouseUp (const juce::MouseEvent&)
{
    panning = false;
    setMouseCursor (juce::MouseCursor::NormalCursor);
}

bool PatchCanvas::keyPressed (const juce::KeyPress& key)
{
    if (key == juce::KeyPress::deleteKey || key == juce::KeyPress::backspaceKey)
    {
        deleteSelection();
        return true;
    }
    return false;
}
} // namespace signalpatch::ui
