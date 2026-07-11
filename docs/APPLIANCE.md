# The appliance build: a handheld that is only SignalPatch

Target rig: **ASUS ROG Ally (Z1 Extreme) + Zoom F4** as a dedicated guitar
patching studio, voice processor and synth. This document is the concrete
build guide. Short answer to "Linux or Windows?": **Linux, stripped down**,
for three reasons — you control the scheduler and audio stack completely, the
machine does nothing else (no updaters, overlays or game bars stealing the
CPU mid-song), and JACK/PipeWire at a 64-sample quantum is reliably
achievable. Windows works (notes at the end) but is the fallback, not the
recommendation.

## Latency reality check

"No latency" budget = converters + USB + 2× buffer. At 48 kHz / 64 samples
the software adds 2.7 ms; the F4's converters and USB framing add roughly
2–4 ms more. Total round trip in the 5–7 ms range — tighter than standing
two metres from your amp. 32 samples halves the software share when the
patch's worst case allows it.

The two tools that tell you whether a patch fits the deadline:

- The header readout **`DSP x% (pk y%)`** — `pk` is the worst-case block. If
  `pk` stays under ~60% at your buffer size, the patch will not glitch.
- The model benchmark:
  `SIGNALPATCH_BENCH_NAM_DIR=~/nam-models ./signalpatch_tests` prints
  avg/worst cost per model with an OK / TIGHT / OVER verdict.

Measured on a desktop Zen3 core at 64/48k (the Ally's Z1E is comparable):
Boss LSTM captures 5–9% average, feather WaveNet ~11% average / ~50% worst
case. Rule of thumb: **stack LSTM pedal captures freely (3–5 in a chain),
budget WaveNets one per patch**, and re-run the benchmark on the Ally itself.

## Base system (Linux)

1. **Distro**: minimal Debian (netinst, "standard system utilities" only) or
   Arch. No desktop environment — the app is the desktop.
2. **Packages**: `pipewire pipewire-jack wireplumber cage greetd` (or plain
   Xorg + `xinit` if you prefer X11), plus the build dependencies from the
   README if compiling on-device.
3. **Realtime privileges** — `/etc/security/limits.d/audio.conf`:
   ```
   @audio - rtprio 95
   @audio - memlock unlimited
   ```
   Add your user to the `audio` group.
4. **PipeWire quantum** — `~/.config/pipewire/pipewire.conf.d/lowlatency.conf`:
   ```
   context.properties = {
       default.clock.rate = 48000
       default.clock.quantum = 64
       default.clock.min-quantum = 32
       default.clock.max-quantum = 64
   }
   ```
5. **CPU**: performance governor
   (`cpupower frequency-set -g performance` via a systemd unit), and on the
   Ally set the TDP profile high while plugged in.
6. **USB**: disable autosuspend for the F4
   (`usbcore.autosuspend=-1` on the kernel cmdline is the blunt, reliable
   option for a dedicated box).
7. **Silence the box**: `systemctl disable --now bluetooth cups avahi-daemon`
   and anything else the machine doesn't need to be an instrument.

## Boot-to-instrument

greetd config (`/etc/greetd/config.toml`) starting a cage kiosk as your user:

```toml
[initial_session]
command = "cage -- env PIPEWIRE_LATENCY=64/48000 SignalPatch --kiosk --unmute /home/USER/live.signalpatch"
user = "USER"
```

- `--kiosk` = fullscreen, no window chrome.
- `--unmute` = skips the restore-muted safety pause. This is the explicit
  opt-in for a dedicated machine; leave it off on a desktop.
- The patch argument is optional — without it the last session is restored.

Result: power button → your patch, sounding, in well under a minute. The
Zoom F4 is picked automatically on first boot (device preference), and the
choice persists.

## Zoom F4 notes

- Use the F4's **audio-interface (pro audio) mode**; SignalPatch sees all six
  inputs and four returns as individual patch ports.
- Set input gain on the F4's preamps; keep SignalPatch's input trims near 0
  and do creative gain staging in the graph.
- Monitor through the F4 headphone out or its main outs — that is the
  low-latency path being measured.

## Models on the appliance

Put your `.nam` captures in `~/Documents/SignalPatch/models` (or point
`SIGNALPATCH_MODELS_DIR` anywhere). The Neural Amp and Neural Pedal modules
step through that folder with their ◀ ▶ buttons — a pedal library on two
buttons, which maps directly onto handheld shoulder keys later (roadmap 0.6).
Each neural module shows its own cost ("- x% of block") so you can see which
capture is eating the budget while you play.

## Windows fallback (the Ally's native OS)

Works, with caveats: install Zoom's ASIO driver for the F-series, run
SignalPatch with `-DSIGNALPATCH_ENABLE_ASIO=ON` built in, pick the ASIO
backend and 64 samples in AUDIO SETUP. Then fight the OS: High Performance
power plan, disable Game Bar/Game Mode overlays, Armoury Crate silent
updates, and scheduled maintenance. A Task Scheduler entry can launch
`SignalPatch.exe --kiosk --unmute patch.signalpatch` at logon. Expect a few
ms more jitter than the Linux build and re-run the benchmark to confirm your
chains still fit.

## Freeze it

Once the rig works, stop updating it. It is an instrument now: no unattended
upgrades, no new kernels before a gig, and keep a known-good SD/USB image of
the whole system so the appliance can be re-flashed in minutes.
