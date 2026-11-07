"""Step 1 probe: DefaultMachines layout in a cooked BuildingBlockObject (.uasset/.uexp).
Usage: python -I dump_bpo_machines.py <path to BPO .uasset or .uexp dir/base name without ext>
Prints: name/import/export tables, export stride check (96 vs 104), machine class import refs,
and uexp bytes around every int32 equal to the connector class ref (-(import index + 1)).
"""
import struct
import sys

S = sys.argv[1]
a = open(S + '.uasset', 'rb').read()
e = open(S + '.uexp', 'rb').read()
i32 = lambda b, q: struct.unpack_from('<i', b, q)[0]
i64 = lambda b, q: struct.unpack_from('<q', b, q)[0]

# header offsets (UE5 FPackageFileSummary, offsets from the dump2.py probe, B = 60 + FolderName len)
B = 60 + i32(a, 52)
NN, NO = i32(a, B), i32(a, B + 4)
IC, IO = i32(a, B + 32), i32(a, B + 36)
EC, EO = i32(a, B + 24), i32(a, B + 28)
TOTAL = i32(a, 44)
PLC, PLO = i32(a, 266), i32(a, 270)
print('uasset', len(a), 'uexp', len(e), 'TotalHeaderSize', TOTAL, 'B', B)
print('names', NN, '@', NO, '| imports', IC, '@', IO, '| exports', EC, '@', EO, '| preload', PLC, '@', PLO)

names, p = [], NO
for _ in range(NN):
    n = i32(a, p)
    names.append(a[p + 4:p + 3 + n].decode('latin1'))
    p += 8 + n
assert p == IO, 'name table does not end at ImportOffset'

imports = []
for i in range(IC):
    t = struct.unpack_from('<8i', a, IO + 32 * i)
    imports.append(t)
    print('import %2d ref=%4d' % (i, -(i + 1)), t[0], t[2], t[5], '|', names[t[0]], names[t[2]], names[t[5]],
          'outer', t[4])

# machine class imports: ClassName BlueprintGeneratedClass ending in _C, package /Game/Blueprints/...
cls = [i for i, t in enumerate(imports) if names[t[2]] == 'BlueprintGeneratedClass' and names[t[5]].endswith('_C')
       and not names[t[5]].startswith('Default__')]
print('BlueprintGeneratedClass _C imports (candidate machine classes):', [(i, names[imports[i][5]]) for i in cls])

# export table: detect stride (96 or 104) by chaining SerialOffset/SerialSize and name validity
def parse(stride):
    out = []
    for k in range(EC):
        b = EO + stride * k
        if b + 44 > len(a):
            return None
        ser, off = i64(a, b + 28), i64(a, b + 36)
        out.append({'at': b, 'cls': i32(a, b), 'super': i32(a, b + 4), 'tmpl': i32(a, b + 8), 'outer': i32(a, b + 12),
                    'name': i32(a, b + 16), 'num': i32(a, b + 20), 'ser': ser, 'off': off})
    return out

stride = None
for st in (96, 104):
    ex = parse(st)
    if ex is None:
        continue
    ok = all(0 <= x['name'] < NN for x in ex)
    pos = TOTAL
    for x in ex:
        ok = ok and x['off'] == pos
        pos += x['ser']
    ok = ok and pos == len(a) + len(e) - 4
    print('stride %d chain/name check: %s' % (st, 'PASS' if ok else 'fail'))
    if ok and stride is None:
        stride = st
        exports = ex
assert stride is not None, 'no export stride matches the chain check'
print('export stride', stride, '(table end %d, preload @%d)' % (EO + stride * EC, PLO))
for k, x in enumerate(exports):
    print('export %d @%d class=%d super=%d tmpl=%d outer=%d name=%s(%d) SerialSize=%d SerialOffset=%d' % (
        k, x['at'], x['cls'], x['super'], x['tmpl'], x['outer'], names[x['name']], x['num'], x['ser'], x['off']))

# connector refs: the class import of BPO_LargeConnector_C (names[14] == 'BPO_LargeConnector_C')
hits_ref = {}
for i, t in enumerate(imports):
    if names[t[5]].endswith('_C') and not names[t[5]].startswith('Default__'):
        hits_ref[-(i + 1)] = names[t[5]]
print('ref values for machine-ish imports:', hits_ref)
for ref, label in sorted(hits_ref.items()):
    if label != 'BPO_LargeConnector_C':
        continue
    offs = [q for q in range(0, len(e) - 3, 4) if i32(e, q) == ref]
    print('uexp int32 == %d (%s) at: %s' % (ref, label, offs))
    for q in offs:
        print('  context -24..+20 around', q, e[max(0, q - 24):q + 20].hex(' '))
        print('  as i32 -24..+20:', [i32(e, r) for r in range(q - 24, q + 20, 4) if 0 <= r <= len(e) - 4])

print('uexp size %d, footer 4 bytes at %d' % (len(e), len(e) - 4))
