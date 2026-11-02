"""Dev-time: extract the 13 bundled item icons from the game's cooked assets to art\\items\\ITEM_<Name>.png (64x64).

Each ITEM_<Name>.uasset names its icon texture (T_*); the texture is read with extract_textures.parse().
make_assets.py imports the PNGs as /Game/MiningUnitUI/Art/T_Item_<Name> (our shipped copies, see docs/ASSETS.md).
Existing PNGs are kept (delete one to re-extract it).

Usage (python 3, Pillow):  cd scripts ; python -I extract_item_icons.py
Output per item: "ITEM_<Name> -> T_<icon> -> WxH FMT". Errors: "NO ICON" / "decode error" lines (return to planner).
"""
import os
import re
import sys
import tempfile

from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import extract_textures as xt  # noqa: E402

MODS_ROOT = os.environ.get("MS_MODS_ROOT", r"F:\MSDemoMods")  # repo root, holds legacy\ (game cook)
ITEMS_DIR = os.path.join(MODS_ROOT, "legacy", "MainSequence", "Content", "Data", "Items")
ICONS_DIR = os.path.join(MODS_ROOT, "legacy", "MainSequence", "Content", "Materials", "Icons", "Items")
RENDER_DIR = os.path.join(MODS_ROOT, "legacy", "MainSequence", "Content", "Editor", "IconRender")  # fallback: ores only exist here
OUT_DIR = os.path.join(os.path.dirname(HERE), "art", "items")
IDS = ["ITEM_CobaltOre", "ITEM_Graphite", "ITEM_IridiumOre", "ITEM_NickelOre", "ITEM_Silica",
       "ITEM_TitaniumOre", "ITEM_WaterIce", "ITEM_EnrichedCobaltOre", "ITEM_EnrichedNickelOre",
       "ITEM_EnrichedTitaniumOre", "ITEM_EnrichedFuel", "ITEM_HydrogenFuel", "ITEM_WarpFuel"]
NAME_RE = re.compile(rb"T_[A-Za-z0-9_]+")


def icon_name(item_id):
    """Return (T_name, source dir) for the item's icon texture, or (None, None)."""
    data = open(os.path.join(ITEMS_DIR, item_id + ".uasset"), "rb").read()
    for m in NAME_RE.findall(data):
        n = m.decode("ascii")
        for d in (ICONS_DIR, RENDER_DIR):
            if os.path.exists(os.path.join(d, n + ".uasset")):
                return n, d
    return None, None


def main():
    os.makedirs(OUT_DIR, exist_ok=True)
    tmp = os.path.join(tempfile.mkdtemp(), "icon.png")
    failed = []
    for item_id in IDS:
        out = os.path.join(OUT_DIR, item_id + ".png")
        name, src_dir = icon_name(item_id)
        if name is None:
            print("NO ICON %s" % item_id)
            failed.append(item_id)
            continue
        xt.UI_DIR = src_dir
        origin = "Materials\\Icons\\Items" if src_dir == ICONS_DIR else "Editor\\IconRender"
        try:
            info = xt.parse(name)
            if info["data"] is None:
                raise ValueError("no mip data (ubulk too short)")
            xt.to_png(info, tmp)
            img = Image.open(tmp).convert("RGBA").resize((64, 64), Image.LANCZOS)
        except Exception as e:  # report and keep going
            print("decode error %s (%s): %r" % (item_id, name, e))
            failed.append(item_id)
            continue
        if not os.path.exists(out):
            img.save(out)
        print("%s -> %s -> %dx%d %s (%s)" % (item_id, name, info["w"], info["h"], info["fmt"], origin))
    if failed:
        print("FAILED: " + " ".join(failed))
        sys.exit(1)


if __name__ == "__main__":
    main()
