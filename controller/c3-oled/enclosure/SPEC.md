# SignalPatch C3 enclosure: specification

## 1. Frame and outline

mm throughout. X right, Y away from the player, Z up. Z = 0 is the lid's
underside; the plan origin is the middle of the case.

Outside: 116 x 74 x 31.4 mm (shell 29.4, lid 2.0), vertical corners R9, a
2 mm 45-degree chamfer on the bottom edge and 1.5 mm on the top edge. Walls
and floor 2.4 mm, lid 2.0 mm.

| Part | Where | Holds |
| --- | --- | --- |
| lid | Z 0 to 2.0, pillars down to -24.0 | 3 pots, encoder, 3 buttons (own nuts), OLED window, 2 LED holes, BOOT and RST pin holes, 4 pillars with M3 inserts, a 3 mm lip inside the walls |
| shell | Z -29.4 to -0.3 | the ESP32-C3 board's stand, the MPU-6050 bosses, the jack and USB-C openings in the far wall, 4 bosses with counterbored M3 holes, 4 feet recesses |

## 2. Z stack-up

The lid's height is set by its four pillars standing on the shell's bosses
(both printed); the shell's rim stays 0.3 mm below the lid, so only one
pair of surfaces carries the lid. Every panel part's height is set by its
own mounting face against the lid's underside. The ESP32-C3 board's height
is set by its stand from below.

| Z | What | Set by | Tolerance |
| --- | --- | --- | --- |
| +2.0 | lid top | the pots' 5.0 mm bushing: 4.7 at its shortest - 2.0 lid - 0.5 washer = 2.2 mm of thread for a 2.0 mm nut | printing +-0.1 |
| 0 | lid underside; pots, encoder, buttons bear on it | | |
| -0.3 | shell rim | the seam; the lid stands on the pillars | +-0.1 per print |
| -0.6 | highest USB-C receptacle top (0.3 mm under the rim with clearance) | the stand's height | |
| -1.4 | highest OLED glass | stand + 1.2 tape + 1.6 board + 2.6 OLED stack | |
| -4.0 / -5.4 | ESP32-C3 board top, thickest / thinnest stack | tape 0.6 to 1.2, board 0.8 to 1.6 | 1.4 mm span |
| -6.8 | the stand's top | receptacle top at the rim | |
| -13.5 | jack axis | the 23 mm Cliff body between floor and lid, 0.5 mm clear each side | |
| -22.5 | MPU-6050 board underside, on 4.5 mm bosses | M2 x 6 engagement and the floor under the pilots | |
| -24.0 | boss tops = pillar bottoms | M3 x 8 into a 5.7 mm insert | |
| -27.0 | floor top | the 7 mm buttons at 29 mm overall, 5 mm above the panel, plus 3 mm for wires: 1.65 mm left | |
| -29.4 | bottom | 2.4 mm floor | |

The OLED's depth below the top face is 3.4 to 6.2 mm depending on the tape,
the board and how the panel sits on it. The window is sized for the deepest
case and opens at 45 degrees from 11.2 x 10.5 mm at the underside to
15.2 x 14.5 mm at the top.

## 3. Parts and their sources

Every value that touches the case has two sources unless marked; the larger
value is used. Where no drawing exists the gap is listed in the build log as
a reviewed finding.

### ESP32-C3 OLED board (ABrobot "ESP32-C3 OLED", SuperMini with a 0.42" OLED)

| Value | Used | Source 1 | Source 2 |
| --- | --- | --- | --- |
| outline | 25.0 x 20.5 +-0.2 | maker dimension drawing (github.com/zhuhai-esp/ESP32-C3-ABrobot-OLED, Document/尺寸图.jpg): 25.00 x 20.50 | emalliab.wordpress.com/2025/02/12/esp32-c3-0-42-oled: 24.8 x 20.45; the maker's case STL in the same repository has a 25.5 x 21.0 cavity |
| header rows | 17.78 apart, 8 x 2.54, first pin 2.5 from the USB edge | maker drawing (17.78) | emalliab (8 per side, 17.78); pin position measured on the drawing |
| PCB thickness | 0.8 to 1.6 | not published | not published (reviewed finding) |
| USB-C | top side, centred, overhang 2.0 +-0.5, body 9.0 x 3.4 x 7.35 | karamo/ESP32-C3-mini-with-0.42-OLED README: 27 x 20 with the USB (27 - 25) | maker case STL: a 3.5 mm tall slot; receptacle body from USB Type-C receptacles (8.94 x 3.16-3.26) |
| OLED panel position | centred across, 13.5 +-0.5 from the USB edge | maker pinout photo (Document/引脚图.png) | emalliab photo with a ruler (reviewed finding) |
| BOOT, RST | top side at the edge opposite the USB, 4.0 and 16.5 from the left edge, 3 x 4.2 x 2.5 | maker drawing | maker pinout photo |
| LEDs | IO8 and PWR, 3.8 from the USB edge, 4.5 and 16.0 from the left | maker drawing | maker pinout photo |

The board's header carries GPIO 0-10, 20 and 21 (5V, GND, 3V3, 20, 21, 2,
1, 0 on one side; 10, 9, 8, 7, 6, 5, 4, 3 on the other), which matches the
firmware's `include/Pins.h`. The 01Space ESP32-C3-0.42LCD is a different
board: it does not bring out GPIO2 or GPIO9 and does not fit this case.

