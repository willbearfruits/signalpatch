# Foot controller protocol

How a foot controller talks to SignalPatch. The firmware lives in its own
repository. Input is ordinary MIDI, so any controller works through MIDI
learn. What this adds is feedback: SignalPatch tells the controller what
each switch means right now, so its LEDs and display match the Board. A
controller that ignores the SysEx still works.

The reference hardware is an ESP32-S3 with TinyUSB, class compliant, so no
drivers are needed.

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

## Notes

- A response curve for an expression pedal belongs to the knob it drives:
  set it in the inspector.
- Not done yet: a view in the app that shows the eight switches the way the
  pedal does.

## Hardware sketch

- ESP32-S3 (the SuperMini kit already in use), TinyUSB MIDI.
- 8 momentary footswitches with a WS2812 LED each; 2 × 1/4" TRS expression
  inputs (ADC); 4 encoders; a small OLED for labels and the rig name.
- Everything debounced in firmware; switches send Note On/Off, never
  toggles, so SignalPatch owns the state.
