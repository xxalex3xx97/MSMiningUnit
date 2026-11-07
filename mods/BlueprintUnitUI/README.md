# BlueprintUnitUI (Lua)
Opens WBP_BlueprintUnit on block use (post-hooks: WidgetSubsystem ToggleWindow/ShowWindow, DMSPlayerController ToggleWidget; block BPO_BlueprintUnit_C). Unit + player WBP_Inventory grids (read-only), cost rows, Import modal, Build placeholder. Lua never writes inventory. F closes the window (not while the import modal is open), ESC closes the import modal first, then the window. F9 dev cost test (3 fake rows). Log prefix [BPUnitUI].
Install: copy this folder to the game's Mods\BlueprintUnitUI (enabled.txt + Scripts\), game closed; needs WBP_BlueprintUnit from the BlueprintUnitPak build.

## Import protocol (MiningBenchMod\dlls\, same dir as MiningCmd.txt)
- Import button: modal with the JSON text box. Import (OK): text read once, written to `BlueprintImport.json` (via `BlueprintImport.json.tmp` + rename, target removed first), log `import text N bytes written`.
- Command: `MiningCmd.txt` gets ONE line, no trailing newline: `<seq>|0|bp_import <abs path to BlueprintImport.json, forward slashes>`.
- seq: strictly increasing. = max(unix seconds*1000, last seq Lua wrote + 1, seq in MiningCmd.txt + 1). Mining's own seq is separate: a same-second collision with it is possible (not handled).
- Poll every 0.5 s while waiting, 60 s timeout -> `error: timeout`. Lines from the status file are used only if they have `seq=<n>` equal to the sent seq.
- `BlueprintImport.status`: line 1 `state|text` (idle/working/done/error), then `key=value` lines: seq, blueprint, station, place, keep, remove, recipe, port, skipped, creative, removes (e.g. `removes=BP_ConveyorBeltObject:3,BP_Splitter:1`). Missing keys = 0 / empty. Done shows Txt_Status (`Blueprint: <name>` or `Ready`), Txt_Summary (line 1 text), Txt_Removes (only when remove > 0).
- `BlueprintImport.cost` (read on done): no header, lines `ITEM_Name|need|refund|net` (integers). Malformed lines skipped. Rows: net 0 hidden; net > 0 `have/netx` (red when short); net < 0 `needed 0` + green `+N refund`. Max 40 rows, sorted by name.
- Build button: placeholder only (`Build is not implemented yet (M5)`), no command. Planned M5: bp_build / bp_cancel.

Unverified: all runtime behaviour (hooks, container lookup, grid setup, teardown order, import round trip with the DLL).

- Window size: the WBP owns it (full-screen Root, centred fixed 1200x900 SizeBox). Lua viewport sizing is off (`USE_VIEWPORT_SIZING = false` in bpu_window.lua). A `[BPUnitUI] geometry ...` line is logged ~200 ms after open (desired, cached abs/local, viewport, DPI).
