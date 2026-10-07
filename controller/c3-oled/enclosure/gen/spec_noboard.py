"""A 9 V battery pack for the Lunetta 100: the board-less example of the kit.

A PP3 battery lies in a printed tray with its snap; the snap's lead leaves through a groove in the
end wall, where the lid pinches it (the strain relief), and ends in the 5.5 x 2.1 mm plug that goes
into the Lunetta's DC jack. Four M3 x 8 screws hold the lid, into heat-set inserts in the end
blocks; they also locate it (there is no lip: the battery fills the cavity). Two finger notches let
the battery be lifted out.

No board: every part is placed by coordinates in the enclosure frame, with the same library,
judge and checks as a board-driven spec.
Frame: mm, X along the battery (its terminals toward +X), Y across, Z up, Z = 0 on the floor's top.
"""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import ekit                    # noqa: E402
import parts_lib as L          # noqa: E402
from ekit import Box, Cyl, Text   # noqa: E402

NAME = 'pp3-battery-box'
OUTDIR = os.path.join('examples', 'pp3-battery-box')      # under enclosure/: stl/, images/, build/
BOARD_GLB = None

FIT = 0.5                       # the battery in the tray: sliding, with printing tolerance
WALL, END, FLOOR, LID_T = 2.4, 8.0, 2.0, 2.0
SCREW = 8                       # M3 x 8, ISO 4762, from the lid's top
GROOVE_W, GROOVE_D = 2.8, 1.2   # the lead's groove in the end block: two 26 AWG wires side by side, pinched
MIN_WEB = 1.2

b, sn, w = L.PP3, L.PP3_SNAP, L.WIRE
CAV_X0, CAV_Y = -FIT, b['w'] / 2 + FIT
CAV_Z = b['d'] + FIT                                          # the lid's underside
BAT_ZC = b['d'] / 2
LT = CAV_Z + LID_T

REVIEWED = {}


def build_model():
    m = ekit.Model(NAME)
    shell = m.part('shell', 'shell', note='printed floor down; no supports')
    lid = m.part('lid', 'lid', print_flip=True, note='printed top face down; no supports')

    bat = L.pp3_battery(0.0, 0.0, 0.0)
    snap = L.pp3_snap_t(b['h6'], 0.0, BAT_ZC)
    for res in (bat, snap):
        for e in res['envelopes']:
            m.envelope(*e)
    cav_x1 = snap['x1'] + FIT
    X0, X1 = CAV_X0 - END, cav_x1 + END
    Y = CAV_Y + WALL
    m.info.append(f'outside {X1 - X0:.1f} x {2 * Y:.1f} x {LT + FLOOR:.1f} mm; cavity {cav_x1 - CAV_X0:.1f} x {2 * CAV_Y:.1f} x {CAV_Z:.1f}')

    # ---- the tray: floor, walls, end blocks with the inserts, the lead's groove, finger notches, feet
    shell.add.append(Box(X0, -Y, -FLOOR, X1, Y, CAV_Z, r=3.0, label='tray block'))
    shell.cut.append(Box(CAV_X0, -CAV_Y, 0, cav_x1, CAV_Y, CAV_Z + 1, r=0.3, label='cavity'))   # sharp: the battery's corner radius is not published
    ins = L.INSERT
    screws = [(x, y) for x in (CAV_X0 - END / 2, cav_x1 + END / 2) for y in (-7.0, 7.0)]
    for x, y in screws:
        shell.cut.append(Cyl('z', x, y, CAV_Z - ins['hole_depth'], CAV_Z + 1, ins['hole'] / 2, label='insert hole'))
        lid.cut.append(Cyl('z', x, y, CAV_Z - 1, LT + 1, L.M3_SHCS['clear'] / 2, label='screw hole'))
        s = L.M3_SHCS
        m.keep('screws', f'M3x{SCREW} head at ({x:.1f}, {y:.1f})', Cyl('z', x, y, LT, LT + s['head_h'], s['head_d_max'] / 2 + 0.2))
        m.keep('screws', f'M3x{SCREW} shank at ({x:.1f}, {y:.1f})', Cyl('z', x, y, LT - SCREW - s['l_tol'], LT, s['d'] / 2))
    s = L.M3_SHCS
    m.margin('M3 screws: thread in the insert (shortest screw)', min(SCREW - s['l_tol'] - LID_T, ins['l']), 1.5 * s['d'])
    m.margin('M3 screws: tip above the insert hole bottom (longest screw)', ins['hole_depth'] - (SCREW + s['l_tol'] - LID_T), 0.3)
    m.margin('M3 screws: a standard length', 0 if SCREW in s['lengths'] else 1, 0, kind='max')
    m.margin('end blocks: wall around the insert holes, along X', (END - ins['hole']) / 2, ins['min_wall'])
    m.margin('end blocks: wall around the insert holes, to the side wall', Y - 7.0 - ins['hole'] / 2, ins['min_wall'])
    m.margin('insert hole deeper than the insert + 1', ins['hole_depth'], ins['min_depth'])
    # the lead: up from the snap's long side, along under the lid, out through the groove, pinched by the lid
    ex, ey, ez = snap['exit']
    pair_w, pair_t = 2 * (w['awg26'] + w['tol']), w['awg26'] + w['tol']
    m.keep('lead', 'lead above the snap', Box(ex - pair_w / 2, -pair_w / 2, snap['top'], cav_x1, pair_w / 2, CAV_Z - 0.01))
    m.keep('lead', 'lead in the groove (pinched)', Box(cav_x1 - 0.5, -pair_w / 2, CAV_Z - GROOVE_D, X1 + 1, pair_w / 2, CAV_Z - 0.01))
    shell.cut.append(Box(cav_x1 - 1, -GROOVE_W / 2, CAV_Z - GROOVE_D, X1 + 1, GROOVE_W / 2, CAV_Z + 1, label='lead groove'))
    m.margin('lead: pinch on the thinnest wire (the lid on the groove)', (w['awg26'] - w['tol']) - GROOVE_D, 0.03)
    m.margin('lead: pinch on the thickest wire', pair_t - GROOVE_D, 0.25, kind='max', note='more would cut into 0.4 mm insulation')
    m.margin('lead: groove wide enough for the widest pair', GROOVE_W - pair_w, 0.05)
    m.margin('lead: room above the snap for the wires to turn', CAV_Z - snap['top'], pair_t + 0.5)
    m.margin('end block: web between the groove and the insert holes', 7.0 - ins['hole'] / 2 - GROOVE_W / 2, MIN_WEB)
    m.conflicts += [('lead', 'battery')]         # (the lead starts at the snap: no conflict there)
    # finger notches in both long walls, at the battery's middle, open to the rim
    for sy in (-1, 1):
        shell.cut.append(Cyl('y', 23.0, CAV_Z, sy * (CAV_Y - 1), sy * (Y + 1), 8.0, label='finger notch'))
    m.margin('finger notches: wall left under them', CAV_Z - 8.0, 6.0, note='the battery still held along its lower third')
    fr = (L.FEET['d'] + L.FEET['tol']) / 2 + 0.25
    for fx in (X0 + 9, X1 - 9):
        for fy in (-Y + 7, Y - 7):
            shell.cut.append(Cyl('z', fx, fy, -FLOOR - 1, -FLOOR + 0.6, fr, label='foot recess'))
    m.margin('floor under the foot recesses', FLOOR - 0.6, 1.2)
    m.margin('battery: lid above the largest battery', CAV_Z - b['d'], 0.3)
    m.info.append(f"battery: the smallest PP3 is {bat['slack_y']:.1f} mm narrower and {bat['slack_z']:.1f} mm thinner "
                  f"than the largest; a thin soft foam pad under the lid stops a rattle")

    # ---- the lid: plate, lip inside the walls (notched for the lead), labels
    lid.add.append(Box(X0, -Y, CAV_Z, X1, Y, LT, r=3.0, label='lid plate'))
    # no lip: the cavity is 0.5 mm larger than the battery, so a lip would stand on it; the four screws locate the lid
    cx = (CAV_X0 + snap['x1']) / 2 - 3
    for t, y, size in (('LUNETTA 9V', 4.6, 4.0), ('CENTRE -', -4.6, 3.2)):
        lid.engrave.append(Text(t, cx, y, LT, size, 0.6, label=f'"{t}"'))
    cuts = [c for c in lid.cut]
    for t in lid.engrave:
        g = t.glyphs()
        m.margin(f'engraving {t.label}: letters lost to strokes under {ekit.NOZZLE} mm', 100 * (1 - g['opened'] / g['area']), 5.0,
                 kind='max', note='(% of the letters)')
        m.margin(f'engraving {t.label}: letters filled by gaps under {ekit.NOZZLE} mm', 100 * (g['closed'] / g['area'] - 1), 5.0,
                 kind='max', note='(% of the letters)')
        near, who = min((ekit.gap2d(Box(*t.bounds), c), c.label) for c in cuts)
        m.margin(f'engraving {t.label}: flat lid to the nearest cut ({who})', near, 1.0)
        heads = min(ekit.gap2d(Box(*t.bounds), Cyl('z', x, y, 0, 1, s['head_d_max'] / 2)) for x, y in screws)
        m.margin(f'engraving {t.label}: clear of the screw heads', heads, 1.0)
    t0, t1 = lid.engrave
    m.margin('engraving: space between the two labels', ekit.gap2d(Box(*t0.bounds), Box(*t1.bounds)), 2.0)
    return m


