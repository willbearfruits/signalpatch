"""SignalPatch C3 controller: a handheld two-part case (shell and lid) around the ESP32-C3 OLED board.

Concept (DECISIONS.md): the lid is the control panel. Three 9 mm pots, the encoder and three 7 mm
buttons hang from it by their own nuts, with the OLED behind a window. The ESP32-C3 board sits on a
stand that rises from the shell's floor, stuck down with 1 mm foam tape; its USB-C edge goes into a
slot in the far wall, so the receptacle's face ends at the outer surface and a cable's plug grip never
enters the case. The 1/4" expression jack goes through the far wall too, the MPU-6050 board is screwed
to bosses on the floor (it is the tilt sensor: it must not move). Four M3 x 8 screws go up through the
floor into heat-set inserts in pillars that hang from the lid; the pillars stand on bosses, so the lid
is set by them and the shell's rim stays 0.3 mm below it.

Frame: mm; X right, Y away from the player, Z up; Z = 0 is the lid's underside; the plan origin is the
middle of the case.
"""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import ekit                    # noqa: E402
import parts_lib as L          # noqa: E402
from ekit import Box, Cyl, Hull, Text, Diff, gap2d   # noqa: E402

NAME = 'signalpatch-c3'
OUTDIR = '.'
BOARD_GLB = None

# ------------------------------------------------------------------ numbers (mm)
LID_T = 2.0                     # the 9 mm pots' 5 mm bushing: thread for the nut over 2.0 mm (SPEC.md 2)
LT = LID_T
HX, HY = 58.0, 37.0             # outside: 116 x 74
WALL = 2.4
IX, IY = HX - WALL, HY - WALL   # cavity half sizes: 55.6, 34.6
R_OUT = 9.0
R_IN = R_OUT - WALL
FT = -27.0                      # floor top: below the deepest part (the buttons' wires)
FLOOR = 2.4
FB = FT - FLOOR
RIM_GAP = 0.3                   # the lid stands on the pillars, not on the rim
RIM = -RIM_GAP
CH_TOP, CH_BOT = 1.5, 2.0       # 45-degree chamfers: the lid's top edge, the shell's bottom edge
LIP_T, LIP_H, LIP_CL = 1.2, 3.0, 0.25
MIN_WEB = 1.2
LABEL_GAP = 2.0
TEXT = dict(size=3.2, depth=0.6)
PILLARS = [(sx * 50.4, sy * 29.4) for sx in (-1, 1) for sy in (-1, 1)]
PILLAR_D, BOSS_D = 8.0, 10.0
BOSS_TOP = FT + 3.0
SCREW = 8                       # M3 x 8, ISO 4762, from below into the lid's pillars
FEET = [(sx * 30.0, sy * 27.0) for sx in (-1, 1) for sy in (-1, 1)]

POTS = [('RV1', '1', -26.0, -6.0), ('RV2', '2', 0.0, -6.0), ('RV3', '3', 26.0, -6.0)]
ENC = ('SW4', 30.0, 20.0)
BUTTONS = [('SW1', '1', -26.0, -26.0), ('SW2', '2', 0.0, -26.0), ('SW3', '3', 26.0, -26.0)]
JACK = ('J1', -34.0, -13.5)     # x, axis z

# the ESP32-C3 board: USB-C toward +Y, OLED up, on a stand
C = L.C3OLED
Z_P = -6.8                      # the stand's top: the receptacle's highest top 0.3 mm under the rim (SPEC.md 2)
BOARD_EDGE = IY + 0.95          # the USB edge sits in a 0.95 mm slot in the far wall: the receptacle face reaches the outside
B_LEN = C['l'] + C['tol']       # longest board
B_HW = (C['w'] + C['tol']) / 2  # half the widest board
B_Y0 = BOARD_EDGE - B_LEN       # the longest board's button edge
T = L.TAPE
Z_BOT = (Z_P + T['t_min'], Z_P + T['t_max'])                       # the board's underside, thinnest / thickest tape
Z_TOP = (Z_BOT[0] + C['t_min'], Z_BOT[1] + C['t_max'])             # its top face
GY_C = (36.0, 18.0)             # MPU-6050 board centre: long side along Y, pins on the -X edge
GY_BOT = FT + 4.5               # it lies on 4.5 mm bosses

