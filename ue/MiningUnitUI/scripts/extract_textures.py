"""Dev-time: extracts game UI textures to art\\textures\\*.png; make_assets.py imports them as our shipped copies.

Usage (python 3, Pillow, numpy not needed):
  python -I extract_textures.py                 # the 7 header textures -> art\\textures\\<name>.png
  python -I extract_textures.py T_UI_X T_UI_Footer   # named textures
  python -I extract_textures.py --all            # every T_UI_* inline texture (+ art\\textures\\sizes.txt for all)

Cooked layout (UE5, inline mips, verified on T_UI_Header_L / T_UI_X): FTexturePlatformData fields in order:
SizeX i32, SizeY i32, PackedData u32 (low 28 bits = NumSlices), FString PixelFormat ("PF_DXT5"), FirstMip i32,
NumMips i32. Mip 0 is the first (largest) mip. The inline mip chain sits right before a trailing copy of
SizeX/SizeY/PackedData, then the 4-byte package tag C1 83 2A 9E.
Textures with a .ubulk: mip0 is read from the .ubulk (assumed offset 0).
"""
import glob
import io
import os
import re
import struct
import sys

from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
MODS_ROOT = os.environ.get("MS_MODS_ROOT", r"F:\MSDemoMods")  # repo root, holds legacy\ (game cook)
UI_DIR = os.environ.get("MS_TEX_DIR", os.path.join(MODS_ROOT, "legacy", "MainSequence", "Content", "Materials", "UI_Textures"))
OUT_DIR = os.path.join(os.path.dirname(HERE), "art", "textures")
TAG = bytes.fromhex("c1832a9e")

HEADERS = ["T_UI_Header_L", "T_UI_Header_R", "T_UI_Header_Bar", "T_UI_Header_Bar_R",
           "T_UI_Header_R_In", "T_UI_Header_R_Out", "T_UI_X"]
PF_RE = re.compile(rb"PF_[A-Za-z0-9_]+\x00")


