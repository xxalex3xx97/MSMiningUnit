"""Re-parses the patched RSC_MiningLaser and asserts the Mining Unit is in the block array and the 2 vanilla entries are intact.
  python -I tests\\test_patch_research.py     runs the patch into a temp dir, checks it, then checks the committed src copy equals it
Independent of patch_research.py's own verify(): own small parser. Needs <repo>/legacy (patch source)."""
import os
import struct
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TOOL = os.path.join(ROOT, 'mods', 'MiningUnitBlockPak')
SCRIPT = os.path.join(TOOL, 'patch_research.py')
COMMITTED = os.path.join(TOOL, 'src', 'MainSequence', 'Content', 'Data', 'Research')
VAN_REFS = [-1, -2]
PKG = '/Game/Blueprints/BuildingBlocks/BPO_MinerModuleBlock'
u32 = lambda b, q: struct.unpack_from('<i', b, q)[0]


def load(d):
    return (open(os.path.join(d, 'RSC_MiningLaser.uasset'), 'rb').read(),
            open(os.path.join(d, 'RSC_MiningLaser.uexp'), 'rb').read())


def parse(a, e):
    nn, no, ic, io_ = u32(a, 96), u32(a, 100), u32(a, 128), u32(a, 132)
    names, p = [], no
    for _ in range(nn):
        n = u32(a, p)
        names.append(a[p + 4:p + 3 + n].decode())
        p += 8 + n
    assert p == io_
    imports = [struct.unpack_from('<8i', a, io_ + 32 * i) for i in range(ic)]
    c = e.index(b'\x80\x0d') - 4
    n, p = u32(e, c), c + 4
    refs = []
    for _ in range(n):
        assert e[p:p + 2] == b'\x80\x0d'
        m = e[p + 2]
        assert m in (0x1e, 0x1c)
        L = 3 + (m & 1 == 0) + (m & 2 == 0) + 4
        refs.append(u32(e, p + L - 4))
        p += L
    assert e[p:] == b'\0\0\0\0' + e[-4:]
    return names, imports, refs


def check(d):
    a, e = load(d)
    names, imports, refs = parse(a, e)
    assert len(refs) == 3, refs
    assert refs[:2] == VAN_REFS, refs
    for r in refs[:2]:
        assert names[imports[-r - 1][5]].startswith('BPO_MiningLaserBlock'), names[imports[-r - 1][5]]
    miner = imports[-refs[2] - 1]
    assert names[miner[5]] == 'BPO_MinerModuleBlock_C'
    assert names[miner[2]] == 'BlueprintGeneratedClass'
    pkg = imports[-miner[4] - 1]
    assert names[pkg[2]] == 'Package' and names[pkg[5]] == PKG
    # header / export consistency
    total, eo, plo, plc = u32(a, 44), u32(a, 124), u32(a, 256), u32(a, 252)
    assert len(a) == total == plo + 4 * plc + 20
    ser = struct.unpack_from('<q', a, eo + 28)[0]
    assert struct.unpack_from('<q', a, eo + 36)[0] == total
    assert len(e) == ser + 4 and u32(a, 236) == total + ser
    pre = [u32(a, plo + 4 * i) for i in range(plc)]
    assert pre == [-16, -17, -15, -13, -1, -2, refs[2], -5, -14], pre
    assert u32(a, eo + 84) == 7 and u32(a, eo + 88) == 2


def test_generated():
    d = tempfile.mkdtemp()
    r = subprocess.run([sys.executable, '-I', SCRIPT, d], capture_output=True, text=True)
    assert r.returncode == 0 and 'RESULT PASS' in r.stdout, r.stdout + r.stderr
    check(d)
    return d


def test_committed(gen_dir):
    assert os.path.isdir(COMMITTED), COMMITTED
    check(COMMITTED)
    assert load(COMMITTED) == load(gen_dir), 'committed src copy differs from generator output'


if __name__ == '__main__':
    g = test_generated()
    print('ok test_generated')
    test_committed(g)
    print('ok test_committed')
    print('ALL OK')
