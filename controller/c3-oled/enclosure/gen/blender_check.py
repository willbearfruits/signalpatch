"""Mesh checks on the exported STLs, run by build.py inside Blender (headless):

    blender -b --factory-startup -P blender_check.py -- --out mesh.json --bed X Y Z --min-wall 0.8 --bridge 10 --stl a.stl ...

For each part, in its print orientation:
  - closed and manifold (every edge between exactly two faces), normals outward (positive volume);
  - on the bed (lowest point at z = 0, with a flat first layer) and inside the bed's volume;
  - overhangs: faces pointing down more steeply than 45 degrees above the bed, joined into islands.
    A flat island is a bridge; its span is its shorter side, at most --bridge. A sloped island is an
    overhang that needs support unless it is under 1 mm tall;
  - the thinnest wall: from the centre of every face, a ray into the material to the next surface
    (grazing hits along the same face are skipped); at least --min-wall. Engraved letters are left
    out (--exclude, boxes in the print frame): the spec measures their strokes and gaps itself.
"""
import json
import sys

import bmesh
import bpy
from mathutils.bvhtree import BVHTree


def args():
    a = sys.argv[sys.argv.index('--') + 1:]
    out, bed, wall, bridge, stls, excl, i = None, None, 0.8, 10.0, [], {}, 0
    while i < len(a):
        if a[i] == '--out':
            out, i = a[i + 1], i + 2
        elif a[i] == '--bed':
            bed, i = [float(v) for v in a[i + 1:i + 4]], i + 4
        elif a[i] == '--min-wall':
            wall, i = float(a[i + 1]), i + 2
        elif a[i] == '--bridge':
            bridge, i = float(a[i + 1]), i + 2
        elif a[i] == '--exclude':
            excl, i = json.loads(a[i + 1]), i + 2
        elif a[i] == '--stl':
            stls, i = a[i + 1:], len(a)
        else:
            raise SystemExit(f'unknown argument {a[i]}')
    return out, bed, wall, bridge, stls, excl


def check(path, bed, min_wall, max_bridge, skip=()):
    bpy.ops.wm.read_factory_settings(use_empty=True)
    bpy.ops.wm.stl_import(filepath=path)
    ob = bpy.context.selected_objects[0]
    bm = bmesh.new()
    bm.from_mesh(ob.data)
    bmesh.ops.remove_doubles(bm, verts=bm.verts, dist=1e-4)
    bm.normal_update()
    bm.faces.ensure_lookup_table()
    problems = []
    nm = sum(1 for e in bm.edges if not e.is_manifold)
    vol = bm.calc_volume(signed=True)
    zs = [v.co.z for v in bm.verts]
    lo = [min(v.co[k] for v in bm.verts) for k in range(3)]
    hi = [max(v.co[k] for v in bm.verts) for k in range(3)]
    size = [hi[k] - lo[k] for k in range(3)]
    first = sum(f.calc_area() for f in bm.faces if f.normal.z < -0.999 and max(v.co.z for v in f.verts) < 1e-3)
    on_bed = abs(min(zs)) < 1e-3 and first > 100
    fits = all(size[k] <= bed[k] for k in range(3))
    if nm:
        problems.append(f'{nm} non-manifold edges')
    if vol <= 0:
        problems.append('normals point inward')
    if not on_bed:
        problems.append(f'not flat on the bed (lowest z {min(zs):.3f}, first layer {first:.0f} mm2)')
    if not fits:
        problems.append(f'larger than the bed {bed}')

    # overhangs
    down = {f.index for f in bm.faces if f.normal.z < -0.72 and min(v.co.z for v in f.verts) > 0.05}
    seen, islands = set(), []
    for s in down:
        if s in seen:
            continue
        stack, isl = [s], []
        seen.add(s)
        while stack:
            f = bm.faces[stack.pop()]
            isl.append(f)
            for e in f.edges:
                for g in e.link_faces:
                    if g.index in down and g.index not in seen:
                        seen.add(g.index)
                        stack.append(g.index)
        islands.append(isl)
    max_bridge_seen = 0.0
    for isl in islands:
        vs = [v.co for f in isl for v in f.verts]
        dx = max(v.x for v in vs) - min(v.x for v in vs)
        dy = max(v.y for v in vs) - min(v.y for v in vs)
        dz = max(v.z for v in vs) - min(v.z for v in vs)
        area = sum(f.calc_area() for f in isl)
        flat = all(f.normal.z < -0.99 for f in isl)
        at = (sum(v.x for v in vs) / len(vs), sum(v.y for v in vs) / len(vs), min(v.z for v in vs))
        if flat:
            span = min(dx, dy)
            max_bridge_seen = max(max_bridge_seen, span)
            if span > max_bridge:
                problems.append(f'bridge {span:.1f} mm wide ({dx:.1f} x {dy:.1f}) at z {at[2]:.1f}, '
                                f'near ({at[0]:.1f}, {at[1]:.1f})')
        elif dz > 1.0 and area > 1.0:
            problems.append(f'overhang steeper than 45 deg, {dz:.1f} mm tall, {area:.1f} mm2, near '
                            f'({at[0]:.1f}, {at[1]:.1f}, {at[2]:.1f})')

    # thinnest wall
    bvh = BVHTree.FromBMesh(bm)
    walls = []
    def skipped(p):
        return any(b[0] <= p.x <= b[3] and b[1] <= p.y <= b[4] and b[2] <= p.z <= b[5] for b in skip)
    for f in bm.faces:
        if f.calc_area() < 0.02:
            continue
        n = f.normal
        o = f.calc_center_median() - n * 1e-4
        if skipped(o):
            continue
        loc, hn, idx, dist = bvh.ray_cast(o, -n, 500.0)
        if loc is None or abs(hn.dot(n)) < 0.3:
            continue
        walls.append((dist + 1e-4, tuple(round(c, 2) for c in o)))
    walls.sort()
    thin = [w for w in walls if w[0] < min_wall - 1e-3]     # STL coordinates carry float noise
    if thin:
        spots = []
        for d, p in thin:
            if all(sum((p[k] - q[k]) ** 2 for k in range(3)) > 4.0 for _, q in spots):
                spots.append((d, p))
        for d, p in spots[:8]:
            problems.append(f'wall {d:.2f} mm at ({p[0]:.1f}, {p[1]:.1f}, {p[2]:.1f})')
    return dict(tris=len(bm.faces), volume_cm3=abs(vol) / 1000, size=size, non_manifold=nm, outward=vol > 0,
                on_bed=on_bed, fits=fits, first_layer_mm2=first, overhang_islands=len(islands),
                max_bridge=max_bridge_seen, min_wall=walls[0][0] if walls else 0.0,
                min_wall_at=walls[0][1] if walls else None, problems=problems, ok=not problems)


def main():
    out, bed, wall, bridge, stls, excl = args()
    res = {}
    for p in stls:
        name = p.rsplit('/', 1)[-1].rsplit('.', 1)[0]
        res[name] = check(p, bed, wall, bridge, excl.get(name, ()))
        print(name, 'ok' if res[name]['ok'] else res[name]['problems'])
    json.dump(res, open(out, 'w'), indent=1)


main()
