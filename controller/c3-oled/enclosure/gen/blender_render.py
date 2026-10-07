"""Renders of the enclosure, run by build.py inside Blender (headless, Cycles on the CPU):

    blender -b --factory-startup -P blender_render.py -- --out images/ --glb board.joined.glb
            --knobs '<json from spec.render_extras()>' --stl shell-assembled.stl lid-assembled.stl
            --colors shell lid --print shell.stl lid.stl

The printed parts are the exported STLs; the board is KiCad's own 3D export of it (metres, Y up,
centred on the board), placed in the enclosure frame. Knobs, nuts, plugs and jumpers are simple
stand-ins drawn from the spec's numbers, for the pictures only. Sections are real cuts: a boolean
on the printed parts and stand-ins (closed meshes), a bisection on the board's meshes (KiCad's are
open shells). Every picture is a render, not a photo.
"""
import json
import math
import os
import sys

import bmesh
import bpy
from mathutils import Vector

COLORS = {'shell': (0.045, 0.048, 0.055), 'lid': (0.70, 0.68, 0.64)}


def rgb(col):
    """A part's colour: a name in COLORS, or '#rrggbb' (sRGB, turned linear for the shader)."""
    if col in COLORS:
        return COLORS[col]
    if col.startswith('#') and len(col) == 7:
        return tuple((int(col[i:i + 2], 16) / 255) ** 2.2 for i in (1, 3, 5))
    return (0.5, 0.5, 0.5)


def part_names(paths):
    """Parts are keyed by name (from '<name>-assembled.stl'), so two parts may share a colour."""
    return [os.path.basename(p)[:-len('-assembled.stl')] if p.endswith('-assembled.stl') else os.path.splitext(os.path.basename(p))[0]
            for p in paths]


def args():
    a = sys.argv[sys.argv.index('--') + 1:]
    d, key = {}, None
    for x in a:
        if x.startswith('--'):
            key = x[2:]
            d[key] = []
        else:
            d[key].append(x)
    return d


def mat(name, rgb, rough=0.55, metal=0.0):
    m = bpy.data.materials.new(name)
    m.use_nodes = True
    b = next(n for n in m.node_tree.nodes if n.type == 'BSDF_PRINCIPLED')
    b.inputs['Base Color'].default_value = (*rgb, 1)
    b.inputs['Roughness'].default_value = rough
    b.inputs['Metallic'].default_value = metal
    return m


def import_stl(path, material, name):
    bpy.ops.wm.stl_import(filepath=path)
    ob = bpy.context.selected_objects[0]
    ob.name = name
    ob.data.materials.clear()
    ob.data.materials.append(material)
    try:
        bpy.ops.object.shade_auto_smooth(angle=math.radians(35))
    except Exception:
        pass
    return ob


def import_board(glb):
    before = set(bpy.data.objects)
    bpy.ops.import_scene.gltf(filepath=glb)
    new = [o for o in bpy.data.objects if o not in before]
    root = bpy.data.objects.new('board', None)
    bpy.context.scene.collection.objects.link(root)
    for o in new:
        if o.parent is None:
            o.parent = root
    root.scale = (1000, 1000, 1000)
    root.location = (50.0, 50.0, -1.595)          # KiCad GLB: centred, board top at 1.595 mm
    bpy.context.view_layer.update()
    return [o for o in new if o.type == 'MESH']


def cyl(name, x, y, z0, z1, r, material, verts=64, axis='z'):
    bpy.ops.mesh.primitive_cylinder_add(vertices=verts, radius=r, depth=abs(z1 - z0))
    ob = bpy.context.object
    ob.name = name
    if axis == 'x':                                 # (x, y, z0, z1) = (y, z, x0, x1) along X
        ob.rotation_euler = (0, math.pi / 2, 0)
        ob.location = ((z0 + z1) / 2, x, y)
    else:
        ob.location = (x, y, (z0 + z1) / 2)
    ob.data.materials.append(material)
    return ob


