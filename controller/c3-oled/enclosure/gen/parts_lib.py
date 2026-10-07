"""Component library: every part that meets the enclosure, as solids built from its maker's
drawings. Each part keeps its numbers in one dict: nominal values, the tolerance or the spread
between makers, and the sources (SPEC.md section 3 has the URLs, the second source for every value
and the disagreements). Envelopes are drawn at maximum material (nominal plus tolerance), then the
judge grows them by their clearance, which is for printing.

A library function returns a dict:
    envelopes   [(group, label, solid, clearance)]   the part's body: no printed part may touch it
    keep        [(group, label, solid)]              what must stay empty for it to be used
    lid_cuts    [solid]                              through-cuts it needs in the lid
    lid_pockets [solid]                              blind pockets in the lid's underside
    wall_cuts   [solid]                              openings it needs in the shell's walls
    kicad       (x0, y0, z0, x1, y1, z1) or None     its nominal body, seated on the board, to compare
                                                     with KiCad's 3D model (the measurement check)
Frame: the enclosure frame of ekit (mm, X right, Y away from the player, Z = 0 on the board top).

Nothing here depends on a board: a part is placed by coordinates, so a spec without a board uses
the same functions (spec_noboard.py).
"""
from ekit import Box, Cyl

# ------------------------------------------------------------------ numbers, with their sources
RD901F = dict(          # Alpha RD901F-40 9 mm vertical pot (15R1: 15 mm plain D6.35 shaft; 15K: knurled D6.0 T18)
    body_w=9.5, body_w_tol=0.3,                       # across the pins axis
    body_far=4.85, body_pins=6.5, body_tol=0.3,       # shaft axis to the far face / to the pin face
    lugs=12.0, lugs_tol=0.5, lug_slot=(1.8, 1.1),     # bracket lugs across (on the axis line), board slots
    mount=10.0, mount_tol=0.5,                        # mounting face (bushing start) above the board
    bushing_d=7.0, bushing_l=5.0, bushing_tol=0.3,    # M7 x 0.75
    shaft_d=6.35, shaft_l=15.0, shaft_tol=0.5,        # from the mounting face; 15K: D6.0
    pin_row=7.5, pin_pitch=2.5, pin=(0.9, 0.3), tail=3.5, tail_tol=0.3,   # tail: below the seating plane
    tab=dict(w=2.0, r0=6.0, r1=6.8, h=0.8, h_tol=0.2),    # anti-rotation tab (-00DL1 only), pin side
    nut_af=10.0, nut_af_max=11.0, nut_t=2.0, nut_t_tol=0.3,      # Alpha 10 AF (Alps and Small Bear 11)
    washer_od=12.0, washer_od_tol=0.5, washer_id=7.1, washer_t=0.5,
    src='Alpha drawings SLH-211-433 (RD901F-40-(L)R1) and RD901F-40-15K T18; Bourns PDB184 and Alps RK097 (nut, washer)')
HEADER = dict(          # 2.54 mm female header, single row, 8.5 mm
    pitch=2.54, w=2.5, w_tol=0.2, extra=0.51, extra_tol=0.5,    # length = n * pitch + extra
    h=8.51, h_tol=0.2, tail=3.2, tail_tol=0.3,
    src='Harwin M20-782, Samtec SSW, Wurth 6130xx11821, BOOMELE C27438 drawings (worst of the four)')
DUPONT = dict(          # 1-pin male housing
    l=14.0, l_tol=0.25, w=2.54, w_max=3.04, pin=6.08,
    src='Pololu 1900 drawing (2.54 x 14.00); Harwin M20-106 (2.74 +-0.30 x 2.50); FCI Mini-PV 13.84')
DC005 = dict(           # BOOMELE DC-005 2.0, 5.5 x 2.1 mm right-angle jack (LCSC C16214)
    l=14.2, l_max=14.4, w=9.0, h=11.0, tol=0.3, axis_h=6.5, bore=6.3, tail=4.1, tail_tol=0.3,
    sleeve_contact=7.5,    # BOOMELE: spring tab 7.5 in; Same Sky PJ-002A: about 6.0
    src='BOOMELE DC-005 drawing (C16214); Same Sky PJ-002A; Tayda A-4118')
DC_PLUG = dict(barrel_d=5.5, barrel_l=9.5, barrel_l_tol=0.3, grip_d=11.0,
               src='Same Sky PP3-002A (barrel 9.5 +-0.3, grip 7.8-8.2); Switchcraft S760 (9.50); Tensility 10-02222 (up to 11)')
PJ320D = dict(          # Korean Hroparts PJ-320D-4A 3.5 mm SMD jack (LCSC C95562)
    l=12.1, w=6.1, h=5.0, tol=0.1, nose_d=5.0, nose_l=2.0, axis_h=2.5, hole=3.6, tabs=8.3,
    src='Hroparts PJ-320D-4A drawing (C95562); Shou Han PJ-320D (C431535); axis: the nose resting on the board')
