# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

SignalPatch is a C++20/JUCE standalone modular effects processor for Linux and Windows. The patch graph derives its ports from the selected audio device's actual enabled channels — there is deliberately no hard-coded input count anywhere in the graph or DSP layer.

## Build and test (Linux)

```sh
cmake -S . -B build -G Ninja \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DJUCE_DIR=/usr/local/lib/cmake/JUCE-8.0.13
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

Run the app: `./build/signalpatch_artefacts/RelWithDebInfo/SignalPatch` — on this workstation (PipeWire desktop), prefer `pw-jack ./build/.../SignalPatch` for the JACK backend.

- JUCE is pinned to **8.0.13 EXACT**; the installed package at `/usr/local/lib/cmake/JUCE-8.0.13` is preferred, with a network FetchContent fallback (disable via `-DSIGNALPATCH_ALLOW_JUCE_FETCH=OFF`).
- Windows: VS 2022 generator, `-DSIGNALPATCH_ENABLE_ASIO=ON` optionally (brings Steinberg ASIO SDK licence into scope).
- Targets: `signalpatch` (GUI app) and `signalpatch_tests` (console app).

### Tests

`ctest` registers a single test (`signalpatch_engine_tests`) that runs the whole console binary; there is no per-case filter. To iterate, run the binary directly:

```sh
./build/signalpatch_tests_artefacts/RelWithDebInfo/signalpatch_tests
```

Tests are a hand-rolled harness in `tests/EngineTests.cpp`: each case is a free function that throws on failure via `expect(...)`, registered in the `tests` vector in `main()`. **Adding a test case requires adding it to that vector.**

Note an asymmetry in CMakeLists.txt: the app target globs `src/audio/` and `src/ui/` (new files are picked up automatically via `CONFIGURE_DEPENDS`), but `signalpatch_tests` lists its sources explicitly (`Graph.cpp`, `Processors.cpp`) — a new audio source file the tests depend on must be added to the test target by hand. The test target links no audio-device modules, so anything it compiles must not require them.

## Architecture

Read `docs/ARCHITECTURE.md` for the full design; the essentials:

**Editable document vs. immutable snapshot.** `PatchDocument` (src/audio/Graph.h) is the user-facing, serializable patch, mutated only on the message thread. `GraphCompiler` consumes a document copy plus the current device layout and produces a `RenderPlan` (the docs call this `CompiledGraph`) — an immutable execution snapshot with a topologically ordered schedule, prepared node state, and buffer plan. Compilation happens off the audio thread; a failed compile leaves the currently sounding snapshot untouched. `PatchEngine` (a `juce::AudioIODeviceCallback`) adopts a new snapshot only at a block boundary, and retired snapshots must never be destroyed on the audio thread.

**Dependency direction is enforced by intent:** UI (`src/ui/`) may depend on the patch model and telemetry; DSP (`src/audio/`) must never depend on UI objects. Telemetry flows audio→UI through bounded, lossy queues; the UI is observational and can disappear entirely without changing the sound.

**Feedback is never a plain connection.** A directed audio cycle is legal only through a Feedback Guard node (fixed causal delay → gain → DC blocker → finite-value check → hard ceiling, with runaway latching and local reset). The compiler rejects unguarded cycles. Device outputs additionally have a fixed safety ceiling and a panic/restart ramp, and loaded patches start muted.

**Device channels are identities.** Physical channel index + remembered label persist in patches; a saved channel missing from the current device shows as a disconnected placeholder — it is never silently remapped (several tests pin this behavior).

**Patches** save/load as versioned `.signalpatch` JSON; device/backend preferences persist separately from the creative patch.

## Real-time safety is a release gate

`docs/REALTIME_SAFETY.md` is the contract for any code reachable from the audio callback (`PatchEngine`'s callback or a node's `process()`): no allocation, no locks, no I/O, no exceptions, no UI calls, no `shared_ptr` last-owner release, bounded loops over prepared storage, finite output even on NaN/Inf input, and tolerance of any block length from zero to the prepared maximum. Its review checklist applies to every callback-reachable change — consult it before touching `src/audio/`.

## Current boundaries (deliberate, don't "fix" silently)

- Effect nodes are mono; MIDI/OSC mapping, undo/redo, and third-party plug-in hosting are not implemented yet. `docs/ROADMAP.md` is the single ordered plan (0.2 MIDI/control → 0.3 stereo/recording → 0.4 NAM qualification → 0.5 engine maturity/shipping) — keep it updated as phases land.
- First-launch device preference: `PatchEngine::applyPreferredCaptureDevice` picks a Zoom F4 / H-series interface when no saved `audio-device.xml` exists; the saved state always wins afterwards. On this machine launch via `pw-jack` — PipeWire holds the F4 in Pro Audio mode, so raw ALSA cannot open it.
- Adding a node kind touches five places: the `NodeKind` enum (Graph.h), the three name/key registries in Graph.cpp, the DSP class + factory in Processors.cpp, `kindAccent`/`kindTag`/`drawKindGlyph` in Theme.cpp (exhaustive switches), and the shared `nodePalette()` table in Theme.cpp (feeds both the left palette and the canvas right-click menu). Extend the kinds list in the "all node kinds render finite output" engine test.
- Nodes with UI beyond knobs use three `DspNode` hooks, all message-thread-only: `getExtraState`/`setExtraState` (script text, NAM model path — persisted as `extra` in patch JSON), `handleUiCommand`/`uiToggleState` (sampler REC/PLAY, 4-track transport/arm — communicate with the callback via atomics), and pedal bypass (`setBypassed`, atomic flag honoured inside `DspNode::render`; Feedback Guard and hardware are never bypassable).

## NAM (Neural Amp Modeler)

- `SIGNALPATCH_ENABLE_NAM` (default ON) compiles `nam_core` from the checkout at `SIGNALPATCH_NAM_CORE_DIR` (defaults to `~/Projects/GitHub/external_clones/neural-amp-modeler-plugin-a2/NeuralAmpModelerCore`). Missing checkout → the Neural Amp node silently becomes a passthrough placeholder.
- nam_core must be linked **whole-archive**: NAM architectures (WaveNet/LSTM/ConvNet) register their config parsers via static initializers that a normal static-lib link drops ("No config parser registered for architecture" at model load = this regression).
- That clone carries a 3-line local patch: the `#ifdef _LIBCPP_VERSION` guards in `NAM/wavenet/slimmable.{h,cpp}` were widened to `|| defined(NAM_NONATOMIC_SHARED_PTR)` because GCC 11's libstdc++ lacks `std::atomic<std::shared_ptr>`. If the clone is ever re-pulled, reapply it or the build breaks.
- Model parsing runs on a worker thread (`NeuralAmpNode::setExtraState` → `juce::Thread::launch`, weak node handle, message-thread apply in `finishModelLoad`); headless tests take the synchronous path (no MessageManager). The previous model is retired, never freed by the audio thread. The "NAM model loads and processes" engine test loads a real Boss model from the lv2 clone's `models/` dir and skips gracefully when absent. Remaining NAM work: Stage 3 performance qualification per `docs/NAM_ROADMAP.md`.
- `docs/PRODUCT.md` is the product target, not a claim of what ships; `README.md` lists what actually works. `docs/PRODUCTION_READINESS.md` tracks REALTIME_SAFETY verification — closed: allocation trap (armed in the "callback path performs no allocation" test; `SIGNALPATCH_SOAK_BLOCKS` env scales it into a soak), ASan+UBSan (`build-asan/`), peak-DSP readout. Open: TSan, live-device soak, Windows. Update it when a gate moves.
- Node components are torn down and rebuilt on every engine change broadcast — any async UI callback (menus, choosers) launched from a `NodeComponent` must hold a `Component::SafePointer`, never a raw `this`.
- No distribution licence is declared yet; JUCE 8 is AGPLv3/commercial dual-licensed.
