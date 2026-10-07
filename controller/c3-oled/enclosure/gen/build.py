#!/usr/bin/env python3
"""Build and check the enclosure; the last line says whether it is done.

    python3 enclosure/gen/build.py                 # everything: model, STLs, judge, margins, mesh, renders
    python3 enclosure/gen/build.py --no-render     # skip the renders (the mesh check still runs)
    python3 enclosure/gen/build.py --spec spec     # the spec module in enclosure/gen (default: spec)

Stages:
  1. model        the spec builds the solids, envelopes, access volumes and margins
  2. measurement  every body the library draws against the part's KiCad 3D model; a difference
                  over 0.3 mm is a finding until REVIEWED in the spec says why
  3. export       each printed part as an STL in its print orientation (stl/), plus in place
  4. judge        OpenSCAD: every printed part against every envelope and access volume, and the
                  access volumes that must not overlap each other; an empty intersection passes
  5. margins      the numbers, each with its limit
  6. mesh         Blender, on the exported STLs: closed and manifold, outward normals, on the bed
                  and inside it, overhangs and bridges, thinnest wall
  7. renders      Blender: assembled with the board's own 3D model, exploded, the print plate
Done means every check passes, every finding is reviewed, and the STLs exist.
"""
import argparse
import datetime
import importlib
import json
import os
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ENC = os.path.dirname(HERE)
sys.path.insert(0, HERE)
import ekit  # noqa: E402

