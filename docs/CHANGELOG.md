# Changelog

Newest first. Versions are git tags; "Unreleased" is what `main` carries
beyond the last tag.

## Unreleased (since v0.2.1, 2026-09-15)

**The app is the GLFW/OpenGL/NanoVG one now.** `signalpatch` builds the
executable `SignalPatch`; the JUCE rack is kept behind
`-DSIGNALPATCH_BUILD_JUCE_UI=ON` (tag `v0.2.1`, branch `juce-ui`) as the
place to go back to.

Engine
- Looper node: record, close, overdub, undo a pass, half speed, reverse;
  undo stays real-time safe.
- Recordings (looper, 4-track, sampler) are saved with the patch as
  `assets/audio/*.wav` and ride along in portable project zips and autosave.
- MIDI: every input opened (hot-plug), mappings saved in the patch (CC /
  note / program change to knob, stomp, button, slot, group), MIDI learn,
  relative encoders (CC 64±n), expression pedal calibration (heel/toe span,
  inverted when swapped), MIDI Note node (keyboard notes reach the synth and
  pluck on the audio thread: Gate / Pitch / Velocity outputs).
- Foot controller feedback over SysEx (`docs/CONTROLLER.md`): hello
  handshake, switch LEDs and labels, live slot, rig name; diffed, verified
  through ALSA's Midi Through.
- Stereo stage 1: Pan, Stereo Merge, Stereo Delay (ping-pong), Stereo
  Chorus, Stereo Reverb, a right output on the Cabinet (IR B); one drag
  cables an L/R pair.
- Tuner node (YIN on the message thread); drum machine tap tempo.
- Undo: compound gestures (a multi-module drag or delete is one step).
- HUD warns **NO RT PRIORITY** when the callback thread is not
  SCHED_FIFO/RR (install rtkit or realtime-privileges).

UI
- Board: flow layout, movable pedals, pedal groups (one stomp, up to four
  exposed knobs), five rig slots with knob glides, transport buttons on the
  pedals, MIDI labels everywhere a binding exists.
- Rack: cached face plates, in-canvas menus / prompts / file browser, module
  palette (P), multi-selection (Shift+click, rubber band, Ctrl+A), group
  drag and delete, Menu key / Shift+F10 context menus.
- Menus walk by keyboard; a gamepad drives the Board (d-pad, A/B, stick,
  LB/RB slots, RT/LT pedal buttons, stick clicks for menus, Guide for FILE)
  and gets an on-screen keyboard in prompts.
- Global UI scale (Ctrl +/-/0, `--scale=`, `SIGNALPATCH_SCALE`).
- `--board`, `--kiosk`, `--unmute`, `--help`.
- OpenGL through a vendored glad loader, GLFW fetched when the system has
  none: the app builds on Windows too (CI uploads SignalPatch.exe and the
  Linux binary as artifacts).

Tests: 34 headless engine tests including the allocation trap over the
worst-case graph with MIDI notes dispatched; ASan/UBSan and TSan clean;
30-minute soak re-run.

## v0.2.1 — 2026-09-15

Last tree with both UIs built by default; tagged before the JUCE rack was
retired behind an option.

## v0.2.0 — 2026-09-14

Undo/redo, Cabinet impulse-response node, File menu with recent files and
portable project bundles, context menus everywhere, a much lighter rack UI.

## v0.1.0 — 2026-07-11

First public release: 35 modules, Neural Amp Modeler support, rack UI with
stomp bypass and live cables, guarded feedback, real-time safety gates.