TRS_PLUG = dict(shaft_d=3.5, shaft_l=14.0, shaft_l_min=13.61, body_d=10.3, body_d_tol=0.5, body_d_min=6.1,
                src='Same Sky MP-3502 (14 +-0.3); Switchcraft 35HD (14.22 +-0.61, body 10.3 +-0.5); Tensility (14.5 +-0.5)')
KNOB = dict(d=13.0, h=16.02, bore_min=12.7, bore_max=14.86,
            src='Davies 1900H (12.70 x 15.88, 12.70 deep); Tayda A-4559 (12.7 x 15.9, 14.3 deep); Tayda A-8657 (13 x 16.02, 14.86 deep)')
M3_SHCS = dict(d=3.0, head_d=5.5, head_d_max=5.68, head_h=3.0, lengths=(5, 6, 8, 10, 12, 16, 20, 25, 30), l_tol=0.35,
               clear=3.4, cbore_d=6.6, cbore_h=3.5,
               src='ISO 4762 (head 5.50 max, 5.68 knurled; k 3.00); ISO 273 medium 3.4; DIN 974-1 counterbore 6.5 x 3.4, +0.1 for printing')
INSERT = dict(od=4.6, l=5.7, hole=4.0, hole_depth=7.5, min_depth=6.7, min_wall=1.6,
              src='Ruthex RX-M3x5.7 and CNC Kitchen M3x5.7 (the same numbers): hole 4.0, depth >= L + 1, wall >= 1.6')
FEET = dict(d=7.9, h=2.2, tol=0.5, src='3M Bumpon SJ5302 data sheet (+-0.5); Digikey 7.92 x 2.16')
BOARD = dict(t=1.6, t_tol=0.16, outline_tol=0.2, src='JLCPCB capabilities: 1.6 mm +-10 %, routed outline +-0.2')
PP3 = dict(             # 9 V battery, IEC 6LR61 / 6F22 (alkaline, lithium, NiMH)
    h1=48.5, h1_min=46.5, h1_max=49.0,               # overall, with the terminals; 49.0: Energizer L522 lithium
    h6=46.4,                                          # the shoulder (the body without the terminals), max
    w=26.5, w_min=24.5, d=17.5, d_min=15.5, spacing=12.7, spacing_tol=0.25,
    src='IEC 60086-2 via GOST R IEC 60086-2-2019; Energizer 522, L522 (49.0), NH22-175; Duracell MN1604; Varta 4922, 56722; '
        'Panasonic 6LF22XWA; ANSI C18.1M')
PP3_SNAP = dict(        # hard-shell T-type snap (the wires leave from the middle of a long side)
    l=24.4, w=13.0, h=8.1, h_max=8.5, tol=0.2, spacing=12.9,
    src='Tayda DCK4 drawing (24.4 x 13 x 8.1, general tolerance +-0.2); Keystone K75 catalogue: 81-x T-type 24.4, 7.9 tall; '
        '84-x I-type 25.0 x 13.0 x 8.5')
WIRE = dict(awg26=1.30, awg24=1.42, awg22=1.57, tol=0.05,
            src='Alpha Wire 3049-3051 (UL1007); Remington Industries UL1007; Farnell UL1007 data sheet')


def empty():
    return dict(envelopes=[], keep=[], lid_cuts=[], lid_pockets=[], wall_cuts=[], kicad=None, info=[])


# ------------------------------------------------------------------ parts

