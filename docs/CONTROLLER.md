# The SignalPatch foot controller — protocol

A purpose-built controller for the Board. The firmware lives in its own
repository; this document is the contract between it and SignalPatch, so any
class-compliant MIDI device that follows it works, and the controller also
works with anything else that speaks MIDI.

## Principles

- **Plain MIDI in.** Footswitches, expression pedals and encoders arrive as
  ordinary CC / note / program-change messages. SignalPatch binds them with
  MIDI learn like any controller; nothing on the input side is proprietary.
- **Feedback out over SysEx.** SignalPatch tells the controller what each
  switch currently means (LED state, a short label), so the pedal's display
  matches the Board. A controller that ignores SysEx still works blind.
- **USB MIDI, class compliant.** ESP32-S3 with TinyUSB. No drivers on Linux,
  Windows or the Ally.

## Input (controller → SignalPatch)

| Control            | Message                        | Notes                                  |
| ------------------ | ------------------------------ | -------------------------------------- |
| Footswitch n (1-8) | Note On 60+n-1 (press), Note Off (release), channel 10 | Momentary. SignalPatch toggles a stomp on Note On, fires a command on the rising edge, loads a slot on Note On. |
| Bank / slot buttons| Program Change 0-4             | Direct slot select.                    |
| Expression 1 / 2   | CC 11 / CC 4                   | 0-127, 7-bit is enough for a pedal. "Calibrate the pedal" on the knob's menu learns the heel/toe span (saved with the mapping; swapped ends invert). |
| Encoder 1-4        | CC 20-23, relative (64 ± delta)| Learn the knob with "MIDI learn as relative encoder": each detent nudges it 1/128 of its range, n > 1 for fast spins. |
| Tap tempo          | Note On 48, channel 10         | Mapped like any footswitch: the drum machine's TAP button (command "tap", four taps averaged) or the looper's REC. |

Channel 10 keeps the controller's notes away from anything driving a synth.

## Feedback (SignalPatch → controller)

SysEx, manufacturer id `0x7D` (non-commercial), device id `0x53` ('S').

```
F0 7D 53 <command> <payload...> F7
```

| command | payload                                       | meaning |
| ------- | --------------------------------------------- | ------- |
| 0x01    | switch (0-7), state (0 off / 1 on / 2 blink)  | LED for a footswitch |
| 0x02    | switch (0-7), label as 7-bit ASCII, up to 8 chars | text under a switch |
| 0x03    | slot (0-4), active (0/1)                      | which rig slot is live |
| 0x04    | name as 7-bit ASCII, up to 16 chars           | current rig name for the display |
| 0x7F    | side (0x00 controller, 0x01 SignalPatch)      | hello |

The controller sends `F0 7D 53 7F 00 F7` on its input port when it boots
(and whenever it wants the state again). SignalPatch answers on the output
port with the same name as that input: `F0 7D 53 7F 01 F7`, then the whole
state (eight LED + label pairs, five slot messages, the rig name). After
that it sends only what changed, checked ten times a second: a stomp,
a looper going into record (LED 2 = blink), a slot change, a rename. Labels
are the pedal's name (8 chars), `REC <pedal>`, `RIG n` or the group's name.
The side byte is what keeps a looped-back port (ALSA's Midi Through) from
answering itself.

Try it without hardware: `aseqdump -p "Midi Through"` in one terminal,
`aseqsend -p "Midi Through" F0 7D 53 7F 00 F7` in another.

## What SignalPatch already does

- Opens every MIDI input, hot-plug included.
- MIDI learn on knobs, stomps, module buttons, group pedals and rig slots;
  bindings saved in the patch.
- Note On toggles a stomp, CC >= 64 sets it, commands fire on the rising
  edge, program changes select slots.
- Relative encoders (CC 64 ± n) per mapping; the flag is saved with the patch.
- SysEx feedback as above (`src/audio/ControllerFeedback.*`, diffed on the
  engine timer, tested headless).
- A MIDI Note node: notes reach the synth and pluck through the audio
  callback's queue (Gate / Pitch / Velocity outputs).

## Follow-ups on the SignalPatch side

- An expression curve (log/exp) on top of the calibrated span.
- A "controller layout" view in the app that shows the eight switches the
  way the pedal does.

## Hardware sketch (for the firmware repo)

- ESP32-S3 (the SuperMini kit already in use), TinyUSB MIDI.
- 8 momentary footswitches with a WS2812 LED each; 2 × 1/4" TRS expression
  inputs (ADC); 4 encoders; a small OLED for labels and the rig name.
- Everything debounced in firmware; switches send Note On/Off, never
  toggles, so SignalPatch owns the state.
