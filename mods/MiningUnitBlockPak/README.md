# MiningUnitBlockPak (Main Sequence Demo cloned building blocks)

Build steps (offline, Windows, run from this folder):

1. `node clone.js`: writes src\ blocks and machine from legacy vanilla assets.
   - BPO_MiningBenchBlock (old bench, must stay byte-identical for old saves)
   - BPO_MinerModuleBlock ("Mining Unit", own block class)
   - Machines\BPO_MiningMod (copy of BPO_Workbench, own machine class)
2. Registry: `python -I patch_registry.py <K> <A|B> <OUT_DIR>`
   - `1 A` = original single-entry build (reproduces the k=1 registry byte-for-byte)
   - `2 A` = bench + Mining Unit both in the builder list (written to src\)
   - `2 B` = bench hidden from the builder (still imported/preloaded), Mining Unit listed (written to a staging copy, e.g. stageB\)
   The script re-parses its output and asserts names, imports, export size/offset, preload refs and the Functional Blocks array.
3. Research: `python -I patch_research.py <OUT_DIR>` adds BPO_MinerModuleBlock_C to Data\Research\RSC_MiningLaser (Mining Laser research; array 2 -> 3; the Mining Unit unlocks only after the Mining Laser research is done, which has its own prerequisites).
   Written to src\ and to a staging copy (`robocopy stageB stageC /E`, then patch into stageC\MainSequence\Content\Data\Research).
   Re-parses its output and asserts names, imports, export size/offset, preload refs and the array.
4. Pack: `<repo>\tools\retoc\retoc.exe to-zen --version UE5_7 <staging dir containing MainSequence\Content> <out>\pakchunk99-ClonedBlock_P.utoc`
   - `retoc.exe list --path --size <utoc>` lists the packages.
   - `retoc.exe verify <utoc>` must print `verified`.
   - `to-legacy` round-trip did not work offline (needs game script objects); not used.
   - Legacy src assets have UE 5.6 style headers: master retoc.exe `--version UE5_7` fails with 'failed to fill whole buffer' (upstream retoc #61). Working command for out5: `<repo>\tools\retoc\retoc.exe to-zen --version UE5_6 --no-parallel stageC out5\pakchunk99-ClonedBlock_P.utoc` (run in this folder; out5 must exist). Not yet tested in game.

Do not install into the game Paks folder without a backup (see backup\).

## Installed build (own-actor: Mining Unit uses BP_MiningMod + STB_MiningUnits)

Source: `out4\` (retoc output, `2 B` registry, stageB). Adds `Blueprints\Machines\BP_MiningMod` (copy of BP_Workbench, widget ref -> WBP_MiningUnt which does not exist) and `Data\Localization\STB_MiningUnits` ("to Use Mining Unit"). BPO_MiningMod imports BP_MiningMod. Installed to `...\Windows\MainSequence\Content\Paks\` as `pakchunk99-ClonedBlock_P.{pak,ucas,utoc}` (names unchanged).
Previous install backed up to `backup\pak99-20261028-0932-pre-ownactor\` (= variant B `out3b`).

SHA256:
- pak  75e7144577253917f6da7312ef5e585b12fb728226a22b0938323751a6b555cd
- ucas cd9d41b0fbb652dea1159771eee3687077ad13a97bb40afcfc029139c374e692
- utoc 71d5c190446f53b638e69565323de475393b1c217e9db1c1e246f2384b1143c0

Fallback variant B: `out3b\` (backup above). Variant A fallback: `out3\` (bench + Mining Unit both listed).

## Research unlock build (Mining Unit unlocked via Mining Laser research)

Source: `out5\` (retoc output of stageC = stageB + patched Data\Research\RSC_MiningLaser, registry variant `2 B`, BP_MiningMod, STB). Mining Unit added to the Mining Laser research block list (array 2 -> 3), so it unlocks only after the Mining Laser research is done (which has its own prerequisites), without the dev-menu cheat. Not installed by the build; previous installed build `out4\`. Packed with UE5_6 (see Pack step), 7 packages, `retoc verify` = verified.

SHA256:
- pak  75e7144577253917f6da7312ef5e585b12fb728226a22b0938323751a6b555cd
- ucas ee00acde3ab87ef229ba2e9dd0d40e5831111413d56ae87890ba77a75bb1554e
- utoc b08c0620c2073d4569398036c13dd92a46d7e37809f8d84ee861c2be05561634
