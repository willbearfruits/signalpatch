# SignalPatch Product Specification

This is the product target, not a claim that every item already ships. The
working 0.1 slice and its explicit boundaries are listed in `../README.md`.

## Product mode

SignalPatch is desktop software for building and performing low-latency modular audio-processing graphs on Linux and Windows. It is a professional creative tool first, with live visualization supporting comprehension rather than spectacle.

The interface should retain familiar node-editor, mixer, meter, and patch-cable conventions. Its distinctive character comes from signal-aware cables, process-specific node views, and a calm laboratory-instrument aesthetic. It must not behave like a marketing site or let decorative motion compete with audio processing.

The primary user need is to build, hear, understand, and safely perform an audio graph without stopping playback.

### Product principles

- The audio device determines the available input and output count; six inputs are a useful case, never a hard-coded limit.
- Live sound has priority over UI rendering, animation, analysis, and file operations.
- Every automatable effect parameter can be exposed to internal or external control.
- Patching remains immediate, reversible, click-free, and understandable while audio is running.
- Feedback is supported as a deliberate creative technique, but never as an unguarded connection.
- Latency is reported honestly in samples and milliseconds. SignalPatch does not claim zero latency.

## Core task loop

1. Select an audio device, sample rate, buffer size, and channel mapping.
2. Add device input, processing, control, routing, and device output nodes.
3. Connect ports and hear the graph immediately.
4. Adjust parameters or expose them to modulation and external control.
5. Inspect signal level, waveform, clipping, latency, and processing load.
6. Save the patch, perform with it, or recover safely through undo and autosave.

A new user should be able to route an input through one effect to an output in under one minute.

## Workspace layout

### Top status strip

Always visible:

- Audio engine on/off state.
- Active device, sample rate, and buffer size.
- Driver and graph latency.
- DSP/CPU load and xrun count.
- Patch save state.
- A prominent `PANIC / MUTE` control.

Latency-heavy nodes and settings must remain visible in the graph and contribute to the displayed graph latency. A live mode may warn about or prevent configurations that exceed a chosen latency budget.

### Node palette

A searchable left palette groups nodes by:

- Device I/O.
- Routing and mixing.
- Effects and dynamics.
- Control and modulation.
- Sequencing and timing.
- Analysis.

Pressing `Tab` opens the same search at the pointer for fast insertion.

### Patch canvas

The central canvas supports pan, zoom, multi-select, grouping, alignment, copy/paste, duplication, and optional minimap navigation. Input ports appear on the left of nodes and output ports on the right.

Core patch interactions:

- Drag between compatible ports to connect them; incompatible targets remain unavailable and explain why.
- Drop a node onto a cable to insert it.
- Drag a cable endpoint to reconnect it.
- Select a cable to inspect channel count, gain, level, and latency.
- Undo and redo all graph edits.
- Compile edits away from the audio thread, then swap to the valid graph without interrupting sound.

### Inspector

The right inspector shows exact values and advanced settings for the selected node, parameter, or cable. Parameter inspection includes units, default value, modulation range, polarity, curve, smoothing, and external mapping.

Every automatable parameter has a base value and an optional control input. A visible plug action exposes that input without filling every node with unused ports. Exposed controls remain visible when a node is collapsed.

### Diagnostics dock

A collapsible lower dock contains expanded oscilloscope and spectrum views, the device channel matrix, engine messages, xruns, and performance history. Closing the dock must not remove essential error status from the top strip or affected node.

## Node anatomy and controls

Every node contains:

- A header with name, bypass, and activity or error state.
- Typed input and output ports.
- Essential inline controls with numeric values and units.
- A process-specific live preview.
- Expand and collapse controls.
- Optional latency and CPU badges in diagnostics mode.

Parameter changes use smoothing where needed to prevent zipper noise. Connect, disconnect, bypass, and preset changes use short ramps or crossfades to avoid clicks. Fine adjustment, typed numeric entry, and reset-to-default must be available without relying on a precision pointer gesture.

## Signal visualization conventions

Signal type is communicated by shape and line style as well as color:

| Signal | Port | Cable |
| --- | --- | --- |
| Audio | Circle | Solid |
| Control/modulation | Diamond | Dashed |
| Clock/event | Square | Dotted |
| Guarded feedback | Audio port with guard badge | Amber double line |

