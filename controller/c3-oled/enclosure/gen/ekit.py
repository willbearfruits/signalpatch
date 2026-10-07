"""Enclosure kit: printed parts described as solids, written as OpenSCAD, judged by OpenSCAD.

The design contract (the board's spec.py builds a Model):
  - Model.parts        {name: Part}: what gets printed. A Part is a union of `add` solids minus
                       `cut` solids; `engrave` solids (text) are cut only in the final STL, since
                       removing material cannot cause a collision;
  - Model.envelopes    every component body, as solids from its datasheet dimensions, with the
                       clearance it needs; no printed part may touch one (grown by its clearance);
  - Model.keepclear    volumes that must stay empty for the thing to work: a plug's body in front
                       of its jack, a jumper housing above each socket, a knob's sweep, a screw's
                       shank, a light path above an LED;
  - Model.margins      numbers the checks print with their limit (a web between two holes, a
                       skin over a pocket, a screw's thread engagement), each one a pass or a fail;
  - Model.conflicts    (keep-clear group, group) pairs that must not overlap each other: knobs
                       against the jumpers beside them, a plug's body against the board's edge;
  - Model.reviewed     {finding key: reason} for measurement disagreements a person resolved.
Frame: mm, X right, Y away from the player (board y flipped), Z up, Z = 0 on the board's top face.

The judge: for each printed part and each group of envelopes or keep-clear volumes,
`intersection(){ part; union(){ group } }` is rendered by OpenSCAD; an empty result ("Current top
level object is empty") is a pass. A group that is not empty is split until the offending items are
named. The margins say by how much a check passed; the intersections prove it.
"""
import concurrent.futures as cf
import json
import math
import os
import re
import struct
import subprocess
import tempfile

FN = 64


def f(v):
    return f'{v:.4f}'.rstrip('0').rstrip('.') if isinstance(v, float) else str(v)


# ------------------------------------------------------------------ solids

class Solid:
    label = ''

    def scad(self):
        raise NotImplementedError

    def grown(self, d):
        raise NotImplementedError

    @property
    def bounds(self):
        raise NotImplementedError


class Box(Solid):
    """Axis-aligned box, optionally with rounded vertical edges (radius r)."""

    def __init__(self, x0, y0, z0, x1, y1, z1, r=0.0, label=''):
        self.x0, self.y0, self.z0 = min(x0, x1), min(y0, y1), min(z0, z1)
        self.x1, self.y1, self.z1 = max(x0, x1), max(y0, y1), max(z0, z1)
        self.r, self.label = r, label

    def scad(self):
        w, d, h = self.x1 - self.x0, self.y1 - self.y0, self.z1 - self.z0
        r = min(self.r, w / 2 - 1e-3, d / 2 - 1e-3)
        if r <= 0.01:
            return f'translate([{f(self.x0)},{f(self.y0)},{f(self.z0)}]) cube([{f(w)},{f(d)},{f(h)}]);'
        pts = [(self.x0 + r, self.y0 + r), (self.x1 - r, self.y0 + r), (self.x1 - r, self.y1 - r), (self.x0 + r, self.y1 - r)]
        cyl = ' '.join(f'translate([{f(x)},{f(y)},{f(self.z0)}]) cylinder(r={f(r)},h={f(h)},$fn={FN});' for x, y in pts)
        return f'hull(){{ {cyl} }}'

    def grown(self, d):
        return Box(self.x0 - d, self.y0 - d, self.z0 - d, self.x1 + d, self.y1 + d, self.z1 + d, self.r + d if self.r else 0,
                   self.label)

    @property
    def bounds(self):
        return (self.x0, self.y0, self.z0, self.x1, self.y1, self.z1)