### OLED panel (Newvision N042-7240TSWEG01-H16)

| Value | Used | Source |
| --- | --- | --- |
| panel | 12.0 x 11.0 +-0.15, 1.25 +-0.1 thick | Newvision spec ver A, section 1.4 drawing (Document/SPEC N042-7240TSWEG01-H16 VER A.pdf) |
| active area | 9.196 x 5.18, 1.375 from one short edge, centred the other way | same drawing; second source: the 72 x 40 pixels at 0.128 x 0.13 pitch give 9.2 x 5.2 |
| height on the board | 1.25 to 2.6 | the panel alone, or on the 0.85 mm QFN with tape (reviewed finding) |

### 9 mm pots (Alpha RD901F-40, panel mounted)

From the kit's library: Alpha drawings SLH-211-433 and RD901F-40-15K T18,
Bourns PDB184 and Alps RK097 for the nut and washer. Mounting face 10.0
+-0.5 above the pin plane, M7 x 0.75 bushing 5.0 +-0.3, nut 10-11 AF x 2.0
+-0.3, washer 12 x 0.5, anti-rotation tab 2.0 wide at r 6.0-6.8, 0.8 tall
(a pocket in the lid takes it).

### Encoder (EC11 with M7 bushing and push switch)

| Value | Used | Source 1 | Source 2 |
| --- | --- | --- | --- |
| body | 12.5 x 13.4, 7.0 deep +0.3 | Bourns PEC11R datasheet (rev 04/26) | Alps EC11E (11.7 x 12.0, smaller: the larger is used) |
| bushing | M7 x 0.75, 5 (15 mm shaft) or 7 (20 mm) | PEC11R table L/LB | Alps EC11E15244G1 product page (shaft 20, D 6) |
| pins | A-C-B 7.5 from the axis, switch 7.0, 5.0 +0.3 long | KiCad RotaryEncoder_Alps_EC11E-Switch_Vertical_H20mm footprint | PEC11R drawing |
| nut, washer | 10.0 +-0.5 AF x 2.0 +-0.2; 12.0 +-0.3 x 0.5 | PEC11R hardware | kit RD901F (same M7 hardware) |

### 7 mm momentary buttons (PBS-110 type)

No maker drawing was found. Listings: sigmanortec (25 x 9, hole 7, cap 6),
uelectronics (27 x 7.35), Maplin mini momentary (29 x 9.5, 7 mm cut-out).
Envelope: 29 mm overall with only 5 mm above the panel, body or lower nut
10.4 across. The hole is 7.5 mm. Measure the buttons you buy (README).

### 1/4" stereo jack

Switchcraft 112BX (3/8"-32, 0.375" hole, bushing 0.276", body 0.62 x 0.62 x
0.831"; distributor listings) or Cliff S4 (body 20.1 x 23.0 x 22.8, 0.438"
hole, nut 11.3; distributor listings). Both makers' own drawings could not be
downloaded (reviewed finding). Envelope 21 x 23 x 28 mm behind the wall plus
6 mm for wires; hole 11.5 mm (Cliff's 11.1 + 0.4), teardrop roof; wall
2.4 mm, leaving 3.8 mm of the 7.0 mm bushing for a 0.8 mm washer and a
2.4 mm nut.

### MPU-6050 board (GY-521)

| Value | Used | Source 1 | Source 2 |
| --- | --- | --- | --- |
| outline | 21.0 x 16.0 max (+0.2) | ciaa/Ponchos KiCad footprint (16.00 x 20.26) | opatiny/micromouse footprint (15.88 x 20.95); openelab listing (20.5 x 15.5) |
| holes | two on the long edge opposite the pins, 1.8 from the corners, 16.6 apart, 2.5-3.2 | ciaa footprint (drill 2.5, 16.63 apart) | openelab (3.2) |
| fixing | M2 x 6 thread-forming screws, 1.8 mm pilots 5.9 deep | Bambu Lab forum (M2: 1.84 vertical) | accu-components self-tapping chart (M2.5 in PLA: 2.05) |

### USB-C plug

Grip up to 12.5 x 6.6 (Amphenol Socapex type-C overmoulding limits); plug
shell 8.25 x 2.4 nominal, mated depth 6.65 (USB Type-C specification, as on
the GCT USB4130 drawing). The receptacle's face ends at or outside the
wall's outer face, so the grip never enters the case.

### Fasteners and other parts

M3 x 8 ISO 4762 into Ruthex / CNC Kitchen M3 x 5.7 inserts (hole 4.0, depth
7.5, wall 2.0); counterbore 6.6 x 3.5 (DIN 974-1 + 0.1); 3M Bumpon SJ5302
feet; 1 mm double-sided foam tape (0.6 to 1.2 mm in place); knobs up to
16 x 18 mm. All from the kit's library unless listed above.

## 4. Printing

0.4 mm nozzle, 0.2 mm layers, 220 x 220 mm bed. Shell floor down, lid top
face down; no supports. Overhangs at most 45 degrees, widest bridge 8.9 mm
(the counterbores), thinnest wall 0.8 mm (outside the lettering). PETG or
PLA; PETG takes the heat-set inserts and the M2 screws more kindly.