Audio cable brightness follows RMS level. A restrained moving highlight indicates direction, and a brief peak marker indicates clipping. Selecting a cable reveals numeric RMS and peak values. Reduced-motion mode replaces moving highlights with static port meters.

Node previews explain the process rather than using generic animation:

- Distortion: input/output waveform overlay and transfer curve.
- Sequencer: step grid, values, and active playhead.
- Compressor or limiter: input/output levels and gain reduction.
- EQ or filter: response curve over a live spectrum.
- Delay: tap positions and decaying level bars.
- LFO: waveform and current phase.
- Mixer and I/O: compact channel meters.

Visualization is sampled from the engine through bounded, non-blocking data paths. Its refresh rate must fall before audio performance is affected.

## Feedback safety UX

A connection that creates a cycle cannot become active without a Feedback Guard. The canvas identifies the cycle and offers to insert the guard inline, without opening a disruptive modal dialog.

The Feedback Guard provides:

- A minimum causal delay.
- DC blocking.
- Loop gain control.
- Compression or soft limiting.
- An output ceiling.
- A configurable emergency kill threshold.

Feedback connections use a distinct cable treatment and guard badge. Dangerous gain, sustained limiting, and clipping appear at the affected node, port, and cable. Repeated threshold breaches latch the feedback path off and explain the condition locally.

The persistent panic action immediately mutes or ramp-kills all active feedback paths and device outputs. It must have a discoverable keyboard shortcut.

Other recovery states:

- Invalid edits remain marked while the last valid graph continues running.
- Device loss ramps to silence, preserves the patch, and presents channel remapping.
- Patch restore keeps outputs muted until the graph is valid, then fades in.
- DSP overload reduces analysis and animation first; persistent overload identifies expensive nodes and may bypass an opted-in safety target.

## Accessibility

- Never communicate port type, signal state, clipping, or errors by color alone.
- Provide high-contrast and reduced-motion modes.
- Support adjustable UI scale, cable thickness, and large connection targets.
- Show numeric values with units; a knob angle is supplementary, never the only value display.
- Provide clear keyboard focus and keyboard workflows for adding, selecting, moving, connecting, and deleting nodes.
- Provide a structured node-and-connection list as an accessible alternative to the spatial canvas.
- Use stable, descriptive labels suitable for assistive technology.
- Keep essential status readable when live previews are frozen or disabled.

## Signature aesthetic

SignalPatch should feel like a calm laboratory instrument: a deep graphite workspace, warm light labels, restrained signal colors, crisp technical typography, and tabular numeric readouts. Nodes are differentiated through hierarchy and their live data, not decorative glass, gradients, or excessive skeuomorphism. Signal glow appears only where actual signal energy exists; the visualization is the decoration.

## MVP scope

The first release includes:

- Dynamic enumeration and naming of all channels exposed by the selected device.
- Audio device, sample rate, buffer, and channel mapping controls for Linux and Windows.
- Low-latency graph operation with visible xruns and latency.
- Device input/output, gain, pan, mixer, split, merge, EQ/filter, compressor, limiter, distortion, delay, reverb, and Feedback Guard nodes.
- Macro, LFO, envelope follower, step sequencer, MIDI note/CC input, and parameter exposure.
- Signal-aware cables and process-specific node previews.
- Undo/redo, autosave recovery, patch save/load, bypass, and panic.
- Safe handling of invalid edits, device loss, clipping, feedback, and overload.

MVP acceptance criteria:

- No device channel count is hard-coded.
- No feedback cycle can run unguarded.
- UI and visualization work never block the audio callback.
- Common graph edits do not click or interrupt valid running audio.
- Every included effect parameter can be exposed for control.
- Signal type and error state remain understandable without color or animation.

## Non-goals for the MVP

- Neural amplifier model hosting.
- Third-party plug-in hosting.
- A full DAW timeline, multitrack recording, or clip launcher.
- Network collaboration or cloud patch storage.
- Sample-accurate offline rendering.
- Decorative visualization that consumes audio-thread time or hides operational status.

Neural amplification is a planned optional phase. The core processor interface should leave room for it, but the feature ships only with model loading and warm-up outside the audio thread, fixed-block real-time inference, explicit added-latency reporting, CPU headroom monitoring, and safe overload bypass. It must not delay or destabilize the core patching MVP.
