# SignalPatch architecture

This document records the production architecture and staged direction. The
current 0.1 implementation already uses `PatchDocument -> GraphCompiler ->
RenderPlan` publication, while worker compilation, MIDI/OSC event queues,
undo/redo, probe subscriptions, and latency compensation remain later gates.

SignalPatch is a C++20/JUCE desktop audio processor for Linux and Windows. JUCE owns device discovery, the application shell, and drawing; the patch engine is custom. In particular, the engine does not use a mutable `AudioProcessorGraph` in the device callback. A patch is compiled into an immutable execution snapshot and is exchanged only at an audio-block boundary.

The first release is a standalone application. The same engine should remain usable from a plug-in later, but plug-in hosting and plug-in builds are not prerequisites for the standalone low-latency path.

## Design goals

- Use every input and output actually enabled on the selected device. Six inputs is a normal configuration, not a compiled-in limit.
- Keep graph edits, painting, file I/O, device enumeration, model loading, and memory allocation out of the audio callback.
- Give effect, routing, source, sink, and control nodes the same patching model while keeping audio, control values, and events type-safe.
- Make every continuously adjustable effect parameter addressable by a stable ID and optionally controllable from another node or an external controller.
- Permit feedback only through an explicit, delayed, level-guarded route.
- Feed useful, bounded-rate telemetry to the UI so cables and nodes can show what the engine is doing without making the UI part of the signal path.

"Minimal latency" here means no avoidable buffering beyond the selected device block and an effect's documented algorithmic delay. It does not mean zero latency: converters, the operating-system driver, the device block, and some algorithms necessarily add delay.

## Major components

```text
JUCE message thread
  PatchDocument -> GraphCompiler -> publication queue
        |                |
        |                +-> immutable CompiledGraph + prepared NodeState
        +-> editor/view models

device/control threads             audio callback
  MIDI, OSC, device changes  --->  Engine::processBlock()
       bounded queues               |  acquire snapshot at block boundary
                                    |  apply timestamped control events
                                    |  run precomputed node schedule
                                    +-> bounded telemetry queues

JUCE message thread <----------------------+
  decimate, retain recent history, paint
```

The dependency direction is deliberate. UI classes may depend on the patch model and telemetry schema. DSP code must not depend on UI objects. The callback sees prepared runtime objects and fixed-capacity queues, never the editable document.

## Editable patch and compiled graph

`PatchDocument` is the user-facing, serializable description. It contains node IDs, node type IDs and versions, parameter base values, port-to-port connections, controller assignments, and editor layout. It is mutated on the message thread and is designed to support undo/redo; the 0.1 slice does not expose undo/redo yet.

Every node, port, connection, and exposed parameter has a persistent ID. Display names are not identities. Persist device endpoints as a physical channel index plus a remembered label; a renamed channel must not silently reconnect to a different index.

`GraphCompiler` consumes a complete document copy and the current `DeviceLayout`. It performs the following work away from the callback:

1. Resolve node factories and migrate node state to the current type version.
2. Materialize device ports from the active input and output channel masks.
3. Validate port types, channel widths, parameter targets, and required connections.
4. Reject illegal cycles and compile explicit feedback routes.
5. Topologically order the remaining graph.
6. Determine buffer lifetimes and assign reusable scratch buffers.
7. Prepare every DSP object for sample rate, maximum block size, and channel layout.
8. Build control/event routing tables and a parameter update plan.
9. Build only the telemetry probes requested by the current view/subscriptions.

Compilation either returns a complete `CompiledGraph` or a list of diagnostics tied to persistent IDs. A failed compile leaves the currently sounding snapshot untouched.

`CompiledGraph` is the architectural name for the immutable result; the current code calls that object `RenderPlan`. It owns the execution schedule, prepared runtime node state, buffer plan, constant lookup tables, and probe plan. The audio thread must not follow maps, strings, or editor objects during processing; compilation reduces those relationships to compact indices and contiguous arrays.

### Snapshot publication and reclamation

The control side publishes a pointer/handle to a fully prepared snapshot through a single-producer/single-consumer handoff. The audio callback adopts it only at the start of a block. Publication uses release/acquire ordering.

Do not let the callback destroy the previous snapshot. It places the retired handle in a fixed-capacity audio-to-control retirement queue; the control thread performs destruction later. The publisher must keep ownership until adoption is acknowledged, and queue-full behavior must retain/defer a snapshot on the non-real-time side rather than delete it in the callback. A plain last-owner `shared_ptr` assignment is not sufficient because its destructor can run on the audio thread.

Parameter gestures that do not alter topology do not require recompilation. They are timestamped control updates sent through a bounded queue. Structural edits, channel-layout changes, and node implementations that need new prepared state produce a new snapshot.

