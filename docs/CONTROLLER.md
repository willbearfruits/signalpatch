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
| Expression 1 / 2   | CC 11 / CC 4                   | 0-127, 7-bit is enough for a pedal.    |
| Encoder 1-4        | CC 20-23, relative (64 ± delta)| SignalPatch treats CC on a knob as absolute today; relative encoders are a follow-up (see below). |
| Tap tempo          | Note On 48, channel 10         | Mapped like any footswitch (e.g. looper REC). |

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
| 0x7F    | —                                             | hello: SignalPatch is here, send state |

SignalPatch sends the whole state after a `0x7F` hello from the controller
and after every change that affects a mapped switch. The controller replies
to `0x7F` with its own `0x7F` so both sides know the other is listening.

## What SignalPatch already does (0.4)

- Opens every MIDI input, hot-plug included.
- MIDI learn on knobs, stomps, module buttons, group pedals and rig slots;
  bindings saved in the patch.
- Note On toggles a stomp, CC >= 64 sets it, commands fire on the rising
  edge, program changes select slots.

## Follow-ups on the SignalPatch side

- **Relative encoders**: a mapping flag so CC 64±n nudges the knob instead of
  setting it.
- **SysEx feedback**: the 0x01-0x04 messages above, sent from the message
  thread when mapped state changes.
- **A MIDI Note node** in the engine so the synth and pluck are playable from
  a keyboard sample-accurately (events through the callback's queue), rather
  than through the message-thread control path.

## Hardware sketch (for the firmware repo)

- ESP32-S3 (the SuperMini kit already in use), TinyUSB MIDI.
- 8 momentary footswitches with a WS2812 LED each; 2 × 1/4" TRS expression
  inputs (ADC); 4 encoders; a small OLED for labels and the rig name.
- Everything debounced in firmware; switches send Note On/Off, never
  toggles, so SignalPatch owns the state.
