# S1 step 5: append the hidden container machine (BP_BPUnitStoreObj_01) as the 2nd DefaultMachines element of
# stage BPO_BlueprintUnit (clone of vanilla BPO_CommandModule). Patches the .uasset/.uexp in place.
# Usage: python -I patch_bpo_machine.py <DIR with BPO_BlueprintUnit.uasset/.uexp>
#        python -I patch_bpo_machine.py --walk <base path without ext>   (print DefaultMachines walk only)
# Layout is parsed and asserted, not hardcoded. verify() re-parses the output bytes before anything is written.
# ponytail: grid cell fixed at X,Y below (0,0); no real placement logic. In-game may need another cell.
import struct
import sys
import os
import hashlib

X, Y = 0, 0  # grid cell of the new container (default; vanilla connector is (-2,0)). Tune in-game if needed.
NEW_PKG = '/Game/Blueprints/Machines/BP_BPUnitStoreObj_01'
NEW_CLS = 'BP_BPUnitStoreObj_01_C'
CONNECTOR = 'BPO_LargeConnector_C'
DM = 374            # uexp offset of DefaultMachines count (vanilla: 1)
EXP_STRIDE = 96     # export table entry size in this file (chain-checked below)
ELEM_NEW = 22       # bytes of the new element

i32 = lambda b, q: struct.unpack_from('<i', b, q)[0]
i64 = lambda b, q: struct.unpack_from('<q', b, q)[0]


def fs(s):  # FString name entry + 4 zero bytes hash (same as patch_research.py)
    b = s.encode() + b'\0'
    return struct.pack('<i', len(b)) + b + b'\0\0\0\0'


def read_names(buf, n, p):
    out = []
    for _ in range(n):
        l = i32(buf, p)
        out.append(buf[p + 4:p + 3 + l].decode('latin1'))
        p += 8 + l
    return out, p


def walk(e):
    # DefaultMachines: count, then per element: 80 0b 0c | class ref i32 | 80 07 tmask | X i32 | Y i32 | [Rot i32 if !(tmask&4)] | cfg count i32 (0)
    # Height is omitted (tmask&2) in every element seen; MachineID/PaletteID omitted (struct mask 0c).
    n = i32(e, DM)
    p = DM + 4
    out = []
    for _ in range(n):
        assert e[p:p + 2] == b'\x80\x0b' and e[p + 2] == 0x0c, ('struct header', p)
        ref = i32(e, p + 3)
        p += 7
        assert e[p:p + 2] == b'\x80\x07' and e[p + 2] in (0x02, 0x06), ('transform header', p)
        tm = e[p + 2]
        p += 3
        x, y = i32(e, p), i32(e, p + 4)
        p += 8
        rot = None
        if not tm & 0x04:
            rot = i32(e, p)
            p += 4
        cfg = i32(e, p)
        assert cfg == 0, ('config bytes not handled', p)
        p += 4
        out.append(dict(ref=ref, x=x, y=y, rot=rot, tmask=tm))
    return out, p, i32(e, p)


if sys.argv[1] == '--walk':
    base = sys.argv[2]
    els, end, nxt = walk(open(base + '.uexp', 'rb').read())
    print('elements', len(els), 'end', end, 'next int32', nxt)
    for el in els:
        print('  ', el)
    sys.exit(0)

BASE = os.path.join(sys.argv[1], 'BPO_BlueprintUnit')
a = open(BASE + '.uasset', 'rb').read()
e = open(BASE + '.uexp', 'rb').read()

# ---- vanilla layout (parsed + asserted) ----
B = 60 + i32(a, 52)
NN, NO, IC, IO = i32(a, B), i32(a, B + 4), i32(a, B + 32), i32(a, B + 36)
EC, EO = i32(a, B + 24), i32(a, B + 28)
PLC, PLO = i32(a, B + 156), i32(a, B + 160)
TOT = i32(a, 44)
assert TOT == len(a) and EC == 2 and IO + 32 * IC == EO and i32(a, B + 12) == IO
assert i32(a, B + 44) == i32(a, B + 52) == i32(a, B + 60) == EO + EXP_STRIDE * EC
assert i32(a, B + 136) == PLO - 4
names, p = read_names(a, NN, NO)
assert p == IO, 'name table does not end at ImportOffset'
ix = names.index
for s in ('/Script/CoreUObject', 'Package', '/Script/Engine', 'BlueprintGeneratedClass', CONNECTOR):
    assert s in names, s