def pot_rd901f(ref, x, y, pins_dir, lid_under, lid_top, knob_name, clearance=0.3):
    """A 9 mm pot on the board, shaft at (x, y), pins toward pins_dir (0, -1) or (0, 1). As fitted: the
    nut pulls the pot up until its mounting face meets the lid's underside, before it is soldered,
    so the mounting face is at lid_under whatever its height tolerance."""
    d = RD901F
    out = empty()
    s = -1 if pins_dir[1] < 0 else 1
    t = d['body_tol']
    hw = d['body_w'] / 2 + d['body_w_tol'] / 2
    y_pins, y_far = y + s * (d['body_pins'] + t), y - s * (d['body_far'] + t)
    top = lid_under - 1e-3                             # the face the lid bears on: no clearance there
    body = Box(x - hw - clearance, min(y_pins, y_far) - clearance, 0, x + hw + clearance, max(y_pins, y_far) + clearance, top,
               label=f'{ref} body')
    lh = d['lugs'] / 2 + d['lugs_tol'] / 2
    lugs = Box(x - lh - clearance, y - d['lug_slot'][0] / 2 - 0.2 - clearance, 0, x + lh + clearance,
               y + d['lug_slot'][0] / 2 + 0.2 + clearance, top, label=f'{ref} lugs')
    py = y + s * d['pin_row']
    terms = Box(x - d['pin_pitch'] - d['pin'][0], min(py + s * 0.6, y_pins), 0, x + d['pin_pitch'] + d['pin'][0],
                max(py + s * 0.6, y_pins), 3.0, label=f'{ref} terminals')
    bush_top = lid_under + d['bushing_l'] + d['bushing_tol']
    out['envelopes'] += [
        ('pot bodies', f'{ref} body', body, 0),               # clearance drawn in: none on the face the lid bears on
        ('pot bodies', f'{ref} lugs', lugs, 0),               # the same: the lugs end at the mounting face
        ('pot bodies', f'{ref} terminals', terms, clearance),
        ('pot bushings', f'{ref} M7 bushing', Cyl('z', x, y, lid_under, bush_top, d['bushing_d'] / 2), clearance),
        ('pot shafts', f'{ref} shaft', Cyl('z', x, y, bush_top, lid_under + d['shaft_l'] + d['shaft_tol'], d['shaft_d'] / 2),
         clearance),
        ('THT tails', f'{ref} pins', Box(x - hw, min(py, y), -(d['tail'] + d['tail_tol']), x + hw, max(py, y), -1.44), 0.5),
    ]
    out['lid_cuts'].append(Cyl('z', x, y, lid_under - 1, lid_top + 1, d['bushing_d'] / 2 + clearance + 0.05,
                               label=f'{ref} bushing hole'))
    # washer and nut on the lid; the knob pushed down onto the nut
    stack_min = d['washer_t'] + d['nut_t'] - d['nut_t_tol']
    stack_max = d['washer_t'] + d['nut_t'] + d['nut_t_tol']
    out['keep'] += [
        ('pot nuts', f'{ref} washer and nut', Cyl('z', x, y, lid_top, lid_top + stack_max,
                                                   max(d['washer_od'] + d['washer_od_tol'], d['nut_af_max'] * 1.1547) / 2 + 0.3)),
        ('knobs', f'{ref} knob ({knob_name})', Cyl('z', x, y, lid_top + stack_min, lid_top + stack_max + KNOB['h'] + 0.5,
                                                    KNOB['d'] / 2 + 0.5)),
    ]
    # nominal body seated on the board, as KiCad's model draws it
    out['kicad'] = (x - d['body_w'] / 2, min(y + s * d['body_pins'], y - s * d['body_far'], py + s * 0.4), 0,
                    x + d['body_w'] / 2, max(y + s * d['body_pins'], y - s * d['body_far'], py + s * 0.4), d['mount'] + d['shaft_l'])
    # thread for the nut: the shortest bushing, above the lid and the washer
    out['thread'] = min(d['nut_t'], d['bushing_l'] - d['bushing_tol'] - (lid_top - lid_under) - d['washer_t'])
    out['shaft_in_knob'] = d['shaft_l'] - d['shaft_tol'] - (lid_top - lid_under) - stack_max
    out['lift_max'] = lid_under - (d['mount'] - d['mount_tol'])
    return out


def header_strip(ref, pins, lid_top=None):
    """A female header strip; pins = [(x, y)] socket centres in order, in a row along X."""
    d = HEADER
    out = empty()
    xs, ys = [p[0] for p in pins], [p[1] for p in pins]
    ex = (d['extra'] + d['extra_tol']) / 2 + d['pitch'] / 2         # beyond the end pins, at maximum material
    hw = (d['w'] + d['w_tol']) / 2
    body = Box(min(xs) - ex, min(ys) - hw, 0, max(xs) + ex, max(ys) + hw, d['h'] + d['h_tol'], label=f'{ref} body')
    out['envelopes'] += [('headers', f'{ref} body', body, 0.3),
                         ('THT tails', f'{ref} tails', Box(body.x0, body.y0, -(d['tail'] + d['tail_tol']), body.x1, body.y1, -1.44),
                          0.5)]
    p = DUPONT
    for i, (x, y) in enumerate(pins):
        out['keep'].append(('jumper plugs', f'{ref}.{i + 1} jumper plug',
                            Box(x - p['w_max'] / 2, y - p['w_max'] / 2, d['h'], x + p['w_max'] / 2, y + p['w_max'] / 2,
                                d['h'] + p['l'] + p['l_tol'])))
    out['kicad'] = (min(xs) - d['pitch'] / 2 - d['extra'] / 2, min(ys) - d['w'] / 2, 0, max(xs) + d['pitch'] / 2 + d['extra'] / 2,
                    max(ys) + d['w'] / 2, d['h'])
    out['body'] = body
    out['plugs'] = (min(xs) - p['w_max'] / 2, min(ys) - p['w_max'] / 2, max(xs) + p['w_max'] / 2, max(ys) + p['w_max'] / 2)
    return out


