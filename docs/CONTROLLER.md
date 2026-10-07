# Controller protocol

How a hardware controller talks to SignalPatch. Input is ordinary MIDI, so
any controller works through MIDI learn. What this adds is feedback:
SignalPatch tells the controller what each control means right now, so its
LEDs and display match the app. A controller that ignores the SysEx still
works.

Two controllers use it:

- The ESP32-C3 handheld controller in `controller/c3-oled/`: a 0.42" OLED,
  an encoder, three pots, three buttons, an expression jack and a gyro,
  over Bluetooth LE MIDI. Wiring, flashing and pairing are in that folder's
  README.
- A foot controller on an ESP32-S3 over USB MIDI (sketch at the end; no
  firmware yet).

## Input (controller → SignalPatch)

| Control            | Message                        | Notes                                  |
| ------------------ | ------------------------------ | -------------------------------------- |
| Footswitch / button n (1-8) | Note On 60+n-1 (press), Note Off (release), channel 10 | Momentary. SignalPatch toggles a stomp on Note On, fires a command on the rising edge, loads a slot on Note On. |
| Bank / slot buttons| Program Change 0-4             | Direct slot select.                    |
| Expression 1 / 2   | CC 11 / CC 4                   | 0-127, 7-bit is enough for a pedal. "Calibrate the pedal" on the knob's menu learns the heel/toe span (saved with the mapping; swapped ends invert). |
| Gyro tilt / roll   | CC 12 / CC 13                  | 64 is level. Absolute, like a pedal.   |
| Pots 1-3           | CC 16-18                       | Absolute.                              |
| Encoder 1-4        | CC 20-23, relative (64 ± delta)| Learn the knob with "MIDI learn as relative encoder": each detent nudges it 1/128 of its range, n > 1 for fast spins. |
| Tap tempo          | Note On 48, channel 10         | Mapped like any footswitch: the drum machine's TAP button (command "tap", four taps averaged) or the looper's REC. |

Channel 10 keeps the controller's notes away from anything driving a synth.

The C3 controller sends buttons 1-3 as notes 60-62, the encoder push as
note 63, the encoder as CC 20, and the rest as above. All of its numbers
and its channel can be changed (see Settings).

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
| 0x05    | CC number, label as 7-bit ASCII, up to 12 chars (empty = nothing) | what a continuous control drives |
| 0x06    | CC number, position (0-127), value text, up to 10 chars | where that knob is and how the app prints it ("6.50 kHz") |
| 0x08    | flags: bit 0 = muted                          | app state for the display |
| 0x7F    | side (0x00 controller, 0x01 SignalPatch), then optionally model, firmware major, firmware minor | hello |

