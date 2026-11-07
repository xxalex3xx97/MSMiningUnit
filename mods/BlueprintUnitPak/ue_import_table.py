import os, traceback, unreal
ART = "F:/ms-bpunit/mods/BlueprintUnitPak/art/"
LOG = os.path.join(os.environ.get("TEMP", "."), "bpunit_import.log")
# stand-ins at the exact game paths (never packed): name -> (dir, class)
MI, MAT = "mic", "mat"
STAND = {
    "MI_PaintedMetal_Grey": ("/Game/Materials/Instances", MI), "MI_PaintedMetal_White": ("/Game/Materials/Instances", MI),
    "MI_PaintedMetal_Orange": ("/Game/Materials/Instances", MI), "MI_PaintedMetal_Black": ("/Game/Materials/Instances", MI),
    "MI_Screens_01": ("/Game/Art/MaterialLibrary", MI),
    "M_Holotable": ("/Game/Materials", MAT), "M_NewStickers": ("/Game/Materials", MAT), "M_Hologram": ("/Game/Materials", MAT),
}
# meshes: name -> (dest dir, {slot name: material asset path})
MESHES = {
    "SM2_BPUnitTbl": ("/Game/Meshes/Machines", {
        "Orange1": "/Game/Materials/Instances/MI_PaintedMetal_Orange", "White1": "/Game/Materials/Instances/MI_PaintedMetal_White",
        "Grey1": "/Game/Materials/Instances/MI_PaintedMetal_Grey", "Black1": "/Game/Materials/Instances/MI_PaintedMetal_Black",
        "Glow1": "/Game/Materials/M_Holotable", "Screen1": "/Game/Art/MaterialLibrary/MI_Screens_01",
        "Grid1": "/Game/Materials/M_BPUnitGrid"}),
    "SM_BPUnitBlueprintHolo_01": ("/Game/Meshes/Effects", {"Holo1": "/Game/Materials/M_Hologram"}),
}
L = []
def log(m):
    L.append(str(m)); unreal.log("[bpunit] " + str(m))
def sp(o, k, v):
    try: o.set_editor_property(k, v)
    except Exception as e: log("  set %s failed: %r" % (k, e))