def dc005(ref, front_x, axis_y, clearance=0.3):
    """The DC jack lying on the board, its front face at front_x, opening toward +X."""
    d = DC005
    out = empty()
    t = d['tol']
    body = Box(front_x - d['l_max'], axis_y - d['w'] / 2 - t, 0, front_x + 0.1, axis_y + d['w'] / 2 + t, d['h'] + t,
               label=f'{ref} body')
    out['envelopes'] += [('jacks', f'{ref} body', body, clearance),
                         ('THT tails', f'{ref} tabs', Box(body.x0, body.y0, -(d['tail'] + d['tail_tol']), body.x1, body.y1, -1.44),
                          0.5)]
    p = DC_PLUG
    plug = Cyl('x', axis_y, d['axis_h'], front_x, front_x + 40, p['grip_d'] / 2, label='DC plug grip')
    out['keep'].append(('DC plug', 'DC plug (grip up to the jack face)', plug))
    r = p['grip_d'] / 2 + 0.4
    out['open'] = dict(y=axis_y, z=d['axis_h'], r=r)
    out['kicad'] = (front_x - d['l'], axis_y - d['w'] / 2, 0, front_x, axis_y + d['w'] / 2, d['h'])
    out['plug_r'], out['open_r'] = p['grip_d'] / 2, r
    out['insertion'] = p['barrel_l'] - p['barrel_l_tol']       # the grip reaches the face: the whole barrel is in
    out['need_insertion'] = d['sleeve_contact'] + 1.0
    out['body'] = body
    return out


def pj320d(ref, nose_x, axis_y, stop_x=None, clearance=0.3):
    """The 3.5 mm jack on the board, the tip of its round nose at nose_x, opening toward +X. stop_x:
    where a plug's body is stopped before the nose (the board's edge, when the nose is inside it)."""
    d = PJ320D
    out = empty()
    front = nose_x - d['nose_l']
    t = d['tol']
    body = Box(front - d['l'] - t, axis_y - d['w'] / 2 - t, 0, front, axis_y + d['w'] / 2 + t, d['h'] + t, label=f'{ref} body')
    nose = Cyl('x', axis_y, d['axis_h'], front - 0.01, nose_x + t, d['nose_d'] / 2 + 0.05, label=f'{ref} nose')
    out['envelopes'] += [('jacks', f'{ref} body', body, clearance), ('jacks', f'{ref} nose', nose, clearance)]
    p = TRS_PLUG
    rb = (p['body_d'] + p['body_d_tol']) / 2
    out['keep'].append(('phones plug', '3.5 mm plug body (up to the nose)',
                        Cyl('x', axis_y, d['axis_h'], nose_x, nose_x + 40, rb)))
    r = rb + 0.4
    out['open'] = dict(y=axis_y, z=d['axis_h'], r=r)
    out['kicad'] = None                          # KiCad has no 3D model for it; the fab outline is compared instead
    out['fab'] = (front - d['l'], axis_y - d['w'] / 2, nose_x, axis_y + d['w'] / 2)
    out['plug_r'], out['open_r'] = rb, r
    out['nose_x'] = nose_x
    out['short'] = max(0.0, (stop_x or nose_x) - nose_x) if p['body_d_min'] / 2 > d['axis_h'] else 0.0
    return out


def led_window(ref, x, y, top_z, lid_top):
    out = empty()
    out['keep'].append(('LED light', f'{ref} light path', Cyl('z', x, y, top_z, lid_top + 1, 0.8)))
    out['lid_cuts'].append(Cyl('z', x, y, -1, lid_top + 1, 1.2, label=f'{ref} window'))
    return out


def generic_body(ref, bounds, clearance=0.3, group='components'):
    """Any other part: its KiCad 3D model's box (the tallest ones get datasheet numbers above)."""
    out = empty()
    out['envelopes'].append((group, ref, Box(*bounds, label=ref), clearance))
    return out


def m3_screw(x, y, seat_z, length):
    """An ISO 4762 M3 screw driven up from a counterbore whose ceiling is at seat_z."""
    s = M3_SHCS
    return [('screws', f'M3x{length} head at ({x:.1f}, {y:.1f})', Cyl('z', x, y, seat_z - s['head_h'], seat_z, s['head_d_max'] / 2 + 0.2)),
            ('screws', f'M3x{length} shank at ({x:.1f}, {y:.1f})', Cyl('z', x, y, seat_z, seat_z + length + s['l_tol'], s['d'] / 2))]


def pp3_battery(x0, yc, z0, clearance=0.3):
    """A PP3 lying flat, its base at x0, its terminals toward +X, centred on yc, on the plane z0. The
    body is drawn to its shoulder at maximum size; the terminals are inside the snap's envelope."""
    d = PP3
    out = empty()
    c = clearance                                        # drawn in: none under it, where it rests on z0
    out['envelopes'].append(('battery', 'PP3 body (largest)', Box(x0 - c, yc - d['w'] / 2 - c, z0, x0 + d['h6'] + c,
                                                                  yc + d['w'] / 2 + c, z0 + d['d'] + c), 0))
    out['slack_y'] = d['w'] - d['w_min']                 # how much smaller the smallest battery is
    out['slack_z'] = d['d'] - d['d_min']
    return out


