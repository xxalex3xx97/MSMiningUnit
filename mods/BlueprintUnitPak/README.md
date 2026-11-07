# Blueprint Unit pak (M2)
Cooks the Blueprint Unit block (BPO_BlueprintUnit / BP_BlueprintUnit) plus its table mesh and registry patch for Main Sequence. Also clones STB_BPUnitPromp ("to Use Blueprint"), renames the hologram mesh to SM_BPUnitBlueprintHolo_01 and patches its transform (HOLO_SCALE / HOLO_Z consts in clone.js).
Build order: `build_mesh.ps1` (pakchunk97 mesh), then `build_pak.ps1` (pakchunk100 blocks, runs clone.js + patch_registry.py 3 B + patch_research.py).
Outputs in `out\`: pakchunk97-BPUnitMesh_P.{pak,ucas,utoc} and pakchunk100-BlueprintUnit_P.{pak,ucas,utoc}.
Blender: `mk_table.py` is a bpy script that generates `art\SM2_BPUnitTbl.fbx` (run it inside Blender; Blender exe path not recorded here).
Not installed by the build: copy the pak files into the game's Paks folder yourself.
Co-op: every player needs the same pak. Chunk 97 is also used by the dev MSTestCube pak: never install both.
Tools: MS_MODS_ROOT (default F:\MSDemoMods) for legacy/ source and tools\retoc.
