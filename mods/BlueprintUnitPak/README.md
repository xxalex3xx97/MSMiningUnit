# Blueprint Unit pak (M2)
Cooks the Blueprint Unit block (BPO_BlueprintUnit / BP_BlueprintUnit) plus its table mesh and registry patch for Main Sequence. Also clones STB_BPUnitPromp ("to Use Blueprint"), renames the hologram mesh to SM_BPUnitBlueprintHolo_01 and patches its transform (HOLO_SCALE / HOLO_Z consts in clone.js).
Build order: `build_mesh.ps1` (pakchunk97 mesh), then `build_pak.ps1` (pakchunk100 blocks, runs clone.js + patch_registry.py 3 B + patch_research.py).
Outputs in `out\`: pakchunk97-BPUnitMesh_P.{pak,ucas,utoc} and pakchunk100-BlueprintUnit_P.{pak,ucas,utoc}.
Blender: `mk_table.py` is a bpy script that generates `art\SM2_BPUnitTbl.fbx` (run it inside Blender; Blender exe path not recorded here).
Not installed by the build: copy the pak files into the game's Paks folder yourself.
Co-op: every player needs the same pak. Chunk 97 is also used by the dev MSTestCube pak: never install both.
Tools: MS_MODS_ROOT (default F:\MSDemoMods) for legacy/ source and tools\retoc.

## S1 hidden container default machine (step 4+5 done, not installed, not in game)
- clone.js Job D: Machines\BP_ContainerObject_2 -> BP_BPUnitStoreObj_01 (sim), Machines\BP_Container_2 -> BP_BPUnitStore (actor). Parent BP_ContainerObject untouched.
- Sim uexp int32 patch: InventorySize @120 32->120, StackSizeBonusMultiplier @124 10->200 (from diff_container.py: large=32/10, normal=16 at 120, normal has no bonus value).
- Sizes differ: normal uexp 322 B, large uexp 454 B (not same-length). Large uexp values stored as plain LE int32.
- DefaultMachines decoded: uexp 374 count (1->2), element 378..403 (26 B), next int32 20 @404. patch_bpo_machine.py inserts element 2 (22 B) at uexp 404: class ref -35 (new import 34 BP_BPUnitStoreObj_01_C), X=Y=0 (tune in-game), Height/Rot omitted. Header: +2 names, +2 imports (package 33, class 34), +1 preload (export 1 CreateBefore block, same as connector). Run after clone.js + registry + research, before to-zen. `python -I patch_bpo_machine.py --walk <base>` prints the element walk.

## Window fix (vanilla suppress)
- clone.js Job E: BP_BlueprintUnit.uasset import WBP_StationInventory -> WBP_BPUnitNoWindow00 (same length, ascii+utf16; 3 hits: package path, WBP_StationInventory_C, Default__). Same trick as Mining Unit WBP_Workbench -> WBP_MiningUnt. Lua also closes any live WBP_StationInventory_C 100 ms after open (main.lua closeVanilla).
