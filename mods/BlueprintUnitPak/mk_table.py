import bpy, bmesh, sys, math
from mathutils import Vector
# blender -b -P mk_table.py -- out.fbx [preview_prefix]
a = sys.argv[sys.argv.index('--') + 1:]
out = a[0]
s = 0.01  # cm -> Blender m
bpy.ops.wm.read_factory_settings(use_empty=True)
SEG = 48
NAMES = ('Orange1', 'White1', 'Grey1', 'Black1', 'Glow1', 'Screen1', 'Grid1')
M = {n: bpy.data.materials.new(n) for n in NAMES}
parts, hulls = [], []
# UV rule: 1 uv unit = 1 m everywhere (matches vanilla world-ish trimsheet tiling), except
# Grid1 (disc 0..1 planar), Screen1 (each panel 0..1) and Glow1 (collapsed to one texel = solid colour).

def edit_uv(o, fn):
    bm = bmesh.new(); bm.from_mesh(o.data)
    uv = bm.loops.layers.uv.verify()
    for f in bm.faces:
        for l in f.loops:
            l[uv].uv = fn(f, l)
    bm.to_mesh(o.data); bm.free()

def fin(mat, o=None):
    o = o or bpy.context.active_object
    bpy.ops.object.transform_apply(location=False, rotation=True, scale=True)
    for m in (mat if isinstance(mat, (list, tuple)) else [mat]):
        o.data.materials.append(M[m])
    parts.append(o)
    return o

def uv_round(o, slant=1.0, solid=False):
    # caps: planar world metres; sides: u = angle * integer turns (seamless), v = height (slant-corrected)
    me = o.data
    rs = [Vector((v.co.x, v.co.y)).length for v in me.vertices]
    K = max(1, round(2 * math.pi * (sum(rs) / len(rs))))
    zmin = min(v.co.z for v in me.vertices) + o.location.z
    def fn(f, l):
        if solid: return (0.5, 0.5)
        zs = [v.co.z for v in f.verts]
        p = l.vert.co
        if max(zs) - min(zs) < 1e-6:
            return (p.x, p.y)
        c = f.calc_center_median()
        ca = math.atan2(c.y, c.x)
        ang = math.atan2(p.y, p.x)
        while ang - ca > math.pi: ang -= 2 * math.pi
        while ang - ca < -math.pi: ang += 2 * math.pi
        return (ang / (2 * math.pi) * K, (p.z + o.location.z - zmin) * slant)
    edit_uv(o, fn)

def cyl(r, z0, z1, mat, solid=False):
    bpy.ops.mesh.primitive_cylinder_add(vertices=SEG, radius=r * s, depth=(z1 - z0) * s, location=(0, 0, (z0 + z1) / 2 * s))
    o = bpy.context.active_object
    uv_round(o, solid=solid)
    return fin(mat, o)

def cone(r0, r1, z0, z1, mat):
    bpy.ops.mesh.primitive_cone_add(vertices=SEG, radius1=r0 * s, radius2=r1 * s, depth=(z1 - z0) * s, location=(0, 0, (z0 + z1) / 2 * s))
    o = bpy.context.active_object
    L = math.hypot(r1 - r0, z1 - z0) / (z1 - z0)
    uv_round(o, slant=L)
    return fin(mat, o)

def box_uv(o):
    # box mapping in local metres by dominant normal axis
    def fn(f, l):
        n = f.normal; p = l.vert.co
        ax = max(range(3), key=lambda i: abs(n[i]))
        return {0: (p.y, p.z), 1: (p.x, p.z), 2: (p.x, p.y)}[ax]
    edit_uv(o, fn)