def pp3_snap_t(x_shoulder, yc, zc, clearance=0.3):
    """A hard-shell T-type snap clipped on a PP3 whose shoulder is at x_shoulder (terminals toward +X),
    its long side across Y, centred on (yc, zc). Its seated height is not published: the envelope runs
    from the shoulder (seated) to the tallest shell on the longest terminals (not seated), a bound."""
    d, b = PP3_SNAP, PP3
    out = empty()
    hl, hw = d['l'] / 2 + d['tol'], d['w'] / 2 + d['tol']
    x1 = x_shoulder + (b['h1_max'] - b['h6']) + d['h_max']
    out['envelopes'].append(('snap', 'PP3 snap (seated or not)', Box(x_shoulder, yc - hl, zc - hw, x1, yc + hl, zc + hw), clearance))
    out['x1'], out['top'] = x1, zc + hw
    out['exit'] = (x_shoulder + (x1 - x_shoulder) / 2, yc, zc + hw)     # the wires leave the middle of a long side
    return out


# ================================================================== the SignalPatch C3 controller's parts
# Panel-mounted through the lid or a wall, wired by hand (no board). Frame of the C3 spec: Z = 0 is
# the lid's underside, so these parts hang below it. SPEC.md section 3 has the second source for
# each value and the disagreements.

RD901F_PANEL_WIRE = 5.0          # room below the pins for soldering and bending the wires

EC11 = dict(            # rotary encoder with push switch and M7 x 0.75 bushing
    body_x=12.5, body_y=13.4, body_tol=0.3,            # Bourns PEC11R drawing (Alps EC11E: 11.7 x 12.0, smaller)
    depth=7.0, depth_tol=0.3,                           # mounting face to the pin seating plane, switch version
    lugs=11.2, lug_w=2.0,                               # bracket lugs on the body's sides (KiCad EC11E footprint: pads 11.2 apart)
    pins_abc=7.5, pins_sw=7.0, pin_pitch=2.5, pin_l=5.0, pin_tol=0.3,   # KiCad EC11E footprint; pin length PEC11R
    bushing_d=7.0, bushing_l=5.0, bushing_l_long=7.0, bushing_tol=0.3,  # PEC11R: LB 5 (L 15) or 7 (L 20)
    shaft_d=6.0, shaft_l=20.0, shaft_tol=0.5,
    nut_af=10.0, nut_af_tol=0.5, nut_t=2.0, nut_t_tol=0.2, washer_od=12.0, washer_od_tol=0.3, washer_t=0.5,
    src='Bourns PEC11R datasheet rev 04/26 (body, bushing, nut, washer, shaft); KiCad '
        'RotaryEncoder_Alps_EC11E-Switch_Vertical_H20mm footprint (pins, lugs)')
BUTTON7 = dict(         # 7 mm panel-mount momentary push button (PBS-110 type), sold without a maker drawing
    hole=7.0, thread_d=7.0, body_d=10.0, nut_af=9.0, nut_t=2.0, cap_d=7.35,
    length_max=29.0, above_min=5.0,                     # overall 25-29 mm; at least 5 mm (nut and cap) above the panel
    src='sigmanortec PBS-110 listing (25 x 9 mm, hole 7, cap 6); uelectronics PBS-110 (27 x 7.35); Maplin mini momentary '
        '(29 x 9.5, 7 mm cut-out). No maker drawing found: worst case of the three')
JACK14 = dict(          # 1/4" (6.35 mm) stereo panel jack, enclosed (Switchcraft 112BX; Cliff S4 also fits the envelope)
    hole=11.1, bushing_d=9.53, bushing_l=7.0,           # 3/8"-32 x 0.276" (112BX); Cliff S4 needs an 11.1 mm hole
    body_w=21.0, body_h=23.0, body_l=28.0,              # 112BX 15.7 x 15.7 x 21.1 + lugs; Cliff S4 20.1 x 23.0 x 22.8
    nut_af=12.7, nut_t=2.4, washer_od=14.3, washer_t=0.8,
    plug_d=20.0,                                        # the plug's body outside: moulded plugs up to about 20 mm
    src='Switchcraft 112BX listing (3/8"-32, 0.375" hole, bushing 0.276", body 0.62 x 0.62 x 0.831"); Cliff S4 listing '
        '(23.0 x 22.8 x 20.1 mm, 0.438" hole, nut 11.3)')
