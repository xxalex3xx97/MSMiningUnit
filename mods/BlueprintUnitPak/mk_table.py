import bpy, sys
a = sys.argv[sys.argv.index('--') + 1:]
out = a[0]
ox, oy, oz, ex, ey, ez = [float(v) for v in a[1:7]]
s = 0.01  # cm -> Blender m
bpy.ops.wm.read_factory_settings(use_empty=True)

def box(cx, cy, cz, sx, sy, sz):
    bpy.ops.mesh.primitive_cube_add(size=1, location=(cx * s, cy * s, cz * s))
    o = bpy.context.active_object
    o.scale = (sx * s, sy * s, sz * s)
    bpy.ops.object.transform_apply(scale=True)
    return o

parts = [box(ox, oy, oz + ez - 4, 2 * ex, 2 * ey, 8)]  # top slab
h = 2 * ez - 8
for dx in (-1, 1):
    for dy in (-1, 1):
        parts.append(box(ox + dx * (ex - 8), oy + dy * (ey - 8), oz - 4, 8, 8, h))  # legs

bpy.ops.object.select_all(action='DESELECT')
for p in parts:
    p.select_set(True)
bpy.context.view_layer.objects.active = parts[0]
bpy.ops.object.join()
o = bpy.context.active_object
o.name = 'SM2_BPUnitTbl'
o.data.materials.append(bpy.data.materials.new('Colour'))
bpy.ops.object.select_all(action='DESELECT')
o.select_set(True)
bpy.context.view_layer.objects.active = o
bpy.ops.export_scene.fbx(filepath=out, use_selection=True, object_types={'MESH'},
    apply_scale_options='FBX_SCALE_UNITS', axis_forward='-Y', axis_up='Z',
    add_leaf_bones=False, bake_anim=False, mesh_smooth_type='FACE')
print('EXPORTED', out, tuple(o.dimensions))
