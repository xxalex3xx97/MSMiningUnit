import os, traceback, unreal
FBX = "F:/ms-bpunit/mods/BlueprintUnitPak/art/SM2_BPUnitTbl.fbx"
DEST = "/Game/Meshes/Machines"
MATD, MATN = "/Game/Materials", "M_BasicEmissive"
LOG = os.path.join(os.environ.get("TEMP", "."), "bpunit_import.log")
L = []
def log(m):
    L.append(str(m)); unreal.log("[bpunit] " + str(m))
def sp(o, k, v):
    try: o.set_editor_property(k, v)
    except Exception as e: log("  set %s failed: %r" % (k, e))
def main():
    eal = unreal.EditorAssetLibrary
    at = unreal.AssetToolsHelpers.get_asset_tools()
    mp = MATD + "/" + MATN
    if not eal.does_asset_exist(mp):
        at.create_asset(MATN, MATD, unreal.Material, unreal.MaterialFactoryNew())
        eal.save_asset(mp)
    mat = eal.load_asset(mp)
    if mat is None: raise RuntimeError("stand-in material missing")
    for name in ("SM2_BPUnitTbl",):
        p = DEST + "/" + name
        # ponytail: import_asset_tasks for a static mesh saves the package, then asserts in Slate (commandlet has no
        # SlateApplication). Import ONCE per run: skip when present, rerun the script to import the next mesh, then post-process.
        if not eal.does_asset_exist(p):
            t = unreal.AssetImportTask()
            sp(t, "filename", FBX); sp(t, "destination_path", DEST); sp(t, "destination_name", name)
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
            continue
        sm = eal.load_asset(p)
        if sm is None: raise RuntimeError("no asset " + p)
        sm.set_material(0, mat)
        eal.save_loaded_asset(sm)
        ext = sm.get_bounds().box_extent
        slots = [str(s.material_slot_name) for s in sm.static_materials]
        log("%s slots=%s extent=(%s,%s,%s)" % (p, slots, ext.x, ext.y, ext.z))
try: main()
except Exception as e:
    log("FAILED: %r" % e); log(traceback.format_exc())
finally:
    open(LOG, "w", encoding="utf-8").write("\n".join(L) + "\n")
