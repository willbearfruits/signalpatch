# Changelog

Newest first. "Unreleased" is what `main` has beyond the last tag.

## Unreleased

- Inspector (`I`, or from a module's or knob's menu): a panel on the right
  listing the selected module's knobs with the exact value, the minimum and
  maximum the knob travels between, a curve, and the mod depth. Drag a
  field, click to type, right-click to reset. Minimum above maximum reverses
  the knob. Saved with the patch and the rig slots. MIDI, expression pedals
  and modulation stay inside the range.
- A knob with a cable in its mod input moves on screen, with a ring for how
  far the modulation can push it and a notch where it is set.
- Hardware Inputs matches the interface. PipeWire lists an interface's
  output monitors among its capture ports, so a 6-input Zoom showed ten
  inputs; those are now left out. Picking a device enables all of its
  channels instead of two. A saved channel the device lacks is kept only
  while a cable uses it.
- The session comes back as it was: rack or Board, camera, slot, file name,
  unsaved mark, window size. An edit made in the last second before quitting
  is no longer lost. FILE > Start muted can be turned off.
- On Linux the UI settings moved from `~/SignalPatch/` to
  `~/.config/SignalPatch/`.

## v0.3.2 — 2026-09-16

- Touch mode for handhelds. On automatically for a small high-DPI screen,
  or `--touch` / `--no-touch`, or the FILE menu. Larger targets, tap an
  output and then an input to connect, hold for the context menu, double
  tap to fit, zoom and fit buttons on screen, a tappable keyboard in
  prompts. UI scale and fit are in the FILE menu.

## v0.3.1 — 2026-09-15

Bug fixes after reading through everything added since v0.2.0.

- Recordings always reach the saved file: overdubs stopped with PLAY,
  4-track takes, autosave after recording. WAVs are written to a temporary
  file first.
- Crash on quit fixed. New and Open ask about unsaved changes. Undoing a
  delete restores MIDI bindings and groups. A slot change is one undo step.
- MIDI: a note-on with velocity 0 is a release; commands fire on every
  press; bindings keep their channel; replugged controllers reconnect.
- Looper undo no longer stalls the audio. Overdub is exact at half speed,
  double speed and in reverse.
- 4-track: no doubled input while recording, no stutter with SYNC.
- Stereo modules keep both sides when bypassed. Recordings survive a sample
  rate change. Clock start and reset realign the drum machine and
  sequencer. The pluck is in tune. The tuner works at 96 kHz.
- New: a tone stack on the Neural Amp (gain, bass, mid, treble, presence,
  master) and a tone knob on the Neural Pedal; the 4-track rebuilt with
  per-track record, play, speed and sync, and reels that turn; TONE3000
  filters, photos, auditioning and cabinet impulses; a pitch input on the
  synth and pluck.

## v0.3.0 — 2026-09-15

The app is now the GLFW / OpenGL / NanoVG one. The JUCE rack is kept behind
`-DSIGNALPATCH_BUILD_JUCE_UI=ON` (tag `v0.2.1`, branch `juce-ui`).

Engine
- Looper: record, overdub, undo a pass, half speed, reverse.
- Recordings (looper, 4-track, sampler) are saved with the patch as WAV
  files and included in project zips and the autosave.
- MIDI: every input opened, hot-plug included. Learn on knobs,
  footswitches, buttons, slots and groups. Relative encoders, expression
  pedal calibration. A MIDI Note module for the synth and pluck.
- Foot controller feedback over SysEx (`CONTROLLER.md`).
- Stereo as left/right pairs: pan, merge, ping-pong delay, chorus, reverb,
  and a right output on the Cabinet.
- Tuner. Clock module with tap tempo; the drum machine, sequencer and looper
  follow it.
- TONE3000 inside the app (`Ctrl+T`).
- The header warns when the audio thread has no realtime priority.

App
- The Board: pedals laid out by signal flow, movable, groupable into one
  pedal with up to four knobs, five rig slots with gliding knobs.
- Rack: cached face plates, menus and file browser drawn in the canvas,
  module palette, multi-selection, module colours by family.
- Keyboard and gamepad reach every menu and prompt.
- UI scale (`Ctrl +/-`, `--scale=`), `--board`, `--kiosk`, `--unmute`.
- Builds on Windows; CI uploads both binaries.

## v0.2.1 — 2026-09-15

The last tree that built both UIs by default.

## v0.2.0 — 2026-09-14

Undo and redo, the Cabinet module, a File menu with recent files and
portable project zips, context menus, a much lighter rack UI.

## v0.1.0 — 2026-07-11

First public release: 35 modules, Neural Amp Modeler support, a rack with
bypass footswitches and live cables, guarded feedback.