BED = (220.0, 220.0, 250.0)       # the smallest common bed (Ender-3 class); DECISIONS.md
MIN_WALL = 0.8                    # two 0.4 mm perimeters
MAX_BRIDGE = 10.0                 # a bridge span any tuned printer manages (SPEC.md)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--spec', default='spec')
    ap.add_argument('--no-render', action='store_true')
    ap.add_argument('--jobs', type=int, default=None)
    a = ap.parse_args()
    t0 = time.time()
    spec = importlib.import_module(a.spec)
    global BED, MIN_WALL, MAX_BRIDGE                            # a spec may set its own printer (nozzle, bed)
    BED = tuple(getattr(spec, 'BED', BED))
    MIN_WALL = getattr(spec, 'MIN_WALL', MIN_WALL)
    MAX_BRIDGE = getattr(spec, 'MAX_BRIDGE', MAX_BRIDGE)
    root = os.path.join(ENC, getattr(spec, 'OUTDIR', '.'))      # a spec's outputs: stl/, images/, build/
    out = os.path.join(root, 'build')
    os.makedirs(out, exist_ok=True)
    logf = open(os.path.join(out, 'build.log'), 'w')

    def log(*s):
        line = ' '.join(str(x) for x in s)
        print(line, flush=True)
        logf.write(line + '\n')
        logf.flush()

    status = []

    # 1. model
    m = spec.build_model()
    m.reviewed.update(getattr(spec, 'REVIEWED', {}))
    log(f'== model: {m.name}: {len(m.parts)} printed parts, {len(m.envelopes)} part bodies, '
        f'{len(m.keepclear)} access volumes, {len(m.margins)} margins')
    for i in m.info:
        log('  ' + i)
    log(f'  printer: nozzle {ekit.NOZZLE} mm, thinnest wall {MIN_WALL} mm, widest bridge {MAX_BRIDGE} mm, '
        f"bed {' x '.join(f'{v:.0f}' for v in BED)} mm")

    # 2. measurement
    log('== measurement check (library vs KiCad 3D models)')
    open_f = []
    for key, text in m.findings:
        why = m.reviewed.get(key)
        log(f"  {'reviewed' if why else 'FINDING '} {text}" + (f'\n           -> {why}' if why else ''))
        if not why:
            open_f.append(key)
    for key in m.reviewed:
        if key not in dict(m.findings) and not key.startswith(('judge:', 'margin:')):
            log(f'  stale review note (no such finding): {key}')
    n = len(m.findings)
    status.append(('measurement check', f'{n} difference{"" if n == 1 else "s"} over 0.3 mm, {n - len(open_f)} reviewed',
                   not open_f))

    # 3. export
    log('== export')
    stl = os.path.join(root, 'stl')
    files = ekit.export(m, stl, log=lambda s: log('  ' + s))
    write_assembly(m, os.path.join(root, f'{m.name}.scad'))
    if hasattr(spec, 'after_export'):                           # a spec's own packaging (a print folder, say)
        spec.after_export(m, files, root, lambda s: log('  ' + s))
    status.append(('STL export', ', '.join(f'{n}.stl' for n in files), all(os.path.getsize(p) > 0 for p, _ in files.values())))

    # 3b. every added solid survives the part's cuts (a cavity cut after its bosses removes them)
    log('== additions (OpenSCAD)')
    lost = ekit.additions_check(m, os.path.join(out, 'additions'), log=lambda s: log('  ' + s), jobs=a.jobs)
    for pn, label, frac in lost:
        log(f'  LOST {pn}: {label} keeps {100 * frac:.0f} % of its volume')
    status.append(('additions survive the cuts', f'{len(lost)} lost', not lost))

    # 4. judge, after testing it: a fault planted in every group must be found and named
    log('== judge (OpenSCAD intersections)')
    work = os.path.join(out, 'judge')
    planted, caught = ekit.selftest_judge(m, os.path.join(out, 'selftest'), log=lambda s: log('  ' + s), jobs=a.jobs)
    status.append(('judge self-test', f'{planted} faults planted, {caught} caught and named', caught == planted))
    coll = ekit.judge(m, work, log=lambda s: log('  ' + s), jobs=a.jobs)
    open_c = []
    for pn, g, label, vol, (lo, hi) in sorted(coll):
        key = f'judge:{pn}/{label}'
        why = m.reviewed.get(key)
        box = ' '.join(f'{u:.1f}..{v:.1f}' for u, v in zip(lo, hi))
        log(f"  {'reviewed' if why else 'COLLISION'} {pn} x {g}: {label}, {vol:.2f} mm3 at {box}" + (f'\n           -> {why}' if why else ''))
        if not why:
            open_c.append(key)
    status.append(('collisions (OpenSCAD)', f'{len(coll)} found, {len(coll) - len(open_c)} reviewed', not open_c))

    # 5. margins
    log('== margins')
    fails = []
    for label, value, limit, kind, note in m.margins:
        ok = value >= limit if kind == 'min' else value <= limit
        why = None if ok else m.reviewed.get(f'margin:{label}')
        rel = value - limit if kind == 'min' else limit - value
        log(f"  {'ok  ' if ok else 'reviewed' if why else 'FAIL'} {label}: {value:.2f} ({'at least' if kind == 'min' else 'at most'} "
            f"{limit:.2f}, margin {rel:+.2f}){'  ' + note if note else ''}" + (f'\n           -> {why}' if why else ''))
        if not ok and not why:
            fails.append(label)
    status.append(('margins', f'{len(m.margins)} checked, {len(fails)} failing', not fails))

    # 6. mesh (Blender), after testing it on meshes with known faults
    log('== mesh check (Blender)')
    ok_st, text = mesh_selftest(os.path.join(out, 'selftest'))
    log('  ' + text)
    status.append(('mesh self-test', text.split(': ', 1)[1], ok_st))
    rep = os.path.join(out, 'mesh.json')
    args = ['--out', rep, '--bed', *map(str, BED), '--min-wall', str(MIN_WALL), '--bridge', str(MAX_BRIDGE),
            '--exclude', json.dumps(engraving_boxes(m)), '--stl']
    args += [p for p, _ in files.values()]
    r = blender('blender_check.py', args)
    mesh_ok = False
    if r.returncode == 0 and os.path.exists(rep):
        mesh = json.load(open(rep))
        mesh_ok = True
        for name, s in mesh.items():
            ok = s['ok']
            mesh_ok &= ok
            log(f"  {'ok  ' if ok else 'FAIL'} {name}: {s['tris']} triangles, {s['volume_cm3']:.1f} cm3, "
                f"{' x '.join(f'{v:.1f}' for v in s['size'])} mm; non-manifold edges {s['non_manifold']}, "
                f"normals {'out' if s['outward'] else 'IN'}, on the bed {s['on_bed']}, fits {s['fits']}; "
                f"thinnest wall {s['min_wall']:.2f} mm at {fmt(s['min_wall_at'])}; "
                f"{s['overhang_islands']} downward faces, widest bridge {s['max_bridge']:.1f} mm")
            for p in s['problems']:
                log('       ' + p)
    else:
        log('  blender failed:', (r.stdout + r.stderr)[-1500:])
    status.append(('mesh (Blender)', 'closed, on the bed, walls and bridges' if mesh_ok else 'see the log', mesh_ok))

    # 7. renders
    if not a.no_render:
        log('== renders (Blender)')
        img = os.path.join(root, 'images')
        os.makedirs(img, exist_ok=True)
        for f in os.listdir(img):                  # generated here: the renders replace the last run's
            if f.endswith('.png'):
                os.remove(os.path.join(img, f))
        asm = [q for _, q in files.values()]
        colors = [m.parts[n].color for n in files]
        glb = getattr(spec, 'BOARD_GLB', None)      # the board's own 3D model, if there is a board
        r = blender('blender_render.py', ['--out', img, *(['--glb', glb] if glb else []),
                                          '--knobs', json.dumps(getattr(spec, 'render_extras', lambda: {})()),
                                          '--stl', *asm, '--colors', *colors,
                                          '--print', *[p for p, _ in files.values()]])
        shots = sorted(f for f in os.listdir(img) if f.endswith('.png')) if os.path.isdir(img) else []
        log('  ' + (', '.join(shots) if r.returncode == 0 else (r.stdout + r.stderr)[-1500:]))
        status.append(('renders', f'{len(shots)} images in images/', r.returncode == 0))

    # report
    done = all(ok for _, _, ok in status)
    log('')
    log('| check | result | status |')
    log('| - | - | - |')
    for name, text, ok in status:
        log(f"| {name} | {text} | {'pass' if ok else 'FAIL'} |")
    json.dump(dict(date=datetime.date.today().isoformat(), status=[dict(check=n, result=t, ok=o) for n, t, o in status],
                   margins=[dict(label=l, value=v, limit=lim, kind=k) for l, v, lim, k, _ in m.margins],
                   collisions=[dict(part=p, group=g, label=l, volume=v) for p, g, l, v, _ in coll],
                   findings=[dict(key=k, text=t, reviewed=m.reviewed.get(k)) for k, t in m.findings]),
              open(os.path.join(out, 'report.json'), 'w'), indent=1)
    log(f'({time.time() - t0:.0f} s)')
    log('DONE' if done else 'NOT DONE')
    sys.exit(0 if done else 1)


