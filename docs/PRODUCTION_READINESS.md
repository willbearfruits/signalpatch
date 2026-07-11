# Production readiness — status as of 2026-07-11 (hardening pass 2)

This document records what has been audited, what is verified by tests, and
what remains open against the gates in `REALTIME_SAFETY.md` and the MVP scope
in `PRODUCT.md`. It is a snapshot, not a certificate: items marked open are
open.

## What was audited in this pass

A full-source audit across seven dimensions (real-time safety, DSP
correctness for effects and instruments, concurrency/lifetime, persistence,
engine/device management, graph compiler), followed by fixes:

- **Drum machine port explosion (fixed).** Step parameters created 24 useless
  modulation input ports; `DspNode::addParameter` gained a `modulatable`
  flag and grid steps no longer create ports. `parameterValue` already
  tolerated portless parameters.
- **UI use-after-free risk (fixed).** Node components are rebuilt on every
  engine change broadcast; the node context menu's async callback captured a
  raw `this`. Now uses `Component::SafePointer`. File choosers are owned
  members (JUCE cancels their callbacks on destruction); canvas-level menus
  capture the app-lifetime canvas.
- **4-track varispeed record gaps (fixed).** At speed > 1 the write head
  skipped tape slots, leaving silent holes; recording now fills the whole
  span between consecutive write positions (bounded, ≤ 8 slots).
- **Autosave recovery (implemented).** Autosaves were written but never
  restored. On startup with no saved session content, the engine now restores
  the autosave muted (falls back to the default patch on parse/compile
  failure). This closes an MVP checklist item.
- **Mono synth envelope wart (fixed).** Dead `floorLevel` path removed;
  drone-at-HOLD behaviour is now explicit.

Verified by inspection (no change needed): fan-in mixing copies-then-adds (no
double summing) and scrubs non-finite samples; hardware output has a hard
±0.98 ceiling; bypass cannot be applied to Feedback Guards or hardware
endpoints, so bypass cannot create an unguarded cycle; NAM/script hot-swaps
publish through an atomic pointer with the previous object retained until the
next swap; feedback-guard delay/reset semantics are pinned by tests.

## Verification state vs. REALTIME_SAFETY.md gates

| Gate | State |
| --- | --- |
| Unit tests: prepare/reset, variable block lengths, bypass, invalid boundaries | Partial — 16 engine tests incl. all-kinds finite-output sweep with alternating block sizes; per-node parameter-boundary tests not exhaustive |
| Allocation/deallocation trap around the callback | **Closed** — global new/delete trap (incl. aligned forms) armed around a kitchen-sink graph (every node kind, guarded feedback, live NAM inference); zero allocations over 400 variable-size blocks ("callback path performs no allocation" test) |
| 30-minute worst-case soak at 48 kHz/64 | **Closed (offline)** — 1.69 M blocks (30 min of 48 kHz audio) rendered through the kitchen-sink graph under the allocation trap with finite outputs (`SIGNALPATCH_SOAK_BLOCKS=1687500`). A live-device wall-clock soak with xrun counting remains worthwhile before a stage gig |
| Stress: rapid edits/recompiles | Partial — "recompile churn keeps rendering" test (25 add/connect/compile/remove rounds with parameter bursts); live UI-stall stress not automated |
| Adversarial signals (NaN/Inf/full-scale/subnormal) | Partial — guard containment + all-kinds sweep tested; per-node NaN injection only for delay/guard |
| Callback duration measurement | **Closed** — worst-case callback ratio latched in the callback, displayed as "DSP x% (pk y%)" with slow decay |
| ASan/UBSan runs | **Closed** — full test suite passes under `-fsanitize=address,undefined -fno-sanitize-recover=all` (`build-asan/`) |
| TSan run | **Open** — cross-thread engine paths not yet exercised under TSan |

## NAM status vs. NAM_ROADMAP.md

Stages 0–2 are functionally in place (optional build boundary, model
load/prewarm off the audio thread, atomic swap, patch persistence, mono
enforcement, engine test loading a real WaveNet model). Model parsing now runs
on a worker thread (`juce::Thread::launch` + message-thread apply through a
weak node handle), so loading no longer freezes the UI; the node shows
"loading ..." until the swap. NAM inference is covered by the allocation trap.
Stage 3 (performance qualification: benchmark matrix across model classes and
buffer sizes, honest supported configurations) has **not** been run; heavy
models on small buffers remain unqualified.

## Known deliberate boundaries (not defects)

Mono effect nodes; no MIDI/OSC mapping; no undo/redo; no plug-in hosting; no
sample-accurate parallel-path latency compensation; sampler/4-track content is
not saved into patches; drum-machine grid steps are not modulatable.

## Build/platform

Linux build is warning-clean with JUCE recommended warnings. Windows path
(WASAPI/ASIO) compiles in CI-less theory only — **untested this pass**. The
NAM dependency builds from a local checkout with a 3-line guard patch
(documented in CLAUDE.md); re-pulling that clone requires reapplying it.
