#!/usr/bin/env python3
# FILE: tools/compat_dump.py
# WHAT: native signature generator and checker (package E). Reads the game exe as bytes (no copy), finds .text, and builds a
#       byte pattern per target RVA. Fixed bytes are literal; wildcards are only RIP-relative disp32 and rel32 of call/jmp/jcc.
#       gen <exe> system=0x.. setlevel=0x.. apply=0x..   print NAT_PAT_* lines (unique in .text, at the target RVA)
#       verify <exe> <mb_natives.hpp>                      count matches of each NAT_PAT_* string in .text (expect 1 each)
# WHY: game patches move functions; the mod finds natives by pattern and must fail closed when a pattern is not unique.
# DEPENDS ON: Python 3 stdlib + capstone (pip install capstone). PE parsed by hand (no pefile).
# USED BY: mods/MiningBenchMod mb_natives.hpp (NAT_PAT_* strings, later slice). Run by hand after a game update.
import datetime
import re
import struct
import sys

from capstone import Cs, CS_ARCH_X86, CS_MODE_64
from capstone.x86 import X86_OP_MEM, X86_OP_IMM, X86_REG_RIP

MIN_LEN = 24
MAX_LEN = 96
CODE_READ = 160
HASH_LEN = 64
MASK64 = 0xFFFFFFFFFFFFFFFF


def die(msg, code=2):
    print(msg, file=sys.stderr)
    sys.exit(code)


class PE:
    def __init__(self, path):
        with open(path, 'rb') as f:
            self.d = f.read()
        d = self.d
        if d[:2] != b'MZ':
            die('not a PE file: ' + path)
        lf = struct.unpack_from('<I', d, 0x3C)[0]
        if d[lf:lf + 4] != b'PE\0\0':
            die('bad PE signature')
        nsec = struct.unpack_from('<H', d, lf + 6)[0]
        self.timestamp = struct.unpack_from('<I', d, lf + 8)[0]
        opt_size = struct.unpack_from('<H', d, lf + 20)[0]
        opt = lf + 24
        if struct.unpack_from('<H', d, opt)[0] != 0x20B:
            die('not PE32+ (x64) image')
        self.size_of_image = struct.unpack_from('<I', d, opt + 56)[0]
        self.checksum = struct.unpack_from('<I', d, opt + 64)[0]
        self.sections = []
        for i in range(nsec):
            base = lf + 24 + opt_size + 40 * i
            name = d[base:base + 8].rstrip(b'\0').decode('ascii', 'replace')
            vsize, va, rawsz, rawptr = struct.unpack_from('<IIII', d, base + 8)
            self.sections.append((name, va, vsize, rawsz, rawptr))
        text = [s for s in self.sections if s[0] == '.text']
        if not text:
            die('no .text section')
        _, self.text_va, _, self.text_rawsz, self.text_raw = text[0]
        self.text = d[self.text_raw:self.text_raw + self.text_rawsz]

    def info(self):
        when = datetime.datetime.fromtimestamp(self.timestamp, datetime.timezone.utc)
        print(f'file size      : {len(self.d)} bytes')
        print(f'TimeDateStamp  : 0x{self.timestamp:08x} ({when:%Y-%m-%d %H:%M:%S} UTC)')
        print(f'SizeOfImage    : 0x{self.size_of_image:x}')
        print(f'CheckSum       : 0x{self.checksum:08x}')
        print(f'.text          : va 0x{self.text_va:x} raw 0x{self.text_raw:x} size 0x{self.text_rawsz:x}')


def fnv1a64(b):
    h = 0xCBF29CE484222325
    for x in b:
        h = ((h ^ x) * 0x100000001B3) & MASK64
    return h


def build_rx(tokens):
    # WHAT: lookahead regex, so overlapping matches are counted too. None token = any byte.
    body = b''.join(b'.' if t is None else re.escape(bytes([t])) for t in tokens)
    return re.compile(b'(?=(' + body + b'))', re.S)


