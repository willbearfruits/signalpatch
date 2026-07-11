# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

SignalPatch is a C++20/JUCE 8 standalone modular audio processor (guitar/voice/synth rack) for Linux and Windows, published at github.com/willbearfruits/signalpatch under AGPL-3.0, with a site at willbearfruits.github.io/signalpatch. The patch graph derives its ports from the selected audio device's actual enabled channels — there is deliberately no hard-coded input count anywhere in the graph or DSP layer.

Doc map: `docs/ROADMAP.md` (the single ordered plan, 0.2 MIDI/control → 0.6 handheld instrument — keep it updated as phases land) · `docs/ARCHITECTURE.md` (engine design) · `docs/REALTIME_SAFETY.md` (callback contract, a release gate) · `docs/PRODUCTION_READINESS.md` (which verification gates are closed vs. open — update when a gate moves) · `docs/PRODUCT.md` (target spec, not shipped claims) · `docs/NAM_ROADMAP.md` (neural amp staging).

## Build and test

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

Run: `./build/signalpatch_artefacts/RelWithDebInfo/SignalPatch [patch.signalpatch]` — on this workstation (PipeWire desktop) use `pw-jack ./build/.../SignalPatch`; PipeWire holds the Zoom F4 in Pro Audio mode, so raw ALSA cannot open it.

- **JUCE 8.0.13 EXACT**: installed package at `/usr/local/lib/cmake/JUCE-8.0.13` preferred, FetchContent fallback otherwise (`-DSIGNALPATCH_ALLOW_JUCE_FETCH=OFF` disables).
- **NAM source resolution**: local checkout at `SIGNALPATCH_NAM_CORE_DIR` (defaults to the `external_clones/neural-amp-modeler-plugin-a2` submodule path) if present, else FetchContent of pinned upstream `NeuralAmpModelerCore@ab72c07` with `packaging/patches/namcore-nonatomic-shared-ptr.patch` applied. `-DSIGNALPATCH_ENABLE_NAM=OFF` builds the Neural Amp node as passthrough.
- **Windows**: `cmake -S . -B build -A x64` — do NOT pass an explicit `-G "Visual Studio 17 2022"`; runner/user VS versions vary.
- **Sanitizers**: `build-asan/` is configured with `-fsanitize=address,undefined -fno-sanitize-recover=all`; build `signalpatch_tests` there and run it.
- **Flatpak**: `flatpak run org.flatpak.Builder --user --install --force-clean build-flatpak packaging/flatpak/io.github.willbearfruits.SignalPatch.yml` (manifest allows network at build time for the fetches — would need vendored sources for Flathub proper).
- Targets: `signalpatch` (GUI app), `signalpatch_tests` (console), `nam_core` (static lib, linked **whole-archive** — NAM architectures self-register via static initializers a normal archive link drops; "No config parser registered for architecture" at model load = this regression).

### Tests

`ctest` runs the whole console binary as one test; iterate by running `./build/signalpatch_tests_artefacts/RelWithDebInfo/signalpatch_tests` directly. Hand-rolled harness in `tests/EngineTests.cpp`: each case is a free function throwing via `expect(...)`, **registered in the `tests` vector in `main()`**.

- The test binary replaces global `new`/`delete` as an **allocation trap**; `renderPlanBlocks(..., armTrap=true)` asserts the render path allocates nothing. Never allocate (including `std::string` construction for expect-messages) inside an armed window — the trap counts harness allocations too, and that exact mistake has already produced 68k false violations once.
- `SIGNALPATCH_SOAK_BLOCKS=1687500 ./signalpatch_tests` turns the trap test into a 30-minutes-of-audio soak.
- `buildKitchenSinkDocument()` wires one of every node kind (chain + sources + mod routes + guarded feedback); extend it and the kinds list in "all node kinds render finite output" when adding kinds.
- CMake asymmetry: the app target globs `src/audio/` and `src/ui/` (`CONFIGURE_DEPENDS`), but `signalpatch_tests` lists sources explicitly (Graph.cpp, Processors.cpp) and links only headless JUCE modules (incl. `juce_dsp`) — no audio-device modules.
- The NAM engine test loads a real Boss model from the lv2 clone's `models/` dir and skips gracefully when absent (e.g. CI).

## Architecture

Read `docs/ARCHITECTURE.md` for the full design; the essentials:

**Editable document vs. immutable snapshot.** `PatchDocument` (src/audio/Graph.h) is the user-facing, serializable patch, mutated only on the message thread. `GraphCompiler` produces a `RenderPlan` (the docs call it `CompiledGraph`) — an immutable execution snapshot. A failed compile leaves the sounding snapshot untouched. `PatchEngine` adopts a new snapshot only at a block boundary; retired snapshots are reclaimed on the engine timer, never destroyed on the audio thread.

**Dependency direction:** UI (`src/ui/`) may depend on the patch model and telemetry; DSP (`src/audio/`) must never depend on UI objects. Telemetry is lossy and observational — removing it cannot change the sound.

**Feedback is never a plain connection.** Cycles are legal only through a Feedback Guard (causal delay → DC block → finite check → hard ceiling, runaway latching). The compiler rejects unguarded cycles; guards and hardware endpoints are never bypassable. Device outputs carry a fixed safety ceiling; loaded/restored patches start panic-muted.

**Device channels are identities.** Physical index + label persist; a missing saved channel stays a disconnected placeholder, never silently remapped (pinned by tests).

**Node hooks beyond knobs** (all message-thread-only): `getExtraState`/`setExtraState` (script text, NAM model path — persisted as `extra` in patch JSON), `handleUiCommand`/`uiToggleState` (sampler/4-track transport via atomics), and `setBypassed` (atomic, honoured inside `DspNode::render`). `DspNode` derives `enable_shared_from_this` so async workers hold weak handles (see `NeuralAmpNode::setExtraState`: parse on `juce::Thread::launch`, apply in `finishModelLoad` on the message thread; headless tests take the synchronous path since no MessageManager exists).

**Adding a node kind touches five places:** the `NodeKind` enum (Graph.h), the three name/key registries in Graph.cpp, the DSP class + factory in Processors.cpp, `kindAccent`/`kindTag`/`drawKindGlyph` in Theme.cpp (exhaustive switches), and the shared `nodePalette()` table in Theme.cpp (feeds both the palette and the canvas right-click menu). Step-grid parameters pass `modulatable=false` to `addParameter` so they don't spawn mod ports.

**UI lifetime rule:** node components are torn down and rebuilt on every engine change broadcast — any async UI callback (menus, choosers) launched from a `NodeComponent` must hold a `Component::SafePointer`, never a raw `this`.

## Real-time safety is a release gate

`docs/REALTIME_SAFETY.md` is the contract for any callback-reachable code: no allocation, locks, I/O, exceptions, UI calls, or `shared_ptr` last-owner release; bounded loops over prepared storage; finite output on NaN/Inf input; any block length from zero to the prepared maximum. Consult its review checklist before touching `src/audio/`. Verified so far: allocation trap, 30-min-audio soak, ASan+UBSan (see PRODUCTION_READINESS.md; TSan and a live-device soak remain open).

## Deliberate boundaries (don't "fix" silently)

- Effect nodes are mono; MIDI/OSC mapping, undo/redo, plug-in hosting are roadmap items, not omissions.
- Sampler/4-track audio content is not saved with patches (roadmap 0.3).
- First-launch device preference: `PatchEngine::applyPreferredCaptureDevice` picks a Zoom F4 / H-series interface when no saved `audio-device.xml` exists; saved state always wins afterwards. Delete `~/.config/SignalPatch/audio-device.xml` to re-trigger.
- Autosave restores on startup, muted — that's the spec'd safety behaviour, not a bug.

## Publishing infrastructure

- **CI**: `.github/workflows/ci.yml` — Linux (green) and Windows (MSVC; as of 2026-07-11 the test suite segfaults on the Windows runner — under investigation with unbuffered test output). CI forces the NAM FetchContent path via `-DSIGNALPATCH_NAM_CORE_DIR=<nonexistent>`.
- **Pages**: served from `main:/docs` with `.nojekyll`; `docs/index.html` is the landing page, screenshots in `docs/assets/`.
- **Packaging**: `packaging/linux/` (.desktop, AppStream metainfo — add a `<release>` entry per version — and SVG icon), `packaging/flatpak/` (manifest), `packaging/patches/` (NAM guard patch; the local clone carries the same patch applied in-tree — if that clone is re-pulled, reapply or rely on the FetchContent path).
- Licence: AGPL-3.0 (JUCE open-source tier; NAM core is MIT; ASIO opt-in brings Steinberg terms).
