# BlueprintUnitUI (Lua)
Opens WBP_BlueprintUnit on block use (post-hooks: WidgetSubsystem ToggleWindow/ShowWindow, DMSPlayerController ToggleWidget; block BPO_BlueprintUnit_C). Unit + player WBP_Inventory grids (read-only), cost rows, Import modal, Build (bp_build). Lua never writes inventory. F closes the window (not while the import modal is open), ESC closes the import modal first, then the window. F9 dev cost test (3 fake rows). Log prefix [BPUnitUI].
- Cost rows use the game's own `WBP_ItemAmount` (in `Scr_Cost`, ScrollBox cleared each import). Need rows first, then refund rows (green `+N refund`). Own `Row_Cost_*` only as fallback if the game row fails. Header `Txt_CostHdr` is set by the designer (Lua logs a mismatch only).
- Crate hide: our container = sim `BP_BPUnitStoreObj_01_C` (ParentID = unit block/frame ID). Its actor = `BP_Container_2_C` whose `BPC_MachineInteraction.MachineObject` has the same ID. On open and every 3 s world scan: actor hidden, no collision, interaction off/hidden. Ambiguous link -> `crate link unknown`, nothing hidden.
- Dev keys: F7 `probeCost` (one WBP_ItemAmount row, step log), F8 crate/link dump (`F8 sim/actor/unit`).
Install: copy this folder to the game's Mods\BlueprintUnitUI (enabled.txt + Scripts\), game closed. Needs its own pak `pakchunk102-BPUnitUI_P` (`/Game/BlueprintUnitUI/WBP_BlueprintUnit` + `Art/` header textures): build with `ue\MiningUnitUI\scriptspunit_cook.ps1`, then `bpunit_pack.ps1`, copy the 3 files to Paks by hand (install.ps1 is pak98-only). The Lua class path is `/Game/BlueprintUnitUI/...`, so Lua and pak102 go together. Mining Unit pak98 no longer contains WBP_BlueprintUnit: no pak98 rebuild for Blueprint Unit changes. Old pak98 on the game still holding `/Game/MiningUnitUI/WBP_BlueprintUnit` is harmless (other path, unused by this Lua).

## Import protocol (MiningBenchMod\dlls\, same dir as MiningCmd.txt)
- Import button: modal with the JSON text box. Import (OK): text read once, written to `BlueprintImport.json` (via `BlueprintImport.json.tmp` + rename, target removed first), log `import text N bytes written`.
- Command: `MiningCmd.txt` gets ONE line, no trailing newline: `<seq>|0|bp_import <abs path to BlueprintImport.json, forward slashes>`.
- seq: must be below 1e8, the DLL keeps only the last 8 digits of seq (a 13-digit ms seq came back as `seq=17948000` and never matched). Strictly increasing. = max(unix seconds % 1e8, last seq Lua wrote + 1, (seq in MiningCmd.txt % 1e8) + 1). Logs `[BPUnitImport] clock seq X <= MiningCmd.txt seq Y` when the clock had to be bumped (DLL would ignore it), and `seq >= 1e8`. Mining's own seq is separate: a same-second collision with it is possible (not handled).
- Poll every 0.5 s while waiting, 60 s timeout -> `error: timeout`. Lines from the status file are used only if `seq=<n>` equals the sent seq modulo 1e8. A status with another seq is logged once per value: `[BPUnitImport] status seq X != import_seq Y (ignored)`.
- Parse helpers are pure (`BPU._parse`: status, cost, removes, seqMatch). Self-check `tests/test_parse.lua` (stock Lua 5.4, not auto-run; no Lua interpreter on this machine, only luac -p was run).
- `BlueprintImport.status`: line 1 `state|text` (idle/working/done/error), then `key=value` lines: seq, blueprint, station, place, keep, remove, recipe, port, skipped, creative, removes (e.g. `removes=BP_ConveyorBeltObject:3,BP_Splitter:1`). Missing keys = 0 / empty. Done shows Txt_Status (`Blueprint: <name>` or `Ready`), Txt_Summary (line 1 text), Txt_Removes (only when remove > 0).
- `BlueprintImport.cost` (read on done): no header, lines `ITEM_Name|need|refund|net` (integers). Malformed lines skipped. Rows: net 0 hidden; net > 0 `have/netx` (red when short); net < 0 `needed 0` + green `+N refund`. Max 40 rows, sorted by name.
## Build protocol (MiningBenchMod\dlls\)
- Build button: needs a finished import in this session (else `import first`). Command: `MiningCmd.txt` ONE line `<seq>|0|bp_build` or `<seq>|0|bp_build unit <sim ID>` (sim ID = the one BP_BPUnitStoreObj_01_C whose ParentID is the opened block or its frame; none or more than one = no unit arg). seq as for import.
- Status: `BlueprintBuild.status`: line 1 `state|text` (working/done/error), then `key=value` lines: seq, stage, placed, kept, removed, recipe, moved_out, moved_in, chunk=k/n, verify=P/N. Only lines with `seq` equal to the sent seq (mod 1e8) are used; others logged once.
- Poll 0.5 s, 330 s timeout -> `error: timeout`. Working: Txt_Status = text + ` (chunk k/n)`. Build and Import buttons disabled while working; Import modal cannot open meanwhile.
- Done/error: Txt_Status = text (error prefixed), buttons re-enabled, then the import is re-run (`bp_import`, same path) so the cost list refreshes; the build line goes above the import summary in Txt_Summary.
- Closing the window stops the build poll; the DLL keeps running (next open shows nothing until a new command).

## Station Build flag (placeholder, DLL ignores it)
- Optional `Chk_StationBuild` (CheckBox, designer-authored; missing = no error). Read at Import OK / Build click under pcall, kept in `S.stationBuild`.
- Checked: `bp_import` (also the cost re-import after a build) and `bp_build` get the trailing token ` blocks 1` (e.g. `<seq>|0|bp_build unit <id> blocks 1`). Unchecked: nothing appended.
- DLL: stores the flag, does not act on it. Status text gets the suffix ` | station blocks: not implemented yet` and key `blocks=ignored`. Optional `Txt_StationBuildHint` shows the same sentence while checked (set on click).
- Teleporter keys (DLL): `locked=Teleporter:N` in BlueprintImport.status (planned Teleporters) and BlueprintBuild.status (missing at verify). Text `not unlocked: Teleporter xN` (import) / `not placed (probably not unlocked): Teleporter xN` (build). Not a research read: the lock is a guess.

## Station Build (planned)
Guard rules for the real implementation (not built; the flag above is only a placeholder):
- Never remove or replace: the block the player stands in, the Dock, Command Module or Blueprint Unit blocks, or any block that connects the player's position to the dock.
- Refuse any plan whose block removals would disconnect the player's block or the dock from the rest of the station graph.
- Any plan that touches blocks needs a connectivity check on the block adjacency graph before the first removal.
- Blocks are paid via PendingBlueprintInput (frame inventory + player inventory), not BuildInput.

Unverified: all runtime behaviour (hooks, container lookup, grid setup, teardown order, import round trip with the DLL).

- Window size: the WBP owns it (full-screen Root, centred fixed 1200x900 SizeBox). Lua viewport sizing is off (`USE_VIEWPORT_SIZING = false` in bpu_window.lua). A `[BPUnitUI] geometry ...` line is logged ~200 ms after open (desired, cached abs/local, viewport, DPI).