def stand_ins(extra, mats):
    obs, curves = [], []
    for k in extra.get('knobs', []):
        x, y = k['x'], k['y']
        obs.append(cyl('washer', x, y, extra['lid_top'], extra['lid_top'] + extra['washer_t'], extra['washer_od'] / 2, mats['steel']))
        z = extra['lid_top'] + extra['washer_t']
        obs.append(cyl('nut', x, y, z, z + extra['nut_t'], extra['nut_af'] / math.sqrt(3), mats['steel'], verts=6))
        obs.append(cyl('knob', x, y, extra['knob_z0'], extra['knob_z1'], extra['knob_d'] / 2, mats['knob']))
        a = math.radians(k.get('angle', 0))
        bpy.ops.mesh.primitive_cube_add(size=1, location=(x - math.sin(a) * extra['knob_d'] * 0.25,
                                                          y + math.cos(a) * extra['knob_d'] * 0.25, extra['knob_z1'] + 0.05))
        p = bpy.context.object
        p.scale = (0.9, extra['knob_d'] * 0.45, 0.2)
        p.rotation_euler = (0, 0, a)
        p.data.materials.append(mats['white'])
        obs.append(p)
    for b in extra.get('boxes', []):            # parts without a 3D model, drawn as their boxes
        x0, y0, z0, x1, y1, z1 = b['box']
        bpy.ops.mesh.primitive_cube_add(size=1, location=((x0 + x1) / 2, (y0 + y1) / 2, (z0 + z1) / 2))
        o = bpy.context.object
        o.name = b['name']
        o.scale = (x1 - x0, y1 - y0, z1 - z0)
        o.data.materials.append(mat(b['name'], tuple(b.get('color', (0.3, 0.3, 0.3))), rough=0.5))
        obs.append(o)
    for w in extra.get('wires', []):            # polylines, drawn as round wires
        c = bpy.data.curves.new('wire', 'CURVE')
        c.dimensions, c.bevel_depth, c.bevel_resolution = '3D', w.get('r', 0.65), 4
        sp = c.splines.new('POLY')
        sp.points.add(len(w['points']) - 1)
        for pt, (x, y, z) in zip(sp.points, w['points']):
            pt.co = (x, y, z, 1)
        o = bpy.data.objects.new('wire', c)
        bpy.context.scene.collection.objects.link(o)
        o.data.materials.append(mat('wire', tuple(w.get('color', (0.8, 0.1, 0.1))), rough=0.4))
        curves.append(o)
    for pl in extra.get('plugs', []):           # along X: (y, z) axis, from x0 to x1
        obs.append(cyl(pl['name'], pl['y'], pl['z'], pl['x0'], pl['x1'], pl['r'], mats[pl.get('mat', 'knob')], axis='x'))
    for j in extra.get('jumpers', []):
        (x0, y0), (x1, y1), col = j['a'], j['b'], j.get('color', (0.8, 0.1, 0.1))
        m = mat('wire', col, rough=0.4)
        z0, z1 = extra['plug_z0'], extra['plug_z1']
        for x, y in ((x0, y0), (x1, y1)):
            bpy.ops.mesh.primitive_cube_add(size=1, location=(x, y, (z0 + z1) / 2))
            h = bpy.context.object
            h.scale = (2.5, 2.5, z1 - z0)
            h.data.materials.append(mats['knob'])
            obs.append(h)
        c = bpy.data.curves.new('wire', 'CURVE')
        c.dimensions = '3D'
        c.bevel_depth = 0.8
        c.bevel_resolution = 4
        s = c.splines.new('BEZIER')
        s.bezier_points.add(1)
        d = math.hypot(x1 - x0, y1 - y0)
        for bp, (x, y) in ((s.bezier_points[0], (x0, y0)), (s.bezier_points[1], (x1, y1))):
            bp.co = (x, y, z1)
            bp.handle_left_type = bp.handle_right_type = 'FREE'
        s.bezier_points[0].handle_right = (x0, y0, z1 + 0.45 * d + 8)
        s.bezier_points[0].handle_left = (x0, y0, z1 - 1)
        s.bezier_points[1].handle_left = (x1, y1, z1 + 0.45 * d + 8)
        s.bezier_points[1].handle_right = (x1, y1, z1 - 1)
        w = bpy.data.objects.new('wire', c)
        bpy.context.scene.collection.objects.link(w)
        w.data.materials.append(m)
        curves.append(w)
    return obs, curves


def look(ob, target):
    d = Vector(target) - ob.location
    ob.rotation_euler = d.to_track_quat('-Z', 'Y').to_euler()


