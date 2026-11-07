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
        "Glow1": "/Game/Materials/M_Holotable", "Screen1": "/Game/Art/MaterialLibrary/MI_Screens_01"}),
    "SM_BPUnitBlueprintHolo_01": ("/Game/Meshes/Effects", {"Holo1": "/Game/Materials/M_Hologram"}),
}
L = []
def log(m):
    L.append(str(m)); unreal.log("[bpunit] " + str(m))
def sp(o, k, v):
    try: o.set_editor_property(k, v)
    except Exception as e: log("  set %s failed: %r" % (k, e))
def main():
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
            sp(d, "build_nanite", False); sp(d, "combine_meshes", True)
            sp(d, "generate_lightmap_u_vs", False); sp(d, "auto_generate_collision", True)
            sp(t, "options", o)
            at.import_asset_tasks([t])
            log("imported %s (crash expected next; rerun to continue)" % p)
            return
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
        log("  mats=" + str([s.material_interface.get_path_name() for s in sm.static_materials]))
    log("POSTDONE")
try: main()
except Exception as e:
    log("FAILED: %r" % e); log(traceback.format_exc())
finally:
    open(LOG, "w", encoding="utf-8").write("\n".join(L) + "\n")
