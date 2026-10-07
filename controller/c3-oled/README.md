# SignalPatch C3 controller

Firmware for a handheld MIDI controller for SignalPatch. It runs on an
ESP32-C3 board with a built-in 0.42" OLED and reads three pots, a rotary
encoder with a push switch, three buttons, an expression pedal jack and an
MPU-6050 gyro. It connects over Bluetooth LE MIDI and follows the protocol in
[docs/CONTROLLER.md](../../docs/CONTROLLER.md): SignalPatch sends back what
each control drives, the button LED states, the rig name and knob values, and
reads and changes the controller's settings from its CONTROLLER page.

## Parts

- ESP32-C3 SuperMini-style board with a 0.42" 72x40 OLED (SSD1306 on I2C, GPIO5/GPIO6)
- 3 linear 10k pots and 3 resistors of 3.3k
- Rotary encoder with push switch (EC11 type)
- 3 momentary push buttons
- 1/4" TRS jack, one 100k resistor, one 3.3k resistor
- GY-521 board (MPU-6050), optional
- USB-C cable, or a USB power bank

A printable case for it is in [enclosure/](enclosure/README.md): 116 x 74 x 31 mm,
the controls on the lid, the board on a stand, not yet printed.

## Wiring

| Part | Connects to | Notes |
| --- | --- | --- |
| Pot 1 wiper | GPIO0 | ADC1 |
| Pot 2 wiper | GPIO1 | ADC1 |
| Pot 3 wiper | GPIO3 | ADC1 |
| Pot ends | GND and 3V3 | the 3V3 end through a 3.3k resistor |
| Expression jack tip | GPIO4 | ADC1, 100k from GPIO4 to GND |
| Expression jack ring | 3V3 | through a 3.3k resistor (see below) |
| Expression jack sleeve | GND | |
| Encoder A | GPIO7 | common pin to GND |
| Encoder B | GPIO10 | |
| Encoder push | GPIO20 | other side to GND |
| Button 1 | GPIO21 | other side to GND |
| Button 2 | GPIO2 | other side to GND, strapping pin |
| Button 3 | GPIO9 | other side to GND, same pin as the BOOT button |
| MPU-6050 SDA | GPIO5 | shared with the OLED |
| MPU-6050 SCL | GPIO6 | shared with the OLED |
| MPU-6050 VCC / GND | 3V3 / GND | AD0 low (address 0x68) |
| Onboard LED | GPIO8 | on the board, lit when low |

All buttons and the encoder use the C3's internal pull-ups. Every pin is set
in `include/Pins.h`.

The C3's ADC reads about 0 to 2.5 V. The firmware maps 0 to 2450 mV onto 0 to
127. A 3.3k resistor between 3V3 and the top of each 10k pot puts the top of
the travel at about 2.48 V, so the whole travel is used. Without it the last
quarter of the travel reads 127.

The expression jack expects a standard TRS pedal (tip = wiper, ring = top of
the pot). The 100k pull-down makes an empty jack read 0. A 3.3k resistor
between 3V3 and the ring does the same job as for the pots with a 10k pedal
(use about 10k for a 25k pedal), and limits the current if the plug briefly
connects ring to sleeve while it goes in. SignalPatch's "Calibrate the pedal"
learns whatever heel-to-toe span is left.

The MPU-6050 shares the I2C bus with the OLED. Most GY-521 boards already have
pull-up resistors on SDA and SCL. Without the MPU-6050 the firmware runs and
shows the gyro as missing.

Power comes from USB-C or a USB power bank. The SuperMini has no battery
charger. Some power banks switch off when the current draw is low.

### Strapping pins

GPIO2 and GPIO9 decide how the C3 boots. Do not hold button 2 or button 3
while powering up. Holding button 3 (or BOOT) at power-up starts the
bootloader, which is also how to flash a board that no longer accepts an
upload. GPIO21 is the UART transmit pin; the boot ROM prints on it for a
moment at power-up, so do not hold button 1 at power-up either.

## Build and flash

