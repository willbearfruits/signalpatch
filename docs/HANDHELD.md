# A dedicated machine

Notes for turning a handheld or a small PC into a box that boots straight
into the rig. The example is an ASUS ROG Ally with a Zoom F4 as the
interface. None of this is needed to use SignalPatch on a desktop.

Linux is the easier choice for this: you decide what runs, and PipeWire or
JACK at 64 samples is reachable. Windows works too; notes at the end.

## Latency

At 48 kHz and 64 samples the software adds 2.7 ms for the round trip. The
interface's converters and USB add roughly 2 to 4 ms more, so expect 5 to
7 ms in total.

Two things tell you whether a patch fits:

- The header shows `DSP x% (pk y%)`. `pk` is the worst block. Under about
  60% at your buffer size is comfortable.
- `SIGNALPATCH_BENCH_NAM_DIR=~/models ./signalpatch_tests` prints average
  and worst cost per capture.

Measured on a desktop Zen 3 core at 64 samples: LSTM pedal captures cost 5
to 9% each, a "feather" WaveNet about 11% on average and 50% in its worst
block. So several LSTM captures in a chain are fine, and a WaveNet is
roughly one per patch. Measure again on the machine you will play on.

## Linux setup

1. A minimal Debian or Arch install with no desktop environment.
2. `pipewire pipewire-jack wireplumber cage greetd`.
3. Realtime priority. On Arch, `realtime-privileges` and the `realtime`
   group. Elsewhere, `/etc/security/limits.d/audio.conf`:
   ```
   @audio - rtprio 95
   @audio - memlock unlimited
   ```
   and add your user to `audio`. Avoid rtkit with PipeWire 1.6.8 (see
   `BUILDING.md`).
4. A fixed quantum, in `~/.config/pipewire/pipewire.conf.d/lowlatency.conf`:
   ```
   context.properties = {
       default.clock.rate = 48000
       default.clock.quantum = 64
       default.clock.min-quantum = 32
       default.clock.max-quantum = 64
   }
   ```
5. The performance CPU governor, and a high TDP profile while plugged in.
6. USB autosuspend off for the interface. `usbcore.autosuspend=-1` on the
   kernel command line is blunt but reliable.
7. Disable services the box does not need (bluetooth, cups, avahi).

## Booting into the rig

`/etc/greetd/config.toml`:

```toml
[initial_session]
command = "cage -- env PIPEWIRE_QUANTUM=64/48000 pw-jack SignalPatch --kiosk --board --unmute"
user = "USER"
```

`--unmute` skips the muted start. That is reasonable on a dedicated box and
a bad idea on a desktop with open microphones. With no patch argument the
last session comes back.

## The Zoom F4

Use its audio-interface mode. All six inputs and four outputs show up as
ports. Set gain on the F4's preamps and monitor from its outputs.

## Models

Captures go in `~/Documents/SignalPatch/models`. The neural modules step
through that folder with their arrow buttons, and each shows its own cost
as a share of the block.

## Windows

Pick "Windows Audio (Exclusive Mode)" and 64 or 128 samples under AUDIO.
An ASIO build (`-DSIGNALPATCH_ENABLE_ASIO=ON`, with the interface's ASIO
driver) is usually tighter. Use the High Performance power plan and turn
off Game Bar and the vendor's overlay and updater. A Task Scheduler entry
can start `SignalPatch.exe --kiosk --board --unmute` at logon.

## Afterwards

Once it works, stop updating it, and keep an image of the disk.