MESH_FAULTS = {        # name: (OpenSCAD source, the problem the check must report)
    'floating': ('translate([0,0,5]) cube([20,20,5]);', 'not flat on the bed'),
    'thin-wall': ('cube([30,0.5,10]); cube([30,20,1.5]);', 'wall 0.5'),
    'wide-bridge': ('cube([4,20,10]); translate([24,0,0]) cube([4,20,10]); translate([0,0,10]) cube([28,20,2]);', 'bridge 20.0'),
    'overhang': ('cube([20,20,2]); translate([0,0,2]) multmatrix([[1,0,1.5,0],[0,1,0,0],[0,0,1,0],[0,0,0,1]]) cube([6,20,8]);',
                 'overhang steeper than 45'),
    'too-big': ('cube([230,20,2]);', 'larger than the bed'),
}


def mesh_selftest(work):
    """Meshes with one known fault each, and one open mesh: the Blender check must report every one."""
    os.makedirs(work, exist_ok=True)
    paths = []
    for name, (src, _) in MESH_FAULTS.items():
        pth = os.path.join(work, f'{name}.stl')
        ekit.openscad(src, pth)
        paths.append(pth)
    open_mesh = os.path.join(work, 'open.stl')        # a cube missing its top: non-manifold edges
    f = [((0, 0, 0), (1, 0, 0), (1, 1, 0)), ((0, 0, 0), (1, 1, 0), (0, 1, 0)), ((0, 0, 0), (1, 0, 1), (1, 0, 0)),
         ((0, 0, 0), (0, 0, 1), (1, 0, 1)), ((1, 0, 0), (1, 1, 1), (1, 1, 0)), ((1, 0, 0), (1, 0, 1), (1, 1, 1)),
         ((0, 1, 0), (1, 1, 0), (1, 1, 1)), ((0, 1, 0), (1, 1, 1), (0, 1, 1)), ((0, 0, 0), (0, 1, 0), (0, 1, 1)),
         ((0, 0, 0), (0, 1, 1), (0, 0, 1))]
    with open(open_mesh, 'w') as fh:
        fh.write('solid open\n')
        for t in f:
            fh.write('facet normal 0 0 0\nouter loop\n' + ''.join(f'vertex {10 * a} {10 * b} {10 * c}\n' for a, b, c in t)
                     + 'endloop\nendfacet\n')
        fh.write('endsolid open\n')
    paths.append(open_mesh)
    rep = os.path.join(work, 'mesh.json')
    r = blender('blender_check.py', ['--out', rep, '--bed', *map(str, BED), '--min-wall', str(MIN_WALL), '--bridge',
                                     str(MAX_BRIDGE), '--stl', *paths])
    if r.returncode != 0 or not os.path.exists(rep):
        return False, 'mesh self-test: blender failed ' + (r.stdout + r.stderr)[-300:]
    res = json.load(open(rep))
    want = {n: w for n, (_, w) in MESH_FAULTS.items()} | {'open': 'non-manifold'}
    caught = [n for n, w in want.items() if any(w in p for p in res.get(n, {}).get('problems', []))]
    missed = sorted(set(want) - set(caught))
    return not missed, f'mesh self-test: {len(want)} faulty meshes, {len(caught)} caught' + (f' (missed: {missed})' if missed else '')