REVIEWED = {
    'C3:pcb thickness': 'Not published anywhere found (maker drawing, schematic, three sellers). The stack is drawn for 0.8 to '
                        '1.6 mm with the tape; the OLED window and the USB slot are sized for the whole range (SPEC.md 2).',
    'C3:oled height': "The panel is 1.25 +-0.1 mm (Newvision drawing) but how it sits on the board is not drawn: it may rest on "
                      "the 0.85 mm QFN with tape. The envelope runs from 1.25 to 2.6 mm above the board; the window is sized "
                      "for the deepest case (a 6.2 mm deep view) and the lid clears the highest.",
    'C3:oled position': 'The panel position on the board comes from two photos (the maker pinout and emalliab): centred across, '
                        '13.5 mm from the USB edge, +-0.5 mm. Which end of the panel the 72 x 40 area sits at is not visible: '
                        'the window covers both.',
    'C3:usb overhang': 'The receptacle\'s overhang is 27 - 25 = 2.0 mm from one measurement (karamo, "27 x 20 mm with the USB"); '
                       'taken as 1.5 to 2.5. Its face ends 0.05 to 1.05 mm outside the wall, never inside it.',
    'button:no drawing': 'No maker drawing for the 7 mm PBS-110 buttons; three listings give 25 to 29 mm overall. The envelope '
                         'is the longest, with only 5 mm above the panel: the README asks to measure the buttons bought.',
    'jack:no drawing': "Switchcraft's and Cliff's own drawings could not be downloaded (both sites refused); the numbers are their "
                       "distributors' listings. The envelope covers both jacks; the hole is Cliff's 11.1 mm (a 3/8\" "
                       "Switchcraft fits it with its washer).",
}


