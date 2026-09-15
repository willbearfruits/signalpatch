# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

SignalPatch is a C++20 real-time guitar rig / modular rack for Linux and Windows (AGPL-3.0, github.com/willbearfruits/signalpatch, site at willbearfruits.github.io/signalpatch). A headless JUCE 8 audio engine (`src/audio/`) runs under a GLFW + OpenGL 3.3 + NanoVG app (`src/v2/`) with two views of one patch: the **rack** (modules and cables) and the **Board** (the same patch as pedals, groups and five rig slots). The graph derives its ports from the audio device's enabled channels — no hard-coded input count anywhere.

Doc map: `docs/CHANGELOG.md` (keep "Unreleased" current; add a `<release>` to `packaging/linux/*.metainfo.xml` per version) · `docs/ROADMAP.md` (the ordered plan — update as phases land) · `docs/ARCHITECTURE.md` · `docs/REALTIME_SAFETY.md` (callback contract, a release gate) · `docs/PRODUCTION_READINESS.md` (verification gates and audit passes — update when one moves) · `docs/CONTROLLER.md` (foot-controller MIDI/SysEx protocol) · `docs/NAM_ROADMAP.md` · `docs/APPLIANCE.md`.

## Build, run, test

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo   # Windows: -A x64, never an explicit -G "Visual Studio …"
cmake --build build --parallel
./build/signalpatch_tests_artefacts/RelWithDebInfo/signalpatch_tests   # ctest runs this binary as one test
PIPEWIRE_QUANTUM=128/48000 pw-jack ./build/signalpatch_artefacts/RelWithDebInfo/SignalPatch [patch|bundle.zip]
```

- App flags: `--board --kiosk --unmute --scale=N --help`. `packaging/linux/run-from-build.sh` wraps the PipeWire line for a desktop entry.
- **PipeWire**: `PIPEWIRE_LATENCY` is only a hint (pw-jack locks the client to the graph's current quantum, e.g. 1024 while a browser plays); `PIPEWIRE_QUANTUM` forces it. Keep the interface on the **Pro Audio** profile — a raw ALSA open makes WirePlumber drop the capture node. Realtime priority on Arch comes from `realtime-privileges` + the `realtime` group; **rtkit with PipeWire 1.6.8 sets RLIMIT_RTTIME to 0 and the kernel SIGKILLs the app** (debug with `PIPEWIRE_DEBUG="mod.rt:5"`). The HUD says NO RT when the callback thread is not FIFO/RR.
- Dependencies: JUCE 8.0.13 exact (installed package, else FetchContent; `-DFETCHCONTENT_SOURCE_DIR_JUCE=<path>` reuses a tree). GLFW from the system, else FetchContent 3.4. OpenGL entry points come from the vendored glad loader (`external/glad`; `gladLoadGL(glfwGetProcAddress)` in `main`; include `<glad/gl.h>` before GLFW with `GLFW_INCLUDE_NONE`). Roboto (`external/fonts`) is copied next to the executable after each build. libcurl is linked on Linux (JUCE's own HTTP has no TLS — TONE3000 needs it).
- NAM core: local checkout at `SIGNALPATCH_NAM_CORE_DIR` if present, else pinned FetchContent with an idempotent patch step (`packaging/patches/apply-namcore-patch.cmake`); `-DSIGNALPATCH_ENABLE_NAM=OFF` makes the neural nodes passthrough. `nam_core` is linked **whole-archive** (architectures self-register via static initialisers; "No config parser registered" at model load = this regression).
- Targets: `signalpatch` (the app, executable `SignalPatch`), `signalpatch_tests`, `signalpatch_juce` (the retired JUCE rack in `src/ui/`, only with `-DSIGNALPATCH_BUILD_JUCE_UI=ON`; tag `v0.2.1` / branch `juce-ui` hold the last tree where it was the app). JUCE_ALSA/JACK are Linux-only defines; Windows gets WASAPI/DirectSound and a GUI-subsystem link.
- Sanitizers: `build-asan/` (`-fsanitize=address,undefined`) and `build-tsan/` (`-fsanitize=thread`, `-DSIGNALPATCH_BUILD_V2=OFF`) — build and run `signalpatch_tests` there.

### Tests

Hand-rolled harness in `tests/EngineTests.cpp`: each case is a free function throwing via `expect(...)`, registered in the `tests` vector in `main()`. There is no name filter; to run one case, temporarily trim that vector.

- Global `new`/`delete` are replaced by an **allocation trap**; `renderPlanBlocks(..., armTrap=true)` fails if the render path allocates. Never allocate inside an armed window (including building `std::string` messages) — the trap counts the harness too.
- `buildKitchenSinkDocument()` wires one of every node kind (MIDI notes are dispatched every fifth block); extend it and the kinds list in "all node kinds render finite output" when adding a kind. `SIGNALPATCH_SOAK_BLOCKS=1687500` turns the trap test into a 30-minutes-of-audio soak.
- `signalpatch_tests` lists its sources explicitly in CMakeLists (Graph, PatchHistory, PatchBundle, MidiMap, ControllerFeedback, Tone3000, Processors) and links only headless JUCE modules — a new `src/audio/*.cpp` must be added there; the app target globs.
- Nodes rendered on their own (not through a plan) see every input as unconnected (`isInputConnected`), and knob smoothers need a few blocks to arrive before a test measures.

## Engine architecture (src/audio)

**Document vs. snapshot.** `PatchDocument` (Graph.h) is the serialisable patch, mutated only on the message thread. `GraphCompiler` builds an immutable `RenderPlan`; a failed compile leaves the sounding plan untouched. `PatchEngine` swaps plans at a block boundary and reclaims retired ones on its timer, never on the audio thread. `PatchEngine::connect` rolls a rejected cable back out of the document.

**Feedback and safety.** Cycles are legal only through a Feedback Guard (delay → DC block → finite check → ceiling, latching); the compiler rejects unguarded cycles. Outputs carry a safety ceiling; loaded and autosave-restored patches start muted (by design). Device channels are identities: a missing saved channel stays a placeholder, never remapped. First launch prefers a Zoom F4/H-series interface, JACK before ALSA; saved `~/.config/SignalPatch/audio-device.xml` wins afterwards.

**DspNode contract.** `processDsp` is callback code (see REALTIME_SAFETY.md). Per-sample knob reads use `parameterValue` (steps a smoother); **reads made once per block must use `parameterTarget`**, or the value takes seconds to arrive. Message-thread hooks: `getExtraState`/`setExtraState` (model/IR paths, script, looper flags — the `extra` JSON), `handleUiCommand`/`uiToggleState` (transport commands through atomics), `audioContentVersion` + `export/importAudioContent` (recordings), `lanePosition`, `setBypassed`. Audio-thread hooks: `handleMidiNote`/`allNotesOff` (from the engine's lock-free note FIFO). Bypass passes audio inputs to audio outputs in order (a single input feeds every audio output). Async model loads hold weak handles (`enable_shared_from_this`).

**Compatibility rule.** Saved patches store parameter values and cable port indices by position. New parameters and new input/output ports go **at the end** of a node's constructor (the tone stack, 4-track per-track speeds, Clock inputs and synth Pitch inputs all follow this).

**Recordings.** Whenever a node's recorded content changes, its `audioContentVersion` must move (take ended, overdub stopped by any path, undo finished). `bundle::toJsonWithAudio` rewrites a WAV only when the version moved, and the engine timer marks the document edited when a version moves (autosave, quit prompt). WAVs are written to `.part` and moved into place; loads resample to the device rate, and `prepareDsp` resamples on a live rate change.

**Undo.** `PatchHistory` records entries *after* the change; continuous edits coalesce until `closeGesture()`; `beginCompoundGesture` groups several edits (multi-drag/delete, a slot glide via `recordGlide`) into one step. Removing a node snapshots groups and MIDI mappings so undo restores them. `loadPatch`/`newPatch`/device relayout clear the stack.

**Control.** `MidiMap` (CC/note/program → knob/stomp/command/slot/group; `relative`, calibrated `low`/`high`) is saved as `"midi"`. A velocity-0 note-on is a release everywhere (`isNoteOn()`, never `isNoteOn(true)`). Keys and program changes fire commands per press; CCs use a rising-edge gate. `ControllerFeedback` plus a controller's SysEx hello drive LED/label feedback. The Clock node emits 0.8-high pulses and a 1.0-high pulse on start/reset; `ClockFollower` (drum machine, sequencer, looper) treats pulses as "external" for max(4 s, 2.5 intervals) and realigns on restart pulses.

**Stereo** is L/R port pairs over mono buffers (Pan, Stereo Merge/Delay/Chorus/Reverb, Cabinet R); the rack cables the R pair in the same drag.

**Other modules:** `PatchBundle` (portable zips; `extra.model/ir/irB` and audio paths stored relative to the patch folder; extraction never overwrites an existing project folder), `Tone3000` (PKCE OAuth against tone3000.com/api/v1 with a localhost:41443 callback; keys in `~/.config/SignalPatch/tone3000.json`, tokens in `tone3000-tokens.json`; only the publishable key is used).

## App architecture (src/v2)

- `Main.cpp` owns the GLFW window, the NanoVG context and a `unique_ptr<RackView>` destroyed **before** NanoVG/GL teardown. JUCE runs headless with `JUCE_MODAL_LOOPS_PERMITTED=1`; the frame loop pumps `runDispatchLoopUntil(1)`, so engine timers, async loads and MIDI hops all ride on it. Frames render only when input, telemetry or an animation changed.
- `RackView` owns everything visible: rack plates cached in framebuffers with a live overlay, cables, the Board, in-canvas `Menu`/`TextPrompt`/`FileBrowser` (Menu.*) and `ToneBrowser`, MIDI learn/calibration, gamepad, UI scale (one transform; inputs are divided by `uiScale` at the entry points).
- **Patch swaps**: opening a patch, New, or a slot with different modules calls `patchReplaced()`, which drops drags, overlays, learn state, glides and selections and bumps `patchEpoch`. Callbacks that captured a `NodeId` must check the epoch and the node's kind before touching it (see `applyExtraKey`). Undo/redo go through `undoNow`/`redoNow`, which end drags and glides first.
- `ToneBrowser` network jobs run on a thread pool with a **copy** of the client and write only into shared result objects; the message thread adopts results and rotated tokens after the `callAsync` hop.
- **Adding a node kind**: `NodeKind` enum (Graph.h) → the three name/key registries in Graph.cpp → DSP class + factory in Processors.cpp → `moduleCatalogue()` in Palette.h (colour is derived from its family) → transport buttons, if any, in the `switch` in `RackView::rebuildLayouts()` (Board pedals pick them up) → the exhaustive switches in `src/ui/Theme.cpp` (the optional JUCE target still compiles them) → tests (kinds list, kitchen sink).
- Tooling: never `pkill -f SignalPatch` from a tool shell (it matches the shell) — use `pkill -x`; after renaming targets delete stale binaries in `build/*_artefacts/`.

## Publishing

- CI (`.github/workflows/ci.yml`): Linux GCC and Windows MSVC build the app and run the full suite (allocation trap included), forcing the NAM FetchContent path; both upload the executable plus `fonts/`, which release zips are made from. MSVC's 1.5x vector growth once exposed a use-after-realloc Linux never showed — don't hold pointers into `PatchDocument::getNodes()` across mutations.
- Pages are served from `main:/docs` (`docs/index.html`, screenshots in `docs/assets/`). `packaging/flatpak/` builds GLFW as a module (not rebuilt since the GLFW switch).
