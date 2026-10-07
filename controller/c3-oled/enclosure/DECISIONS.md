# Decisions

Choices made without asking, each with what else was possible.

## Concept

- **Two printed parts.** A shell and a lid that is the control panel.
  Every control hangs from the lid by its own nut; the wiring stays
  attached when the lid is lifted off. Alternatives: a three-part case (a
  separate carrier for the electronics), or a clamshell split at mid-height,
  which puts a seam under the hands.
- **Closed with four M3 x 8 screws from below** into heat-set inserts in
  pillars that hang from the lid and stand on bosses in the floor. The lid
  rests only on the pillars, so the shell's rim is not a second support
  (0.3 mm seam). Alternatives: snap fits (hard to check without printing,
  wear out), screws from the top (heads under the thumbs).
- **Size 116 x 74 x 31.4 mm**, set by the jack's body (23 mm tall) and the
  longest 7 mm button (29 mm overall). A smaller jack or shorter buttons
  would allow a thinner case.

## Layout

- Pots in a row across the middle, buttons in a row nearer the player
  (thumbs), the encoder and the OLED toward the far edge, the USB-C and the
  expression jack in the far wall (cables leave away from the hands).
  Alternatives: buttons on the sides for a two-handed grip, or the jack on
  a side wall. Knobs are 26 mm apart (10 mm between 16 mm knobs).
- The MPU-6050 lies under the encoder, screwed down: as the tilt sensor it
  must not shift. Alternatives: M2 heat-set inserts (stronger, one more
  part to buy), double-sided tape (would let it creep).

## The ESP32-C3 board

- **On a printed stand with 1 mm double-sided foam tape**, located by two
  L-shaped ears around its far corners and by a 0.95 mm slot in the far
  wall that takes its USB edge. The board has no mounting holes, its long
  edges carry the pins (no room for rails), and its short edges carry the
  buttons, the antenna and the USB receptacle. Alternatives: a pocket on the
  lid's underside (the USB-C would then need a notch through the lid's
  edge), clips over the corners (too little free board surface), hot glue.
- **The USB edge goes into the wall** so the receptacle's face ends at or
  just outside the wall's outer face (0.05 to 1.05 mm, from the receptacle's
  overhang tolerance). A cable's plug grip then never enters the case, and
  no recess or lid notch is needed. The plug pushes the board toward the
  ears; the tape and the ears take it.
- **The stand's height** puts the highest receptacle 0.3 mm under the rim.
  With the board thickness and the OLED's stack height unknown (no source),
  the OLED glass ends up 3.4 to 6.2 mm below the lid's top face; the window
  is chamfered at 45 degrees so it stays readable from an angle.
- **The OLED window covers both possible positions** of the 72 x 40 area on
  the panel (the photos do not show which end it sits at). The panel's
  edge may show in the window.

## Parts

- **9 mm Alpha pots** (RD901F-40) for their size. 16 mm pots would need a
  larger case. The pots' anti-rotation tab, where present, sits in a pocket
  in the lid's underside.
- **EC11 encoder with an M7 bushing** (Bourns PEC11R, Alps EC11E or the
  generic EC11), envelope from the largest (PEC11R).
- **7 mm PBS-110 buttons**: the smallest common panel-mount momentary.
  The R13-507 often suggested for this is a 16 mm part.
- **1/4" jack**: Switchcraft 112BX or Cliff S4. The hole is 11.5 mm so both
  fit; a 3/8" Switchcraft clamps with its washer.
- **Knobs** up to 16 mm across and 18 mm tall.

## Printing

- 0.4 mm nozzle, 0.2 mm layers, a 220 x 220 mm bed (the kit's defaults).
  Both parts fit one bed each.
- Shell floor down, lid top face down: no supports; the engraving is in the
  lid's bed face.
- PETG preferred (inserts and thread-forming screws), PLA works.
- Engraving 3.2 mm Liberation Sans Bold, 0.6 mm deep: numbers beside the
  pots and buttons, EXP over the jack, the name.