class Cyl(Solid):
    """Cylinder along `axis` ('x', 'y' or 'z'), centred on (u, v) in the other two axes (in xyz
    order), from a0 to a1 along the axis. teardrop: a 45-degree point on top (for holes in walls,
    so they print without support)."""

    def __init__(self, axis, u, v, a0, a1, r, teardrop=False, label='', fn=FN):
        self.axis, self.u, self.v, self.a0, self.a1, self.r = axis, u, v, min(a0, a1), max(a0, a1), r
        self.teardrop, self.label, self.fn = teardrop, label, fn

    def scad(self):
        h, r = self.a1 - self.a0, self.r
        if self.teardrop:       # the circle hulled with an apex at r * sqrt(2): a 45-degree roof
            shape = (f'hull(){{ circle(r={f(r)},$fn={self.fn}); '
                     f'polygon([[{f(-r / 2 ** 0.5)},{f(r / 2 ** 0.5)}],[0,{f(r * 2 ** 0.5)}],[{f(r / 2 ** 0.5)},{f(r / 2 ** 0.5)}]]); }}')
        else:
            shape = f'circle(r={f(r)},$fn={self.fn});'
        body = f'linear_extrude({f(h)}) {shape}'
        # local z (the extrusion) along the axis, local y (the teardrop's apex) up (+z)
        m = {'z': ([1, 0, 0], [0, 1, 0], [0, 0, 1], (self.u, self.v, self.a0)),
             'x': ([0, 0, 1], [1, 0, 0], [0, 1, 0], (self.a0, self.u, self.v)),
             'y': ([-1, 0, 0], [0, 0, 1], [0, 1, 0], (self.u, self.a0, self.v))}[self.axis]
        rows = [list(m[i]) + [m[3][i]] for i in range(3)] + [[0, 0, 0, 1]]
        mat = '[' + ','.join('[' + ','.join(f(float(x)) for x in row) + ']' for row in rows) + ']'
        return f'multmatrix({mat}) {body}'

    def grown(self, d):
        return Cyl(self.axis, self.u, self.v, self.a0 - d, self.a1 + d, self.r + d, self.teardrop, self.label, self.fn)

    @property
    def bounds(self):
        r, u, v = self.r * (1.42 if self.teardrop else 1), self.u, self.v
        if self.axis == 'z':
            return (u - self.r, v - self.r, self.a0, u + self.r, v + self.r, self.a1)
        if self.axis == 'x':
            return (self.a0, u - self.r, v - self.r, self.a1, u + self.r, v + r)
        return (u - self.r, self.a0, v - self.r, u + self.r, self.a1, v + r)


class Prism(Solid):
    """A polygon (counter-clockwise, seen from above) standing along Z from its bottom to z1. The bottom
    is the plane z = z0 + sx * x + sy * y (sx = sy = 0: flat), so a part can end in a 45-degree ramp
    and print without support (a tongue standing out of a wall)."""

    def __init__(self, pts, z0, z1, sx=0.0, sy=0.0, label=''):
        self.pts, self.z0, self.z1, self.sx, self.sy, self.label = [tuple(p) for p in pts], z0, z1, sx, sy, label

    def zb(self, x, y):
        return self.z0 + self.sx * x + self.sy * y

    def scad(self):
        n = len(self.pts)
        pts = [(x, y, self.zb(x, y)) for x, y in self.pts] + [(x, y, self.z1) for x, y in self.pts]
        faces = [list(range(n)), list(range(2 * n - 1, n - 1, -1))]
        faces += [[i, n + i, n + (i + 1) % n, (i + 1) % n] for i in range(n)]
        P = ','.join(f'[{f(float(x))},{f(float(y))},{f(float(z))}]' for x, y, z in pts)
        F = ','.join('[' + ','.join(map(str, fc)) + ']' for fc in faces)
        return f'polyhedron(points=[{P}],faces=[{F}],convexity=4);'

    def grown(self, d):        # a prism's box, grown: good enough for an envelope's clearance
        b = self.bounds
        return Box(b[0] - d, b[1] - d, b[2] - d, b[3] + d, b[4] + d, b[5] + d, label=self.label)

    @property
    def bounds(self):
        xs, ys = [p[0] for p in self.pts], [p[1] for p in self.pts]
        return (min(xs), min(ys), min(self.zb(x, y) for x, y in self.pts), max(xs), max(ys), self.z1)


class Hull(Solid):
    def __init__(self, items, label=''):
        self.items, self.label = items, label

    def scad(self):
        return 'hull(){ ' + ' '.join(i.scad() for i in self.items) + ' }'

    def grown(self, d):
        return Hull([i.grown(d) for i in self.items], self.label)

    @property
    def bounds(self):
        bs = [i.bounds for i in self.items]
        return tuple(min(b[k] for b in bs) for k in range(3)) + tuple(max(b[k] for b in bs) for k in range(3, 6))


