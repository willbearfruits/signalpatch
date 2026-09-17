# What is tested

The rules are in `REALTIME_SAFETY.md`. This is where things stand against
them.

| Check | State |
| --- | --- |
| No allocation in the callback | Tested. A trap on `new` / `delete` around a graph with every module, guarded feedback, a live neural model and incoming MIDI notes. |
| 30 minutes of audio through that graph | Passes offline (`SIGNALPATCH_SOAK_BLOCKS=1687500`). A soak on a live device with xrun counting has not been automated. |
| ASan + UBSan | Clean over the whole suite (`build-asan/`). |
| TSan | Clean over the suite (`build-tsan/`). The app itself has not been run under TSan with a real device. |
| Every module renders finite output | Tested with changing block sizes. NaN injection is only tested for the delay and the Feedback Guard. |
| Rapid graph edits while rendering | Tested (add, connect, compile and remove in a loop with parameter bursts). |
| Callback timing | Measured in the app: average and worst block in the header. |
| Linux and Windows | CI builds the app and runs the suite on both, from a bare clone. |

The suite has 49 cases. Beyond the above they cover save and load, device
changes with missing channels, undo, the looper and 4-track at odd speeds,
recordings across sample rates, the clock, MIDI, tuning of the pluck and
synth, the TONE3000 client, and the inspector's ranges and curves.

## Not verified

- Windows with real audio hardware. It has run on one ROG Ally; nothing was
  measured there.
- The gamepad code on a real pad.
- Touch mode on a real touch screen.
- The Flatpak manifest since the app moved to GLFW.
- Heavy WaveNet captures at 64 samples on anything slower than a desktop
  Zen 3 core. `SIGNALPATCH_BENCH_NAM_DIR` measures a folder of models.

## Audits

Twice so far the code has been read end to end for real-time safety, DSP
errors, thread lifetime, persistence, device handling and undo. The second
pass (September 2026, over the code added since v0.2.0) turned up data loss
in recordings, a crash on quit, several MIDI bugs and a looper undo that
could stall the callback. The fixes are listed under v0.3.1 in
`CHANGELOG.md`.

One lesson worth keeping: the Windows CI found a use-after-realloc that
Linux and ASan never showed, because MSVC grows vectors by 1.5x and
libstdc++ doubles them. Do not hold pointers into
`PatchDocument::getNodes()` across a mutation.