def place(o, r, ang, z, tilt=0):
    t = math.radians(ang)
    o.location = (0, 0, 0)
    o.rotation_euler = (math.radians(tilt), 0, 0)
    bpy.ops.object.transform_apply(location=False, rotation=True)
    o.rotation_euler = (0, 0, t + math.pi / 2)  # local X = tangent, local Y = radial
    bpy.ops.object.transform_apply(location=False, rotation=True)
    o.location = (r * math.cos(t) * s, r * math.sin(t) * s, z * s)
    bpy.ops.object.transform_apply(location=True)

def box(r, ang, z, sx, sy, sz, mat):
    # sx tangent, sy radial, sz up (cm), centred at radius r, angle ang, height z
    bpy.ops.mesh.primitive_cube_add(size=1)
    o = bpy.context.active_object
    o.scale = (sx * s, sy * s, sz * s)
    bpy.ops.object.transform_apply(location=False, scale=True)
    box_uv(o)
    place(o, r, ang, z)
    return fin(mat, o)

def mesh_obj(name, bm):
    me = bpy.data.meshes.new(name); bm.to_mesh(me); bm.free()
    o = bpy.data.objects.new(name, me); bpy.context.collection.objects.link(o)
    bpy.context.view_layer.objects.active = o
    for x in bpy.context.selected_objects: x.select_set(False)
    o.select_set(True)
    return o

# data panel: wedge housing (inner edge tall, slope falls outward) + one 0..1 screen quad on the slope
PW, PD, HI, HO, SW, SL = 40.0, 21.0, 17.0, 3.0, 36.0, 20.25   # width, depth, inner/outer heights, screen w, slope length (16:9)
def panel(ang, r=147, z=100):
    bm = bmesh.new()
    prof = [(-PD / 2, 0), (PD / 2, 0), (PD / 2, HI), (-PD / 2, HO)]  # local +y = toward centre
    vs = [[bm.verts.new((x * s, y * s, h * s)) for (y, h) in prof] for x in (-PW / 2, PW / 2)]
    bm.faces.new(vs[0]); bm.faces.new(vs[1])
    for i in range(4):
        bm.faces.new((vs[0][i], vs[0][(i + 1) % 4], vs[1][(i + 1) % 4], vs[1][i]))
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    uv = bm.loops.layers.uv.verify()
    for f in bm.faces:
        n = f.normal
        ax = max(range(3), key=lambda i: abs(n[i]))
        for l in f.loops:
            p = l.vert.co
            l[uv].uv = {0: (p.y, p.z), 1: (p.x, p.z), 2: (p.x, p.y)}[ax]
    wedge = mesh_obj('wedge', bm)
    # hull = the wedge itself (convex)
    h = wedge.copy(); h.data = wedge.data.copy(); bpy.context.collection.objects.link(h); hulls.append((h, r, ang, z))
    place(wedge, r, ang, z)
    fin('Grey1', wedge)
    # screen quad on slope
    d = Vector((0, PD, HI - HO)).normalized()  # outer-low -> inner-high
    n = Vector((0, -(HI - HO), PD)).normalized()   # outward-up
    c = Vector((0, -PD / 2, HO)) + d * (math.hypot(PD, HI - HO) / 2) + n * 0.25
    bm = bmesh.new()
    pts = []
    for (sx_, st, uvc) in ((-1, -1, (0, 0)), (1, -1, (1, 0)), (1, 1, (1, 1)), (-1, 1, (0, 1))):
        pts.append((bm.verts.new(((sx_ * SW / 2) * s, (c + d * (st * SL / 2)).y * s, (c + d * (st * SL / 2)).z * s)), uvc))
    f = bm.faces.new([p for p, _ in pts])
    if f.normal.dot(n) < 0:
        f.normal_flip();
    f.normal_update()
    uvl = bm.loops.layers.uv.verify()
    for l in f.loops:
        l[uvl].uv = [u for p, u in pts if p == l.vert][0]
    scr = mesh_obj('screen', bm)
    place(scr, r, ang, z)
    fin('Screen1', scr)

