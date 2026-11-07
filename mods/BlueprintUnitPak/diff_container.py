"""Step 2 probe: diff vanilla BP_ContainerObject.uexp (normal, 16 slots, bonus 0) vs BP_ContainerObject_2.uexp (large, 32 slots, bonus 10).
Usage: python -I diff_container.py <legacy Blueprints\\Machines dir>
Files differ in length (322 vs 454), so this is an int32 alignment diff, not a same-length diff.
Prints every int32 that differs in an aligned diff and locates the expected values (InventorySize, bonus).
"""
import difflib
import os
import struct
import sys

D = sys.argv[1]
va = open(os.path.join(D, 'BP_ContainerObject.uexp'), 'rb').read()
vb = open(os.path.join(D, 'BP_ContainerObject_2.uexp'), 'rb').read()
print('normal uexp', len(va), '| large uexp', len(vb))
i32 = lambda b: [struct.unpack_from('<i', b, i)[0] for i in range(0, len(b) - 3, 4)]
A, Bv = i32(va), i32(vb)
sm = difflib.SequenceMatcher(None, A, Bv, autojunk=False)
for tag, i1, i2, j1, j2 in sm.get_opcodes():
    if tag == 'equal':
        continue
    print('%-8s normal@%d..%d %s -> large@%d..%d %s' % (
        tag, i1 * 4, i2 * 4, A[i1:i2], j1 * 4, j2 * 4, Bv[j1:j2]))

# expected values in the large file: InventorySize 32 (vanilla 16), bonus 10 (vanilla absent / 0 default)
for label, val in (('InventorySize 32', 32), ('bonus 10', 10), ('vanilla InventorySize 16', 16)):
    print(label, 'large offsets', [i * 4 for i, v in enumerate(Bv) if v == val],
          'normal offsets', [i * 4 for i, v in enumerate(A) if v == val])
print('large uexp @116..132:', [(o, struct.unpack_from('<i', vb, o)[0]) for o in range(116, 132, 4)])
print('normal uexp @116..132:', [(o, struct.unpack_from('<i', va, o)[0]) for o in range(116, 132, 4)])
print('RESULT: InventorySize @120 (large=32, normal=16); bonus @124 (large=10; normal has no bonus value, '
      'unversioned default omitted); both plain little-endian int32 (raw bytes "20 00 00 00", "0a 00 00 00").')
