# SignalPatch roadmap

The single ordered plan for the project. Detail lives in the referenced docs:
`PRODUCT.md` (product spec), `ARCHITECTURE.md` (engine design),
`NAM_ROADMAP.md` (neural amp stages), `PRODUCTION_READINESS.md` (verification
gates). This file says what comes next and why.

## Where we are (0.2, shipped 2026-09-14/15)

Everything in 0.1 plus: undo/redo over every edit; a Cabinet node (two IRs,
blend, cuts, zero-latency convolution); File menu, recent files, portable
project bundles (patch + models + IRs as a zip); context menus everywhere; a
much lighter JUCE rack. And **SignalPatch 2** (`signalpatch2`): the same
engine under a GLFW + OpenGL + NanoVG UI that renders only what changed and
idles at nothing — rack view with cached face plates, in-canvas menus, file
browser and text prompts, an AUDIO device menu, and the **Board**: the patch
arranged by signal flow as pedals, movable, with pedal **groups** (one
footswitch and up to four knobs over several modules) and five **rig slots**
whose knob values glide when the modules and cables match.

Load-bearing gaps that most requests run into: the engine is **mono**, has
**no external control** (MIDI/gamepad), and **does not persist recordings**
(sampler/4-track audio). Two UIs exist; only one should.

## The destination

One instrument that runs wherever it is plugged in: the desktop (primary —
where rigs are built and most playing happens), and a handheld such as the
ROG Ally X for the stage, driven by a purpose-built foot controller and a
gamepad. The rule that follows is **scale, don't specialise**: one UI whose
layout, hit targets and density follow the window size and pixel density,
so a 27-inch desktop, a laptop and a 7-inch handheld all get the same app,
not three. Every phase below is ordered by how much closer it gets that.

Cross-cutting from 0.3 on:
- **UI scale**: a global scale factor (Ctrl+/- and a setting, seeded from
  the display's content scale), all sizes in logical units, pedals and
  menus sized for fingers at scale 1.25+ automatically.
- **Input parity**: everything reachable by pointer, touch, gamepad and MIDI.
- **Engine budget**: every node meets a 128-sample deadline on a laptop-class
  CPU (the Ally X's Z1 Extreme is the reference floor).

---

## 0.3 — One app, and a rig that keeps what you play

- **Retire the JUCE UI** (done 2026-09-15). `signalpatch2` is now the
  `signalpatch` target and the `SignalPatch` executable; the JUCE rack builds
  only with `-DSIGNALPATCH_BUILD_JUCE_UI=ON` (target `signalpatch_juce`) and
  `src/ui` is deleted at the 0.3 release. Ways back, in order of how much
  they undo: `cmake -B build -DSIGNALPATCH_BUILD_JUCE_UI=ON` (both apps on
  the current engine); `git checkout juce-ui` (the last both-UIs tree, tagged
  `v0.2.1`, builds forever); `git revert` of the retirement commit.
- **Looper node**: record / overdub / undo-last-layer / half-speed / reverse;
  loop length settable by first take or locked to the drum machine's bar.
- **Recording persistence**: sampler, 4-track and looper audio saved as WAV
  under the project's `assets/audio/` and loaded back with the patch; bundles
  carry them.
- **Tuner** (a control node with a Board face).

Exit: record a loop, save, reopen tomorrow, and it plays.

## 0.4 — Play it without a mouse (control)

The engine's event-queue design becomes real: every external input is a
timestamped event on a bounded queue into the callback.

- **MIDI input** (JUCE MidiInput, its own thread, SPSC queue): notes drive
  the synth/pluck; CC and program change drive the rest.
- **MIDI learn** on stomps, knobs, group pedals and rig slots, with a
  learn-mode overlay on the Board; mappings saved in the patch.
- **Gamepad map** (GLFW): d-pad walks pedals, sticks turn the focused knob,
  triggers stomp, shoulders change slot, a button opens the palette. The
  Board must be fully operable with no pointer.
- **The controller**: a purpose-built foot controller (ESP32-S3, class-
  compliant USB MIDI so any DAW also understands it): footswitches with LED
  feedback, expression inputs, encoders, bank buttons. SignalPatch sends
  LED/label state back over SysEx so the pedal shows what the Board shows.
  Firmware lives in its own repo; this repo defines the protocol
  (`docs/CONTROLLER.md`).

Exit: a whole song with hands on the guitar only.

## 0.5 — Stereo

- Per-port channel policy in the compiler (mono / stereo / match-upstream),
  buffers and cables that carry two channels; mono nodes stay valid forever.
- Stereo delay, reverb, chorus, cabinet (dual IR), pan / split / merge,
  dual-mono NAM with explicit CPU accounting.
- Board and rack show channel count on ports and cables.

Exit: the rig into the interface's two outputs sounds like a record, not a
demo.

## 0.6 — Handheld deployment

- **Ally X bring-up**: OS decision (Linux recommended: native Wayland, the
  PipeWire/JACK path we already qualify on; Windows kept building), USB
  interface latency qualification at 64/128, battery vs performance presets.
- **Boot to Board**: `--board --kiosk --unmute`; the same UI at handheld
  scale, touch as a first-class pointer.
- **7-inch preset**: a named scale/density profile for the Ally X screen
  (larger hit targets, fewer knobs per pedal by default, tighter Board
  spacing) built on the global scale factor, selectable from the AUDIO/
  settings menu and by `--profile handheld`. Deliberately later than the
  scale factor itself.
- **Gamepad-only recovery** from every state (menus, browser, prompts).
- Packaging (AppImage/Flatpak, Windows zip), CI for the app on Linux and
  Windows.

Exit: the rig built on the desktop runs unchanged on the handheld, and a gig
happens without touching a keyboard.

## 0.7 — Pedals you can reuse (sub-patches)

A group becomes a real module: its own ports, saved to a library, dropped
into other rigs, shared with other people. Waits for stereo so ports are
defined once.

## Beyond

TONE3000 in-app browsing and downloads; OSC; plug-in target (VST3/CLAP);
scene morphing beyond slot glides; a third-party node SDK.

## Ordering rationale

Retiring the JUCE UI first halves every later step. The looper and
persistence come before control because a foot controller with nothing to
loop is a light show. Stereo waits for control because a mono rig you can
play beats a stereo rig you cannot; it comes before sub-patches so the port
model changes once. The handheld is last as a phase but a constraint
throughout: from 0.3 on, nothing lands in the UI that does not scale with
the window and cannot be reached with a gamepad, and nothing lands in the
engine that cannot meet a 128-sample deadline on a laptop-class CPU.