def hull_cyl(r, z0, z1, rtop=None):
    # 16-gon prism/frustum, circumradius = r (inside the 158 footprint)
    bm = bmesh.new()
    n = 16
    bot = [bm.verts.new((r * s * math.cos(2 * math.pi * i / n), r * s * math.sin(2 * math.pi * i / n), z0 * s)) for i in range(n)]
    rt = rtop or r
    top = [bm.verts.new((rt * s * math.cos(2 * math.pi * i / n), rt * s * math.sin(2 * math.pi * i / n), z1 * s)) for i in range(n)]
    bm.faces.new(bot[::-1]); bm.faces.new(top)
    for i in range(n):
        bm.faces.new((bot[i], bot[(i + 1) % n], top[(i + 1) % n], top[i]))
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    return bm

# ---- base: plinth, drum, neck
cyl(140, 0, 8, 'Grey1')
cyl(120, 8, 12, 'Black1')            # recessed shadow gap under the drum
cyl(125, 12, 56, 'White1')
cyl(126.5, 26, 34, 'Orange1')
for i in range(8):                    # drum accent blocks, aligned to the 45-degree panel grid
    box(126, i * 45, 40, 26, 6, 30, 'Orange1')
# neck: deliberate recessed band framed by thin orange seats
cyl(100, 56, 70, 'Black1')
cyl(104, 56, 58, 'Orange1')
cyl(104, 68, 70, 'Orange1')
cone(100, 158, 70, 88, 'Grey1')
cyl(158, 88, 91, 'Orange1')
cyl(158, 91, 100, 'White1')
# ---- top: concentric glow rings (solid emissive, collapsed UV) + grid disc
cyl(152, 100, 102, 'Glow1', solid=True)
cyl(146, 100, 103, 'Black1')
cyl(138, 100, 103.5, 'Glow1', solid=True)
cyl(134, 100, 104, 'Black1')
# grid disc: side Black1, top face Grid1 with planar 0..1 UV centred on the disc
bpy.ops.mesh.primitive_cylinder_add(vertices=SEG, radius=126 * s, depth=5 * s, location=(0, 0, 102.5 * s))
disc = bpy.context.active_object
def disc_uv(f, l):
    p = l.vert.co
    if abs(f.normal.z) > .99: return (p.x / (2 * 1.26) + .5, p.y / (2 * 1.26) + .5)
    return (0.5, 0.5)
edit_uv(disc, disc_uv)
disc.data.materials.append(M['Black1']); disc.data.materials.append(M['Grid1'])
for p in disc.data.polygons:
    if p.normal.z > .99: p.material_index = 1
fin([], disc)
# ---- 4 angled data panels
for ang in (45, 135, 225, 315):
    panel(ang)

bpy.ops.object.select_all(action='DESELECT')
for p in parts:
    p.select_set(True)
bpy.context.view_layer.objects.active = parts[0]
bpy.ops.object.join()
o = bpy.context.active_object
o.name = 'SM2_BPUnitTbl'; o.data.name = 'SM2_BPUnitTbl'
bpy.ops.object.shade_flat()

# ---- collision: UCX_ convex hulls (never rendered by UE, collision only)
hl = []
def add_hull(bm):
    h = mesh_obj('UCX_SM2_BPUnitTbl_%02d' % (len(hl) + 1), bm); hl.append(h); return h
add_hull(hull_cyl(140, 0, 12))               # plinth
add_hull(hull_cyl(127, 8, 56))               # drum
add_hull(hull_cyl(100, 56, 70))               # neck
# deck: convex hull of neck-top ring and the r158 flare/deck cylinder
bm = bmesh.new()
n = 16
ring = lambda r, z: [bm.verts.new((r * s * math.cos(2 * math.pi * i / n), r * s * math.sin(2 * math.pi * i / n), z * s)) for i in range(n)]
ring(100, 70); ring(158, 88); ring(158, 105)
bmesh.ops.convex_hull(bm, input=list(bm.verts))
bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
add_hull(bm)
for (h, r, ang, z) in hulls:                 # panel wedges
    place(h, r, ang, z)
    h.name = 'UCX_SM2_BPUnitTbl_%02d' % (len(hl) + 1); hl.append(h)
    h.data.name = h.name
