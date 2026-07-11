# Real-time safety contract

This document is a release gate, not a style preference. Code reachable from `Engine::processBlock()` or a node's `process()` must obey it. A fast operation on a developer machine is not necessarily bounded on a loaded Linux or Windows audio system.

## Callback invariants

The device callback must:

- complete within the device period under the supported worst-case patch;
- perform no heap allocation or deallocation;
- acquire no mutex, condition variable, file lock, message-manager lock, or contended spin lock;
- perform no file, console, socket, device-enumeration, model-loading, or operating-system UI I/O;
- create no threads and wait on no futures;
- call no JUCE UI object or listener that can reach the message thread;
- avoid exceptions, RTTI-dependent dispatch, locale conversion, and text formatting;
- use only prepared, bounded storage and bounded loops;
- produce finite, bounded output even when a node receives invalid state or samples;
- tolerate any block length from zero through the prepared maximum.

The same restrictions apply to constructors, destructors, reference-count releases, and callbacks invoked indirectly while the audio callback is on the stack. In particular, replacing the last `std::shared_ptr` owner can deallocate and is forbidden.

## Allowed communication

Cross-thread communication uses purpose-built bounded channels with one documented owner per endpoint:

| Direction | Mechanism | Overflow policy |
| --- | --- | --- |
| control to audio: prepared graph | SPSC publication/adoption slots | keep newest unpublished state on control side; never destroy on audio side |
| control to audio: parameter/events | fixed-capacity SPSC queue | coalesce continuous values; preserve critical note-off/reset capacity; count drops |
| audio to control: retired snapshots | fixed-capacity SPSC queue | defer adoption or retain retirement token; never delete in callback |
| audio to UI: meters/state | fixed-capacity SPSC/ring | drop visual data and increment counter |
| audio to UI: waveform | preallocated decimating ring | overwrite/drop samples by contract |

Atomics must be lock-free on supported targets for types used in the callback. Assert this during startup or replace them with a supported queue representation. Atomics solve visibility, not lifetime: every published object's owner and reclamation thread must be explicit.

The audio thread never blocks to guarantee delivery. If an event class cannot be safely dropped, reserve capacity for it and define a fail-safe state (for example, all-notes-off after a detected overflow).

## Preparation and memory

All of the following occur before publication:

- DSP object construction and `prepare` calls;
- buffer, delay-line, event, and scratch allocation for maximum block size;
- FFT/window/table construction;
- file parsing and validation;
- sample-rate conversion setup;
- neural model load, weight conversion, and warm-up;
- channel and port lookup construction;
- execution sorting and buffer lifetime planning;
- telemetry probe selection and ring sizing.

Use contiguous owning storage in the immutable snapshot and non-owning spans/views in `process`. Containers reached during processing are fixed-size: reserve is not an adequate guarantee if a later operation can exceed it. `std::vector::push_back`, map/set lookup that may mutate, `std::function` with an allocating target, string construction, and lazy static initialization are not allowed on the callback path.

DSP reset is also real-time code when JUCE may call it near processing. Reset clears existing memory and state; it does not resize or reconstruct resources.

## Immutable graph exchange

The message/worker side fully prepares a new `CompiledGraph` and publishes it with release semantics. At a block boundary, audio acquires the new pointer and switches generations. The old generation remains alive until the callback has stopped referencing it.

Reclamation must occur off the callback. The supported pattern is an epoch/RCU-style owner or an audio-to-control retirement queue whose capacity and queue-full state are tested. Do not use any of these shortcuts:

- deleting the old pointer immediately after exchange;
- relying on a `shared_ptr` destructor to happen on the message thread;
- locking the graph while the editor mutates it;
- mutating node lists, ports, buffers, or routing in place;
- publishing an object before every node has completed preparation.

When retirement capacity is unavailable, the engine keeps processing the current generation and defers the swap, or parks the old generation in preallocated retirement storage. Audio continuity wins over immediate editor feedback.

## Parameter and event rules

UI controls and external controllers enqueue values; they never write a DSP member concurrently. Each event has a target index/generation and a sample offset or engine time. Stale targets are discarded deterministically after a graph swap.

Continuous parameters declare smoothing. The control side may coalesce redundant unscheduled gestures, but the audio side owns the smoothing state. Boolean and stepped values use exact transitions. Expensive changes such as delay capacity, convolution data, device layout, or NAM model selection are prepare-and-swap operations, not parameters applied in `process`.

