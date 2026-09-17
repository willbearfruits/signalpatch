# SignalPatch

A guitar rig you patch with cables. Amps, cabinets, pedals, a looper, a
4-track, synths and voice effects are modules; you connect them however you
like while the sound keeps running. Linux and Windows.

[![CI](https://github.com/willbearfruits/signalpatch/actions/workflows/ci.yml/badge.svg)](https://github.com/willbearfruits/signalpatch/actions/workflows/ci.yml)
[![Release](https://img.shields.io/github/v/release/willbearfruits/signalpatch?label=release)](https://github.com/willbearfruits/signalpatch/releases/latest)
[![License: AGPL-3.0](https://img.shields.io/badge/license-AGPL--3.0-blue)](LICENSE)

![The rack: a neural pedal into an amp and cabinet, a drum machine, a 4-track and synths, cabled together](docs/assets/rack.png)

**[Download](https://github.com/willbearfruits/signalpatch/releases/latest)** (Windows x64, Linux x86_64) ·
[Website](https://willbearfruits.github.io/signalpatch/) ·
[Changelog](docs/CHANGELOG.md)

## What's in it

| Family | Modules |
| --- | --- |
| Neural | Neural Amp, Neural Pedal (any `.nam` capture), Cabinet (two impulse responses and a blend) |
| Effects | distortion, filter, delay, reverb, chorus, phaser, tremolo, bitcrusher, ring mod, pitch shifter, granular |
| Stereo | pan, stereo merge, ping-pong delay, stereo chorus, stereo reverb |
| Voice | vowel filter, vocoder, autotune |
| Instruments | mono synth, pluck, noise, drum machine, sampler, looper, 4-track |
| Dynamics | compressor, limiter, noise gate, Feedback Guard |
| Control | clock, MIDI note, LFO, random, envelope follower, 8-step sequencer, macro, spectral follower, script |
| Utility | tuner, gain, 4-channel mixer, crossfade |

- **Amp captures from TONE3000.** Log in once, search captures and cabinet
  impulses from inside the app, press Enter to hear one in the module you
  are playing through.
- **The Board.** Press Tab and the same patch is a pedalboard: pedals with
  footswitches, several modules grouped into one pedal, five rig slots.
  Knobs glide when you change slot.
- **Modulation.** Every knob has a mod input, and a modulated knob moves on
  screen. The inspector (`I`) sets a knob's range, curve and mod depth.
- **Hands-free control.** MIDI learn on knobs, footswitches, buttons and
  slots; expression pedals and relative encoders; a gamepad; a touch mode
  for handhelds; a small SysEx protocol for a foot controller with LEDs
  ([`docs/CONTROLLER.md`](docs/CONTROLLER.md)).
- **Feedback loops** are allowed, through a Feedback Guard module that keeps
  them bounded.
- **Files.** Patches are JSON with their recordings beside them, and export
  as a zip with models and impulses. Undo covers every edit. The last
  session comes back on launch, muted until you fade it in.

<p align="center">
  <img src="docs/assets/board.png" alt="The Board: the same rig as pedals with footswitches and five rig slots" width="49%">
  <img src="docs/assets/tone3000.png" alt="The TONE3000 browser inside the app" width="49%">
</p>

## Status

Version 0.3, and young. Linux with PipeWire or JACK is the main platform.
The Windows build comes from CI and has seen less use; it is being tried on
a ROG Ally, which is where the touch mode and gamepad support come from.
Bug reports are welcome.

## Run it

Unzip a [release](https://github.com/willbearfruits/signalpatch/releases/latest)
and start `SignalPatch`. Pick your interface under AUDIO. On Windows choose
"Windows Audio (Exclusive Mode)" for the lowest latency.

The Linux build needs `libglfw3`, `libcurl4` and ALSA installed. On a
PipeWire desktop, start it through `pw-jack` with a fixed quantum:

```sh
PIPEWIRE_QUANTUM=128/48000 pw-jack ./SignalPatch
```

Put `.nam` captures in `~/Documents/SignalPatch/models`, or get them from
the TONE3000 browser (`Ctrl+T`).

## Build

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build --parallel
ctest --test-dir build
```

JUCE, GLFW, NanoVG and NeuralAmpModelerCore are fetched if they are not
installed. Packages, Windows, Flatpak, realtime priority and the command
line flags are in [`docs/BUILDING.md`](docs/BUILDING.md).

## How it works

The engine is headless JUCE 8; the app on top is GLFW, OpenGL and NanoVG.
Editing a patch compiles a new immutable render plan, which replaces the
running one at a block boundary. The audio callback does not allocate, lock
or block. The test suite checks that with an allocation trap around a graph
holding every module.

- [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md): engine and app structure
- [`docs/REALTIME_SAFETY.md`](docs/REALTIME_SAFETY.md): rules for callback code
- [`docs/TESTING.md`](docs/TESTING.md): what is verified and what is not
- [`docs/ROADMAP.md`](docs/ROADMAP.md): what comes next
- [`docs/HANDHELD.md`](docs/HANDHELD.md): a dedicated machine that boots into the rig

## Licence

GNU AGPL-3.0, the same as JUCE 8's open-source tier. NeuralAmpModelerCore is
MIT. ASIO support is off by default because it brings in Steinberg's SDK
licence.
