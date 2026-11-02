# Editor steps (UE 5.7.4, UE 5.7 install at <UE 5.7 dir>, Build.version 5.7.4)

Checklist. Game must be closed for steps 8-9. Backups first (install.ps1 does this).

1. Check engine: `<UE 5.7 dir>\Engine\Build\Build.version` says 5.7.4. Do not use UE 5.8 (game is 5.7).
2. Open `MiningUnitUI.uproject` in UE 5.7.4 (it may ask to rebuild the Blueprint-only project: accept, no C++ needed). If it asks to convert or upgrade, stop and report.
3. Content Browser: `Content\MiningUnitUI` (already present, README there). Create a new **Widget Blueprint**, parent class **User Widget**, name `WBP_MiningUnit`.
4. Build the layout exactly per `SPEC.md` (names, Is Variable on every named widget, hierarchy). Use the look from SPEC (dark panel, gold active tab). No size or centre settings in the WBP.
5. Compile, Save All (Ctrl+S). Close the editor.
6. `scripts\cook.ps1` (default `-EngineRoot` = `$env:UE_ROOT`, see README "Configuration"; unversioned cook). Output: `Saved\Cooked\Windows\MiningUnitUI\Content`.
7. `scripts\pack.ps1`. Output: `build\out\pakchunk98-MiningUnitUI_P.{pak,ucas,utoc}`. Must print `verified`. Check that the list shows `/Game/MiningUnitUI/WBP_MiningUnit`.
8. Close the game. `scripts\install.ps1`. It backs up old pakchunk98 files (none expected) and copies the 3 files to the game Paks dir.
9. Start the game. Test-only: copy `lua\wbp_loader.lua` into the game's SandboxMenu Scripts dir and call `M.load()` from a test hook (loader is not wired into main.lua yet). Check probe.txt for `WBP class ok`. Keep native_mining.lua installed as fallback.
10. Test in game: `M.load()`, `M.open(ship)` on a Mining Unit. Window centred, mouse usable, tabs switch, Close restores camera and look. Old bench still loads (no change to pakchunk99).
11. Result to user: pass/fail per point. If pass, the next milestone is wiring the bridge (TODO in `lua\wbp_loader.lua`) and removing native_mining.lua from the flow.

## GATE (stop rule)
If the first cooked WBP fails to load in game (`WBP class NOT FOUND` in probe.txt, or `CreateWidget` fails, or the window is blank/crashes), STOP. Keep `native_mining.lua` as the Mining Unit window. Do not patch around it. Report the log lines to the user.

## Unknowns to verify (in this order)
1. Cooked asset version match: 5.7.4 cook vs game's 5.7 packages. `retoc verify` checks the container, not the game load. The game load (step 9) is the real test.
2. `-unversioned` cook: retoc `to-zen` may reject unversioned assets. If pack.ps1 fails, rerun cook.ps1 with `-Versioned`.
3. Class load path: `StaticFindObject("/Game/MiningUnitUI/WBP_MiningUnit.WBP_MiningUnit_C")` plus `LoadAsset` on a package that is only in a pak (no registry entry). If LoadAsset fails but the asset exists, suspect the asset registry: pack.ps1 packs only `Content` (like MiningUnitBlockPak), not `AssetRegistry.bin`.
4. Pak naming and priority vs `pakchunk99-ClonedBlock_P`: the two packages do not overlap (`/Game/Blueprints/...` vs `/Game/MiningUnitUI/...`), so priority should not matter. Verify that both mount (both load their assets in game).
5. Mount path: pack.ps1 stages under `MainSequence\Content` (same as MiningUnitBlockPak). The project folder is `MiningUnitUI`, so this remap is the thing to verify (`retoc list` must show `/Game/MiningUnitUI/...`).
6. Default map `/Engine/Maps/Entry` in DefaultEngine.ini: exists in <UE 5.7 dir> (checked). Cook should not need a map.
7. Parent class `UserWidget` is engine-only: no game C++ classes needed (plan verdict: S1 is plain UserWidget).
8. Tab highlight and the Items page action button: not in SPEC. Decide with user before step 4 (see SPEC open items).