C3OLED = dict(          # ABrobot "ESP32-C3 OLED" board (ESP32-C3 SuperMini with a 0.42" OLED)
    l=25.0, w=20.5, tol=0.2,                            # maker drawing 25.00 x 20.50; emalliab 24.8 x 20.45
    t_min=0.8, t_max=1.6,                               # not published: the usual JLC range
    rows=17.78, pitch=2.54, pins=8, pin_first=2.5,      # rows 17.78 apart; first pin 2.5 mm from the USB edge
    usb_w=9.0, usb_h=3.4, usb_len=7.35, usb_over=2.0, usb_over_tol=0.5,   # top-mounted receptacle, overhang 27 - 25 mm
    oled_y=13.5, oled_place_tol=0.5,                    # panel centre from the USB edge (two photos)
    oled_h_min=1.25, oled_h_max=2.6,                    # glass top above the board: the panel alone, or on the QFN and tape
    btn_u=(4.0, 16.5), btn_v=1.3, btn=(3.0, 4.2), btn_h=2.5,   # BOOT and RST at the edge opposite the USB, top side
    led_u=(4.5, 16.0), led_v=3.8,                       # IO8 (blue) and PWR (red) LEDs, from the USB edge
    parts_h=1.5,                                        # everything else on the top side (chip, antenna, fillets)
    src='ABrobot dimension drawing and pinout (github.com/zhuhai-esp/ESP32-C3-ABrobot-OLED, Document/); '
        'emalliab.wordpress.com 2025-02-12 (24.8 x 20.45, photos); karamo/ESP32-C3-mini-with-0.42-OLED (27 x 20 with the USB)')
OLED042 = dict(         # Newvision N042-7240TSWEG01-H16 0.42" 72 x 40 OLED
    w=12.0, d=11.0, tol=0.15, h=1.25, h_tol=0.1, aa_w=9.196, aa_d=5.18, aa_edge=1.375,
    src='Newvision N042-7240TSWEG01-H16 spec ver A, section 1.4 mechanical drawing (general tolerance +-0.3)')
USBC_PLUG = dict(shell_w=8.4, shell_h=2.6, grip_w=12.5, grip_h=6.6, mate=6.65,
                 src='Amphenol Socapex type-C overmoulding limits (12.5 x 6.6 max); USB Type-C spec: plug shell 8.25 x 2.4 '
                     'nominal, mated depth 6.65 (GCT USB4130 drawing)')
GY521 = dict(           # MPU-6050 breakout
    l=20.3, w=15.6, l_max=21.0, w_max=16.0, tol=0.2, t=1.6, parts_h=1.2,
    hole_edge=1.8, hole_pitch=16.6, hole_d_min=2.5, pin_edge=1.55, pins=8, pitch=2.54,
    src='KiCad footprints ciaa/Ponchos GY-521 (16.00 x 20.26, holes 2.5 at 1.8 from the corners, 16.63 apart) and '
        'opatiny/micromouse GY-521 (15.88 x 20.95); openelab listing (20.5 x 15.5, holes 3.2)')
M2_SELF = dict(d=2.0, pilot=1.8, head_d=4.0, head_h=1.6, lengths=(4, 5, 6, 8, 10), l_tol=0.3,
               src='Bambu Lab forum (M2 vertical hole 1.84); accu-components self-tapping chart (M2.5 in PLA 2.05): pilot '
                   'between minor and major diameter')
TAPE = dict(t=1.0, t_min=0.6, t_max=1.2, src='1 mm double-sided foam tape: 0.8-1.2 as sold, down to 0.6 pressed')
KNOB16 = dict(d=16.0, h=18.0, src='kit KNOB (13 x 16.02) and the common 15 x 17 aluminium knobs: up to 16 x 18')