def count_matches(rx, text, cap=2):
    hits = []
    for m in rx.finditer(text):
        hits.append(m.start())
        if len(hits) >= cap:
            break
    return hits


def wild_tokens(insns):
    toks = []
    for ins in insns:
        b = bytes(ins.bytes)
        wild = set()
        # RIP-relative memory operand: wildcard its disp32
        if ins.disp_size and any(op.type == X86_OP_MEM and op.mem.base == X86_REG_RIP for op in ins.operands):
            wild.update(range(ins.disp_offset, ins.disp_offset + ins.disp_size))
        # call/jmp/jcc with rel32 (5 or 6 byte form, immediate operand): wildcard the last 4 bytes
        if (ins.mnemonic.startswith(('call', 'j')) and len(b) >= 5
                and any(op.type == X86_OP_IMM for op in ins.operands)):
            wild.update(range(len(b) - 4, len(b)))
        toks += [None if i in wild else b[i] for i in range(len(b))]
    return toks


def tok_text(toks):
    return ' '.join('?' if t is None else f'{t:02X}' for t in toks)


def parse_tok(text):
    out = []
    for t in text.split():
        out.append(None if t in ('?', '??') else int(t, 16))
    return out


def gen_one(md, pe, rva):
    toff = rva - pe.text_va
    if toff < 0 or toff >= len(pe.text):
        die(f'rva 0x{rva:x} is outside .text')
    code = pe.text[toff:toff + CODE_READ]
    insns = list(md.disasm(code, rva))
    picked = []
    total = 0
    for ins in insns:
        picked.append(ins)
        total += ins.size
        if total < MIN_LEN:
            continue
        if total > MAX_LEN:
            break
        toks = wild_tokens(picked)
        if count_matches(build_rx(toks), pe.text) == [toff]:
            return toks
    return None


def cmd_gen(exe, targets):
    pe = PE(exe)
    pe.info()
    md = Cs(CS_ARCH_X86, CS_MODE_64)
    md.detail = True
    for name, rva in targets:
        toks = gen_one(md, pe, rva)
        if toks is None:
            print(f'FAIL {name}')
            sys.exit(2)
        code64 = pe.text[rva - pe.text_va:rva - pe.text_va + HASH_LEN]
        print(f'constexpr const char* NAT_PAT_{name} = "{tok_text(toks)}";   '
              f'// rva 0x{rva:x} len {len(toks)} unique fnv1a64_64=0x{fnv1a64(code64):016x}')


def cmd_verify(exe, header):
    pe = PE(exe)
    pe.info()
    with open(header, encoding='utf-8') as f:
        src = f.read()
    pats = re.findall(r'NAT_PAT_(\w+)\s*=\s*"([^"]*)"', src)
    if not pats:
        die('no NAT_PAT_ strings in ' + header)
    bad = 0
    for name, text in pats:
        hits = count_matches(build_rx(parse_tok(text)), pe.text)
        rva = f'0x{pe.text_va + hits[0]:x}' if len(hits) == 1 else '-'
        print(f'NAT_PAT_{name} matches={len(hits)} rva={rva}')
        if len(hits) != 1:
            bad += 1
    sys.exit(0 if bad == 0 else 2)


def usage():
    die('usage: compat_dump.py gen <exe> system=0x.. setlevel=0x.. apply=0x..\n'
        '       compat_dump.py verify <exe> <mb_natives.hpp>')


def main(argv):
    if len(argv) < 3:
        usage()
    cmd, exe = argv[1], argv[2]
    if cmd == 'gen':
        targets = []
        for kv in argv[3:]:
            if '=' not in kv:
                usage()
            k, v = kv.split('=', 1)
            targets.append((k.upper(), int(v, 0)))
        if not targets:
            usage()
        cmd_gen(exe, targets)
    elif cmd == 'verify' and len(argv) == 4:
        cmd_verify(exe, argv[3])
    else:
        usage()


if __name__ == '__main__':
    main(sys.argv)
