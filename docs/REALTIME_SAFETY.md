# Rules for code that runs in the audio callback

This covers `RenderPlan::render`, every node's `processDsp`, and anything
they call, including constructors and destructors that run while the
callback is on the stack.

## The callback must not

- allocate or free memory. Dropping the last `shared_ptr` to something
  counts.
- take a mutex or wait on anything
- touch files, sockets, the console, or a UI object
- throw, format text, or build strings
- loop without a bound known at prepare time

It must produce finite output whatever it is fed, accept any block length
from zero to the prepared maximum, and leave unconnected outputs at zero.

## What goes where

Everything expensive happens before a plan is published: constructing DSP
objects, sizing buffers and delay lines, FFT setup, parsing files, loading
and warming a neural model, sorting the graph. `resetDsp` clears state and
never resizes.

Threads talk through:

| From, to | How |
| --- | --- |
| message thread to callback: a new plan | atomic pointer, taken at a block boundary |
| callback to message thread: the old plan | retired list, deleted on the engine timer |
| knobs, bypass, transport commands | atomics the node reads |
| MIDI notes | fixed-size lock-free FIFO; if it overflows, all notes are released |
| callback to helper threads: a block's nodes | atomics per node (sources left, state) and a futex wake; helpers have realtime priority one below the callback, or are not used |
| meters and scopes | atomics the UI samples; losing a frame of them is fine |

Atomics handle visibility, not lifetime. Anything the callback can see has
an owner on the message thread that outlives it.

## Parameters

A knob read once per sample uses `parameterValue`, which steps a smoother.
A value read once per block uses `parameterTarget`; stepping the smoother
once per block would take seconds to arrive. Changes that need new memory
(a model, an impulse, a longer delay) are prepared off the callback and
swapped in.

## DSP habits

- Clamp modulated values to the range the algorithm is stable in.
- Check for NaN and Inf at node boundaries and in feedback state; reset and
  output silence rather than pass them on.
- No `-ffast-math`. It breaks the finite checks.
- Report latency honestly. Do not add hidden buffering to cover a slow node.

## How it is checked

- The test binary replaces global `new` and `delete`. A test arms a trap
  around `render` on a graph with every module, guarded feedback and a live
  neural model, with MIDI notes arriving, and fails on any allocation.
- The same graph runs for 30 minutes of audio with
  `SIGNALPATCH_SOAK_BLOCKS=1687500`.
- The suite runs under ASan, UBSan and TSan.
- The app shows callback load as average and worst block: `DSP x% (pk y%)`.

Code from JUCE or NeuralAmpModelerCore that ends up in the callback gets no
exemption. It goes through the same trap.

## Before merging callback code

- Is everything it uses already built and sized?
- Is the work bounded by channels, samples or ports?
- Could any line allocate, free, lock, log or call into the UI?
- Who owns each object it sees, and which thread frees it?
- Does it cope with a block of 1 sample and one of the maximum size?
