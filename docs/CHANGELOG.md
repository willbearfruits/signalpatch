# Changelog

Newest first. Versions are git tags; "Unreleased" is what `main` carries
beyond the last tag.

## Unreleased

- **Touch mode** for handhelds (auto on a small high-DPI screen, `--touch` /
  `--no-touch`, or the FILE menu): finger-sized hit targets for ports,
  knobs, stomps and buttons; **tap an output then an input to connect**
  (the armed output draws a cable to your finger); **long press** opens the
  context menu that a right click would; double tap on empty canvas fits;
  on-screen zoom / fit buttons; taller menu and file-browser rows; the
  on-screen keyboard is tappable; the FILE menu carries UI scale and fit so
  a tablet never needs Ctrl.

## v0.3.1 — 2026-09-15

The hardening release: a seven-dimension audit of everything added since
v0.2.0 (details in `docs/PRODUCTION_READINESS.md`, pass 3), all findings
fixed.

- Recordings always reach the saved file (overdubs stopped with PLAY,
  4-track takes, autosave after recording); crash on quit fixed; New/Open
  ask about unsaved changes; undoing a delete restores MIDI bindings and
  groups; slot glides are one undo step.
- MIDI: velocity-0 note-ons release notes; commands fire on every press;
  learned bindings keep their channel; replugged controllers reconnect.
- Looper undo no longer stalls the audio; overdub is exact at half speed,
  200 % and reverse; 4-track no longer doubles the input while recording
  or stutters with SYNC.
- Stereo pedals keep both sides when bypassed; recordings survive a sample
  rate change; Clock start/reset realigns drums and sequencer; pluck in
  tune; tuner handles 96 kHz.
- Neural Amp tone stack (Gain, Bass, Mid, Treble, Presence, Master) and
  Neural Pedal Tone; 4-track overhaul with reels; TONE3000 filters,
  photos, audition and cabinet impulses; synth/pluck Pitch input.

ASan/UBSan and TSan clean over 46 tests.

## v0.3.0 — 2026-09-15

The revert point before the neural-module dials and the 4-track overhaul.
Everything below landed between v0.2.1 and this tag, plus: TONE3000
inside the app (log in once, search, download into a Neural module —
Ctrl+T), a Clock node, node colours by family, `run-from-build.sh` for a
desktop entry, and libcurl for https on Linux.

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
- Clock node (tempo, tap, run/reset; Beat / Eighth / Bar pulses). The drum
  machine and the step sequencer step on a Clock cabled into their new
  Clock input; the looper's REC/PLAY wait for the next pulse while a clock
  runs, so loops start and close on the bar. Four seconds of silence hand
  control back to each node's own tempo.
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