def setup(size):
    sc = bpy.context.scene
    sc.render.engine = 'CYCLES'
    sc.cycles.device = 'CPU'
    sc.cycles.samples = 96
    sc.cycles.use_denoising = True
    sc.render.resolution_x, sc.render.resolution_y = size
    sc.view_settings.view_transform = 'AgX'
    w = bpy.data.worlds.new('w')
    sc.world = w
    w.use_nodes = True
    bg = next(n for n in w.node_tree.nodes if n.type == 'BACKGROUND')
    bg.inputs['Color'].default_value = (0.82, 0.83, 0.85, 1)
    bg.inputs['Strength'].default_value = 0.55
    for name, loc, energy, size_ in (('key', (-120, -160, 260), 1.4e6, 180), ('fill', (260, -40, 120), 4.0e5, 200),
                                     ('rim', (40, 260, 160), 6.0e5, 150)):
        L = bpy.data.lights.new(name, 'AREA')
        L.energy, L.size = energy, size_
        o = bpy.data.objects.new(name, L)
        sc.collection.objects.link(o)
        o.location = loc
        look(o, (50, 50, 0))
    bpy.ops.mesh.primitive_plane_add(size=4000, location=(50, 50, -30))
    fl = bpy.context.object
    fl.name = 'floor'
    fl.data.materials.append(mat('floor', (0.62, 0.63, 0.65), rough=0.8))
    return fl


def camera(loc, target, lens=50, ortho=None):
    sc = bpy.context.scene
    if sc.camera is None:
        c = bpy.data.objects.new('cam', bpy.data.cameras.new('cam'))
        sc.collection.objects.link(c)
        sc.camera = c
    c = sc.camera
    c.location = loc
    look(c, target)
    c.data.lens = lens
    c.data.type = 'ORTHO' if ortho else 'PERSP'
    if ortho:
        c.data.ortho_scale = ortho
    c.data.clip_start, c.data.clip_end = 1, 5000


def shot(out, name, size=(1600, 1200)):
    sc = bpy.context.scene
    sc.render.resolution_x, sc.render.resolution_y = size
    sc.render.filepath = os.path.join(out, name)
    bpy.ops.render.render(write_still=True)
    print('render', name)


def build(a, extra, stand=True):
    """A fresh scene: the printed parts in place, the board, and the stand-ins."""
    bpy.ops.wm.read_factory_settings(use_empty=True)
    floor = setup((1600, 1200))
    mats = dict(steel=mat('steel', (0.75, 0.75, 0.76), rough=0.3, metal=1.0), knob=mat('knob', (0.03, 0.03, 0.03), rough=0.45),
                white=mat('white', (0.9, 0.9, 0.9), rough=0.5), plug=mat('plug', (0.12, 0.12, 0.13), rough=0.5))
    parts = {n: import_stl(path, mat(n, rgb(col)), n) for n, path, col in zip(part_names(a['stl']), a['stl'], a['colors'])}
    board = import_board(a['glb'][0]) if a.get('glb') and os.path.exists(a['glb'][0]) else []
    obs, curves = stand_ins(extra, mats) if stand else ([], [])
    if parts:                                   # the floor under the lowest part
        floor.location.z = min((ob.matrix_world @ Vector(c)).z for ob in parts.values() for c in ob.bound_box) - 0.01
    return dict(parts=parts, board=board, stand=obs, curves=curves, floor=floor)


def section(scene, axis, value, keep_above=True):
    """Cut everything at the plane axis = value, keeping the side above it (or below)."""
    k = 'xyz'.index(axis)
    big = 1000.0
    loc = [50.0, 50.0, 0.0]
    loc[k] = value - big / 2 if keep_above else value + big / 2
    bpy.ops.mesh.primitive_cube_add(size=big, location=loc)
    cutter = bpy.context.object
    cutter.hide_render = True
    for ob in list(scene['parts'].values()) + scene['stand']:
        mod = ob.modifiers.new('cut', 'BOOLEAN')
        mod.operation, mod.object, mod.solver = 'DIFFERENCE', cutter, 'EXACT'
        bpy.context.view_layer.objects.active = ob
        bpy.ops.object.modifier_apply(modifier=mod.name)
    for ob in scene['board']:
        mw = ob.matrix_world
        inv = mw.inverted()
        co = Vector((0, 0, 0))
        co[k] = value
        no = Vector((0, 0, 0))
        no[k] = 1.0
        lco = inv @ co
        lno = (mw.to_3x3().transposed() @ no).normalized()      # normals go to local space by the transpose
        bm = bmesh.new()
        bm.from_mesh(ob.data)
        geom = bm.verts[:] + bm.edges[:] + bm.faces[:]
        bmesh.ops.bisect_plane(bm, geom=geom, plane_co=lco, plane_no=lno, clear_inner=keep_above, clear_outer=not keep_above)
        bm.to_mesh(ob.data)
        bm.free()
    for ob in scene['stand']:
        if len(ob.data.vertices) == 0:
            ob.hide_render = True


