# SignalPatch

SignalPatch is a native, cross-platform modular effects processor for Linux and
Windows. Its patch graph discovers the selected sound device's actual input
channel count instead of assuming that every interface has six inputs.

The application is built in C++20 with JUCE so audio processing stays in a
native callback. Linux builds expose JUCE's ALSA and JACK backends; JACK can
also run through PipeWire with `pw-jack`. Windows builds always expose WASAPI
and can opt into ASIO when the interface provides an ASIO driver.

## What works now

- The selected device's physical channel names become patch ports. Sparse
  channel masks keep their physical identity, and unavailable saved channels
  remain visible as disconnected placeholders instead of being silently
  remapped or deleted.
- The custom immutable render graph supports typed audio/control cables,
  live graph replacement, fan-in/fan-out, and audio-rate modulation sockets on
  every exposed parameter.
- 33 built-in boxes across six palette groups: utility (gain, 4-ch mixer,
  crossfade), effects (distortion, SVF filter, delay, reverb, chorus, phaser,
  tremolo, bitcrusher, ring mod, pitch shifter, granular cloud, Neural Amp),
  voice (vowel/formant filter, 12-band vocoder, autotune), instruments (mono
  synth, Karplus-Strong pluck, noise, 3-lane drum machine, live-input sampler,
  60 s 4-track tape loop with varispeed), dynamics (compressor, limiter, noise
  gate, Feedback Guard) and control (LFO, random S&H, envelope follower,
  8-step sequencer, macro, FFT spectral follower, per-sample Script node).
- Rack/pedalboard interaction: modules wear accent-coloured face plates with
  mounting rails, screws and an output-activity LED; every effect has a stomp
  footswitch with an LED for true bypass; cables sag, carry flowing signal
  comets and are coloured by their source module; right-click on empty canvas
  adds a node at the cursor, right-click on a node offers bypass/delete.
- Autosave recovery: on startup the last session is restored muted; the
  default patch is only created when no autosave exists. Current audit status
  lives in `docs/PRODUCTION_READINESS.md`.
- The Neural Amp box runs real NAM models when built against a
  NeuralAmpModelerCore checkout (`SIGNALPATCH_ENABLE_NAM`, on by default when
  the checkout is present). Model loading and warm-up happen on the message
  thread and are handed to the callback atomically; the model path is saved in
  the patch. Without the checkout the box stays a safe passthrough.
- A patch path can be passed on the command line:
  `SignalPatch demo-pedalboard.signalpatch` (loads panic-muted).
- On first launch (no saved audio state) the engine scans backends for a
  Zoom F4 / H-series interface and opens it with every channel enabled;
  once any device choice is saved, the saved state always wins.
- Every cycle must contain a Feedback Guard. It provides a fixed causal delay,
  gain, DC blocking, finite-value checks, a hard bound, runaway latching, and
  a local reset. Device outputs also have a fixed safety ceiling and a 5 ms
  panic/restart ramp.
- Nodes show process-specific scopes, step state, delay decay, or gain
  reduction. Cable brightness and moving pulses follow signal level; clipping
  gets an explicit marker.
- Patches save/load as versioned `.signalpatch` JSON, edit autosaves are kept
  separately, and audio-device/backend/channel/buffer choices persist across
  launches. Loaded patches stay muted until the user deliberately fades them
  back in.
- The canvas supports cable and node deletion, scroll/pan, 50-160% zoom, and
  exact value entry with units.

Current boundaries are deliberate: effect nodes are mono, and MIDI/OSC mapping
and third-party plug-in hosting are not implemented yet. The NAM integration
follows the staging in `docs/NAM_ROADMAP.md`; its performance-qualification
gates (Stage 3) have not been run yet.

## Requirements

- CMake 3.22 or newer
- A C++20 compiler
- Ninja, Make, or Visual Studio 2022
- JUCE 8.0.13
- Linux: ALSA and JACK development packages

CMake first looks for an installed JUCE 8.0.13 package. On this workstation it
resolves `/usr/local/lib/cmake/JUCE-8.0.13`. If the package is unavailable,
CMake fetches the pinned `8.0.13` release from the official JUCE repository.
Disable network fallback with `-DSIGNALPATCH_ALLOW_JUCE_FETCH=OFF`.

## Configure and build on Linux

```sh
cmake -S . -B build -G Ninja \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DJUCE_DIR=/usr/local/lib/cmake/JUCE-8.0.13
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

Run the application:

```sh
./build/signalpatch_artefacts/RelWithDebInfo/SignalPatch
```

If the desktop audio graph is managed by PipeWire and the JACK backend is
preferred, run it through the installed bridge:

```sh
pw-jack ./build/signalpatch_artefacts/RelWithDebInfo/SignalPatch
```

For live monitoring, start with 48 kHz and a 128-sample buffer, then try 64
samples if the device and patch remain stable. A 64-sample buffer at 48 kHz is
1.33 ms per buffer; complete input-to-output latency is higher because drivers
and converters add their own buffers.

## Configure and build on Windows

From a Visual Studio 2022 developer shell:

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure
.\build\signalpatch_artefacts\Release\SignalPatch.exe
```

WASAPI is enabled by default. For the lowest latency with a hardware interface,
enable ASIO at configure time:

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 `
  -DSIGNALPATCH_ENABLE_ASIO=ON
```

ASIO support only helps when a suitable driver is installed. It also brings
the Steinberg ASIO SDK licence terms into scope.

## Repository layout

```text
src/Main.cpp          application entry point
src/audio/            real-time graph, nodes, effects, metering and safety
src/ui/               patch canvas, boxes, cables and visualisations
tests/EngineTests.cpp audio-engine console tests
docs/                 product and architecture notes
```

The CMake target names are `signalpatch` and `signalpatch_tests`.

## Licensing

JUCE 8 modules are dual-licensed under AGPLv3 and JUCE's commercial licence.
Choose and comply with one of those licences before distributing SignalPatch.
Enabling `SIGNALPATCH_ENABLE_ASIO` additionally requires compliance with the
licence bundled with JUCE's Steinberg ASIO SDK headers. SignalPatch itself does
not yet declare a distribution licence; add one before publishing binaries or
source.
