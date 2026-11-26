# MSDemoMods — Main Sequence Demo Mining Unit

Current version: **0.3.2** (`mods/MiningBenchMod/mb_version.hpp`). A ship is a mining ship only with the Mining Bench block AND a mining laser (no laser = not in the Mining window list). 0.3.2 requires MSCore >= 0.8.15 (API 16).

**Rock registry.** The table of every resource asteroid (seed from the save objects, disk cache, delete listener) lives in MSCore (API 16, MSCore >= 0.8.15, cache `Mods\MSCore\rockreg\RockRegCache.bin`). The mod is a thin client (`mb_registry.hpp`, owner id 1 in the feed protocol) and needs MSCore >= 0.8.15 for it: 0.3.2 and newer REQUIRE MSCore >= 0.8.15: there is no bench-only fallback for the registry. With an older MSCore the mod logs `rock registry needs MSCore 0.8.15 (API 16)` once and mining waits. While MSCore rockreg is off or starting it logs `rock registry not ready (MSCore rockreg off or starting)` once, retries every call, and mining pauses until it answers. Both this mod and Fleet installed seed once, in MSCore. The old per-mod `MiningRegistryCache.bin` (in the mod dlls folder) is orphaned and safe to delete.

**Ownership notice.** All game assets, textures, names, code, binaries and data belong to the game's developer and publisher. This is an unofficial fan mod; no ownership is claimed. Game-derived files in this repo are included only to build the mod (see `docs/ASSETS.md`). They are not covered by the MIT license.

> **Build prerequisite.** `build.ps1` needs the RE-UE4SS source tree (MIT, not included). Clone https://github.com/UE4SS/RE-UE4SS to `build\RE-UE4SS` (init submodules) before building; for the asset packs also put retoc (MIT) in `tools\retoc\`. Without them the build fails.

## License

MIT, see `LICENSE`. It covers our own code only.

## Requirements

