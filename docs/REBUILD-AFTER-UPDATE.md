# Rebuild the mod paks after a game update

## Why

A game update can change the bytes of the donor assets the mod paks are cloned from. On 2026-11-20 two donor headers moved (`BP_Workbench.uexp` @1296 16->17, `BP_CommandModule.uexp` @740 8->10). The clone paks built from the old donors crashed at start with AsyncLoading2 "Bad export index". Rebuilding from the new vanilla fixes it.

## Run it

Close the game first, then from the repo root:

```powershell
powershell -File scripts\rebuild-after-update.ps1 [-Check | -Install]
```

- No flags: rebuilds paks 99, 100 and 101 into `-OutDir`, runs `retoc verify` on each, and prints the result. It does not touch the live Paks folder.
- `-Check`: rebuilds to a temp `-OutDir` (removed afterwards unless you passed `-OutDir`), SHA256-compares each `.ucas` and `.utoc` against the live pak, prints `FRESH` or `STALE` per pak. Exit 1 if any is STALE.
- `-Install`: rebuilds, `verify`s all 3 (aborts if any fails), rechecks the game is closed, backs up the live `pakchunk99/100/101*` files to `%USERPROFILE%\.claude\jobs\rollback-<MMdd>\paks-pre-install-<HHmm>`, then copies the rebuilt `.pak/.utoc/.ucas` into `Content\Paks`. Exit 1 on FAIL.
- `-Check` and `-Install` together throw.

The `-Install` path is untested so far. Run `-Check` afterwards to confirm the live paks are FRESH.

| Flag | Default | Meaning |
|---|---|---|
| `-OutDir` | `%TEMP%\ms-rebuilt-paks-<yyyyMMdd-HHmmss>` | Where the rebuilt paks go (kept unless `-Check` and no `-OutDir`) |
| `-GameRoot` | `F:\SteamLibrary\steamapps\common\Main Sequence` | Game root. Paks are read from `Windows\MainSequence\Content\Paks` |
| `-Retoc` | `F:\MSDemoMods\tools\retoc\retoc.exe` | retoc binary |
| `-BpuDir` | `F:\ms-bpunit\mods\BlueprintUnitPak` | BlueprintUnit pak sources |
| `-MsmDir` | `F:\MSDemoMods\mods\MSMiningModel` | MSMiningModel pak sources |
| `-Work` | `<drive of GameRoot>:\ms-rebuild-tmp\<stamp>` | Scratch dir |
| `-KeepWork` | off | Keep the scratch dir (otherwise deleted) |
| `-Check` | off | Compare rebuilt paks to live, no install |
| `-Install` | off | Verify, back up live paks, copy rebuilt paks in |

## Scope

Rebuilt: pak 99 (ClonedBlock), pak 100 (BlueprintUnit, clone variant, needs pak 97) and pak 101 (MSMiningModel).
Untouched: 95, 97, 98, 102, 103. They have no vanilla donor.

## Requirements

- retoc, BlueprintUnit sources and MSMiningModel sources at the paths above. The last two live outside this repo, so the script is not self-contained.
- `node` and `python` on PATH.
- The script stops with `missing: <path>` if a path is absent, and with `game running, close it` if a Main Sequence process is up.

## Fine print

- It never reads `MS_GAME_DIR`. Use `-GameRoot`.
- Vanilla extract: only `global.utoc/.ucas` and `pakchunk0-Windows.pak/.utoc/.ucas` are read from the Paks folder, as hardlinks into the scratch dir. If a hardlink fails, that file is copied from the live Paks folder instead. Nothing else there is read.
- The scratch dir is removed in a `finally` block unless `-KeepWork`. The default parent `ms-rebuild-tmp` folder stays.