def default_views(parts):
    """Views from the parts' size: assembled three-quarter, top, exploded."""
    bb = [ob.matrix_world @ Vector(c) for ob in parts.values() for c in ob.bound_box]
    lo = Vector([min(v[k] for v in bb) for k in range(3)])
    hi = Vector([max(v[k] for v in bb) for k in range(3)])
    c, d = (lo + hi) / 2, (hi - lo).length
    return [dict(name='01-assembled.png', cam=(c.x + 1.05 * d, c.y - 0.75 * d, c.z + 0.95 * d), target=tuple(c), lens=55),
            dict(name='02-top.png', cam=(c.x, c.y, hi.z + 300), target=(c.x, c.y + 1e-4, 0), ortho=1.08 * max(hi.x - lo.x, hi.y - lo.y),
                 size=(1400, round(1400 * min(1.0, (hi.y - lo.y) / (hi.x - lo.x) + 0.1)))),
            dict(name='04-exploded.png', cam=(c.x + 1.05 * d, c.y - 0.85 * d, c.z + 1.15 * d), target=tuple(c), lens=50,
                 explode=max(30.0, 1.6 * (hi.z - lo.z)), hide='all')]


def main():
    a = args()
    out = a['out'][0]
    os.makedirs(out, exist_ok=True)
    extra = json.loads(a['knobs'][0]) if a.get('knobs') else {}
    sc = build(a, extra)
    views = extra.get('views') or default_views(sc['parts'])
    dirty = False                                   # a section cuts the scene: the next view starts afresh
    for v in views:
        if v.get('section') or dirty:
            sc = build(a, extra)
            dirty = bool(v.get('section'))
        else:
            for o in sc['stand'] + sc['curves']:
                o.hide_render = False
            for o in sc['parts'].values():
                o.hide_render = False
        hide, show = v.get('hide', []), tuple(v.get('show', []))
        for o in sc['stand'] + sc['curves']:
            if (hide == 'all' and not o.name.startswith(show or ('\0',))) or (hide != 'all' and o.name.startswith(tuple(hide) or ('\0',))):
                o.hide_render = True
        for n in v.get('hide_parts', []):
            sc['parts'][n].hide_render = True
        for n, ob in sc['parts'].items():        # every lid lifts in an exploded view
            if 'lid' in n:
                ob.location.z = v.get('explode', 0.0)
        if v.get('section'):
            sec = v['section']
            section(sc, sec['axis'], sec['value'], sec.get('keep_above', True))
        camera(tuple(v['cam']), tuple(v['target']), lens=v.get('lens', 50), ortho=v.get('ortho'))
        shot(out, v['name'], size=tuple(v.get('size', (1600, 1200))))

    # last: the print plates, the parts as they print, packed in rows onto as few 220 x 220 beds as they fit
    bpy.ops.wm.read_factory_settings(use_empty=True)
    floor = setup((1600, 1000))
    floor.location.z = -2
    bedm = mat('bed', (0.55, 0.47, 0.33), rough=0.7)          # a textured PEI sheet's colour
    items = []
    for path, col, n in zip(a['print'], a['colors'], part_names(a['print'])):
        ob = import_stl(path, mat(n + '-print', rgb(col)), n + '-print')
        bb = [ob.matrix_world @ Vector(c) for c in ob.bound_box]
        lo = Vector([min(v[k] for v in bb) for k in range(3)])
        hi = Vector([max(v[k] for v in bb) for k in range(3)])
        items.append((ob, lo, hi - lo))
    bed, gap, bed_i, x, y, row = 220.0, 8.0, 0, 8.0, 8.0, 0.0
    for ob, lo, sz in sorted(items, key=lambda t: -t[2].y):
        if x + sz.x > bed - gap + 1e-6 and x > gap:         # the next row
            x, y, row = gap, y + row + gap, 0.0
        if y + sz.y > bed - gap + 1e-6 and y > gap:         # the next bed
            bed_i, x, y, row = bed_i + 1, gap, gap, 0.0
        ob.location.x += bed_i * 240 + x - lo.x
        ob.location.y += y - lo.y
        x, row = x + sz.x + gap, max(row, sz.y)
    nb = bed_i + 1
    for i in range(nb):
        bpy.ops.mesh.primitive_plane_add(size=bed, location=(110 + i * 240, 110, -0.01))
        bpy.context.object.data.materials.append(bedm)
    mid, k = 110 + (nb - 1) * 120, max(1.0, nb / 2)
    camera((mid, 110 - 340 * k, 330 * k), (mid, 110, 0), lens=40)
    shot(out, f'{len(views) + 1:02d}-print-plates.png', size=(1600, 1000))


main()
