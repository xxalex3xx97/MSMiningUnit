import bpy, sys, math
from mathutils import Vector
a = sys.argv[sys.argv.index('--') + 1:]
out = a[0]
s = 0.01  # cm -> Blender m
bpy.ops.wm.read_factory_settings(use_empty=True)
SEG = 48
M = {n: bpy.data.materials.new(n) for n in ('Orange1', 'White1', 'Grey1', 'Black1', 'Glow1', 'Screen1')}
parts = []

def fin(mat, rotz=0):
    o = bpy.context.active_object
    bpy.ops.object.transform_apply(location=False, scale=True)
    o.data.materials.append(M[mat])
    parts.append(o)
    return o

def cyl(r, z0, z1, mat):
    bpy.ops.mesh.primitive_cylinder_add(vertices=SEG, radius=r * s, depth=(z1 - z0) * s, location=(0, 0, (z0 + z1) / 2 * s))
    return fin(mat)

def cone(r0, r1, z0, z1, mat):
    bpy.ops.mesh.primitive_cone_add(vertices=SEG, radius1=r0 * s, radius2=r1 * s, depth=(z1 - z0) * s, location=(0, 0, (z0 + z1) / 2 * s))
    return fin(mat)

def box(r, ang, z, sx, sy, sz, mat, tilt=0):
    # sx tangent, sy radial, sz up; placed at radius r, angle ang (deg); tilt = pitch about tangent (deg)
    t = math.radians(ang)
    bpy.ops.mesh.primitive_cube_add(size=1, location=(r * math.cos(t) * s, r * math.sin(t) * s, z * s))
    o = bpy.context.active_object
    o.scale = (sx * s, sy * s, sz * s)
    bpy.ops.object.transform_apply(location=False, scale=True)
    # local X = tangent after rotating by ang+90 about Z; local Y = radial
    o.rotation_euler = (math.radians(tilt), 0, 0)
    bpy.ops.object.transform_apply(location=False, rotation=True)
    o.rotation_euler = (0, 0, t + math.pi / 2)
    bpy.ops.object.transform_apply(location=False, rotation=True)
    return fin(mat)

# base drum
cyl(140, 0, 8, 'Grey1')
cyl(125, 8, 56, 'White1')
cyl(126.5, 26, 34, 'Orange1')
cyl(127, 8, 12, 'Black1')
for i in range(8):
    box(126, i * 45 + 22.5, 40, 26, 6, 30, 'Orange1')
# neck + flared deck
cyl(100, 56, 70, 'Black1')
cone(100, 158, 70, 88, 'Grey1')
cyl(158, 88, 91, 'Orange1')
cyl(158, 91, 100, 'White1')
# top: concentric glow rings + disc
cyl(152, 100, 102, 'Glow1')
cyl(146, 100, 103, 'Black1')
cyl(138, 100, 103.5, 'Glow1')
cyl(134, 100, 104, 'Black1')
cyl(126, 100, 105, 'Glow1')
# angled data panels on the rim + orange accents
for ang in (45, 135, 225, 315):
    box(146, ang, 104, 64, 16, 8, 'Grey1')
    box(146, ang, 108.5, 56, 14, 2, 'Screen1', tilt=-25)
    box(146, ang + 20, 104, 8, 8, 8, 'Orange1')
    box(146, ang - 20, 104, 8, 8, 8, 'Orange1')

bpy.ops.object.select_all(action='DESELECT')
for p in parts:
    p.select_set(True)
bpy.context.view_layer.objects.active = parts[0]
bpy.ops.object.join()
o = bpy.context.active_object
o.name = 'SM2_BPUnitTbl'
bpy.ops.object.shade_flat()
bpy.ops.object.select_all(action='DESELECT')
o.select_set(True)
bpy.context.view_layer.objects.active = o
bpy.ops.export_scene.fbx(filepath=out, use_selection=True, object_types={'MESH'},
    apply_scale_options='FBX_SCALE_UNITS', axis_forward='-Y', axis_up='Z',
    add_leaf_bones=False, bake_anim=False, mesh_smooth_type='FACE')
print('EXPORTED', out, tuple(o.dimensions), [m.name for m in o.data.materials])
if len(a) > 1:  # optional preview render: a[1]=png
    import mathutils
    cols = {'Orange1': (1, .35, .02), 'White1': (.85, .85, .85), 'Grey1': (.25, .27, .3), 'Black1': (.03, .03, .04), 'Glow1': (.1, .4, 1), 'Screen1': (.1, .8, .9)}
    for n, m in M.items():
        m.diffuse_color = cols[n] + (1,)
    bpy.context.scene.render.engine = 'BLENDER_WORKBENCH'
    bpy.context.scene.display.shading.color_type = 'MATERIAL'
    bpy.context.scene.render.resolution_x = 900; bpy.context.scene.render.resolution_y = 600
    cam = bpy.data.objects.new('c', bpy.data.cameras.new('c')); bpy.context.collection.objects.link(cam)
    cam.location = (4.2, -4.2, 2.8)
    cam.rotation_euler = (Vector((0, 0, .5)) - cam.location).to_track_quat('-Z', 'Y').to_euler()
    bpy.context.scene.camera = cam
    bpy.context.scene.render.filepath = a[1]
    bpy.ops.render.render(write_still=True)
