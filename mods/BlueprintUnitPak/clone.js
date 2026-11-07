const fs = require('fs'), path = require('path');
const L = (process.env.MS_MODS_ROOT || 'F:\\MSDemoMods') + '\\legacy\\MainSequence\\Content\\Blueprints\\BuildingBlocks';
const OUT = path.join(__dirname, 'stage', 'MainSequence', 'Content', 'Blueprints', 'BuildingBlocks');
fs.mkdirSync(OUT, { recursive: true });
const BPO_OLD = 'BPO_CommandModule', BPO_NEW = 'BPO_BlueprintUnit';
const BP_OLD = 'BP_CommandModule', BP_NEW = 'BP_BlueprintUnit';
const MESH_OLD = 'SM2_Holotable', MESH_NEW = 'SM2_BPUnitTbl';
const DESC_OLD = 'A central command location for stations. Can be used to construct new station modules.';
const DESC_NEW = ('Imports a factory blueprint from the clipboard and builds it on your station.').padEnd(86, ' ');
if (BPO_OLD.length !== BPO_NEW.length || BP_OLD.length !== BP_NEW.length || MESH_OLD.length !== MESH_NEW.length || DESC_OLD.length !== DESC_NEW.length || DESC_OLD.length !== 86) throw new Error('length mismatch');
function replaceAll(buf, from, to) {
  if (from.length !== to.length) throw new Error('length mismatch: ' + from + ' vs ' + to);
  const a = Buffer.from(from, 'latin1'), b = Buffer.from(to, 'latin1');
  let n = 0, i = buf.indexOf(a, 0, 'latin1');
  while (i !== -1) { b.copy(buf, i); n++; i = buf.indexOf(a, i + a.length, 'latin1'); }
  return n;
}
const u16 = (s) => Buffer.from(s, 'utf16le').toString('latin1');
const both = (buf, f, t) => replaceAll(buf, f, t) + replaceAll(buf, u16(f), u16(t));
let bad = false;
function need(label, n) { console.log(label + ': ' + n); if (!(n > 0)) { console.log('ERROR zero: ' + label); bad = true; } }
function job(src, dst, fn) {
  const buf = fs.readFileSync(path.join(L, src)); const len = buf.length;
  const c = fn(buf);
  if (buf.length !== len) throw new Error('length changed ' + src);
  fs.writeFileSync(path.join(OUT, dst), buf);
  return c;
}
const bpoUa = job(BPO_OLD + '.uasset', BPO_NEW + '.uasset', (b) => ({ bpo: both(b, BPO_OLD, BPO_NEW), bp: both(b, BP_OLD, BP_NEW) }));
need('BPO uasset self', bpoUa.bpo); need('BPO uasset actor import', bpoUa.bp);
const bpoUe = job(BPO_OLD + '.uexp', BPO_NEW + '.uexp', (b) => ({
  text: replaceAll(b, '\x0F\x00\x00\x00Command Module\x00', '\x0F\x00\x00\x00Blueprint Unit\x00'),
  desc: replaceAll(b, DESC_OLD, DESC_NEW),
  k1: replaceAll(b, 'C2E293D24543B80DB3F9B5B6F44BEA8F', '12E293D24543B80DB3F9B5B6F44BEA8F'),
  k2: replaceAll(b, '90569B1B4FE3B21DA14BBBBFB2C29545', '10569B1B4FE3B21DA14BBBBFB2C29545'),
}));
need('BPO uexp text', bpoUe.text); need('BPO uexp desc', bpoUe.desc);
need('BPO uexp key1', bpoUe.k1); need('BPO uexp key2', bpoUe.k2);
const bpUa = job(BP_OLD + '.uasset', BP_NEW + '.uasset', (b) => ({
  bpo: both(b, BPO_OLD, BPO_NEW), bp: both(b, BP_OLD, BP_NEW), mesh: replaceAll(b, MESH_OLD, MESH_NEW),
}));
console.log('BP uasset BPO cast import (0 ok): ' + bpUa.bpo);
need('BP uasset self', bpUa.bp); need('BP uasset mesh', bpUa.mesh);
const bpUe = job(BP_OLD + '.uexp', BP_NEW + '.uexp', (b) => ({
  bpo: both(b, BPO_OLD, BPO_NEW), bp: both(b, BP_OLD, BP_NEW), mesh: replaceAll(b, MESH_OLD, MESH_NEW),
}));
console.log('BP uexp (0 ok): bpo=' + bpUe.bpo + ' bp=' + bpUe.bp + ' mesh=' + bpUe.mesh);

// assert the BPO still has the hull/frame items and the new display name
const outBpoA = fs.readFileSync(path.join(OUT, BPO_NEW + '.uasset')), outBpoE = fs.readFileSync(path.join(OUT, BPO_NEW + '.uexp'));
for (const s of ['ITEM_HullPlates', 'ITEM_LightweightFrame']) {
  if (outBpoA.indexOf(s, 0, 'latin1') === -1) throw new Error('BPO uasset missing ' + s);
}
if (outBpoE.indexOf('Blueprint Unit', 0, 'latin1') === -1) throw new Error('BPO uexp missing Blueprint Unit');

// leftover scan: old BP/BPO/mesh names must be gone (bare CommandModule stays on purpose)
for (const f of [BPO_NEW + '.uasset', BPO_NEW + '.uexp', BP_NEW + '.uasset', BP_NEW + '.uexp']) {
  const b = fs.readFileSync(path.join(OUT, f));
  for (const s of [BP_OLD, BPO_OLD, MESH_OLD]) {
    const n = b.indexOf(s, 0, 'latin1') + b.indexOf(u16(s), 0, 'latin1');
    if (n > -2) { console.log('LEFTOVER ' + s + ' in ' + f); bad = true; }
  }
}
if (bad) process.exit(2);
console.log('OK');