def render_extras():
    snap = L.pp3_snap_t(b['h6'], 0.0, BAT_ZC)
    ex, ey, ez = snap['exit']
    cav_x1 = snap['x1'] + FIT
    X1 = cav_x1 + END
    zl = CAV_Z - GROOVE_D / 2
    return dict(
        boxes=[dict(name='battery', box=[0, -b['w'] / 2 + 0.3, 0.2, b['h6'] - 0.4, b['w'] / 2 - 0.3, b['d'] - 0.2], color=(0.55, 0.5, 0.1)),
               dict(name='snap', box=[b['h6'], -sn['l'] / 2, BAT_ZC - sn['w'] / 2, b['h6'] + 2.3 + sn['h'], sn['l'] / 2,
                                      BAT_ZC + sn['w'] / 2], color=(0.05, 0.05, 0.05))],
        wires=[dict(points=[(ex, dy, snap['top'] - 0.5), (ex, dy, zl), (X1 + 25, dy, zl), (X1 + 40, dy, zl - 6)],
                    color=c, r=w['awg26'] / 2) for dy, c in ((-0.65, (0.8, 0.1, 0.1)), (0.65, (0.05, 0.05, 0.05)))],
        views=[
            dict(name='01-assembled.png', cam=(130, -110, 110), target=(29, 0, 5), lens=55),
            dict(name='02-top.png', cam=(29, 0, 300), target=(29, 0.0001, 0), ortho=82, size=(1400, 700)),
            dict(name='03-lid-off.png', cam=(120, -100, 120), target=(29, 0, 5), lens=55, hide_parts=['lid']),
            dict(name='04-exploded.png', cam=(130, -115, 140), target=(29, 0, 12), lens=50, explode=30, hide='all'),
            dict(name='05-section-lead.png', cam=(58, -300, 10), target=(58, 0, 10), ortho=34, size=(1400, 1000),
                 section=dict(axis='y', value=0.0)),
        ])
