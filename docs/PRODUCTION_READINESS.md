# Production readiness — status as of 2026-07-11

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
| Unit tests: prepare/reset, variable block lengths, bypass, invalid boundaries | Partial — 14 engine tests incl. all-kinds finite-output sweep with alternating 17/64-sample blocks; per-node parameter-boundary tests not exhaustive |
| Allocation/deallocation trap around the callback | **Open** — no trap harness yet |
| 30-minute worst-case soak at 48 kHz/64 on target machines | **Open** |
| Stress: UI stalls, rapid edits, overflow, device restart | Partial — exercised manually, not automated |
| Adversarial signals (NaN/Inf/full-scale/subnormal) | Partial — guard containment + all-kinds sweep tested; per-node NaN injection only for delay/guard |
| Callback duration distribution measurement | **Open** — only average CPU load is displayed |
| TSan/ASan/UBSan runs | **Open** |

## NAM status vs. NAM_ROADMAP.md

Stages 0–2 are functionally in place (optional build boundary, model
load/prewarm off the audio thread, atomic swap, patch persistence, mono
enforcement, engine test loading a real WaveNet model). Stage 3 (performance
qualification: benchmark matrix, callback-time percentiles, honest supported
configurations) has **not** been run; heavy models on small buffers are
unqualified. Model load currently runs synchronously on the message thread
(UI freeze of ~0.1–1 s per load); the roadmap's worker-thread staging is still
to do.

## Known deliberate boundaries (not defects)

Mono effect nodes; no MIDI/OSC mapping; no undo/redo; no plug-in hosting; no
sample-accurate parallel-path latency compensation; sampler/4-track content is
not saved into patches; drum-machine grid steps are not modulatable.

## Build/platform

Linux build is warning-clean with JUCE recommended warnings. Windows path
(WASAPI/ASIO) compiles in CI-less theory only — **untested this pass**. The
NAM dependency builds from a local checkout with a 3-line guard patch
(documented in CLAUDE.md); re-pulling that clone requires reapplying it.