## Ports and signal types

Connections are legal only between compatible typed ports:

| Type | Payload | Typical use |
| --- | --- | --- |
| `Audio` | one or more sample channels | device audio, effects, mixers |
| `Control` | scalar control stream with declared rate/domain | LFO, envelope, macro, parameter modulation |
| `Trigger` | timestamped edge/event | sequencer gate, reset, one-shot |
| `Note` | timestamped note plus expression data | MIDI input, sequencer, future instruments |

Audio ports declare a channel-width policy: fixed mono/stereo, match-upstream, or device-derived. No implicit mono/stereo conversion is allowed unless the connection owns a visible conversion policy. This prevents a patch from changing sound merely because a device was replaced.

Control ports carry metadata as well as values: unit/domain, nominal range, rate capability, and whether interpolation is meaningful. Boolean, choice, and trigger-like destinations are not treated as arbitrary floating-point audio. The compiler reports a type error or inserts an explicitly represented adapter chosen by the user.

## Parameters and modulation

Every externally controllable parameter has a stable `ParameterID` and descriptor containing:

- display name, unit, physical range, default, and normalization curve;
- update class (`continuous`, `stepped`, `boolean`, or `action`);
- accepted modulation rate (`block`, `control`, or `audio`);
- smoothing policy and safe bounds;
- whether changing it requires asynchronous preparation rather than real-time modulation.

The saved base value and zero or more modulation routes are separate. Each route records source, depth, polarity, transfer curve, and combination mode. A compiled parameter plan combines them in normalized space, clamps to the descriptor's safe range, maps to physical units, and applies the declared smoothing. This makes an on-screen knob, MIDI CC, envelope follower, LFO, macro, and sequencer lane use the same target contract.

Audio-rate modulation is opt-in per destination because it changes CPU and buffer planning. Block/control-rate sources are interpolated when the destination declares continuous interpolation. Stepped and boolean destinations change only at an event offset and never pass through a generic smoother.

External input (initially MIDI; OSC can be added on a dedicated I/O thread) is converted to timestamped engine events before reaching the callback. Parsing, socket access, MIDI learn, and mapping edits remain off the callback. Queue overflow increments telemetry and drops/coalesces according to the event type; it must never block audio.

## Dynamic audio devices

JUCE's `AudioDeviceManager` is the source of truth for sample rate, block size, active channel masks, and channel names. On device open or layout change, SignalPatch constructs a `DeviceLayout` containing only enabled physical channels. Device input/output nodes then expose ports for those channels. There is no constant such as `kInputCount = 6` in the graph or DSP layer.

The default patch may create one input strip per enabled input and one output strip per enabled output. Users can group adjacent channels explicitly (for example, inputs 1-2 as stereo). Missing saved channels appear disconnected with a diagnostic; SignalPatch must not silently shift input 6 to input 5.

A device change is a non-real-time transition:

1. Stop or suspend the callback through JUCE's device lifecycle.
2. Read the new rate, maximum block size, and active masks.
3. Compile and prepare a compatible graph snapshot.
4. Publish it, clear stale timing state, and resume with a short fade.

If the selected layout cannot satisfy the patch, keep the document intact, disconnect unavailable endpoints, and show the mismatch. Do not allocate substitute channel buffers from inside the first callback.

### Backend guidance

Expose the backend and device choice rather than guessing silently.

- Windows: prefer a manufacturer's ASIO driver for consistently small buffers. Offer JUCE's WASAPI device types as the broadly available fallback. Do not route through two backends at once or add an internal safety buffer to mask an unstable driver.
- Linux: support JACK and ALSA device types built into the JUCE configuration. JACK is the preferred graph-friendly low-latency route and also works with PipeWire's JACK compatibility when installed/configured by the user; direct ALSA is a valid dedicated-device path.
- On both platforms: show the requested and actual sample rate/block size, round-trip latency reported by the driver, and xrun/dropout count. A setting the driver rejects is not active merely because it remains selected in the UI.

Start with 64 or 128 samples as practical low-latency choices and allow 32 where the driver and patch can sustain it. Avoid sample-rate conversion in the normal device path. CPU headroom and glitch-free operation are more useful than reporting the smallest selectable number.

## Execution model

At each callback, the engine:

1. Checks once for a prepared snapshot and adopts it at the block boundary.
2. Binds JUCE device buffers to compiled device endpoints, clearing absent/unused outputs.
3. Drains the bounded event queue up to this block and preserves sample offsets.
4. Runs the compiled control/event plan and node schedule.
5. Runs explicit feedback delay/guard processing in the compiled order.
6. Emits bounded telemetry records and retires superseded state without destroying it.