def build_model():
    m = ekit.Model(NAME)
    shell = m.part('shell', 'shell', note='printed floor down; no supports')
    lid = m.part('lid', 'lid', print_flip=True, note='printed top face down; no supports')
    lid_cuts, pockets, wall_cuts = [], [], []

    def take(res):
        for e in res['envelopes']:
            m.envelope(*e)
        for k in res['keep']:
            m.keep(*k)
        lid_cuts.extend(res['lid_cuts'])
        pockets.extend(res['lid_pockets'])
        wall_cuts.extend(res['wall_cuts'])
        return res

    for key, why in REVIEWED.items():           # the gaps in the sources, listed in the build log
        m.finding(key, key.split(':')[1] + ': ' + why.split('.')[0])

    # ---- the shell: chamfered block, cavity, bosses with counterbored screw holes, feet recesses
    # the cavity is part of the block: what is added below (bosses, the stand) must not be cut by it
    shell.add.append(Diff(Hull([Box(-HX + CH_BOT, -HY + CH_BOT, FB, HX - CH_BOT, HY - CH_BOT, FB + 0.01, r=R_OUT - CH_BOT),
                                Box(-HX, -HY, FB + CH_BOT, HX, HY, RIM, r=R_OUT)]),
                          [Box(-IX, -IY, FT, IX, IY, RIM + 1, r=R_IN)], label='shell block and cavity'))
    s = L.M3_SHCS
    seat = FB + s['cbore_h']
    for x, y in PILLARS:
        shell.add.append(Cyl('z', x, y, FT - 0.01, BOSS_TOP, BOSS_D / 2, label='boss'))
        shell.cut.append(Cyl('z', x, y, FB - 1, BOSS_TOP + 1, s['clear'] / 2, label='screw hole'))
        shell.cut.append(Cyl('z', x, y, FB - 1, seat, s['cbore_d'] / 2, label='counterbore'))
        for k in L.m3_screw(x, y, seat, SCREW):
            m.keep(*k)
    ins = L.INSERT
    m.margin('M3 screws: thread in the insert', min(seat + SCREW - s['l_tol'] - BOSS_TOP, ins['l']), 1.5 * s['d'])
    m.margin('M3 screws: tip below the insert hole bottom (longest screw)', ins['hole_depth'] - (seat + SCREW + s['l_tol'] - BOSS_TOP), 0.3)
    m.margin('M3 screws: a standard length', 0 if SCREW in s['lengths'] else 1, 0, kind='max')
    m.margin('pillars: wall around the insert hole', (PILLAR_D - ins['hole']) / 2, ins['min_wall'])
    m.margin('insert hole deeper than the insert + 1', ins['hole_depth'], ins['min_depth'])
    m.margin('bosses: wall around the counterbore', (BOSS_D - s['cbore_d']) / 2, 1.0)
    m.margin('counterbore: head below the floor (longest head)', s['cbore_h'] - s['head_h'], 0.3)
    fr = (L.FEET['d'] + L.FEET['tol']) / 2 + 0.25
    for x, y in FEET:
        shell.cut.append(Cyl('z', x, y, FB - 1, FB + 0.6, fr, label='foot recess'))
    m.margin('floor under the foot recesses', FLOOR - 0.6, 1.2)

    # ---- the lid: chamfered plate and the pillars with the inserts
    lid.add.append(Hull([Box(-HX, -HY, 0, HX, HY, LT - CH_TOP, r=R_OUT),
                         Box(-HX + CH_TOP, -HY + CH_TOP, LT - 0.01, HX - CH_TOP, HY - CH_TOP, LT, r=R_OUT - CH_TOP)], label='lid plate'))
    for x, y in PILLARS:
        lid.add.append(Cyl('z', x, y, BOSS_TOP, 0.01, PILLAR_D / 2, label='pillar'))
        lid.cut.append(Cyl('z', x, y, BOSS_TOP - 1, BOSS_TOP + ins['hole_depth'], ins['hole'] / 2, label='insert hole'))
    m.margin('seam between the shell rim and the lid', RIM_GAP, 0.1)
    m.margin('pillars inside the cavity corner', R_IN - (abs(PILLARS[0][0]) - (IX - R_IN)) * 1.4143 - PILLAR_D / 2, 0.5,
             note='pillar to the corner wall, along the diagonal')
    m.part_pairs.append(('lid', 'shell'))

    # ---- panel parts on the lid
    pots = {}
    for ref, name, x, y in POTS:
        res = take(L.pot_rd901f_panel(ref, x, y, (0, 1), LT, f'pot {name}'))
        pots[ref] = (x, y, name, res)
    d = L.RD901F
    m.margin('pots: nut thread on the bushing (shortest bushing, thickest washer)', res['thread'], 2 * 0.75, note='two turns of M7 x 0.75')
    m.margin('pots: shaft above the nut, for the knob (shortest shaft, thickest nut)', res['shaft_in_knob'], 7.0)
    ref, ex, ey = ENC
    enc = take(L.encoder_ec11_panel(ref, ex, ey, LT))
    m.margin('encoder: nut thread on the bushing (the 5 mm bushing of the 15 mm shaft)', enc['thread'], 2 * 0.75)
    m.margin('encoder: shaft above the nut, for the knob (shortest shaft, thickest nut)', enc['shaft_in_knob'], 7.0)
    buttons = {}
    for ref, name, x, y in BUTTONS:
        buttons[ref] = (x, y, name, take(L.button7_panel(ref, x, y, LT)))
    knob_at = [(x, y, f'pot {n}') for _, n, x, y in POTS] + [(ex, ey, 'encoder')]
    worst = min((((a[0] - b[0]) ** 2 + (a[1] - b[1]) ** 2) ** 0.5 - L.KNOB16['d'], f'{a[2]} / {b[2]}')
                for i, a in enumerate(knob_at) for b in knob_at[i + 1:])
    m.margin(f'knobs: space between the closest two ({worst[1]}, 16 mm knobs)', worst[0], 6.0, note='a finger between them')
    cap = min(((((a[0] - bx) ** 2 + (a[1] - by) ** 2) ** 0.5 - L.KNOB16['d'] / 2 - L.BUTTON7['nut_af'] * 0.577, a[2])
               for a in knob_at for bx, by, _, _ in buttons.values()))
    m.margin(f'buttons: room between a button and the nearest knob ({cap[1]})', cap[0], 5.0, note='a thumb on the button')

    # ---- the expression jack in the far wall
    ref, jx, jz = JACK
    jack = take(L.jack14_wall(ref, jx, IY, HY, jz))
    m.margin('jack: thread outside the wall for washer and nut', jack['thread'], L.JACK14['nut_t'])
    m.margin('jack: body above the floor', jack['body'].z0 - 0.3 - FT, 0.5)
    m.margin('jack: body below the lid', -(jack['body'].z1 + 0.3), 0.5)

    # ---- the ESP32-C3 board on its stand
    ot = 0.05                                       # the board bears on the tape, the ears and the slot
    m.envelope('C3 board', 'C3 board (any thickness, either tape)', Box(-B_HW - ot, B_Y0 - ot, Z_BOT[0], B_HW + ot, BOARD_EDGE + ot, Z_TOP[1]), 0)
    m.envelope('C3 board', 'foam tape on the stand', Box(-6.0, B_Y0, Z_P, 6.0, BOARD_EDGE - 1.0, Z_BOT[1]), 0)
    # top-side parts: the chip, antenna, LEDs and BOOT/RST pads keep 1.25 mm from the long edges (maker
    # drawing); nothing stands within 2 mm of the USB edge but the receptacle; the pins' solder fillets
    # along the rows are their own envelope
    m.envelope('C3 board', 'C3 top-side parts (chip, antenna, LEDs)',
               Box(-B_HW + 1.25, B_Y0, Z_TOP[0], B_HW - 1.25, IY - 0.35, Z_TOP[1] + C['parts_h']), 0.3)
    pin_rows = [BOARD_EDGE - C['pin_first'] - i * C['pitch'] for i in range(C['pins'])]
    for sx in (-1, 1):
        m.envelope('C3 board', f"C3 {'left' if sx < 0 else 'right'} pin row solder", Box(sx * C['rows'] / 2 - 1.0, min(pin_rows) - 1.0, Z_TOP[0],
                   sx * C['rows'] / 2 + 1.0, max(pin_rows) + 1.0, Z_TOP[1] + 1.0), 0.3)
    o = L.OLED042
    oy = BOARD_EDGE - C['oled_y']
    pl = C['oled_place_tol'] + o['tol']
    m.envelope('C3 board', 'OLED panel (highest stack, any position)',
               Box(-o['w'] / 2 - pl, oy - o['d'] / 2 - pl - 0.4, Z_TOP[0], o['w'] / 2 + pl, oy + o['d'] / 2 + pl, Z_TOP[1] + C['oled_h_max']), 0.3)
    over = (C['usb_over'] - C['usb_over_tol'], C['usb_over'] + C['usb_over_tol'])
    face = (BOARD_EDGE + over[0], BOARD_EDGE + over[1])
    usb = Box(-C['usb_w'] / 2 - 0.2, face[0] - C['usb_len'], Z_TOP[0], C['usb_w'] / 2 + 0.2, face[1], Z_TOP[1] + C['usb_h'])
    m.envelope('C3 board', 'USB-C receptacle (any overhang, any height)', usb, 0.3)
    btn_y = B_Y0 + C['btn_v']
    btns = []
    for u, what in zip(C['btn_u'], ('BOOT', 'RST')):
        bx = -C['w'] / 2 + u
        btns.append((bx, btn_y + 0.2, what))
        m.envelope('C3 board', f'{what} button', Box(bx - C['btn'][1] / 2 - 0.5, btn_y - C['btn'][0] / 2 - 0.3, Z_TOP[0],
                                                     bx + C['btn'][1] / 2 + 0.5, btn_y + C['btn'][0] / 2 + 0.7, Z_TOP[1] + C['btn_h']), 0.3)
    # the stand: a column under the middle (the pin rows' wires pass beside it), a full-width head at the
    # button end, and two L-shaped ears around the board's button-end corners that locate it; the far
    # wall's slot takes the USB edge. The ears' returns cover only the outer 0.8 mm of the edge: BOOT and
    # RST stand 1.9 mm in from the sides.
    shell.add.append(Box(-6.0, B_Y0 + 3.0, FT - 0.01, 6.0, IY + 0.01, Z_P, label='stand column'))
    shell.add.append(Box(-B_HW - 1.25, B_Y0 - 1.3, FT - 0.01, B_HW + 1.25, B_Y0 + 3.0, Z_P, label='stand head'))
    ear_top = Z_TOP[1] + 0.3
    for sx in (-1, 1):
        x0, x1 = sx * (B_HW + ot + 0.05), sx * (B_HW + 1.25)
        shell.add.append(Box(min(x0, x1), B_Y0 - 1.3, Z_P - 0.01, max(x0, x1), B_Y0 + 3.0, ear_top, label='stand ear'))
        r0, r1 = sx * (B_HW - 0.8), sx * (B_HW + 1.25)
        shell.add.append(Box(min(r0, r1), B_Y0 - 1.3, Z_P - 0.01, max(r0, r1), B_Y0 - ot - 0.05, ear_top, label='stand ear return'))
    m.margin('C3 stand: ears above the thickest board\'s top face', ear_top - Z_TOP[1], 0.2)
    bx_in = -C['w'] / 2 + C['btn_u'][0] - C['btn'][1] / 2 - 0.5
    m.margin('C3 stand: ear returns clear of BOOT and RST (their placement tolerance and clearance in)', (bx_in - 0.3) - (-(B_HW - 0.8)), 0.1)
    m.margin('C3 stand: ears end before the first pin from the button end', (BOARD_EDGE - C['pin_first'] - 7 * C['pitch'] - 0.9) - (B_Y0 + 3.0), 0.3)
    wall_cuts.append(Box(-B_HW - 0.1, IY - 1, Z_BOT[0] - 0.1, B_HW + 0.1, BOARD_EDGE + ot + 0.05, Z_TOP[1] + 0.15, label='board slot'))
    m.margin('far wall behind the board slot', HY - (BOARD_EDGE + ot + 0.05), 1.2)
    # the receptacle through the wall, open to the rim (its highest top meets the rim's level)
    wall_cuts.append(Box(usb.x0 - 0.35, IY - 1, Z_TOP[0] - 0.35, usb.x1 + 0.35, HY + 1, RIM + 1, label='USB-C notch'))
    m.margin('USB-C: receptacle face at or beyond the wall\'s outer face (shortest overhang)', face[0] - HY, 0.0,
             note='so the plug grip never has to enter the wall')
    m.margin('USB-C: highest receptacle top under the lid', -(Z_TOP[1] + C['usb_h']), 0.3)
    m.margin('USB-C: stand top below the lowest receptacle opening', (Z_TOP[0] - 0.35) - Z_P, 0.5)
    pg = L.USBC_PLUG
    for zt, what in ((Z_TOP[0], 'lowest'), (Z_TOP[1], 'highest')):
        ax = zt + C['usb_h'] / 2
        m.keep('USB plug', f'USB-C plug grip ({what} board)', Box(-pg['grip_w'] / 2, face[0], ax - pg['grip_h'] / 2, pg['grip_w'] / 2,
                                                                  face[0] + 30, ax + pg['grip_h'] / 2))
    # pins and wires below the board's two rows
    rows = [BOARD_EDGE - C['pin_first'] - i * C['pitch'] for i in range(C['pins'])]
    for sx in (-1, 1):
        m.keep('wiring', f"C3 {'left' if sx < 0 else 'right'} pin row wires",
               Box(sx * C['rows'] / 2 - 1.4, min(rows) - 1.3, Z_BOT[0] - 8.0, sx * C['rows'] / 2 + 1.4, max(rows) + 1.3, Z_BOT[0] - 0.05))
    m.margin('C3 stand column beside the pin-row wires', C['rows'] / 2 - 1.4 - 6.0, 1.0)
    m.margin('C3 stand head before the first pin-row wires', (min(rows) - 1.3) - (B_Y0 + 3.0), 0.3)

    # the window over the OLED, the light holes and the BOOT/RST pin holes
    aa_hw = o['aa_w'] / 2 + pl + 0.05
    reach = o['d'] / 2 - o['aa_edge']                       # the 72 x 40 area may sit at either end of the panel
    aa_y0, aa_y1 = oy - reach - pl - 0.4, oy + reach + pl
    m.keep('OLED view', 'OLED active area (either end, any placement)', Box(-aa_hw, aa_y0, Z_TOP[0] + o['h'] - o['h_tol'], aa_hw, aa_y1, LT + 1))
    w0 = Box(-aa_hw - 0.3, aa_y0 - 0.3, -1, aa_hw + 0.3, aa_y1 + 0.3, 0.0)
    window = Hull([w0, Box(w0.x0 - LT, w0.y0 - LT, LT, w0.x1 + LT, w0.y1 + LT, LT + 1)], label='OLED window')
    lid_cuts.append(window)
    deep = LT - (Z_TOP[0] + o['h'] - o['h_tol'])
    m.info.append(f'OLED: the window is {w0.x1 - w0.x0:.1f} x {w0.y1 - w0.y0:.1f} mm at the lid\'s underside and opens at 45 degrees '
                  f'to {w0.x1 - w0.x0 + 2 * LT:.1f} x {w0.y1 - w0.y0 + 2 * LT:.1f}; the glass is {LT - (Z_TOP[1] + C["oled_h_max"]):.1f} '
                  f'to {deep:.1f} mm below the top face')
    m.margin('OLED: highest glass under the lid', -(Z_TOP[1] + C['oled_h_max']), 0.3)
    for u, what in zip(C['led_u'], ('IO8 LED', 'PWR LED')):
        lx, ly = -C['w'] / 2 + u, BOARD_EDGE - C['led_v']
        m.keep('LED light', f'{what} light', Cyl('z', lx, ly, Z_TOP[1] + 0.6, LT + 1, 0.5))
        lid_cuts.append(Cyl('z', lx, ly, -1, LT + 1, 1.0, label=f'{what} window'))
    for bx, by, what in btns:
        m.keep('pin access', f'{what} pin access', Cyl('z', bx, by, Z_TOP[1] + C['btn_h'] + 0.35, LT + 1, 0.6))
        lid_cuts.append(Cyl('z', bx, by, -1, LT + 1, 1.25, label=f'{what} pin hole'))

    # ---- the MPU-6050 board: two M2 screws into bosses, a rib under its middle
    g = L.GY521
    gx, gy = GY_C
    hl, hw = g['l_max'] / 2 + g['tol'], g['w_max'] / 2 + g['tol']
    pins_x = gx - g['w'] / 2 + g['pin_edge']
    m.envelope('gyro', 'MPU-6050 board', Box(gx - hw, gy - hl, GY_BOT, gx + hw, gy + hl, GY_BOT + g['t']), 0)
    m.envelope('gyro', 'MPU-6050 parts', Box(pins_x + 1.4, gy - hl, GY_BOT + g['t'], gx + hw, gy + hl, GY_BOT + g['t'] + g['parts_h']), 0.3)
    m.keep('wiring', 'MPU-6050 wires (soldered on top, leaving toward -X)',
           Box(pins_x - 6.0, gy - 4.5 * g['pitch'], GY_BOT + g['t'] + 0.35, pins_x + 1.0, gy + 4.5 * g['pitch'], GY_BOT + g['t'] + 3.35))
    sw = L.M2_SELF
    hx = gx + g['w'] / 2 - g['hole_edge']
    screw_l = 6
    for hy in (gy - g['hole_pitch'] / 2, gy + g['hole_pitch'] / 2):
        shell.add.append(Cyl('z', hx, hy, FT - 0.01, GY_BOT, 2.5, label='MPU-6050 boss'))
        shell.cut.append(Cyl('z', hx, hy, FB + 1.0, GY_BOT + 1, sw['pilot'] / 2, label='M2 pilot'))
        m.keep('screws', f'M2 head at ({hx:.1f}, {hy:.1f})', Cyl('z', hx, hy, GY_BOT + g['t'], GY_BOT + g['t'] + sw['head_h'] + 0.2, sw['head_d'] / 2 + 0.2))
    shell.add.append(Box(gx - 0.8, gy - 7.0, FT - 0.01, gx + 0.8, gy + 7.0, GY_BOT, label='MPU-6050 rib'))
    m.margin('M2 screws: thread in the boss (shortest screw, thickest board)', screw_l - sw['l_tol'] - g['t'], 2 * sw['d'])
    m.margin('M2 screws: tip above the pilot\'s bottom (longest screw, a 1.0 mm board)', (GY_BOT - (FB + 1.0)) - (screw_l + sw['l_tol'] - 1.0), 0.3)
    m.margin('floor under the M2 pilots', 1.0, 0.8)
    m.margin('M2 screws: a standard length', 0 if screw_l in sw['lengths'] else 1, 0, kind='max')
    m.margin('MPU-6050 boss: wall around the pilot', 2.5 - sw['pilot'] / 2, 1.2)
    m.margin('MPU-6050 rib clear of the pin row', (gx - 0.8) - (pins_x + 1.0), 1.0)

    # ---- the lid: cuts, pockets, lip; the shell's wall cuts
    lid.cut += lid_cuts + pockets
    shell.cut += wall_cuts
    for pck in pockets:
        m.margin(f'{pck.label}: lid skin above it', LT - pck.z1, 0.8)
    ring = Box(-IX + LIP_CL, -IY + LIP_CL, -LIP_H, IX - LIP_CL, IY - LIP_CL, 0.01, r=R_IN - LIP_CL)
    hole = Box(-IX + LIP_CL + LIP_T, -IY + LIP_CL + LIP_T, -LIP_H - 1, IX - LIP_CL - LIP_T, IY - LIP_CL - LIP_T, 1,
               r=max(0.5, R_IN - LIP_CL - LIP_T))

    def near_ring(b):             # reaches the lip's height, within 3 mm of the cavity's edge
        return b[5] > -LIP_H - 0.6 and b[2] < 0 and (b[0] < -IX + 3 or b[1] < -IY + 3 or b[3] > IX - 3 or b[4] > IY - 3)
    notch = [e[2].grown(e[3] + 0.5) for e in m.envelopes if near_ring(e[2].grown(e[3]).bounds)]
    notch += [k[2].grown(0.5) for k in m.keepclear if near_ring(k[2].bounds) and k[0] not in ('screws',)]
    ri0, ro0 = -IX + LIP_CL + LIP_T + 0.1, -IX + LIP_CL - 0.1
    ri1, ro1 = IX - LIP_CL - LIP_T - 0.1, IX - LIP_CL + 0.1
    rj0, rk0, rj1, rk1 = -IY + LIP_CL + LIP_T + 0.1, -IY + LIP_CL - 0.1, IY - LIP_CL - LIP_T - 0.1, IY - LIP_CL + 0.1
    full = []
    for n in notch:
        b = n.bounds
        z0, z1 = -LIP_H - 1, 0.005
        if b[0] < ri0:
            full.append(Box(ro0, b[1], z0, ri0, b[4], z1))
        if b[3] > ri1:
            full.append(Box(ri1, b[1], z0, ro1, b[4], z1))
        if b[1] < rj0:
            full.append(Box(b[0], rk0, z0, b[3], rj0, z1))
        if b[4] > rj1:
            full.append(Box(b[0], rj1, z0, b[3], rk1, z1))
    lid.add.append(Diff(Diff(ring, [hole]), notch + full, label='lip'))
    m.info.append(f'lid lip: notched around {len(notch)} parts and access volumes near the walls')

    # ---- engraving: the numbers beside the pots and buttons, EXP over the jack, the name
    labels = [(n, x - 11.5, y) for _, n, x, y in POTS] + [(n, x - 9.6, y) for _, n, x, y in BUTTONS]
    labels += [('EXP', jx, 31.5), ('SIGNALPATCH', -30.0, 22.0)]     # the name over the jack, where nothing comes through the lid
    for t, x, y in labels:
        lid.engrave.append(Text(t, x, y, LT, TEXT['size'], TEXT['depth'], label=f'"{t}" at ({x:.0f}, {y:.0f})'))
    edge = HY - CH_TOP
    for t in lid.engrave:
        g_ = t.glyphs()
        m.margin(f'engraving {t.label}: letters lost to strokes under {ekit.NOZZLE} mm', 100 * (1 - g_['opened'] / g_['area']), 5.0,
                 kind='max', note='(% of the letters)')
        m.margin(f'engraving {t.label}: letters filled by gaps under {ekit.NOZZLE} mm', 100 * (g_['closed'] / g_['area'] - 1), 5.0,
                 kind='max', note='(% of the letters)')
        near, who = min((gap2d(Box(*t.bounds), c), c.label) for c in lid_cuts)
        m.margin(f'engraving {t.label}: flat lid to the nearest cut ({who})', near, 1.0)
        word, who = min([(gap2d(Box(*t.bounds), Box(*o_.bounds)), o_.label) for o_ in lid.engrave if o_ is not t])
        m.margin(f'engraving {t.label}: space to the next label ({who})', word, LABEL_GAP)
        b = t.bounds
        m.margin(f'engraving {t.label}: on the flat top, inside the chamfer', min(edge - b[4], b[1] + edge, HX - CH_TOP - b[3], b[0] + HX - CH_TOP), 1.0)
        over = [p.label for p in pockets if gap2d(Box(*t.bounds), p) < 0.5]
        m.margin(f'engraving {t.label}: pockets under it ({", ".join(over) or "none"})', len(over), 0, kind='max')
        knobs = [Cyl('z', x, y, 0, 1, L.KNOB16['d'] / 2) for x, y, _ in knob_at]
        m.margin(f'engraving {t.label}: outside every knob', min(gap2d(Box(*b), k) for k in knobs), 0.5)

    # ---- lid margins
    web, who = ekit.narrowest_web(lid_cuts)
    m.margin(f'lid: narrowest web between through-cuts ({who})', web, MIN_WEB)
    for ref, (x, y, name, res) in pots.items():
        near = min(gap2d(Cyl('z', x, y, 0, 1, res['washer_r']), c) for c in lid_cuts if not c.label.startswith(ref + ' '))
        m.margin(f'{ref} (pot {name}): flat lid around the washer', near, 0.8)
    near = min(gap2d(Cyl('z', ex, ey, 0, 1, enc['washer_r']), c) for c in lid_cuts if not c.label.startswith(ENC[0] + ' '))
    m.margin(f'{ENC[0]} (encoder): flat lid around the washer', near, 0.8)
    lows = [(e[2].bounds[2] - e[3], e[1]) for e in m.envelopes] + [(k[2].bounds[2], k[1]) for k in m.keepclear if k[0] == 'wiring']
    b_, who = min(lows)
    m.margin(f'floor below the deepest part or wire ({who})', b_ - FT, 1.0)
    m.conflicts += [('knobs', 'button caps'), ('USB plug', 'jack outside'), ('pin access', 'C3 board'),
                    ('wiring', 'gyro'), ('wiring', 'jack'), ('wiring', 'buttons'), ('wiring', 'C3 board')]
    m.info.append(f'outside {2 * HX:.0f} x {2 * HY:.0f} x {LT - FB:.1f} mm; knobs up to '
                  f'{d["washer_t"] + d["nut_t"] + d["nut_t_tol"] + L.KNOB16["h"]:.0f} mm above the lid')
    return m