def pot_rd901f_panel(ref, x, y, pins_dir, lid_top, knob_name, clearance=0.3):
    """A 9 mm pot panel-mounted from below, its mounting face on the lid's underside (Z = 0), wired by
    hand. As the board-mounted version, but the body hangs below the lid."""
    d = RD901F
    out = empty()
    s = 1 if pins_dir[1] > 0 else -1
    t = d['body_tol']
    hw = d['body_w'] / 2 + d['body_w_tol'] / 2
    bottom = -(d['mount'] + d['mount_tol'])
    y_pins, y_far = y + s * (d['body_pins'] + t), y - s * (d['body_far'] + t)
    top = -1e-3                                            # the face the lid bears on: no clearance there
    out['envelopes'] += [
        ('pot bodies', f'{ref} body', Box(x - hw - clearance, min(y_pins, y_far) - clearance, bottom - clearance,
                                          x + hw + clearance, max(y_pins, y_far) + clearance, top), 0),
        ('pot bodies', f'{ref} lugs', Box(x - d['lugs'] / 2 - d['lugs_tol'] / 2 - clearance, y - 1.3 - clearance, bottom - clearance,
                                          x + d['lugs'] / 2 + d['lugs_tol'] / 2 + clearance, y + 1.3 + clearance, top), 0),
    ]
    py = y + s * d['pin_row']
    terms = Box(x - d['pin_pitch'] - d['pin'][0], min(py + s * 0.6, y_pins), bottom - d['tail'] - d['tail_tol'],
                x + d['pin_pitch'] + d['pin'][0], max(py + s * 0.6, y_pins), bottom + 3.0, label=f'{ref} terminals')
    out['envelopes'].append(('pot bodies', f'{ref} terminals', terms, clearance))
    out['keep'].append(('wiring', f'{ref} wires', Box(terms.x0 - 0.5, terms.y0 - 1.0, terms.z0 - RD901F_PANEL_WIRE,
                                                      terms.x1 + 0.5, terms.y1 + 1.5, terms.z1)))
    bush_top = d['bushing_l'] + d['bushing_tol']
    out['envelopes'] += [('pot bushings', f'{ref} M7 bushing', Cyl('z', x, y, 0, bush_top, d['bushing_d'] / 2), clearance),
                         ('pot shafts', f'{ref} shaft', Cyl('z', x, y, bush_top, d['shaft_l'] + d['shaft_tol'], d['shaft_d'] / 2),
                          clearance)]
    out['lid_cuts'].append(Cyl('z', x, y, -1, lid_top + 1, d['bushing_d'] / 2 + clearance + 0.05, label=f'{ref} bushing hole'))
    tb = d['tab']                                          # anti-rotation tab (-00DL1 types): a pocket so the face sits flat
    out['lid_pockets'].append(Box(x - tb['w'] / 2 - 0.3, y + s * (tb['r0'] - 0.3), -1, x + tb['w'] / 2 + 0.3, y + s * (tb['r1'] + 0.3),
                                  tb['h'] + tb['h_tol'] + 0.1, label=f'{ref} tab pocket'))
    stack_min = d['washer_t'] + d['nut_t'] - d['nut_t_tol']
    stack_max = d['washer_t'] + d['nut_t'] + d['nut_t_tol']
    k = KNOB16
    out['keep'] += [
        ('pot nuts', f'{ref} washer and nut', Cyl('z', x, y, lid_top, lid_top + stack_max,
                                                   max(d['washer_od'] + d['washer_od_tol'], d['nut_af_max'] * 1.1547) / 2 + 0.3)),
        ('knobs', f'{ref} knob ({knob_name})', Cyl('z', x, y, lid_top + stack_min, lid_top + stack_max + k['h'] + 0.5, k['d'] / 2 + 0.5)),
    ]
    out['thread'] = min(d['nut_t'], d['bushing_l'] - d['bushing_tol'] - lid_top - d['washer_t'])
    out['shaft_in_knob'] = d['shaft_l'] - d['shaft_tol'] - lid_top - stack_max
    out['washer_r'] = (d['washer_od'] + d['washer_od_tol']) / 2
    out['bottom'] = terms.z0
    return out


def encoder_ec11_panel(ref, x, y, lid_top, clearance=0.3):
    """The encoder panel-mounted from below, mounting face on the lid's underside (Z = 0); its A-C-B pins
    toward -X, the switch pins toward +X, the bracket lugs on the +-Y sides."""
    d = EC11
    out = empty()
    hx, hy = d['body_x'] / 2 + d['body_tol'], d['body_y'] / 2 + d['body_tol']
    bottom = -(d['depth'] + d['depth_tol'])
    top = -1e-3
    out['envelopes'] += [
        ('encoder', f'{ref} body', Box(x - hx - clearance, y - hy - clearance, bottom - clearance, x + hx + clearance, y + hy + clearance,
                                       top), 0),
        ('encoder', f'{ref} lugs', Box(x - d['lug_w'] / 2 - clearance, y - d['lugs'] / 2 - 1.0 - clearance, bottom - 3.0,
                                       x + d['lug_w'] / 2 + clearance, y + d['lugs'] / 2 + 1.0 + clearance, top), 0),
    ]
    zt = bottom - d['pin_l'] - d['pin_tol']
    for side, off, n in ((-1, d['pins_abc'], 3), (1, d['pins_sw'], 2)):
        span = (n - 1) * d['pin_pitch'] / 2 + 0.6
        pins = Box(x + side * off - 0.8, y - span, zt, x + side * off + 0.8, y + span, bottom, label=f'{ref} pins')
        out['envelopes'].append(('encoder', f"{ref} {'A-C-B' if side < 0 else 'switch'} pins", pins, clearance))
        out['keep'].append(('wiring', f"{ref} {'A-C-B' if side < 0 else 'switch'} wires",
                            Box(pins.x0 - 1.0, pins.y0 - 1.0, zt - RD901F_PANEL_WIRE, pins.x1 + 1.0, pins.y1 + 1.0, bottom)))
    bl = d['bushing_l_long'] + d['bushing_tol']
    out['envelopes'] += [('encoder', f'{ref} M7 bushing', Cyl('z', x, y, 0, bl, d['bushing_d'] / 2), clearance),
                         ('encoder', f'{ref} shaft', Cyl('z', x, y, bl, d['shaft_l'] + d['shaft_tol'], d['shaft_d'] / 2), clearance)]
    out['lid_cuts'].append(Cyl('z', x, y, -1, lid_top + 1, d['bushing_d'] / 2 + clearance + 0.05, label=f'{ref} bushing hole'))
    stack_min = d['washer_t'] + d['nut_t'] - d['nut_t_tol']
    stack_max = d['washer_t'] + d['nut_t'] + d['nut_t_tol']
    k = KNOB16
    out['keep'] += [
        ('pot nuts', f'{ref} washer and nut', Cyl('z', x, y, lid_top, lid_top + stack_max,
                                                   max(d['washer_od'] + d['washer_od_tol'], (d['nut_af'] + d['nut_af_tol']) * 1.1547) / 2 + 0.3)),
        ('knobs', f'{ref} knob', Cyl('z', x, y, lid_top + stack_min, lid_top + stack_max + k['h'] + 0.5, k['d'] / 2 + 0.5)),
    ]
    out['thread'] = min(d['nut_t'], d['bushing_l'] - d['bushing_tol'] - lid_top - d['washer_t'])
    out['shaft_in_knob'] = d['shaft_l'] - d['shaft_tol'] - lid_top - stack_max
    out['washer_r'] = (d['washer_od'] + d['washer_od_tol']) / 2
    out['bottom'] = zt
    return out