class Diff(Solid):
    """A solid minus others (a lip ring notched where tall parts stand)."""

    def __init__(self, base, cuts, label=''):
        self.base, self.cuts, self.label = base, cuts, label

    def scad(self):
        return f'difference(){{ {self.base.scad()} {union(self.cuts)} }}' if self.cuts else self.base.scad()

    def grown(self, d):
        return Diff(self.base.grown(d), self.cuts, self.label)

    @property
    def bounds(self):
        return self.base.bounds


class Text(Solid):
    """Engraved text on a face. face 'top': on the plane z, reading along +x; 'x+': on the plane
    x (a right-hand wall seen from outside), reading along -y... (see scad())."""

    FONT, SPACING = 'Liberation Sans:style=Bold', 1.0      # a spec may set its own

    def __init__(self, s, x, y, z, size, depth=0.6, face='top', halign='center', valign='center',
                 font=None, label='', spacing=None):
        self.s, self.x, self.y, self.z, self.size, self.depth = s, x, y, z, size, depth
        self.face, self.halign, self.valign, self.label = face, halign, valign, label or s
        self.font, self.spacing = font or Text.FONT, spacing or Text.SPACING

    def scad(self):
        t = (f'linear_extrude({f(self.depth + 0.5)}) text("{self.s}", size={f(self.size)}, font="{self.font}", '
             f'halign="{self.halign}", valign="{self.valign}", spacing={f(float(self.spacing))}, $fn=24);')
        if self.face == 'top':
            return f'translate([{f(self.x)},{f(self.y)},{f(self.z - self.depth)}]) {t}'
        if self.face == 'x+':     # right wall, read from outside: text +x along world +y, up +z
            return f'translate([{f(self.x - self.depth)},{f(self.y)},{f(self.z)}]) rotate([90,0,90]) {t}'
        raise ValueError(self.face)

    def grown(self, d):
        return self

    def glyphs(self):
        """The text's outline as OpenSCAD draws it, measured once (text_metrics): its box relative to
        the anchor, its area, and its area after an opening by the nozzle width (strokes thinner
        than the nozzle vanish)."""
        return text_metrics(self.s, self.size, self.font, self.halign, self.valign, self.spacing)

    @property
    def bounds(self):
        g = self.glyphs()
        if self.face != 'top':
            raise ValueError(self.face)
        return (self.x + g['box'][0], self.y + g['box'][1], self.z - self.depth, self.x + g['box'][2], self.y + g['box'][3], self.z)


NOZZLE = 0.4
_TM_CACHE = os.path.join(os.path.dirname(os.path.abspath(__file__)), '.text-metrics.json')
_tm = None


def text_metrics(s, size, font, halign='center', valign='center', spacing=1.0):
    """Rendered by OpenSCAD itself, so the numbers are what gets cut; cached in gen/.text-metrics.json.
    area: the letters; opened: after removing strokes thinner than the nozzle (engraved letters
    the nozzle cannot leave open); closed: after filling gaps narrower than the nozzle (material
    between strokes, inside letters and between them, too thin to print)."""
    global _tm
    if _tm is None:
        _tm = json.load(open(_TM_CACHE)) if os.path.exists(_TM_CACHE) else {}
    key = json.dumps([s, size, font, halign, valign, float(spacing), NOZZLE])
    if key not in _tm:
        t = (f'text("{s}", size={f(size)}, font="{font}", halign="{halign}", valign="{valign}", '
             f'spacing={f(float(spacing))}, $fn=24)')
        w = NOZZLE / 2
        out = {}
        with tempfile.TemporaryDirectory() as d:
            for name, src in (('text', f'linear_extrude(1) {t};'),
                              ('opened', f'linear_extrude(1) offset(r={w},$fn=16) offset(delta={-w}) {t};'),
                              ('closed', f'linear_extrude(1) offset(r={-w},$fn=16) offset(delta={w}) {t};')):
                ok, log = openscad(src, os.path.join(d, name + '.stl'))
                n, vol, (lo, hi) = stl_stats(os.path.join(d, name + '.stl')) if ok else (0, 0.0, ([0] * 3, [0] * 3))
                out[name] = (vol, lo, hi)
        vol, lo, hi = out['text']
        _tm[key] = dict(box=[lo[0], lo[1], hi[0], hi[1]], area=vol, opened=out['opened'][0], closed=out['closed'][0])
        json.dump(_tm, open(_TM_CACHE, 'w'), indent=0)
    return _tm[key]