The controller sends `F0 7D 53 7F 00 F7` on its input port when it boots
(and whenever it wants the state again). A controller may add three bytes
after the side: model (0x01 the S3 foot controller, 0x02 the C3 handheld)
and its firmware version, e.g. `F0 7D 53 7F 00 02 01 00 F7`. SignalPatch
answers on the output port with the same name as that input (or, failing
that, the one whose name contains it): `F0 7D 53 7F 01 F7`, then the whole
state (eight LED + label pairs, five slot messages, the rig name, labels and
values for the continuous controls, the flags). After that it sends only
what changed, checked twenty times a second: a stomp, a looper going into
record (LED 2 = blink), a slot change, a rename, a knob that moved. Labels
are the pedal's name (8 chars), `REC <pedal>`, `RIG n` or the group's name.
The side byte is what keeps a looped-back port (ALSA's Midi Through) from
answering itself.

Labels and values (0x05, 0x06) are sent for the CC numbers the controller
reported in its settings, or for CC 4, 11-13, 16-18 and 20-23 when it
reported none. A 0x05 clears that control's value until a 0x06 for it
arrives; 0x06 is only sent when the app knows a value. Switch n always
means the controller's button n, whatever note it was given in the
settings; mappings are matched on the controller's channel or on any
channel. App actions get short labels: `SLOT +`, `SLOT -`, `MUTE` (lit
while muted), `KNOB`, `KNOB +`, `KNOB n` on a switch, and "Next slot",
"Knob" and so on for a continuous control.

Try it without hardware: `aseqdump -p "Midi Through"` in one terminal,
`aseqsend -p "Midi Through" F0 7D 53 7F 00 F7` in another. For the whole C3
side (hello, settings, controls) on Linux, `controller/tools/fakec3.c` makes
an ALSA port called "SignalPatch C3" that answers like the controller.

## Settings (SignalPatch ↔ controller)

A controller that says it is model 0x02 gets a settings page in the app.
The values live on the controller (flash) and are read and written over the
same SysEx:

| command | direction | payload | meaning |
| ------- | --------- | ------- | ------- |
| 0x10 | app → controller | none | send your settings |
| 0x11 | controller → app | key, value, key, value, ... | every setting (the answer to 0x10, 0x12 and 0x13) |
| 0x12 | app → controller | key, value, ... | change these, save them, answer with 0x11 |
| 0x13 | app → controller | none | back to defaults, answer with 0x11 |
| 0x14 | app → controller | none | zero the gyro at its current angle |

Keys and values are 7-bit. A controller ignores keys it does not know.

| key  | setting | default | range |
| ---- | ------- | ------- | ----- |
| 0x00 | MIDI channel | 10 | 1-16 |
| 0x01 | Pot 1 CC | 16 | 0-119 |
| 0x02 | Pot 2 CC | 17 | 0-119 |
| 0x03 | Pot 3 CC | 18 | 0-119 |
| 0x04 | Expression CC | 11 | 0-119 |
| 0x05 | Gyro tilt CC | 12 | 0-119 |
| 0x06 | Gyro roll CC | 13 | 0-119 |
| 0x07 | Encoder CC | 20 | 0-119 |
| 0x08 | Button 1 note | 60 | 0-127 |
| 0x09 | Button 2 note | 61 | 0-127 |
| 0x0A | Button 3 note | 62 | 0-127 |
| 0x0B | Encoder push note | 63 | 0-127 |
| 0x0C | Expression input on | 1 | 0-1 |
| 0x0D | Expression reversed | 0 | 0-1 |
| 0x0E | Gyro mode | 3 | 0 off, 1 always on, 2 while button 3 is held, 3 a long press on the encoder toggles it |
| 0x0F | Gyro range, degrees each side of level | 30 | 5-90 |
| 0x10 | Gyro smoothing | 4 | 0-10 |
| 0x11 | Pot dead band, in CC steps | 1 | 0-4 |
| 0x12 | Encoder reversed | 0 | 0-1 |
| 0x13 | Encoder acceleration | 1 | 0-1 |
| 0x14 | Display brightness | 100 | 0-127 |
| 0x15 | Display upside down | 0 | 0-1 |
| 0x16 | Display sleep, minutes (0 = never) | 0 | 0-60 |
| 0x17 | Number added to the Bluetooth name (0 = none), after a restart | 0 | 0-9 |

Gyro modes: in mode 2 button 3 sends no note; it only holds the gyro on.
In mode 3 a press on the encoder shorter than 0.6 s sends its note when it
is released, and a longer one toggles the gyro. The gyro is zeroed at its
current angle whenever it is switched on.

## In the app

The CONTROLLER page (header button, or FILE > Controller) lists the
controls of the connected controller with what each one drives and its last
value. A control can drive something in the open patch (saved with the
patch, like any MIDI mapping) or an app action that works in every patch
(saved in the app settings):

- the knob you last touched (the encoder, relative) and the next knob of
  the selected module (the push);
- knob 1, 2 or 3 of the selected module (the pots; a pot takes over only
  once it passes the knob's current value, so nothing jumps);
- next or previous rig slot, a rig slot, mute.

A mapping in the patch wins over an app action on the same control.
"Default layout" sets the app actions above: encoder, push, pots 1-3 on
the selected module, button 1 previous slot, button 2 next slot, button 3
mute.

## Notes

- A response curve for an expression pedal belongs to the knob it drives:
  set it in the inspector.

## Foot controller sketch

- ESP32-S3 (the SuperMini kit already in use), TinyUSB MIDI.
- 8 momentary footswitches with a WS2812 LED each; 2 × 1/4" TRS expression
  inputs (ADC); 4 encoders; a small OLED for labels and the rig name.
- Everything debounced in firmware; switches send Note On/Off, never
  toggles, so SignalPatch owns the state.