Nodes process caller-owned spans/views over preallocated buffers. In-place processing is allowed only where the compiler's lifetime analysis proves it safe. The engine supports callbacks smaller than the prepared maximum and never assumes the device always returns the nominal block size.

Bypass behavior is part of each node descriptor: hard bypass for zero-state routing where safe, or a short crossfade/latency-compensated bypass where discontinuity would click. Nodes that introduce algorithmic latency report it during preparation. Initial standalone routing need not globally compensate arbitrary parallel paths, but latency must be visible and the graph contract must leave room for compiler-inserted compensation later.

## Feedback

A normal audio connection may not complete a directed cycle. Cycles are legal only through an explicit `Feedback` node/edge that provides memory and a guard stage. The minimum delay is one sample in a sample-wise implementation or one engine block in the initial block-wise implementation; the UI displays the actual value. A zero-delay algebraic loop is rejected.

The feedback route contains, in order, a user gain, a DC blocker, and a safety guard with a hard finite ceiling. The initial guard should be a compressor feeding a final lookahead-free soft/hard limiter or saturating clip stage. User parameters may make feedback musically aggressive, but they may not bypass the final finite-value and ceiling protection. Every stage must handle NaN/Inf by resetting its local state and outputting a bounded sample.

The compiler accounts for all feedback storage. The callback neither grows delay lines nor reconstructs the cycle. Feedback level reduction, limiter activity, and non-finite resets are exposed as telemetry. Safety limiting reduces catastrophic levels; it does not make high monitor gain or acoustic feedback safe, so output starts muted after device changes and ramps up deliberately.

## Telemetry and visualization

Visualization is observational. The audio callback writes small records to fixed-capacity SPSC queues or preallocated rings; it never calls a component, takes a paint lock, formats text, or waits for the UI. The message thread drains records on a timer, keeps a bounded history, and repaints at a rate independent of the device callback.

Telemetry is subscription-based and has explicit budgets:

- Cables: decimated peak, RMS, polarity/zero-crossing activity, and optional envelope. Animation speed comes from the retained envelope, not per-sample UI messages.
- Distortion and waveshaping nodes: decimated pre/post waveform windows and gain-reduction/transfer indicators.
- Sequencers: playhead step, gate state, and transport position as timestamped state, so the UI can interpolate between updates.
- Dynamics and feedback: input/output level, gain reduction, ceiling hits, and guard resets.
- Engine/device: callback load, maximum callback time, queue overflows, xruns/dropouts when available, sample rate, block size, and graph generation.

Probe points are compiled indices. Enabling a heavy scope subscription may require a new probe plan; disabling it removes that DSP work. Waveform rings contain decimated or bounded windows, never unbounded recording. Overflow drops the oldest visual information or the newest low-priority record according to the queue contract and increments a counter; audio continues.

## Node SDK contract

Each built-in node provides a descriptor/factory usable without constructing UI:

- stable type ID and state schema version;
- typed port and parameter descriptors;
- a non-real-time `prepare(spec, state)` step;
- a real-time `process(context)` step;
- reported algorithmic latency and scratch/state requirements;
- bounded telemetry capabilities;
- state migration and serialization hooks.

`process` receives only prepared state, buffer views, compiled parameter views, and event spans for the current block. A node that needs a file, FFT plan, impulse response, neural model, resized delay, or lookup table constructs it on a worker/control thread and publishes prepared state through the same snapshot mechanism. Third-party binary nodes are out of scope until this contract and its real-time test harness are stable.

## Persistence and compatibility

Patch files include an application schema version, node type versions, persistent IDs, parameter values in canonical physical/normalized form, modulation routes, endpoint indices/labels, and editor layout. Loading happens into a temporary document, runs deterministic migrations, validates, and only then replaces the current document. Unknown nodes are retained as disabled placeholders with their raw state so a patch can survive an optional component being absent.

Device preferences are stored separately from the creative patch. A patch can remember intended endpoint indices, but opening it must not silently reconfigure the operating system's audio device.

## Initial implementation boundary

The first useful slice should include dynamic device input/output nodes; gain, mixer, delay, distortion, compressor/limiter, feedback, LFO, envelope follower, macro, and step-sequencer nodes; typed patching; save/load; cable meters; node-specific scopes; and MIDI mapping. NAM is an optional node developed behind a build/runtime capability boundary as described in `NAM_ROADMAP.md`.

Features intentionally deferred include hosting arbitrary plug-ins, distributed/network audio, arbitrary scripting in the callback, and sample-accurate global latency compensation. The architecture leaves room for them without putting them on the critical path to a reliable processor.