for h in hl:
    h.data.materials.clear()
bpy.ops.object.select_all(action='DESELECT')
o.select_set(True)
for h in hl: h.select_set(True)
bpy.context.view_layer.objects.active = o
bpy.ops.export_scene.fbx(filepath=out, use_selection=True, object_types={'MESH'},
    apply_scale_options='FBX_SCALE_UNITS', axis_forward='-Y', axis_up='Z',
    add_leaf_bones=False, bake_anim=False, mesh_smooth_type='FACE')
print('EXPORTED', out, tuple(o.dimensions), [m.name for m in o.data.materials], 'hulls', [(h.name, len(h.data.vertices)) for h in hl])

if len(a) > 1:  # optional previews: a[1] = path prefix
    pre = a[1]
    sc = bpy.context.scene
    for h in hl: h.hide_render = True
    cols = {'Orange1': (1, .35, .02), 'White1': (.85, .85, .85), 'Grey1': (.25, .27, .3), 'Black1': (.03, .03, .04), 'Glow1': (.1, .4, 1), 'Screen1': (.1, .8, .9), 'Grid1': (.02, .08, .25)}
    for n_, m in M.items():
        m.diffuse_color = cols[n_] + (1,)
    sc.render.engine = 'BLENDER_WORKBENCH'
    sc.display.shading.color_type = 'MATERIAL'
    sc.display.shading.light = 'STUDIO'
    sc.render.resolution_x = 900; sc.render.resolution_y = 600
    cam = bpy.data.objects.new('c', bpy.data.cameras.new('c')); bpy.context.collection.objects.link(cam)
    sc.camera = cam
    def shot(name, loc, tgt, ortho=0, lens=50):
        cam.location = loc
        cam.data.type = 'ORTHO' if ortho else 'PERSP'
        cam.data.ortho_scale = ortho or 6; cam.data.lens = lens
        cam.rotation_euler = (Vector(tgt) - Vector(loc)).to_track_quat('-Z', 'Y').to_euler()
        sc.render.filepath = pre + '_' + name + '.png'
        bpy.ops.render.render(write_still=True)
    def shots(tag):
        shot(tag + 'iso', (4.2, -4.2, 2.8), (0, 0, .5))
        shot(tag + 'top', (0, 0, 8), (0, 0, 0), ortho=4.8)
        shot(tag + 'side', (0, -8, .55), (0, 0, .55), ortho=4.8)
        shot(tag + 'close', (2.3, -2.3, 1.7), (1.03, -1.03, 1.0), lens=35)
    shots('')
    # checker: 1 m uv tiles show density/stretch
    img = bpy.data.images.new('chk', 1024, 1024)
    img.generated_type = 'COLOR_GRID'
    for m in M.values():
        m.use_nodes = True
        t = m.node_tree.nodes.new('ShaderNodeTexImage'); t.image = img
        m.node_tree.nodes.active = t
    sc.display.shading.color_type = 'TEXTURE'
    shots('chk_')
    # UV layout of the 0..1 tile
    bpy.context.view_layer.objects.active = o
    bpy.ops.object.mode_set(mode='EDIT'); bpy.ops.mesh.select_all(action='SELECT')
    bpy.ops.uv.export_layout(filepath=pre + '_uv.png', size=(1024, 1024), opacity=0.5)
    bpy.ops.object.mode_set(mode='OBJECT')
    # collision wire
    for h in hl: h.hide_render = False; h.display_type = 'WIRE'
    sc.display.shading.color_type = 'MATERIAL'
    shot('hulls', (4.2, -4.2, 2.8), (0, 0, .5))
