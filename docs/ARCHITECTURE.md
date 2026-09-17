# Architecture

Two parts: a headless audio engine in `src/audio/` (JUCE 8, no GUI modules)
and the app in `src/v2/` (GLFW, OpenGL 3.3, NanoVG). The engine has no
knowledge of the app; the tests link the engine alone.

## Document and render plan

`PatchDocument` is the editable patch: nodes, cables, pedal groups and MIDI
mappings. It is only touched on the message thread and it is what gets
saved.

`GraphCompiler` turns a document into a `RenderPlan`: nodes in execution
order, their buffers, and for each input the outputs that feed it. A plan
never changes after it is built. If a compile fails (an unguarded cycle, a
type mismatch) the plan that is sounding stays, and `PatchEngine::connect`
takes the rejected cable back out of the document.

`PatchEngine` publishes a new plan through an atomic pointer. The audio
callback picks it up at the start of a block with a short fade, and pushes
the old plan onto a retired list. The engine's timer deletes retired plans
on the message thread, so nothing is freed in the callback.

Knob changes do not recompile. A parameter is an atomic value that the node
reads and smooths itself.

## Nodes

A `DspNode` has typed ports (audio or control), parameters, and a
`processDsp` that runs in the callback. Every parameter that can be
modulated gets a control input; the modulation is added to the knob's
travel before it is mapped to a value. The inspector's range and curve
(`ParameterShape`) are part of that mapping, so the mouse, MIDI and
modulation all stay inside the range.

Other hooks, all on the message thread: extra state (model and impulse
paths, script text), transport commands passed to the callback through
atomics, and recorded audio (import, export and a version counter that
tells the autosave when a WAV is stale). MIDI notes reach nodes in the
callback through a lock-free FIFO.

Stereo is pairs of mono ports. The rack cables the right side in the same
drag.

Saved patches store parameter values and cable ports by position, so new
parameters and ports are always added at the end of a node.

## Feedback

A cycle is legal only through a Feedback Guard: a delay, a DC blocker, a
finite check and a ceiling that latches off when the loop runs away. The
compiler rejects any other cycle. The hardware outputs have a fixed
ceiling, and loaded patches start muted.

## Devices

The Hardware Inputs and Outputs modules get one port per channel the device
has. Nothing assumes a channel count. A saved channel the current device
lacks stays as a placeholder while a cable uses it, and is never remapped to
a different channel. Under PipeWire's JACK layer an interface's output
monitors are listed among its capture ports; those are left off.

On first launch a Zoom F or H series interface is preferred, JACK before
ALSA. After that the saved device wins.

## Neural amps

NeuralAmpModelerCore is pinned and linked whole-archive, because its
architectures register themselves from static initialisers. A model is
parsed and warmed up on a worker thread and swapped into the node
atomically; the previous model is kept until the next swap, so it is never
freed in the callback. `SIGNALPATCH_ENABLE_NAM=OFF` builds without it.

## Undo and files

`PatchHistory` records an edit after it has happened. Drags coalesce into
one step and a compound gesture groups several edits (a multi-module drag,
a slot change). Removing a node keeps the node object, so undo brings back
the same loaded model and recorded tape.

A patch is JSON. Recordings are WAV files beside it, written to a `.part`
file and then moved into place. A bundle is a zip of the patch folder with
models and impulses, with paths stored relative to it.

## The app

`Main.cpp` owns the window and the frame loop, and pumps JUCE's message
loop from it, so engine timers and async loads run without a JUCE GUI. A
frame is drawn only when input, telemetry or an animation changed.

`RackView` draws everything. Each module's face plate is cached in a
framebuffer and redrawn only when the module changes; meters, scopes, cable
glow and modulated knobs are drawn over it each frame. The Board is a second
view of the same document. Menus, prompts, the file browser, the TONE3000
browser and the inspector are drawn in the same canvas.

Opening a patch or loading a different slot replaces every node. Anything
that captured a node id checks a patch epoch before using it.
