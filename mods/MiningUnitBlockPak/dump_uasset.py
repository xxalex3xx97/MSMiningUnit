"""Dev dump of a cooked .uasset/.uexp pair: header offsets, names, imports, exports, preload deps, raw uexp.
Usage: python -I dump_uasset.py <path without extension>   (e.g. src\\RSC_MiningLaser)
"""
import struct, sys
S = sys.argv[1]
a = open(S + '.uasset', 'rb').read(); e = open(S + '.uexp', 'rb').read()
u = lambda b, q: struct.unpack_from('<i', b, q)[0]
q8 = lambda b, q: struct.unpack_from('<q', b, q)[0]
print('uasset', len(a), 'uexp', len(e))
print('hdr i32 @44,97,101,121,125,129,133,237,253,257:', [u(a, x) for x in (44, 97, 101, 121, 125, 129, 133, 237, 253, 257)])
no = u(a, 101)
print('header i32 with value in [100, %d]:' % (len(a) + len(e)), [(x, u(a, x)) for x in range(0, no - 3) if 100 <= u(a, x) <= len(a) + len(e)])
nn = u(a, 97); p = no; names = []
for i in range(nn):
    n = u(a, p); names.append(a[p + 4:p + 3 + n].decode('latin1')); p += 8 + n
print('names end at', p, '(ImportOffset %d)' % u(a, 133))
for i, n in enumerate(names): print('name', i, n)
ic, io_ = u(a, 129), u(a, 133)
for i in range(ic):
    t = struct.unpack_from('<8i', a, io_ + 32 * i)
    print('import', i, t, '|', names[t[0]], names[t[2]], names[t[5]])
ec, eo = u(a, 121), u(a, 125)
for i in range(ec):
    b = eo + 104 * i
    print('export', i, struct.unpack_from('<8i', a, b), 'ser', q8(a, b + 28), 'off', q8(a, b + 36), 'tail', struct.unpack_from('<14i', a, b + 44))
plc, plo = u(a, 253), u(a, 257)
print('preload', plc, 'at', plo, [u(a, plo + 4 * i) for i in range(plc)])
print('tail bytes after preload:', plo + 4 * plc, 'to', len(a), a[plo + 4 * plc:].hex())
print('uexp hex:')
for i in range(0, len(e), 16):
    print('%04x' % i, e[i:i + 16].hex(' ', 4), ' ', ''.join(chr(c) if 32 <= c < 127 else '.' for c in e[i:i + 16]))
print('uexp as i32:', [(i, u(e, i)) for i in range(0, len(e) - 3, 4)])