def engraving_boxes(m):
    """Where each part's engraving is, in its print orientation: the wall check leaves the letters to
    the text checks (their strokes and gaps are measured in the spec's margins)."""
    out = {}
    for name, p in m.parts.items():
        x0, y0, z0, x1, y1, z1 = p.bounds()
        boxes = []
        for t in p.engrave:
            b = t.bounds
            if p.print_flip:
                boxes.append([b[0], y0 + y1 - b[4], z1 - b[5], b[3], y0 + y1 - b[1], z1 - b[2]])
            else:
                boxes.append([b[0], b[1], b[2] - z0, b[3], b[4], b[5] - z0])
        out[name] = [[v - 0.1 for v in bx[:3]] + [v + 0.1 for v in bx[3:]] for bx in boxes]
    return out


def fmt(p):
    return '(' + ', '.join(f'{v:.1f}' for v in p) + ')' if p else '-'


def blender(script, args):
    return subprocess.run(['blender', '-b', '--factory-startup', '-noaudio', '--python-exit-code', '1',
                           '-P', os.path.join(HERE, script), '--', *args], capture_output=True, text=True, timeout=3600)


def write_assembly(m, path):
    """One file to open in OpenSCAD: every part in place, coloured, and each alone behind a switch."""
    lines = [f'// {m.name}: generated by enclosure/gen/build.py from spec.py; do not edit (edit the spec)',
             '// part = "all" shows the assembly; set it to a part name to see that part alone',
             'part = "all";']
    for name, p in m.parts.items():
        lines.append(f'if (part == "all" || part == "{name}") color("{ {"shell": "#3a3d42", "lid": "#d9d6cf"}.get(p.color, "#aaaaaa") }") '
                     f'{p.scad(engrave=True)}')
    open(path, 'w').write('\n'.join(lines) + '\n')


if __name__ == '__main__':
    main()
