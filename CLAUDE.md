# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

SignalPatch is a C++20 real-time guitar rig / modular rack for Linux and Windows (AGPL-3.0, github.com/willbearfruits/signalpatch, site at willbearfruits.github.io/signalpatch). A headless JUCE 8 audio engine (`src/audio/`) runs under a GLFW + OpenGL 3.3 + NanoVG app (`src/v2/`) with two views of one patch: the **rack** (modules and cables) and the **Board** (the same patch as pedals, groups and five rig slots). The graph derives its ports from the audio device's enabled channels — no hard-coded input count anywhere.

Doc map: `docs/CHANGELOG.md` (keep "Unreleased" current; add a `<release>` to `packaging/linux/*.metainfo.xml` per version) · `docs/ROADMAP.md` · `docs/ARCHITECTURE.md` · `docs/REALTIME_SAFETY.md` (rules for callback code) · `docs/TESTING.md` (what is verified and what is not; update when that changes) · `docs/BUILDING.md` · `docs/CONTROLLER.md` (foot-controller MIDI/SysEx protocol) · `docs/HANDHELD.md`. **Writing style for every public text (README, docs, site, release notes): short, plain, factual. No slogans, no bold-phrase bullet openers stacked with em dashes, no "for real" / "honest" / "proven" claims; say what it does and what is untested.**

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

**Multi-core render.** The plan carries its DAG as tasks (`taskIndegree`, CSR successors, per-node atomics); `RenderPlan::render(..., RenderPool*)` uses the pool only when `hasParallelWork()` (heavy kinds with a cabled input, more of them than fit on one path) and `pool->isUsable()` (every helper got realtime scheduling). `RenderPool::run` opens the block, works alongside the helpers, closes it and spins until `inside == 0`, so plans can be retired as before. Helpers set FTZ/DAZ themselves. NAM runs with `enable_fast_tanh()` (set once in `parseModel`) and the build defaults to `-march=x86-64-v3`; FMA changes float rounding, so tests compare with tolerances, never exact round trips.

**Feedback and safety.** Cycles are legal only through a Feedback Guard (delay → DC block → finite check → ceiling, latching); the compiler rejects unguarded cycles. Outputs carry a safety ceiling; loaded and autosave-restored patches start muted (by design). Device channels are identities: a missing saved channel stays a placeholder while a cable uses it (unused ones are dropped), never remapped. Input channels whose name starts with "monitor" (PipeWire-JACK files a sink's monitors under the same client as its captures) are never enabled or shown; picking a device enables every real channel (`useEveryDeviceChannel`). First launch prefers a Zoom F4/H-series interface, JACK before ALSA; saved `~/.config/SignalPatch/audio-device.xml` wins afterwards.

**DspNode contract.** `processDsp` is callback code (see REALTIME_SAFETY.md). Per-sample knob reads use `parameterValue` (steps a smoother); **reads made once per block must use `parameterTarget`**, or the value takes seconds to arrive. Message-thread hooks: `getExtraState`/`setExtraState` (model/IR paths, script, looper flags — the `extra` JSON), `handleUiCommand`/`uiToggleState` (transport commands through atomics), `audioContentVersion` + `export/importAudioContent` (recordings), `lanePosition`, `setBypassed`. Audio-thread hooks: `handleMidiNote`/`allNotesOff` (from the engine's lock-free note FIFO). Bypass passes audio inputs to audio outputs in order (a single input feeds every audio output). Async model loads hold weak handles (`enable_shared_from_this`).

**Parameter shape (the inspector).** `DspParameter` keeps a per-instance `ParameterShape` (minimum, maximum, curve): `baseNormalised` is the knob's *travel* (0..1), and `valueFromNormalised` / `normalisedFromValue` map it through the shape. Nothing outside the parameter converts with `range` directly. Modulation adds to the travel, so it stays inside min..max; `setShape` keeps the current value where it still fits. Saved as `min` / `max` / `curve` only when edited; load the shape before the value (`applySavedParameter`). `DspNode::render` leaves each mod socket's level in `liveModulation` once per block; the UI draws `getLiveNormalised()`.

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
- **Inspector** (`Inspector.*`): a non-modal panel docked right, following `selectedNode`; it edits through `engine.setParameter` / `setParameterShape` / `setModulationDepth` and asks for text through RackView's prompt. Knobs with a cabled mod socket (`modulatedKnobs`, rebuilt with the layouts) get `drawKnobModulation` over the cached plate.
- **Session** (`saveSession` / `restoreSession`, keys `session.*` in `~/.config/SignalPatch/SignalPatch.settings`): view mode, camera as the patch point at the view centre (re-placed for each window size during the first 1.5 s, because a tiling desktop resizes a mapping window), slot, file, unsaved mark, mute when "start muted" is off. Saved every few seconds when changed and on quit. **Testing the app beside a running session**: launch a copy of the binary under another name with `HOME=<scratch>` (JUCE ignores `XDG_CONFIG_HOME`), on an empty workspace via `hyprctl dispatch 'hl.dsp.exec_cmd("<script>", { workspace = "11 silent" })'`; never start a second instance on the real config, and never under `timeout`.
- **Touch mode** (`setTouchMode`, persisted in settings, auto-detected in `Main.cpp` from a small physical screen at a high content scale): `touchHit()` multiplies every hit radius, taps arm `pendingSource` for tap-to-connect, a held press in `tick()` synthesises a right click, and `touchButtonBounds` draws the zoom/fit buttons. Menu/FileBrowser/TextPrompt metrics are members with `setTouchMode`.
- Tooling: never `pkill -f SignalPatch` from a tool shell (it matches the shell) — use `pkill -x`; after renaming targets delete stale binaries in `build/*_artefacts/`.

## Publishing

- CI (`.github/workflows/ci.yml`): Linux GCC and Windows MSVC build the app and run the full suite (allocation trap included), forcing the NAM FetchContent path; both upload the executable plus `fonts/`, which release zips are made from. MSVC's 1.5x vector growth once exposed a use-after-realloc Linux never showed — don't hold pointers into `PatchDocument::getNodes()` across mutations.
- Pages are served from `main:/docs` (`docs/index.html`, screenshots in `docs/assets/`). `packaging/flatpak/` builds GLFW as a module (not rebuilt since the GLFW switch).
