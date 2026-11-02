// ponytail: one-shot clones from legacy vanilla assets. Same-length byte patches only (no UAsset re-serialise).
// Jobs:
//   1. BPO_WorkstationBlock -> BPO_MiningBenchBlock  (old bench, output must stay byte-identical; saves depend on it)
//   2. BPO_WorkstationBlock -> BPO_MinerModuleBlock  ("Mining Unit", own block class)
//   3. Machines/BPO_Workbench -> Machines/BPO_MiningMod (own machine class used by block 2)
//   4. Machines/BP_Workbench  -> Machines/BP_MiningMod  (own actor for BPO_MiningMod, no workbench widget)
//   5. Data/Localization/STB_Interaction -> STB_MiningUnits ("Press F to Use Mining Unit" text)
const fs = require('fs');
const path = require('path');

const ROOT = process.env.MS_MODS_ROOT || 'F:\\MSDemoMods';  // repo root, holds legacy\ (vanilla source)
const L = ROOT + '\\legacy\\MainSequence\\Content';
const W = ROOT + '\\mods\\MiningUnitBlockPak';
const OUT = path.join(W, 'src', 'MainSequence', 'Content');
const BLK = path.join('Blueprints', 'BuildingBlocks');
const MCH = path.join('Blueprints', 'Machines');
const LOC = path.join('Data', 'Localization');
// Same-length renames for the own-actor chain. ORDER MATTERS: WBP_Workbench contains BP_Workbench, so do WBP first.
const ACT_OLD = 'BP_Workbench', ACT_NEW = 'BP_MiningMod';
const STB_OLD = 'STB_Interaction', STB_NEW = 'STB_MiningUnits';
const WBP_OLD = 'WBP_Workbench', WBP_NEW = 'WBP_MiningUnt';

// Replace all occurrences of `from` with `to` (same length) in buf. Returns count.
function replaceAll(buf, from, to) {
  if (from.length !== to.length) throw new Error('length mismatch: ' + from + ' vs ' + to);
  const a = Buffer.from(from, 'latin1');
  const b = Buffer.from(to, 'latin1');
  let count = 0;
  let idx = buf.indexOf(a, 0, 'latin1');
  while (idx !== -1) {
    b.copy(buf, idx);
    count++;
    idx = buf.indexOf(a, idx + a.length, 'latin1');
  }
  return count;
}

function utf16(s) {
  return Buffer.from(s, 'utf16le').toString('latin1');
}

// Load legacy file, apply patch fn(buf) -> counts, assert length unchanged, write to src.
function job(label, srcRel, dstRel, patch) {
  const src = path.join(L, srcRel);
  const buf = fs.readFileSync(src);
  const origLen = buf.length;
  const counts = patch(buf);
  if (buf.length !== origLen) throw new Error('length changed for ' + srcRel);
  const dst = path.join(OUT, dstRel);
  fs.mkdirSync(path.dirname(dst), { recursive: true });
  fs.writeFileSync(dst, buf);
  console.log(label, srcRel, '->', dst, JSON.stringify(counts));
  return counts;
}

// Fails the run if any required count is zero.
let bad = false;
function need(label, n) {
  console.log(label + ': ' + n);
  if (!(n > 0)) { console.log('ERROR: zero replacements for ' + label); bad = true; }
}

// ---- Job 1: old bench (output must stay byte-identical) ----
const BENCH_NAME = 'BPO_MiningBenchBlock';
const WS = 'BPO_WorkstationBlock';
const benchUa = job('bench', path.join(BLK, WS + '.uasset'), path.join(BLK, BENCH_NAME + '.uasset'), (buf) => ({
  bpoAscii: replaceAll(buf, WS, BENCH_NAME),
  bpoUtf16: replaceAll(buf, utf16(WS), utf16(BENCH_NAME)),
}));
const benchUe = job('bench', path.join(BLK, WS + '.uexp'), path.join(BLK, BENCH_NAME + '.uexp'), (buf) => {
  const pre = '\x0C\x00\x00\x00';
  return {
    // uexp has no BPO name text (names are index-based), so no BPO count here
    workstationText: replaceAll(buf, pre + 'Workstation\x00', pre + 'MiningBench\x00'),
    key1: replaceAll(buf, 'F7D64C41E6BC788772AC9C0BB74E', '07D64C41E6BC788772AC9C0BB74E'),
    key2: replaceAll(buf, 'AA2C2B4A42CDCA43FE5EB1725CF49E', '0A2C2B4A42CDCA43FE5EB1725CF49E'),
  };
});
need('bench uasset BPO rename', benchUa.bpoAscii + benchUa.bpoUtf16);
need('bench uexp Workstation text', benchUe.workstationText);
need('bench uexp key1', benchUe.key1);
need('bench uexp key2', benchUe.key2);

