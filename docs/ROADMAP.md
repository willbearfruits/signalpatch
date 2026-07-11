# SignalPatch roadmap

The single ordered plan for the project. Detail lives in the referenced docs:
`PRODUCT.md` (product spec), `ARCHITECTURE.md` (engine design),
`NAM_ROADMAP.md` (neural amp stages), `PRODUCTION_READINESS.md` (verification
gates). This file says what comes next and why.

## Where we are (0.1, shipped)

35 node kinds across utility / effects / voice / instruments / dynamics /
control; working Neural Amp Modeler node; rack UI with stomp bypass,
right-click patching and live signal-flow cables; guarded-feedback-only graph
engine with block-boundary snapshot swaps; Zoom F4 first-launch preference;
autosave recovery; patch files with per-node extra state. Verified: zero
allocations on the render path (trap test), 30-minutes-of-audio worst-case
soak, ASan+UBSan clean, 16 engine tests.

Deliberate boundaries today: mono effect nodes, no MIDI/OSC, no undo/redo, no
plug-in hosting, sampler/4-track audio not saved with patches.

---

## 0.2 — Play it like an instrument

The biggest missing MVP pillar is external control. Everything here rides the
architecture's existing event-queue design (ARCHITECTURE.md "Parameters and
modulation"): external input becomes timestamped engine events on bounded
queues, never touching the callback directly.

- **MIDI input device handling** in PatchEngine (JUCE MidiInput on its own
  thread, fixed-capacity SPSC queue into the callback, note-off capacity
  reserved per REALTIME_SAFETY).
- **MIDI Note node** (control outs: pitch, gate, velocity) so the mono synth
  and pluck become playable; drum machine and sequencer accept MIDI clock or
  keep their internal clock.
- **MIDI learn on mod sockets**: right-click a diamond port → "learn CC";
  mapping persisted in the patch.
- **Undo/redo** on PatchDocument (command pattern over add/remove
  node/connection, parameter gestures coalesced) — the document was designed
  for this and the UI already funnels every mutation through PatchEngine.
- **Node niceties**: rename in inspector, duplicate node, per-kind presets
  (JSON snippets of the parameter block).

Exit: play the synth from a keyboard, twist a hardware knob into any mod
socket, and undo a mistaken cable mid-performance without an audio glitch.

## 0.3 — Stereo and the studio loop

- **Stereo channel policy** per port (ARCHITECTURE.md already specifies
  fixed/match-upstream/device-derived widths; the compiler and buffers must
  learn multi-channel ports). Start with stereo variants of delay, reverb,
  chorus and a Pan node; keep mono nodes valid forever.
- **Split / merge / pan utilities** (mono→stereo, stereo→mono, L/R split).
- **Master recorder**: bounce the hardware-output bus to WAV on a writer
  thread (lock-free FIFO out of the callback) — the 4-track spirit, but for
  the whole patch.
- **Sampler / 4-track persistence**: save takes as WAV sidecars next to the
  patch (`<patch>.assets/`), load them back with the patch. Patches stop
  being silent shells of recorded work.

Exit: a stereo patch that records itself, saves, reloads with its audio, and
sums correctly.

## 0.4 — NAM qualification and tone tools

NAM_ROADMAP Stage 3 and the useful half of Stage 4:

- **Performance qualification**: benchmark matrix (model class × buffer size
  × sample rate) using the peak-DSP instrumentation; publish supported
  configurations; warn in the node UI when a model class can't meet the
  current buffer deadline.
- **Cabinet/IR node** (convolution via partitioned FFT, impulse loaded like a
  NAM model through extra state + worker thread).
- **Model browser**: scan a models directory, quick-switch with crossfade,
  remember favourites.
- **Dual-mono NAM wrapper** once stereo lands (explicit CPU accounting).

Exit: honest "this model works at this buffer size on this machine" claims,
and an amp+cab chain that rivals the LV2 plugin rig.

## 0.5 — Engine maturity and shipping

The remaining ARCHITECTURE.md gates plus distribution:

- **Worker-thread graph compilation** (edits never stall the message thread
  on big patches).
- **Latency compensation** for parallel paths (the compiler already computes
  per-node latency; insert compensation delays where branches re-join).
- **Probe subscriptions** (telemetry only for visible scopes — matters as
  patches grow).
- **TSan pass** over engine cross-thread paths; **live-device wall-clock
  soak** with xrun logging (PRODUCTION_READINESS open items).
- **Windows build** brought up and smoke-tested (WASAPI/ASIO), CI for both
  platforms running the test suite + sanitizers.
- **Packaging**: version stamping, .desktop entry + icon, AppImage or deb;
  choose and declare the project licence (required before any distribution —
  JUCE AGPLv3 vs commercial decision).

Exit: a stranger can download a build, and a soak on stage hardware produces
a clean xrun log.

## Beyond (unscheduled, deliberately)

- Plug-in target (VST3/CLAP) hosting the same engine — the standalone-first
  rule in ARCHITECTURE.md keeps this possible, not scheduled.
- OSC control surface (phone/tablet patch control; pairs with the existing
  OSC-based projects elsewhere in this workspace).
- Patch A/B morphing, scene snapshots for live sets.
- Third-party node SDK — only after the node contract survives 0.2–0.5
  unchanged (ARCHITECTURE.md "Node SDK contract").

## Ordering rationale

Control (0.2) precedes stereo (0.3) because a playable mono rig makes music
today, while stereo without playability is still furniture. NAM qualification
(0.4) waits for the recorder/persistence loop so benchmarks reflect real
sessions. Engine maturity (0.5) lands last among the numbered phases because
its items are invisible until patches get large — but TSan/CI inside it
should be pulled earlier if any 0.2 concurrency work feels risky.
