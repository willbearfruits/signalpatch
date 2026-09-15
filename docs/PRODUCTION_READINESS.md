# Production readiness — status as of 2026-09-15 (hardening pass 3)

This document records what has been audited, what is verified by tests, and
what remains open against the gates in `REALTIME_SAFETY.md` and the MVP scope
in `PRODUCT.md`. It is a snapshot, not a certificate: items marked open are
open.

## Pass 3 — 2026-09-15, the code added since v0.2.0

About 11,000 lines landed between v0.2.0 and v0.3.0 (the GLFW app, Board,
looper, 4-track rewrite, MIDI and controller feedback, stereo nodes, Clock,
tone stack, TONE3000). A seven-dimension audit ran over it — real-time
safety, DSP (effects), DSP (instruments and transports), concurrency and
lifetime, persistence and compatibility, engine/devices/MIDI, graph/undo/UI
integrity — one reader per dimension, then a skeptic per dimension
re-reading the code to refute each finding. 90 findings survived (3
critical, 26 high, 42 medium, 19 low, with heavy overlap between
dimensions); every critical and high one is fixed, as are most mediums.

**Fixed** (commits 37bcf19, 1dc2310 and the follow-up):

- *Data loss:* an overdub stopped with PLAY, or a 4-track take ended by
  PLAY or an arm toggle, never reached the saved WAV; recording did not mark
  the patch unsaved (autosave and the quit prompt ignored it); autosave kept
  the previous patch's loop for a reused node id; WAVs were deleted before
  being rewritten; a slot glide left Save pointed at the previous slot's
  file; storing a slot cleared the open patch's unsaved flag; re-opening a
  project zip overwrote the extracted project; New/Open discarded edits
  without asking; undoing a delete lost the module's MIDI bindings and
  group membership; loops saved at another sample rate played at the wrong
  speed.
- *Crashes and races:* NanoVG freed before the rack on quit; the TONE3000
  worker reassigned the page the frame loop was drawing (and wrote tokens
  concurrently); a MIDI slot change during a knob drag or with a knob menu
  open threw `out_of_range`; downloads and file-browser callbacks could
  land in whatever module reused an id.
- *Real-time:* looper undo copied up to 2.88 M samples in one callback
  (~12 ms) — now bounded per block.
- *Wrong sound:* velocity-0 note-ons treated as presses (stuck MIDI Note
  gates, commands firing once, bypass toggling back on release); 4-track
  monitoring doubled the input on armed tracks, and SYNC with track 1
  stopped stuttered the others; looper overdub added the input twice at
  half speed, left gaps at 200 % and undid the wrong slots in reverse;
  bypassed stereo nodes dropped the right side; "Sum inputs" switched gain
  per sample; block-rate knobs (tone stack, cabinet cuts, looper tempo)
  took seconds to respond; the Neural Pedal's Mix used the driven signal as
  "dry"; MIDI Note's default base played A2 for middle C; the tuner could not
  see a bass E at 96 kHz; slow clocks dropped followers to internal time.
- *Control:* group footswitches did nothing in Rack mode; program changes
  on a bypass did nothing and on a command fired once; the same MIDI
  message ran through the new rig's bindings after a slot change; learned
  bindings ignored the channel; RESET LOOP bindings did nothing; a queued
  looper command could not be cancelled; Clock RUN resumed mid-beat; lost
  note-offs (queue full or audio stopped) now release every note.

**Still open from pass 3** (medium/low, reachable but not stage-critical):

- Changing the device sample rate clears recorded looper and 4-track audio
  (resampling on load exists; a live rate change does not preserve it).
- Clock RESET does not realign drum machine and sequencer steps (they stay
  on their step, just re-timed).
- A replugged MIDI controller may not be reopened, and SysEx feedback can
  keep targeting the dead output.
- When every preferred audio device is rejected the app ends offline and
  saves that choice.
- Slot glides: undo records map/group changes but not the glided knob
  values; properties the target slot omits are not reset; an undo during a
  multi-module drag can split the gesture.
- Stereo Chorus sweep flattens at very short delay and high depth; looper
  Bars ignores a connected Clock's tempo; pluck high notes are slightly
  sharp (integer delay length); the tuner reads its ring buffer without a
  lock (benign torn read of floats).
- A compound move gesture stays open if a drag is abandoned outside the
  window.

New tests from this pass: looper per-slot overdub and exact undo at half,
reverse and 200 %; 4-track sync with track 1 stopped, single monitoring and
take versions; stereo bypass; undo restoring MIDI bindings and groups; a
take saved at 48 kHz reopened at 96 kHz. 44 tests.

## Pass 2 — 2026-07-11

### What was audited in this pass

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
| 30-minute worst-case soak at 48 kHz/64 | **Closed (offline)** — 1.69 M blocks (30 min of 48 kHz audio) rendered through the kitchen-sink graph under the allocation trap with finite outputs (`SIGNALPATCH_SOAK_BLOCKS=1687500`; last run 2026-09-15 with the looper, tuner, stereo nodes and MIDI Note in the graph and notes dispatched every fifth block). A live-device wall-clock soak with xrun counting remains worthwhile before a stage gig |
| Stress: rapid edits/recompiles | Partial — "recompile churn keeps rendering" test (25 add/connect/compile/remove rounds with parameter bursts); live UI-stall stress not automated |
| Adversarial signals (NaN/Inf/full-scale/subnormal) | Partial — guard containment + all-kinds sweep tested; per-node NaN injection only for delay/guard |
| Callback duration measurement | **Closed** — worst-case callback ratio latched in the callback, displayed as "DSP x% (pk y%)" with slow decay |
| ASan/UBSan runs | **Closed** — full test suite passes under `-fsanitize=address,undefined -fno-sanitize-recover=all` (`build-asan/`; last run 2026-09-15 over 34 tests incl. looper, MIDI, controller feedback, compound undo; re-run pending for the pass-3 fixes) |
| TSan run | **Closed** for the test suite — `build-tsan/` (`-fsanitize=thread`, headless modules) runs all 34 tests with zero reports (2026-09-15). The live app (audio callback vs. message thread under a real device) is still not TSan-instrumented. |

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

Linux build is warning-clean with JUCE recommended warnings. **CI runs both
platforms on every push**: Linux (GCC) and Windows (MSVC) build from a bare
clone — JUCE and NeuralAmpModelerCore fetched automatically, the NAM guard
patch applied from `packaging/patches/` — and pass the full test suite
including the allocation trap. The Windows bring-up caught one real bug: a
use-after-realloc in a test that only manifested under MSVC's 1.5x vector
growth (libstdc++'s doubling left spare capacity, so Linux and ASan never
saw it — a good reminder that green ASan is not a proof). Windows remains
untested against real audio hardware (WASAPI/ASIO runtime behaviour is
roadmap 0.5).