// ---- Job 2: Mining Unit block ----
const MU_NAME = 'BPO_MinerModuleBlock';
const MACHINE_OLD = 'BPO_Workbench';
const MACHINE_NEW = 'BPO_MiningMod';
const muUa = job('mining-unit', path.join(BLK, WS + '.uasset'), path.join(BLK, MU_NAME + '.uasset'), (buf) => ({
  bpoAscii: replaceAll(buf, WS, MU_NAME),
  bpoUtf16: replaceAll(buf, utf16(WS), utf16(MU_NAME)),
  // 3 ASCII hits checked by hand: package path, BPO_Workbench_C, Default__BPO_Workbench_C (all the machine import)
  machine: replaceAll(buf, MACHINE_OLD, MACHINE_NEW),
}));
const muUe = job('mining-unit', path.join(BLK, WS + '.uexp'), path.join(BLK, MU_NAME + '.uexp'), (buf) => {
  const pre = '\x0C\x00\x00\x00';
  return {
    workstationText: replaceAll(buf, pre + 'Workstation\x00', pre + 'Mining Unit\x00'),
    key1: replaceAll(buf, 'F7D64C41E6BC788772AC9C0BB74E', '17D64C41E6BC788772AC9C0BB74E'),
    key2: replaceAll(buf, 'AA2C2B4A42CDCA43FE5EB1725CF49E', '1A2C2B4A42CDCA43FE5EB1725CF49E'),
    // same length (74 chars, space padded): tooltip text of the Mining Unit
    desc: replaceAll(buf, 'Provides a refinery and assemble to use for crafting. Cannot be automated.', 'Automates mining ships: mine, dock, refuel, warp. F4 = multi-ship menu.   '),
  };
});
need('mining-unit uasset BPO rename', muUa.bpoAscii + muUa.bpoUtf16);
need('mining-unit uasset machine import', muUa.machine);
need('mining-unit uexp display text', muUe.workstationText);
need('mining-unit uexp key1', muUe.key1);
need('mining-unit uexp key2', muUe.key2);
need('mining-unit uexp description', muUe.desc);

// ---- Job 3: machine BPO_MiningMod (copy of BPO_Workbench) ----
const mcA = job('machine', path.join(MCH, MACHINE_OLD + '.uasset'), path.join(MCH, MACHINE_NEW + '.uasset'), (buf) => ({
  ascii: replaceAll(buf, MACHINE_OLD, MACHINE_NEW),
  utf16: replaceAll(buf, utf16(MACHINE_OLD), utf16(MACHINE_NEW)),
  // own actor: BPO_MiningMod imports BP_MiningMod instead of BP_Workbench
  act: replaceAll(buf, ACT_OLD, ACT_NEW),
}));
need('machine uasset ascii', mcA.ascii);
need('machine uasset actor import', mcA.act);
job('machine', path.join(MCH, MACHINE_OLD + '.uexp'), path.join(MCH, MACHINE_NEW + '.uexp'), (buf) => ({
  // uexp has no ASCII BPO_Workbench (checked); copied with the same replacement, count may be 0
  ascii: replaceAll(buf, MACHINE_OLD, MACHINE_NEW),
}));

// ---- Job 4: own actor BP_MiningMod (copy of BP_Workbench, no shared workbench widget / STB key) ----
const actUa = job('actor', path.join(MCH, ACT_OLD + '.uasset'), path.join(MCH, ACT_NEW + '.uasset'), (buf) => ({
  wbp: replaceAll(buf, WBP_OLD, WBP_NEW),
  wbpUtf16: replaceAll(buf, utf16(WBP_OLD), utf16(WBP_NEW)),
  act: replaceAll(buf, ACT_OLD, ACT_NEW),
  actUtf16: replaceAll(buf, utf16(ACT_OLD), utf16(ACT_NEW)),
  stb: replaceAll(buf, STB_OLD, STB_NEW),
  stbUtf16: replaceAll(buf, utf16(STB_OLD), utf16(STB_NEW)),
}));
need('actor uasset BP rename', actUa.act + actUa.actUtf16);
job('actor', path.join(MCH, ACT_OLD + '.uexp'), path.join(MCH, ACT_NEW + '.uexp'), (buf) => ({
  // uexp holds no BP/WBP/STB names (index-based); same replacements, counts may be 0
  wbp: replaceAll(buf, WBP_OLD, WBP_NEW),
  act: replaceAll(buf, ACT_OLD, ACT_NEW),
  stb: replaceAll(buf, STB_OLD, STB_NEW),
}));

// ---- Job 5: own interaction string table (copy of STB_Interaction, "to Use Mining Unit") ----
const stbUa = job('stb', path.join(LOC, STB_OLD + '.uasset'), path.join(LOC, STB_NEW + '.uasset'), (buf) => ({
  stb: replaceAll(buf, STB_OLD, STB_NEW),
  stbUtf16: replaceAll(buf, utf16(STB_OLD), utf16(STB_NEW)),
}));
need('stb uasset table rename', stbUa.stb + stbUa.stbUtf16);
const stbUe = job('stb', path.join(LOC, STB_OLD + '.uexp'), path.join(LOC, STB_NEW + '.uexp'), (buf) => ({
  stb: replaceAll(buf, STB_OLD, STB_NEW),
  // FString with NUL; 18 chars both sides so length prefix unchanged
  text: replaceAll(buf, 'to Use Workstation\x00', 'to Use Mining Unit\x00'),
}));
need('stb uexp text', stbUe.text);

if (bad) process.exit(2);
console.log('OK');