def union(items):
    return 'union(){ ' + ' '.join(i.scad() for i in items) + ' }' if items else ''


# ------------------------------------------------------------------ model

class Part:
    def __init__(self, name, color, print_flip=False, note=''):
        self.name, self.color, self.print_flip, self.note = name, color, print_flip, note
        self.add, self.cut, self.engrave = [], [], []

    def scad(self, engrave=True):
        cuts = self.cut + (self.engrave if engrave else [])
        body = union(self.add)
        return f'difference(){{ {body} {union(cuts)} }}' if cuts else body

    def bounds(self):
        bs = [s.bounds for s in self.add]
        return tuple(min(b[k] for b in bs) for k in range(3)) + tuple(max(b[k] for b in bs) for k in range(3, 6))


class Model:
    def __init__(self, name):
        self.name = name
        self.parts = {}
        self.envelopes = []     # (group, label, solid, clearance)
        self.keepclear = []     # (group, label, solid)
        self.margins = []       # (label, value, limit, 'min' | 'max', note)
        self.findings = []      # (key, text): measurement disagreements etc.
        self.conflicts = []     # (keep group, keep group): access volumes that must not overlap each other
        self.part_pairs = []    # (part, part): printed parts that must not overlap each other, assembled
        self.reviewed = {}
        self.info = []

    def part(self, name, color, **kw):
        self.parts[name] = Part(name, color, **kw)
        return self.parts[name]

    def envelope(self, group, label, solid, clearance=0.3):
        self.envelopes.append((group, label, solid, clearance))

    def keep(self, group, label, solid):
        self.keepclear.append((group, label, solid))

    def margin(self, label, value, limit, kind='min', note=''):
        self.margins.append((label, value, limit, kind, note))

    def finding(self, key, text):
        self.findings.append((key, text))


# ------------------------------------------------------------------ OpenSCAD

def openscad(src, out, timeout=900):
    """Render `src` to `out` (STL). Returns (non-empty, log)."""
    with tempfile.NamedTemporaryFile('w', suffix='.scad', delete=False) as t:
        t.write(src)
        path = t.name
    try:
        r = subprocess.run(['openscad', '-o', out, path], capture_output=True, text=True, timeout=timeout)
    finally:
        os.remove(path)
    log = r.stdout + r.stderr
    empty = 'top level object is empty' in log or not os.path.exists(out)
    if r.returncode != 0 and not empty:
        raise RuntimeError(log[-2000:])
    return not empty, log


def stl_stats(path):
    """(triangles, volume mm^3, bounds) of an STL (ASCII or binary)."""
    data = open(path, 'rb').read()
    tris = []
    if data[:5] == b'solid' and (b'facet' in data[:400] or len(data) < 84 or b'endsolid' in data[:400]):
        v = [tuple(map(float, m.groups())) for m in
             re.finditer(rb'vertex\s+(\S+)\s+(\S+)\s+(\S+)', data)]
        tris = [v[i:i + 3] for i in range(0, len(v) - 2, 3)]
    else:
        n = struct.unpack('<I', data[80:84])[0]
        for i in range(n):
            o = 84 + 50 * i
            vals = struct.unpack('<12f', data[o:o + 48])
            tris.append((vals[3:6], vals[6:9], vals[9:12]))
    vol = 0.0
    lo, hi = [1e9] * 3, [-1e9] * 3
    for a, b, c in tris:
        vol += (a[0] * (b[1] * c[2] - b[2] * c[1]) - a[1] * (b[0] * c[2] - b[2] * c[0]) + a[2] * (b[0] * c[1] - b[1] * c[0])) / 6
        for p in (a, b, c):
            for k in range(3):
                lo[k], hi[k] = min(lo[k], p[k]), max(hi[k], p[k])
    return len(tris), abs(vol), (lo, hi)


class _PartSolid(Solid):
    """A printed part standing in as a solid, for part-against-part checks (without its engraving)."""

    def __init__(self, part):
        self.part, self.label = part, part.name

    def scad(self):
        return self.part.scad(engrave=False)


