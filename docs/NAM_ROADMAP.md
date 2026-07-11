# Neural Amp Modeler integration roadmap

Neural Amp Modeler support is optional. SignalPatch must remain a complete patchable processor when NAM support is not built or no model is installed. The integration target is NAMCore behind a narrow adapter, not NAM-specific assumptions spread through the graph engine.

Neural inference cannot be literally latencyless. The goal is to add no extra buffering beyond NAMCore/model requirements and the current device callback, while keeping worst-case compute below the callback deadline. A model's algorithmic behavior, CPU cost, device driver, converters, and chosen block size all affect the experienced latency.

## Product boundary

The initial `NAM Amp` node is:

- one mono audio input and one mono audio output;
- a prepared model instance plus input gain, output gain, wet/dry, bypass, and output safety trim;
- model identity and load status in saved node state;
- meters and decimated pre/post waveform telemetry;
- explicit reported algorithmic latency, including any adapter/resampler delay;
- unavailable-but-preserved when SignalPatch is built without NAM support or a saved model cannot be loaded.

Stereo begins as two explicit mono instances or a later stereo wrapper with documented CPU cost. SignalPatch must not silently process only the left channel. Model selection is asynchronous structural state, not an audio-rate parameter. Gain, mix, and bypass follow the normal parameter/modulation contract; arbitrary neural weights are not exposed as patch parameters.

## Build and dependency boundary

Add NAM support behind a CMake option such as `SIGNALPATCH_ENABLE_NAM`, defaulting off until CI covers it. A small adapter library owns all NAMCore headers and translates between SignalPatch's prepared-node contract and the pinned NAMCore API. The rest of the engine sees only an interface similar to:

```cpp
struct NamPreparedModel;

struct NamAdapter {
    static LoadResult loadAndPrepare(const ModelPath&,
                                     double sampleRate,
                                     int maximumBlockSize);
    static void process(NamPreparedModel&, AudioSpan mono) noexcept;
    static void reset(NamPreparedModel&) noexcept;
    static int latencySamples(const NamPreparedModel&) noexcept;
};
```

The exact API should follow the pinned NAMCore revision rather than this illustrative shape. Pin a known revision, record its license and notices in distribution artifacts, and verify redistribution obligations before enabling release packages. Do not download code or models implicitly at runtime. Model files remain user-provided unless a separately reviewed model pack is shipped.

Linux and Windows CI must build both NAM-disabled and NAM-enabled variants. Keep compiler flags compatible with the rest of the DSP; any optimization or SIMD flags needed by NAM are target-scoped rather than global. A failed optional build must not weaken the non-NAM processor.

## Runtime lifecycle

Loading never occurs in the audio callback:

1. The user chooses a model path on the message thread.
2. A worker reads and parses the file, validates supported architecture/metadata, constructs weights and workspaces, and prepares for the current sample rate and maximum block size.
3. The worker runs silent warm-up blocks while allocation trapping/logging is enabled in development builds.
4. The graph compiler constructs a new immutable snapshot containing the prepared model.
5. At a block boundary the engine adopts it and crossfades from the previous node state.
6. The old model is retired and destroyed on a non-real-time thread.

Cancellation and rapid repeated model choices are generation-based: only the newest completed request is publishable. A failure leaves the sounding model in place and returns a diagnostic. Saved paths are canonicalized for display/load but patches also retain model metadata/hash so a same-named replacement can be detected. Patch portability can later add an explicit asset bundle; it should not copy large models without consent.

Device sample-rate or maximum-block-size changes require re-preparation. If a model or NAMCore revision expects a particular rate, the adapter must either reject the mismatch with a clear message or use a prepared, measured resampler. Resampling cost and group delay are reported. Never construct a resampler in the first callback and never pretend a mismatched rate is harmless.

## Low-latency strategy

The preferred path processes NAM synchronously on the device audio thread using the current block. It does not create a neural worker thread, queue audio one block ahead, or accumulate a larger inference batch. Those approaches add latency and scheduling risk.

For each supported CPU/backend combination:

- reuse NAMCore's prepared input/output storage where possible and avoid format copies;
- process variable callback lengths up to the prepared maximum, splitting only if the pinned API requires it and without adding a full-block delay;
- warm instruction/data paths before unmuting;
- suppress denormals and verify finite output at the node boundary;
- keep optional oversampling off by default and account for its filters' delay;
- benchmark release builds with representative light, typical, and heavy models at 44.1, 48, and 96 kHz where those combinations are supported;
- publish a supported block/model budget based on high-percentile and maximum callback time, not average inference throughput.