Needs [PlatformIO](https://platformio.org/). The first build downloads the
RISC-V toolchain and the libraries (versions are pinned in `platformio.ini`).

```sh
cd controller/c3-oled
pio run                   # build
pio run -t upload         # flash over USB
pio device monitor        # log over the same USB port
pio test -e native        # host unit tests, no board needed
```

If an upload fails or the board is stuck: unplug it, hold button 3 (or BOOT),
plug it in, release the button, upload again, then unplug and plug it back in.

## Pairing

The controller advertises as "SignalPatch C3", or "SignalPatch C3 n" when the
name number setting is 1 to 9. It takes one connection at a time and
advertises again after a disconnect.

Windows 11: Settings > Bluetooth & devices > Add device > Bluetooth, pick
"SignalPatch C3". Then start SignalPatch; the controller appears as a MIDI
input and output port.

Linux (BlueZ built with MIDI support; Arch's `bluez` package is):

```sh
bluetoothctl
scan on            # wait for "SignalPatch C3", note its address
pair <address>
trust <address>
connect <address>
```

The controller then appears as an ALSA sequencer port (`aconnect -l`), and
SignalPatch opens it like any other MIDI port.

To forget every paired computer, hold the encoder push while powering up. The
screen shows "Pairings forgotten". Remove the controller on the computer as
well before pairing again.

## What it sends

Everything goes out on MIDI channel 10 by default. The channel and every
number can be changed from SignalPatch.

| Control | Message (default) |
| --- | --- |
| Buttons 1-3 | Note On 60-62 on press, Note Off on release |
| Encoder push | Note 63 (see gyro modes) |
| Encoder | CC 20, relative: 64 + n clockwise, 64 - n counter-clockwise |
| Pots 1-3 | CC 16-18 |
| Expression | CC 11 |
| Gyro tilt / roll | CC 12 / CC 13, 64 is level |

Buttons never toggle; SignalPatch keeps the state. The encoder sends one
message per detent, with n above 1 on fast turns when acceleration is on.
Pots and the expression input are averaged and smoothed and send only when
they move, at most every 10 ms each. The pot dead band setting is how far a
pot has to move before it sends again after it stopped or turned back; while
it keeps moving one way every step is sent.

## Gyro

The gyro setting has four modes:

- 0: off.
- 1: always on.
- 2: on while button 3 is held; button 3 then sends no note.
- 3 (default): a press on the encoder push longer than 0.6 s switches the gyro
  on or off; a shorter press sends the push note when it is released.

The gyro is zeroed at its current angle whenever it is switched on, and when
SignalPatch asks for it. The range setting (5 to 90 degrees each side of
level) maps onto 0 to 127, with 64 at level. Tilt is rotation about the GY-521
board's Y axis, roll about its X axis. If the board sits differently in the
case, swap or invert the axes in `include/Pins.h`.

## Screens

Not connected: "SignalPatch", "C3" (with the name number), "pair me" and a
blinking square. The onboard LED blinks.

Connected, SignalPatch not answering yet: "connected". Moving a control shows
its name and the raw message, for example "Pot 1" and "CC16  64". The onboard
LED is on. The controller sends its hello every 3 seconds until SignalPatch
answers, so SignalPatch can be started after the controller connects.

Connected to SignalPatch: the top row shows the live rig slot and the rig name
(up to 12 characters fit). The middle row shows MUTED, the encoder push's
label, and GYRO while the gyro is on (struck through when a gyro mode is set
but no MPU-6050 answered). The bottom row shows buttons 1-3 with their labels,
filled while the button's LED is on and flashing when SignalPatch says blink.
The onboard LED is off.

When a control moves, the screen shows for 1.5 s what it drives (the label from
SignalPatch, or "Pot 1" and so on when it drives nothing), the value as
SignalPatch prints it ("6.50 kHz"; the raw CC value until SignalPatch sends
one) and a bar for the position.

The brightness, upside-down and sleep settings apply as soon as they change.
Any control wakes a sleeping screen. The display is redrawn only when its
content changes, at most about 30 times a second, one row per pass of the main
loop.

## Settings

SignalPatch's CONTROLLER page reads and writes the settings listed in
docs/CONTROLLER.md. They are stored in the controller's flash. Out-of-range
values are clamped and unknown keys are ignored. The Bluetooth name number
takes effect after a restart; everything else applies at once.

## Code layout

- `src/core/`: plain C++ with no hardware access, built and tested on the
  host. `BleMidi` (packet codec), `Protocol` (SysEx), `Settings`, `InputMath`
  (encoder, debounce, analog mapping), `Tilt` (complementary filter),
  `Screen` (layouts as draw lists), `Controller` (behaviour).
- `src/`: the hardware side. `BleMidiPort` (NimBLE), `Inputs`, `Mpu6050`,
  `Display` (U8g2), `SettingsStore` (NVS), `main.cpp`.
- `test/`: Unity tests for `pio test -e native`.

## Not tested

The firmware compiles without warnings (PlatformIO espressif32 6.13.0,
Arduino core 2.0.17, NimBLE-Arduino 2.5.1, U8g2 2.36.18), and the code in
`src/core/` passes the host unit tests: BLE-MIDI framing in both directions,
SysEx parsing and building, settings, encoder decoding, value mapping, the
gyro filter, the controller's behaviour and that every screen fits 72x40. It
has not been run on a board. Pairing and MIDI with Windows 11 and with BlueZ
(including whether Windows' Bluetooth MIDI driver passes SysEx that spans
several packets; BlueZ does),
the ADC readings and their noise, the encoder on real contacts, the MPU-6050
axes, the display's look and current draw are all untested.
