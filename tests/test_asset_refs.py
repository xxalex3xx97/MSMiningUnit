"""Asset reference checks for the MiningUnitUI package (plain CPython, no UE needed).

  python -I tests\\test_asset_refs.py                 run all test_* (prints "ok name" / "FAIL name")
  python -I tests\\test_asset_refs.py --cooked DIR    scan cooked .uasset/.uexp under DIR for /Game/ refs outside /Game/MiningUnitUI/

Rules: our paste and our cooked files reference only /Game/MiningUnitUI/*; every texture in the allow-list has a
source PNG and is named in docs\\ASSETS.md; the Lua may only touch our package or the one runtime-only game widget.
"""
import importlib.util
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
UE = os.path.join(ROOT, "ue", "MiningUnitUI")
OWN = "/Game/MiningUnitUI/"
LUA_ALLOWED = (OWN, "/Game/Blueprints/Widgets/Display/WBP_FleetShipEntry")
GAME_RE = re.compile(r"/Game/[A-Za-z0-9_./-]+")
GAME_RE_B = re.compile(rb"/Game/[A-Za-z0-9_./-]+")


def load_gp():
    spec = importlib.util.spec_from_file_location("gen_paste", os.path.join(UE, "scripts", "gen_paste.py"))
    gp = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(gp)
    return gp


def test_paste_own_only():
    gp = load_gp()
    for art in (True, False):
        gp.ART = art
        out = []
        gp.emit(gp.build_tree(), out)
        bad = sorted({p for p in GAME_RE.findall("\n".join(out)) if not p.startswith(OWN)})
        assert not bad, "game refs in paste (art=%s): %s" % (art, bad[:5])


def test_every_art_has_source_and_doc():
    gp = load_gp()
    with open(os.path.join(ROOT, "docs", "ASSETS.md"), encoding="utf-8") as f:
        doc = f.read()
    for path in sorted(gp.ART_PATHS | gp.ITEM_PATHS):
        name = path.split("/")[-1].split(".")[0]
        if name.startswith("T_Item_"):
            src = os.path.join(UE, "art", "items", "ITEM_" + name[len("T_Item_"):] + ".png")
        else:
            src = os.path.join(UE, "art", "textures", name + ".png")
        assert os.path.exists(src), "no source PNG: " + src
        assert name in doc, "not in docs/ASSETS.md: " + name


def test_lua_game_paths():
    with open(os.path.join(ROOT, "mods", "MiningUnitUI", "Scripts", "wbp_loader.lua"), encoding="utf-8") as f:
        txt = f.read()
    bad = sorted({p for p in re.findall(r'"(/Game/[^"]+)"', txt) if not p.startswith(LUA_ALLOWED)})
    assert not bad, "game paths in Lua: %s" % bad


def scan_cooked(d):
    """Return 'file: /Game/... ' hits for cooked files outside our package. NULs stripped so UTF-16 names match too."""
    hits = []
    for dp, _, fs in os.walk(d):
        for f in fs:
            if not f.lower().endswith((".uasset", ".uexp")):
                continue
            data = open(os.path.join(dp, f), "rb").read().replace(b"\x00", b"")
            for m in sorted({m.decode("ascii") for m in GAME_RE_B.findall(data)}):
                if not (m == OWN[:-1] or m.startswith(OWN)):
                    hits.append("%s: %s" % (f, m))
    return hits


if __name__ == "__main__":
    if len(sys.argv) == 3 and sys.argv[1] == "--cooked":
        hits = scan_cooked(sys.argv[2])
        for h in hits:
            print(h)
        print("cooked scan: %d hit(s)" % len(hits))
        sys.exit(1 if hits else 0)
    rc = 0
    for name, fn in sorted(globals().items()):
        if name.startswith("test_") and callable(fn):
            try:
                fn()
                print("ok", name)
            except AssertionError as e:
                rc = 1
                print("FAIL", name, e)
    sys.exit(rc)