- Main Sequence Demo (Steam) and RE-UE4SS for the game (`Binaries\Win64\ue4ss`). RE-UE4SS (MIT) is fetched separately, not in this repo.
- **MSCore**: separate repo, https://github.com/xxalex3xx97/MSCore. `mods/MiningBenchMod/mb_mscore.hpp` must match its `msc_api.hpp` layout.
- **MSCore 0.8.15** or newer (API 16) and **ShipControl 0.3.0** or newer (API level 4). ShipControl is its own repo, https://github.com/xxalex3xx97/ShipControl, pinned as the submodule `external/ShipControl` (tag in `deps.lock`). Clone with `git clone --recurse-submodules`, or run `git submodule update --init` in an existing checkout. Mining needs both: every ship move, speed guard and bubble ladder runs in ShipControl. Without ShipControl API 4, Start and Collect refuse with `error: ShipControl not installed or too old (needs API 4); update MSCore Manager`, and no loop starts.
- retoc (MIT), fetched separately into `tools\retoc\`, only for the asset packs (`ue/MiningUnitUI/scripts/pack.ps1`, `mods/MiningUnitBlockPak`).
- VS2022 BuildTools for the C++ build.

## What it does

A Mining Unit window (F4) automates mining for the bench ships: mine rocks, collect floating ore, dock, refuel, unload, and warp to a belt, with per-ship settings saved in `MiningBench.ini`. Ships act only through the game's normal input commands (`AddInputCommand`), the same path the player's controls use. The mod writes no simulation state of its own. It is designed to be co-op safe for that reason, but it has been tested in single-player only. It is made for single-player.

## How to use

### 1. Make a mining ship

A ship is a mining ship only with **both**:

| Part | Note |
|---|---|
| Mining Bench block | the building block added by this mod (`pakchunk99-ClonedBlock_P`) |
| Mining laser | no laser = the ship is not in the Mining window list |

Build it, then press **F4**.

### 2. The F4 window

Five tabs (Ship, Items, Operation, Log, Docks; names follow the widget names, captions not verified). Pick the ship in the header selector (bench ships only).

| Tab | Use it for |
|---|---|
| Ship | start / stop the loop, collect floating ore, and the dock buttons (Dock, Undock, Refuel, Unload, Park). **Dock** asks "Dock at" first (the Dock picker). |
| Items | ordered ore queue: ore + amount (0 = until the cargo is full). Saved as `items=` for that ship. |
| Operation | radius, mining distance, warp range, idle / wait / dock-hold seconds, per-fuel on / threshold / amount, Enabled, Repeat. Saved at once to `MiningBench.ini`. |
| Log | last lines per ship (`MiningLog.txt`). |
| Docks | dock queues, who waits where, per dock purpose. |

Steps:
1. Pick a ship, open the Items tab, add an ore (e.g. Titanium).
2. Open the Ship tab, click the loop start button. The ship mines, docks, unloads, refuels and goes back.
3. Floating ore around? Click the collect button (it shows the floating ore count).
4. The loop stop button ends the loop.

### 3. Name your docks with tags

Rename a dock in game. The mod reads `[tags]` from the dock name. No INI edit.

| Tag | Meaning |
|---|---|
| `[TITANIUM]` `[NICKEL]` `[COBALT]` `[IRIDIUM]` `[WATERICE]` (or `[ICE]`) `[SILICA]` `[GRAPHITE]` | unload dock for that ore. One ore tag per dock: 2 or more ore tags = the dock is ignored (logged once). |
| `[MISC]` | unload dock for non-ore cargo, and for ores with no dock of their own |
| `[AUTO-FILTER]` | sets the dock's item filter to its ore (its ore tag, else the ore whose INI dock list holds it) |
| `[PARKING]` | parking dock. Never an unload, misc or refuel target. |
| `[REFUEL]` | refuel-only dock |
| `[HYDROGEN]` `[DEUTERIUM]` `[WARP]` | which fuel the `[REFUEL]` dock serves. No fuel tag = all fuels. |

Rules: tags are in square brackets, any case. Spaces, `_` and `-` inside the brackets are ignored (`[Water Ice]` = `[WATERICE]`, `[auto filter]` = `[AUTO-FILTER]`). Tags can be combined in one name.

Copy-paste names:
```
Titanium unload [TITANIUM]
Ice [WATERICE] [AUTO-FILTER]
Junk [MISC]
Hangar [PARKING]
Fuel [REFUEL] [HYDROGEN] [WARP]
Any fuel [REFUEL]
```

Tags can also go in a **ship name** to set its queue (replaces the Items tab queue, INI untouched): `Miner [TITANIUM:200] [NICKEL]` = 200 titanium, then nickel until full. An unknown word is skipped; a repeated ore keeps the first.

### 4. Refuel rule

Each cycle the ship checks its enabled fuels (Operation tab; per-fuel on / threshold / amount, see `MiningBench.ini`).
- Any **enabled** fuel in cargo **below its threshold** -> refuel route.
- Otherwise -> mining route (unload).

Refuel dock order:
1. the Dock picker choice (Dock at)
2. `refuel_dock_id` / `refuel_dock_name` (INI)
3. `refuel_docks` list: `[REFUEL]`-tagged docks first (id order), then the manual picks
4. first free dock of that list

### 5. MiningBench.ini keys

File: `Binaries\Win64\ue4ss\Mods\MiningBenchMod\dlls\MiningBench.ini`. Created with defaults on first run, reloaded while the game runs. Top level = global; a `[ship.<id>]` section at the end overrides per-ship keys.

```ini
# kill_switch: 1 = mod stops touching the game (global)
kill_switch=0
# watchdog: freeze watchdog (global)
watchdog=1
# radius: mining radius (per-ship ok)
radius=50km
# warp_trip_km: a dock trip longer than this warps if the ship has warp fuel
warp_trip_km=5
# dock_hold_limit: max seconds at the dock, 0 = no limit
dock_hold_limit=300
# wait_seconds: wait at the dock (global)
wait_seconds=10
# fuel_<hydrogen|warp|deuterium>_on / _threshold / _amount (global)
fuel_hydrogen_on=1
fuel_hydrogen_threshold=300
fuel_hydrogen_amount=300
# refuel_dock_id: dock id (digits) or in-game dock name; empty = none
refuel_dock_id=Fuel
# refuel_docks: ids only, tried in order (per-ship ok)
refuel_docks=12,15
# unload_docks_<titanium|nickel|cobalt|iridium|waterice|silica|graphite>: ids
unload_docks_titanium=12
# unload_docks_misc: ids (global)
unload_docks_misc=14
# parking_docks: ids (per-ship ok)
parking_docks=20