def _collide(args):
    part_scad, items, workdir, tag = args
    src = f'intersection(){{ {part_scad} {union(items)} }}'
    out = os.path.join(workdir, f'hit-{tag}.stl')
    hit, _ = openscad(src, out)
    stats = stl_stats(out) if hit else None
    if stats and (stats[0] == 0 or stats[1] < 1e-6):      # faces only touching: no volume in common
        hit = False
    if os.path.exists(out):
        os.remove(out)
    return hit, stats


def judge(model, workdir, log=print, jobs=None, pairs=None):
    """Every printed part against every envelope group and keep-clear group. Returns a list of
    collisions: (part, group, label, volume mm^3, bounds)."""
    os.makedirs(workdir, exist_ok=True)
    groups = {}
    for g, label, s, cl in model.envelopes:
        groups.setdefault(('envelope', g), []).append((label, s.grown(cl) if cl else s))
    for g, label, s in model.keepclear:
        groups.setdefault(('keep-clear', g), []).append((label, s))
    tasks = []
    for pname, part in model.parts.items():
        ps = part.scad(engrave=False)
        for (kind, g), items in groups.items():
            if pairs is None or (pname, g) in pairs:
                tasks.append((pname, kind, g, items, ps))
    for ga, gb in (model.conflicts if pairs is None else []):           # an access volume against another, or against a part's body
        a = [s for (k, g), items in groups.items() if g == ga and k == 'keep-clear' for _, s in items]
        b_ = [(l, s) for (k, g), items in groups.items() if g == gb for l, s in items]
        assert a and b_, f'conflict ({ga}, {gb}): no such group'
        tasks.append((f'[{ga}]', 'vs', gb, b_, union(a)))
    for pa, pb in (model.part_pairs if pairs is None else []):          # printed part against printed part, in place
        tasks.append((pa, 'part', pb, [(pb, _PartSolid(model.parts[pb]))], model.parts[pa].scad(engrave=False)))
    collisions, checked = [], 0
    jobs = jobs or max(2, (os.cpu_count() or 4) - 2)
    with cf.ThreadPoolExecutor(jobs) as ex:
        pending = {ex.submit(_collide, (ps, [s for _, s in items], workdir, f'{pn}-{k}-{g}-{i}')):
                   (pn, k, g, items, ps) for i, (pn, k, g, items, ps) in enumerate(tasks)}
        while pending:
            done, _ = cf.wait(pending, return_when=cf.FIRST_COMPLETED)
            for fut in done:
                pn, k, g, items, ps = pending.pop(fut)
                hit, stats = fut.result()
                checked += 1
                if not hit:
                    continue
                if len(items) == 1:
                    collisions.append((pn, f'{k} {g}', items[0][0], stats[1], stats[2]))
                    continue
                half = len(items) // 2          # split until each offender is named
                for j, sub in enumerate((items[:half], items[half:])):
                    pending[ex.submit(_collide, (ps, [s for _, s in sub], workdir, f'{pn}-{k}-{g}-{j}-{len(sub)}'))] = \
                        (pn, k, g, sub, ps)
    log(f'judge: {checked} OpenSCAD intersections, {len(collisions)} collisions')
    return collisions


def _volume(src, workdir, tag):
    out = os.path.join(workdir, f'vol-{tag}.stl')
    ok, _ = openscad(src, out)
    vol = stl_stats(out)[1] if ok else 0.0
    if os.path.exists(out):
        os.remove(out)
    return vol


