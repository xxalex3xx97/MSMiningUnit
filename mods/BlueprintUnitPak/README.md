# Blueprint Unit pak (M2)
Cooks the Blueprint Unit block (BPO_BlueprintUnit / BP_BlueprintUnit) plus its table mesh and registry patch for Main Sequence. Also clones STB_BPUnitPromp ("to Use Blueprint"), renames the hologram mesh to SM_BPUnitBlueprintHolo_01 and patches its transform (HOLO_SCALE / HOLO_Z consts in clone.js).
Build order: `build_mesh.ps1` (pakchunk97 mesh), then `build_pak.ps1` (pakchunk100 blocks, runs clone.js + patch_registry.py 3 B + patch_research.py).
Outputs in `out\`: pakchunk97-BPUnitMesh_P.{pak,ucas,utoc} and pakchunk100-BlueprintUnit_P.{pak,ucas,utoc}.
Blender: `mk_table.py` is a bpy script that generates `art\SM2_BPUnitTbl.fbx` (run it inside Blender; Blender exe path not recorded here).
Not installed by the build: copy the pak files into the game's Paks folder yourself.
Co-op: every player needs the same pak. Chunk 97 is also used by the dev MSTestCube pak: never install both.
Tools: MS_MODS_ROOT (default F:\MSDemoMods) for legacy/ source and tools\retoc.

## S1 hidden container default machine (in progress)
- clone.js Job D: Machines\BP_ContainerObject_2 -> BP_BPUnitStoreObj_01 (sim), Machines\BP_Container_2 -> BP_BPUnitStore (actor). Parent BP_ContainerObject untouched.
- Sim uexp int32 patch: InventorySize @120 32->120, StackSizeBonusMultiplier @124 10->200 (from diff_container.py: large=32/10, normal=16 at 120, normal has no bonus value).
- Sizes differ: normal uexp 322 B, large uexp 454 B (not same-length). Large uexp values stored as plain LE int32.
- dump_bpo_machines.py: BPO_CommandModule export stride 96 (chain check), 2 exports (CDO export 1 @2990 SerialSize 1203 @3254). Only one ref -2 (BPO_LargeConnector_C class import, import index 1) in uexp @388, no count found next to it. DefaultMachines element layout NOT yet decoded: patch_bpo_machine.py blocked (NEEDS-THINKING).
