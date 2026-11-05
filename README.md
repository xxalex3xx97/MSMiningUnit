# MSDemoMods — Main Sequence Demo Mining Unit

Current version: **0.2.1** (`mods/MiningBenchMod/mb_version.hpp`).

**Ownership notice.** All game assets, textures, names, code, binaries and data belong to the game's developer and publisher. This is an unofficial fan mod; no ownership is claimed. Game-derived files in this repo are included only to build the mod (see `docs/ASSETS.md`). They are not covered by the MIT license.

> **Build prerequisite.** `build.ps1` needs the RE-UE4SS source tree (MIT, not included). Clone https://github.com/UE4SS/RE-UE4SS to `build\RE-UE4SS` (init submodules) before building; for the asset packs also put retoc (MIT) in `tools
etoc\`. Without them the build fails.

## License

MIT, see `LICENSE`. It covers our own code only.

## Requirements

- Main Sequence Demo (Steam) and RE-UE4SS for the game (`Binaries\Win64\ue4ss`). RE-UE4SS (MIT) is fetched separately, not in this repo.
- **MSCore**: separate repo at `<MSCore repo>`. `mods/MiningBenchMod/mb_mscore.hpp` must match its `msc_api.hpp` layout.
- retoc (MIT), fetched separately into `tools\retoc\`, only for the asset packs (`ue/MiningUnitUI/scripts/pack.ps1`, `mods/MiningUnitBlockPak`).
- VS2022 BuildTools for the C++ build.

## What it does

A Mining Unit window (F4) automates mining for the bench ships: mine rocks, collect floating ore, dock, refuel, unload, and warp to a belt, with per-ship settings saved in `MiningBench.ini`. Ships act only through the game's normal input commands (`AddInputCommand`), the same path the player's controls use. The mod writes no simulation state of its own. It is designed to be co-op safe for that reason, but it has been tested in single-player only. It is made for single-player.

## Install

Use **MSCoreManager.exe** from [MSCore](https://github.com/xxalex3xx97/MSCore/releases/latest). It installs and updates the mod for you. Do not copy files by hand unless you have to.

1. Close the game.
2. Run `MSCoreManager.exe` and click **Install/Repair MSCore**.
3. Click **Add mod...** and enter `xxalex3xx97/MiningBenchMod`.
4. Start the game, press F4.

**Uninstall.** Select the mod in MSCoreManager and click **Remove mod**.

**Disable without uninstalling.** Set `kill_switch=1` in `Binaries\Win64\ue4ss\Mods\MiningBenchMod\dlls\MiningBench.ini`. The mod then stops touching the game (hot reload). `0` turns it back on.

**What it hooks and touches.**
- One `AddInputCommand` pre-hook, used to read map pin commands (`mb_pin.hpp`). Released on unload.
- Native chunk-load calls (keeps far bench ships' chunks loaded). The three functions are found by byte-pattern scan, exactly one match each; otherwise chunk hold stays off (`mb_natives.hpp`, `mb_chunkhold.hpp`).
- Reflection reads of game objects (ship health, cargo, docks, orders, sector data). Writes go through the game's own functions or input commands.
- Files next to the DLL: `MiningBench.ini` (settings), `MiningCmd.txt` (command channel), `MiningStatus*.txt` / `MiningLog.txt` (UI data), `MiningCompat.txt` (compat report).

**Known risks.**
- A native crash (access violation) cannot be caught by C++ `try/catch`. Tick steps run under an SEH guard, but cached raw object pointers can still go stale when the game streams actors out.
- The natives and the game's property names can change in a game update. The compat self-test disables a feature that no longer matches (see "After a game update").
- Dev test commands can be sent through `MiningCmd.txt` (instant warp, chunk release, debug). They are not in the UI, but they are reachable from that file.

**Manual install (only if the manager cannot be used).**
1. Close the game. Copy the `Binaries` and `Content` folders from the zip into `<game>\Windows\MainSequence\` (merge, keep existing files). Steam default: `<SteamLibrary>\steamapps\common\Main Sequence Demo\Windows\MainSequence`.
2. In `Binaries\Win64\ue4ss\Mods\mods.txt` add (or set to 1):
   ```
   MiningBenchMod : 1
   MiningUnitUI : 1
   ```
3. Start the game, press F4. The package has 2 paks: `pakchunk98-MiningUnitUI_P` (window) and `pakchunk99-ClonedBlock_P` (mining building block).

**Manual uninstall.** Set both lines to 0 (or delete them). Delete `Binaries\Win64\ue4ss\Mods\MiningBenchMod` and `Binaries\Win64\ue4ss\Mods\MiningUnitUI`. Delete `Content\Paks\pakchunk98-MiningUnitUI_P.*` and `Content\Paks\pakchunk99-ClonedBlock_P.*`.

## Known issues

- Cached raw `UObject*` globals can go stale and crash the game (see Known risks).
- Flee can trigger on idle ships: a ship can start a dock trip after a hull drop even when its loop was never started.

**Happy to remove or change anything you dislike.** Tell me which part and I will take it out or alter it.

## Configuration

Build and asset scripts read these environment variables. Unset = the built-in fallback, which is the original author's local path. Set them to your own locations.

| Variable | Used by | Points to |
|---|---|---|
| `MS_GAME_DIR` | `build.ps1`, `scripts/package.ps1`, `ue/MiningUnitUI/scripts/install.ps1` | the game's `Windows\MainSequence` folder |
| `MS_MODS_ROOT` | `build.ps1`, `tests/run.ps1`, `ue/MiningUnitUI/scripts/pack.ps1`, `extract_item_icons.py`, `extract_textures.py`, `mods/MiningUnitBlockPak/*` | this repo's root (the folder with `build\`, `legacy\`, `tools\`) |
| `UE_ROOT` | `ue/MiningUnitUI/scripts/cook.ps1` | the UE 5.7 install folder |
| `MS_TEX_DIR` | `ue/MiningUnitUI/scripts/extract_textures.py` | game UI texture folder (default: under `MS_MODS_ROOT\legacy`) |

`legacy\` (the vanilla game extract that the extract and clone scripts read) is git-ignored and not part of the repo. Point `MS_MODS_ROOT` at a checkout that has it.

## Layout
| Path | What |
|---|---|
| `mods/MiningBenchMod/` | the mining C++ mod (release): `dllmain.cpp` entry point, `mb_*.hpp` header-only split (each file starts with a WHAT / WHY / DEPENDS ON / USED BY banner) |
| `mods/MiningBenchMod/mb_logic.hpp` | pure logic (config/INI parse, rule maths), no game or Windows calls; unit tested |
| `mods/MiningBenchMod/mb_mining.hpp` | mining loop state machine per ship (`mine_step` and `st_*` states), status and log output |
| `mods/MiningBenchMod/mb_net.hpp` | dock network: one queue per dock, reservations, holding slots (Docks tab data) |
| `mods/MiningUnitUI/` | Mining Unit window (release Lua): `Scripts/main.lua` (F4 / ESC / F keys), `Scripts/wbp_loader.lua` (widget loader, data) |
| `mods/MSDevMenu/` | Dev Menu (dev only, separate mod, NOT in the release zip): F3 window, cheats, test buttons, probes |
| `ue/MiningUnitUI/` | Unreal project for the window and the mining building block (art, paste trees, cook scripts) |
| `tests/` | `run.ps1` (all checks), `test_logic.cpp`, `test_compat.cpp`, Python checks |
| `tools/` | `compat_dump.py` (native pattern generator / verifier), `compat_names.py` (contract name list) |
| `scripts/package.ps1` | builds `dist\MSMiningUnit-<ver>-preview-for-developer.zip` (and `-DevMenu` for `dist\MSDevMenu-<ver>.zip`) |
| `build.ps1` | compile the DLL (`-Install` copies it into the game; game must be closed) |
| `docs/` | `ASSETS.md` (art origin and license), `DevReference.html` (developer reference, not in the zip) |

Include order (dependency order, `dllmain.cpp`): `mb_version, mb_common, mb_pattern, mb_compat, mb_config, mb_ue, mb_mscore, mb_math, mb_docks, mb_ships, mb_weapons, mb_orders, mb_filters, mb_cmdstate, mb_rocks, mb_steer, mb_net, mb_dockjob, mb_pin, mb_warp, mb_warpreal, mb_belt, mb_mining, mb_natives, mb_chunkhold, mb_devcmd, mb_testcmd, mb_compat_run`.

## Build and test
Needs VS2022 BuildTools and a RE-UE4SS checkout at `<repo>\build\RE-UE4SS` (not in git).
```powershell
.\build.ps1                 # compile only; check for "error C" in the output
.\build.ps1 -Install        # compile + back up and install into the game (game must be closed)
.\tests\run.ps1             # all unit and static checks, no game needed
.\scripts\package.ps1 -DevMenu   # release zip (+ dev menu zip)
```
A full game restart is needed for any DLL change.

## Known facts worth remembering
- Save-object `Location` is chunk-relative: never measure distances with it. Use actor positions.
- `raw_per_meter` is 65536 (rock radii come out as exact 8 / 70 m).
- Looping `FindAllOf` or reflection over every rock each second freezes the game: lists are cached and refreshed every 5-10 s.
- Cached actor pointers can be freed by streaming: validate with `actor_ok()` before touching them.

## After a game update
- Before: a patched game could move the chunk-hold natives, rename a property or change a command's type id, and the mod could crash or write the wrong field.
- Now: the natives are found by byte pattern (exactly 1 match each, else chunk hold is off). The command type id at +0x28 is validated. The contract table (every game name the mod reads) is checked once per session; each feature shows OK or DISABLED. Every tick step runs under SEH; 5 faults in a row -> SAFE MODE (ships stopped, holds released, mod idle until restart).
- Limits: a same-name, same-type change in behaviour is not detected; the natives are confirmed only by the chunk-hold canary; not tested in a patched game.
- Check: search `COMPAT` in UE4SS.log or `MiningCompat.txt` in the mod folder. All OK = nothing to do. A DISABLED feature = that part is off, the game runs. SAFE MODE = restart the game.
- `kill_switch=1` in `MiningBench.ini` stops the mod completely (hot reload); `0` resumes.
- Tested build exe size 149200896 (informational only, logged as `COMPAT build size=`).
- Developer: `python tools\compat_dump.py gen <exe> system=0x... setlevel=0x... apply=0x...` then paste the patterns into `mb_natives.hpp` and run `python tools\compat_dump.py verify <exe> mods\MiningBenchMod\mb_natives.hpp` (expect 1/1/1).