[ship.123456]
items=ITEM_TitaniumOre:500,ITEM_NickelOre:0
refuel_dock_id=12
```

Dock lists take ids only (names are not allowed in lists). Tagged docks are merged in on top of these lists.

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
- Chunk hold (keeps far bench ships' chunks loaded): the chunk-load calls are made by MSCore 0.8.7 or newer (`MSCore_chunk_hold2`); the mod only sends its ship cubes there (`mb_chunkhold.hpp`, `mb_mscore.hpp`). Older MSCore: chunk hold stays off.
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
- Reported earlier, not re-verified: flee may trigger on an idle ship (a dock trip after a hull drop with the loop never started).

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
| `mods/MiningBenchMod/mb_docktags.hpp` | dock name tags (`[TITANIUM]`, `[MISC]`, `[PARKING]`, `[REFUEL]` ...) read from the dock walk; writes `DockTags.txt` / `DockFilter.txt` |
| `mods/MiningBenchMod/mb_dockjob.hpp` | dock and undock jobs, one per ship (stages, leave-dock watch) |
| `mods/MiningBenchMod/mb_net.hpp` | dock network: one queue per dock, reservations, holding slots (Docks tab data) |
| `mods/MiningUnitUI/` | Mining Unit window (release Lua): `Scripts/main.lua` (F4 / ESC / F keys), `Scripts/wbp_loader.lua` (widget loader, data) |
| `ue/MiningUnitUI/` | Unreal project for the window and the mining building block (art, paste trees, cook scripts) |
| `tests/` | `run.ps1` (all checks), `test_logic.cpp`, `test_compat.cpp`, Python checks |
| `tools/` | `compat_dump.py` (native pattern generator / verifier), `compat_names.py` (contract name list) |
| `scripts/package.ps1` | builds `dist\MSMiningUnit-<ver>-preview-for-developer.zip` |
| `build.ps1` | compile the DLL (`-Install` copies it into the game; game must be closed) |
| `docs/` | `ASSETS.md` (art origin and license), `DevReference.html` (developer reference, not in the zip), `REBUILD-AFTER-UPDATE.md` (rebuild mod paks after a game update) |

Include order (dependency order, `dllmain.cpp`): `mb_version, mb_common, mb_pattern, mb_compat, mb_config, mb_ue, mb_mscore, mb_math, mb_docks, mb_ships, mb_weapons, mb_orders, mb_filters, mb_cmdstate, mb_rocks, mb_steer, mb_net, mb_dockjob, mb_pin, mb_warp, mb_warpreal, mb_belt, mb_mining, mb_chunkhold, mb_devcmd, mb_testcmd, mb_compat_run`.

## Build and test
Needs VS2022 BuildTools and a RE-UE4SS checkout at `<repo>\build\RE-UE4SS` (not in git).
```powershell
.\build.ps1                 # compile only; check for "error C" in the output
.\build.ps1 -Install        # compile + back up and install into the game (game must be closed)
.\tests\run.ps1             # all unit and static checks, no game needed
.\scripts\package.ps1            # release zip
```
A full game restart is needed for any DLL change.

## Known facts worth remembering
- Save-object `Location` is chunk-relative: never measure distances with it. Use actor positions.
- `raw_per_meter` is 65536 (rock radii come out as exact 8 / 70 m).
- Looping `FindAllOf` or reflection over every rock each second freezes the game: lists are cached and refreshed every 5-10 s.
- Cached actor pointers can be freed by streaming: validate with `actor_ok()` before touching them.

## After a game update
- Before: a patched game could move the chunk-hold natives, rename a property or change a command's type id, and the mod could crash or write the wrong field.
- Now: the chunk-hold natives live in MSCore (0.8.7+; the mod checks its exports, else chunk hold is off). The command type id at +0x28 is validated. The contract table (every game name the mod reads) is checked once per session; each feature shows OK or DISABLED. Every tick step runs under SEH; 5 faults in a row -> SAFE MODE (ships stopped, holds released, mod idle until restart).
- Limits: a same-name, same-type change in behaviour is not detected; the natives are confirmed only by the chunk-hold canary; not tested in a patched game.
- Check: search `COMPAT` in UE4SS.log or `MiningCompat.txt` in the mod folder. All OK = nothing to do. A DISABLED feature = that part is off, the game runs. SAFE MODE = restart the game.
- `kill_switch=1` in `MiningBench.ini` stops the mod completely (hot reload); `0` resumes.
- Tested build exe size 149200896 (informational only, logged as `COMPAT build size=`).
- Pak donors can change too: see [docs/REBUILD-AFTER-UPDATE.md](docs/REBUILD-AFTER-UPDATE.md) (`scripts\rebuild-after-update.ps1`).