def block_bytes(fmt, w, h):
    if fmt in ("PF_DXT1",):
        return max(1, (w + 3) // 4) * max(1, (h + 3) // 4) * 8
    if fmt in ("PF_DXT5",):
        return max(1, (w + 3) // 4) * max(1, (h + 3) // 4) * 16
    if fmt == "PF_B8G8R8A8":
        return w * h * 4
    if fmt == "PF_FloatRGBA":
        return w * h * 8
    raise ValueError("unsupported pixel format " + fmt)


def float_rgba_to_rgba8(data, w, h):
    """PF_FloatRGBA: 4 little-endian half floats per pixel. Clamp 0..1, *255 (ponytail: no tone mapping)."""
    vals = struct.unpack("<%de" % (w * h * 4), data[:w * h * 8])
    return bytes(min(255, max(0, int(round(v * 255)))) for v in vals)


def dds_wrap(fourcc, w, h, data):
    """Wrap raw BC blocks in a DDS header so Pillow's DDS plugin decodes them."""
    hdr = struct.pack("<4sIIIIIII", b"DDS ", 124, 0x81007, h, w, len(data), 0, 1)
    hdr += b"\x00" * 44  # reserved1[11]
    hdr += struct.pack("<II4sIIIII", 32, 4, fourcc, 0, 0, 0, 0, 0)  # pixel format (FOURCC)
    hdr += struct.pack("<IIIII", 0x1000, 0, 0, 0, 0)  # caps
    return hdr + data


def parse(name):
    """Return dict(name, w, h, fmt, mips, inline, data) for one texture; data = mip0 bytes or None."""
    base = os.path.join(UI_DIR, name)
    uexp_p, ubulk_p = base + ".uexp", base + ".ubulk"
    if not os.path.exists(uexp_p):
        raise FileNotFoundError(uexp_p)
    d = open(uexp_p, "rb").read()
    if d[-4:] != TAG:
        raise ValueError("no package tag at end of uexp")
    m = PF_RE.search(d)
    if not m:
        raise ValueError("no PixelFormat string")
    p = m.start()
    w, h = struct.unpack_from("<ii", d, p - 16)
    fmt = m.group()[:-1].decode("ascii")
    mips = struct.unpack_from("<i", d, m.end() + 4)[0]
    sizes = []
    mw, mh = w, h
    for _ in range(max(1, mips)):
        sizes.append(block_bytes(fmt, mw, mh))
        mw, mh = max(1, mw // 2), max(1, mh // 2)
    total = sum(sizes)
    info = dict(name=name, w=w, h=h, fmt=fmt, mips=mips, inline=True, data=None, ubulk=os.path.exists(ubulk_p))
    if info["ubulk"]:
        # ponytail: assume mip0 sits at ubulk offset 0 (largest mip first); verify by eye.
        ub = open(ubulk_p, "rb").read()
        if len(ub) >= sizes[0]:
            info["data"] = ub[:sizes[0]]
        else:
            info["inline"] = False
        return info
    # The mip chain ends where the export's trailing copy of the platform header starts
    # (SizeX, SizeY, PackedData again, then zeros, then the package tag). Verified on T_UI_Header_L / T_UI_X.
    trail = d.rfind(d[p - 16:p - 4], 0, len(d) - 4)
    if trail < 0 or trail - total < 0:
        raise ValueError("cannot locate mip data (trailing header not found)")
    start = trail - total
    info["data"] = d[start:start + sizes[0]]
    return info


def to_png(info, path):
    fmt, w, h, data = info["fmt"], info["w"], info["h"], info["data"]
    if fmt == "PF_B8G8R8A8":
        img = Image.frombytes("RGBA", (w, h), data, "raw", "BGRA")
    elif fmt == "PF_FloatRGBA":
        img = Image.frombytes("RGBA", (w, h), float_rgba_to_rgba8(data, w, h))
    elif fmt in ("PF_DXT1", "PF_DXT5"):
        fourcc = b"DXT1" if fmt == "PF_DXT1" else b"DXT5"
        img = Image.open(io.BytesIO(dds_wrap(fourcc, w, h, data))).convert("RGBA")
    else:
        raise ValueError("no decoder for " + fmt)
    img.save(path)


def main(argv):
    all_mode = "--all" in argv
    names = [a for a in argv if not a.startswith("--")]
    os.makedirs(OUT_DIR, exist_ok=True)
    if all_mode:
        names = sorted(os.path.basename(p)[:-5] for p in glob.glob(os.path.join(UI_DIR, "T_UI_*.uexp")))
    elif not names:
        names = HEADERS
    # sizes.txt always covers every T_UI_* texture (cheap: no decode)
    size_names = sorted(os.path.basename(p)[:-5] for p in glob.glob(os.path.join(UI_DIR, "T_UI_*.uexp")))
    lines = []
    for n in size_names:
        try:
            info = parse(n)
        except Exception as e:  # keep going, report
            lines.append("%s\tERROR %r" % (n, e))
            print("[skip] %s: %r" % (n, e))
            continue
        if not info["inline"]:
            lines.append("%s\t%dx%d\t%s\tmips=%d\tUBULK (not extracted)" % (n, info["w"], info["h"], info["fmt"], info["mips"]))
            print("[ubulk] %s needs FModel export" % n)
            continue
        lines.append("%s\t%dx%d\t%s\tmips=%d" % (n, info["w"], info["h"], info["fmt"], info["mips"]))
        if n in names:
            out = os.path.join(OUT_DIR, n + ".png")
            to_png(info, out)
            print("[png] %s %dx%d %s -> %s" % (n, info["w"], info["h"], info["fmt"], out))
    with open(os.path.join(OUT_DIR, "sizes.txt"), "w", encoding="utf-8") as f:
        f.write("\n".join(lines) + "\n")
    print("sizes -> " + os.path.join(OUT_DIR, "sizes.txt"))


if __name__ == "__main__":
    main(sys.argv[1:])