def mk_grid():
    # our own shipped material: blueprint grid on the 0..1 disc UV, unlit emissive, opaque
    p = "/Game/Materials/M_BPUnitGrid"
    if eal.does_asset_exist(p): eal.delete_asset(p)
    m = at.create_asset("M_BPUnitGrid", "/Game/Materials", unreal.Material, unreal.MaterialFactoryNew())
    ml = unreal.MaterialEditingLibrary
    m.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    m.set_editor_property("blend_mode", unreal.BlendMode.BLEND_OPAQUE)
    m.set_editor_property("two_sided", True)
    X = [0]
    def n(cls, **kw):
        e = ml.create_material_expression(m, cls, X[0], 0); X[0] += 200
        for k, v in kw.items(): e.set_editor_property(k, v)
        return e
    def c(a, o, b, i): ml.connect_material_expressions(a, o, b, i)
    E = unreal
    uv = n(E.MaterialExpressionTextureCoordinate)
    def mul(a, k):
        e = n(E.MaterialExpressionMultiply, const_b=k); c(a, "", e, "A"); return e
    def sub(a, k):
        e = n(E.MaterialExpressionSubtract, const_b=k); c(a, "", e, "A"); return e
    def mask(a, r, g):
        e = n(E.MaterialExpressionComponentMask, r=r, g=g, b=False, a=False); c(a, "", e, ""); return e
    def mx(a, b):
        e = n(E.MaterialExpressionMax); c(a, "", e, "A"); c(b, "", e, "B"); return e
    def sat(a):
        e = n(E.MaterialExpressionSaturate); c(a, "", e, ""); return e
    def grid(cells, w):
        # distance of each cell centre axis: abs(frac(uv*cells)-0.5); line where max(dx,dy) > 0.5-w
        f = n(E.MaterialExpressionFrac); c(mul(uv, cells), "", f, "")
        d = n(E.MaterialExpressionAbs); c(sub(f, 0.5), "", d, "")
        e = mx(mask(d, True, False), mask(d, False, True))
        return sat(mul(sub(e, 0.5 - w), 1.0 / (w * 0.5)))
    minor = mul(grid(20, 0.012), 0.45)
    major = grid(4, 0.02)
    ctr = n(E.MaterialExpressionConstant2Vector, r=0.5, g=0.5)
    dist = n(E.MaterialExpressionDistance); c(uv, "", dist, "A"); c(ctr, "", dist, "B")
    border = sat(mul(sub(dist, 0.47), 1.0 / 0.012))      # strong outer border circle, stays inside r=0.5
    inside = n(E.MaterialExpressionOneMinus); c(sat(mul(sub(dist, 0.5), 400.0)), "", inside, "")  # circular mask
    line = mul(mx(mx(minor, major), border), 1.0)
    lit = n(E.MaterialExpressionMultiply); c(line, "", lit, "A"); c(inside, "", lit, "B")
    bg = n(E.MaterialExpressionConstant3Vector, constant=unreal.LinearColor(0.0, 0.015, 0.06, 1))
    fg = n(E.MaterialExpressionConstant3Vector, constant=unreal.LinearColor(0.25, 0.75, 2.0, 1))
    lp = n(E.MaterialExpressionLinearInterpolate); c(bg, "", lp, "A"); c(fg, "", lp, "B"); c(lit, "", lp, "Alpha")
    ml.connect_material_property(lp, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    ml.recompile_material(m)
    eal.save_asset(p)
    log("created " + p)

def main():
    global eal, at
    # Interchange ignores FbxImportUI options and skipped our UCX_ hulls: force the legacy FBX importer
    unreal.SystemLibrary.execute_console_command(None, "Interchange.FeatureFlags.Import.FBX 0")
    eal = unreal.EditorAssetLibrary
    at = unreal.AssetToolsHelpers.get_asset_tools()
    for n, (d, k) in STAND.items():
        p = d + "/" + n
        if eal.does_asset_exist(p): continue
        if k == MAT:
            at.create_asset(n, d, unreal.Material, unreal.MaterialFactoryNew())
        else:
            a = at.create_asset(n, d, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
            a.set_editor_property("parent", eal.load_asset("/Game/Materials/MM_Interior"))
        eal.save_asset(p)
        log("stand-in " + p)
    for name, (dest, _) in MESHES.items():
        p = dest + "/" + name
        # ponytail: import_asset_tasks for a static mesh saves the package, then asserts in Slate (commandlet has no
        # SlateApplication). Import ONE mesh per run: return after it, rerun the script for the next, then post-process.
        if not eal.does_asset_exist(p):
            t = unreal.AssetImportTask()
            sp(t, "filename", ART + name + ".fbx"); sp(t, "destination_path", dest); sp(t, "destination_name", name)
            sp(t, "automated", True); sp(t, "save", True); sp(t, "replace_existing", True)
            o = unreal.FbxImportUI()
            sp(o, "import_mesh", True); sp(o, "import_textures", False); sp(o, "import_materials", False)
            sp(o, "import_as_skeletal", False)
            d = o.static_mesh_import_data
            sp(d, "build_nanite", False); sp(d, "combine_meshes", True); sp(d, "convert_scene_unit", True)
            sp(d, "generate_lightmap_u_vs", False); sp(d, "auto_generate_collision", name != "SM2_BPUnitTbl")  # table ships UCX_ hulls
            sp(t, "options", o)
            at.import_asset_tasks([t])
            log("imported %s (crash expected next; rerun to continue)" % p)
            return
    mk_grid()
    for name, (dest, slotmap) in MESHES.items():
        p = dest + "/" + name
        sm = eal.load_asset(p)
        if sm is None: raise RuntimeError("no asset " + p)
        for i, st in enumerate(sm.static_materials):
            sn = str(st.material_slot_name)
            if sn in slotmap: sm.set_material(i, eal.load_asset(slotmap[sn]))
            else: log("  UNMAPPED slot " + sn)
        eal.save_loaded_asset(sm)
        e = sm.get_bounds().box_extent; org = sm.get_bounds().origin
        log("%s slots=%s origin=(%s,%s,%s) extent=(%s,%s,%s)" % (p, [str(s.material_slot_name) for s in sm.static_materials], org.x, org.y, org.z, e.x, e.y, e.z))
        try:
            g = sm.get_editor_property("body_setup").get_editor_property("agg_geom")
            cv = g.get_editor_property("convex_elems")
            log("  collision: %d convex elems, box=%d, trace_flag=%s, EditorStaticMeshLibrary convex count=%s" % (len(cv), len(g.get_editor_property("box_elems")), sm.get_editor_property("body_setup").get_editor_property("collision_trace_flag"), unreal.EditorStaticMeshLibrary.get_convex_collision_count(sm)))
        except Exception as ex: log("  collision read failed %r" % ex)
        log("  mats=" + str([s.material_interface.get_path_name() for s in sm.static_materials]))
    log("POSTDONE")
try: main()
except Exception as e:
    log("FAILED: %r" % e); log(traceback.format_exc())
finally:
    open(LOG, "w", encoding="utf-8").write("\n".join(L) + "\n")