If a model cannot finish with comfortable headroom at the requested device period, show that fact and offer a larger device block, a lighter model, or NAM bypass. SignalPatch must not silently add hidden blocks. A practical acceptance target is total worst-case graph processing below roughly 70% of the device period on the documented reference machine, leaving room for driver jitter and UI/OS activity; this is an engineering margin, not a universal guarantee.

Measure round-trip latency with a loopback when making user-facing claims. Device-reported latency plus node-reported algorithmic latency is useful telemetry, but it is not a substitute for end-to-end measurement.

## Safety and controls

NAM output passes through the same finite-value policy as every built-in node. The node resets/mutes its local model state on non-finite output and increments telemetry. The default preset places a limiter at the final output, but a NAM node is not itself a feedback safety boundary.

Externally controllable parameters in the first version:

- input gain with smoothing;
- output gain with smoothing;
- wet/dry with an equal-power or documented mix law;
- click-free bypass.

Model path/selection, architecture, and sample-rate preparation are non-real-time properties changed by prepare-and-swap. Future tone-stack, gate, sag, cabinet IR, and oversampling controls should be ordinary explicit nodes or clearly separated stages, not presented as learned parameters unless the model format actually supports them. This keeps automation behavior deterministic and makes cable visualization honest.

## Telemetry and UI states

The node exposes bounded numeric telemetry only:

- input/output peak and RMS;
- decimated pre/post waveform;
- callback time for the node and recent maximum;
- non-finite/reset and overload counters;
- model status generation (`empty`, `loading`, `ready`, `failed`, `rate mismatch`);
- prepared model sample rate and reported latency.

File paths, model names, error strings, hashes, and formatting stay on the control/UI side. The editor should display model name, status, sample rate compatibility, measured node CPU, and latency. Cable animation uses the normal graph probes, so NAM does not create a second visualization system.

## Delivery stages

### Stage 0: prove the boundary

- Freeze the generic node preparation/snapshot contract and real-time allocation tests.
- Add a placeholder optional node that prepares a CPU workload off-thread and swaps state safely.
- Verify absent optional nodes survive save/load as placeholders.
- Decide and document the pinned NAMCore revision and licensing/notice requirements.

Exit condition: optional heavy prepared state can be loaded, swapped, retired, and reloaded during audio without allocation, use-after-free, or discontinuity beyond the specified crossfade.

### Stage 1: offline adapter

- Build the adapter on Linux and Windows.
- Load supported models and run deterministic impulse/reference vectors outside the device callback.
- Establish model metadata, hash, errors, and sample-rate behavior.
- Compare adapter output with the pinned upstream reference within a documented numeric tolerance.

Exit condition: a command-line/unit-test path produces reference-matching finite output for the supported model set on both platforms.

### Stage 2: real-time mono node

- Integrate prepared mono inference in the graph schedule.
- Add gain/mix/bypass smoothing, reset, telemetry, crossfaded model swap, and state persistence.
- Exercise variable block sizes, device restart, rapid load cancellation, missing models, corrupt files, and non-finite input.
- Run callback allocation/deallocation traps.

Exit condition: no callback allocations or model destruction, bounded behavior under fault tests, and glitch-free swaps on representative ASIO, JACK/ALSA, and WASAPI paths.

### Stage 3: performance qualification

- Create a reproducible benchmark matrix by CPU, OS, backend, rate, block size, graph load, and model class.
- Record median, high percentile, and maximum callback time plus xruns over long runs.
- Measure end-to-end loopback latency.
- Tune copies/SIMD/compiler settings only with output-equivalence tests.

Exit condition: publish honest supported configurations and refuse or warn on combinations known to miss deadlines. "Low latency" claims include model, machine, rate, buffer, backend, and measured result.

### Stage 4: usability extensions

- Add explicit dual-mono/stereo composition with clear CPU accounting.
- Add portable asset relinking/bundling and model library search off the callback.
- Consider quality modes or oversampling only after latency reporting is complete.
- Add optional cabinet/IR and tone stages as normal graph nodes.

Exit condition: extensions preserve the same real-time contract and do not make the base NAM-disabled build depend on NAMCore.

## Non-goals for the first integration

- Training or editing NAM models.
- Downloading community models from inside the audio callback or without explicit user action.
- Running inference on a remote service.
- Hiding an inference worker buffer behind a "zero latency" label.
- Morphing between arbitrary model weight sets at audio rate.
- Claiming that every NAM model performs equally or supports every device sample rate.

The simplest acceptable NAM release is a robust mono node with honest load/CPU/latency reporting. More features are useful only after that path survives the same real-time gates as the built-in effects.
