#include "PatchCanvas.h"

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
    syncNodeComponents();
    repaint();
}

void PatchCanvas::timerCallback()
{
    animationPhase += 0.013f;
    if (animationPhase >= 1.0f)
        animationPhase -= 1.0f;
    repaint();
    for (auto& component : nodeComponents)
        component->repaint();
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

void PatchCanvas::drawCable (juce::Graphics& graphics, const Connection& connection, bool isSelected)
{
    const auto* sourceComponent = componentForNode (connection.sourceNode);
    const auto* destinationComponent = componentForNode (connection.destinationNode);
    const auto* sourceModel = engine.getDocument().findNode (connection.sourceNode);
    const auto* destinationModel = engine.getDocument().findNode (connection.destinationNode);
    if (sourceComponent == nullptr || destinationComponent == nullptr
        || sourceModel == nullptr || destinationModel == nullptr)
        return;

    const auto start = sourceComponent->outputPortCentreInParent (connection.sourcePort);
    const auto end = destinationComponent->inputPortCentreInParent (connection.destinationPort);
    const auto path = cablePath (start, end);
    const auto type = sourceModel->processor->getOutputPort (connection.sourcePort).type;
    const bool feedback = sourceModel->processor->isFeedbackGuard()
                       || destinationModel->processor->isFeedbackGuard();
    // Rack wiring: every cable wears its source module's colour. Signal type
    // stays readable through the stroke style (control cables are dashed).
    const auto colour = feedback ? colours::feedback
                                 : kindAccent (sourceModel->processor->getKind());
    const auto rms = sourceModel->processor->outputRms (connection.sourcePort);
    const auto energy = juce::jlimit (0.0f, 1.0f, std::sqrt (juce::jmax (0.0f, rms)));
    const auto width = (isSelected ? 3.4f : 2.0f) + energy * 2.6f;
    const auto rounded = [] (float strokeWidth)
    {
        return juce::PathStrokeType (strokeWidth, juce::PathStrokeType::curved,
                                     juce::PathStrokeType::rounded);
    };

    // Drop shadow, then a signal-level glow halo, then the cable core.
    graphics.setColour (juce::Colours::black.withAlpha (0.5f));
    graphics.strokePath (path, rounded (width + 3.5f), juce::AffineTransform::translation (0.0f, 2.0f));

    graphics.setColour (colour.withAlpha (0.06f + energy * 0.30f));
    graphics.strokePath (path, rounded (width + 7.0f));
    graphics.setColour (colour.withAlpha (0.12f + energy * 0.30f));
    graphics.strokePath (path, rounded (width + 3.0f));

    if (isSelected)
    {
        graphics.setColour (colours::selection.withAlpha (0.35f));
        graphics.strokePath (path, rounded (width + 5.0f));
    }

    if (type == SignalType::control)
    {
        juce::Path dashed;
        const float dashes[] { 7.0f, 5.0f };
        rounded (width).createDashedStroke (dashed, path, dashes, 2);
        graphics.setColour ((isSelected ? colours::selection : colour).withAlpha (0.6f + energy * 0.4f));
        graphics.fillPath (dashed);
    }
    else
    {
        graphics.setGradientFill (juce::ColourGradient (
            (isSelected ? colours::selection : colour).brighter (0.15f), start,
            (isSelected ? colours::selection : colour).darker (0.1f), end, false));
        graphics.strokePath (path, rounded (width));
    }

    // Directional comets: a bright head with a fading tail marching from the
    // source to the destination shows which way the signal flows.
    const auto length = path.getLength();
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
                const auto pointA = path.getPointAlongPath (length * juce::jmax (0.0f, p0));
                const auto pointB = path.getPointAlongPath (length * p1);
                const auto fade = static_cast<float> (tailSegments - segment) / tailSegments;
                graphics.setColour (colour.brighter (0.7f)
                                        .withAlpha ((0.28f + 0.5f * energy) * fade));
                graphics.drawLine ({ pointA, pointB }, (1.2f + energy * 2.2f) * fade + 0.6f);
            }
            const auto headPoint = path.getPointAlongPath (length * head);
            graphics.setColour (colour.brighter (0.9f).withAlpha (0.55f + 0.45f * energy));
            graphics.fillEllipse (headPoint.x - 2.0f, headPoint.y - 2.0f, 4.0f, 4.0f);
        }
    }

    if (sourceModel->processor->outputPeak (connection.sourcePort) >= 0.98f)
    {
        const auto pulse = 0.5f + 0.5f * std::sin (animationPhase * juce::MathConstants<float>::twoPi * 3.0f);
        const auto point = path.getPointAlongPath (length * 0.84f);
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

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this),
                        [this, position] (int result)
    {
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
            juce::PopupMenu menu;
            menu.addItem (1, "Disconnect cable");
            menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this),
                                [this] (int result)
            {
                if (result == 1)
                    deleteSelection();
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