def button7_panel(ref, x, y, lid_top, clearance=0.3):
    """A 7 mm momentary button through the lid, one nut above and one below (worst case of the listings)."""
    d = BUTTON7
    out = empty()
    below = d['length_max'] - d['above_min'] - lid_top          # under the lid's underside, at the longest
    r = max(d['body_d'], d['nut_af'] * 1.1547) / 2 + clearance      # the body, or the lower nut that bears on the lid
    out['envelopes'].append(('buttons', f'{ref} body and lower nut', Cyl('z', x, y, -below - clearance, -1e-3, r), 0))
    out['envelopes'].append(('buttons', f'{ref} thread', Cyl('z', x, y, 0, lid_top + 3, d['thread_d'] / 2), 0.2))
    out['keep'] += [('wiring', f'{ref} wires', Cyl('z', x, y, -below - 3.35, -below - 0.35, d['body_d'] / 2 + 1.0)),
                    ('button caps', f'{ref} nut and cap', Cyl('z', x, y, lid_top, lid_top + 10.0, d['nut_af'] * 1.1547 / 2 + 0.5))]
    out['lid_cuts'].append(Cyl('z', x, y, -1, lid_top + 1, d['hole'] / 2 + 0.25, label=f'{ref} hole'))
    out['bottom'] = -below - 3.35
    return out


def jack14_wall(ref, x, y_in, y_out, z, clearance=0.3):
    """A 1/4" panel jack through a wall facing +Y (inner face y_in, outer face y_out), axis at (x, z);
    its body inside, the washer and nut outside."""
    d = JACK14
    out = empty()
    body = Box(x - d['body_w'] / 2, y_in - d['body_l'], z - d['body_h'] / 2, x + d['body_w'] / 2, y_in - 1e-3, z + d['body_h'] / 2,
               label=f'{ref} body')
    out['envelopes'].append(('jack', f'{ref} body (112BX or Cliff S4)', Box(body.x0 - clearance, body.y0 - clearance, body.z0 - clearance,
                                                                             body.x1 + clearance, body.y1, body.z1 + clearance), 0))
    out['envelopes'].append(('jack', f'{ref} bushing', Cyl('y', x, z, y_in, y_in + d['bushing_l'], d['bushing_d'] / 2), 0.3))
    out['keep'] += [('wiring', f'{ref} wires', Box(body.x0, body.y0 - 6.35, body.z0, body.x1, body.y0 - 0.35, body.z1)),
                    ('jack outside', f'{ref} washer and nut', Cyl('y', x, z, y_out, y_out + d['washer_t'] + d['nut_t'] + 0.3,
                                                                 d['nut_af'] * 1.1547 / 2 + 0.4)),
                    ('jack outside', f'{ref} plug body', Cyl('y', x, z, y_out + d['washer_t'] + d['nut_t'] + 0.3, y_out + 40,
                                                            d['plug_d'] / 2))]
    out['wall_cuts'].append(Cyl('y', x, z, y_in - 1, y_out + 1, d['hole'] / 2 + 0.2, teardrop=True, label=f'{ref} hole'))
    out['thread'] = d['bushing_l'] - (y_out - y_in) - d['washer_t']
    out['body'] = body
    return out
