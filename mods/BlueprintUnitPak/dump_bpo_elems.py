"""Probe: DefaultMachines element layout in every BuildingBlocks .uasset/.uexp pair (read-only).
Usage: python -I dump_bpo_elems.py
Prints to stdout and writes probe_bpo_elems.txt next to this script.
Import/export table parsing copied from dump_bpo_machines.py.
"""
import glob
import os
import struct

SRC = 'F:/MSDemoMods/legacy/MainSequence/Content/Blueprints/BuildingBlocks'
OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'probe_bpo_elems.txt')
LINES = []


def out(s=''):
    LINES.append(s)
    print(s)


def i32(b, q):
    return struct.unpack_from('<i', b, q)[0]


def i64(b, q):
    return struct.unpack_from('<q', b, q)[0]


def hx(b):
    return b.hex(' ')


def parse(base):
    a = open(base + '.uasset', 'rb').read()
    e = open(base + '.uexp', 'rb').read()
    B = 60 + i32(a, 52)
    NN, NO = i32(a, B), i32(a, B + 4)
    IC, IO = i32(a, B + 32), i32(a, B + 36)
    EC, EO = i32(a, B + 24), i32(a, B + 28)
    TOTAL = i32(a, 44)
    names, p = [], NO
    for _ in range(NN):
        n = i32(a, p)
        names.append(a[p + 4:p + 3 + n].decode('latin1'))
        p += 8 + n
    assert p == IO, 'name table does not end at ImportOffset'
    imports = [struct.unpack_from('<8i', a, IO + 32 * i) for i in range(IC)]
    exports = None
    for stride in (96, 104):
        if EO + stride * EC > len(a):
            continue
        ex = []
        for k in range(EC):
            b = EO + stride * k
            ex.append({'cls': i32(a, b), 'name': i32(a, b + 16), 'num': i32(a, b + 20),
                       'ser': i64(a, b + 28), 'off': i64(a, b + 36)})
        pos, ok = TOTAL, True
        for x in ex:
            ok = ok and 0 <= x['name'] < NN and x['off'] == pos
            pos += x['ser']
        if ok and pos == len(a) + len(e) - 4:
            exports = ex
            break
    assert exports is not None, 'no export stride matches the chain check'
    return a, e, names, imports, TOTAL, exports


def probe(stem):
    base = os.path.join(SRC, stem)
    a, e, names, imports, TOTAL, ex = parse(base)
    assert len(ex) >= 2, 'fewer than 2 exports'
    e1 = ex[1]
    start = e1['off'] - TOTAL
    end = min(start + e1['ser'], len(e))
    own = 'BP_%s_C' % stem.split('_', 1)[1]

    # machine refs: ref = -(import_index+1); BlueprintGeneratedClass imports ending _C, minus own class and BPC_*
    mach = {}
    for i, t in enumerate(imports):
        obj = names[t[5]]
        if names[t[2]] != 'BlueprintGeneratedClass' or not obj.endswith('_C') or obj.startswith('Default__'):
            continue
        if obj == own or obj.startswith('BPC_'):
            continue
        mach[-(i + 1)] = obj

    # unaligned scan of export 1 (CDO) inside uexp
    hits = []
    for q in range(max(start, 0), end - 3):
        v = i32(e, q)
        if v in mach:
            hits.append((q, v, mach[v]))
    K = len(hits)
    pos_list = [h[0] for h in hits]

    if K < 2 and stem != 'BPO_CommandModule':
        out('%s: hits=%d %s' % (stem, K, [(p, n) for p, _, n in hits]))
        return K

    out('== %s own=%s export1 start=%d size=%d' % (stem, own, start, e1['ser']))
    out('machine refs=%s' % mach)
    out('hits=%d %s' % (K, [(p, v, n) for p, v, n in hits]))
    if K >= 2:
        gaps = [pos_list[j + 1] - pos_list[j] for j in range(K - 1)]
        st = gaps[0]
        first = pos_list[0]
        out('gaps=%s stride cand (gap0)=%d' % (gaps, st))
        out('hex -16..first: %s' % hx(e[max(0, first - 16):first]))
        out('hex first..first+%d: %s' % (st + 8, hx(e[first:first + st + 8])))
    elif K == 1:
        first = pos_list[0]
        out('hex -16..first: %s' % hx(e[max(0, first - 16):first]))
        out('hex first..first+48: %s' % hx(e[first:first + 48]))
    if K:
        cands = []
        for h in range(7):
            q = first - 4 - h
            if q < 0:
                continue
            v = i32(e, q)
            cands.append('h=%d@%d:%d%s' % (h, q, v, ' <== =K' if v == K else ''))
        out('count cands at first-4-h: ' + '  '.join(cands))

    if stem == 'BPO_CommandModule':
        pos, frags = start, []
        while len(frags) < 64:
            v = struct.unpack_from('<H', e, pos)[0]
            pos += 2
            frags.append(v)
            out('frag @%d u16=0x%04x skip=%d zeroes=%d last=%d valuenum=%d' % (
                pos - 2, v, v & 0x7f, (v & 0x80) >> 7, (v & 0x100) >> 8, v >> 9))
            if v & 0x100:
                break
        total = sum(v >> 9 for v in frags)
        zero = any(v & 0x80 for v in frags)
        zsz = (1 if total <= 8 else 2 if total <= 16 else 4) if zero else 0
        out('total valuenum=%d any_zeroes=%s zero-mask size guess=%d bytes @%d: %s' % (
            total, zero, zsz, pos, hx(e[pos:pos + zsz])))
        out('uexp[364:460]:')
        for r in range(364, 460, 16):
            out('  %4d: %s' % (r, hx(e[r:min(r + 16, 460)])))
    return K


stems = sorted(os.path.basename(p)[:-len('.uasset')] for p in glob.glob(os.path.join(SRC, '*.uasset')))
pairs = [s for s in stems if os.path.exists(os.path.join(SRC, s + '.uexp'))]
res = {}
for s in pairs:
    try:
        res[s] = probe(s)
    except Exception as err:
        out('%s: SKIP %r' % (s, err))
        res[s] = None

out('')
out('SUMMARY (%d pairs)' % len(pairs))
for label, pred in (('0 hits', lambda k: k == 0), ('1 hit', lambda k: k == 1), ('2+ hits', lambda k: k >= 2)):
    got = [s for s in pairs if res[s] is not None and pred(res[s])]
    out('%s (%d): %s' % (label, len(got), ' '.join(got)))
skipped = [s for s in pairs if res[s] is None]
out('SKIPPED (%d): %s' % (len(skipped), ' '.join(skipped)))

with open(OUT, 'w', encoding='utf-8') as f:
    f.write('\n'.join(LINES) + '\n')
print('wrote', OUT)