# ------------------------------------------------------------------ for the renders only

def render_extras():
    d, k, e = L.RD901F, L.KNOB16, L.EC11
    z0 = LT + d['washer_t'] + d['nut_t']
    o = L.OLED042
    oy = BOARD_EDGE - C['oled_y']
    zb, zt = Z_P + L.TAPE['t'], Z_P + L.TAPE['t'] + 1.0
    g = L.GY521
    gx, gy = GY_C
    ref, jx, jz = JACK
    jb = L.JACK14
    face = BOARD_EDGE + C['usb_over']
    ax = zt + C['usb_h'] / 2
    boxes = [
        dict(name='C3 board', box=[-C['w'] / 2, BOARD_EDGE - C['l'], zb, C['w'] / 2, BOARD_EDGE, zt], color=(0.35, 0.08, 0.45)),
        dict(name='C3 OLED', box=[-o['w'] / 2, oy - o['d'] / 2, zt, o['w'] / 2, oy + o['d'] / 2, zt + 2.0], color=(0.02, 0.02, 0.03)),
        dict(name='C3 USB-C', box=[-C['usb_w'] / 2, face - C['usb_len'], zt, C['usb_w'] / 2, face, zt + 3.2], color=(0.7, 0.7, 0.72)),
        dict(name='USB plug grip', box=[-6.0, face, ax - 3.0, 6.0, face + 22, ax + 3.0], color=(0.1, 0.1, 0.11)),
        dict(name='tape', box=[-6, BOARD_EDGE - 20, Z_P, 6, BOARD_EDGE - 4, zb], color=(0.9, 0.9, 0.85)),
        dict(name='MPU-6050', box=[gx - g['w'] / 2, gy - g['l'] / 2, GY_BOT, gx + g['w'] / 2, gy + g['l'] / 2, GY_BOT + g['t']],
             color=(0.1, 0.25, 0.6)),
        dict(name='jack body', box=[jx - 7.9, IY - 21.1, jz - 7.9, jx + 7.9, IY, jz + 7.9], color=(0.15, 0.15, 0.16)),
        dict(name='jack plug', box=[jx - 7.0, HY + 3.2, jz - 7.0, jx + 7.0, HY + 40, jz + 7.0], color=(0.08, 0.08, 0.09)),
        dict(name='encoder body', box=[ENC[1] - e['body_x'] / 2, ENC[2] - e['body_y'] / 2, -e['depth'], ENC[1] + e['body_x'] / 2,
                                      ENC[2] + e['body_y'] / 2, 0], color=(0.6, 0.6, 0.62)),
    ]
    for _, _, x, y in POTS:
        boxes.append(dict(name='pot body', box=[x - 4.75, y - 4.85, -d['mount'], x + 4.75, y + 6.5, 0], color=(0.1, 0.3, 0.12)))
    for _, _, x, y in BUTTONS:
        b = L.BUTTON7
        boxes.append(dict(name='button body', box=[x - 4.5, y - 4.5, -16.0, x + 4.5, y + 4.5, 0], color=(0.12, 0.12, 0.12)))
        boxes.append(dict(name='button nut', box=[x - 4.5, y - 4.5, LT, x + 4.5, y + 4.5, LT + 2.0], color=(0.75, 0.75, 0.76)))
        boxes.append(dict(name='button cap', box=[x - 3.0, y - 3.0, LT + 2.0, x + 3.0, y + 3.0, LT + 6.0], color=(0.8, 0.15, 0.1)))
    knobs = [dict(x=x, y=y, angle=a) for (_, _, x, y), a in zip(POTS, (60, -20, 100))] + [dict(x=ENC[1], y=ENC[2], angle=0)]
    return dict(
        lid_top=LT, washer_t=d['washer_t'], washer_od=d['washer_od'], nut_t=d['nut_t'], nut_af=d['nut_af'],
        knob_d=15.0, knob_z0=z0 + 0.3, knob_z1=z0 + 0.3 + 16.0, knobs=knobs, boxes=boxes,
        views=[
            dict(name='01-assembled.png', cam=(150, -170, 150), target=(0, 0, -8), lens=55),
            dict(name='02-top.png', cam=(0, 0, 300), target=(0, 0.0001, 0), ortho=126, size=(1500, 1000)),
            dict(name='03-far-wall.png', cam=(0, 300, -12), target=(0, 0, -12), ortho=126, size=(1500, 600),
                 hide=['USB plug', 'jack plug']),
            dict(name='04-lid-off.png', cam=(110, -150, 170), target=(0, 5, -20), lens=50, hide_parts=['lid'],
                 hide=['knob', 'washer', 'nut', 'Cube', 'button', 'pot body', 'encoder body']),
            dict(name='05-exploded.png', cam=(160, -190, 190), target=(0, 0, 0), lens=50, explode=45, hide='all'),
            dict(name='06-section-c3.png', cam=(-300, 22, -8), target=(0, 22, -8), ortho=80, size=(1500, 900),
                 section=dict(axis='x', value=0.0), hide=['knob', 'washer', 'nut']),
            dict(name='07-section-pots.png', cam=(0, -300, -10), target=(0, -6, -10), ortho=126, size=(1500, 700),
                 section=dict(axis='y', value=-6.0), hide=['button cap', 'USB plug', 'jack plug']),
            dict(name='08-section-jack.png', cam=(-300, 20, -13), target=(-34, 20, -13), ortho=80, size=(1500, 900),
                 section=dict(axis='x', value=-34.0), hide=['knob', 'washer', 'nut']),
        ])