assert NEW_PKG not in names and NEW_CLS not in names


def export(k):
    b = EO + EXP_STRIDE * k
    return dict(at=b, ser=i64(a, b + 28), off=i64(a, b + 36), fed=i32(a, b + 76),
                sb=i32(a, b + 80), cb=i32(a, b + 84), sbc=i32(a, b + 88), cbc=i32(a, b + 92))


X0, X1 = export(0), export(1)
assert X0['off'] == TOT and X1['off'] == TOT + X0['ser'] and TOT + X0['ser'] + X1['ser'] == TOT + len(e) - 4
assert i32(a, B + 140) == TOT + X0['ser'] + X1['ser'], 'BulkDataStart layout'
assert X0['fed'] == 0 and X1['fed'] == X0['sb'] + X0['cb'] + X0['sbc'] + X0['cbc']
assert X1['fed'] + X1['sb'] + X1['cb'] + X1['sbc'] + X1['cbc'] == PLC
PL = [i32(a, PLO + 4 * i) for i in range(PLC)]

conn_idx = [i for i in range(IC) if names[struct.unpack_from('<8i', a, IO + 32 * i)[5]] == CONNECTOR]
assert len(conn_idx) == 1
CONN_REF = -(conn_idx[0] + 1)
cb0 = X1['fed'] + X1['sb']
assert CONN_REF in PL[cb0:cb0 + X1['cb']], 'connector not in export 1 CreateBefore block'

els0, end0, nxt0 = walk(e)
assert len(els0) == 1 and els0[0]['ref'] == CONN_REF and end0 == 404 and nxt0 == 20
assert X1['at'] and X1['off'] - TOT <= end0 < X1['off'] - TOT + X1['ser']

# ---- new names / imports / preload ----
NEWREF = -(IC + 2)                    # new class import index IC+1 -> ref -(IC+2)
newnames = fs(NEW_PKG) + fs(NEW_CLS)
A = len(newnames)
imp = struct.pack('<8i', ix('/Script/CoreUObject'), 0, ix('Package'), 0, 0, NN, 0, 0)
imp += struct.pack('<8i', ix('/Script/Engine'), 0, ix('BlueprintGeneratedClass'), 0, -(IC + 1), NN + 1, 0, 0)
PIDX = cb0 + X1['cb']                 # preload index = end of export 1 CreateBefore block (where the connector sits)
P3 = PLO + 4 * PIDX
ins = [(IO, A), (EO, len(imp)), (P3, 4)]


def sh(v):
    return v + sum(l for pos, l in ins if v >= pos)


TOTN = sh(TOT)
ELEM_BYTES = b'\x80\x0b\x0c' + struct.pack('<i', NEWREF) + b'\x80\x07\x06' + struct.pack('<ii', X, Y) + struct.pack('<i', 0)
assert len(ELEM_BYTES) == ELEM_NEW

# ---- uasset header ----
h = bytearray(a[:NO])
OFF_FIELDS = [44, B + 12, B + 28, B + 36, B + 44, B + 52, B + 60, B + 136, B + 160]
for q in OFF_FIELDS:
    struct.pack_into('<i', h, q, sh(i32(a, q)))
struct.pack_into('<i', h, B, NN + 2)
struct.pack_into('<i', h, B + 32, IC + 2)
struct.pack_into('<i', h, B + 156, PLC + 1)
struct.pack_into('<i', h, B + 140, TOTN + X0['ser'] + X1['ser'] + ELEM_NEW)   # BulkDataStart
h = bytes(h)

ex = bytearray(a[EO:EO + EXP_STRIDE * EC])
for k, x in enumerate((X0, X1)):
    rel = x['off'] - TOT
    struct.pack_into('<q', ex, EXP_STRIDE * k + 36, sh(x['off']) + (ELEM_NEW if rel > end0 else 0))