def additions_check(model, workdir, log=print, jobs=None, keep=0.1):
    """Every solid a part adds must survive its part's cuts: a boss or a stand added inside a cavity
    that is cut afterwards disappears, and neither the judge nor the mesh check can see a missing
    feature. Returns [(part, label, kept fraction)] for the additions that keep less than `keep`
    of their own volume (10 %: a hollowed block keeps far more, an erased boss nothing)."""
    os.makedirs(workdir, exist_ok=True)
    tasks = []
    for pname, part in model.parts.items():
        ps = part.scad(engrave=False)
        for i, item in enumerate(part.add):
            tasks.append((pname, item.label or f'addition {i}', item.scad(), f'intersection(){{ {ps} {item.scad()} }}', f'{pname}-{i}'))
    lost = []
    jobs = jobs or max(2, (os.cpu_count() or 4) - 2)
    with cf.ThreadPoolExecutor(jobs) as ex:
        res = {t: (ex.submit(_volume, t[2], workdir, t[4] + '-own'), ex.submit(_volume, t[3], workdir, t[4] + '-kept')) for t in tasks}
        for (pname, label, _, _, _), (own, kept) in res.items():
            v0, v1 = own.result(), kept.result()
            frac = v1 / v0 if v0 > 0 else 1.0
            if frac < keep:
                lost.append((pname, label, frac))
    log(f'additions: {len(tasks)} added solids checked, {len(lost)} lost to their part\'s cuts')
    return lost


def export(model, outdir, log=print):
    """Each part as <name>.stl in its print orientation (flipped parts turned over, then set on
    z = 0), and <name>-assembled.stl in place. Returns {name: (print path, assembled path)}."""
    os.makedirs(outdir, exist_ok=True)
    out = {}
    for name, p in model.parts.items():
        src = p.scad(engrave=True)
        asm = os.path.join(outdir, f'{name}-assembled.stl')
        openscad(src, asm)
        x0, y0, z0, x1, y1, z1 = p.bounds()
        if p.print_flip:           # turned over about X: z -> -z, y -> -y; then onto the bed
            ps = f'translate([0,{f(y0 + y1)},{f(z1)}]) rotate([180,0,0]) {src}'
        else:
            ps = f'translate([0,0,{f(-z0)}]) {src}'
        pp = os.path.join(outdir, f'{name}.stl')
        openscad(ps, pp)
        open(os.path.join(outdir, f'{name}.scad'), 'w').write(ps + '\n')
        n, vol, (lo, hi) = stl_stats(pp)
        log(f'export: {name}.stl  {n} triangles, {vol / 1000:.1f} cm3, '
            f'{hi[0] - lo[0]:.1f} x {hi[1] - lo[1]:.1f} x {hi[2] - lo[2]:.1f} mm')
        out[name] = (pp, asm)
    return out


def report_margins(model, log=print):
    fails = 0
    for label, value, limit, kind, note in model.margins:
        ok = value >= limit if kind == 'min' else value <= limit
        fails += not ok
        rel = f'{value - limit:+.2f}' if kind == 'min' else f'{limit - value:+.2f}'
        log(f"{'ok  ' if ok else 'FAIL'} {label}: {value:.2f} mm ({'at least' if kind == 'min' else 'at most'} "
            f"{limit:.2f}, margin {rel}){'  ' + note if note else ''}")
    return fails


# ------------------------------------------------------------------ plan-view helpers

def gap2d(a, b):
    """Plan-view distance between two solids standing along Z (circles, boxes, hulls of circles in a
    row); negative when they overlap."""
    def prims(s):
        if isinstance(s, Cyl) and s.axis == 'z':
            return [('c', s.u, s.v, s.r)]
        if isinstance(s, Box):
            return [('b', s.x0, s.y0, s.x1, s.y1)]
        if isinstance(s, Hull) and all(isinstance(i, Cyl) and i.axis == 'z' for i in s.items):
            cs = s.items
            out = [('c', c.u, c.v, c.r) for c in cs]
            xs, ys, r = [c.u for c in cs], [c.v for c in cs], cs[0].r
            if max(ys) - min(ys) < 1e-6:           # a row: the straight part between the end circles
                out.append(('b', min(xs), ys[0] - r, max(xs), ys[0] + r))
            elif max(xs) - min(xs) < 1e-6:
                out.append(('b', xs[0] - r, min(ys), xs[0] + r, max(ys)))
            return out
        return [('b',) + tuple(s.bounds[i] for i in (0, 1, 3, 4))]

    def d(p, q):
        if p[0] == 'c' and q[0] == 'c':
            return math.hypot(p[1] - q[1], p[2] - q[2]) - p[3] - q[3]
        if p[0] == 'b' and q[0] == 'b':
            dx = max(p[1] - q[3], q[1] - p[3], 0)
            dy = max(p[2] - q[4], q[2] - p[4], 0)
            return math.hypot(dx, dy) if dx or dy else -1
        c, b = (p, q) if p[0] == 'c' else (q, p)
        dx = max(b[1] - c[1], 0, c[1] - b[3])
        dy = max(b[2] - c[2], 0, c[2] - b[4])
        return math.hypot(dx, dy) - c[3]
    return min(d(p, q) for p in prims(a) for q in prims(b))


