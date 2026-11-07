# BlueprintUnitUI (Lua, slice S3)
Opens WBP_BlueprintUnit on block use (post-hooks: WidgetSubsystem ToggleWindow/ShowWindow, DMSPlayerController ToggleWidget; block BPO_BlueprintUnit_C). Unit + player WBP_Inventory grids, cost rows, placeholder Import/Build. Read-only: Lua never writes inventory. F / ESC close, F9 dev cost test (3 fake rows). Log prefix [BPUnitUI]; probes P1, P2, P3, P5.
Install (not done by this slice): copy this folder to the game's Mods\BlueprintUnitUI (enabled.txt + Scripts\), game closed; needs WBP_BlueprintUnit from the BlueprintUnitPak build.
Planned M4/M5 (not implemented): MiningCmd.txt in MiningBenchMod/dlls, one line '<seq>|<id>|<cmd>' (seq strictly increasing), cmds bp_import clip / bp_build / bp_cancel.
Planned status: BPUStatus.txt 'state|text', BPUCost.txt 'ITEM_x|need' per line; Lua polls ~0.5 s while open.
Unverified: all runtime behaviour (hooks, container lookup, grid setup, teardown order).