struct.pack_into('<q', ex, EXP_STRIDE + 28, X1['ser'] + ELEM_NEW)
struct.pack_into('<i', ex, EXP_STRIDE + 84, X1['cb'] + 1)
ex = bytes(ex)

out_a = (h + a[NO:IO] + newnames + a[IO:EO] + imp + ex + a[EO + EXP_STRIDE * EC:P3]
         + struct.pack('<i', NEWREF) + a[P3:])

# ---- uexp: count 1->2, insert element at the end of element 1 (uexp 404) ----
out_e = bytearray(e[:DM + 4] + e[DM + 4:end0] + ELEM_BYTES + e[end0:])
struct.pack_into('<i', out_e, DM, 2)
out_e = bytes(out_e)


def verify(oa, oe):
    fail = []

    def chk(label, cond):
        print(('ok   ' if cond else 'FAIL ') + label)
        if not cond:
            fail.append(label)

    tot = i32(oa, 44)
    chk('uasset length %d == TotalHeaderSize %d' % (len(oa), tot), len(oa) == tot == TOTN)
    Bo = 60 + i32(oa, 52)
    nn, no, ic, io_ = i32(oa, Bo), i32(oa, Bo + 4), i32(oa, Bo + 32), i32(oa, Bo + 36)
    ec, eo, plc, plo = i32(oa, Bo + 24), i32(oa, Bo + 28), i32(oa, Bo + 156), i32(oa, Bo + 160)
    chk('NameCount %d == %d' % (nn, NN + 2), nn == NN + 2)
    chk('NameOffset == %d' % NO, no == NO)
    chk('ExportCount 2', ec == 2)
    chk('ImportCount %d == %d' % (ic, IC + 2), ic == IC + 2)
    chk('PreloadCount %d == %d' % (plc, PLC + 1), plc == PLC + 1)
    onames, p2 = read_names(oa, nn, no)
    chk('name table ends at ImportOffset (%d)' % io_, p2 == io_)
    chk('SoftObjectPathsOffset == ImportOffset', i32(oa, Bo + 12) == io_)
    chk('vanilla names kept', onames[:NN] == names and oa[NO:IO] == a[NO:IO])
    chk('name[%d] == %s' % (NN, NEW_PKG), onames[NN] == NEW_PKG)
    chk('name[%d] == %s' % (NN + 1, NEW_CLS), onames[NN + 1] == NEW_CLS)
    chk('ExportOffset == ImportOffset + 32*ImportCount', eo == io_ + 32 * ic)
    chk('PreloadOffset == ExportOffset + 2*96 + 12 (gap kept)', plo == eo + EXP_STRIDE * ec + (PLO - EO - EXP_STRIDE * EC))
    chk('TotalHeaderSize == PreloadOffset + 4*PreloadCount + tail', tot == plo + 4 * plc + (TOT - PLO - 4 * PLC))
    chk('Depends/SoftPkg/Searchable offsets == ExportEnd', i32(oa, Bo + 44) == i32(oa, Bo + 52) == i32(oa, Bo + 60) == eo + EXP_STRIDE * ec)
    chk('AssetRegistry offset == PreloadOffset-4', i32(oa, Bo + 136) == plo - 4)
    chk('vanilla import bytes unchanged', oa[io_:io_ + 32 * IC] == a[IO:IO + 32 * IC])
    pkg = struct.unpack_from('<8i', oa, io_ + 32 * IC)
    cls = struct.unpack_from('<8i', oa, io_ + 32 * (IC + 1))
    chk('import[%d] Package %s, outer 0' % (IC, NEW_PKG),
        onames[pkg[0]] == '/Script/CoreUObject' and onames[pkg[2]] == 'Package' and pkg[4] == 0 and onames[pkg[5]] == NEW_PKG)
    chk('import[%d] BlueprintGeneratedClass %s, outer %d' % (IC + 1, NEW_CLS, -(IC + 1)),
        onames[cls[0]] == '/Script/Engine' and onames[cls[2]] == 'BlueprintGeneratedClass'
        and cls[4] == -(IC + 1) and onames[cls[5]] == NEW_CLS)
    eall = [(i32(oa, eo + 96 * k + 76), i32(oa, eo + 96 * k + 80), i32(oa, eo + 96 * k + 84),
             i32(oa, eo + 96 * k + 88), i32(oa, eo + 96 * k + 92)) for k in range(ec)]
    ser = [i64(oa, eo + 96 * k + 28) for k in range(ec)]
    off = [i64(oa, eo + 96 * k + 36) for k in range(ec)]
    chk('export sizes %s == [104, %d]' % (ser, X1['ser'] + ELEM_NEW), ser == [X0['ser'], X1['ser'] + ELEM_NEW])
    chk('export SerialOffset chain %s' % off, off == [tot, tot + ser[0]])
    chk('export 1 CreateBefore count %d == %d' % (eall[1][2], X1['cb'] + 1), eall[1][2] == X1['cb'] + 1)
    chk('export fields FED/SB/SBC/CBC unchanged', eall == [(X0['fed'], X0['sb'], X0['cb'], X0['sbc'], X0['cbc']),
                                                           (X1['fed'], X1['sb'], X1['cb'] + 1, X1['sbc'], X1['cbc'])])
    for k in range(2):  # all export fields except SerialSize(28), SerialOffset(36), CreateBefore count(84) unchanged
        o, v = eo + 96 * k, EO + 96 * k
        chk('export %d entry fields unchanged except size/offset/CB' % k,
            oa[o:o + 28] == a[v:v + 28] and oa[o + 44:o + 84] == a[v + 44:v + 84] and oa[o + 88:o + 96] == a[v + 88:v + 96])
    pref = [i32(oa, plo + 4 * i) for i in range(plc)]
    want = PL[:PIDX] + [NEWREF] + PL[PIDX:]
    chk('preload == vanilla with %d inserted at index %d' % (NEWREF, PIDX), pref == want)
    bulk = i32(oa, Bo + 140)
    chk('BulkDataStart %d == TotalHeaderSize + sum(SerialSize)' % bulk, bulk == tot + sum(ser))
    chk('uexp length %d == sum(SerialSize) + 4 footer' % len(oe), len(oe) == sum(ser) + 4)
    els, end, nxt = walk(oe)
    chk('DefaultMachines count 2', i32(oe, DM) == 2)
    chk('element 1 unchanged (ref %d)' % CONN_REF, els[0] == els0[0])
    chk('element 2 == ref %d, X=%d Y=%d, no rot, tmask 6' % (NEWREF, X, Y),
        len(els) == 2 and els[1] == dict(ref=NEWREF, x=X, y=Y, rot=None, tmask=6))
    chk('walk end 426, next int32 == 20 (got %d)' % nxt, end == 426 and nxt == 20)
    chk('uexp prefix up to count unchanged', oe[:DM] == e[:DM])
    chk('uexp element 1 bytes unchanged', oe[DM + 4:404] == e[DM + 4:404])
    chk('uexp new element bytes at 404', oe[404:426] == ELEM_BYTES)
    chk('uexp tail (from 404) unchanged', oe[426:] == e[404:])
    diff = {q for q in range(NO) if oa[q] != a[q]}
    allowed = {q + i for q in OFF_FIELDS + [B, B + 32, B + 140, B + 156] for i in range(4)}
    chk('header bytes changed only at patched positions %s' % sorted(diff - allowed)[:8], diff <= allowed)
    print('RESULT ' + ('PASS' if not fail else 'FAIL %d' % len(fail)))
    return not fail


ok = verify(out_a, out_e)
print('uasset %d -> %d   uexp %d -> %d' % (len(a), len(out_a), len(e), len(out_e)))
if ok:
    open(BASE + '.uasset', 'wb').write(out_a)
    open(BASE + '.uexp', 'wb').write(out_e)
    for fn in ('BPO_BlueprintUnit.uasset', 'BPO_BlueprintUnit.uexp'):
        print(hashlib.sha256(open(os.path.join(sys.argv[1], fn), 'rb').read()).hexdigest(), fn)
sys.exit(0 if ok else 1)
