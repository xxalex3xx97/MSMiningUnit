# Blueprint Unit pak (M2)
Cooks the Blueprint Unit block (BPO_BlueprintUnit / BP_BlueprintUnit) plus its table mesh and registry patch for Main Sequence. Also clones STB_BPUnitPromp ("to Use Blueprint"), renames the hologram mesh to SM_BPUnitBlueprintHolo_01 and patches its transform (HOLO_SCALE / HOLO_Z consts in clone.js).
Build order: `build_mesh.ps1` (pakchunk97 mesh), then `build_pak.ps1` (pakchunk100 blocks, runs clone.js + patch_registry.py 3 B + patch_research.py).
Outputs in `out\`: pakchunk97-BPUnitMesh_P.{pak,ucas,utoc} and pakchunk100-BlueprintUnit_P.{pak,ucas,utoc}.
Blender: `mk_table.py` is a bpy script that generates `art\SM2_BPUnitTbl.fbx` (run it inside Blender; Blender exe path not recorded here).
Not installed by the build: copy the pak files into the game's Paks folder yourself.
Co-op: every player needs the same pak. Chunk 97 is also used by the dev MSTestCube pak: never install both.
Tools: MS_MODS_ROOT (default F:\MSDemoMods) for legacy/ source and tools\retoc.

## S1 hidden container default machine (build variants)
- `build_pak.ps1 -Container none|vanilla|clone` (default none). Output `out\<variant>\pakchunk100-BlueprintUnit_P.{pak,ucas,utoc}`, last built also copied to `out\`.
  - none: 5 packages, no container.
  - vanilla: 5 packages, container = vanilla `BP_ContainerObject_2` (32 slots, bonus 10). No Machines clone. Try this first.
  - clone: 6 packages (+ `Machines/BP_BPUnitStoreObj_01`), container = clone sim (120 slots, bonus 200).
- Container = DefaultMachines entry. It is NOT the connector array: vanilla uexp 374 is the CONNECTOR array (`BPO_LargeConnector_C`, count 1, end 404), left untouched.
  DefaultMachines is a separate property, unversioned-header index 24, absent in BPO_CommandModule. `patch_bpo_machine.py <DIR> <CLASS PKG> <CLASS NAME>`:
  header uexp 116-117 `07 02` -> `02 02 04 02` (+2 bytes, idx 24 added), 14-byte value `01 00 00 00 | 80 09 0c | class ref | 80 07 07` inserted before idx 29 value (`ff ff ff ff`).
  Export 1 SerialSize +16. Connector array moves 374 -> 376 (its end 404 -> 406) after the header growth; verify() checks it there.
  Machine cell (0,0), no payload. Real placement later: `80 07 06` + X,Y int32, machine coords ~8x finer than block cells.
- Names/imports/preload: +2 names, +2 imports (package + BlueprintGeneratedClass), +1 CreateBefore preload entry (export 1, same as connector).
- `python -I patch_bpo_machine.py --walk <base>` prints the CONNECTOR walk only.
- clone.js Job D (gated on BPU_CONTAINER=1): sim BP_ContainerObject_2 -> BP_BPUnitStoreObj_01 only, InventorySize @120 32->120, bonus @124 10->200.
  Actor is NOT cloned/renamed. Sim imports actor `BP_Container_2_C` from package `/Game/Blueprints/Machines/BP_Container` (vanilla quirk, package exists in legacy).
  Earlier renaming of the actor (BP_Container_2 -> BP_BPUnitStore) also rewrote that import name to `BP_BPUnitStore_C` and left it dangling against package BP_Container (inferred from the import dump, not run in game).
- Previous crash cause: container appended to the CONNECTOR array (debugger finding). Not yet tested in game.

## Window fix (vanilla suppress)
- clone.js Job E: BP_BlueprintUnit.uasset import WBP_StationInventory -> WBP_BPUnitNoWindow00 (same length, ascii+utf16; 3 hits: package path, WBP_StationInventory_C, Default__). Same trick as Mining Unit WBP_Workbench -> WBP_MiningUnt. Lua also closes any live WBP_StationInventory_C 100 ms after open (main.lua closeVanilla).