def narrowest_web(cuts):
    """The narrowest web between any two through-cuts, and which two."""
    worst = (1e9, '')
    for i in range(len(cuts)):
        for j in range(i + 1, len(cuts)):
            g = gap2d(cuts[i], cuts[j])
            if g < worst[0]:
                worst = (g, f'{cuts[i].label} / {cuts[j].label}')
    return worst


def cluster(points, dist):
    """Group (label, x, y) points closer than dist to any other in the group."""
    groups = []
    for p in points:
        hit = [g for g in groups if any(math.hypot(p[1] - o[1], p[2] - o[2]) < dist for o in g)]
        merged = [p] + [o for g in hit for o in g]
        groups = [g for g in groups if g not in hit] + [merged]
    return groups


def measure_check(model, part, ref, ours, kicad, tol=0.3):
    """The measurement check: a body drawn from the maker's drawing (nominal, seated on the board)
    against KiCad's 3D model of the same part. Differences over tol are findings, one per part
    type and face, listing every ref; a person reviews each (Model.reviewed)."""
    if ours is None or kicad is None:
        return
    names = ('x min', 'y min', 'z min', 'x max', 'y max', 'z max')
    for k in range(6):
        if k == 2:
            continue               # below the board: tails are drawn from the drawings' lead lengths
        d = ours[k] - kicad[k]
        if abs(d) > tol + 1e-6:
            key = f'{part}:{names[k]}'
            reg = model.__dict__.setdefault('_measure', {})
            refs = reg.setdefault(key, [])
            refs.append(ref)
            text = f'{part} ({names[k]}): drawing {ours[k]:.2f} vs KiCad model {kicad[k]:.2f}, {d:+.2f} mm ({" ".join(refs)})'
            model.findings = [(fk, t) for fk, t in model.findings if fk != key] + [(key, text)]


def join_close(cuts, min_web):
    """Through-cuts closer than min_web become one (their hull): a web that thin would not print."""
    cuts = list(cuts)
    while True:
        pairs = [(gap2d(a, b), i, j) for i, a in enumerate(cuts) for j, b in enumerate(cuts) if i < j]
        close = [p for p in pairs if p[0] < min_web]
        if not close:
            return cuts
        _, i, j = min(close)
        a, b = cuts[i], cuts[j]
        joined = Hull([a, b], label=f'{a.label} + {b.label}')
        cuts = [c for k, c in enumerate(cuts) if k not in (i, j)] + [joined]


# ------------------------------------------------------------------ self-test

def selftest_judge(model, workdir, log=print, jobs=None):
    """Plant one fault per group, the group's first item added to the simplest printed part, and
    check that the judge reports it by name. Returns (planted, caught)."""
    base = min(model.parts.values(), key=lambda p: len(p.scad(engrave=False)))
    groups = {}
    for g, label, s, cl in model.envelopes:
        groups.setdefault(('envelope', g), []).append((label, s.grown(cl) if cl else s))
    for g, label, s in model.keepclear:
        groups.setdefault(('keep-clear', g), []).append((label, s))
    probe = Model(model.name + '-selftest')
    for (kind, g), items in groups.items():
        p = probe.part(f'probe {g}', base.color)
        p.add, p.cut = base.add + [items[0][1]], []           # the fault: a solid right where the part must not be
        (probe.envelope if kind == 'envelope' else probe.keep)(*((g, items[0][0], items[0][1], 0) if kind == 'envelope'
                                                                 else (g, items[0][0], items[0][1])))
    hits = judge(probe, workdir, log=lambda s: None, jobs=jobs, pairs={(f'probe {g}', g) for (_, g) in groups})
    named = {(pn[len('probe '):], label) for pn, g, label, v, b in hits}
    caught = sum(1 for (kind, g), items in groups.items() if (g, items[0][0]) in named)
    log(f'judge self-test: {len(groups)} planted, {caught} caught and named')
    return len(groups), caught