Audio-rate modulation is compiled as a buffer-producing route. Control-rate modulation uses prepared ramps or values. The callback must not discover which mode applies by inspecting editor state.

## DSP defensive rules

- Flush or suppress denormals for the callback and ensure recursive filters decay to exact zero where practical.
- Validate coefficients before publication and clamp runtime modulation to the descriptor's stable domain.
- Check external samples and feedback state for finite values at defined boundaries. On NaN/Inf, reset affected state and emit silence or a bounded fallback for that route.
- Clear output channels before any early return. Unconnected and inactive outputs are always zero.
- Make channel count and block length explicit arguments; never assume stereo or the nominal block size.
- Avoid unbounded iteration, convergence loops, recursion, and data-dependent allocation.
- Keep bypass click-free and deterministic. Crossfades use prepared state and bounded sample loops.
- Report algorithmic latency from prepared configuration. Do not hide extra buffering as a stability workaround.

### Feedback-specific requirements

No audio cycle exists without an explicit delay element. The compiled feedback route always retains its final DC/finite/ceiling guard regardless of user modulation. Guard coefficient ranges are validated before publication. A non-finite feedback sample resets the loop state instead of recirculating it.

The limiter is a final containment layer, not permission to emit unsafe level. Device open, graph replacement with changed routing, and feedback enable transitions ramp from silence. Tests must include maximum feedback gain and adversarial NaN/Inf input.

## Telemetry rules

Telemetry is lossy by design. Records contain numeric IDs and POD-like numeric payloads; names and formatted units are resolved on the message thread. No telemetry call may repaint, log, allocate, or wake the callback into a blocking system call.

Meters update accumulators while samples are already in cache and publish at a decimated cadence. Scopes copy only into a preallocated, bounded ring. The UI may request probes, but a changed probe plan becomes active via graph compilation. If the UI stalls, audio overwrites/drops telemetry and continues.

Callback timing instrumentation must use a monotonic, non-allocating clock supported by the target. Store raw ticks/counters; convert them to text later.

## Platform and build expectations

- Compile DSP code with floating-point behavior that preserves finite checks; do not enable unsafe fast-math globally.
- Build and test x86-64 Windows and Linux initially. Any SIMD path has a scalar fallback and handles unaligned tails.
- Do not perform first-use initialization in the first audible callback. Run a silent warm-up after preparation for code paths such as neural inference and FFT-based processing.
- Name and prioritize threads through supported JUCE/OS facilities where appropriate, but never assume priority can compensate for blocking or excessive DSP.
- Treat ASIO/JACK/ALSA/WASAPI callback shape and device-reported values as runtime facts. Test more than one block size and non-power-of-two callback lengths where the backend can produce them.

## Verification gates

Before a node or engine change is considered real-time safe:

1. Unit-test `prepare`, `reset`, variable block lengths, all supported channel layouts, bypass, and invalid parameter boundaries.
2. Run an allocation/deallocation trap around the callback in test builds. Include graph swaps and parameter/event bursts.
3. Exercise a worst-case graph for at least 30 minutes at 48 kHz/64 samples on representative Linux and Windows machines, then repeat at the smallest claimed stable setting.
4. Force UI stalls, rapid graph edits, telemetry overflow, event overflow, device restart, and snapshot retirement pressure. Audio must not deadlock or access freed memory.
5. Feed silence, impulses, full scale, subnormals, NaN, Inf, and feedback stress signals. Outputs must remain finite and guards must report activation.
6. Measure callback duration distribution and maximum, not only average CPU. Record hardware, backend, driver, sample rate, block size, graph, build mode, and underrun count.
7. Run thread/address/undefined-behavior sanitizers in non-real-time test configurations. Sanitizer timing is not a latency measurement, but it is useful for ownership and race defects.

No document can prove an unknown third-party library safe. Code from JUCE modules, NAMCore, or future dependencies used on the callback path must be audited and exercised under the same allocation and timing tests.

## Review checklist

For every callback-reachable change, reviewers should be able to answer yes to all of these:

- Is every object already constructed, sized, and warmed?
- Is runtime work bounded by prepared channels, samples, ports, or events?
- Can any operation allocate, deallocate, lock, log, format, or invoke user/UI code?
- Are cross-thread ownership and memory ordering explicit?
- Is overflow behavior bounded and observable?
- Are channel count and block length dynamic?
- Are coefficient ranges stable and outputs finite?
- Can telemetry disappear entirely without changing the sound?
- Has worst-case callback time been measured on both target operating systems?
